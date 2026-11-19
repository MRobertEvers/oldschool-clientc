#ifndef SRC_REPLAY_REPLAY_VIEWER_H
#define SRC_REPLAY_REPLAY_VIEWER_H

/*
 * The RSProx replay viewer's launch and frame state (main.c's
 * main_rsprox_replay_* functions): --replay FILE (or TORIRS_RSPROX_REPLAY), an
 * RSProx recording this client plays as its server
 * (platform/net_transport_replay.c), the --replay-at seek list and the
 * --replay-digest stage machine. One instance, zeroed at process start.
 *
 * The command-bus replay (TORIRS_CMD_REPLAY) is a different feature and is not
 * here.
 */

#include "net/rsprox_recording.h"

#include <stdbool.h>

/* --replay-digest (or TORIRS_REPLAY_DIGEST=1): once the last --replay-at seek
 * has landed and the client has settled, print one `replay: digest` line of
 * the authoritative state (replay/replay_digest.h), play 64 more ticks, print
 * another, and exit. */
struct ReplayViewerDigest
{
    int on;
    /** 0 waiting for the seek, 1 settling at T, 2 playing to T + 64,
     *  3 settling there, 4 done. */
    int stage;
    int seek_issued;
    int tick;
    int settle;
    unsigned long long serial_at;
};

/* What a seek changes for as long as it runs, and puts back when it lands: the
 * replay as fast as the client can go (the transport reports itself unlocked,
 * k cycles a frame), nothing drawn (App_RenderSkipHold), nothing heard. */
struct ReplayViewerSeek
{
    int active;
    int cycles_per_frame;
    int master_volume;
    /** The audio device as it was: a seek runs with none, so nothing is
     *  fetched or decoded for a sound nobody hears (RS_Audio_Tick). */
    bool device_present;
    /** A seek landed last frame: its breakdown is printed once the frame
     *  that presents it has run. */
    int present_pending;
};

struct ReplayViewer
{
    /** The --replay file; NULL = not a replay boot. Opened at boot, so its
     *  header can choose the revision before the network is made; moved into
     *  the transport when that is. */
    char const* path;
    /** The opened recording. */
    struct RsproxRecording rec;
    /** `uncapped` as the flags left it, which an unlocked replay overrides and
     *  a relocked one restores. */
    int uncapped;
    /** --replay-at T[,T2,...] (or TORIRS_REPLAY_AT): once the login completes,
     *  seek to T and hold there; each later tick is sought, and held at, once
     *  the one before it has landed. The rest of the list. */
    char const* at;
    struct ReplayViewerDigest digest;
    struct ReplayViewerSeek seek;
};

#endif
