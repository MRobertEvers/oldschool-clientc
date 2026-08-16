#ifndef RSCACHE_RSCACHESHARED_STRINGUTILS_H
#define RSCACHE_RSCACHESHARED_STRINGUTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * Case-insensitive string comparison
 * Returns:
 *   < 0 if s1 < s2
 *   0   if s1 == s2
 *   > 0 if s1 > s2
 */
#if !defined(_WIN32)
int
strcasecmp(
    const char* s1,
    const char* s2);

/**
 * Case-insensitive string comparison with length limit
 * Returns:
 *   < 0 if s1 < s2
 *   0   if s1 == s2
 *   > 0 if s1 > s2
 */
int
strncasecmp(
    const char* s1,
    const char* s2,
    size_t n);
#endif

void
RSCacheShared_StrAsciiToupper(
    char* str,
    int len);

#define TRIM_CHARS_WHITESPACE " \t\r\n"

void
RSCacheShared_StrncpyTrimmed(
    char* dest,
    const char* src,
    size_t n,
    char const* trim_chars);

#ifdef __cplusplus
}
#endif

#endif // STRING_UTILS_H
