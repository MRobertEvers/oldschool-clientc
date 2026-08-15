#include "audio/torirs_midi_synth.h"
#include <assert.h>

#include <rscache.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Port of MidiPcmStream + MusicPatchPcmStream + MusicPatchNode.
 *
 * The magic constants are the reference's and every one of them is a unit
 * conversion, so they are named here once rather than explained at each use:
 *
 *   PITCH_PER_OCTAVE   3072 = 256 * 12. Pitch is carried in 1/256 semitones.
 *   ENVELOPE_PER_OCTAVE 196608 = 256 * 768. Envelope rates scale with a note's
 *                       distance from middle C, in a coarser unit.
 *   VIBRATO_PHASE_SCALE 0.012271846 = 2*pi/512. The LFO phase is a 9-bit
 *                       counter.
 *   DECAY_PER_STEP      1/51200. The exponential decay's time base.
 *   PORTAMENTO_BASE     1/2032. Controller 5's glide rate.
 */
#define MIDI_PITCH_PER_OCTAVE 3072.0
#define MIDI_ENVELOPE_PER_OCTAVE 196608.0
#define MIDI_VIBRATO_PHASE_SCALE 0.01227184630308513
#define MIDI_DECAY_PER_STEP 1.953125e-5
#define MIDI_PORTAMENTO_BASE 4.921259842519685e-4
/** Microseconds in a second: the sequencer's clock unit. */
#define MIDI_MICROSECONDS 1000000

static int
clamp_int(
    int value,
    int low,
    int high)
{
    return value < low ? low : value > high ? high : value;
}

/* --- node bookkeeping ----------------------------------------------------- */

static struct ToriRS_MidiNode*
node_alloc(struct ToriRS_MidiSynth* synth)
{
    for( int i = 0; i < TORIRS_MIDI_MAX_NODES; i++ )
    {
        if( !synth->nodes[i].active )
        {
            struct ToriRS_MidiNode* node = &synth->nodes[i];
            memset(node, 0, sizeof(*node));
            node->active = true;
            synth->order[synth->order_count++] = i;
            return node;
        }
    }
    synth->stats.notes_stolen++;
    return NULL;
}

static void
node_release(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_MidiNode* node)
{
    int index = (int)(node - synth->nodes);

    for( int i = 0; i < synth->order_count; i++ )
    {
        if( synth->order[i] != index )
            continue;
        for( int j = i; j + 1 < synth->order_count; j++ )
            synth->order[j] = synth->order[j + 1];
        synth->order_count--;
        break;
    }
    /* Whatever else it was attached to must forget it before the slot is
     * reused, or a later note-off will release a stranger's note. */
    if( node->channel >= 0 && node->channel < TORIRS_MIDI_CHANNELS )
    {
        struct ToriRS_MidiChannel* channel = &synth->channels[node->channel];
        if( node->note >= 0 && node->note < 128 && channel->note_on[node->note] == node )
            channel->note_on[node->note] = NULL;
        if( node->group >= 0 && node->group < 128 && channel->group[node->group] == node )
            channel->group[node->group] = NULL;
    }
    memset(node, 0, sizeof(*node));
    node->active = false;
}

int
ToriRS_MidiSynth_LiveNoteCount(const struct ToriRS_MidiSynth* synth)
{
    return synth ? synth->order_count : 0;
}

int
ToriRS_MidiSynth_HeldNoteCount(const struct ToriRS_MidiSynth* synth)
{
    int held = 0;

    assert(synth);
    for( int i = 0; i < synth->order_count; i++ )
    {
        const struct ToriRS_MidiNode* node = &synth->nodes[synth->order[i]];

        if( node->active && node->release_time < 0 )
            held++;
    }
    return held;
}

/* --- per-node parameter computation --------------------------------------- */

/** Reference method5926: the voice's 8.8 step for this node, right now. */
static int
node_step(
    struct ToriRS_MidiSynth* synth,
    const struct ToriRS_MidiNode* node)
{
    const struct ToriRS_MidiChannel* channel = &synth->channels[node->channel];
    const struct RSCache_MusicPatchEnvelope* envelope = node->envelope;
    int pitch = ((node->portamento_delta * node->portamento_remaining) >> 12) + node->base_pitch;
    int step;

    pitch += ((channel->pitch_bend - 8192) * channel->bend_range) >> 12;

    if( envelope && envelope->vibrato_speed > 0 &&
        (envelope->vibrato_depth > 0 || channel->modulation > 0) )
    {
        int depth = envelope->vibrato_depth << 2;
        int delay = envelope->vibrato_delay << 1;
        int amount;

        /* The LFO fades in over `vibrato_delay` envelope steps, so a held note
         * swells into its vibrato instead of starting with it. */
        if( delay > 0 && node->age < delay )
            depth = node->age * depth / delay;
        amount = (channel->modulation >> 7) + depth;
        pitch += (int)((double)amount * sin((double)(node->vibrato_phase & 511) *
                                            MIDI_VIBRATO_PHASE_SCALE));
    }

    step = (int)((double)(node->sound->sample_rate * 256) *
                     pow(2.0, (double)pitch / MIDI_PITCH_PER_OCTAVE) / (double)synth->sample_rate +
                 0.5);
    return step < 1 ? 1 : step;
}

