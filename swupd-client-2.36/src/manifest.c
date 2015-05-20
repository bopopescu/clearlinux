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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>

#include <swupd.h>
#include <xattrs.h>
#include <executor.h>
#include "progress.h"
#include <signature.h>

static struct executor *executor;
static int threaded_hash_compute_result;

#define MANIFEST_LINE_MAXLEN 8192

int file_sort_hash(const void* a, const void* b)
{
	struct file *A, *B;
	A = (struct file *) a;
	B = (struct file *) b;

	return strcmp(A->hash, B->hash);
}

/* sort by full path filename */
int file_sort_filename(const void* a, const void* b)
{
	struct file *A, *B;
	int ret;
	A = (struct file *) a;
	B = (struct file *) b;

	ret = strcmp(A->filename, B->filename);
	if (ret)
		return ret;
	return 0;
}

int file_sort_version(const void* a, const void* b)
{
	struct file *A, *B;
	A = (struct file *) a;
	B = (struct file *) b;

	if (A->last_change < B->last_change)
		return -1;
	if (A->last_change > B->last_change)
		return 1;

	return strcmp(A->filename, B->filename);
}

static int file_found_in_older_manifest(struct manifest *from_manifest, struct file *searched_file)
{
	struct list *list;
	struct file *file;

	list = list_head(from_manifest->files);
	while (list) {
		file = list->data;
		list = list->next;

		if (file->is_deleted)
			continue;
		if (!strcmp(file->filename, searched_file->filename)) {
			return 1;
		}
	}

	return 0;
}

static int file_has_different_hash_in_older_manifest(struct manifest *from_manifest, struct file *searched_file)
{
	struct list *list;
	struct file *file;

	list = list_head(from_manifest->files);
	while (list) {
		file = list->data;
		list = list->next;

		if (file->is_deleted)
			continue;
		if (!strcmp(file->filename, searched_file->filename) &&
				strcmp(file->hash, searched_file->hash)) {
			return 1;
		}
	}

	return 0;
}

struct manifest *manifest_from_file(int version, char *component)
{
	FILE *infile;
	char line[MANIFEST_LINE_MAXLEN], *c, *c2;
	int count = 0;
	int deleted = 0;
	struct manifest *manifest;
	char *filename;
	uint64_t contentsize = 0;
	int manifest_hdr_version;
	int manifest_enc_version;

	if (asprintf(&filename, "%s/%i/Manifest.%s", STATE_DIR, version, component) < 0)
		abort();

	LOG_INFO(NULL, "Reading manifest", class_manifest, "%s", filename);
	infile = fopen(filename, "rb");

	if (infile == NULL) {
		LOG_DEBUG(NULL, "Cannot open manifest", class_manifest, "%s %s", filename, strerror(errno));
		free(filename);
		return NULL;
	}
	free(filename);

	/* line 1: MANIFEST\t<version> */
	line[0] = 0;
	if (fgets(line, MANIFEST_LINE_MAXLEN - 1, infile) == NULL)
		goto err_nofree;

	if (strncmp(line, "MANIFEST\t", 9)!=0)
		goto err_nofree;

	c = &line[9];
	manifest_enc_version = strtoull(c, NULL, 10);
	if (manifest_enc_version == 0)
		goto err_nofree;

	line[0] = 0;
	while (strcmp(line, "\n")!=0) {
		/* read the header */
		line[0] = 0;
		if (fgets(line, MANIFEST_LINE_MAXLEN - 1, infile) == NULL)
			break;
		c = strchr(line, '\n');
		if (c)
			*c = 0;
		else
			goto err_nofree;

		if (strlen(line) == 0)
			break;
		c = strchr(line, '\t');
		if (c)
			c++;
		else
			goto err_nofree;

		if (strncmp(line,"version:", 8) == 0) {
			manifest_hdr_version = strtoull(c, NULL, 10);
			if (manifest_hdr_version != version) {
				LOG_ERROR(NULL, "Corrupt manifest", class_manifest, "\\*manifest_header_version=\"%i\",filename_version=\"%i\"*\\",
						manifest_hdr_version, version);
				goto err_close;
			}
		}
		if (strncmp(line,"contentsize:", 12) == 0) {
			contentsize = strtoull(c, NULL, 10);
		}
	}

	manifest = alloc_manifest(version, component);
	if (manifest == NULL)
		goto err_close;

	manifest->contentsize = contentsize;
	manifest->manifest_version = manifest_enc_version;

