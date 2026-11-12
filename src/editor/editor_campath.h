#ifndef SRC_EDITOR_EDITOR_CAMPATH_H
#define SRC_EDITOR_EDITOR_CAMPATH_H

/**
 * CAMERA PATHS (the Camera path panel, module "campath"): a cutscene's
 * scripted camera as a list of KEYFRAMES a map maker frames in the World and
 * the editor turns into RuneScript -- `cam_moveto` / `cam_lookat` /
 * `p_delay` lines, `cam_reset` at the end -- without anyone doing the
 * arithmetic between "what the World camera shows" and "what the game's
 * camera commands take".
 *
 * WHERE IT LIVES: `<content_dir>/editor/campaths.txt` (TEXT document key
 * "editor:campaths.txt"), beside the scenes, the areas and the encounter
 * sets and for their reasons: versioned with the content it frames, a lane
 * file like any other (it publishes with the lane or dies with it), and
 * outside every pack input -- nothing that builds the game reads `editor/`.
 * The game only ever sees the proc Copy script renders, pasted into a script
 * file by the person who owns that file.
 *
 *     # ToriRSMapEd camera paths (editor/campaths.txt).
 *     # path <name> [| coords instance <mX_Z>] [| frame]
 *     # key <path> | eye <x>,<z>,<level>,<height> | look <x>,<z>,<height> | cut | hold <ticks>
 *     # key <path> | eye ... | look ... | glide <eye rate>,<eye rate2>,<look rate>,<look rate2> | hold <ticks>
 *     path lumbridge_intro | frame
 *     key lumbridge_intro | eye 3215,3211,0,1100 | look 3222,3218,180 | cut | hold 4
 *     key lumbridge_intro | eye 3230,3214,0,700 | look 3222,3219,150 | glide 5,5,5,5 | hold 8
 *
 * GRAMMAR. A line is a comment (`#` or `//` first), blank, or one record of
 * `|`-separated segments. `path` opens a path: its name ([a-z][a-z0-9_]*,
 * under 40 bytes: it becomes the proc's name), then in any order `coords
 * instance <mX_Z>` (the coordinates are emitted as offsets from that square's
 * south-west tile; absent: world tiles) and `frame` (Copy script wraps the
 * proc in the tree's cutscene frame). `key` is one keyframe of the path it
 * names, which a `path` line above it must have opened; a path's keyframes
 * play in file order. Its segments, each exactly once:
 *
 *   eye    the tile the eye stands over (ABSOLUTE tiles), the level whose
 *          ground heights are measured on (0..3: the level the player stands
 *          on when the path plays), and the eye's height in fine units (128 a
 *          tile) above the ground under that tile's centre -- cam_moveto's
 *          own arguments.
 *   look   the tile looked at and the height above its ground of the point
 *          looked at -- cam_lookat's.
 *   cut    the camera is there at once (rates 232, 232: any rate2 of 100 or
 *          more snaps), or
 *   glide  it eases there: each client cycle (20 ms) the eye covers `rate`
 *          plus `rate2`/1000 of what is left, per axis, and the angles turn
 *          by the look pair the same way (src/app/app_camera.c
 *          app_world_camera_cinema). 0..255 each, rate2 under 100.
 *   hold   game ticks (0.6 s) from this keyframe's commands to the next
 *          keyframe's (or to the end), 1..500: `p_delay(hold - 1)`.
 *
 * LINE-PRESERVING, like editor_encounters.c: every line keeps its own bytes,
 * so parse -> emit is byte-identical (comments, blank lines, CRLF, a missing
 * final newline). A verb (add a keyframe, retime one, rename, delete) is the
 * whole new text of the document planned from the old
 * (Editor_CamPathsTextWith / Without) and written as TEXT_SPLICE commands
 * (Editor_StampSplices: the common head and tail kept), ONE undo step. A
 * path the editor writes is its `path` line followed by its `key` lines, where
 * its `path` line stood. The first path added to an empty document brings the
 * header lines above with it and removing the last one of a document that is
 * exactly that header takes them back.
 *
 * THE MATHS (pure, below). The World camera is an EYE: a point in absolute
 * fine units (x east, z north, y DOWN -- up is negative, the scene's own
 * sense), a yaw and a pitch in the client's angle units (2048 a turn; yaw 0
 * looks north, pitch 0 is level and 512 straight down). The game's camera is
 * told an eye TILE with a height and a look TILE with a height, and derives
 * its angles from the two points. A keyframe is the second form;
 * Editor_CamKeyFromEye fits one to an eye and Editor_CamKeyEye says exactly
 * where the game will stand and look for one. Both read the terrain through
 * a callback, so heights are right on a slope: a height is measured above
 * the ground under ITS OWN tile's centre.
 *
 * Pure: no document, no host, no App. Malformed text is data (the parser
 * reports the line); NULL arguments assert.
 */

