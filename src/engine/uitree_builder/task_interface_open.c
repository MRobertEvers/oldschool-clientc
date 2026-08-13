#include "task_interface_open.h"

#include "task_static_sprites_load.h"

#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_component_hook.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "task_pack_assets_load.h"
#include "perf/torirs_perf.h"
#include "ui/uitree.h"
#include "ui/uitree_iface_stats.h"
#include "ui/uitree_layout.h"
#include "uitree_builder_bake.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Standard human idle sequence for the player preview (clientCode 327/328). */
#ifndef INTERFACE_PLAYER_IDLE_SEQ
#define INTERFACE_PLAYER_IDLE_SEQ UITREE_BUILDER_PLAYER_IDLE_SEQ
#endif

#define INTERFACE_OPEN_ONLOAD_ARGV_MAX TORIRS_COMPONENT_HOOK_ARG_MAX
#define INTERFACE_OPEN_ONLOAD_MAX 256
#define INTERFACE_OPEN_RUNTIME_HOOK_MAX 512
#define INTERFACE_OPEN_SEED_OBJ_MAX 32

struct InterfaceOpenOnLoad
{
    int component_id;
    int script_id;
    int argc;
    int argv[INTERFACE_OPEN_ONLOAD_ARGV_MAX];
    /* Positional string args (see ToriRS_ScriptHook): bit i of str_mask marks
     * argv[i] as a string; strings fill strv[] in position order. */
    uint64_t str_mask;
    int str_argc;
    char strv[TORIRS_COMPONENT_HOOK_STR_MAX][TORIRS_COMPONENT_HOOK_STR_LEN];
};

struct InterfaceOpenRuntimeHook
{
    int component_id;
    int script_id;
    int argc;
    int argv[UITREE_HOOK_ARG_MAX];
    uint64_t str_mask;
    int str_argc;
    char strv[UITREE_HOOK_STR_ARG_MAX][UITREE_HOOK_STR_ARG_LEN];
};

struct Task_InterfaceOpen
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    struct UITree* tree;
    struct RS_CS2Host* host;
    struct InvManager* invs;
    struct UITreeSceneBridge* bridge;
    int interface_id;
    int target_uid; /* -1 = root open */
    int mount_type;
    struct InterfaceOpenStats* stats;

    /*
     * Grown on demand rather than sized for the worst interface in the cache.
     * Inline, these two were `onloads[256]` + `runtime_hooks[512]` of ~600-byte
     * entries — 460 KB of the task's 556 KB, on every mount, when the gameframe
     * collects six onloads and no runtime hooks. Thirty-eight of these tasks
     * were live at peak: 21 MB. Push/At below; nothing indexes them directly.
     */
    struct InterfaceOpenOnLoad* onloads;
    int onload_count;
    int onload_cap;

    struct InterfaceOpenRuntimeHook* runtime_hooks;
    int runtime_hook_count;
    int runtime_hook_cap;

    int seed_obj_ids[INTERFACE_OPEN_SEED_OBJ_MAX];
    int seed_obj_count;

    int i;
    int script_id;
};

static int
open_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)ud;
    int scene_id;
    assert(self && self->bridge);
    if( graphic_id <= 0 )
        return -1;
    scene_id = UITreeSceneBridge_EnsureSprite(self->bridge, graphic_id);
    if( scene_id < 0 )
        fprintf(stderr, "InterfaceOpen: RS sprite %d not in scene\n", graphic_id);
    return scene_id;
}

static int
open_resolve_font(
    void* ud,
    int font_id)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)ud;
    assert(self && self->bridge);
    if( font_id < 0 )
        return -1;
    return UITreeSceneBridge_EnsureFont(self->bridge, font_id);
}

