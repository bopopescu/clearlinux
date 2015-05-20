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
 *         Sebastien Boeuf <sebastien.boeuf@intel.com>
 *
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <swupd.h>

static FILE *logfile;

static struct timeval start_time;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_log(void)
{
	int err;

#warning remove this mkdir and local storage of logfile when STPK-1391 is implemented
	err = mkdir(LOG_DIR, 0700);
	if (err && (errno != EEXIST)) {
		LOG_WARN(NULL, "failed to create log dir", class_file_io, "\\*log_dir=\"%s\"*\\", LOG_DIR);
		goto out;
	}

	logfile = fopen(LOG_DIR "/swupd-update.log", "w");
	if (!logfile) {
		logfile = stdout;
		LOG_WARN(NULL, "log fopen failed", class_file_io, "");
	}

out:
	LOG_INFO(NULL, "swupd version", class_undef, "\\*codeversion=\"%s\"*\\", VERSION);
	gettimeofday(&start_time, NULL);
}
void init_log_stdout(void)
{
	logfile = stdout;
	gettimeofday(&start_time, NULL);
}

static const char *log_priority_to_text(enum log_priority priority)
{
	switch(priority) {
	case log_info:
		return "   INFO  ";
	case log_debug:
		return "   DEBUG ";
	case log_warning:
		return " * WARN  ";
	case log_error:
		return "** ERROR ";
	default:
		abort();
	}

	return NULL;
}

static const char *log_class_msg_to_text(enum log_class_msg class_msg)
{
	switch(class_msg) {
	case class_undef:
		return "undefined";
	case class_file_io:
		return "fileio";
	case class_file_compression:
		return "filecompression";
	case class_file_misc:
		return "filemiscellaneous";
	case class_mnt_pt:
		return "mountpoint";
	case class_btrfs_mnt_pt:
		return "btrfsmountpoint";
	case class_esp_mnt_pt:
		return "espmountpoint";
	case class_bootloader:
		return "bootloader";
	case class_manifest:
		return "manifest";
	case class_curl:
		return "curl";
	case class_disk_sp:
		return "diskspace";
	case class_xattrs:
		return "xattrs";
	case class_security:
		return "security";
	case class_stats:
		return "statistics";
	case class_mem_alloc:
		return "memoryallocation";
	case class_delta:
		return "delta";
	case class_thread:
		return "thread";
	case class_efi:
		return "efi";
	case class_hash:
		return "hash";
	case class_permission:
		return "permission";
	case class_subscription:
		return "subscription";
	case class_sync:
		return "synchronisation";
	case class_version:
		return "version";
	case class_url:
		return "url";
	case class_ui:
		return "ui";
	case class_scripts:
		return "scripts";
	default:
		return "unknown_class_msg";
	}
}

/*
 * Return a new string consisting of a copy of str modified by
 * replacing all occurrences of targ with repl.
 *
 * Example :
 * 	str     = "field = value"
 * 	targ    = "="
 * 	repl    = "<=>"
 * 	Result  = "field <=> value"
 *
 */
static char *format_str_replace(char *str, char *targ, char *repl)
{
	char *new_str = NULL;
	char *new_str_pos;
	char *str_pos;
	char *targ_pos;
	int nb_targ_found = 0;

	if (!str || !targ || !*targ || !repl) {
		return NULL;
	}
	str_pos = str;

	int targ_len = strlen(targ);
	while ((str_pos = strstr(str_pos, targ)) != NULL) {
		nb_targ_found++;
		str_pos += targ_len;
	}

	if (!nb_targ_found) {
		return strdup(str);
	}

	int repl_len = strlen(repl);
	new_str = (char *) malloc(strlen(str) + (nb_targ_found * (repl_len - targ_len)) + 1);
	if (new_str == NULL) {
		return NULL;
	}
	new_str_pos = new_str;

	str_pos = str;
	while ((targ_pos = strstr(str_pos, targ)) != NULL) {
		int len_to_cpy = targ_pos - str_pos;
		memcpy(new_str_pos, str_pos, len_to_cpy);
		new_str_pos += len_to_cpy;
		memcpy(new_str_pos, repl, repl_len);
		new_str_pos += repl_len;
		str_pos = targ_pos + targ_len;
	}
	*new_str_pos = '\0';
	strcpy(new_str_pos, str_pos);

	return new_str;
}

