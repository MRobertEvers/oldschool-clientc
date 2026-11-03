#ifndef SRC_EDITOR_EDITOR_RECENT_H
#define SRC_EDITOR_EDITOR_RECENT_H

/**
 * What the editor remembers per user beside the dock layout and the hotkeys:
 * the squares opened most recently (File > Recent squares, the command
 * palette's "recent" hits) and the camera scheme (default / Unity,
 * docs/MAPED_HOTKEYS.md "Camera").
 *
 * Pure: the ring and its ini codec. `editor_recent.ini` sits beside
 * `editor_layout.ini` under the same path rule (TORIRS_EDITOR_RECENT unset =
 * that name, empty = no file read or written, else the path); the App reads
 * it at boot and writes it when `serial` moves (src/app/app_editor_hotkeys.c,
 * through task_editor_recent_io.h). Never in the content tree.
 *
 *     [recent]
 *     version = 1
 *     square = m50_50
 *     square = ported/testlane/m50_50
 *     [camera]
 *     scheme = unity
 *
 * Contract violations assert (CLAUDE.md).
 */

#include "editor/editor_doc.h"

#include <stdint.h>

#define EDITOR_RECENT_MAX 10
#define EDITOR_RECENT_DEFAULT_PATH "editor_recent.ini"
#define EDITOR_RECENT_VERSION 1

enum Editor_CameraScheme
{
    /** WASD / RF flight, arrows turn, middle-button orbit, wheel dolly. */
    EDITOR_CAMERA_SCHEME_DEFAULT = 0,
    /** Right-drag looks, middle-drag pans the ground, wheel zooms toward
     *  the cursor's tile, Alt+left-drag orbits the selection, F frames. */
    EDITOR_CAMERA_SCHEME_UNITY,
    EDITOR_CAMERA_SCHEME_COUNT
};

struct Editor_Recent
{
    /** Newest first: "m50_50", or "<storage>/m50_50" for a lane square. */
    char keys[EDITOR_RECENT_MAX][EDITOR_DOC_KEY_MAX];
    int count;
    enum Editor_CameraScheme camera_scheme;
    /** Bumps on every change: the File menu relabels and the App saves. */
    uint32_t serial;
};

/** Empty ring, the default scheme, serial 0. */
void
Editor_RecentInit(struct Editor_Recent* recent);

/** Square (map_x, map_z) of `storage` ("" or "maps" = the content tree's
 *  maps/) was opened: it goes to the front (once). Bumps the serial. */
void
Editor_RecentNote(
    struct Editor_Recent* recent,
    int map_x,
    int map_z,
    const char* storage);

/** Entry `index` (0 newest) as its square and storage ("" for maps/).
 *  0 past the end. */
int
Editor_RecentEntry(
    const struct Editor_Recent* recent,
    int index,
    int* out_map_x,
    int* out_map_z,
    char* out_storage,
    int storage_size);

/** "m50_50" or "<storage>/m50_50" into its parts; 0 for anything else. */
int
Editor_RecentParseKey(
    const char* key,
    int* out_map_x,
    int* out_map_z,
    char* out_storage,
    int storage_size);

/** The scheme (bumps the serial when it changes). */
void
Editor_RecentSetScheme(
    struct Editor_Recent* recent,
    enum Editor_CameraScheme scheme);

/** "default" / "unity", and back (-1 for an unknown word). */
const char*
Editor_CameraSchemeName(enum Editor_CameraScheme scheme);

int
Editor_CameraSchemeParse(const char* text);

/** The path rule of TORIRS_EDITOR_LAYOUT: env unset = the default name,
 *  empty = NULL (persistence off), else the value. */
const char*
Editor_RecentPath(void);

/** `size` bytes of ini text into `recent` (reset first). Unknown lines are
 *  skipped; a bad square key is skipped. Returns the squares taken. */
int
Editor_RecentDecode(
    struct Editor_Recent* recent,
    const void* data,
    int size);

/** The ring and the scheme as ini text. `*out_data` is ToriMem_Process()'s
 *  and the caller's; NUL-terminated, not counted. */
void
Editor_RecentEncode(
    const struct Editor_Recent* recent,
    char** out_data,
    int* out_size);

#endif
