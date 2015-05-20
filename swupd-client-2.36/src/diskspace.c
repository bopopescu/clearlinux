/*
 *   Software Updater - client side
 *
 *      Copyright © 2014-2015 Intel Corporation.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2 or later of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Authors:
 *         Regis Merlino <regis.merlino@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

#include <swupd.h>

#if 0
static int clean_swupd_state_data(int UNUSED_PARAM current_version, int UNUSED_PARAM latest_version)
{
#warning should handle other data files and consider current and latest version
	LOG_INFO(NULL, "Cleaning swupd state files", class_disk_sp, "");

	if (rm_staging_dir_contents("delta"))
		return -1;

	if (rm_staging_dir_contents("download"))
		return -1;

	if (rm_staging_dir_contents("staged"))
		return -1;

	return 0;

}
#endif

#ifdef SWUPD_WITH_BTRFS
uint64_t get_available_btrfs_space(void)
{
	uint64_t available_btrfs;
	struct statvfs sfs;

#warning TODO need to find way to trigger synchronous btrfs garbage collection
	if (statvfs(MOUNT_POINT, &sfs) != -1) {
		available_btrfs = (uint64_t)sfs.f_bsize * sfs.f_bfree;
	} else {
		LOG_ERROR(NULL, "Unable to statvfs", class_btrfs_mnt_pt, "\\*mount_point=\"%s\"*\\", MOUNT_POINT);
		return false;
	}

	LOG_INFO(NULL, "BTRFS disk available size", class_disk_sp, "%lld", available_btrfs);

	return available_btrfs;
}

static int remove_btrfs_snapshot(const char *version)
{
	int ret;
	char *cmd = NULL;

	LOG_INFO(NULL, "Removing snapshot OS files", class_disk_sp, "");

	if (asprintf(&cmd, "%s subvolume delete %s/%s", BTRFS_CMD, MOUNT_POINT, version) < 0)
		abort();

	ret = system(cmd);
	if (ret != 0)
		LOG_ERROR(NULL, "Unable to delete subvolume", class_disk_sp, "\\*version=\"%s\"*\\", version);

	free(cmd);

	sync();

	return ret;
}

int snapshotsort(const struct dirent **first, const struct dirent **second)
{
	long n1, n2;

	n1 = strtol((*first)->d_name, NULL, 10);
	n2 = strtol((*second)->d_name, NULL, 10);

	if ((n1 == LONG_MIN) || (n1 == LONG_MAX) ||
					(n2 == LONG_MIN) || (n2 == LONG_MAX)) {
		return strcmp((*first)->d_name, (*second)->d_name);
	} else {
		return n1 - n2;
	}
}

char *get_snapshot_list(int current, int latest, dirent_cmp_fn_t dirent_cmp_fn, int *num_elements)
{
	int found;
	struct dirent **namelist = NULL;
	int num;
	char *list = NULL;
	int len;
	int count;
	int list_counter = 0;

	/* get & sort snapshots */
	count = scandir(MOUNT_POINT, &namelist, NULL, dirent_cmp_fn);
	if (count < 0) {
		LOG_ERROR(NULL, "Could not scan mount point dir", class_btrfs_mnt_pt, "\\*strerror=\"%s\"*\\", strerror(errno));
		return NULL;
	}

	num = 0;
	while (num < count) {
		found = strtol(namelist[num]->d_name, NULL, 10);
		if ((found > 0) && (found != current) && (found != latest)) {
			len = (list == NULL) ? 0 : strlen(list);
			list = realloc(list, len + strlen(namelist[num]->d_name) + 2);
			if (!list)
				abort();
			sprintf(list + len, ":%s", namelist[num]->d_name);
			list_counter++;
		}
		free(namelist[num]);
		num++;
	}

	free(namelist);

	*num_elements = list_counter;
	return list;
}

