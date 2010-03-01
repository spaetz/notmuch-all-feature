/* database.cc - The database interfaces of the notmuch mail library
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

#include "database-private.h"

#include <iostream>

#include <sys/time.h>
#include <signal.h>
#include <xapian.h>

#include <glib.h> /* g_free, GPtrArray, GHashTable */

using namespace std;

#define ARRAY_SIZE(arr) (sizeof (arr) / sizeof (arr[0]))

typedef struct {
    const char *name;
    const char *prefix;
} prefix_t;

#define NOTMUCH_DATABASE_VERSION 1

#define STRINGIFY(s) _SUB_STRINGIFY(s)
#define _SUB_STRINGIFY(s) #s

/* Here's the current schema for our database (for NOTMUCH_DATABASE_VERSION):
 *
 * We currently have two different types of documents: mail and directory.
 *
 * Mail document
 * -------------
 * A mail document is associated with a particular email message file
 * on disk. It is indexed with the following prefixed terms which the
 * database uses to construct threads, etc.:
 *
 *    Single terms of given prefix:
 *
 *	type:	mail
 *
 *	id:	Unique ID of mail, (from Message-ID header or generated
 *		as "notmuch-sha1-<sha1_sum_of_entire_file>.
 *
 *	thread:	The ID of the thread to which the mail belongs
 *
 *	replyto: The ID from the In-Reply-To header of the mail (if any).
 *
 *    Multiple terms of given prefix:
 *
 *	reference: All message IDs from In-Reply-To and Re ferences
 *		   headers in the message.
 *
 *	tag:	   Any tags associated with this message by the user.
 *
 *	file-direntry:  A colon-separated pair of values
 *		        (INTEGER:STRING), where INTEGER is the
 *		        document ID of a directory document, and
 *		        STRING is the name of a file within that
 *		        directory for this mail message.
 *
 *    A mail document also has two values:
 *
 *	TIMESTAMP:	The time_t value corresponding to the message's
 *			Date header.
 *
 *	MESSAGE_ID:	The unique ID of the mail mess (see "id" above)
 *
 * In addition, terms from the content of the message are added with
 * "from", "to", "attachment", and "subject" prefixes for use by the
 * user in searching. But the database doesn't really care itself
 * about any of these.
 *
 * The data portion of a mail document is empty.
 *
 * Directory document
 * ------------------
 * A directory document is used by a client of the notmuch library to
 * maintain data necessary to allow for efficient polling of mail
 * directories.
 *
 * All directory documents contain one term:
 *
 *	directory:	The directory path (relative to the database path)
 *			Or the SHA1 sum of the directory path (if the
 *			path itself is too long to fit in a Xapian
 *			term).
 *
 * And all directory documents for directories other than top-level
 * directories also contain the following term:
 *
 *	directory-direntry: A colon-separated pair of values
 *		            (INTEGER:STRING), where INTEGER is the
 *		            document ID of the parent directory
 *		            document, and STRING is the name of this
 *		            directory within that parent.
 *
 * All directory documents have a single value:
 *
 *	TIMESTAMP:	The mtime of the directory (at last scan)
 *
 * The data portion of a directory document contains the path of the
 * directory (relative to the database path).
 */

/* With these prefix values we follow the conventions published here:
 *
 * http://xapian.org/docs/omega/termprefixes.html
 *
 * as much as makes sense. Note that I took some liberty in matching
 * the reserved prefix values to notmuch concepts, (for example, 'G'
 * is documented as "newsGroup (or similar entity - e.g. a web forum
 * name)", for which I think the thread is the closest analogue in
 * notmuch. This in spite of the fact that we will eventually be
 * storing mailing-list messages where 'G' for "mailing list name"
 * might be even a closer analogue. I'm treating the single-character
 * prefixes preferentially for core notmuch concepts (which will be
 * nearly universal to all mail messages).
 */

prefix_t BOOLEAN_PREFIX_INTERNAL[] = {
    { "type",			"T" },
    { "reference",		"XREFERENCE" },
    { "replyto",		"XREPLYTO" },
    { "directory",		"XDIRECTORY" },
    { "file-direntry",		"XFDIRENTRY" },
    { "directory-direntry",	"XDDIRENTRY" },
};

prefix_t BOOLEAN_PREFIX_EXTERNAL[] = {
    { "thread",			"G" },
    { "tag",			"K" },
    { "id",			"Q" }
};

prefix_t PROBABILISTIC_PREFIX[]= {
    { "from",			"XFROM" },
    { "to",			"XTO" },
    { "attachment",		"XATTACHMENT" },
    { "subject",		"XSUBJECT"}
};

int
_internal_error (const char *format, ...)
{
    va_list va_args;

    va_start (va_args, format);

    fprintf (stderr, "Internal error: ");
    vfprintf (stderr, format, va_args);

    exit (1);

    return 1;
}

const char *
_find_prefix (const char *name)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE (BOOLEAN_PREFIX_INTERNAL); i++) {
	if (strcmp (name, BOOLEAN_PREFIX_INTERNAL[i].name) == 0)
	    return BOOLEAN_PREFIX_INTERNAL[i].prefix;
    }

    for (i = 0; i < ARRAY_SIZE (BOOLEAN_PREFIX_EXTERNAL); i++) {
	if (strcmp (name, BOOLEAN_PREFIX_EXTERNAL[i].name) == 0)
	    return BOOLEAN_PREFIX_EXTERNAL[i].prefix;
    }

    for (i = 0; i < ARRAY_SIZE (PROBABILISTIC_PREFIX); i++) {
	if (strcmp (name, PROBABILISTIC_PREFIX[i].name) == 0)
	    return PROBABILISTIC_PREFIX[i].prefix;
    }

    INTERNAL_ERROR ("No prefix exists for '%s'\n", name);

    return "";
}

