#include "varc_manager.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define CHANGE_LOG_MAX 64

struct ChangeLog
{
    int ids[CHANGE_LOG_MAX];
    int is_string[CHANGE_LOG_MAX];
    int count;
};

static void
on_change(
    void* userdata,
    int varc_id,
    bool is_string)
{
    struct ChangeLog* log = userdata;
    if( log->count < CHANGE_LOG_MAX )
    {
        log->is_string[log->count] = is_string ? 1 : 0;
        log->ids[log->count++] = varc_id;
    }
}

static void
test_int_get_set_grow(void)
{
    printf("TEST: int get/set/grow\n");

    struct VarCManager mgr;
    VarCManager_Init(&mgr);

    /* RuneLite's Varcs map distinguishes an absent int (-1) from a stored 0. */
    TEST_ASSERT(VarCManager_GetInt(&mgr, 0) == -1, "unset reads -1");
    TEST_ASSERT(VarCManager_GetInt(&mgr, 5000) == -1, "unset high id reads -1");
    TEST_ASSERT(VarCManager_GetInt(&mgr, -1) == -1, "negative id reads -1");

    VarCManager_SetInt(&mgr, 0, 0);
    TEST_ASSERT(VarCManager_GetInt(&mgr, 0) == 0, "explicit zero remains set");

    VarCManager_SetInt(&mgr, 3, 42);
    TEST_ASSERT(VarCManager_GetInt(&mgr, 3) == 42, "set/get roundtrip");

    /* Grows on demand for a high id. */
    VarCManager_SetInt(&mgr, 1000, 7);
    TEST_ASSERT(VarCManager_GetInt(&mgr, 1000) == 7, "grow high id");
    TEST_ASSERT(VarCManager_GetInt(&mgr, 3) == 42, "prior value survives grow");

    VarCManager_SetInt(&mgr, -1, 99); /* ignored, no crash */

    VarCManager_Free(&mgr);
    VarCManager_Free(&mgr); /* double free safe */
}

static void
test_string_get_set(void)
{
    printf("TEST: string get/set\n");

    struct VarCManager mgr;
    VarCManager_Init(&mgr);

    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 0), "") == 0, "unset string is empty");
    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 335), "") == 0, "unset high string empty");

    VarCManager_SetString(&mgr, 335, "hello");
    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 335), "hello") == 0, "string roundtrip");

    VarCManager_SetString(&mgr, 335, "world");
    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 335), "world") == 0, "string overwrite");

    VarCManager_SetString(&mgr, 335, NULL); /* NULL treated as "" */
    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 335), "") == 0, "NULL clears");

    VarCManager_Free(&mgr);
}

static void
test_change_callback(void)
{
    printf("TEST: change callback fires only on real change\n");

    struct VarCManager mgr;
    struct ChangeLog log = { 0 };
    VarCManager_Init(&mgr);
    VarCManager_SetChangeCallback(&mgr, on_change, &log);

    VarCManager_SetInt(&mgr, 10, 1);
    TEST_ASSERT(log.count == 1, "first int write notifies");
    TEST_ASSERT(log.ids[0] == 10 && log.is_string[0] == 0, "int change id + kind");

    VarCManager_SetInt(&mgr, 10, 1); /* same value: no notify */
    TEST_ASSERT(log.count == 1, "unchanged int does not notify");

    VarCManager_SetInt(&mgr, 10, 2); /* real change */
    TEST_ASSERT(log.count == 2, "changed int notifies");

    VarCManager_SetString(&mgr, 20, "a");
    TEST_ASSERT(log.count == 3 && log.is_string[2] == 1, "string write notifies as string");

    VarCManager_SetString(&mgr, 20, "a"); /* same: no notify */
    TEST_ASSERT(log.count == 3, "unchanged string does not notify");

    VarCManager_Free(&mgr);
}

static void
test_reset_all(void)
{
    printf("TEST: reset clears and notifies\n");

    struct VarCManager mgr;
    struct ChangeLog log = { 0 };
    VarCManager_Init(&mgr);
    VarCManager_SetInt(&mgr, 1, 5);
    VarCManager_SetString(&mgr, 2, "x");
    VarCManager_SetChangeCallback(&mgr, on_change, &log);

    VarCManager_ResetAll(&mgr);
    TEST_ASSERT(VarCManager_GetInt(&mgr, 1) == -1, "int unset");
    TEST_ASSERT(strcmp(VarCManager_GetString(&mgr, 2), "") == 0, "string cleared");
    TEST_ASSERT(log.count == 2, "reset notifies each changed id");

    /* Reset again: nothing changed, so no further notifications. */
    log.count = 0;
    VarCManager_ResetAll(&mgr);
    TEST_ASSERT(log.count == 0, "reset of empty store is quiet");

    VarCManager_Free(&mgr);
}

int
main(void)
{
    test_int_get_set_grow();
    test_string_get_set();
    test_change_callback();
    test_reset_all();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
