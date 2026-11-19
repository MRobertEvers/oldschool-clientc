#ifndef SRC_GAME_RS_MOUSEOVER_REPORT_H
#define SRC_GAME_RS_MOUSEOVER_REPORT_H

/*
 * When the debug mouseover report line is printed again.
 *
 * The mouseover subject is rebuilt from the minimenu every frame
 * (rs_minimenu_build.h), and the debug report about it would print the same
 * line every frame. The caller folds the subject to one int signature; this
 * keeps the signature last reported and answers whether the one seen now
 * differs. Asking remembers the new signature whether or not the caller then
 * prints.
 *
 * Reset seeds it to RS_MOUSEOVER_REPORT_UNSEEN, -2. -1 is the signature of
 * "no subject", which is reported once like any other, so the first ask after
 * a reset reports whatever is under the pointer.
 *
 * This does not know what a signature is made of, whether the debug report is
 * switched on, or what the line says. The caller computes the signature, asks
 * only when the report is on, and prints.
 */

#include <stdbool.h>

/** The seed: a value no signature is expected to take. */
#define RS_MOUSEOVER_REPORT_UNSEEN (-2)

struct RS_MouseoverReport
{
    int last_signature;
};

/** Forget the last signature, so the next ask reports. */
void
RS_MouseoverReport_Reset(struct RS_MouseoverReport* report);

/** True when `signature` is not the one last seen; remembers it. */
bool
RS_MouseoverReport_Changed(
    struct RS_MouseoverReport* report,
    int signature);

#endif /* SRC_GAME_RS_MOUSEOVER_REPORT_H */
