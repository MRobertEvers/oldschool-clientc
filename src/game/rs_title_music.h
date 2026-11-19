#ifndef SRC_GAME_RS_TITLE_MUSIC_H
#define SRC_GAME_RS_TITLE_MUSIC_H

/*
 * The title screen's music: which track it plays, and whether this client
 * started it.
 *
 * The track is the profile's, named by the title's music button (its song=),
 * so it is not known until that button has been drawn once, and nothing plays
 * before then. While the title or its connecting bar is up, the track plays
 * when it is known and the player has not turned title music off, and stops
 * when either stops being true; each change is said once. Leaving the title
 * forgets that the title started anything without asking for a stop: the track
 * runs on into the world until the server's first song replaces it.
 *
 * This does not read the player's option, know which screen is up, or play
 * anything. The caller passes whether the title is up and whether title music
 * is turned off, and plays or stops the track it is told to.
 */

#include <stdbool.h>

struct RS_TitleMusic
{
    /** The track the music button names; meaningful only while `known`. */
    int song;
    bool known;
    /** The title's own track is playing because this client started it. */
    bool playing;
};

/** What the caller does with the track this step. */
enum RS_TitleMusicStep
{
    RS_TITLE_MUSIC_KEEP = 0,
    RS_TITLE_MUSIC_PLAY,
    RS_TITLE_MUSIC_STOP,
};

/** The music button was drawn naming `song` (>= 0). */
void
RS_TitleMusic_SetSong(
    struct RS_TitleMusic* music,
    int song);

/** The track to play; only meaningful once a song is known. */
int
RS_TitleMusic_Song(struct RS_TitleMusic const* music);

/**
 * One step: `on_title` is whether the title or its connecting bar is up,
 * `turned_off` whether the player turned title music off. Returns PLAY or
 * STOP when the title's track should change, KEEP otherwise, and records the
 * answer as what is now playing.
 */
enum RS_TitleMusicStep
RS_TitleMusic_Step(
    struct RS_TitleMusic* music,
    bool on_title,
    bool turned_off);

#endif