static void
upload_model_nodes(
    struct UITree* tree,
    struct UITreeSceneBridge* bridge)
{
    int mi;
    assert(tree && bridge);
    for( mi = 0; mi < tree->models.count; mi++ )
    {
        int32_t i = tree->models.slots[mi];
        struct UITreeComponent* c;
        int cache_id;
        int scene_id;
        assert(i >= 0 && (uint32_t)i < tree->component_count);
        c = &tree->components[i];
        if( c->type != UIELEM_RS_MODEL )
            continue;
        cache_id = c->u.rs_model.gamecache_model_id;
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "upload_model_nodes: com=0x%x cache_id=%d client_code=%d\n",
                (unsigned)c->component_id,
                cache_id,
                c->behavior.client_code);
        if( cache_id < 0 )
        {
            /* Local-player / player-design preview: composite the default avatar. */
            if( c->behavior.client_code == 327 || c->behavior.client_code == 328 )
            {
                scene_id = UITreeSceneBridge_EnsurePlayerModel(bridge);
                if( getenv("TORIRS_ANIM_DEBUG") )
                    fprintf(stderr, "  EnsurePlayerModel -> %d\n", scene_id);
                if( scene_id >= 0 )
                {
                    c->u.rs_model.gamecache_model_id = scene_id;
                    /* Pose the preview. The server appearance's readyanim is not
                     * decoded here, so use the standard human ready sequence; the
                     * tick driver loads it and disables gracefully if absent.
                     * Held at frame 0 — the reference poses the design composite
                     * once and only spins modelYAn after that. */
                    if( c->u.rs_model.anim_seq_id < 0 )
                        c->u.rs_model.anim_seq_id = INTERFACE_PLAYER_IDLE_SEQ;
                    c->u.rs_model.anim_frame = 0;
                    c->u.rs_model.anim_frame_cycle = 0;
                    c->u.rs_model.anim_hold = 1;
                }
            }
            continue;
        }
        scene_id = UITreeSceneBridge_EnsureModel(bridge, cache_id);
        if( scene_id >= 0 )
            c->u.rs_model.gamecache_model_id = scene_id;
    }
}

/*
 * The two collected lists. `Push` returns the new entry to fill (NULL if the
 * cap or the allocator says no); `At` is the checked read — the loops that
 * replay these run across protothread yields, so an index that has drifted past
 * the count is a real failure mode rather than a theoretical one.
 */
static struct InterfaceOpenOnLoad*
Task_InterfaceOpen_PushOnLoad(struct Task_InterfaceOpen* self)
{
    assert(self);
    if( self->onload_count >= self->onload_cap )
    {
        int cap = self->onload_cap ? self->onload_cap * 2 : 8;
        struct InterfaceOpenOnLoad* grown;
        if( cap > INTERFACE_OPEN_ONLOAD_MAX )
            cap = INTERFACE_OPEN_ONLOAD_MAX;
        if( self->onload_count >= cap )
            return NULL;
        grown = (struct InterfaceOpenOnLoad*)realloc(
            self->onloads, (size_t)cap * sizeof(*grown));
        if( !grown )
            return NULL;
        self->onloads = grown;
        self->onload_cap = cap;
    }
    return &self->onloads[self->onload_count++];
}

static struct InterfaceOpenOnLoad const*
Task_InterfaceOpen_OnLoadAt(
    struct Task_InterfaceOpen const* self,
    int index)
{
    assert(self);
    assert(index >= 0 && index < self->onload_count && "onload index out of range");
    return &self->onloads[index];
}

static struct InterfaceOpenRuntimeHook*
Task_InterfaceOpen_PushRuntimeHook(struct Task_InterfaceOpen* self)
{
    assert(self);
    if( self->runtime_hook_count >= self->runtime_hook_cap )
    {
        int cap = self->runtime_hook_cap ? self->runtime_hook_cap * 2 : 8;
        struct InterfaceOpenRuntimeHook* grown;
        if( cap > INTERFACE_OPEN_RUNTIME_HOOK_MAX )
            cap = INTERFACE_OPEN_RUNTIME_HOOK_MAX;
        if( self->runtime_hook_count >= cap )
            return NULL;
        grown = (struct InterfaceOpenRuntimeHook*)realloc(
            self->runtime_hooks, (size_t)cap * sizeof(*grown));
        if( !grown )
            return NULL;
        self->runtime_hooks = grown;
        self->runtime_hook_cap = cap;
    }
    return &self->runtime_hooks[self->runtime_hook_count++];
}

