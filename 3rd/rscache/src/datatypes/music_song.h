#ifndef RSCACHE_DATATYPES_MUSIC_SONG_H
#define RSCACHE_DATATYPES_MUSIC_SONG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Music tracks (index 6) and jingles (index 11) — column-packed MIDI.
 *
 * The archive is not a MIDI stream. It is the same events with every *field*
 * gathered into its own contiguous run and delta-coded against the previous
 * value of that field: all note numbers together, all velocities together, all
 * controller values split by controller number, and so on. The header (track
 * count and division) lives in the **last three bytes**. Decoding it needs two
 * passes — one to count events by kind so the run offsets can be computed, one
 * to re-emit — and there is no shortcut, which is why this is a faithful port
 * of the reference rather than a reader.
 *
 * The output is a real Standard MIDI File. That is worth more than an internal
 * event list: `audioprobe --song <id> --out x.mid` opens in any sequencer, so
 * the port can be checked by ear and by eye instead of only against itself.
 *
 * The archive also yields an instrument manifest: which banked programs the
 * song uses and, for each, exactly which notes it ever plays. The client loads
 * only those samples. Loading all 128 notes of every referenced patch instead
 * costs several times the memory and stalls the first bar.
 */

/** One instrument the song uses, and the notes it uses from it. */
struct RSCache_MusicSongPatch
{
    /** Banked program: `(bankMSB << 14) | (bankLSB << 7) | program`, which is
     *  also the index-15 archive id. */
    int patch_id;
    /** Tick of the first note that referenced it — the reference's load order. */
    int first_tick;
    /** Bit `n` set means note `n` is played by this patch at least once. */
    uint8_t notes[16];
};

struct RSCache_MusicSong
{
    /** Standard MIDI File bytes: `MThd` header plus one `MTrk` per track. */
    uint8_t* midi;
    int midi_size;

    int track_count;
    /** MIDI division (ticks per quarter note). */
    int division;

    struct RSCache_MusicSongPatch* patches;
    int patch_count;
};

/** Decode one index 6 / index 11 archive. Returns NULL if it is malformed. */
struct RSCache_MusicSong*
RSCache_MusicSongNewDecode(
    const char* data,
    int data_size);

void
RSCache_MusicSongFree(struct RSCache_MusicSong* song);

static inline bool
RSCache_MusicSongPatchUsesNote(
    const struct RSCache_MusicSongPatch* patch,
    int note)
{
    if( !patch || note < 0 || note >= 128 )
        return false;
    return (patch->notes[note >> 3] & (1u << (note & 7))) != 0;
}

#endif
