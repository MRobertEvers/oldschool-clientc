#ifndef CONTENT_TEST_SAILING_H
#define CONTENT_TEST_SAILING_H

#include <stddef.h>

struct App;
struct ToriRSServerEmbed;

/** Real second server player; mutations still publish through normal packets. */
int ContentTestSailing_PeerCommand(struct ToriRSServerEmbed*, const char* command,
                                  int* changed, char* error, size_t error_size);
void ContentTestSailing_PeerState(struct App*, struct ToriRSServerEmbed*, char* json, size_t json_size);

/** Run a named content proc in the PRIMARY player's context; the mirror of
 * "peer proc". `command` is the bare name followed by up to four int args. */
int ContentTestSailing_PrimaryProc(struct ToriRSServerEmbed*, const char* command,
                                  char* error, size_t error_size);

/* Bounded, in-memory movement/facilities test checkpoints. These preserve
 * vessel identity and publish changes through the existing protocol; they are
 * not a VM, scene, encounter, or network rollback. Call only after the harness
 * has drained input and async work. Restore then needs publish-only server
 * output; SettleRestore then snaps the received interpolation targets. */
int ContentTestSailing_Save(struct App*, struct ToriRSServerEmbed*,
                           const char* name, char* error, size_t error_size);
int ContentTestSailing_Restore(struct App*, struct ToriRSServerEmbed*,
                              const char* name, char* error, size_t error_size);
void ContentTestSailing_State(struct App*, struct ToriRSServerEmbed*,
                             char* json, size_t json_size);
/** True only after the actual network targets match the saved root poses. */
int ContentTestSailing_RestoreDelivered(struct App*);
void ContentTestSailing_SettleRestore(struct App*);
void ContentTestSailing_Clear(void);

#endif
