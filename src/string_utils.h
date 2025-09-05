#ifndef STRING_UTILS_H
#define STRING_UTILS_H

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
int strcasecmp(const char *s1, const char *s2);

/**
 * Case-insensitive string comparison with length limit
 * Returns:
 *   < 0 if s1 < s2
 *   0   if s1 == s2
 *   > 0 if s1 > s2
 */
int strncasecmp(const char *s1, const char *s2, size_t n);

#ifdef __cplusplus
}
#endif

#endif // STRING_UTILS_H
