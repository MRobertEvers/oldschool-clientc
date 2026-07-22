#ifndef SRC_GAME_RS_AUDIO_H
#define SRC_GAME_RS_AUDIO_H

/*
 * Audio packet sink (SYNTH_SOUND / MIDI_SONG / MIDI_JINGLE). No playback —
 * state is recorded so a later audio backend can consume it, and headless
 * tests can assert the packets arrived.
 */

#define RS_AUDIO_SYNTH_RING 16

struct RS_AudioSynth
{
    int id;
    int loops;
    int delay;
};

struct RS_Audio
{
    int current_song; /* -1 = none */
    int jingle_id;    /* -1 = none */
    int jingle_delay;
    struct RS_AudioSynth synths[RS_AUDIO_SYNTH_RING];
    int synth_head;
    int synth_count;
};

void
RS_Audio_Init(struct RS_Audio* audio);

void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay);

void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id);

void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int delay);

#endif
