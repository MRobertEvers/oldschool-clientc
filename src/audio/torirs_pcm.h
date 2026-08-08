#ifndef SRC_AUDIO_TORIRS_PCM_H
#define SRC_AUDIO_TORIRS_PCM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * One playing sample: resampling, looping, and a ramped stereo gain.
 *
 * This is the reference's RawPcmStream, reduced. The reference hand-specialises
 * eight combinations of {forward, backward} x {no loop, loop, ping-pong} x
 * {ramping, steady} into eight near-identical inner loops -- 1500 lines that
 * differ only in which bound they compare against. Here there is one loop that
 * reads its direction and bounds from state. The arithmetic is the same
 * (8.8 fixed-point position, linear interpolation, constant-power pan) and the
 * output is the same; only the unrolling is gone.
 *
 * Two things use it and both need all of it:
 *
 *   the mixer      plays cache sound effects and area loops
 *   the MIDI synth plays one of these per sounding note, retuned per note and
 *                  ramped by the patch's envelopes
 *
 * ## Gain
 *
 * Volume is 0..TORIRS_PCM_VOLUME_MAX and pan is 0..TORIRS_PCM_PAN_MAX with
 * TORIRS_PCM_PAN_CENTRE in the middle, matching the reference's ranges. Pan is
 * constant power (`sqrt`), so a sound swept across the field keeps its
 * loudness; a linear pan law dips ~3dB in the middle and is audible on the
 * area sounds, which pan continuously as the camera turns.
 *
 * The absolute output scale is *not* bit-identical to the reference. The
 * reference's shift was chosen for 8-bit samples and its modern revision
 * carries 16-bit ones; rather than reproduce a constant that changed meaning
 * between revisions, gain here is normalised so full volume at centre is unity.
 * Loudness is set by the volume settings on top of that.
 */

/**
 * Volume domains.
 *
 * The command interface speaks 0..TORIRS_PCM_VOLUME_MAX, which is what a
 * setting slider and the server's own numbers are in. Inside a voice, gain is
 * carried six bits finer -- `volume << 6`, unity at TORIRS_PCM_VOLUME_UNITY --
 * because that is the domain the reference's synth computes note volumes in and
 * because 255 integer steps is too coarse for a single note of a twelve-voice
 * chord: a typical note sits around 1000/16320, and rounding that to 15/255
 * quantises a chord into audible steps.
 */
#define TORIRS_PCM_VOLUME_MAX 255
#define TORIRS_PCM_VOLUME_SHIFT 6
#define TORIRS_PCM_VOLUME_UNITY (TORIRS_PCM_VOLUME_MAX << TORIRS_PCM_VOLUME_SHIFT)
#define TORIRS_PCM_PAN_CENTRE 8192
#define TORIRS_PCM_PAN_MAX 16384
/** Position and step are 8.8 fixed point, like the reference. */
#define TORIRS_PCM_POSITION_SHIFT 8
#define TORIRS_PCM_POSITION_ONE (1 << TORIRS_PCM_POSITION_SHIFT)
/** Gains are 14-bit so `sample * gain >> 14` stays in int32 without saturating. */
#define TORIRS_PCM_GAIN_SHIFT 14
#define TORIRS_PCM_GAIN_ONE (1 << TORIRS_PCM_GAIN_SHIFT)

/** Immutable sample data. Borrowed: the owner outlives every voice on it. */
struct ToriRS_PcmSound
{
    const int16_t* samples;
    int sample_count;
    int sample_rate;
    /** Loop span. Ignored when `loop_end <= loop_start`. */
    int loop_start;
    int loop_end;
    /** Ping-pong rather than restart at `loop_start` (reference RawSound's
     *  trailing flag, which the music patches set for sustained instruments). */
    bool ping_pong;
};

struct ToriRS_PcmVoice
{
    const struct ToriRS_PcmSound* sound;

    /** 8.8 fixed-point index into `sound->samples`. */
    int position;
    /** 8.8 fixed-point advance per output frame. Negative plays backwards. */
    int step;