	/* empty line */
	while (!feof(infile)) {
		struct file *file;

		line[0] = 0;
		if (fgets(line, MANIFEST_LINE_MAXLEN - 1, infile) == NULL)
			break;
		c = strchr(line, '\n');
		if (c) *c = 0;
		if (strlen(line) == 0)
			break;

		file = calloc(1, sizeof(struct file));
		if (file == NULL)
			abort();
		c = line;

		c2 = strchr(c, '\t');
		if (c2) { *c2 = 0; c2++;};

		if (c[0] == 'F') {
			file->is_file = 1;
		} else if (c[0] == 'D') {
			file->is_dir = 1;
		} else if (c[0] == 'L') {
			file->is_link = 1;
		} else if (c[0] == 'M') {
			file->is_manifest = 1;
		} else if (c[0] != '.') { /* unknown file type */
			free(file);
			goto err;
		}

		if (c[1] == 'd') {
			file->is_deleted = 1;
			deleted++;
		} else if (c[1] != '.') { /* unknown modifier #1 */
			free(file);
			goto err;
		}

		if (c[2] == 'C') {
			file->is_config = 1;
		} else if (c[2] == 's') {
			file->is_state = 1;
		} else if (c[2] == 'b') {
			file->is_boot = 1;
		} else if (c[2] != '.') { /* unknown modifier #2 */
			free(file);
			goto err;
		}

		if (c[3] == 'r') {
			file->is_rename = 1;
		} else if (c[3] != '.') { /* unknown modifier #3 */
			free(file);
			goto err;
		}

		c = c2;
		if (!c) {
			free(file);
			continue;
		}
		c2 = strchr(c, '\t');
		if (c2) {
			*c2 = 0;
			c2++;
		} else {
			free(file);
			goto err;
		}

		file->hash = strdup(c);
		if (!file->hash)
			abort();

		c = c2;
		c2 = strchr(c, '\t');
		if (c2) {
			*c2 = 0;
			c2++;
		} else {
			free(file->hash);
			free(file);
			goto err;
		}

		file->last_change = strtoull(c, NULL, 10);

		c = c2;

		file->filename = strdup(c);
		if (!file->filename)
			abort();

		if (file->is_manifest)
			manifest->manifests = list_prepend_data(manifest->manifests, file);
		else
			manifest->files = list_prepend_data(manifest->files, file);
		count ++;
	}

	fclose(infile);
	LOG_DEBUG(NULL, "Manifest summary", class_manifest, "Manifest for version %i/%s contains %i files", version, component, count - deleted);
	return manifest;
err:
	free_manifest(manifest);
err_nofree:
	LOG_ERROR(NULL, "Corrupt manifest", class_manifest, "");
err_close:
	fclose(infile);
	return NULL;

}

static void free_file_data(void *data)
{
	struct file *file = (struct file *) data;

	/* peer and deltapeer are pointers to files contained
	 * in another list and must not be disposed */

	if (file->filename)
		free(file->filename);

	if (file->hash)
		free(file->hash);

	if (file->header)
		free(file->header);

	if (file->priv) {
		fflush((FILE *)file->priv);
		fclose((FILE *)file->priv);
	}

	if (file->dotfile)
		free(file->dotfile);

	free(file);
}

static void free_manifest_data(void *data)
{
	struct manifest *manifest = (struct manifest *) data;

	free_manifest(manifest);
}

void free_manifest(struct manifest *manifest)
{
	if (!manifest)
		return;

	list_free_list_and_data(manifest->files, free_file_data);
	if (manifest->manifests)
		list_free_list_and_data(manifest->manifests, free_file_data);
	if (manifest->submanifests)
		list_free_list_and_data(manifest->submanifests, free_manifest_data);
	free(manifest->component);
	free(manifest);
}

