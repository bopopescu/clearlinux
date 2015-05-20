#ifndef __INCLUDE_GUARD_SWUPD_H
#define __INCLUDE_GUARD_SWUPD_H

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "list.h"
#include <limits.h>
#include <dirent.h>
#include "progress.h"
#include "log.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* WARNING: keep SWUPD_VERSION_INCR in sync with server definition  */
#define SWUPD_VERSION_INCR		10
#define SWUPD_VERSION_IS_DEVEL(v) (((v) % SWUPD_VERSION_INCR) == 8)
#define SWUPD_VERSION_IS_RESVD(v) (((v) % SWUPD_VERSION_INCR) == 9)

#ifndef LINE_MAX
#define LINE_MAX	_POSIX2_LINE_MAX
#endif

#define PATH_MAXLEN 4096

#define UNUSED_PARAM  __attribute__ ((__unused__))

/* Define the supported functional variants' constants and feature set.
 * The major features currently are:
 *    SWUPD_{WITH,WITHOUT}_BINDMNTS  -  cope with bind mounts over rootfs
 *    SWUPD_{WITH,WITHOUT}_BTRFS     -  update btrfs subvol's instead of rootfs
 *    SWUPD_{WITH,WITHOUT}_ESP       -  manage an EFI System Parition
 *    SWUPD_{WITH,WITHOUT}_REPAIR    -  interact with repair entity via efivars
 *    SWUPD_{WITH,WITHOUT}_SELINUX   -  handle selinux attributes
 *    SWUPD_{WITH,WITHOUT}_TELEMETRY -  interact with telemetry client
 */
#ifdef SWUPD_LINUX_ROOTFS
#define SWUPD_WITHOUT_BINDMNTS
#define SWUPD_WITHOUT_BTRFS
#define SWUPD_WITHOUT_ESP
#define SWUPD_WITHOUT_REPAIR
#define SWUPD_WITHOUT_SELINUX
#define SWUPD_WITHOUT_TELEMETRY
#define MOUNT_POINT "/"
#define STATE_DIR "/var/lib/swupd"
#define LOG_DIR "/var/log/swupd"
#define LOCK_DIR "/run/lock"
#define BUNDLES_DIR "/usr/share/clear/bundles"
#define STAGING_SUBVOL "/"
#define UPDATE_CA_CERTS_PATH "/usr/share/clear/update-ca"
#define SIGNATURE_CA_CERT "test-do-not-ship-R0-0.pem"

#else
#ifdef SWUPD_LINUX_BTRFS
#define SWUPD_WITHOUT_BINDMNTS
#define SWUPD_WITH_BTRFS
#define SWUPD_WITHOUT_ESP
#define SWUPD_WITHOUT_REPAIR
#define SWUPD_WITHOUT_SELINUX
#define SWUPD_WITHOUT_TELEMETRY
#define MOUNT_POINT "/mnt/swupd"
#define STATE_DIR "/mnt/swupd/update"
#define LOG_DIR "/var/log/swupd"
#define LOCK_DIR "/run/lock"
#define STAGING_SUBVOL "/mnt/swupd/staging"
#define BTRFS_CMD "/sbin/btrfs"
#define UPDATE_CA_CERTS_PATH "/etc/security/otacerts"
#define SIGNATURE_CA_CERT "test-do-not-ship-R0-0.pem"

#else
#ifdef SWUPD_ANDROID
#define SWUPD_WITH_BINDMNTS
#define SWUPD_WITH_BTRFS
#define SWUPD_WITH_ESP
#define SWUPD_WITH_REPAIR
#define SWUPD_WITH_SELINUX
#define SWUPD_WITH_TELEMETRY
#define MOUNT_POINT "/mnt/swupd"
#define STATE_DIR "/mnt/swupd/update"
#define LOG_DIR "/var/log/swupd"
#define LOCK_DIR "/run/lock"
#define STAGING_SUBVOL "/mnt/swupd/starpeak"
#define BTRFS_CMD "/sbin/btrfs"
#define UPDATE_CA_CERTS_PATH "/etc/security/otacerts"
#define SIGNATURE_CA_CERT "test-do-not-ship-R0-0.pem"

#else
#error undefined build variant
#endif
#endif
#endif

#ifdef SWUPD_WITH_BTRFS
#define BTRFS_SLEEP_TIME 20                             /* 20 seconds */
#define VERIFY_FAILED_MAX_VERSIONS_COUNT 20
#endif

#ifdef SWUPD_WITH_SELINUX
#define TAR_PERM_ATTR_ARGS "--preserve-permissions --xattrs --xattrs-include='*' --selinux"
#else /* SWUPD_WITHOUT_SELINUX */
#define TAR_PERM_ATTR_ARGS "--preserve-permissions --xattrs --xattrs-include='*'"
#endif

