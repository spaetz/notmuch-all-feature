/* notmuch - Not much of an email library, (just index and search)
 *
 * Copyright © 2009 Carl Worth
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/ .
 *
 * Author: Carl Worth <cworth@cworth.org>
 */

#ifndef NOTMUCH_H
#define NOTMUCH_H

#ifdef  __cplusplus
# define NOTMUCH_BEGIN_DECLS  extern "C" {
# define NOTMUCH_END_DECLS    }
#else
# define NOTMUCH_BEGIN_DECLS
# define NOTMUCH_END_DECLS
#endif

NOTMUCH_BEGIN_DECLS

#include <time.h>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

typedef int notmuch_bool_t;

/* Status codes used for the return values of most functions.
 *
 * A zero value (NOTMUCH_STATUS_SUCCESS) indicates that the function
 * completed without error. Any other value indicates an error as
 * follows:
 *
 * NOTMUCH_STATUS_SUCCESS: No error occurred.
 *
 * NOTMUCH_STATUS_OUT_OF_MEMORY: Out of memory
 *
 * XXX: We don't really want to expose this lame XAPIAN_EXCEPTION
 * value. Instead we should map to things like DATABASE_LOCKED or
 * whatever.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: An attempt was made to write to
 *	a database opened in read-only mode.
 *
 * NOTMUCH_STATUS_XAPIAN_EXCEPTION: A Xapian exception occurred
 *
 * NOTMUCH_STATUS_FILE_ERROR: An error occurred trying to read or
 *	write to a file (this could be file not found, permission
 *	denied, etc.)
 *
 * NOTMUCH_STATUS_FILE_NOT_EMAIL: A file was presented that doesn't
 *	appear to be an email message.
 *
 * NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID: A file contains a message ID
 *	that is identical to a message already in the database.
 *
 * NOTMUCH_STATUS_NULL_POINTER: The user erroneously passed a NULL
 *	pointer to a notmuch function.
 *
 * NOTMUCH_STATUS_TAG_TOO_LONG: A tag value is too long (exceeds
 *	NOTMUCH_TAG_MAX)
 *
 * NOTMUCH_STATUS_UNBALANCED_FREEZE_THAW: The notmuch_message_thaw
 *	function has been called more times than notmuch_message_freeze.
 *
 * And finally:
 *
 * NOTMUCH_STATUS_LAST_STATUS: Not an actual status value. Just a way
 *	to find out how many valid status values there are.
 */
typedef enum _notmuch_status {
    NOTMUCH_STATUS_SUCCESS = 0,
    NOTMUCH_STATUS_OUT_OF_MEMORY,
    NOTMUCH_STATUS_READ_ONLY_DATABASE,
    NOTMUCH_STATUS_XAPIAN_EXCEPTION,
    NOTMUCH_STATUS_FILE_ERROR,
    NOTMUCH_STATUS_FILE_NOT_EMAIL,
    NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID,
    NOTMUCH_STATUS_NULL_POINTER,
    NOTMUCH_STATUS_TAG_TOO_LONG,
    NOTMUCH_STATUS_UNBALANCED_FREEZE_THAW,

    NOTMUCH_STATUS_LAST_STATUS
} notmuch_status_t;

/* Get a string representation of a notmuch_status_t value.
 *
 * The result is readonly.
 */
const char *
notmuch_status_to_string (notmuch_status_t status);

/* Various opaque data types. For each notmuch_<foo>_t see the various
 * notmuch_<foo> functions below. */
typedef struct _notmuch_database notmuch_database_t;
typedef struct _notmuch_query notmuch_query_t;
typedef struct _notmuch_threads notmuch_threads_t;
typedef struct _notmuch_thread notmuch_thread_t;
typedef struct _notmuch_messages notmuch_messages_t;
typedef struct _notmuch_message notmuch_message_t;
typedef struct _notmuch_tags notmuch_tags_t;
typedef struct _notmuch_directory notmuch_directory_t;
typedef struct _notmuch_filenames notmuch_filenames_t;

/* Create a new, empty notmuch database located at 'path'.
 *
 * The path should be a top-level directory to a collection of
 * plain-text email messages (one message per file). This call will
 * create a new ".notmuch" directory within 'path' where notmuch will
 * store its data.
 *
 * After a successful call to notmuch_database_create, the returned
 * database will be open so the caller should call
 * notmuch_database_close when finished with it.
 *
 * The database will not yet have any data in it
 * (notmuch_database_create itself is a very cheap function). Messages
 * contained within 'path' can be added to the database by calling
 * notmuch_database_add_message.
 *
 * In case of any failure, this function returns NULL, (after printing
 * an error message on stderr).
 */
notmuch_database_t *
notmuch_database_create (const char *path);