static struct InterfaceOpenRuntimeHook const*
Task_InterfaceOpen_RuntimeHookAt(
    struct Task_InterfaceOpen const* self,
    int index)
{
    assert(self);
    assert(index >= 0 && index < self->runtime_hook_count && "runtime hook index out of range");
    return &self->runtime_hooks[index];
}

static void
collect_onloads(
    struct Task_InterfaceOpen* self,
    struct ToriRS_ComponentPack const* pack)
{
    assert(self && pack);
    self->onload_count = 0;
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        struct ToriRS_ScriptHook const* on_load =
            ToriRS_ComponentHookPeek(src, TORIRS_COMPONENT_HOOK_LOAD);
        if( !on_load || on_load->argc <= 0 )
            continue;
        int script_id = on_load->argv[0];
        if( script_id <= 0 )
            continue;
        struct InterfaceOpenOnLoad* hook = Task_InterfaceOpen_PushOnLoad(self);
        if( !hook )
        {
            fprintf(stderr, "InterfaceOpen: onload list full at %d\n", self->onload_count);
            break;
        }
        hook->component_id = src->id;
        hook->script_id = script_id;
        hook->argc = on_load->argc;
        if( hook->argc > INTERFACE_OPEN_ONLOAD_ARGV_MAX )
        {
            fprintf(
                stderr,
                "InterfaceOpen: onload argc %d truncated to %d (component 0x%x)\n",
                hook->argc,
                INTERFACE_OPEN_ONLOAD_ARGV_MAX,
                (unsigned)src->id);
            hook->argc = INTERFACE_OPEN_ONLOAD_ARGV_MAX;
        }
        memcpy(hook->argv, on_load->argv, (size_t)hook->argc * sizeof(int));
        hook->str_mask = on_load->str_mask;
        hook->str_argc = on_load->str_argc;
        memcpy(hook->strv, on_load->strv, sizeof(hook->strv));
    }
}

/*
 * Arm the transmit hooks the cache records declare, for the group being opened.
 *
 * The twin of collect_onloads: an onload paints the panel once, and these are
 * what repaint it afterwards. Run for a reused group too — the registration is
 * keyed by component id and rewrites in place, so re-opening a panel re-arms it
 * rather than accumulating entries.
 */
static void
arm_cache_transmit_hooks(
    struct Task_InterfaceOpen* self,
    struct ToriRS_ComponentPack const* pack)
{
    assert(self && pack);
    if( !self->host )
        return;
    for( int i = 0; i < pack->component_count; i++ )
        RS_CS2_RegisterCacheTransmitHooks(self->host, &pack->components[i]);
}

static void
collect_seed_objs(struct Task_InterfaceOpen* self)
{
    assert(self && self->invs);
    self->seed_obj_count = 0;

    static int const containers[] = {
        INV_MANAGER_CONTAINER_WORN,
        INV_MANAGER_CONTAINER_BACKPACK,
    };
    for( size_t ci = 0; ci < sizeof(containers) / sizeof(containers[0]); ci++ )
    {
        struct InvContainer const* c = InvManager_FindContainer(self->invs, containers[ci]);
        if( !c || !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            int oid = c->slots[s].obj_id;
            if( oid <= INV_MANAGER_EMPTY_OBJ_ID )
                continue;
            int found = 0;
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
            assert(self->seed_obj_count < INTERFACE_OPEN_SEED_OBJ_MAX);
            self->seed_obj_ids[self->seed_obj_count++] = oid;
        }
    }
}

