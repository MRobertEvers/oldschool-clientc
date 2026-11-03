#ifndef SRC_EDITOR_TASK_EDITOR_RECENT_IO_H
#define SRC_EDITOR_TASK_EDITOR_RECENT_IO_H

/**
 * editor_recent.ini's two trips across the platform seam, exactly as
 * task_editor_hotkeys_io.h carries editor_hotkeys.ini: a task on the App's
 * queue issues ONE ToriRS_IO_QueueFileRead / QueueFileWrite and parks on it.
 * The desktop executor reads and writes the file; the web keeps it in
 * IndexedDB (host.readClientFile / host.writeClientFile). Nothing here
 * touches a file itself.
 */

#include "asyncio.h"
#include "editor/editor_recent.h"

/** What the load task hands back; the caller keeps it alive until `done`. */
struct EditorRecentLoad
{
    int done;
    /** 1 when the file existed and held text. */
    int found;
    /** The file's ring and scheme (Editor_RecentDecode). */
    struct Editor_Recent recent;
};

/** Read `path` into `out`. A missing file is a first launch: done = 1,
 *  found = 0, `recent` empty. */
struct ToriRS_Task*
CreateTask_EditorRecentLoad(
    struct EditorRecentLoad* out,
    const char* path);

/** Write `recent` to `path`. Encoded at creation. */
struct ToriRS_Task*
CreateTask_EditorRecentSave(
    const struct Editor_Recent* recent,
    const char* path);

#endif
