#ifndef __INCLUDE_GUARD_LOG_H
#define __INCLUDE_GUARD_LOG_H

#include <stdlib.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum log_priority {
	log_info,
	log_debug,
	log_warning,
	log_error
};

enum log_str {
	log_update,
	log_verify,
	log_bootloader_pref
};

enum log_class_msg {
	class_undef,
	class_file_io,
	class_file_compression,
	class_file_misc,
	class_mnt_pt,
	class_btrfs_mnt_pt,
	class_osvol,
	class_osvol_staging,
	class_esp_mnt_pt,
	class_bootloader,
	class_manifest,
	class_curl,
	class_disk_sp,
	class_xattrs,
	class_security,
	class_stats,
	class_mem_alloc,
	class_delta,
	class_thread,
	class_efi,
	class_hash,
	class_permission,
	class_subscription,
	class_sync,
	class_version,
	class_url,
	class_ui,
	class_scripts
};

struct file;

void init_log(void);
void init_log_stdout(void);
void close_log(int status, int from_version, int to_version, enum log_str s);
char *format_log_message(int log_type, enum log_priority priority, struct file *file, char *msg,
		char *filename,int linenr, const char *fmt, va_list ap);
char *format_classification_message(enum log_class_msg class_msg);
void __log_message(enum log_priority priority, struct file *file, enum log_class_msg class_msg,
		char *msg, char *filename, int linenr, const char *fmt, ...);
void log_basic(const char *fmt, ...);
void log_stdout(const char *fmt, ...);
void log_stdout_extraverbose(const char *fmt, ...);

#define LOG_INFO(file, msg, class_msg, fmt...) __log_message(log_info, file, class_msg, msg, __FILE__, __LINE__, fmt)
#define LOG_DEBUG(file, msg, class_msg, fmt...) __log_message(log_debug, file, class_msg, msg, __FILE__, __LINE__, fmt)
#define LOG_WARN(file, msg, class_msg, fmt...) __log_message(log_warning, file, class_msg, msg, __FILE__, __LINE__, fmt)
#define LOG_ERROR(file, msg, class_msg, fmt...) __log_message(log_error, file, class_msg, msg, __FILE__, __LINE__, fmt)

#define TM_TYPE  0
#define LOG_TYPE 1

#ifdef SWUPD_WITH_TELEMETRY

int __tm_send_record(enum log_priority priority, char *msg, enum log_class_msg class_msg,
			char *filename, int linenr, const char *fmt, ...);
int tm_send_record(enum log_priority priority, const char *classification, const void *payload, size_t len);

#define TM_SEND_RECORD(priority, msg, class_msg, fmt...) __tm_send_record(priority, msg, class_msg, __FILE__, __LINE__, fmt)
#else
#define TM_SEND_RECORD(priority, msg, class_msg, fmt...)
#endif

#ifdef  __cplusplus
}
#endif

#endif /* __INCLUDE_GUARD_LOG_H */
