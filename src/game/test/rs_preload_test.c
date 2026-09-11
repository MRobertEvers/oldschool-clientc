/*
 * The loading screen's work list, against the profiles that ship.
 *
 * Read from the real INIs rather than from fixtures, because the things most
 * worth catching here are properties of the SHIPPED lists and not of the
 * parser: a percentage that goes backwards, a caption naming a [string:] that
 * does not exist, a weight table that does not add up. Every one of those
 * draws something wrong on a screen the player stares at for the whole boot,
 * and every one is invisible in a diff.
 */
#include "game/rs_login_replies.h"
#include "game/rs_preload.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static char const* const k_dat1_ini = "../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini";
static char const* const k_dat2_ini = "../revconfig/osrs239/osrs239_ui.ini";

/* Both shipped lists, checked for the properties that make a loading screen
 * read correctly rather than for their exact contents -- the contents are the
 * revision's business and change with it. */
static void
test_shipped_list(
    char const* path,
    char const* label,
    int expect_weight_total)
{
    struct RS_PreloadTable table;
    struct RS_LoginReplyTable strings;
    int last_order = -1;
    int last_percent = -1;
    int renders = 0;

    RS_Preload_Init(&table);
    RS_Preload_LoadSources(&table, path, NULL, NULL);
    RS_LoginReplies_Init(&strings);
    RS_LoginReplies_LoadSources(&strings, path, NULL, NULL);

    TEST_ASSERT(table.count > 0, label);

    for( int i = 0; i < table.count; i++ )
    {
        struct RS_PreloadStep const* step = RS_Preload_At(&table, i);

        TEST_ASSERT(step != NULL, "every index in range answers a step");
        if( !step )
            continue;

        /* Sorted, because the list is a sequence and the file is not obliged
         * to state it in order. */
        TEST_ASSERT(step->order >= last_order, "steps come out in `order`");
        last_order = step->order;

        /* The one that matters most. A step whose percentage is below the one
         * before it makes the bar jump backwards, which reads as the boot
         * having restarted -- and it is exactly what happens when two lanes
         * share a step name or when a client's own stages are numbered from
         * scratch after a fetch phase. */
        if( step->percent >= 0 )
        {
            TEST_ASSERT(step->percent >= last_percent, "the bar never moves backwards");
            TEST_ASSERT(step->percent <= 100, "and never past the end of the track");
            last_percent = step->percent;
        }

        /* A kind the client does not know is a typo, and a typo here silently
         * skips the step rather than loading something wrong -- quiet enough
         * to survive a long time. */
        TEST_ASSERT(step->kind != RS_PRELOAD_KIND_UNKNOWN, step->kind_name);

        /* A caption naming a string the profile never declares draws nothing,
         * so the bar sits wordless through that stretch. */
        if( step->say[0] )
            TEST_ASSERT(
                RS_LoginReplies_String(&strings, step->say) != NULL, step->say);

        if( step->render )
            renders++;
    }

    /* Some step has to ask for the screen, or the whole list runs inside one
     * frame and none of it is ever seen. */
    TEST_ASSERT(renders > 0, "at least one step opts into being drawn");

    /* The modern lane's bar is a weighted sum and the deob's weights total
     * 100; the 2004 lane states none, and 0 is the right answer there rather
     * than a failure. */
    TEST_ASSERT(RS_Preload_TotalWeight(&table) == expect_weight_total, "weights total");

    RS_LoginReplies_Free(&strings);
    RS_Preload_Free(&table);
}

/* A profile that declares no list gets an empty one, not a default one. */
static void
test_absent_profile(void)
{
    struct RS_PreloadTable table;

    RS_Preload_Init(&table);
    RS_Preload_LoadSources(&table, NULL, NULL, NULL);
    TEST_ASSERT(table.count == 0, "an undeclared list is empty");
    TEST_ASSERT(RS_Preload_At(&table, 0) == NULL, "and answers nothing");
    TEST_ASSERT(RS_Preload_TotalWeight(&table) == 0, "and weighs nothing");
    RS_Preload_Free(&table);
}

/*
 * Loading the same source twice restates its steps rather than doubling them.
 *
 * This is what lets a manifest override one step of its revision's list: the
 * three sources are read in order and a later one wins by name. Doubling
 * instead would run every fetch twice.
 */
static void
test_a_later_source_restates(void)
{
    struct RS_PreloadTable once;
    struct RS_PreloadTable twice;

    RS_Preload_Init(&once);
    RS_Preload_LoadSources(&once, k_dat1_ini, NULL, NULL);

    RS_Preload_Init(&twice);
    RS_Preload_LoadSources(&twice, k_dat1_ini, k_dat1_ini, NULL);

    TEST_ASSERT(twice.count == once.count, "the same source twice is the same list");

    RS_Preload_Free(&once);
    RS_Preload_Free(&twice);
}

/* The kinds the two lanes actually dispatch. A profile whose steps all fall to
 * `prepare` announces a boot it never performs. */
static void
test_each_lane_has_work_to_do(void)
{
    struct RS_PreloadTable dat1;
    struct RS_PreloadTable dat2;
    int jagfiles = 0;
    int indices = 0;

    RS_Preload_Init(&dat1);
    RS_Preload_LoadSources(&dat1, k_dat1_ini, NULL, NULL);
    for( int i = 0; i < dat1.count; i++ )
    {
        if( dat1.steps[i].kind == RS_PRELOAD_KIND_JAGFILE )
            jagfiles++;
        TEST_ASSERT(
            dat1.steps[i].kind != RS_PRELOAD_KIND_INDEX,
            "the 2004 lane names no cache indices");
    }
    TEST_ASSERT(jagfiles > 0, "the 2004 lane names jag archives");

    RS_Preload_Init(&dat2);
    RS_Preload_LoadSources(&dat2, k_dat2_ini, NULL, NULL);
    for( int i = 0; i < dat2.count; i++ )
    {
        if( dat2.steps[i].kind == RS_PRELOAD_KIND_INDEX )
            indices++;
        TEST_ASSERT(
            dat2.steps[i].kind != RS_PRELOAD_KIND_JAGFILE,
            "the modern lane names no jag archives");
    }
    TEST_ASSERT(indices > 0, "the modern lane names cache indices");

    RS_Preload_Free(&dat1);
    RS_Preload_Free(&dat2);
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    /* The deob's weights are stated to total 100; Client-TS states none. */
    test_shipped_list(k_dat1_ini, "the 2004 profile declares a preload list", 0);
    test_shipped_list(k_dat2_ini, "the modern profile declares a preload list", 100);
    test_absent_profile();
    test_a_later_source_restates();
    test_each_lane_has_work_to_do();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("rs_preload_test: ok (%d checks)\n", g_checks);
    return 0;
}
