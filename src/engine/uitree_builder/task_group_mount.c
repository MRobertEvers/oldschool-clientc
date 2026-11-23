/*
 * Task_GroupMount: an interface group's ComponentPack, what its bake reads, and the
 * bake. @see task_group_mount.h.
 */
#include "task_group_mount.h"

#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "mem/torirs_mem.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_GroupMount
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_CS2Host* host;
    int group;
    int mount_parent_id;
    /* The loads asked of the assets table and not yet ended. */
    int join;
};

static void
group_mount_ask(
    struct Task_GroupMount* self,
    struct AssetKey key)
{
    (void)Assets_Await(self->host->provider->assets, key, &self->join);
}

static void
component_pack_ask(
    struct CacheProvider* provider,
    struct AssetKey key,
    int* join)
{
    (void)Assets_Await(provider->assets, key, join);
}

void
GroupMount_AskComponentPackParts(
    struct CacheProvider* provider,
    int group,
    int* join)
{
    struct ToriRS_ComponentPack* component_pack;

    assert(provider);
    assert(provider->assets);
    assert(join);
    component_pack = CacheProvider_ComponentPackGet(provider, group);
    assert(component_pack);
    for( int i = 0; i < component_pack->component_count; i++ )
    {
        struct ToriRS_Component const* c = &component_pack->components[i];
        if( c->graphic >= 0 )
            component_pack_ask(provider, AssetKey_Make(ASSET_KIND_SPRITE, c->graphic, 0), join);
        if( c->graphic_active >= 0 )
            component_pack_ask(
                provider, AssetKey_Make(ASSET_KIND_SPRITE, c->graphic_active, 0), join);
        for( int slot = 0; c->inv_slots && slot < TORIRS_INV_SLOT_MAX; slot++ )
            if( c->inv_slots->graphic_id[slot] > 0 )
                component_pack_ask(
                    provider,
                    AssetKey_Make(ASSET_KIND_SPRITE, c->inv_slots->graphic_id[slot], 0),
                    join);
        if( c->font_id >= 0 )
            component_pack_ask(
                provider, AssetKey_Make(ASSET_KIND_CONFIG, c->font_id, ASSET_CONFIG_FONT), join);
        if( c->model_type == 2 && c->model_id >= 0 )
            component_pack_ask(
                provider, AssetKey_Make(ASSET_KIND_CONFIG, c->model_id, ASSET_CONFIG_NPC), join);
        if( c->active_model_type == 2 && c->active_model_id >= 0 )
            component_pack_ask(
                provider,
                AssetKey_Make(ASSET_KIND_CONFIG, c->active_model_id, ASSET_CONFIG_NPC),
                join);
    }
}

void
GroupMount_AskComponentPackModels(
    struct CacheProvider* provider,
    int group,
    int* join)
{
    struct ToriRS_ComponentPack* component_pack;

    assert(provider);
    assert(provider->assets);
    assert(join);
    component_pack = CacheProvider_ComponentPackGet(provider, group);
    assert(component_pack);
    for( int i = 0; i < component_pack->component_count; i++ )
    {
        struct ToriRS_Component const* c = &component_pack->components[i];
        if( c->model_type == 1 && c->model_id >= 0 )
            component_pack_ask(provider, AssetKey_Make(ASSET_KIND_MODEL, c->model_id, 0), join);
        if( c->active_model_type == 1 && c->active_model_id >= 0 )
            component_pack_ask(
                provider, AssetKey_Make(ASSET_KIND_MODEL, c->active_model_id, 0), join);
    }
}

static int
group_mount_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;

    assert(bridge);
    if( graphic_id < 0 )
        return -1;
    /* Bound, not ensured: a sprite whose read came back empty is filled when
     * it lands (UITreeSceneBridge_BindSprite) instead of blank for good. */
    return UITreeSceneBridge_BindSprite(bridge, graphic_id);
}

static int
group_mount_resolve_font(
    void* ud,
    int font_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;

    assert(bridge);
    if( font_id < 0 )
        return -1;
    return UITreeSceneBridge_EnsureFont(bridge, font_id);
}