/** Interpolate a (time, level) envelope at `time`. */
static int
envelope_level(
    const uint8_t* points,
    int length,
    int index,
    int time)
{
    int level = (int8_t)points[index + 1];

    if( index < length - 2 )
    {
        int t0 = (points[index] & 255) << 8;
        int t1 = (points[index + 2] & 255) << 8;
        if( t1 != t0 )
            level += ((int8_t)points[index + 3] - level) * (time - t0) / (t1 - t0);
    }
    return level;
}

/** Reference method5890: the node's volume, 0..255-ish, right now. */
static int
node_volume(
    struct ToriRS_MidiSynth* synth,
    const struct ToriRS_MidiNode* node)
{
    const struct ToriRS_MidiChannel* channel = &synth->channels[node->channel];
    const struct RSCache_MusicPatchEnvelope* envelope = node->envelope;
    int scaled = ((channel->volume * channel->expression) + 4096) >> 13;
    int volume;

    scaled = (scaled * scaled + 16384) >> 15;
    volume = (node->volume * scaled + 16384) >> 15;
    volume = (synth->master_volume * volume + 128) >> 8;

    if( !envelope )
    {
        if( volume > TORIRS_PCM_VOLUME_UNITY )
            synth->stats.volume_clamped++;
        return clamp_int(volume, 0, TORIRS_PCM_VOLUME_UNITY);
    }

    if( envelope->decay > 0 )
        volume = (int)((double)volume * pow(0.5,
                                            (double)node->decay_time * MIDI_DECAY_PER_STEP *
                                                (double)envelope->decay) +
                       0.5);
    if( envelope->amplitude )
        volume = (volume * envelope_level(
                               envelope->amplitude,
                               envelope->amplitude_len,
                               node->amplitude_index,
                               node->amplitude_time) +
                  32) >>
                 6;
    if( node->release_time > 0 && envelope->release )
        volume = (volume * envelope_level(
                               envelope->release,
                               envelope->release_len,
                               node->release_index,
                               node->release_time) +
                  32) >>
                 6;
    /* Fine domain: this is the reference's `volume << 6`, handed straight to
     * the voice without narrowing. */
    if( volume > TORIRS_PCM_VOLUME_UNITY )
        synth->stats.volume_clamped++;
    return clamp_int(volume, 0, TORIRS_PCM_VOLUME_UNITY);
}

/** Reference method5940: the node's pan, 0..16384. */
static int
node_pan(
    struct ToriRS_MidiSynth* synth,
    const struct ToriRS_MidiNode* node)
{
    int channel_pan = synth->channels[node->channel].pan;

    if( channel_pan < 8192 )
        return (node->pan * channel_pan + 32) >> 6;
    return 16384 - (((128 - node->pan) * (16384 - channel_pan) + 32) >> 6);
}

/* --- channel operations --------------------------------------------------- */

static void
channel_note_off(
    struct ToriRS_MidiSynth* synth,
    int channel_index,
    int note)
{
    struct ToriRS_MidiChannel* channel = &synth->channels[channel_index];
    struct ToriRS_MidiNode* node;

    if( note < 0 || note >= 128 )
        return;
    node = channel->note_on[note];
    if( !node )
        return;
    channel->note_on[note] = NULL;

    if( (channel->flags & 2) != 0 )
    {
        /*
         * Legato: releasing a key only ends the note if another key on the
         * channel is still down. Lifting the last one leaves it sounding,
         * which is what makes a legato line join rather than gap.
         */
        for( int i = 0; i < synth->order_count; i++ )
        {
            struct ToriRS_MidiNode* other = &synth->nodes[synth->order[i]];
            if( other->channel == node->channel && other->release_time < 0 && other != node )
            {
                node->release_time = 0;
                break;
            }
        }
    }
    else
    {
        node->release_time = 0;
    }
}

/** Start the release envelope on every held note of a channel (CC 64 off). */
static void
channel_sustain_off(
    struct ToriRS_MidiSynth* synth,
    int channel_index)
{
    struct ToriRS_MidiChannel* channel = &synth->channels[channel_index];

    if( (channel->flags & 2) == 0 )
        return;
    for( int i = 0; i < synth->order_count; i++ )
    {
        struct ToriRS_MidiNode* node = &synth->nodes[synth->order[i]];
        if( node->channel == channel_index && node->release_time < 0 &&
            channel->note_on[node->note] == NULL )
            node->release_time = 0;
    }
}

