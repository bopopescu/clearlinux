/*
 *   Software Updater - client side
 *
 *      Copyright © 2012-2015 Intel Corporation.
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
 *         Arjan van de Ven <arjan@linux.intel.com>
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <swupd.h>

#ifdef SWUPD_WITH_BTRFS
static int create_staging_subvol_from(const char *version)
{
	int ret = 0;
	char *source_dir;
	char *btrfs_cmd;

	if (asprintf(&btrfs_cmd, "%s subvolume delete %s",
			BTRFS_CMD, STAGING_SUBVOL) < 0)
		abort();

	ret = system(btrfs_cmd);
	free(btrfs_cmd);
	if (ret != 0) {
		LOG_ERROR(NULL, "btrfs staging snapshot deletion failed", class_btrfs_mnt_pt,
				"\\*ret=\"%d\"*\\", ret);
		return -1;
	}

	if (asprintf(&source_dir, "%s/%s", MOUNT_POINT, version) < 0)
		abort();

	if (asprintf(&btrfs_cmd, "%s subvolume snapshot %s %s",
			BTRFS_CMD, source_dir, STAGING_SUBVOL) < 0)
		abort();

	ret = system(btrfs_cmd);
	free(btrfs_cmd);
	if (ret != 0) {
		LOG_ERROR(NULL, "btrfs staging snapshot creation failed", class_btrfs_mnt_pt,
				"\\*source=\"%s\",ret=\"%d\"*\\", source_dir, ret);
		ret = -1;
	}

	free(source_dir);

	sleep(BTRFS_SLEEP_TIME);

	return ret;
}

static int mount_esp_and_remove_snapshot(const char *version)
{
	int ret = 0;
#ifdef SWUPD_WITH_ESP

	ret = mount_esp_fs();
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount esp", class_esp_mnt_pt, "%d (%s)", ret, strerror(errno));
		return ret;
	}

	ret = remove_snapshot(version);
	unmount_esp_fs();
#endif
	return ret;
}

static int descending_snapshotsort(const struct dirent **first, const struct dirent **second)
{
	return snapshotsort(second, first);
}

/* verify snapshots from the newest down to oldest one.
 * If snapshot is bad:
 * - if it's the currently running system -> failure -> need external help
 * - otherwise, delete snapshot and try previous one
 * If snapshot is good:
 * - make a new staging subvol from verified snapshot
 * If no good snapshot found -> failure -> need external help
 * Returns:
 * 0 : staging subvol was restored
 * <0 : didn't work...need external help
 */
