#include "music_song.h"
#include "../rscache_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * A two-pass port of the reference's Song constructor.
 *
 * Pass one walks the opcode stream counting events by kind, which is the only
 * way to know where each field's run starts. Pass two walks it again, pulling
 * one byte from each run as it re-emits a Standard MIDI File. The reference
 * computes the exact output size in pass one; this keeps that and treats a
 * write past it as a decode failure, because the alternative -- growing the
 * buffer -- would hide a counting mistake that is otherwise caught immediately.
 *
 * Event kinds, low nibble of the opcode byte:
 *
 *   0 note on        1 note off       2 controller     3 pitch bend
 *   4 channel aftertouch              5 poly aftertouch
 *   6 program change 7 end of track   23 (whole byte) set tempo
 *
 * The high nibble is a channel *delta*, XORed into the running channel. A
 * status byte is emitted only when the opcode byte differs from the previous
 * one -- that is MIDI running status, reconstructed rather than stored.
 */

#define MIDI_HEADER_MAGIC 1297377380 /* "MThd" */
#define MIDI_TRACK_MAGIC 1297379947  /* "MTrk" */

struct song_reader
{
    const uint8_t* data;
    int size;
    int pos;
    bool overrun;
};

static int
sr_u8(struct song_reader* r)
{
    if( r->pos < 0 || r->pos >= r->size )
    {
        r->overrun = true;
        return 0;
    }
    return r->data[r->pos++];
}

static int
sr_s8(struct song_reader* r)
{
    return (int8_t)sr_u8(r);
}

static int
sr_u16(struct song_reader* r)
{
    int high = sr_u8(r);
    return (high << 8) | sr_u8(r);
}

static int
sr_varint(struct song_reader* r)
{
    int value = 0;
    int byte = sr_s8(r);
    int guard = 0;

    while( byte < 0 )
    {
        value = (value | (byte & 127)) << 7;
        byte = sr_s8(r);
        if( r->overrun || ++guard > 5 )
            return value;
    }
    return value | byte;
}

/** Byte at an absolute offset, signed, with a bound check. */
static int
sr_at_s8(
    struct song_reader* r,
    int* cursor)
{
    if( *cursor < 0 || *cursor >= r->size )
    {
        r->overrun = true;
        return 0;
    }
    return (int8_t)r->data[(*cursor)++];
}

struct song_writer
{
    uint8_t* data;
    int size;
    int pos;
    bool overflow;
};

static void
sw_u8(
    struct song_writer* w,
    int value)
{
    if( w->pos < 0 || w->pos >= w->size )
    {
        w->overflow = true;
        return;
    }
    w->data[w->pos++] = (uint8_t)value;
}

static void
sw_u16(
    struct song_writer* w,
    int value)
{
    sw_u8(w, value >> 8);
    sw_u8(w, value);
}

static void
sw_u32(
    struct song_writer* w,
    int value)
{
    sw_u8(w, value >> 24);
    sw_u8(w, value >> 16);
    sw_u8(w, value >> 8);
    sw_u8(w, value);
}

static void
sw_varint(
    struct song_writer* w,
    int value)
{
    if( (value & ~0x7F) != 0 )
    {
        if( (value & ~0x3FFF) != 0 )
        {
            if( (value & ~0x1FFFFF) != 0 )
            {
                if( (value & ~0x0FFFFFFF) != 0 )
                    sw_u8(w, (int)((uint32_t)value >> 28) | 128);
                sw_u8(w, (int)((uint32_t)value >> 21) | 128);
            }
            sw_u8(w, (int)((uint32_t)value >> 14) | 128);
        }
        sw_u8(w, (int)((uint32_t)value >> 7) | 128);
    }
    sw_u8(w, value & 127);
}

/** Back-patch a track's length into the four bytes before its body. */
static void
sw_patch_length(
    struct song_writer* w,
    int length)
{
    int at = w->pos - length - 4;

    if( length < 0 || at < 0 || at + 4 > w->size )
    {
        w->overflow = true;
        return;
    }
    w->data[at] = (uint8_t)(length >> 24);
    w->data[at + 1] = (uint8_t)(length >> 16);
    w->data[at + 2] = (uint8_t)(length >> 8);
    w->data[at + 3] = (uint8_t)length;
}

