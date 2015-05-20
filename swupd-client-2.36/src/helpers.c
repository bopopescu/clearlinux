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
 *         Tim Pepper <timothy.c.pepper@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <errno.h>

#include <swupd.h>


void check_root(void)
{
	if (getuid() != 0) {
		log_stdout("This program must be run as root..aborting.\n\n");
		LOG_ERROR(NULL, "Not running as root", class_permission, "\\*uid=\"%i\"*\\", getuid());
		exit(EXIT_FAILURE);
	}
}

/* Remove the contents of a staging directory (eg: /mnt/swupd/update/780 or
 * /mnt/swupd/update/delta) which are not supposed to contain
 * subdirectories containing files, ie: no need for true recursive removal.
 * Just the relative path (et: "780" or "delta" is passed as a parameter).
 *
 * return: 0 on success, non-zero on error
 */
int rm_staging_dir_contents(const char *rel_path)
{
	DIR *dir;
	struct dirent entry;
	struct dirent *result;
	char *filename;
	char *abs_path;
	int ret;

	if (asprintf(&abs_path, "%s/%s", STATE_DIR, rel_path) <= 0)
		abort();

	dir = opendir(abs_path);
	if (dir == NULL)
		return -1;

	while ((ret = readdir_r(dir, &entry, &result)) == 0) {
		if (result == NULL)
			break;

		if (!strcmp(entry.d_name, ".") ||
		    !strcmp(entry.d_name, ".."))
			continue;

		if (asprintf(&filename, "%s/%s", abs_path, entry.d_name) <= 0)
			abort();

		ret = remove(filename);
		if (ret != 0) {
			LOG_ERROR(NULL, "Could not remove tmp file", class_disk_sp, "\\*ret=\"%d\",file=\"%s\"*\\", ret, filename);
			free(filename);
			break;
		}
		free(filename);
	}

	free(abs_path);
	closedir(dir);

	sync();
	return ret;
}

void unlink_all_staged_content(struct file *file)
{
	char *filename;

	/* downloaded tar file */
	if (asprintf(&filename, "%s/download/%s.tar", STATE_DIR, file->hash) <= 0)
		abort();
	unlink(filename);
	free(filename);
	if (asprintf(&filename, "%s/download/.%s.tar", STATE_DIR, file->hash) <= 0)
		abort();
	unlink(filename);
	free(filename);

	/* downloaded and un-tar'd file */
	if (asprintf(&filename, "%s/staged/%s", STATE_DIR, file->hash) <= 0)
		abort();
	if (file->is_dir)
		rmdir(filename);
	else
		unlink(filename);
	free(filename);

	/* delta file */
	if (file->peer) {
		if (asprintf(&filename, "%s/delta/%i-%i-%s", STATE_DIR,
		    file->peer->last_change, file->last_change, file->hash) <= 0)
			abort();
		unlink(filename);
		free(filename);
	}

	/* remove now unused / legacy server_version file if present */
	if (asprintf(&filename, "%s/server_version", STATE_DIR) <= 0)
		abort();
	unlink(filename);
	free(filename);
}

FILE * fopen_exclusive(const char *filename) /* no mode, opens for write only */
{
	int fd;

	fd = open(filename,O_CREAT | O_EXCL | O_RDWR , 00600);
	if (fd < 0)
		return NULL;
	return fdopen(fd, "w");
}

static int create_required_dirs(void)
{
	int err;
	char *cmd;

	if (asprintf(&cmd, "mkdir -p %s/{delta,staged,download}", STATE_DIR) < 0)
		abort();

	err = system(cmd);
	free(cmd);
	if (err)
		return -1;

	if (asprintf(&cmd, "chmod 700 %s/{delta,staged,download}", STATE_DIR) < 0)
		abort();

	err = system(cmd);
	free(cmd);
	if (err)
		return -1;

	return EXIT_SUCCESS;
}

#ifdef SWUPD_WITH_BTRFS
static bool btrfs_is_rw = false;

/* Try to leave the rootfs mounted read-only
 * There's not much we can do about errors here. */