static int restore_staging_subvol(int current_version, int latest_version)
{
	int ret = -1;
	char *token;
	char *list;
	char *savelist;
	bool is_corrupted;
	int list_elements;

	list = get_snapshot_list(0, 0, descending_snapshotsort, &list_elements);
	if (!list)
		return -1;

	/* if current_version is not an official build (e.g xx8 - dev build),
	 * ignore it to prevent stopping the snapshot at current_version */
	if (SWUPD_VERSION_IS_DEVEL(current_version) ||
		SWUPD_VERSION_IS_RESVD(current_version))
		current_version = 0;

	token = strtok_r(list + 1, ":", &savelist);
	while (token != NULL) {
		int version = atoi(token);

		/* if version is not an official build (e.g xx8 - dev build),
		 * ignore it because we can't update from a non-official builds */
		if (SWUPD_VERSION_IS_DEVEL(version) || SWUPD_VERSION_IS_RESVD(version)) {
			goto next;
		}

		if (version < current_version) {
			/* If we ever come to that case, it means that we have
			 * just failed to verify on the currently running OS
			 * snapshot, which implies we are potentially running
			 * corrupted code. In this situation, don't try to
			 * fix.  Need external help. */
#warning TODO restore from versions older than the running version instead of external help required
			ret = -1;
			break;
		}

		/* It is impossible to have a valid snapshot that is
		 * newer than latest_version in the normal update flow.
		 * However, it is possible to have one if it was flashed
		 * outside of the update process. If we are running such
		 * a version, we assume it is valid and will check it */
		if ((version > latest_version) && (version != current_version)) {
			ret = -1;
			goto check_result;
		}

		if (asprintf(&path_prefix, "%s/%s", MOUNT_POINT, token) < 0)
			abort();
		ret = do_verify("os-core", version, &is_corrupted);
		if (ret != 0) {
			/* transient error, abort */
			break;
		}
		if (is_corrupted) {
			ret = -1;
			goto check_result;
		}

#ifdef SWUPD_WITH_ESP
		ret = mount_esp_fs();
		if (ret != 0) {
			/* transient error, abort */
			LOG_ERROR(NULL, "Could not mount esp", class_esp_mnt_pt, "%d (%s)", ret, strerror(errno));
			break;
		}

		if (asprintf(&path_prefix, "%s", ESP_MOUNT) < 0);
			abort();
		ret = do_verify("esp", version, &is_corrupted);
		unmount_esp_fs();
		if (ret != 0) {
			/* transient error, abort */
			break;
		}
		if (is_corrupted) {
			ret = -1;
			goto check_result;
		}
#endif

		/* recreate staging subvol from this verified good snapshot */
		ret = create_staging_subvol_from(token);

		/* also need to remember this is now our latest version */
		if (ret == 0 && version != latest_version)
			ret = update_device_latest_version(version);

		/* if it worked, we're done */
		if (ret == 0)
			break;

check_result:
		if ((ret != 0) && (version != current_version)) {
			/* this version is bad, or we couldn't use it to
			 * recreate staging subvol. Delete it and try next one,
			 * unless this is the currently running snapshot.
			 * If deletion fails -> failure -> need external help */

			ret = mount_esp_and_remove_snapshot(token);
			if (ret != 0) {
				break;
			}
		}

next:
		token = strtok_r(NULL, ":", &savelist);
		ret = -1;
	}

	free(list);

	if (ret && current_version == 0) {
		/* we couldn't restore, but we are not running an official
		 * build. */
		LOG_ERROR(NULL, "Couldn't restore, unknown version!", class_version, "");
	}

	return ret;
}
#endif /* SWUPD_WITH_BTRFS */

/* Prepare the filesystem for an update
 * return: <0 error, 0 success, 1 success via restore and caller must restart */
int prepare(bool *is_corrupted, int current_version, int latest_version)
{
	int ret = 0;
#ifdef SWUPD_WITH_BTRFS
	if (asprintf(&path_prefix, "%s", STAGING_SUBVOL) < 0);
		abort();
	LOG_INFO(NULL, "Verifying OS volume (pre)", class_osvol_staging, "%s (%d)",
		path_prefix, latest_version);
	ret = do_verify("os-core", latest_version, is_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify staging subvolume (pre)", class_osvol_staging,
				"\\*path_prefix=\"%s\",latest_version=\"%d\",ret=\"%d\"*\\",
			path_prefix, latest_version, ret);
		return ret;
	}
	if (*is_corrupted) {
		LOG_ERROR(NULL, "corrupt staging subvolume (pre)", class_osvol_staging,
				"\\*path_prefix=\"%s\",latest_version=\"%d\"*\\",
			path_prefix, latest_version);

		ret = restore_staging_subvol(current_version, latest_version);
		if (ret == 0) {
			return 1;
		} else {
			return -1;
		}
	}
#elif SWUPD_LINUX_ROOTFS
	ret = do_verify(NULL, latest_version, is_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify OS volume (pre)", class_osvol,
				"\\*path_prefix=\"%s\",current_version=\"%d\",ret=\"%d\"*\\",
			path_prefix, current_version, ret);
		return ret;
	}
	if (*is_corrupted) {
		LOG_ERROR(NULL, "corrupt OS volume (pre)", class_osvol,
				"\\*path_prefix=\"%s\",current_version=\"%d\",ret=\"%d\"*\\",
			path_prefix, current_version, ret);
#warning tolerate for now a nonexpected rootfs state
//		return -1;
		return 0;
	}
#endif
	return ret;
}

