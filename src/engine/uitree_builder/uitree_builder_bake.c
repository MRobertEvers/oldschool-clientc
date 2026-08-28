#include "uitree_builder_bake.h"

#include "uitree_builder.h"
#include "uitree_builder_inv.h"
#include "uitree_builder_manifest.h"

#include "engine/cache_provider.h"
#include "engine/torirs_component_hook.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "game/rs_title.h"
#include "input/torirs_keymap.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_role.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

static enum UITreeComponentType
type_from_string(char const* type)
{
    assert(type);
    if( strcmp(type, "compass") == 0 )
        return UIELEM_BUILTIN_COMPASS;
    if( strcmp(type, "cross") == 0 )
        return UIELEM_BUILTIN_CROSS;
    if( strcmp(type, "entity_overlay") == 0 )
        return UIELEM_BUILTIN_ENTITY_OVERLAY;
    if( strcmp(type, "hovertext") == 0 )
        return UIELEM_BUILTIN_HOVERTEXT;
    if( strcmp(type, "multiway") == 0 )
        return UIELEM_BUILTIN_MULTIWAY;
    if( strcmp(type, "reboot_timer") == 0 )
        return UIELEM_BUILTIN_REBOOT_TIMER;
    if( strcmp(type, "login_input") == 0 )
        return UIELEM_BUILTIN_LOGIN_INPUT;
    if( strcmp(type, "login_button") == 0 )
        return UIELEM_BUILTIN_LOGIN_BUTTON;
    if( strcmp(type, "login_toggle") == 0 )
        return UIELEM_BUILTIN_LOGIN_TOGGLE;
    if( strcmp(type, "login_message") == 0 )
        return UIELEM_BUILTIN_LOGIN_MESSAGE;
    if( strcmp(type, "title_progress") == 0 )
        return UIELEM_BUILTIN_TITLE_PROGRESS;
    if( strcmp(type, "title_progress_text") == 0 )
        return UIELEM_BUILTIN_TITLE_PROGRESS_TEXT;
    if( strcmp(type, "title_flames") == 0 )
        return UIELEM_BUILTIN_TITLE_FLAMES;
    if( strcmp(type, "minimenu") == 0 )
        return UIELEM_BUILTIN_MINIMENU;
    if( strcmp(type, "minimap") == 0 )
        return UIELEM_BUILTIN_MINIMAP;
    if( strcmp(type, "world") == 0 )
        return UIELEM_BUILTIN_WORLD;
    if( strcmp(type, "sidebar") == 0 )
        return UIELEM_BUILTIN_SIDEBAR;
    if( strcmp(type, "chat") == 0 )
        return UIELEM_BUILTIN_CHAT;
    if( strcmp(type, "chat_button") == 0 )
        return UIELEM_BUILTIN_CHAT_BUTTON;
    if( strcmp(type, "sprite") == 0 )
        return UIELEM_BUILTIN_SPRITE;
    if( strcmp(type, "redstone_tab") == 0 )
        return UIELEM_BUILTIN_REDSTONE_TAB;
    if( strcmp(type, "builtin_tab_icons") == 0 || strcmp(type, "tab_icon") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    if( strcmp(type, "debug_overlay") == 0 )
        return UIELEM_BUILTIN_DEBUG_OVERLAY;
    /*
     * `rs_iface` is a mount point, not a widget: the node itself draws nothing
     * and the cache pack is baked underneath it (bake_rs_subtree_for_op). A
     * plain container layer is exactly that — uitree_emit skips a layer with no
     * if3 and no scroll extents, and it does not eat clicks.
     */
    if( strcmp(type, "rs_iface") == 0 )
        return UIELEM_RS_LAYER;
    if( strcmp(type, "rs_model") == 0 )
        return UIELEM_RS_MODEL;
    if( strcmp(type, "rs_inv") == 0 )
        return UIELEM_RS_INV;
    if( strcmp(type, "rs_line") == 0 )
        return UIELEM_RS_LINE;
    if( strcmp(type, "rs_graphic") == 0 )
        return UIELEM_RS_GRAPHIC;
    if( strcmp(type, "rs_layer") == 0 )
        return UIELEM_RS_LAYER;
    if( strcmp(type, "rs_text") == 0 )
        return UIELEM_RS_TEXT;
    if( strcmp(type, "rs_rect") == 0 )
        return UIELEM_RS_RECT;
    return UIELEM_BUILTIN_SPRITE;
}

/*
 * A component's font: the revconfig `font=` name when it has one, else the
 * dat2 archive id the cache carried. -1 when neither resolves, which every
 * caller treats as a missing asset rather than a default.
 */
static enum UITreeSlotTag
slot_tag_from_string(char const* slot)
{
    assert(slot);
    if( !slot[0] )
        return UITREE_SLOT_NONE;
    if( strcmp(slot, "main_modal") == 0 )
        return UITREE_SLOT_MAIN_MODAL;
    if( strcmp(slot, "main_overlay") == 0 )
        return UITREE_SLOT_MAIN_OVERLAY;
    if( strcmp(slot, "side_modal") == 0 )
        return UITREE_SLOT_SIDE_MODAL;
    if( strcmp(slot, "chat") == 0 )
        return UITREE_SLOT_CHAT;
    if( strcmp(slot, "tut") == 0 )
        return UITREE_SLOT_TUT;
    TORIRS_ERR("revconfig: unknown slot tag '%s'\n", slot);
    return UITREE_SLOT_NONE;
}

static void
apply_layout_position(
    struct UIBuilderTreeOp const* op,
    struct UITreeElemPosition* pos)
{
    assert(op && pos);
    memset(pos, 0, sizeof(*pos));

    if( op->xalign_center )
    {
        /* Centred horizontally, `y` from the top. resolve_relative already
         * centres an axis carrying no edge flag, so this only has to say that
         * the row is relative and that the vertical edge is the top one. */
        pos->kind = UIPOS_RELATIVE;
        pos->relative_flags = UITREE_RELATIVE_FLAG_TOP;
        pos->top = op->y;
        pos->width = op->width;
        pos->height = op->height;
    }
    else if( op->left || op->right || op->top || op->bottom )
    {
        pos->kind = UIPOS_RELATIVE;
        pos->relative_flags = 0;
        if( op->left )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_LEFT;
        if( op->right )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_RIGHT;
        if( op->top )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_TOP;
        if( op->bottom )
            pos->relative_flags |= UITREE_RELATIVE_FLAG_BOTTOM;
        pos->left = op->left;
        pos->right = op->right;
        pos->top = op->top;
        pos->bottom = op->bottom;
        pos->width = op->width;
        pos->height = op->height;
    }
    else
    {
        pos->kind = UIPOS_XY;
        pos->x = op->x;
        pos->y = op->y;
        pos->width = op->width;
        pos->height = op->height;
    }
    pos->anchor_x = op->anchor_x;
    pos->anchor_y = op->anchor_y;
}

static void
copy_menu_options(
    struct UIBuilderTreeOp const* op,
    struct UITreeMenuOptions* dst)
{
    assert(op && dst);
    memset(dst, 0, sizeof(*dst));
    strncpy(dst->option, op->option, sizeof(dst->option) - 1);
    dst->option_action = op->option_action;
    /* The two sides do not agree on a slot count: the manifest op carries
     * REVCONFIG_MENU_OPTION_SLOTS (5), the runtime widget UITREE_MENU_OPTION_SLOTS
     * (10). Walk the *source's* count -- running to the destination's read five
     * entries off the end of op->ops, which GCC reports as
     * -Waggressive-loop-optimizations and is free to miscompile. The memset above
     * already leaves the surplus destination slots empty. */
    _Static_assert(
        REVCONFIG_MENU_OPTION_SLOTS <= UITREE_MENU_OPTION_SLOTS,
        "manifest menu ops must fit the runtime widget's slots");
    for( int i = 0; i < REVCONFIG_MENU_OPTION_SLOTS; i++ )
    {
        strncpy(dst->ops[i], op->ops[i], sizeof(dst->ops[i]) - 1);
        dst->op_actions[i] = op->op_actions[i];
    }
}

static struct UIBuilderSpriteEntry const*
find_sprite_entry(
    struct UITreeBuilder const* builder,
    char const* ref)
{
    assert(builder && ref);
    char name[UITREE_BUILDER_NAME_MAX];
    char const* bracket = strchr(ref, '[');
    if( bracket )
    {
        size_t nlen = (size_t)(bracket - ref);
        if( nlen >= sizeof(name) )
            nlen = sizeof(name) - 1;
        memcpy(name, ref, nlen);
        name[nlen] = '\0';
    }
    else
    {
        strncpy(name, ref, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    for( int i = 0; i < builder->sprite_count; i++ )
    {
        if( strcmp(builder->sprites[i].name, name) == 0 )
            return &builder->sprites[i];
    }
    return NULL;
}

/**
 * `chrome:<slot>` -> the baked chrome skin, or -1 when `ref` is not one.
 *
 * The other sprite source, and the reason it exists: everything in the
 * builder's own table is a CACHE sprite, declared by a `[sprite:...]` section
 * that names an archive. Art the CLIENT owns has no archive to name -- it is
 * compiled in (engine/torirs_chrome_skin_baked.h) precisely so that it draws
 * on a revision whose cache does not contain it, on a cache that failed to
 * open, and on a build shipped without one.
 *
 * A profile reaches it by SLOT NAME rather than by number, for the same reason
 * every other revconfig binding is by name: the bake order is spritebake's
 * argument order, and a re-bake that moved a slot would otherwise silently
 * draw a scrollbar arrow where a button should be.
 *
 * The whole skin is ONE scene entry with a frame per slot, so the answer is
 * that entry's id and the slot as the atlas index -- the same shape a
 * multi-frame cache sprite already has.
 */
static int
resolve_chrome_sprite(struct UITreeBuilder* builder, char const* ref, int* out_atlas)
{
    static char const* const SLOT_NAME[TORIRS_CHROME_SKIN_SLOT_COUNT] = {
        [TORIRS_CHROME_SKIN_BUTTON_LEFT] = "button_left",
        [TORIRS_CHROME_SKIN_BUTTON_MID] = "button_mid",
        [TORIRS_CHROME_SKIN_BUTTON_RIGHT] = "button_right",
        [TORIRS_CHROME_SKIN_CLOSE] = "close",
        [TORIRS_CHROME_SKIN_CLOSE_OVER] = "close_over",
        [TORIRS_CHROME_SKIN_PANEL_BODY] = "panel_body",
        [TORIRS_CHROME_SKIN_PLUGIN_ICON] = "plugin_icon",
    };
    char const* slot_name;
    int scene_id;

    assert(builder);
    assert(ref);

    if( strncmp(ref, "chrome:", 7) != 0 )
        return -1;
    slot_name = ref + 7;

    for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
    {
        if( !SLOT_NAME[slot] || strcmp(SLOT_NAME[slot], slot_name) != 0 )
            continue;
        if( !builder->bridge )
            return -1;
        scene_id = UITreeSceneBridge_EnsureChromeSkin(builder->bridge);
        /* -1 is a real answer: a lane can be built with the skin module
         * stubbed out, and a component that asked for one draws nothing rather
         * than the client refusing to boot. */
        if( scene_id < 0 )
            return -1;
        if( out_atlas )
            *out_atlas = slot;
        return scene_id;
    }

    /*
     * A name that is not a slot IS a mistake worth stopping on, unlike a
     * missing skin: the profile spelled something, and the only outcomes are
     * the right picture or a silently blank control.
     */
    TORIRS_LOG("uitree_builder_bake: no chrome skin slot named '%s'\n", slot_name);
    assert(0 && "unknown chrome: sprite slot");
    return -1;
}

/**
 * `chrome:<slot>` -> a baked chrome face, or -1 when `ref` is not one.
 *
 * The font twin of resolve_chrome_sprite, and it exists for the same reason: a
 * `[font:...]` section names a face in the CACHE, and a control the client
 * owns has to be readable on a cache that does not carry one. The three faces
 * are the ones the chrome already bakes -- 494, 495 and 496 in the OSRS fonts
 * table -- and `bold` is 496, which is the face the interfaces' own buttons
 * set their captions in.
 *
 * Pinned at 1x (EnsureDebugFont1x). These glyphs land in INTERFACE pixels,
 * which the gameframe lays out and the shell scales afterwards; the
 * chrome-scale accessor beside it would hand a 2x face to a 1x coordinate
 * system on any HighDPI display and the caption would come out double-sized.
 */
static int
resolve_chrome_font(struct UITreeBuilder* builder, char const* ref)
{
    static const struct
    {
        char const* name;
        int slot;
    } SLOT[] = {
        { "small", TORIRS_CHROME_FONT_SMALL },
        { "body", TORIRS_CHROME_FONT_BODY },
        /* The bold one. Named for what an author is choosing rather than for
         * the chrome's own use of it -- `menu` says where it is spent, `bold`
         * says what it looks like, and a profile is picking a face. */
        { "bold", TORIRS_CHROME_FONT_MENU },
    };
    char const* slot_name;

    assert(builder);
    assert(ref);

    if( strncmp(ref, "chrome:", 7) != 0 )
        return -1;
    slot_name = ref + 7;

    for( size_t i = 0; i < sizeof(SLOT) / sizeof(SLOT[0]); i++ )
    {
        if( strcmp(SLOT[i].name, slot_name) != 0 )
            continue;
        if( !builder->bridge )
            return -1;
        return UITreeSceneBridge_EnsureDebugFont1x(builder->bridge, SLOT[i].slot);
    }

    TORIRS_LOG("uitree_builder_bake: no chrome font named '%s'\n", slot_name);
    assert(0 && "unknown chrome: font slot");
    return -1;
}

static int
resolve_sprite_required(
    struct UITreeBuilder* builder,
    char const* ref,
    int* out_atlas)
{
    if( !ref || ref[0] == '\0' )
    {
        if( out_atlas )
            *out_atlas = 0;
        return -1;
    }
    if( strncmp(ref, "chrome:", 7) == 0 )
        return resolve_chrome_sprite(builder, ref, out_atlas);
    if( !find_sprite_entry(builder, ref) )
    {
        TORIRS_ERR("uitree_builder_bake: missing sprite '%s' (registered=%d)\n",
            ref,
            builder->sprite_count);
        assert(0 && "required sprite name missing after assets load");
    }
    int atlas = 0;
    int id = UITreeBuilder_ResolveSpriteRef(builder, ref, &atlas);
    if( out_atlas )
        *out_atlas = atlas;
    if( builder->bridge && id >= 0 )
        id = UITreeSceneBridge_EnsureSprite(builder->bridge, id);
    return id;
}

static int
bake_resolve_sprite(void* ud, int graphic_id)
{
    struct UITreeBuilder* builder = (struct UITreeBuilder*)ud;
    assert(builder && builder->provider);
    if( graphic_id <= 0 )
        return -1;
    if( !CacheProvider_SpriteHas(builder->provider, graphic_id) )
    {
        /* Pack-internal sprites are not always prefetched yet — leave unbound. */
        TORIRS_LOG("uitree_builder_bake: RS sprite %d not loaded\n", graphic_id);
        return -1;
    }
    if( builder->bridge )
        return UITreeSceneBridge_EnsureSprite(builder->bridge, graphic_id);
    return graphic_id;
}

/*
 * A component's font, uploaded into the scene as a side effect.
 *
 * The upload is the part that is easy to lose. Resolving a revconfig `font=`
 * name yields a cache font id, and scene font ids ARE cache font ids -- so the
 * id looks perfectly good while the scene has never been handed the glyphs,
 * and every string drawn with it silently draws nothing. Only the
 * cache-component path (bake_resolve_font) used to Ensure, so the first
 * revconfig widget to name a font by name -- the login screen -- was the first
 * to find out.
 *
 * chrome: names are a different id space entirely and must not be Ensured as
 * cache fonts; resolve_chrome_font owns their upload.
 */
static int
bake_op_font_id(
    struct UITreeBuilder const* builder,
    struct UIBuilderTreeOp const* op)
{
    int font_id = -1;

    assert(builder);
    assert(op);

    if( op->has_font_ref && op->font_ref[0] )
    {
        font_id = resolve_chrome_font((struct UITreeBuilder*)builder, op->font_ref);
        if( font_id >= 0 || strncmp(op->font_ref, "chrome:", 7) == 0 )
            return font_id;
        font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
    }
    else if( op->font >= 0 )
    {
        font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
    }

    if( builder->bridge && font_id >= 0 )
        UITreeSceneBridge_EnsureFont(builder->bridge, font_id);
    return font_id;
}

static int
bake_resolve_font(void* ud, int font_id)
{
    struct UITreeBuilder* builder = (struct UITreeBuilder*)ud;
    assert(builder);
    if( font_id < 0 )
        return -1;
    int resolved = UITreeBuilder_ResolveFontArchive(builder, font_id);
    /* Scene font ids equal cache font ids; Ensure is an upload side effect. */
    if( builder->bridge && resolved >= 0 )
        UITreeSceneBridge_EnsureFont(builder->bridge, resolved);
    return resolved;
}

static void
collect_onload(
    struct UITreeBuilder* builder,
    struct ToriRS_Component const* src)
{
    assert(builder && src);
    struct ToriRS_ScriptHook const* on_load =
        ToriRS_ComponentHookPeek(src, TORIRS_COMPONENT_HOOK_LOAD);
    if( !on_load || on_load->argc <= 0 )
        return;
    int script_id = on_load->argv[0];
    if( script_id <= 0 )
        return;
    {
        char const* strp[TORIRS_COMPONENT_HOOK_STR_MAX];
        for( int i = 0; i < TORIRS_COMPONENT_HOOK_STR_MAX; i++ )
            strp[i] = on_load->strv[i];
        UITreeBuilder_AddOnLoad(
            builder,
            src->id,
            script_id,
            on_load->argv,
            on_load->argc,
            on_load->str_mask,
            strp,
            on_load->str_argc);
    }
}

void
uitree_builder_bake_pack_under_owner(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct ToriRS_ComponentPack const* pack,
    int32_t owner_idx,
    int inv_source_id)
{
    assert(tree && builder && pack);
    if( pack->component_count <= 0 )
        return;

    int32_t* index_map = calloc((size_t)pack->component_count, sizeof(int32_t));
    assert(index_map);
    for( int i = 0; i < pack->component_count; i++ )
        index_map[i] = -1;

    /* Pass 1: insert under owner so forward pack-internal parents can be resolved
     * after every index_map slot is filled. */
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        struct UIBuildComponent build;
        UITree_FillBuildFromToriRS(&build, src);

        int32_t idx = UITree_PushBuildComponent(
            tree, owner_idx, &build, bake_resolve_sprite, bake_resolve_font, builder);
        assert(idx >= 0);
        index_map[i] = idx;

        if( inv_source_id >= 0 &&
            (build.type == UIBUILD_INV || build.type == UIBUILD_INV_TEXT) )
        {
            struct UITreeComponent* node = &tree->components[idx];
            if( node->type == UIELEM_RS_INV )
                node->u.rs_inv.inv_source_id = inv_source_id;
            else if( node->type == UIELEM_RS_INV_TEXT )
                node->u.rs_inv_text.inv_source_id = inv_source_id;
        }

        collect_onload(builder, src);
        /* The record's own transmit hooks, armed here rather than by a script.
         * They must exist before the builder's initial var/inv dispatch or the
         * mount paints from the onload and then goes deaf. */
        if( builder->host )
            RS_CS2_RegisterCacheTransmitHooks(builder->host, src);
    }

    /* Pass 2: reparent using full pack id scan (forward layer refs). */
    for( int i = 0; i < pack->component_count; i++ )
    {
        if( index_map[i] < 0 )
            continue;

        struct ToriRS_Component const* src = &pack->components[i];
        int32_t parent_idx = owner_idx;
        if( src->parent_id >= 0 )
        {
            parent_idx = -1;
            for( int j = 0; j < pack->component_count; j++ )
            {
                if( pack->components[j].id == src->parent_id )
                {
                    parent_idx = index_map[j];
                    break;
                }
            }
            if( parent_idx < 0 )
                parent_idx = UITree_FindByComponentId(tree, src->parent_id);
            if( parent_idx < 0 )
                parent_idx = owner_idx;
        }

        if( tree->components[index_map[i]].parent != parent_idx )
            UITree_Reparent(tree, index_map[i], parent_idx);
    }

    /* Pass 3: upload MODEL widgets into the scene (runestones, quest journal
     * scroll, combat spec bars). The pack-assets task loaded the model into
     * the provider; without this Ensure the frame translate finds no scene
     * model and silently drops the widget. Mirrors the font/sprite Ensure
     * side effects — task_interface_open's upload_model_nodes only covered
     * its own demo path. */
    if( builder->bridge )
    {
        for( int i = 0; i < pack->component_count; i++ )
        {
            struct UITreeComponent* node;
            int cache_id;
            int scene_id;
            if( index_map[i] < 0 )
                continue;
            node = &tree->components[index_map[i]];
            if( node->type != UIELEM_RS_MODEL )
                continue;
            cache_id = node->u.rs_model.gamecache_model_id;
            if( cache_id < 0 )
            {
                /* Local-player preview (client_code 327/328): composite the
                 * default avatar and pose it at readyanim frame 0. The
                 * reference (CC_DESIGN_PREVIEW) poses the composite once at
                 * SeqType.list[readyanim].frames[0] and never advances it —
                 * only modelYAn spins — so hold the frame. */
                if( node->behavior.client_code == 327 || node->behavior.client_code == 328 )
                {
                    scene_id = UITreeSceneBridge_EnsurePlayerModel(builder->bridge);
                    if( scene_id >= 0 )
                    {
                        /* The human ready animation. Which seq that is belongs
                         * to the profile: a preview posed at some other cache's
                         * 808 is a bind-pose snap or a wrong pose, silently. */
                        node->u.rs_model.gamecache_model_id = scene_id;
                        if( node->u.rs_model.anim_seq_id < 0 )
                            node->u.rs_model.anim_seq_id = builder->bridge->player_idle_seq;
                        node->u.rs_model.anim_frame = 0;
                        node->u.rs_model.anim_frame_cycle = 0;
                        node->u.rs_model.anim_hold = 1;
                    }
                }
                continue;
            }
            scene_id = UITreeSceneBridge_EnsureModel(builder->bridge, cache_id);
            if( scene_id >= 0 )
                node->u.rs_model.gamecache_model_id = scene_id;
            else if( getenv("TORIRS_ANIM_DEBUG") )
                TORIRS_LOG("bake: model widget com=0x%x cache_id=%d not loadable\n",
                    (unsigned)node->component_id,
                    cache_id);
        }
    }

    /*
     * The pack's own handlers, which this path used to drop on the floor.
     *
     * `UITree_BuildFromComponentPack` bakes them and this function does not,
     * and the two are not interchangeable: every *sidebar tab* mounts through
     * here (`task_slot_mount.c`), so interface 182's "Click here to logout" and
     * "World Switcher" carried `onmouseover`/`onmouseleave` in the cache,
     * decoded them correctly, and arrived in the tree with no hooks at all —
     * the text never changed colour under the pointer. The combat tab's
     * buttons lost their `onmouserepeat` tooltips the same way.
     *
     * Last, not with the insert pass: hooks are resolved by component id, so
     * every node has to be in the tree first.
     */
    UITree_BakePackRuntimeHooks(tree, pack);

    free(index_map);
}

static int32_t
find_op_node(
    struct UIBuilderTreeOp const* ops,
    int32_t const* node_index,
    int op_count,
    char const* name)
{
    assert(name);
    if( name[0] == '\0' )
        return -1;
    for( int i = 0; i < op_count; i++ )
    {
        if( strcmp(ops[i].name, name) == 0 )
            return node_index[i];
    }
    return -1;
}

static int32_t
push_builtin_op(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderTreeOp const* op,
    int32_t parent_index,
    struct InvManager* invs)
{
    assert(tree && builder && op);

    enum UITreeComponentType type = type_from_string(op->type);
    int is_iface_mount = strcmp(op->type, "rs_iface") == 0;
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = type;
    /*
     * An rs_iface owner keeps componentno as the group it mounts, not as its own
     * uid: the pack's own child 0 already owns (group << 16), and an unpacked
     * group id would collide with some other interface's uid in
     * UITree_FindByComponentId. -1 also keeps the owner out of the
     * unmounted-spillover sweep, which is what it is there for.
     */
    spec.component_id = is_iface_mount ? -1 : op->componentno;
    /*
     * A control the PROFILE invented gets an id of its own, so a click on it
     * has something to travel as.
     *
     * Only one that offers a menu row or a hover colour -- the two things a
     * control has to be RECOGNISED at runtime for. The click path carries a
     * component id, and so does the hover walk, which reports a node by id and
     * only ever a node that has one (uitree_hover.c gates on `component_id >=
     * 0`): a caption with `over_color=` and no id is a caption that never
     * lights up. An id costs nothing but it also means nothing for the frame's
     * stone sprites, and handing every decorative layer one would put
     * thousands of them into the tree's by-id lookups for no reader.
     */
    if( spec.component_id < 0 && !is_iface_mount &&
        (op->option[0] != '\0' || op->over_color != 0) )
        spec.component_id = TORIRS_REVCONFIG_ID_BASE + builder->authored_id_next++;
    apply_layout_position(op, &spec.position);
    spec.has_position = 1;
    spec.slot_tag = (uint8_t)slot_tag_from_string(op->slot);
    if( op->role[0] != '\0' && builder->roles )
    {
        spec.role_id = UITree_RoleIntern(builder->roles, op->role);
        /* Told at the same moment the tag is stamped, so a lookup for a role
         * nothing authored never walks the tree hunting for one. */
        UITree_RoleMarkAuthored(builder->roles, spec.role_id);
    }
    if( op->dirty )
        spec.always_dirty = 1;
    copy_menu_options(op, &spec.menu_options);
    /* Advertised hotkey effects (revconfig hotkey= lines) are a property of the
     * component, not of its type, so they resolve here rather than in the type
     * switch. An unknown effect name asserts: a silently-ignored one leaves a
     * key that quietly does nothing, and the set is closed and hard-coded. */
    for( int i = 0; i < op->hotkey_count && i < REVCONFIG_COMPONENT_HOTKEY_MAX; i++ )
    {
        uint32_t effect = UITree_HotkeyEffectFromName(op->hotkeys[i]);
        assert(effect != 0 && "component hotkey= names an unknown effect");
        spec.hotkey_effects |= effect;
    }

    int sprite_id = -1;
    int atlas_index = 0;
    int sprite_active_id = -1;
    int atlas_active = 0;

    if( op->sprite_ref[0] )
        sprite_id = resolve_sprite_required(builder, op->sprite_ref, &atlas_index);
    if( op->sprite_active_ref[0] )
        sprite_active_id =
            resolve_sprite_required(builder, op->sprite_active_ref, &atlas_active);

    struct UITreeBehavior behavior;
    memset(&behavior, 0, sizeof(behavior));
    /*
     * -1, not the memset's 0, and this one has bitten before.
     *
     * `over_layer_id` is a COMPONENT ID, and zero is a real one. The hover walk
     * tests it before it tests anything else (`if( over_layer_id >= 0 )
     * redirect`), so a behaviour block left at zero says "when the pointer is
     * on me, report component 0 as hovered" -- which is not this node, so this
     * node's own hover colour never applies, and some unrelated component
     * lights up instead. The chrome's own sidebar button carries the same
     * initialiser for the same reason.
     */
    behavior.over_layer_id = -1;
    if( op->button_type != 0 || op->client_code != 0 || op->over_color != 0 )
    {
        behavior.button_type = op->button_type;
        behavior.client_code = op->client_code;
        behavior.over_color = op->over_color;
        spec.behavior = &behavior;
    }

    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_SPRITE:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_CROSS:
        spec.always_dirty = 1;
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = 0;
        break;
    /* Unlike the cross, the frame matters: `sprite=headicons[1]` is how the
     * indicator picks its icon out of a 20-frame pack, so the atlas index the
     * ref resolved has to survive. */
    case UIELEM_BUILTIN_MULTIWAY:
        spec.always_dirty = 1;
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_REBOOT_TIMER:
        spec.always_dirty = 1;
        spec.u.reboot_timer.color = op->color;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "reboot_timer font missing");
            spec.u.reboot_timer.font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            spec.u.reboot_timer.font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        break;
    /*
     * Title-screen widgets. Every one is always_dirty: what they draw is the
     * host's, and a blinking caret or a moving bar that the retention gate
     * decided was unchanged is a frozen login screen.
     */
    case UIELEM_BUILTIN_LOGIN_INPUT:
        spec.always_dirty = 1;
        spec.u.login_input.field = strcmp(op->title_field, "password") == 0 ? 1 : 0;
        spec.u.login_input.color = op->color;
        spec.u.login_input.center = op->center;
        spec.u.login_input.shadowed = op->shadowed;
        spec.u.login_input.caret_blink = op->title_caret_blink;
        spec.u.login_input.maxlen = op->title_maxlen;
        strncpy(spec.u.login_input.prefix, op->title_prefix, sizeof(spec.u.login_input.prefix) - 1);
        strncpy(spec.u.login_input.caret, op->title_caret, sizeof(spec.u.login_input.caret) - 1);
        strncpy(spec.u.login_input.mask, op->title_mask, sizeof(spec.u.login_input.mask) - 1);
        strncpy(
            spec.u.login_input.charset, op->title_charset, sizeof(spec.u.login_input.charset) - 1);
        spec.u.login_input.font_id = bake_op_font_id(builder, op);
        assert(spec.u.login_input.font_id >= 0 && "login_input font missing");
        break;
    case UIELEM_BUILTIN_LOGIN_BUTTON:
        spec.u.login_button.scene_id = sprite_id;
        spec.u.login_button.atlas_index = atlas_index;
        spec.u.login_button.action = RS_Title_ActionFromName(op->title_action);
        /* A button whose action= did not resolve would sit there swallowing
         * clicks and doing nothing, which is the hardest kind of INI typo to
         * see. Name it here instead. */
        assert(spec.u.login_button.action != 0 && "login_button action= unknown");
        break;
    case UIELEM_BUILTIN_LOGIN_TOGGLE:
        /* Redrawn whenever its own state changes, and that state is the
         * host's -- nothing about the node says it moved. */
        spec.always_dirty = 1;
        spec.u.login_toggle.scene_id = sprite_id;
        spec.u.login_toggle.atlas_index = atlas_index;
        spec.u.login_toggle.scene_id_on = sprite_active_id;
        spec.u.login_toggle.atlas_index_on = atlas_active;
        spec.u.login_toggle.action = RS_Title_ActionFromName(op->title_action);
        /* Same reasoning as the button's: an unresolved action= is a widget
         * that sits there eating clicks and doing nothing. */
        assert(
            (spec.u.login_toggle.action == RS_TITLE_ACTION_TOGGLE_REMEMBER ||
             spec.u.login_toggle.action == RS_TITLE_ACTION_TOGGLE_HIDE) &&
            "login_toggle action= must be toggle_remember or toggle_hide");
        spec.u.login_toggle.toggle =
            spec.u.login_toggle.action == RS_TITLE_ACTION_TOGGLE_HIDE
                ? RS_TITLE_TOGGLE_HIDE
                : RS_TITLE_TOGGLE_REMEMBER;
        break;
    case UIELEM_BUILTIN_LOGIN_MESSAGE:
        spec.always_dirty = 1;
        spec.u.login_message.index = op->title_message_index;
        spec.u.login_message.color = op->color;
        spec.u.login_message.center = op->center;
        spec.u.login_message.shadowed = op->shadowed;
        spec.u.login_message.font_id = bake_op_font_id(builder, op);
        assert(spec.u.login_message.font_id >= 0 && "login_message font missing");
        break;
    case UIELEM_BUILTIN_TITLE_PROGRESS:
        spec.always_dirty = 1;
        spec.u.title_progress.color = op->color;
        spec.u.title_progress.px_per_percent = op->title_px_per_percent;
        break;
    case UIELEM_BUILTIN_TITLE_PROGRESS_TEXT:
        spec.always_dirty = 1;
        spec.u.title_progress_text.color = op->color;
        spec.u.title_progress_text.center = op->center;
        spec.u.title_progress_text.shadowed = op->shadowed;
        spec.u.title_progress_text.font_id = bake_op_font_id(builder, op);
        assert(spec.u.title_progress_text.font_id >= 0 && "title_progress_text font missing");
        break;
    case UIELEM_BUILTIN_TITLE_FLAMES:
        /* always_dirty is not optional here. The desc never changes as the
         * fire burns -- same scene id, same box -- so a retained emit list
         * would keep drawing a sprite whose pixels have moved on, and the fire
         * would stop dead while still looking like a fire. */
        spec.always_dirty = 1;
        spec.u.title_flames.side = strcmp(op->title_field, "right") == 0 ? 1 : 0;
        spec.u.title_flames.bias = op->flame_bias;
        spec.u.title_flames.sway = op->flame_sway;
        spec.u.title_flames.run = op->flame_run;
        spec.u.title_flames.row = op->flame_row;
        /* `box` is the deob's; anything else, including nothing, is the
         * 2004 four-neighbour average. */
        spec.u.title_flames.blur = strcmp(op->flame_blur, "box") == 0 ? 1 : 0;
        break;
    case UIELEM_BUILTIN_ENTITY_OVERLAY:
        spec.always_dirty = 1;
        break;
    case UIELEM_BUILTIN_HOVERTEXT:
        spec.always_dirty = 1;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "hovertext font missing");
            spec.u.hovertext.font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            spec.u.hovertext.font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        break;
    case UIELEM_BUILTIN_MINIMENU:
        spec.always_dirty = 1;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "minimenu font missing");
            spec.u.minimenu.font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            spec.u.minimenu.font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        break;
    case UIELEM_BUILTIN_MINIMAP:
        spec.always_dirty = 1;
        spec.u.minimap.scene_id = sprite_id;
        break;
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        /* The overlay's own dirty tracking decides what it repaints; the tree
         * only has to keep asking it, so the node never goes clean. Fonts are
         * baked in rather than named by the config: the overlay has to work on
         * a cache that failed to open, which is when it is most wanted. */
        spec.always_dirty = 1;
        spec.u.debug_overlay.font_id_small = -1;
        spec.u.debug_overlay.font_id_menu = -1;
        spec.u.debug_overlay.font_id_body = -1;
        spec.u.debug_overlay.skin_scene_id = -1;
        for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
            spec.u.debug_overlay.skin_atlas[i] = -1;
        if( builder->bridge )
        {
            spec.u.debug_overlay.font_id_small =
                UITreeSceneBridge_EnsureDebugFont(builder->bridge, TORIRS_CHROME_FONT_SMALL);
            spec.u.debug_overlay.font_id_menu =
                UITreeSceneBridge_EnsureDebugFont(builder->bridge, TORIRS_CHROME_FONT_MENU);
            spec.u.debug_overlay.font_id_body =
                UITreeSceneBridge_EnsureDebugFont(builder->bridge, TORIRS_CHROME_FONT_BODY);
            /* The bake emits the skin slots in enum order, so slot i is atlas
             * i. Stated rather than assumed: spritebake's --sprite order is
             * the build recipe's, and this is where the two agree. */
            spec.u.debug_overlay.skin_scene_id =
                UITreeSceneBridge_EnsureChromeSkin(builder->bridge);
            if( spec.u.debug_overlay.skin_scene_id >= 0 )
                for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
                    spec.u.debug_overlay.skin_atlas[i] = i;
        }
        break;
    case UIELEM_BUILTIN_CHAT:
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "chat font missing");
            spec.u.chat.font_id = font_id;
        }
        strncpy(
            spec.u.chat.minimenu.op_report_abuse,
            op->chat_op_report_abuse,
            sizeof(spec.u.chat.minimenu.op_report_abuse) - 1);
        spec.u.chat.minimenu.op_report_abuse_action = op->chat_op_report_abuse_action;
        strncpy(
            spec.u.chat.minimenu.op_add_ignore,
            op->chat_op_add_ignore,
            sizeof(spec.u.chat.minimenu.op_add_ignore) - 1);
        spec.u.chat.minimenu.op_add_ignore_action = op->chat_op_add_ignore_action;
        strncpy(
            spec.u.chat.minimenu.op_add_friend,
            op->chat_op_add_friend,
            sizeof(spec.u.chat.minimenu.op_add_friend) - 1);
        spec.u.chat.minimenu.op_add_friend_action = op->chat_op_add_friend_action;
        strncpy(
            spec.u.chat.minimenu.op_accept_trade,
            op->chat_op_accept_trade,
            sizeof(spec.u.chat.minimenu.op_accept_trade) - 1);
        spec.u.chat.minimenu.op_accept_trade_action = op->chat_op_accept_trade_action;
        strncpy(
            spec.u.chat.minimenu.op_accept_duel,
            op->chat_op_accept_duel,
            sizeof(spec.u.chat.minimenu.op_accept_duel) - 1);
        spec.u.chat.minimenu.op_accept_duel_action = op->chat_op_accept_duel_action;
        break;
    case UIELEM_BUILTIN_CHAT_BUTTON:
    {
        struct UITreeChatButtonConfig* cb = &spec.u.chat_button;
        if( op->chat_button_filter >= 0 && op->chat_button_filter <= 3 )
            cb->filter = (enum UITreeChatButtonFilter)op->chat_button_filter;
        else
            cb->filter = UITREE_CHAT_BUTTON_PUBLIC;
        strncpy(cb->label, op->chat_button_label, sizeof(cb->label) - 1);
        /* label_y/mode_y are box-top offsets, while the reference numbers
         * (redrawPrivacySettings: label 14 / report 19, mode 27 below the
         * node top) are baselines, so the defaults carry the same
         * baseline - p12 ascent shift the INI values use. */
        cb->label_y = op->chat_button_label_y != 0
                          ? op->chat_button_label_y
                          : (cb->filter == UITREE_CHAT_BUTTON_REPORT ? 19 - 12 : 14 - 12);
        cb->mode_y = op->chat_button_mode_y != 0 ? op->chat_button_mode_y : 27 - 12;
        if( op->has_font_ref && op->font_ref[0] )
        {
            int font_id = UITreeBuilder_ResolveFontName(builder, op->font_ref);
            assert(font_id >= 0 && "chat_button font missing");
            cb->font_id = font_id;
        }
        else if( op->font >= 0 )
        {
            cb->font_id = UITreeBuilder_ResolveFontArchive(builder, op->font);
        }
        cb->center = op->center;
        cb->shadowed = op->shadowed;
        for( int i = 0; i < 4; i++ )
        {
            strncpy(
                cb->mode_label[i],
                op->chat_button_mode_label[i],
                sizeof(cb->mode_label[i]) - 1);
            cb->mode_color[i] = op->chat_button_mode_color[i];
        }
        break;
    }
    case UIELEM_BUILTIN_WORLD:
        spec.u.world.level_mask = (uint8_t)op->level_mask;
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        spec.u.redstone_tab.tabno = op->tabno;
        spec.u.redstone_tab.scene_id = sprite_id;
        spec.u.redstone_tab.atlas_index = atlas_index;
        spec.u.redstone_tab.scene_id_active = sprite_active_id;
        spec.u.redstone_tab.atlas_index_active = atlas_active;
        break;
    case UIELEM_BUILTIN_TAB_ICONS:
        spec.u.tab_icon.tabno = op->tabno;
        spec.u.tab_icon.scene_id = sprite_id;
        spec.u.tab_icon.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_SIDEBAR:
    {
        int inv_source_id = UITREE_INV_SOURCE_INVALID;
        if( op->inv_name[0] && invs )
            inv_source_id = InvManager_ResolveSource(invs, op->inv_name);
        spec.u.sidebar.tabno = op->tabno;
        spec.u.sidebar.componentno = op->componentno;
        spec.u.sidebar.inv_source_id = inv_source_id;
        spec.u.sidebar.selected = op->selected ? 1 : 0;
        break;
    }
    case UIELEM_RS_GRAPHIC:
        spec.u.rs_graphic.scene_id = sprite_id;
        spec.u.rs_graphic.atlas_index = atlas_index;
        spec.u.rs_graphic.scene_id_active = sprite_active_id;
        spec.u.rs_graphic.atlas_index_active = atlas_active;
        spec.u.rs_graphic.tiled = op->tiled ? 1 : 0;
        break;
    case UIELEM_RS_TEXT:
        spec.u.rs_text.font_id = bake_op_font_id(builder, op);
        assert(spec.u.rs_text.font_id >= 0 && "rs_text font missing");
        spec.u.rs_text.color = op->color;
        spec.u.rs_text.center = op->center ? 1 : 0;
        spec.u.rs_text.y_align = op->valign;
        spec.u.rs_text.shadowed = op->shadowed ? 1 : 0;
        spec.u.rs_text.baseline = op->text_baseline;
        spec.u.rs_text.text = op->text[0] ? op->text : NULL;
        break;
    case UIELEM_RS_RECT:
        spec.u.rs_rect.color = op->color;
        spec.u.rs_rect.filled = op->filled ? 1 : 0;
        break;
    case UIELEM_RS_LAYER:
        spec.u.rs_layer.scroll_height = 0;
        spec.u.rs_layer.scroll_width = 0;
        break;
    case UIELEM_RS_MODEL:
        spec.u.rs_model.gamecache_model_id = op->componentno >= 0 ? op->componentno : 0;
        spec.u.rs_model.zoom = 100;
        break;
    case UIELEM_RS_INV:
    {
        int inv_source_id = UITREE_INV_SOURCE_INVALID;
        if( op->inv_name[0] && invs )
            inv_source_id = InvManager_ResolveSource(invs, op->inv_name);
        spec.u.rs_inv.inv_source_id = inv_source_id;
        spec.u.rs_inv.cols = op->width > 0 ? op->width : 4;
        spec.u.rs_inv.rows = op->height > 0 ? op->height : 7;
        /* INI-built grids carry no objSwap flag; keep them draggable (prior
         * behaviour). Real equipment/worn grids come from the cache mount,
         * which decodes the reference objSwap || objReplace. */
        spec.u.rs_inv.can_drag = 1;
        break;
    }
    case UIELEM_RS_LINE:
        spec.u.rs_line.color = op->color;
        spec.u.rs_line.line_width = 1;
        spec.u.rs_line.horizontal = op->filled ? 1 : 0;
        break;
    default:
        break;
    }

    int32_t idx = UITree_Push(tree, parent_index, &spec);
    assert(idx >= 0);
    return idx;
}