int remove_snapshot(const char *version)
{
	int ret;
	uint64_t before, after;
	int attempts = 2;

	LOG_INFO(NULL, "Removing snapshot files", class_disk_sp, "%s", version);

#warning TODO remove when repair OS updates are available
	/* Right now repairOS and OS can change in the same build.
	 * That should be disallowed in the future.
	 * Until then, remove the repair OS config files
	*/
	ret = remove_esp_repairOS_boot_config_files(version);
	if (ret != 0)
		return ret;

	ret = remove_esp_OS_boot_config_files(version);
	if (ret != 0)
		return ret;

	before = get_available_btrfs_space();

	ret = remove_btrfs_snapshot(version);
	if (ret != 0)
		return ret;

	LOG_INFO(NULL, "Removing snapshot OS tmp files", class_disk_sp, "");
	ret = rm_staging_dir_contents(version);
	if (ret != 0)
		return ret;

	/* Give time to btrfs to update its disk space numbers */
	after = get_available_btrfs_space();
	while (after <= before) {
		LOG_ERROR(NULL, "btrfs free space did not change", class_disk_sp, "");
		sleep(BTRFS_SLEEP_TIME);

		attempts--;
		if (!attempts) {
			break;
		}

		after = get_available_btrfs_space();
	}

	return ret;
}

/* Keep at most MAX_OS_TO_KEEP instances of the OS
 * returns -1 = error, 0 = success
 */
static int clean_old_OS(char *list, int num_elements)
{
	int ret = 0;
	char *token;
	int force_remove_cnt;
	char *savelist;

	LOG_DEBUG(NULL, "Cleaning old OS instances", class_disk_sp, "");

	token = strtok_r(list + 1, ":", &savelist);

	/* remove all excess snapshots above MAX_OS_TO_KEEP count,
	 * oldest first based on snapshotsort() in get_snapshot_list() */
	force_remove_cnt = num_elements - MAX_OS_TO_KEEP;
	while ((force_remove_cnt > 0) && (token != NULL)) {
		LOG_DEBUG(NULL, "Force removing extra snapshot", class_disk_sp, "%s", token);
		ret = remove_snapshot(token);

		if (ret != 0) {
			LOG_ERROR(NULL, "Unable to remove snapshot", class_disk_sp, "\\*snapshot=\"%s\"*\\", token);
			return ret;
		}

		force_remove_cnt--;
		token = strtok_r(NULL, ":", &savelist);
	}
	return 0;
}

/* remove OS instances, oldest first based on snapshotsort() in get_snapshot_list(),
 * checking after each removal if sufficient space was freed
 * returns -1 = error, 0 = still not enough space, 1 = enough space
 */
static int remove_snapshot_list(char *list, uint64_t target_esp, uint64_t target_btrfs)
{
	int ret = 0;
	char *token;
	char *savelist;

	token = strtok_r(list + 1, ":", &savelist);

	while (token != NULL) {
		LOG_DEBUG(NULL, "Removing snapshot to free space", class_disk_sp, "%s", token);
		ret = remove_snapshot(token);

		if (ret != 0) {
			return ret;
		}

		if (check_available_esp_space(target_esp)
			return 1;
		}
		if (check_available_btrfs_space(target_btrfs)
			return 1;
		}

		token = strtok_r(NULL, ":", &savelist);
	}

	return ret;
}

static bool check_available_btrfs_space(uint64_t target_esp)
{
	uint64_t btrfs_space = get_available_btrfs_space();
	if (btrfs_space < target_btrfs) {
#warning TODO consider reducing to LOG_INFO
		LOG_WARN(NULL, "Insufficient btrfs space", class_disk_sp,
			"\\*btrfs_space=\"%llu\",target_btrfs=\"%llu\"*\\",
			(unsigned long long) btrfs_space, (unsigned long long) target_btrfs);
		return false;
	}
	return true;
}
#endif /* SWUPD_WITH_BTRFS */

#ifdef SWUPD_WITH_ESP
static bool check_available_esp_space(uint64_t target_esp)
{
	uint64_t esp_space = get_available_esp_space();
	if (esp_space < target_esp) {
#warning TODO consider reducing to LOG_INFO
		LOG_WARN(NULL, "Insufficient ESP space", class_disk_sp,
			"\\*esp_space=\"%llu\",target_esp=\"%llu\"*\\",
			(unsigned long long) esp_space, (unsigned long long) target_esp);
		return false;
	}
	return true;
}
#endif /* SWUPD_WITH_ESP */

