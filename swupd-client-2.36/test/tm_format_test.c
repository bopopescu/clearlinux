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
 *         Sebastien Boeuf <sebastien.boeuf@intel.com>
 *         Timothy C. Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include <swupd.h>

static const int MAX_BUF_SIZE = 10000;
static const char *HUMANSTRING = "TEST_HUMANSTRING";
static const char *FIELD_1 = "TEST_FIELD_1";
static const char *FIELD_2 = "TEST_FIELD_2";
static const char *VALUE_1 = "TEST_VALUE_1";
static const char *VALUE_2 = "TEST_VALUE_2";
static const enum log_class_msg CLASSIFICATION = class_undef;

static int find_pattern_in_file(FILE *file_ptr, char *pattern) {
	char line[MAX_BUF_SIZE];
	int pattern_found = 0;
	while (fgets(line, MAX_BUF_SIZE, file_ptr)) {
		if (strstr(line, pattern)) {
			pattern_found = 1;
			break;
		}
	}
	if (!pattern_found)
		return -1;

	return 0;
}

static int check_tm_format(const char *dir_path) {
	DIR *dir = NULL;
	struct dirent *item;
	struct dirent *item_buf;
	FILE *file = NULL;
	char *filename;
	char *str_test = NULL;
	char *classification = NULL;
	int right_fmt = 0;
	int ret = -1;
	struct stat stat_buf;

	classification = format_classification_message(CLASSIFICATION);
	if (!classification)
		goto exit;

	dir = opendir(dir_path);
	if (!dir) {
		printf("Error : Failed to open input directory %s - %s\n",
				dir_path, strerror(errno));
		goto exit;
	}
	item_buf = readdir(dir);
	while (item_buf && !right_fmt) {
		item = item_buf;
		item_buf = readdir(dir);

		if (!item->d_name)
			continue;

		/* Do not test "." file */
		if (strcmp(item->d_name, ".") == 0)
			continue;

		/* Do not test ".." file */
		if (strcmp(item->d_name, "..") == 0)
			continue;

		if (asprintf(&filename, "%s/%s", dir_path, item->d_name) < 0)
			goto exit;

		stat(filename, &stat_buf);
		/* Do not test file whether it is a directory */
		if (!S_ISREG(stat_buf.st_mode)) {
			free(filename);
			continue;
		}

		file = fopen(filename, "r");
		free(filename);
		if (!file) {
			printf("Error : Failed to open entry file %s - %s\n",
					item->d_name, strerror(errno));
			continue;
		}

		/* Check for a file including PACKAGE_NAME pattern */
		if (find_pattern_in_file(file, classification) == 0) {
			/*
			 * As PACKAGE_NAME has been found in this file, we must find
			 * humantring, field_1 and field_2 as expected. Otherwise it
			 * will be considered as an incorrect format.
			*/
			if (asprintf(&str_test, "Humanstring: \"%s\"", HUMANSTRING) < 0)
				goto exit;
			if (find_pattern_in_file(file, str_test) != 0)
				goto exit;
			free(str_test);
			str_test = NULL;

			if (asprintf(&str_test, "%s: %s", FIELD_1, VALUE_1) < 0)
				goto exit;
			if (find_pattern_in_file(file, str_test) != 0)
				goto exit;
			free(str_test);
			str_test = NULL;

			if (asprintf(&str_test, "%s: %s", FIELD_2, VALUE_2) < 0)
				goto exit;
			if (find_pattern_in_file(file, str_test) != 0)
				goto exit;
			free(str_test);
			str_test = NULL;

			right_fmt = 1;
		}

		fclose(file);
		file = NULL;
	}

	if (right_fmt)
		ret = 0;

exit:
	if (file)
		fclose(file);
	if (dir)
		closedir(dir);
	free(str_test);
	free(classification);
	return ret;
}

int main(int argc, char *argv[])
{
	char *cmd = NULL;
	int ret = -1;
	const char *dir_path = "/data/telemetry/queue";

	/* Remove all records */
	if (asprintf(&cmd, "rm -rf %s/*", dir_path) < 0)
		goto exit;
	if (system(cmd) != 0)
		goto exit;

	/*
	 * Call LOG_WARN or LOG_ERROR macro to send a specific message
	 * in order to generate a Telemetry record
	 */
	LOG_ERROR(NULL, HUMANSTRING, CLASSIFICATION, "\\*%s=\"%s\",%s=\"%s\"*\\",
			FIELD_1, VALUE_1, FIELD_2, VALUE_2);

	/*
	 * For each record, check classification is correct
	 * and check we can find our specific message and fields
	 */
	if (check_tm_format(dir_path) == 0) {
		printf("Telemetry formatting = OK\n");
		ret = 0;
	} else
		printf("Telemetry formatting = FAIL\n");

exit:
	free(cmd);
	return ret;
}
