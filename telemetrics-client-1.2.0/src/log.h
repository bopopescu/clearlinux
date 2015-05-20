/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#pragma once

#include <stdio.h>

#include "config.h"

#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
#include <systemd/sd-journal.h>
#else
#include <errno.h>
#include <string.h>
#include <syslog.h>
#endif

#ifdef DEBUG
#define telem_debug(...) do { \
                (telem_log(LOG_DEBUG, "%s():[%d]", __func__, __LINE__), \
                 telem_log(LOG_DEBUG, __VA_ARGS__)); \
} while (0);
#else
#define telem_debug(...) do {} while (0);
#endif /* DEBUG */

/*
   Acceptable values for priority:

   LOG_EMERG      system is unusable
   LOG_ALERT      action must be taken immediately
   LOG_CRIT       critical conditions
   LOG_ERR        error conditions
   LOG_WARNING    warning conditions
   LOG_NOTICE     normal, but significant, condition
   LOG_INFO       informational message
   LOG_DEBUG      debug-level message
 */

#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
#define telem_log(priority, ...) sd_journal_print(priority, __VA_ARGS__)
#else
#define telem_log(priority, ...) syslog(priority, __VA_ARGS__)
#endif

#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
#define telem_perror(msg) sd_journal_perror(msg)
#else
#define telem_perror(msg) syslog(LOG_ERR, msg ": %s", strerror(errno))
#endif

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
