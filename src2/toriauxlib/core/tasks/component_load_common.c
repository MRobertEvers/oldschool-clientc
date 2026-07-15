#include "toriauxlib/core/tasks/component_load_common.h"

#include "games/runescape.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool
rs_component_owner_is_critical(char const* owner)
{
    if( !owner || owner[0] == '\0' )
        return false;
    if( strncmp(owner, "sidebar_tab_", 12) == 0 )
        return true;
    if( strcmp(owner, "sidebar") == 0 )
        return true;
    if( strstr(owner, "inv") != NULL )
        return true;
    if( strstr(owner, "inventory") != NULL )
        return true;
    return false;
}

void
rs_component_stack_push(
    struct RSComponentWalkStack* stk,
    int id,
    int parent_w,
    int parent_h,
    int parent_id)
{
    assert(stk);
    if( stk->stack_count >= RS_COMPONENT_STACK_MAX )
        return;
    stk->stack[stk->stack_count] = id;
    stk->stack_x[stk->stack_count] = parent_w;
    stk->stack_y[stk->stack_count] = parent_h;
    stk->stack_parent_id[stk->stack_count] = parent_id;
    stk->stack_count++;
}

bool
rs_component_stack_pop(
    struct RSComponentWalkStack* stk,
    int* out_id,
    int* out_parent_w,
    int* out_parent_h,
    int* out_parent_id)
{
    assert(stk);
    if( stk->stack_count <= 0 )
        return false;
    stk->stack_count--;
    if( out_id )
        *out_id = stk->stack[stk->stack_count];
    if( out_parent_w )
        *out_parent_w = stk->stack_x[stk->stack_count];
    if( out_parent_h )
        *out_parent_h = stk->stack_y[stk->stack_count];
    if( out_parent_id )
        *out_parent_id = stk->stack_parent_id[stk->stack_count];
    return true;
}

void
rs_component_id_list_reset(struct RSComponentIdList* list)
{
    assert(list);
    list->count = 0;
    free(list->ids);
    list->ids = NULL;
}

bool
rs_component_id_list_add_unique(struct RSComponentIdList* list, int id)
{
    assert(list);
    assert(id >= 0);

    for( int i = 0; i < list->count; i++ )
    {
        if( list->ids[i] == id )
            return true;
    }

    int new_count = list->count + 1;
    int* grown = realloc(list->ids, (size_t)new_count * sizeof(int));
    if( !grown )
        return false;
    list->ids = grown;
    list->ids[list->count] = id;
    list->count = new_count;
    return true;
}

void
rs_component_id_list_free(struct RSComponentIdList* list)
{
    if( !list )
        return;
    free(list->ids);
    list->ids = NULL;
    list->count = 0;
}

int
inv_load_model_id_add_unique(
    int* model_ids,
    int* model_count,
    int capacity,
    int model_id)
{
    if( model_id < 0 || !model_ids || !model_count || *model_count >= capacity )
        return -1;

    for( int i = 0; i < *model_count; i++ )
    {
        if( model_ids[i] == model_id )
            return 0;
    }

    model_ids[(*model_count)++] = model_id;
    return 0;
}

void
inv_load_ensure_objtypes(
    struct ToriAuxLibCache* cache,
    int const* obj_ids,
    int count)
{
    if( !cache || !obj_ids || count <= 0 )
        return;

    for( int i = 0; i < count; i++ )
    {
        if( obj_ids[i] > 0 )
            ToriAuxLibCache_EnsureObjtype(cache, obj_ids[i]);
    }
}

int
inv_load_register_obj_sprite(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCore_Sprite* sprite)
{
    if( !rc_ctx || !rc_ctx->cache || !sprite || !rc_ctx->game || !rc_ctx->game->td )
        return -1;

    int element_id = rc_ctx->next_element_id++;
    ToriAuxLibCache_SubmitSprite(rc_ctx->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(rc_ctx->game->td, element_id);
    return element_id;
}
