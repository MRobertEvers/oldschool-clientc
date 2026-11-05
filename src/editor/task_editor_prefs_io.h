#ifndef SRC_EDITOR_TASK_EDITOR_PREFS_IO_H
#define SRC_EDITOR_TASK_EDITOR_PREFS_IO_H

/**
 * editor_prefs.ini's two trips across the platform seam, exactly as
 * task_editor_hotkeys_io.h carries editor_hotkeys.ini: a task on the App's
 * queue issues ONE ToriRS_IO_QueueFileRead / QueueFileWrite and parks on it.
 * The desktop executor reads and writes the file; the web keeps it in
 * IndexedDB (host.readClientFile / host.writeClientFile). Nothing here
 * touches a file itself.
 */

#include "asyncio.h"
#include "editor/editor_prefs.h"

/** What the load task hands back; the caller keeps it alive until `done`. */
struct EditorPrefsLoad
{
    int done;
    /** 1 when the file existed and held text. */
    int found;
    /** The defaults with the file's lines applied (Editor_PrefsDecode). */
    struct Editor_Prefs prefs;
    /** Lines Editor_PrefsDecode skipped as malformed. */
    int skipped;
};

/** Read `path` into `out`. A missing file is a first launch: done = 1,
 *  found = 0, `prefs` the defaults. */
struct ToriRS_Task*
CreateTask_EditorPrefsLoad(
    struct EditorPrefsLoad* out,
    const char* path);

/** Write `prefs` to `path`. Encoded at creation. */
struct ToriRS_Task*
CreateTask_EditorPrefsSave(
    const struct Editor_Prefs* prefs,
    const char* path);

#endif