/* Do the staging of new files into the filesystem */
#warning need error handling from do_staging up to update_loop
void do_staging(struct file *file)
{
	char *statfile = NULL, *tmp = NULL, *tmp2 = NULL;
	char *dir, *base, *rel_dir;
	char *tarcommand = NULL;
#if SWUPD_LINUX_ROOTFS
	char *original = NULL;
	char *target = NULL;
#endif
	int ret;
	struct stat stat;

	tmp = strdup(file->filename);
	tmp2 = strdup(file->filename);

	dir = dirname(tmp);
	base = basename(tmp2);

	rel_dir = dir;
	if (*dir=='/')
		rel_dir = dir + 1;

	if (asprintf(&original, "%s/staged/%s", STATE_DIR, file->hash) <= 0)
		abort();

#if SWUPD_LINUX_ROOTFS
	if (asprintf(&target, "%s%s/.update.%s", path_prefix, rel_dir, base) <= 0)
		abort();
	ret = swupd_rm(target);
	if (ret == 0)
		LOG_DEBUG(file, "Previous update dotfile found", class_osvol_staging, "%s", target);
	free(target);

	if (asprintf(&statfile, "%s%s", path_prefix, file->filename) <= 0)
		abort();
#endif

#if SWUPD_WITH_BTRFS
	if (asprintf(&statfile, "%s/%s/%s", STAGING_SUBVOL, rel_dir, base) <= 0)
		abort();
#endif

	memset(&stat, 0, sizeof(struct stat));
	ret = lstat(statfile, &stat);
	if (ret == 0) {
		if ((file->is_dir  && !S_ISDIR(stat.st_mode)) ||
		    (file->is_link && !S_ISLNK(stat.st_mode)) ||
		    (file->is_file && !S_ISREG(stat.st_mode))) {
			LOG_INFO(file, "Type changed!", class_osvol_staging, "%s", statfile);
			//file type changed, move old out of the way for new
			ret = swupd_rm(statfile);
			if (ret < 0) {
				LOG_ERROR(file, "Couldn't remove type-changed file!", class_osvol_staging, "%s", statfile);
#warning should bale out here
			}
		}
	}
	free(statfile);

#if SWUPD_LINUX_ROOTFS
	if (file->is_dir || S_ISDIR(stat.st_mode)) {
		/* In the btrfs only scenario there is an implicit
		 * "create_or_update_dir()" via un-tar-ing a directory.tar after
		 * download and the untar happens in the staging subvolume which
		 * then gets promoted to a "real" usable subvolume.  But for
		 * a live rootfs the directory needs copied out of staged
		 * and into the rootfs.  Tar is a way to copy with
		 * attributes and it includes internal logic that does the
		 * right thing to overlay a directory onto something
		 * pre-existing: */
		if (asprintf(&tarcommand, "tar -C %s/staged " TAR_PERM_ATTR_ARGS " -cf - %s 2> /dev/null | "
			"tar -C %s%s " TAR_PERM_ATTR_ARGS " -xf - --transform=\"s/%s/%s/x\" 2> /dev/null",
			STATE_DIR, file->hash, path_prefix, rel_dir, file->hash, base) <= 0)
			abort();
		LOG_DEBUG(file, "directory overwrite", class_osvol_staging, "%s", tarcommand);
		ret = system(tarcommand);
		free(tarcommand);
		if (ret < 0) {
			LOG_ERROR(file, "Failed directory overwrite", class_osvol_staging, "%s", strerror(errno));
		}
	} else { /* (!file->is_dir && !S_ISDIR(stat.st_mode)) */
		// can't hard link(): files with same hash must remain separate copies
		if (asprintf(&tarcommand, "tar -C %s/staged " TAR_PERM_ATTR_ARGS " -cf - %s 2> /dev/null | "
			"tar -C %s%s " TAR_PERM_ATTR_ARGS " -xf - --transform=\"s/%s/.update.%s/x\" 2> /dev/null",
			STATE_DIR, file->hash, path_prefix, rel_dir, file->hash, base) <= 0)
			abort();
		LOG_DEBUG(file, "dotfile install", class_osvol_staging, "%s", tarcommand);
		ret = system(tarcommand);
		free(tarcommand);
		if (ret < 0) {
			LOG_ERROR(file, "Failed tar dotfile install", class_osvol_staging,
				"%s to %s%s/.update.%s: %s", original, path_prefix, rel_dir, base, strerror(errno));
#warning consider propagating error to caller to cease update
		} else {
			struct stat buf;
			int err;

			if (asprintf(&file->dotfile, "%s%s/.update.%s", path_prefix, rel_dir, base) <=0 )
				abort();

			err = lstat(file->dotfile, &buf);
			if (err != 0) {
				LOG_DEBUG(file, "Installed dotfile not present", class_osvol_staging, "%s", file->dotfile);
			}
		}
	}
#endif
#if SWUPD_WITH_BTRFS
#warning user btrfs reflinks
	/* For initial simplicity replace the file.  Ideally this would be
	 * an intelligent btrfs reflink to maximize block level reuse. */
	if (asprintf(&tarcommand, "tar -C %s/staged " TAR_PERM_ATTR_ARGS " -cf - %s 2> /dev/null | "
		"tar -C %s/%s " TAR_PERM_ATTR_ARGS " -xf - --transform=\"s/%s/%s/x\" 2> /dev/null",
		STATE_DIR, file->hash, STAGING_SUBVOL, rel_dir, file->hash, base) <= 0)
		abort();
	ret = system(tarcommand);
	free(tarcommand);
#endif

	free(original);
	free(tmp);
	free(tmp2);
}