/*
 * Return a new string consisting of a copy of str modified by replacing
 * all occurrences of targ with repl, specifying the replacement has to
 * be done outside delimiters.
 *
 * Example :
 * 	str     = "field1=/value1=10/"
 * 	targ    = "="
 * 	repl    = " <=> "
 * 	delim   = "/"
 * 	Result = "field1=/value1 <=> 10/"
 *
 */
static char *format_str_replace_out_delim(char *str, char *targ, char *repl, char *delim)
{
	char *new_str = NULL;
	char *new_str_pos;
	char *str_pos;
	char *targ_pos;
	char *delim_pos;
	int delim_found = 1;
	int pos_between_delim = 0;
	int nb_targ_found = 0;

	if (!str || !targ || !*targ || !repl || !delim) {
		return NULL;
	}

	if (!*delim) {
		return format_str_replace(str, targ, repl);
	}

	str_pos = str;
	int str_len = strlen(str);
	int targ_len = strlen(targ);
	int repl_len = strlen(repl);
	int delim_len = strlen(delim);
	while (*str_pos) {
		if ((delim_pos = strstr(str_pos, delim)) == NULL) {
			delim_found = 0;
			delim_pos = str + str_len;
		}
		if (!pos_between_delim) {
			if ((str_pos = strstr(str_pos, targ)) == NULL) {
				break;
			}
			while (str_pos && (str_pos < delim_pos)) {
				nb_targ_found++;
				str_pos += targ_len;
				str_pos = strstr(str_pos, targ);
			}
		}
		if (delim_found) {
			str_pos = delim_pos + delim_len;
		} else {
			str_pos = delim_pos;
		}
		pos_between_delim = !pos_between_delim;
	}

	new_str = (char *) malloc(str_len + (nb_targ_found * (repl_len - targ_len)) + 1);
	if (new_str == NULL) {
		return NULL;
	}
	new_str_pos = new_str;

	delim_found = 1;
	pos_between_delim = 0;
	str_pos = str;
	while (*str_pos) {
		if ((delim_pos = strstr(str_pos, delim)) == NULL) {
			delim_found = 0;
			delim_pos = str + str_len;
		}

		int len_to_delim;
		if (!pos_between_delim) {
			while ((targ_pos = strstr(str_pos, targ)) && (targ_pos < delim_pos)) {
				int len_to_targ = targ_pos - str_pos;
				memcpy(new_str_pos, str_pos, len_to_targ);
				new_str_pos += len_to_targ;
				memcpy(new_str_pos, repl, repl_len);
				new_str_pos += repl_len;
				str_pos = targ_pos + targ_len;
			}
		}
		len_to_delim = delim_pos - str_pos;

		memcpy(new_str_pos, str_pos, len_to_delim);
		new_str_pos += len_to_delim;

		if (delim_found) {
			memcpy(new_str_pos, delim, delim_len);
			new_str_pos += delim_len;
			str_pos = delim_pos + delim_len;
		} else {
			str_pos = delim_pos;
		}
		pos_between_delim = !pos_between_delim;
	}
	*new_str_pos = '\0';

	return new_str;
}

