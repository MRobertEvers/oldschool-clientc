#include "rs_minimenu_world.h"

#include "rs_minimenu_build.h"

#include "world/world.h"
#include "world/world_pickset.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void
add_npc_rows(
    struct UIMinimenu* menu,
    struct WorldEntity_NPC const* npc,
    struct World_Picked const* picked)
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

    if( npc->combat_level > 0 )
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s (level-%d)",
            npc->name[0] ? npc->name : "NPC",
            npc->combat_level);
    else
        snprintf(tooltip, sizeof(tooltip), "%s", npc->name[0] ? npc->name : "NPC");

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
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPNPC1 + i, i, pick);
    }
    for( int i = 4; i >= 0; i-- )
    {
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") != 0 )
            continue;
        snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPNPC1 + i, i, pick);
    }

    snprintf(text, sizeof(text), "Examine @yel@ %s", tooltip);
    UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPNPC6, 0, pick);
}

static void
add_scenery_rows(
    struct UIMinimenu* menu,
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

    for( int i = 4; i >= 0; i-- )
    {
        if( scenery->actions[i].name[0] == '\0' )
            continue;
        snprintf(text, sizeof(text), "%s @cya@ %s", scenery->actions[i].name, name);
        UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPLOC1 + i, i, pick);
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

    for( int i = 4; i >= 0; i-- )
    {
        if( stack->actions[i].name[0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s @lre@ %s", stack->actions[i].name, name);
            UIMinimenu_AddOption(menu, text, REVCONFIG_MINIMENU_OPOBJ1 + i, i, pick);
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

void
RS_Minimenu_AddWorldRows(
    struct RS_MinimenuBuildCtx const* ctx,
    struct UIMinimenu* menu)
{
    struct World_PickSet const* picks;

    assert(ctx && menu);
    if( !ctx->click_in_world || !ctx->world || !ctx->world_pickset )
        return;
    picks = ctx->world_pickset;

    /* Walk here targets the nearest picked tile — the painter walks
     * back-to-front, so that is the LAST terrain item (matches the hover
     * tile the click cross and spawn hotkeys use). */
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
                add_npc_rows(menu, npc, picked);
            break;
        }
        case WORLD_PICK_SCENERY:
        {
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(ctx->world, picked->element_id);
            if( scenery )
                add_scenery_rows(menu, scenery, picked);
            break;
        }
        case WORLD_PICK_OBJSTACK:
        {
            struct WorldEntity_ObjStack* stack =
                World_ObjStackGetByElementId(ctx->world, picked->element_id);
            if( stack )
                add_obj_rows(menu, stack, picked);
            break;
        }
        case WORLD_PICK_TERRAIN:    /* Walk here only (above). */
        case WORLD_PICK_PROJECTILE: /* Not clickable (v1 parity). */
            break;
        }
    }
}