static void
channel_retrigger_off(
    struct ToriRS_MidiSynth* synth,
    int channel_index)
{
    if( (synth->channels[channel_index].flags & 4) == 0 )
        return;
    for( int i = 0; i < synth->order_count; i++ )
    {
        struct ToriRS_MidiNode* node = &synth->nodes[synth->order[i]];
        if( node->channel == channel_index )
            node->retrigger = 0;
    }
}

/** All notes off: release everything still held (CC 123). */
static void
channel_all_notes_off(
    struct ToriRS_MidiSynth* synth,
    int channel_index)
{
    for( int i = 0; i < synth->order_count; i++ )
    {
        struct ToriRS_MidiNode* node = &synth->nodes[synth->order[i]];
        if( (channel_index < 0 || node->channel == channel_index) && node->release_time < 0 )
        {
            synth->channels[node->channel].note_on[node->note] = NULL;
            node->release_time = 0;
        }
    }
}

/** All sound off: kill everything immediately (CC 120, and song reset). */
static void
channel_all_sound_off(
    struct ToriRS_MidiSynth* synth,
    int channel_index)
{
    for( int i = synth->order_count - 1; i >= 0; i-- )
    {
        struct ToriRS_MidiNode* node = &synth->nodes[synth->order[i]];
        if( channel_index >= 0 && node->channel != channel_index )
            continue;
        node_release(synth, node);
    }
}

static void
channel_set_retrigger_rate(
    struct ToriRS_MidiChannel* channel,
    int raw)
{
    channel->retrigger_raw = raw;
    channel->retrigger_rate =
        (int)(2097152.0 * pow(2.0, (double)raw * 5.4931640625e-4) + 0.5);
}

static void
channel_reset(
    struct ToriRS_MidiSynth* synth,
    int channel_index)
{
    struct ToriRS_MidiChannel* channel;

    if( channel_index < 0 )
    {
        for( int i = 0; i < TORIRS_MIDI_CHANNELS; i++ )
            channel_reset(synth, i);
        return;
    }
    channel = &synth->channels[channel_index];
    channel->volume = 12800;
    channel->pan = 8192;
    channel->expression = 16383;
    channel->pitch_bend = 8192;
    channel->modulation = 0;
    channel->portamento_time = 8192;
    channel_sustain_off(synth, channel_index);
    channel_retrigger_off(synth, channel_index);
    channel->flags = 0;
    channel->rpn = 32767;
    channel->bend_range = 256;
    channel->start_offset = 0;
    channel_set_retrigger_rate(channel, 8192);
}

/**
 * Bind a channel to a patch.
 *
 * Changing patch invalidates every note-on slot: the slots index the *old*
 * patch's notes, and reusing them would release notes the new patch never
 * started.
 */
static void
channel_set_patch(
    struct ToriRS_MidiSynth* synth,
    int channel_index,
    int patch_id)
{
    struct ToriRS_MidiChannel* channel = &synth->channels[channel_index];

    if( channel->patch_id == patch_id )
        return;
    channel->patch_id = patch_id;
    for( int i = 0; i < 128; i++ )
        channel->group[i] = NULL;
}

/** Reference method5865: start a note partway into its sample (CC 16/48). */
static void
node_apply_start_offset(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_MidiNode* node,
    bool looping)
{
    const struct ToriRS_MidiChannel* channel = &synth->channels[node->channel];
    int length = node->sound->sample_count;
    int position;

    if( looping && node->sound->ping_pong )
    {
        int span = length + length - node->sound->loop_start;
        int limit = length << 8;
        position = (int)(((int64_t)channel->start_offset * (int64_t)span) >> 6);
        if( position >= limit )
        {
            /* Past the end of the forward pass: reflect into the reverse one. */
            position = limit + limit - 1 - position;
            ToriRS_PcmVoice_SetBackwards(&node->voice, true);
        }
    }
    else
    {
        position = (int)(((int64_t)channel->start_offset * (int64_t)length) >> 6);
    }
    ToriRS_PcmVoice_SeekFixed(&node->voice, position);
}