const char *
notmuch_status_to_string (notmuch_status_t status)
{
    switch (status) {
    case NOTMUCH_STATUS_SUCCESS:
	return "No error occurred";
    case NOTMUCH_STATUS_OUT_OF_MEMORY:
	return "Out of memory";
    case NOTMUCH_STATUS_READ_ONLY_DATABASE:
	return "Attempt to write to a read-only database";
    case NOTMUCH_STATUS_XAPIAN_EXCEPTION:
	return "A Xapian exception occurred";
    case NOTMUCH_STATUS_FILE_ERROR:
	return "Something went wrong trying to read or write a file";
    case NOTMUCH_STATUS_FILE_NOT_EMAIL:
	return "File is not an email";
    case NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID:
	return "Message ID is identical to a message in database";
    case NOTMUCH_STATUS_NULL_POINTER:
	return "Erroneous NULL pointer";
    case NOTMUCH_STATUS_TAG_TOO_LONG:
	return "Tag value is too long (exceeds NOTMUCH_TAG_MAX)";
    case NOTMUCH_STATUS_INVALID_DATE:
	return "Date value did not parse to a valid date";
    case NOTMUCH_STATUS_UNBALANCED_FREEZE_THAW:
	return "Unbalanced number of calls to notmuch_message_freeze/thaw";
    default:
    case NOTMUCH_STATUS_LAST_STATUS:
	return "Unknown error status value";
    }
}

static void
find_doc_ids_for_term (notmuch_database_t *notmuch,
		       const char *term,
		       Xapian::PostingIterator *begin,
		       Xapian::PostingIterator *end)
{
    *begin = notmuch->xapian_db->postlist_begin (term);

    *end = notmuch->xapian_db->postlist_end (term);
}

static void
find_doc_ids (notmuch_database_t *notmuch,
	      const char *prefix_name,
	      const char *value,
	      Xapian::PostingIterator *begin,
	      Xapian::PostingIterator *end)
{
    char *term;

    term = talloc_asprintf (notmuch, "%s%s",
			    _find_prefix (prefix_name), value);

    find_doc_ids_for_term (notmuch, term, begin, end);

    talloc_free (term);
}

notmuch_private_status_t
_notmuch_database_find_unique_doc_id (notmuch_database_t *notmuch,
				      const char *prefix_name,
				      const char *value,
				      unsigned int *doc_id)
{
    Xapian::PostingIterator i, end;

    find_doc_ids (notmuch, prefix_name, value, &i, &end);

    if (i == end) {
	*doc_id = 0;
	return NOTMUCH_PRIVATE_STATUS_NO_DOCUMENT_FOUND;
    }

    *doc_id = *i;

#if DEBUG_DATABASE_SANITY
    i++;

    if (i != end)
	INTERNAL_ERROR ("Term %s:%s is not unique as expected.\n",
			prefix_name, value);
#endif

    return NOTMUCH_PRIVATE_STATUS_SUCCESS;
}

static Xapian::Document
find_document_for_doc_id (notmuch_database_t *notmuch, unsigned doc_id)
{
    return notmuch->xapian_db->get_document (doc_id);
}

notmuch_message_t *
notmuch_database_find_message (notmuch_database_t *notmuch,
			       const char *message_id)
{
    notmuch_private_status_t status;
    unsigned int doc_id;

    status = _notmuch_database_find_unique_doc_id (notmuch, "id",
						   message_id, &doc_id);

    if (status == NOTMUCH_PRIVATE_STATUS_NO_DOCUMENT_FOUND)
	return NULL;

    return _notmuch_message_create (notmuch, notmuch, doc_id, NULL);
}

/* Advance 'str' past any whitespace or RFC 822 comments. A comment is
 * a (potentially nested) parenthesized sequence with '\' used to
 * escape any character (including parentheses).
 *
 * If the sequence to be skipped continues to the end of the string,
 * then 'str' will be left pointing at the final terminating '\0'
 * character.
 */
static void
skip_space_and_comments (const char **str)
{
    const char *s;

    s = *str;
    while (*s && (isspace (*s) || *s == '(')) {
	while (*s && isspace (*s))
	    s++;
	if (*s == '(') {
	    int nesting = 1;
	    s++;
	    while (*s && nesting) {
		if (*s == '(') {
		    nesting++;
		} else if (*s == ')') {
		    nesting--;
		} else if (*s == '\\') {
		    if (*(s+1))
			s++;
		}
		s++;
	    }
	}
    }

    *str = s;
}

/* Parse an RFC 822 message-id, discarding whitespace, any RFC 822
 * comments, and the '<' and '>' delimeters.
 *
 * If not NULL, then *next will be made to point to the first character
 * not parsed, (possibly pointing to the final '\0' terminator.
 *
 * Returns a newly talloc'ed string belonging to 'ctx'.
 *
 * Returns NULL if there is any error parsing the message-id. */
static char *
_parse_message_id (void *ctx, const char *message_id, const char **next)
{
    const char *s, *end;
    char *result;

    if (message_id == NULL || *message_id == '\0')
	return NULL;

    s = message_id;

    skip_space_and_comments (&s);

    /* Skip any unstructured text as well. */
    while (*s && *s != '<')
	s++;

    if (*s == '<') {
	s++;
    } else {
	if (next)
	    *next = s;
	return NULL;
    }

    skip_space_and_comments (&s);

    end = s;
    while (*end && *end != '>')
	end++;
    if (next) {
	if (*end)
	    *next = end + 1;
	else
	    *next = end;
    }

    if (end > s && *end == '>')
	end--;
    if (end <= s)
	return NULL;

    result = talloc_strndup (ctx, s, end - s + 1);

    /* Finally, collapse any whitespace that is within the message-id
     * itself. */
    {
	char *r;
	int len;

	for (r = result, len = strlen (r); *r; r++, len--)
	    if (*r == ' ' || *r == '\t')
		memmove (r, r+1, len);
    }

    return result;
}

/* Parse a References header value, putting a (talloc'ed under 'ctx')
 * copy of each referenced message-id into 'hash'.
 *
 * We explicitly avoid including any reference identical to
 * 'message_id' in the result (to avoid mass confusion when a single
 * message references itself cyclically---and yes, mail messages are
 * not infrequent in the wild that do this---don't ask me why).
*/
static void
parse_references (void *ctx,
		  const char *message_id,
		  GHashTable *hash,
		  const char *refs)
{
    char *ref;

    if (refs == NULL || *refs == '\0')
	return;

    while (*refs) {
	ref = _parse_message_id (ctx, refs, &refs);

	if (ref && strcmp (ref, message_id))
	    g_hash_table_insert (hash, ref, NULL);
    }
}