static int try_delta_manifest_download(int current, int new, char *component, struct file *file)
{
	char *original = NULL;
	char *newfile = NULL;
	char *deltafile = NULL;
	char *url = NULL;
	char *hash = NULL;
	int ret = 0;
	struct stat buf;

	if (strcmp(component, "MoM") == 0) {
#warning need to crypto validate MoM and allow delta
		return -1;
	}

	if (asprintf(&original, "%s/%i/Manifest.%s", STATE_DIR, current, component) < 0) {
		ret = -ENOMEM;
		goto out;
	}
	hash = compute_hash(file, original);
	if (hash == NULL) {
		LOG_ERROR(file, "hash computation failed", class_hash,
			"\\*computedhash=NULL,expectedhash=\"%s\",manifest=\"%i/Manifest-%s-delta-from-%i\"*\\",
			file->hash, new, component, current);
		goto out;
	}
	ret = strcmp(file->hash, hash);
	if (ret != 0) {
		LOG_ERROR(NULL, "delta manifest input mismatch", class_security,
			"\\*computedhash=\"%s\",expectedhash=\"%s\",manifest=\"%i/Manifest-%s-delta-from-%i\"*\\",
			hash, file->hash, new, component, current);
		goto out;
	}

	if (asprintf(&deltafile, "%s/Manifest-%s-delta-from-%i-to-%i", STATE_DIR, component, current, new) < 0) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&buf, 0, sizeof(struct stat));
	ret = stat(deltafile, &buf);
	if (ret || buf.st_size == 0) {
		LOG_DEBUG(NULL, "downloading delta manifest", class_manifest,
				"\\*ret=\"%i\",size=\"%i\",component=\"%s\",new=\%i\"*\\",
				ret, buf.st_size, component, new);

		if (asprintf(&url, "%s/%i/Manifest-%s-delta-from-%i", preferred_content_url, new, component, current) < 0) {
			ret = -ENOMEM;
			goto out;
		}

		ret = swupd_curl_get_file(url, deltafile, NULL, NULL, 0, PROGRESS_MSG_NONE, 0);
		if (ret != 0) {
			LOG_DEBUG(NULL, "delta manifest download failed", class_curl, "%d for %i/Manifest-%s-delta-from-%i",
				  ret, new, component, current);
			unlink(deltafile);
			goto out;
		}

		if (!signature_download_and_verify(url, deltafile)) {
			LOG_ERROR(NULL, "manifest delta signature failed", class_security, "\\*file=\"%i/Manifest-%s-delta-from-%i\"*\\",
				  new, component, current);
			ret = -1;
			unlink(deltafile);
			goto out;
		}
	} else {
		LOG_INFO(NULL, "using existing manifest delta", class_manifest, "");
	}

	/* Now apply the manifest delta */

	if (asprintf(&newfile, "%s/%i/Manifest.%s", STATE_DIR, new, component) < 0) {
		ret = -ENOMEM;
		goto out;
	}

	ret = apply_bsdiff_delta(original, newfile, deltafile);
	if (ret != 0) {
		unlink(newfile);
	} else if ((ret = xattrs_compare(original, newfile)) != 0) {
		LOG_ERROR(NULL, "Manifest Delta patch xattrs copy failed", class_xattrs, "");
		unlink(newfile);
	}

	unlink(deltafile);
	signature_delete(deltafile);

out:
	free(original);
	free(url);
	free(hash);
	free(newfile);
	free(deltafile);

	return ret;
}

/* TODO: This should deal with nested manifests better */
static int retrieve_manifests(int current, int version, char *component, struct file *file, struct manifest **manifest)
{
	char *url;
	char *filename;
	char *dir;
	int ret = 0;
	char *tar;

	*manifest = NULL;

	if (preferred_content_url == NULL) {
		ret = pick_urls(NULL);
		if (ret != 0)
			return ret;
	}

	if(asprintf(&dir, "%s/%i", STATE_DIR, version) < 0)
		abort();
	ret = mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if ((ret != 0) && (errno != EEXIST))
		return ret;
	free(dir);

	if (current < version) {
		if (try_delta_manifest_download(current, version, component, file) == 0) {
			LOG_DEBUG(NULL, "Delta download and apply ok", class_delta, "");
			*manifest = manifest_from_file(version, component);
			if (*manifest) {
				LOG_DEBUG(NULL, "Manifest from Delta: read successful", class_manifest, "");
				return 0;
			} else
				LOG_WARN(NULL, "Manifest from Delta: read failed", class_manifest, "");
		}
	}

	if (asprintf(&filename, "%s/%i/Manifest.%s.tar", STATE_DIR, version, component) < 0)
		abort();

	if (asprintf(&url, "%s/%i/Manifest.%s.tar", preferred_content_url, version, component) < 0)
		abort();

	LOG_DEBUG(NULL, "downloading full manifest", class_manifest, "\\*component=\"%s\",version=\%i\"*\\",
				component, version);

	ret = swupd_curl_get_file(url, filename, NULL, NULL, 0, PROGRESS_MSG_NONE, 0);
	if (ret) {
		LOG_ERROR(NULL, "Manifest retrieval failed", class_manifest, "\\*ret=\"%d\",file=\"%i/Manifest.%s.tar\"*\\",
			  ret, version, component);
		unlink(filename);
		goto out;
	}

	if (!signature_download_and_verify(url, filename)) {
		LOG_ERROR(NULL, "manifest signature failed", class_security, "\\*file=\"%i/Manifest.%s.tar\"*\\",
			  version, component);
		unlink(filename);
		goto out;
	}

	if (asprintf(&tar, "tar --directory=%s/%i --warning=no-timestamp -axf %s/%i/Manifest.%s.tar 2> /dev/null",
			STATE_DIR, version, STATE_DIR, version, component) < 0)
		abort();

	LOG_DEBUG(NULL, "tar", class_file_compression, "running %s", tar);
	/* this is is historically a point of odd errors */
	ret = system(tar);
	free(tar);
	if (ret != 0) {
		goto out;
	}

	*manifest = manifest_from_file(version, component);
	if (*manifest) {
		LOG_DEBUG(NULL, "Manifest from Tar: read successful", class_manifest, "");
	} else {
		LOG_WARN(NULL, "Manifest from Tar: read failed", class_manifest, "");
	}
out:
	free(filename);
	free(url);
	return ret;
}