#warning maybe name this rename_staged_to_final
/* caller should not call this function for do_not_update marked files */
static int rename_dot_file_to_final(struct file *file)
{
	int ret;
	char *target;
	if(asprintf(&target, "%s%s", path_prefix, file->filename) < 0)
		abort();

	if (!file->dotfile && !file->is_deleted && !file->is_dir) {
		LOG_DEBUG(file, "No dotfile!", class_osvol_staging, "");
		free(target);
		return -1;
	}

	if (file->is_deleted) {
		LOG_DEBUG(file, "Deleting file", class_osvol_staging, "");

		ret = swupd_rm(target);

		/* don't count missing ones as errors...
		 * if somebody already deleted them for us then all is well */
		if ((ret == -ENOENT) || (ret == -ENOTDIR)) {
			ret = 0;
		}
		if (ret < 0) {
			LOG_ERROR(file, "Failed unlink/rmdir", class_osvol_staging, "%s", strerror(errno));
		}
	} else if (file->is_dir) {
		//LOG_INFO(file, "directory already handled", class_osvol_staging, "");
		ret = 0;
	} else {
		struct stat stat;
		ret = lstat(target, &stat);

		/* If the file was previously a directory but no longer, then
		 * we need to move it out of the way.
		 * This should not happen because the server side complains
		 * when creating update content that includes such a state
		 * change.  But...you never know. */

		if ((ret == 0) && (S_ISDIR(stat.st_mode))) {
			char *lostnfound;
			char *base;

			if(asprintf(&lostnfound, "%slost+found", path_prefix) < 0)
				abort();
			ret = mkdir(lostnfound, S_IRWXU);
			if ((ret != 0) && (errno != EEXIST)) {
				LOG_DEBUG(file, "no lost+found", class_osvol_staging, "\\*ret=\"%d\"\\*", ret);
				free(lostnfound);
				free(target);
				return ret;
			}
			free(lostnfound);

			LOG_INFO(file, "unexpected dir overwrite", class_osvol_staging, "");
			base = basename(file->filename);
			if (asprintf(&lostnfound, "%slost+found/%s", path_prefix, base) <= 0)
				assert(0);
			/* this will fail if the directory was not already emptied */
			ret = rename(target, lostnfound);
			if (ret < 0) {
				LOG_ERROR(file, "dir overwrite rename failure", class_osvol_staging,
					"%s to %s: %s", target, lostnfound, strerror(errno));
			}
			free(lostnfound);
		} else {
			ret = rename(file->dotfile, target);
			if (ret < 0) {
				LOG_ERROR(file, "rename failure", class_osvol_staging,
					"%s to %s: %s", file->dotfile, target, strerror(errno));
			}
		}
	}

	free(target);
	return ret;
}

static int rename_all_files_to_final(struct list *updates)
{
	int ret, update_errs = 0, update_good = 0, skip = 0;
	struct list *list;

	list = list_head(updates);
	while (list) {
		struct file *file;
		file = list->data;
		list = list->next;
		if (file->do_not_update) {
			skip += 1;
			continue;
		}

		ret = rename_dot_file_to_final(file);
		if (ret != 0)
			update_errs += 1;
		else
			update_good += 1;
	}

	LOG_DEBUG(NULL, "Final update count", class_osvol_staging,
			"expecting %d: saw %d good, %d bad. do_not_update: %d of %d skipped.",
			update_count, update_good, update_errs, skip, update_skip);

	ret = update_count - update_good - update_errs - (update_skip - skip);
	if (ret != 0) {
		LOG_ERROR(NULL, "Rename anomaly", class_osvol_staging,
			"%d updates lost vs. initial calculation", ret);
	}
	return ret;
}