static void
channel_note_on(
    struct ToriRS_MidiSynth* synth,
    int channel_index,
    int note,
    int velocity)
{
    struct ToriRS_MidiChannel* channel = &synth->channels[channel_index];
    struct ToriRS_SoundBankPatch* patch;
    const struct ToriRS_PcmSound* sound;
    struct ToriRS_MidiNode* node;
    int envelope_index;
    bool looping;

    channel_note_off(synth, channel_index, note);

    if( (channel->flags & 2) != 0 )
    {
        /* Portamento: retune the channel's held note instead of starting one. */
        for( int i = synth->order_count - 1; i >= 0; i-- )
        {
            struct ToriRS_MidiNode* held = &synth->nodes[synth->order[i]];
            int target;

            if( held->channel != channel_index || held->release_time >= 0 )
                continue;
            channel->note_on[held->note] = NULL;
            channel->note_on[note] = held;
            target =
                ((held->portamento_delta * held->portamento_remaining) >> 12) + held->base_pitch;
            held->base_pitch += (note - held->note) << 8;
            held->portamento_delta = target - held->base_pitch;
            held->portamento_remaining = 4096;
            held->note = note;
            return;
        }
    }

    patch = ToriRS_SoundBank_FindPatch(synth->bank, channel->patch_id);
    if( !patch || !patch->patch )
    {
        synth->stats.notes_dropped_no_patch++;
        return;
    }
    sound = ToriRS_SoundBank_NoteSound(synth->bank, patch, note);
    if( !sound )
    {
        synth->stats.notes_dropped_no_sample++;
        return;
    }
    node = node_alloc(synth);
    if( !node )
        return;

    envelope_index = patch->patch->envelope_index[note];
    node->channel = channel_index;
    node->patch = patch;
    node->sound = sound;
    node->envelope = envelope_index >= 0 && envelope_index < patch->patch->envelope_count
                         ? &patch->patch->envelopes[envelope_index]
                         : NULL;
    node->group = patch->patch->note_group[note];
    node->note = note;
    /* Velocity is squared, so the response is perceptual rather than linear. */
    node->volume =
        (patch->patch->volume_scale * velocity * velocity * patch->patch->volume[note] + 1024) >>
        11;
    node->pan = patch->patch->pan[note] & 255;
    node->base_pitch = (note << 8) - (patch->patch->pitch_offset[note] & 32767);
    node->portamento_remaining = 0;
    node->portamento_delta = 0;
    node->release_time = -1;
    node->step_countdown = synth->sample_rate / 100;

    looping = patch->patch->pitch_offset[note] < 0;

    ToriRS_PcmVoice_Start(
        &node->voice,
        sound,
        sound->sample_rate,
        synth->sample_rate,
        channel->start_offset == 0 ? node_volume(synth, node) : 0,
        node_pan(synth, node),
        0);
    ToriRS_PcmVoice_SetStep(&node->voice, node_step(synth, node));
    if( channel->start_offset != 0 )
        node_apply_start_offset(synth, node, looping);
    if( looping )
        node->voice.loops_left = -1;

    if( node->group >= 0 && node->group < 128 )
    {
        struct ToriRS_MidiNode* previous = channel->group[node->group];
        if( previous && previous->release_time < 0 )
        {
            channel->note_on[previous->note] = NULL;
            previous->release_time = 0;
        }
        channel->group[node->group] = node;
    }
    channel->note_on[note] = node;
    synth->stats.notes_started++;
}

/* --- MIDI dispatch -------------------------------------------------------- */

static void
dispatch_controller(
    struct ToriRS_MidiSynth* synth,
    int channel_index,
    int controller,
    int value)
{
    struct ToriRS_MidiChannel* channel = &synth->channels[channel_index];

    switch( controller )
    {
    case 0: /* bank select MSB */
        channel->bank = (value << 14) + (channel->bank & ~2080768);
        break;
    case 32: /* bank select LSB */
        channel->bank = (value << 7) + (channel->bank & ~16256);
        break;
    case 1:
        channel->modulation = (value << 7) + (channel->modulation & ~16256);
        break;
    case 33:
        channel->modulation = (channel->modulation & ~127) + value;
        break;
    case 5:
        channel->portamento_time = (value << 7) + (channel->portamento_time & ~16256);
        break;
    case 37:
        channel->portamento_time = (channel->portamento_time & ~127) + value;
        break;
    case 7:
        channel->volume = (value << 7) + (channel->volume & ~16256);
        break;
    case 39:
        channel->volume = (channel->volume & ~127) + value;
        break;
    case 10:
        channel->pan = (value << 7) + (channel->pan & ~16256);
        break;
    case 42:
        channel->pan = (channel->pan & ~127) + value;
        break;
    case 11:
        channel->expression = (value << 7) + (channel->expression & ~16256);
        break;
    case 43:
        channel->expression = (channel->expression & ~127) + value;
        break;
    case 64: /* sustain */
        if( value >= 64 )
            channel->flags |= 1;
        else
            channel->flags &= ~1;
        break;
    case 65: /* legato / portamento */
        if( value >= 64 )
        {
            channel->flags |= 2;
        }
        else
        {
            channel_sustain_off(synth, channel_index);
            channel->flags &= ~2;
        }
        break;
    case 81: /* retrigger */
        if( value >= 64 )
        {
            channel->flags |= 4;
        }
        else
        {
            channel_retrigger_off(synth, channel_index);
            channel->flags &= ~4;
        }
        break;
    case 99:
        channel->rpn = (value << 7) + (channel->rpn & 127);
        break;
    case 98:
        channel->rpn = (channel->rpn & 16256) + value;
        break;
    case 101:
        channel->rpn = (value << 7) + (channel->rpn & 127) + 16384;
        break;
    case 100:
        channel->rpn = (channel->rpn & 16256) + 16384 + value;
        break;
    case 6: /* data entry MSB — only RPN 0 (bend range) is honoured */
        if( channel->rpn == 16384 )
            channel->bend_range = (value << 7) + (channel->bend_range & ~16256);
        break;
    case 38:
        if( channel->rpn == 16384 )
            channel->bend_range = (channel->bend_range & ~127) + value;
        break;
    case 16:
        channel->start_offset = (value << 7) + (channel->start_offset & ~16256);
        break;
    case 48:
        channel->start_offset = (channel->start_offset & ~127) + value;
        break;
    case 17:
        channel_set_retrigger_rate(channel, (value << 7) + (channel->retrigger_raw & ~16256));
        break;
    case 49:
        channel_set_retrigger_rate(channel, (channel->retrigger_raw & ~127) + value);
        break;
    case 120:
        channel_all_sound_off(synth, channel_index);
        break;
    case 121:
        channel_reset(synth, channel_index);
        break;
    case 123:
        channel_all_notes_off(synth, channel_index);
        break;
    default:
        break;
    }
}

