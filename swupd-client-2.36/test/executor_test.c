/*
 *   Software Updater - client side
 *
 *      Copyright © 2013 Intel Corporation.
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
 *         Tom Keel <thomas.keel@intel.com>
 *
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <executor.h>

#define UNUSED_PARAM  __attribute__ ((__unused__))

#define AN_ARG "My Arg"

// Returns 0 half the time, (2^n) with prob 1/(2^(n+2)) for 1 <= n <= 6.
// Except for the last one...
// Mean value returned is 2.
//
int myrand(void) {
	int r = rand();
	if (r & 0x01) return 0;   // prob = 1/2
	if (r & 0x02) return 1;   // prob = 1/4
	if (r & 0x04) return 2;   // prob = 1/8
	if (r & 0x08) return 4;   // prob = 1/16
	if (r & 0x10) return 8;   // prob = 1/32
	if (r & 0x20) return 16;  // prob = 1/64
	if (r & 0x40) return 32;  // prob = 1/128
	return 64;                // prob = 1/128
}

// Sleep for ms milliseconds (but return immediately if ms is 0).
//
void mysleep(int ms)
{
	if (ms > 0) {
		struct timespec tspec;
		int64_t nsec  = ((int64_t)ms) * 1000000;
		tspec.tv_sec  = (time_t)(nsec / 1000000000);
		tspec.tv_nsec = (long)  (nsec % 1000000000);
		nanosleep(&tspec, NULL);
	}
}

void task_func(void *data)
{
	if (strcmp((char*)data, AN_ARG) != 0) {
		printf("BAD ARG!!!\n");
	}
	mysleep(myrand());
}

int main(int UNUSED_PARAM argc, char UNUSED_PARAM **argv)
{
	int nworkers = 10;
	int qsize = 10;
	bool blocking_submit = true;
	int ntasks = 1000;
	bool blocking_destroy = false;
	int i;
	int nwaiting;
	int ntasksdone;
	char *ok;
	int *waitingsizes = calloc(qsize, sizeof(int));

	printf("Begin nworkers=%d queuesize=%d blocking_submit=%d ntasks=%d blocking_destroy=%d\n",
			nworkers, qsize, blocking_submit, ntasks, blocking_destroy);

	struct executor *e = executor_create(nworkers, qsize, blocking_submit);

	for (i=0; i < ntasks; i++) {
		int nwaiting = executor_submit_task(e, task_func, AN_ARG);
		if (nwaiting < 0) {
			printf("Error %d from submit_task!\n", nwaiting);
			break;
		}
		waitingsizes[nwaiting-1]++;
		mysleep(myrand()/4);
	}
	ntasksdone = i;

	executor_destroy(e, blocking_destroy);

	if (!blocking_destroy) {
		sleep(1); // in case logging in the impl
	}

	for (i=0; i < qsize; i++) {
		printf("Queue size %d on %d submissions.\n", i+1, waitingsizes[i]);
	}

	printf("Executed %d of %d tasks.\n", ntasksdone, ntasks);

	nwaiting = executor_submit_task(e, task_func, AN_ARG);
	ok = (nwaiting == EXECUTOR_ERR_ARGS) ? "CORRECT" : "FAIL!";
	printf("Submitting to destroyed executor returns %d (%s)\n", nwaiting, ok);

	executor_destroy(e, true); // should log an error

	printf("End.\n");
	return 0;
}
