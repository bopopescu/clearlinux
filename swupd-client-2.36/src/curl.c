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

/*
 * The curl library is great, but it is a little bit of a pain to get it to
 * reuse connections properly for simple cases. This file will manage our
 * curl handle properly so that we have a standing chance to get reuse
 * of our connections.
 *
 * NOTE NOTE NOTE
 *
 * Only use these from the main thread of the program. For multithreaded
 * use, you need to manage your own curl mutli environment.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <curl/curl.h>
#include <swupd.h>
#include "progress.h"

static struct curl_slist *make_header_fields(void);
static struct curl_slist *add_hdr_i(struct curl_slist *l, const char *name,
				    int value);
static struct curl_slist *add_hdr_s(struct curl_slist *l, const char *name,
				    const char *value);

static CURL *curl = NULL;

#warning TODO decide telemetry variation marking strategy
#ifdef SWUPD_LINUX_ROOTFS
static char *oem_name = "Intel";
static char *oem_board = "rootfs";
#endif

static int curr_version = -1;
static int req_version = -1;

static progress_msg_id curl_progress_msg_id;

int swupd_curl_init(void)
{
	CURLcode curl_ret;

	curl_ret = curl_global_init(CURL_GLOBAL_ALL);
	if (curl_ret != CURLE_OK) {
		LOG_ERROR(NULL, "CURL Global Init failed", class_curl, "\\*curl_ret=\"%li\"*\\", curl_ret);
		return -1;
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		curl_global_cleanup();
		LOG_ERROR(NULL, "CURL Easy Init failed", class_curl, "");
		return -1;
	}

	return 0;
}

void swupd_curl_cleanup(void)
{
	if (curl)
		curl_easy_cleanup(curl);
	curl = NULL;
	curl_global_cleanup();
}

void swupd_curl_set_current_version(int v)
{
	curr_version = v;
}

void swupd_curl_set_requested_version(int v)
{
	req_version = v;
}

static int swupd_curl_progress(void UNUSED_PARAM *curl, curl_off_t dltotal, curl_off_t dlnow,
				curl_off_t UNUSED_PARAM ultotal, curl_off_t UNUSED_PARAM ulnow)
{
	progress_step_ongoing(curl_progress_msg_id, (size_t) dlnow, (size_t) dltotal);

	return 0;
}

static size_t curl_download_version_to_memory(void *contents, size_t size, size_t nmemb, void *userp)
{
	char *tmp_version = (char *)userp;
	size_t data_len = size * nmemb;

	if (data_len >= LINE_MAX) {
		LOG_ERROR(NULL, "Curl offering much more than a version", class_curl, "");
		return 0;
	}

	memcpy(tmp_version, contents, data_len);
	tmp_version[data_len + 1] = '\0';

	return data_len;
}

/* Download a file
 * - If (in_memory_version_string != NULL) the file downloaded is expected
 *   to be a version file and it it downloaded to memory instead of disk.
 * - If (resume != 0) then the function attempts to finish downloading a file
 *   (ie: completing a partial download if one exists otherwise download the
 *   whole thing if no partial is present).
 * - The download is synchronous.  It may make sense to wrap in a managed work
 *   queue or that could be ceded to a separate function using curl multi queues.
 * - This function returns zero or a standard < 0 status code.
 * - If failure to download, partial download is not deleted.
 */
int swupd_curl_get_file(const char *url, const char *filename, struct file *file,
		  char *in_memory_version_string, int uncached, progress_msg_id msg_id, int resume)
{
	CURLcode curl_ret;
	long ret = 0;
	int fd;
	FILE *f = NULL;
	struct curl_slist *header_fields = NULL;
	struct stat stat;
	int err;
	bool use_ssl = false;

	if (strncmp(url, content_server_urls[1], strlen(content_server_urls[1])) == 0)
		use_ssl = true;

	if (in_memory_version_string == NULL)
		LOG_INFO(file, "Downloading file", class_curl, "%s", filename);
	else
		LOG_INFO(file, "Downloading file to memory", class_curl, "%s", filename);

	if (!curl)
		abort();
	curl_easy_reset(curl);

	if (in_memory_version_string == NULL) {
		if (resume && lstat(filename, &stat) == 0) {
			curl_ret = curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t) stat.st_size);
			if (curl_ret != CURLE_OK)
				goto exit_closed;
		} else {
			unlink(filename);
		}

		fd = open(filename, O_CREAT | O_RDWR , 00600);
		if (fd < 0) {
			LOG_ERROR(file, "Cannot open file for write", class_file_io, "\\*filename=\"%s\",strerror=\"%s\"*\\", filename, strerror(errno));
			return -1;
		}

		f = fdopen(fd, resume ? "a" : "w");
		if (!f) {
			LOG_ERROR(file, "Cannot fdopen file for write", class_file_io, "\\*filename=\"%s\",strerror=\"%s\"*\\", filename, strerror(errno));
			close(fd);
			return -1;
		}

		curl_ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
		if (curl_ret != CURLE_OK)
			goto exit;
	} else {
		curl_ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_download_version_to_memory);
		if (curl_ret != CURLE_OK)
			goto exit;
		curl_ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)in_memory_version_string);
		if (curl_ret != CURLE_OK)
			goto exit;
	}

	curl_ret = curl_easy_setopt(curl, CURLOPT_URL, url);
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = swupd_curl_set_basic_options(curl, use_ssl);
	if (curl_ret != CURLE_OK)
		goto exit;

	if (uncached) {
		curl_ret = curl_easy_setopt(curl, CURLOPT_COOKIE, "request=uncached");
		if (curl_ret != CURLE_OK)
			goto exit;
	}

	if (msg_id != PROGRESS_MSG_NONE) {
		curl_progress_msg_id = msg_id;

		curl_ret = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		if (curl_ret != CURLE_OK)
			goto exit;

		curl_ret = curl_easy_setopt(curl, CURLOPT_XFERINFODATA, curl);
		if (curl_ret != CURLE_OK)
			goto exit;

		curl_ret = curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, swupd_curl_progress);
		if (curl_ret != CURLE_OK)
			goto exit;

		progress_step_ongoing(msg_id, 0, 0);
	}

	header_fields = make_header_fields();
	curl_ret = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_fields);
	if (curl_ret != CURLE_OK) {
		curl_slist_free_all(header_fields);
		goto exit;
	}

	curl_ret = curl_easy_perform(curl);
	curl_slist_free_all(header_fields);
	if (curl_ret == CURLE_OK)
		curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);