notmuch_database_t *
notmuch_database_create (const char *path)
{
    notmuch_database_t *notmuch = NULL;
    char *notmuch_path = NULL;
    struct stat st;
    int err;

    if (path == NULL) {
	fprintf (stderr, "Error: Cannot create a database for a NULL path.\n");
	goto DONE;
    }

    err = stat (path, &st);
    if (err) {
	fprintf (stderr, "Error: Cannot create database at %s: %s.\n",
		 path, strerror (errno));
	goto DONE;
    }

    if (! S_ISDIR (st.st_mode)) {
	fprintf (stderr, "Error: Cannot create database at %s: Not a directory.\n",
		 path);
	goto DONE;
    }

    notmuch_path = talloc_asprintf (NULL, "%s/%s", path, ".notmuch");

    err = mkdir (notmuch_path, 0755);

    if (err) {
	fprintf (stderr, "Error: Cannot create directory %s: %s.\n",
		 notmuch_path, strerror (errno));
	goto DONE;
    }

    notmuch = notmuch_database_open (path,
				     NOTMUCH_DATABASE_MODE_READ_WRITE);
    notmuch_database_upgrade (notmuch, NULL, NULL);

  DONE:
    if (notmuch_path)
	talloc_free (notmuch_path);

    return notmuch;
}

notmuch_status_t
_notmuch_database_ensure_writable (notmuch_database_t *notmuch)
{
    if (notmuch->mode == NOTMUCH_DATABASE_MODE_READ_ONLY) {
	fprintf (stderr, "Cannot write to a read-only database.\n");
	return NOTMUCH_STATUS_READ_ONLY_DATABASE;
    }

    return NOTMUCH_STATUS_SUCCESS;
}

struct MaildateValueRangeProcessor : public Xapian::ValueRangeProcessor {
    MaildateValueRangeProcessor() {}

    Xapian::valueno operator()(std::string &begin, std::string &end) {
      time_t begin_first,begin_last, end_first, end_last;
      int retval;

      if (begin.substr(0, 5) != "date:")
	 return Xapian::BAD_VALUENO;
      begin.erase(0, 5);

      retval = notmuch_parse_date(begin.c_str(), &begin_first, &begin_last, 0);

      if (retval == NOTMUCH_STATUS_INVALID_DATE) {
	fprintf(stderr,"Begin date failed to parse: %s",begin.c_str());
	return Xapian::BAD_VALUENO;
      }

      retval = notmuch_parse_date(end.c_str(),&end_first,&end_last,begin_first);
      if (retval == NOTMUCH_STATUS_INVALID_DATE) {
	fprintf(stderr,"End date failed to parse: %s",end.c_str());
	return Xapian::BAD_VALUENO;
      }

      begin.assign(Xapian::sortable_serialise(begin_first));
      end.assign(Xapian::sortable_serialise(end_last));

      return NOTMUCH_VALUE_TIMESTAMP;
    }
};

notmuch_database_t *
notmuch_database_open (const char *path,
		       notmuch_database_mode_t mode)
{
    notmuch_database_t *notmuch = NULL;
    char *notmuch_path = NULL, *xapian_path = NULL;
    struct stat st;
    int err;
    unsigned int i, version;

    if (asprintf (&notmuch_path, "%s/%s", path, ".notmuch") == -1) {
	notmuch_path = NULL;
	fprintf (stderr, "Out of memory\n");
	goto DONE;
    }

    err = stat (notmuch_path, &st);
    if (err) {
	fprintf (stderr, "Error opening database at %s: %s\n",
		 notmuch_path, strerror (errno));
	goto DONE;
    }

    if (asprintf (&xapian_path, "%s/%s", notmuch_path, "xapian") == -1) {
	xapian_path = NULL;
	fprintf (stderr, "Out of memory\n");
	goto DONE;
    }

    notmuch = talloc (NULL, notmuch_database_t);
    notmuch->exception_reported = FALSE;
    notmuch->path = talloc_strdup (notmuch, path);

    if (notmuch->path[strlen (notmuch->path) - 1] == '/')
	notmuch->path[strlen (notmuch->path) - 1] = '\0';

    notmuch->needs_upgrade = FALSE;
    notmuch->mode = mode;
    try {
	string last_thread_id;

	if (mode == NOTMUCH_DATABASE_MODE_READ_WRITE) {
	    notmuch->xapian_db = new Xapian::WritableDatabase (xapian_path,
							       Xapian::DB_CREATE_OR_OPEN);
	    version = notmuch_database_get_version (notmuch);

	    if (version > NOTMUCH_DATABASE_VERSION) {
		fprintf (stderr,
			 "Error: Notmuch database at %s\n"
			 "       has a newer database format version (%u) than supported by this\n"
			 "       version of notmuch (%u). Refusing to open this database in\n"
			 "       read-write mode.\n",
			 notmuch_path, version, NOTMUCH_DATABASE_VERSION);
		notmuch->mode = NOTMUCH_DATABASE_MODE_READ_ONLY;
		notmuch_database_close (notmuch);
		notmuch = NULL;
		goto DONE;
	    }

	    if (version < NOTMUCH_DATABASE_VERSION)
		notmuch->needs_upgrade = TRUE;
	} else {
	    notmuch->xapian_db = new Xapian::Database (xapian_path);
	    version = notmuch_database_get_version (notmuch);
	    if (version > NOTMUCH_DATABASE_VERSION)
	    {
		fprintf (stderr,
			 "Warning: Notmuch database at %s\n"
			 "         has a newer database format version (%u) than supported by this\n"
			 "         version of notmuch (%u). Some operations may behave incorrectly,\n"
			 "         (but the database will not be harmed since it is being opened\n"
			 "         in read-only mode).\n",
			 notmuch_path, version, NOTMUCH_DATABASE_VERSION);
	    }
	}

	last_thread_id = notmuch->xapian_db->get_metadata ("last_thread_id");
	if (last_thread_id.empty ()) {
	    notmuch->last_thread_id = 0;
	} else {
	    const char *str;
	    char *end;

	    str = last_thread_id.c_str ();
	    notmuch->last_thread_id = strtoull (str, &end, 16);
	    if (*end != '\0')
		INTERNAL_ERROR ("Malformed database last_thread_id: %s", str);
	}

	notmuch->query_parser = new Xapian::QueryParser;
	notmuch->term_gen = new Xapian::TermGenerator;
	notmuch->term_gen->set_stemmer (Xapian::Stem ("english"));
	notmuch->value_range_processor = new MaildateValueRangeProcessor();

	notmuch->query_parser->set_default_op (Xapian::Query::OP_AND);
	notmuch->query_parser->set_database (*notmuch->xapian_db);
	notmuch->query_parser->set_stemmer (Xapian::Stem ("english"));
	notmuch->query_parser->set_stemming_strategy (Xapian::QueryParser::STEM_SOME);
	notmuch->query_parser->add_valuerangeprocessor (notmuch->value_range_processor);

	for (i = 0; i < ARRAY_SIZE (BOOLEAN_PREFIX_EXTERNAL); i++) {
	    prefix_t *prefix = &BOOLEAN_PREFIX_EXTERNAL[i];
	    notmuch->query_parser->add_boolean_prefix (prefix->name,
						       prefix->prefix);
	}

	for (i = 0; i < ARRAY_SIZE (PROBABILISTIC_PREFIX); i++) {
	    prefix_t *prefix = &PROBABILISTIC_PREFIX[i];
	    notmuch->query_parser->add_prefix (prefix->name, prefix->prefix);
	}
    } catch (const Xapian::Error &error) {
	fprintf (stderr, "A Xapian exception occurred opening database: %s\n",
		 error.get_msg().c_str());
	notmuch = NULL;
    }

  DONE:
    if (notmuch_path)
	free (notmuch_path);
    if (xapian_path)
	free (xapian_path);

    return notmuch;
}

