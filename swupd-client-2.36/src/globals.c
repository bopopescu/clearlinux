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
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <swupd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int verbose = 0; /* possibly library, so default quiet. cmd line overrides */
bool download_only;
bool verify_esp_only;
int update_count = 0;
int update_skip = 0;
int need_update_boot = 0;
int need_update_bootloader = 0;
bool update_complete = false;
bool network_available = true; /* assume net access until disproved */
bool ignore_config = true;
bool ignore_state = true;
bool ignore_boot = false;
bool ignore_orphans = true;
bool fix = false;
char *format_string = NULL;
char *path_prefix = NULL;      /* must always end in '/' */
char *mounted_dirs = NULL;

#define SWUPD_DEFAULT_FORMAT "2"
bool set_format_string(char *userinput)
{
	int version;

	if (userinput == NULL) {
		if (asprintf(&format_string, "%s", SWUPD_DEFAULT_FORMAT) < 0)
			abort();
		return true;
	}

	// allow "staging" as a format string
	if ((strcmp(userinput, "staging") == 0)) {
		if (asprintf(&format_string, "%s", userinput) < 0)
			abort();
		return true;
	}

	// otherwise, expect a positive integer
	errno = 0;
	version = strtoull(userinput, NULL, 10);
	if ((errno < 0) || (version <= 0))
		return false;
	if (asprintf(&format_string, "%d", version) < 0)
		abort();

	return true;
}

bool init_globals(void)
{
	struct stat statbuf;
	int ret;

	/* insure path_prefix is at least '/', ends in '/' and is a valid dir */
	if (path_prefix != NULL) {
		int len;
		len = strlen(path_prefix);
		if (!len || (path_prefix[len-1] != '/')) {
			if (asprintf(&path_prefix, "%s/", path_prefix) <= 0)
				abort();
		}
	} else {
		if (asprintf(&path_prefix, "/") < 1)
			abort();
	}
	ret = stat(path_prefix, &statbuf);
	if (ret != 0 || !S_ISDIR(statbuf.st_mode)) {
		printf("Bad path_prefix %s (%s), cannot continue.\n",
			path_prefix, strerror(errno));
		return false;
	}
	return true;
}

void free_globals(void)
{
	free(content_server_urls[0]);
	free(version_server_urls[0]);
	free(path_prefix);
	free(format_string);
	free(mounted_dirs);
}
