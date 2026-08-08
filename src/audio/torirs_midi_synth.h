#ifndef SRC_AUDIO_TORIRS_MIDI_SYNTH_H
#define SRC_AUDIO_TORIRS_MIDI_SYNTH_H

#include "audio/torirs_midi_file.h"
#include "audio/torirs_pcm.h"
#include "audio/torirs_soundbank.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The music synthesiser: a MIDI sequencer driving a sampler over the cache's
 * own soundfont. Reference MidiPcmStream + MusicPatchPcmStream + MusicPatchNode.
 *
 * This is a software instrument, not a MIDI *player*. There is no system
 * synth involved and there could not be: the game's instruments are the
 * patches and samples in cache indices 14 and 15, and a track played on a
 * general-MIDI device sounds like a different game.
 *
 * ## Two clocks
 *
 * Sequencing and synthesis run at different rates and both are sample-accurate:
 *
 *   the sequencer advances in MIDI ticks. A tick's wall time is
 *     `tempo * tick + elapsed` microseconds, and the synth converts output
 *     frames into the same units so an event lands on the exact frame it should.
 *     Rendering therefore proceeds in *chunks between events*, not in fixed
 *     blocks.
 *   each sounding note advances its envelopes every 10ms (`sample_rate / 100`,
 *     the reference's rate), and its pitch, volume and pan are recomputed then
 *     and ramped across the interval rather than stepped.
 *
 * ## What a note is
 *
 * One `ToriRS_MidiNode`: a PcmVoice over a patch sample, plus the envelope
 * state that shapes it -- amplitude envelope, release envelope, exponential
 * decay, vibrato LFO with its own delay, and portamento towards a retargeted
 * pitch. Notes are killed by note-off, by their release envelope finishing, by
 * an exclusive group (drums choke each other), or by the channel being reset.
 *
 * ## Output
 *
 * `Render` writes interleaved stereo into an int16 block, which the game hands
 * to the mixer as a stream. It is not mixed here with sound effects: effects
 * are retained assets played by the backend, and music is the one thing the
 * game must generate itself.
 */

#define TORIRS_MIDI_CHANNELS 16
#define TORIRS_MIDI_MAX_NODES 128

struct ToriRS_MidiNode
{
    bool active;

    int channel;
    struct ToriRS_SoundBankPatch* patch;
    const struct ToriRS_PcmSound* sound;
    const struct RSCache_MusicPatchEnvelope* envelope;

    /** Exclusive group; -1 for none. Starting a note in a group releases the
     *  group's previous note, which is how a hi-hat closes. */
    int group;
    int note;
    /** Velocity-scaled patch volume, fixed for the note's lifetime. */
    int volume;
    /** Patch pan, 0..128. */
    int pan;

    /** Pitch in 1/256 semitones, and the portamento glide towards it. */
    int base_pitch;
    int portamento_delta;
    int portamento_remaining;

    int decay_time;
    int amplitude_time;
    int amplitude_index;
    /** -1 while the note is held; >= 0 once released, and then the release
     *  envelope's clock. */
    int release_time;
    int release_index;

    /** Envelope steps elapsed — the vibrato delay counts in these. */
    int age;
    int vibrato_phase;
    /** Frames until the next envelope step. */
    int step_countdown;
    /** Retrigger accumulator for controller 81's re-attack mode. */
    int retrigger;

    struct ToriRS_PcmVoice voice;
};

struct ToriRS_MidiChannel
{
    int volume;          /* CC 7/39, 14-bit */
    int pan;             /* CC 10/42, 14-bit */
    int expression;      /* CC 11/43, 14-bit */
    int pitch_bend;      /* 14-bit, 8192 centre */
    int modulation;      /* CC 1/33 */
    int portamento_time; /* CC 5/37 */
    int bank;            /* CC 0/32 folded into the program */
    int program;         /* the banked program most recently selected */
    int patch_id;        /* the patch actually bound */
    /** bit 0 sustain (CC 64), bit 1 legato/portamento (CC 65), bit 2 retrigger
     *  (CC 81). */
    int flags;
    int rpn;
    int bend_range;    /* RPN 0, in 1/256 semitones per bend unit */
    int start_offset;  /* CC 16/48: start a note partway into its sample */
    int retrigger_raw; /* CC 17/49 */
    int retrigger_rate;

    /** Node currently sounding for each note, and for each exclusive group. */
    struct ToriRS_MidiNode* note_on[128];
    struct ToriRS_MidiNode* group[128];
};

struct ToriRS_MidiSynthStats
{
    int notes_started;
    int notes_dropped_no_patch;
    int notes_dropped_no_sample;
    int notes_stolen;
    int events_dispatched;
    int loops;

    /** Largest |sample| the mix reached before the output clamp. */
    int unclamped_peak;

    /** Times a note's computed gain exceeded unity and was clamped. The
     *  reference (class356.method8228) returns this value unclamped. */
    int volume_clamped;
};

struct ToriRS_MidiSynth
{
    int sample_rate;
    /** Synth-internal master gain, reference field3629 (256 = unity). */
    int master_volume;

    struct ToriRS_SoundBank* bank; /* borrowed */
    struct ToriRS_MidiFile file;
    bool looping;
    bool playing;

    struct ToriRS_MidiChannel channels[TORIRS_MIDI_CHANNELS];

    struct ToriRS_MidiNode nodes[TORIRS_MIDI_MAX_NODES];
    /** Node indices in start order — the reference walks its list in both
     *  directions and the order is load-bearing for legato and for choking. */
    int order[TORIRS_MIDI_MAX_NODES];
    int order_count;

    /* Sequencer position, in the reader's microsecond*division units. */
    int64_t position;
    int64_t next_event_time;
    int current_track;
    int current_tick;
    /** Microseconds*division consumed per output frame. */
    int step_per_frame;

    int32_t* accumulator;
    int accumulator_frames;

    struct ToriRS_MidiSynthStats stats;
};

void
ToriRS_MidiSynth_Init(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_SoundBank* bank,
    int sample_rate);

void
ToriRS_MidiSynth_Free(struct ToriRS_MidiSynth* synth);

/**
 * Start playing a song.
 *
 * `midi` is borrowed and must outlive playback -- the sequencer reads from it
 * every block rather than copying it, which is what keeps a 60MB track's cost
 * at one allocation. Returns false if it is not a MIDI file.
 */
bool
ToriRS_MidiSynth_Play(
    struct ToriRS_MidiSynth* synth,
    const uint8_t* midi,
    int midi_size,
    bool looping);

/** Silence every note and forget the song. */
void
ToriRS_MidiSynth_Stop(struct ToriRS_MidiSynth* synth);

static inline bool
ToriRS_MidiSynth_Playing(const struct ToriRS_MidiSynth* synth)
{
    return synth && synth->playing;
}

/** True once a non-looping song has run out and every note has died away. */
bool
ToriRS_MidiSynth_Finished(const struct ToriRS_MidiSynth* synth);

/**
 * Render `frames` interleaved stereo frames.
 *
 * Always fills the block. Dispatches every MIDI event that falls inside it, at
 * the frame it falls on.
 */
void
ToriRS_MidiSynth_Render(
    struct ToriRS_MidiSynth* synth,
    int16_t* out,
    int frames);

int
ToriRS_MidiSynth_LiveNoteCount(const struct ToriRS_MidiSynth* synth);

/**
 * Of the live notes, how many are still held -- key down, not yet released.
 *
 * Live count alone cannot tell a dense arrangement from a note-off that never
 * landed: both look like a large number of sounding notes. A release tail is
 * supposed to outlive its key, so a high live count with a low held count is
 * simply long releases. A high *held* count is notes that were never let go.
 */
int
ToriRS_MidiSynth_HeldNoteCount(const struct ToriRS_MidiSynth* synth);

#endif
