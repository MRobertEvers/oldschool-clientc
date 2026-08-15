#include "audio/torirs_midi_file.h"
#include <assert.h>

#include <string.h>

/*
 * Port of the reference's MidiFileReader.
 *
 * The one table worth keeping verbatim is the data-byte count per status byte:
 * it is indexed by `status - 128`, so it covers 0x80..0xFF, and the tail
 * (0xF0 and up) is what makes sysex and the system-common messages skip the
 * right number of bytes without a second switch.
 */
static const uint8_t MIDI_DATA_BYTES[128] = {
    /* 0x80..0xBF: note off, note on, poly pressure, controller — 2 bytes */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xC0..0xDF: program change, channel pressure — 1 byte */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0xE0..0xEF: pitch bend — 2 bytes */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xF0..0xFF: system messages */
    0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int
read_u8(struct ToriRS_MidiFile* file)
{
    if( file->position < 0 || file->position >= file->size )
    {
        file->position = file->size;
        return 0;
    }
    return file->data[file->position++];
}

static int
read_u16(struct ToriRS_MidiFile* file)
{
    int high = read_u8(file);
    return (high << 8) | read_u8(file);
}

static int
read_u24(struct ToriRS_MidiFile* file)
{
    int value = read_u8(file) << 16;
    value |= read_u8(file) << 8;
    return value | read_u8(file);
}

static int
read_u32(struct ToriRS_MidiFile* file)
{
    int value = read_u8(file) << 24;
    value |= read_u8(file) << 16;
    value |= read_u8(file) << 8;
    return value | read_u8(file);
}

static int
read_varint(struct ToriRS_MidiFile* file)
{
    int value = 0;
    int guard = 0;

    for( ;; )
    {
        int byte = read_u8(file);
        value = (value << 7) | (byte & 0x7F);
        if( (byte & 0x80) == 0 )
            return value;
        if( ++guard >= 4 )
            return value;
    }
}

bool
ToriRS_MidiFile_Open(
    struct ToriRS_MidiFile* file,
    const uint8_t* data,
    int size)
{
    int declared_tracks;
    int found = 0;

    assert(file);
    memset(file, 0, sizeof(*file));
    if( !data || size < 14 || memcmp(data, "MThd", 4) != 0 )
        return false;

    file->data = data;
    file->size = size;
    /* Skip "MThd", the header length and the format word: the reference reads
     * from offset 10, which is exactly the track count. */
    file->position = 10;
    declared_tracks = read_u16(file);
    file->division = read_u16(file);
    file->tempo = 500000; /* MIDI's default: 120bpm */
    file->elapsed = 0;

    if( declared_tracks <= 0 || declared_tracks > TORIRS_MIDI_MAX_TRACKS || file->division <= 0 )
        return false;

    while( found < declared_tracks && file->position + 8 <= file->size )
    {
        int magic = read_u32(file);
        int length = read_u32(file);

        if( length < 0 || file->position + length > file->size )
            return false;
        if( magic == 0x4D54726B /* MTrk */ )
        {
            file->track_start[found] = file->position;
            found++;
        }
        file->position += length;
    }
    if( found != declared_tracks )
        return false;

    file->track_count = declared_tracks;
    for( int i = 0; i < declared_tracks; i++ )
    {
        file->track_cursor[i] = file->track_start[i];
        file->track_tick[i] = 0;
        file->track_status[i] = 0;
    }
    file->open = true;

    /* Prime every track with its first delta, so the tick fields are the tick
     * of the *next* event and NextTrack is a plain minimum. */
    for( int i = 0; i < declared_tracks; i++ )
    {
        file->position = file->track_cursor[i];
        file->track_tick[i] += read_varint(file);
        file->track_cursor[i] = file->position;
    }
    return true;
}

void
ToriRS_MidiFile_Close(struct ToriRS_MidiFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

int
ToriRS_MidiFile_NextTrack(const struct ToriRS_MidiFile* file)
{
    int best = -1;
    int best_tick = 0;

    assert(file);
    if( !file->open )
        return -1;
    for( int i = 0; i < file->track_count; i++ )
    {
        if( file->track_cursor[i] < 0 )
            continue;
        if( best < 0 || file->track_tick[i] < best_tick )
        {
            best = i;
            best_tick = file->track_tick[i];
        }
    }
    return best;
}

bool
ToriRS_MidiFile_Finished(const struct ToriRS_MidiFile* file)
{
    assert(file);
    if( !file->open )
        return true;
    for( int i = 0; i < file->track_count; i++ )
    {
        if( file->track_cursor[i] >= 0 )
            return false;
    }
    return true;
}

int64_t
ToriRS_MidiFile_TimeAt(
    const struct ToriRS_MidiFile* file,
    int tick)
{
    assert(file);
    return (int64_t)file->tempo * (int64_t)tick + file->elapsed;
}

enum ToriRS_MidiEventKind
ToriRS_MidiFile_ReadEvent(
    struct ToriRS_MidiFile* file,
    int track,
    int* out_packed)
{
    int status;
    enum ToriRS_MidiEventKind kind = TORIRS_MIDI_EVENT_IGNORED;

    if( out_packed )
        *out_packed = 0;
    assert(file);
    if( !file->open || track < 0 || track >= file->track_count )
        return TORIRS_MIDI_EVENT_ERROR;
    if( file->track_cursor[track] < 0 )
        return TORIRS_MIDI_EVENT_END_OF_TRACK;

    file->position = file->track_cursor[track];
    if( file->position >= file->size )
        goto ended;

    if( (file->data[file->position] & 0x80) != 0 )
    {
        status = file->data[file->position] & 255;
        file->track_status[track] = status;
        file->position++;
    }
    else
    {
        status = file->track_status[track];
        if( status < 128 )
            goto ended;
    }

    if( status == 0xF0 || status == 0xF7 )
    {
        /* Sysex. A 0xF7 "escape" may carry a real-time status byte, which the
         * reference promotes; anything else is skipped whole. */
        int length = read_varint(file);
        if( status == 0xF7 && length > 0 && file->position < file->size )
        {
            int embedded = file->data[file->position] & 255;
            if( (embedded >= 0xF1 && embedded <= 0xF3) || embedded == 0xF6 ||
                embedded == 0xF8 || (embedded >= 0xFA && embedded <= 0xFC) || embedded == 0xFE )
            {
                file->position++;
                file->track_status[track] = embedded;
                status = embedded;
                goto channel_or_meta;
            }
        }
        file->position += length;
        goto advance;
    }

channel_or_meta:
    if( status == 0xFF )
    {
        int meta = read_u8(file);
        int length = read_varint(file);

        if( meta == 0x2F )
        {
            file->position += length;
            goto ended;
        }
        if( meta == 0x51 )
        {
            int microseconds = read_u24(file);
            /* Keep `tempo * tick + elapsed` continuous across the change. */
            file->elapsed += (int64_t)(file->tempo - microseconds) * (int64_t)file->track_tick[track];
            file->tempo = microseconds;
            file->position += length - 3;
            kind = TORIRS_MIDI_EVENT_TEMPO;
            goto advance;
        }
        file->position += length;
        kind = TORIRS_MIDI_EVENT_IGNORED;
        goto advance;
    }
    else
    {
        int data_bytes = MIDI_DATA_BYTES[(status - 128) & 127];
        int packed = status;

        if( data_bytes >= 1 )
            packed |= read_u8(file) << 8;
        if( data_bytes >= 2 )
            packed |= read_u8(file) << 16;
        if( out_packed )
            *out_packed = packed;
        kind = TORIRS_MIDI_EVENT_CHANNEL;
    }

advance:
    if( file->position > file->size )
        goto ended;
    /* Read the next delta now so `track_tick` is always the tick of the event
     * that has not happened yet. */
    file->track_tick[track] += read_varint(file);
    file->track_cursor[track] = file->position;
    return kind;

ended:
    file->track_cursor[track] = -1;
    return TORIRS_MIDI_EVENT_END_OF_TRACK;
}

void
ToriRS_MidiFile_Rewind(
    struct ToriRS_MidiFile* file,
    int64_t elapsed)
{
    assert(file);
    if( !file->open )
        return;
    file->elapsed = elapsed;
    for( int i = 0; i < file->track_count; i++ )
    {
        file->track_tick[i] = 0;
        file->track_status[i] = 0;
        file->position = file->track_start[i];
        file->track_tick[i] += read_varint(file);
        file->track_cursor[i] = file->position;
    }
}
