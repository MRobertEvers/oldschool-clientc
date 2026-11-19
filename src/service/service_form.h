#ifndef SRC_SERVICE_SERVICE_FORM_H
#define SRC_SERVICE_SERVICE_FORM_H

/*
 * What the servers say to each other is HTTP with form-encoded bodies in and
 * lines of tab-separated fields out, the first field naming the line --
 * readable with curl, parsed with a split. A text field never holds a tab or
 * a newline: the writer turns any into a space.
 */

#include <stddef.h>

/** `text`, with tabs and newlines made spaces, into `out`. */
void
ServiceForm_Clean(
    char const* text,
    char* out,
    size_t cap);

/** Split `line` in place at tabs into at most `max` fields. The count. */
int
ServiceForm_Fields(
    char* line,
    char** fields,
    int max);

/** `&name=value` (or `name=value` first) onto `out`, form-encoded. */
void
ServiceForm_Add(
    char* out,
    size_t cap,
    char const* name,
    char const* value);

/** `name`'s value in the form-encoded `data` (`len` bytes), decoded into
 *  `out`. 1 when present and it fits. */
int
ServiceForm_Get(
    char const* data,
    size_t len,
    char const* name,
    char* out,
    size_t cap);

/** Split `text` in place into lines (the newline dropped). The count. */
int
ServiceForm_Lines(
    char* text,
    char** lines,
    int max);

#endif
