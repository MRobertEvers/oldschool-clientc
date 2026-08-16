#ifndef RSCACHE_DATATYPES_MUSIC_PATCH_H
#define RSCACHE_DATATYPES_MUSIC_PATCH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Music patches (cache index 15) — one instrument of the game's soundfont.
 *
 * A patch describes all 128 MIDI notes of one program: which sample each note
 * plays, at what pitch offset, volume and pan, and which amplitude/release
 * envelope shapes it. The archive id is the *banked* program number the song
 * asks for: `(bankMSB << 14) | (bankLSB << 7) | program`, so channel 10's
 * percussion (bank 1) lands at 128 + program.
 *
 * ## Why the decoder looks the way it does
 *
 * Nothing is stored per note. The record is five interleaved run-length streams
 * plus delta coding, and each of the 128 notes is filled by advancing counters
 * across them:
 *
 *   - three NUL-terminated byte runs at the front, each a *count* stream paired
 *     with a *value* stream that sits immediately after it,
 *   - a "note tree" that says which of N distinct envelope sets each run uses,
 *     coded as a move-to-front-ish index,
 *   - a fourth count stream shared by the sample-id and volume passes, read
 *     twice with independent cursors,
 *   - optional volume and pan *ramps* applied over note ranges at the end.
 *
 * Mis-stepping any counter still decodes and still plays — just the wrong
 * instrument on the wrong notes — so `_consumed` is exported and the test
 * asserts it equals the record size for every patch in the cache. That is the
 * only cheap check that the walk stayed in step.
 */

/** Per-note envelope shape shared by a run of notes (reference class334). */
struct RSCache_MusicPatchEnvelope
{
    /** Amplitude envelope as (time, level) byte pairs; NULL when absent. */
    uint8_t* amplitude;
    int amplitude_len;
    /** Release envelope as (time, level) byte pairs, two longer than declared
     *  because the reference reserves a leading pair (level 64). */
    uint8_t* release;
    int release_len;

    int decay;          /* exponential decay applied while the note sounds */
    int amplitude_rate; /* how fast the amplitude envelope advances */
    int release_rate;   /* ...and the release envelope */
    int decay_rate;     /* pitch-dependent scaling of `decay` */

    int vibrato_speed; /* LFO rate; 0 disables vibrato entirely */
    int vibrato_depth;
    int vibrato_delay;
};

struct RSCache_MusicPatch
{
    /** Master volume for the patch, 1..256 (reference field3690). */
    int volume_scale;

    /**
     * Per-note sample reference, as stored: `ref - 1` splits into
     * `(ref-1) >> 2` = sample id, bit 0 = which table (0 = sound effects
     * index 4, 1 = music samples index 14), bit 1 = the sample loops.
     * Zero means the note is silent.
     */
    int sample_ref[128];

    /** Per-note pitch offset in 1/256 semitones; bit 15 mirrors the loop flag,
     *  which is why the synth masks with 0x7FFF before using it. */
    int16_t pitch_offset[128];
    /**
     * Per-note volume, **signed**.
     *
     * The wire carries 0..255 and the reference stores it in a Java `byte`,
     * then does its ramp arithmetic and its per-note gain in signed byte
     * arithmetic. A value above 127 therefore becomes negative and inverts the
     * note's phase rather than making it loud. That is a reference quirk, not a
     * decode bug, and matching it is the point -- widening to unsigned here
     * would make a handful of notes twice as loud as the game plays them.
     */
    int8_t volume[128];
    /** Per-note pan 0..128 (64 centre). Stored signed by the reference but
     *  always read back with `& 255`, so unsigned here is equivalent. */
    uint8_t pan[128];
    /** Index into `envelopes`, or -1 for a silent note. */
    int envelope_index[128];
    /** Exclusive ("key") group: starting a note kills the group's previous
     *  note. -1 when the note belongs to no group. */
    int8_t note_group[128];

    struct RSCache_MusicPatchEnvelope* envelopes;
    int envelope_count;

    /** Bytes the decode consumed. Compare against the record size. */
    int _consumed;
};

struct RSCache_MusicPatch*
RSCache_MusicPatchNewDecode(
    const char* data,
    int data_size);

void
RSCache_MusicPatchFree(struct RSCache_MusicPatch* patch);

/** Sample id a note references, or -1 when the note is silent. */
static inline int
RSCache_MusicPatchNoteSampleId(
    const struct RSCache_MusicPatch* patch,
    int note)
{
    if( !patch || note < 0 || note >= 128 || patch->sample_ref[note] == 0 )
        return -1;
    return (patch->sample_ref[note] - 1) >> 2;
}

/** True when a note's sample lives in the music-samples table (index 14)
 *  rather than the sound-effects table (index 4). */
static inline bool
RSCache_MusicPatchNoteIsMusicSample(
    const struct RSCache_MusicPatch* patch,
    int note)
{
    if( !patch || note < 0 || note >= 128 || patch->sample_ref[note] == 0 )
        return false;
    return ((patch->sample_ref[note] - 1) & 1) != 0;
}

#endif