void
notmuch_database_close (notmuch_database_t *notmuch)
{
    try {
	if (notmuch->mode == NOTMUCH_DATABASE_MODE_READ_WRITE)
	    (static_cast <Xapian::WritableDatabase *> (notmuch->xapian_db))->flush ();
    } catch (const Xapian::Error &error) {
	if (! notmuch->exception_reported) {
	    fprintf (stderr, "Error: A Xapian exception occurred flushing database: %s\n",
		     error.get_msg().c_str());
	}
    }

    delete notmuch->term_gen;
    delete notmuch->query_parser;
    delete notmuch->xapian_db;
    delete notmuch->value_range_processor;
    talloc_free (notmuch);
}

const char *
notmuch_database_get_path (notmuch_database_t *notmuch)
{
    return notmuch->path;
}

unsigned int
notmuch_database_get_version (notmuch_database_t *notmuch)
{
    unsigned int version;
    string version_string;
    const char *str;
    char *end;

    version_string = notmuch->xapian_db->get_metadata ("version");
    if (version_string.empty ())
	return 0;

    str = version_string.c_str ();
    if (str == NULL || *str == '\0')
	return 0;

    version = strtoul (str, &end, 10);
    if (*end != '\0')
	INTERNAL_ERROR ("Malformed database version: %s", str);

    return version;
}

notmuch_bool_t
notmuch_database_needs_upgrade (notmuch_database_t *notmuch)
{
    return notmuch->needs_upgrade;
}

static volatile sig_atomic_t do_progress_notify = 0;

static void
handle_sigalrm (unused (int signal))
{
    do_progress_notify = 1;
}

/* Upgrade the current database.
 *
 * After opening a database in read-write mode, the client should
 * check if an upgrade is needed (notmuch_database_needs_upgrade) and
 * if so, upgrade with this function before making any modifications.
 *
 * The optional progress_notify callback can be used by the caller to
 * provide progress indication to the user. If non-NULL it will be
 * called periodically with 'count' as the number of messages upgraded
 * so far and 'total' the overall number of messages that will be
 * converted.
 */
