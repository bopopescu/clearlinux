/*
 *   Software Updater - client side
 *
 *      Copyright © 2014 Intel Corporation.
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
 *         Frederic PAUT <frederic.paut@linux.intel.com>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <swupd.h>


static void print_usage(void)
{
	printf("Display available disk space on selected partition [esp / btrfs] \n");
	printf("Usage:\n");
	printf("	swupd_get_diskspace [esp | btrfs] \n");
	exit(1);
}

int main(int argc, char *argv[])
{
	uint64_t diskspace = 0;

	if (argc > 1) {
		if (strcmp(argv[1], "btrfs") == 0)
			diskspace = get_available_btrfs_space();
		else if (strcmp(argv[1], "esp") == 0)
			diskspace = get_available_esp_space();
		else
			print_usage();

		printf("%llu\n", (unsigned long long) diskspace);

	} else {
		print_usage();
	}

	exit(0);
}
