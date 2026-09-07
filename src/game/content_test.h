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
#endif