typedef enum {
    NOTMUCH_DATABASE_MODE_READ_ONLY = 0,
    NOTMUCH_DATABASE_MODE_READ_WRITE
} notmuch_database_mode_t;

/* XXX: I think I'd like this to take an extra argument of
 * notmuch_status_t* for returning a status value on failure. */

/* Open an existing notmuch database located at 'path'.
 *
 * The database should have been created at some time in the past,
 * (not necessarily by this process), by calling
 * notmuch_database_create with 'path'. By default the database should be
 * opened for reading only. In order to write to the database you need to
 * pass the NOTMUCH_DATABASE_MODE_READ_WRITE mode.
 *
 * An existing notmuch database can be identified by the presence of a
 * directory named ".notmuch" below 'path'.
 *
 * The caller should call notmuch_database_close when finished with
 * this database.
 *
 * In case of any failure, this function returns NULL, (after printing
 * an error message on stderr).
 */
notmuch_database_t *
notmuch_database_open (const char *path,
		       notmuch_database_mode_t mode);

/* Close the given notmuch database, freeing all associated
 * resources. See notmuch_database_open. */
void
notmuch_database_close (notmuch_database_t *database);

/* Return the database path of the given database.
 *
 * The return value is a string owned by notmuch so should not be
 * modified nor freed by the caller. */
const char *
notmuch_database_get_path (notmuch_database_t *database);

/* Return the database format version of the given database. */
unsigned int
notmuch_database_get_version (notmuch_database_t *database);

/* Does this database need to be upgraded before writing to it?
 *
 * If this function returns TRUE then no functions that modify the
 * database (notmuch_database_add_message, notmuch_message_add_tag,
 * notmuch_directory_set_mtime, etc.) will work unless the function
 * notmuch_database_upgrade is called successfully first. */
notmuch_bool_t
notmuch_database_needs_upgrade (notmuch_database_t *database);

/* Upgrade the current database.
 *
 * After opening a database in read-write mode, the client should
 * check if an upgrade is needed (notmuch_database_needs_upgrade) and
 * if so, upgrade with this function before making any modifications.
 *
 * The optional progress_notify callback can be used by the caller to
 * provide progress indication to the user. If non-NULL it will be
 * called periodically with 'progress' as a floating-point value in
 * the range of [0.0 .. 1.0] indicating the progress made so far in
 * the upgrade process.
 */
notmuch_status_t
notmuch_database_upgrade (notmuch_database_t *database,
			  void (*progress_notify) (void *closure,
						   double progress),
			  void *closure);

/* Retrieve a directory object from the database for 'path'.
 *
 * Here, 'path' should be a path relative to the path of 'database'
 * (see notmuch_database_get_path), or else should be an absolute path
 * with initial components that match the path of 'database'.
 *
 * Can return NULL if a Xapian exception occurs.
 */
notmuch_directory_t *
notmuch_database_get_directory (notmuch_database_t *database,
				const char *path);

/* Add a new message to the given notmuch database.
 *
 * Here,'filename' should be a path relative to the path of
 * 'database' (see notmuch_database_get_path), or else should be an
 * absolute filename with initial components that match the path of
 * 'database'.
 *
 * The file should be a single mail message (not a multi-message mbox)
 * that is expected to remain at its current location, (since the
 * notmuch database will reference the filename, and will not copy the
 * entire contents of the file.
 *
 * If 'message' is not NULL, then, on successful return '*message'
 * will be initialized to a message object that can be used for things
 * such as adding tags to the just-added message. The user should call
 * notmuch_message_destroy when done with the message. On any failure
 * '*message' will be set to NULL.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: Message successfully added to database.
 *
 * NOTMUCH_STATUS_XAPIAN_EXCEPTION: A Xapian exception occurred,
 *	message not added.
 *
 * NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID: Message has the same message
 *	ID as another message already in the database. The new
 *	filename was successfully added to the message in the database
 *	(if not already present).
 *
 * NOTMUCH_STATUS_FILE_ERROR: an error occurred trying to open the
 *	file, (such as permission denied, or file not found,
 *	etc.). Nothing added to the database.
 *
 * NOTMUCH_STATUS_FILE_NOT_EMAIL: the contents of filename don't look
 *	like an email message. Nothing added to the database.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so no message can be added.
 */
notmuch_status_t
notmuch_database_add_message (notmuch_database_t *database,
			      const char *filename,
			      const char *folder_name,
			      notmuch_message_t **message);

