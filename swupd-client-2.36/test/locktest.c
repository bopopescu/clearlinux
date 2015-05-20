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
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "swupd.h"
#define NUM_THREADS 4

void work(void)
{
	int lock_fd;
	pid_t pid = getpid();
	printf("pid %d started\n", pid);

	lock_fd = p_lockfile();
	if (lock_fd == -1) {
		printf("pid %d unable to acquire lock\n", pid);
		return;
	}

	printf("pid %d acquired lock\n", pid);

	printf("pid %d going to sleep\n", pid);
	sleep(4);
	printf("pid %d back from sleep\n", pid);

	v_lockfile(lock_fd);
	printf("pid %d lock released\n\n", pid);

	return;
}

int main(int UNUSED_PARAM argc, char UNUSED_PARAM **argv)
{
	int ret = EXIT_FAILURE, t_cnt;
	pid_t pid[NUM_THREADS];

	for (t_cnt = 0; t_cnt < NUM_THREADS; t_cnt++) {
		pid[t_cnt] = fork();

		if (pid[t_cnt] == 0) {
			work();
			return EXIT_SUCCESS;
		}
	}
	for (t_cnt = 0; t_cnt < NUM_THREADS; t_cnt++) {
		wait(&ret);
	}

	return ret;
}
