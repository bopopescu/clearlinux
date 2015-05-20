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
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <curl/curl.h>

#include <swupd.h>
#include <swupd_bsdiff.h>
#include <executor.h>

static int needs_mcurl_finish = 0;

static CURLM *mcurl = NULL;

static int downloadcount = 0;

static struct executor *executor;

static struct list *easy_curl_list = NULL;

static void untar_full_download(void *data);
static int poll_fewer_than(int xfer_queue_high, int xfer_queue_low);
static int perform_curl_io_and_complete(int *left);

/* don't do unlimited parallel downloads, we'll run out of fd's */
int MAX_XFER = 25;
int MAX_XFER_BOTTOM = 15;

int start_full_download(int attempt_number)
{
	int nworkers = (attempt_number == 1) ? (int) sysconf(_SC_NPROCESSORS_ONLN) : 1;

	mcurl = curl_multi_init();
	if (mcurl == NULL) {
		LOG_ERROR(NULL, "curl_multi_init failed, cannot download full files", class_curl, "");
		return -1;
	}

	executor = executor_create(nworkers, 10*nworkers, true);
	if (executor == NULL)
		LOG_WARN(NULL, "Could not create full download threadpool, continuing single-threaded.", class_thread, "");

	downloadcount = 0;

	/*
	 * we want to not do HTTP pipelining once things have failed once.. in case some transpoxy in the middle
	 * is even more broken than average. This at least will allow the user to update, albeit slowly.
	 */
	if (attempt_number == 1) {
		if (curl_multi_setopt(mcurl, CURLMOPT_PIPELINING, 1) != CURLM_OK)
			LOG_WARN(NULL, "Could not set opt CURLMOPT_PIPELINING for full file download, continuing...", class_curl, "");
	} else {
		/* survival: don't go too parallel in the retry loop */
		MAX_XFER = 1;
		MAX_XFER_BOTTOM = 1;
	}

	return 0;
}

static void free_curl_list_data(void *data)
{
	CURL *curl = data;

	curl_multi_remove_handle(mcurl, curl);
	curl_easy_cleanup(curl);
}

static void clean_curl_multi_queue(void)
{
	list_free_list_and_data(easy_curl_list, free_curl_list_data);
	easy_curl_list = NULL;

	needs_mcurl_finish = 0;
}

void end_full_download(void)
{
	int left;
	int err;
	CURLMcode curlm_ret;

	if (needs_mcurl_finish) {
		err = poll_fewer_than(0, 0);
		if (err == 0)
			err = perform_curl_io_and_complete(&left);
		if (err) {
			LOG_WARN(NULL, "end_full_download failed to finish handling the curl multi queue", class_curl, "");
			clean_curl_multi_queue();
		}
	}

	if (executor) {
		LOG_DEBUG(NULL, "Waiting for fullfiles thread pools to finish", class_thread, "");
		executor_destroy(executor, true);
	}

	curlm_ret = curl_multi_cleanup(mcurl);
	if (curlm_ret != CURLM_OK)
		LOG_WARN(NULL, "end_full_download failed to curl_multi_cleanup()", class_curl,
				"\\*curlm_ret=\"%d\"*\\", curlm_ret);
}

/* list the tarfile content, and verify it contains only one line equal to the expected hash.
 * loop through all the content to detect the case where archive contains more than one file.
 */
static int check_tarfile_content(struct file *file, const char *tarfilename)
{
	int err;
	char *tarcommand;
	FILE *tar;

	/* we're using -a because the server side has a choice between different compression methods */
	if (asprintf(&tarcommand, "tar --warning=no-timestamp -atf %s/download/%s.tar 2> /dev/null", STATE_DIR, file->hash) < 0)
		abort();

	err = access(tarfilename, R_OK);
	if (err) {
		LOG_ERROR(file, "Cannot access tarfilename", class_file_io, "\\*tar_filename=\"%s\",strerror=\"%s\"*\\",
				tarfilename, strerror(errno));
		goto free_tarcommand;
	}

	tar = popen(tarcommand, "r");
	if (tar == NULL) {
		LOG_ERROR(file, "Cannot popen tarcommand", class_file_io, "\\*tar_command=\"%s\"*\\", tarcommand);
		err = -1;
		goto free_tarcommand;
	}

	while (!feof(tar)) {
		char *c;
		char buffer[PATH_MAXLEN];

		if (fgets(buffer, PATH_MAXLEN, tar) == NULL)
			break;

		c = strchr(buffer, '\n');
		if (c) *c  = 0;
		if (c && (c != buffer) && (*(c-1)=='/')) *(c-1) = 0; /* strip trailing '/' from directory tar */
		if (strcmp(buffer, file->hash) != 0) {
			LOG_WARN(file, "Malicious tar file downloaded", class_security,
					"\\*filename=\"%s\",hash=\"%s\",buffer=\"%s\"*\\",
					file->filename, file->hash, buffer);
			err = -1;
			break;
		}
	}

	pclose(tar);

free_tarcommand:
	free(tarcommand);

	return err;
}