/* Remove a message from the given notmuch database.
 *
 * Note that only this particular filename association is removed from
 * the database. If the same message (as determined by the message ID)
 * is still available via other filenames, then the message will
 * persist in the database for those filenames. When the last filename
 * is removed for a particular message, the database content for that
 * message will be entirely removed.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: The last filename was removed and the
 *	message was removed from the database.
 *
 * NOTMUCH_STATUS_XAPIAN_EXCEPTION: A Xapian exception occurred,
 *	message not removed.
 *
 * NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID: This filename was removed but
 *	the message persists in the database with at least one other
 *	filename.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so no message can be removed.
 */
notmuch_status_t
notmuch_database_remove_message (notmuch_database_t *database,
				 const char *filename);

/* Find a message with the given message_id.
 *
 * If the database contains a message with the given message_id, then
 * a new notmuch_message_t object is returned. The caller should call
 * notmuch_message_destroy when done with the message.
 *
 * This function returns NULL in the following situations:
 *
 *	* No message is found with the given message_id
 *	* An out-of-memory situation occurs
 *	* A Xapian exception occurs
 */
notmuch_message_t *
notmuch_database_find_message (notmuch_database_t *database,
			       const char *message_id);

/* Return a list of all tags found in the database.
 *
 * This function creates a list of all tags found in the database. The
 * resulting list contains all tags from all messages found in the database.
 *
 * On error this function returns NULL.
 */
notmuch_tags_t *
notmuch_database_get_all_tags (notmuch_database_t *db);

/* Create a new query for 'database'.
 *
 * Here, 'database' should be an open database, (see
 * notmuch_database_open and notmuch_database_create).
 *
 * For the query string, we'll document the syntax here more
 * completely in the future, but it's likely to be a specialized
 * version of the general Xapian query syntax:
 *
 * http://xapian.org/docs/queryparser.html
 *
 * As a special case, passing either a length-zero string, (that is ""),
 * or a string consisting of a single asterisk (that is "*"), will
 * result in a query that returns all messages in the database.
 *
 * See notmuch_query_set_sort for controlling the order of results.
 * See notmuch_query_search_messages and notmuch_query_search_threads
 * to actually execute the query.
 *
 * User should call notmuch_query_destroy when finished with this
 * query.
 *
 * Will return NULL if insufficient memory is available.
 */
notmuch_query_t *
notmuch_query_create (notmuch_database_t *database,
		      const char *query_string);

/* Sort values for notmuch_query_set_sort */
typedef enum {
    NOTMUCH_SORT_OLDEST_FIRST,
    NOTMUCH_SORT_NEWEST_FIRST,
    NOTMUCH_SORT_MESSAGE_ID,
    NOTMUCH_SORT_UNSORTED
} notmuch_sort_t;

/* Specify the sorting desired for this query. */
void
notmuch_query_set_sort (notmuch_query_t *query, notmuch_sort_t sort);

/* Execute a query for threads, returning a notmuch_threads_t object
 * which can be used to iterate over the results. The returned threads
 * object is owned by the query and as such, will only be valid until
 * notmuch_query_destroy.
 *
 * Typical usage might be:
 *
 *     notmuch_query_t *query;
 *     notmuch_threads_t *threads;
 *     notmuch_thread_t *thread;
 *
 *     query = notmuch_query_create (database, query_string);
 *
 *     for (threads = notmuch_query_search_threads (query);
 *          notmuch_threads_valid (threads);
 *          notmuch_threads_move_to_next (threads))
 *     {
 *         thread = notmuch_threads_get (threads);
 *         ....
 *         notmuch_thread_destroy (thread);
 *     }
 *
 *     notmuch_query_destroy (query);
 *
 * Note: If you are finished with a thread before its containing
 * query, you can call notmuch_thread_destroy to clean up some memory
 * sooner (as in the above example). Otherwise, if your thread objects
 * are long-lived, then you don't need to call notmuch_thread_destroy
 * and all the memory will still be reclaimed when the query is
 * destroyed.
 *
 * Note that there's no explicit destructor needed for the
 * notmuch_threads_t object. (For consistency, we do provide a
 * notmuch_threads_destroy function, but there's no good reason
 * to call it if the query is about to be destroyed).
 */
notmuch_threads_t *
notmuch_query_search_threads (notmuch_query_t *query);

/* Execute a query for messages, returning a notmuch_messages_t object
 * which can be used to iterate over the results. The returned
 * messages object is owned by the query and as such, will only be
 * valid until notmuch_query_destroy.
 *
 * Typical usage might be:
 *
 *     notmuch_query_t *query;
 *     notmuch_messages_t *messages;
 *     notmuch_message_t *message;
 *
 *     query = notmuch_query_create (database, query_string);
 *
 *     for (messages = notmuch_query_search_messages (query);
 *          notmuch_messages_valid (messages);
 *          notmuch_messages_move_to_next (messages))
 *     {
 *         message = notmuch_messages_get (messages);
 *         ....
 *         notmuch_message_destroy (message);
 *     }
 *
 *     notmuch_query_destroy (query);
 *
 * Note: If you are finished with a message before its containing
 * query, you can call notmuch_message_destroy to clean up some memory
 * sooner (as in the above example). Otherwise, if your message
 * objects are long-lived, then you don't need to call
 * notmuch_message_destroy and all the memory will still be reclaimed
 * when the query is destroyed.
 *
 * Note that there's no explicit destructor needed for the
 * notmuch_messages_t object. (For consistency, we do provide a
 * notmuch_messages_destroy function, but there's no good
 * reason to call it if the query is about to be destroyed).
 *
 * If a Xapian exception occurs this function will return NULL.
 */
