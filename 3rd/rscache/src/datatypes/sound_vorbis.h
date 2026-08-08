#ifndef RSCACHE_DATATYPES_SOUND_VORBIS_H
#define RSCACHE_DATATYPES_SOUND_VORBIS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Vorbis-compressed audio samples — how modern OldSchool stores recorded sound.
 *
 * Two tables carry them and they are packaged differently:
 *
 *   music samples (index 14) — archive 0 is *the* setup header for the whole
 *       cache: blocksizes, codebooks, floors, residues, mappings and modes,
 *       shared by every other archive. Archives 1..N are a 20-byte sample
 *       header followed by length-laced audio packets and nothing else. A
 *       decoder that expects each archive to be a self-contained Vorbis stream
 *       finds no codebooks and produces silence.
 *
 *   sound effects (index 4) — when an effect archive has a second group file,
 *       that file *is* self-contained: `u4 setupLength`, a setup header, then
 *       the same sample header and packets. Its setup must not replace the
 *       music one, which is why a setup is an explicit object here (the
 *       reference keeps one in a static and reparses it per use).
 *
 * This is not a general Vorbis implementation. It is the reference's decoder:
 * mono only, no Ogg framing, no comment or identification header, and the
 * floor-1 / residue-0..2 subset Jagex's encoder emits. Anything outside that
 * fails the decode rather than guessing.
 */

/** Shared setup header: codebooks, floors, residues, mappings, modes. */
struct RSCache_VorbisSetup;

/**
 * One decoded sample: mono 16-bit PCM plus its loop points.
 *
 * `loop_start`/`loop_end` are sample offsets, not milliseconds — the units the
 * mixer wants. `loop` is the wire's sign bit on the end offset (a negative end
 * means "this sample loops"), and is what selects ping-pong looping in the
 * music synth.
 */
struct RSCache_AudioSample
{
    int sample_rate;
    int sample_count;
    int loop_start;
    int loop_end;
    bool loop;
    int16_t* samples;
};

/** Decode index 14 archive 0 (or an index 4 effect's embedded setup). */
struct RSCache_VorbisSetup*
RSCache_VorbisSetupNewDecode(
    const char* data,
    int data_size);

void
RSCache_VorbisSetupFree(struct RSCache_VorbisSetup* setup);

/**
 * Decode a sample blob — 20-byte header then laced audio packets — against a
 * setup. Returns NULL if the blob is truncated or a packet is malformed.
 */
struct RSCache_AudioSample*
RSCache_VorbisSampleNewDecode(
    const struct RSCache_VorbisSetup* setup,
    const char* data,
    int data_size);

/**
 * Decode a self-contained index 4 sample file: `u4 setupLength`, setup header,
 * sample blob. The setup is built, used and freed here — call it only for
 * effect samples, never for music samples, which share one setup.
 */
struct RSCache_AudioSample*
RSCache_VorbisEffectSampleNewDecode(
    const char* data,
    int data_size);

/** Allocate a silent sample of `sample_count` frames (samples zeroed). */
struct RSCache_AudioSample*
RSCache_AudioSampleNew(
    int sample_rate,
    int sample_count);

void
RSCache_AudioSampleFree(struct RSCache_AudioSample* sample);

#endif
