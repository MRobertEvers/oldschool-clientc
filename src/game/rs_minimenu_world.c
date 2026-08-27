#include "rs_minimenu_world.h"

#include "rs_minimenu_build.h"

#include "rs_attack_option.h"
#include "world/entity_player.h"
#include "world/world.h"
#include "world/wev.h"
#include "world/world_pickset.h"

#include <datatypes/dat2_config_loc.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Row text below is display-only (dispatch runs on the separate action/pick
 * fields) and safely clipped by snprintf if a name/tooltip is unusually long,
 * so GCC's worst-case-from-declared-array-bounds truncation math is noise
 * here. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

/* Level-difference colour tag for a combat level shown to the local player
 * (reference Client.combatColourCode, Client.ts:10111). diff = viewer - other:
 * an NPC far above the player reads red/orange, far below reads green, equal
 * yellow. viewer < 0 means "no local player" — the caller then omits the level
 * entirely (reference gates the whole `(level-N)` suffix on `this.localPlayer`). */
static char const*
combat_colour_code(int viewer, int other)
{
    int diff = viewer - other;
    if( diff < -9 )
        return "@red@";
    if( diff < -6 )
        return "@or3@";
    if( diff < -3 )
        return "@or2@";
    if( diff < 0 )
        return "@or1@";
    if( diff > 9 )
        return "@gre@";
    if( diff > 6 )
        return "@gr3@";
    if( diff > 3 )
        return "@gr2@";
    if( diff > 0 )
        return "@gr1@";
    return "@yel@";
}

/* The rev-254 OP*1..5 action ids are NOT contiguous (revconfig.h:52-69), so
 * `OP*1 + slot` only resolves correctly for slot 0. The reference assigns them
 * with a per-slot switch (Client.ts addNpcOptions:9730-9740); mirror that with
 * a lookup so RS_Minimenu_CrossModeForAction (an exact-id switch) still matches
 * and the interact cross appears for ops in slots 1..4 (bug: NPC attack/talk
 * often lands on a non-zero slot → no red cross). */
static int
opnpc_action_for_slot(int slot)
{
    static int const ids[5] = {
        REVCONFIG_MINIMENU_OPNPC1, REVCONFIG_MINIMENU_OPNPC2, REVCONFIG_MINIMENU_OPNPC3,
        REVCONFIG_MINIMENU_OPNPC4, REVCONFIG_MINIMENU_OPNPC5,
    };
    return (slot >= 0 && slot < 5) ? ids[slot] : REVCONFIG_MINIMENU_OPNPC1;
}

static int
oploc_action_for_slot(int slot)
{
    static int const ids[5] = {
        REVCONFIG_MINIMENU_OPLOC1, REVCONFIG_MINIMENU_OPLOC2, REVCONFIG_MINIMENU_OPLOC3,
        REVCONFIG_MINIMENU_OPLOC4, REVCONFIG_MINIMENU_OPLOC5,
    };
    return (slot >= 0 && slot < 5) ? ids[slot] : REVCONFIG_MINIMENU_OPLOC1;
}

static int
opobj_action_for_slot(int slot)
{
    static int const ids[5] = {
        REVCONFIG_MINIMENU_OPOBJ1, REVCONFIG_MINIMENU_OPOBJ2, REVCONFIG_MINIMENU_OPOBJ3,
        REVCONFIG_MINIMENU_OPOBJ4, REVCONFIG_MINIMENU_OPOBJ5,
    };
    return (slot >= 0 && slot < 5) ? ids[slot] : REVCONFIG_MINIMENU_OPOBJ1;
}

/* True and emits the single use/target row when a select mode is active for
 * this target kind (reference addWorldOptions useMode/targetMode branches):
 * "Use <obj> with <colour><name>" or "<targetOp> <colour><name>". mask_bit is
 * the target's targetMask bit (0x1 obj / 0x2 npc / 0x4 loc). */
static bool
add_world_select_row(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct UIMinimenuPick pick,
    char const* colour,
    char const* name,
    int mask_bit,
    int use_action,
    int tgt_action)
{
    char text[UITREE_MINIMENU_OPTION_LEN];

    if( sel->mode == RS_MINIMENU_SELECT_USE_ITEM )
    {
        snprintf(
            text,
            sizeof(text),
            "Use %s with %s%s",
            sel->obj_name[0] ? sel->obj_name : "item",
            colour,
            name);
        UIMinimenu_AddOption(menu, text, use_action, 0, pick);
        return true;
    }
    if( sel->mode == RS_MINIMENU_SELECT_TARGET )
    {
        if( (sel->target_mask & mask_bit) == 0 )
            return true; /* mode active but this kind is not a valid target */
        snprintf(text, sizeof(text), "%s %s%s", sel->target_op, colour, name);
        UIMinimenu_AddOption(menu, text, tgt_action, 0, pick);
        return true;
    }
    return false;
}

/*
 * "Depends on combat levels", the half of the NPC Attack option that is not
 * about the Attack row at all.
 *
 * The settings-era reference's per-op priority bump (Statics.method7229) tests
 * the option and the level difference OUTSIDE its is-this-the-attack-pass
 * branch, so with Depends selected a higher-level NPC has EVERY row
 * deprioritized, not just Attack — left-clicking a level-21 guard at level 10
 * walks rather than pickpockets. Only the "Always right-click" arm is
 * attack-pass-only.
 *
 * The 2004 client computes the same comparison inside its attack pass and
 * nowhere else (Client-TS addNpcOptions), so under the classic model this
 * spread does not happen and Pickpocket stays the left click.
 */
static bool
npc_option_deprioritizes(
    int attack_option_model,
    int npc_attack_option,
    struct WorldEntity_NPC const* npc,
    int viewer_combat_level)
{
    return attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS &&
           npc_attack_option == RS_ATTACK_OPTION_DEPENDS && viewer_combat_level >= 0 &&
           npc->combat_level > viewer_combat_level;
}

