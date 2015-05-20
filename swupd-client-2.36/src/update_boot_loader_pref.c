/*
 *   Software Update BootLoader Pref - client side
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
 *         Christophe Guiraud <christophe.guiraud@intel.com>
 *         Sebastien Boeuf <sebastien.boeuf@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>

#include <swupd.h>
#include <telemetry.h>

int current_version = -1;
int next_version = -1;

bool boot_repair = false;
bool boot_prior = false;
bool dump = false;
struct list *next_repair = NULL;

static enum repair_reason repair_reason;

static const struct option prog_opts[] = {
	{"boot_repair", no_argument, 0, 'r'},
	{"boot_prior", no_argument, 0, 'p'},
	{"next_version", required_argument, 0, 'n'},
	{"next_repair_boot_check_failed", required_argument, 0, 'b'},
	{"next_repair_integrity_check_failed", required_argument, 0, 'i'},
	{"next_repair_update_failed", required_argument, 0, 'u'},
	{"dump", no_argument, 0, 'd'},
	{"help", no_argument, 0, 'h'},
	{0, 0, 0, 0}
};

static void print_help(const char *name) {
	printf("Usage:\n");
	printf("   %s [OPTION...]\n\n", basename((char*)name));
	printf("Help Options:\n");
	printf("   -h, --help                                     Show help options\n\n");
	printf("Application Options:\n");
	printf("   -r, --boot_repair                              Do system boot status check and fallback to repair OS\n");
	printf("   -p, --boot_prior                               Do system boot status check and fallback to prior OS\n");
	printf("   -n, --next_version=N                           Set next boot to version N\n");
	printf("   -b, --next_repair_boot_check_failed=N          Set next boot to repair with 'Boot Check Failed' reason on version N\n");
	printf("   -i, --next_repair_integrity_check_failed=N,... Set next boot to repair with 'Integrity Check Failed' reason on version(s) N\n");
	printf("   -u, --next_repair_update_failed=N,M            Set next boot to repair with 'Update Failed' reason from version N to M\n");
	printf("   -d, --dump                                     Dump EFI variables contents if they exist\n");
	printf("Examples:\n");
	printf("   %s -r\n", basename((char*)name));
	printf("   %s -p\n", basename((char*)name));
	printf("   %s -n100\n", basename((char*)name));
	printf("   %s -b100\n", basename((char*)name));
	printf("   %s -i100\n", basename((char*)name));
	printf("   %s -i100,110,120\n", basename((char*)name));
	printf("   %s -u100,110\n", basename((char*)name));
	printf("   %s -d\n", basename((char*)name));
	printf("\n");
}

static struct list *parse_version_list(char *csv_version_str)
{
	char *token;
	int version;
	struct list *list = NULL;

	if (!csv_version_str)
		return NULL;

	token = strtok(csv_version_str, ",");
	while (token != NULL) {
		if (sscanf(token, "%i", &version) != 1) {
			list_free_list(list);
			return NULL;
		}
		list = list_prepend_data(list, (void *)((intptr_t)version));
		token = strtok(NULL, ",");
	}

	return list;
}

static bool parse_options(int argc, char **argv)
{
	int opt;
	int opt_count = 0;
	unsigned int count;
	int tmp_val;

	while ((opt = getopt_long(argc, argv, "hrpn:b:i:u:d", prog_opts, NULL)) != -1) {
		switch (opt) {
		case '?':
		case 'h':
			goto err;
		case 'r':
			boot_repair = true;
			break;
		case 'p':
			boot_prior = true;
			break;
		case 'n':
			if (sscanf(optarg, "%i", &next_version) != 1) {
				printf("Invalid --next_version argument\n\n");
				goto err;
			}
			break;
		case 'b':
			next_repair = parse_version_list(optarg);
			count = list_len(next_repair);
			if (count != 1) {
				printf("Invalid --next_repair_boot_check_failed argument\n\n");
				list_free_list(next_repair);
				goto err;
			}
			repair_reason = repair_boot_check_failure;
			break;
		case 'i':
			next_repair = parse_version_list(optarg);
			count = list_len(next_repair);
			if ((count == 0) || (count > VERIFY_FAILED_MAX_VERSIONS_COUNT)) {
				printf("Invalid --next_repair_integrity_check_failed argument\n\n");
				list_free_list(next_repair);
				goto err;
			}
			repair_reason = repair_verify_failure;
			break;
		case 'u':
			next_repair = parse_version_list(optarg);
			count = list_len(next_repair);
			if (count != 2) {
				printf("Invalid --next_repair_update_failed argument\n\n");
				list_free_list(next_repair);
				goto err;
			}
			/* switch values to have: to,from*/
			tmp_val = (int)next_repair->data;
			next_repair->data = next_repair->next->data;
			next_repair->next->data = (void *)((intptr_t)tmp_val);
			repair_reason = repair_update_failure;
			break;
		case 'd':
			dump = true;
			break;
		default:
			printf("Unrecognized option\n\n");
			goto err;
		}

		if (opt_count > 1) {
			printf("Invalid options\n\n");
			goto err;
		}
		opt_count++;
	}

	if (opt_count == 0)
		goto err;

	return true;
err:
	print_help(argv[0]);
	return false;
}

static void banner(void)
{
	printf(SWUPD_PACKAGE "software update boot loader pref " SWUPD_VERSION "\n");
	/* this runs via a service at each boot of StarPeak to indicate
	 * successful boot of the new snapshot and mark that new snapshot
	 * as preferred */
	LOG_INFO(NULL, "StarPeak startup", class_bootloader, "version " SWUPD_VERSION);
	TM_SEND_RECORD(log_info, "StarPeak startup", class_bootloader, "version " SWUPD_VERSION);
	printf("   Copyright (C) 2014-2015 Intel Corporation\n");
	printf("\n");
}

/* try to log build version at first boot for product stats */
static void try_log_first_boot(void)
{
	struct stat statbuf;
	int fd;
	char *buf;
	char *classification = NULL;
	int ret;

	fd = open("/system/etc/buildstamp", O_RDONLY);
	if (fd == -1)
		return;

	if (fstat(fd, &statbuf) != -1)
		goto close_out;

	buf = malloc(statbuf.st_size);

	if (buf == NULL)
		goto close_out;

	ret = read(fd, buf, statbuf.st_size);
	if (ret != statbuf.st_size)
		goto free_out;

	classification = format_classification_message(class_bootloader);

	tm_send_record(log_info, classification, buf, statbuf.st_size);

free_out:
	free(classification);
	free(buf);
close_out:
	close(fd);
}

static int do_update_bootloader_pref(void)
{
	int result = 0;

	if (boot_prior) {
		try_log_first_boot();
		result = efivar_bootloader_boot_check(current_version, false);
	} else if (boot_repair) {
		result = efivar_bootloader_boot_check(current_version, true);
	} else if (next_version != -1) {
		result = efivar_bootloader_set_next_boot_to_version(next_version);
	} else if (next_repair) {
		result = efivar_bootloader_set_next_boot_to_repair(repair_reason,
								   next_repair);
		list_free_list(next_repair);
	}

	if (result < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	int ret = EXIT_FAILURE;

	banner();

	//verbose++;

	if (!parse_options(argc, argv))
		return ret;

	check_root();

	if (dump) {
		efivar_bootloader_dump();
		printf("\n");
		return EXIT_SUCCESS;
	}

	init_log();

	/* insure current_version is set */
	if (current_version == -1)
		current_version = read_version_from_subvol_file("");

	ret = do_update_bootloader_pref();

	close_log(ret, current_version, 0, log_bootloader_pref);

	return ret;
}
