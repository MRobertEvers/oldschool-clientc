#ifndef SRC_GAME_RS_SOUNDSCAPE_H
#define SRC_GAME_RS_SOUNDSCAPE_H

#include <stdbool.h>

/*
 * Ambient soundscapes: what an `AMBIENTSOUND_START` id actually names.
 *
 * The packet's id is not a sound-effect id. It indexes config group 15, whose
 * records describe a *bed*: some number of sounds looping continuously at once,
 * plus up to eight independent random sets, each firing one shot from its own
 * list on its own timer. A cave is a drone plus unrelated drips; playing the id
 * as a single looping effect gives neither.
 *
 * This is a game-side mirror of `RSCache_Dat2ConfigSoundscape`, loaded once at
 * boot by CreateTask_Dat2SoundscapeLoad, for the same reason RS_Hitsplats is:
 * the consumer is the audio tick, which has nowhere to yield to a cache read.
 * Eight records of a few dozen bytes -- the whole table is smaller than one
 * sound clip.
 *
 * ## Era
 *
 * Group 15 is an OldSchool 231+ addition; `cache.osrs230` has none. `count == 0`
 * is therefore normal and means "this cache has no soundscapes", which
 * RS_Audio_SetAmbient reads as "treat the id the old way -- as a sound effect".
 * That fallback is what keeps rev 230 working, so it is a supported path rather
 * than a safety net.
 */

#define RS_SOUNDSCAPE_MAX_LOOPS 8
#define RS_SOUNDSCAPE_MAX_SETS 8
#define RS_SOUNDSCAPE_MAX_SET_IDS 48

struct RS_SoundscapeSet
{
    int ids[RS_SOUNDSCAPE_MAX_SET_IDS];
    int id_count;
    /** Gap bounds in milliseconds, as the cache states them. */
    int min_ms;
    int max_ms;
};

struct RS_Soundscape
{
    bool present;
    int loop_ids[RS_SOUNDSCAPE_MAX_LOOPS];
    int loop_count;
    struct RS_SoundscapeSet sets[RS_SOUNDSCAPE_MAX_SETS];
    int set_count;
    int fade_in_ms;
    int fade_out_ms;
};

struct RS_Soundscapes
{
    struct RS_Soundscape* entries; /* [count], indexed by id; owned */
    int count;
};

void
RS_Soundscapes_Init(struct RS_Soundscapes* table);

void
RS_Soundscapes_Free(struct RS_Soundscapes* table);

/** Take ownership of `entries` (length `count`). Returns false on a bad call,
 *  in which case the caller still owns the array. */
bool
RS_Soundscapes_SetEntries(
    struct RS_Soundscapes* table,
    struct RS_Soundscape* entries,
    int count);

/** The record for `id`, or NULL when absent (including an empty table). */
const struct RS_Soundscape*
RS_Soundscapes_Get(
    const struct RS_Soundscapes* table,
    int id);

#endif