notmuch_messages_t *
notmuch_query_search_messages (notmuch_query_t *query);

/* Destroy a notmuch_query_t along with any associated resources.
 *
 * This will in turn destroy any notmuch_threads_t and
 * notmuch_messages_t objects generated by this query, (and in
 * turn any notmuch_thread_t and notmuch_message_t objects generated
 * from those results, etc.), if such objects haven't already been
 * destroyed.
 */
void
notmuch_query_destroy (notmuch_query_t *query);

/* Is the given 'threads' iterator pointing at a valid thread.
 *
 * When this function returns TRUE, notmuch_threads_get will return a
 * valid object. Whereas when this function returns FALSE,
 * notmuch_threads_get will return NULL.
 *
 * See the documentation of notmuch_query_search_threads for example
 * code showing how to iterate over a notmuch_threads_t object.
 */
notmuch_bool_t
notmuch_threads_valid (notmuch_threads_t *threads);

/* Get the current thread from 'threads' as a notmuch_thread_t.
 *
 * Note: The returned thread belongs to 'threads' and has a lifetime
 * identical to it (and the query to which it belongs).
 *
 * See the documentation of notmuch_query_search_threads for example
 * code showing how to iterate over a notmuch_threads_t object.
 *
 * If an out-of-memory situation occurs, this function will return
 * NULL.
 */
notmuch_thread_t *
notmuch_threads_get (notmuch_threads_t *threads);

/* Move the 'threads' iterator to the next thread.
 *
 * If 'threads' is already pointing at the last thread then the
 * iterator will be moved to a point just beyond that last thread,
 * (where notmuch_threads_valid will return FALSE and
 * notmuch_threads_get will return NULL).
 *
 * See the documentation of notmuch_query_search_threads for example
 * code showing how to iterate over a notmuch_threads_t object.
 */
void
notmuch_threads_move_to_next (notmuch_threads_t *threads);

/* Destroy a notmuch_threads_t object.
 *
 * It's not strictly necessary to call this function. All memory from
 * the notmuch_threads_t object will be reclaimed when the
 * containg query object is destroyed.
 */
void
notmuch_threads_destroy (notmuch_threads_t *threads);

/* Return an estimate of the number of messages matching a search
 *
 * This function performs a search and returns Xapian's best
 * guess as to number of matching messages.
 *
 * If a Xapian exception occurs, this function may return 0 (after
 * printing a message).
 */
unsigned
notmuch_query_count_messages (notmuch_query_t *query);
 
/* Get the thread ID of 'thread'.
 *
 * The returned string belongs to 'thread' and as such, should not be
 * modified by the caller and will only be valid for as long as the
 * thread is valid, (which is until notmuch_thread_destroy or until
 * the query from which it derived is destroyed).
 */
const char *
notmuch_thread_get_thread_id (notmuch_thread_t *thread);

/* Get the total number of messages in 'thread'.
 *
 * This count consists of all messages in the database belonging to
 * this thread. Contrast with notmuch_thread_get_matched_messages() .
 */
int
notmuch_thread_get_total_messages (notmuch_thread_t *thread);

/* Get a notmuch_messages_t iterator for the top-level messages in
 * 'thread'.
 *
 * This iterator will not necessarily iterate over all of the messages
 * in the thread. It will only iterate over the messages in the thread
 * which are not replies to other messages in the thread.
 *
 * To iterate over all messages in the thread, the caller will need to
 * iterate over the result of notmuch_message_get_replies for each
 * top-level message (and do that recursively for the resulting
 * messages, etc.).
 */
notmuch_messages_t *
notmuch_thread_get_toplevel_messages (notmuch_thread_t *thread);

/* Get the number of messages in 'thread' that matched the search.
 *
 * This count includes only the messages in this thread that were
 * matched by the search from which the thread was created. Contrast
 * with notmuch_thread_get_total_messages() .
 */
int
notmuch_thread_get_matched_messages (notmuch_thread_t *thread);

