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
 *         pplaquet <paul.plaquette@intel.com>
 *         Eric Lapuyade <eric.lapuyade@intel.com>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "list.h"

static struct list *list_append_item(struct list *list, struct list *item)
{
	list = list_tail(list);
	if (list) {
		list->next = item;
		item->prev = list;
	}

	return item;
}

static struct list *list_prepend_item(struct list *list, struct list *item)
{
	list = list_head(list);
	if (list) {
		list->prev = item;
		item->next = list;
	}

	return item;
}

static struct list *list_alloc_item(void *data)
{
	struct list *item;

	item = (struct list *)malloc(sizeof(struct list));
	if (item) {
		item->data = data;
		item->next = NULL;
		item->prev = NULL;
	}

	return item;
}

/* ------------ Public API ------------ */

struct list *list_append_data(struct list *list, void *data)
{
	struct list *item = NULL;

	item = list_alloc_item(data);
	if (item)
		item = list_append_item(list, item);

	return item;
}

struct list *list_prepend_data(struct list *list, void *data)
{
	struct list *item = NULL;

	item = list_alloc_item(data);
	if (item)
		item = list_prepend_item(list, item);

	return item;
}

struct list *list_head(struct list *item)
{
	if (item)
		while (item->prev)
			item = item->prev;

	return item;
}

struct list *list_tail(struct list *item)
{
	if (item)
		while (item->next)
			item = item->next;

	return item;
}

unsigned int list_len(struct list *list)
{
	unsigned int len;
	struct list *item;

	if (list == NULL)
		return 0;

	len = 1;

	item = list;
	while ((item = item->next) != NULL)
		len++;

	item = list;
	while ((item = item->prev) != NULL)
		len++;

	return len;
}

struct list *list_find_data(struct list *list, void *data)
{
	list = list_head(list);
	while (list) {
		if (list->data == data)
			return list;
		list = list->next;
	}

	return NULL;
}

struct list *list_sort(struct list *list, comparison_fn_t comparison_fn)
{
	/* this is an implementation I just invented so that I have something without
	 * requiring qsort...
	 * algorithm is simple: we go from 2nd to last item in the list, and
	 * compare with previous items until we find the right place. */

	unsigned int len;
	struct list *item;	/* item being sorted */
	struct list *tail;	/* last sorted item */
	struct list *ref;	/* sorted reference item compared to item */
	struct list *head;	/* current sorted head */

	if (list == NULL)
		return NULL;

	len = list_len(list);
	if (len == 1)
		return list;

	head = list_head(list);
	tail = head;
	while ((item = tail->next) != NULL) { /* one more item to sort */
		/* unlink item we want to sort */
		item->prev->next = item->next;
		if (item->next)
			item->next->prev = item->prev;
		/* start to compare with the last sorted item */
		ref = tail;
		/* compare from last sorted toward list head */
		while (comparison_fn(item->data, ref->data) < 0) { /* ref not small enough yet */
			if (ref == head) {
				/* head of the list reached, place item first */
				ref->prev = item;
				head = item;
				item->prev = NULL;
				item->next = ref;
				item = NULL; /* it's placed */
				break;
			}
			ref = ref->prev; /* go test previous sorted ref */
		}
		/* if item not placed yet, it goes after ref */
		if (item) {
			if (ref == tail)
				tail = item;
			item->prev = ref;
			item->next = ref->next;
			if (ref->next != NULL)
				ref->next->prev = item;
			ref->next = item;
		}
	}

	return head;
}

struct list *list_concat(struct list *list1, struct list *list2)
{
	struct list *tail;

	list2 = list_head(list2);

	if (list1 == NULL)
		return list2;

	list1 = list_head(list1);

	if (list2) {
		tail = list_tail(list1);

		tail->next = list2;
		list2->prev = tail;
	}

	return list1;
}

struct list *list_free_item(struct list *item, list_free_data_fn_t list_free_data_fn)
{
	struct list *ret_item;

	if (item->prev) {
		item->prev->next = item->next;
		ret_item = item->prev;
	} else
		ret_item = item->next;

	if (item->next)
		item->next->prev = item->prev;

	if (list_free_data_fn)
		list_free_data_fn(item->data);

	free(item);

	return ret_item;
}

void list_free_list_and_data(struct list *list, list_free_data_fn_t list_free_data_fn)
{
	struct list *item = list_head(list);

	while (item)
		item = list_free_item(item, list_free_data_fn);
}

void list_free_list(struct list *list)
{
	list_free_list_and_data(list, NULL);
}
