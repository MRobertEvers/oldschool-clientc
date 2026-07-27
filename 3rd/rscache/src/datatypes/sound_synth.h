#ifndef RSCACHE_DATATYPES_SOUND_SYNTH_H
#define RSCACHE_DATATYPES_SOUND_SYNTH_H

#include "../rscache_profile.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Synthesised sound effects — the format every era in this corpus stores its
 * sound effects in.
 *
 * A sound effect is not a recording: it is a tiny FM synthesiser program. One
 * effect holds up to ten *tones* (the reference's SoundTone / SynthInstrument),
 * each an oscillator bank driven by breakpoint envelopes, plus optional
 * modulation, gating, reverb and (from the second generation) a resonant
 * filter. Rendering it to PCM is sound_render.h; this file only reads and
 * writes the program.
 *
 * ## One codec, two containers
 *
 * The record layout is the same in both containers, which is why this file
 * carries no dat1_/dat2_ prefix. Only the packaging differs:
 *
 *   dat1 — one blob, `sounds.dat` inside config archive 8 ("sound effects"),
 *          holding every effect as `u16 id` + record, terminated by id 65535.
 *          Decode with RSCache_SoundBankNewDecode.
 *   dat2 — one archive per effect id in RSCACHE_DAT2_TABLE_SOUND_EFFECTS,
 *          holding a single record. Decode with RSCache_SoundEffectNewDecode.
 *
 * ## Two generations
 *
 * The tone record grew a trailing SynthFilter (+ its envelope) when the sound
 * engine was rewritten. Measured over the corpus by exact consumption of whole
 * archives — the gate is not guesswork, but the two flavours are
 * indistinguishable byte-by-byte, so it has to come from the stated profile:
 *
 *   no filter — rs2/dat1 254 (cache254, cache254.lostcity, rs254_zuk,
 *               rs254_steeltitan): 150520/150520 and 185747/185747 bytes
 *               consumed exactly; parsing a filter instead desyncs on the
 *               second record.
 *   filter    — rs2/dat1 377 (929467/929467 bytes, 2727 effects), and every
 *               dat2 cache: osrs 184/230/239, rs2 634/643. Parsing without a
 *               filter desyncs immediately.
 *
 * No cache in the corpus sits between 254 and 377, so the exact RS2 revision
 * that introduced the filter is unmeasured; RSCache_SoundCodecVersion states
 * the boundary at 377 and test_sound.c fails loudly if a cache disagrees.
 *
 * ## What is not decoded here
 *
 * Modern OldSchool (239) also ships Jagex-compressed audio samples in the same
 * table — a "BCV" container, sometimes as a second group file next to the synth
 * record, sometimes as the whole archive. Those are not decoded (see
 * EXCEPTIONS.md); RSCache_SoundIsSampleBlob identifies them so a caller skips
 * them instead of decoding noise.
 */

/** Tone records carry a trailing filter + filter envelope. */
#define RSCACHE_SOUND_HAS_FILTER 0x1

/** dat1 up to 254: no filter. Reference Tone (Client-TS sound/Tone.ts). */
#define RSCACHE_CODEC_SOUND_WAVE 1
/** dat1 377 and every dat2: filter present. Reference SynthInstrument (rt4). */
#define RSCACHE_CODEC_SOUND_SYNTH 2

/** Which sound codec this cache uses. */
int
RSCache_SoundCodecVersion(const struct RSCache* cache);

/** Decode flags for this cache, derived from the codec version. */
int
RSCache_SoundFlags(const struct RSCache* cache);

/** Tones per effect (reference SoundTone[10] / SynthInstrument[10]). */
#define RSCACHE_SOUND_TONES 10

/**
 * Harmonic slots stored per tone.
 *
 * The wire loop runs ten times, breaking on a zero volume, but the reference's
 * arrays are five long — a sixth harmonic would throw there. Ten are stored so
 * decode stays faithful to the bytes (and round-trips); the renderer sums only
 * the first five, which is what the client does. Every effect in the corpus
 * stops at five.
 */
#define RSCACHE_SOUND_HARMONICS 10

/** Biquad pairs per filter direction. The wire nibble allows 15; the reference
 *  arrays hold four. Pairs past this are consumed and dropped (the stream stays
 *  aligned) and flagged in `pairs_dropped`. */
#define RSCACHE_SOUND_FILTER_PAIRS 4

/**
 * A breakpoint envelope (reference SoundEnvelope / SynthEnvelope).
 *
 * `times[i]` is a position on a 0..65535 timeline scaled to the render length,
 * `levels[i]` the value reached at it. `form` selects the oscillator waveform
 * for the envelopes that drive one (1 square, 2 sine, 3 saw, 4 noise);
 * `start`/`end` are the interval range the level interpolates between.
 */
struct RSCache_SoundEnvelope
{
    int form;  /* wavetable */
    int start; /* minInterval */
    int end;   /* maxInterval */
    int stage_count;
    int* times;  /* [stage_count] shapeDelta */
    int* levels; /* [stage_count] shapePeak */
};

/**
 * Resonant filter (reference SoundFilter / SynthFilter), second generation only.
 *
 * `pairs[dir]` biquad pairs per direction (0 = feed-forward, 1 = feedback),
 * each with an octave and a gain at both ends of the filter envelope's sweep.
 * `migrated` is the bitmask saying which pairs carry a distinct second-end
 * value; the rest reuse the first.
 */
struct RSCache_SoundFilter
{
    int pairs[2];
    int inverse_gain[2];
    /** [direction][end][pair] */
    int octaves[2][2][RSCACHE_SOUND_FILTER_PAIRS];
    int gain[2][2][RSCACHE_SOUND_FILTER_PAIRS];
    int migrated;
    /** Filter sweep envelope; only its stage list is on the wire (form/start/end
     *  stay at the reference's constructed defaults). */
    struct RSCache_SoundEnvelope envelope;
    bool has_envelope;
    /** Pairs the wire declared past RSCACHE_SOUND_FILTER_PAIRS, consumed and
     *  dropped. Non-zero means an encode cannot reproduce the bytes. */
    int pairs_dropped;
};

/**
 * One tone (reference SoundTone / SynthInstrument).
 *
 * `length` and `start` are milliseconds: how long the tone sounds and how far
 * into the effect it begins.
 */
struct RSCache_SoundTone
{
    struct RSCache_SoundEnvelope frequency_base; /* phaseEnvelope */
    struct RSCache_SoundEnvelope amplitude_base; /* amplitudeEnvelope */

    /** Frequency (phase) modulation: rate + range. Present as a pair. */
    struct RSCache_SoundEnvelope frequency_mod_rate;
    struct RSCache_SoundEnvelope frequency_mod_range;
    bool has_frequency_mod;

    /** Amplitude modulation: rate + range. Present as a pair. */
    struct RSCache_SoundEnvelope amplitude_mod_rate;
    struct RSCache_SoundEnvelope amplitude_mod_range;
    bool has_amplitude_mod;

    /** Gate: release (gate-closed) + attack (gate-open) phase. Present as a
     *  pair. Chops the tone into pulses. */
    struct RSCache_SoundEnvelope release;
    struct RSCache_SoundEnvelope attack;
    bool has_gate;

    int harmonic_volume[RSCACHE_SOUND_HARMONICS];
    int harmonic_semitone[RSCACHE_SOUND_HARMONICS];
    int harmonic_delay[RSCACHE_SOUND_HARMONICS];
    /** Harmonics the wire actually carried (the loop breaks on volume 0). */
    int harmonic_count;

    int reverb_delay;
    int reverb_volume; /* percent; 100 = unity */
    int length;        /* ms */
    int start;         /* ms */

    struct RSCache_SoundFilter filter;
    bool has_filter;
};

/** An audio blob this library does not decode (see RSCache_SoundIsSampleBlob). */
enum RSCache_SoundSampleKind
{
    RSCACHE_SOUND_SAMPLE_NONE = 0,
    /** Jagex "BCV" compressed audio (modern OldSchool). Not decoded. */
    RSCACHE_SOUND_SAMPLE_BCV = 1,
    /** Present, magic unrecognised. Not decoded. */
    RSCACHE_SOUND_SAMPLE_UNKNOWN = 2,
};

/**
 * One sound effect (reference Wave / SynthSound / SoundEffect).
 *
 * `loop_begin`/`loop_end` are milliseconds; when begin < end the span loops for
 * as many repeats as the server asked for. They are the same two fields the
 * modern client calls start/end.
 */
struct RSCache_SoundEffect
{
    /** Effect id. Set by the bank decoder; -1 for a single-record decode, whose
     *  id is the archive it came from. */
    int id;
    struct RSCache_SoundTone* tones[RSCACHE_SOUND_TONES]; /* NULL = absent */
    int loop_begin;
    int loop_end;
    /** Bytes consumed by the decode. Compare against the record size to detect
     *  misalignment (zero if the decode bailed). */
    int _consumed;
};

/** Every effect in a dat1 `sounds.dat`, indexed by id (holes are NULL). */
struct RSCache_SoundBank
{
    struct RSCache_SoundEffect** effects;
    /** One past the highest id present, so `effects[id]` is valid for id < count. */
    int count;
    /**
     * Ids in the order the file listed them, which is *not* ascending — every
     * dat1 cache here starts partway up the id space (cache.rs377's first record
     * is id 1278) and jumps around. The order carries no meaning to a client,
     * which looks effects up by id, but reproducing it is what lets an encode
     * reproduce the file byte for byte.
     */
    int* order;
    int order_count;
    int _consumed;
};

/**
 * Is this archive/file an audio sample rather than a synth program?
 *
 * Modern OldSchool stores some sound effects as Jagex-compressed audio in the
 * same table. Returns the kind, or RSCACHE_SOUND_SAMPLE_NONE when the bytes
 * look like a synth record. Cheap header check, not a decode.
 */
enum RSCache_SoundSampleKind
RSCache_SoundSampleKindOf(
    const char* data,
    int data_size);

/** Convenience: true when RSCache_SoundSampleKindOf is not NONE. */
bool
RSCache_SoundIsSampleBlob(
    const char* data,
    int data_size);

/** Decode one effect record (a dat2 archive, or a dat2 group's file 0). */
struct RSCache_SoundEffect*
RSCache_SoundEffectNewDecode(
    const struct RSCache* cache,
    char* data,
    int data_size);

/** Decode a dat1 `sounds.dat`: every effect keyed by id. */
struct RSCache_SoundBank*
RSCache_SoundBankNewDecode(
    const struct RSCache* cache,
    char* data,
    int data_size);

/**
 * Encode one effect record. Exact inverse of the decoder for every effect in
 * the corpus — the format has no optional-field ordering to guess at, so a
 * decode/encode round trip is byte-identical unless a filter dropped pairs.
 *
 * Returns bytes written, or 0 on failure (out_capacity too small).
 */
uint32_t
RSCache_SoundEffectEncode(
    const struct RSCache* cache,
    const struct RSCache_SoundEffect* effect,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_SoundEffectEncode. */
uint32_t
RSCache_SoundEffectEncodeBound(const struct RSCache_SoundEffect* effect);

/** Encode a whole bank back to `sounds.dat` bytes (ids ascending, 65535 tail). */
uint32_t
RSCache_SoundBankEncode(
    const struct RSCache* cache,
    const struct RSCache_SoundBank* bank,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_SoundBankEncode. */
uint32_t
RSCache_SoundBankEncodeBound(const struct RSCache_SoundBank* bank);

/**
 * Shift the effect so it starts at zero, returning the removed lead-in in
 * client ticks (20ms). Reference Wave.trim / SynthSound.getStart: the server's
 * play delay is expressed relative to the effect's own start, so the client
 * subtracts the lead-in here and adds it back to the queue delay.
 *
 * Idempotent — a trimmed effect returns 0.
 */
int
RSCache_SoundEffectTrim(struct RSCache_SoundEffect* effect);

/** Effect duration in milliseconds (max over tones of start + length). */
int
RSCache_SoundEffectDurationMs(const struct RSCache_SoundEffect* effect);

void
RSCache_SoundEffectFree(struct RSCache_SoundEffect* effect);

void
RSCache_SoundBankFree(struct RSCache_SoundBank* bank);

#endif