static void
layout_tree(struct Task_InterfaceOpen* self)
{
    /* Always full client canvas. Host size for openSub flows through the
     * mount parent's abs_w/h after reparent — do not shrink root_w/h or
     * toplevel + bank abs collapse to the top-left. */
    UITree_LayoutResolve(
        self->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

/* True when the tree already holds nodes of `group` — the pack was baked by an
 * earlier open or, more often, by the CS2 runtime (task_cs2_bake_pack) when a
 * script touched the group before the server opened it. */
static int
interface_group_in_tree(
    struct UITree const* tree,
    int group)
{
    return UITree_GroupPresent(tree, group);
}

/*
 * Move every node of the opened group under the mount target.
 *
 * The group's nodes are not necessarily tree roots: the CS2 runtime bakes a
 * pack the moment a script touches it, and the unmounted-spillover pass then
 * hides that copy — so by the time the server's IF_OPENSUB arrives the pack can
 * already be sitting in the tree (hidden, and holding every dynamic child the
 * script created). Scanning all components, rather than only the root sibling
 * list, catches that copy and any stale mount at a different slot, and clearing
 * hide_unmounted un-hides exactly what the mount bookkeeping hid.
 */
static void
mount_pack_under_target(struct Task_InterfaceOpen* self)
{
    int32_t mount_idx;
    struct UITreeNodeSet const* gset;
    int gi;
    assert(self->target_uid >= 0);
    mount_idx = UITree_FindByComponentId(self->tree, self->target_uid);
    assert(mount_idx >= 0 && "openSub target must exist");

    (void)UITree_InterfaceParentSet(
        self->tree, self->target_uid, self->interface_id, self->mount_type);

    gset = UITree_GroupNodes(self->tree, self->interface_id);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_IFACE_GROUP_SCAN_NODES, gset ? (int64_t)gset->count : 0);
    if( !gset )
        return;
    for( gi = 0; gi < gset->count; gi++ )
    {
        int32_t i = gset->slots[gi];
        struct UITreeComponent* c;
        int cid;
        assert(i >= 0 && (uint32_t)i < self->tree->component_count);
        c = &self->tree->components[i];
        cid = c->component_id;
        if( c->freed || cid < 0 )
            continue;
        /* Pack-internal node (its parent belongs to the same group): the bake
         * already linked it, and its ancestor carries the mount. */
        if( c->parent >= 0 && c->parent != mount_idx &&
            ((self->tree->components[c->parent].component_id >> 16) & 0xffff) ==
                self->interface_id )
            continue;
        if( c->behavior.hide_unmounted )
        {
            c->behavior.hide = 0;
            c->behavior.hide_unmounted = 0;
        }
        if( c->parent == mount_idx )
            continue;
        UITree_Reparent(self->tree, i, mount_idx);
    }
}

static void
collect_sub_change_hooks(struct Task_InterfaceOpen* self)
{
    struct UITreeNodeSet const* set;
    int si;
    self->runtime_hook_count = 0;
    set = &self->tree->sub_change_hooks;
    for( si = 0; si < set->count; si++ )
    {
        int32_t i = set->slots[si];
        struct UITreeComponent const* c;
        struct UITreeRuntimeHooks const* hooks;
        struct UITreeRuntimeScriptHook const* slot;
        struct InterfaceOpenRuntimeHook* dst;
        assert(i >= 0 && (uint32_t)i < self->tree->component_count);
        c = &self->tree->components[i];
        if( c->freed )
            continue;
        hooks = UITree_Hooks(c);
        slot = &hooks->on_sub_change;
        if( slot->script_id <= 0 )
            continue;
        if( getenv("TORIRS_ONSUBCHANGE_DEBUG") )
            fprintf(
                stderr,
                "onsubchange-collect: sub-iface=%d component 0x%08x (%d|%d) script=%d\n",
                self->interface_id,
                (unsigned)c->component_id,
                (c->component_id >> 16) & 0xffff,
                c->component_id & 0xffff,
                slot->script_id);
        dst = Task_InterfaceOpen_PushRuntimeHook(self);
        if( !dst )
        {
            fprintf(stderr, "InterfaceOpen: runtime-hook list full at %d\n",
                    self->runtime_hook_count);
            break;
        }
        dst->component_id = c->component_id;
        dst->script_id = slot->script_id;
        dst->argc = slot->argc;
        if( dst->argc > 32 )
            dst->argc = 32;
        /* Through the accessors: a slot's arguments are owned tails now, so the
         * old `memcpy(dst->strv, slot->strv, sizeof(dst->strv))` read 320 bytes
         * from what is a `char**` — NULL for the (common) hook with no string
         * arguments. */
        for( int ai = 0; ai < dst->argc; ai++ )
            dst->argv[ai] = UITree_HookArg(slot, ai);
        dst->str_mask = slot->str_mask;
        dst->str_argc =
            slot->str_argc > UITREE_HOOK_STR_ARG_MAX ? UITREE_HOOK_STR_ARG_MAX : slot->str_argc;
        memset(dst->strv, 0, sizeof(dst->strv));
        for( int si = 0; si < dst->str_argc; si++ )
            snprintf(dst->strv[si], UITREE_HOOK_STR_ARG_LEN, "%s", UITree_HookStr(slot, si));
    }
}

static int
Task_InterfaceOpen_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)base;
    assert(self->provider);
    assert(self->tree);
    assert(self->host);
    assert(self->invs);
    assert(self->bridge);
    assert(self->interface_id > 0);

    PT_BEGIN(&self->pt);

    /* 1. Load interface pack. A live server can open an interface id our local
     * cache does not carry; the load then no-ops and the pack stays absent, so
     * skip the mount gracefully instead of asserting (and crashing the client). */
    TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->interface_id));
    if( !CacheProvider_ComponentPackHas(self->provider, self->interface_id) )
    {
        fprintf(
            stderr,
            "interface open: pack %d missing from cache; skipping mount\n",
            self->interface_id);
        PT_EXIT(&self->pt);
    }

    /* 2. Prefetch pack sprites / fonts / models. */
    TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(self->provider, self->interface_id));

    /* 2b. Client-hardcoded sprites (scrollbar arrows, compass, cross, …). */
    TASK_AWAITSELF_IF(CreateTask_StaticSpritesLoad(self->provider, self->bridge));

    /* 3. Load seeded inv objs + inventory models, then rasterize icons. */
    collect_seed_objs(self);
    TASK_AWAITSELF_IF(
        CreateTask_ObjModelLoad(self->provider, self->seed_obj_ids, NULL, self->seed_obj_count));
    {
        static int const k_containers[] = {
            INV_MANAGER_CONTAINER_WORN,
            INV_MANAGER_CONTAINER_BACKPACK,
        };
        int ci;
        for( ci = 0; ci < (int)(sizeof(k_containers) / sizeof(k_containers[0])); ci++ )
        {
            struct InvContainer* c = InvManager_GetContainer(self->invs, k_containers[ci]);
            int slot;
            if( !c )
                continue;
            for( slot = 0; slot < c->slot_count; slot++ )
            {
                int oid = c->slots[slot].obj_id;
                int count = c->slots[slot].obj_count;
                int scene_id;
                if( oid <= 0 )
                    continue;
                scene_id =
                    UITreeSceneBridge_EnsureObjIcon(self->bridge, oid, count > 0 ? count : 1);
                if( scene_id >= 0 )
                {
                    c->slots[slot].scene_id = scene_id;
                    c->slots[slot].atlas_index = 0;
                }
            }
        }
    }

    /* 4. Bake pack into UITree + collect on_load hooks (scene ids via bridge).
     *
     * Bake only when the group is not in the tree yet. The CS2 runtime bakes a
     * pack as soon as a script touches it (task_cs2_bake_pack), which for the
     * gameframe happens well before the server's IF_OPENSUB, and every dynamic
     * child the scripts create (an inventory's 28 obj boxes, say) lives in that
     * copy — component lookups resolve an id to its lowest node index, so the
     * scripts keep writing there. Baking a second copy therefore mounts an
     * empty duplicate over the populated one and the panel renders blank. */
    {
        struct timespec mount_t0;
        uint64_t mount_ns;
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        assert(pack);
        clock_gettime(CLOCK_MONOTONIC, &mount_t0);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_OPEN, 1);
        UITreeIfaceStats_NoteOpen(self->interface_id);
        collect_onloads(self, pack);
        arm_cache_transmit_hooks(self, pack);
        if( interface_group_in_tree(self->tree, self->interface_id) )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_BAKE_REUSE, 1);
            UITreeIfaceStats_NoteBakeReuse(self->interface_id);
            if( getenv("TORIRS_NET_DEBUG") )
                fprintf(
                    stderr,
                    "interface open: group %d already baked; reusing it\n",
                    self->interface_id);
        }
        else
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_BAKE, 1);
            UITreeIfaceStats_NoteBake(self->interface_id);
            (void)UITree_BuildFromComponentPack(
                self->tree, pack, open_resolve_sprite, open_resolve_font, self);
        }
        upload_model_nodes(self->tree, self->bridge);
        {
            struct timespec mount_t1;
            clock_gettime(CLOCK_MONOTONIC, &mount_t1);
            mount_ns =
                (uint64_t)(mount_t1.tv_sec - mount_t0.tv_sec) * 1000000000ull +
                (uint64_t)(mount_t1.tv_nsec - mount_t0.tv_nsec);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_MOUNT_NS, (int64_t)mount_ns);
        }
    }

    if( self->target_uid >= 0 )
    {
        /* Close existing mount at target, then reparent. The outgoing group's
         * nodes are children of the mount target (that is what mounting did),
         * so hiding only tree roots would leave them on screen. */
        {
            int old = UITree_InterfaceParentFind(self->tree, self->target_uid);
            if( old >= 0 )
            {
                int old_group = self->tree->interface_parents[old].group_id;
                if( old_group != self->interface_id )
                {
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_IFACE_CLOSE, 1);
                    UITreeIfaceStats_NoteClose(old_group);
                    if( self->target_uid == UITREE_CHATBOX_CHATMODAL_UID )
                    {
                        /* Same reclaim path as IF_CLOSESUB for chatmodal:
                         * dialogue packs have no cc_create kids to reuse. */
                        UITree_ReclaimInterfaceGroup(self->tree, old_group);
                    }
                    else
                    {
                        for( uint32_t i = 0; i < self->tree->component_count; i++ )
                        {
                            struct UITreeComponent* c = &self->tree->components[i];
                            if( c->freed || c->component_id < 0 )
                                continue;
                            if( ((c->component_id >> 16) & 0xffff) != old_group )
                                continue;
                            if( c->parent >= 0 &&
                                ((self->tree->components[c->parent].component_id >> 16) &
                                 0xffff) == old_group )
                                continue;
                            if( !c->behavior.hide )
                                c->behavior.hide_unmounted = 1;
                            c->behavior.hide = 1;
                        }
                    }
                    RS_CS2Host_ClearHooksForInterfaceGroup(self->host, old_group);
                }
                UITree_InterfaceParentClear(self->tree, self->target_uid);
            }
        }
        mount_pack_under_target(self);
    }
    else
    {
        /* IF_OPENTOP: no mount target, so the group stays a tree root and
         * mount_pack_under_target never runs — but the same hide it undoes can
         * already be on this pack. The CS2 runtime hides a pack it baked
         * speculatively, and the spillover sweep hides a root nothing had
         * mounted yet; either one applied to a group that is now being opened
         * as the toplevel would render the whole gameframe blank, with nothing
         * to point at. Clearing hide_unmounted here is the opentop half of the
         * same bookkeeping. */
        struct UITreeNodeSet const* gset =
            UITree_GroupNodes(self->tree, self->interface_id);
        int gi;
        for( gi = 0; gset && gi < gset->count; gi++ )
        {
            int32_t idx = gset->slots[gi];
            struct UITreeComponent* c;
            assert(idx >= 0 && (uint32_t)idx < self->tree->component_count);
            c = &self->tree->components[idx];
            if( c->freed || c->component_id < 0 )
                continue;
            if( c->behavior.hide_unmounted )
            {
                c->behavior.hide = 0;
                c->behavior.hide_unmounted = 0;
            }
        }
    }

    /* 5. Layout at full client canvas (host size via parent abs after reparent). */
    layout_tree(self);

    /* 6. Run IF3 on_load hooks. */
    for( self->i = 0; self->i < self->onload_count; self->i++ )
    {
        struct InterfaceOpenOnLoad const* hook = Task_InterfaceOpen_OnLoadAt(self, self->i);
        int const* args;
        int arg_count;

        self->script_id = hook->script_id;
        if( self->script_id <= 0 )
            continue;

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
            char const* strp[TORIRS_COMPONENT_HOOK_STR_MAX];
            for( int si = 0; si < TORIRS_COMPONENT_HOOK_STR_MAX; si++ )
                strp[si] = hook->strv[si];
            TASK_AWAITSELF_IF(CreateTask_CS2RunMixed(
                self->host,
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

    /* 7. Re-layout after onLoad. */
    layout_tree(self);

    /* 8. Sub-only: notify the toplevel about the changed mount.
     *
     * Rev239's open-sub path lays out the mounted pack with resize-event
     * dispatch disabled, then queues onSubChange listeners. onResize is not an
     * interface-open lifecycle hook: layout queues it only when its resize
     * trigger flag is enabled and the component's resolved width or height
     * changed, while a script can request an initial callback itself through
     * IF_CALLONRESIZE.
     * Dispatching every listener registered by the newly opened group here
     * reruns those explicit initializers with host-relative coordinates and can
     * write the mounted host origin back as a child-local offset. */
    if( self->target_uid >= 0 )
    {
        collect_sub_change_hooks(self);
        for( self->i = 0; self->i < self->runtime_hook_count; self->i++ )
        {
            struct InterfaceOpenRuntimeHook const* hook =
                Task_InterfaceOpen_RuntimeHookAt(self, self->i);
            char const* strp[UITREE_HOOK_STR_ARG_MAX];
            for( int si = 0; si < UITREE_HOOK_STR_ARG_MAX; si++ )
                strp[si] = hook->strv[si];
            TASK_AWAITSELF_IF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->argc > 0 ? hook->argv : NULL,
                hook->argc,
                hook->str_mask,
                strp,
                hook->str_argc));
        }
        layout_tree(self);
    }

    /* 9. Inv + var transmit hooks registered during onLoad/onSubChange. */
    TASK_AWAITSELF_IF(CreateTask_CS2InvTransmitDispatch(self->host, -1));
    TASK_AWAITSELF_IF(CreateTask_CS2VarTransmitDispatch(self->host, -1));

    /* 9b. Player-preview components idle with the player readyanim, not whatever
     * sequence an onLoad script set (TS parity — see uitree_builder_bake.h). */
    uitree_builder_reassert_player_idle_anim(self->tree);

    /* 10. Final layout. */
    layout_tree(self);

    /* Spillover sibling roots (not InterfaceParent-mounted) stay hidden. */
    uitree_builder_hide_unmounted_spillover(
        self->tree, self->interface_id, self->target_uid);

    if( self->stats )
    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        self->stats->interface_id = self->interface_id;
        self->stats->pack_component_count = pack ? pack->component_count : 0;
        self->stats->onload_count = self->onload_count;
        self->stats->tree_component_count = self->tree->component_count;
    }

    PT_END(&self->pt);
}