struct sub {
	char *component;	/* name of bundle/component/subscription */
	int version;		/* if non-zero, version read from MoM */
};

struct manifest {
	int version;
	int manifest_version;
	uint64_t contentsize;
	struct list *files;
	struct list *manifests; /* struct file for possible manifests */
	struct list *submanifests; /* struct manifest for subscribed manifests */
	char *component;
};

struct header;

extern int verbose;
extern int update_count;
extern int update_skip;
extern bool update_complete;
extern int need_update_boot;
extern int need_update_bootloader;

struct file {
	char *filename;
	char *hash;
	bool use_xattrs;
	int  last_change;

	unsigned int is_dir		: 1;
	unsigned int is_file		: 1;
	unsigned int is_link		: 1;
	unsigned int is_deleted		: 1;
	unsigned int is_manifest	: 1;

	unsigned int is_config		: 1;
	unsigned int is_state		: 1;
	unsigned int is_boot		: 1;
	unsigned int is_rename		: 1;
	unsigned int is_orphan		: 1;
	unsigned int do_not_update	: 1;

	struct file *peer;  /* same file in another manifest */
	struct file *deltapeer; /* the file to do the binary delta against; often same as "peer" except in rename cases */
	struct header *header;
	void *priv;

	char *dotfile;
};

struct update_stat {
	uint64_t	st_mode;
	uint64_t	st_uid;
	uint64_t	st_gid;
	uint64_t	st_rdev;
	uint64_t	st_size;
};

#ifdef SWUPD_WITH_REPAIR
enum repair_reason {
	repair_boot_check_failure,
	repair_verify_failure,
	repair_update_failure,
	repair_restore_starpeak
};
#endif

extern bool download_only;
extern bool verify_esp_only;
extern bool network_available;
extern bool ignore_config;
extern bool ignore_state;
extern bool ignore_boot;
extern bool ignore_orphans;
extern bool fix;
extern char *format_string;
extern char *path_prefix;
extern bool set_format_string(char *userinput);
extern bool init_globals(void);
extern void free_globals(void);

extern char *version_server_urls[];
extern char *preferred_version_url;
extern char *content_server_urls[];
extern char *preferred_content_url;
extern int pick_urls(int *server_version);

extern void check_root(void);

extern int main_update(void);
extern int main_verify(int current_version);

extern int read_versions(int *current_version, int *latest_version, int *server_version);
extern int read_version_from_subvol_file(char *path_prefix);

extern int try_version_download(char *test_url);

extern bool ignore(struct file *file);
extern bool is_state(char *filename);
extern void apply_heuristics(struct file *file);

extern int apply_bsdiff_delta(char *oldfile, char *newfile, char *deltafile);
extern int make_bsdiff_delta(char *oldfile, char *newfile, char *deltafile, int raw);
extern int file_sort_hash(const void *a, const void *b);
extern int file_sort_filename(const void *a, const void *b);
extern int load_manifests(int current, int version, char *component, struct file *file, struct manifest **manifest);
extern struct list *create_update_list(struct manifest *current, struct manifest *server);
extern struct list *create_difference_list(struct manifest *m1, struct manifest *m2);
extern void link_manifests(struct manifest *m1, struct manifest *m2);
extern void link_submanifests(struct manifest *m1, struct manifest *m2);
extern struct manifest *alloc_manifest(int version, char *component);
extern void free_manifest(struct manifest *manifest);

extern void account_new_file(void);
extern void account_deleted_file(void);
extern void account_changed_file(void);
extern void account_new_manifest(void);
extern void account_deleted_manifest(void);
extern void account_changed_manifest(void);
extern void account_delta_hit(void);
extern void account_delta_miss(void);
extern void print_statistics(int version1, int version2);
extern void print_delta_statistics(void);
extern int have_delta_files(void);
extern int have_new_files(void);

extern int download_subscribed_packs(int oldversion, int newversion);

extern void try_delta_download(struct file *file);
extern int full_download(struct file *file);
extern int start_full_download(int attempt_number);
extern void end_full_download(void);

extern int prepare(bool *is_corrupted, int current_version, int latest_version);
extern int finalize(struct list *updates, int latest_version, int target_version);
extern void do_staging(struct file *file);

extern int update_device_latest_version(int version);

extern int swupd_curl_init(void);
extern void swupd_curl_cleanup(void);
extern void swupd_curl_set_current_version(int v);
extern void swupd_curl_set_requested_version(int v);
extern int swupd_curl_get_file(const char *url, const char *filename, struct file *file,
			    char *tmp_version, int uncached, progress_msg_id msg_id, int resume);
#define SWUPD_CURL_LOW_SPEED_LIMIT	1
#define SWUPD_CURL_CONNECT_TIMEOUT	30
#define SWUPD_CURL_RCV_TIMEOUT		120
extern CURLcode swupd_curl_set_basic_options(CURL *curl, bool ssl);