/* The attack pass's own bump, which both eras compute: the target out-levels
 * the viewer. Shared so the classic model still gets it with the spread above
 * turned off. */
static bool
npc_attack_row_deprioritizes(
    int npc_attack_option,
    struct WorldEntity_NPC const* npc,
    int viewer_combat_level)
{
    return npc_attack_option == RS_ATTACK_OPTION_DEPENDS && viewer_combat_level >= 0 &&
           npc->combat_level > viewer_combat_level;
}

static void
add_npc_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuBuildCtx const* ctx,
    struct RS_MinimenuSelection const* sel,
    struct WorldEntity_NPC const* npc,
    struct World_Picked const* picked,
    int viewer_combat_level)
{
    int const attack_option = ctx->npc_attack_option;
    char text[UITREE_MINIMENU_OPTION_LEN];
    char tooltip[UITREE_MINIMENU_OPTION_LEN];
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_NPC,
        .id = picked->element_id,
        .secondary_id = npc->npc_id,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
    };

    if( npc->multinpc_hidden )
        return;

    /* Colour the level by its distance from the local player's combat level
     * (reference addNpcOptions:9695 — `name + combatColourCode(localPlayer,
     * vislevel) + ' (level-N)'`). The suffix is gated on a local player being
     * present (viewer_combat_level >= 0), matching the reference guard. */
    if( npc->combat_level > 0 && viewer_combat_level >= 0 )
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s%s (level-%d)",
            npc->name[0] ? npc->name : "NPC",
            combat_colour_code(viewer_combat_level, npc->combat_level),
            npc->combat_level);
    else
        snprintf(tooltip, sizeof(tooltip), "%s", npc->name[0] ? npc->name : "NPC");

    if( add_world_select_row(
            menu, sel, pick, "@yel@", tooltip, 0x2, REVCONFIG_MINIMENU_USEHELD_ONNPC,
            REVCONFIG_MINIMENU_TGT_NPC) )
        return;

    /* Non-attack ops first, then attack, so attack draws below by insertion
     * order — the reference runs the same builder twice, once per pass
     * (Statics.method7229 called with its attack flag false then true). */
    for( int i = 4; i >= 0; i-- )
    {
        int action;
        if( (npc->visible_ops & (1u << i)) == 0 )
            continue;
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") == 0 )
            continue;
        action = opnpc_action_for_slot(i);
        if( npc_option_deprioritizes(
                ctx->attack_option_model, attack_option, npc, viewer_combat_level) )
            action = UIMinimenu_ActionDeprioritize(action);
        snprintf(text, sizeof(text), "%s @yel@%s", npc->actions[i].name, tooltip);
        UIMinimenu_AddOption(menu, text, action, i, pick);
    }
    /* "Hidden" drops the whole attack pass; the reference `continue`s before
     * the row is built, so an NPC with nothing but Attack gets no row at all
     * (Examine below still appears). */
    if( attack_option != RS_ATTACK_OPTION_HIDDEN )
    {
        for( int i = 4; i >= 0; i-- )
        {
            int action;
            if( (npc->visible_ops & (1u << i)) == 0 )
                continue;
            if( npc->actions[i].name[0] == '\0' )
                continue;
            if( strcasecmp(npc->actions[i].name, "attack") != 0 )
                continue;
            action = opnpc_action_for_slot(i);
            if( attack_option == RS_ATTACK_OPTION_RIGHTCLICK ||
                npc_attack_row_deprioritizes(attack_option, npc, viewer_combat_level) )
                action = UIMinimenu_ActionDeprioritize(action);
            snprintf(text, sizeof(text), "%s @yel@%s", npc->actions[i].name, tooltip);
            UIMinimenu_AddOption(menu, text, action, i, pick);
        }
    }

    snprintf(text, sizeof(text), "Examine @yel@%s", tooltip);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPNPC6, 0, pick);
}

/* TORIRS_LOC_DEBUG name suffix: the two coordinates a misplaced loc is judged
 * by — the scene slot it occupies and the absolute tile that slot resolves to.
 * Appended to the loc name so it rides every row of this loc, including the
 * acting row the hover line is composed from (UIHoverText_Compose takes the
 * last row after the priority sort, so a separate row would never show there).
 * Kept short for the same reason: the hover line has one viewport-wide line to
 * draw in. The full record is a row of its own, below. */
static char const*
scenery_debug_name(
    struct World* world,
    struct WorldEntity_Scenery const* scenery,
    char* buf,
    int cap)
{
    char const* name = scenery->info->name[0] ? scenery->info->name : "Scenery";

    if( !WorldEntity_SceneryDebugEnabled() )
        return name;
    assert(world);

    /* The footprint rides here, not only on the detail row: the painter orders
     * scenery by its width x depth, so "which tiles does the client think this
     * loc covers" is the first question of every placement or draw-order bug,
     * and this is the one string that reaches the hover line. `draw_size_*` is
     * the placed footprint (orientation applied), which is the one the painter
     * used — the config's own size is on the detail row beside it. */
    snprintf(
        buf,
        cap,
        "%s @whi@sc(%d,%d) abs(%d,%d) %dx%d",
        name,
        scenery->grid_position.x,
        scenery->grid_position.z,
        world->_base_tile_x + scenery->grid_position.x,
        world->_base_tile_z + scenery->grid_position.z,
        scenery->debug.draw_size_x,
        scenery->debug.draw_size_z);
    return buf;
}

