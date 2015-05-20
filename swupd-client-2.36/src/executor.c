/*
 *   Executor thread pool manager.
 *
 *      Copyright © 2013 Intel Corporation.
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
 *         Tom Keel <thomas.keel@intel.com>
 *
 */

/*
 * Implementation Notes
 * --------------------
 *
 * Per-process, there's a set of executor instance pointers, and a mutex for
 * ensuring that operations on this set are thread-safe. This set is used to
 * validate the pointers passed to the instance methods. It's implemented as a
 * binary tree using tsearch(3).
 *
 * Per-instance, there are one mutex, two condition variables, a task queue, and
 * various counters, flags, etc. All of this is allocated in the create()
 * method.
 *
 * The instance mutex is used to ensure thread-safeness of all access to that
 * instance's data, and for waiting/signalling the instance's condition
 * variables.
 *
 * One of the condition variables means "this queue is no longer empty or the
 * stop flag has been set (or both)". Worker threads wait on this condition.
 * Callers of the submit_task() method broadcast this condition when submitting
 * to an empty queue, and the caller of the destroy() method broadcasts it after
 * setting the instance's "stop flag".
 *
 * One of the condition variables means "this queue is no longer full".
 * Callers of the submit_task() method wait on this condition, and worker
 * threads broadcast it when removing a task from a full queue.
 *
 * The set-of-all-instances mutex and any given instance mutex are never held
 * simultaneously.
 *
 * The task queue is a simple "circular table".
 */

#include <pthread.h>
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>

#include <executor.h>

#define LOGGING 0

#if LOGGING
#include <stdio.h>
#define LOG(format, ...) fprintf (stderr, format, ##__VA_ARGS__)
#else
#define LOG(format, ...)
#endif


struct task
{
	void	(*func)(void*);
	void	*data;
};

struct task_queue
{
	int		capacity;
	int		n_tasks;
	int		head;
	int		tail;
	struct task	*tasks;
};

struct worker
{
	struct executor	*e;
	pthread_t 	thread;
	int		n_tasks_done;
	int		n_times_i_working;
};

struct executor
{
	int			n_workers;
	int			n_workers_working;
	struct worker		*workers;
	struct task_queue	task_queue;
	bool			blocking;
	bool			stop_flag;
	pthread_mutex_t		mutex;
	pthread_cond_t		cond_queue_not_empty_or_stop_flag_set;
	pthread_cond_t		cond_queue_not_full;
	bool			mutex_allocated;
	bool			cond_queue_not_empty_or_stop_flag_set_allocated;
	bool			cond_queue_not_full_allocated;
	pthread_t		cleanup_thread;
};


// The global set of instances, and a mutex for operations on it.
static void *instances = NULL;
static pthread_mutex_t instances_mutex = PTHREAD_MUTEX_INITIALIZER;


static void *destroy(void *_e);
static struct executor *allocate(int n_workers, int queue_size);
static void deallocate(struct executor *e);
static void *do_work(void* data);
static bool task_queue_is_empty(struct task_queue *tq);
static bool task_queue_is_full(struct task_queue *tq);
static int task_queue_get_len(struct task_queue *tq);
static void task_queue_add(struct task_queue *tq, void (*func)(void*), void *data);
static void task_queue_remove(struct task_queue *tq, struct task *t);
static void inst_add(struct executor *e);
static bool inst_find(struct executor *e);
static bool inst_delete(struct executor *e);


struct executor* executor_create(int n_workers, int queue_size, bool blocking)
{
	int n;

	if (n_workers < 1 || queue_size < 1) {
		return NULL;
	}
	// Args are valid.

	struct executor *e = allocate(n_workers, queue_size);
	if (e == NULL) {
		return NULL;
	}
	// We were able to allocate memory, mutex, condition variables.

	e->blocking = blocking;
	e->stop_flag = false;

	pthread_mutex_lock(&e->mutex);
	for (n = 0; n < n_workers; n++) {
		e->workers[n].e = e;
		if (pthread_create(&e->workers[n].thread, NULL, do_work, &e->workers[n]) != 0) {
			break;
		}
	}
	e->n_workers = n;
	pthread_mutex_unlock(&e->mutex);
	// We have created e->n_workers number of workers.