//NOTE: file==NULL when component=="MoM", else file->hash is needed
int load_manifests(int current, int version, char *component, struct file *file, struct manifest **manifest)
{
	int ret = 0;

	*manifest = manifest_from_file(version, component);

	if (*manifest == NULL) {
		ret = prep_mount(O_RDWR);
		if (ret != 0)
			return ret;

		ret = retrieve_manifests(current, version, component, file, manifest);
	}

	return ret;
}

/* Find files which need updated based on deltas in last_change.
   Should let further do_not_update policy be handled in the caller, but for
   now some hacky exclusions are done here. */
struct list *create_update_list(struct manifest *current, struct manifest *server)
{
	struct list *output = NULL;
	struct list *list;

	update_count = 0;
	update_skip = 0;
	list = list_head(server->files);
	while (list) {
		struct file *file;
		file = list->data;
		list = list->next;

		if ((file->last_change > current->version) ||
				(file->is_rename && file_has_different_hash_in_older_manifest(current, file))) {

			/* check and if needed mark as do_not_update */
			ignore(file);

			output = list_prepend_data(output, file);
			LOG_DEBUG(NULL, "Pending update", class_undef, "%s", file->filename);
		}
	}
	update_count = list_len(output) - update_skip;
	LOG_INFO(NULL, "Initial update count", class_undef,
		"update_count=%i, update_skip=%i", update_count, update_skip);
	return output;
}

/* create a difference list using names and hashes
 * m1: local system manifest
 * m2: official server manifest
 * NOTE: The official server manifest may be intentionally empty, in which
 *       case local system manifest should also be empty and that should be
 *       a valid match.  For this to not return a false positive match of
 *       zero differences, callers of this function must insure they have
 *       truly received a valid empty manifest from the server and
 *       otherwise not call this function.
 * The difference list will be complete, but files have flags set so
 * that later a given file may be ignored in reporting or skipped in
 * fixing/updating.
 */
struct list *create_difference_list(struct manifest *m1, struct manifest *m2)
{
	struct list *difference = NULL;
	struct list *list1, *list2;
	struct file *file1, *file2;
	int ret;

	m1->files = list_sort(m1->files, file_sort_filename);
	m2->files = list_sort(m2->files, file_sort_filename);

	list1 = list_head(m1->files);
	list2 = list_head(m2->files);

