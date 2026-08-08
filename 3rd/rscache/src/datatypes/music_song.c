#include "music_song.h"

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
        grown = realloc(song->patches, (size_t)next * sizeof(*song->patches));
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
    free(song->midi);
    free(song->patches);
    free(song);
}

struct RSCache_MusicSong*
RSCache_MusicSongNewDecode(
    const char* data,
    int data_size)
{
    struct RSCache_MusicSong* song;
    struct song_reader reader;
    struct song_writer writer;
    int patch_capacity = 0;

    int track_count;
    int division;
    int header_bytes;

    int count_tempo = 0;
    int count_controller = 0;
    int count_note_on = 0;
    int count_note_off = 0;
    int count_pitch_bend = 0;
    int count_channel_pressure = 0;
    int count_poly_pressure = 0;
    int count_program = 0;

    int count_modwheel_msb = 0;
    int count_modwheel_lsb = 0;
    int count_volume_msb = 0;
    int count_volume_lsb = 0;
    int count_pan_msb = 0;
    int count_pan_lsb = 0;
    int count_nrpn_msb = 0;
    int count_nrpn_lsb = 0;
    int count_rpn_msb = 0;
    int count_rpn_lsb = 0;
    int count_switch = 0;
    int count_other_controller = 0;

    int total_bytes;
    int delta_stream;
    int controller_stream;

    int opcode_cursor = 0;
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

    int channel = 0;
    int running_note = 0;
    int running_note_on_velocity = 0;
    int running_note_off_velocity = 0;
    int running_bend = 0;
    int running_channel_pressure = 0;
    int running_poly_pressure = 0;
    int running_controller = 0;
    int controller_value[128];
    int channel_bank[16];
    int channel_patch[16];

    if( !data || data_size < 4 )
        return NULL;

    reader.data = (const uint8_t*)data;
    reader.size = data_size;
    reader.overrun = false;

    /* The header is at the tail: track count then division. */
    reader.pos = data_size - 3;
    track_count = sr_u8(&reader);
    division = sr_u16(&reader);
    if( track_count <= 0 || track_count > 64 || reader.overrun )
        return NULL;
    header_bytes = track_count * 10 + 14;

    /* Pass one: count events by kind. */
    reader.pos = 0;
    for( int track = 0; track < track_count; track++ )
    {
        int previous = -1;
        for( ;; )
        {
            int opcode = sr_u8(&reader);
            int kind;

            if( reader.overrun )
                return NULL;
            if( previous != opcode )
                header_bytes++;
            kind = opcode & 15;
            previous = kind;
            if( opcode == 7 )
                break;
            if( opcode == 23 )
                count_tempo++;
            else if( kind == 0 )
                count_note_on++;
            else if( kind == 1 )
                count_note_off++;
            else if( kind == 2 )
                count_controller++;
            else if( kind == 3 )
                count_pitch_bend++;
            else if( kind == 4 )
                count_channel_pressure++;
            else if( kind == 5 )
                count_poly_pressure++;
            else if( kind == 6 )
                count_program++;
            else
                return NULL;
        }
    }

    total_bytes = count_tempo * 5 + header_bytes;
    total_bytes = (count_note_on + count_note_off + count_controller + count_pitch_bend +
                   count_poly_pressure) *
                      2 +
                  total_bytes;
    total_bytes = count_channel_pressure + count_program + total_bytes;

    delta_stream = reader.pos;
    {
        int delta_count = track_count + count_tempo + count_controller + count_note_on +
                          count_note_off + count_pitch_bend + count_channel_pressure +
                          count_poly_pressure + count_program;
        for( int i = 0; i < delta_count; i++ )
            sr_varint(&reader);
        if( reader.overrun )
            return NULL;
    }
    total_bytes = reader.pos - delta_stream + total_bytes;
    controller_stream = reader.pos;

    /* Controller numbers are delta-coded and decide which value run each event
     * draws from, so they have to be counted before the runs can be placed. */
    {
        int controller = 0;
        for( int i = 0; i < count_controller; i++ )
        {
            controller = (controller + sr_u8(&reader)) & 127;
            if( controller == 0 || controller == 32 )
                count_program++;
            else if( controller == 1 )
                count_modwheel_msb++;
            else if( controller == 33 )
                count_modwheel_lsb++;
            else if( controller == 7 )
                count_volume_msb++;
            else if( controller == 39 )
                count_volume_lsb++;
            else if( controller == 10 )
                count_pan_msb++;
            else if( controller == 42 )
                count_pan_lsb++;
            else if( controller == 99 )
                count_nrpn_msb++;
            else if( controller == 98 )
                count_nrpn_lsb++;
            else if( controller == 101 )
                count_rpn_msb++;
            else if( controller == 100 )
                count_rpn_lsb++;
            else if( controller == 64 || controller == 65 || controller == 120 ||
                     controller == 121 || controller == 123 )
                count_switch++;
            else
                count_other_controller++;
        }
        if( reader.overrun )
            return NULL;
    }

#define TAKE_RUN(name, length)                                                                     \
    do                                                                                             \
    {                                                                                              \
        (name) = reader.pos;                                                                       \
        reader.pos += (length);                                                                    \
        if( reader.pos > reader.size )                                                             \
            return NULL;                                                                           \
    } while( 0 )

    TAKE_RUN(cursor_switch, count_switch);
    TAKE_RUN(cursor_poly_pressure, count_poly_pressure);
    TAKE_RUN(cursor_channel_pressure, count_channel_pressure);
    TAKE_RUN(cursor_bend_msb, count_pitch_bend);
    TAKE_RUN(cursor_modwheel_msb, count_modwheel_msb);
    TAKE_RUN(cursor_volume_msb, count_volume_msb);
    TAKE_RUN(cursor_pan_msb, count_pan_msb);
    TAKE_RUN(cursor_note, count_note_on + count_note_off + count_poly_pressure);
    TAKE_RUN(cursor_note_on_velocity, count_note_on);
    TAKE_RUN(cursor_other_controller, count_other_controller);
    TAKE_RUN(cursor_note_off_velocity, count_note_off);
    TAKE_RUN(cursor_modwheel_lsb, count_modwheel_lsb);
    TAKE_RUN(cursor_volume_lsb, count_volume_lsb);
    TAKE_RUN(cursor_pan_lsb, count_pan_lsb);
    TAKE_RUN(cursor_program, count_program);
    TAKE_RUN(cursor_bend_lsb, count_pitch_bend);
    TAKE_RUN(cursor_nrpn_msb, count_nrpn_msb);
    TAKE_RUN(cursor_nrpn_lsb, count_nrpn_lsb);
    TAKE_RUN(cursor_rpn_msb, count_rpn_msb);
    TAKE_RUN(cursor_rpn_lsb, count_rpn_lsb);
    TAKE_RUN(cursor_tempo, count_tempo * 3);
#undef TAKE_RUN

    if( total_bytes <= 0 || total_bytes > (1 << 26) )
        return NULL;

    song = calloc(1, sizeof(*song));
    if( !song )
        return NULL;
    song->track_count = track_count;
    song->division = division;
    song->midi = calloc((size_t)total_bytes, 1);
    if( !song->midi )
    {
        free(song);
        return NULL;
    }
    song->midi_size = total_bytes;

    writer.data = song->midi;
    writer.size = total_bytes;
    writer.pos = 0;
    writer.overflow = false;

    sw_u32(&writer, MIDI_HEADER_MAGIC);
    sw_u32(&writer, 6);
    sw_u16(&writer, track_count > 1 ? 1 : 0);
    sw_u16(&writer, track_count);
    sw_u16(&writer, division);

    reader.pos = delta_stream;
    memset(controller_value, 0, sizeof(controller_value));
    memset(channel_bank, 0, sizeof(channel_bank));
    memset(channel_patch, 0, sizeof(channel_patch));
    /* MIDI channel 10 is percussion and starts in bank 1 (patch id 128). */
    channel_bank[9] = 128;
    channel_patch[9] = 128;

    for( int track = 0; track < track_count; track++ )
    {
        int body_start;
        int tick;
        int previous = -1;
        bool ended = false;

        sw_u32(&writer, MIDI_TRACK_MAGIC);
        writer.pos += 4; /* length, back-patched at end of track */
        if( writer.pos > writer.size )
        {
            writer.overflow = true;
            break;
        }
        body_start = writer.pos;
        tick = body_start;

        while( !ended )
        {
            int delta = sr_varint(&reader);
            int opcode;
            bool status_changed;
            int kind;

            sw_varint(&writer, delta);
            tick += delta;
            if( opcode_cursor < 0 || opcode_cursor >= reader.size )
            {
                reader.overrun = true;
                break;
            }
            opcode = reader.data[opcode_cursor++] & 255;
            status_changed = previous != opcode;
            kind = opcode & 15;
            previous = kind;

            if( opcode == 7 )
            {
                if( status_changed )
                    sw_u8(&writer, 255);
                sw_u8(&writer, 47);
                sw_u8(&writer, 0);
                sw_patch_length(&writer, writer.pos - body_start);
                ended = true;
                break;
            }
            if( opcode == 23 )
            {
                if( status_changed )
                    sw_u8(&writer, 255);
                sw_u8(&writer, 81);
                sw_u8(&writer, 3);
                sw_u8(&writer, sr_at_s8(&reader, &cursor_tempo));
                sw_u8(&writer, sr_at_s8(&reader, &cursor_tempo));
                sw_u8(&writer, sr_at_s8(&reader, &cursor_tempo));
            }
            else
            {
                channel ^= opcode >> 4;
                channel &= 15;
                if( kind == 0 )
                {
                    int note;
                    int velocity;
                    if( status_changed )
                        sw_u8(&writer, channel + 144);
                    running_note += sr_at_s8(&reader, &cursor_note);
                    running_note_on_velocity += sr_at_s8(&reader, &cursor_note_on_velocity);
                    note = running_note & 127;
                    velocity = running_note_on_velocity & 127;
                    sw_u8(&writer, note);
                    sw_u8(&writer, velocity);
                    if( velocity > 0 )
                    {
                        struct RSCache_MusicSongPatch* entry =
                            song_patch_for(song, channel_patch[channel], tick, &patch_capacity);
                        if( !entry )
                        {
                            reader.overrun = true;
                            break;
                        }
                        entry->notes[note >> 3] |= (uint8_t)(1u << (note & 7));
                    }
                }
                else if( kind == 1 )
                {
                    if( status_changed )
                        sw_u8(&writer, channel + 128);
                    running_note += sr_at_s8(&reader, &cursor_note);
                    running_note_off_velocity += sr_at_s8(&reader, &cursor_note_off_velocity);
                    sw_u8(&writer, running_note & 127);
                    sw_u8(&writer, running_note_off_velocity & 127);
                }
                else if( kind == 2 )
                {
                    int value;
                    int accumulated;
                    int masked;

                    if( status_changed )
                        sw_u8(&writer, channel + 176);
                    running_controller =
                        (running_controller + sr_at_s8(&reader, &controller_stream)) & 127;
                    sw_u8(&writer, running_controller);
                    if( running_controller == 0 || running_controller == 32 )
                        value = sr_at_s8(&reader, &cursor_program);
                    else if( running_controller == 1 )
                        value = sr_at_s8(&reader, &cursor_modwheel_msb);
                    else if( running_controller == 33 )
                        value = sr_at_s8(&reader, &cursor_modwheel_lsb);
                    else if( running_controller == 7 )
                        value = sr_at_s8(&reader, &cursor_volume_msb);
                    else if( running_controller == 39 )
                        value = sr_at_s8(&reader, &cursor_volume_lsb);
                    else if( running_controller == 10 )
                        value = sr_at_s8(&reader, &cursor_pan_msb);
                    else if( running_controller == 42 )
                        value = sr_at_s8(&reader, &cursor_pan_lsb);
                    else if( running_controller == 99 )
                        value = sr_at_s8(&reader, &cursor_nrpn_msb);
                    else if( running_controller == 98 )
                        value = sr_at_s8(&reader, &cursor_nrpn_lsb);
                    else if( running_controller == 101 )
                        value = sr_at_s8(&reader, &cursor_rpn_msb);
                    else if( running_controller == 100 )
                        value = sr_at_s8(&reader, &cursor_rpn_lsb);
                    else if( running_controller == 64 || running_controller == 65 ||
                             running_controller == 120 || running_controller == 121 ||
                             running_controller == 123 )
                        value = sr_at_s8(&reader, &cursor_switch);
                    else
                        value = sr_at_s8(&reader, &cursor_other_controller);

                    accumulated = controller_value[running_controller] + value;
                    controller_value[running_controller] = accumulated;
                    masked = accumulated & 127;
                    sw_u8(&writer, masked);
                    if( running_controller == 0 )
                        channel_bank[channel] =
                            (masked << 14) + (channel_bank[channel] & ~2080768);
                    if( running_controller == 32 )
                        channel_bank[channel] = (masked << 7) + (channel_bank[channel] & ~16256);
                }
                else if( kind == 3 )
                {
                    int low;
                    if( status_changed )
                        sw_u8(&writer, channel + 224);
                    low = running_bend + sr_at_s8(&reader, &cursor_bend_lsb);
                    running_bend = low + (sr_at_s8(&reader, &cursor_bend_msb) << 7);
                    sw_u8(&writer, running_bend & 127);
                    sw_u8(&writer, (running_bend >> 7) & 127);
                }
                else if( kind == 4 )
                {
                    if( status_changed )
                        sw_u8(&writer, channel + 208);
                    running_channel_pressure += sr_at_s8(&reader, &cursor_channel_pressure);
                    sw_u8(&writer, running_channel_pressure & 127);
                }
                else if( kind == 5 )
                {
                    if( status_changed )
                        sw_u8(&writer, channel + 160);
                    running_note += sr_at_s8(&reader, &cursor_note);
                    running_poly_pressure += sr_at_s8(&reader, &cursor_poly_pressure);
                    sw_u8(&writer, running_note & 127);
                    sw_u8(&writer, running_poly_pressure & 127);
                }
                else if( kind == 6 )
                {
                    int program;
                    if( status_changed )
                        sw_u8(&writer, channel + 192);
                    program = sr_at_s8(&reader, &cursor_program);
                    channel_patch[channel] = channel_bank[channel] + program;
                    sw_u8(&writer, program);
                }
                else
                {
                    reader.overrun = true;
                    break;
                }
            }
            if( reader.overrun || writer.overflow )
                break;
        }
        if( reader.overrun || writer.overflow || !ended )
            break;
    }

    if( reader.overrun || writer.overflow )
    {
        RSCache_MusicSongFree(song);
        return NULL;
    }
    /* The reference sizes the output exactly; a short write means a miscount. */
    if( writer.pos != total_bytes )
    {
        RSCache_MusicSongFree(song);
        return NULL;
    }
    return song;
}
