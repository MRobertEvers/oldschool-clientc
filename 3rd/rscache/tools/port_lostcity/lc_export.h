#ifndef RSCACHE_TOOLS_LC_EXPORT_H
#define RSCACHE_TOOLS_LC_EXPORT_H

#include "asset_access.h"
#include "lc_out.h"
#include "lc_pack.h"

/** src id -> LostCity id, so a shared asset is exported once. */
struct LC_IdMap
{
    int* src;
    int* dst;
    int count;
    int cap;
};

int
lc_id_map_get(
    const struct LC_IdMap* map,
    int src,
    int* out_dst);

int
lc_id_map_put(
    struct LC_IdMap* map,
    int src,
    int dst);

void
lc_id_map_free(struct LC_IdMap* map);

/**
 * One dat1 animset under construction.
 *
 * A dat1 animset archive is a single AnimBase plus every frame rigged to it, so
 * frames are grouped by their source framemap: two sequences sharing a framemap
 * must land in the same archive, and two framemaps can never share one. Frames
 * accumulate here across all exported sequences and the archive is encoded once
 * at the end.
 */
struct LC_AnimSet
{
    int src_framemap_id;
    int lc_animset_id;
    char name[64];

    /** Source composite frame ids `(archive << 16) | file` already added. */
    int* src_frames;
    /** LostCity global frame id for each entry, from `anim.pack`. */
    int* lc_frames;
    int frame_count;
    int frame_cap;
};

struct LC_Ctx
{
    struct Tool_Dat2Cache* src;
    struct LC_Packs* packs;
    struct LC_Out* out;

    struct LC_IdMap npc_map;
    struct LC_IdMap seq_map;
    struct LC_IdMap spotanim_map;
    struct LC_IdMap loc_map;
    /** Keyed by `(src_model_id << 8) | shape+1`, since a loc model is exported
     *  once per shape suffix the LostCity packer looks the shape up by. */
    struct LC_IdMap model_map;

    struct LC_AnimSet* animsets;
    int animset_count;
    int animset_cap;

    /** Highest npc `size` LostCity accepts; larger footprints are clamped. */
    int max_npc_size;
};

void
lc_ctx_free(struct LC_Ctx* ctx);

/**
 * Export a model as `.ob2`.
 *
 * `shape` is the loc shape the model serves, or -1 for npc / spotanim models
 * which are referenced by a bare name. Returns the LostCity model id, or -1.
 */
int
lc_export_model(
    struct LC_Ctx* ctx,
    int src_model_id,
    int shape);

/** Export a sequence and everything it animates. Returns the LostCity seq id. */
int
lc_export_seq(
    struct LC_Ctx* ctx,
    int src_seq_id,
    const char* name);

int
lc_export_npc(
    struct LC_Ctx* ctx,
    int src_npc_id,
    const char* name);

int
lc_export_spotanim(
    struct LC_Ctx* ctx,
    int src_spotanim_id,
    const char* name);

int
lc_export_loc(
    struct LC_Ctx* ctx,
    int src_loc_id,
    const char* name);

/**
 * Export a map square as `<content>/maps/m<X>_<Z>.jm2`, porting every loc it
 * references. Emits the `map.pack` lines for both the land and loc archives.
 */
int
lc_export_map(
    struct LC_Ctx* ctx,
    int map_x,
    int map_z,
    int max_level);

/** Encode every accumulated animset. Call once after the last sequence. */
int
lc_export_flush_animsets(struct LC_Ctx* ctx);

#endif
