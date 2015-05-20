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
 *         Timothy C. Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <swupd.h>

/* trailing slash is to indicate dir itself is expected to exist, but
 * contents are ignored */
static void config_file_heuristics(struct file *file)
{
	if (strncmp(file->filename, "/etc/", 5) == 0) {
		file->is_config = 1;
	}
}

/* trailing slash is to indicate dir itself is expected to exist, but
 * contents are ignored */
bool is_state(char *filename)
{
	if (is_directory_mounted(filename))
		return true;

	if (is_under_mounted_directory(filename))
		return true;

	if ((strlen(filename) == 14) && (strncmp(filename, "/usr/src/debug", 14) == 0)) {
		return false;
	}

	if ((strncmp(filename, "/data", 5) == 0) ||
	    (strncmp(filename, "/dev/", 5) == 0) ||
	    (strncmp(filename, "/home/", 6) == 0) ||
	    (strncmp(filename, "/lost+found", 11) == 0) ||
	    (strncmp(filename, "/proc/", 6) == 0) ||
	    (strncmp(filename, "/root/", 6) == 0) ||
	    (strncmp(filename, "/run/", 5) == 0) ||
	    (strncmp(filename, "/sys/", 5) == 0) ||
	    (strncmp(filename, "/tmp/", 5) == 0) ||
	    (strncmp(filename, "/usr/src/", 9) == 0) ||
	    (strncmp(filename, "/var/", 5) == 0)) {
		return true;
	}
	return false;
}

static void runtime_state_heuristics(struct file *file)
{
	if (is_state(file->filename))
		file->is_state = 1;
}

static void boot_file_heuristics(struct file *file)
{
	if ((strncmp(file->filename, "/boot/", 6) == 0) ||
	    (strncmp(file->filename, "/usr/lib/modules/", 17) == 0)) {
			file->is_boot = 1;
	}

	if (strncmp(file->filename, "/usr/lib/kernel/", 16) == 0) {
		file->is_boot = 1;
		need_update_boot++;
	}

	if ((strncmp(file->filename, "/usr/lib/gummiboot", 18) == 0) ||
	    (strncmp(file->filename, "/usr/bin/gummiboot", 18) == 0)) {
		file->is_boot = 1;
		need_update_bootloader++;
	}
}

void apply_heuristics(struct file *file)
{
	runtime_state_heuristics(file);
	boot_file_heuristics(file);
	config_file_heuristics(file);
}

bool ignore(struct file *file)
{
	if ((file->is_config) ||
	    (file->is_state) ||
	    is_state(file->filename) || // ideally we trust the manifest but short term reapply check here
	    (!fix && ignore_boot && file->is_boot) ||
	    (ignore_orphans && file->is_orphan)) {
		LOG_DEBUG(file, "ignore", class_file_misc, "");
		update_skip++;
		file->do_not_update = 1;
		return true;
	}

	return false;
}