	/* search for differences */
	while (list1 && list2) {
		file1 = list1->data;
		file2 = list2->data;

		ret = strcmp(file1->filename, file2->filename);
		if (ret == 0) { /* m1/file1 matches m2/file2 */
			list1 = list1->next;
			list2 = list2->next;

			if (file1->hash == NULL) {
				LOG_WARN(NULL, "file1 hash null", class_hash, "\\*file1_name=\"%s\"*\\",
						file1->filename);
			}
			if (file2->hash == NULL) {
				LOG_WARN(NULL, "file2 hash null", class_hash, "\\*file2_name=\"%s\"*\\",
						file2->filename);
			}
			if ((file1->hash != NULL) && (file2->hash != NULL) &&
			    (strcmp(file1->hash, file2->hash) != 0)) {

				file1->is_orphan = file2->is_deleted;

				/* heuristics applied at local manifest creation time,
				 * but also need to factor in server hints */
				file1->is_config |= file2->is_config;
				file1->is_state |= file2->is_state;
				file1->is_boot |= file2->is_boot;

				difference = list_prepend_data(difference, file2);
				LOG_DEBUG(NULL, "Found difference (hash)", class_hash,
					"%s", file1->filename);
			}
		} else if (ret < 0) { /*  m1/file1 is BEFORE m2/file2 := extra local file */
			list1 = list1->next;

			apply_heuristics(file1);

			difference = list_prepend_data(difference, file1);
			/* useful debug output, normally too verbose
			LOG_DEBUG(NULL, "Found difference (extra local file)", class_file_misc, "%s", file1->filename);
			*/
		} else {/* ret > 0        m1/file1 is AFTER  m2/file2 := missing server file */
			list2 = list2->next;
			/* server manifest may have old deleted files listed, others may be of interest */
			if (!file2->is_deleted) {
				if (file2->is_state) /* ignore manifest state trash */
					continue;

				difference = list_prepend_data(difference, file2);
				LOG_DEBUG(NULL, "Found difference (missing server file)", class_file_misc, "%s", file2->filename);
			}
		}
	}

	/* We finish the loop above when either list is empty.  Now we may need
	 * to drain the other list: */
	while (list1) { /* extra local files */
		file1 = list1->data;
		list1 = list1->next;

		apply_heuristics(file1);

		difference = list_prepend_data(difference, file1);
		/* useful debug output, normally too verbose
		LOG_DEBUG(NULL, "Found difference (extra local file)", class_file_misc, "%s", file1->filename);
		*/
	}
	while (list2) { /* missing server files */
		file2 = list2->data;
		list2 = list2->next;
		/* server manifest may have old deleted files listed, others may be of interest */
		if (!file2->is_deleted){
			if (file2->is_state) /* ignore manifest state trash */
				continue;

			difference = list_prepend_data(difference, file2);
			LOG_DEBUG(NULL, "Found difference (missing server file)", class_file_misc, "%s", file2->filename);
		}
	}

	return difference;
}

/* m1: old (or current when verifying) manifest
 * m2: new (or official if verifying) manifest */
void link_manifests(struct manifest *m1, struct manifest *m2)
{
	struct list *list1, *list2;
	struct file *file1, *file2;

	m1->files = list_sort(m1->files, file_sort_filename);
	m2->files = list_sort(m2->files, file_sort_filename);

	list1 = list_head(m1->files);
	list2 = list_head(m2->files);

	while (list1 && list2) { /* m1/file1 matches m2/file2 */
		int ret;
		file1 = list1->data;
		file2 = list2->data;

		ret = strcmp(file1->filename, file2->filename);
		if (ret == 0) {
			if (!file1->is_deleted || file2->is_deleted) {
				file1->peer = file2;
				file2->peer = file1;
				file1->deltapeer = file2;
				file2->deltapeer = file1;
			}

			if (!file1->is_deleted && !file2->is_deleted && !strcmp(file1->hash, file2->hash))
				file2->last_change = file1->last_change;

			list1 = list1->next;
			list2 = list2->next;

			if (((file2->last_change == m2->version) || (file2->last_change > m1->version)) && !file2->is_deleted)
				account_changed_file();
			if (file2->last_change == m2->version && file2->is_deleted)
				account_deleted_file();
			continue;
		}
		if (ret < 0) { /*  m1/file1 is before m2/file2 */
			list1 = list1->next;
			account_deleted_file();
			continue;
		} /* else ret > 0  m1/file1 is after m2/file2 */
		list2 = list2->next;
		account_new_file();
	}
}

/* m1: old manifest
 * m2: new manifest */