#include <stddef.h>

/** The TEXT document key, and its path relative to the content dir
 *  (Editor_HostDocPath maps "editor:<file>" to "editor/<file>"). */
#define EDITOR_CAMPATHS_DOC_KEY "editor:campaths.txt"
#define EDITOR_CAMPATHS_FILE_PATH "editor/campaths.txt"

/** Bytes of a path's name, the terminator included. */
#define EDITOR_CAMPATH_NAME_MAX 40
/** Keyframes one path holds, and paths the list helpers walk. */
#define EDITOR_CAMPATH_KEYS_MAX 48
#define EDITOR_CAMPATHS_MAX 64
/** Tiles are 0..EDITOR_CAMPATH_TILE_MAX; heights are cam_moveto's u16. */
#define EDITOR_CAMPATH_TILE_MAX 16383
#define EDITOR_CAMPATH_HEIGHT_MAX 65535
#define EDITOR_CAMPATH_RATE_MAX 255
#define EDITOR_CAMPATH_HOLD_MAX 500
/** A cut's rate pair, as the tree's scripts write it; and the rate2 from
 *  which the client snaps instead of easing (rs_gameproto_exec.c). */
#define EDITOR_CAMPATH_CUT_RATE 232
#define EDITOR_CAMPATH_SNAP_RATE2 100
/** Client cycles (20 ms) in a game tick (600 ms). */
#define EDITOR_CAMPATH_CYCLES_PER_TICK 30
#define EDITOR_CAMPATH_CYCLE_MS 20
/** The scripted camera's pitch range (revconfig [camera] pitch_flattest /
 *  pitch_steepest for every OSRS revision here), and the range after
 *  `cam_unlock(true)` (app_camera.c APP_CAM_UNLOCK_PITCH_*). */
#define EDITOR_CAMPATH_PITCH_MIN 128
#define EDITOR_CAMPATH_PITCH_MAX 383
#define EDITOR_CAMPATH_UNLOCK_PITCH_MIN 0
#define EDITOR_CAMPATH_UNLOCK_PITCH_MAX 520
/** Bytes Editor_CamPathScript writes at most for a full path, framed. */
#define EDITOR_CAMPATH_SCRIPT_MAX 16384

struct Editor_CamKey
{
    /** ABSOLUTE tiles of the eye, the level heights are measured on, and
     *  the eye's height above the ground under the tile's centre (fine
     *  units). */
    int eye_x;
    int eye_z;
    int level;
    int eye_height;
    /** The tile looked at and the height of the point looked at. */
    int look_x;
    int look_z;
    int look_height;
    /** 0 a cut, 1 a glide at the four rates below (ignored by a cut). */
    int glide;
    int move_rate;
    int move_rate2;
    int look_rate;
    int look_rate2;
    /** Game ticks until the next keyframe's commands, 1.. */
    int hold;
};

struct Editor_CamPath
{
    char name[EDITOR_CAMPATH_NAME_MAX];
    /** 1: coordinates are offsets from square (origin_mx, origin_mz)'s
     *  south-west tile, emitted as map_instance_coord($handle, dx, dz,
     *  level). 0: world tiles. */
    int instance;
    int origin_mx;
    int origin_mz;
    /** Copy script wraps the proc in the cutscene frame. */
    int frame;
    struct Editor_CamKey keys[EDITOR_CAMPATH_KEYS_MAX];
    int key_count;
};

enum Editor_CamPathsLineKind
{
    /** Blank or a comment: carried, never interpreted. */
    EDITOR_CAMPATHS_LINE_TEXT = 0,
    EDITOR_CAMPATHS_LINE_PATH,
    EDITOR_CAMPATHS_LINE_KEY,
};

struct Editor_CamPathsLine
{
    enum Editor_CamPathsLineKind kind;
    /** Owned, NUL-terminated: the line's bytes without its newline (a CR
     *  before the newline is kept, so CRLF round-trips). */
    char* text;
    /** PATH, KEY: the path the line names. */
    char name[EDITOR_CAMPATH_NAME_MAX];
    /** PATH. */
    int instance;
    int origin_mx;
    int origin_mz;
    int frame;
    /** KEY. */
    struct Editor_CamKey key;
};

struct Editor_CamPathsFile
{
    struct Editor_CamPathsLine* lines;
    int line_count;
    int line_capacity;
    /** The text ended with a newline. */
    int final_newline;
};

