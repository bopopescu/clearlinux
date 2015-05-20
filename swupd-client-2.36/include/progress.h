#ifndef __PROGRESS__
#define __PROGRESS__

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This enum MUST be kept in sync with its sibling found at:
 * service/interface/com/intel/service/update/Progress.java
 */
typedef enum {
	PROGRESS_MSG_NONE = 0,
	PROGRESS_MSG_START = 1,
	PROGRESS_MSG_CHECK_DISKSPACE = 2,
	PROGRESS_MSG_LOAD_CURRENT_MANIFEST = 3,
	PROGRESS_MSG_LOAD_SERVER_MANIFEST = 4,
	PROGRESS_MSG_DOWNLOAD_PACK = 5,
	PROGRESS_MSG_EXTRACTING_PACK = 6,
	PROGRESS_MSG_VERIFY_STAGING_PRE = 7,
	PROGRESS_MSG_DOWNLOAD_DELTA = 8,
	PROGRESS_MSG_DOWNLOAD_FULL = 9,
	PROGRESS_MSG_STAGING = 10,
	PROGRESS_MSG_VERIFY_STAGING_POST = 11,
	PROGRESS_MSG_SNAPSHOT = 12,
	PROGRESS_MSG_VERIFY_SNAPSHOT = 13,
	PROGRESS_MSG_UPDATE_ESP = 14,
	PROGRESS_MSG_SYNCING = 15,
	PROGRESS_MSG_UPDATED = 16,
	PROGRESS_MSG_DONE = 17,
	PROGRESS_MSG_GET_SERVER_VERSION = 18
} progress_msg_id;

struct progress_msg {
	progress_msg_id msg_id;
	size_t size_total;
	size_t size_done;
};

typedef void (*progress_cb_t)(struct progress_msg *progress_msg);

/* called by progress 'client' to receive progress information */
void progress_register_cb(progress_cb_t progress_cb);

/* Sets progress callback options:
 * - min_size_increment is the minimum amount of data that must be received
 *   between two invocations of the progress callback.
 * - min_mseconds_update is the minimum delay in milliseconds between
 *   2 progress updates.
 * The delay criteria is always applied: Callback N+1 is not made until both
 * the minimum data have been received and the minimum delay has elapsed
 * since callback N.
 * This function can be called to change options any time during update */
void progress_set_options(size_t min_size_increment, int min_mseconds_update);

/* call this for a step that is not going to get further info. The callback
 * will be invoked regardless of how much time passed since the last cb */
void progress_step(progress_msg_id msg_id);

/* call this repeatedly for a long step with periodic updated info.
 * The first call should pass zero for both done and total. The last call
 * should pass equal done and total values. Those two calls will invoke
 * callback immediately. Other calls will invoke cb only when the conditions
 * set by the options are satisfied (elapsed time, min size increment. */
void progress_step_ongoing(progress_msg_id msg_id, size_t size_done,
				size_t size_total);

#ifdef __cplusplus
}
#endif

#endif
