#ifndef SRC_REPLAY_REPLAY_CONFIG_H
#define SRC_REPLAY_REPLAY_CONFIG_H

/*
 * The replay viewer's own launch options: one member of struct AppConfig
 * (app.h `replay`), filled by BootManifest_ApplyToConfig and read by
 * main_rsprox_replay_open. The replay also rewrites shared and network
 * members of the configuration to fit the recording; those stay where every
 * other boot reads them.
 *
 * This header names no App: it is plain data.
 */

struct ReplayConfig
{
    /** Another revision's cache, `{revision}` standing for the number
     * ([cache:boot] dir_for_revision); NULL when the manifest names none. */
    char const* cache_dir_for_revision;
};

#endif