/* Get the authors of 'thread'
 *
 * The returned string is a comma-separated list of the names of the
 * authors of mail messages in the query results that belong to this
 * thread.
 *
 * The returned string belongs to 'thread' and as such, should not be
 * modified by the caller and will only be valid for as long as the
 * thread is valid, (which is until notmuch_thread_destroy or until
 * the query from which it derived is destroyed).
 */
const char *
notmuch_thread_get_authors (notmuch_thread_t *thread);

/* Get the subject of 'thread'
 *
 * The subject is taken from the first message (according to the query
 * order---see notmuch_query_set_sort) in the query results that
 * belongs to this thread.
 *
 * The returned string belongs to 'thread' and as such, should not be
 * modified by the caller and will only be valid for as long as the
 * thread is valid, (which is until notmuch_thread_destroy or until
 * the query from which it derived is destroyed).
 */
const char *
notmuch_thread_get_subject (notmuch_thread_t *thread);

/* Get the date of the oldest message in 'thread' as a time_t value.
 */
time_t
notmuch_thread_get_oldest_date (notmuch_thread_t *thread);

/* Get the date of the newest message in 'thread' as a time_t value.
 */
time_t
notmuch_thread_get_newest_date (notmuch_thread_t *thread);

/* Get the tags for 'thread', returning a notmuch_tags_t object which
 * can be used to iterate over all tags.
 *
 * Note: In the Notmuch database, tags are stored on individual
 * messages, not on threads. So the tags returned here will be all
 * tags of the messages which matched the search and which belong to
 * this thread.
 *
 * The tags object is owned by the thread and as such, will only be
 * valid for as long as the thread is valid, (for example, until
 * notmuch_thread_destroy or until the query from which it derived is
 * destroyed).
 *
 * Typical usage might be:
 *
 *     notmuch_thread_t *thread;
 *     notmuch_tags_t *tags;
 *     const char *tag;
 *
 *     thread = notmuch_threads_get (threads);
 *
 *     for (tags = notmuch_thread_get_tags (thread);
 *          notmuch_tags_valid (tags);
 *          notmuch_result_move_to_next (tags))
 *     {
 *         tag = notmuch_tags_get (tags);
 *         ....
 *     }
 *
 *     notmuch_thread_destroy (thread);
 *
 * Note that there's no explicit destructor needed for the
 * notmuch_tags_t object. (For consistency, we do provide a
 * notmuch_tags_destroy function, but there's no good reason to call
 * it if the message is about to be destroyed).
 */
notmuch_tags_t *
notmuch_thread_get_tags (notmuch_thread_t *thread);

/* Destroy a notmuch_thread_t object. */
void
notmuch_thread_destroy (notmuch_thread_t *thread);

/* Is the given 'messages' iterator pointing at a valid message.
 *
 * When this function returns TRUE, notmuch_messages_get will return a
 * valid object. Whereas when this function returns FALSE,
 * notmuch_messages_get will return NULL.
 *
 * See the documentation of notmuch_query_search_messages for example
 * code showing how to iterate over a notmuch_messages_t object.
 */
notmuch_bool_t
notmuch_messages_valid (notmuch_messages_t *messages);

/* Get the current message from 'messages' as a notmuch_message_t.
 *
 * Note: The returned message belongs to 'messages' and has a lifetime
 * identical to it (and the query to which it belongs).
 *
 * See the documentation of notmuch_query_search_messages for example
 * code showing how to iterate over a notmuch_messages_t object.
 *
 * If an out-of-memory situation occurs, this function will return
 * NULL.
 */
notmuch_message_t *
notmuch_messages_get (notmuch_messages_t *messages);

/* Move the 'messages' iterator to the next message.
 *
 * If 'messages' is already pointing at the last message then the
 * iterator will be moved to a point just beyond that last message,
 * (where notmuch_messages_valid will return FALSE and
 * notmuch_messages_get will return NULL).
 *
 * See the documentation of notmuch_query_search_messages for example
 * code showing how to iterate over a notmuch_messages_t object.
 */
void
notmuch_messages_move_to_next (notmuch_messages_t *messages);

/* Destroy a notmuch_messages_t object.
 *
 * It's not strictly necessary to call this function. All memory from
 * the notmuch_messages_t object will be reclaimed when the containing
 * query object is destroyed.
 */
void
notmuch_messages_destroy (notmuch_messages_t *messages);

/* Return a list of tags from all messages.
 *
 * The resulting list is guaranteed not to contain duplicated tags.
 *
 * WARNING: You can no longer iterate over messages after calling this
 * function, because the iterator will point at the end of the list.
 * We do not have a function to reset the iterator yet and the only
 * way how you can iterate over the list again is to recreate the
 * message list.
 *
 * The function returns NULL on error.
 */
notmuch_tags_t *
notmuch_messages_collect_tags (notmuch_messages_t *messages);

