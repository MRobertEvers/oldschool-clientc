#include "rs_audio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
audio_debug(char const* what, int aid, int barg)
{
    if( getenv("TORIRS_NET_DEBUG") )
        fprintf(stderr, "rs_audio: %s id=%d arg=%d (no playback backend)\n", what, aid, barg);
}

void
RS_Audio_Init(struct RS_Audio* audio)
{
    assert(audio);
    memset(audio, 0, sizeof(*audio));
    audio->current_song = -1;
    audio->jingle_id = -1;
}

void
RS_Audio_Synth(
    struct RS_Audio* audio,
    int synth_id,
    int loops,
    int delay)
{
    assert(audio);
    audio->synths[audio->synth_head].id = synth_id;
    audio->synths[audio->synth_head].loops = loops;
    audio->synths[audio->synth_head].delay = delay;
    audio->synth_head = (audio->synth_head + 1) % RS_AUDIO_SYNTH_RING;
    if( audio->synth_count < RS_AUDIO_SYNTH_RING )
        audio->synth_count++;
    audio_debug("synth", synth_id, loops);
}

void
RS_Audio_Song(
    struct RS_Audio* audio,
    int song_id)
{
    assert(audio);
    audio->current_song = song_id;
    audio_debug("song", song_id, 0);
}

void
RS_Audio_Jingle(
    struct RS_Audio* audio,
    int jingle_id,
    int delay)
{
    assert(audio);
    audio->jingle_id = jingle_id;
    audio->jingle_delay = delay;
    audio_debug("jingle", jingle_id, delay);
}
