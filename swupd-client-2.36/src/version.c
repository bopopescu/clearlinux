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

#include <swupd.h>
#include "progress.h"

int read_version_from_subvol_file(char *path_prefix)
{
	char line[LINE_MAX];
	FILE *file;
	int v = -1;
	char *buildstamp;

	if (asprintf(&buildstamp, "%s/usr/lib/os-release", path_prefix) < 0)
		abort();
	file = fopen(buildstamp, "rm");
	if (!file) {
		LOG_ERROR(NULL, "Cannot read os-release from /usr/lib", class_file_io, "\\*buildstamp=\"%s\",strerror=\"%s\"*\\",
				buildstamp, strerror(errno));
		if (asprintf(&buildstamp, "%s/etc/os-release", path_prefix) < 0)
			abort();
		file = fopen(buildstamp, "rm");
		if (!file) {
			LOG_ERROR(NULL, "Cannot read os-release /etc", class_file_io, "\\*buildstamp=\"%s\",strerror=\"%s\"*\\",
					buildstamp, strerror(errno));
			free(buildstamp);
			return v;
		}
	}

	while(!feof(file)) {
		line[0] = 0;
		if (fgets(line, LINE_MAX, file) == NULL)
			break;

		if (strncmp(line,"VERSION_ID=", 11) == 0) {
			v = strtoull(&line[11], NULL, 10);
			break;
		}
	}

	free(buildstamp);
	fclose(file);
	return v;
}

#ifdef SWUPD_WITH_BTRFS
static int read_version_from_swupd_file(char *filename)
{
	FILE *file;
	int v = -1;
	char *fullfile = NULL;

	if (asprintf(&fullfile, "%s/%s", STATE_DIR, filename) < 0)
		abort();

	file = fopen(fullfile, "rm");

	if (!file) {
		LOG_ERROR(NULL, "Cannot read version file", class_file_io, "\\*fullfile=\"%s\",strerror=\"%s\"*\\",
				fullfile, strerror(errno));
		free(fullfile);
		return v;
	}

	if (fscanf(file, "%i", &v) < 0)
		LOG_ERROR(NULL, "version file empty", class_file_misc, "\\*fullfile=\"%s\"*\\", fullfile);

	fclose(file);
	free(fullfile);

	return v;
}
#endif

int read_versions(int *current_version, int *latest_version, int *server_version)
{
	int ret;

#ifdef SWUPD_WITH_BTRFS
#warning btrfs version handling needs fixing after zero version fixes
	*current_version = read_version_from_subvol_file("");
	*latest_version = read_version_from_swupd_file("version");
	if (*latest_version == -1)
		*latest_version = read_version_from_subvol_file(STAGING_SUBVOL);
	if (*current_version <= 0 || *latest_version <= 0) {
		LOG_ERROR(NULL, "Invalid version numbers", class_version, "\\*current_version=\"%d\",latest_version=\"%d\"*\\",
				*current_version, *latest_version);
		return -1;
	}
#else	/*SWUPD_WITHOUT_BTRFS*/
	*current_version = *latest_version = read_version_from_subvol_file("");
	if (*latest_version < 0) {
		LOG_ERROR(NULL, "Invalid version number", class_version, "\\*latest_version=\"%d\"*\\",
				*latest_version);
	}
	if (*latest_version == 0) {
		LOG_ERROR(NULL, "Update from version 0 not supported yet", class_version, "");
		log_stdout("Update from version 0 not supported yet.\n");
		return -1;
	}
#endif
	if (SWUPD_VERSION_IS_DEVEL(*current_version) || SWUPD_VERSION_IS_RESVD(*current_version)) {
		LOG_ERROR(NULL, "Update of dev build not supported", class_version,
				"\\*current_version=\"%d\"*\\", *current_version);
		log_stdout("Update of dev build not supported %d\n", *current_version);
		return -1;
	}
	swupd_curl_set_current_version(*latest_version);

	/* set preferred version and content server urls (and as a
	 * side-effect download the current server_version) */
	progress_step(PROGRESS_MSG_GET_SERVER_VERSION);
	LOG_INFO(NULL, "Getting version from server", class_version, "");
	ret = pick_urls(server_version);
	if (ret < 0) {
		log_stdout("Unable to download server version, ret = %i\n", ret);
		return ret;
	}

	if (*server_version < 0) {
		LOG_ERROR(NULL, "Invalid Server version number", class_version,
				"\\*server_version=\"%d\"*\\", *server_version);
		return -1;
	}

	//TODO allow policy layer to send us to intermediate version?

	swupd_curl_set_requested_version(*server_version);

	return 0;
}

/* this function attempts to download the latest server version string file from
 * the specified server to a memory buffer, returning either a negative integer
 * error code or >= 0 representing the server version*/
int try_version_download(char *test_url)
{
	char *url = NULL;
	char *path = NULL;
	int ret = 0;
	char *tmp_version;

	tmp_version = malloc(LINE_MAX);
	if (tmp_version == NULL)
		return -ENOMEM;

	if (asprintf(&url, "%s/version/format%s/latest", test_url, format_string) < 0) {
		ret = -ENOMEM;
		goto out_ver;
	}

	if (asprintf(&path, "%s/server_version", STATE_DIR) < 0) {
		ret = -ENOMEM;
		goto out_url;
	}
	unlink(path);

	ret = swupd_curl_get_file(url, path, NULL, tmp_version, 1, PROGRESS_MSG_NONE, 0);
	if (ret) {
		LOG_DEBUG(NULL, "Getting server version failed", class_curl,
				"\\*ret=\"%d\",url=\"%s\"*\\", ret, url);
		goto out_path;
	} else {
		ret = strtol(tmp_version, NULL, 10);
		LOG_DEBUG(NULL, "Got server version", class_curl,
				"\\*ret=\"%d\"*\\", ret);
	}

out_path:
	free(path);
out_url:
	free(url);
out_ver:
	free(tmp_version);
	return ret;
}

int update_device_latest_version(int version)
{
	FILE *file = NULL;
	char *path = NULL;

	if (asprintf(&path, "%s/version", STATE_DIR) < 0)
		return -ENOMEM;
	file = fopen(path, "w");
	if (!file) {
		LOG_ERROR(NULL, "Cannot open version file for write", class_version, "");
		free(path);
		return -1;
	}

	fprintf(file, "%i\n", version);
	fflush(file);
	fdatasync(fileno(file));
	fclose(file);
	free(path);
	return 0;
}