/* Get the message ID of 'message'.
 *
 * The returned string belongs to 'message' and as such, should not be
 * modified by the caller and will only be valid for as long as the
 * message is valid, (which is until the query from which it derived
 * is destroyed).
 *
 * This function will not return NULL since Notmuch ensures that every
 * message has a unique message ID, (Notmuch will generate an ID for a
 * message if the original file does not contain one).
 */
const char *
notmuch_message_get_message_id (notmuch_message_t *message);

/* Get the thread ID of 'message'.
 *
 * The returned string belongs to 'message' and as such, should not be
 * modified by the caller and will only be valid for as long as the
 * message is valid, (for example, until the user calls
 * notmuch_message_destroy on 'message' or until a query from which it
 * derived is destroyed).
 *
 * This function will not return NULL since Notmuch ensures that every
 * message belongs to a single thread.
 */
const char *
notmuch_message_get_thread_id (notmuch_message_t *message);

/* Get a notmuch_messages_t iterator for all of the replies to
 * 'message'.
 *
 * Note: This call only makes sense if 'message' was ultimately
 * obtained from a notmuch_thread_t object, (such as by coming
 * directly from the result of calling notmuch_thread_get_
 * toplevel_messages or by any number of subsequent
 * calls to notmuch_message_get_replies).
 *
 * If 'message' was obtained through some non-thread means, (such as
 * by a call to notmuch_query_search_messages), then this function
 * will return NULL.
 *
 * If there are no replies to 'message', this function will return
 * NULL. (Note that notmuch_messages_valid will accept that NULL
 * value as legitimate, and simply return FALSE for it.)
 */
notmuch_messages_t *
notmuch_message_get_replies (notmuch_message_t *message);

/* Get a filename for the email corresponding to 'message'.
 *
 * The returned filename is an absolute filename, (the initial
 * component will match notmuch_database_get_path() ).
 *
 * The returned string belongs to the message so should not be
 * modified or freed by the caller (nor should it be referenced after
 * the message is destroyed).
 *
 * Note: If this message corresponds to multiple files in the mail
 * store, (that is, multiple files contain identical message IDs),
 * this function will arbitrarily return a single one of those
 * filenames.
 */
const char *
notmuch_message_get_filename (notmuch_message_t *message);

/* Message flags */
typedef enum _notmuch_message_flag {
    NOTMUCH_MESSAGE_FLAG_MATCH,
} notmuch_message_flag_t;

/* Get a value of a flag for the email corresponding to 'message'. */
notmuch_bool_t
notmuch_message_get_flag (notmuch_message_t *message,
			  notmuch_message_flag_t flag);

/* Set a value of a flag for the email corresponding to 'message'. */
void
notmuch_message_set_flag (notmuch_message_t *message,
			  notmuch_message_flag_t flag, notmuch_bool_t value);

/* Get the date of 'message' as a time_t value.
 *
 * For the original textual representation of the Date header from the
 * message call notmuch_message_get_header() with a header value of
 * "date". */
time_t
notmuch_message_get_date  (notmuch_message_t *message);

/* Get the value of the specified header from 'message'.
 *
 * The value will be read from the actual message file, not from the
 * notmuch database. The header name is case insensitive.
 *
 * The returned string belongs to the message so should not be
 * modified or freed by the caller (nor should it be referenced after
 * the message is destroyed).
 *
 * Returns an empty string ("") if the message does not contain a
 * header line matching 'header'. Returns NULL if any error occurs.
 */
const char *
notmuch_message_get_header (notmuch_message_t *message, const char *header);

/* Get the tags for 'message', returning a notmuch_tags_t object which
 * can be used to iterate over all tags.
 *
 * The tags object is owned by the message and as such, will only be
 * valid for as long as the message is valid, (which is until the
 * query from which it derived is destroyed).
 *
 * Typical usage might be:
 *
 *     notmuch_message_t *message;
 *     notmuch_tags_t *tags;
 *     const char *tag;
 *
 *     message = notmuch_database_find_message (database, message_id);
 *
 *     for (tags = notmuch_message_get_tags (message);
 *          notmuch_tags_valid (tags);
 *          notmuch_result_move_to_next (tags))
 *     {
 *         tag = notmuch_tags_get (tags);
 *         ....
 *     }
 *
 *     notmuch_message_destroy (message);
 *
 * Note that there's no explicit destructor needed for the
 * notmuch_tags_t object. (For consistency, we do provide a
 * notmuch_tags_destroy function, but there's no good reason to call
 * it if the message is about to be destroyed).
 */
notmuch_tags_t *
notmuch_message_get_tags (notmuch_message_t *message);

/* The longest possible tag value. */
#define NOTMUCH_TAG_MAX 200

