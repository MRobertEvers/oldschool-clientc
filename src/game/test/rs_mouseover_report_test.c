/*
 * When the debug mouseover report line is printed again.
 *
 * The first ask after a reset reports, "no subject" (-1) included. After that
 * a signature reports only when it differs from the one last asked about.
 *
 *   make -C src test-rs-mouseover-report
 */
#include "game/rs_mouseover_report.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_first_ask_after_reset_reports_even_no_subject(void)
{
    struct RS_MouseoverReport report;

    memset(&report, 0, sizeof(report));
    RS_MouseoverReport_Reset(&report);
    CHECK(report.last_signature == RS_MOUSEOVER_REPORT_UNSEEN, "reset did not seed -2");
    CHECK(RS_MouseoverReport_Changed(&report, -1), "no subject was not reported once");
    CHECK(!RS_MouseoverReport_Changed(&report, -1), "no subject was reported twice");

    /* 0 is a signature like any other; a zeroed latch that was never reset
     * would have swallowed it. */
    RS_MouseoverReport_Reset(&report);
    CHECK(RS_MouseoverReport_Changed(&report, 0), "signature 0 was not reported after reset");
}

static void
test_only_a_change_reports(void)
{
    struct RS_MouseoverReport report;

    RS_MouseoverReport_Reset(&report);
    CHECK(RS_MouseoverReport_Changed(&report, 4096), "the first subject was not reported");
    CHECK(!RS_MouseoverReport_Changed(&report, 4096), "a held subject was reported again");
    CHECK(RS_MouseoverReport_Changed(&report, 8192), "a new subject was not reported");
    /* Only the last one is remembered: going back is a change. */
    CHECK(RS_MouseoverReport_Changed(&report, 4096), "returning to a subject was not reported");
}

int
main(void)
{
    test_first_ask_after_reset_reports_even_no_subject();
    test_only_a_change_reports();

    if( g_failures )
    {
        printf("rs_mouseover_report_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_mouseover_report_test: OK\n");
    return 0;
}
