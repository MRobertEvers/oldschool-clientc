#ifndef SRC_AUDIO_TOOLS_AUDIO_SONGLOAD_H
#define SRC_AUDIO_TOOLS_AUDIO_SONGLOAD_H

#include "audio/torirs_soundbank.h"

#include <rscache.h>

#include <stdbool.h>

/*
 * Load a song and its soundfont out of a cache, synchronously.
 *
 * The client does this asynchronously through a protothread task
 * (engine/dat2/task_dat2_music_load.c). This is the same *walk* with the
 * scheduler taken out: track -> instrument manifest -> patches -> the samples
 * the used notes reference.
 *
 * It lives here, shared by the harness and the engine test, because keeping two
 * copies of this walk is what let a loader bug hide. When the client's loader
 * collapsed every instrument into one patch slot, the test had its own eager
 * loader that did it correctly and reported everything green. Two
 * implementations of one walk means the tested one can be right while the
 * shipped one is wrong -- so at minimum the *offline* copy is written once.
 *
 * This is deliberately not the client's task: a protothread needs an IO layer
 * and a cache provider, and pulling those in would tie every audio tool to the
 * client's link set, which is the thing the harness exists to avoid.
 */

struct AudioSongLoad
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_VorbisSetup* setup;
    struct RSCache_MusicSong* song;
    struct ToriRS_SoundBank bank;

    /* What the walk found, for a report. */
    int patches_named;
    int patches_loaded;
    int samples_loaded;
    int samples_missing;
};

/** Which cache index the id refers to. */
enum AudioSongSource
{
    AUDIO_SONG_TRACK = 0,
    AUDIO_SONG_JINGLE,
};

/**
 * Open `cache_dir` under `profile_name` and load song `song_id`.
 *
 * Returns false if the cache, the profile or the song is absent; `out` is
 * always safe to pass to AudioSongLoad_Free afterwards.
 */
bool
AudioSongLoad_Open(
    struct AudioSongLoad* out,
    const char* cache_dir,
    const char* profile_name,
    enum AudioSongSource source,
    int song_id);

void
AudioSongLoad_Free(struct AudioSongLoad* load);

#endif