/** Reference method5870: apply one packed channel event. */
static void
dispatch_event(
    struct ToriRS_MidiSynth* synth,
    int packed)
{
    int status = packed & 240;
    int channel_index = packed & 15;

    synth->stats.events_dispatched++;
    switch( status )
    {
    case 0x80:
        channel_note_off(synth, channel_index, (packed >> 8) & 127);
        break;
    case 0x90:
    {
        int note = (packed >> 8) & 127;
        int velocity = (packed >> 16) & 127;
        /*
         * While seeking, run the song's state forward without sounding it.
         * Program changes, controllers and pitch bends must all be applied --
         * arriving at tick N with the wrong instrument selected is the whole
         * failure mode a seek has to avoid -- but the notes that were struck on
         * the way there are in the past and must not all fire at once.
         */
        if( velocity > 0 && !synth->seeking )
            channel_note_on(synth, channel_index, note, velocity);
        else if( velocity == 0 )
            channel_note_off(synth, channel_index, note);
        break;
    }
    case 0xA0: /* polyphonic aftertouch: the reference ignores it */
        break;
    case 0xB0:
        dispatch_controller(synth, channel_index, (packed >> 8) & 127, (packed >> 16) & 127);
        break;
    case 0xC0:
        channel_set_patch(
            synth, channel_index, synth->channels[channel_index].bank + ((packed >> 8) & 127));
        break;
    case 0xD0: /* channel aftertouch: ignored, as in the reference */
        break;
    case 0xE0:
        synth->channels[channel_index].pitch_bend =
            ((packed >> 8) & 127) + (((packed >> 16) & 127) << 7);
        break;
    default:
        if( (packed & 255) == 255 )
        {
            channel_all_sound_off(synth, -1);
            channel_reset(synth, -1);
        }
        break;
    }
}

/* --- lifecycle ------------------------------------------------------------ */

void
ToriRS_MidiSynth_Init(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_SoundBank* bank,
    int sample_rate)
{
    assert(synth);
    memset(synth, 0, sizeof(*synth));
    synth->bank = bank;
    synth->sample_rate = sample_rate > 0 ? sample_rate : 22050;
    synth->master_volume = 256;
    channel_all_sound_off(synth, -1);
    channel_reset(synth, -1);
    for( int i = 0; i < TORIRS_MIDI_CHANNELS; i++ )
    {
        /* Channel 10 is percussion and lives in bank 1. */
        synth->channels[i].bank = i == 9 ? 128 : 0;
        synth->channels[i].patch_id = i == 9 ? 128 : 0;
    }
}

void
ToriRS_MidiSynth_Free(struct ToriRS_MidiSynth* synth)
{
    if( !synth )
        return;
    free(synth->accumulator);
    synth->accumulator = NULL;
    synth->accumulator_frames = 0;
    channel_all_sound_off(synth, -1);
    ToriRS_MidiFile_Close(&synth->file);
    synth->playing = false;
}

void
ToriRS_MidiSynth_Stop(struct ToriRS_MidiSynth* synth)
{
    assert(synth);
    channel_all_sound_off(synth, -1);
    channel_reset(synth, -1);
    for( int i = 0; i < TORIRS_MIDI_CHANNELS; i++ )
    {
        synth->channels[i].bank = i == 9 ? 128 : 0;
        synth->channels[i].patch_id = i == 9 ? 128 : 0;
    }
    ToriRS_MidiFile_Close(&synth->file);
    synth->playing = false;
    synth->position = 0;
    synth->next_event_time = 0;
}

