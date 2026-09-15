#include "uitree_builder.h"
#include "uitree_builder_bake.h"
#include "uitree_builder_manifest.h"

#include "task_static_sprites_load.h"

#include "engine/task_obj_model_load.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Same bound as the interface-open path: the two seeded containers (worn +
 * backpack) hold well under this many distinct objs at build time. */
#define UITREE_BUILD_SEED_OBJ_MAX 32

/* Containers whose contents exist before any script runs, so their icons have
 * to be rasterized as part of the build rather than by an inv-transmit hook. */
static int const k_seed_containers[] = {
    INV_MANAGER_CONTAINER_WORN,
    INV_MANAGER_CONTAINER_BACKPACK,
};

struct Task_UITreeBuild
{
    struct ToriRS_Task task;
    struct pt pt;

    struct UITreeBuilder* builder;
    struct UIBuilderManifest manifest;
    int i;
    int script_id;
    int seed_obj_ids[UITREE_BUILD_SEED_OBJ_MAX];
    int seed_obj_count;
};

static void
collect_seed_objs(struct Task_UITreeBuild* self)
{
    assert(self && self->builder && self->builder->invs);
    self->seed_obj_count = 0;

    for( size_t ci = 0; ci < sizeof(k_seed_containers) / sizeof(k_seed_containers[0]); ci++ )
    {
        struct InvContainer const* c =
            InvManager_FindContainer(self->builder->invs, k_seed_containers[ci]);
        if( !c || !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            int oid = c->slots[s].obj_id;
            int found = 0;
            if( oid <= INV_MANAGER_EMPTY_OBJ_ID )
                continue;
            for( int j = 0; j < self->seed_obj_count; j++ )
            {
                if( self->seed_obj_ids[j] == oid )
                {
                    found = 1;
                    break;
                }
            }
            if( found )
                continue;
            assert(self->seed_obj_count < UITREE_BUILD_SEED_OBJ_MAX);
            self->seed_obj_ids[self->seed_obj_count++] = oid;
        }
    }
}

/* Bind a rasterized icon to every seeded slot. Runs after ObjModelLoad, which
 * is what put the inventory models in the provider for EnsureObjIcon to find. */
static void
bind_seed_obj_icons(struct Task_UITreeBuild* self)
{
    assert(self && self->builder);
    if( !self->builder->bridge )
        return;
    for( size_t ci = 0; ci < sizeof(k_seed_containers) / sizeof(k_seed_containers[0]); ci++ )
    {
        struct InvContainer* c =
            InvManager_GetContainer(self->builder->invs, k_seed_containers[ci]);
        if( !c )
            continue;
        for( int slot = 0; slot < c->slot_count; slot++ )
        {
            int oid = c->slots[slot].obj_id;
            int count = c->slots[slot].obj_count;
            int scene_id;
            if( oid <= 0 )
                continue;
            scene_id =
                UITreeSceneBridge_EnsureObjIcon(self->builder->bridge, oid, count > 0 ? count : 1);
            if( scene_id >= 0 )
            {
                c->slots[slot].scene_id = scene_id;
                c->slots[slot].atlas_index = 0;
            }
        }
    }
}

