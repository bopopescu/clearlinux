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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>

#include <swupd.h>
#include "progress.h"
#include <signature.h>

#ifdef SWUPD_WITH_BINDMNTS
static bool is_bind_mount(const char *filename)
{
	char *fname;
	bool ret = false;

	if (asprintf(&fname, ":%s:", filename) < 0)
		abort();

	if (strstr(SWUPD_BM_TARGETS, fname)) {
		ret = true;
		LOG_DEBUG(NULL, "Known Bind Mount: ", class_mnt_pt, "%s", filename);
	}

	free(fname);

	return ret;
}
#else /* SWUPD_WITHOUT_BINDMNTS */
static bool is_bind_mount(const char UNUSED_PARAM *filename)
{
	return false;
}
#endif /* SWUPD_*_BINDMNTS */

/* Tests for component integrity at path_prefix against specified version. If
 * component is NULL, path_prefix's contents are tested against all subscribed
 * components tracked components.
 * Returns:
 *     0: function succeeded, integrity is returned in is_corrupted
 *   < 0: function failed (no network or ?), impossible to assess integrity
 */
int do_verify(const char *component, int version, bool *is_corrupted)
{
	struct file *file;
	struct list *iter;
	struct list *difference = NULL;
	int difference_count = 0;
	struct manifest *official_manifest = NULL, *system_manifest = NULL;
	bool use_xattrs = true;
	int ret = -1;
	int err;

	swupd_curl_set_current_version(version);

#ifndef SWUPD_ANDROID
	/* check disk space before downloading the manifests */
	progress_step(PROGRESS_MSG_CHECK_DISKSPACE);
	ret = free_disk_space_for_manifest(0, 0);
	if (ret != 0) {
		log_stdout("Not enough disk space for manifests\n");
		goto clean_exit;
	}
#endif

	/* read current version's official (ie: server) manifest */
	err = load_manifests(version, version, "MoM", NULL, &official_manifest);
	if (official_manifest == NULL) {
		log_stdout("Unable to find official manifest version %d\n", version);
		log_stdout("(bad %s/version and/or network problem?)\n", STATE_DIR);
		goto clean_exit;
	}
	subscription_versions_from_MoM(official_manifest);

	err = recurse_manifest(official_manifest, component);
	if (err != 0) {
		log_stdout("Cannot load official MoM sub-manifests, err = %i\n", err);
		goto clean_exit;
	}
	consolidate_submanifests(official_manifest);

	if (component) {
		log_stdout("Verifying [%s] against version %i in path %s\n", component, version, path_prefix);
	} else {
		log_stdout("Verifying all tracked files against version %i in path %s\n", version, path_prefix);
		//TODO: output list of subscriptions?
	}

	/* create manifest describing what's installed on the system disk */
	if (component && strcmp(component, "esp") == 0) {
		use_xattrs = false; /* esp files are on vfat and have no xattrs. */
	}
	system_manifest = manifest_from_directory(version, use_xattrs);
	if (system_manifest == NULL) {
		LOG_ERROR(NULL, "Unable to create local system manifest", class_manifest, "\\*path_prefix=\"%s\"*\\", path_prefix);
		goto clean_exit;
	}

/*	LOG_INFO(NULL, "Writing debug manifests", class_manifest, "");
	debug_write_manifest(system_manifest, "debug_manifest_localsystem.txt");
	debug_write_manifest(official_manifest, "debug_manifest_officialserver.txt");
*/
	LOG_INFO(NULL, "Creating difference list", class_delta, "");
	difference = create_difference_list(system_manifest, official_manifest);

	update_count = 0;
	update_skip = 0;
	difference_count = list_len(difference);
	log_stdout("==========================================================\n");
	ret = 0;

	iter = list_head(difference);
	while (iter) {
		file = iter->data;
		iter = iter->next;

		if (ignore(file))
			continue;

		if (file->is_boot) {
			if (access(file->filename, R_OK) == 0) {
				log_stdout_extraverbose("\t(modified boot file)\t\t%s\n", file->filename);
			} else {
				log_stdout_extraverbose("\t(missing boot file)\t\t%s\n", file->filename);
			}
		} else if (is_directory_mounted(file->filename) &&  is_bind_mount(file->filename)) {
			log_stdout_extraverbose("\t(bindmount)\t\t\t%s\n", file->filename);
		} else if (is_directory_mounted(file->filename) && !is_bind_mount(file->filename)) {
			log_stdout_extraverbose("\t(mountpoint)\t\t\t%s\n", file->filename);
		} else if (is_under_mounted_directory(file->filename)) {
			log_stdout_extraverbose("\t(under mountpoint)\t\t%s\n", file->filename);
		} else if (file->is_orphan) {
			file->is_deleted = 1;
			log_stdout_extraverbose("\t(orphan)\t\t\t%s\n", file->filename);
		} else if (file->is_dir) {
			log_stdout("\t(directory)\t\t\t%s\n", file->filename);
		} else { /* plain file*/
			log_stdout("\t\t\t\t\t%s\n", file->filename);
		}
	}

	if (difference_count) { /* summarize the differences for the user */
		log_stdout("==========================================================\n");
		log_stdout("%d files in %s differ from the manifest\n", difference_count, path_prefix);
	}

	LOG_DEBUG(NULL, "Verify stats", class_delta,
		"%d files processed in %s: %d differences, %d ignored",
		list_len(system_manifest->files), path_prefix, difference_count, update_skip);
	log_stdout("%d files processed in %s: %d differences, %d ignored\n",
		list_len(system_manifest->files), path_prefix, difference_count, update_skip);
	log_stdout("==========================================================\n");

	if (difference_count - update_skip == 0) {
		log_stdout("\nFiles in path %s match Manifest version %d.\n", path_prefix, version);
		*is_corrupted = false;
	} else {
		log_stdout("\nFiles in path %s do not match Manifest version %d.\n", path_prefix, version);
		*is_corrupted = true;
	}

#ifndef SWUPD_ANDROID
	/* optionally fixup differences...this should go into a caller who wants
	 * to fix after verifying and finding corruption */
	if ((*is_corrupted == true) && (fix)) {
		LOG_INFO(NULL, "verify fix", class_undef, "");

		progress_step(PROGRESS_MSG_CHECK_DISKSPACE);
		ret = free_disk_space_generic(0, 0, official_manifest);
		if (ret != 0) {
			log_stdout("Not enough disk space for OS repair\n");
			goto clean_exit;
		}

		ret = download_subscribed_packs(0, version);
		if (ret < 0) { // require zero pack
			log_stdout("zero pack downloads failed\n");
			goto clean_exit;
		}

		update_count = difference_count - update_skip;
		LOG_DEBUG(NULL, "Pruned update count", class_undef, "%i updates + %i skip", update_count, update_skip);
		ret = update_loop(difference, version, version);
		if (ret == 0)
			*is_corrupted = false;
		if (strcmp(path_prefix, "/") == 0)
			run_scripts();
		LOG_DEBUG(NULL, "Verify fix update loop complete", class_undef, "ret=%i, is_corrupted=%i", ret, *is_corrupted);
	}
#endif

clean_exit:
	LOG_DEBUG(NULL, "do_verify complete", class_undef, "ret=%i, is_corrupted=%i", ret, *is_corrupted);
	list_free_list(difference);
	free_manifest(official_manifest);
	free_manifest(system_manifest);
	return ret;
}