#ifdef SWUPD_WITH_REPAIR
/* Keep at most MAX_REPAIR_OS_TO_KEEP
 * returns -1 = error, 0 = success
 */
static int clean_old_repairOS(void)
{
	int ret = 0;
	char *token;
	char *list;
	int max_count;
	char *savelist;
	LOG_DEBUG(NULL, "Cleaning old repairOS instances", class_disk_sp, "");

	list = get_esp_repairOS_list(&max_count);

	if (!list)
		return -1;

	max_count -= MAX_REPAIR_OS_TO_KEEP;
	if (max_count <= 0) {
		free(list);
		return 0;
	}

	LOG_DEBUG(NULL, "Removing repairOS list", class_disk_sp, "%s", list);

	token = strtok_r(list + 1, ":", &savelist);
	while (token != NULL && max_count) {
		LOG_INFO(NULL, "Removing repair OS files", class_disk_sp, "%s", token);

		ret = remove_esp_repairOS_boot_config_files(token);

		if (ret < 0) {
			free(list);
			return ret;
		}

		token = strtok_r(NULL, ":", &savelist);
		max_count--;
	}

	free(list);
	return ret;
}
#endif

#ifdef SWUPD_ANDROID
/* The logic tries to remove as few things as possible to reach the targets
 * on both btrfs and esp partitions.
 */
static int free_disk_space_android(int current_version, int latest_version,
				uint64_t target_btrfs, struct manifest *manifest)
{
	int ret;
	int snapshot_count = 0;
	char *list = NULL;
	struct manifest *sub;
	uint64_t target_esp = ESP_OS_MIN_FREE_SIZE;

	if (manifest) {
		/* Try to get actual esp size from Manifest.esp
		 * and take its value if greater than ESP_OS_MIN_FREE_SIZE
		 */
		list = list_head(manifest->submanifests);
		while (list) {
			sub = list->data;
			list = list->next;

			if (!strcmp(sub->component, "esp") &&
					(sub->contentsize > ESP_OS_MIN_FREE_SIZE)) {
				target_esp = sub->contentsize;
				break;
			}
		}
		target_esp += target_esp / FREE_MARGIN; /* add a safety margin. */
		LOG_INFO(NULL, "Requested ESP disk space", class_disk_sp, "esp(%llu)",
			(unsigned long long) target_esp);
		log_stdout("Requested ESP disk space %llu\n", (unsigned long long) target_esp);
	}

	ret = mount_esp_fs();
	if (ret < 0)
		return ret;

	/* always do some basic housekeeping */
	ret = clean_swupd_state_data(current_version, latest_version);
	if (ret != 0) {
		goto out;
	}

	ret = clean_old_repairOS();
	if (ret != 0) {
		goto out;
	}

	list = get_snapshot_list(current_version, latest_version, snapshotsort, &snapshot_count);
	if (list) {
		ret = clean_old_OS(list, snapshot_count);
		if (ret != 0) {
			free(list);
			goto out;
		}
	}

	/* 2nd only start bulk freeing if there's not already enough space,
	 *	removing OS versions (btrfs snapshot and associated ESP files)
	 *	iteratively until we reach btrfs & esp required sizes */
	if (check_available_esp_space(target_esp) &&
	    check_avaialble_btrfs_space(target_btrfs)) {
		log_stdout("Sufficient disk space exists, continuing with update.\n");
		ret = 0;
		free(list);
		goto umountesp;
	}
	free(list);
	list = get_snapshot_list(current_version, latest_version, snapshotsort, &snapshot_count);
	if (!list) {
		ret = -1;
		goto out;
	}
	log_stdout("Insufficient disk space, attempting to free some space...\n");
	ret = remove_snapshot_list(list, target_esp, target_btrfs);
	free(list);
	if (ret == 1){ /* sufficient space freed */
		ret = 0;
	} else { /* couldn't achieve target space */
		ret = -1;
	}

out:
	if (ret == 0) {
		log_stdout("Freed enough disk space, continuing with update.\n");
	} else {
		log_stdout("Unable to free enough disk space, aborting update!\n");
		LOG_ERROR(NULL, "Insufficient disk space", class_disk_sp, "");
	}

umountesp:
	unmount_esp_fs();

	return ret;
}
#else
static int free_disk_space_linux(int UNUSED_PARAM current_version, int UNUSED_PARAM latest_version, uint64_t target_size)
{
	int ret = 0;
	struct statvfs sfs;
	uint64_t available_space;

#warning hack to enable installation
#if 0
	/* always do some basic housekeeping */
	ret = clean_swupd_state_data(current_version, latest_version);
	if (ret != 0) {
		return ret;
	}
	/* TODO:
	 * We need to allow an external entity to pre-populate packs and pack
	 * content (eg: hash files) into staged.  So we can't simply
	 * unconditionally delete things in there.  Additionally there's a
	 * variable path_prefix for the installation directory, which may
	 * be a mount point different from the rootfs or MOUNT_POINT.
	 * 1) separate the checking of:
	 *   a) path_prefix free space vs manifest content size
	 *   b) manifest & pack size guess vs path_prefix/var/lib/swupd free space
	 * 2) add freeing of old manifests and (truncated) pack files
	 * 3) tmpwatch path_prefix/var/lib/swupd/staged
	 * 4) explicit removal of unneeded files in path_prefix/var/lib/swupd/staged
	 */
#endif
	if (statvfs(path_prefix, &sfs) != -1) {
		available_space = (uint64_t)sfs.f_bsize * sfs.f_bfree;
	} else {
		LOG_ERROR(NULL, "Unable to statvfs", class_osvol, "\\*rootfs\"%s\"*\\", path_prefix);
		return -1;
	}

	if (available_space >= target_size) {
		log_stdout("Have enough disk space, continuing with update.\n");
	} else {
		log_stdout("Unable to free enough disk space, aborting update!\n");
		LOG_ERROR(NULL, "Insufficient disk space", class_disk_sp, "\\*available\"%lld\"", available_space);
		ret = -1;
	}

	LOG_INFO(NULL, "OS disk available size", class_disk_sp, "%lld", available_space);
	return ret;
}
#endif /* SWUPD_ANDROID */

