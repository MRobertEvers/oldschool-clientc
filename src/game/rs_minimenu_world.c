#include "rs_minimenu_world.h"

#include "rs_minimenu_build.h"

#include "world/entity_player.h"
#include "world/world.h"
#include "world/world_pickset.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

static void
add_npc_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct WorldEntity_NPC const* npc,
    struct World_Picked const* picked,
    int viewer_combat_level)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char tooltip[UITREE_MINIMENU_OPTION_LEN];
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_NPC,
        .id = picked->element_id,
        .secondary_id = npc->npc_id,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
    };

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
            menu, sel, pick, "@yel@ ", tooltip, 0x2, REVCONFIG_MINIMENU_USEHELD_ONNPC,
            REVCONFIG_MINIMENU_TGT_NPC) )
        return;

    /* Non-attack ops first, then attack, so attack draws below by insertion
     * order (v1 parity; the level-based deprioritize needs player stats the
     * client does not track yet, so attack stays normal priority). */
    for( int i = 4; i >= 0; i-- )
    {
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") == 0 )
            continue;
        snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);
        UIMinimenu_AddOption(menu, text, opnpc_action_for_slot(i), i, pick);
    }
    for( int i = 4; i >= 0; i-- )
    {
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") != 0 )
            continue;
        snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);
        UIMinimenu_AddOption(menu, text, opnpc_action_for_slot(i), i, pick);
    }

    snprintf(text, sizeof(text), "Examine @yel@ %s", tooltip);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPNPC6, 0, pick);
}

static void
add_scenery_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct WorldEntity_Scenery const* scenery,
    struct World_Picked const* picked)
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    char const* name = scenery->name[0] ? scenery->name : "Scenery";
    struct UIMinimenuPick pick = {
        .kind = UI_MINIMENU_PICK_SCENERY,
        .id = picked->element_id,
        .secondary_id = scenery->loc_id,
        .tertiary_id = picked->tile_x,
        .quaternary_id = picked->tile_z,
    };

    if( add_world_select_row(
            menu, sel, pick, "@cya@", name, 0x4, REVCONFIG_MINIMENU_USEHELD_ONLOC,
            REVCONFIG_MINIMENU_TGT_LOC) )
        return;

    for( int i = 4; i >= 0; i-- )
    {
        if( scenery->actions[i].name[0] == '\0' )
            continue;
        snprintf(text, sizeof(text), "%s @cya@ %s", scenery->actions[i].name, name);
        UIMinimenu_AddOption(menu, text, oploc_action_for_slot(i), i, pick);
    }

    snprintf(text, sizeof(text), "Examine @cya@ %s", name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPLOC6, 0, pick);
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
            menu, sel, pick, "@lre@ ", name, 0x1, REVCONFIG_MINIMENU_USEHELD_ONOBJ,
            REVCONFIG_MINIMENU_TGT_OBJ) )
        return;

    for( int i = 4; i >= 0; i-- )
    {
        if( stack->actions[i].name[0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s @lre@ %s", stack->actions[i].name, name);
            UIMinimenu_AddOption(menu, text, opobj_action_for_slot(i), i, pick);
        }
        else if( i == 2 )
        {
            snprintf(text, sizeof(text), "Take @lre@ %s", name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPOBJ3, 2, pick);
        }
    }

    snprintf(text, sizeof(text), "Examine @lre@ %s", name);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPOBJ6, 0, pick);
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

static void
add_npc_stack_rows(
    struct UIMinimenu* menu,
    struct RS_MinimenuSelection const* sel,
    struct World* world,
    struct WorldEntity_NPC const* npc,
    struct World_Picked const* picked)
{
    int viewer_combat_level = world_local_combat_level(world);

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
            add_npc_rows(menu, sel, other, &other_picked, viewer_combat_level);
        }
    }

    add_npc_rows(menu, sel, npc, picked, viewer_combat_level);
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
            };
            UIMinimenu_AddOption(menu, "Walk here", REVCONFIG_MINIMENU_WALK, 0, pick);
        }
        else
        {
            UIMinimenu_AddOption(
                menu,
                "Walk here",
                REVCONFIG_MINIMENU_WALK,
                0,
                (struct UIMinimenuPick){ .kind = UI_MINIMENU_PICK_NONE });
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
                add_npc_stack_rows(menu, sel, ctx->world, npc, picked);
            break;
        }
        case WORLD_PICK_SCENERY:
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(ctx->world, picked->element_id);
            if( scenery )
                add_scenery_rows(menu, sel, scenery, picked);
            break;
        }
        case WORLD_PICK_OBJSTACK:
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(ctx->world, picked->element_id);
            if( stack )
                add_obj_rows(menu, sel, stack, picked);
            break;
        }
        case WORLD_PICK_TERRAIN:    /* Walk here only (above). */
        case WORLD_PICK_PROJECTILE: /* Not clickable (v1 parity). */
            break;
        }
    }
}