    /** 0 = play through once, -1 = loop forever, n > 0 = n more loop passes. */
    int loops_left;

    int volume;
    int pan;
    /** Derived from volume/pan; the ramp interpolates these directly so a ramp
     *  never has to re-derive a square root per frame. */
    int left_gain;
    int right_gain;

    /* Ramp state. `ramp_frames` counts down; at zero the target is in force. */
    int ramp_frames;
    int target_left_gain;
    int target_right_gain;
    int left_step;
    int right_step;
    /** True once the ramp is a fade to silence, so the voice ends when it lands
     *  rather than continuing at zero gain. */
    bool ramp_to_stop;

    bool active;
};

/**
 * Constant-power pan gains (reference method877/872).
 *
 * `volume` is in the fine domain: TORIRS_PCM_VOLUME_UNITY is unity gain.
 */
void
ToriRS_PcmGains(
    int volume,
    int pan,
    int* out_left,
    int* out_right);

/**
 * Start a voice.
 *
 * `rate_numerator`/`rate_denominator` set playback speed: pass the sound's own
 * rate over the mixer's rate for natural pitch. `volume` is in the fine domain
 * (TORIRS_PCM_VOLUME_UNITY is unity). `loop_count` follows the server's
 * convention -- 0 plays once, n plays the loop span n times, -1 loops until
 * stopped.
 */
void
ToriRS_PcmVoice_Start(
    struct ToriRS_PcmVoice* voice,
    const struct ToriRS_PcmSound* sound,
    int rate_numerator,
    int rate_denominator,
    int volume,
    int pan,
    int loop_count);

/** Change target volume/pan. `ramp_frames` 0 applies immediately. */
void
ToriRS_PcmVoice_SetGain(
    struct ToriRS_PcmVoice* voice,
    int volume,
    int pan,
    int ramp_frames);

/** Retune without restarting — how the synth applies pitch bend and vibrato. */
void
ToriRS_PcmVoice_SetRate(
    struct ToriRS_PcmVoice* voice,
    int rate_numerator,
    int rate_denominator);

/** Fade to silence over `ramp_frames`, then end. 0 ends immediately. */
void
ToriRS_PcmVoice_Stop(
    struct ToriRS_PcmVoice* voice,
    int ramp_frames);

/** Jump to an absolute sample position. */
void
ToriRS_PcmVoice_Seek(
    struct ToriRS_PcmVoice* voice,
    int sample_position);

/** Jump to an 8.8 fixed-point position (reference method883, which is what the
 *  synth's per-note start offset is expressed in). */
void
ToriRS_PcmVoice_SeekFixed(
    struct ToriRS_PcmVoice* voice,
    int fixed_position);

/** Set the 8.8 step directly. The synth computes pitch as a step rather than a
 *  rate ratio -- it is already working in fixed point and a round trip through
 *  numerator/denominator would only lose bits. */
void
ToriRS_PcmVoice_SetStep(
    struct ToriRS_PcmVoice* voice,
    int step);

/** Play backwards (ping-pong start offsets land mid-reverse-pass). */
void
ToriRS_PcmVoice_SetBackwards(
    struct ToriRS_PcmVoice* voice,
    bool backwards);

/** True once the position has run outside the sample entirely. */
bool
ToriRS_PcmVoice_Exhausted(const struct ToriRS_PcmVoice* voice);

/**
 * Add `frames` frames of this voice into the interleaved stereo accumulator.
 *
 * `out` holds `frames * 2` int32 slots; the caller clamps once at the end
 * rather than per voice, so summed voices do not clip each other.
 *
 * Returns true while the voice is still sounding.
 */
bool
ToriRS_PcmVoice_Mix(
    struct ToriRS_PcmVoice* voice,
    int32_t* out,
    int frames);

/** Advance without producing output — a muted voice must still age. */
void
ToriRS_PcmVoice_Skip(
    struct ToriRS_PcmVoice* voice,
    int frames);

#endif
