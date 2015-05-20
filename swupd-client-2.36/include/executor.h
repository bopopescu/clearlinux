#ifndef EXECUTOR_H_
#define EXECUTOR_H_

#include <stdbool.h>

#define EXECUTOR_ERR_ARGS (-1)
#define EXECUTOR_ERR_SATURATED (-2)


/*
 * An executor manages a queue of tasks and a pool of worker threads to execute them.
 * A task is just a function that returns void, and some data to pass to it.
 * When you create an executor, you specify the number of worker threads in the pool,
 * and the maximum number of tasks that can be queued waiting for workers.
 */
struct executor;

/*
 * Create a new executor.
 *
 * @param n_workers - The number of worker threads in the pool. Must be > 0.
 * @param queue_size - The maximum number of tasks that can be queued. Must be > 0.
 * @param blocking - Whether attempts to submit tasks to a full queue should block
 * until a slot becomes available or fail immediately.
 * @return - Pointer to the new executor object, or NULL if memory allocation fails,
 * or arguments are invalid.
 */
struct executor* executor_create(int n_workers, int queue_size, bool blocking);

/*
 * Submit a task to be executed by a worker thread. If this executor was created
 * with the blocking flag true, then calls to this function will block if necessary
 * until a free slot in the task queue becomes available.
 *
 * @param executor - The executor.
 * @param func - The task function.
 * @param data - The data to pass to the task function.
 * @return - Upon success, a positive number indicating the number of queued tasks,
 * including this one (but not including any tasks being executed by workers).
 * Upon failure, one of:
 *   EXECUTOR_ERR_ARGS - null, invalid, or already-destroyed executor; or null task.
 *   EXECUTOR_ERR_SATURATED - the task queue is full in non-blocking mode
 */
int executor_submit_task(struct executor *executor, void (*func)(void*), void *data);

/*
 * Wait for all pending tasks (both queued and executing) to finish execution, then
 * deallocate all resources. It is an error to attempt to use this executor in any
 * way after calling this function.
 *
 * @param executor - The executor.
 * @param bloc - Whether to block until all worker threads have finished. If this
 * argument is false, then a thread will be created to manage the proper deallocation
 * of all resources.
 */
void executor_destroy(struct executor *executor, bool blocking);

#endif /* EXECUTOR_H_ */
