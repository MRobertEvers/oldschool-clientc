#ifndef RSCACHE_TOOLS_LC_MANIFEST_H
#define RSCACHE_TOOLS_LC_MANIFEST_H

/*
 * Port manifest — one INI file holding a whole `port_lostcity` invocation.
 *
 * A port is not a one-off command. The exporter rewrites each config file from
 * what a single run accumulated, so a re-run has to repeat the *entire* asset
 * list or it drops what the previous run put there — and that list reaches
 * dozens of `--npc`/`--seq`/`--texture` flags for anything the size of an area.
 * Keeping it in a file makes the port reviewable, diffable and repeatable;
 * keeping it in shell history does not.
 *
 * Schema (house style [type:name] sections, lowercase key=value, ; / # comments,
 * matching src/bootmanifest):
 *
 *   [port:lostcity]  rev=<name>  cache=<dir>  content=<dir>
 *                    area=<subdir>  prefix=<name>  max_textures=<n>
 *
 *   [export:npc]       <src_id>=<name>   name optional; generated when empty
 *   [export:seq]       <src_id>=<name>
 *   [export:spotanim]  <src_id>=<name>
 *   [export:texture]   <src_id>=<name>
 *   [export:loc]       <src_id>=        locs are named from the source config
 *   [export:map]       <X>_<Z>=         a square, not an id
 *
 * `rev`, `cache` and `content` are required; everything else has a default.
 * A missing or unreadable key fails the load with a stated reason, because all
 * of it is user input rather than an internal invariant.
 *
 * Relative `cache` / `content` values resolve against the directory holding the
 * manifest, as boot manifests do, so a manifest checked in beside the tool
 * works from any cwd.
 *
 * Order within a section is file order. Order *between* sections is fixed by
 * the exporter and does not depend on the manifest: textures, then sequences,
 * then npcs, spotanims, locs, maps. That is what retires the CLI's sharpest
 * edge — a `--seq 2863=..._defend` written after the npc that also uses 2863 as
 * its walk used to lose its name to the generated one.
 *
 * `--apply` is deliberately *not* a manifest key. Whether a run writes is a
 * property of the invocation, not of the port, and a manifest that wrote to the
 * server content tree merely by being read would be a poor thing to hand
 * someone.
 */

/** One requested export: an id, and the LostCity name to give it. */
struct LC_Request
{
    int id;
    char name[96];
    int has_name;
    /** The request verbatim, for `map X_Z` where the id is not a number. */
    char raw[96];
};

struct LC_RequestList
{
    struct LC_Request* items;
    int count;
    int cap;
};

/**
 * Append one request written as `ID` or `ID=name`.
 *
 * Takes the same text the CLI flags do, so `--npc 7706=inferno_zuk` and a
 * manifest's `7706=inferno_zuk` land on one parser rather than two.
 */
int
lc_request_add(
    struct LC_RequestList* list,
    const char* arg);

void
lc_request_list_free(struct LC_RequestList* list);

struct LC_Manifest
{
    char rev[64];
    /** Resolved against the manifest's directory. */
    char cache_dir[1024];
    char content_dir[1024];
    char area[256];
    char prefix[64];
    /** -1 when the manifest did not say; the caller applies the default. */
    int max_textures;
    /** Raw source underlay id backing overlay-only tiles; 0 = off. */
    int overlay_backing;

    struct LC_RequestList npcs;
    struct LC_RequestList objs;
    /** Vertex-label remap pairs, `from` -> `to`. */
    int label_map_from[256];
    int label_map_to[256];
    int label_map_count;

    /** Source rig joint -> destination rig joint, from `[export:rig_map]`. */
    int rig_map_from[256];
    int rig_map_to[256];
    int rig_map_count;
    /** Joint number used for 'no counterpart'; never treated as drivable. */
    int rig_inert;
    /** Source framemap ids the rig map applies to (`rig_framemaps`). */
    int rig_framemaps[64];
    int rig_framemap_count;

    /**
     * Verbatim config lines to append to a named block, from `[extra:<name>]`.
     *
     * A ported record only carries what the source cache states. Everything a
     * LostCity config needs beyond that — combat bonuses, `category`, attack
     * animations, spec params — is authored by hand, and the exporter owns the
     * file it would otherwise be written into. Keeping those lines in the
     * manifest is what makes a re-export idempotent instead of destructive.
     */
    struct LC_ExtraLine
    {
        char config[64];
        char line[192];
    }* extras;
    int extra_count;
    int extra_cap;
    /**
     * Extra map placements, from `[export:maploc]` / `--maploc`, written into
     * the square's jm2 after the source placements.
     *
     * Some arena dressing is not in any square's static data — Kronos builds
     * the Zuk alcove's flanking crags with dynamic spawns on level 1 — and a
     * rev-254 zone update cannot express a level the player is not on, so the
     * only faithful home for such a loc is the map file itself. Recording the
     * placement here keeps the export reproducible.
     */
    struct LC_MapLocAdd
    {
        int map_x, map_z;
        int level, x, z;
        int src_loc;
        int shape, angle;
    }* maplocs;
    int maploc_count;
    int maploc_cap;

    /**
     * Underlay fills for floor-less tile regions, from `[export:mapfill]` /
     * `--mapfill`. A tile with neither overlay nor underlay is void — the
     * reference client draws nothing there, and anything standing on it with
     * translucent faces composites against black. Inside an arena that reads
     * as holes; outside the walls it is the authored darkness, which is why
     * this is a bounded region and not a squares-wide default.
     */
    struct LC_MapFill
    {
        int map_x, map_z;
        int level, x1, z1, x2, z2;
        int underlay;
    }* mapfills;
    int mapfill_count;
    int mapfill_cap;
    struct LC_RequestList seqs;
    struct LC_RequestList spotanims;
    struct LC_RequestList locs;
    struct LC_RequestList maps;
    struct LC_RequestList textures;
};

/** Zero `manifest` and set the "unset" sentinels. */
void
lc_manifest_init(struct LC_Manifest* manifest);

/**
 * Load `path` into `manifest`, keeping anything already in it that the file
 * does not mention. Returns 1 on success; on failure a stderr line names the
 * section, key and reason.
 */
int
lc_manifest_load(
    struct LC_Manifest* manifest,
    const char* path);

void
lc_manifest_free(struct LC_Manifest* manifest);

/** Append one `35_83,level,x,z,locid,shape,angle` placement. */
int
lc_manifest_add_maploc(
    struct LC_Manifest* manifest,
    const char* text);

/** Append one `35_83,level,x1,z1,x2,z2,underlay` fill region. */
int
lc_manifest_add_mapfill(
    struct LC_Manifest* manifest,
    const char* text);

#endif
