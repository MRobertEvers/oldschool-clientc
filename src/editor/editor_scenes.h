#ifndef SRC_EDITOR_EDITOR_SCENES_H
#define SRC_EDITOR_EDITOR_SCENES_H

/**
 * The SCENES document (EDITOR_DOC_SCENES, key "scenes"; workbench 6.5,
 * design #lanes "What a scene stores"): named snapshots of "what I was
 * looking at and testing" -- the squares open, the World's camera, the
 * pinned level and Above, the lane, the var preview and the play account --
 * so a builder can leave an encounter half-tuned, fix a door across the map
 * and come back to exactly that. Shared by the Client (every window mirrors
 * it, as the AREAS document) and by everyone who edits the tree.
 *
 * WHERE IT LIVES: `<content_dir>/editor/scenes.txt`, beside the areas and the
 * palettes and for their reasons: versioned with the content it names, read
 * by no pack step (a scene can never change a pack), and a lane file like any
 * other -- a scene saved in a scratch lane publishes with the lane or dies
 * with it.
 *
 *     # ToriRSMapEd scenes (editor/scenes.txt). One record per line, |-separated segments.
 *     # scene <name> | squares <key> ... | camera <x>,<z>,<level>,<yaw>,<pitch>,<zoom>
 *     # scene <name> | level <0..3|auto> [above] | lane <name> | preview <var>=<value> ... | account <name> at <x>,<z>,<level>
 *     # scene <name> | note <text>
 *     scene lumbridge-smithy | squares m50_50 m50_51 | camera 3222.5,3218,1,512,400,1800
 *     scene lumbridge-smithy | level 1 above | lane working | preview varb1200=1 | account maped43620 at 3224,3219,1
 *     scene lumbridge-smithy | note Fitting the forge walls; the anvil loc is a stamp
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record:
 * `scene`, the scene's name, then `|`-separated segments in any order. A
 * scene is the union of its lines; each SEGMENT KIND (squares, camera, level,
 * lane, preview, account, note) is held by at most one line of a scene, and
 * a bare `scene <name>` line (no segment: a header) at most once -- that is
 * the key every by-line command finds a line by: (name, the set of segment
 * kinds it holds). A name is 1..63 printable bytes, no space, no `|`.
 *
 *   squares   1..16 square keys, the picker's ("m50_50",
 *             "ported/<lane>/maps/m16_101").
 *   camera    the World's orbit pose: the point looked at as ABSOLUTE tiles
 *             with an optional fraction (3222.5; held in fine units, 128 a
 *             tile, written with the fewest digits that read back exactly),
 *             its level 0..3, yaw and pitch 0..2047, the eye distance in fine
 *             units 0..65535.
 *   level     the pinned edit level 0..3, or `auto`; `above` draws the
 *             levels over the pin too.
 *   lane      the editor lane the scene was made on (torirs_maped_lanes.h's
 *             names, "main" included).
 *   preview   1..5 `<var>=<value>` pairs, STATE_VAR_PREVIEW's entries
 *             (builder_shell.h): `varp<id>` or `varb<id>`, optionally with
 *             the tree's `_name` suffix; the value a signed integer.
 *   account   the lane account the live link teleports on Enter, and where.
 *   note      free text, no `|`.
 *
 * LINE-PRESERVING, like editor_palette.c: parse -> emit is byte-identical
 * (comments, blank lines, CRLF, a missing final newline), a save touches only
 * the edited rows, and an undo puts a row back byte for byte. The editor
 * writes a scene as up to three lines in the order above (Editor_ScenesFormat:
 * where it looks, what it pins, the note). A new line of a scene goes after
 * its last line, a new scene at the end; the first record added to an empty
 * file brings the four header lines above with it and removing the last
 * record of a file that is exactly that header takes them back.
 *
 * COMMANDS (editor_cmd.h): SCENE_SET upserts one line (has_after; has_before
 * too when it rewrites a line, whose exact text it carries), SCENE_REMOVE
 * carries the removed line and the index it stood at. A verb -- Save, Update,
 * Rename, Delete -- is the commands of one plan (Editor_ScenePlan*), applied
 * as ONE stroke. Both ride in `u.area` (`i32 line, string before, string
 * after` on the wire), as the AREAS and PALETTES commands do.
 *
 * Pure: no document, no content tree, no host. Malformed text is data (the
 * parser reports the line); NULL arguments assert.
 */

#include "editor_cmd.h"

#include <stddef.h>
#include <stdint.h>

#define EDITOR_SCENES_DOC_KEY "scenes"
/** Relative to the content dir (editor_host_local.c Editor_HostDocPath). */
#define EDITOR_SCENES_FILE_PATH "editor/scenes.txt"

/** Bytes of a scene / lane / account name, a note and one square key, the
 *  terminator included. */