static char *get_delimited_substr(char *str, char *delim1, char *delim2)
{
	char *str_modif = NULL;
	char *delim_pos;
	int delim1_len;
	int str_rem_len;

	if (!str || !delim1 || !*delim1 || !delim2 || !*delim2) {
		goto free_str_modif;
	}

	if (!*str) {
		return strdup("");
	}

	str_modif = strdup(str);
	if (str_modif == NULL) {
		goto free_str_modif;
	}

	delim_pos = strstr(str_modif, delim1);
	if (delim_pos == NULL) {
		goto free_str_modif;
	}

	delim1_len = strlen(delim1);
	str_rem_len = strlen(delim_pos + delim1_len) + 1;
	memmove(str_modif, delim_pos + delim1_len, str_rem_len);

	delim_pos = strstr(str_modif, delim2);
	if (delim_pos == NULL) {
		goto free_str_modif;
	}

	*delim_pos = '\0';

	return str_modif;

free_str_modif:
	free(str_modif);
	return NULL;
}

char *format_log_message(int log_type, enum log_priority priority, struct file *file, char *msg,
		char *filename, int linenr, const char *fmt, va_list ap)
{
	char *ret_list = NULL;
	char *ret_list_buf;
	char *ret_list_refmt = NULL;
	char *message;
	struct timeval current_time;
	static struct timeval previous_time;
	struct timeval diff_time;
	char logstring[128];
	char filebuf[PATH_MAXLEN];
	char filebuf2[PATH_MAXLEN];

	gettimeofday(&current_time, NULL);
	timersub(&current_time, &start_time, &current_time);

	logstring[0] = 0;
	if (previous_time.tv_usec || previous_time.tv_sec) {
		timersub(&current_time, &previous_time, &diff_time);
		if (diff_time.tv_sec || diff_time.tv_usec > 1000)
			sprintf(logstring, "%ld.%03ld", diff_time.tv_sec, diff_time.tv_usec / 1000);
	}

	previous_time = current_time;

	/* fmt can be "", so an empty result is OK here */
	if (vasprintf(&ret_list, fmt, ap) < 0)
		abort();

	filebuf[PATH_MAXLEN - 1] = 0;
	filebuf2[0] = 0;
	strncpy(filebuf, filename, PATH_MAXLEN - 1);
	if (file) {
		strncpy(filebuf2, file->filename, PATH_MAXLEN - 1);
		filebuf2[PATH_MAXLEN - 1] = 0;
	}
	while (strlen(filebuf) < 29)
		strcat(filebuf, " ");
	while (strlen(filebuf2) < 30)
		strcat(filebuf2, " ");

	if (log_type == TM_TYPE) {
		int tm_fmt_success = 1;

		if ((ret_list_buf = get_delimited_substr(ret_list, "\\*", "*\\")) == NULL) {
			tm_fmt_success = 0;
		}

		if (tm_fmt_success) {
			ret_list_refmt = ret_list_buf;
			if ((ret_list_buf = format_str_replace_out_delim(ret_list_refmt,
					"=", ": ", "\"")) == NULL) {
				tm_fmt_success = 0;
			}
			free(ret_list_refmt);
		}

		if (tm_fmt_success) {
			ret_list_refmt = ret_list_buf;
			if ((ret_list_buf = format_str_replace_out_delim(ret_list_refmt,
					",", "\n", "\"")) == NULL) {
				tm_fmt_success = 0;
			}
			free(ret_list_refmt);
		}

		if (tm_fmt_success) {
			ret_list_refmt = ret_list_buf;
			if ((ret_list_buf = format_str_replace(ret_list_refmt, "\"", "")) == NULL) {
				tm_fmt_success = 0;
			}
			free(ret_list_refmt);
		}

		if (!tm_fmt_success) {
			if (asprintf(&ret_list_buf, "Unfmt details: %s", ret_list) <= 0)
				abort();
		}

		ret_list_refmt = ret_list_buf;
		if (asprintf(&message, "Filename: %s\nLine: %03i\nHumanstring: \"%s\"\n%s\n",
				filename, linenr, msg, ret_list_refmt) <= 0)
			abort();

		free(ret_list_refmt);
	} else if (log_type == LOG_TYPE) {
		if (asprintf(&message,
				"%9s %3i.%03i %5s %s:%03i\t| %s\t| %s\t| %s\n",
				log_priority_to_text(priority),
				(int) current_time.tv_sec,
				(int) current_time.tv_usec / 1000, logstring,
				filebuf, linenr, filebuf2, msg, ret_list) <= 0)
			abort();
	} else {
		message = strdup("");
	}

	free(ret_list);
	return message;
}