#ifdef SWUPD_LINUX_ROOTFS
static int finalize_rootfs(struct list *updates, int UNUSED_PARAM target_version)
{
	int ret;

	/* sync */
	LOG_INFO(NULL, "Calling sync (rootfs-finalize-start)", class_sync, "");
	sync();

	LOG_INFO(NULL, "Rootfs update critical section starting", class_osvol, "");
/************************************************** critical section starts **************************************************/
#warning arguably critical section starts in do_staging
	/* rename to apply update */
	ret = rename_all_files_to_final(updates);
	if (ret != 0)
		return ret;
	update_complete = true;


	/* TODO: do we need to optimize directory-permission-only changes (directories
	 *       are now sent as tar's so permissions are handled correctly, even
	 *       if less than efficiently)? */

	LOG_INFO(NULL, "Calling sync (rootfs-finalize-finish)", class_sync, "");
	sync();

/*************************************************** critical section ends ***************************************************/
	LOG_INFO(NULL, "Rootfs update critical section finished", class_osvol, "");

#if 0
we have been requested to be more efficient and not verify post-update
	bool is_osvol_corrupted=true;
	fix = false;

	LOG_INFO(NULL, "Verifying OS volume (final)", class_osvol, "%s (%d)", path_prefix, target_version);
	ret = do_verify(NULL, target_version, &is_osvol_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify OS volume (final)", class_osvol,
				"\\*path_prefix=\"%s\",target_version=\"%d\",ret=\"%d\"*\\",
				path_prefix, target_version, ret);
		return -1;
	}
	if (is_osvol_corrupted) {
		LOG_ERROR(NULL, "corrupt OS volume (final)", class_osvol,
				"\\*path_prefix=\"%s\",target_version=\"%d\"*\\",
				path_prefix, target_version);
		//fatal_update_error(latest_version, target_version);
		return -1;
	}
#endif
	return 0;
}
#else
static int finalize_rootfs(struct list UNUSED_PARAM *updates, int UNUSED_PARAM target_version)
{
	return 0;
}
#endif

#ifdef SWUPD_WITH_BTRFS
static int finalize_btrfs(struct list *updates, int latest_version, int target_version)
{
	char *snap_cmd = NULL;
	bool is_subvol_corrupted;
	char *version_str;
	int ret;

	if (asprintf(&path_prefix, "%s", STAGING_SUBVOL) < 0);
		abort();
	LOG_INFO(NULL, "Verifying staging subvolume (post)", class_osvol_staging, "%s (%d)",
		path_prefix, target_version);
	ret = do_verify("os-core", target_version, &is_subvol_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify staging subvolume (post)", class_osvol_staging,
				"\\*path_prefix=\"%s\",target_version=\"%d\",ret=\"%d\"*\\",
			path_prefix, target_version, ret);
		goto out;
	}
	if (is_subvol_corrupted) {
		LOG_ERROR(NULL, "corrupt staging subvolume (post)", class_osvol_staging,
				"\\*path_prefix=\"%s\",target_version=\"%d\"*\\",
			path_prefix, target_version);
		fatal_update_error(latest_version, target_version);
		ret = -1;
		goto out;
	}

	/* create read-only snapshot */
	progress_step(PROGRESS_MSG_SNAPSHOT);
	log_stdout("Creating read-only snapshot %d\n", target_version);
	LOG_INFO(NULL, "Creating read-only snapshot", class_btrfs_mnt_pt, "%d", target_version);
	if (asprintf(&path_prefix, "%s/%d", MOUNT_POINT, target_version) < 0)
		abort();
	if (asprintf(&snap_cmd, "%s subvolume snapshot -r %s %s", BTRFS_CMD, STAGING_SUBVOL, path_prefix) < 0) {
		abort();
	ret = system(snap_cmd);
	free(snap_cmd);
	if (ret != 0) {
		LOG_ERROR(NULL, "btrfs snapshot creation failed", class_btrfs_mnt_pt,
				"\\*target_version=\"%d\"*\\", target_version);
		fatal_update_error(latest_version, target_version);
		goto out;
	}

	/* TODO: we need to sort directory permissions */

	/* sync */
	LOG_INFO(NULL, "Calling sync (btrfs)", class_sync, "");
	sync();

	LOG_INFO(NULL, "Verifying new OS subvolume (final)", class_osvol, "%s (%d)",
		path_prefix, target_version);
	ret = do_verify("os-core", target_version, &is_subvol_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify new OS subvolume (final)", class_osvol,
				"\\*path_prefix=\"%s\",target_version=\"%d\",ret=\"%d\"*\\",
				path_prefix, target_version, ret);
		goto out;
	}
	if (is_subvol_corrupted) {
		LOG_ERROR(NULL, "corrupt new OS subvolume (final)", class_osvol,
				"\\*path_prefix=\"%s\",target_version=\"%d\"*\\",
				path_prefix, target_version);
		fatal_update_error(latest_version, target_version);
		goto out;
	}


	return 0;
