#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_LANES_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_LANES_H

/*
 * Editor lanes: where edits land, and publishing them (owner, 2026-11-02:
 * "Edits should go to a dedicated 'working' lane and then there should be a
 * publish menu that publishes to a target lane or a new lane").
 *
 * A LANE is a content tree to edit plus the live directory its RUN builds
 * into (packs, client cache, the lane's servers' saves and logs):
 *
 *   main       the real tree (OSRS-Content/osrs-content) and build/live/default
 *   <name>     <repo>/build/lanes/<name>/{lane.ini, content/, live/}
 *
 * A named lane's content is a copy-on-write MIRROR of its base (the main
 * tree): real directories holding one symlink per file, directory symlinks
 * for the large pure-asset trees nothing writes (models, sprites, ...), real
 * copies of pack/ (the id allocation rewrites its ledgers in place), and no
 * pack outputs or locks (maped/test/live_e2e/mirror.py's rules, ported). A
 * save writes a temporary beside the target and renames it over the symlink,
 * so an edited file becomes a real file in the mirror and the base is
 * untouched; the lane's CHANGES are exactly its real files that differ from
 * the base (modified / added) and the base files it no longer has (deleted).
 *
 * PUBLISH writes those changes into a target lane's content -- the main
 * tree, another lane, or a new lane mirrored from main first -- and, when
 * the target is the lane's own base, re-links the published files so the
 * working lane is clean again. DISCARD re-links every change. The main
 * tree's editor-session lock is honoured: a tree another editor serves is
 * not published into unless forced.
 *
 * Nothing here touches the wire: torirs_maped_lanes_wire.c (compiled into
 * torirs_maped.c) carries LANE_* requests (torirs_maped.h) over this.
 */

#include <stddef.h>
#include <stdint.h>

/* The wire (torirs_maped.h's numbering; requests 81..90, facts 201..210):
 *
 *   LANE_INFO     (empty)
 *          -> FACT_LANE_INFO      string name, string content, string live,
 *                                 string base, u32 is_main, u32 writable
 *   LANE_LIST     (empty)
 *          -> FACT_LANE_LIST      u32 n, n * (string name, string content,
 *                                 string base, u32 is_main, u32 is_this)
 *   LANE_CHANGES  (empty)
 *          -> FACT_LANE_CHANGES   u32 modified, u32 added, u32 deleted,
 *                                 u32 rows, rows * (u32 kind, string path,
 *                                 u32 size)   (rows capped at 1024)
 *   LANE_PUBLISH  string target (a lane name, "main"), string new_name
 *                 ("" unless a new lane is made from main first), u32 flags
 *                 (TORIRSMAPED_LANE_PUBLISH_*)
 *          -> every dirty document is saved first (FACT_SAVED), then
 *             broadcast FACT_LANE_PUBLISHED  u32 kind (1 publish, 2 discard,
 *                                 3 create), u32 status, string message,
 *                                 string target, u32 written, u32 removed,
 *                                 u32 reload (1: every window must reopen
 *                                 what it shows; the documents were dropped)
 *   LANE_DISCARD  (empty)    -> FACT_LANE_PUBLISHED (kind 2, reload 1): the
 *                 server drops every document and its history, every change
 *                 goes back to the base.
 *   LANE_CREATE   string name -> FACT_LANE_PUBLISHED (kind 3): a new lane
 *                 mirrored from the main tree, nothing published into it.
 *
 * Workbench 6.5 (the Lanes panel, the status bar's lane chip), behind
 * TORIRSMAPED_CAP_LANES_V2 -- a client sends none of this to a server
 * without the bit, which would end the session on an unknown request:
 *
 *   FACT_LANE_INFO      gains a trailing `string next_boot`: the lane
 *                       build/lanes/lane.ini names for the next boot ("" none).
 *   FACT_LANE_LIST      gains a trailing `u32 n, n * (i32 changes, string
 *                       held)`, in the rows' order: each lane's change count
 *                       against its base (-1 not counted) and who holds its
 *                       tree's editor lock ("pid N (cmd)", "" nobody).
 *                       The counts are the server's cache of each lane's
 *                       last walk unless the LANE_LIST carries `u32 flags`
 *                       with TORIRSMAPED_LANE_LIST_RESCAN (an older server
 *                       reads no body and walks every time).
 *   LANE_CHANGES  u32 flags (TORIRSMAPED_LANE_CHANGES_COUNT_ONLY) ->
 *          FACT_LANE_COUNT  string name, u32 is_main, u32 modified, u32
 *                           added, u32 deleted, string next_boot: the count
 *                           alone, for the status bar's periodic ask (an
 *                           empty body is the v1 ask: FACT_LANE_CHANGES).
 *   LANE_SWITCH   string name -> FACT_LANE_PUBLISHED (kind 4, reload 0):
 *                 the lane the NEXT boot serves, written to
 *                 <repo>/build/lanes/lane.ini (ToriRSMapEdLanes_SetBoot).
 *                 A server cannot re-point its lane while it runs -- its
 *                 tree lock, documents and history, catalog, live dir and
 *                 launched servers are all the lane's, and every session
 *                 mirrors them -- so the switch is staged and said so.
 */