extern int do_verify(const char *component, int version, bool *is_corrupted);
extern int update_loop(struct list *updates, int latest_version, int target_version);

extern struct list *subs;
extern void free_subscriptions(void);
extern void read_subscriptions(void);
extern void read_subscriptions_alt(void);
extern int component_subscribed(char *component);
extern void subscription_versions_from_MoM(struct manifest *MoM);

extern int recurse_manifest(struct manifest *manifest, const char *component);
extern void consolidate_submanifests(struct manifest *manifest);
extern struct manifest *manifest_from_directory(int version, bool use_xattrs);
extern void debug_write_manifest(struct manifest *manifest, char *filename);
extern char *compute_hash(struct file *file, char *filename) __attribute__((warn_unused_result));
extern void unlink_all_staged_content(struct file *file);
extern void link_renames(struct list *newfiles, struct manifest *from_manifest);
extern void dump_file_descriptor_leaks(void);
extern void start_delta_download(void);
extern void end_delta_download(void);
extern FILE * fopen_exclusive(const char *filename); /* no mode, opens for write only */
extern int rm_staging_dir_contents(const char *rel_path);
extern int prep_mount(int rw);
extern void post_unmount(void);

extern int mount_esp_fs(void);
extern void unmount_esp_fs(void);
extern int remove_esp_OS_boot_config_files(const char *version);
extern int remove_esp_repairOS_boot_config_files(const char *version);
extern uint64_t get_available_esp_space(void);
extern uint64_t get_available_btrfs_space(void);
extern bool esp_updates_available(struct list *updates, int target_version);
extern int copy_files_to_esp(int target_version);
char *get_esp_repairOS_list(int *count);

extern char *mounted_dirs;
extern void get_mounted_directories(void);
extern char *mk_full_filename(const char *prefix, const char *path);
extern bool is_directory_mounted(const char *filename);
extern bool is_under_mounted_directory(const char *filename);

extern void run_scripts(void);

#ifdef SWUPD_WITH_ESP
extern int efivar_bootloader_boot_check(int version, bool repair_fallback);
extern int efivar_bootloader_set_next_boot_to_version(int version);
extern int efivar_bootloader_set_next_boot_to_repair(enum repair_reason reason, struct list *version_list);
typedef void (*efivar_bootloader_boot_for_repair_needed_cb_t)(void);
extern void efivar_bootloader_set_boot_for_repair_needed_cb(efivar_bootloader_boot_for_repair_needed_cb_t);
extern int efivar_bootloader_clear_verify_error(void);
extern void efivar_bootloader_dump(void);

extern void critical_verify_error(int version);
extern void critical_verify_multi_error(struct list *version_list);
extern void fatal_update_error(int from_version, int to_version);
extern void clear_verify_error(void);
#else /* SWUPD_WITHOUT_ESP */
extern void critical_verify_error(int version);
extern void clear_verify_error(void);
#endif /* SWUPD_*_ESP */

/* lock.c */
int p_lockfile(void);
void v_lockfile(int fd);

extern int swupd_rm(const char *path);
extern int verify_fix(int picky);

/* some disk sizes constants for the various features:
 *   ...consider adding build automation to catch at build time
 *      if the build's artifacts are larger than these thresholds */
#define MANIFEST_REQUIRED_SIZE (1024 * 1024 * 100) 	/* 100M */
#define FREE_MARGIN 10					/* 10%  */
#define STATE_DIR_MIN_FREE_SIZE (1024 * 1024 * 260) 	/* 260M */

#ifdef SWUPD_WITH_ESP
#define ESP_OS_MIN_FREE_SIZE (1024 * 1024 * 120)	/* 120M */
#endif

#ifdef SWUPD_WITH_REPAIROS
#define MAX_REPAIR_OS_TO_KEEP 2 /* plus 1 about to be installed */
#endif

#ifdef SWUPD_WITH_BTRFS
#define MAX_OS_TO_KEEP 4	/* plus 1 about to be installed */
#endif
/****************************************************************/

#ifdef SWUPD_WITH_BTRFS
extern int remove_snapshot(const char *version);
extern int snapshotsort(const struct dirent **first, const struct dirent **second);
typedef int (*dirent_cmp_fn_t)(const struct dirent **first, const struct dirent **second);
extern char *get_snapshot_list(int current, int latest, dirent_cmp_fn_t dirent_cmp_fn, int *num_elements);
#endif
extern bool free_disk_space_for_manifest(int current_version, int latest_version);
extern int free_disk_space_generic(int current_version, int latest_version, struct manifest *manifest);
#ifdef SWUPD_WITH_REPAIR
extern bool free_disk_space_for_repairOS_update(int current_version, int latest_version, struct manifest *manifest);
#endif

#ifdef  __cplusplus
}
#endif

#endif