void link_submanifests(struct manifest *m1, struct manifest *m2)
{
	struct list *list1, *list2;
	struct file *file1, *file2;

	m1->files = list_sort(m1->files, file_sort_filename);
	m2->files = list_sort(m2->files, file_sort_filename);

	list1 = list_head(m1->manifests);
	list2 = list_head(m2->manifests);

	while (list1 && list2) { /* m1/file1 matches m2/file2 */
		int ret;
		file1 = list1->data;
		file2 = list2->data;

		ret = strcmp(file1->filename, file2->filename);
		if (ret == 0) {
			file1->peer = file2;
			file2->peer = file1;
			file1->deltapeer = file2;
			file2->deltapeer = file1;
			list1 = list1->next;
			list2 = list2->next;

			if (((file2->last_change == m2->version) || (file2->last_change > m1->version)) && !file2->is_deleted)
				account_changed_manifest();
			if (file2->last_change == m2->version && file2->is_deleted)
				account_deleted_manifest();
			continue;
		}
		if (ret < 0) { /*  m1/file1 is before m2/file2 */
			list1 = list1->next;
			account_deleted_manifest();
			continue;
		} /* else ret > 0  m1/file1 is after m2/file2 */
		list2 = list2->next;
		account_new_manifest();
	}
}


struct manifest *alloc_manifest(int version, char *component)
{
	struct manifest *manifest;

	manifest = calloc(1, sizeof(struct manifest));
	if (manifest == NULL)
		abort();

	manifest->version = version;
	manifest->component = strdup(component);

	return manifest;
}

/* if component is specified explicitly, pull in submanifest only for that
 * if component is not specified, pull in any tracked component submanifest */
int recurse_manifest(struct manifest *manifest, const char *component)
{
	struct list *list;
	struct file *file;
	struct manifest *sub;
	int version1, version2;
	int err;

	manifest->contentsize = 0;
	list = manifest->manifests;
	while (list) {
		file = list->data;
		list = list->next;

		if (!component && !component_subscribed(file->filename))
			continue;

		if ( component && !(strcmp(component, file->filename) == 0))
			continue;

		version2 = file->last_change;
		if (file->peer)
			version1 = file->peer->last_change;
		else
			version1 = version2;
		if (version1 > version2)
			version1 = version2;

		if (prep_mount(O_RDWR) != 0)
			return -1;

		LOG_INFO(file, "Loading submanifest", class_manifest, "%i->%i", version1, version2);
		err = load_manifests(version1, version2, file->filename, file, &sub);
		if (err)
			return err;
		if (sub != NULL) {
			manifest->submanifests = list_prepend_data(manifest->submanifests, sub);
			manifest->contentsize += sub->contentsize;
			LOG_DEBUG(file, "submanifest", class_manifest, "ver=%d, size=%i", sub->version, sub->contentsize);
		}
	}

	return 0;
}


void consolidate_submanifests(struct manifest *manifest)
{
	struct list *list, *next, *tmp;
	struct manifest *sub;
	struct file *file1, *file2;

	/* Create a consolidated, sorted list of files from all of the
	 * manifests' lists of files.  */
	list = list_head(manifest->submanifests);
	while (list) {
		sub = list->data;
		list = list->next;
		if (!sub)
			continue;
		manifest->files = list_concat(manifest->files, sub->files);
		sub->files = NULL;
	}
	manifest->files = list_sort(manifest->files, file_sort_filename);

	/* Two pointers ("list" and "next") traverse the consolidated, filename sorted
	 * struct list of files.  The "list" pointer is marched forward through the
	 * struct list as long as it and the next do not point
	 * to two objects with the same filename.  If the name is the same, then
	 * "list" and "next" point to the first and second in a series of perhaps
	 * many objects referring to the same filename.  As we determine which file out
	 * of multiples to keep in our consolidated, deduplicated, filename sorted list
	 * there are Manifest invariants to maintain.  The following table shows the
	 * associated decision matrix.  Note that "file" may be a file, directory or
	 * symlink.
	 *
	 *         | File 2:
	 *         |  A'    B'    C'    D'
	 * File 1: |------------------------
	 *    A    |  -  |  2  |  2  |  2  |
	 *    B    |  1  |  -  |  2  |  2  |
	 *    C    |  1  |  1  |  -  |  X  |
	 *    D    |  1  |  1  |  X  |  X  |
	 *
	 *   State for file1 {A,B,C,D}
	 *         for file2 {A',B',C',D'}
	 *       A:  is_deleted && !is_rename
	 *       B:  is_deleted &&  is_rename
	 *       C: !is_deleted && (file1->hash == file2->hash)
	 *       D: !is_deleted && (file1->hash != file2->hash)
	 *
	 *   Action
	 *       -: Don't Care   - choose/remove either file
	 *       X: Error State  - remove both files, LOG error
	 *       1: choose file1 - remove file2
	 *       2: choose file2 - remove file1
	 *
	 * NOTE: the code below could be rewritten to be more "efficient", but clarity
	 *       and concreteness here are of utmost importance if we are to correctly
	 *       maintain the installed system's state in the filesystem across updates
	 */
	list = list_head(manifest->files);
	while (list) {
		next = list->next;
		if (next == NULL)
			break;
		file1 = list->data;
		file2 = next->data;

		if (strcmp(file1->filename, file2->filename)) {
			list = next;
			continue;
		} /* from here on, file1 and file2 have a filename match */

		/* (case 1) A'                     : choose file1 */
		if (file2->is_deleted && !file2->is_rename) {
			list_free_item(next, free_file_data);
			continue;
		}
		/* (case 2) A                      : choose file2 */
		if (file1->is_deleted && !file1->is_rename) {
			list_free_item(list, free_file_data);
			list = next;
			continue;
		}
		/* (case 3) B' AND NOT A           : choose file 1*/
		if (file2->is_deleted && file2->is_rename) { // && !(file1->is_deleted && !file1->is_rename)
			list_free_item(next, free_file_data);
			continue;
		}

		/* (case 4) B AND NOT (A' OR B')   : choose file2 */
		if (file1->is_deleted && file1->is_rename) { // && !(file2->is_deleted)
			list_free_item(list, free_file_data);
			list = next;
			continue;
		}

		/* (case 5) C and C'               : choose file1 */
		if (!file1->is_deleted && !file2->is_deleted && (strcmp(file1->hash, file2->hash) == 0)) {
			list_free_item(next, free_file_data);
			continue;
		}

		/* (case 6) all others constitute errors */
		LOG_DEBUG(NULL, "unhandled filename pair", class_file_misc, "file1 %s %s (%d), file2 %s %s (%d)",
			file1->filename, file1->hash, file1->last_change,
			file1->filename, file2->hash, file2->last_change);
		tmp = next->next;
		list_free_item(list, free_file_data);
		list_free_item(next, free_file_data);
		list = tmp;
	}
}

