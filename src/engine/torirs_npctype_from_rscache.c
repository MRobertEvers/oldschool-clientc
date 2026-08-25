#include "engine/torirs_npctype_from_rscache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

static void
torirs_copy_menu_actions(
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN],
    char* const* src_actions)
{
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions )
        return;

    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIRS_MENU_ACTION_LEN - 1);
            actions[i][TORIRS_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static void
torirs_npctype_copy_models(
    struct ToriRS_Npctype* dst,
    const int* models,
    int models_count)
{
    dst->models_count = models_count;
    if( models_count <= 0 )
        return;

    dst->models = malloc((size_t)models_count * sizeof(int));
    assert(dst->models);
    memcpy(dst->models, models, (size_t)models_count * sizeof(int));
}

static void
torirs_npctype_copy_heads(
    struct ToriRS_Npctype* dst,
    const int* heads,
    int heads_count)
{
    dst->heads_count = heads_count;
    if( heads_count <= 0 || !heads )
    {
        dst->heads_count = 0;
        return;
    }

    dst->heads = malloc((size_t)heads_count * sizeof(int));
    assert(dst->heads);
    memcpy(dst->heads, heads, (size_t)heads_count * sizeof(int));
}

static void
torirs_npctype_copy_pairs_int(
    int** dst_from,
    int** dst_to,
    int* dst_count,
    const int* src_from,
    const int* src_to,
    int count)
{
    *dst_count = 0;
    if( count <= 0 )
        return;
    assert(src_from);
    assert(src_to);

    *dst_from = malloc((size_t)count * sizeof(int));
    *dst_to = malloc((size_t)count * sizeof(int));
    assert(*dst_from);
    assert(*dst_to);
    memcpy(*dst_from, src_from, (size_t)count * sizeof(int));
    memcpy(*dst_to, src_to, (size_t)count * sizeof(int));
    *dst_count = count;
}


/*
 * The record's params (opcode 249), carried whole for CS2's `nc_param`.
 *
 * The rscache decoder has always parsed these; this conversion simply did not
 * bring them across, so `nc_param` had nothing to read and no implementation. The
 * cache's own "Highlight entities on mouse-over" (clientscript 5350) is what
 * needs them: it reads `param_2312` off the hovered record to decide which
 * highlight group the thing belongs in.
 */
static void
torirs_npctype_copy_params(struct ToriRS_Npctype* dst, struct RSCache_Params const* src)
{
    assert(dst);
    assert(src);
    if( src->count <= 0 )
        return;

    dst->param_count = src->count;
    dst->params = calloc((size_t)src->count, sizeof(*dst->params));
    assert(dst->params);
    for( int i = 0; i < src->count; i++ )
    {
        dst->params[i].key = src->keys[i];
        if( src->kinds && src->kinds[i] == RSCACHE_PARAM_STRING )
        {
            char const* str = (char const*)src->values[i];
            dst->params[i].string_value = strdup(str ? str : "");
            assert(dst->params[i].string_value);
        }
        else if( src->values[i] )
        {
            if( src->kinds && src->kinds[i] == RSCACHE_PARAM_LONG )
                dst->params[i].int_value = (int)*(int64_t*)src->values[i];
            else
                dst->params[i].int_value = *(int*)src->values[i];
        }
    }
}

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat1(
    int npc_id,
    const struct RSCache_Dat1ConfigNpc* src)
{
    struct ToriRS_Npctype* npctype;

    assert(src);

    npctype = calloc(1, sizeof(*npctype));
    assert(npctype);

    npctype->id = npc_id;
    if( src->name )
    {
        strncpy(npctype->name, src->name, TORIRS_NAME_MAX - 1);
        npctype->name[TORIRS_NAME_MAX - 1] = '\0';
    }
    if( src->desc )
    {
        strncpy(npctype->desc, src->desc, TORIRS_DESC_MAX - 1);
        npctype->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(npctype->actions, src->op);
    npctype->combat_level = src->vislevel;
    npctype->size = src->size;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);
    torirs_npctype_copy_heads(npctype, src->heads, src->heads_count);

    /* dat1-era npcs carry no retexture pairs. */
    torirs_npctype_copy_pairs_int(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recol_s,
        src->recol_d,
        src->recol_count);

    npctype->readyanim = src->readyanim;
    npctype->walkanim = src->walkanim;
    npctype->walkanim_b = src->walkanim_b;
    npctype->walkanim_r = src->walkanim_r;
    npctype->walkanim_l = src->walkanim_l;
    /* dat1 genuinely has no turn/run/crawl opcodes -- the claim the dat2 path
     * inherited by accident is true here. -1 across the set so the mover's
     * `!= -1` tests fall back to walkanim, as they did before any of this. */
    npctype->turnanim_l = -1;
    npctype->turnanim_r = -1;
    npctype->runanim = -1;
    npctype->runanim_b = -1;
    npctype->runanim_r = -1;
    npctype->runanim_l = -1;
    npctype->crawlanim = -1;
    npctype->crawlanim_b = -1;
    npctype->crawlanim_r = -1;
    npctype->crawlanim_l = -1;
    npctype->turn_speed = src->turnspeed;
    npctype->width_scale = src->resizeh > 0 ? src->resizeh : 128;
    npctype->height_scale = src->resizev > 0 ? src->resizev : 128;
    /* dat1 has no overhead-height opcode; -1 means "use the model's height". */
    npctype->height = -1;
    npctype->alwaysontop = src->alwaysontop;
    npctype->minimap_visible = src->minimap;
    /* dat1 has no opcode 107. True rather than the calloc zero: the flag's
     * default IS true, and a false here would take the minimap dot off every
     * npc in every dat1 cache. */
    npctype->interactable = true;
    npctype->ambient = src->ambient;
    npctype->contrast = src->contrast;
    /* The dat1 decoder does not surface opcode 102. Stated rather than left at
     * the calloc zeroes: sprite group 0 and frame 0 are both real. */
    npctype->head_icon_group = -1;
    npctype->head_icon_index = -1;
    /*
     * dat1 has no movement-sound opcode. Say so explicitly rather than leaving
     * the calloc zeroes: 0 is a real sound-effect id, so a zeroed field would
     * give every npc on this path the same sound.
     */
    npctype->sound_idle = -1;
    npctype->sound_crawl = -1;
    npctype->sound_walk = -1;
    npctype->sound_run = -1;
    npctype->sound_radius = 0;
    npctype->ambient_sound_volume = 255;

    /* dat1's NpcType has no multiNpc opcode. */
    npctype->transform_varbit = -1;
    npctype->transform_varp = -1;

    return npctype;
}

struct ToriRS_Npctype*
ToriRS_NpctypeFromRSCacheDat2(
    int npc_id,
    const struct RSCache_Dat2ConfigNpc* src)
{
    struct ToriRS_Npctype* npctype;

    assert(src);

    npctype = calloc(1, sizeof(*npctype));
    assert(npctype);

    npctype->id = npc_id;
    /* Opcode 18. The client trigger lookup's category form is keyed on it —
     * see game/rs_client_trigger.h. */
    npctype->category = src->category;
    if( src->name )
    {
        strncpy(npctype->name, src->name, TORIRS_NAME_MAX - 1);
        npctype->name[TORIRS_NAME_MAX - 1] = '\0';
    }
    /*
     * The examine string, npc config opcode 3.
     *
     * Jagex's own dat2 records state none — the opcode was retired in 2006 and
     * OldSchool sends npc examine from the server — so for a pristine cache this
     * leaves `desc` at the calloc'd "" and the Examine handler falls back to
     * "It's a <name>.". A record the content pack authored does state one, and
     * that is where an npc's examine text comes from here: the same archive, the
     * same opcode, and the same field a loc has always used.
     */
    if( src->desc )
    {
        strncpy(npctype->desc, src->desc, TORIRS_DESC_MAX - 1);
        npctype->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(npctype->actions, src->actions);
    npctype->combat_level = src->combat_level;
    npctype->size = src->size;
    npctype->sound_idle = src->sound_idle;
    npctype->sound_crawl = src->sound_crawl;
    npctype->sound_walk = src->sound_walk;
    npctype->sound_run = src->sound_run;
    npctype->sound_radius = src->sound_radius;
    npctype->ambient_sound_volume = src->ambient_sound_volume;
    torirs_npctype_copy_models(npctype, src->models, src->models_count);
    torirs_npctype_copy_heads(npctype, src->chathead_models, src->chathead_models_count);

    /*
     * TORIRS_NPC_SOUND_DEBUG=1 prints every npc that names a movement sound.
     *
     * The byte layout of opcode 134 is settled by exact-consumption scanning,
     * but which of its four ids is idle versus walk versus run is not something
     * a byte count can tell you. Printing them next to the npc's name is the
     * check that can: a wrong assignment shows up as a walking sound on a
     * stationary npc, and the names make that obvious where the numbers do not.
     */
    if( npctype->sound_idle >= 0 || npctype->sound_crawl >= 0 || npctype->sound_walk >= 0 ||
        npctype->sound_run >= 0 )
    {
        static int debug = -1;

        if( debug < 0 )
        {
            const char* env = getenv("TORIRS_NPC_SOUND_DEBUG");
            debug = env && *env && *env != '0';
        }
        if( debug )
            TORIRS_LOG("npc-sound: %-5d %-28s idle %-6d crawl %-6d walk %-6d run %-6d "
                "radius %-3d vol %d\n",
                npc_id,
                npctype->name,
                npctype->sound_idle,
                npctype->sound_crawl,
                npctype->sound_walk,
                npctype->sound_run,
                npctype->sound_radius,
                npctype->ambient_sound_volume);
    }

    torirs_npctype_copy_pairs_int(
        &npctype->recolors_from,
        &npctype->recolors_to,
        &npctype->recolor_count,
        src->recolor_to_find,
        src->recolor_to_replace,
        src->recolor_count);
    torirs_npctype_copy_pairs_int(
        &npctype->retextures_from,
        &npctype->retextures_to,
        &npctype->retexture_count,
        src->retexture_to_find,
        src->retexture_to_replace,
        src->retexture_count);

    npctype->readyanim = src->standing_animation > 0 ? src->standing_animation : -1;
    /* Opcode 130 (rev 236+): restart the idle when an action animation finishes.
     * Decoded here since 236 and read by nobody until now — see
     * World_StepEntityAnimation. dat1 has no equivalent opcode. */
    npctype->idle_anim_restart = src->idle_anim_restart;
    npctype->walkanim = src->walking_animation > 0 ? src->walking_animation : -1;
    npctype->walkanim_b = src->rotate180_animation > 0 ? src->rotate180_animation : -1;
    npctype->walkanim_r = src->rotate_right_animation > 0 ? src->rotate_right_animation : -1;
    npctype->walkanim_l = src->rotate_left_animation > 0 ? src->rotate_left_animation : -1;
    /* The rest of the set, decoded since forever and dropped here until now. */
    npctype->turnanim_l =
        src->idle_rotate_left_animation > 0 ? src->idle_rotate_left_animation : -1;
    npctype->turnanim_r =
        src->idle_rotate_right_animation > 0 ? src->idle_rotate_right_animation : -1;
    npctype->runanim = src->run_animation > 0 ? src->run_animation : -1;
    npctype->runanim_b = src->run_rotate180_animation > 0 ? src->run_rotate180_animation : -1;
    npctype->runanim_r =
        src->run_rotate_right_animation > 0 ? src->run_rotate_right_animation : -1;
    npctype->runanim_l = src->run_rotate_left_animation > 0 ? src->run_rotate_left_animation : -1;
    npctype->crawlanim = src->crawl_animation > 0 ? src->crawl_animation : -1;
    npctype->crawlanim_b =
        src->crawl_rotate180_animation > 0 ? src->crawl_rotate180_animation : -1;
    npctype->crawlanim_r =
        src->crawl_rotate_right_animation > 0 ? src->crawl_rotate_right_animation : -1;
    npctype->crawlanim_l =
        src->crawl_rotate_left_animation > 0 ? src->crawl_rotate_left_animation : -1;
    /* They do carry it: opcode 103, decoded as `rotation_speed`, defaulted to 32
     * by the decoder itself — this line used to say dat2 had no such field and
     * pinned 32, which silently overrode every record that states one. `0` means
     * "never turns", and it is how a loc-like npc keeps a fixed facing: the
     * Inferno's Ancestral Glyph states 0 so it slides along its row still facing
     * the arena, and with 32 forced it swung to face east and west as it walked. */
    npctype->turn_speed = src->rotation_speed;
    /* The dat2 decoder calloc's its struct, so an absent opcode 97/98 reads 0, not the
     * 128 the reference defaults to — map it here or every unscaled npc collapses. */
    npctype->width_scale = src->width_scale > 0 ? src->width_scale : 128;
    npctype->height_scale = src->height_scale > 0 ? src->height_scale : 128;
    /* Opcode 124, already -1 from the decoder when the record does not state
     * it. Note 123 is a boolean here and only carries a height on RS2 caches. */
    npctype->height = src->height;
    npctype->alwaysontop = src->has_render_priority;
    npctype->minimap_visible = src->is_minimap_visible;
    npctype->interactable = src->is_interactable;
    npctype->ambient = src->ambient;
    npctype->contrast = src->contrast;
    /*
     * Overhead prayer icon (opcode 102). The decoder keeps a list; the client
     * plots one, so the first entry is the one carried. Pre-210 caches store a
     * bare sprite index with no archive — `npc_copy_head_icons` in cachepack
     * documents that shape — so a record with an index and no group is read as
     * "the prayer pack", which is what the group would have named anyway.
     */
    npctype->head_icon_group = -1;
    npctype->head_icon_index = -1;
    if( src->head_icon_count > 0 && src->head_icon_sprite_index )
    {
        npctype->head_icon_index = src->head_icon_sprite_index[0];
        if( src->head_icon_archive_ids )
            npctype->head_icon_group = src->head_icon_archive_ids[0];
    }

    /*
     * Client render hints ride the npc's params.
     *
     * The keys the client itself acts on are resolved here into plain fields,
     * and the whole table is carried alongside for the SCRIPTS -- `nc_param`
     * reads it by key. It used to be only the former, on the reasoning that
     * "the client is not a script host"; it is one, and clientscript 5350 sorts
     * hovered npcs into highlight groups by `param_2312`. A key the content
     * never sets simply leaves its field at 0.
     */
    for( int i = 0; i < src->params.count; i++ )
    {
        if( src->params.keys[i] != TORIRS_PARAM_ZBUFFER_MODEL )
            continue;
        if( src->params.kinds && src->params.kinds[i] == RSCACHE_PARAM_STRING )
            continue;
        if( src->params.values[i] )
            npctype->zbuffer_model = *(int*)src->params.values[i];
    }
    /* ... and the whole table, which the scripts read by key. */
    torirs_npctype_copy_params(npctype, &src->params);

    /*
     * NpcType.multiNpc (opcode 106) -- a shell record with no model of its
     * own, resolved through VarPManager_ResolveTransform exactly like a
     * loc's transform table. `configs` already carries the decoder's own
     * -1 sentinel for a hidden entry (0xFFFF on the wire), so it copies
     * straight across.
     */
    npctype->transform_varbit = src->varbit_id;
    npctype->transform_varp = src->varp_index;
    if( src->configs_count > 0 )
    {
        npctype->transforms = malloc((size_t)src->configs_count * sizeof(int));
        assert(npctype->transforms);
        memcpy(npctype->transforms, src->configs, (size_t)src->configs_count * sizeof(int));
        npctype->transform_count = src->configs_count;
    }

    return npctype;
}
