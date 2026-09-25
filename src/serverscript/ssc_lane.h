#ifndef SRC_SERVERSCRIPT_SSC_LANE_H
#define SRC_SERVERSCRIPT_SSC_LANE_H

/*
 * Content lanes: a body of ported content that a build either compiles or does
 * not, described by the lane itself.
 *
 * A lane is a directory under `<content>/ported/` holding a `lane.ini`. That
 * file is the only statement anywhere of what the lane consists of — which
 * server scripts belong to it, which symbol indexes it brings, which interface
 * roots hold its component names, and which `^flag` shared content tests. The
 * build reads it; nothing outside the lane names any of those paths.
 *
 * That is the whole point of the format. The same four lanes used to be spelled
 * out as SUMMONING_CLIENT_LANE / RS2012_QBD_TD_CLIENT_LANE / CURSES_CLIENT_LANE /
 * HERBLORE_ITEMS_LANE in src/makefile, repeated across four near-identical
 * compile recipes, with a fifth copy of the flag values in a python stager. A
 * fifth lane meant editing all of them, and *not* compiling one of them was not
 * expressible at all: every recipe passed every lane. So a lane could be
 * switched off only in the sense that its feature constant read 0 while its
 * scripts were still compiled into the pack.
 *
 * `default=on` marks a lane that is additive rather than gated — new records
 * that base content already refers to by name (herb tar, the QBD's npcs). It is
 * compiled unless something explicitly leaves it out. `default=off` marks a
 * gated lane: it is compiled only when the build asks for it by name, and its
 * flag reads 0 otherwise.
 *
 * Where the base tree must *call* a gated lane (combat asking about curses,
 * login asking about familiars), the base tree carries a seam — a default
 * definition in a weak source root that the lane replaces when it is present.
 * See struct SSC_SourceRoot in ssc.h.
 */

enum
{
    SSC_LANE_MAX = 32,
    /** Per key, per lane. Generous against the largest real lane (summoning:
     *  two component roots), small enough that the set is a plain array. */
    SSC_LANE_PATHS_MAX = 8,
    SSC_LANE_NAME_MAX = 128,
    SSC_LANE_PATH_MAX = 1024
};

struct SSC_Lane
{
    char name[SSC_LANE_NAME_MAX];
    /** `<content_root>/ported/<name>`, where the descriptor was read. */
    char dir[SSC_LANE_PATH_MAX];

    /** Compiled unless excluded, rather than only when asked for. */
    int enabled_by_default;
    /** Selected for this build. */
    int enabled;

    /** The `^name` shared content tests, or "" when the lane declares none.
     *  Defined as 1 when the lane is in the build and 0 when it is not — never
     *  left undefined, because an undefined constant is a compile error and the
     *  files that test it do so unconditionally. */
    char constant[SSC_LANE_NAME_MAX];

    /** Server script directories, compiled only when the lane is enabled, and
     *  subtracted from the base walk when it is not. */
    char scripts[SSC_LANE_PATHS_MAX][SSC_LANE_PATH_MAX];
    int script_count;

    /** `--pack` directories. */
    char packs[SSC_LANE_PATHS_MAX][SSC_LANE_PATH_MAX];
    int pack_count;

    /** Individual `id=name` index files, for a lane whose directory holds more
     *  than the build should read. */
    char pack_files[SSC_LANE_PATHS_MAX][SSC_LANE_PATH_MAX];
    int pack_file_count;

    /** Roots holding `interfaces/<name>.compack` member indexes. */
    char component_roots[SSC_LANE_PATHS_MAX][SSC_LANE_PATH_MAX];
    int component_root_count;
};

struct SSC_LaneSet
{
    char content_root[SSC_LANE_PATH_MAX];
    struct SSC_Lane lanes[SSC_LANE_MAX];
    int count;
};

/**
 * Read the `lane.ini` in every directory under `<content_root>/ported`, in name
 * order.
 *
 * Name order rather than readdir order because a lane contributes source
 * directories, and script ids come out of the sorted file set — a build whose
 * lane order depended on the filesystem would be reproducible only by accident.
 *
 * Returns the number of lanes found, or -1 when a descriptor exists but cannot
 * be read or is missing its name. A tree with no `ported/` directory at all is
 * 0 lanes, not an error: not every content tree carries ported content.
 */
int
SSC_LanesDiscover(
    struct SSC_LaneSet* set,
    const char* content_root);

/** The named lane, or NULL. */
struct SSC_Lane*
SSC_LaneFind(
    struct SSC_LaneSet* set,
    const char* name);

#endif
