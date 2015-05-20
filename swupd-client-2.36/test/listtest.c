/*
 *   Software Updater - client side
 *
 *      Copyright © 2012-2013 Intel Corporation.
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

/* list test */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include "list.h"

#include <swupd.h>

int data_compare(const void *a, const void *b)
{
	int A, B;

	A = (intptr_t)a;
	B = (intptr_t)b;
//	printf("comparing data %ld > %ld\n", (long int) a, (long int) b);
	return (A - B);
}

int data_compare_reverse(const void *a, const void *b)
{
	int A, B;

	A = (intptr_t)a;
	B = (intptr_t)b;
//	printf("comparing data %ld < %ld\n", (long int) a, (long int) b);
	return (B - A);
}

void dump_list(struct list *list)
{
	int i;

	i = 1;
	while (list) {
		printf("Item %d %lx data = %ld\n", i, (long unsigned int) list, (long int) list->data);
		list = list->next;
		i++;
	}
}

int check_list_order(struct list *list, int sort_criteria)
{
	unsigned long data;
	int i;
	struct list *item;
	int wrong;

	if (sort_criteria > 0)
		data = 0;
	else
		data = (unsigned long) ULONG_MAX;
	i = 0;
	item = list_head(list);
	while (item) {
		i++;
		if (sort_criteria > 0)
			wrong = (unsigned long) item->data < data;
		else
			wrong = (unsigned long) item->data > data;
		if (wrong) {
			printf ("List is in wrong order\n");
			return EXIT_FAILURE;
		}
		data = (unsigned long) item->data;
		item = item->next;
	}

	return 0;
}

#define TEST_LIST_LEN 20

