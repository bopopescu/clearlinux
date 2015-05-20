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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <errno.h>

#include <swupd.h>
#include "progress.h"

static const struct option prog_opts[] = {
	{"download", no_argument, 0, 'd'},
	{"help", no_argument, 0, 'h'},
	{"url", required_argument, 0, 'u'},
	{"contenturl", required_argument, 0, 'c'},
	{"versionurl", required_argument, 0, 'v'},
	{"format", required_argument, 0, 'F'},
	{"verbose", no_argument, 0, 'V'},
	{"quiet", no_argument, 0, 'q'},
	{0, 0, 0, 0}
};

static void print_help(const char *name) {
	printf("Usage:\n");
	printf("   %s [OPTION...]\n\n", basename((char*)name));
	printf("Help Options:\n");
	printf("   -h, --help              Show help options\n\n");
	printf("Application Options:\n");
	printf("   -d, --download          Download all content, but do not actually install the update\n");
#warning remove user configurable url when alternative exists
	printf("   -u, --url=[URL]         RFC-3986 encoded url for version string and content file downloads\n");
#warning remove user configurable content url when alternative exists
	printf("   -c, --contenturl=[URL]  RFC-3986 encoded url for content file downloads\n");
#warning remove user configurable version url when alternative exists
	printf("   -v, --versionurl=[URL]  RFC-3986 encoded url for version string download\n");
	printf("   -F, --format=[staging,1,2,etc.]  the format suffix for version file downloads\n");
	printf("   -V, --verbose           Increase verbosity of log and console messages\n");
	printf("   -q, --quiet             Silent run, do not print any ouput to the screen\n");
	printf("\n");
}

static bool parse_options(int argc, char **argv)
{
	int opt;

	//set default initial values
	set_format_string(NULL);

	while ((opt = getopt_long(argc, argv, "hdu:c:v:F:Vq", prog_opts, NULL)) != -1) {
		switch (opt) {
		case '?':
		case 'h':
			print_help(argv[0]);
			return false;
		case 'd':
			download_only = true;
			break;
		case 'u':
			if (!optarg) {
				printf("Invalid --url argument\n\n");
				goto err;
			}
			if (version_server_urls[0])
				free(version_server_urls[0]);
			if (content_server_urls[0])
				free(content_server_urls[0]);
			if (asprintf(&version_server_urls[0], "%s", optarg) < 0)
				abort();
			if (asprintf(&content_server_urls[0], "%s", optarg) < 0)
				abort();
			break;
		case 'c':
			if (!optarg) {
				printf("Invalid --contenturl argument\n\n");
				goto err;
			}
			if (content_server_urls[0])
				free(content_server_urls[0]);
			if (asprintf(&content_server_urls[0], "%s", optarg) < 0)
				abort();
			break;
		case 'v':
			if (!optarg) {
				printf("Invalid --versionurl argument\n\n");
				goto err;
			}
			if (version_server_urls[0])
				free(version_server_urls[0]);
			if (asprintf(&version_server_urls[0], "%s", optarg) < 0)
				abort();
			break;
		case 'F':
			if (!optarg || !set_format_string(optarg)) {
				printf("Invalid --format argument\n\n");
				goto err;
			}
			break;
		case 'V':
			if (verbose < 0)
				break;
			else
				verbose++;
			break;
		case 'q':
			verbose = -1;
			break;
		default:
			printf("Unrecognized option\n\n");
			goto err;
		}
	}

	if (!init_globals())
		return false;

	return true;
err:
	print_help(argv[0]);
	return false;
}

static void banner(void)
{
	printf(PACKAGE " software update " VERSION "\n");
	printf("   Copyright (C) 2012-2015 Intel Corporation\n");
	printf("   bsdiff portions Copyright Colin Percival, see COPYING file for details\n");
	printf("\n");
}

static void download_progress(float done, float total)
{
	if (done < 1024) {
		log_stdout_extraverbose("Downloading pack: %3.2f/%3.2f Bytes...", done, total);
	} else if (done < 1024*1024) {
		log_stdout_extraverbose("Downloading pack: %3.2f/%3.2f KB...", done/1024, total/1024);
	} else {
		log_stdout_extraverbose("Downloading pack: %3.2f/%3.2f MB...", done/1024/1024, total/1024/1024);
	}
}

static void progress_cb(struct progress_msg *progress_msg)
{
	static progress_msg_id last_msg_id = PROGRESS_MSG_NONE;

	switch (progress_msg->msg_id){
	case PROGRESS_MSG_START:
		log_basic("Update started.\n");
		break;
	case PROGRESS_MSG_CHECK_DISKSPACE:
		log_basic("Checking diskspace.\n");
		break;
	case PROGRESS_MSG_GET_SERVER_VERSION:
		log_basic("Querying server version.\n");
		break;
	case PROGRESS_MSG_LOAD_CURRENT_MANIFEST:
		log_basic("Querying current manifest.\n");
		break;
	case PROGRESS_MSG_LOAD_SERVER_MANIFEST:
		log_basic("Querying server manifest.\n");
		break;
	case PROGRESS_MSG_DOWNLOAD_PACK:
		if (last_msg_id == PROGRESS_MSG_DOWNLOAD_PACK) {
			log_stdout_extraverbose("\r\033[K");
			download_progress((float) progress_msg->size_done, (float) progress_msg->size_total);
		}
		fflush(stdout);
		break;
	case PROGRESS_MSG_EXTRACTING_PACK:
		if (last_msg_id == PROGRESS_MSG_DOWNLOAD_PACK) {
			log_stdout_extraverbose("\r\033[K");
			download_progress((float) progress_msg->size_done, (float) progress_msg->size_total);
		}
		fflush(stdout);
		log_stdout("Extracting pack.\n");
		break;
	case PROGRESS_MSG_VERIFY_STAGING_PRE:
		break;
	case PROGRESS_MSG_DOWNLOAD_DELTA:
		break;
	case PROGRESS_MSG_DOWNLOAD_FULL:
		break;
	case PROGRESS_MSG_STAGING:
		break;
	case PROGRESS_MSG_VERIFY_STAGING_POST:
		break;
	case PROGRESS_MSG_SNAPSHOT:
		log_basic("Creating snapshot.\n");
		break;
	case PROGRESS_MSG_VERIFY_SNAPSHOT:
		break;
	case PROGRESS_MSG_UPDATE_ESP:
		log_stdout("Copying ESP files.\n");
		break;
	case PROGRESS_MSG_SYNCING:
		log_stdout("Syncing...\n");
		break;
	case PROGRESS_MSG_UPDATED:
		log_basic("Update was applied.\n");
		break;
	case PROGRESS_MSG_DONE:
		log_basic("Update exiting.\n");
		break;
	default:
		log_basic("Unknown progress msg id %d\n", progress_msg->msg_id);
		break;
	}

	last_msg_id = progress_msg->msg_id;
}

int main(int argc, char **argv)
{
	int ret;
	verbose = 1;
	banner();

	if (!parse_options(argc, argv)) {
		free_globals();
		return EXIT_FAILURE;
	}

	progress_register_cb(progress_cb);
	progress_set_options(1024*1024, 1000);

	ret = main_update();
	free_globals();
	return ret;
}