struct Editor_CamPathsParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/* ---- names and rows ----------------------------------------------------- */

/** [a-z][a-z0-9_]*, under EDITOR_CAMPATH_NAME_MAX: a proc's name. */
int
Editor_CamPathNameValid(const char* name);

/** Every field in range (tiles, level, heights, a glide's rates, the hold). */
int
Editor_CamKeyValid(const struct Editor_CamKey* key);

/** A valid name, a square in range when instance-relative, every keyframe
 *  valid. */
int
Editor_CamPathValid(const struct Editor_CamPath* path);

/** An empty world-tiles path named `name` (which must be valid). */
void
Editor_CamPathInit(
    struct Editor_CamPath* path,
    const char* name);

/** The `path` line the editor writes. snprintf's contract. */
size_t
Editor_CamPathFormatHeader(
    const struct Editor_CamPath* path,
    char* out,
    size_t out_capacity);

/** The `key` line the editor writes for a keyframe of path `name`. */
size_t
Editor_CamKeyFormat(
    const char* name,
    const struct Editor_CamKey* key,
    char* out,
    size_t out_capacity);

/* ---- the file ----------------------------------------------------------- */

void
Editor_CamPathsInit(struct Editor_CamPathsFile* file);

/** Frees every line and zeroes the file. NULL-tolerant. */
void
Editor_CamPathsFree(struct Editor_CamPathsFile* file);

/** Parse, replacing what `file` held. NULL with length 0 is an empty file.
 *  A malformed record, a path opened twice, a keyframe of a path no line
 *  above it opened, or a path's 49th keyframe is a failure. */
