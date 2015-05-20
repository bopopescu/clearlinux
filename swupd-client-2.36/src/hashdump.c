/*
 *   Software Updater - client side
 *
 *      Copyright © 2013-2015 Intel Corporation.
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
 *         Timothy C. Pepper <timothy.c.pepper@linux.intel.com>
 *         cguiraud <christophe.guiraud@intel.com>
 *
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "swupd.h"

/* outputs the hash of a file */

static struct option opts[] = {
	{ "no-xattrs", 0, NULL, 'n' },
	{ "basepath",  1, NULL, 'b' },
	{ "help",      0, NULL, 'h' },
	{ 0, 0, NULL, 0 }
};

void usage(void) {
printf(
"   USAGE:\n"
"      swupd_hashdump filename [--basepath prefix] [--no-xattrs]\n"
"      swupd_hashdump /system/xbin/timeinfo --basepath /var/lib/update/image/60/base --no-xattrs\n"
"\n"
"   The basepath optional argument is a leading path so that chroot's can be supported.\n"
"   The filename is the name as it would appear in a Manifest file.\n");
}

int main(int argc, char **argv)
{
	char *hash = NULL;
	struct file* file;
	char *fullname;
	struct stat stat;

	file = calloc(1, sizeof(struct file));
	if (!file)
		abort();

	file->use_xattrs = true;

	while (1) {
		int c;
		int i;

		c = getopt_long(argc, argv, "nb:h", opts, &i);
		if (c == -1) {
			break;
		}

		switch(c) {
		case 'n':
			file->use_xattrs = false;
			break;
		case 'b':
			path_prefix = strdup(optarg);
			break;
		case 'h':
			usage();
			exit(0);
			break;
		default:
			usage();
			exit(-1);
			break;
		}
	}

	if (!init_globals()) {
		free_globals();
		exit(-1);
	}

	if (optind >= argc) {
		usage();
		free_globals();
		exit(-1);
	}

	// mk_full_filename expects absolute filenames (eg: from Manifest)
	if (argv[optind][0] == '/') {
		file->filename = strdup(argv[optind]);
		if (!file->filename)
			abort();
	} else {
		if (asprintf(&file->filename, "/%s", argv[optind]) <= 0)
			abort();
	}

	printf("Calculating hash %s xattrs for: (%s) ... %s\n",
	       (file->use_xattrs ? "with":"without"), path_prefix, file->filename);
	fullname = mk_full_filename(path_prefix, file->filename);
	printf("fullname=%s\n", fullname);
	hash = compute_hash(file, fullname);
	if ((hash == NULL) || (lstat(fullname, &stat) == -1)) {
		printf("compute_hash() failed\n");
	} else {
		printf("%s\n", hash);
		if (S_ISDIR(stat.st_mode)) {
			if (is_directory_mounted(fullname))
				printf("!! dumped hash might not match a manifest hash because a mount is active\n");
		}
	}

	free(fullname);
	free(hash);
	free(file->filename);
	free(file);
	free_globals();
	return 0;
}
