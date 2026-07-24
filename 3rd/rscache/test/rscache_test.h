#ifndef RSCACHE_TEST_H
#define RSCACHE_TEST_H

/*
 * Minimal assertion harness shared by the rscache tests.
 *
 * Deliberately tiny and dependency-free: these tests build against the same
 * unity translation unit the client links, so anything they pull in would also
 * land in the client's link set.
 *
 * Counts failures instead of aborting on the first one, so a single run reports
 * every broken codec rather than just the earliest.
 */

#include <stdio.h>
#include <string.h>

static int rscache_test_checks = 0;
static int rscache_test_failures = 0;
static const char* rscache_test_group = "";

#define RSCACHE_TEST_GROUP(name)                                                                   \
    do                                                                                             \
    {                                                                                              \
        rscache_test_group = (name);                                                                \
        printf("-- %s\n", (name));                                                                  \
    } while( 0 )

#define RSCACHE_CHECK(cond)                                                                        \
    do                                                                                             \
    {                                                                                              \
        rscache_test_checks++;                                                                     \
        if( !(cond) )                                                                              \
        {                                                                                          \
            rscache_test_failures++;                                                               \
            printf("   FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, rscache_test_group, #cond);      \
        }                                                                                          \
    } while( 0 )

/* Equality of two integers, printing both sides so a mismatch is diagnosable
 * without a debugger. */
#define RSCACHE_CHECK_EQ(actual, expected)                                                         \
    do                                                                                             \
    {                                                                                              \
        long long rscache_a = (long long)(actual);                                                 \
        long long rscache_e = (long long)(expected);                                               \
        rscache_test_checks++;                                                                     \
        if( rscache_a != rscache_e )                                                               \
        {                                                                                          \
            rscache_test_failures++;                                                               \
            printf(                                                                                \
                "   FAIL %s:%d [%s] %s == %s (got %lld, want %lld)\n",                             \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                rscache_test_group,                                                                \
                #actual,                                                                           \
                #expected,                                                                          \
                rscache_a,                                                                          \
                rscache_e);                                                                         \
        }                                                                                          \
    } while( 0 )

#define RSCACHE_CHECK_STR_EQ(actual, expected)                                                     \
    do                                                                                             \
    {                                                                                              \
        const char* rscache_a = (actual);                                                          \
        const char* rscache_e = (expected);                                                        \
        rscache_test_checks++;                                                                     \
        if( !rscache_a || !rscache_e || strcmp(rscache_a, rscache_e) != 0 )                        \
        {                                                                                          \
            rscache_test_failures++;                                                               \
            printf(                                                                                \
                "   FAIL %s:%d [%s] \"%s\" != \"%s\"\n",                                           \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                rscache_test_group,                                                                \
                rscache_a ? rscache_a : "(null)",                                                  \
                rscache_e ? rscache_e : "(null)");                                                 \
        }                                                                                          \
    } while( 0 )

#define RSCACHE_CHECK_BYTES_EQ(actual, expected, len)                                              \
    do                                                                                             \
    {                                                                                              \
        const unsigned char* rscache_a = (const unsigned char*)(actual);                           \
        const unsigned char* rscache_e = (const unsigned char*)(expected);                         \
        int rscache_n = (int)(len);                                                                \
        rscache_test_checks++;                                                                     \
        int rscache_bad = -1;                                                                      \
        for( int rscache_i = 0; rscache_i < rscache_n; rscache_i++ )                               \
        {                                                                                          \
            if( rscache_a[rscache_i] != rscache_e[rscache_i] )                                     \
            {                                                                                      \
                rscache_bad = rscache_i;                                                            \
                break;                                                                              \
            }                                                                                       \
        }                                                                                           \
        if( rscache_bad >= 0 )                                                                     \
        {                                                                                           \
            rscache_test_failures++;                                                                \
            printf(                                                                                 \
                "   FAIL %s:%d [%s] bytes differ at %d (got 0x%02x, want 0x%02x)\n",               \
                __FILE__,                                                                           \
                __LINE__,                                                                           \
                rscache_test_group,                                                                 \
                rscache_bad,                                                                        \
                rscache_a[rscache_bad],                                                             \
                rscache_e[rscache_bad]);                                                            \
        }                                                                                           \
    } while( 0 )

static inline int
rscache_test_report(const char* suite)
{
    if( rscache_test_failures == 0 )
    {
        printf("%s: %d checks passed\n", suite, rscache_test_checks);
        return 0;
    }
    printf("%s: %d/%d checks FAILED\n", suite, rscache_test_failures, rscache_test_checks);
    return 1;
}

#endif
