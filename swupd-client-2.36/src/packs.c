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
#include <sys/types.h>
#include <sys/stat.h>

#include <swupd.h>
#include "progress.h"
#include <signature.h>


static int download_pack(int oldversion, int newversion, char *module)
{
	FILE *tarfile = NULL;
	char *tar = NULL;
	char *url = NULL;
	int err = -1;
	char buffer[PATH_MAXLEN];
	struct stat stat;

	sprintf(buffer, "%s/pack-%s-from-%i-to-%i.tar", STATE_DIR, module, oldversion, newversion);

	err = lstat(buffer, &stat);
	if (err == 0 && stat.st_size == 0) {
		LOG_INFO(NULL, "Downloading pack", class_curl, "%s was already downloaded/extracted", buffer);
		return 0;
	}

	log_stdout("Downloading %s pack for version %i\n", module, newversion);
	LOG_INFO(NULL, "Downloading pack", class_curl, "module %s version %i", module, newversion);

	if (asprintf(&url, "%s/%i/pack-%s-from-%i.tar", preferred_content_url, newversion, module, oldversion) < 0)
		return -ENOMEM;

	err = swupd_curl_get_file(url, buffer, NULL, NULL, 0, PROGRESS_MSG_DOWNLOAD_PACK, 1);
	if (err) {
		LOG_WARN(NULL, "pack download failed", class_curl, "\\*err=\"%d\"*\\", err);
		free(url);
		if ((lstat(buffer, &stat) == 0) && (stat.st_size == 0))
			unlink(buffer);
		return err;
	}

	if (!signature_download_and_verify(url, buffer)) {
		LOG_ERROR(NULL, "manifest delta signature failed", class_security, "\\*file=\"%i/pack-%s-from-%i.tar\"*\\",
			  newversion, module, oldversion);
		free(url);
		unlink(buffer);
		return -1;
	}

	free(url);

	progress_step(PROGRESS_MSG_EXTRACTING_PACK);
	if (asprintf(&tar, "tar --directory=%s --warning=no-timestamp " TAR_PERM_ATTR_ARGS " -axf %s/pack-%s-from-%i-to-%i.tar 2> /dev/null",
			STATE_DIR, STATE_DIR, module, oldversion, newversion) < 0)
		return -ENOMEM;

	LOG_INFO(NULL, "Untar of delta pack", class_file_compression, "%s", tar);
	err = system(tar);
	if (err)
		LOG_INFO(NULL, "Untar of delta pack had errors, probably acceptable symlink permission \"errors\"", class_file_compression, "");
	free(tar);
	unlink(buffer);
	/* make a zero sized file to prevent redownload */
	tarfile = fopen(buffer, "w");
	if (tarfile)
		fclose(tarfile);

	return 0;
}

/* pull in packs for base and any subscription */
int download_subscribed_packs(int oldversion, int newversion)
{
	struct list *iter;
	struct sub *sub = NULL;
	int dummy;
	int err;

	LOG_INFO(NULL, "downloading packs", class_subscription, "");
	err = pick_urls(&dummy);
	if (err < 0) {
		LOG_ERROR(NULL, "Unable to pick_url() for pack download", class_subscription,
			"%d (%s)\n", err, strerror(errno));
		return err;
	}
	err = download_pack(oldversion, newversion, "os-core");
	if (err < 0) {
		LOG_DEBUG(NULL, "downloading pack failed", class_subscription,
			"\\*component=base,oldversion=\"%d\",newversion=\"%d\"*\\",
			oldversion, newversion);
		return err;
	}
	iter = list_head(subs);
	while (iter) {
		sub = iter->data;
		iter = iter->next;

		if (strcmp("os-core", sub->component) == 0) // already got it
			continue;

		if (oldversion == sub->version) // pack didn't change in this release
			continue;

		LOG_DEBUG(NULL, "downloading component zero pack newversion", class_subscription,
			"\\*component=\"%s\",oldversion=\"%d\",newversion=\"%d\"*\\",
			sub->component, oldversion, sub->version);
		err = download_pack(oldversion, sub->version, sub->component);
		if (err < 0) {
			LOG_DEBUG(NULL, "downloading zero pack failed", class_subscription,
				"\\*component=\"%s\",oldversion=\"%d\",newversion=\"%d\"*\\",
				sub->component, oldversion, sub->version);
			return err;
		}
	}
	return 0;
}
