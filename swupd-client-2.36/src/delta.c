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
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <libgen.h>

#include <swupd.h>
#include <executor.h>
#include <xattrs.h>
#include "progress.h"

static struct executor *executor;

static void do_delta_download(void* data);


void try_delta_download(struct file *file)
{
	char *url = NULL;
	char *deltafile = NULL;
	int err;
	char buffer[PATH_MAXLEN];
	struct stat stat;

	if (file->is_file == 0)
		return;

	if (file->deltapeer == NULL)
		return;

	if (file->deltapeer->is_file == 0)
		return;

	if (file->deltapeer->is_deleted)
		return;

	/* check if the full file is there already, because if it is, don't do the delta */
	sprintf(buffer, "%s/staged/%s", STATE_DIR, file->hash);
	if (lstat(buffer, &stat) == 0)
		return;

	if (asprintf(&deltafile, "%s/delta/%i-%i-%s", STATE_DIR,
			file->deltapeer->last_change, file->last_change, file->hash) < 0)
		abort();

	if (lstat(deltafile, &stat) != 0) {
		if (asprintf(&url, "%s/%i/delta/%i-%i-%s", preferred_content_url, file->last_change,
				file->deltapeer->last_change, file->last_change, file->hash) < 0)
			abort();

		err = swupd_curl_get_file(url, deltafile, file, NULL, 0, PROGRESS_MSG_NONE, 0);
		if (err) {
			LOG_WARN(file, "delta file download failed", class_curl, "\\*err=\"%d\",file=\"%i-%i-%s\"*\\",
					err, file->deltapeer->last_change, file->last_change, file->hash);
			goto out;
		}
	}

	if (executor)
		executor_submit_task(executor, do_delta_download, file);
	else
		do_delta_download(file);

out:
	free(url);
	free(deltafile);
}

static void do_delta_download(void *data)
{
	struct file *file = data;
	char *origin;
	char *dir, *base, *tmp = NULL, *tmp2 = NULL;
	char *deltafile = NULL;
	char *buffer;
	char *hash = NULL;
	int ret;
	struct stat stat;

	if (asprintf(&deltafile, "%s/delta/%i-%i-%s", STATE_DIR,
			file->deltapeer->last_change, file->last_change, file->hash) < 0)
		abort();

	/* check if the full file is there already, because if it is, don't do the delta */
	if (asprintf(&buffer, "%s/staged/%s", STATE_DIR, file->hash) < 0)
		abort();
	ret = lstat(buffer, &stat);
	if (ret == 0) {
		unlink(deltafile);
		free(deltafile);
		free(buffer);
		return;
	}

	tmp = strdup(file->deltapeer->filename);
	tmp2 = strdup(file->deltapeer->filename);

	dir = dirname(tmp);
	base = basename(tmp2);

	if (asprintf(&origin, "%s/%s/%s", STAGING_SUBVOL, dir, base) < 0)
		abort();

	ret = apply_bsdiff_delta(origin, buffer, deltafile);
	if (ret) {
		LOG_ERROR(file, "Delta patch failed", class_delta, "\\*ret=\"%i\"*\\", ret);
		unlink_all_staged_content(file);
		goto out;
	}

	hash = compute_hash(file, buffer);
	if (hash == NULL) {
		LOG_ERROR(file, "hash computation failed", class_hash,
			"\\*computedhash=NULL,expectedhash=\"%s\",originfile=\"%s\",deltafile=\"%s\"*\\",
			file->hash, origin, deltafile);
		unlink_all_staged_content(file);
		goto out;
	}
	if (strcmp(file->hash, hash) != 0) {
		LOG_ERROR(file, "Delta patch application failed", class_delta,
			"\\*computedhash=\"%s\",expectedhash=\"%s\",originfile=\"%s\",deltafile=\"%s\"*\\",
			hash, file->hash, origin, deltafile);
		unlink_all_staged_content(file);
		goto out;
	}

	if (xattrs_compare(origin, buffer) != 0) {
		LOG_ERROR(file, "Delta patch xattrs copy failed", class_xattrs, "");
		unlink_all_staged_content(file);
		goto out;
	}

	unlink(deltafile);

out:
	free(origin);
	free(deltafile);
	free(buffer);
	free(hash);
	free(tmp);
	free(tmp2);
}


void start_delta_download(void)
{
	int nworkers = (int) sysconf(_SC_NPROCESSORS_ONLN);
	executor = executor_create(nworkers, 10*nworkers, true);
	if (executor == NULL)
		LOG_WARN(NULL, "Could not create deltadownload threadpool, continuing single-threaded.", class_thread, "");
}

void end_delta_download(void)
{
	if (executor) {
		LOG_DEBUG(NULL, "Waiting for delta thread pools to finish", class_thread, "");
		executor_destroy(executor, true);
	}
}
