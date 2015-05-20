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
 *         Regis Merlino <regis.merlino@intel.com>
 *         Sebastien Boeuf <sebastien.boeuf@intel.com>
 *
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <swupd.h>

#ifdef SWUPD_WITH_TELEMETRY
#include <telemetry.h>
static enum tm_event_severity priority_to_tm(enum log_priority priority)
{
	switch (priority) {
	case log_info:
		return TM_INFO;
	case log_debug:
		return TM_DEBUG;
	case log_warning:
		return TM_WARNING;
	case log_error:
		return TM_ERROR;
	default:
		abort();
	}

	return TM_ERROR;
}

int tm_send_record(enum log_priority priority, const char *classification, const void *payload, size_t len)
{
	return tm_record(priority_to_tm(priority), classification, payload, len);
}

int __tm_send_record(enum log_priority priority, char *msg, enum log_class_msg class_msg,
			char *filename, int linenr, const char *fmt, ...)
{
	char *buf;
	char *classification = NULL;
	int ret;
	va_list ap;

	va_start(ap, fmt);
	buf = format_log_message(TM_TYPE, priority, NULL, msg, filename, linenr, fmt, ap);
	va_end(ap);

	classification = format_classification_message(class_msg);

	ret = tm_send_record(priority, classification, buf, strlen(buf));

	free(classification);

	free(buf);

	return ret;
}
#endif