	if (e->n_workers < n_workers) {
		// But that wasn't enough...
		destroy(e);
		return NULL;
	}

	inst_add(e);
	// We have added the new instance to the global set of instances.

	return e;
}

void executor_destroy(struct executor *e, bool blocking)
{
	if (e == NULL || !inst_delete(e)) {
		LOG("ERROR: Trying to destroy invalid instance!\n");
		return;
	}
	// Args are valid, and we have removed this instance from the set.

	if (blocking) {
		// Blocking cleanup selected.
		destroy(e);
	} else {
		// Non-blocking cleanup selected.
		if (pthread_create(&e->cleanup_thread, NULL, destroy, e) == 0) {
			// We were able to create the cleanup thread.
			pthread_detach(e->cleanup_thread);
		} else {
			// We were unable to create the cleanup thread.
			// As a last resort, clean up synchronously.
			LOG("ERROR: Have to clean up synchronously!\n");
			destroy(e);
		}
	}
}

static void *destroy(void *_e)
{
	struct executor *e = (struct executor *)_e;
	int i;

	LOG("Cleaning up\n");

	pthread_mutex_lock(&e->mutex);

	e->stop_flag = true;
	pthread_cond_broadcast(&e->cond_queue_not_empty_or_stop_flag_set);

	pthread_mutex_unlock(&e->mutex);

	for (i = 0; i < e->n_workers; i++) {
		pthread_join(e->workers[i].thread, NULL);
	}

#if LOGGING
	int total_tasks = 0;
	for (i = 0; i < e->n_workers; i++) {
		int n = e->workers[i].n_times_i_working;
		LOG("Multithreading = %d on %d task starts.\n", i+1, n);
		total_tasks += n;
	}
	LOG("%d total task starts.\n", total_tasks);
	total_tasks = 0;
	for (i = 0; i < e->n_workers; i++) {
		int n = e->workers[i].n_tasks_done;
		LOG("Worker %d executed %d tasks.\n", i, n);
		total_tasks += n;
	}
	LOG("%d total tasks executed.\n", total_tasks);
#endif

	deallocate(e);

	return NULL;
}

int executor_submit_task(struct executor *e, void (*func)(void*), void *data)
{
	int len;
	bool was_empty;

	if (e == NULL || func == NULL || !inst_find(e)) {
		return EXECUTOR_ERR_ARGS;
	}
	// Args are valid.

	pthread_mutex_lock(&e->mutex);

	while (task_queue_is_full(&e->task_queue)) {
		if (!e->blocking) {
			// We have the mutex, non-blocking mode, queue is full.
			pthread_mutex_unlock(&e->mutex);
			return EXECUTOR_ERR_SATURATED;
		}
		// We have the mutex, blocking mode, queue is full.
		pthread_cond_wait(&e->cond_queue_not_full, &e->mutex);
	}
	// We have the mutex, and the queue is not full.

	was_empty = task_queue_is_empty(&e->task_queue);
	task_queue_add(&e->task_queue, func, data);
	if (was_empty) {
		// We have the mutex, and just added a task to an empty queue.
		pthread_cond_broadcast(&e->cond_queue_not_empty_or_stop_flag_set);
	}
	len = task_queue_get_len(&e->task_queue);

	pthread_mutex_unlock(&e->mutex);

	return len;
}

static void *do_work(void *data)
{
	struct worker *w = (struct worker *)data;
	struct executor *e = w->e;
	struct task_queue *tq = &e->task_queue;
	bool first_time = true;
	bool was_full;
	struct task t;

	for (;;) {
		pthread_mutex_lock(&e->mutex);

		if (!first_time) {
			e->n_workers_working--;
		}
		first_time = false;

		while (task_queue_is_empty(tq) && !e->stop_flag) {
			pthread_cond_wait(&e->cond_queue_not_empty_or_stop_flag_set, &e->mutex);
		}
		// We have the mutex, and the queue is not empty or the stop flag is set (or both).

		if (task_queue_is_empty(tq)) {
			// We have the mutex, the queue is empty, and the stop flag is set.
			pthread_mutex_unlock(&e->mutex);
			return NULL;
		}

		was_full = task_queue_is_full(tq);

		task_queue_remove(tq, &t);
		// We have task t to execute.

		if (was_full & e->blocking) {
			// We made the queue non-full and task submission mode is blocking.
			pthread_cond_broadcast(&e->cond_queue_not_full);
		}

		e->n_workers_working++;
		e->workers[e->n_workers_working-1].n_times_i_working++;

		pthread_mutex_unlock(&e->mutex);

		t.func(t.data);
		w->n_tasks_done++;
	}
	return NULL;
}