static void remount_rootfs_ro(void) {
	int ret;
	struct statvfs buf_vfs;

	ret = statvfs("/", &buf_vfs);
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not stat rootfs for r/o remount", class_btrfs_mnt_pt, "\\*ret=\"%d\"*\\", ret);
		return;
	}
	if (!(buf_vfs.f_flag & ST_RDONLY)) {
		ret = mount("/", "/", "btrfs", MS_SILENT|MS_REMOUNT|MS_RDONLY, NULL);
		if (ret != 0) {
			LOG_ERROR(NULL, "Could not remount rootfs r/o", class_btrfs_mnt_pt, "\\*ret=\"%d\",strerror=\"%s\"*\\",
					ret, strerror(errno));
		}
	}
	btrfs_is_rw = false;
	return;
}

/* rootfs is a btrfs subvolume and is mounted readonly, but
 * btrfs wont let us mount the main volume r/w if any
 * subvolume is mounted readonly.  So we temporarily remount
 * the read-only snapshot subvolume read-write (the rootfs
 * is still read-only thanks to btrfs).  We'll remount it
 * back to read-only later.
 */
static int remount_rootfs_rw(void) {
	int ret;
	struct statvfs buf_vfs;

	ret = statvfs("/", &buf_vfs);
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not stat rootfs for r/w remount", class_btrfs_mnt_pt,
				"\\*ret=\"%d\"*\\", ret);
		return -1;
	}
	if (buf_vfs.f_flag & ST_RDONLY) {
		ret = mount("/", "/", "btrfs", MS_SILENT|MS_REMOUNT, NULL);
		if (ret != 0) {
			LOG_ERROR(NULL, "Could not remount rootfs r/w", class_btrfs_mnt_pt,
					"\\*ret=\"%d\"*\\", ret);
			return -1;
		}
	}
	btrfs_is_rw = true;
	return 0;
}

static int prep_btrfs(int rw)
{
	int ret;
	struct stat buf;
	unsigned long mountflags;

	if (rw == O_RDWR) {
		if (btrfs_is_rw == false) {
			post_unmount();
			ret = remount_rootfs_rw();
			if (ret != 0) {
				goto exit;
			}
		} else {
			return 0;
		}
	}

	memset(&buf, 0, sizeof(struct stat));
	ret = stat(MOUNT_POINT, &buf);
	if ((ret != 0) || !S_ISDIR(buf.st_mode) ||
	    (buf.st_uid != 0) || (buf.st_gid != 0) ||
	    ((S_IRWXU & buf.st_mode) != 00700) ||
	    ((S_IRWXG & buf.st_mode) != 0) || ((S_IRWXO & buf.st_mode) != 0)) {
		LOG_ERROR(NULL, "bad update btrfs mount point", class_btrfs_mnt_pt,
			"\\*ret=\"%d\",dir=\"%d\",uid=\"%lu\",gid=\"%lu\",u=\"%o\",g=\"%o\",o=\"%o\"*\\",
			ret, S_ISDIR(buf.st_mode), buf.st_uid, buf.st_gid, S_IRWXU & buf.st_mode,
			S_IRWXG & buf.st_mode, S_IRWXO & buf.st_mode);
		log_stdout("Bad update btrfs mount point\n");
		ret = -1;
		goto exit;
	}

	ret = mount(NULL, MOUNT_POINT, "tmpfs", MS_SILENT, NULL);
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount tmpfs", class_mnt_pt, "\\*ret=\"%d\"*\\", ret);
		goto exit;
	}

	ret = mount(MOUNT_POINT, MOUNT_POINT, "", MS_SILENT|MS_BIND, NULL);
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount bind", class_mnt_pt, "\\*ret=\"%d\"*\\", ret);
		goto exit;
	}

	ret = mount("", MOUNT_POINT, "", MS_SILENT|MS_PRIVATE, NULL);
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount make-private", class_mnt_pt, "\\*ret=\"%d\"*\\", ret);
		goto exit;
	}

	if (rw == O_RDONLY) {
		mountflags = MS_SILENT | MS_RDONLY;
	} else { /* (rw == O_RDWR) */
		mountflags = MS_SILENT;
	}

#ifdef SWUPD_ANDROID
	ret = mount("/dev/block/by-uuid/632b73c9-3b78-4ee5-a856-067bb4e1b745", MOUNT_POINT, "btrfs", mountflags, NULL);
#else /* !SWUPD_ANDROID */
	ret = mount("/dev/disk/by-partuuid/632b73c9-3b78-4ee5-a856-067bb4e1b745", MOUNT_POINT, "btrfs", mountflags, NULL);