static void untar_full_download(void *data)
{
	struct file *file = data;
	char *tarfilename;
	char *tarfilenamedot;
	char *newfile;
	struct stat stat;
	int err;
	char *tarcommand;

	if (asprintf(&tarfilenamedot, "%s/download/.%s.tar", STATE_DIR, file->hash) < 0)
		abort();
	if (asprintf(&tarfilename, "%s/download/%s.tar", STATE_DIR, file->hash) < 0)
		abort();

	/* the renamed tarfile may already exist at this point */
	if (lstat(tarfilename, &stat) != 0) {
		err = rename(tarfilenamedot, tarfilename);
		if (err) {
			LOG_ERROR(file, "Cannot rename tarfile", class_file_io, "\\*tar_filename_dot=\"%s\",strerror=\"%s\"*\\",
					tarfilenamedot, strerror(errno));
			free(tarfilenamedot);
			goto exit;
		}
	}
	free(tarfilenamedot);

	err = check_tarfile_content(file, tarfilename);
	if (err)
		goto exit;

	/* we're using -a because the server side has a choice between different compression methods and will pick the smallest result */
	if (asprintf(&tarcommand, "tar --directory=%s/staged/ --warning=no-timestamp " TAR_PERM_ATTR_ARGS " -axf %s 2> /dev/null",
			STATE_DIR, tarfilename) < 0)
		abort();

	LOG_DEBUG(file, "Doing tar operation", class_file_compression, "%s", tarcommand);
	err = system(tarcommand);
	if (err) {
		/* symlink untars may have perm/xattr complaints and non-zero
		 * tar return, but symlink (probably?) untarred ok.
		 *
		 * Also getting complaints on some new regular files.
		 *
		 * Either way we verify the hash later and try to recover. */
		LOG_ERROR(file, "Tar command error (ignoring)", class_file_compression, "\\*err=\"%i\"*\\", err);
		log_stdout("ignoring tar \"error\" for %s\n", file->hash);
	}
	free(tarcommand);

	if (asprintf(&newfile, "%s/staged/%s", STATE_DIR, file->hash) < 0)
		abort();
	err = lstat(newfile, &stat);
	if (err)
		LOG_ERROR(file, "newfile stat error after untar", class_file_io, "\\*new_file=\"%s\",strerror=\"%s\"*\\",
				newfile, strerror(errno));

	unlink(tarfilename);

	free(newfile);

exit:
	free(tarfilename);
	if (err)
		unlink_all_staged_content(file);
}

static int perform_curl_io_and_complete(int *left)
{
	CURLMsg *msg;
	long ret;
	CURLMcode curlm_ret;
	CURLcode curl_ret;

	curlm_ret = curl_multi_perform(mcurl, left);
	if (curlm_ret != CURLM_OK) {
		LOG_ERROR(NULL, "perform_curl_io_and_complete failed to curl_multi_perform(1)", class_curl,
				"\\*curlm_ret=\"%d\"*\\", curlm_ret);
		return -1;
	}

	do {
		msg = curl_multi_info_read(mcurl, left);
		if (!msg)
			break;
		if (msg->msg == CURLMSG_DONE) {
			CURL *handle;
			FILE *fp;
			struct file *file;
			handle = msg->easy_handle;
			struct list *item;
			curl_ret = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &ret);
			if (curl_ret == CURLE_OK)
				curl_ret = curl_easy_getinfo(handle, CURLINFO_PRIVATE, (char **)&file);
			if (curl_ret == CURLE_OK) {
				fp = file->priv;
				fflush(fp);
				fclose(fp);
				file->priv = NULL;

				if (ret == 200) {
					if (executor)
						executor_submit_task(executor, untar_full_download, file);
					else
						untar_full_download(file);
				} else {
					LOG_ERROR(file, "http response failure", class_curl, "\\*http_code=\"%i\"*\\", ret);
					unlink_all_staged_content(file);
				}
			} else {
				LOG_ERROR(NULL, "perform_curl_io_and_complete failed to get done xfer info", class_curl,
						"\\*curl_ret=\"%d\"*\\", curl_ret);
			}
			item = list_find_data(easy_curl_list, handle);
			if (!item)
				abort();
			easy_curl_list = list_free_item(item, NULL);
			curl_multi_remove_handle(mcurl, handle);
			curl_easy_cleanup(handle);
		} else {
			LOG_WARN(NULL, "Unknown CURL MSG", class_curl, "\\*curl_msg=\"%d\"*\\", msg->msg);
		}
	} while (msg != NULL);

	curlm_ret = curl_multi_perform(mcurl, left);
	if (curlm_ret != CURLM_OK) {
		LOG_ERROR(NULL, "perform_curl_io_and_complete failed to curl_multi_perform(2)", class_curl,
				"\\*curlm_ret=\"%d\"*\\", curlm_ret);
		return -1;
	}

	return 0;
}

/* 2 limits, so that we can have hysteresis in behavior. We let the caller
 * add new transfer up until the queue reaches the high threshold. At this point
 * we don't return to the caller and handle the queue until its len
 * gets below the low threshold */