char *format_classification_message(enum log_class_msg class_msg)
{
	char *ret_msg = NULL;
	const char *msg = log_class_msg_to_text(class_msg);

	if (asprintf(&ret_msg, "%s/%s", PACKAGE_NAME, msg) <= 0) {
		abort();
	}

	return ret_msg;
}

/*
 * Rules regarding format of different outputs inside swupd-client component.
 * Final goal is to easily parse these outputs so as to send data into a
 * specific format expected by Telemetry.
 *
 *
 * We are currently sending this type of log messages to Telemetry:
 *
 * <LOG_TYPE> <TIME> <FILE_PATH>:<LINE> | <FILE_PATH_2> | <MESSAGE> | <RET_VALUES>
 * LOG_TYPE : Different values can be INFO, DEBUG, WARN or ERROR.
 * TIME : Time when log message has been emited. ex: 1394226116.685.
 * FILE_PATH : Path to the specific file. ex: swupd-client/src/helpers.c.
 * LINE : Line of the file defined by FILE_PATH. ex: 164.
 * FILE_PATH_2 : Another file path (not empty if struct 'file' is not NULL).
 * MESSAGE : Specifies action performed or error raised. ex: bad update btrfs mount point.
 * RET_VALUES : Values returned by action performed. ex: ret=0, dir=1, uid=0, gid=0, u=700, g=50, o=
 *
 *
 * In order to make easier data formatting before sending to Telemetry,
 * you need to enforce following rules:
 *
 * RET_VALUES :
 * It will be parsed by Telemetry to stock different values corresponding to given fields.
 * Delimiter patterns define where relevant information are written, allowing to add comments beside.
 * 	Content : A list of specific fields with their corresponding values.
 * 	Delimiters : '\*' defines the beginning and '*\' defines the end.
 * 	Format : Space ' ' is allowed only within values. Equal sign '=' is the separator
 * 		between a field and its corresponding value. Comma ',' is the separator
 * 		between field/value pairs. All values must be contained between double quotes "<value>".
 * 	Template : \*<Field1>="<Value1>",<Field2>="<Value2>",...,<FieldN>="<ValueN>"*\
 * 	Comment : Any other information in any other format (excepted delimiters).
 * 	Example : "Comment 1 \*ret="0", path="/update/swupd-update.log"*\ Comment 2"
 *
 */
#ifndef SWUPD_WITH_TELEMETRY
void __log_message(enum log_priority priority, struct file *file, enum log_class_msg UNUSED_PARAM class_msg,
		char *msg, char *filename, int linenr, const char *fmt, ...)
#else /* SWUPD_WITHOUT_TELEMETRY */
void __log_message(enum log_priority priority, struct file *file, enum log_class_msg class_msg,
		char *msg, char *filename, int linenr, const char *fmt, ...)
#endif
{
	char *buf = NULL;
	va_list ap;

	pthread_mutex_lock(&log_mutex);

	va_start(ap, fmt);

#ifdef SWUPD_WITH_TELEMETRY
	{
		va_list ap_copy;

		va_copy(ap_copy, ap);
		if ((priority == log_warning) || (priority == log_error)) {
			char *classification = NULL;
			buf = format_log_message(TM_TYPE, priority, file, msg, filename, linenr, fmt, ap_copy);
			classification = format_classification_message(class_msg);
			tm_send_record(priority, classification, buf, strlen(buf));
			free(classification);
			free(buf);
		}
		va_end(ap_copy);
	}
#endif

	if (logfile) {
		buf = format_log_message(LOG_TYPE, priority, file, msg, filename, linenr, fmt, ap);

		fprintf(logfile, "%s", buf);
		fflush(logfile);

		free(buf);
	}

	va_end(ap);

	pthread_mutex_unlock(&log_mutex);
}