/*
 * The bake of a group not yet in the tree.
 *
 * A ComponentPack nobody mounted is not on screen yet, so it must not be drawn or
 * measured: unless it went under a parent of another group, its root is
 * hidden until an open clears it. Baked at the tree root and visible, a
 * speculatively baked panel is laid out against the whole canvas, and the
 * fixed-mode canvas is sized from the widest right-docked full-height column
 * in the tree (App_MeasureRightChromeStripWidth): the login arming of XP
 * tracker 729 baked a 264-wide right-docked column, so fixed mode grew the
 * canvas to 765+264 and left a 222px black band beside the frame.
 *
 * mount_hidden, not a native hide: it is the marker the spillover sweep and
 * a replacing mount use, and what an open knows how to undo.
 */
static void
group_mount_bake(struct Task_GroupMount* self)
{
    struct UITree* tree = self->host->tree;
    struct UITreeSceneBridge* bridge = self->host->bridge;
    struct ToriRS_ComponentPack* component_pack =
        CacheProvider_ComponentPackGet(self->host->provider, self->group);
    int32_t root_idx;
    int const parent_group = (self->mount_parent_id >> 16) & 0xffff;

    assert(component_pack);
    (void)UITree_BuildFromComponentPack(
        tree, component_pack, group_mount_resolve_sprite, group_mount_resolve_font, bridge);

    root_idx = UITree_FindByComponentId(tree, self->group << 16);
    if( root_idx >= 0 && self->mount_parent_id > 0 && parent_group > 0 &&
        parent_group != self->group )
    {
        int32_t parent_idx = UITree_FindByComponentId(tree, self->mount_parent_id);
        if( parent_idx >= 0 )
            UITree_Reparent(tree, root_idx, parent_idx);
    }
    if( root_idx >= 0 && tree->components[root_idx].parent < 0 &&
        !UITree_InterfaceParentIsMountedGroup(tree, self->group) )
        (void)UITree_SetMountHiddenAt(tree, root_idx, 1);

    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

static int
Task_GroupMount_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IOBatch* io)
{
    struct Task_GroupMount* self = (struct Task_GroupMount*)task;

    (void)io;
    PT_BEGIN(&self->pt);
    group_mount_ask(self, AssetKey_Make(ASSET_KIND_IF_GROUP, self->group, 0));
    TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);
    if( !CacheProvider_ComponentPackHas(self->host->provider, self->group) )
    {
        TORIRS_ERR("group mount: ComponentPack %d is not in the cache\n", self->group);
        PT_EXIT(&self->pt);
    }
    GroupMount_AskComponentPackParts(self->host->provider, self->group, &self->join);
    TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);

    /* Whoever asked has gone (its join detached): nobody reaches into the
     * group, so it is not put in the tree. */
    if( !self->task.join )
        PT_EXIT(&self->pt);
    if( UITree_GroupPresent(self->host->tree, self->group) )
        PT_EXIT(&self->pt);
    group_mount_bake(self);
    if( self->host->pack_models_lazy )
        self->host->pack_models_lazy(self->host->world_user, self->group);
    PT_END(&self->pt);
}

static void
Task_GroupMount_Free(struct ToriRS_Task* task)
{
    struct Task_GroupMount* self = (struct Task_GroupMount*)task;

    assert(self);
    /* Freed mid-wait (its queue freed whole): the table stops counting. */
    if( self->join > 0 )
        Assets_Unjoin(self->host->provider->assets, &self->join);
    ToriMem_Free(ToriMem_Ui(), self);
}

static struct ToriRS_TaskVTable Task_GroupMount_VTable = {
    .run = Task_GroupMount_Run,
    .free = Task_GroupMount_Free,
};

struct ToriRS_Task*
CreateTask_GroupMount(
    struct RS_CS2Host* host,
    int group,
    int mount_parent_id)
{
    struct Task_GroupMount* self;

    assert(host);
    assert(host->provider);
    assert(host->provider->assets);
    assert(host->tree);
    assert(group > 0);
    self = ToriMem_Calloc(ToriMem_Ui(), 1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_GroupMount_VTable;
    strncpy(self->task.name, "GroupMount", sizeof(self->task.name) - 1);
    self->host = host;
    self->group = group;
    self->mount_parent_id = mount_parent_id;
    PT_INIT(&self->pt);
    return &self->task;
}