struct Editor_CamPathsParseResult
Editor_CamPathsParse(
    struct Editor_CamPathsFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_CamPathsEmit(
    const struct Editor_CamPathsFile* file,
    char* out,
    size_t out_capacity);

/** Paths the file holds, in the order of their `path` lines. */
int
Editor_CamPathsCount(const struct Editor_CamPathsFile* file);

/** The `nth` path's name (a pointer into the file), NULL past the end. */
const char*
Editor_CamPathsName(
    const struct Editor_CamPathsFile* file,
    int nth);

/** Path `name` with its keyframes into `out`; 0 when no line opens it. */
int
Editor_CamPathsGet(
    const struct Editor_CamPathsFile* file,
    const char* name,
    struct Editor_CamPath* out);

/* ---- the verbs: the document's new text ---------------------------------- */

/**
 * The text of `file` with `path` written: in place of the path named
 * `old_name` (every line of it replaced where its `path` line stood; a
 * rename when the names differ), or, with `old_name` NULL or "", appended
 * (the header lines first when the file holds no line at all). Writes the
 * new text into `out` (snprintf's contract) and its length into
 * `*out_length`. Returns 1, or 0 -- nothing written -- when the path is not
 * valid, `old_name` names no path, or another path holds the name.
 */
int
Editor_CamPathsTextWith(
    const struct Editor_CamPathsFile* file,
    const char* old_name,
    const struct Editor_CamPath* path,
    char* out,
    size_t out_capacity,
    size_t* out_length);

/** The text of `file` without path `name` (every line of it); a file left
 *  exactly the header lines becomes empty. 0 when there is no such path. */
int
Editor_CamPathsTextWithout(
    const struct Editor_CamPathsFile* file,
    const char* name,
    char* out,
    size_t out_capacity,
    size_t* out_length);

/* ---- the maths ------------------------------------------------------------ */

/** A camera as the renderer holds one: where the eye is and which way it
 *  faces. x, z ABSOLUTE fine units (128 a tile); y the scene's, DOWN (an eye
 *  above the ground has the smaller y); yaw and pitch 2048 a turn. */
struct Editor_CamEye
{
    int x;
    int y;
    int z;
    int yaw;
    int pitch;
};

/** The ground's y at absolute fine (x, z) on `level`: 1 with `*out_y`, 0
 *  when that point is outside the terrain the caller holds. */
typedef int (*Editor_CamGroundFn)(void* user, int fine_x, int fine_z, int level, int* out_y);

enum Editor_CamFit
{
    EDITOR_CAM_FIT_OK = 0,
    /** The eye is over no loaded terrain: its height cannot be measured. */
    EDITOR_CAM_FIT_EYE_OFF_TERRAIN,
    /** The eye's tile is outside 0..EDITOR_CAMPATH_TILE_MAX. */
    EDITOR_CAM_FIT_EYE_OFF_WORLD,
    /** The eye looks above the horizon, which the game's camera cannot. */
    EDITOR_CAM_FIT_LOOKS_UP,
};

/** What a fit had to give up, for the panel to say. */
struct Editor_CamFitNote
{
    /** The eye was under the ground of its tile's centre and stands on it. */
    int eye_raised;
    /** The eye looks so nearly straight down that the look tile is the
     *  eye's neighbour and the pitch the steepest that tile allows. */
    int steep;
    /** How far the fitted camera's yaw and pitch are from the eye's, in
     *  angle units (the eye and the look point both snap to tile centres). */
    int yaw_error;
    int pitch_error;
};

/**
 * The keyframe that frames what `eye` frames: the eye tile and height under
 * it, and the look tile and height that point the game's camera along the
 * eye's view ray -- the tile whose centre lies nearest the line from the
 * snapped eye to the SUBJECT (where the ray meets the ground of `level`, or
 * 16 tiles out when it meets none), with the height that puts the look point
 * on that line. Fills the eye and look fields of `out_key` and leaves the
 * rest (glide, rates, hold) as they were. `note` may be NULL.
 */
enum Editor_CamFit
Editor_CamKeyFromEye(
    const struct Editor_CamEye* eye,
    int level,
    Editor_CamGroundFn ground,
    void* user,
    struct Editor_CamKey* out_key,
    struct Editor_CamFitNote* note);

/**
 * Where the game's camera stands and faces once it has arrived at `key`:
 * the eye over the eye tile's centre, the yaw and pitch the client derives
 * from eye and look point (app_camera.c app_cinema_angles_to), NOT clamped
 * (Editor_CamPitchClamp). 0 when either tile is outside the terrain.
 */
int
Editor_CamKeyEye(
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    struct Editor_CamEye* out_eye);

/** The scene point a keyframe's look half names (absolute fine x, z; y
 *  down). 0 when the tile is outside the terrain. */
int
Editor_CamKeyLookPoint(
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    int* out_x,
    int* out_y,
    int* out_z);

/** The yaw and pitch that point an eye at (x, y, z) at a target, as the
 *  client computes them: truncated, the pitch masked to 0..2047 (so an eye
 *  below its target reads as a pitch near 2047). */
void
Editor_CamAnglesTo(
    int eye_x,
    int eye_y,
    int eye_z,
    int target_x,
    int target_y,
    int target_z,
    int* out_pitch,
    int* out_yaw);

/** `pitch` held to the scripted camera's range: the locked one, or the one
 *  `cam_unlock(true)` opens. */
int
Editor_CamPitchClamp(
    int pitch,
    int unlocked);

/** 1 when `pitch` is outside the locked range: the script must unlock the
 *  camera for the game to frame it. */
int
Editor_CamPitchNeedsUnlock(int pitch);

/* ---- the client's scripted camera, stepped --------------------------------
 *
 * app_camera.c's app_world_camera_cinema as a pure state machine, so the
 * panel's Preview flies the World camera the way the game will: MoveTo and
 * LookAt are the packets (a rate2 of 100 or more snaps), Step is one client
 * cycle. Coordinates are absolute fine units; the easing only ever reads
 * differences, so it matches the client's scene-relative walk.
 */
struct Editor_CamSim
{
    struct Editor_CamEye eye;
    /** cam_unlock(true) is in force: the wider pitch range. */
    int unlocked;
    int move_set;
    int move_x;
    int move_y;
    int move_z;
    int move_rate;
    int move_rate2;
    int look_set;
    int look_x;
    int look_y;
    int look_z;
    int look_rate;
    int look_rate2;
};

/** A camera at rest at `eye`, no target yet. */
void
Editor_CamSimInit(
    struct Editor_CamSim* sim,
    const struct Editor_CamEye* eye,
    int unlocked);

void
Editor_CamSimMoveTo(
    struct Editor_CamSim* sim,
    int x,
    int y,
    int z,
    int rate,
    int rate2);

void
Editor_CamSimLookAt(
    struct Editor_CamSim* sim,
    int x,
    int y,
    int z,
    int rate,
    int rate2);

/** One client cycle. */
void
Editor_CamSimStep(struct Editor_CamSim* sim);

/** 1 when the eye is on its target and the angles on theirs. */
int
Editor_CamSimArrived(const struct Editor_CamSim* sim);

/** One axis of the client's ease: a flat `rate` plus `rate2`/1000 of what is
 *  left, never overshooting (app_camera.c app_cinema_ease). */
int
Editor_CamEase(
    int current,
    int target,
    int rate,
    int rate2);

/** The same for a yaw, the short way round (app_cinema_ease_yaw). */
int
Editor_CamEaseYaw(
    int current,
    int target,
    int rate,
    int rate2);

/**
 * Client cycles the camera takes from rest at `from` to arrive at `key`
 * (eye and angles), by stepping the simulation; 0 for a cut. -1 when it has
 * not arrived after `max_cycles` (a rate of 0 never closes the last
 * 1000/rate2 units) or a tile is outside the terrain.
 */
int
Editor_CamGlideCycles(
    const struct Editor_CamEye* from,
    const struct Editor_CamKey* key,
    Editor_CamGroundFn ground,
    void* user,
    int unlocked,
    int max_cycles);

/** Game ticks that hold at least `cycles` client cycles. */
int
Editor_CamCyclesToTicks(int cycles);

/* ---- the speed presets ---------------------------------------------------- */

enum Editor_CamSpeed
{
    EDITOR_CAM_SPEED_SLOW = 0,
    EDITOR_CAM_SPEED_MEDIUM,
    EDITOR_CAM_SPEED_FAST,
    EDITOR_CAM_SPEED_PRESETS,
    /** Rates no preset names. */
    EDITOR_CAM_SPEED_CUSTOM = EDITOR_CAM_SPEED_PRESETS,
};

/** "Slow", "Medium", "Fast", "Custom". */
const char*
Editor_CamSpeedName(int speed);

/** A preset's four rates into `key` (which becomes a glide). The pairs are
 *  the ones the tree's own cutscenes pan with: 2,2 crosses ten tiles in
 *  about eight seconds, 5,5 in about three, 10,10 in under two. */
void
Editor_CamSpeedApply(
    int preset,
    struct Editor_CamKey* key);

/** The preset whose rates `key` holds, else EDITOR_CAM_SPEED_CUSTOM. */
int
Editor_CamSpeedOf(const struct Editor_CamKey* key);

/* ---- the script ------------------------------------------------------------ */

/** A coord literal for ABSOLUTE tile (x, z) on `level`: "0_50_50_22_18". */
void
Editor_CamCoordText(
    int x,
    int z,
    int level,
    char* out,
    size_t out_size);

/** A keyframe's eye or look coordinate as the script states it: the coord
 *  literal, or `map_instance_coord($handle, dx, dz, level)` for an
 *  instance-relative path. */
void
Editor_CamPathCoordText(
    const struct Editor_CamPath* path,
    int x,
    int z,
    int level,
    char* out,
    size_t out_size);

/**
 * The path as a complete RuneScript proc, `[proc,<name>_camera]` (taking
 * `(int $handle)` when instance-relative): per keyframe a cam_moveto, a
 * cam_lookat and the p_delay that holds it, then cam_reset. `unlock` adds
 * `cam_unlock(true);` before the first keyframe and `cam_unlock(false);`
 * before the reset: the caller passes 1 when a keyframe's pitch is outside
 * the locked range (or cannot be measured). A path with `frame` set is
 * wrapped in the tree's cutscene frame (general/scripts/cutscene_frame.rs2:
 * fade out, lock, HUD lock, the first keyframe, fade in ... fade out, unlock,
 * HUD unlock -- which resets the camera itself -- fade in). snprintf's
 * contract; EDITOR_CAMPATH_SCRIPT_MAX always holds it.
 */
size_t
Editor_CamPathScript(
    const struct Editor_CamPath* path,
    int unlock,
    char* out,
    size_t out_capacity);

/** Game ticks the proc runs from its first command to its last (the holds,
 *  and the frame's own ticks when framed). */
int
Editor_CamPathTicks(const struct Editor_CamPath* path);

/** The `::cam` cheat that holds keyframe `key` on the play client
 *  (general/scripts/misc/cheat_cam.rs2), without the "::": "cam 0_50_50_22_18
 *  900 0_50_50_25_30 120 232 232". A glide sends its eye pair for both
 *  halves: the cheat takes one. */
void
Editor_CamKeyCheat(
    const struct Editor_CamKey* key,
    char* out,
    size_t out_size);

/** A list row for keyframe `index` (0-based; shown 1-based): how it moves and
 *  holds, then where it stands and looks (the level only when not 0):
 *  "1  cut, hold 4  eye 3215,3211 +1100  look 3222,3218 +180",
 *  "2  Medium, hold 8  eye 3230,3214 L1 +700  look 3222,3219 +150" (a preset
 *  glide by its speed alone), "3  glide 5,5/9,5, hold 8  eye ..." (rates no
 *  preset names). */
void
Editor_CamKeyRowText(
    const struct Editor_CamKey* key,
    int index,
    char* out,
    size_t out_size);

#endif