static void
bake_rs_subtree_for_op(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderTreeOp const* op,
    int32_t owner_idx,
    struct InvManager* invs)
{
    assert(tree && builder && op && owner_idx >= 0);
    assert(op->componentno >= 0);

    int packed = uibuilder_pack_component_id(op->componentno);
    int iface_id = packed >> 16;
    struct ToriRS_ComponentPack* pack =
        CacheProvider_ComponentPackGet(builder->provider, iface_id);
    assert(pack && "RS component pack missing after assets load");

    int inv_source_id = UITREE_INV_SOURCE_INVALID;
    if( op->inv_name[0] && invs )
        inv_source_id = InvManager_ResolveSource(invs, op->inv_name);

    uitree_builder_bake_pack_under_owner(tree, builder, pack, owner_idx, inv_source_id);
}

/*
 * Resolve the [hotkey:…] bindings against the nodes this bake just produced.
 *
 * A binding names a COMPONENT, and a component can be placed by more than one
 * layout entry (a fixed and a resizable gameframe both mounting the same tab
 * icon), so every op built from that component gets its own binding — whichever
 * copy is on screen answers the key.
 *
 * Three ways a binding is dropped, all quietly, because a revision's INI can
 * legitimately name chrome it does not lay out: an unknown key name, a
 * component with no layout entry, and — the one that matters — a component that
 * never advertised the effect. That last check is why `hotkey=` exists on the
 * component at all: a key can only reach behaviour the component opted into.
 */
