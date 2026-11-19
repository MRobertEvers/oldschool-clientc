#include "game/rs_mouseover_report.h"

#include <assert.h>

void
RS_MouseoverReport_Reset(struct RS_MouseoverReport* report)
{
    assert(report);
    report->last_signature = RS_MOUSEOVER_REPORT_UNSEEN;
}

bool
RS_MouseoverReport_Changed(
    struct RS_MouseoverReport* report,
    int signature)
{
    assert(report);
    if( signature == report->last_signature )
        return false;
    report->last_signature = signature;
    return true;
}