int update_loop(struct list *updates, int latest_version, int target_version)
{
	int count, ret;
	struct file *file;
	struct list *iter;
	int err;

	LOG_INFO(NULL, "Going into update loop", class_undef,
		"update_count:%d, update_skip:%d", update_count, update_skip);

	/* need update list if filename order to insure directories are
	 * created before their contents */
	updates = list_sort(updates, file_sort_filename);

	LOG_INFO(NULL, "Downloading remaining delta files", class_curl, "");

	start_delta_download();

	iter = list_head(updates);
	count = 0;
	while (iter) {
		file = iter->data;
		iter = iter->next;

		if (!file->is_file)
			continue;

		count++;

		if (count % 100 == 0)
			log_stdout("\r\033[K %i...", count);

		/* for each file: download delta if the delta file does not exist yet */
		/* for each file: apply delta to the staged/ directory; filename is the hash */

		try_delta_download(file);
	}

	end_delta_download();
	log_stdout("\r\033[K");

	/* if (rm_staging_dir_contents("download"))
		return -1;
	 */

	LOG_INFO(NULL, "Downloading remaining full files", class_curl, "");

	if (start_full_download(1) == 0) {
		struct file *last_file = NULL;

		updates = list_sort(updates, file_sort_hash);

		iter = list_head(updates);
		count = 0;
		err = 0;
		while (iter) {
			file = iter->data;
			iter = iter->next;

			count++;

			if (count % 100 == 0)
				log_stdout("\r\033[K %i...", count);

			if (last_file && (strcmp(last_file->hash, file->hash) == 0)) {
				/*
				LOG_DEBUG(NULL, "Skipping dup file", class_curl, "%s = %s",
						last_file->filename, file->filename);
				*/
				continue;
			}

			if (file->is_deleted)
				continue;

			/* if the full file is not there yet, do a full download for it */
			err = full_download(file);
			if (err) {
				LOG_WARN(NULL, "Full file download loop aborted because of fatal mcurl error, continuing to hash verification",
						class_curl, "");
				break;
			}

			last_file = file;
		}

		updates = list_sort(updates, file_sort_filename);

		LOG_INFO(NULL, "calling end_full_download", class_curl, "");

		end_full_download();
		log_stdout("\r\033[K");
	}

	print_delta_statistics();

	if (download_only)
		return -1;

	/* from here onward we're doing real update work modifying "the disk" */

	/* starting at list_head in the filename alpha-sorted updates list
	 * means node directories are added before leaf files */
	LOG_INFO(NULL, "Staging file content", class_osvol_staging, "path_prefix=%s", path_prefix);
	log_stdout("Staging file content\n");
	iter = list_head(updates);
	count = 0;
	while (iter) {
		file = iter->data;
		iter = iter->next;

		if (file->do_not_update || file->is_deleted) {
			LOG_DEBUG(file, "Skipping file staging", class_osvol_staging, "");
			continue;
		}

		/* for each file: fdatasync to persist changed content over reboot, or maybe a global sync */
		/* for each file: check hash value; on mismatch delete and queue full download */
		/* todo: hash check */

		if (!file->is_deleted) {
			do_staging(file);
			count++;
			if (count % 100 == 0)
				log_stdout("\r\033[K %i...", count);
		}
	}

	log_stdout("\r\033[K");

	/* check policy, and if policy says, "ask", ask the user at this point */
	/* check for reboot need - if needed, wait for reboot */

	LOG_INFO(NULL, "Calling sync (pre-finalize)", class_sync, "");
	/* sync */
	sync();

	ret = finalize(updates, latest_version, target_version);

	return ret;
}

