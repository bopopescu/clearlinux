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
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifndef ENONET
#define ENONET          64      /* Machine is not on the network */
#endif

#include <swupd.h>

/* NOTE: Today the content and version server urls are the same in
 * all cases.  It is highly likely these will eventually differ, eg:
 * swupd-version.01.org and swupd-files.01.org as this enables
 * different quality of server and control of the servers
 */

#define URL_COUNT 2
char *version_server_urls[URL_COUNT] = {
	NULL,
	"https://download.clearlinux.org/update",
};
char *content_server_urls[URL_COUNT] = {
	NULL,
	"https://download.clearlinux.org/update",
};

char *preferred_version_url;
char *preferred_content_url;

static int test_version_urls(int *server_version)
{
	int ver;
	int idx;

	for(idx = 1; idx < URL_COUNT; idx++) {
		ver = try_version_download(version_server_urls[idx]);
		if (ver >= 0) {
			if (server_version) {
				*server_version = ver;
			}

			return idx;
		}
	}
	return -1;
}

/*
 * There are three possible urls for each of content and version.
 *
 * For the version server:
 * 1) If the user has specified one we use it, else...
 * 2) Try downloading the version file from the external server, subsequently
 *    preferring the external server if successful, else...
 * 3) Try downloading the version file from the internal server, subsequently
 *    preferring the internal server if successful, else...
 * 4) Set preferred to external and return -ENONET.
 *
 * For the content server:
 * 1) If the user has specified one we use it, else...
 * 2) Use the version url determination code to figure out if we can
 *    communicate with the external or internal server, else...
 * 3) Toggle network_available to false
 * 4) Set preferred to external and return -ENONET.
 *
 * This function calls test_version_urls() which has the side-effect of
 * trying to download and store the current server_version.
 * */
int pick_urls(int *server_version)
{
	int idx;
	int ret = 0;
	int v_idx = -1;
	int c_idx = -1;

	if ((version_server_urls[0] != NULL) && (content_server_urls[0] != NULL)) {
		v_idx = 0;
		c_idx = 0;

		// informational LOG to build beta usage statistics
		LOG_ERROR(NULL, "Custom urls", class_url, "\\*version_server_urls=\"%s\",content_server_urls=\"%s\"*\\",
				version_server_urls[0], content_server_urls[0]);

		if (server_version) {
			*server_version = try_version_download(version_server_urls[0]);
			if (*server_version < 0) {
				network_available = false;
				ret = *server_version;
			}
		}

		goto out;
	}

	idx = test_version_urls(server_version);

	if ((version_server_urls[0] != NULL) && (content_server_urls[0] == NULL)) {
		if (idx == -1) {
			c_idx = 1;
			network_available = false;
			ret = -ENONET;
		} else {
			c_idx = idx;
		}
		v_idx = 0;

		goto out;
	} else if ((version_server_urls[0] == NULL) && (content_server_urls[0] != NULL)) {
		if (idx == -1) {
			v_idx = 1;
			network_available = false;
			ret = -ENONET;
		} else {
			v_idx = idx;
		}
		c_idx = 0;

		goto out;
	} else {//((version_server_urls[0] == NULL) && (content_server_urls[0] == NULL))
		if (idx == -1) {
			v_idx = 1;
			c_idx = 1;
			network_available = false;
			ret = -ENONET;
		} else {
			v_idx = idx;
			c_idx = idx;
		}
	}
out:
	if (network_available == true) {// informational LOG for usage statistics
		LOG_ERROR(NULL, "Urls", class_url, "\\*version_index=\"%d\",content_index=\"%d\"*\\", v_idx, c_idx);
	}

	preferred_version_url = version_server_urls[v_idx];
	preferred_content_url = content_server_urls[c_idx];
	return ret;
}
