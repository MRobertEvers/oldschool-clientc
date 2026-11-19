/*
 * The title screen's music (game/rs_title_music.h): nothing before the button
 * names a track, one play when it does, one stop when the player turns it
 * off, and leaving the title forgets without a stop.
 *
 * Run: make -C src test-rs-title-music
 */

#include "game/rs_title_music.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
test_waits_for_the_button(void)
{
    struct RS_TitleMusic music;

    memset(&music, 0, sizeof(music));
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_KEEP);
    assert(!music.playing);
    RS_TitleMusic_SetSong(&music, 0);
    assert(RS_TitleMusic_Song(&music) == 0);
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_PLAY);
    assert(music.playing);
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_KEEP);
}

static void
test_turned_off_stops_once(void)
{
    struct RS_TitleMusic music;

    memset(&music, 0, sizeof(music));
    RS_TitleMusic_SetSong(&music, 7);
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_PLAY);
    assert(RS_TitleMusic_Step(&music, true, true) == RS_TITLE_MUSIC_STOP);
    assert(RS_TitleMusic_Step(&music, true, true) == RS_TITLE_MUSIC_KEEP);
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_PLAY);
    /* Off before anything played: nothing to stop. */
    memset(&music, 0, sizeof(music));
    RS_TitleMusic_SetSong(&music, 7);
    assert(RS_TitleMusic_Step(&music, true, true) == RS_TITLE_MUSIC_KEEP);
}

static void
test_leaving_forgets_without_a_stop(void)
{
    struct RS_TitleMusic music;

    memset(&music, 0, sizeof(music));
    RS_TitleMusic_SetSong(&music, 3);
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_PLAY);
    assert(RS_TitleMusic_Step(&music, false, false) == RS_TITLE_MUSIC_KEEP);
    assert(!music.playing);
    /* Off while away says nothing either. */
    assert(RS_TitleMusic_Step(&music, false, true) == RS_TITLE_MUSIC_KEEP);
    /* Back on the title, the track is started again. */
    assert(RS_TitleMusic_Step(&music, true, false) == RS_TITLE_MUSIC_PLAY);
    /* A new button song is the track from then on. */
    RS_TitleMusic_SetSong(&music, 9);
    assert(RS_TitleMusic_Song(&music) == 9);
}

int
main(void)
{
    test_waits_for_the_button();
    test_turned_off_stops_once();
    test_leaving_forgets_without_a_stop();
    printf("rs_title_music_test: ok\n");
    return 0;
}