out:
	/* something went wrong. Make nothing sees this version snapshot */
	if (asprintf(&version_str, "%i", target_version) < 0)
		abort();
	remove_snapshot(version_str);
	free(version_str);
	return ret;
}
#else
static int finalize_btrfs(struct list UNUSED_PARAM *updates, int UNUSED_PARAM latest_version, int UNUSED_PARAM target_version)
{
	return 0;
}
#endif

#ifdef SWUPD_WITH_ESP
static int finalize_esp(struct list *updates, int latest_version, int target_version)
{
	bool is_esp_corrupted;

	/* check if ESP update if needed */
	if (!esp_updates_available(updates, target_version)) {
		return 0;
	}

	ret = mount_esp_fs();
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount esp", class_esp_mnt_pt, "%d (%s)", ret, strerror(errno));
		fatal_update_error(latest_version, target_version);
		return -1;
	}

	if (copy_files_to_esp(target_version) == -1) {
		fatal_update_error(latest_version, target_version);
		unmount_esp_fs();
		return -1;
	}

	if (asprintf(&path_prefix, "%s", ESP_MOUNT) < 0);
		abort();
	LOG_INFO(NULL, "Verifying ESP volume: (final)", class_esp_mnt_pt, "%s (%d)",
		path_prefix, target_version);
	ret = do_verify("esp", target_version, &is_esp_corrupted);
	if (ret != 0) {
		LOG_ERROR(NULL, "cannot verify read-only esp", class_esp_mnt_pt, "%s (%d) ret=%d",
			path_prefix, target_version, ret);
		unmount_esp_fs();
		return -1;
	}
	if (is_esp_corrupted) {
		LOG_ERROR(NULL, "corrupt read-only esp", class_esp_mnt_pt, "%s (%d)",
			path_prefix, target_version);
		fatal_update_error(latest_version, target_version);
		unmount_esp_fs();
		return -1;
	}

	/* insure ESP is unmounted */
	unmount_esp_fs();

	/* set EFI variable for successful update */
	if (efivar_bootloader_set_next_boot_to_version(target_version) == -1) {
		fatal_update_error(latest_version, target_version);
		return -1;
	}

	/* sync */
	LOG_INFO(NULL, "Calling sync (esp)", class_sync, "");
	sync();

	return 0;
}
#else
static int finalize_esp(struct list UNUSED_PARAM *updates, int UNUSED_PARAM latest_version, int UNUSED_PARAM target_version)
{
	return 0;
}
#endif

int finalize(struct list *updates, int latest_version, int target_version)
{
	int ret = 0;

	ret = finalize_btrfs(updates, latest_version, target_version);
	if (ret != 0) {
		LOG_ERROR(NULL, "Updating btrfs failed", class_btrfs_mnt_pt, "");
		return -1;
	}

	ret = finalize_esp(updates, latest_version, target_version);
	if (ret != 0) {
		LOG_ERROR(NULL, "Updating ESP failed", class_esp_mnt_pt, "");
		return -1;
	}

	ret = finalize_rootfs(updates, target_version);
	if (ret != 0) {
		LOG_ERROR(NULL, "Updating rootfs failed", class_osvol, "");
		return -1;
	}

	return ret;
}