/* Add a tag to the given message.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: Tag successfully added to message
 *
 * NOTMUCH_STATUS_NULL_POINTER: The 'tag' argument is NULL
 *
 * NOTMUCH_STATUS_TAG_TOO_LONG: The length of 'tag' is too long
 *	(exceeds NOTMUCH_TAG_MAX)
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so message cannot be modified.
 */
notmuch_status_t
notmuch_message_add_tag (notmuch_message_t *message, const char *tag);

/* Remove a tag from the given message.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: Tag successfully removed from message
 *
 * NOTMUCH_STATUS_NULL_POINTER: The 'tag' argument is NULL
 *
 * NOTMUCH_STATUS_TAG_TOO_LONG: The length of 'tag' is too long
 *	(exceeds NOTMUCH_TAG_MAX)
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so message cannot be modified.
 */
notmuch_status_t
notmuch_message_remove_tag (notmuch_message_t *message, const char *tag);

/* Remove all tags from the given message.
 *
 * See notmuch_message_freeze for an example showing how to safely
 * replace tag values.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so message cannot be modified.
 */
notmuch_status_t
notmuch_message_remove_all_tags (notmuch_message_t *message);

/* Freeze the current state of 'message' within the database.
 *
 * This means that changes to the message state, (via
 * notmuch_message_add_tag, notmuch_message_remove_tag, and
 * notmuch_message_remove_all_tags), will not be committed to the
 * database until the message is thawed with notmuch_message_thaw.
 *
 * Multiple calls to freeze/thaw are valid and these calls will
 * "stack". That is there must be as many calls to thaw as to freeze
 * before a message is actually thawed.
 *
 * The ability to do freeze/thaw allows for safe transactions to
 * change tag values. For example, explicitly setting a message to
 * have a given set of tags might look like this:
 *
 *    notmuch_message_freeze (message);
 *
 *    notmuch_message_remove_all_tags (message);
 *
 *    for (i = 0; i < NUM_TAGS; i++)
 *        notmuch_message_add_tag (message, tags[i]);
 *
 *    notmuch_message_thaw (message);
 *
 * With freeze/thaw used like this, the message in the database is
 * guaranteed to have either the full set of original tag values, or
 * the full set of new tag values, but nothing in between.
 *
 * Imagine the example above without freeze/thaw and the operation
 * somehow getting interrupted. This could result in the message being
 * left with no tags if the interruption happened after
 * notmuch_message_remove_all_tags but before notmuch_message_add_tag.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: Message successfully frozen.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so message cannot be modified.
 */
notmuch_status_t
notmuch_message_freeze (notmuch_message_t *message);

/* Thaw the current 'message', synchronizing any changes that may have
 * occurred while 'message' was frozen into the notmuch database.
 *
 * See notmuch_message_freeze for an example of how to use this
 * function to safely provide tag changes.
 *
 * Multiple calls to freeze/thaw are valid and these calls with
 * "stack". That is there must be as many calls to thaw as to freeze
 * before a message is actually thawed.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: Message successfully thawed, (or at least
 *	its frozen count has successfully been reduced by 1).
 *
 * NOTMUCH_STATUS_UNBALANCED_FREEZE_THAW: An attempt was made to thaw
 *	an unfrozen message. That is, there have been an unbalanced
 *	number of calls to notmuch_message_freeze and
 *	notmuch_message_thaw.
 */
notmuch_status_t
notmuch_message_thaw (notmuch_message_t *message);

/* Destroy a notmuch_message_t object.
 *
 * It can be useful to call this function in the case of a single
 * query object with many messages in the result, (such as iterating
 * over the entire database). Otherwise, it's fine to never call this
 * function and there will still be no memory leaks. (The memory from
 * the messages get reclaimed when the containing query is destroyed.)
 */
void
notmuch_message_destroy (notmuch_message_t *message);

/* Is the given 'tags' iterator pointing at a valid tag.
 *
 * When this function returns TRUE, notmuch_tags_get will return a
 * valid string. Whereas when this function returns FALSE,
 * notmuch_tags_get will return NULL.
 *
 * See the documentation of notmuch_message_get_tags for example code
 * showing how to iterate over a notmuch_tags_t object.
 */
notmuch_bool_t
notmuch_tags_valid (notmuch_tags_t *tags);

/* Get the current tag from 'tags' as a string.
 *
 * Note: The returned string belongs to 'tags' and has a lifetime
 * identical to it (and the query to which it ultimately belongs).
 *
 * See the documentation of notmuch_message_get_tags for example code
 * showing how to iterate over a notmuch_tags_t object.
 */
const char *
notmuch_tags_get (notmuch_tags_t *tags);