/* TORIRS_LOC_DEBUG detail row: the rest of WorldEntity_SceneryDebug — where
 * the loc came from, and where its geometry actually landed. `map` is what the
 * cache asked for (square * 64 + chunk-local) and `abs` on the name suffix is
 * what the scene slot resolved to; the two disagreeing is the bug this exists
 * to show. `f` is the element's placed fine position and `t` the SW tile that
 * implies, which catches a slot that is right while the geometry is not — `t`
 * must equal `sc` on the name suffix. `t` backs the footprint offset out of
 * `f` rather than shifting it a whole tile, because placement centres a model
 * over its draw footprint: a 2x2 loc anchored on tile 25 sits at fine 3328,
 * whose raw tile is 26 and would read as an off-by-one that is not there.
 *
 * Carries the walk action and a TERRAIN pick at the loc's own slot, for two
 * reasons. Sorting: an inert (> 1000) row sinks below the acting rows, and the
 * hover line is composed from the LAST row — so on the inactive locs this mode
 * exists to inspect (a bush has no ops at all) the readout would never reach
 * the hover line, which is the whole ask. Clicking: the pick dispatch switches
 * on kind alone, so this walks to the tile the loc claims — the same thing
 * "Walk here" does, and a direct check of the claim. */
/* Rotate a model-local AABB by the element's draw yaw, in place.
 *
 * Needed because a loc's angle reaches the geometry by one of two routes. A
 * static loc has it baked into the vertices at build (so the captured extent is
 * already rotated), but an ANIMATED loc is loaded unrotated and turned by the
 * element yaw at draw time — its captured extent is the unrotated box, and
 * reporting a tile span from that names the wrong axis entirely for a quarter
 * turn. Scenery yaw is always a multiple of 256 (512 per angle step, +256 for a
 * diagonal shape), so eight entries cover every value it can hold.
 *
 * Convention matches the projection kernel exactly (projection.u.c
 * project_orthographic_fast_pitchyaw): x' = x*cos + z*sin, z' = z*cos - x*sin,
 * 16.16 fixed point. */
static void
rotate_extent_by_yaw(
    int yaw,
    int* min_x,
    int* max_x,
    int* min_z,
    int* max_z)
{
    /* sin/cos * 65536 at 1/8 turn steps, index = yaw / 256. */
    static int const sin8[8] = { 0, 46341, 65536, 46341, 0, -46341, -65536, -46341 };
    static int const cos8[8] = { 65536, 46341, 0, -46341, -65536, -46341, 0, 46341 };
    int const corners_x[4] = { *min_x, *max_x, *min_x, *max_x };
    int const corners_z[4] = { *min_z, *min_z, *max_z, *max_z };
    int idx;
    int out_min_x, out_max_x, out_min_z, out_max_z;

    yaw = ((yaw % 2048) + 2048) % 2048;
    if( yaw % 256 != 0 )
        return; /* not a value scenery placement can produce; leave unrotated */
    idx = yaw / 256;
    if( idx == 0 )
        return;

    out_min_x = out_max_x = (corners_x[0] * cos8[idx] + corners_z[0] * sin8[idx]) >> 16;
    out_min_z = out_max_z = (corners_z[0] * cos8[idx] - corners_x[0] * sin8[idx]) >> 16;
    for( int i = 1; i < 4; i++ )
    {
        int const rx = (corners_x[i] * cos8[idx] + corners_z[i] * sin8[idx]) >> 16;
        int const rz = (corners_z[i] * cos8[idx] - corners_x[i] * sin8[idx]) >> 16;
        if( rx < out_min_x )
            out_min_x = rx;
        if( rx > out_max_x )
            out_max_x = rx;
        if( rz < out_min_z )
            out_min_z = rz;
        if( rz > out_max_z )
            out_max_z = rz;
    }
    *min_x = out_min_x;
    *max_x = out_max_x;
    *min_z = out_min_z;
    *max_z = out_max_z;
}

/* Where the geometry sits, as opposed to where the slot is. The element
 * position places the model's ORIGIN, so a correct slot still draws on the
 * wrong tile if the model's mass is not centred on that origin — `ctr` is the
 * midpoint of the post-transform extent and reads 0,0 for a centred model.
 * `tiles` is the absolute tile span the geometry actually covers, which is the
 * number to hold against another renderer: it answers "which tiles does this
 * bush occupy on screen" without going through any of our own slot math. */
static void
add_scenery_geometry_row(
    struct UIMinimenu* menu,
    struct World* world,
    struct WorldEntity_Scenery const* scenery,
    struct UIMinimenuPick pick)
{
    struct WorldEntity_SceneryDebug const* dbg = &scenery->debug;
    int const base_x = world ? world->_base_tile_x : 0;
    int const base_z = world ? world->_base_tile_z : 0;
    /* Drawn extent: the captured box turned by whatever the element yaw will
     * apply at draw. Zero for a static loc (its angle is already in the
     * vertices), a quarter turn per angle step for an animated one. */
    int min_x = dbg->model_min_x;
    int max_x = dbg->model_max_x;
    int min_z = dbg->model_min_z;
    int max_z = dbg->model_max_z;
    int world_min_x;
    int world_max_x;
    int world_min_z;
    int world_max_z;
    char text[UITREE_MINIMENU_OPTION_LEN];

    rotate_extent_by_yaw(dbg->draw_yaw, &min_x, &max_x, &min_z, &max_z);
    world_min_x = dbg->draw_x + min_x;
    world_max_x = dbg->draw_x + max_x;
    world_min_z = dbg->draw_z + min_z;
    world_max_z = dbg->draw_z + max_z;

    snprintf(
        text,
        sizeof(text),
        "@whi@mdl x[%d..%d] z[%d..%d] ctr(%d,%d) tiles x[%d..%d] z[%d..%d]",
        min_x,
        max_x,
        min_z,
        max_z,
        (min_x + max_x) / 2,
        (min_z + max_z) / 2,
        base_x + (world_min_x >> 7),
        base_x + (world_max_x >> 7),
        base_z + (world_min_z >> 7),
        base_z + (world_max_z >> 7));
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_WALK, -1, pick);
}

