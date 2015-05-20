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
 *         Timothy C Pepper <timothy.c.pepper@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

#include <swupd.h>
#include "progress.h"

/*algorithm should be something like:
 *  figure out how much space is needed
 *  make it available
 *  if repair updated
 *  	write latest repairOS binary
 *	sync
 *	write associated config file
 *	sync
 *  if kernel updated
 *	write latest kernel binary
 *	sync
 *	write associated config file
 *	sync
 *  verify all hashes
 */

#ifdef SWUPD_WITH_ESP
int mount_esp_fs(void)
{
	int ret;

	/* create an esp mountpoint */
	ret = mkdir(ESP_MOUNT, S_IRWXU);
	if ((ret != 0) && (errno != EEXIST)) {
		LOG_ERROR(NULL, "Could not make ESP_MOUNT", class_file_misc, "\\*ret=\"%d\",strerror=\"%s\"*\\",
				ret, strerror(errno));
		return ret;
	}

	/* mount esp with vfat file and directory modes which match
	 * official build ESP modes, otherwise we get hash mismatches */
	ret = mount(ESP_PARTITION,
			ESP_MOUNT, "vfat", MS_SILENT, "fmask=0133,dmask=0022");
	if (ret != 0)
		LOG_ERROR(NULL, "Could not mount esp", class_esp_mnt_pt, "\\*ret=\"%d\",strerror=\"%s\"*\\",
				ret, strerror(errno));

	return ret;
}

void unmount_esp_fs(void)
{
	/* insure ESP is unmounted */
	while (umount(ESP_MOUNT) == 0)
		LOG_DEBUG(NULL, "umounted once", class_esp_mnt_pt, "%s", ESP_MOUNT);

}

int remove_esp_OS_boot_config_files(const char *version)
{
	int ret;
	char *cmd = NULL;

	if (asprintf(&cmd, "rm -f %s/loader/entries_ia32/sp32-%s.* %s/loader/entries_x64/sp64-%s.* %s/sp/vmlinux-%s.*",
			ESP_MOUNT, version, ESP_MOUNT, version, ESP_MOUNT, version) < 0)
		return -ENOMEM;

	ret = system(cmd);
	if (ret < 0)
		LOG_ERROR(NULL, "Unable to delete ESP OS config files for", class_disk_sp,
				"\\*version=\"%s\"*\\", version);

	free(cmd);

	sync();

	return ret;
}

int remove_esp_repairOS_boot_config_files(const char *version)
{
	int ret;
	char *cmd = NULL;

	if (asprintf(&cmd, "rm -f %s/MANIFEST.%s %s/loader/entries_ia32/repair32-%s.* %s/loader/entries_x64/repair64-%s.* %s/sp/repair-%s.*",
			ESP_MOUNT, version, ESP_MOUNT, version, ESP_MOUNT,
			version, ESP_MOUNT, version) < 0)
		abort();

	ret = system(cmd);
	if (ret < 0)
		LOG_ERROR(NULL, "Unable to delete ESP repair OS config files for", class_disk_sp,
				"\\*version=\"%s\"*\\", version);

	free(cmd);

	sync();

	return ret;
}

uint64_t get_available_esp_space(void)
{
	uint64_t available_esp;
	struct statvfs sfs;

	if (statvfs(ESP_MOUNT, &sfs) != -1) {
		available_esp = (uint64_t)sfs.f_bsize * sfs.f_bfree;
	} else {
		LOG_ERROR(NULL, "Unable to statvfs", class_esp_mnt_pt, "\\*esp_mount=\"%s\"*\\", ESP_MOUNT);
		return false;
	}

	LOG_INFO(NULL, "ESP disk available size", class_disk_sp, "%lld", available_esp);

	return available_esp;
}

static int repairfilter(const struct dirent *entry)
{
	return !strncmp(entry->d_name, "MANIFEST.", 9);
}

static int repairsort(const struct dirent **first, const struct dirent **second)
{
	long n1, n2;
	char *ver1 = strchr((*first)->d_name, '.') + 1;
	char *ver2 = strchr((*second)->d_name, '.') + 1;

	if (!ver1 || !ver2)
		abort();

	n1 = strtol(ver1, NULL, 10);
	n2 = strtol(ver2, NULL, 10);

	return (n1 - n2);
}

char *get_esp_repairOS_list(int *count)
{
	struct dirent **namelist = NULL;
	int num;
	char *list = NULL;
	int len;
	char *version;

	*count = scandir(ESP_MOUNT, &namelist, repairfilter, repairsort);
	if (*count < 0) {
		LOG_ERROR(NULL, "Could not scan mount point dir", class_esp_mnt_pt, "\\*strerror=\"%s\"*\\",
				strerror(errno));
		return NULL;
	}

	num = 0;
	while (num < *count) {
		version = strchr(namelist[num]->d_name, '.') + 1;
		if (!version)
			break;

		len = (list == NULL) ? 0 : strlen(list);
		list = realloc(list, len + strlen(version) + 2);
		if (!list)
			abort();
		sprintf(list + len, ":%s", version);

		free(namelist[num]);
		num++;
	}

	free(namelist);

	return list;
}


bool esp_updates_available(struct list *updates, int target_version)
{
	struct file *file;
	struct list *iter;
	bool have_esp_updates = false;

	iter = list_head(updates);
	while (iter) {

		file = iter->data;
		iter = iter->next;

		if (file->do_not_update || file->is_deleted || file->is_link)
			continue;

		if (strncmp(file->filename, "/system/vendor/intel/esp", 24) != 0) {
			have_esp_updates = true;
			break;
		}
	}

	return have_esp_updates;
}

/* cp any non-deleted regular file from /system/vendor/intel/esp/
 * which is in the update list to the esp */
int copy_files_to_esp(int target_version)
{
	int ret;
	char *tarcommand;

	progress_step(PROGRESS_MSG_UPDATE_ESP);

	if (asprintf(&tarcommand, "tar -C %s/%d/system/vendor/intel/ -cf - esp 2> /dev/null | "
				  "tar -C %s/ -xf - --no-same-permissions --no-same-owner --transform=\"s/esp//x\" 2> /dev/null",
				  MOUNT_POINT, target_version, ESP_MOUNT) < 0) {
		ret = -ENOMEM;
		goto on_exit;
	}

	ret = system(tarcommand);
	free(tarcommand);

	progress_step(PROGRESS_MSG_SYNCING);

	sync();

	/* validate the hash of the files as read from the fs */
#warning need to validate files after write

	LOG_INFO(NULL, "Updated ESP", class_esp_mnt_pt, "");

on_exit:
	return ret ? -1 : 0;
}

#else /* SWUPD_WITHOUT_ESP */
int mount_esp_fs(void)
{
	return 0;
}

void unmount_esp_fs(void)
{
}

int remove_esp_OS_boot_config_files(const char UNUSED_PARAM *version)
{
	return 0;
}

int remove_esp_repairOS_boot_config_files(const char UNUSED_PARAM *version)
{
	return 0;
}

uint64_t get_available_esp_space(void)
{
	return ULLONG_MAX;
}

char *get_esp_repairOS_list(int UNUSED_PARAM *count)
{
	return NULL;
}

int copy_files_to_esp(int UNUSED_PARAM target_version)
{
	return 0;
}

bool esp_updates_available(struct list UNUSED_PARAM *updates, int UNUSED_PARAM target_version)
{
	return false;
}
#endif /* SWUPD_*_ESP */
