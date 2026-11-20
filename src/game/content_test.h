#ifndef CONTENT_TEST_H
#define CONTENT_TEST_H
#include <stdint.h>
struct App;
struct NetTransport;
struct ToriRS_CmdBus;
/* Opt-in, local file mailbox for a persistent content acceptance session. */
int ContentTest_Enabled(void);
int ContentTest_DrawRequested(struct App*);
uint64_t ContentTest_Begin(struct App*, struct NetTransport*, struct ToriRS_CmdBus*, uint64_t real_now);
void ContentTest_End(struct App*, struct NetTransport*);
/* TORIRS_CONTENT_TEST_HOLD / _RACE: this frame's virtual clock is held, and
 * the quest driver reads and acts only on frames it steps, so a slow read
 * shows it nothing a fast one would not (content_test.c). 0 by default. */
int ContentTest_DriverHeld(void);
/* TORIRS_CONTENT_TEST_BACKGROUND=1 with TORIRS_CONTENT_TEST_RACE=1: the
 * world's own rebuild model (background) runs under the test, so a race run
 * meets what a player's client meets while a REBUILD gathers. 0 otherwise:
 * a content test blocks on every rebuild (app_world_job.c). */
int ContentTest_BackgroundRebuild(void);
#endif