static void
add_scenery_debug_row(
    struct UIMinimenu* menu,
    struct World* world,
    struct WorldEntity_Scenery const* scenery)
{
    struct WorldEntity_SceneryDebug const* dbg = &scenery->debug;
    /* The scene origin this loc was placed under. A survivor of an earlier
     * rebuild carries the old one, and then its slot is right for a scene that
     * no longer exists — which renders exactly like a misplaced loc. */
    bool const stale_origin = world && (dbg->base_tile_x != world->_base_tile_x ||
                                        dbg->base_tile_z != world->_base_tile_z);
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_TERRAIN,
        .id = scenery->element_id,
        .secondary_id = scenery->grid_position.x,
        .tertiary_id = scenery->grid_position.z,
        .quaternary_id = scenery->grid_position.level,
    };
    char text[UITREE_MINIMENU_OPTION_LEN];

    /* The ground this loc stands on, in the same spelling the Walk-here row
     * uses (World_TileSettingsText). A loc's own record cannot say why it draws
     * or picks the way it does when the column under it is a bridge deck or
     * carries VIS_BELOW, and reading that off a second hover — of the tile, not
     * the loc — means the two readings are of different frames. `l` is the
     * loc's own level, `d` the level the floor beneath it draws at.
     *
     * First of the three debug rows so the geometry and placement rows keep
     * their existing order, and the placement row stays the last inserted. */
    {
        char settings[4 * 6 + 1];
        char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];

        World_TileSettingsText(
            world, scenery->grid_position.x, scenery->grid_position.z, settings,
            (int)sizeof(settings));
        World_TerrainMeshLevelsText(
            world, scenery->grid_position.x, scenery->grid_position.z, meshes,
            (int)sizeof(meshes));
        snprintf(
            text,
            sizeof(text),
            "@whi@tile sc(%d,%d) l%d d%d s[%s] m[%s]",
            scenery->grid_position.x,
            scenery->grid_position.z,
            scenery->grid_position.level,
            World_TerrainDrawLevel(
                world, scenery->grid_position.x, scenery->grid_position.z,
                scenery->grid_position.level),
            settings,
            meshes);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_WALK, -1, pick);
    }

    /* Geometry first so the placement row stays the last-inserted one, and so
     * stays the row the hover line is composed from. */
    add_scenery_geometry_row(menu, world, scenery, pick);

    snprintf(
        text,
        sizeof(text),
        "@whi@loc %d%s%s sq(%d,%d) ch(%d,%d) map(%d,%d) l%d sh%d a%d "
        "f(%d,%d) t(%d,%d) y%d cfg%dx%d drw%dx%d rt%dx%d el%d",
        scenery->loc_id,
        dbg->runtime ? " RT" : "",
        stale_origin ? " STALE-ORIGIN" : "",
        dbg->map_square_x,
        dbg->map_square_z,
        dbg->chunk_x,
        dbg->chunk_z,
        dbg->map_square_x * 64 + dbg->chunk_x,
        dbg->map_square_z * 64 + dbg->chunk_z,
        dbg->chunk_level,
        scenery->shape,
        scenery->angle,
        dbg->draw_x,
        dbg->draw_z,
        (dbg->draw_x - 64 * dbg->draw_size_x) >> 7,
        (dbg->draw_z - 64 * dbg->draw_size_z) >> 7,
        dbg->draw_yaw,
        dbg->config_size_x,
        dbg->config_size_z,
        dbg->draw_size_x,
        dbg->draw_size_z,
        scenery->size_x,
        scenery->size_z,
        scenery->element_id);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_WALK, -1, pick);
}

/* The map editor SELECT tool's row label: which LAYER a loc occupies, since
 * that (not the loc's own name) is what a tile-tool session cares about --
 * "Select Wall" says which of a stacked wall + wall-decor + ground-decor the
 * row will latch, the way the loc's own actions row already says what it is. */
static char const*
mapedit_select_category(int shape)
{
    switch( shape )
    {
    case RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE:
    case RSCACHE_LOC_SHAPE_WALL_TRI_CORNER:
    case RSCACHE_LOC_SHAPE_WALL_TWO_SIDES:
    case RSCACHE_LOC_SHAPE_WALL_RECT_CORNER:
    case RSCACHE_LOC_SHAPE_WALL_DIAGONAL:
        return "Wall";
    case RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE:
    case RSCACHE_LOC_SHAPE_WALL_DECOR_OUTSIDE:
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE:
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE:
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        return "Wall Decor";
    case RSCACHE_LOC_SHAPE_SCENERY:
    case RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL:
        return "Object";
    case RSCACHE_LOC_SHAPE_FLOOR_DECORATION:
        return "Ground Decor";
    default:
        /* Roof shapes (12-21): rare on a right-click menu, and share no
         * single-word name worth inventing one for. */
        return "Loc";
    }
}

static void
add_scenery_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct World* world,
    struct WorldEntity_Scenery const* scenery,
    struct World_Picked const* picked,
    bool locedit_active,
    bool mapedit_select_active)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char name_buf[UITREE_MINIMENU_OPTION_LEN];
    char const* name = scenery_debug_name(world, scenery, name_buf, sizeof(name_buf));
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_SCENERY,
        .id = picked->element_id,
        .secondary_id = scenery->loc_id,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
        .view_id = picked->view_id,
    };

    if( add_world_select_row(
            menu, sel, pick, "@cya@", name, 0x4, REVCONFIG_MINIMENU_USEHELD_ONLOC,
            REVCONFIG_MINIMENU_TGT_LOC) )
        return;

    if( WorldEntity_SceneryDebugEnabled() )
        add_scenery_debug_row(menu, world, scenery);

    for( int i = 4; i >= 0; i-- )
    {
        if( scenery->info->actions[i].name[0] == '\0' )
            continue;
        snprintf(
            text, sizeof(text), "%s @cya@%s", scenery->info->actions[i].name, name);
        UIMinimenu_AddOption(menu, text, oploc_action_for_slot(i), i, pick);
    }

    snprintf(text, sizeof(text), "Examine @cya@%s", name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPLOC6, 0, pick);

    /* Loc editor dev tool (src/app.c): every loc that earns an Examine row
     * also earns a Select row while the tool is open, so its target comes
     * from the same pick/classify/dedup pipeline the real minimenu already
     * uses to disambiguate a tile -- not a guess at "first loc on the tile"
     * by layer. Intercepted client-side in app_minimenu_run_option before it
     * can reach the real OPLOC dispatch; never sent to a server. */
    if( locedit_active )
    {
        snprintf(text, sizeof(text), "Select @cya@%s", name);
        UIMinimenu_AddOption(menu, text, RS_MINIMENU_ACTION_LOCEDIT_SELECT, 0, pick);
    }

    /* Map editor's own twin, labeled by shape category rather than name --
     * see mapedit_select_category. Both tools may be open at once, on
     * different subjects, so this is additive to the row above, not a
     * replacement for it. */
    if( mapedit_select_active )
    {
        snprintf(
            text, sizeof(text), "Select %s @cya@%s", mapedit_select_category(scenery->shape),
            name);
        UIMinimenu_AddOption(menu, text, RS_MINIMENU_ACTION_MAPEDIT_SELECT, 0, pick);
    }

}