int main_update()
{
	int current_version = -1, server_version = -1, latest_version = -1;
	struct manifest *current_manifest = NULL, *server_manifest = NULL;
	struct list *updates = NULL;
	int ret = EXIT_FAILURE;
	int lock_fd;
	int err;
	int mounted_rw = 0;
	bool is_corrupted = false;

	srand(time(NULL));

	check_root();

	lock_fd = p_lockfile();
	if (lock_fd == -EAGAIN) {
		ret = -EAGAIN;	/* will return 245 to the shell */
		goto exit;
	} else if (lock_fd == -1)
		goto exit;

	init_log();
	get_mounted_directories();

	progress_step(PROGRESS_MSG_START);

	if (prep_mount(O_RDONLY) != 0)
		goto clean_progress;

	if (swupd_curl_init() != 0)
		goto clean_prep_mount;

	read_subscriptions_alt();

	if (!signature_initialize(UPDATE_CA_CERTS_PATH "/" SIGNATURE_CA_CERT)) {
		LOG_ERROR(NULL, "Can't initialize the signature module!", class_security, "");
		goto clean_curl;
	}

	/* Step 1: get versions */

reread_versions:
	err = read_versions(&current_version, &latest_version, &server_version);
	if (err < 0) {
		goto clean_curl;
	}
	if (server_version <= latest_version) {
		log_stdout("Version on server (%i) is not newer than system version (%i)\n", server_version, latest_version);
		ret = EXIT_SUCCESS;
		goto clean_curl;
	}
	log_stdout("Preparing to update from %i to %i\n", latest_version, server_version);

	/* Step 2: housekeeping */

	if (mounted_rw == 0) {
		if (prep_mount(O_RDWR) != 0) {
			goto clean_curl;
		}
		mounted_rw = 1;
	}

	if (rm_staging_dir_contents("download")) {
		goto clean_curl;
	}

	/* Step 3: setup manifests */

	/* check disk space before downloading the manifests */
	progress_step(PROGRESS_MSG_CHECK_DISKSPACE);
	ret = free_disk_space_for_manifest(current_version, latest_version);
	if (ret != 0) {
		log_stdout("Not enough disk space for manifests\n");
		goto clean_curl;
	}

	/* get the from/to MoM manifests */
	LOG_INFO(NULL, "Updating MoM manifests", class_manifest, "%i->%i", latest_version, server_version);
	progress_step(PROGRESS_MSG_LOAD_CURRENT_MANIFEST);
	load_manifests(latest_version, latest_version, "MoM", NULL, &current_manifest);
	if (current_manifest == NULL)
		LOG_ERROR(NULL, "load_manifest() returned NULL current_manifest", class_manifest, "");
	progress_step(PROGRESS_MSG_LOAD_SERVER_MANIFEST);
	load_manifests(latest_version, server_version, "MoM", NULL, &server_manifest);
	if (server_manifest == NULL)
		LOG_ERROR(NULL, "load_manifest() returned NULL server_manifest", class_manifest, "");
	if (current_manifest == NULL || server_manifest == NULL) {
		log_stdout("Unable to load manifest (config or network problem?)\n");
		goto clean_exit;
	}
	subscription_versions_from_MoM(server_manifest);

	LOG_INFO(NULL, "linking submanifests", class_manifest, "");
	link_submanifests(current_manifest, server_manifest);

	/* updating subscribed manifests is done as part of recurse_manifest */

	/* read the current collective of manifests that we are subscribed to */
	LOG_INFO(NULL, "recursing current", class_manifest, "");
	err = recurse_manifest(current_manifest, NULL);
	if (err != 0) {
		log_stdout("Cannot load current MoM sub-manifests, err = %d (%s), exiting\n", err, strerror(errno));
		goto clean_exit;
	}
	LOG_INFO(NULL, "current contentsize", class_manifest, "ver=%d, size=%i", current_manifest->version, current_manifest->contentsize);

	/* consolidate the current collective manifests down into one in memory */
	LOG_INFO(NULL, "consolidating current", class_manifest, "");
	consolidate_submanifests(current_manifest);

	/* read the new collective of manifests that we are subscribed to */
	LOG_INFO(NULL, "recursing server", class_manifest, "");
	err = recurse_manifest(server_manifest, NULL);
	if (err != 0) {
		log_stdout("Cannot load server MoM sub-manifests, err = %d (%s), exiting\n", err, strerror(errno));
		goto clean_exit;
	}
	LOG_INFO(NULL, "server contentsize", class_manifest, "ver=%d, size=%i", server_manifest->version, server_manifest->contentsize);

	/* consolidate the new collective manifests down into one in memory */
	LOG_INFO(NULL, "consolidating server", class_manifest, "");
	consolidate_submanifests(server_manifest);

	/* prepare for an update process based on comparing two in memory manifests */
	LOG_INFO(NULL, "linking manifests", class_manifest, "");
	link_manifests(current_manifest, server_manifest);

/*	LOG(NULL, "Writing debug manifests", "");
	debug_write_manifest(current_manifest, "debug_manifest_current.txt");
	debug_write_manifest(server_manifest, "debug_manifest_server.txt");
*/
	/* Step 4: check disk state before attempting update */

	ret = prepare(&is_corrupted, current_version, latest_version);
	if ((ret < 0) || is_corrupted) {
#warning ignoring preparation phase corruption
		log_stdout("\n\n\nFAILED PREPARATION PHASE of update\n");
		log_stdout("FAILED PREPARATION PHASE of update\n");
		log_stdout("FAILED PREPARATION PHASE of update\n");
		log_stdout("....carrying on anyway\n\n\n");
		//goto clean_curl;
	} else if (ret == 1) {
		log_stdout("Restarting update after preparation phase restored state...\n");
		free_manifest(current_manifest);
		free_manifest(server_manifest);
		goto reread_versions;
	}

	/* Step 5: if disk space allows get the packs and untar */

	progress_step(PROGRESS_MSG_CHECK_DISKSPACE);
	ret = free_disk_space_generic(current_version, latest_version, server_manifest);
	if (ret != 0) {
		log_stdout("Not enough disk space for OS Update\n");
		goto clean_exit;
	}
	ret = download_subscribed_packs(latest_version, server_version);
	if (ret == -ENONET) {
		// packs don't always exist, tolerate that but not ENONET
		log_stdout("No network, or server unavailable for pack downloads\n");
		goto clean_exit;
	}

	/* Step 6: some more housekeeping */

	/* TODO: consider trying to do less sorting of manifests */

	LOG_INFO(NULL, "Creating update list", class_manifest, "");
	updates = create_update_list(current_manifest, server_manifest);

	link_renames(updates, current_manifest); /* TODO: Have special lists for candidate and renames */

	print_statistics(latest_version, server_version);

	/* Step 7: apply the update */

	ret = update_loop(updates, latest_version, server_version);
	if (ret == 0) {
		ret = update_device_latest_version(server_version);
		progress_step(PROGRESS_MSG_UPDATED);
	}

	/* Run any scripts that are needed to complete update */
	run_scripts();

clean_exit:
	list_free_list(updates);
	free_manifest(current_manifest);
	free_manifest(server_manifest);

clean_curl:
	signature_terminate();
	swupd_curl_cleanup();
	free_subscriptions();

clean_prep_mount:
	post_unmount();

clean_progress:

	progress_step(PROGRESS_MSG_DONE);

	close_log(ret, latest_version, server_version, log_update);
	v_lockfile(lock_fd);

	dump_file_descriptor_leaks();

exit:
	return ret;
}