exit:
	if (f) {
		fclose(f);
	}

exit_closed:
	if (curl_ret == CURLE_OK) {
		/* curl command succeeded, download might've failed, let our caller LOG_ */
		switch (ret) {
			case 200:
			case 206:
				err = 0;
				break;
			case 403:
				err = -EACCES;
				break;
			case 404:
				err = -ENOENT;
				break;
			default:
				err = -1;
				break;
		}
	} else { /* download failed but don't LOG_ here, let our caller do it */
		switch (curl_ret) {
			case CURLE_COULDNT_RESOLVE_PROXY:
			case CURLE_COULDNT_RESOLVE_HOST:
			case CURLE_COULDNT_CONNECT:
				err = -ENONET;
				break;
			case CURLE_PARTIAL_FILE:
			case CURLE_RECV_ERROR:
				err = -ENOLINK;
				break;
			case CURLE_WRITE_ERROR:
				err = -EIO;
				break;
			case CURLE_OPERATION_TIMEDOUT:
				err = -ETIMEDOUT;
				break;
			default :
				err = -1;
				break;
		}
	}

	if (err) {
		if (resume == 0) {
			LOG_DEBUG(file, "Deleting partial download", class_curl, "%s", filename);
			unlink(filename);
		} else {
			LOG_DEBUG(file, "Keeping partial download", class_curl, "%s", filename);
		}
	}

	return err;
}

static struct curl_slist *make_header_fields(void)
{
	struct curl_slist *l = NULL;

	l = add_hdr_s(l, "X-Swupd-OEM-Name", oem_name);
	l = add_hdr_s(l, "X-Swupd-OEM-Board", oem_board);
	l = add_hdr_i(l, "X-Swupd-Current-Version", curr_version);
	l = add_hdr_i(l, "X-Swupd-Requested-Version", req_version);
	return l;
}

static struct curl_slist *add_hdr_s(struct curl_slist *l, const char *name,
				    const char *value)
{
	char *s;

	if (asprintf(&s, "%s: %s", name, value) < 0) abort();
	l = curl_slist_append(l, s);
	if (l == NULL)
		abort();
	free(s);
	return l;
}

static struct curl_slist *add_hdr_i(struct curl_slist *l, const char *name,
				    int value)
{
	char *s;

	if (asprintf(&s, "%s: %d", name, value) < 0) abort();
	l = curl_slist_append(l, s);
	if (l == NULL)
		abort();
	free(s);
	return l;
}

static CURLcode swupd_curl_set_security_opts(CURL *curl)
{
	CURLcode curl_ret = CURLE_OK;

	curl_ret = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, true);
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
	if (curl_ret != CURLE_OK)
		goto exit;
	// TODO: change this to to use tlsv1.2 when it is supported and enabled
	curl_ret = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_0);
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "HIGH");
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, "/usr/share/clear/update-ca/425b0f6b.key");
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_CAPATH , UPDATE_CA_CERTS_PATH);
	if (curl_ret != CURLE_OK)
		goto exit;

	// TODO: add the below when you know the paths:
	//curl_easy_setopt(curl, CURLOPT_CRLFILE, path-to-cert-revoc-list);
	//if (curl_ret != CURLE_OK)
	//	goto exit;

exit:
	return curl_ret;
}

CURLcode swupd_curl_set_basic_options(CURL *curl, bool ssl)
{
	CURLcode curl_ret = CURLE_OK;
#warning SECURITY HOLE since we can't SSL pin arbitrary servers
	if (ssl == true) {
		curl_ret = swupd_curl_set_security_opts(curl);
		if (curl_ret != CURLE_OK)
			goto exit;
	}

	curl_ret = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, SWUPD_CURL_CONNECT_TIMEOUT);
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, SWUPD_CURL_LOW_SPEED_LIMIT);
	if (curl_ret != CURLE_OK)
		goto exit;

	curl_ret = curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, SWUPD_CURL_RCV_TIMEOUT);
	if (curl_ret != CURLE_OK)
		goto exit;

#warning setup a means to validate IPv6 works end to end
	curl_ret = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	if (curl_ret != CURLE_OK)
		goto exit;

exit:
	return curl_ret;
}
