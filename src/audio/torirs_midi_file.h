#ifndef SRC_AUDIO_TORIRS_MIDI_FILE_H
#define SRC_AUDIO_TORIRS_MIDI_FILE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A Standard MIDI File, read the way a sequencer reads it: one cursor per
 * track, advanced by whoever is next in time.
 *
 * This is the reference's MidiFileReader. It does not build an event list --
 * the file stays as bytes and the reader keeps, per track, a byte offset, the
 * accumulated tick, and the running status. `NextTrack` picks the track whose
 * tick is smallest, and the synth pulls events from it until that stops being
 * true. That is what makes looping cheap (rewind every cursor, keep the
 * elapsed microseconds) and what makes a several-megabyte song cost a few
 * hundred bytes of state.
 *
 * Events are returned *packed into an int*, again like the reference:
 * `status | data1 << 8 | data2 << 16`. The synth switches on the top nibble of
 * the low byte, so nothing is gained by unpacking into a struct here and the
 * dispatcher stays a direct port.
 */

#define TORIRS_MIDI_MAX_TRACKS 64

/** What ReadEvent produced. */
enum ToriRS_MidiEventKind
{
    /** A channel event; `packed` holds status/data1/data2. */
    TORIRS_MIDI_EVENT_CHANNEL = 0,
    /** End of track. The track's cursor is retired. */
    TORIRS_MIDI_EVENT_END_OF_TRACK,
    /** Tempo change; already applied to the reader's clock. */
    TORIRS_MIDI_EVENT_TEMPO,
    /** A meta or sysex event the synth ignores. */
    TORIRS_MIDI_EVENT_IGNORED,
    /** Malformed: the reader stops. */
    TORIRS_MIDI_EVENT_ERROR
};

struct ToriRS_MidiFile
{
    const uint8_t* data;
    int size;
    /** Cursor while reading; the reference keeps one shared Buffer. */
    int position;

    /** Ticks per quarter note from the header. */
    int division;
    int track_count;
    int track_start[TORIRS_MIDI_MAX_TRACKS];
    /** Byte offset of each track's next event, or -1 once the track ended. */
    int track_cursor[TORIRS_MIDI_MAX_TRACKS];
    /** Accumulated tick of each track's next event. */
    int track_tick[TORIRS_MIDI_MAX_TRACKS];
    /** Running status per track. */
    int track_status[TORIRS_MIDI_MAX_TRACKS];

    /** Microseconds per quarter note; MIDI's default until a tempo event. */
    int tempo;
    /**
     * Microseconds accounted for by tempo changes already seen.
     *
     * Time is `tempo * tick + elapsed`; when the tempo changes at tick T the
     * reader folds `(old - new) * T` into this so the expression stays valid
     * for ticks on either side of the change. That is the reference's trick and
     * it is why a song with tempo changes does not drift.
     */
    int64_t elapsed;

    bool open;
};

/** Point the reader at a whole SMF. Returns false if it is not one. */
bool
ToriRS_MidiFile_Open(
    struct ToriRS_MidiFile* file,
    const uint8_t* data,
    int size);

/** Forget the file. Does not free `data`, which the caller owns. */
void
ToriRS_MidiFile_Close(struct ToriRS_MidiFile* file);

static inline bool
ToriRS_MidiFile_IsOpen(const struct ToriRS_MidiFile* file)
{
    return file && file->open;
}

/** Track with the smallest pending tick, or -1 when every track has ended. */
int
ToriRS_MidiFile_NextTrack(const struct ToriRS_MidiFile* file);

/** True once every track has hit end-of-track. */
bool
ToriRS_MidiFile_Finished(const struct ToriRS_MidiFile* file);

/** Microseconds at `tick`, under the tempo history seen so far. */
int64_t
ToriRS_MidiFile_TimeAt(
    const struct ToriRS_MidiFile* file,
    int tick);

/** Read the next event of `track`. Advances that track's cursor and tick. */
enum ToriRS_MidiEventKind
ToriRS_MidiFile_ReadEvent(
    struct ToriRS_MidiFile* file,
    int track,
    int* out_packed);

/**
 * Rewind every track to the start, keeping `elapsed` so the loop is seamless.
 *
 * The reference passes the time the song *ended* so the next pass continues the
 * same clock rather than jumping back, which is what stops a looped track from
 * clicking at the seam.
 */
void
ToriRS_MidiFile_Rewind(
    struct ToriRS_MidiFile* file,
    int64_t elapsed);

#endif
