#include "game/rs_title_music.h"

#include <assert.h>

void
RS_TitleMusic_SetSong(
    struct RS_TitleMusic* music,
    int song)
{
    assert(music);
    assert(song >= 0);
    music->song = song;
    music->known = true;
}

int
RS_TitleMusic_Song(struct RS_TitleMusic const* music)
{
    assert(music);
    return music->song;
}

enum RS_TitleMusicStep
RS_TitleMusic_Step(
    struct RS_TitleMusic* music,
    bool on_title,
    bool turned_off)
{
    bool want;

    assert(music);
    if( !on_title )
    {
        music->playing = false;
        return RS_TITLE_MUSIC_KEEP;
    }
    want = music->known && !turned_off;
    if( want == music->playing )
        return RS_TITLE_MUSIC_KEEP;
    music->playing = want;
    return want ? RS_TITLE_MUSIC_PLAY : RS_TITLE_MUSIC_STOP;
}