/** Find (or append) the manifest entry for a banked program. */
static struct RSCache_MusicSongPatch*
song_patch_for(
    struct RSCache_MusicSong* song,
    int patch_id,
    int tick,
    int* capacity)
{
    struct RSCache_MusicSongPatch* grown;

    for( int i = 0; i < song->patch_count; i++ )
    {
        if( song->patches[i].patch_id == patch_id )
            return &song->patches[i];
    }
    if( song->patch_count >= *capacity )
    {
        int next = *capacity > 0 ? *capacity * 2 : 16;
        grown = RSCACHE_REALLOC(song->patches, (size_t)next * sizeof(*song->patches));
        if( !grown )
            return NULL;
        song->patches = grown;
        *capacity = next;
    }
    grown = &song->patches[song->patch_count++];
    memset(grown, 0, sizeof(*grown));
    grown->patch_id = patch_id;
    grown->first_tick = tick;
    return grown;
}

void
RSCache_MusicSongFree(struct RSCache_MusicSong* song)
{
    if( !song )
        return;
    RSCACHE_FREE(song->midi);
    RSCACHE_FREE(song->patches);
    RSCACHE_FREE(song);
}

/*
 * The decode in steps (RSCache_MusicSongDecodeNew). What the reference's
 * constructor keeps in locals across its passes is kept here, and a step does
 * a bounded number of UNITS -- one opcode counted, one delta skipped, one
 * controller counted, one event re-emitted -- so a large track (a few
 * milliseconds in one piece, natively) is spread over a background job's
 * slices. The passes, their order and every byte written are the one-call
 * decode's: that decode is these steps with an unbounded budget.
 */
enum song_phase
{
    SONG_PHASE_COUNT = 0,   /* pass one: count events by kind */
    SONG_PHASE_DELTAS,      /* skip the delta stream */
    SONG_PHASE_CONTROLLERS, /* count controller numbers */
    SONG_PHASE_EMIT,        /* pass two: re-emit as a Standard MIDI File */
    SONG_PHASE_DONE,
    SONG_PHASE_FAILED,
};

struct RSCache_MusicSongDecode
{
    enum song_phase phase;
    struct song_reader reader; /* borrows the caller's bytes */
    struct song_writer writer;
    struct RSCache_MusicSong* song;
    int patch_capacity;

    int track_count;
    int division;
    int header_bytes;

    int count_tempo;
    int count_controller;
    int count_note_on;
    int count_note_off;
    int count_pitch_bend;
    int count_channel_pressure;
    int count_poly_pressure;
    int count_program;

    int count_modwheel_msb;
    int count_modwheel_lsb;
    int count_volume_msb;
    int count_volume_lsb;
    int count_pan_msb;
    int count_pan_lsb;
    int count_nrpn_msb;
    int count_nrpn_lsb;
    int count_rpn_msb;
    int count_rpn_lsb;
    int count_switch;
    int count_other_controller;

    int total_bytes;
    int delta_stream;
    int delta_count;
    int controller_stream;

    int opcode_cursor;
    int cursor_switch;
    int cursor_poly_pressure;
    int cursor_channel_pressure;
    int cursor_bend_msb;
    int cursor_modwheel_msb;
    int cursor_volume_msb;
    int cursor_pan_msb;
    int cursor_note;
    int cursor_note_on_velocity;
    int cursor_other_controller;
    int cursor_note_off_velocity;
    int cursor_modwheel_lsb;
    int cursor_volume_lsb;
    int cursor_pan_lsb;
    int cursor_program;
    int cursor_bend_lsb;
    int cursor_nrpn_msb;
    int cursor_nrpn_lsb;
    int cursor_rpn_msb;
    int cursor_rpn_lsb;
    int cursor_tempo;

    int channel;
    int running_note;
    int running_note_on_velocity;
    int running_note_off_velocity;
    int running_bend;
    int running_channel_pressure;
    int running_poly_pressure;
    int running_controller;
    int controller_value[128];
    int channel_bank[16];
    int channel_patch[16];

    /* Where the walk is: the track, the previous opcode, the index into the
     * delta or controller stream, the controller being accumulated, and (pass
     * two) whether a track's header is out, where its body starts, its tick. */
    int track;
    int previous;
    int index;
    int controller;
    bool in_track;
    int body_start;
    int tick;
};

void
RSCache_MusicSongDecodeFree(struct RSCache_MusicSongDecode* decode)
{
    if( !decode )
        return;
    RSCache_MusicSongFree(decode->song);
    RSCACHE_FREE(decode);
}