static void
Task_InterfaceOpen_Free(struct ToriRS_Task* base)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)base;
    if( self )
    {
        free(self->onloads);
        free(self->runtime_hooks);
    }
    free(base);
}

static struct ToriRS_TaskVTable Task_InterfaceOpen_VTable = {
    .run = Task_InterfaceOpen_Run,
    .free = Task_InterfaceOpen_Free,
};

static struct ToriRS_Task*
create_interface_open_common(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    int target_uid,
    int mount_type,
    struct InterfaceOpenStats* stats,
    char const* name)
{
    struct Task_InterfaceOpen* task;

    assert(provider);
    assert(tree);
    assert(host);
    assert(invs);
    assert(bridge);
    assert(interface_id > 0);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InterfaceOpen_VTable;
    strncpy(task->task.name, name, sizeof(task->task.name) - 1);
    task->provider = provider;
    task->tree = tree;
    task->host = host;
    task->invs = invs;
    task->bridge = bridge;
    task->interface_id = interface_id;
    task->target_uid = target_uid;
    task->mount_type = mount_type;
    task->stats = stats;
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    struct InterfaceOpenStats* stats)
{
    return create_interface_open_common(
        provider, tree, host, invs, bridge, interface_id, -1, 0, stats, "InterfaceOpen");
}

struct ToriRS_Task*
CreateTask_InterfaceOpenSub(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int target_uid,
    int interface_id,
    int type,
    struct InterfaceOpenStats* stats)
{
    assert(target_uid >= 0);
    return create_interface_open_common(
        provider,
        tree,
        host,
        invs,
        bridge,
        interface_id,
        target_uid,
        type,
        stats,
        "InterfaceOpenSub");
}
