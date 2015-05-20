/*
 *   Software Updater - client side
 *
 *      Copyright (c) 2012-2015 Intel Corporation.
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
 *         Jaime A. Garcia <jaime.garcia.naranjo@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>

#include <swupd.h>

#define MODE_RW_O (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define VERIFY_NOPICKY 0

static char *bundle = NULL;

static void print_help(const char *name) {
	printf("Usage:\n");
	printf("   %s bundlename\n\n", basename((char*)name));
	printf("Help Options:\n");
	printf("   -h, --help              Show help options\n\n");
	printf("\n");
}

static bool parse_options(int argc, char **argv)
{ /* Let's keep the option arg parsing straight until we need more than one arg to be collected */
	if (argc != 2) {
		print_help(argv[0]);
		return false;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0
	    || strcmp(argv[1], "?") == 0) {
		print_help(argv[0]);
		return false;
	}

	if (asprintf(&bundle, "%s", argv[1]) <= 0) {
		/* No worth initializing LOG only to print this before we can go to main */
		printf("ERROR: allocating memory, exiting now\n\n");
		abort();
	}

	return true;
}

static void banner(void)
{
	printf(PACKAGE " bundle adder " VERSION "\n");
	printf("   Copyright (C) 2012-2015 Intel Corporation\n");
	printf("   bsdiff portions Copyright Colin Percival, see COPYING file for details\n");
	printf("\n");
}

static int bundle_add()
{
	char *filename = NULL;
	int f = -1;
	int ret = 0;

	if (!init_globals()) {
		free_globals();
		return -1;
	}

	init_log_stdout();
	verbose = 1;

	if (asprintf(&filename, "%s/%s", BUNDLES_DIR, bundle) <= 0) {
		log_stdout("Error: allocating memory, exiting now\n\n");
		abort();
	}

	f = open(filename, O_WRONLY | O_CREAT | O_NONBLOCK | O_NOCTTY, MODE_RW_O);
	if (f < 0) {
		log_stdout("cannot create bundle file %s exiting now\n\n", filename);
		ret = EXIT_FAILURE;
		goto out;
	}

	close(f);
	log_stdout("bundle %s added, trying to install it now\n", filename);
	verbose = 0;
	ret = verify_fix(VERIFY_NOPICKY);

out:
	free_globals();
	free(filename);
	free(bundle);
	return ret;
}

int main(int argc, char **argv)
{
	banner();
	check_root();

	if (!parse_options(argc, argv))
		return EXIT_FAILURE;

	return  bundle_add();
}