/* Ground-item rows (Client.ts addWorldOptions entityType 3): ObjType.op in
 * reverse slot order, with a defaulted "Take" whenever op slot 2 is empty,
 * then Examine. Colour tag is @lre@, not the loc @cya@. */
static void
add_obj_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct WorldEntity_ObjStack const* stack,
    struct World_Picked const* picked)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char const* name = stack->name[0] ? stack->name : "Item";
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_OBJ,
        .id = picked->element_id,
        .secondary_id = stack->obj_id,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
    };

    if( add_world_select_row(
            menu, sel, pick, "@lre@", name, 0x1, REVCONFIG_MINIMENU_USEHELD_ONOBJ,
            REVCONFIG_MINIMENU_TGT_OBJ) )
        return;

    for( int i = 4; i >= 0; i-- )
    {
        if( stack->actions[i].name[0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s @lre@%s", stack->actions[i].name, name);
            UIMinimenu_AddOption(menu, text, opobj_action_for_slot(i), i, pick);
        }
        else if( i == 2 )
        {
            snprintf(text, sizeof(text), "Take @lre@%s", name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPOBJ3, 2, pick);
        }
    }

    snprintf(text, sizeof(text), "Examine @lre@%s", name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPOBJ6, 0, pick);
}

/* True when this obj_id on (tile_x, tile_z) already has a menu row — avoids
 * double-emitting when both an entity pick expands the tile and an obj pick
 * for the same stack is also in the pickset. */
static bool
menu_has_obj_on_tile(
    struct UIMinimenu const* menu,
    int obj_id,
    int tile_x,
    int tile_z)
{
    for( int i = 0; i < menu->option_count; i++ )
    {
        struct UIMinimenuPick const* p = &menu->options[i].pick;
        if( p->kind == UI_MINIMENU_PICK_OBJ && p->secondary_id == obj_id &&
            p->tertiary_id == tile_x && p->quaternary_id == tile_z )
            return true;
    }
    return false;
}

/* Client-TS entityType 3: one ground-obj pick lists every obj on the tile.
 * Entity models often occlude the item pick here, so NPC/player stack expansion
 * also pulls in every ObjStack on the same grid tile (deduped). */
static void
add_objs_on_tile(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct World* world,
    int tile_x,
    int tile_z,
    int tile_level)
{
    struct World_EntityPool* pool;

    assert(menu && sel && world);
    pool = &world->entities.obj_stack;
    for( int oi = World_EntityPoolHead(pool); oi != WORLD_ENTITY_NIL;
         oi = World_EntityPoolNext(pool, oi) )
    {
        struct WorldEntity_ObjStack* stack = World_EntityPoolGet(pool, oi);
        if( !stack || stack->element_id < 0 )
            continue;
        if( stack->grid_position.x != tile_x || stack->grid_position.z != tile_z ||
            stack->grid_position.level != tile_level )
            continue;
        if( menu_has_obj_on_tile(menu, stack->obj_id, tile_x, tile_z) )
            continue;
        struct World_Picked other_picked = {
            .element_id = stack->element_id,
            .type = WORLD_PICK_OBJSTACK,
            .tile_x = tile_x,
            .tile_z = tile_z,
            .tile_level = tile_level,
        };
        add_obj_rows(menu, sel, stack, &other_picked);
    }
}

/* Client-TS addWorldOptions (Client.ts:9591-9603): a picked size-1, tile-centred
 * NPC stands in for the whole pile of NPCs on its tile. The renderer draws — and
 * therefore picks — only one model per tile (the one-model-per-tile dedup,
 * world_cycle.c world_dyn_tile_claim), so a stack of NPCs on one square yields a
 * single pick. The menu must still list every one of them. Mirror the reference:
 * scan the NPC pool for the OTHER NPCs sharing the picked NPC's fine draw coords
 * and emit their rows first, then the picked NPC last so its rows sort on top. */
/* Local player's combat level for the level-difference colouring (reference
 * `this.localPlayer.combatLevel`), or -1 when there is no local player yet —
 * the caller then omits the `(level-N)` suffix, matching the reference guard. */
static int
world_local_combat_level(struct World* world)
{
    struct WorldEntity_Player* lp =
        World_PlayerGetByServerPid(world, world->local_pid);
    return lp ? lp->combat_level : -1;
}

/* Local player's team-cape id for the Attack row's team override (reference
 * Statics.method4492, which likewise reads 0 when there is no local player —
 * and 0 is "no team", which disables the override). */
static int
world_local_team(struct World* world)
{
    struct WorldEntity_Player* lp =
        World_PlayerGetByServerPid(world, world->local_pid);
    return lp ? lp->team : 0;
}

static void
add_npc_stack_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuBuildCtx const* ctx,
    struct World* world,
    struct WorldEntity_NPC const* npc,
    struct World_Picked const* picked)
{
    struct RS_MinimenuSelection const* sel = &ctx->selection;
    int viewer_combat_level = world_local_combat_level(world);

