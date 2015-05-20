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
 *         Timothy C. Pepper <timothy.c.pepper@linux.intel.com>
 *         Tudor Marcu <tudor.marcu@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <swupd.h>

static void update_kernel(void)
{
	int ret;

	log_basic("** Progress: Calling kernel_update\n");
	ret = system("kernel_updater.sh");
	if (ret != 0)
		LOG_ERROR(NULL, "kernel_update failed", class_scripts, "%d", ret);
}

static void update_bootloader(void)
{
	int ret;

	log_basic("** Progress: Calling gummiboot_updaters\n");
	ret = system("gummiboot_updaters.sh");
	if (ret != 0)
		LOG_ERROR(NULL, "gummiboot_updaters failed", class_scripts, "%d", ret);
}

static void update_triggers(void)
{
	int ret;

	ret = system("/usr/bin/systemctl daemon-reload");
	if (ret != 0)
		LOG_ERROR(NULL, "systemd daemon reload failed", class_scripts, "%d", ret);
	ret = system("/usr/bin/systemctl restart update-triggers.target");
	if (ret != 0)
		LOG_ERROR(NULL, "systemd update triggers failed", class_scripts, "%d", ret);
}

void run_scripts(void)
{
	LOG_INFO(NULL, "calling update helpers", class_scripts, "");

	if (need_update_boot)
		update_kernel();
	if (need_update_bootloader)
		update_bootloader();

	/* Crudely call post-update hooks after every update, must fix with proper conditions and log output */
	update_triggers();
}