struct RSCache_MusicSongDecode*
RSCache_MusicSongDecodeNew(
    const char* data,
    int data_size)
{
    struct RSCache_MusicSongDecode* d;

    if( !data || data_size < 4 )
        return NULL;
    d = RSCACHE_CALLOC(1, sizeof(*d));
    if( !d )
        return NULL;
    d->reader.data = (const uint8_t*)data;
    d->reader.size = data_size;
    d->reader.overrun = false;

    /* The header is at the tail: track count then division. */
    d->reader.pos = data_size - 3;
    d->track_count = sr_u8(&d->reader);
    d->division = sr_u16(&d->reader);
    if( d->track_count <= 0 || d->track_count > 64 || d->reader.overrun )
    {
        RSCACHE_FREE(d);
        return NULL;
    }
    d->header_bytes = d->track_count * 10 + 14;

    d->reader.pos = 0;
    d->phase = SONG_PHASE_COUNT;
    d->track = 0;
    d->previous = -1;
    return d;
}

static void
song_fail(struct RSCache_MusicSongDecode* d)
{
    d->phase = SONG_PHASE_FAILED;
}

/* Pass one, one opcode. */
static void
song_count_one(struct RSCache_MusicSongDecode* d)
{
    struct song_reader* reader = &d->reader;
    int opcode = sr_u8(reader);
    int kind;

    if( reader->overrun )
    {
        song_fail(d);
        return;
    }
    if( d->previous != opcode )
        d->header_bytes++;
    kind = opcode & 15;
    d->previous = kind;
    if( opcode == 7 )
    {
        d->track++;
        d->previous = -1;
        if( d->track < d->track_count )
            return;

        d->total_bytes = d->count_tempo * 5 + d->header_bytes;
        d->total_bytes = (d->count_note_on + d->count_note_off + d->count_controller +
                          d->count_pitch_bend + d->count_poly_pressure) *
                             2 +
                         d->total_bytes;
        d->total_bytes = d->count_channel_pressure + d->count_program + d->total_bytes;
        d->delta_stream = reader->pos;
        d->delta_count = d->track_count + d->count_tempo + d->count_controller +
                         d->count_note_on + d->count_note_off + d->count_pitch_bend +
                         d->count_channel_pressure + d->count_poly_pressure + d->count_program;
        d->index = 0;
        d->phase = SONG_PHASE_DELTAS;
        return;
    }
    if( opcode == 23 )
        d->count_tempo++;
    else if( kind == 0 )
        d->count_note_on++;
    else if( kind == 1 )
        d->count_note_off++;
    else if( kind == 2 )
        d->count_controller++;
    else if( kind == 3 )
        d->count_pitch_bend++;
    else if( kind == 4 )
        d->count_channel_pressure++;
    else if( kind == 5 )
        d->count_poly_pressure++;
    else if( kind == 6 )
        d->count_program++;
    else
        song_fail(d);
}

/* The delta stream, one varint (or, past its end, the move to the
 * controller count). */
static void
song_delta_one(struct RSCache_MusicSongDecode* d)
{
    if( d->index < d->delta_count )
    {
        sr_varint(&d->reader);
        d->index++;
        return;
    }
    if( d->reader.overrun )
    {
        song_fail(d);
        return;
    }
    d->total_bytes = d->reader.pos - d->delta_stream + d->total_bytes;
    d->controller_stream = d->reader.pos;
    d->index = 0;
    d->controller = 0;
    d->phase = SONG_PHASE_CONTROLLERS;
}

/* Place the value runs, size and start the output: the step between the
 * passes, all of it bounded by the run count. False: malformed. */