    /* Ground items sit under entities and are often occluded from the pick
     * pass — always list every ObjStack on this grid tile. */
    add_objs_on_tile(
        menu, sel, world, picked->tile_x, picked->tile_z, picked->tile_level);

    if( npc->size == 1 && ((int)npc->draw_position.x & 0x7f) == 64 &&
        ((int)npc->draw_position.z & 0x7f) == 64 )
    {
        struct World_EntityPool* pool = &world->entities.npc;
        for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
             ni = World_EntityPoolNext(pool, ni) )
        {
            struct WorldEntity_NPC* other = World_EntityPoolGet(pool, ni);
            if( !other || other->element_id < 0 )
                continue;
            if( other->element_id == npc->element_id || other->size != 1 )
                continue;
            if( other->draw_position.x != npc->draw_position.x ||
                other->draw_position.z != npc->draw_position.z )
                continue;
            struct World_Picked other_picked = {
                .element_id = other->element_id,
                .type = WORLD_PICK_NPC,
                .tile_x = other->grid_position.x,
                .tile_z = other->grid_position.z,
                .tile_level = picked->tile_level,
            };
            add_npc_rows(menu, ctx, sel, other, &other_picked, viewer_combat_level);
        }
    }

    add_npc_rows(menu, ctx, sel, npc, picked, viewer_combat_level);
}

static int
opplayer_action_for_slot(int slot)
{
    static int const ids[5] = {
        REVCONFIG_MINIMENU_OPPLAYER1, REVCONFIG_MINIMENU_OPPLAYER2,
        REVCONFIG_MINIMENU_OPPLAYER3, REVCONFIG_MINIMENU_OPPLAYER4,
        REVCONFIG_MINIMENU_OPPLAYER5,
    };
    return (slot >= 0 && slot < 5) ? ids[slot] : REVCONFIG_MINIMENU_OPPLAYER1;
}

/* Client-TS addPlayerOptions: never emit rows for the local player. Use/target
 * modes short-circuit; otherwise SET_PLAYER_OP text drives OPPLAYER1..5. */
static void
add_player_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuBuildCtx const* ctx,
    struct World* world,
    struct WorldEntity_Player const* player,
    struct World_Picked const* picked)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char tooltip[UITREE_MINIMENU_OPTION_LEN];
    struct RS_MinimenuSelection const* sel;
    int viewer_combat_level;
    int viewer_team;
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_PLAYER,
        .id = picked->element_id,
        .secondary_id = player->server_pid,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
    };

    assert(menu && ctx && world && player && picked);
    if( world->local_pid >= 0 && player->server_pid == world->local_pid )
        return;

    sel = &ctx->selection;
    viewer_combat_level = world_local_combat_level(world);
    viewer_team = world_local_team(world);
    if( player->combat_level > 0 && viewer_combat_level >= 0 )
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s%s (level-%d)",
            player->name[0] ? player->name : "Player",
            combat_colour_code(viewer_combat_level, player->combat_level),
            player->combat_level);
    else
        snprintf(tooltip, sizeof(tooltip), "%s", player->name[0] ? player->name : "Player");

    if( add_world_select_row(
            menu, sel, pick, "@whi@", tooltip, 0x8, REVCONFIG_MINIMENU_USEHELD_ONPLAYER,
            REVCONFIG_MINIMENU_TGT_PLAYER) )
        return;

    if( !ctx->player_ops )
        return;

    for( int i = 4; i >= 0; i-- )
    {
        int action;
        bool deprioritize;
        char const* op = ctx->player_ops[i];
        if( !op || op[0] == '\0' )
            continue;
        action = opplayer_action_for_slot(i);
        if( strcasecmp(op, "attack") == 0 )
        {
            if( ctx->player_attack_option == RS_ATTACK_OPTION_HIDDEN )
                continue;

            deprioritize =
                ctx->player_attack_option == RS_ATTACK_OPTION_RIGHTCLICK ||
                (ctx->player_attack_option == RS_ATTACK_OPTION_DEPENDS &&
                 viewer_combat_level >= 0 && player->combat_level > viewer_combat_level);

            /*
             * Team capes OVERRIDE the setting in both directions, which is why
             * this is an assignment and not another `||`: with both players in
             * a team, a different team is always left-click-attackable and the
             * same team never is, whatever the dropdown says (reference
             * Statics.method2599 breaks out of the whole attack branch here).
             * A zero on either side leaves the setting in charge.
             */
            if( viewer_team != 0 && player->team != 0 )
                deprioritize = player->team == viewer_team;
            else if(
                ctx->player_attack_option == RS_ATTACK_OPTION_CLAN && ctx->is_clan_member &&
                ctx->is_clan_member(ctx->clan_user, player->name) )
                deprioritize = true;
        }
        else
        {
            /* SET_PLAYER_OP's own primary flag — nothing to do with the
             * Attack setting, and the reference tests it only on the ops the
             * attack branch did not claim. */
            deprioritize = ctx->player_ops_primary && !ctx->player_ops_primary[i];
        }
        if( deprioritize )
            action = UIMinimenu_ActionDeprioritize(action);
        snprintf(text, sizeof(text), "%s @whi@%s", op, tooltip);
        UIMinimenu_AddOption(menu, text, action, i, pick);
    }

    /* Reference: rename the existing Walk here row with this player's tooltip. */
    for( int i = 0; i < menu->option_count; i++ )
    {
        if( menu->options[i].action == REVCONFIG_MINIMENU_WALK )
        {
            snprintf(
                menu->options[i].text,
                sizeof(menu->options[i].text),
                "Walk here @whi@%s",
                tooltip);
            break;
        }
    }
}

/* Client-TS addWorldOptions player branch (Client.ts:9607-9627): a picked
 * tile-centred player stands in for the whole pile — expand co-located size-1
 * NPCs and other players, then the picked player (no-op when local). Ground
 * items on the tile are included too (see add_objs_on_tile). */