bool free_disk_space_for_manifest(int current_version, int latest_version)
{
	uint64_t target_size = STATE_DIR_MIN_FREE_SIZE + MANIFEST_REQUIRED_SIZE;

	LOG_INFO(NULL, "Requested disk space for manifest", class_disk_sp, "statedir(%llu)", (unsigned long long) target_size);
	log_stdout("Requested disk space for manifest %llu\n", (unsigned long long) target_size);
#ifdef SWUPD_ANDROID
	return free_disk_space_android(current_version, latest_version, target_size, NULL);
#else
	return free_disk_space_linux(current_version, latest_version, target_size);
#endif
}


int free_disk_space_generic(int current_version, int latest_version, struct manifest *manifest)
{
	int ret;
	uint64_t target_disk_space = manifest->contentsize;

	/* Required disk space:
	 * 2x == 1x pack download + 1x opened up pack, or
	 *       1x opened up pack + 1x staging (either subvolume or dotfiles)
	 * (worst case install from zero pack and no fs sharing)
	 */
	target_disk_space *= 2;
	target_disk_space += target_disk_space / FREE_MARGIN; /* add margin for links, etc. */
	LOG_INFO(NULL, "Requested OS disk space", class_disk_sp, "osvol(%llu)",
		(unsigned long long) target_disk_space);
	log_stdout("Requested OS disk space %llu\n", (unsigned long long) target_disk_space);
#ifdef SWUPD_ANDROID
	ret = free_disk_space_android(current_version, latest_version, target_disk_space, manifest);
#else
	ret = free_disk_space_linux(current_version, latest_version, target_disk_space);
#endif
	return ret;
}