static void
bake_hotkeys(
    struct UITree* tree,
    struct UIBuilderManifest const* manifest,
    int32_t const* node_index)
{
    assert(tree && manifest);

    tree->hotkey_count = 0;
    for( int i = 0; i < manifest->hotkey_count; i++ )
    {
        struct UIBuilderHotkey const* binding = &manifest->hotkeys[i];
        int osrs_key = LibToriRS_OsrsKeyFromName(binding->key_name);
        uint32_t effect = UITree_HotkeyEffectFromName(binding->effect);

        if( osrs_key < 0 || effect == 0 )
            continue;

        for( int op = 0; op < manifest->op_count; op++ )
        {
            if( strcmp(manifest->ops[op].component_name, binding->component_name) != 0 )
                continue;
            if( node_index[op] < 0 )
                continue;
            if( (tree->components[node_index[op]].hotkey_effects & effect) == 0 )
                continue;
            if( tree->hotkey_count >= UITREE_HOTKEY_MAX )
                return;
            tree->hotkeys[tree->hotkey_count].osrs_key = osrs_key;
            tree->hotkeys[tree->hotkey_count].node_index = node_index[op];
            tree->hotkeys[tree->hotkey_count].effect = effect;
            tree->hotkey_count++;
        }
    }
}

void
uitree_builder_bake(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs)
{
    assert(tree);
    assert(builder);
    assert(manifest);
    assert(invs);

    uitree_builder_inv_seed(invs, manifest);

    if( manifest->op_count <= 0 )
        return;

    int32_t* node_index = calloc((size_t)manifest->op_count, sizeof(int32_t));
    assert(node_index);
    for( int i = 0; i < manifest->op_count; i++ )
        node_index[i] = -1;

    int built = 0;
    int guard = 0;
    int progress = 1;
    while( built < manifest->op_count && progress && guard < manifest->op_count * 4 )
    {
        progress = 0;
        guard++;
        for( int i = 0; i < manifest->op_count; i++ )
        {
            if( node_index[i] >= 0 )
                continue;

            struct UIBuilderTreeOp const* op = &manifest->ops[i];
            int32_t parent_index = -1;
            if( op->parent_name[0] != '\0' )
            {
                parent_index =
                    find_op_node(manifest->ops, node_index, manifest->op_count, op->parent_name);
                if( parent_index < 0 )
                    continue;
            }

            int32_t idx = push_builtin_op(tree, builder, op, parent_index, invs);
            node_index[i] = idx;
            built++;
            progress = 1;

            if( op->kind == UIBUILDER_OP_PUSH_RS_SUBTREE && op->componentno >= 0 )
                bake_rs_subtree_for_op(tree, builder, op, idx, invs);
        }
    }

    assert(built == manifest->op_count && "incomplete layout bake (parent cycle?)");

    uitree_builder_inv_bind_tree(tree, builder, manifest, invs);
    bake_hotkeys(tree, manifest, node_index);
    free(node_index);
}