static void
add_player_stack_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuBuildCtx const* ctx,
    struct World* world,
    struct WorldEntity_Player const* player,
    struct World_Picked const* picked)
{
    int viewer_combat_level = world_local_combat_level(world);

    assert(menu && ctx && world && player && picked);

    add_objs_on_tile(
        menu, &ctx->selection, world, picked->tile_x, picked->tile_z, picked->tile_level);

    if( ((int)player->draw_position.x & 0x7f) == 64 &&
        ((int)player->draw_position.z & 0x7f) == 64 )
    {
        struct World_EntityPool* npc_pool = &world->entities.npc;
        struct World_EntityPool* player_pool = &world->entities.player;

        for( int ni = World_EntityPoolHead(npc_pool); ni != WORLD_ENTITY_NIL;
             ni = World_EntityPoolNext(npc_pool, ni) )
        {
            struct WorldEntity_NPC* other = World_EntityPoolGet(npc_pool, ni);
            if( !other || other->element_id < 0 || other->size != 1 )
                continue;
            if( other->draw_position.x != player->draw_position.x ||
                other->draw_position.z != player->draw_position.z )
                continue;
            struct World_Picked other_picked = {
                .element_id = other->element_id,
                .type = WORLD_PICK_NPC,
                .tile_x = other->grid_position.x,
                .tile_z = other->grid_position.z,
                .tile_level = picked->tile_level,
            };
            add_npc_rows(
                menu, ctx, &ctx->selection, other, &other_picked, viewer_combat_level);
        }

        for( int pi = World_EntityPoolHead(player_pool); pi != WORLD_ENTITY_NIL;
             pi = World_EntityPoolNext(player_pool, pi) )
        {
            struct WorldEntity_Player* other = World_EntityPoolGet(player_pool, pi);
            if( !other || other->element_id < 0 )
                continue;
            if( other->element_id == player->element_id )
                continue;
            if( other->draw_position.x != player->draw_position.x ||
                other->draw_position.z != player->draw_position.z )
                continue;
            struct World_Picked other_picked = {
                .element_id = other->element_id,
                .type = WORLD_PICK_PLAYER,
                .tile_x = other->grid_position.x,
                .tile_z = other->grid_position.z,
                .tile_level = picked->tile_level,
            };
            add_player_rows(menu, ctx, world, other, &other_picked);
        }
    }

    add_player_rows(menu, ctx, world, player, picked);
}

