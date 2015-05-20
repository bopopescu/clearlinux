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
 *         Eric Lapuyade <eric.lapuyade@intel.com>
 *
 */

#define _GNU_SOURCE

#include <time.h>
#include <swupd.h>
#include "progress.h"

static progress_cb_t s_progress_cb = NULL;
static struct progress_msg s_progress_msg;

static size_t s_min_size_increment;
static size_t s_last_size_notified;

static int s_min_mseconds_update;
struct timeval s_last_notification_time;

void progress_register_cb(progress_cb_t progress_cb)
{
	s_progress_cb = progress_cb;

	s_min_size_increment = 1024*1024;
	s_last_size_notified = 0;

	s_min_mseconds_update = 1000;
	s_last_notification_time.tv_sec = 0;
	s_last_notification_time.tv_usec = 0;
}

void progress_set_options(size_t min_size_increment, int min_mseconds_update)
{
	if (s_min_size_increment != min_size_increment) {
		s_min_size_increment = min_size_increment;
		s_last_size_notified = 0;
	}

	if (s_min_mseconds_update != min_mseconds_update) {
		s_min_mseconds_update = min_mseconds_update;
		s_last_notification_time.tv_sec = 0;
		s_last_notification_time.tv_usec = 0;
	}
}

static void progress_notify_step(progress_msg_id msg_id)
{
	if (s_progress_cb) {
		s_progress_msg.msg_id = msg_id;

		s_progress_cb(&s_progress_msg);

		gettimeofday(&s_last_notification_time, NULL);

		if (msg_id == PROGRESS_MSG_DONE)
			s_progress_cb = NULL;
	}
}

void progress_step(progress_msg_id msg_id)
{
	progress_notify_step(msg_id);
}

static bool progress_delay_elapsed(void)
{
	struct timeval delay_since_last_notif;

	/* always true if delay is disabled */
	if (s_min_mseconds_update == 0)
		return true;

	gettimeofday(&delay_since_last_notif, NULL);
	timersub(&delay_since_last_notif, &s_last_notification_time, &delay_since_last_notif);

	if ((delay_since_last_notif.tv_sec * 1000) +
		(delay_since_last_notif.tv_usec / 1000) > s_min_mseconds_update)
		return true;

	return false;
}

void progress_step_ongoing(progress_msg_id msg_id, size_t size_done,
				size_t size_total)
{
	/* first and last steps are always notified */
	if (size_done > 0 && size_done != size_total) {
		if (!progress_delay_elapsed())
			return;

		/* always report if there is no min size increment */
		if ((s_min_size_increment != 0) &&
			(size_done < s_last_size_notified + s_min_size_increment))
			return;
	}

	s_progress_msg.size_done = size_done;
	s_progress_msg.size_total = size_total;

	progress_notify_step(msg_id);

	s_last_size_notified = size_done;
}
