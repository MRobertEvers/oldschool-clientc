#ifndef REVCONFIG_TEST_HARNESS_H
#define REVCONFIG_TEST_HARNESS_H

#include "revconfig.h"
#include "revconfig_load.h"

#include <stdio.h>

extern int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

void test_buffers(void);
void test_items_build(void);
void test_load(void);
void test_parse(void);
void test_parse_numbers(void);
void test_refs(void);
void test_profile(void);

#endif