#endif
	if (ret != 0) {
		LOG_ERROR(NULL, "Could not mount magic UUID", class_btrfs_mnt_pt, "\\*ret=\"%d\"*\\", ret);
		ret = -1;
		goto exit;
	}
exit:
	if (ret)
		post_unmount();

	return ret;
}
#endif

int prep_mount(int rw)
{
	int ret = 0;

#ifdef SWUPD_WITH_BTRFS
	ret = prep_btrfs(rw);
#endif /*SWUPD_WITH_BTRFS*/

	if (rw == O_RDWR) {
		ret = create_required_dirs();
	}
	return ret;
}

void post_unmount(void)
{
#ifdef SWUPD_WITH_BTRFS
	/* try to get rid of our stack of mounts */
	while (umount(MOUNT_POINT) == 0) {
		LOG_DEBUG(NULL, "umounted once", class_btrfs_mnt_pt, "");
	}

	/* try to leave the rootfs mounted read-only */
	remount_rootfs_ro();
#endif
	return;
}

/* returns a colon separated list of current mountpoints.
 * e.g: :/proc:/mnt/acct:
 */
void get_mounted_directories(void)
{
	FILE *file;
	char *line = NULL;
	char *mnt;
	char *tmp;
	ssize_t ret;
	char *c;
	size_t n;

	file = fopen("/proc/self/mountinfo", "r");
	if (!file) {
		LOG_ERROR(NULL, "Unable to check active mounts", class_mnt_pt, "");
		return;
	}

	while (!feof(file)) {
		ret = getline(&line, &n, file);
		if ((ret < 0) || (line == NULL))
			break;

		c = strchr(line, '\n');
		if (c)
			*c = 0;

		n = 0;
		mnt = strtok(line, " ");
		while (mnt != NULL) {
			if (n == 4) {
				/* The "4" assumes today's mountinfo form of:
				* 16 36 0:3 / /proc rw,relatime master:7 - proc proc rw
				* where the fifth field is the mountpoint. */
				if (strcmp(mnt, "/") == 0)
					break;

				if (mounted_dirs == NULL)
					if (asprintf(&mounted_dirs, "%s", ":") <= 0)
						abort();
				tmp = mounted_dirs;
				if (asprintf(&mounted_dirs, "%s%s:", tmp, mnt) <= 0)
					abort();
				LOG_INFO(NULL, "Added mount point", class_mnt_pt, "\\*path=\"%s\"*\\", mnt);
				free(tmp);
				break;
			}
			n++;
			mnt = strtok(NULL, " ");
		}
		free(line);
		line = NULL;
	}
	free(line);
	fclose(file);
}

// prepends prefix to an path (eg: the global path_prefix to a
// file->filename or some other path prefix and path), insuring there
// is no duplicate '/' at the strings' junction and no trailing '/'
char *mk_full_filename(const char *prefix, const char *path)
{
	char *fname=NULL;
	char *abspath;

	if (path[0] == '/') {
		abspath = strdup(path);
	} else {
		if (asprintf(&abspath, "/%s", path) <= 0)
			abort();
	}
	if (abspath == NULL)
		abort();

	// The prefix is a minimum of "/" or "".  If the prefix is only that,
	// just use abspath.  If the prefix is longer than the minimal, insure
	// it ends in not "/" and append abspath.
	if ((strcmp(prefix, "/") == 0) ||
	    (strcmp(prefix, "") == 0)) {
		// rootfs, use absolute path
		fname = strdup(abspath);
		if (fname == NULL)
			abort();
	} else if (strcmp(&prefix[strlen(prefix)-1], "/") == 0) {
		// chroot and need to strip trailing "/" from prefix
		char *tmp = strdup(prefix);
		if (tmp == NULL)
			abort();
		tmp[strlen(tmp) - 1] = '\0';

		if (asprintf(&fname, "%s%s", tmp, abspath) <= 0)
			abort();
		free(tmp);
	} else {
		// chroot and no need to strip trailing "/" from prefix
		if (asprintf(&fname, "%s%s", prefix, abspath) <= 0)
			abort();
	}
	free(abspath);
	return fname;
}

