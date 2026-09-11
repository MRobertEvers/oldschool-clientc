#include "plattools.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__);                         \
            failures++;                                                                           \
        }                                                                                          \
    } while( 0 )

int
main(void)
{
    char* normalized;
    char* expected;
    char* repeated;

    errno = 0;
    CHECK(plattools_full_path(NULL) == NULL && errno == EINVAL, "NULL is rejected");
    errno = 0;
    CHECK(plattools_full_path("") == NULL && errno == EINVAL, "an empty path is rejected");

    normalized = plattools_full_path("plattools_test_root/./child/../file.txt");
    expected = plattools_full_path("plattools_test_root/file.txt");
    CHECK(normalized != NULL, "a relative path is resolved");
    CHECK(expected != NULL, "a nonexistent output path is supported");
    if( normalized && expected )
        CHECK(strcmp(normalized, expected) == 0, "dot segments are normalized");

    repeated = normalized ? plattools_full_path(normalized) : NULL;
    CHECK(repeated != NULL, "an absolute path is accepted");
    if( normalized && repeated )
        CHECK(strcmp(normalized, repeated) == 0, "resolving an absolute path is idempotent");

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if( normalized )
        CHECK(strlen(normalized) >= 3 && normalized[1] == ':' && normalized[2] == '\\',
              "Windows returns a drive-qualified native path");
#else
    if( normalized )
        CHECK(normalized[0] == '/', "POSIX and web return an absolute virtual/native path");
#endif

#if defined(__EMSCRIPTEN__)
    {
        char* idb = plattools_full_path("IDB://cache/./archives/../main.dat");
        char* local = plattools_full_path("localstorage:///preferences/../volume");
        char* session = plattools_full_path("sessionstorage://../../login/token");

        CHECK(idb && strcmp(idb, "idb://cache/main.dat") == 0,
              "IndexedDB key paths are qualified and normalized");
        CHECK(local && strcmp(local, "localstorage://volume") == 0,
              "localStorage key paths are qualified and normalized");
        CHECK(session && strcmp(session, "sessionstorage://login/token") == 0,
              "sessionStorage key paths cannot escape their storage root");
        free(idb);
        free(local);
        free(session);
    }
#endif

    free(normalized);
    free(expected);
    free(repeated);

    if( failures )
    {
        fprintf(stderr, "%d plattools test(s) failed\n", failures);
        return 1;
    }

    puts("plattools: all tests passed");
    return 0;
}
