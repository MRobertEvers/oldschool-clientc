#ifndef SRC_EDITOR_EDITOR_PREFS_H
#define SRC_EDITOR_EDITOR_PREFS_H

/**
 * The editor's device-local view preferences as a file: `editor_prefs.ini`,
 * beside `editor_layout.ini` and `editor_hotkeys.ini` and resolved the same
 * way (the working directory, IndexedDB on the web; TORIRS_EDITOR_PREFS
 * overrides the path, an empty value turns the file off). Workbench step
 * 6.0b, design section 2 "R1 in full": View > Chrome scale and View > Text
 * size are "device-local prefs, not shared state" -- a 1.5x laptop and a 1x
 * monitor in one Client each keep their own, so neither travels on the
 * Client's state keys. A window popped out of this one reads the same file
 * at its boot, which is how it inherits the leader's values.
 *
 * This module is the pure half: text <-> struct Editor_Prefs. No chrome, no
 * IO; task_editor_prefs_io.h carries the text across the platform IO queue,
 * editor_ui.c applies it (Editor_UiApplyPrefs).
 *
 * The format (hand-editable; unknown sections and keys are ignored, a
 * malformed value is skipped and keeps the default):
 *
 *     [view]
 *     version = 1
 *     chrome_scale = auto   ; auto | 100 | 125 | 150 | 175 | 200 | 250 | 300
 *     text_size = normal    ; normal | large
 *     text_rendering = bitmap ; bitmap (at whole scales) | smooth (always)
 *     spawn_view = npcs objs player generated edited scripted markers wander
 *                            ; the Spawns panel's view toggles that are on
 */

#define EDITOR_PREFS_VERSION 1
/** `TORIRS_EDITOR_PREFS` unset: this name, resolved by the platform the way
 *  `editor_layout.ini` is. */
#define EDITOR_PREFS_DEFAULT_PATH "editor_prefs.ini"
/** View > Chrome scale's Auto: the display's device pixel ratio (main.c). */
#define EDITOR_PREFS_SCALE_AUTO 0

struct Editor_Prefs
{
    /** EDITOR_PREFS_SCALE_AUTO, or a step of ui/torirs_chrome_scale_q.h in
     *  quarter units (4 = 100 %, 6 = 150 %, 12 = 300 %). */
    int chrome_scale_q;
    /** View > Text size: 0 Normal (the Body face, 18 px rows at 1x), 1 Large
     *  (the Menu face as the body face, 22 px rows at 1x). */
    int text_large;
    /** View > Text rendering: 0 "bitmap at whole scales" (the baked 1x /
     *  2x / 3x faces at 100, 200 and 300 %, the synthesized vector face
     *  between), 1 "smooth always" (the vector face at every scale). */
    int text_smooth;
    /** The Spawns panel's view toggles, EDITOR_PREFS_SPAWN_*. */
    int spawn_view;
};

/** The Spawns panel's view toggles (Editor_Prefs.spawn_view). */
enum
{
    EDITOR_PREFS_SPAWN_SHOW = 1 << 0,
    EDITOR_PREFS_SPAWN_NPCS = 1 << 1,
    EDITOR_PREFS_SPAWN_OBJS = 1 << 2,
    EDITOR_PREFS_SPAWN_PLAYER = 1 << 3,
    EDITOR_PREFS_SPAWN_GENERATED = 1 << 4,
    EDITOR_PREFS_SPAWN_EDITED = 1 << 5,
    EDITOR_PREFS_SPAWN_SCRIPTED = 1 << 6,
    EDITOR_PREFS_SPAWN_SUPPRESSED = 1 << 7,
    EDITOR_PREFS_SPAWN_MARKERS = 1 << 8,
    EDITOR_PREFS_SPAWN_WANDER = 1 << 9,
    /** A first boot: everything but Show and the suppressed rows. */
    EDITOR_PREFS_SPAWN_DEFAULT = EDITOR_PREFS_SPAWN_NPCS | EDITOR_PREFS_SPAWN_OBJS | EDITOR_PREFS_SPAWN_PLAYER
                                 | EDITOR_PREFS_SPAWN_GENERATED | EDITOR_PREFS_SPAWN_EDITED
                                 | EDITOR_PREFS_SPAWN_SCRIPTED | EDITOR_PREFS_SPAWN_MARKERS
                                 | EDITOR_PREFS_SPAWN_WANDER,
};

/** Auto, Normal and bitmap at whole scales. */
void
Editor_PrefsInit(struct Editor_Prefs* prefs);

/**
 * Apply `text` (`size` bytes, not necessarily terminated) over `prefs`: the
 * keys it states replace the values there, the rest stay. Returns the number
 * of lines skipped as malformed.
 */
int
Editor_PrefsDecode(
    struct Editor_Prefs* prefs,
    const char* text,
    int size);

/** The whole file for `prefs`, ToriMem_Alloc(ToriMem_Process()) into `*out_text` (terminated;
 *  the caller ToriMem_Frees it), its length in `*out_size`. */
void
Editor_PrefsEncode(
    const struct Editor_Prefs* prefs,
    char** out_text,
    int* out_size);

/** Where the file lives: TORIRS_EDITOR_PREFS when set (NULL when it is set
 *  empty: no file), else EDITOR_PREFS_DEFAULT_PATH. */
const char*
Editor_PrefsPath(void);

#endif