/*
 * True when `group` hosts at least one mounted sub-interface.
 *
 * Such a group is part of the live tree by definition — something is mounted
 * inside it — so it is never spillover, however many levels down the mount
 * target happens to be.
 */
static int
group_hosts_a_mount(
    struct UITree const* tree,
    int group)
{
    for( int i = 0; i < tree->interface_parent_count; i++ )
    {
        if( ((tree->interface_parents[i].container_uid >> 16) & 0xffff) == group )
            return 1;
    }
    return 0;
}

void
uitree_builder_hide_unmounted_spillover(
    struct UITree* tree,
    int opening_group,
    int target_uid)
{
    int32_t root;
    assert(tree);
    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int cid = tree->components[root].component_id;
        int group = (cid >> 16) & 0xffff;
        if( cid < 0 || group <= 0 )
            continue;
        /* Reserved client chrome ids are never interface spillover. Current
         * visual overlays come from RevConfig and normally have id -1, but
         * retain the guard for older/custom profiles that assigned this group. */
        if( group == 0x7FFE )
            continue;
        if( opening_group >= 0 && group == opening_group )
            continue;
        if( UITree_InterfaceParentIsMountedGroup(tree, group) )
            continue;
        /* Keep already-mounted groups and chrome that parents them. A pack baked
         * under a revconfig `rs_iface` owner is parented by construction, which
         * is what makes a declared mount immune to this sweep. */
        if( tree->components[root].parent >= 0 )
            continue;
        /* Never hide the active toplevel root group (e.g. 161) while subs are
         * mounted into it — only hide accidental sibling spillover packs.
         *
         * The mount target's own group is the obvious case, but it is not the
         * only one: mounting into a *nested* sub-interface leaves every group
         * above it unprotected. Opening the chat dialogue (231) into 162:561
         * gave host_group 162 and then hid 161 — the entire gameframe — which
         * renders as a blank frame with no error anywhere.
         *
         * Hosting a mount is the general form of "part of the live tree", and
         * it covers the immediate host as well, so one test replaces both. */
        if( group_hosts_a_mount(tree, group) )
            continue;
        if( target_uid >= 0 )
        {
            int host_group = (target_uid >> 16) & 0xffff;
            if( group == host_group )
                continue;
        }
        /* Mark it as ours: a pack the CS2 runtime baked ahead of its mount is
         * hidden here, and mount_pack_under_target must be able to tell that
         * hide apart from a cache/script one when the mount finally lands. */
        /* Kept: this is exactly the print that identified the bug above, and
         * "which root did the tree just hide" is invisible from anywhere else. */
        if( getenv("TORIRS_SPILLOVER_DEBUG") )
            TORIRS_LOG("spillover: hiding root group %d (opening %d, target %d:%d)\n",
                group,
                opening_group,
                (target_uid >> 16) & 0xffff,
                target_uid & 0xffff);
        if( !tree->components[root].behavior.hide )
            tree->components[root].behavior.hide_unmounted = 1;
        tree->components[root].behavior.hide = 1;
        /* A hide is a change in what the walk emits just as much as an unhide;
         * the retention gate (Opt 11) reads dirty_gen and nothing here marks. */
        tree->dirty_gen++;
    }
}

void
uitree_builder_reassert_player_idle_anim(
    struct UITree* tree,
    struct UITreeSceneBridge const* bridge)
{
    int mi;
    assert(tree);
    assert(bridge);
    for( mi = 0; mi < tree->models.count; mi++ )
    {
        int32_t i = tree->models.slots[mi];
        struct UITreeComponent* c;
        assert(i >= 0 && (uint32_t)i < tree->component_count);
        c = &tree->components[i];
        if( c->type != UIELEM_RS_MODEL )
            continue;
        if( c->behavior.client_code != 327 && c->behavior.client_code != 328 )
            continue;
        if( c->u.rs_model.gamecache_model_id != UITREE_SCENE_PLAYER_MODEL_ID )
            continue;
        c->u.rs_model.anim_seq_id = bridge->player_idle_seq;
        c->u.rs_model.anim_frame = 0;
        c->u.rs_model.anim_frame_cycle = 0;
        c->u.rs_model.anim_hold = 1;
    }
}
