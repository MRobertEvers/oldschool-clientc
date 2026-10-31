#ifndef SRC_EDITOR_TASK_EDITOR_HOTKEYS_IO_H
#define SRC_EDITOR_TASK_EDITOR_HOTKEYS_IO_H

/**
 * editor_hotkeys.ini's two trips across the platform seam, exactly as
 * task_editor_layout_io.h carries editor_layout.ini: a task on the App's
 * queue issues ONE ToriRS_IO_QueueFileRead / QueueFileWrite and parks on it.
 * The desktop executor reads and writes the file; the web keeps it in
 * IndexedDB (host.readClientFile / host.writeClientFile). Nothing here
 * touches a file itself.
 */

#include "asyncio.h"
#include "editor/editor_hotkeys.h"

/** What the load task hands back; the caller keeps it alive until `done`. */
struct EditorHotkeysLoad
{
    int done;
    /** 1 when the file existed and held text. */
    int found;
    /** The defaults with the file's lines applied (Editor_HotkeysDecode). */
    struct Editor_Hotkeys hotkeys;
    struct Editor_HotkeysReport report;
};

/** Read `path` into `out`. A missing file is a first launch: done = 1,
 *  found = 0, `hotkeys` the defaults. */
struct ToriRS_Task*
CreateTask_EditorHotkeysLoad(
    struct EditorHotkeysLoad* out,
    const char* path);

/** Write `hotkeys` (its non-default bindings) to `path`. Encoded at creation. */
struct ToriRS_Task*
CreateTask_EditorHotkeysSave(
    const struct Editor_Hotkeys* hotkeys,
    const char* path);

#endif