enum
{
    TORIRSMAPED_REQ_LANE_INFO = 81,
    TORIRSMAPED_REQ_LANE_LIST = 82,
    TORIRSMAPED_REQ_LANE_CHANGES = 83,
    TORIRSMAPED_REQ_LANE_PUBLISH = 84,
    TORIRSMAPED_REQ_LANE_DISCARD = 85,
    TORIRSMAPED_REQ_LANE_CREATE = 86,
    TORIRSMAPED_REQ_LANE_SWITCH = 87,
    TORIRSMAPED_REQ_LANES_RANGE_FIRST = 81,
    TORIRSMAPED_REQ_LANES_RANGE_LAST = 90,

    TORIRSMAPED_FACT_LANE_INFO = 201,
    TORIRSMAPED_FACT_LANE_LIST = 202,
    TORIRSMAPED_FACT_LANE_CHANGES = 203,
    TORIRSMAPED_FACT_LANE_PUBLISHED = 204,
    TORIRSMAPED_FACT_LANE_COUNT = 205,
    TORIRSMAPED_FACT_LANES_RANGE_FIRST = 201,
    TORIRSMAPED_FACT_LANES_RANGE_LAST = 210,

    TORIRSMAPED_LANE_PUBLISHED_PUBLISH = 1,
    TORIRSMAPED_LANE_PUBLISHED_DISCARD = 2,
    TORIRSMAPED_LANE_PUBLISHED_CREATE = 3,
    TORIRSMAPED_LANE_PUBLISHED_SWITCH = 4,
    /** LANE_CHANGES' flags: answer FACT_LANE_COUNT, no rows. */
    TORIRSMAPED_LANE_CHANGES_COUNT_ONLY = 1,
    /** LANE_LIST's optional u32 flags (an empty body is 0): walk every
     *  lane's mirror again instead of answering the counts from the
     *  server's cache (the Lanes panel's Refresh). */
    TORIRSMAPED_LANE_LIST_RESCAN = 1,
    TORIRSMAPED_LANE_CHANGE_ROWS_MAX = 1024,
};

enum
{
    TORIRSMAPED_LANE_PATH = 1024,
    TORIRSMAPED_LANE_NAME_MAX = 40,
    TORIRSMAPED_LANES_MAX = 64,
};

struct ToriRSMapEdLaneDef
{
    /** "main", or the lane's directory name under build/lanes. */
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    /** <repo>/build/lanes/<name>; "" for main. */
    char dir[TORIRSMAPED_LANE_PATH];
    /** The tree this lane's editor serves and its RUN packs. */
    char content[TORIRSMAPED_LANE_PATH];
    /** The live dir its RUN builds into and launches from. */
    char live[TORIRSMAPED_LANE_PATH];
    /** The tree the content mirrors; "" for main. */
    char base[TORIRSMAPED_LANE_PATH];
    int is_main;
    /** When the mirror was made (lane.ini `created`, seconds): a base file
     *  younger than this is new in the base, not deleted by the lane. */
    long long created;
};

enum ToriRSMapEdLaneChangeKind
{
    TORIRSMAPED_LANE_CHANGE_MODIFIED = 1,
    TORIRSMAPED_LANE_CHANGE_ADDED = 2,
    TORIRSMAPED_LANE_CHANGE_DELETED = 3,
};

struct ToriRSMapEdLaneChange
{
    enum ToriRSMapEdLaneChangeKind kind;
    /** Relative to the lane's content ("maps/m50_50.jm2"). */
    char path[TORIRSMAPED_LANE_PATH];
    /** The lane's file's size (0 for a deletion). */
    uint32_t size;
};

struct ToriRSMapEdLaneCounts
{
    int modified;
    int added;
    int deleted;
    /** Files written / removed by a publish or a discard. */
    int written;
    int removed;
};

enum
{
    /** Publish: re-link the published files in the source lane when the
     *  target is its base (the working lane is clean afterwards). */
    TORIRSMAPED_LANE_PUBLISH_RESET = 1,
    /** Publish into the main tree although another editor holds its lock. */
    TORIRSMAPED_LANE_PUBLISH_FORCE = 2,
};

/** 1 when `name` can be a lane directory: [a-z0-9_-], 1..40 chars, and not
 *  "main". */
int
ToriRSMapEdLanes_NameValid(const char* name);

/** The main lane: `base_tree` itself and <repo>/build/live/default. */
void
ToriRSMapEdLanes_Main(
    const char* repo_root,
    const char* base_tree,
    struct ToriRSMapEdLaneDef* out);