#define EDITOR_SCENE_NAME_MAX 64
#define EDITOR_SCENE_LANE_MAX 48
#define EDITOR_SCENE_ACCOUNT_MAX 32
#define EDITOR_SCENE_NOTE_MAX 160
#define EDITOR_SCENE_KEY_MAX 64
#define EDITOR_SCENE_VAR_MAX 48
/** Squares and preview entries one scene holds. */
#define EDITOR_SCENE_SQUARES_MAX 16
#define EDITOR_SCENE_PREVIEW_MAX 5
/** Bytes of one record line, the terminator excluded: two ride in one
 *  command (EDITOR_CMD_SPLICE_MAX). */
#define EDITOR_SCENES_LINE_MAX (EDITOR_CMD_SPLICE_MAX / 2 - 1)
/** Lines the editor writes for one scene, and so the commands a Save,
 *  Update or Delete of an editor-written scene needs at most; a rename or
 *  delete of a hand-written scene takes one per line (at most one per
 *  segment kind plus the header). */
#define EDITOR_SCENE_FORMAT_LINES 3
#define EDITOR_SCENE_PLAN_MAX 16
/** Tiles are 0..EDITOR_SCENE_TILE_MAX; fine units are 128 a tile. */
#define EDITOR_SCENE_TILE_MAX 65535
#define EDITOR_SCENE_ZOOM_MAX 65535
/** Scenes the list helpers walk at most. */
#define EDITOR_SCENES_MAX 256

/** The segment kinds, as bits: a line's key is its name and its mask. */
enum Editor_SceneSegment
{
    EDITOR_SCENE_SEG_SQUARES = 1u << 0,
    EDITOR_SCENE_SEG_CAMERA = 1u << 1,
    EDITOR_SCENE_SEG_LEVEL = 1u << 2,
    EDITOR_SCENE_SEG_LANE = 1u << 3,
    EDITOR_SCENE_SEG_PREVIEW = 1u << 4,
    EDITOR_SCENE_SEG_ACCOUNT = 1u << 5,
    EDITOR_SCENE_SEG_NOTE = 1u << 6,
    EDITOR_SCENE_SEG_ALL = 0x7fu,
};

enum Editor_ScenesLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_SCENES_LINE_TEXT = 0,
    EDITOR_SCENES_LINE_RECORD,
};

/** The World's orbit pose (editor_view.h EditorViewCamera's fields). */
struct Editor_SceneCamera
{
    /** ABSOLUTE fine units (tile * 128) of the point looked at. */
    int32_t x;
    int32_t z;
    int32_t level;
    int32_t yaw;
    int32_t pitch;
    int32_t zoom;
};

struct Editor_ScenePreview
{
    /** "varp29", "varb1200_golem_a". */
    char var[EDITOR_SCENE_VAR_MAX];
    int32_t value;
};

/**
 * One line's segments, or a whole scene (Editor_ScenesGet: the union of its
 * lines). `segments` says which fields hold anything; the rest are zero.
 */
struct Editor_Scene
{
    char name[EDITOR_SCENE_NAME_MAX];
    unsigned segments;

    int square_count;
    char squares[EDITOR_SCENE_SQUARES_MAX][EDITOR_SCENE_KEY_MAX];

    struct Editor_SceneCamera camera;

    /** 0..3, or -1 for Auto; `above` only with a pinned level. */
    int level;
    int above;

    char lane[EDITOR_SCENE_LANE_MAX];

    int preview_count;
    struct Editor_ScenePreview preview[EDITOR_SCENE_PREVIEW_MAX];

    char account[EDITOR_SCENE_ACCOUNT_MAX];
    int account_x;
    int account_z;
    int account_level;

    char note[EDITOR_SCENE_NOTE_MAX];
};

struct Editor_ScenesLine
{
    enum Editor_ScenesLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
    /** Owned; set for a record, NULL for TEXT. */
    struct Editor_Scene* row;
};