int main(int UNUSED_PARAM argc, char UNUSED_PARAM **argv)
{
	struct list *list = NULL;
	struct list *list1, *list2;
	struct list *item, *item2, *item3, *head, *tail;
	int i;
	struct timeval tod;
	unsigned int seed;

	/* seed the random generator so that we get different lists each time */
	gettimeofday(&tod, NULL);
	seed = (unsigned int) tod.tv_sec;
	srand(seed);

	/* create a list with random data between 0 and 999 */
	for (i = 1; i <= TEST_LIST_LEN; i++)
		list = list_append_data(list, (void *) ((unsigned long) rand() % TEST_LIST_LEN));

	printf("List constructed, seed = %d, len = %d, content:\n", seed, list_len(list));
	dump_list(list_head(list));

	list = list_sort(list, data_compare);
	printf("List sorted, content:\n");
	dump_list(list);

	/* check list elements are in right order */
	if (check_list_order(list, 1) != 0) {
		printf ("Sorted (1) List is in wrong order\n");
		return EXIT_FAILURE;
	}

	/* check sorted list has the expected len */
	if (list_len(list) != TEST_LIST_LEN) {
		printf("Wrong sorted (1) list len = %d instead of %d", i, TEST_LIST_LEN);
		return EXIT_FAILURE;
	}
	printf ("List correctly sorted\n");

	/* sort again on sorted list to check special case */
	list = list_sort(list, data_compare);
	printf("Sorted List sorted again, content:\n");
	dump_list(list);
	if (check_list_order(list, 1) != 0) {
		printf ("Sorted (2) List is in wrong order\n");
		return EXIT_FAILURE;
	}
	if (list_len(list) != TEST_LIST_LEN) {
		printf("Wrong sorted (2) list len = %d instead of %d", i, TEST_LIST_LEN);
		return EXIT_FAILURE;
	}

	/* reverse sort from sorted state */
	list = list_sort(list, data_compare_reverse);
	printf("Sorted List sorted reverse, content:\n");
	dump_list(list);
	if (check_list_order(list, -1) != 0) {
		printf ("Sorted (3) List is in wrong order\n");
		return EXIT_FAILURE;
	}
	if (list_len(list) != TEST_LIST_LEN) {
		printf("Wrong sorted (3) list len = %d instead of %d", i, TEST_LIST_LEN);
		return EXIT_FAILURE;
	}

	/* Check freeing the head item.
	 * This must return the 2nd item, which must be the new head */
	head = list_head(list);
	item2 = head->next;
	list = list_free_item(head, NULL);
	if (list != item2) {
		printf("removing head item did not return 2nd item\n");
		return EXIT_FAILURE;
	}
	if (item2->prev) {
		printf("item returned after removing head is not new head\n");
		return EXIT_FAILURE;
	}
	if (list_len(item2) != TEST_LIST_LEN - 1) {
		printf("removing head item did not result in the right list len\n");
		return EXIT_FAILURE;
	}
	printf ("Removing head correctly returned 2nd item as new head\n");

	/* Check freeing middle item, must return previous item */
	head = list_head(list);
	item2 = head->next;
	item3 = item2->next;
	list = list_free_item(item2, NULL);
	if (list != head) {
		printf("removing 2nd item did not return head item\n");
		return EXIT_FAILURE;
	}
	if ((head != item3->prev) || (head->next != item3)) {
		printf("removing 2nd item did not link 3rd item to head\n");
		return EXIT_FAILURE;
	}

	if (list_len(list) != TEST_LIST_LEN - 2) {
		printf("removing 2nd item did not result in the right list len\n");
		return EXIT_FAILURE;
	}
	printf ("Removing middle item correctly returned previous item\n");

	/* Check freeing tail, must return new tail */
	tail = list_tail(list);
	item = tail->prev;
	list = list_free_item(tail, NULL);
	if (list != item) {
		printf("removing tail did not return prev item\n");
		return EXIT_FAILURE;
	}
	tail = list_tail(list);
	if (list != tail) {
		printf("removing tail did not return new tail\n");
		return EXIT_FAILURE;
	}
	if (list_len(list) != TEST_LIST_LEN - 3) {
		printf("removing tail did not result in the right list len\n");
		return EXIT_FAILURE;
	}
	printf ("Removing tail correctly returned previous item as new tail\n");

	list_free_list(list);
	list = NULL;

	/* Check list_concat */
	list1 = NULL;
	list1 = list_prepend_data(list1, (void *) 3);
	list1 = list_prepend_data(list1, (void *) 2);
	list1 = list_prepend_data(list1, (void *) 1);

	list2 = NULL;
	list2 = list_prepend_data(list2, (void *) 6);
	list2 = list_prepend_data(list2, (void *) 5);
	list2 = list_prepend_data(list2, (void *) 4);

	/* Check concat one list with empty list */
	list = list_concat(list1, NULL);
	if (list_len(list) != 3) {
		printf("concat(list1, NULL) did not result in a list len of 3\n");
		return EXIT_FAILURE;
	}
	if (list != list1) {
		printf("concat(list1, NULL) did not return list1\n");
		return EXIT_FAILURE;
	}
	if (list_head(list) != list) {
		printf("concat(list1, NULL) did not return list1 head\n");
		return EXIT_FAILURE;
	}
	if ((unsigned long) list->data != 1) {
		printf("concat(list1, NULL) head is wrong\n");
		return EXIT_FAILURE;
	}
	printf ("concat(list1, NULL) is OK\n");
	dump_list(list);

	/* Check concat empty list with one list*/
	list = list_concat(NULL, list2);
	if (list_len(list) != 3) {
		printf("concat(NULL, list2) did not result in a list len of 3\n");
		return EXIT_FAILURE;
	}
	if (list != list2) {
		printf("concat(NULL, list2) did not return list2\n");
		return EXIT_FAILURE;
	}
	if (list_head(list) != list) {
		printf("concat(NULL, list2) did not return list2 head\n");
		return EXIT_FAILURE;
	}
	if ((unsigned long) list->data != 4) {
		printf("concat(NULL, list2) head is wrong\n");
		return EXIT_FAILURE;
	}
	printf ("concat(NULL, list2) is OK\n");
	dump_list(list);

	/* Check concat two lists */
	list = list_concat(list1->next, list2->next->next);
	if (list_len(list) != 6) {
		printf("concat(list1, list2) did not result in a list len of 6\n");
		return EXIT_FAILURE;
	}
	if ((unsigned long) list->data != 1) {
		printf("concat(list1, list2) did not return list1 head\n");
		return EXIT_FAILURE;
	}
	if ((unsigned long) list->next->next->next->data != 4) {
		printf("concat(list1, list2) 4th item is not 4\n");
		return EXIT_FAILURE;
	}
	printf ("concat(list1, list2) is OK\n");
	dump_list(list);

	list_free_list(list);

	printf ("*** ALL LIST TESTS COMPLETED OK***\n");

    return EXIT_SUCCESS;
}
