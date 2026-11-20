#ifndef WORLD_ENTITY_FREEZE_H
#define WORLD_ENTITY_FREEZE_H

/*
 * An actor's FREEZE windows: NPC_INFO 0x20 / PLAYER_INFO 0x80000 at rev 239
 * (`freeze`), as the official client honours them. Ported from the rev-239
 * deob only:
 *   write    the decode (Statics ~47731, class95 ~650): a delay and a
 *            duration (65535 each is none) and a reset byte. Both set:
 *            class105.method3557 adds the window [cycle + delay,
 *            cycle + delay + duration), reusing one that is over. Either
 *            none: method3627 drops every window and the snapshots, and the
 *            reset byte stops the sequence now (method3498).
 *   read     Statics.method7852, the actor's animation step, once a cycle:
 *            a window whose start was crossed since it was last looked at
 *            starts the freeze -- the sequence (when active and past its
 *            delay) and the pose are snapshotted, unless already frozen --
 *            and its reset byte stops the live sequence. While any window
 *            covers the cycle the pose and the sequence do not step (the
 *            sequence's start delay still counts down; attached graphics
 *            keep playing), the actor does not turn (Statics.method1), and
 *            what is DRAWN is the snapshot (class105.method3528 /
 *            Statics.method3603), whatever the live tracks are given. When
 *            none does, the snapshots go and the live tracks resume.
 * The windows are as many as the server keeps open (a list in the
 * reference): grown explicitly on ToriMem_Scene, owned by the row, freed
 * with it.
 */

#include <stdbool.h>

#include "entity_facets.h"

struct WorldFreezeWindow
{
    int start_cycle;
    int end_cycle;
    /* The cycle the step last looked at it (class431.field5366). */
    int seen_cycle;
    bool reset_seq;
};

struct WorldEntityFreeze
{
    struct WorldFreezeWindow* windows;
    int count;
    int cap;
    /* A window covers the cycle the step last ran (class105.field1479). */
    bool frozen;
    /* What is drawn while frozen: the tracks as the freeze began, the
     * primary cleared when it was not playing then (field1510 null). */
    struct WorldEntityFacet_Animation drawn;
};

#endif
