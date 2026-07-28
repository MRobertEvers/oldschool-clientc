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
 *
 * The archive trailer stores its four section lengths as u16, so a framemap with
 * many frames needs several archives. `part` numbers them; the AnimBase is
 * repeated in each, which is what makes the split invisible to the client — it
 * registers frames by the id embedded in the head, not by archive.
 */
#define LC_ANIMSET_MAX_FRAMES 48

struct LC_AnimSet
{
    int src_framemap_id;
    int part;
    int lc_animset_id;
    int lc_base_id;
    char name[64];

    /** Source composite frame ids `(archive << 16) | file` already added. */
    int* src_frames;
    /** LostCity global frame id for each entry, from `anim.pack`. */
    int* lc_frames;
    /** Per-frame delay, taken from the first sequence that used the frame. */
    int* delays;
    int frame_count;
    int frame_cap;
};

/**
 * One source texture record, cached so the closure can ask about any id cheaply.
 *
 * The table is small (a few hundred seven-byte records) and every model export
 * consults it, so it is decoded once at context init rather than per lookup.
 */
struct LC_TextureDef
{
    /** Sprite archive holding the pixels, or -1 when the record names none. */
    int sprite_id;
    /** Mean colour as HSL16, or -1 when the record has none. */
    int average_hsl;
    int opaque;
};

struct LC_Ctx
{
    struct Tool_Dat2Cache* src;
    struct LC_Packs* packs;
    struct LC_Out* out;

    struct LC_IdMap npc_map;
    struct LC_IdMap obj_map;
    struct LC_IdMap seq_map;
    struct LC_IdMap spotanim_map;
    struct LC_IdMap loc_map;
    /** Keyed by `(src_model_id << 8) | shape+1`, since a loc model is exported
     *  once per shape suffix the LostCity packer looks the shape up by. */
    struct LC_IdMap model_map;
    /** Source composite frame id -> LostCity global frame id (`anim.pack`). */
    struct LC_IdMap frame_map;
    /** Source underlay / overlay floor id -> LostCity `flo.pack` id. */
    struct LC_IdMap underlay_map;
    struct LC_IdMap overlay_map;
    /** Source texture id -> LostCity `texture.pack` id. */
    struct LC_IdMap texture_map;

    struct LC_AnimSet* animsets;
    int animset_count;
    int animset_cap;

    /** Highest npc `size` LostCity accepts; larger footprints are clamped. */
    int max_npc_size;

    /** The source texture table, indexed by texture id. */
    struct LC_TextureDef* textures;
    int texture_count;
    /** Colour a textured face falls back to when its texture has no average. */
    int texture_fallback_hsl;
    /**
     * How many texture ids the destination may hold. Zero ports none.
     *
     * The check is on the *id*, not on how many this run adds: `texture.pack` is
     * appended to at `pack->max`, so a stock pack already listing 0..49 puts the
     * first new material at 50 and nothing fits. Whatever does not fit falls
     * back to the texture's own average HSL, exactly as an unported face does.
     */
    int max_textures;
};

/**
 * Texture ids a stock rev-254 client will look at.
 *
 * Pix3D sizes its per-texture arrays to 50 and stops unpacking there, so an id
 * at or past this is written but never read — the face would render untextured
 * and unlit rather than falling back to a colour. Nothing in the *format* says
 * 50 (the flo config's one-byte `texture` opcode is the real ceiling), so a
 * client with a wider table can be targeted with `--max-textures`.
 */
#define LC_DEFAULT_MAX_TEXTURES 50

int
lc_ctx_init(
    struct LC_Ctx* ctx,
    struct Tool_Dat2Cache* src,
    struct LC_Packs* packs,
    struct LC_Out* out);

void
lc_ctx_free(struct LC_Ctx* ctx);

/**
 * Export a model as `.ob2`.
 *
 * `shape` is the loc shape the model serves, or -1 for npc / spotanim models
 * which are referenced by a bare name. A loc model's file name carries the
 * shape's suffix, which is how the LostCity packer recovers the shape, so
 * `out_base` receives the *unsuffixed* name the config must reference.
 * Returns the LostCity model id, or -1.
 */
int
lc_export_model(
    struct LC_Ctx* ctx,
    int src_model_id,
    int shape,
    char* out_base,
    int out_base_size);

/** Export a sequence and every frame it plays. Returns the LostCity seq id. */
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

/**
 * Export an item as a LostCity `.obj` config plus its models.
 *
 * An item carries up to three: the one the inventory icon is rendered from and
 * the male and female wield models. LostCity names the wield models by
 * convention (`<base>_manwear`), and the packer finds them by that name rather
 * than by anything in the config, so the names have to be built here.
 */
int
lc_export_obj(
    struct LC_Ctx* ctx,
    int src_obj_id,
    const char* name);

int
lc_export_spotanim(
    struct LC_Ctx* ctx,
    int src_spotanim_id,
    const char* name);

int
lc_export_loc(
    struct LC_Ctx* ctx,
    int src_loc_id);

/**
 * Export a map square as `<content>/maps/m<X>_<Z>.jm2`, porting every loc it
 * references. Emits the `map.pack` lines for both the land and loc archives.
 */
int
lc_export_map(
    struct LC_Ctx* ctx,
    int map_x,
    int map_z);

/** Encode every accumulated animset. Call once after the last sequence. */
int
lc_export_flush_animsets(struct LC_Ctx* ctx);

/**
 * Export a source material as `<content>/textures/<name>.png` plus its
 * `texture.pack` line, and return the LostCity texture id (-1 on failure).
 *
 * `name` is generated when NULL. Idempotent by source id like every other
 * exporter, so a material shared by a floor and twenty model faces is written
 * once — and, as with sequences, whichever caller asks first decides the name.
 */
int
lc_export_texture(
    struct LC_Ctx* ctx,
    int src_texture_id,
    const char* name);

/** Load the source texture table into the context. Called by lc_ctx_init. */
void
lc_textures_load(struct LC_Ctx* ctx);

/** Average colour of a source texture as HSL16, for the flatten fallback. */
int
lc_texture_average_hsl(
    const struct LC_Ctx* ctx,
    int src_texture_id);

/* ---- shared internals (lc_export.c <-> lc_map.c) ------------------------- */

/** Load one record out of a dat2 config group. Caller frees `out->data`. */
int
lc_config_record(
    struct LC_Ctx* ctx,
    enum RSCache_Type type,
    int config_kind,
    int id,
    struct Tool_Bytes* out,
    int* out_group_revision);

/** LostCity's model-name suffix for a loc shape, e.g. 10 -> "_8". */
const char*
lc_loc_shape_suffix(int shape);

/**
 * HSL16 back to RGB15.
 *
 * Loc and spotanim recolours are authored as RGB15 in LostCity source and
 * converted to HSL16 at pack time, while the cache stores HSL16 already — so a
 * ported recolour has to travel backwards through the client's own palette
 * build to survive the round trip. (Npc recolours take the value raw, which is
 * why only these two need it.)
 */
int
lc_hsl16_to_rgb15(int hsl);

#endif