notmuch_status_t
notmuch_database_upgrade (notmuch_database_t *notmuch,
			  void (*progress_notify) (void *closure,
						   double progress),
			  void *closure)
{
    Xapian::WritableDatabase *db;
    struct sigaction action;
    struct itimerval timerval;
    notmuch_bool_t timer_is_active = FALSE;
    unsigned int version;
    notmuch_status_t status;
    unsigned int count = 0, total = 0;

    status = _notmuch_database_ensure_writable (notmuch);
    if (status)
	return status;

    db = static_cast <Xapian::WritableDatabase *> (notmuch->xapian_db);

    version = notmuch_database_get_version (notmuch);

    if (version >= NOTMUCH_DATABASE_VERSION)
	return NOTMUCH_STATUS_SUCCESS;

    if (progress_notify) {
	/* Setup our handler for SIGALRM */
	memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = handle_sigalrm;
	sigemptyset (&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction (SIGALRM, &action, NULL);

	/* Then start a timer to send SIGALRM once per second. */
	timerval.it_interval.tv_sec = 1;
	timerval.it_interval.tv_usec = 0;
	timerval.it_value.tv_sec = 1;
	timerval.it_value.tv_usec = 0;
	setitimer (ITIMER_REAL, &timerval, NULL);

	timer_is_active = TRUE;
    }

    /* Before version 1, each message document had its filename in the
     * data field. Copy that into the new format by calling
     * notmuch_message_add_filename.
     */
    if (version < 1) {
	notmuch_query_t *query = notmuch_query_create (notmuch, "");
	notmuch_messages_t *messages;
	notmuch_message_t *message;
	char *filename;
	Xapian::TermIterator t, t_end;

	total = notmuch_query_count_messages (query);

	for (messages = notmuch_query_search_messages (query);
	     notmuch_messages_has_more (messages);
	     notmuch_messages_advance (messages))
	{
	    if (do_progress_notify) {
		progress_notify (closure, (double) count / total);
		do_progress_notify = 0;
	    }

	    message = notmuch_messages_get (messages);

	    filename = _notmuch_message_talloc_copy_data (message);
	    if (filename && *filename != '\0') {
		_notmuch_message_add_filename (message, filename);
		_notmuch_message_sync (message);
	    }
	    talloc_free (filename);

	    notmuch_message_destroy (message);

	    count++;
	}

	notmuch_query_destroy (query);

	/* Also, before version 1 we stored directory timestamps in
	 * XTIMESTAMP documents instead of the current XDIRECTORY
	 * documents. So copy those as well. */

	t_end = notmuch->xapian_db->allterms_end ("XTIMESTAMP");

	for (t = notmuch->xapian_db->allterms_begin ("XTIMESTAMP");
	     t != t_end;
	     t++)
	{
	    Xapian::PostingIterator p, p_end;
	    std::string term = *t;

	    p_end = notmuch->xapian_db->postlist_end (term);

	    for (p = notmuch->xapian_db->postlist_begin (term);
		 p != p_end;
		 p++)
	    {
		Xapian::Document document;
		time_t mtime;
		notmuch_directory_t *directory;

		if (do_progress_notify) {
		    progress_notify (closure, (double) count / total);
		    do_progress_notify = 0;
		}

		document = find_document_for_doc_id (notmuch, *p);
		mtime = Xapian::sortable_unserialise (
		    document.get_value (NOTMUCH_VALUE_TIMESTAMP));

		directory = notmuch_database_get_directory (notmuch,
							    term.c_str() + 10);
		notmuch_directory_set_mtime (directory, mtime);
		notmuch_directory_destroy (directory);
	    }
	}
    }

    db->set_metadata ("version", STRINGIFY (NOTMUCH_DATABASE_VERSION));
    db->flush ();

    /* Now that the upgrade is complete we can remove the old data
     * and documents that are no longer needed. */
    if (version < 1) {
	notmuch_query_t *query = notmuch_query_create (notmuch, "");
	notmuch_messages_t *messages;
	notmuch_message_t *message;
	char *filename;

	for (messages = notmuch_query_search_messages (query);
	     notmuch_messages_has_more (messages);
	     notmuch_messages_advance (messages))
	{
	    if (do_progress_notify) {
		progress_notify (closure, (double) count / total);
		do_progress_notify = 0;
	    }

	    message = notmuch_messages_get (messages);

	    filename = _notmuch_message_talloc_copy_data (message);
	    if (filename && *filename != '\0') {
		_notmuch_message_clear_data (message);
		_notmuch_message_sync (message);
	    }
	    talloc_free (filename);

	    notmuch_message_destroy (message);
	}

	notmuch_query_destroy (query);
    }

    if (version < 1) {
	Xapian::TermIterator t, t_end;

	t_end = notmuch->xapian_db->allterms_end ("XTIMESTAMP");

	for (t = notmuch->xapian_db->allterms_begin ("XTIMESTAMP");
	     t != t_end;
	     t++)
	{
	    Xapian::PostingIterator p, p_end;
	    std::string term = *t;

	    p_end = notmuch->xapian_db->postlist_end (term);

	    for (p = notmuch->xapian_db->postlist_begin (term);
		 p != p_end;
		 p++)
	    {
		if (do_progress_notify) {
		    progress_notify (closure, (double) count / total);
		    do_progress_notify = 0;
		}

		db->delete_document (*p);
	    }
	}
    }

    if (timer_is_active) {
	/* Now stop the timer. */
	timerval.it_interval.tv_sec = 0;
	timerval.it_interval.tv_usec = 0;
	timerval.it_value.tv_sec = 0;
	timerval.it_value.tv_usec = 0;
	setitimer (ITIMER_REAL, &timerval, NULL);

	/* And disable the signal handler. */
	action.sa_handler = SIG_IGN;
	sigaction (SIGALRM, &action, NULL);
    }

    return NOTMUCH_STATUS_SUCCESS;
}

/* We allow the user to use arbitrarily long paths for directories. But
 * we have a term-length limit. So if we exceed that, we'll use the
 * SHA-1 of the path for the database term.
 *
 * Note: This function may return the original value of 'path'. If it
 * does not, then the caller is responsible to free() the returned
 * value.
 */
const char *
_notmuch_database_get_directory_db_path (const char *path)
{
    int term_len = strlen (_find_prefix ("directory")) + strlen (path);

    if (term_len > NOTMUCH_TERM_MAX)
	return notmuch_sha1_of_string (path);
    else
	return path;
}

/* Given a path, split it into two parts: the directory part is all
 * components except for the last, and the basename is that last
 * component. Getting the return-value for either part is optional
 * (the caller can pass NULL).
 *
 * The original 'path' can represent either a regular file or a
 * directory---the splitting will be carried out in the same way in
 * either case. Trailing slashes on 'path' will be ignored, and any
 * cases of multiple '/' characters appearing in series will be
 * treated as a single '/'.
 *
 * Allocation (if any) will have 'ctx' as the talloc owner. But
 * pointers will be returned within the original path string whenever
 * possible.
 *
 * Note: If 'path' is non-empty and contains no non-trailing slash,
 * (that is, consists of a filename with no parent directory), then
 * the directory returned will be an empty string. However, if 'path'
 * is an empty string, then both directory and basename will be
 * returned as NULL.
 */
notmuch_status_t
_notmuch_database_split_path (void *ctx,
			      const char *path,
			      const char **directory,
			      const char **basename)
{
    const char *slash;

    if (path == NULL || *path == '\0') {
	if (directory)
	    *directory = NULL;
	if (basename)
	    *basename = NULL;
	return NOTMUCH_STATUS_SUCCESS;
    }

    /* Find the last slash (not counting a trailing slash), if any. */

    slash = path + strlen (path) - 1;

    /* First, skip trailing slashes. */
    while (slash != path) {
	if (*slash != '/')
	    break;

	--slash;
    }

    /* Then, find a slash. */
    while (slash != path) {
	if (*slash == '/')
	    break;

	if (basename)
	    *basename = slash;

	--slash;
    }

    /* Finally, skip multiple slashes. */
    while (slash != path) {
	if (*slash != '/')
	    break;

	--slash;
    }

    if (slash == path) {
	if (directory)
	    *directory = talloc_strdup (ctx, "");
	if (basename)
	    *basename = path;
    } else {
	if (directory)
	    *directory = talloc_strndup (ctx, path, slash - path + 1);
    }

    return NOTMUCH_STATUS_SUCCESS;
}

notmuch_status_t
_notmuch_database_find_directory_id (notmuch_database_t *notmuch,
				     const char *path,
				     unsigned int *directory_id)
{
    notmuch_directory_t *directory;
    notmuch_status_t status;

    if (path == NULL) {
	*directory_id = 0;
	return NOTMUCH_STATUS_SUCCESS;
    }

    directory = _notmuch_directory_create (notmuch, path, &status);
    if (status) {
	*directory_id = -1;
	return status;
    }

    *directory_id = _notmuch_directory_get_document_id (directory);

    notmuch_directory_destroy (directory);

    return NOTMUCH_STATUS_SUCCESS;
}

const char *
_notmuch_database_get_directory_path (void *ctx,
				      notmuch_database_t *notmuch,
				      unsigned int doc_id)
{
    Xapian::Document document;

    document = find_document_for_doc_id (notmuch, doc_id);

    return talloc_strdup (ctx, document.get_data ().c_str ());
}

/* Given a legal 'filename' for the database, (either relative to
 * database path or absolute with initial components identical to
 * database path), return a new string (with 'ctx' as the talloc
 * owner) suitable for use as a direntry term value.
 *
 * The necessary directory documents will be created in the database
 * as needed.
 */
notmuch_status_t
_notmuch_database_filename_to_direntry (void *ctx,
					notmuch_database_t *notmuch,
					const char *filename,
					char **direntry)
{
    const char *relative, *directory, *basename;
    Xapian::docid directory_id;
    notmuch_status_t status;

    relative = _notmuch_database_relative_path (notmuch, filename);

    status = _notmuch_database_split_path (ctx, relative,
					   &directory, &basename);
    if (status)
	return status;

    status = _notmuch_database_find_directory_id (notmuch, directory,
						  &directory_id);
    if (status)
	return status;

    *direntry = talloc_asprintf (ctx, "%u:%s", directory_id, basename);

    return NOTMUCH_STATUS_SUCCESS;
}

/* Given a legal 'path' for the database, return the relative path.
 *
 * The return value will be a pointer to the originl path contents,
 * and will be either the original string (if 'path' was relative) or
 * a portion of the string (if path was absolute and begins with the
 * database path).
 */
const char *
_notmuch_database_relative_path (notmuch_database_t *notmuch,
				 const char *path)
{
    const char *db_path, *relative;
    unsigned int db_path_len;

    db_path = notmuch_database_get_path (notmuch);
    db_path_len = strlen (db_path);

    relative = path;

    if (*relative == '/') {
	while (*relative == '/' && *(relative+1) == '/')
	    relative++;

	if (strncmp (relative, db_path, db_path_len) == 0)
	{
	    relative += db_path_len;
	    while (*relative == '/')
		relative++;
	}
    }

    return relative;
}

notmuch_directory_t *
notmuch_database_get_directory (notmuch_database_t *notmuch,
				const char *path)
{
    notmuch_status_t status;

    return _notmuch_directory_create (notmuch, path, &status);
}

/* Find the thread ID to which the message with 'message_id' belongs.
 *
 * Returns NULL if no message with message ID 'message_id' is in the
 * database.
 *
 * Otherwise, returns a newly talloced string belonging to 'ctx'.
 */
static const char *
_resolve_message_id_to_thread_id (notmuch_database_t *notmuch,
				  void *ctx,
				  const char *message_id)
{
    notmuch_message_t *message;
    const char *ret = NULL;

    message = notmuch_database_find_message (notmuch, message_id);
    if (message == NULL)
	goto DONE;

    ret = talloc_steal (ctx, notmuch_message_get_thread_id (message));

  DONE:
    if (message)
	notmuch_message_destroy (message);

    return ret;
}

static notmuch_status_t
_merge_threads (notmuch_database_t *notmuch,
		const char *winner_thread_id,
		const char *loser_thread_id)
{
    Xapian::PostingIterator loser, loser_end;
    notmuch_message_t *message = NULL;
    notmuch_private_status_t private_status;
    notmuch_status_t ret = NOTMUCH_STATUS_SUCCESS;

    find_doc_ids (notmuch, "thread", loser_thread_id, &loser, &loser_end);

    for ( ; loser != loser_end; loser++) {
	message = _notmuch_message_create (notmuch, notmuch,
					   *loser, &private_status);
	if (message == NULL) {
	    ret = COERCE_STATUS (private_status,
				 "Cannot find document for doc_id from query");
	    goto DONE;
	}

	_notmuch_message_remove_term (message, "thread", loser_thread_id);
	_notmuch_message_add_term (message, "thread", winner_thread_id);
	_notmuch_message_sync (message);

	notmuch_message_destroy (message);
	message = NULL;
    }

  DONE:
    if (message)
	notmuch_message_destroy (message);

    return ret;
}

static void
_my_talloc_free_for_g_hash (void *ptr)
{
    talloc_free (ptr);
}

static notmuch_status_t
_notmuch_database_link_message_to_parents (notmuch_database_t *notmuch,
					   notmuch_message_t *message,
					   notmuch_message_file_t *message_file,
					   const char **thread_id)
{
    GHashTable *parents = NULL;
    const char *refs, *in_reply_to, *in_reply_to_message_id;
    GList *l, *keys = NULL;
    notmuch_status_t ret = NOTMUCH_STATUS_SUCCESS;

    parents = g_hash_table_new_full (g_str_hash, g_str_equal,
				     _my_talloc_free_for_g_hash, NULL);

    refs = notmuch_message_file_get_header (message_file, "references");
    parse_references (message, notmuch_message_get_message_id (message),
		      parents, refs);

    in_reply_to = notmuch_message_file_get_header (message_file, "in-reply-to");
    parse_references (message, notmuch_message_get_message_id (message),
		      parents, in_reply_to);

    /* Carefully avoid adding any self-referential in-reply-to term. */
    in_reply_to_message_id = _parse_message_id (message, in_reply_to, NULL);
    if (in_reply_to_message_id &&
	strcmp (in_reply_to_message_id,
		notmuch_message_get_message_id (message)))
    {
	_notmuch_message_add_term (message, "replyto",
			     _parse_message_id (message, in_reply_to, NULL));
    }

    keys = g_hash_table_get_keys (parents);
    for (l = keys; l; l = l->next) {
	char *parent_message_id;
	const char *parent_thread_id;

	parent_message_id = (char *) l->data;
	parent_thread_id = _resolve_message_id_to_thread_id (notmuch,
							     message,
							     parent_message_id);

	if (parent_thread_id == NULL) {
	    _notmuch_message_add_term (message, "reference",
				       parent_message_id);
	} else {
	    if (*thread_id == NULL) {
		*thread_id = talloc_strdup (message, parent_thread_id);
		_notmuch_message_add_term (message, "thread", *thread_id);
	    } else if (strcmp (*thread_id, parent_thread_id)) {
		ret = _merge_threads (notmuch, *thread_id, parent_thread_id);
		if (ret)
		    goto DONE;
	    }
	}
    }

  DONE:
    if (keys)
	g_list_free (keys);
    if (parents)
	g_hash_table_unref (parents);

    return ret;
}

static notmuch_status_t
_notmuch_database_link_message_to_children (notmuch_database_t *notmuch,
					    notmuch_message_t *message,
					    const char **thread_id)
{
    const char *message_id = notmuch_message_get_message_id (message);
    Xapian::PostingIterator child, children_end;
    notmuch_message_t *child_message = NULL;
    const char *child_thread_id;
    notmuch_status_t ret = NOTMUCH_STATUS_SUCCESS;
    notmuch_private_status_t private_status;

    find_doc_ids (notmuch, "reference", message_id, &child, &children_end);

    for ( ; child != children_end; child++) {

	child_message = _notmuch_message_create (message, notmuch,
						 *child, &private_status);
	if (child_message == NULL) {
	    ret = COERCE_STATUS (private_status,
				 "Cannot find document for doc_id from query");
	    goto DONE;
	}

	child_thread_id = notmuch_message_get_thread_id (child_message);
	if (*thread_id == NULL) {
	    *thread_id = talloc_strdup (message, child_thread_id);
	    _notmuch_message_add_term (message, "thread", *thread_id);
	} else if (strcmp (*thread_id, child_thread_id)) {
	    _notmuch_message_remove_term (child_message, "reference",
					  message_id);
	    _notmuch_message_sync (child_message);
	    ret = _merge_threads (notmuch, *thread_id, child_thread_id);
	    if (ret)
		goto DONE;
	}

	notmuch_message_destroy (child_message);
	child_message = NULL;
    }

  DONE:
    if (child_message)
	notmuch_message_destroy (child_message);

    return ret;
}

static const char *
_notmuch_database_generate_thread_id (notmuch_database_t *notmuch)
{
    /* 16 bytes (+ terminator) for hexadecimal representation of
     * a 64-bit integer. */
    static char thread_id[17];
    Xapian::WritableDatabase *db;

    db = static_cast <Xapian::WritableDatabase *> (notmuch->xapian_db);

    notmuch->last_thread_id++;

    sprintf (thread_id, "%016" PRIx64, notmuch->last_thread_id);

    db->set_metadata ("last_thread_id", thread_id);

    return thread_id;
}

/* Given a (mostly empty) 'message' and its corresponding
 * 'message_file' link it to existing threads in the database.
 *
 * We first look at 'message_file' and its link-relevant headers
 * (References and In-Reply-To) for message IDs. We also look in the
 * database for existing message that reference 'message'. In either
 * case, we will assign to the current message the first thread_id
 * found (through either parent or child). We will also merge any
 * existing, distinct threads where this message belongs to both,
 * (which is not uncommon when mesages are processed out of order).
 *
 * Finally, if not thread ID has been found through parent or child,
 * we call _notmuch_message_generate_thread_id to generate a new
 * generates a new thread ID if the message doesn't connect to any
 * existing threads.
 */
static notmuch_status_t
_notmuch_database_link_message (notmuch_database_t *notmuch,
				notmuch_message_t *message,
				notmuch_message_file_t *message_file)
{
    notmuch_status_t status;
    const char *thread_id = NULL;

    status = _notmuch_database_link_message_to_parents (notmuch, message,
							message_file,
							&thread_id);
    if (status)
	return status;

    status = _notmuch_database_link_message_to_children (notmuch, message,
							 &thread_id);
    if (status)
	return status;

    /* If not part of any existing thread, generate a new thread ID. */
    if (thread_id == NULL) {
	thread_id = _notmuch_database_generate_thread_id (notmuch);

	_notmuch_message_add_term (message, "thread", thread_id);
    }

    return NOTMUCH_STATUS_SUCCESS;
}

notmuch_status_t
notmuch_database_add_message (notmuch_database_t *notmuch,
			      const char *filename,
			      notmuch_message_t **message_ret)
{
    notmuch_message_file_t *message_file;
    notmuch_message_t *message = NULL;
    notmuch_status_t ret = NOTMUCH_STATUS_SUCCESS;
    notmuch_private_status_t private_status;

    const char *date, *header;
    const char *from, *to, *subject;
    char *message_id = NULL;

    if (message_ret)
	*message_ret = NULL;

    ret = _notmuch_database_ensure_writable (notmuch);
    if (ret)
	return ret;

    message_file = notmuch_message_file_open (filename);
    if (message_file == NULL)
	return NOTMUCH_STATUS_FILE_ERROR;

    notmuch_message_file_restrict_headers (message_file,
					   "date",
					   "from",
					   "in-reply-to",
					   "message-id",
					   "references",
					   "subject",
					   "to",
					   (char *) NULL);

    try {
	/* Before we do any real work, (especially before doing a
	 * potential SHA-1 computation on the entire file's contents),
	 * let's make sure that what we're looking at looks like an
	 * actual email message.
	 */
	from = notmuch_message_file_get_header (message_file, "from");
	subject = notmuch_message_file_get_header (message_file, "subject");
	to = notmuch_message_file_get_header (message_file, "to");

	if ((from == NULL || *from == '\0') &&
	    (subject == NULL || *subject == '\0') &&
	    (to == NULL || *to == '\0'))
	{
	    ret = NOTMUCH_STATUS_FILE_NOT_EMAIL;
	    goto DONE;
	}

	/* Now that we're sure it's mail, the first order of business
	 * is to find a message ID (or else create one ourselves). */

	header = notmuch_message_file_get_header (message_file, "message-id");
	if (header && *header != '\0') {
	    message_id = _parse_message_id (message_file, header, NULL);

	    /* So the header value isn't RFC-compliant, but it's
	     * better than no message-id at all. */
	    if (message_id == NULL)
		message_id = talloc_strdup (message_file, header);

	    /* Reject a Message ID that's too long. */
	    if (message_id && strlen (message_id) + 1 > NOTMUCH_TERM_MAX) {
		talloc_free (message_id);
		message_id = NULL;
	    }
	}

	if (message_id == NULL ) {
	    /* No message-id at all, let's generate one by taking a
	     * hash over the file's contents. */
	    char *sha1 = notmuch_sha1_of_file (filename);

	    /* If that failed too, something is really wrong. Give up. */
	    if (sha1 == NULL) {
		ret = NOTMUCH_STATUS_FILE_ERROR;
		goto DONE;
	    }

	    message_id = talloc_asprintf (message_file,
					  "notmuch-sha1-%s", sha1);
	    free (sha1);
	}

	/* Now that we have a message ID, we get a message object,
	 * (which may or may not reference an existing document in the
	 * database). */

	message = _notmuch_message_create_for_message_id (notmuch,
							  message_id,
							  &private_status);

	talloc_free (message_id);

	if (message == NULL) {
	    ret = COERCE_STATUS (private_status,
				 "Unexpected status value from _notmuch_message_create_for_message_id");
	    goto DONE;
	}

	_notmuch_message_add_filename (message, filename);

	/* Is this a newly created message object? */
	if (private_status == NOTMUCH_PRIVATE_STATUS_NO_DOCUMENT_FOUND) {
	    _notmuch_message_add_term (message, "type", "mail");

	    ret = _notmuch_database_link_message (notmuch, message,
						  message_file);
	    if (ret)
		goto DONE;

	    date = notmuch_message_file_get_header (message_file, "date");
	    _notmuch_message_set_date (message, date);

	    _notmuch_message_index_file (message, filename);
	} else {
	    ret = NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID;
	}

	_notmuch_message_sync (message);
    } catch (const Xapian::Error &error) {
	fprintf (stderr, "A Xapian exception occurred adding message: %s.\n",
		 error.get_description().c_str());
	notmuch->exception_reported = TRUE;
	ret = NOTMUCH_STATUS_XAPIAN_EXCEPTION;
	goto DONE;
    }

  DONE:
    if (message) {
	if (ret == NOTMUCH_STATUS_SUCCESS && message_ret)
	    *message_ret = message;
	else
	    notmuch_message_destroy (message);
    }

    if (message_file)
	notmuch_message_file_close (message_file);

    return ret;
}

notmuch_status_t
notmuch_database_remove_message (notmuch_database_t *notmuch,
				 const char *filename)
{
    Xapian::WritableDatabase *db;
    void *local = talloc_new (notmuch);
    const char *prefix = _find_prefix ("file-direntry");
    char *direntry, *term;
    Xapian::PostingIterator i, end;
    Xapian::Document document;
    notmuch_status_t status;

    status = _notmuch_database_ensure_writable (notmuch);
    if (status)
	return status;

    db = static_cast <Xapian::WritableDatabase *> (notmuch->xapian_db);

    status = _notmuch_database_filename_to_direntry (local, notmuch,
						     filename, &direntry);
    if (status)
	return status;

    term = talloc_asprintf (notmuch, "%s%s", prefix, direntry);

    find_doc_ids_for_term (notmuch, term, &i, &end);

    for ( ; i != end; i++) {
	Xapian::TermIterator j;

	document = find_document_for_doc_id (notmuch, *i);

	document.remove_term (term);

	j = document.termlist_begin ();
	j.skip_to (prefix);

	/* Was this the last file-direntry in the message? */
	if (j == document.termlist_end () ||
	    strncmp ((*j).c_str (), prefix, strlen (prefix)))
	{
	    db->delete_document (document.get_docid ());
	    status = NOTMUCH_STATUS_SUCCESS;
	} else {
	    db->replace_document (document.get_docid (), document);
	    status = NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID;
	}
    }

    talloc_free (local);

    return status;
}

notmuch_tags_t *
_notmuch_convert_tags (void *ctx, Xapian::TermIterator &i,
		       Xapian::TermIterator &end)
{
    const char *prefix = _find_prefix ("tag");
    notmuch_tags_t *tags;
    std::string tag;

    /* Currently this iteration is written with the assumption that
     * "tag" has a single-character prefix. */
    assert (strlen (prefix) == 1);

    tags = _notmuch_tags_create (ctx);
    if (unlikely (tags == NULL))
	return NULL;

    i.skip_to (prefix);

    while (i != end) {
	tag = *i;

	if (tag.empty () || tag[0] != *prefix)
	    break;

	_notmuch_tags_add_tag (tags, tag.c_str () + 1);

	i++;
    }

    _notmuch_tags_prepare_iterator (tags);

    return tags;
}

notmuch_tags_t *
notmuch_database_get_all_tags (notmuch_database_t *db)
{
    Xapian::TermIterator i, end;
    i = db->xapian_db->allterms_begin();
    end = db->xapian_db->allterms_end();
    return _notmuch_convert_tags(db, i, end);
}