static int
Task_UITreeBuild_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_UITreeBuild* self = (struct Task_UITreeBuild*)base;
    assert(self->builder);
    assert(self->builder->tree);
    assert(self->builder->invs);

    PT_BEGIN(&self->pt);

    /* 1. Parse every RevConfig source the manifest named into one manifest
     * (local files, no cache IO). A manifest may name none of them and only set
     * an interface: from_sources then synthesises the single rs_iface mount that
     * the old open-the-interface-as-the-root path produced. */
    {
        struct UIBuilderManifestSources src = { 0 };
        src.ui_ini_path = self->builder->ini_path[0] ? self->builder->ini_path : NULL;
        src.cache_ini_path =
            self->builder->cache_ini_path[0] ? self->builder->cache_ini_path : NULL;
        src.inline_ini_path =
            self->builder->inline_ini_path[0] ? self->builder->inline_ini_path : NULL;
        src.root_interface_id = self->builder->root_interface_id;
        src.layout_group =
            self->builder->layout_group[0] ? self->builder->layout_group : NULL;
        src.layout_group_exclude =
            self->builder->layout_group_exclude[0] ? self->builder->layout_group_exclude : NULL;
        uibuilder_manifest_from_sources(&self->manifest, &src);
    }

    /* 2. Load every cache asset the manifest needs. */
    PT_TASK_AWAITSELF(CreateTask_UIBuilderAssetsLoad(self->builder, &self->manifest));

    /* 2b. Host static sprite packs (cross fallback, hitmarks, map dots, …).
     * Configured nodes prefer their own sprite= binding; bridge slots cover
     * overlays and compatibility profiles with no explicit binding. */
    PT_TASK_AWAITSELF_IF(
        CreateTask_StaticSpritesLoad(self->builder->provider, self->builder->bridge));

    /* 2c. Inventory models for objs the containers already hold, then their
     * icons. Without this the worn/backpack grids mount with empty slots until
     * the first inv transmit — the interface-open path did this and the
     * config-driven one has to as well. */
    collect_seed_objs(self);
    PT_TASK_AWAITSELF_IF(CreateTask_ObjModelLoad(
        self->builder->provider, self->seed_obj_ids, NULL, self->seed_obj_count));
    bind_seed_obj_icons(self);

    /* 3. Bake the tree (synchronous; provider is fully populated). */
    uitree_builder_bake(
        self->builder->tree, self->builder, &self->manifest, self->builder->invs);
    UITree_LayoutResolve(
        self->builder->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    /* 4. Run IF3 on_load hooks collected during bake. */
    if( self->builder->host )
    {
        for( self->i = 0; self->i < self->builder->onload_count; self->i++ )
        {
            struct UIBuilderOnLoadEntry const* hook = &self->builder->onloads[self->i];
            int const* args;
            int arg_count;

            self->script_id = hook->script_id;
            if( self->script_id <= 0 )
                continue;

            /* on_load.argv[0] is the script id; remaining entries are frame locals. */
            if( hook->argc > 1 )
            {
                args = hook->argv + 1;
                arg_count = hook->argc - 1;
            }
            else
            {
                args = NULL;
                arg_count = 0;
            }

            {
                /* argv[0] is the script id (always int) — args drop it, shift mask. */
                char const* strp[UITREE_BUILDER_ONLOAD_STR_MAX];
                for( int si = 0; si < UITREE_BUILDER_ONLOAD_STR_MAX; si++ )
                    strp[si] = hook->strv[si];
                PT_TASK_AWAITSELF_IF(CreateTask_CS2RunMixed(
                    self->builder->host,
                    self->script_id,
                    hook->component_id,
                    hook->component_id,
                    args,
                    arg_count,
                    hook->str_mask >> 1,
                    strp,
                    hook->str_argc));
            }
        }

        /* 4b. Relayout before the transmit dispatch, not after. The on_load
         * scripts resize and reposition; the transmit hooks then read that
         * geometry back (the world map sizes its view from the resolved box),
         * so dispatching against stale boxes paints the wrong thing. */
        UITree_LayoutResolve(
            self->builder->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

        /* 5. Dispatch initial inv- and var-transmit hooks registered by on_load.
         * The var ones paint the stat/quest/settings panels: without them those
         * tabs mount blank until something happens to change a varp. */
        PT_TASK_AWAITSELF_IF(CreateTask_CS2InvTransmitDispatch(self->builder->host, -1));
        PT_TASK_AWAITSELF_IF(CreateTask_CS2VarTransmitDispatch(self->builder->host, -1));
    }

    /* 6. Player-preview components idle with the player readyanim, not whatever
     * sequence an onLoad script set (TS parity — see uitree_builder_bake.h). */
    uitree_builder_reassert_player_idle_anim(self->builder->tree, self->builder->bridge);

    /* 7. Final layout: the onLoad scripts above resize and reposition nodes. */
    UITree_LayoutResolve(
        self->builder->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    /* 8. Hide packs the CS2 runtime baked at the root ahead of a mount that
     * never came. Every mount this build declared is parented under its
     * rs_iface owner, so there is no opening group or mount target to spare —
     * anything still loose at the root is spillover by definition. */
    uitree_builder_hide_unmounted_spillover(self->builder->tree, -1, -1);

    PT_END(&self->pt);
}

static void
Task_UITreeBuild_Free(struct ToriRS_Task* base)
{
    struct Task_UITreeBuild* self = (struct Task_UITreeBuild*)base;
    uibuilder_manifest_free(&self->manifest);
    free(self);
}

static struct ToriRS_TaskVTable Task_UITreeBuild_VTable = {
    .run = Task_UITreeBuild_Run,
    .free = Task_UITreeBuild_Free,
};

struct ToriRS_Task*
CreateTask_UITreeBuild(struct UITreeBuilder* builder)
{
    assert(builder);

    struct Task_UITreeBuild* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_UITreeBuild_VTable;
    strncpy(task->task.name, "UITreeBuild", sizeof(task->task.name) - 1);
    task->builder = builder;
    uibuilder_manifest_init(&task->manifest);
    PT_INIT(&task->pt);
    return &task->task;
}