bool
ToriRS_MidiSynth_Play(
    struct ToriRS_MidiSynth* synth,
    const uint8_t* midi,
    int midi_size,
    bool looping)
{
    assert(synth);
    ToriRS_MidiSynth_Stop(synth);
    if( !ToriRS_MidiFile_Open(&synth->file, midi, midi_size) )
        return false;

    synth->looping = looping;
    synth->playing = true;
    synth->position = 0;
    synth->step_per_frame =
        (int)(((int64_t)MIDI_MICROSECONDS * synth->file.division) / synth->sample_rate);
    if( synth->step_per_frame <= 0 )
        synth->step_per_frame = 1;
    synth->current_track = ToriRS_MidiFile_NextTrack(&synth->file);
    if( synth->current_track < 0 )
    {
        synth->playing = false;
        return false;
    }
    synth->current_tick = synth->file.track_tick[synth->current_track];
    synth->next_event_time = ToriRS_MidiFile_TimeAt(&synth->file, synth->current_tick);
    return true;
}

/** Defined below, with the rest of the sequencer. */
static void
dispatch_due_events(struct ToriRS_MidiSynth* synth);

/**
 * Bound on seek iterations.
 *
 * A seek advances one event batch per turn, so a long song legitimately needs
 * thousands. This only has to stop a malformed file whose tick never reaches
 * the target from spinning forever.
 */
#define TORIRS_MIDI_SEEK_GUARD 1000000

bool
ToriRS_MidiSynth_PlayFrom(
    struct ToriRS_MidiSynth* synth,
    const uint8_t* midi,
    int midi_size,
    bool looping,
    int start_tick)
{
    int guard = 0;

    if( !ToriRS_MidiSynth_Play(synth, midi, midi_size, looping) )
        return false;
    if( start_tick <= 0 )
        return true;

    /*
     * Fast-forward the sequencer to `start_tick` without sounding anything.
     *
     * This is what MIDI_SWAP means by swapping "while letting the song play on
     * from where it was, rather than re-starting" -- the secondary is a tonal
     * variant of the same arrangement, so the listener should hear the
     * instrumentation change under a phrase that keeps going, not the phrase
     * start over. Seeking by replaying state-only events is the standard way:
     * there is no index into a MIDI stream, and the channel state at tick N is
     * the accumulation of every controller and program change before it.
     *
     * The guard bounds a malformed file whose tick never reaches the target.
     */
    synth->seeking = true;
    while( synth->playing && synth->current_tick < start_tick )
    {
        if( ++guard > TORIRS_MIDI_SEEK_GUARD )
            break;
        dispatch_due_events(synth);
    }
    synth->seeking = false;
    /* The audio clock has to land where the sequencer did, or the first render
     * would replay the whole span it just skipped. */
    synth->position = synth->next_event_time;
    return true;
}

bool
ToriRS_MidiSynth_Finished(const struct ToriRS_MidiSynth* synth)
{
    assert(synth);
    if( synth->playing )
        return false;
    return synth->order_count == 0;
}

/* --- sequencing ----------------------------------------------------------- */

/** Reference method5912: run every event whose tick has arrived. */
static void
dispatch_due_events(struct ToriRS_MidiSynth* synth)
{
    int track = synth->current_track;
    int tick = synth->current_tick;
    int64_t time = synth->next_event_time;
    const int original_tick = synth->current_tick;

    /* The reference's condition compares against the tick this call *started*
     * at, not against the moving one: every track whose next event shares that
     * tick is drained before the clock moves. */
    while( tick == original_tick )
    {
        while( track >= 0 && synth->file.track_cursor[track] >= 0 &&
               synth->file.track_tick[track] == tick )
        {
            int packed = 0;
            enum ToriRS_MidiEventKind kind =
                ToriRS_MidiFile_ReadEvent(&synth->file, track, &packed);

            if( kind == TORIRS_MIDI_EVENT_END_OF_TRACK || kind == TORIRS_MIDI_EVENT_ERROR )
            {
                if( ToriRS_MidiFile_Finished(&synth->file) )
                {
                    /*
                     * A song that ends at tick 0 has no content; looping it
                     * would spin. The reference guards the same way.
                     */
                    if( !synth->looping || tick == 0 )
                    {
                        channel_all_sound_off(synth, -1);
                        channel_reset(synth, -1);
                        ToriRS_MidiFile_Close(&synth->file);
                        synth->playing = false;
                        return;
                    }
                    ToriRS_MidiFile_Rewind(&synth->file, time);
                    synth->stats.loops++;
                }
                break;
            }
            if( kind == TORIRS_MIDI_EVENT_CHANNEL )
                dispatch_event(synth, packed);
        }

        track = ToriRS_MidiFile_NextTrack(&synth->file);
        if( track < 0 )
        {
            synth->playing = false;
            return;
        }
        tick = synth->file.track_tick[track];
        time = ToriRS_MidiFile_TimeAt(&synth->file, tick);
    }

    synth->current_track = track;
    synth->current_tick = tick;
    synth->next_event_time = time;
}