struct Editor_ScenesFile
{
    struct Editor_ScenesLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_ScenesParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/* ---- the file ---------------------------------------------------------- */

void
Editor_ScenesInit(struct Editor_ScenesFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_ScenesFree(struct Editor_ScenesFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file.
 *  A segment kind a scene's earlier line holds, or a second header, is a
 *  failure. */
struct Editor_ScenesParseResult
Editor_ScenesParse(
    struct Editor_ScenesFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_ScenesEmit(
    const struct Editor_ScenesFile* file,
    char* out,
    size_t out_capacity);

/** `dst` becomes a copy of `src` (Free'd first). */
void
Editor_ScenesCopy(
    struct Editor_ScenesFile* dst,
    const struct Editor_ScenesFile* src);

/** Distinct scenes, in the order of each one's first line. */
int
Editor_ScenesCount(const struct Editor_ScenesFile* file);

/** The `nth` scene's name (a pointer into the file), NULL past the end. */
const char*
Editor_ScenesName(
    const struct Editor_ScenesFile* file,
    int nth);

/** 1 when any line names scene `name`. */
int
Editor_ScenesHas(
    const struct Editor_ScenesFile* file,
    const char* name);

/** Scene `name` as the union of its lines into `out`; 0 when no line names
 *  it. */
int
Editor_ScenesGet(
    const struct Editor_ScenesFile* file,
    const char* name,
    struct Editor_Scene* out);

/* ---- rows ------------------------------------------------------------------ */

/** A scene name the grammar takes: 1..63 printable bytes, no space, no `|`,
 *  not starting with `#` or `//`. */
int
Editor_SceneNameValid(const char* name);

/** A preview var: `varp<digits>` or `varb<digits>`, optionally `_<name>`
 *  after, under EDITOR_SCENE_VAR_MAX, no space. */
int
Editor_SceneVarValid(const char* var);

/** `into` takes the segments `from` holds (its own other segments kept):
 *  what a partial edit -- torirsmapedctl's scene-set, a Holds chip -- does to
 *  a scene before it is saved. */
void
Editor_SceneMerge(
    struct Editor_Scene* into,
    const struct Editor_Scene* from);

/** Drop the segments in `mask` (their fields zeroed, the level Auto). */
void
Editor_SceneClear(
    struct Editor_Scene* scene,
    unsigned mask);

/** An empty scene named `name` (no segment). */
void
Editor_SceneInitRow(
    struct Editor_Scene* out,
    const char* name);

/** Every set segment in range, the name valid. (Each formatted line's length
 *  is Editor_ScenesFormat's to check.) */
int
Editor_SceneValid(const struct Editor_Scene* scene);

/** The segments of `scene` in `mask`, as one line ("scene <name> | ...").
 *  snprintf's contract; returns the length. A mask of 0 is the header. */
int
Editor_SceneFormatLine(
    const struct Editor_Scene* scene,
    unsigned mask,
    char* out,
    size_t out_capacity);

/** The lines the editor writes for `scene`: where it looks (squares,
 *  camera), what it pins (level, lane, preview, account), the note -- each
 *  only when it holds a set segment; a scene with none is its header.
 *  Returns the line count (1..EDITOR_SCENE_FORMAT_LINES) and each line's
 *  mask, or 0 when a line would be longer than EDITOR_SCENES_LINE_MAX. */
int
Editor_ScenesFormat(
    const struct Editor_Scene* scene,
    char lines[EDITOR_SCENE_FORMAT_LINES][EDITOR_SCENES_LINE_MAX + 1],
    unsigned masks[EDITOR_SCENE_FORMAT_LINES]);

/** Parse one line on its own. Returns the kind (TEXT for a blank or comment
 *  line), or -1 when it does not parse. `out_row` (may be NULL) receives a
 *  record's segments. */
int
Editor_ScenesParseLine(
    const char* text,
    struct Editor_Scene* out_row);

/** The camera's x / z as written: "3222.5" for fine units 412480 + 64. */
void
Editor_SceneFormatFine(
    int32_t fine,
    char* out,
    size_t out_size);

/** A one-line summary for a list row: "m50_50 m50_51 | L1 above | working",
 *  "m50_50 | L0 | preview varp29=11". */
void
Editor_SceneSummary(
    const struct Editor_Scene* scene,
    char* out,
    size_t out_size);

/* ---- commands and plans ----------------------------------------------- */

/**
 * The commands of one verb, planned against a copy of `file` so each holds
 * against the file as the ones before it left it: apply them in order as ONE
 * stroke. Each returns how many it wrote into `out` (capacity `capacity`;
 * EDITOR_SCENE_PLAN_MAX is always enough), 0 when the verb does not fit:
 *
 *   Save    `scene` becomes the scene of its name: a new scene's lines are
 *           added at the end; an existing one's are rewritten -- its first
 *           line keeps its place, a line already exactly as the editor would
 *           write it is kept, the rest go, the missing ones follow its first.
 *           0 when nothing would change, `scene` is not valid, or a line
 *           would be too long.
 *   Rename  every line of `old_name` to `new_name` (taken or invalid: 0).
 *   Delete  every line of `name`.
 */
int
Editor_ScenePlanSave(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const struct Editor_Scene* scene);

int
Editor_ScenePlanRename(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const char* old_name,
    const char* new_name);

int
Editor_ScenePlanDelete(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_ScenesFile* file,
    const char* name);

/** The command's before / after line ("" when it has none). Writes at most
 *  `out_size` bytes, NUL-terminated. */
void
Editor_ScenesCmdLine(
    const struct Editor_Cmd* command,
    int after,
    char* out,
    size_t out_size);

/**
 * Apply a SCENE_* command to `file`, forward or (`inverse`) backward.
 * Returns 1 when the file changed; 0 when it does not fit: the line to
 * rewrite or remove is not there with exactly the command's bytes, or a
 * segment kind the line to add holds is held by another line of its scene.
 */
int
Editor_ScenesApply(
    struct Editor_ScenesFile* file,
    const struct Editor_Cmd* command,
    int inverse);

/** The History row for one SCENE_* command: "Save scene X", "Update scene
 *  X", "Rename scene X to Y", "Delete scene X". A verb of several lines is
 *  named by the window (Editor_HistoryName) before its first command. */
void
Editor_ScenesCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size);

#endif