/** A lane by name: "main", or one whose lane.ini exists. 1 found. */
int
ToriRSMapEdLanes_Lookup(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out);

/** Create <repo>/build/lanes/<name>: lane.ini, the content mirror of
 *  `base_tree`, an empty live dir. 0 with `why` on a bad name, a lane that
 *  exists, or a mirror that failed. */
int
ToriRSMapEdLanes_Create(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out,
    char* why,
    size_t why_size);

/** Lookup, else Create: what an editor server does with `--lane <name>`. */
int
ToriRSMapEdLanes_Open(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    struct ToriRSMapEdLaneDef* out,
    char* why,
    size_t why_size);

/** Every lane: main first, then the lanes under build/lanes by name.
 *  Returns the count. */
int
ToriRSMapEdLanes_List(
    const char* repo_root,
    const char* base_tree,
    struct ToriRSMapEdLaneDef* out,
    int max);

/** 1 when the mirror entry at `path` is still a link to its base, by the rule
 *  the changes scan uses: a symbolic link, or on Windows also a junction or a
 *  file with more than one hard link (ToriRSMapEdLanes_Mirror's fallbacks). */
int
ToriRSMapEdLanes_IsLink(const char* path);

/** `link_path` made a link to the directory `target` the way a mirror links
 *  one: a symbolic link, or on Windows where the account may not make one
 *  (Developer Mode off) a directory junction. 1 on success, 0 with errno. */
int
ToriRSMapEdLanes_LinkDirectory(
    const char* target,
    const char* link_path);

/** The mirror itself (mirror.py's rules). `dst` must not exist. Returns the
 *  number of files mirrored, -1 with `why`. */
int
ToriRSMapEdLanes_Mirror(
    const char* base,
    const char* dst,
    char* why,
    size_t why_size);

/**
 * The lane's changes against its base, sorted by path. `out` may be NULL
 * with max 0 to count only. Returns the number of changes (all of them, even
 * past `max`); main has none. Lock files, temporaries and the pack outputs
 * are never changes.
 */
int
ToriRSMapEdLanes_Changes(
    const struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneChange* out,
    int max,
    struct ToriRSMapEdLaneCounts* counts);

/**
 * Write `from`'s changes into `to`'s content (flags: enum above). 1 on
 * success with `counts` filled; 0 with `why` -- a target that is the source,
 * a main tree another editor holds (unless FORCE), a write that failed. A
 * failed write stops the publish where it was; what was written stays.
 */
int
ToriRSMapEdLanes_Publish(
    const struct ToriRSMapEdLaneDef* from,
    const struct ToriRSMapEdLaneDef* to,
    uint32_t flags,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size);

/** Put every change back to the base: re-link modified and deleted files,
 *  remove added ones. 1 on success. */
int
ToriRSMapEdLanes_Discard(
    const struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size);

/* ---- the next boot's lane (workbench 6.5) -------------------------------------
 *
 * <repo>/build/lanes/lane.ini, the lanes directory's own descriptor (not a
 * lane: its name is no lane name), holds `lane = <name>`: the lane every
 * editor boot on this repo serves from now on, whatever its manifest or
 * --lane asked, until it is switched again. The Lanes panel's Switch and
 * Scratch lane write it (LANE_SWITCH); the daemon's main and the embedded
 * server read it at boot (ToriRSMapEdLanes_BootLane) and say which they
 * served and why. */

/** Name the lane the next boot serves ("main" or an existing lane). 0 with
 *  `why` on a bad name, a lane that does not exist, or a write that failed. */
int
ToriRSMapEdLanes_SetBoot(
    const char* repo_root,
    const char* base_tree,
    const char* name,
    char* why,
    size_t why_size);

/** The lane build/lanes/lane.ini names, "" (returns 0) when none. */
int
ToriRSMapEdLanes_BootName(
    const char* repo_root,
    char* out,
    size_t out_size);

/**
 * The lane a boot serves: the one build/lanes/lane.ini names when it exists,
 * else `configured` (NULL, "" or "main" are the tree itself). `out` gets the
 * name ("main" for the tree); `note` (one line, "" when the file named
 * nothing) says what the file did: "lane 'x' (build/lanes/lane.ini, switched
 * in the editor; the boot asked for 'working')", or that the lane it names is
 * gone. Returns 1 when the file chose the lane.
 */
int
ToriRSMapEdLanes_BootLane(
    const char* repo_root,
    const char* base_tree,
    const char* configured,
    char* out,
    size_t out_size,
    char* note,
    size_t note_size);

/** Who holds `tree`'s editor-session lock, when a live process does:
 *  "pid N (<command>)". 0 when nobody. */
int
ToriRSMapEdLanes_TreeHeld(
    const char* tree,
    char* out,
    size_t out_size);

#endif
