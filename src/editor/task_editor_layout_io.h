#ifndef SRC_EDITOR_TASK_EDITOR_LAYOUT_IO_H
#define SRC_EDITOR_TASK_EDITOR_LAYOUT_IO_H

/**
 * The editor layout file's two trips across the platform seam, the way
 * src/game/task_prefs_io.c carries preferences.ini: a task on the App's
 * queue issues ONE ToriRS_IO_QueueFileRead / QueueFileWrite and parks on it.
 * On the desktop the executor (platform_x_io.c) reads and writes the path on
 * disk; on the web the same item goes to platform_web_io.js, which keeps
 * client files in IndexedDB (host.readClientFile / host.writeClientFile,
 * src/web/torirs_hostio.js). Nothing here touches a file itself.
 */

#include "asyncio.h"
#include "editor/editor_layout.h"

/** What the load task hands back. The caller owns it and must keep it alive
 *  until `done` (the App keeps one for the process). */
struct EditorLayoutLoad
{
    /** Set by the task when it finished (found or not). */
    int done;
    /** 1 when the file existed and held a [layout] section. */
    int found;
    struct EditorLayoutFile file;
};

/** Read `path` into `out` (cleared first). A missing file is a first launch:
 *  done = 1, found = 0, nothing reported. */
struct ToriRS_Task*
CreateTask_EditorLayoutLoad(
    struct EditorLayoutLoad* out,
    const char* path);

/** Write `file` to `path`. Encoded at creation: the task writes the layout as
 *  it was when asked, whatever the panels do meanwhile. */
struct ToriRS_Task*
CreateTask_EditorLayoutSave(
    const struct EditorLayoutFile* file,
    const char* path);

#endif
