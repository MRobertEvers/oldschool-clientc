#ifndef COMPONENT_LOAD_COMMON_H
#define COMPONENT_LOAD_COMMON_H

#include "toriauxlib/core/tasks/component_load_types.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"

struct ToriAuxLibCore_Sprite;

struct RSComponentWalkStack
{
    int stack[RS_COMPONENT_STACK_MAX];
    int stack_x[RS_COMPONENT_STACK_MAX];
    int stack_y[RS_COMPONENT_STACK_MAX];
    int stack_parent_id[RS_COMPONENT_STACK_MAX];
    int stack_count;
};

struct RSComponentIdList
{
    int* ids;
    int count;
};

bool
rs_component_owner_is_critical(char const* owner);

void
rs_component_stack_push(
    struct RSComponentWalkStack* stk,
    int id,
    int parent_w,
    int parent_h,
    int parent_id);

bool
rs_component_stack_pop(
    struct RSComponentWalkStack* stk,
    int* out_id,
    int* out_parent_w,
    int* out_parent_h,
    int* out_parent_id);

void
rs_component_id_list_reset(struct RSComponentIdList* list);

bool
rs_component_id_list_add_unique(struct RSComponentIdList* list, int id);

void
rs_component_id_list_free(struct RSComponentIdList* list);

int
inv_load_model_id_add_unique(
    int* model_ids,
    int* model_count,
    int capacity,
    int model_id);

void
inv_load_ensure_objtypes(
    struct ToriAuxLibCache* cache,
    int const* obj_ids,
    int count);

int
inv_load_register_obj_sprite(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCore_Sprite* sprite);

#endif