/* --- per-note stepping ---------------------------------------------------- */

/**
 * Advance one node's envelopes by one 10ms step.
 *
 * Returns true when the node has finished and been released. Mirrors the
 * reference's method5879, including the order: portamento, then pitch, then
 * each envelope, then the decision.
 */
static bool
node_step_envelopes(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_MidiNode* node,
    int32_t* out,
    int offset,
    int frames)
{
    const struct ToriRS_MidiChannel* channel = &synth->channels[node->channel];
    const struct RSCache_MusicPatchEnvelope* envelope = node->envelope;
    double pitch_factor;
    bool finished = false;

    node->step_countdown = synth->sample_rate / 100;

    if( node->release_time >= 0 && (!node->voice.active || ToriRS_PcmVoice_Exhausted(&node->voice)) )
    {
        node_release(synth, node);
        return true;
    }
    if( !node->voice.active )
    {
        node_release(synth, node);
        return true;
    }

    if( node->portamento_remaining > 0 )
    {
        int glide = node->portamento_remaining -
                    (int)(16.0 * pow(2.0, (double)channel->portamento_time * MIDI_PORTAMENTO_BASE) +
                          0.5);
        node->portamento_remaining = glide < 0 ? 0 : glide;
    }
    ToriRS_PcmVoice_SetStep(&node->voice, node_step(synth, node));

    node->age++;
    if( envelope )
        node->vibrato_phase += envelope->vibrato_speed;

    pitch_factor = (double)(((node->note - 60) << 8) +
                            ((node->portamento_delta * node->portamento_remaining) >> 12)) /
                   MIDI_ENVELOPE_PER_OCTAVE;

    if( envelope && envelope->decay > 0 )
    {
        if( envelope->decay_rate > 0 )
            node->decay_time +=
                (int)(128.0 * pow(2.0, (double)envelope->decay_rate * pitch_factor) + 0.5);
        else
            node->decay_time += 128;
    }
    if( envelope && envelope->amplitude )
    {
        if( envelope->amplitude_rate > 0 )
            node->amplitude_time +=
                (int)(128.0 * pow(2.0, (double)envelope->amplitude_rate * pitch_factor) + 0.5);
        else
            node->amplitude_time += 128;
        while( node->amplitude_index < envelope->amplitude_len - 2 &&
               node->amplitude_time > ((envelope->amplitude[node->amplitude_index + 2] & 255) << 8) )
            node->amplitude_index += 2;
        /* Reaching the last breakpoint at level zero means the note has decayed
         * to nothing even though nobody released it. */
        if( node->amplitude_index == envelope->amplitude_len - 2 &&
            envelope->amplitude[node->amplitude_index + 1] == 0 )
            finished = true;
    }
    if( node->release_time >= 0 && envelope && envelope->release && (channel->flags & 1) == 0 &&
        (node->group < 0 || channel->group[node->group] != node) )
    {
        if( envelope->release_rate > 0 )
            node->release_time +=
                (int)(128.0 * pow(2.0, (double)envelope->release_rate * pitch_factor) + 0.5);
        else
            node->release_time += 128;
        while( node->release_index < envelope->release_len - 2 &&
               node->release_time > ((envelope->release[node->release_index + 2] & 255) << 8) )
            node->release_index += 2;
        if( node->release_index == envelope->release_len - 2 )
            finished = true;
    }

    if( finished )
    {
        /* Fade over the step rather than cutting, then let the remainder of the
         * block drain through the fade so the note ends silently. */
        ToriRS_PcmVoice_Stop(&node->voice, node->step_countdown);
        if( out )
            ToriRS_PcmVoice_Mix(&node->voice, out + offset * 2, frames);
        else
            ToriRS_PcmVoice_Skip(&node->voice, frames);
        node_release(synth, node);
        return true;
    }

    ToriRS_PcmVoice_SetGain(
        &node->voice, node_volume(synth, node), node_pan(synth, node), node->step_countdown);
    return false;
}

/**
 * Mix one node across `frames`, stepping its envelopes whenever the 10ms
 * countdown lands inside the block.
 */