static int poll_fewer_than(int xfer_queue_high, int xfer_queue_low)
{
	int left;
	CURLMcode curlm_ret;

	curlm_ret = curl_multi_perform(mcurl, &left);
	if (curlm_ret != CURLM_OK) {
		LOG_ERROR(NULL, "poll fewer than failed to curl_multi_perform()", class_curl,
				"\\*curlm_ret=\"%d\"*\\", curlm_ret);
		return -1;
	}

	if (left <= xfer_queue_high)
		return 0;

	LOG_DEBUG(NULL, "poll fewer than", class_curl, "xfer_queue_high %i, left %i", xfer_queue_high, left);
	while (left > xfer_queue_low) {
		usleep(500); /* TODO this really ought to be a select() statement */
		if (perform_curl_io_and_complete(&left) != 0)
			return -1;
	}

	return 0;
}

/* full_download() attempts to enqueue a file for later asynchronous download.
 * Returns an error only in case of a fatal error that prevents it from
 * continuing running the multi queue.  Failure to enqueue a new download only
 * outputs a LOG_ERROR, but doesn't return an error.  That assumes something
 * else will follow later and synchronously download any remaining missing files. */
int full_download(struct file *file)
{
	FILE *tarfile = NULL;
	char *url = NULL;
	CURL *curl;
	int err;
	char filename[PATH_MAXLEN];
	struct stat stat;
	CURLMcode curlm_ret = CURLM_OK;
	CURLcode curl_ret;
	struct list *item;
	bool use_ssl = false;

	/* if STATE_DIR/staged/HASH exists, then the file is already downloaded
	 * and untarred, so nothing to do */
	sprintf(filename, "%s/staged/%s", STATE_DIR, file->hash);
	if (lstat(filename, &stat) == 0)
		return 0;

	err = poll_fewer_than(MAX_XFER, MAX_XFER_BOTTOM);
	if (err) {
		clean_curl_multi_queue();
		return err;
	}

	/* if STATE_DIR/download/HASH.tar exists, then the file is already downloaded
	 * but not yet untarred, so untar it and return */
	sprintf(filename, "%s/download/%s.tar", STATE_DIR, file->hash);
	if (lstat(filename, &stat) == 0) {
		if (executor)
			executor_submit_task(executor, untar_full_download, file);
		else
			untar_full_download(file);
		return 0;
	}

	sprintf(filename, "%s/download/.%s.tar", STATE_DIR, file->hash);
	unlink(filename);

	/* no STATE_DIR/{download,staged}/HASH* exists, so download file */

	tarfile = fopen_exclusive(filename);
	if (!tarfile) {
		LOG_ERROR(file, "Could not open exclusive", class_file_io, "\\*filename=\"%s\",strerror=\"%s\"*\\",
				filename, strerror(errno));
		return 0;
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		LOG_ERROR(file, "full_download failed to curl_easy_init()", class_curl, "");
		fclose(tarfile);
		return 0;
	}

	if (asprintf(&url, "%s/%i/files/%s.tar", preferred_content_url, file->last_change, file->hash) < 0)
		abort();
	curl_ret = curl_easy_setopt(curl, CURLOPT_URL, url);
	if (curl_ret != CURLE_OK)
		goto clean_curl;

	if (strncmp(url, content_server_urls[1], strlen(content_server_urls[1])) == 0)
		use_ssl = true;

	file->priv = tarfile;
	curl_ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, tarfile);
	if (curl_ret != CURLE_OK)
		goto clean_curl;

	curl_ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, file);
	if (curl_ret != CURLE_OK)
		goto clean_curl;

	curl_ret = swupd_curl_set_basic_options(curl, use_ssl);
	if (curl_ret != CURLE_OK)
		goto clean_curl;

	if ((item = list_append_data(easy_curl_list, curl)) == NULL) {
		LOG_ERROR(file, "full_download failed to list_append_data(curl item)", class_curl, "");
		goto clean_curl;
	}
	easy_curl_list = item;

	curlm_ret = curl_multi_add_handle(mcurl, curl);
	if (curlm_ret != CURLM_OK) {
		easy_curl_list = list_free_item(item, NULL);
		goto clean_curl;
	}

	needs_mcurl_finish = 1;

	LOG_INFO(file, "Downloading full file", class_curl, "%i/files/%s.tar", file->last_change, file->hash);
	err = poll_fewer_than(MAX_XFER + 10, MAX_XFER);
	if (err)
		clean_curl_multi_queue();
	goto free_url;

clean_curl:
	if (curlm_ret != CURLM_OK)
		LOG_ERROR(file, "full_download failed to curl_multi_add_handle()", class_curl,
				"\\*curlm_ret=\"%d\"*\\", curlm_ret);
	else
		LOG_ERROR(file, "full_download failed to set curl options", class_curl,
				"\\*curl_ret=\"%d\"*\\", curl_ret);
	curl_easy_cleanup(curl);
	file->priv = NULL;
	fclose(tarfile);

free_url:
	free(url);

	return err;
}
