#ifndef SRC_EDITOR_MAP_EDITOR_CONFIG_H
#define SRC_EDITOR_MAP_EDITOR_CONFIG_H

/*
 * The map editor's launch options: which content it edits, which ToriRSMapEd
 * deployment serves the session, which Client it joins and where its command
 * panel is drawn.
 *
 * One member of struct AppConfig (app.h `map_editor`). BootManifest_ApplyToConfig
 * fills it from `[editor:boot]`; main.c's --editor-view, --editor-panel,
 * --maped and --maped-group rewrite it before App_Init copies it. A zeroed one
 * is every boot that is not an editor boot.
 *
 * `[world:boot] layout=` / --world-layout is not here: any manifest and the
 * command line can choose the edit layout without an editor (AppConfig
 * world_layout).
 *
 * This header names no App: it is plain data plus the presence tests.
 */

#include "bootmanifest/bootmanifest.h"

#include <assert.h>
#include <stddef.h>

struct MapEditorConfig
{
    /** [editor:boot] content_dir — the content root whose `maps/` the world map
     *  editor edits. NULL = no editor this boot, which is every normal client
     *  run. Borrowed from the BootManifest, which outlives the App. */
    char const* content_dir;
    /** [editor:boot] lane — the editor lane over `content_dir`
     *  (torirsmaped/torirs_maped_lanes.h); NULL or "main" edits the tree itself. */
    char const* lane;
    /** [editor:boot] repo_root — where a bake would run. NULL disables baking. */
    char const* repo_root;
    /**
     * enum BootManifestEditorServer: which ToriRSMapEd deployment the editor
     * session talks to. 0 (embed) hosts the server in-process over
     * `content_dir`; tcp dials the torirsmaped daemon below, and then
     * enables the editor even with no content_dir of its own.
     */
    int server;
    /** torirsmaped daemon address; NULL/0 = localhost, its default port. */
    char const* server_host;
    int server_port;
    /**
     * [editor:boot] client — the ToriRSMapEd Client (session group) to join,
     * or 0 to be granted a fresh one.
     *
     * A Client is a group of connections sharing selection and tool state, so
     * this is how a second PROCESS joins a session already running: read the
     * id the first one printed at boot and state it here. server=tcp only —
     * an embedded server has no other process to share with.
     */
    int client_id;
    /**
     * enum BootManifestEditorPanel: where the command panel is drawn.
     *
     * 0 (inprocess) is the default a zeroed config gets, which is the binding
     * that needs nothing outside this process -- so a caller that never sets
     * this gets the panel it already had.
     */
    int panel;
};

/**
 * Is this an editor boot: a content root to edit, or a torirsmaped daemon to
 * dial (which enables the editor with no content root of its own). App_Init
 * opens the editor on it, and the chrome takes the editor's theme on it.
 */
static inline int
MapEditorConfig_IsEditorBoot(struct MapEditorConfig const* config)
{
    assert(config);
    return (config->content_dir && config->content_dir[0]) ||
           config->server == BOOTMANIFEST_EDITOR_SERVER_TCP;
}

/**
 * Did the configuration name a content root at all (an empty one included).
 * main.c restores the editor's saved window box on it; a daemon-only editor
 * boot has none.
 */
static inline int
MapEditorConfig_StatesContentDir(struct MapEditorConfig const* config)
{
    assert(config);
    return config->content_dir != NULL;
}

#endif
