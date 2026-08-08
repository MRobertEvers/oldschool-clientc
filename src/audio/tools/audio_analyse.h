#ifndef SRC_AUDIO_TOOLS_AUDIO_ANALYSE_H
#define SRC_AUDIO_TOOLS_AUDIO_ANALYSE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What a block of PCM actually sounds like, in numbers.
 *
 * "It sounds wrong" is not something a counter can answer and not something a
 * peak level can answer either. Every audio bug found in this subsystem showed
 * up in one of the measurements below, and every one of them was originally run
 * by hand in a throwaway script -- so they live here instead, where the harness
 * and the tests can both reach them and where the thresholds are written down.
 *
 * What each one catches, from the bugs that motivated it:
 *
 *   peak / clipped     gain staging. A synth whose volume domain is wrong
 *                      saturates: peak 32767 and a mean near it.
 *   silent             a soundfont that never assembled. The stream is open,
 *                      frames flow, every one of them is zero.
 *   discontinuities    a clip cut off mid-waveform -- a loop span mistaken for
 *                      the end of playback clicks on every play.
 *   gaps               stream starvation: runs of exact zero mid-signal.
 *   flatness           noise versus music. White noise sits near 1.0; anything
 *                      tonal is well under 0.3. This is the one that tells a
 *                      broken decoder from a working one.
 *   high_ratio         broadband grit. A gain ramp that staircases instead of
 *                      sliding adds energy up here without changing the notes,
 *                      which is heard as a buzz over the mix rather than as a
 *                      fault in any note.
 *   dc                 a voice whose gain is applied without a ramp leaves a
 *                      step; a persistent offset means one never came back.
 */

struct AudioAnalysis
{
    int frames;
    int channels;
    int sample_rate;

    int peak;
    double rms;
    long clipped;
    /** Mean sample value; a healthy mix is near zero. */
    double dc;

    /** Sample-to-sample steps larger than a tenth of full scale. */
    long discontinuities;
    /** Runs of exact zero longer than 8 samples, after the first second. */
    long gap_runs;
    long gap_samples;

    /** Spectral flatness, geometric over arithmetic mean. 1 = white noise. */
    double flatness;
    /** Fraction of spectral energy above 5kHz, 0..1. */
    double high_ratio;

    /** True when the block is entirely silent. */
    bool silent;
};

/**
 * Analyse interleaved PCM.
 *
 * `channels` may be 1 or 2; a stereo block is analysed on its left channel,
 * which is where every artifact this catches appears too. Windows for the
 * spectral measures start one second in, so a load-time silence does not
 * dominate them.
 */
void
AudioAnalyse(
    const int16_t* pcm,
    int frames,
    int channels,
    int sample_rate,
    struct AudioAnalysis* out);

/** One line, for a sweep. */
void
AudioAnalyse_PrintLine(
    const char* label,
    const struct AudioAnalysis* a);

/** A paragraph, for a single subject. */
void
AudioAnalyse_PrintReport(
    const char* label,
    const struct AudioAnalysis* a);

/**
 * Does this look like music?
 *
 * Deliberately loose: the point is to separate "plausibly a tune" from silence,
 * saturation and hiss across a whole cache, not to judge a performance. Writes
 * the first failing reason to `reason` when it returns false.
 */
bool
AudioAnalyse_LooksMusical(
    const struct AudioAnalysis* a,
    const char** reason);

/** Write interleaved PCM as a RIFF/WAVE file. Returns false on an IO error. */
bool
AudioWriteWav(
    const char* path,
    const int16_t* pcm,
    int frames,
    int channels,
    int sample_rate);

/** Read a RIFF/WAVE file written by AudioWriteWav (or by the client's
 *  TORIRS_AUDIO_WAV capture). Caller frees *out_pcm. */
bool
AudioReadWav(
    const char* path,
    int16_t** out_pcm,
    int* out_frames,
    int* out_channels,
    int* out_sample_rate);

#endif
