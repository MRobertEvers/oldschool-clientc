#ifndef SRC_AUDIO_TORIRS_SOUNDBANK_H
#define SRC_AUDIO_TORIRS_SOUNDBANK_H

#include "audio/torirs_pcm.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_MusicPatch;

/*
 * The soundfont the music synth plays: instrument patches and the samples they
 * reference.
 *
 * A patch (cache index 15) says, for each of 128 notes, which sample to play
 * and how. The samples come from two different tables -- index 14 (Vorbis music
 * samples) and index 4 (the sound-effect table, whose synth programs double as
 * percussion) -- so a sample is keyed by *table and id*, not id alone. Getting
 * that wrong is silent: id 12 exists in both and one of them is a drum.
 *
 * ## Lifetime
 *
 * The bank owns everything it holds. Samples are reference-counted by the
 * patches that resolved against them, because two patches routinely share one
 * sample and dropping a patch must not pull the sample out from under the
 * other. `ToriRS_SoundBank_Retain`/`Release` bracket a song: a song retains
 * every patch it names, and releasing the song drops the patches, which drops
 * the samples nothing else holds.
 *
 * This is deliberately *not* an LRU. A song's sample set is known before it
 * plays (the track carries its own instrument manifest) and is a few megabytes;
 * evicting from underneath a sounding note would be a use-after-free, so the
 * rule is "held for as long as a song references it, freed the moment none
 * does".
 */

#define TORIRS_SOUNDBANK_TABLE_EFFECTS 0
#define TORIRS_SOUNDBANK_TABLE_MUSIC 1

struct ToriRS_SoundBankSample
{
    int table;
    int id;
    int ref_count;
    struct ToriRS_PcmSound sound;
    int16_t* samples; /* owned */
};

struct ToriRS_SoundBankPatch
{
    int patch_id;
    int ref_count;
    struct RSCache_MusicPatch* patch; /* owned */
    /**
     * Resolved per note. Indices into `bank->samples`, or -1. Resolved once
     * when the patch's samples land rather than per note-on: a note-on is on
     * the audio path and a linear search per note would show up.
     */
    int note_sample[128];
    /** True once every note that wanted a sample either got one or was proven
     *  absent, so the synth can stop asking. */
    bool resolved;
};

struct ToriRS_SoundBank
{
    struct ToriRS_SoundBankPatch* patches;
    int patch_count;
    int patch_capacity;

    struct ToriRS_SoundBankSample* samples;
    int sample_count;
    int sample_capacity;

    /** Bytes of PCM currently held, for the memory readout. */
    long sample_bytes;
};

void
ToriRS_SoundBank_Init(struct ToriRS_SoundBank* bank);

/** Free every patch and sample. */
void
ToriRS_SoundBank_Free(struct ToriRS_SoundBank* bank);

/** Look up a patch, or NULL. */
struct ToriRS_SoundBankPatch*
ToriRS_SoundBank_FindPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id);

/**
 * Install a decoded patch, taking ownership of `patch`.
 *
 * Replacing an existing entry frees the old one. Returns the slot, or NULL if
 * it could not be stored (in which case `patch` is freed, so the caller never
 * has to decide).
 */
struct ToriRS_SoundBankPatch*
ToriRS_SoundBank_AddPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id,
    struct RSCache_MusicPatch* patch);

struct ToriRS_SoundBankSample*
ToriRS_SoundBank_FindSample(
    struct ToriRS_SoundBank* bank,
    int table,
    int id);

/**
 * Install decoded PCM, taking ownership of `samples`.
 *
 * `loop`/`ping_pong` come from the sample's own header; the patch decides
 * whether a given note loops at all.
 */
struct ToriRS_SoundBankSample*
ToriRS_SoundBank_AddSample(
    struct ToriRS_SoundBank* bank,
    int table,
    int id,
    int16_t* samples,
    int sample_count,
    int sample_rate,
    int loop_start,
    int loop_end,
    bool ping_pong);

/**
 * Bind each of a patch's notes to a resident sample.
 *
 * Returns the number of notes still waiting for a sample that has not arrived,
 * so the caller knows whether to keep loading. Safe to call repeatedly.
 */
int
ToriRS_SoundBank_ResolvePatch(
    struct ToriRS_SoundBank* bank,
    struct ToriRS_SoundBankPatch* patch);

/** The sound a note plays, or NULL when the note is silent or unresolved. */
const struct ToriRS_PcmSound*
ToriRS_SoundBank_NoteSound(
    struct ToriRS_SoundBank* bank,
    const struct ToriRS_SoundBankPatch* patch,
    int note);

/** Hold a patch (and, transitively, its samples) for as long as a song needs. */
void
ToriRS_SoundBank_RetainPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id);

/** Drop a hold. Frees the patch and any sample nothing else holds. */
void
ToriRS_SoundBank_ReleasePatch(
    struct ToriRS_SoundBank* bank,
    int patch_id);

#endif
