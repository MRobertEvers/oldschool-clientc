#ifndef PLATTOOLS_H
#define PLATTOOLS_H

/*
 * Return an absolute, lexically normalised path for `path`.
 *
 * The target does not need to exist.  The returned UTF-8 string is allocated
 * with malloc and belongs to the caller; release it with free().  NULL is
 * returned for an invalid path or an allocation/platform error, with errno set.
 *
 * On Windows the result uses native separators and Windows path semantics.  On
 * Linux and macOS it uses POSIX path semantics.  A web build returns an
 * absolute path in Emscripten's virtual filesystem; browsers cannot reveal a
 * host-machine filesystem path.  Web builds also accept the storage-qualified
 * forms `idb://`, `localstorage://`, and `sessionstorage://`.  These are
 * normalized as rooted key paths and retain their storage qualifier.
 */
char*
plattools_full_path(const char* path);

#endif
