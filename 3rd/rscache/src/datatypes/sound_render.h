#ifndef RSCACHE_DATATYPES_SOUND_RENDER_H
#define RSCACHE_DATATYPES_SOUND_RENDER_H

#include "sound_synth.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Render a decoded sound effect to PCM.
 *
 * This is the other half of the format: the bytes in the cache describe an FM
 * synthesiser program, and nothing can play them until it is run. The same
 * argument that puts procedural texture generation in this library (see
 * dat2_proctexture.h) puts this here — it is the meaning of the format, and it
 * belongs next to the decoder that reads it rather than being re-derived by
 * every consumer.
 *
 * Output is 8-bit unsigned mono at RSCACHE_SOUND_SAMPLE_RATE, i.e. exactly a
 * RIFF/WAVE `data` payload for that format, with 128 as silence.
 *
 * ## Two mixing generations
 *
 * The tone synthesis is identical in both eras. How tones are summed into the
 * output is not, and each era is matched to its own reference client:
 *
 *   RSCACHE_CODEC_SOUND_WAVE  — Client-TS `Wave.generate` / Client3 `wave.c`.
 *     The buffer starts at -128 and tone contributions are added in **wrapping**
 *     signed-8-bit arithmetic. A sum loud enough to overflow wraps, which is
 *     audible, and reproducing it is what makes these effects sound like the
 *     2004 client rather than a cleaned-up version of it.
 *   RSCACHE_CODEC_SOUND_SYNTH — rt4 `SynthSound.getSamples`.
 *     The buffer starts at 0 and contributions **clamp** to [-128, 127].
 *
 * Both then interpret their byte as a sample; WAVE's is already biased by 128,
 * SYNTH's is signed and gets the bias added on the way out, so callers see one
 * format.
 *
 * ## Deviation: the noise table
 *
 * Waveform 4 reads a table of +-1 values. rt4 fills it from a Java `Random`
 * seeded with 0; Client-TS and Client3 fill it from an *unseeded* RNG, so their
 * table differs between runs. Both are white noise and neither client's exact
 * sequence is meaningful, so this renderer always uses the seeded sequence: it
 * makes every render reproducible, which is what lets the tests compare bytes
 * at all. See EXCEPTIONS.md.
 */

/** Hz. Both eras: the reference's AUDIO_SAMPLE_RATE. */
#define RSCACHE_SOUND_SAMPLE_RATE 22050

/** Rendered PCM plus the loop span, in samples. */
struct RSCache_SoundPcm
{
    uint8_t* samples; /* 8-bit unsigned mono, 128 = silence */
    int sample_count;
    int sample_rate;
    /** Loop span in samples, or both -1 when the effect does not loop. */
    int loop_start;
    int loop_end;
};

/**
 * Render one effect at its natural length (no loop repeats).
 *
 * Returns false when the effect has no audible tone (zero duration), leaving
 * *out zeroed — the reference produces an empty buffer for those and the caller
 * should play nothing.
 *
 * The caller owns out->samples and frees it with RSCache_SoundPcmRelease.
 */
bool
RSCache_SoundEffectRender(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect,
    struct RSCache_SoundPcm* out);

/**
 * Repeat the loop span `loop_count` times in place of the single pass the
 * render produced (reference Wave.generate's loop expansion, which runs on the
 * rendered bytes and so composes with a render done once and cached).
 *
 * loop_count <= 1, or a loop span the effect does not have, leaves `pcm`
 * untouched and returns true. Reallocates pcm->samples on success.
 */
bool
RSCache_SoundPcmExpandLoops(
    struct RSCache_SoundPcm* pcm,
    int loop_count);

/** Wrap the samples in a 44-byte RIFF/WAVE header. Returns bytes written (44 +
 *  sample_count), or 0 if the buffer is too small. For platforms that would
 *  rather be handed a .wav than raw PCM. */
uint32_t
RSCache_SoundPcmWriteWav(
    const struct RSCache_SoundPcm* pcm,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_SoundPcmRelease(struct RSCache_SoundPcm* pcm);

#endif