static void
node_render(
    struct ToriRS_MidiSynth* synth,
    struct ToriRS_MidiNode* node,
    int32_t* out,
    int frames)
{
    int offset = 0;
    int remaining = frames;

    while( remaining > 0 )
    {
        int chunk = remaining <= node->step_countdown ? remaining : node->step_countdown;

        if( chunk > 0 )
        {
            if( out )
                ToriRS_PcmVoice_Mix(&node->voice, out + offset * 2, chunk);
            else
                ToriRS_PcmVoice_Skip(&node->voice, chunk);
            offset += chunk;
            remaining -= chunk;
            node->step_countdown -= chunk;
        }
        if( !node->voice.active )
        {
            node_release(synth, node);
            return;
        }
        if( node->step_countdown > 0 )
            return;
        if( node_step_envelopes(synth, node, out, offset, remaining) )
            return;
    }
}

static bool
ensure_accumulator(
    struct ToriRS_MidiSynth* synth,
    int frames)
{
    int32_t* grown;

    if( synth->accumulator_frames >= frames )
        return true;
    grown = realloc(synth->accumulator, (size_t)frames * 2 * sizeof(int32_t));
    if( !grown )
        return false;
    synth->accumulator = grown;
    synth->accumulator_frames = frames;
    return true;
}

/** Mix every live node for `frames`, into `out` when there is one. */
static void
render_nodes(
    struct ToriRS_MidiSynth* synth,
    int32_t* out,
    int frames)
{
    /* Nodes can end and be removed mid-walk, so iterate over a snapshot of the
     * order rather than the live array. */
    int snapshot[TORIRS_MIDI_MAX_NODES];
    int count = synth->order_count;

    if( frames <= 0 )
        return;
    memcpy(snapshot, synth->order, (size_t)count * sizeof(int));
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_MidiNode* node = &synth->nodes[snapshot[i]];
        if( !node->active )
            continue;
        node_render(synth, node, out, frames);
    }
}

void
ToriRS_MidiSynth_Render(
    struct ToriRS_MidiSynth* synth,
    int16_t* out,
    int frames)
{
    int32_t* accumulator;
    int written = 0;
    /*
     * A block can contain many events, and several can share one frame. The
     * guard bounds the dispatch loop so a reader that stops advancing (a
     * malformed track, a zero-tick loop) costs a frame of silence rather than
     * a hang -- which is exactly the failure an audio path must not have.
     */
    int guard = 0;
    const int guard_limit = 1 << 16;

    if( frames <= 0 )
        return;
    assert(synth);
    assert(out);
    memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
    if( !ensure_accumulator(synth, frames) )
        return;
    accumulator = synth->accumulator;
    memset(accumulator, 0, (size_t)frames * 2 * sizeof(int32_t));

    while( written < frames )
    {
        int chunk = frames - written;

        if( ++guard > guard_limit )
        {
            synth->playing = false;
            render_nodes(synth, accumulator + written * 2, chunk);
            written += chunk;
            break;
        }

        if( synth->playing && ToriRS_MidiFile_IsOpen(&synth->file) )
        {
            int64_t candidate = (int64_t)chunk * synth->step_per_frame + synth->position;

            if( synth->next_event_time - candidate >= 0 )
            {
                synth->position = candidate;
            }
            else
            {
                /* Render exactly up to the event, then dispatch it. */
                int64_t gap = synth->next_event_time - synth->position;
                int until = (int)((gap + synth->step_per_frame - 1) / synth->step_per_frame);
                if( until < 0 )
                    until = 0;
                if( until > chunk )
                    until = chunk;
                synth->position += (int64_t)synth->step_per_frame * until;
                render_nodes(synth, accumulator + written * 2, until);
                written += until;
                dispatch_due_events(synth);
                if( until == 0 && !synth->playing )
                    break;
                if( until == 0 )
                {
                    /* Several events share this frame: keep dispatching without
                     * advancing, but do not spin if the reader stalled. */
                    if( synth->next_event_time - synth->position < 0 &&
                        ToriRS_MidiFile_NextTrack(&synth->file) < 0 )
                        synth->playing = false;
                }
                continue;
            }
        }
        render_nodes(synth, accumulator + written * 2, chunk);
        written += chunk;
    }

    for( int i = 0; i < frames * 2; i++ )
    {
        int32_t value = accumulator[i];
        int32_t magnitude = value < 0 ? -value : value;

        /*
         * Record how far the sum ran past full scale before the clamp took it.
         * The reference clamps in exactly the same place and to the same range
         * (class477.method468: `8388607 ^ var3 >> 31`, then bits 8..23), so a
         * track that clips here clips there too -- what this number answers is
         * the different question of *by how much*. A dense arrangement peaking a
         * few percent over is the material; a mix running several times over is
         * a gain-staging fault, and the peak alone cannot tell those apart once
         * the clamp has flattened both to 32767.
         */
        if( magnitude > synth->stats.unclamped_peak )
            synth->stats.unclamped_peak = magnitude;
        if( value > 32767 )
            value = 32767;
        if( value < -32768 )
            value = -32768;
        out[i] = (int16_t)value;
    }
}