static bool
song_place_runs(struct RSCache_MusicSongDecode* d)
{
    struct song_reader* reader = &d->reader;
    struct song_writer* writer = &d->writer;
    struct RSCache_MusicSong* song;

#define TAKE_RUN(name, length)                                                                     \
    do                                                                                             \
    {                                                                                              \
        (name) = reader->pos;                                                                      \
        reader->pos += (length);                                                                   \
        if( reader->pos > reader->size )                                                           \
            return false;                                                                          \
    } while( 0 )

    TAKE_RUN(d->cursor_switch, d->count_switch);
    TAKE_RUN(d->cursor_poly_pressure, d->count_poly_pressure);
    TAKE_RUN(d->cursor_channel_pressure, d->count_channel_pressure);
    TAKE_RUN(d->cursor_bend_msb, d->count_pitch_bend);
    TAKE_RUN(d->cursor_modwheel_msb, d->count_modwheel_msb);
    TAKE_RUN(d->cursor_volume_msb, d->count_volume_msb);
    TAKE_RUN(d->cursor_pan_msb, d->count_pan_msb);
    TAKE_RUN(d->cursor_note, d->count_note_on + d->count_note_off + d->count_poly_pressure);
    TAKE_RUN(d->cursor_note_on_velocity, d->count_note_on);
    TAKE_RUN(d->cursor_other_controller, d->count_other_controller);
    TAKE_RUN(d->cursor_note_off_velocity, d->count_note_off);
    TAKE_RUN(d->cursor_modwheel_lsb, d->count_modwheel_lsb);
    TAKE_RUN(d->cursor_volume_lsb, d->count_volume_lsb);
    TAKE_RUN(d->cursor_pan_lsb, d->count_pan_lsb);
    TAKE_RUN(d->cursor_program, d->count_program);
    TAKE_RUN(d->cursor_bend_lsb, d->count_pitch_bend);
    TAKE_RUN(d->cursor_nrpn_msb, d->count_nrpn_msb);
    TAKE_RUN(d->cursor_nrpn_lsb, d->count_nrpn_lsb);
    TAKE_RUN(d->cursor_rpn_msb, d->count_rpn_msb);
    TAKE_RUN(d->cursor_rpn_lsb, d->count_rpn_lsb);
    TAKE_RUN(d->cursor_tempo, d->count_tempo * 3);
#undef TAKE_RUN

    if( d->total_bytes <= 0 || d->total_bytes > (1 << 26) )
        return false;

    song = RSCACHE_CALLOC(1, sizeof(*song));
    if( !song )
        return false;
    song->track_count = d->track_count;
    song->division = d->division;
    song->midi = RSCACHE_CALLOC((size_t)d->total_bytes, 1);
    if( !song->midi )
    {
        RSCACHE_FREE(song);
        return false;
    }
    song->midi_size = d->total_bytes;
    d->song = song;

    writer->data = song->midi;
    writer->size = d->total_bytes;
    writer->pos = 0;
    writer->overflow = false;

    sw_u32(writer, MIDI_HEADER_MAGIC);
    sw_u32(writer, 6);
    sw_u16(writer, d->track_count > 1 ? 1 : 0);
    sw_u16(writer, d->track_count);
    sw_u16(writer, d->division);

    reader->pos = d->delta_stream;
    memset(d->controller_value, 0, sizeof(d->controller_value));
    memset(d->channel_bank, 0, sizeof(d->channel_bank));
    memset(d->channel_patch, 0, sizeof(d->channel_patch));
    /* MIDI channel 10 is percussion and starts in bank 1 (patch id 128). */
    d->channel_bank[9] = 128;
    d->channel_patch[9] = 128;
    d->track = 0;
    d->in_track = false;
    return true;
}

/* The controller numbers, one (or, past their end, placing the runs). */
static void
song_controller_one(struct RSCache_MusicSongDecode* d)
{
    if( d->index < d->count_controller )
    {
        int controller = (d->controller + sr_u8(&d->reader)) & 127;

        /* Controller numbers are delta-coded and decide which value run each
         * event draws from, so they have to be counted before the runs can
         * be placed. */
        d->controller = controller;
        d->index++;
        if( controller == 0 || controller == 32 )
            d->count_program++;
        else if( controller == 1 )
            d->count_modwheel_msb++;
        else if( controller == 33 )
            d->count_modwheel_lsb++;
        else if( controller == 7 )
            d->count_volume_msb++;
        else if( controller == 39 )
            d->count_volume_lsb++;
        else if( controller == 10 )
            d->count_pan_msb++;
        else if( controller == 42 )
            d->count_pan_lsb++;
        else if( controller == 99 )
            d->count_nrpn_msb++;
        else if( controller == 98 )
            d->count_nrpn_lsb++;
        else if( controller == 101 )
            d->count_rpn_msb++;
        else if( controller == 100 )
            d->count_rpn_lsb++;
        else if( controller == 64 || controller == 65 || controller == 120 || controller == 121 ||
                 controller == 123 )
            d->count_switch++;
        else
            d->count_other_controller++;
        return;
    }
    if( d->reader.overrun || !song_place_runs(d) )
    {
        song_fail(d);
        return;
    }
    d->phase = SONG_PHASE_EMIT;
}

/* The end of pass two: every track written, and exactly the bytes counted
 * (the reference sizes the output exactly; a short write means a miscount). */
