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
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <errno.h>

#include <swupd.h>

static int current_version = -1;

static const struct option prog_opts[] = {
	{"help", no_argument, 0, 'h'},
	{"manifest", required_argument, 0, 'm'},
	{"path", required_argument, 0, 'p'},
	{"url", required_argument, 0, 'u'},
	{"contenturl", required_argument, 0, 'c'},
	{"versionurl", required_argument, 0, 'v'},
	{"ignore-state", no_argument, 0, 's'},
	{"ignore-boot", no_argument, 0, 'b'},
	{"ignore-orphans", no_argument, 0, 'o'},
	{"fix", no_argument, 0, 'f'},
	{"format", required_argument, 0, 'F'},
	{"verbose", no_argument, 0, 'V'},
	{0, 0, 0, 0}
};

static void print_help(const char *name) {
	printf("Usage:\n");
	printf("   %s [OPTION...]\n\n", basename((char*)name));
	printf("Help Options:\n");
	printf("   -h, --help              Show help options\n\n");
	printf("Application Options:\n");
	printf("   -m, --manifest=M        Verify against manifest version M\n");
	printf("   -p, --path=[PATH...]    Use [PATH...] as the path to verify (eg: a chroot or btrfs subvol\n");
#warning remove user configurable url when alternative exists
	printf("   -u, --url=[URL]         RFC-3986 encoded url for version string and content file downloads\n");
#warning remove user configurable content url when alternative exists
	printf("   -c, --contenturl=[URL]  RFC-3986 encoded url for content file downloads\n");
#warning remove user configurable version url when alternative exists
	printf("   -v, --versionurl=[URL]  RFC-3986 encoded url for version file downloads\n");
	printf("   -s, --ignore-state      Ignore differences in runtime state files\n");
	printf("   -b, --ignore-boot       Ignore differences in boot files\n");
	printf("   -o, --ignore-orphans    Ignore extra local files in managed directories\n");
	printf("   -f, --fix               Fix local issues relative to server manifest (will not modify ignored files)\n");
	printf("   -F, --format=[staging,1,2,etc.]  the format suffix for version file downloads\n");
	printf("   -V, --verbose           Increase verbosity of log and console messages\n");
	printf("\n");
}

static bool parse_options(int argc, char **argv)
{
	int opt;

	//set default initial values
	set_format_string(NULL);

	while ((opt = getopt_long(argc, argv, "hm:p:u:c:v:sbofF:V", prog_opts, NULL)) != -1) {
		switch (opt) {
		case '?':
		case 'h':
			print_help(argv[0]);
			return false;
		case 'm':
			if (sscanf(optarg, "%i", &current_version) != 1) {
				printf("Invalid --manifest argument\n\n");
				goto err;
			}
			break;
		case 'p': /* default empty path_prefix verifies the running OS */
			if (!optarg) {
				printf("Invalid --path argument\n\n");
				goto err;
			}
			if (path_prefix) /* multiple -p options */
				free(path_prefix);
			if (asprintf(&path_prefix, "%s", optarg) <= 0)
				abort();
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
		case 's':
			ignore_state = true;
			break;
		case 'b':
			ignore_boot = true;
			break;
		case 'o':
			ignore_orphans = true;
			break;
		case 'f':
			fix = true;
			break;
		case 'F':
			if (!optarg || !set_format_string(optarg)) {
				printf("Invalid --format argument\n\n");
				goto err;
			}
			break;
		case 'V':
			verbose++;
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
	printf(PACKAGE " software verify " VERSION "\n");
	printf("   Copyright (C) 2012-2015 Intel Corporation\n");
	printf("   bsdiff portions Copyright Colin Percival, see COPYING file for details\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int ret;
	banner();

	if (!parse_options(argc, argv)) {
		free_globals();
		return EXIT_FAILURE;
	}

	ret = main_verify(current_version);
	free_globals();
	return ret;
}