static struct executor *allocate(int n_workers, int queue_size)
{
	struct executor *e = calloc(1, sizeof(struct executor));
	if (e == NULL) {
		return NULL;
	}

	e->workers = calloc(n_workers, sizeof(struct worker));
	if (e->workers == NULL) {
		deallocate(e);
		return NULL;
 	}

	e->task_queue.tasks = calloc(queue_size, sizeof(struct task));
	if (e->task_queue.tasks == NULL) {
		deallocate(e);
		return NULL;
 	}
	e->task_queue.capacity = queue_size;

	if (pthread_mutex_init(&e->mutex, NULL) != 0) {
		deallocate(e);
		return NULL;
	}
	e->mutex_allocated = true;

	if (pthread_cond_init(&e->cond_queue_not_empty_or_stop_flag_set, NULL) != 0) {
		deallocate(e);
		return NULL;
	}
	e->cond_queue_not_empty_or_stop_flag_set_allocated = true;

	if (pthread_cond_init(&e->cond_queue_not_full, NULL) != 0) {
		deallocate(e);
		return NULL;
	}
	e->cond_queue_not_full_allocated = true;

	return e;
}

static void deallocate(struct executor *e)
{
	if (e->workers != NULL) {
		free(e->workers);
	}
	if (e->task_queue.tasks != NULL) {
		free(e->task_queue.tasks);
	}
	if (e->mutex_allocated) {
		pthread_mutex_destroy(&e->mutex);
	}
	if (e->cond_queue_not_empty_or_stop_flag_set_allocated) {
		pthread_cond_destroy(&e->cond_queue_not_empty_or_stop_flag_set);
	}
	if (e->cond_queue_not_full_allocated) {
		pthread_cond_destroy(&e->cond_queue_not_full);
	}
	free(e);
}


static bool task_queue_is_empty(struct task_queue *tq)
{
	return tq->n_tasks == 0;
}

static bool task_queue_is_full(struct task_queue *tq)
{
	return tq->n_tasks == tq->capacity;
}

static int task_queue_get_len(struct task_queue *tq)
{
	return tq->n_tasks;
}

static void task_queue_add(struct task_queue *tq, void (*func)(void*), void *data)
{
	tq->tasks[tq->tail].func = func;
	tq->tasks[tq->tail].data = data;
	tq->tail = (tq->tail + 1) % tq->capacity;
	tq->n_tasks++;
}

static void task_queue_remove(struct task_queue *tq, struct task *t)
{
	t->func = tq->tasks[tq->head].func;
	t->data = tq->tasks[tq->head].data;
	tq->head = (tq->head + 1) % tq->capacity;
	tq->n_tasks--;
}

// Compare two pointers.
static int pointer_diff(const void *e1, const void *e2)
{
	return (int)((size_t)e1 - (size_t)e2);
}

// Add an instance to the global set of instances.
static void inst_add(struct executor *e)
{
	pthread_mutex_lock(&instances_mutex);
	tsearch(e, &instances, pointer_diff);
	pthread_mutex_unlock(&instances_mutex);
}

// Return true iff the given instance is in the global set of instances.
static bool inst_find(struct executor *e)
{
	bool found;
	pthread_mutex_lock(&instances_mutex);
	found = (tfind(e, &instances, pointer_diff) != NULL);
	pthread_mutex_unlock(&instances_mutex);
	return found;
}

// Delete the given instance from the global set of instances.
// Return true iff the instance was indeed in the set.
static bool inst_delete(struct executor *e)
{
	bool found;
	pthread_mutex_lock(&instances_mutex);
	found = (tfind(e, &instances, pointer_diff) != NULL);
	if (found) {
		tdelete(e, &instances, pointer_diff);
	}
	pthread_mutex_unlock(&instances_mutex);
	return found;
}
