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
#include <dirent.h>

#include <swupd.h>

struct list *subs;

static void free_subscription_data(void *data)
{
	struct sub *sub = (struct sub *) data;

	free(sub->component);
	free(sub);
}


void free_subscriptions(void)
{
	list_free_list_and_data(subs, free_subscription_data);
	subs = NULL;
}

void read_subscriptions_alt(void)
{
	char *path = NULL;
	DIR *dir;
	struct dirent *ent;
	struct sub *sub;

	if (asprintf(&path, "%s/%s", path_prefix, BUNDLES_DIR) <= 0)
		abort();

	dir = opendir(path);
	if (!dir) {
		LOG_INFO(NULL, "Cannot read bundles directory, assuming os-core", class_subscription, "%s", path);
		free(path);
		return;
	}

	while((ent = readdir(dir))) {
		if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0))
			continue;
		if (ent->d_type == DT_REG) {
			if (component_subscribed(ent->d_name)) {
			/*  This is considered odd since means two files same name on same folder */
				LOG_DEBUG(NULL, "Bundle already loaded, skipping it", class_subscription, "%s", ent->d_name);
				continue;
			}

			sub = calloc(1, sizeof(struct sub));
			if (!sub) {
				LOG_ERROR(NULL, "Cannot allocate memory for bundle", class_subscription, "%s", ent->d_name);
				abort();
			}

			sub->component = strdup(ent->d_name);
			if (sub->component == NULL) {
				LOG_ERROR(NULL, "Cannot allocate memory for bundle name", class_subscription, "");
				abort();
			}
			sub->version = 0;
			subs = list_prepend_data(subs, sub);
			LOG_INFO(NULL, "Bundle added", class_subscription, "%s", ent->d_name);
		} else {
			/* all non regular files inside bundles dir are considered corrupted */
			LOG_WARN(NULL, "File is corrupted, skipping it", class_subscription, "%s", ent->d_name);
		}
	}

	if (closedir(dir) != 0)
		LOG_ERROR(NULL, "Cannot close directory", class_subscription, "%s", path);
	free(path);
}

void read_subscriptions(void)
{
	FILE *file;
	char *line;
	char *path = NULL;
	struct sub *sub;

#warning TODO change to a bundles directory with empty files present to indicate subscription
	if (asprintf(&path, "%s/%s/subscriptions", path_prefix, STATE_DIR) <= 0)
		abort();
	file = fopen(path, "r");
	if (!file) {
		LOG_INFO(NULL, "No subscriptions file, assuming os-core", class_subscription, "%s", path);
		free(path);
		return;
	} else {
		LOG_INFO(NULL, "Reading subscriptions", class_subscription, "");
	}

	while (!feof(file)) {
		ssize_t ret;
		char *c;
		size_t n;

		line = NULL;
		n = 0;
		ret = getline(&line, &n, file);

		if (ret < 0)
			break;
		if (line == NULL)
			break;
		c = strchr(line, '\n');
		if (c)
			*c = 0;

		sub = calloc(1, sizeof(struct sub));
		if (!sub)
			abort();

		LOG_INFO(NULL, "Subscription added", class_subscription, "%s", line);

		sub->component = strdup(line);
		if (sub->component == NULL) {
			LOG_ERROR(NULL, "Cannot allocate memory for bundle name", class_subscription, "");
			abort();
		}
		sub->version = 0;

		subs = list_prepend_data(subs, sub);
	}
	fclose(file);
	free(path);
}

int component_subscribed(char *component)
{
	struct list *list;
	struct sub *sub;

	LOG_DEBUG(NULL, "Checking subscription", class_subscription, "%s", component);

	if (strcmp(component, "os-core") == 0)
		return 1;

	list = list_head(subs);
	while (list) {
		sub = list->data;
		list = list->next;

		if (strcmp(sub->component, component) == 0)
			return 1;
	}
	return 0;
}

void subscription_versions_from_MoM(struct manifest *MoM)
{
	struct list *list;
	struct list *list2;
	struct file *file;
	struct sub *sub;

	list = MoM->manifests;
	while (list) {
		file = list->data;
		list = list->next;

		list2 = list_head(subs);
		while (list2) {
			sub = list2->data;
			list2 = list2->next;

			if (strcmp(sub->component, file->filename) == 0) {
				sub->version = file->last_change;
			}
		}
	}
}