// expects filename w/o path_prefix prepended
bool is_directory_mounted(const char *filename)
{
	char *fname;
	bool  ret = false;
	char *tmp;

	if (mounted_dirs == NULL)
		return false;

	tmp = mk_full_filename(path_prefix, filename);
	if (asprintf(&fname, ":%s:", tmp) <= 0)
		abort();
	free(tmp);

	if (strstr(mounted_dirs, fname))
		ret = true;

	free(fname);

	return ret;
}

// expects filename w/o path_prefix prepended
bool is_under_mounted_directory(const char *filename)
{
	bool  ret = false;
	int   err;
	char *token;
	char *mountpoint;
	char *dir;
	char *fname;
	char *tmp;

	if (mounted_dirs == NULL)
		return false;

	dir = strdup(mounted_dirs);
	if (dir == NULL)
		abort();

	token = strtok(dir + 1, ":");
	while (token != NULL) {
		if (asprintf(&mountpoint, "%s/", token) < 0)
			abort();

		tmp = mk_full_filename(path_prefix, filename);
		if (asprintf(&fname, ":%s:", tmp) <= 0)
			abort();
		free(tmp);

		err = strncmp(fname, mountpoint, strlen(mountpoint));
		free(fname);
		if (err == 0) {
			free(mountpoint);
			ret = true;
			break;
		}

		token = strtok(NULL, ":");

		free(mountpoint);
	}

	free(dir);

	return ret;
}

static int swupd_rm_file(const char *path)
{
	int err = unlink(path);
	if (err) {
		if (errno == ENOENT) {
			LOG_INFO(NULL, "Cannot remove file", class_file_io,
					"\\*path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
			return 0;
		} else {
			LOG_ERROR(NULL, "Cannot remove file", class_file_io,
					"\\*path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static int swupd_rm_dir(const char *path)
{
	DIR *dir;
	struct dirent entry;
	struct dirent *result;
	char *filename = NULL;
	int ret, err;

	dir = opendir(path);
	if (dir == NULL) {
		if (errno == ENOENT) {
			LOG_INFO(NULL, "Cannot open directory", class_file_io,
					"\\*dir_path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
			ret = 0;
			goto exit;
		} else {
			LOG_ERROR(NULL, "Cannot open directory", class_file_io,
					"\\*dir_path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
			ret = -1;
			goto exit;
		}
	}

	while ((ret = readdir_r(dir, &entry, &result)) == 0) {
		if (result == NULL)
			break;

		if (!strcmp(entry.d_name, ".") ||
		    !strcmp(entry.d_name, ".."))
			continue;

		free(filename);
		if (asprintf(&filename, "%s/%s", path, entry.d_name) <= 0)
			abort();

		err = swupd_rm(filename);
		if (err) {
			ret = -1;
			goto exit;
		}
	}

	/* Delete directory once it's empty */
	err = rmdir(path);
	if (err) {
		if (errno == ENOENT) {
			LOG_INFO(NULL, "Cannot remove directory", class_file_io,
					"\\*path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
		} else {
			LOG_ERROR(NULL, "Cannot remove directory", class_file_io,
					"\\*path=\"%s\",strerror=\"%s\"*\\",
					path, strerror(errno));
			ret = -1;
			goto exit;
		}
	}

exit:
	closedir(dir);
	free(filename);
	return ret;
}

int swupd_rm(const char *filename) {
	struct stat stat;
	int ret;

	ret = lstat(filename, &stat);
	if (ret) {
		if (errno == ENOENT) {
			// Quiet, no real failure here
			return -ENOENT;
		} else {
			LOG_ERROR(NULL, "lstat failed", class_file_io,
					"\\*path=\"%s\",strerror=\"%s\"*\\",
					filename, strerror(errno));
			return -1;
		}
	}

	if (S_ISDIR(stat.st_mode)) {
		ret = swupd_rm_dir(filename);
	} else {
		ret = swupd_rm_file(filename);
	}
	return ret;
}

int verify_fix(int picky)
{	/* Caller function must call init_globals() before calling this function */
	/* and then do proper free_globals() */
	int ret = 0;
	int cur_version = -1;

	/* Set defaults */
	fix = true;
	set_format_string(NULL);

	if (picky)
	{
		ignore_config = false;
		ignore_state = false;
		ignore_orphans = false;
		ignore_boot = false;
	}

	ret = main_verify(cur_version);

	return ret;
}
