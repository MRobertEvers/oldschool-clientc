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
 * A named lane's content is a copy-on-write view of its base (the main
 * tree), in one of two layouts (lane.ini):
 *
 *   sparse  (`layout = sparse`, `sparse_base = <tree>`; every lane Create
 *           makes since 2026-11-10) a directory is ONE LINK to the base's
 *           until something is about to be written inside it; then
 *           ToriRSMapEdLanes_MakeWritable EXPLODES it one level into a real
 *           directory holding one link per base entry. Real from creation:
 *           the root, server/ and server/scripts/ (the pack outputs are left
 *           out and the tools' outputs land in the lane), maps/ (exploded),
 *           pack/ (copied). A sparse lane.ini has no `base = ` line, so an
 *           editor that predates the layout refuses the lane instead of
 *           writing through its links.
 *   legacy  (`base = <tree>`) a full MIRROR: real directories holding one
 *           link per file, directory links for the pure-asset trees
 *           (models, sprites, ...), a copy of pack/.
 *
 * Either way there are no pack outputs or locks (maped/test/live_e2e/
 * mirror.py's rules, ported). A save writes a temporary beside the target
 * and renames it over the link, so an edited file becomes a real file in the
 * lane and the base is untouched -- PROVIDED the directory it lands in is
 * real: a write that reaches a directory link lands in the BASE, which is why
 * every writer into a lane calls ToriRSMapEdLanes_MakeWritable first. The
 * lane's CHANGES are exactly its real files that differ from the base
 * (modified / added) and the base files it no longer has (deleted); the
 * walks go only into the lane's real directories, so on a sparse lane they
 * cost what the lane changed.
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
 *          -> FACT_LANE_CHANGES   u32 state (0 the answer, 1 still scanning:
 *                                 the counts and rows below are empty and the
 *                                 answer follows as a BROADCAST when the scan
 *                                 ends), u32 modified, u32 added, u32 deleted,
 *                                 u32 rows, rows * (u32 kind, string path,
 *                                 u32 size)   (rows capped at 1024)
 *          The scan stats every entry of the lane and of its base (~100 k
 *          each, 1..7 s on a cold disk): it runs on a worker thread where the
 *          platform has one, never inside the server pump, so an editor that
 *          asks at boot keeps drawing.
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
 *                       build/lanes/lane.ini names for the next boot ("" none);
 *                       then `string asked`: the lane the boot asked for before
 *                       lane.ini had its say (the manifest's `lane=` / --lane,
 *                       "main" when neither; "" from a server that predates it).
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

    TORIRSMAPED_LANE_CHANGES_DONE = 0,
    TORIRSMAPED_LANE_CHANGES_SCANNING = 1,
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
    /** 1: a sparse lane (lane.ini `layout = sparse`, `sparse_base = `);
     *  0: a legacy full mirror (`base = `), or main. */
    int sparse;
    /** When the mirror last looked at its base (lane.ini `created`, or
     *  `followed` once ToriRSMapEdLanes_Follow has run; seconds): a base file
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

/** The lane whose content directory `content_dir` is ("<lane dir>/content"
 *  with a lane.ini beside it), found without its repo or its name: so an
 *  editor server opened on a lane's content as a plain tree still knows its
 *  directories may be links to the base (ToriRSMapEd_Open). 1 found. */
int
ToriRSMapEdLanes_OfContent(
    const char* content_dir,
    struct ToriRSMapEdLaneDef* out);

/** Create <repo>/build/lanes/<name>: lane.ini, a SPARSE content view of
 *  `base_tree`, the ancestor/ seed of its copied trees, an empty live dir.
 *  0 with `why` on a bad name, a lane that exists, or a link that failed. */
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

/**
 * Make `rel` writable in `lane`: `rel` (relative to the content root, '/'
 * separated) names a file or directory about to be created, replaced or
 * removed, and every ANCESTOR directory of it is made real, top down -- one
 * that is a link to the base is exploded (a real directory with one link per
 * base entry), one that is absent is created. `rel` itself is not touched.
 * Call it before EVERY write into a lane's content (a save's temporary and
 * rename, a mkdir, a delete): a write that reaches a directory that is still
 * a link lands in the base tree.
 *
 * An absent ancestor the base HAS is filled from the base (its entries
 * linked), never made empty: an empty one would read as the base's files in
 * it deleted by the lane.
 *
 * Crash-safe: an explode builds "<parent>/.<dir>.explode.tmp" complete (a
 * dot-name, which no build tool reads, ending in .tmp, which no lane walk
 * reads), records the time in "<lane dir>/exploded.ini", then removes the
 * link, then renames; a leftover temporary with "<dir>" absent is completed
 * by the next call (and by Follow), one beside the directory is removed (by
 * Follow) or rebuilt (by the next call). A rename that fails after the link
 * went puts the link back. Explodes of one lane take turns across processes
 * ("<lane dir>/explode.lock"; a dead holder's lock is broken, a live one is
 * waited for up to TORIRSMAPED_LANES_LOCK_WAIT_MS, 60 s by default).
 *
 * exploded.ini is the lane's last look at each directory it made real after
 * its creation: Changes and Follow take the later of it and `created` when
 * they tell a base entry the lane deleted from one new in the base, so a file
 * the base gained after `created` that the lane showed through a link, and
 * then deleted, is the lane's deletion (and Follow keeps it deleted).
 *
 * Nothing is ever deleted through a link. Main, and a path whose ancestors
 * are real already: 1 at once. Works on a legacy lane too (its linked asset
 * trees explode the same way, and its walks then go into them). 1 on
 * success, 0 with `why` (a path that leaves the lane, an ancestor that is a
 * file, an IO error, the lock held too long).
 */
int
ToriRSMapEdLanes_MakeWritable(
    const struct ToriRSMapEdLaneDef* lane,
    const char* rel,
    char* why,
    size_t why_size);

/** A LEGACY full mirror (mirror.py's rules): what Create made before sparse
 *  lanes, kept for tests and tools. `dst` must not exist. Returns the
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
 * A target lane's directories are made writable before each write
 * (ToriRSMapEdLanes_MakeWritable), so nothing lands in the target's base.
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
 *  remove added ones. 1 on success. A sparse lane's directories a discard
 *  leaves holding only links stay exploded (real directories of links read
 *  exactly as the links they replaced). */
int
ToriRSMapEdLanes_Discard(
    const struct ToriRSMapEdLaneDef* lane,
    struct ToriRSMapEdLaneCounts* counts,
    char* why,
    size_t why_size);

/** Bring the mirror up to the base's present, keeping the lane's own
 *  changes: link every file and directory the base gained since the mirror's
 *  last look, drop links the base's deletions left dangling, and take the
 *  base's version of each copied (pack/) file the lane never changed -- the
 *  copy cannot follow on its own the way a link does, and a stale copy is a
 *  silent revert of the base (packed into RUN's server, published over the
 *  main tree). A copied file BOTH sides changed is refused, named in `why`.
 *  The common ancestor of a copy is <lane>/ancestor/<rel>, taken by Create
 *  and by every follow. RUN and PUBLISH follow first. 1 on success (`created`
 *  and lane.ini's `followed` move to this look); 0 on a conflict or an IO
 *  error. counts: written = linked or refreshed, removed = dropped. A no-op
 *  on the main lane. A sparse lane's directory links follow on their own --
 *  the walk never enters one, nothing is done or counted for what changed
 *  inside it -- and a directory new in the base is ONE link (counted once);
 *  so a sparse lane's counts are lower than a full mirror's for the same
 *  base, while the tree it shows and its changes are the same. */
int
ToriRSMapEdLanes_Follow(
    struct ToriRSMapEdLaneDef* lane,
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