void log_basic(const char *fmt, ...)
{
	if (verbose < 0)
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void log_stdout(const char *fmt, ...)
{
	if (verbose < 0)
		return;
	if (verbose) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
}

void log_stdout_extraverbose(const char *fmt, ...)
{
	if (verbose < 0)
		return;
	if (verbose >= 2) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
}

void close_log(int status, int from_version, int to_version, enum log_str s)
{
	struct timeval current_time;

	if (!logfile) {
		LOG_INFO(NULL, "no log file to close", class_file_misc, "");
	} else {
		if (fflush(logfile) != 0)
			LOG_ERROR(NULL, "log fflush failed", class_file_io, "");
		if (fclose(logfile) != 0)
			LOG_ERROR(NULL, "log fclose failed", class_file_io, "");
		logfile = NULL;
	}

	gettimeofday(&current_time, NULL);
	timersub(&current_time, &start_time, &current_time);

	if (s == log_bootloader_pref) {
		if (status == EXIT_SUCCESS) {
			log_stdout("Modification of bootloader next boot target succeeded.\n");
			LOG_WARN(NULL, "Bootloader config change success", class_bootloader,
				"\\*version=\"%d\"*\\", from_version);
		} else {
			log_stdout("Modification of bootloader next boot target failed.\n");
			LOG_WARN(NULL, "Bootloader config change failure", class_bootloader,
				"\\*version=\"%d\"*\\", from_version);
		}
	} else if (s == log_update) {
		if (status == EXIT_SUCCESS) {
			if (from_version < to_version) {
				log_basic("Update complete. System updated from version %d to version %d\n", from_version, to_version);
				LOG_WARN(NULL, "Successful update", class_version,
						"\\*from_version=\"%d\",to_version=\"%d\",runtime=\"%d.%03i\"*\\",
						from_version, to_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			} else {
				log_basic("Update complete. System already up-to-date at version %d\n", from_version);
				LOG_WARN(NULL, "Already up-to-date", class_version,
						"\\*from_version=\"%d\",runtime=\"%d.%03i\"*\\",
						from_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			}
		} else {
			if (update_complete) {
				log_basic("Update complete, but some failures occurred. Please check log.\n");
			} else if (network_available == true) {
				LOG_WARN(NULL, "Failed update", class_version,
						"\\*from_version=\"%d\",to_version=\"%d\",runtime=\"%d.%03i\"*\\",
						from_version, to_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			} else {
				log_stdout("Network unavailable.\n");
				LOG_INFO(NULL, "No network available", class_version,
						"\\*from_version=\"%d\",runtime=\"%d.%03i\"*\\",
						from_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			}
		}
	} else if (s == log_verify) {
		if (status == EXIT_SUCCESS) {
			log_stdout("Verify complete. System verified successfully against Manifest version %d\n", from_version);
			LOG_WARN(NULL, "Successful verify", class_version,
					"\\*version=\"%d\",runtime=\"%d.%03i\"*\\",
					from_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
		} else {
			log_stdout("Verify failed.\n");
			if (network_available == true) {
				LOG_WARN(NULL, "Failed verify", class_version,
					"\\*version=\"%d\",runtime=\"%d.%03i\"*\\",
					from_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			} else {
				log_stdout("Network unavailable.\n");
				LOG_INFO(NULL, "No network available", class_version,
					"\\*version=\"%d\",runtime=\"%d.%03i\"*\\",
					from_version, (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
			}
		}
	}
	log_stdout_extraverbose("Runtime duration was %d.%03i seconds\n", (int)current_time.tv_sec, (int)current_time.tv_usec / 1000);
}