int main_verify(int current_version)
{
	int ret = EXIT_FAILURE;
	int lock_fd;
	bool is_corrupted=true;
	int is_system;

	check_root();

	lock_fd = p_lockfile();
	if (lock_fd == -EAGAIN) {
		ret = -EAGAIN;	/* will return 245 to the shell */
		goto exit;
	} else if (lock_fd == -1)
		goto exit;

	init_log();
	get_mounted_directories();

	if (prep_mount((verify_esp_only ? O_RDWR : O_RDONLY)) != 0)
		goto clean_closelog;

	if (current_version == -1) {
		current_version = read_version_from_subvol_file(path_prefix);
		if (current_version == -1) {
			LOG_ERROR(NULL, "Unable to determine version of snapshot", class_version,
					"\\*path_prefix=\"%s\"*\\", path_prefix);
			log_stdout("Unable to determine version of snapshot %s\n", path_prefix);
			log_stdout("(possibly empty snapshot, or try passing -m flag)\n\n");
			goto clean_prep_mount;
		}
	}
	if (current_version == 0) {
		LOG_ERROR(NULL, "Update from version 0 not supported yet", class_version, "");
		log_stdout("Update from version 0 not supported yet.\n");
		goto clean_prep_mount;
	}
	if (SWUPD_VERSION_IS_DEVEL(current_version) || SWUPD_VERSION_IS_RESVD(current_version)) {
		LOG_ERROR(NULL, "Skipping verify of dev build", class_version,
				"\\*current_version=\"%d\"*\\", current_version);
		log_stdout("Skipping verify of dev build %d\n", current_version);
		goto clean_prep_mount;
	}

	if (swupd_curl_init() != 0)
		goto clean_prep_mount;

	read_subscriptions_alt();

#ifdef SWUPD_WITH_ESP
	if (verify_esp_only) {
		ret = mount_esp_fs();
		if (ret != 0) {
			LOG_DEBUG(NULL, "Could not mount esp", class_esp_mnt_pt, "%d (%s)", ret, strerror(errno));
			goto clean_curl;
		}
		if (asprintf(&path_prefix, "%s", ESP_MOUNT) < 0)
			abort();
		ret = do_verify("esp", current_version, &is_corrupted);
		if (ret != 0) {
			LOG_DEBUG(NULL, "cannot verify read-only esp", class_esp_mnt_pt, "%s (%d) ret=%d",
				path_prefix, current_version, ret);
			unmount_esp_fs();
			goto clean_curl;
		}
		if (is_corrupted) {
			LOG_DEBUG(NULL, "corrupt read-only esp", class_esp_mnt_pt, "%s (%d)",
				path_prefix, current_version);
		}
		/* insure ESP is unmounted */
		unmount_esp_fs();

		swupd_curl_cleanup();
		goto clean_curl;
	}
#endif /* SWUPD_WITH_ESP */

	ret = do_verify(NULL, current_version, &is_corrupted);
	is_system = !strcmp(path_prefix, "/");
	if (ret == 0) {
		ret = EXIT_SUCCESS;
		if (is_corrupted) {
			ret = -1;
			if (is_system)
				critical_verify_error(current_version);
		} else if (is_system){
			clear_verify_error();
		}
		LOG_INFO(NULL, "Main verify complete", class_undef, "is_corrupted=%d", is_corrupted);
	}

clean_curl:
	swupd_curl_cleanup();
	free_subscriptions();

clean_prep_mount:
	post_unmount();

clean_closelog:
	close_log(ret, current_version, 0, log_verify);
	v_lockfile(lock_fd);

	dump_file_descriptor_leaks();

exit:
	return ret;
}