static char *type_to_string(struct file *file)
{
	static char type[5];

	strcpy(type, "....");

	if (file->is_dir)
		type[0] = 'D';

	if (file->is_link)
		type[0] = 'L';
	if (file->is_file)
		type[0] = 'F';
	if (file->is_manifest)
		type[0] = 'M';

	if (file->is_deleted)
		type[1] = 'd';

	if (file->is_config)
		type[2] = 'C';
	if (file->is_state)
		type[2] = 's';
	if (file->is_boot)
		type[2] = 'b';

	if (file->is_rename)
		type[3] = 'r';

	return type;
}


/* non-production code: a useful function to have and maintain and some useful
 * call sites exist though they are commented out */
void debug_write_manifest(struct manifest *manifest, char *filename)
{
	struct list *list;
	struct file *file;
	FILE *out;
	char *fullfile = NULL;

	if (asprintf(&fullfile, "%s/%s", STATE_DIR, filename) <= 0)
		abort();

	out = fopen(fullfile, "w");
	if (out == NULL) {
		printf("Failed to open %s for write\n", fullfile);
	}
	if (out == NULL)
		abort();

	fprintf(out, "MANIFEST\t1\n");
	fprintf(out, "version:\t%i\n", manifest->version);

	list = list_head(manifest->files);
	fprintf(out, "\n");
	while (list) {
		file = list->data;
		list = list->next;
		fprintf(out, "%s\t%s\t%i\t%s\n", type_to_string(file), file->hash, file->last_change, file->filename);
	}


	list = list_head(manifest->manifests);
	while (list) {
		file = list->data;
		list = list->next;
		fprintf(out, "%s\t%s\t%i\t%s\n", type_to_string(file), file->hash, file->last_change, file->filename);
	}
	fclose(out);
	free(fullfile);
}

void link_renames(struct list *newfiles, struct manifest *from_manifest)
{
	struct list *list1, *list2;
	struct list *targets;
	struct file *file1, *file2;

	targets = newfiles = list_sort(newfiles, file_sort_version);

	list1 = list_head(newfiles);

	/* todo: sort newfiles and targets by hash */

	while (list1) {
		file1 = list1->data;
		list1 = list1->next;

		if (file1->peer || !file1->is_rename)
			continue;
		if (file1->is_deleted)
			continue;
		/* now, file1 is the new file that got renamed. time to search the rename targets */
		list2 = list_head(targets);
		while (list2) {
			file2 = list2->data;
			list2 = list2->next;

			if (!file2->peer || !file2->is_rename)
				continue;
			if (!file2->is_deleted)
				continue;
			if (!file_found_in_older_manifest(from_manifest, file2))
				continue;
			if (strcmp(file2->hash, file1->hash) == 0) {
				LOG_DEBUG(file1, "File rename detected", class_file_misc, "%s -> %s rename", file2->filename, file1->filename);
				file1->deltapeer = file2->peer;
				file1->peer = file2->peer;
				file2->peer->deltapeer = file1;
				list2 = NULL;
			}
		}
	}
}

