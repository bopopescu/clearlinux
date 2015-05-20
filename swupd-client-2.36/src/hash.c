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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <openssl/hmac.h>

#include <swupd.h>
#include <xattrs.h>

static char *hmac_sha256_for_data(const unsigned char *key, size_t key_len, const unsigned char *data, size_t data_len)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_len = 0;
	char *digest_str;
	unsigned int i;

	if (data == NULL)
		return NULL;

	if (HMAC(EVP_sha256(), (const void *)key, key_len, data, data_len, digest, &digest_len) == NULL) {
		LOG_ERROR(NULL, "HMAC error", class_hash, "");
		return NULL;
	}

	digest_str = calloc((digest_len * 2) + 1, sizeof(char));
	if (digest_str == NULL)
		return NULL;

	for (i = 0; i < digest_len; i++) {
		sprintf(&digest_str[i * 2], "%02x", (unsigned int)digest[i]);
	}

	return digest_str;
}

static char *hmac_sha256_for_string(const unsigned char *key, size_t key_len, const char *str)
{
	if (str == NULL) {
		LOG_ERROR(NULL, "refusing to hmac NULL string", class_hash, "");
		return NULL;
	}

	return hmac_sha256_for_data(key, key_len, (const unsigned char *)str, strlen(str));
}

static void hmac_compute_key(const char *file,
			     const struct update_stat *updt_stat,
			     char **key, size_t *key_len, bool use_xattrs)
{
	char *xattrs_blob = (void *)0xdeadcafe;
	size_t xattrs_blob_len = 0;

	if (use_xattrs)
		xattrs_get_blob(file, &xattrs_blob, &xattrs_blob_len);
	*key = hmac_sha256_for_data((const unsigned char *)updt_stat,
				    sizeof(struct update_stat),
				    (const unsigned char *)xattrs_blob,
				    xattrs_blob_len);

	if (*key == NULL)
		*key_len = 0;
	else
		*key_len = strlen((const char*)*key);

	if (xattrs_blob_len != 0)
		free(xattrs_blob);
}

/* this function MUST be kept in sync with the server
 * return is NULL if there was an error. If the file does not exist,
 * a "0000000..." hash is returned as is our convention in the manifest
 * for deleted files */
char *compute_hash(struct file *file, char *filename)
{
	struct stat stat;
	int ret;
	unsigned char *blob;
	char *key = NULL;
	size_t key_len;
	FILE *fl;
	struct update_stat tfstat;
	char *hash = NULL;

	memset(&stat, 0, sizeof(stat));
	memset(&tfstat, 0, sizeof(tfstat));
	ret = lstat(filename, &stat);
	if (ret < 0) {
		if (errno == ENOENT) {
			LOG_DEBUG(NULL, "File does not exist, mark as deleted", class_file_misc, "%s", filename);
			file->is_deleted = 1;
			hash = strdup("0000000000000000000000000000000000000000000000000000000000000000");
			if (!hash)
				abort();
			return hash;
		}

		LOG_ERROR(NULL, "stat error ", class_file_io, "\\*filename=\"%s\",strerror=\"%s\"*\\",
				filename, strerror(errno));
		return NULL;
	}
	tfstat.st_mode = stat.st_mode;
	tfstat.st_uid = stat.st_uid;
	tfstat.st_gid = stat.st_gid;
	tfstat.st_rdev = stat.st_rdev;
	tfstat.st_size = stat.st_size;
	/* just server does this:
	file->size = stat.st_size;
	 */
	if ((file->is_link) || (S_ISLNK(stat.st_mode))) {
		char link[PATH_MAXLEN];
		memset(link, 0, PATH_MAXLEN);

		file->is_file = 0;
		file->is_dir = 0;
		file->is_link = 1;

		ret = readlink(filename, link, PATH_MAXLEN - 1);

		memset(&tfstat.st_mode, 0, sizeof(tfstat.st_mode));

		if (ret >= 0) {
			hmac_compute_key(filename, &tfstat, &key, &key_len, file->use_xattrs);
			hash = hmac_sha256_for_string(
					(const unsigned char *)key,
					key_len,
					link);
			if (!hash)
				abort();
			free(key);
			return hash;
		} else {
			LOG_ERROR(NULL, "readlink error ", class_file_io, "\\*ret=\"%i\",errno=\"%i\",strerror=\"%s\"*\\",
					ret, errno, strerror(errno));
			return NULL;
		}
	}

	if ((file->is_dir) || (S_ISDIR(stat.st_mode))) {
		file->is_file = 0;
		file->is_dir = 1;
		file->is_link = 0;

		tfstat.st_size = 0;

		hmac_compute_key(filename, &tfstat, &key, &key_len, file->use_xattrs);
		hash = hmac_sha256_for_string(
					(const unsigned char *)key,
					key_len,
					file->filename);	//file->filename not filename
		if (!hash)
			abort();
		free(key);
		return hash;
	}

	/* if we get here, this is a regular file */
	file->is_file = 1;
	file->is_dir = 0;
	file->is_link = 0;

	fl = fopen(filename, "r");
	if (!fl) {
		LOG_ERROR(NULL, "file open error ", class_file_io, "\\*filename=\"%s\",strerror=\"%s\"*\\",
				filename, strerror(errno));
		return NULL;
	}
	blob = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fileno(fl), 0);
	if (blob == MAP_FAILED && stat.st_size != 0)
		abort();

	hmac_compute_key(filename, &tfstat, &key, &key_len, file->use_xattrs);
	hash = hmac_sha256_for_data(
				(const unsigned char *)key,
				key_len,
				blob,
				stat.st_size);
	munmap(blob, stat.st_size);
	fclose(fl);
	if (!hash)
		abort();
	free(key);
	return hash;
}

/* similar to get_hash(), but called from src/update.c
 * The goal here is to check content staged for update.
 */
int verify_hash(struct file *file)
{
	char *base;
	char *filename;
	char *hash;
	int err = 0;

	if (file->is_deleted)
		return 0;

	if (asprintf(&base, "%s/staged/", STATE_DIR) < 0)
		abort();
	if (asprintf(&filename, "%s/%s", base, file->hash) < 0)
		abort();

	hash = compute_hash(file, filename);
	if (hash == NULL) {
		LOG_WARN(NULL, "Could not compute Hash for file, cannot verify ", class_hash,
				"\\*filename=\"%s\",hash=\"%s\"*\\", filename, file->hash);
		err = -1;
		goto out;
	}

	if (strcmp(hash, file->hash) != 0) {
		LOG_WARN(NULL, "Hash verification failed for file ", class_hash,
				"\\*filename=\"%s\",file_hash=\"%s\",computed_hash=\"%s\"*\\",
				filename, file->hash, hash);
		//unlink_all_staged_content(file);
		err = -1;
	}
	free(hash);
out:
	free(base);
	free(filename);

	return err;
}