static void
song_finish(struct RSCache_MusicSongDecode* d)
{
    if( d->reader.overrun || d->writer.overflow || d->writer.pos != d->total_bytes )
    {
        song_fail(d);
        return;
    }
    d->phase = SONG_PHASE_DONE;
}

/* Pass two, one event (after a track's header when one starts). */
static void
song_emit_one(struct RSCache_MusicSongDecode* d)
{
    struct song_reader* reader = &d->reader;
    struct song_writer* writer = &d->writer;
    struct RSCache_MusicSong* song = d->song;
    int delta;
    int opcode;
    bool status_changed;
    int kind;

    if( !d->in_track )
    {
        sw_u32(writer, MIDI_TRACK_MAGIC);
        writer->pos += 4; /* length, back-patched at end of track */
        if( writer->pos > writer->size )
        {
            writer->overflow = true;
            song_fail(d);
            return;
        }
        d->body_start = writer->pos;
        d->tick = d->body_start;
        d->previous = -1;
        d->in_track = true;
    }

    delta = sr_varint(reader);
    sw_varint(writer, delta);
    d->tick += delta;
    if( d->opcode_cursor < 0 || d->opcode_cursor >= reader->size )
    {
        song_fail(d);
        return;
    }
    opcode = reader->data[d->opcode_cursor++] & 255;
    status_changed = d->previous != opcode;
    kind = opcode & 15;
    d->previous = kind;

    if( opcode == 7 )
    {
        if( status_changed )
            sw_u8(writer, 255);
        sw_u8(writer, 47);
        sw_u8(writer, 0);
        sw_patch_length(writer, writer->pos - d->body_start);
        if( reader->overrun || writer->overflow )
        {
            song_fail(d);
            return;
        }
        d->in_track = false;
        d->track++;
        if( d->track >= d->track_count )
            song_finish(d);
        return;
    }
    if( opcode == 23 )
    {
        if( status_changed )
            sw_u8(writer, 255);
        sw_u8(writer, 81);
        sw_u8(writer, 3);
        sw_u8(writer, sr_at_s8(reader, &d->cursor_tempo));
        sw_u8(writer, sr_at_s8(reader, &d->cursor_tempo));
        sw_u8(writer, sr_at_s8(reader, &d->cursor_tempo));
    }
    else
    {
        d->channel ^= opcode >> 4;
        d->channel &= 15;
        if( kind == 0 )
        {
            int note;
            int velocity;
            if( status_changed )
                sw_u8(writer, d->channel + 144);
            d->running_note += sr_at_s8(reader, &d->cursor_note);
            d->running_note_on_velocity += sr_at_s8(reader, &d->cursor_note_on_velocity);
            note = d->running_note & 127;
            velocity = d->running_note_on_velocity & 127;
            sw_u8(writer, note);
            sw_u8(writer, velocity);
            if( velocity > 0 )
            {
                struct RSCache_MusicSongPatch* entry = song_patch_for(
                    song, d->channel_patch[d->channel], d->tick, &d->patch_capacity);
                if( !entry )
                {
                    song_fail(d);
                    return;
                }
                entry->notes[note >> 3] |= (uint8_t)(1u << (note & 7));
            }
        }
        else if( kind == 1 )
        {
            if( status_changed )
                sw_u8(writer, d->channel + 128);
            d->running_note += sr_at_s8(reader, &d->cursor_note);
            d->running_note_off_velocity += sr_at_s8(reader, &d->cursor_note_off_velocity);
            sw_u8(writer, d->running_note & 127);
            sw_u8(writer, d->running_note_off_velocity & 127);
        }
        else if( kind == 2 )
        {
            int value;
            int accumulated;
            int masked;
            int rc;

            if( status_changed )
                sw_u8(writer, d->channel + 176);
            d->running_controller =
                (d->running_controller + sr_at_s8(reader, &d->controller_stream)) & 127;
            rc = d->running_controller;
            sw_u8(writer, rc);
            if( rc == 0 || rc == 32 )
                value = sr_at_s8(reader, &d->cursor_program);
            else if( rc == 1 )
                value = sr_at_s8(reader, &d->cursor_modwheel_msb);
            else if( rc == 33 )
                value = sr_at_s8(reader, &d->cursor_modwheel_lsb);
            else if( rc == 7 )
                value = sr_at_s8(reader, &d->cursor_volume_msb);
            else if( rc == 39 )
                value = sr_at_s8(reader, &d->cursor_volume_lsb);
            else if( rc == 10 )
                value = sr_at_s8(reader, &d->cursor_pan_msb);
            else if( rc == 42 )
                value = sr_at_s8(reader, &d->cursor_pan_lsb);
            else if( rc == 99 )
                value = sr_at_s8(reader, &d->cursor_nrpn_msb);
            else if( rc == 98 )
                value = sr_at_s8(reader, &d->cursor_nrpn_lsb);
            else if( rc == 101 )
                value = sr_at_s8(reader, &d->cursor_rpn_msb);
            else if( rc == 100 )
                value = sr_at_s8(reader, &d->cursor_rpn_lsb);
            else if( rc == 64 || rc == 65 || rc == 120 || rc == 121 || rc == 123 )
                value = sr_at_s8(reader, &d->cursor_switch);
            else
                value = sr_at_s8(reader, &d->cursor_other_controller);

            accumulated = d->controller_value[rc] + value;
            d->controller_value[rc] = accumulated;
            masked = accumulated & 127;
            sw_u8(writer, masked);
            if( rc == 0 )
                d->channel_bank[d->channel] =
                    (masked << 14) + (d->channel_bank[d->channel] & ~2080768);
            if( rc == 32 )
                d->channel_bank[d->channel] = (masked << 7) + (d->channel_bank[d->channel] & ~16256);
        }
        else if( kind == 3 )
        {
            int low;
            if( status_changed )
                sw_u8(writer, d->channel + 224);
            low = d->running_bend + sr_at_s8(reader, &d->cursor_bend_lsb);
            d->running_bend = low + (sr_at_s8(reader, &d->cursor_bend_msb) << 7);
            sw_u8(writer, d->running_bend & 127);
            sw_u8(writer, (d->running_bend >> 7) & 127);
        }
        else if( kind == 4 )
        {
            if( status_changed )
                sw_u8(writer, d->channel + 208);
            d->running_channel_pressure += sr_at_s8(reader, &d->cursor_channel_pressure);
            sw_u8(writer, d->running_channel_pressure & 127);
        }
        else if( kind == 5 )
        {
            if( status_changed )
                sw_u8(writer, d->channel + 160);
            d->running_note += sr_at_s8(reader, &d->cursor_note);
            d->running_poly_pressure += sr_at_s8(reader, &d->cursor_poly_pressure);
            sw_u8(writer, d->running_note & 127);
            sw_u8(writer, d->running_poly_pressure & 127);
        }
        else if( kind == 6 )
        {
            int program;
            if( status_changed )
                sw_u8(writer, d->channel + 192);
            program = sr_at_s8(reader, &d->cursor_program);
            d->channel_patch[d->channel] = d->channel_bank[d->channel] + program;
            sw_u8(writer, program);
        }
        else
        {
            song_fail(d);
            return;
        }
    }
    if( reader->overrun || writer->overflow )
        song_fail(d);
}