/* Move the 'tags' iterator to the next tag.
 *
 * If 'tags' is already pointing at the last tag then the iterator
 * will be moved to a point just beyond that last tag, (where
 * notmuch_tags_valid will return FALSE and notmuch_tags_get will
 * return NULL).
 *
 * See the documentation of notmuch_message_get_tags for example code
 * showing how to iterate over a notmuch_tags_t object.
 */
void
notmuch_tags_move_to_next (notmuch_tags_t *tags);

/* Destroy a notmuch_tags_t object.
 *
 * It's not strictly necessary to call this function. All memory from
 * the notmuch_tags_t object will be reclaimed when the containing
 * message or query objects are destroyed.
 */
void
notmuch_tags_destroy (notmuch_tags_t *tags);

/* Store an mtime within the database for 'directory'.
 *
 * The 'directory' should be an object retrieved from the database
 * with notmuch_database_get_directory for a particular path.
 *
 * The intention is for the caller to use the mtime to allow efficient
 * identification of new messages to be added to the database. The
 * recommended usage is as follows:
 *
 *   o Read the mtime of a directory from the filesystem
 *
 *   o Call add_message for all mail files in the directory
 *
 *   o Call notmuch_directory_set_mtime with the mtime read from the
 *     filesystem.
 *
 * Then, when wanting to check for updates to the directory in the
 * future, the client can call notmuch_directory_get_mtime and know
 * that it only needs to add files if the mtime of the directory and
 * files are newer than the stored timestamp.
 *
 * Note: The notmuch_directory_get_mtime function does not allow the
 * caller to distinguish a timestamp of 0 from a non-existent
 * timestamp. So don't store a timestamp of 0 unless you are
 * comfortable with that.
 *
 * Return value:
 *
 * NOTMUCH_STATUS_SUCCESS: mtime successfully stored in database.
 *
 * NOTMUCH_STATUS_XAPIAN_EXCEPTION: A Xapian exception
 *	occurred, mtime not stored.
 *
 * NOTMUCH_STATUS_READ_ONLY_DATABASE: Database was opened in read-only
 *	mode so directory mtime cannot be modified.
 */
notmuch_status_t
notmuch_directory_set_mtime (notmuch_directory_t *directory,
			     time_t mtime);

/* Get the mtime of a directory, (as previously stored with
 * notmuch_directory_set_mtime).
 *
 * Returns 0 if no mtime has previously been stored for this
 * directory.*/
time_t
notmuch_directory_get_mtime (notmuch_directory_t *directory);

/* Get a notmuch_filenames_t iterator listing all the filenames of
 * messages in the database within the given directory.
 *
 * The returned filenames will be the basename-entries only (not
 * complete paths). */
notmuch_filenames_t *
notmuch_directory_get_child_files (notmuch_directory_t *directory);

/* Get a notmuch_filenams_t iterator listing all the filenames of
 * sub-directories in the database within the given directory.
 *
 * The returned filenames will be the basename-entries only (not
 * complete paths). */
notmuch_filenames_t *
notmuch_directory_get_child_directories (notmuch_directory_t *directory);

/* Destroy a notmuch_directory_t object. */
void
notmuch_directory_destroy (notmuch_directory_t *directory);

/* Is the given 'filenames' iterator pointing at a valid filename.
 *
 * When this function returns TRUE, notmuch_filenames_get will return
 * a valid string. Whereas when this function returns FALSE,
 * notmuch_filenames_get will return NULL.
 *
 * It is acceptable to pass NULL for 'filenames', in which case this
 * function will always return FALSE.
 */
notmuch_bool_t
notmuch_filenames_valid (notmuch_filenames_t *filenames);

/* Get the current filename from 'filenames' as a string.
 *
 * Note: The returned string belongs to 'filenames' and has a lifetime
 * identical to it (and the directory to which it ultimately belongs).
 *
 * It is acceptable to pass NULL for 'filenames', in which case this
 * function will always return NULL.
 */
const char *
notmuch_filenames_get (notmuch_filenames_t *filenames);

/* Move the 'filenames' iterator to the next filename.
 *
 * If 'filenames' is already pointing at the last filename then the
 * iterator will be moved to a point just beyond that last filename,
 * (where notmuch_filenames_valid will return FALSE and
 * notmuch_filenames_get will return NULL).
 *
 * It is acceptable to pass NULL for 'filenames', in which case this
 * function will do nothing.
 */
void
notmuch_filenames_move_to_next (notmuch_filenames_t *filenames);

/* Destroy a notmuch_filenames_t object.
 *
 * It's not strictly necessary to call this function. All memory from
 * the notmuch_filenames_t object will be reclaimed when the
 * containing directory object is destroyed.
 *
 * It is acceptable to pass NULL for 'filenames', in which case this
 * function will do nothing.
 */
void
notmuch_filenames_destroy (notmuch_filenames_t *filenames);

NOTMUCH_END_DECLS

#endif