void
RS_Minimenu_AddWorldRows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UIMinimenu* menu)
{
    struct World_PickSet const* picks;
    struct RS_MinimenuSelection const* sel = &ctx->selection;

    assert(ctx && menu);
    if( !ctx->click_in_world || !ctx->world || !ctx->world_pickset )
        return;
    picks = ctx->world_pickset;

    /* Walk here targets the nearest picked tile — the painter walks
     * back-to-front, so that is the LAST terrain item (matches the hover
     * tile the click cross and spawn hotkeys use). Suppressed while a use/
     * target mode is armed (reference gates it on useMode==0 && targetMode==0). */
    if( sel->mode == RS_MINIMENU_SELECT_NONE )
    {
        struct World_Picked const* terrain = NULL;
        for( int i = 0; i < picks->count; i++ )
            if( picks->items[i].type == WORLD_PICK_TERRAIN )
                terrain = &picks->items[i];
        if( terrain )
        {
            struct UIMinimenuPick pick = {
                .kind = UI_MINIMENU_PICK_TERRAIN,
                .id = terrain->element_id,
                .secondary_id = terrain->tile_x,
                .tertiary_id = terrain->tile_z,
                .quaternary_id = terrain->tile_level,
                .view_id = terrain->view_id,
            };
            /* TORIRS_LOC_DEBUG: name the tile the pointer is actually over,
             * so a loc's slot can be read against the ground under it — and
             * the local player's own tile, which is what the minimap centres
             * on.
             *
             * `l` is the picked mesh level and `d` the level that mesh actually
             * draws and picks against; they differ on exactly the columns where
             * ground behaves surprisingly (a bridge deck reads l1 d0), so the
             * pair is worth more than either alone. `s` is the column's land
             * settings and `m` the levels carrying a floor — see the two
             * helpers above. The local player's fine position stays last
             * because it is the field most often read against a loc's own
             * draw position on the row below. */
            if( WorldEntity_SceneryDebugEnabled() && terrain->view_id == 0 )
            {
                /* Root tiles only: a view pick's tiles are DECK-LOCAL, and
                 * every lookup below (settings, mesh levels, base rebasing)
                 * reads the ROOT world — the readout would be confidently
                 * wrong. The view tile keeps the plain row below. */
                struct WorldEntity_Player* lp =
                    World_PlayerGetByServerPid(ctx->world, ctx->world->local_pid);
                char text[UITREE_MINIMENU_OPTION_LEN];
                char settings[4 * 6 + 1];
                char meshes[WORLD_MAP_TERRAIN_LEVELS + 1];

                World_TileSettingsText(
                    ctx->world, terrain->tile_x, terrain->tile_z, settings, (int)sizeof(settings));
                World_TerrainMeshLevelsText(
                    ctx->world, terrain->tile_x, terrain->tile_z, meshes, (int)sizeof(meshes));
                snprintf(
                    text,
                    sizeof(text),
                    "Walk here @whi@sc(%d,%d) abs(%d,%d) l%d d%d s[%s] m[%s] | you sc(%d,%d) "
                    "f(%d,%d)",
                    terrain->tile_x,
                    terrain->tile_z,
                    ctx->world->_base_tile_x + terrain->tile_x,
                    ctx->world->_base_tile_z + terrain->tile_z,
                    terrain->tile_level,
                    World_TerrainDrawLevel(
                        ctx->world, terrain->tile_x, terrain->tile_z, terrain->tile_level),
                    settings,
                    meshes,
                    lp ? lp->grid_position.x : -1,
                    lp ? lp->grid_position.z : -1,
                    lp ? (int)lp->draw_position.x : -1,
                    lp ? (int)lp->draw_position.z : -1);
                UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_WALK, 0, pick);
            }
            else
                UIMinimenu_AddOption(menu, "Walk here", REVCONFIG_MINIMENU_WALK, 0, pick);

            /* Loc editor: the ground is selectable too, and by the same
             * disambiguated route the Select rows use — the pick set already
             * names the exact tile the pointer is over, so this needs no
             * second hit test and cannot disagree with the row above it. */
            if( ctx->locedit_active )
                UIMinimenu_AddOption(
                    menu, "Select @whi@terrain", RS_MINIMENU_ACTION_LOCEDIT_SELECT_TERRAIN, 0,
                    pick);

            /* Map editor's own twin -- same pick, different tool, both may be
             * open at once. */
            if( ctx->mapedit_select_active )
                UIMinimenu_AddOption(
                    menu, "Select Terrain", RS_MINIMENU_ACTION_MAPEDIT_SELECT_TERRAIN, 0, pick);
        }
        else
        {
            /*
             * Nothing under the cursor drew any ground: the sky, the void
             * around an instance's floor, or a tile the pick's level filter
             * refused. The row is offered either way (the reference always
             * offers it), but it only carries a destination when the caller
             * resolved one — see ground_fallback_valid. Without it the row is
             * inert and the click walks nowhere, which is the reference
             * behaviour and what a click on the sky does.
             */
            struct UIMinimenuPick pick = { .kind = UI_MINIMENU_PICK_NONE };
            if( ctx->ground_fallback_valid )
            {
                pick.kind = UI_MINIMENU_PICK_TERRAIN;
                pick.id = -1; /* no scene element — this tile was not picked */
                pick.secondary_id = ctx->ground_fallback_x;
                pick.tertiary_id = ctx->ground_fallback_z;
                pick.quaternary_id = ctx->ground_fallback_level;
            }
            UIMinimenu_AddOption(menu, "Walk here", REVCONFIG_MINIMENU_WALK, 0, pick);
        }
    }

    /*
     * A picked HULL's config ops (SAILING_PLAN C5.2 / deob menu-hash type 4):
     * a click that landed on a boat's own sub-scene carries the view id, and
     * the WevConfig's five op strings — gated by the wire's 5-bit op mask —
     * become rows against that hull ("Board" on the Zenith). One hull per
     * click, the nearest (last) picked, matching the deob's one-boat-menu
     * rule; flattened hulls never pick, so they never reach here.
     */
    if( sel->mode == RS_MINIMENU_SELECT_NONE && ctx->wevs )
    {
        struct World_Picked const* hull_pick = NULL;

        for( int i = 0; i < picks->count; i++ )
            if( picks->items[i].view_id != 0 )
                hull_pick = &picks->items[i];
        if( hull_pick && Wevs_IsLive(ctx->wevs, hull_pick->view_id) )
        {
            struct Wev const* wev = Wevs_Get((struct Wevs*)ctx->wevs, hull_pick->view_id);

            if( wev->config )
                for( int op = 0; op < WEV_CONFIG_OPS; op++ )
                {
                    struct UIMinimenuPick pick = {
                        .kind = UI_MINIMENU_PICK_WEV,
                        .id = hull_pick->view_id,
                        .secondary_id = op,
                    };
                    char text[UITREE_MINIMENU_OPTION_LEN];

                    if( !wev->config->ops[op] || !(wev->op_mask & (1u << op)) )
                        continue;
                    snprintf(
                        text,
                        sizeof(text),
                        "%s @cya@%s",
                        wev->config->ops[op],
                        wev->config->name ? wev->config->name : "Boat");
                    UIMinimenu_AddOption(menu, text, oploc_action_for_slot(op), op, pick);
                }
        }
    }


    for( int i = 0; i < picks->count; i++ )
    {
        struct World_Picked const* picked = &picks->items[i];
        switch( picked->type )
        {
        case WORLD_PICK_NPC:
        {
            struct WorldEntity_NPC* npc =
                World_NpcGetByElementId(ctx->world, picked->element_id, NULL);
            if( npc )
                add_npc_stack_rows(menu, ctx, ctx->world, npc, picked);
            break;
        }
        case WORLD_PICK_PLAYER:
        {
            struct WorldEntity_Player* player =
                World_PlayerGetByElementId(ctx->world, picked->element_id);
            if( player )
                add_player_stack_rows(menu, ctx, ctx->world, player, picked);
            break;
        }
        case WORLD_PICK_SCENERY:
        {
            /* A deck loc's record lives in its VIEW's world (SAILING_PLAN
             * C5.2) — resolve the pick against the world that owns it. The
             * view world's base is the staging base, so the debug rows'
             * absolute-tile math reads correctly there too. */
            struct World* pick_world = ctx->world;
            if( picked->view_id != 0 )
                pick_world = ctx->view_world_fn
                                 ? ctx->view_world_fn(ctx->view_world_user, picked->view_id)
                                 : NULL;
            if( pick_world )
            {
                struct WorldEntity_Scenery* scenery =
                    World_SceneryGetByElementId(pick_world, picked->element_id);
                if( scenery )
                    add_scenery_rows(
                        menu, sel, pick_world, scenery, picked, ctx->locedit_active,
                        ctx->mapedit_select_active);
            }
            break;
        }
        case WORLD_PICK_OBJSTACK:
        {
            /* Client-TS entityType 3: one pick lists every obj on the tile. */
            add_objs_on_tile(
                menu,
                sel,
                ctx->world,
                picked->tile_x,
                picked->tile_z,
                picked->tile_level);
            break;
        }
        case WORLD_PICK_TERRAIN:    /* Walk here only (above). */
        case WORLD_PICK_PROJECTILE: /* Not clickable (v1 parity). */
        case WORLD_PICK_WEV:        /* Hull op rows only (above). */
            break;
        }
    }
}