bool
RSCache_MusicSongDecodeStep(
    struct RSCache_MusicSongDecode* decode,
    int unit_budget)
{
    assert(decode);
    assert(unit_budget > 0);
    for( int units = 0; units < unit_budget; units++ )
    {
        if( decode->phase == SONG_PHASE_COUNT )
            song_count_one(decode);
        else if( decode->phase == SONG_PHASE_DELTAS )
            song_delta_one(decode);
        else if( decode->phase == SONG_PHASE_CONTROLLERS )
            song_controller_one(decode);
        else if( decode->phase == SONG_PHASE_EMIT )
            song_emit_one(decode);
        else
            break;
    }
    return decode->phase == SONG_PHASE_DONE || decode->phase == SONG_PHASE_FAILED;
}

struct RSCache_MusicSong*
RSCache_MusicSongDecodeTake(struct RSCache_MusicSongDecode* decode)
{
    struct RSCache_MusicSong* song;

    assert(decode);
    assert(decode->phase == SONG_PHASE_DONE || decode->phase == SONG_PHASE_FAILED);
    if( decode->phase != SONG_PHASE_DONE )
        return NULL;
    song = decode->song;
    decode->song = NULL;
    return song;
}

struct RSCache_MusicSong*
RSCache_MusicSongNewDecode(
    const char* data,
    int data_size)
{
    struct RSCache_MusicSongDecode* decode = RSCache_MusicSongDecodeNew(data, data_size);
    struct RSCache_MusicSong* song;

    if( !decode )
        return NULL;
    while( !RSCache_MusicSongDecodeStep(decode, 1 << 30) )
        ;
    song = RSCache_MusicSongDecodeTake(decode);
    RSCache_MusicSongDecodeFree(decode);
    return song;
}