/* similar to verify_hash(), but called from src/verify.c
 * The goal here is to get a hash for local files, which will later be
 * compared against an official manifest of the installed build version.
 * This is called from a threadpool, so any error is just flagged to a global
 * and the caller will check that after all threads have finished.
 */
static void get_hash(void *data)
{
	struct file *file = data;
	char *filename;

	if (asprintf(&filename, "%s%s", path_prefix, file->filename) < 0)
		abort();

	file->hash = compute_hash(file, filename);
	if (file->hash == NULL) {
		LOG_WARN(file, "Failed to compute hash", class_hash, "\\*filename=\"%s\"*\\",
				filename);
		threaded_hash_compute_result = -1;
	}

	free(filename);
}

static void iterate_directory(struct manifest *manifest,
		char *subpath, int depth, bool use_xattrs)
{
	DIR *dir;
	struct dirent *entry;
	char *fullpath;
	char *relpath;

	if (is_state(subpath)) {
		LOG_DEBUG(NULL, "skipping is_state subpath", class_file_misc, "\\*path_prefix=\"%s\",subpath=\"%s\"*\\", path_prefix, subpath);
		return;
	}

	fullpath = mk_full_filename(path_prefix, subpath);
	if (fullpath == NULL)
		abort();
	relpath = &fullpath[strlen(path_prefix)-1];

	dir = opendir(fullpath);
	if (!dir) {
		free(fullpath);
		return;
	}

	while (dir) {
		struct file *file;

		entry = readdir(dir);
		if (!entry)
			break;

		if (strcmp(entry->d_name, ".") == 0)
			continue;
		if (strcmp(entry->d_name, "..") == 0)
			continue;

		file = calloc(1, sizeof(struct file));
		if (!file)
			abort();

		file->last_change = manifest->version;
		file->filename = mk_full_filename(relpath, entry->d_name);
		if (file->filename == NULL)
			abort();

		apply_heuristics(file);

		if (entry->d_type == DT_DIR) {
			file->is_dir = 1;

			/* skip the contents of mounts and known state directories */
			if (is_directory_mounted(file->filename)) {
				file->is_state = 1;
			} else if (is_under_mounted_directory(file->filename)) {
				file->is_state = 1;
			}
			if (!file->is_state) {
				iterate_directory(manifest, file->filename, depth + 1, use_xattrs);
			}
		} else if (entry->d_type == DT_LNK) {
			file->is_link = 1;
		} else if (entry->d_type == DT_REG) {
			file->is_file = 1;
		} else {
			LOG_DEBUG(NULL, "Ignoring unknown file type", class_file_misc, "%s%s",
				path_prefix, file->filename);
			free_file_data(file);
			continue;
		}

		/* compute the hash from a thread */
		file->use_xattrs = use_xattrs;
		executor_submit_task(executor, get_hash, file);
		manifest->files = list_prepend_data(manifest->files, file);
	}
	closedir(dir);
	free(fullpath);
}


struct manifest *manifest_from_directory(int version, bool use_xattrs)
{
	struct manifest *manifest;
	int nworkers = (int) sysconf(_SC_NPROCESSORS_ONLN);

	LOG_INFO(NULL, "Building OS image manifest", class_manifest, "%s", path_prefix);

	manifest = alloc_manifest(version, "full");
	if (manifest == NULL)
		return NULL;

	threaded_hash_compute_result = 0;
	executor = executor_create(nworkers, 10*nworkers, true);

	iterate_directory(manifest, "/", 0, use_xattrs);
	/* wait for the hash computation to finish */
	executor_destroy(executor, true);

	if (threaded_hash_compute_result) {
		LOG_WARN(NULL, "Failed to compute some hashes, abort manifest_from_directory()", class_hash, "");
		free_manifest(manifest);
		return NULL;
	}

	manifest->files = list_sort(manifest->files, file_sort_filename);

	return manifest;
}
