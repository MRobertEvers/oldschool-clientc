#include "game/rs_cs2_host.h"

#include "game/rs_loot_store.h"
#include "game/rs_player_stats.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_db.h"
#include "engine/torirs_types.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_worldmap.h"
#include "inv/inv_manager.h"
#include "perf/torirs_perf.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_scroll.h"
#include "toridraw_font.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

/** UIZOOM_RESET / UIZOOM_GETDEFAULT constant (1000 = 100%, reference scheme). */
#define RS_CS2_UIZOOM_DEFAULT 1000

/* =========================================================================
 * Helpers
 * ========================================================================= */

static struct UITree*
rs_cs2_tree(struct RS_CS2Host* host)
{
    assert(host);
    assert(host->tree);
    return host->tree;
}

static struct CacheProvider*
rs_cs2_provider(struct RS_CS2Host* host)
{
    assert(host);
    return host->provider;
}

static int32_t
rs_cs2_find_node(
    struct RS_CS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    if( component_id < 0 )
        return -1;
    return UITree_FindByComponentId(tree, component_id);
}

static struct UITreeComponent*
rs_cs2_node(
    struct RS_CS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    int32_t idx = rs_cs2_find_node(host, component_id);
    if( idx < 0 )
        return NULL;
    return &tree->components[idx];
}

static int
rs_cs2_yield(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest const* request)
{
    assert(host);
    assert(request);
    host->pending = *request;
    host->has_pending = true;
    return CS2VM_EXECNO_YIELD;
}

/**
 * One opcode, one yield. `rs_cs2_yield_load` parks a request for the task layer
 * and records what it is waiting for; `rs_cs2_await_spent` tells the handler on
 * the retry that this exact wait already happened, so a resource that is still
 * missing is genuinely absent and the handler must complete with a default
 * rather than yield again (which the VM's yield-halt guard treats as a bug).
 *
 * `id2` carries the second resource of a two-resource request (obj + param
 * type, struct + param type); pass -1 when there is only one.
 */
static bool
rs_cs2_await_spent(
    struct RS_CS2Host* host,
    enum CS2VM_HostRequestKind kind,
    int id,
    int id2)
{
    assert(host);
    return host->has_awaited && host->awaited_kind == kind && host->awaited_id == id &&
           host->awaited_id2 == id2;
}

static int
rs_cs2_yield_load(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest const* request,
    int id,
    int id2)
{
    assert(host);
    assert(request);
    host->awaited_kind = request->kind;
    host->awaited_id = id;
    host->awaited_id2 = id2;
    host->has_awaited = true;
    return rs_cs2_yield(host, request);
}

/**
 * Yield when the target's interface group is not mounted yet. The test is the
 * group *root* (`group<<16`), never the exact component: the task layer loads
 * and bakes a whole pack, so once the root is present a still-missing child
 * cannot be conjured by loading again — the caller treats it as not-found.
 */
static int
rs_cs2_yield_if_group_missing(
    struct RS_CS2Host* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;

    assert(host);
    assert(request);

    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_find_node(host, group_id << 16) >= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_await_spent(host, request->kind, group_id, -1) )
        return CS2VM_EXECNO_OK;

    if( getenv("TORIRS_CS2_MOUNT_DEBUG") )
        fprintf(
            stderr,
            "cs2-automount: group %d requested via component 0x%08x (req kind=%d)\n",
            group_id,
            (unsigned)component_id,
            (int)request->kind);
    return rs_cs2_yield_load(host, request, group_id, -1);
}

static bool
rs_cs2_sprite_ready(
    struct RS_CS2Host* host,
    int graphic_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    /* Negative or absurd ids are not cache sprites — treat as ready (no-op load). */
    if( graphic_id < 0 || graphic_id >= 1000000 )
        return true;
    return provider && CacheProvider_SpriteHas(provider, graphic_id);
}

static bool
rs_cs2_font_ready(
    struct RS_CS2Host* host,
    int font_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    if( font_id < 0 )
        return true;
    return provider && CacheProvider_FontHas(provider, font_id);
}

static bool
rs_cs2_model_ready(
    struct RS_CS2Host* host,
    int model_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    if( model_id < 0 )
        return true;
    return provider && CacheProvider_ModelHas(provider, model_id);
}

/* True when the npctype is resident and every non-negative chathead model id
 * is resident. Empty/missing heads count as ready so we do not yield forever —
 * EnsureNpcHead still returns -1 and the widget stays unchanged. */
static bool
rs_cs2_npc_head_ready(
    struct RS_CS2Host* host,
    int npc_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Npctype* npc;
    int i;

    if( npc_id < 0 )
        return true;
    if( !provider || !CacheProvider_NpctypeHas(provider, npc_id) )
        return false;
    npc = CacheProvider_NpctypeGet(provider, npc_id);
    assert(npc);
    if( !npc->heads || npc->heads_count <= 0 )
        return true;
    for( i = 0; i < npc->heads_count; i++ )
    {
        int mid = npc->heads[i];
        if( mid < 0 )
            continue;
        if( !CacheProvider_ModelHas(provider, mid) )
            return false;
    }
    return true;
}

static bool
rs_cs2_resolve_obj_icon(
    struct RS_CS2Host* host,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    int c;
    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    assert(host);
    if( !host->invs || obj_id <= 0 )
        return false;

    for( c = 0; c < host->invs->container_count; c++ )
    {
        struct InvContainer const* container = &host->invs->containers[c];
        int slot;
        for( slot = 0; slot < container->slot_count; slot++ )
        {
            if( container->slots[slot].obj_id != obj_id )
                continue;
            if( container->slots[slot].scene_id < 0 )
                continue;
            if( out_scene_id )
                *out_scene_id = container->slots[slot].scene_id;
            if( out_atlas_index )
                *out_atlas_index = container->slots[slot].atlas_index;
            return true;
        }
    }
    return false;
}

static void
rs_cs2_apply_op(
    struct UITree* tree,
    int component_id,
    int index,
    char const* text)
{
    int32_t idx;
    assert(tree);
    if( index < 1 || index > UITREE_MENU_OPTION_SLOTS )
        return;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    strncpy(
        tree->components[idx].menu_options.ops[index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.ops[index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
    /* TORIRS_OPS_DEBUG=1: which script wrote which verb onto which component.
     * The rev-230 inventory's rows are entirely script-assigned, so a wrong or
     * missing verb there is invisible until the menu is opened. */
    if( getenv("TORIRS_OPS_DEBUG") )
        fprintf(
            stderr,
            "cs2 setop com=0x%08x (%d|%d) op%d=\"%s\"\n",
            (unsigned)component_id,
            (component_id >> 16) & 0xFFFF,
            component_id & 0xFFFF,
            index,
            text ? text : "");
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_clear_ops(
    struct UITree* tree,
    int component_id)
{
    int32_t idx;
    int i;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    for( i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        tree->components[idx].menu_options.ops[i][0] = '\0';
    tree->components[idx].menu_options.option[0] = '\0';
    UITree_MenuSubmenuClear(&tree->components[idx].menu_options, 0);
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_apply_op_submenu(
    struct UITree* tree,
    int component_id,
    int op_index,
    int sub_index,
    char const* text)
{
    int32_t idx;
    assert(tree);
    /* Script indices are 1-based (same convention as rs_cs2_apply_op). */
    if( op_index < 1 || op_index > UITREE_SUBMENU_OP_SLOTS )
        return;
    if( sub_index < 1 || sub_index > UITREE_SUBMENU_ENTRY_SLOTS )
        return;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    UITree_MenuSubmenuSetEntry(
        &tree->components[idx].menu_options, op_index, sub_index, text ? text : "");
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_get_text(
    struct UITree* tree,
    int component_id,
    char* buf,
    int buf_len)
{
    int32_t idx;
    char const* text = NULL;
    assert(tree);
    assert(buf);
    assert(buf_len > 0);
    buf[0] = '\0';
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    if( tree->components[idx].type == UIELEM_RS_TEXT )
        text = tree->components[idx].u.rs_text.text;
    if( !text )
        text = tree->components[idx].data_text;
    if( text )
    {
        strncpy(buf, text, (size_t)buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

static void
rs_cs2_set_cc_target(
    struct CS2VM2_Thread* thread,
    int dot_operand,
    int component_id)
{
    CS2VM2_SetTargetComponentId(thread, dot_operand, component_id);
}

static int
rs_cs2_parent_component_id(
    struct UITree* tree,
    int component_id)
{
    int32_t idx;
    int32_t parent;
    assert(tree);

    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return -1;
    parent = tree->components[idx].parent;
    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return -1;
    return tree->components[parent].component_id;
}

/*
 * PARAWIDTH / PARAHEIGHT: how wide, and how many lines, a string wraps to.
 *
 * Markup is skipped, and that is the whole subtlety. These are measuring what
 * the renderer will DRAW, and the renderer consumes `<col=…>`, `</col>`,
 * `<lt>`, `<gt>` and `@xxx@` without emitting glyphs for them — so a byte walk
 * that counts them measures a string far wider than the one that appears. The
 * grammar comes from the renderer itself (ToriDraw_FontMarkupTokenLength)
 * rather than being restated here, because a second copy is exactly how the two
 * came to disagree.
 *
 * What it cost: the journal's summary panel centres each cell's icon+value pair
 * on `parawidth(value)`. Every value is colour-tagged
 * (`<col=0dc10d>0</col>` — 19 bytes rendering one glyph), so the measurement
 * came back 104 instead of 6, and the icon was placed at
 * `centre - (104 + 18 + 4)/2` — eighteen pixels off the left edge of the panel,
 * with the number stranded at the far left of a 104-wide box.
 */
static void
rs_cs2_font_wrap(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width,
    int* out_lines,
    int* out_width)
{
    int lines = 1;
    int line_w = 0;
    int best = 0;
    int len;
    int i = 0;

    assert(font);
    *out_lines = 0;
    *out_width = 0;
    if( !text || !text[0] )
        return;
    len = (int)strlen(text);

    while( i < len )
    {
        unsigned char emitted = 0;
        int const token = ToriDraw_FontMarkupTokenLength(text, len, i, &emitted);
        unsigned char ch;
        int adv;

        if( token > 0 )
        {
            i += token;
            if( !emitted )
                continue; /* pure markup: consumes bytes, draws nothing */
            ch = emitted;
        }
        else
        {
            ch = (unsigned char)text[i];
            i++;
        }

        if( ch == '\n' )
        {
            lines++;
            if( line_w > best )
                best = line_w;
            line_w = 0;
            continue;
        }
        adv = font->draw_width[ch];
        if( max_width > 0 && line_w + adv > max_width && line_w > 0 )
        {
            lines++;
            if( line_w > best )
                best = line_w;
            line_w = adv;
        }
        else
            line_w += adv;
    }
    if( line_w > best )
        best = line_w;
    if( max_width > 0 && best > max_width )
        best = max_width;

    *out_lines = lines;
    *out_width = best;
}

static int
rs_cs2_font_wrap_line_count(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width)
{
    int lines, width;
    if( !text || !text[0] )
        return 0;
    if( max_width <= 0 )
        return 1;
    rs_cs2_font_wrap(font, text, max_width, &lines, &width);
    return lines;
}

static int
rs_cs2_font_wrap_max_line_width(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width)
{
    int lines, width;
    rs_cs2_font_wrap(font, text, max_width, &lines, &width);
    return width;
}

static int
rs_cs2_enum_lookup_int(
    struct ToriRS_Enum const* e,
    int key)
{
    int i;
    assert(e);
    if( e->output_is_string )
        return -1;
    if( !e->keys || e->count <= 0 )
        return e->default_int;
    for( i = 0; i < e->count; i++ )
    {
        if( e->keys[i] == key )
            return e->int_values ? e->int_values[i] : e->default_int;
    }
    return e->default_int;
}

static char const*
rs_cs2_enum_lookup_string(
    struct ToriRS_Enum const* e,
    int key)
{
    int i;
    assert(e);
    if( !e->output_is_string || !e->keys )
        return NULL;
    if( e->count <= 0 )
        return e->default_string;
    for( i = 0; i < e->count; i++ )
    {
        if( e->keys[i] == key )
            return e->string_values ? e->string_values[i] : e->default_string;
    }
    return e->default_string;
}

static bool
rs_cs2_obj_param_lookup(
    struct ToriRS_Objtype const* obj,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    int i;
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    assert(obj);
    if( param_id < 0 || !obj->params || obj->param_count <= 0 )
        return false;
    for( i = 0; i < obj->param_count; i++ )
    {
        if( obj->params[i].key != param_id )
            continue;
        if( obj->params[i].string_value )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = obj->params[i].string_value;
            return true;
        }
        if( out_int )
            *out_int = obj->params[i].int_value;
        return true;
    }
    return false;
}

static bool
rs_cs2_struct_param_lookup(
    struct ToriRS_Struct const* s,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    int i;
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    assert(s);
    if( param_id < 0 || !s->params || s->param_count <= 0 )
        return false;
    for( i = 0; i < s->param_count; i++ )
    {
        if( s->params[i].key != param_id )
            continue;
        if( s->params[i].string_value )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = s->params[i].string_value;
            return true;
        }
        if( out_int )
            *out_int = s->params[i].int_value;
        return true;
    }
    return false;
}

/* =========================================================================
 * Init / Tick
 * ========================================================================= */

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct VarCManager* varcs)
{
    assert(host);
    assert(tree);
    assert(provider);
    assert(invs);

    memset(host, 0, sizeof(*host));
    host->tree = tree;
    host->provider = provider;
    host->invs = invs;
    host->varps = varps;
    host->varcs = varcs;
    host->client_clock = 100;
    host->top_interface_id = -1;
    host->mouse_x = -1;
    host->mouse_y = -1;
    host->cam_follow_height = 0;
    /* Volumes start muted (0); a settings panel or the server drives them up.
     * memset already zeroed these — kept explicit alongside the getters they back. */
    host->volume_music = 0;
    host->volume_sounds = 0;
    host->volume_area_sounds = 0;
    /* Start at the low end of the documented 2..8 range; a settings panel or the
     * server can drive it. Real default is TBD once minimap zoom is rendered. */
    host->minimap_zoom = 2;
    host->logout_requested = false;
    host->close_modal_requested = false;
    host->resume_pausebutton_component_id = -1;
    /* Preserve what VIEWPORT_GETFOV/GETZOOM returned before they were
     * host-routed (cs2_host_ui.c defaults for fixed-layout clients). */
    host->viewport_fov = 128;
    host->viewport_fov_max = 896;
    host->viewport_zoom = 128;
    host->viewport_zoom_max = 896;
    host->ui_zoom = RS_CS2_UIZOOM_DEFAULT;
    /* Facing north; overwritten every logic tick by RS_CS2Host_SetCameraAngles
     * once a world is up, so this only covers the pre-login window. The pitch
     * default matches app.c's orbit_pitch (the reference orbitCameraPitch). */
    host->cam_yaw = 0;
    host->cam_angle_x = 128;
    host->cam_angle_y = 0;
    host->cam_angle_forced = false;
    /* Op 1 is the primary left-click op, which is what every mouse-driven
     * dispatch reports. Must not be left at the memset 0: on_op handlers read
     * this through the CS2VM_SCRIPT_ARG_OP_INDEX sentinel. */
    host->event_op_index = 1;
    /* No key event in flight: a key CODE of -1 means "this is a character
     * event", so 0 (a real code) would be a lie. */
    host->event_key_typed = -1;
    host->viewport_w = 765;
    host->viewport_h = 503;
    /* Boot mode. 161 (toplevel_osrs_stretch) is the root the manifest opens and
     * the resizable-family gameframe, so resizable is what the client is in
     * before any script says otherwise. App_Init republishes the canvas through
     * App_SetCanvasSize immediately after this. */
    host->window_mode = CS2VM_WINDOW_MODE_RESIZABLE;
    host->default_window_mode = CS2VM_WINDOW_MODE_RESIZABLE;
    host->window_mode_dirty = false;
    host->client_layout_mode = 1; /* resizable classic — matches stretch boot */
    host->client_layout_dirty = false;
    /* pack/12_clientscripts.pack: 3998=script_3998; decompile name settings_client_mode */
    host->script_settings_client_mode = 3998;
    host->bridge = NULL;
    host->has_awaited = false;
    /* Serials start at 1 so fresh hooks (last_seen_serial=0) fire once on the
     * first dispatch after registration (widget-loaded parity). */
    host->var_change_serial = 1;
    host->inv_change_serial = 1;
    host->stat_change_serial = 1;
    host->worldmap = RS_WorldMap_New(provider);
}

void
RS_CS2Host_NotifyVarChanged(
    struct RS_CS2Host* host,
    int var_id)
{
    if( !host )
        return;
    /* Advance the serial so already-fired var-transmit hooks re-run, and flag the
     * per-tick pump to re-dispatch (TS parity: value changes bump changedVarpCount,
     * processed once per cycle rather than synchronously mid-script). */
    host->var_change_serial++;
    host->var_transmit_dirty = 1;
    if( getenv("TORIRS_CC_DEBUG") )
        fprintf(stderr, "VAR_CHANGED id=%d serial=%u\n", var_id, host->var_change_serial);

    /* Remember which id changed so the dispatch can skip hooks that do not list
     * it as a trigger. An unknown id (< 0) or a full set means "re-run
     * everything" — never fewer hooks than the wildcard dispatch would run. */
    if( host->var_changed_all )
        return;
    if( var_id < 0 || host->var_changed_count >= RS_CS2_HOST_VAR_CHANGED_MAX )
    {
        host->var_changed_all = 1;
        host->var_changed_count = 0;
        return;
    }
    for( int i = 0; i < host->var_changed_count; i++ )
    {
        if( host->var_changed_ids[i] == var_id )
            return;
    }
    host->var_changed_ids[host->var_changed_count++] = var_id;
}

void
RS_CS2Host_NotifyInvChanged(
    struct RS_CS2Host* host,
    int container_id)
{
    if( !host )
        return;
    host->inv_change_serial++;
    host->inv_transmit_dirty = 1;
    if( getenv("TORIRS_CC_DEBUG") )
        fprintf(
            stderr,
            "INV_CHANGED container=%d serial=%u\n",
            container_id,
            host->inv_change_serial);

    if( host->inv_changed_all )
        return;
    if( container_id < 0 || host->inv_changed_count >= RS_CS2_HOST_VAR_CHANGED_MAX )
    {
        host->inv_changed_all = 1;
        host->inv_changed_count = 0;
        return;
    }
    for( int i = 0; i < host->inv_changed_count; i++ )
    {
        if( host->inv_changed_ids[i] == container_id )
            return;
    }
    host->inv_changed_ids[host->inv_changed_count++] = container_id;
}

void
RS_CS2Host_SetStats(
    struct RS_CS2Host* host,
    struct RS_PlayerStats* stats)
{
    if( host )
        host->stats = stats;
}

void
RS_CS2Host_NotifyStatChanged(
    struct RS_CS2Host* host,
    int stat_id)
{
    if( !host )
        return;
    host->stat_change_serial++;
    host->stat_transmit_dirty = 1;

    if( host->stat_changed_all )
        return;
    if( stat_id < 0 || host->stat_changed_count >= RS_CS2_HOST_VAR_CHANGED_MAX )
    {
        host->stat_changed_all = 1;
        host->stat_changed_count = 0;
        return;
    }
    for( int i = 0; i < host->stat_changed_count; i++ )
    {
        if( host->stat_changed_ids[i] == stat_id )
            return;
    }
    host->stat_changed_ids[host->stat_changed_count++] = stat_id;
}

void
RS_CS2Host_NotifyMiscChanged(struct RS_CS2Host* host)
{
    if( !host )
        return;
    host->misc_transmit_dirty = 1;
}

void
RS_CS2Host_SetSocial(
    struct RS_CS2Host* host,
    struct RS_Social* social,
    int* filter_modes,
    int world)
{
    if( !host )
        return;
    host->social = social;
    host->chat_filter_mode = filter_modes;
    host->map_world = world;
}

void
RS_CS2Host_NotifyFriendChanged(struct RS_CS2Host* host)
{
    if( !host )
        return;
    host->friend_transmit_dirty = 1;
}

/* Queue an outbound social request for the App to turn into a packet. */
static void
rs_cs2_social_send_push(
    struct RS_CS2Host* host,
    struct RS_CS2SocialSend const* send)
{
    int slot;

    assert(host);
    assert(send);
    if( host->social_send_count >= RS_CS2_HOST_SOCIAL_SEND_MAX )
    {
        /* Dropping the newest keeps the earlier requests of the same tick,
         * which is the order the script issued them in. Say so: a friend add
         * that silently never reached the server reads as a server bug. */
        fprintf(
            stderr,
            "cs2: social send queue full (%d), dropped kind=%d\n",
            RS_CS2_HOST_SOCIAL_SEND_MAX,
            send->kind);
        return;
    }
    slot = (host->social_send_head + host->social_send_count) % RS_CS2_HOST_SOCIAL_SEND_MAX;
    host->social_send[slot] = *send;
    host->social_send_count++;
}

bool
RS_CS2Host_TakeSocialSend(
    struct RS_CS2Host* host,
    struct RS_CS2SocialSend* out)
{
    if( !host || !out || host->social_send_count <= 0 )
        return false;
    *out = host->social_send[host->social_send_head];
    host->social_send_head = (host->social_send_head + 1) % RS_CS2_HOST_SOCIAL_SEND_MAX;
    host->social_send_count--;
    return true;
}

/* Queue a component whose on-resize listener the App should run this tick. */
static void
rs_cs2_call_on_resize_push(
    struct RS_CS2Host* host,
    int component_id)
{
    int slot;

    assert(host);
    if( host->call_on_resize_count >= RS_CS2_HOST_CALL_ON_RESIZE_MAX )
    {
        /* A dropped one is a panel that never builds itself, and a blank panel
         * has no other symptom — so it says so rather than reading as a missing
         * packet. */
        fprintf(
            stderr,
            "cs2: if_callonresize queue full (%d), dropped component 0x%08x\n",
            RS_CS2_HOST_CALL_ON_RESIZE_MAX,
            (unsigned)component_id);
        return;
    }
    slot = (host->call_on_resize_head + host->call_on_resize_count) %
           RS_CS2_HOST_CALL_ON_RESIZE_MAX;
    host->call_on_resize[slot] = component_id;
    host->call_on_resize_count++;
}

bool
RS_CS2Host_TakeCallOnResize(
    struct RS_CS2Host* host,
    int* out_component_id)
{
    if( !host || !out_component_id || host->call_on_resize_count <= 0 )
        return false;
    *out_component_id = host->call_on_resize[host->call_on_resize_head];
    host->call_on_resize_head =
        (host->call_on_resize_head + 1) % RS_CS2_HOST_CALL_ON_RESIZE_MAX;
    host->call_on_resize_count--;
    return true;
}

/* Queue a (component, op index) pair CC_TRIGGEROP asked the App to run. */
static void
rs_cs2_trigger_op_push(
    struct RS_CS2Host* host,
    int component_id,
    int op_index)
{
    int slot;

    assert(host);
    if( host->trigger_op_count >= RS_CS2_HOST_TRIGGER_OP_MAX )
    {
        fprintf(
            stderr,
            "cs2: cc_triggerop queue full (%d), dropped component 0x%08x op %d\n",
            RS_CS2_HOST_TRIGGER_OP_MAX,
            (unsigned)component_id,
            op_index);
        return;
    }
    slot = (host->trigger_op_head + host->trigger_op_count) % RS_CS2_HOST_TRIGGER_OP_MAX;
    host->trigger_op[slot].component_id = component_id;
    host->trigger_op[slot].op_index = op_index;
    host->trigger_op_count++;
}

bool
RS_CS2Host_TakeTriggerOp(
    struct RS_CS2Host* host,
    struct RS_CS2TriggerOp* out)
{
    if( !host || !out || host->trigger_op_count <= 0 )
        return false;
    *out = host->trigger_op[host->trigger_op_head];
    host->trigger_op_head = (host->trigger_op_head + 1) % RS_CS2_HOST_TRIGGER_OP_MAX;
    host->trigger_op_count--;
    return true;
}

void
RS_CS2Host_Free(struct RS_CS2Host* host)
{
    assert(host);
    RS_WorldMap_Free(host->worldmap);
    host->worldmap = NULL;
    free(host->item_search_results);
    host->item_search_results = NULL;
    host->item_search_count = 0;
    host->item_search_cap = 0;
    host->item_search_index = 0;
    free(host->db_find_rows);
    host->db_find_rows = NULL;
    host->db_find_count = 0;
    host->db_find_cursor = 0;
}

void
RS_CS2Host_SetBridge(
    struct RS_CS2Host* host,
    struct UITreeSceneBridge* bridge)
{
    assert(host);
    host->bridge = bridge;
}

void
RS_CS2Host_SetCameraAngles(
    struct RS_CS2Host* host,
    int angle_x,
    int angle_y)
{
    assert(host);
    /* A snap the app has not picked up yet is the newer value of the two — the
     * script wrote it this tick and the camera it would be mirrored from still
     * holds last tick's angles. Leave it alone until it has been consumed. */
    if( host->cam_angle_forced )
        return;
    host->cam_angle_x = angle_x;
    host->cam_angle_y = angle_y & 0x7ff;
    host->cam_yaw = host->cam_angle_y;
}

bool
RS_CS2Host_TakeCameraForce(
    struct RS_CS2Host* host,
    int* out_angle_x,
    int* out_angle_y)
{
    assert(host);
    if( !host->cam_angle_forced )
        return false;
    host->cam_angle_forced = false;
    if( out_angle_x )
        *out_angle_x = host->cam_angle_x;
    if( out_angle_y )
        *out_angle_y = host->cam_angle_y;
    return true;
}

void
RS_CS2Host_Tick(struct RS_CS2Host* host)
{
    assert(host);
    host->client_clock++;
}

/* =========================================================================
 * Inventory
 * ========================================================================= */

static int
rs_cs2_inv_size(
    struct RS_CS2Host* host,
    int inv_id)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 )
        return 0;
    return InvManager_Size(host->invs, inv_id);
}

static int
rs_cs2_inv_get_obj(
    struct RS_CS2Host* host,
    int inv_id,
    int slot)
{
    int obj;
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return -1;
    obj = InvManager_GetObj(host->invs, inv_id, slot);
    /* Scripts expect -1 for empty (reference INV_GETOBJ pushes -1 when the
     * inv or slot has no item); InvManager's empty sentinel is 0. */
    if( obj <= INV_MANAGER_EMPTY_OBJ_ID )
        return -1;
    return obj;
}

static int
rs_cs2_inv_get_num(
    struct RS_CS2Host* host,
    int inv_id,
    int slot)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return 0;
    return InvManager_GetNum(host->invs, inv_id, slot);
}

static int
rs_cs2_inv_total(
    struct RS_CS2Host* host,
    int inv_id,
    int item_id)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || item_id <= 0 )
        return 0;
    return InvManager_Total(host->invs, inv_id, item_id);
}

/* =========================================================================
 * Exec handlers
 * ========================================================================= */

static int
exec_push_script(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int script_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct CS2VM2_Script* script = NULL;

    if( provider )
        script = CacheProvider_ClientScriptGet(provider, script_id);
    if( !script )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
        req.u.push_script.script_id = script_id;
        if( rs_cs2_await_spent(host, req.kind, script_id, -1) )
        {
            /* No degrade possible: the caller expects this script's return
             * values on the stack and we cannot synthesise them. */
            fprintf(stderr, "RS_CS2Host: script %d failed to load\n", script_id);
            return CS2VM_EXECNO_ERROR;
        }
        return rs_cs2_yield_load(host, &req, script_id, -1);
    }
    return CS2VM2_PushCallScript(thread, script);
}

static int
exec_para_height(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_ParaHeight request,
    int is_width)
{
    int result = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriRS_Font* font =
            rs_cs2_provider(host) ? CacheProvider_FontGet(rs_cs2_provider(host), request.font_id)
                                  : NULL;
        if( !font )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = is_width ? CS2VM_HOST_REQUEST_PARAWIDTH : CS2VM_HOST_REQUEST_PARAHEIGHT;
            req.u.para_height = request;
            /* Font still missing after its load: measure as 0. */
            if( !rs_cs2_await_spent(host, req.kind, request.font_id, -1) )
                return rs_cs2_yield_load(host, &req, request.font_id, -1);
        }
        else
            result = is_width ? rs_cs2_font_wrap_max_line_width(font, text, request.max_width)
                              : rs_cs2_font_wrap_line_count(font, text, request.max_width);
    }
    return CS2VM2_PushInt(thread, result);
}

static int
exec_vars_read_varp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int varp_id)
{
    int value = 0;
    if( host->varps )
        value = VarPManager_GetVarp(host->varps, varp_id);
    return CS2VM2_PushInt(thread, value);
}

static int
exec_vars_read_varbit(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int varbit_id)
{
    int value = 0;
    if( host->varps )
        value = VarPManager_GetVarbit(host->varps, varbit_id);
    return CS2VM2_PushInt(thread, value);
}

static int
exec_enum_lookup(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_EnumLookup request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Enum* e = provider ? CacheProvider_EnumGet(provider, request.enum_id) : NULL;
    if( !e )
    {
        /* A computed enum id can legitimately be negative — the world map's
         * onload (1707) picks its layout enum through script 900, which maps
         * IF_GETTOP to an enum id and returns -1 for a top-level interface it
         * does not know (booting 595 on its own, with no gameframe, is exactly
         * that). There is no archive to wait for, so answer the miss now:
         * yielding would queue a load for a negative id, which asserts. */
        if( request.enum_id >= 0 )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
            req.u.enum_lookup = request;
            if( !rs_cs2_await_spent(host, req.kind, request.enum_id, -1) )
                return rs_cs2_yield_load(host, &req, request.enum_id, -1);
        }
        /* Enum still missing after its load: answer like a key that misses. */
        if( request.output_type == (int)'s' )
            return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, "null"));
        return CS2VM2_PushInt(thread, -1);
    }

    (void)request.input_type;
    if( request.output_type == (int)'s' || e->output_is_string )
    {
        char const* value = rs_cs2_enum_lookup_string(e, request.key);
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, value ? value : "null"));
    }

    return CS2VM2_PushInt(thread, rs_cs2_enum_lookup_int(e, request.key));
}

/* =========================================================================
 * World map (interface 595)
 * ========================================================================= */

/* Pushing two ints for a getter that returns a pair: the script pops them in
 * reverse, so push order is (first, second). */
static int
rs_cs2_push_pair(
    struct CS2VM2_Thread* thread,
    int first,
    int second)
{
    int result = CS2VM2_PushInt(thread, first);
    if( result != CS2VM_EXECNO_OK )
        return result;
    return CS2VM2_PushInt(thread, second);
}

static int
exec_worldmap(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_WorldMap request)
{
    struct RS_WorldMapState* map = host->worldmap;
    struct ToriRS_WorldMapArea* area;
    int first = -1;
    int second = -1;

    if( !map )
        return CS2VM_EXECNO_ERROR;

    /* The areas load once for the whole cache. Yield for that load the first
     * time any world map opcode runs; if they are still missing on the retry,
     * this cache has no world map and every getter answers "nothing". */
    if( !RS_WorldMap_Sync(map) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WORLDMAP;
        req.u.worldmap = request;
        if( !rs_cs2_await_spent(host, req.kind, -1, -1) )
            return rs_cs2_yield_load(host, &req, -1, -1);
    }

    switch( request.opcode )
    {
    case CS2_OP_WORLDMAP_INIT:
        RS_WorldMap_Init(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETMAPNAME:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, area && area->external_name ? area->external_name : ""));

    case CS2_OP_WORLDMAP_SETMAP:
        RS_WorldMap_SetCurrentMapId(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETZOOM:
        return CS2VM2_PushInt(thread, RS_WorldMap_Zoom(map));

    case CS2_OP_WORLDMAP_SETZOOM:
        RS_WorldMap_SetZoom(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_ISLOADED:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsLoaded(map) ? 1 : 0);

    case CS2_OP_WORLDMAP_JUMPTODISPLAYCOORD:
        RS_WorldMap_JumpToDisplayCoord(map, request.arg0, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTODISPLAYCOORD_INSTANT:
        RS_WorldMap_JumpToDisplayCoord(map, request.arg0, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOSOURCECOORD:
        RS_WorldMap_JumpToSourceCoord(map, request.arg0, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOSOURCECOORD_INSTANT:
        RS_WorldMap_JumpToSourceCoord(map, request.arg0, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETDISPLAYPOSITION:
        RS_WorldMap_DisplayPosition(map, &first, &second);
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETCONFIGORIGIN:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushInt(thread, area ? area->origin : 0);

    case CS2_OP_WORLDMAP_GETCONFIGSIZE:
        area = RS_WorldMap_Area(map, request.arg0);
        return rs_cs2_push_pair(
            thread, ToriRS_WorldMapArea_WidthTiles(area), ToriRS_WorldMapArea_HeightTiles(area));

    case CS2_OP_WORLDMAP_GETCONFIGBOUNDS:
    {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        int result;

        area = RS_WorldMap_Area(map, request.arg0);
        ToriRS_WorldMapArea_Bounds(area, &min_x, &min_y, &max_x, &max_y);
        result = rs_cs2_push_pair(thread, min_x, min_y);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return rs_cs2_push_pair(thread, max_x, max_y);
    }

    case CS2_OP_WORLDMAP_GETCONFIGZOOM:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushInt(thread, area ? area->zoom : -1);

    case CS2_OP_WORLDMAP_GETDISPLAYCOORD_CURRENT:
        if( !RS_WorldMap_DisplayCoord(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETCURRENTMAP:
        return CS2VM2_PushInt(thread, RS_WorldMap_CurrentMapId(map));

    case CS2_OP_WORLDMAP_GETDISPLAYCOORD:
        if( !RS_WorldMap_SourceToDisplay(map, request.arg0, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETSOURCECOORD:
        return CS2VM2_PushInt(thread, RS_WorldMap_DisplayToSource(map, request.arg0));

    case CS2_OP_WORLDMAP_JUMPTOMAP:
        RS_WorldMap_JumpToMap(map, request.arg0, request.arg1, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOMAP_INSTANT:
        RS_WorldMap_JumpToMap(map, request.arg0, request.arg1, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_COORDINMAP:
        return CS2VM2_PushInt(
            thread, RS_WorldMap_CoordInMap(map, request.arg0, request.arg1) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETSIZE:
        RS_WorldMap_DisplaySize(map, &first, &second);
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETMAP:
        return CS2VM2_PushInt(thread, RS_WorldMap_MapAtCoord(map, request.arg0));

    case CS2_OP_WORLDMAP_SETMAXFLASHCOUNT:
        RS_WorldMap_SetMaxFlashCount(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_RESETMAXFLASHCOUNT:
        RS_WorldMap_ResetMaxFlashCount(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_SETCYCLESPERFLASH:
        RS_WorldMap_SetCyclesPerFlash(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_RESETCYCLESPERFLASH:
        RS_WorldMap_ResetCyclesPerFlash(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETNEARESTICON:
        return CS2VM2_PushInt(thread, RS_WorldMap_NearestIcon(map, request.arg0, request.arg1));

    case CS2_OP_WORLDMAP_PERPETUALFLASH:
        RS_WorldMap_SetPerpetualFlash(map, request.arg0 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_FLASHELEMENT:
        RS_WorldMap_FlashElement(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_FLASHELEMENTCATEGORY:
        RS_WorldMap_FlashCategory(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_STOPCURRENTFLASHES:
        RS_WorldMap_StopCurrentFlashes(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENTS:
        RS_WorldMap_SetElementsEnabled(map, request.arg0 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENT:
        RS_WorldMap_SetElementEnabled(map, request.arg0, request.arg1 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENTCATEGORY:
        RS_WorldMap_SetCategoryEnabled(map, request.arg0, request.arg1 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETDISABLEELEMENTS:
        return CS2VM2_PushInt(thread, RS_WorldMap_ElementsEnabled(map) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETDISABLEELEMENT:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsElementEnabled(map, request.arg0) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETDISABLEELEMENTCATEGORY:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsCategoryEnabled(map, request.arg0) ? 1 : 0);

    case CS2_OP_WORLDMAP_LISTELEMENT_START:
        if( !RS_WorldMap_IconStart(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_LISTELEMENT_NEXT:
        if( !RS_WorldMap_IconNext(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_ELEMENT:
        return CS2VM2_PushInt(thread, map->event_element);

    case CS2_OP_WORLDMAP_ELEMENTCOORD1:
        return CS2VM2_PushInt(thread, map->event_coord1);

    case CS2_OP_WORLDMAP_ELEMENTCOORD:
        return CS2VM2_PushInt(thread, map->event_coord2);

    default:
        fprintf(stderr, "exec_worldmap: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

static int
exec_mec(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_MEC request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_MapElement* element =
        provider ? CacheProvider_MapElementGet(provider, request.mec_id) : NULL;

    if( !element )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_MEC;
        req.u.mec = request;
        if( !rs_cs2_await_spent(host, req.kind, request.mec_id, -1) )
            return rs_cs2_yield_load(host, &req, request.mec_id, -1);
        /* Still missing after its load: answer as the reference does for an
         * absent map element config. */
        if( request.opcode == CS2_OP_MEC_TEXT )
            return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
        if( request.opcode == CS2_OP_MEC_TEXTSIZE )
            return CS2VM2_PushInt(thread, 0);
        return CS2VM2_PushInt(thread, -1);
    }

    switch( request.opcode )
    {
    case CS2_OP_MEC_TEXT:
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, element->name ? element->name : ""));
    case CS2_OP_MEC_TEXTSIZE:
        return CS2VM2_PushInt(thread, element->text_size);
    case CS2_OP_MEC_CATEGORY:
        return CS2VM2_PushInt(thread, element->category);
    case CS2_OP_MEC_SPRITE:
        return CS2VM2_PushInt(thread, element->sprite_id);
    default:
        fprintf(stderr, "exec_mec: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * MINIMENU_* (7100..7110): mouseover / right-click-menu queries. The live model
 * lives in the app layer (app->interact.minimenu, plus the hover-text target)
 * behind the UITree host bus, which the CS2 host cannot reach. Until a per-frame
 * snapshot is plumbed into RS_CS2Host, answer with "nothing hovered / menu
 * closed" defaults so the polling toplevel scripts keep running: every int getter
 * is 0/false and MINIMENU_ENTRY yields two empty strings. Wire real values here
 * when the snapshot lands — the opcode already routes through this one seam.
 */
static int
exec_minimenu(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int opcode)
{
    (void)host;

    switch( opcode )
    {
    case CS2_OP_MINIMENU_ENTRY:
    {
        /* Two strings, option then target (reference push order). */
        int result = CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
    }
    case CS2_OP_MINIMENU_TYPE:
    case CS2_OP_MINIMENU_FINDNPC:
    case CS2_OP_MINIMENU_FINDLOC:
    case CS2_OP_MINIMENU_FINDOBJ:
    case CS2_OP_MINIMENU_FINDPLAYER:
    case CS2_OP_MINIMENU_ISOPEN:
    case CS2_OP_MINIMENU_FINDCOMPONENT:
    case CS2_OP_MINIMENU_NUMOPS:
        return CS2VM2_PushInt(thread, 0);
    default:
        fprintf(stderr, "exec_minimenu: unhandled opcode %d\n", opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Audio volumes (3203..3208) and client/game/device options (3209..3217). The
 * volume values are host-owned (round-trip: a SET is read back by the matching
 * GET), the port having no audio mixer yet. The keyed option families have no
 * backing store here — SET is a no-op, GET answers 0, GETRANGE answers a 0..255
 * span — enough for a settings panel to run; wire real option state through here
 * when it exists (request.option_id / request.value carry the key + payload).
 */
static int
exec_client_option(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_ClientOption request)
{
    switch( request.opcode )
    {
    case CS2_OP_SETVOLUMEMUSIC:
        host->volume_music = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEMUSIC:
        return CS2VM2_PushInt(thread, host->volume_music);
    case CS2_OP_SETVOLUMESOUNDS:
        host->volume_sounds = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMESOUNDS:
        return CS2VM2_PushInt(thread, host->volume_sounds);
    case CS2_OP_SETVOLUMEAREASOUNDS:
        host->volume_area_sounds = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEAREASOUNDS:
        return CS2VM2_PushInt(thread, host->volume_area_sounds);

    case CS2_OP_CLIENTOPTION_SET:
    case CS2_OP_GAMEOPTION_SET:
    case CS2_OP_DEVICEOPTION_SET:
        return CS2VM_EXECNO_OK;
    case CS2_OP_CLIENTOPTION_GET:
    case CS2_OP_GAMEOPTION_GET:
    case CS2_OP_DEVICEOPTION_GET:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_DEVICEOPTION_GETRANGE:
    {
        /* min then max (reference range order). */
        int result = CS2VM2_PushInt(thread, 0);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, 255);
    }
    default:
        fprintf(stderr, "exec_client_option: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Mobile local notifications (3170..3173). A desktop client has no notification
 * centre to schedule into, so the whole family is accepted and dropped. The two
 * answers still matter: SUPPORTED reports 0 so scripts take their "unavailable"
 * branch, and LOCAL_NOTIFICATION must still push a handle (0 — nothing to cancel)
 * because its caller stores the result immediately (script 5360).
 */
static int
exec_local_notification(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_LocalNotification request)
{
    switch( request.opcode )
    {
    case CS2_OP_LOCAL_NOTIFICATION:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_LOCAL_NOTIFICATION_SUPPORTED:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_LOCAL_NOTIFICATION_CANCEL:
    case CS2_OP_LOCAL_NOTIFICATION_CANCELALL:
        return CS2VM_EXECNO_OK;
    default:
        fprintf(stderr, "exec_local_notification: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Minimap zoom controls (7250..7254). The zoom is host-owned (round-trip: a
 * SETZOOM is read back by GETZOOM), the port having no minimap-zoom render path
 * yet. SETZOOMABLE and SETICONZOOMLIMIT have no backing state — accepted and
 * dropped (request.value is there for when they gain a render effect).
 */
static int
exec_minimap(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_Minimap request)
{
    switch( request.opcode )
    {
    case CS2_OP_MINIMAP_SETZOOM:
        host->minimap_zoom = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_MINIMAP_GETZOOM:
        return CS2VM2_PushInt(thread, host->minimap_zoom);
    case CS2_OP_MINIMAP_SETZOOMABLE:
    case CS2_OP_MINIMAP_SETICONZOOMLIMIT:
        return CS2VM_EXECNO_OK;
    default:
        fprintf(stderr, "exec_minimap: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Viewport FOV/zoom (6200..6205). The host owns both value/max pairs — a
 * SETFOV/SETZOOM (or CLAMPFOV) round-trips through the matching GET, unlike
 * before this opcode was host-routed, when GETFOV/GETZOOM answered a hardcoded
 * constant no SET could ever change. CLAMPFOV's exact arg order is inferred
 * (value, min, max, unused) from its established (4,0,0,0) stack signature —
 * there's no reference decompile to confirm it against.
 */
static int
exec_viewport(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_Viewport request)
{
    switch( request.opcode )
    {
    case CS2_OP_VIEWPORT_SETFOV:
        host->viewport_fov = request.args[0];
        host->viewport_fov_max = request.args[1];
        return CS2VM_EXECNO_OK;
    case CS2_OP_VIEWPORT_GETFOV:
    {
        int result = CS2VM2_PushInt(thread, host->viewport_fov);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, host->viewport_fov_max);
    }
    case CS2_OP_VIEWPORT_SETZOOM:
        host->viewport_zoom = request.args[0];
        host->viewport_zoom_max = request.args[1];
        return CS2VM_EXECNO_OK;
    case CS2_OP_VIEWPORT_GETZOOM:
    {
        int result = CS2VM2_PushInt(thread, host->viewport_zoom);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, host->viewport_zoom_max);
    }
    case CS2_OP_VIEWPORT_CLAMPFOV:
    {
        int value = request.args[0];
        int min = request.args[1];
        int max = request.args[2];
        if( value < min )
            value = min;
        if( value > max )
            value = max;
        host->viewport_fov = value;
        host->viewport_fov_max = max;
        return CS2VM_EXECNO_OK;
    }
    default:
        fprintf(stderr, "exec_viewport: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/* UI zoom (6210..6214). SET/GET/RESET are host-owned state; GETDEFAULT answers
 * the fixed RS_CS2_UIZOOM_DEFAULT constant without touching that state. */
static int
exec_uizoom(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_UiZoom request)
{
    switch( request.opcode )
    {
    case CS2_OP_UIZOOM_SET:
        host->ui_zoom = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GET:
        return CS2VM2_PushInt(thread, host->ui_zoom);
    case CS2_OP_UIZOOM_RESET:
        host->ui_zoom = RS_CS2_UIZOOM_DEFAULT;
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GETDEFAULT:
        return CS2VM2_PushInt(thread, RS_CS2_UIZOOM_DEFAULT);
    default:
        fprintf(stderr, "exec_uizoom: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/* Safe-area bounds (6220..6223, 6231). Desktop client, no notch/home indicator:
 * min corners are 0, max corners are the live canvas size (same source as
 * GETCANVASSIZE / VIEWPORT_GETEFFECTIVESIZE). GETMAXY_ALT (6231) is the same
 * value as GETMAXY per the "alternative opcode used in some contexts" note. */
static int
exec_safearea(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_SafeArea request)
{
    switch( request.opcode )
    {
    case CS2_OP_SAFEAREA_GETMINX:
    case CS2_OP_SAFEAREA_GETMINY:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_SAFEAREA_GETMAXX:
        return CS2VM2_PushInt(thread, thread->canvas_w);
    case CS2_OP_SAFEAREA_GETMAXY:
    case CS2_OP_SAFEAREA_GETMAXY_ALT:
        return CS2VM2_PushInt(thread, thread->canvas_h);
    default:
        fprintf(stderr, "exec_safearea: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

static int
exec_enum_output_count(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_EnumGetOutputCount request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Enum* e = provider ? CacheProvider_EnumGet(provider, request.enum_id) : NULL;
    if( !e )
    {
        /* Same unloadable-id rule as exec_enum_lookup: a negative id has no
         * archive to wait for. */
        if( request.enum_id >= 0 )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT;
            req.u.enum_get_output_count = request;
            if( !rs_cs2_await_spent(host, req.kind, request.enum_id, -1) )
                return rs_cs2_yield_load(host, &req, request.enum_id, -1);
        }
        /* Enum still missing after its load: an empty enum has no outputs. */
        return CS2VM2_PushInt(thread, 0);
    }
    return CS2VM2_PushInt(thread, e->count);
}

static int
exec_struct_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_StructParam request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Struct* s =
        provider ? CacheProvider_StructGet(provider, request.struct_id) : NULL;
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;

    /* Both configs are needed: the struct carries the value, the ParamType
     * decides string-vs-int and supplies the default the struct may omit. One
     * yield loads both. struct -1 ("no struct") is a valid script input — an
     * enum lookup that misses pushes -1 straight into struct_param — so it is
     * never awaited, it just falls through to the param default. */
    if( (!s && request.struct_id >= 0) || (!param && request.param_id >= 0) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
        req.u.struct_param = request;
        if( !rs_cs2_await_spent(host, req.kind, request.struct_id, request.param_id) )
            return rs_cs2_yield_load(host, &req, request.struct_id, request.param_id);
        /* Still missing after the load: complete with whatever did arrive. */
    }

    found = s && rs_cs2_struct_param_lookup(s, request.param_id, &is_string, &intval, &strval);
    if( param && param->is_string )
    {
        if( found && strval )
            return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, strval));
        return CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, param->default_string ? param->default_string : ""));
    }
    if( found && is_string )
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, strval ? strval : ""));
    if( found )
        return CS2VM2_PushInt(thread, intval);
    return CS2VM2_PushInt(thread, param ? param->default_int : 0);
}

/*
 * CC_GETCOMPONENTPARAM (1703): read a component's runtime param table.
 *
 * A miss is the common case, not an error — the table starts empty and only
 * CC_SETCOMPONENTPARAM fills it — and it answers with the ParamType's own
 * default, which is what the scripts' `= -1` guards are testing for. That is the
 * one thing here that can need a load, hence the yield. Unlike STRUCT_PARAM this
 * never pushes a string: the opcode's arity is int-out, so a string-typed param
 * (which no read site in cache.osrs239 asks for) answers with 0 rather than
 * unbalancing the caller's stack.
 */
static int
exec_cc_getcomponentparam(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_ComponentParam request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int value = 0;
    if( tree && UITree_ComponentParamGet(tree, request.component_id, request.param_id, &value) )
        return CS2VM2_PushInt(thread, value);

    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;
    if( !param && request.param_id >= 0 )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM;
        req.u.cc_component_param = request;
        if( !rs_cs2_await_spent(host, req.kind, -1, request.param_id) )
            return rs_cs2_yield_load(host, &req, -1, request.param_id);
        /* Still missing after the load: 0 is the answer a param-less id gets. */
    }
    return CS2VM2_PushInt(thread, param && !param->is_string ? param->default_int : 0);
}

/*
 * IF_GETCOMPONENTPARAM (2703): the same table, for a component named by
 * argument, with the caller's own answer for a miss.
 *
 * Unlike CC_GETCOMPONENTPARAM this never consults the ParamType's default and
 * so never yields: the script supplied the value it wants back, which is the
 * whole point of the third argument. `request.value` carries it.
 */
static int
exec_if_getcomponentparam(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_ComponentParam request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int value = 0;

    if( tree && UITree_ComponentParamGet(tree, request.component_id, request.param_id, &value) )
        return CS2VM2_PushInt(thread, value);
    return CS2VM2_PushInt(thread, request.value);
}

static int
exec_oc_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Param request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;

    /* Objtype and ParamType both feed the answer, so one yield loads both (see
     * exec_struct_param). item -1 (empty slot) is a valid script input and is
     * never awaited — the param default answers it. */
    if( (!obj && request.item_id >= 0) || (!param && request.param_id >= 0) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_PARAM;
        req.u.oc_param = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, request.param_id) )
            return rs_cs2_yield_load(host, &req, request.item_id, request.param_id);
        /* Still missing after the load: complete with whatever did arrive. */
    }

    found = obj && rs_cs2_obj_param_lookup(obj, request.param_id, &is_string, &intval, &strval);
    if( param && param->is_string )
    {
        if( found && strval )
            return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, strval));
        return CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, param->default_string ? param->default_string : ""));
    }
    if( found )
        return CS2VM2_PushInt(thread, intval);
    return CS2VM2_PushInt(thread, param ? param->default_int : 0);
}

static int
exec_oc_int_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_IntParam request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    int value = 0;

    if( request.item_id < 0 )
        return CS2VM2_PushInt(thread, 0);

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_INT_PARAM;
        req.u.oc_int_param = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: answer like the empty slot. */
        return CS2VM2_PushInt(thread, 0);
    }

    switch( request.field )
    {
    case CS2VM_OC_INT_COST:
        value = obj->cost;
        break;
    case CS2VM_OC_INT_STACKABLE:
        value = obj->stackable;
        break;
    case CS2VM_OC_INT_MEMBERS:
        /* Stub: members flag not on ToriRS_Objtype yet. */
        value = 0;
        break;
    case CS2VM_OC_INT_ID:
        value = obj->id;
        break;
    }
    return CS2VM2_PushInt(thread, value);
}

static int
exec_oc_name(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    char const* name = "null";

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_NAME;
        req.u.oc_name = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: the reference "null" name. */
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
    }

    if( obj->name[0] != '\0' )
        name = obj->name;
    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
}

static int
exec_nc_name(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_NC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Npctype* npc =
        provider ? CacheProvider_NpctypeGet(provider, request.npc_id) : NULL;
    char const* name = "null";

    if( request.npc_id < 0 )
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));

    if( !npc )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_NC_NAME;
        req.u.nc_name = request;
        if( !rs_cs2_await_spent(host, req.kind, request.npc_id, -1) )
            return rs_cs2_yield_load(host, &req, request.npc_id, -1);
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
    }

    if( npc->name[0] != '\0' )
        name = npc->name;
    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
}

static int
exec_oc_unplaceholder(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);

    /* item -1 (empty slot) is a valid script input: never yield for it — the
     * yield planner requires a loadable id — and there is nothing to resolve,
     * so pass the id straight through. */
    if( request.item_id < 0 || (provider && CacheProvider_ObjtypeHas(provider, request.item_id)) )
        return CS2VM2_PushInt(thread, request.item_id);

    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER;
        req.u.oc_unplaceholder = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: pass the id through unresolved. */
        return CS2VM2_PushInt(thread, request.item_id);
    }
}

/* OC_OP/OC_IOP: ground/inventory right-click action string at a menu slot
 * (op_index 0..4). Real data, following the exact OC_NAME yield-on-miss shape. */
static int
exec_oc_op(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Op request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind =
            request.opcode == CS2_OP_OC_IOP ? CS2VM_HOST_REQUEST_OC_IOP : CS2VM_HOST_REQUEST_OC_OP;
        req.u.oc_op = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: no action string to give. */
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
    }

    if( request.op_index < 0 || request.op_index >= TORIRS_MENU_ACTION_SLOTS )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));

    char const* action = request.opcode == CS2_OP_OC_IOP ? obj->inv_actions[request.op_index]
                                                         : obj->ground_actions[request.op_index];
    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, action ? action : ""));
}

/* OC_EXAMINE: real data (ToriRS_Objtype.desc), following the OC_NAME shape. */
static int
exec_oc_examine(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_EXAMINE;
        req.u.oc_examine = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
    }

    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, obj->desc[0] != '\0' ? obj->desc : ""));
}

/* OC_PLACEHOLDER: identity passthrough, mirroring OC_UNPLACEHOLDER — no
 * placeholder linkage data exists on ToriRS_Objtype yet. */
static int
exec_oc_placeholder(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    return CS2VM2_PushInt(thread, request.item_id);
}

/* OC_FIND needs every objtype name resident to scan. The dat2 provider can
 * bulk-load the whole obj group in one task; a provider that cannot (dat1)
 * reports ready immediately and the search runs over whatever is cached. */
static bool
rs_cs2_objtypes_ready(struct RS_CS2Host* host)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    if( !provider )
        return true;
    if( provider->objtypes_all_loaded )
        return true;
    if( !provider->vtable || !provider->vtable->Task_ObjLoadAll )
        return true;
    return false;
}

static void
rs_cs2_item_search_clear(struct RS_CS2Host* host)
{
    free(host->item_search_results);
    host->item_search_results = NULL;
    host->item_search_count = 0;
    host->item_search_cap = 0;
    host->item_search_index = 0;
}

/* OC_FIND/OC_FINDNEXT/OC_FINDRESET: a stateful item-name search. FIND scans
 * every resident objtype for a lowercased name substring (yielding once to
 * bulk-load the obj group first), FINDNEXT walks the matches in ascending id
 * order, FINDRESET clears them. */
static int
exec_oc_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Find request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);

    if( request.opcode == CS2_OP_OC_FINDRESET )
    {
        rs_cs2_item_search_clear(host);
        return CS2VM_EXECNO_OK;
    }

    if( request.opcode == CS2_OP_OC_FINDNEXT )
    {
        int next_id = -1;
        if( host->item_search_index < host->item_search_count )
            next_id = host->item_search_results[host->item_search_index++];
        return CS2VM2_PushInt(thread, next_id);
    }

    /* OC_FIND: start a fresh search, discarding any previous results. */
    rs_cs2_item_search_clear(host);

    if( request.query && request.query[0] != '\0' )
    {
        char lower[256];
        size_t qidx = 0;

        /* The scan needs every objtype name in memory; load the whole group
         * once (one yield), then search on the retry. */
        if( !rs_cs2_objtypes_ready(host) )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_OC_FIND;
            req.u.oc_find = request;
            if( !rs_cs2_await_spent(host, req.kind, -1, -1) )
                return rs_cs2_yield_load(host, &req, -1, -1);
            /* Awaited but still not fully loaded (load failed / unsupported):
             * search whatever is resident rather than yield a second time. */
        }

        /* Lowercase the query (reference: query.toLowerCase()); provider-side
         * the objtype names are lowercased per entry for the substring match. */
        for( ; request.query[qidx] != '\0' && qidx + 1 < sizeof(lower); qidx++ )
        {
            char ch = request.query[qidx];
            if( ch >= 'A' && ch <= 'Z' )
                ch = (char)(ch - 'A' + 'a');
            lower[qidx] = ch;
        }
        lower[qidx] = '\0';

        host->item_search_count =
            CacheProvider_ObjtypeSearchByName(provider, lower, &host->item_search_results);
        host->item_search_cap = host->item_search_count;
        host->item_search_index = 0;
    }

    return CS2VM2_PushInt(thread, host->item_search_count);
}

/* OC_SHIFTCLICKIOP: no per-item shift-click preference data exists yet. */
static int
exec_oc_shiftclickiop(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_WEARPOS/WEARPOS2/WEARPOS3: no equip slot data exists on ToriRS_Objtype
 * yet, so every variant answers "not equippable". */
static int
exec_oc_wearpos(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_WearPos request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_WEIGHT: no weight data exists on ToriRS_Objtype yet. */
static int
exec_oc_weight(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    (void)request;
    return CS2VM2_PushInt(thread, 0);
}

/* oc_isubop(obj, opIndex, subIndex) -> string. No sub-menu nesting exists on
 * ToriRS_Objtype yet. */
static int
exec_oc_isubop(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Isubop request)
{
    (void)request;
    return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
}

static int
exec_set_graphic(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    struct UITree* tree = rs_cs2_tree(host);
    (void)thread;

    if( getenv("TORIRS_OBJICON_DEBUG") )
        fprintf(
            stderr,
            "GFXDBG: com=0x%08x gfx=%d\n",
            (unsigned)request.component_id,
            request.graphic_id);

    if( request.graphic_id >= 0 && !rs_cs2_sprite_ready(host, request.graphic_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
        req.u.cc_set_graphic = request;
        if( !rs_cs2_await_spent(host, req.kind, request.graphic_id, -1) )
            return rs_cs2_yield_load(host, &req, request.graphic_id, -1);
        /* Sprite still missing after its load: clear the graphic. */
        (void)UITree_ApplyGraphic(tree, request.component_id, -1, 0);
        return CS2VM_EXECNO_OK;
    }

    /* Upload to scene then store scene element id on the node. */
    {
        int scene_id = request.graphic_id;
        if( host->bridge && request.graphic_id >= 0 && request.graphic_id < 1000000 )
            scene_id = UITreeSceneBridge_EnsureSprite(host->bridge, request.graphic_id);
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETGRAPHIC component_id=%d graphic_id=%d scene_id=%d\n",
            request.component_id,
            request.graphic_id,
            scene_id);
#endif
        (void)UITree_ApplyGraphic(tree, request.component_id, scene_id, 0);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_set_object(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int component_id,
    int obj_id,
    int count,
    int num_mode)
{
    struct UITree* tree = rs_cs2_tree(host);
    struct CacheProvider* provider = rs_cs2_provider(host);
    int scene_id = -1;
    int atlas_index = 0;
    (void)thread;
    if( getenv("TORIRS_OBJICON_DEBUG") )
        fprintf(
            stderr,
            "OBJICON: enter com=0x%08x obj=%d count=%d bridge=%d prov=%d needs=%d\n",
            (unsigned)component_id,
            obj_id,
            count,
            host->bridge ? 1 : 0,
            provider ? 1 : 0,
            provider ? ObjModelLoad_NeedsWork(provider, obj_id, count) : -1);

    if( obj_id <= 0 )
    {
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETOBJECT component_id=%d obj_id=%d count=%d (clear)\n",
            component_id,
            obj_id,
            count);
#endif
        (void)UITree_ApplyObject(tree, component_id, 0, 0, -1, 0, 0);
        return CS2VM_EXECNO_OK;
    }

    /* The icon needs the objtype, its count variant, the inventory model and
     * that model's textures. Task_ObjModelLoad fetches all of them, so ask it
     * once whether anything is missing rather than yielding per piece. */
    if( !provider || ObjModelLoad_NeedsWork(provider, obj_id, count) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
        req.u.cc_set_object.component_id = component_id;
        req.u.cc_set_object.obj_id = obj_id;
        req.u.cc_set_object.count = count;
        if( provider && !rs_cs2_await_spent(host, req.kind, obj_id, count) )
            return rs_cs2_yield_load(host, &req, obj_id, count);
        if( !provider )
        {
            (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0, num_mode);
            return CS2VM_EXECNO_OK;
        }
        /* Still incomplete after the load — a texture that failed, say. Build
         * the icon from what did arrive; the raster skips missing faces. */
    }

    if( host->bridge )
        scene_id = UITreeSceneBridge_EnsureObjIcon(host->bridge, obj_id, count);
    else
        (void)rs_cs2_resolve_obj_icon(host, obj_id, &scene_id, &atlas_index);

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETOBJECT component_id=%d obj_id=%d count=%d scene_id=%d\n",
        component_id,
        obj_id,
        count,
        scene_id);
#endif
    if( getenv("TORIRS_OBJICON_DEBUG") )
    {
        int32_t dbg = UITree_FindByComponentId(tree, component_id);
        fprintf(
            stderr,
            "OBJICON: apply com=0x%08x obj=%d scene=%d idx=%d type=%d\n",
            (unsigned)component_id,
            obj_id,
            scene_id,
            (int)dbg,
            dbg >= 0 ? (int)tree->components[dbg].type : -1);
    }
    (void)UITree_ApplyObject(tree, component_id, obj_id, count, scene_id, atlas_index, num_mode);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_text_font(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_SetTextFont request)
{
    (void)thread;

    if( request.font_id >= 0 && !rs_cs2_font_ready(host, request.font_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
        req.u.cc_set_text_font = request;
        if( !rs_cs2_await_spent(host, req.kind, request.font_id, -1) )
            return rs_cs2_yield_load(host, &req, request.font_id, -1);
        /* Font still missing after its load: leave the node without one. */
        (void)UITree_ApplyTextFont(rs_cs2_tree(host), request.component_id, -1);
        return CS2VM_EXECNO_OK;
    }

    {
        int font_id = request.font_id;
        if( host->bridge && font_id >= 0 )
            font_id = UITreeSceneBridge_EnsureFont(host->bridge, font_id);
        (void)UITree_ApplyTextFont(rs_cs2_tree(host), request.component_id, font_id);
    }
    return CS2VM_EXECNO_OK;
}

/* CC_COPY clones an existing dynamic child into another slot. The bank tab
 * strip (script 505) builds tab 0 with CC_CREATE then copies it into slots
 * 1..9; without this the whole strip collapses onto the one created tab. */
static int
exec_cc_copy(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_CC_Copy request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int const parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_COPY;
    yield_req.u.cc_copy = request;
    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    assert(tree);

    /* Group is mounted; a parent that still isn't there cannot be loaded in. */
    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_OK;

    child_idx = UITree_CcCopy(tree, parent_idx, parent_id, request.src_sub_id, request.dst_sub_id);
    if( child_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_create(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_CC_Create request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    yield_req.u.cc_create = request;
    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    assert(tree);

    /* Group is mounted; a parent that still isn't there cannot be loaded in. */
    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_OK;

    child_idx =
        UITree_CcCreate(tree, parent_idx, parent_id, request.component_type, request.child_index);
    if( child_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    /* Leave size 0; scripts call CC_SETSIZE when needed. Soft3D uses native
     * sprite size when layout w/h are 0 — do not stretch 32x32 icons to the
     * parent slot (that thickens obj-icon outlines). */

    if( getenv("TORIRS_CC_DEBUG") )
        fprintf(
            stderr,
            "CC_CREATE parent=%d|%d sub=%d -> com=0x%08x script=%d\n",
            (parent_id >> 16) & 0xffff,
            parent_id & 0xffff,
            request.child_index,
            (unsigned)tree->components[child_idx].component_id,
            vm && vm->frame_sp > 0 && CS2VM_FRAME(vm)->script
                ? CS2VM_FRAME(vm)->script->script_id
                : -1);

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: CC_CREATE parent_id=%d child_id=%d type=%d idx=%d size=%dx%d\n",
        parent_id,
        tree->components[child_idx].component_id,
        request.component_type,
        (int)child_idx,
        tree->components[child_idx].position.width,
        tree->components[child_idx].position.height);
#endif

    rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_CC_Find request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };
    int32_t parent_idx;

    yield_req.kind = CS2VM_HOST_REQUEST_CC_FIND;
    yield_req.u.cc_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, request.parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    if( tree )
    {
        parent_idx = UITree_FindByComponentId(tree, request.parent_id);
        if( parent_idx >= 0 )
        {
            int32_t child_idx =
                UITree_FindChildBySubid(tree, parent_idx, request.parent_id, request.sub_id);
            if( child_idx >= 0 )
            {
                rs_cs2_set_cc_target(
                    vm, request.dot_operand, tree->components[child_idx].component_id);
                found = 1;
            }
        }
        /* Group is mounted; an absent parent means not-found, not another load. */
    }

    return CS2VM2_PushInt(vm, found);
}

static int
exec_if_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_TargetFind request)
{
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_IF_FIND;
    yield_req.u.if_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, request.component_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    /* Group is mounted; an absent component means not-found, not another load. */
    if( rs_cs2_find_node(host, request.component_id) >= 0 )
    {
        rs_cs2_set_cc_target(vm, request.dot_operand, request.component_id);
        found = 1;
    }

    return CS2VM2_PushInt(vm, found);
}

static int
exec_children_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int parent_id,
    int start_index,
    int set_target_dot,
    int dot_operand,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = rs_cs2_tree(host);
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = kind;
    if( kind == CS2VM_HOST_REQUEST_CC_CHILDREN_FIND )
    {
        yield_req.u.cc_children_find.parent_id = parent_id;
        yield_req.u.cc_children_find.start_index = start_index;
    }
    else
    {
        yield_req.u.if_children_find.uid = parent_id;
        yield_req.u.if_children_find.start_index = start_index;
        yield_req.u.if_children_find.dot_operand = dot_operand;
    }

    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    /* Group is mounted; an absent parent simply has no children to iterate. */
    CS2VM2_ResetChildrenIter(vm);
    vm->children_iter_parent = parent_id;
    if( tree )
    {
        vm->children_iter_count = UITree_CollectDynamicChildIndices(
            tree, parent_id, start_index, vm->children_iter_indices, CS2VM2_CHILDREN_ITER_MAX);

        if( set_target_dot && UITree_FindByComponentId(tree, parent_id) >= 0 )
            rs_cs2_set_cc_target(vm, dot_operand, parent_id);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetModel request)
{
    (void)vm;
    if( request.model_id >= 0 && !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
        req.u.widget_set_model = request;
        if( !rs_cs2_await_spent(host, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, &req, request.model_id, -1);
        /* Model still missing after its load: leave the widget as it was. */
        return CS2VM_EXECNO_OK;
    }
    if( rs_cs2_tree(host) )
    {
        int scene_model = request.model_id;
        if( host->bridge && scene_model >= 0 )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_kind(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetModelKind request)
{
    (void)vm;
#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SET_MODEL_KIND component_id=%d kind=%d model_id=%d\n",
        request.component_id,
        (int)request.model_kind,
        request.model_id);
#endif
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && request.model_id >= 0 &&
        !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
        req.u.widget_set_model_kind = request;
        if( !rs_cs2_await_spent(host, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, &req, request.model_id, -1);
        /* Model still missing after its load: leave the widget as it was. */
        return CS2VM_EXECNO_OK;
    }
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && rs_cs2_tree(host) )
    {
        int scene_model = request.model_id;
        if( host->bridge && scene_model >= 0 )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    /* NPC head (kind 2): request.model_id is the npc id. Composite the chathead
     * (reference IfType.getModel type 2 / NpcType.getHead / deob method3601).
     * Yield until npctype + head models are resident (IF1 Task_AppIfHead parity);
     * EnsureNpcHead returns -1 (widget unchanged) if composition still fails. */
    else if(
        request.model_kind == CS2VM_MODEL_KIND_NPC_HEAD && host->bridge && rs_cs2_tree(host) &&
        request.model_id >= 0 )
    {
        if( !rs_cs2_npc_head_ready(host, request.model_id) )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
            req.u.widget_set_model_kind = request;
            if( !rs_cs2_await_spent(host, req.kind, request.model_id, -1) )
                return rs_cs2_yield_load(host, &req, request.model_id, -1);
            /* Npctype/heads still missing after load: leave the widget as it was. */
            return CS2VM_EXECNO_OK;
        }
        {
            int scene_model =
                UITreeSceneBridge_EnsureNpcHead(host->bridge, request.model_id);
            if( scene_model >= 0 )
                (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
        }
    }
    /* Player head/self/chathead (kinds 3/5/6): composite the local appearance
     * head (reference IfType.getModel type 3 / ClientPlayer.getHeadModel). */
    else if(
        (request.model_kind == CS2VM_MODEL_KIND_PLAYER_HEAD ||
         request.model_kind == CS2VM_MODEL_KIND_PLAYER_SELF ||
         request.model_kind == CS2VM_MODEL_KIND_PLAYER_CHATHEAD) &&
        host->bridge && rs_cs2_tree(host) )
    {
        /* The CS2 host has no world handle, so it can only bind an already
         * composited player head (cache hit). The IF1 IF_SETPLAYERHEAD path
         * (App-driven) is what composites it from the real appearance. */
        int scene_model = UITreeSceneBridge_EnsurePlayerHead(host->bridge, NULL, NULL, 0);
        if( scene_model >= 0 )
            (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_int(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetInt request)
{
    struct UITreeComponent* node = rs_cs2_node(host, request.component_id);
    int32_t idx;
    (void)vm;

    if( !node )
    {
        /* Scripts set properties on other groups (e.g. interface 100's search
         * button targets chatbox 162:36). Sub-mount the group; once it is
         * baked, a still-missing child is a no-op (reference tolerates sets on
         * absent widgets). */
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_INT;
        req.u.widget_set_int = request;
        return rs_cs2_yield_if_group_missing(host, request.component_id, &req);
    }

    idx = rs_cs2_find_node(host, request.component_id);

    switch( request.field )
    {
    case CS2VM_WIDGET_INT_HFLIP:
        if( node->type == UIELEM_RS_GRAPHIC )
            node->u.rs_graphic.flip_h = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_VFLIP:
        if( node->type == UIELEM_RS_GRAPHIC )
            node->u.rs_graphic.flip_v = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_FILL_COLOUR:
        (void)UITree_ApplyFillColour(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_LINE_WIDTH:
        if( node->type == UIELEM_RS_LINE )
            node->u.rs_line.line_width = request.value;
        break;
    case CS2VM_WIDGET_INT_LINE_DIRECTION:
        if( node->type == UIELEM_RS_LINE )
            node->u.rs_line.horizontal = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_NO_CLICK_THROUGH:
        node->no_click_through = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_CLICKMASK:
        (void)UITree_ApplyClickMask(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_FORCE_LEFT_CLICK:
        (void)UITree_ApplyForceLeftClick(
            rs_cs2_tree(host), request.component_id, request.value == 1);
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        node->drag_dead_zone = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        node->drag_dead_time = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        (void)UITree_ApplyModelTransparent(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_MODEL_ANIM:
        /* Sequence id for a model widget. The client tick driver loads the
         * sequence and advances/applies frames to the model. -1 clears.
         *
         * Re-setting the sequence already running leaves the frame counters
         * alone, for the same reason UITree_ApplyModelAnim does: a script that
         * re-states an unchanged anim (an onvartransmit hook re-running, say)
         * must not restart the animation. */
        if( node->type == UIELEM_RS_MODEL && node->u.rs_model.anim_seq_id != request.value )
        {
            node->u.rs_model.anim_seq_id = request.value;
            node->u.rs_model.anim_frame = 0;
            node->u.rs_model.anim_frame_cycle = 0;
        }
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
    case CS2VM_WIDGET_INT_ANGLE_2D:
    case CS2VM_WIDGET_INT_FILL_MODE:
    case CS2VM_WIDGET_INT_TRANS_BOT:
    case CS2VM_WIDGET_INT_NO_SCROLL_THROUGH:
    case CS2VM_WIDGET_INT_PINCH:
    case CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON:
        /* UITree lacks these fields; accept no-op. */
        break;
    default:
        break;
    }
    if( idx >= 0 )
        UITree_MarkNodeDirty(rs_cs2_tree(host), idx);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_angle(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetModelAngle request)
{
    (void)vm;
    (void)UITree_ApplyModelAngle(
        rs_cs2_tree(host), request.component_id, request.angle_x, request.angle_y, request.zoom);
    return CS2VM_EXECNO_OK;
}

/* Acquire the inv-transmit hook slot for component_id. Re-registration for the
 * same component reuses its entry (the new script supersedes the old) while
 * preserving last_seen_serial — a transmit script re-registering itself must not
 * re-arm and re-fire every pump (TS parity: reassigning node.onInvTransmit does
 * not reset lastChangedInvCount).
 *
 * Dead entries (component reclaimed by CC_CREATE/CC_DELETEALL) are compacted
 * before appending: an interface reopen re-runs onload, which often
 * deleteall+create with fresh dynamic uids and re-registers — without compacting
 * on every grow the array climbed until MAX and only then purged. */
static void
rs_cs2_compact_inv_transmit_hooks(struct RS_CS2Host* host)
{
    int w = 0;
    for( int i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        if( UITree_FindByComponentId(host->tree, host->inv_transmit_hooks[i].component_id) < 0 )
            continue;
        if( w != i )
            host->inv_transmit_hooks[w] = host->inv_transmit_hooks[i];
        w++;
    }
    host->inv_transmit_hook_count = w;
}

static struct RS_CS2InvTransmitHook*
rs_cs2_acquire_inv_transmit_hook(
    struct RS_CS2Host* host,
    int component_id)
{
    int i;
    struct RS_CS2InvTransmitHook* hook;

    for( i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        hook = &host->inv_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            return hook;
        }
    }

    rs_cs2_compact_inv_transmit_hooks(host);

    if( host->inv_transmit_hook_count >= RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX )
    {
        /* Say so, once. A dropped registration is invisible at the drop:
         * the script that asked carries on, the panel draws, and the bug
         * only shows up later as a panel that never updates. */
        static int warned;
        if( !warned )
        {
            warned = 1;
            fprintf(stderr,
                    "cs2 host: inv-transmit hooks full (%d); component 0x%08x will "
                    "never update\n",
                    RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX, (unsigned)component_id);
        }
        return NULL;
    }

    hook = &host->inv_transmit_hooks[host->inv_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    return hook;
}

/* Var-transmit counterpart of rs_cs2_acquire_inv_transmit_hook. */
static void
rs_cs2_compact_var_transmit_hooks(struct RS_CS2Host* host)
{
    int w = 0;
    for( int i = 0; i < host->var_transmit_hook_count; i++ )
    {
        if( UITree_FindByComponentId(host->tree, host->var_transmit_hooks[i].component_id) < 0 )
            continue;
        if( w != i )
            host->var_transmit_hooks[w] = host->var_transmit_hooks[i];
        w++;
    }
    host->var_transmit_hook_count = w;
}

static struct RS_CS2VarTransmitHook*
rs_cs2_acquire_var_transmit_hook(
    struct RS_CS2Host* host,
    int component_id)
{
    int i;
    struct RS_CS2VarTransmitHook* hook;

    for( i = 0; i < host->var_transmit_hook_count; i++ )
    {
        hook = &host->var_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            return hook;
        }
    }

    rs_cs2_compact_var_transmit_hooks(host);

    if( host->var_transmit_hook_count >= RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX )
    {
        static int warned;
        if( !warned )
        {
            warned = 1;
            fprintf(stderr,
                    "cs2 host: var-transmit hooks full (%d); component 0x%08x will "
                    "never update\n",
                    RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX, (unsigned)component_id);
        }
        return NULL;
    }

    hook = &host->var_transmit_hooks[host->var_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    return hook;
}

/* Copy hook string args (mask + fixed buffers) from a SetOn request. Both
 * request and hook use the CS2VM_SETON_STR_ARG_* layout. */
#define RS_CS2_COPY_HOOK_STR_ARGS(hook, request)                                                   \
    do                                                                                             \
    {                                                                                              \
        (hook)->str_arg_mask = (request)->str_arg_mask;                                            \
        (hook)->str_arg_count = (request)->str_arg_count;                                          \
        if( (hook)->str_arg_count > CS2VM_SETON_STR_ARG_MAX )                                      \
            (hook)->str_arg_count = CS2VM_SETON_STR_ARG_MAX;                                       \
        memcpy((hook)->str_args, (request)->str_args, sizeof((hook)->str_args));                   \
    } while( 0 )

static int
exec_set_on_inv_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit const* request)
{
    struct RS_CS2InvTransmitHook* hook;
    assert(host);
    assert(request);
    hook = rs_cs2_acquire_inv_transmit_hook(host, request->component_id);
    if( !hook )
    {
        fprintf(
            stderr,
            "rs_cs2_host: inv_transmit_hooks full (%d), dropping script_id=%d component_id=%d\n",
            RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX,
            request->script_id,
            request->component_id);
        return CS2VM_EXECNO_OK;
    }
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON IF_SETONINVTRANSMIT component_id=%d script_id=%d argc=%d "
        "triggers=%d",
        request->component_id,
        request->script_id,
        request->int_arg_count,
        request->trigger_count);
    {
        int ti;
        for( ti = 0; ti < hook->trigger_count; ti++ )
            fprintf(stderr, "%s%d", ti == 0 ? " [" : ",", hook->trigger_ids[ti]);
        if( hook->trigger_count > 0 )
            fprintf(stderr, "]");
    }
    fprintf(stderr, "\n");
#endif
    return CS2VM_EXECNO_OK;
}

/*
 * The stat-transmit registry's acquire, mirroring the var one exactly.
 *
 * Same one-hook-per-component rule and same `last_seen_serial` carry-over: a
 * re-registration must not make a hook fire again for a serial it has already
 * seen, or the XP-drop script would replay its whole queue every time the
 * gameframe re-armed it.
 */
static void
rs_cs2_compact_stat_transmit_hooks(struct RS_CS2Host* host)
{
    int w = 0;
    for( int i = 0; i < host->stat_transmit_hook_count; i++ )
    {
        if( UITree_FindByComponentId(host->tree, host->stat_transmit_hooks[i].component_id) < 0 )
            continue;
        if( w != i )
            host->stat_transmit_hooks[w] = host->stat_transmit_hooks[i];
        w++;
    }
    host->stat_transmit_hook_count = w;
}

static struct RS_CS2StatTransmitHook*
rs_cs2_acquire_stat_transmit_hook(
    struct RS_CS2Host* host,
    int component_id)
{
    int i;
    struct RS_CS2StatTransmitHook* hook;

    for( i = 0; i < host->stat_transmit_hook_count; i++ )
    {
        hook = &host->stat_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            return hook;
        }
    }

    /* Compact dead entries (closed/rebuilt interface left hooks behind) before
     * appending — same as inv/var acquire. */
    rs_cs2_compact_stat_transmit_hooks(host);

    if( host->stat_transmit_hook_count >= RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX )
    {
        static int warned;
        if( !warned )
        {
            warned = 1;
            fprintf(stderr,
                    "cs2 host: stat-transmit hooks full (%d); component 0x%08x will "
                    "never update\n",
                    RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX, (unsigned)component_id);
        }
        return NULL;
    }

    hook = &host->stat_transmit_hooks[host->stat_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    return hook;
}

/* True when `idx` is part of interface `group_id`: its own packed id matches,
 * or an ancestor's does (dynamic cc_create children use 0x8000|n uids). */
static int
rs_cs2_component_in_interface_group(
    struct UITree const* tree,
    int32_t idx,
    int group_id)
{
    while( idx >= 0 && (uint32_t)idx < tree->component_count )
    {
        struct UITreeComponent const* c = &tree->components[idx];
        if( !c->freed && c->component_id >= 0 &&
            ((c->component_id >> 16) & 0xffff) == group_id )
            return 1;
        idx = c->parent;
    }
    return 0;
}

/* True when any click/op/drag slot is still live — those must survive IF_CLOSE
 * on a reused bake (compass on_op is installed once by gameframe onload and is
 * not re-registered when a sidebar pack remounts). */
static int
rs_cs2_hooks_have_interaction(struct UITreeRuntimeHooks const* hooks)
{
    assert(hooks);
    return hooks->on_op.script_id > 0 || hooks->on_click.script_id > 0 ||
           hooks->on_hold.script_id > 0 || hooks->on_drag.script_id > 0 ||
           hooks->on_drag_complete.script_id > 0;
}

/* Drop reactive listeners on one node. Interaction hooks stay; if nothing
 * interactive remains the whole block is freed (purely reactive closed nodes
 * still drop ~10 KB). */
static void
rs_cs2_clear_reactive_hooks_at(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent* c;
    struct UITreeRuntimeHooks* hooks;

    assert(tree);
    assert(idx >= 0 && (uint32_t)idx < tree->component_count);
    c = &tree->components[idx];
    hooks = c->runtime_hooks;
    if( !hooks )
        return;

    memset(&hooks->on_timer, 0, sizeof(hooks->on_timer));
    memset(&hooks->on_key, 0, sizeof(hooks->on_key));
    memset(&hooks->on_var_transmit, 0, sizeof(hooks->on_var_transmit));
    memset(&hooks->on_inv_transmit, 0, sizeof(hooks->on_inv_transmit));
    memset(&hooks->on_misc_transmit, 0, sizeof(hooks->on_misc_transmit));
    memset(&hooks->on_friend_transmit, 0, sizeof(hooks->on_friend_transmit));
    memset(&hooks->on_resize, 0, sizeof(hooks->on_resize));
    memset(&hooks->on_sub_change, 0, sizeof(hooks->on_sub_change));
    /* Mouse-over/leave/repeat and scroll-wheel are not interaction clicks but
     * are also not the transmit/timer/resize set ClearHooks is for; leave them.
     * Remount onLoad rewrites them when the pack owns them. */

    if( !rs_cs2_hooks_have_interaction(hooks) )
        UITree_FreeHooksAt(tree, idx);
    else
        UITree_SyncHookMembership(tree, idx);
}

/* Clear reactive hooks on `root` and same-group descendants. Uses a heap-grown
 * stack because interface trees are shallow but can be very wide (bank). */
static void
rs_cs2_clear_hooks_subtree(
    struct UITree* tree,
    int32_t root,
    int group_id)
{
    int32_t* stack = NULL;
    int sp = 0;
    int cap = 0;

    assert(tree);
    assert(root >= 0 && (uint32_t)root < tree->component_count);

    stack = (int32_t*)malloc(64 * sizeof(int32_t));
    assert(stack);
    cap = 64;
    stack[sp++] = root;

    while( sp > 0 )
    {
        int32_t cur = stack[--sp];
        int32_t child;
        int cid = tree->components[cur].component_id;
        /* Do not walk into / clear foreign-group descendants under a closed
         * root (nested mounts, or a live gameframe child that somehow nested). */
        if( cid >= 0 && ((cid >> 16) & 0xffff) == group_id &&
            tree->components[cur].runtime_hooks )
            rs_cs2_clear_reactive_hooks_at(tree, cur);
        for( child = tree->components[cur].first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int child_cid = tree->components[child].component_id;
            if( child_cid >= 0 && ((child_cid >> 16) & 0xffff) != group_id )
                continue;
            if( sp >= cap )
            {
                int ncap = cap * 2;
                int32_t* nstack =
                    (int32_t*)realloc(stack, (size_t)ncap * sizeof(int32_t));
                assert(nstack);
                stack = nstack;
                cap = ncap;
            }
            stack[sp++] = child;
        }
    }
    free(stack);
}

void
RS_CS2Host_ClearHooksForInterfaceGroup(
    struct RS_CS2Host* host,
    int group_id)
{
    struct UITree* tree;
    int w;
    int i;

    assert(host);
    if( group_id <= 0 )
        return;
    tree = host->tree;
    if( !tree )
        return;

    w = 0;
    for( i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        int32_t idx =
            UITree_FindByComponentId(tree, host->inv_transmit_hooks[i].component_id);
        if( idx < 0 || rs_cs2_component_in_interface_group(tree, idx, group_id) )
            continue;
        if( w != i )
            host->inv_transmit_hooks[w] = host->inv_transmit_hooks[i];
        w++;
    }
    host->inv_transmit_hook_count = w;

    w = 0;
    for( i = 0; i < host->var_transmit_hook_count; i++ )
    {
        int32_t idx =
            UITree_FindByComponentId(tree, host->var_transmit_hooks[i].component_id);
        if( idx < 0 || rs_cs2_component_in_interface_group(tree, idx, group_id) )
            continue;
        if( w != i )
            host->var_transmit_hooks[w] = host->var_transmit_hooks[i];
        w++;
    }
    host->var_transmit_hook_count = w;

    w = 0;
    for( i = 0; i < host->stat_transmit_hook_count; i++ )
    {
        int32_t idx =
            UITree_FindByComponentId(tree, host->stat_transmit_hooks[i].component_id);
        if( idx < 0 || rs_cs2_component_in_interface_group(tree, idx, group_id) )
            continue;
        if( w != i )
            host->stat_transmit_hooks[w] = host->stat_transmit_hooks[i];
        w++;
    }
    host->stat_transmit_hook_count = w;

    /* Drop component-local *reactive* listeners for the group. Interaction
     * hooks (click/op/drag) stay so a reused bake still responds to input —
     * the compass on_op is installed once by toplevel_init and is not rebuilt
     * when a sidebar pack closes. Purely reactive blocks are still freed.
     *
     * Walk the group's live set (plus same-group descendants under each group
     * root) instead of scanning every component with an ancestor walk. */
    {
        struct UITreeNodeSet const* gset = UITree_GroupNodes(tree, group_id);
        int gi;
        TORIRS_PERF_COUNT(
            TORIRS_PERF_CTR_IFACE_GROUP_SCAN_NODES, gset ? (int64_t)gset->count : 0);
        if( gset )
        {
            for( gi = 0; gi < gset->count; gi++ )
            {
                int32_t idx = gset->slots[gi];
                int32_t parent;
                assert(idx >= 0 && (uint32_t)idx < tree->component_count);
                /* Only start a subtree walk from group roots relative to this
                 * group — children that are also in the set are visited from
                 * their ancestor, avoiding double clears. */
                parent = tree->components[idx].parent;
                if( parent >= 0 &&
                    !tree->components[parent].freed &&
                    tree->components[parent].component_id >= 0 &&
                    ((tree->components[parent].component_id >> 16) & 0xffff) ==
                        group_id )
                    continue;
                rs_cs2_clear_hooks_subtree(tree, idx, group_id);
            }
        }
    }
}

/*
 * `if_setonstattransmit(script, args, component)` — the XP drops' listener.
 *
 * It used to be in the VM's discard group: parsed for its operands (which it has
 * to be, or the stack unwinds wrong) and then thrown away. The registry, the
 * `stat_change_serial` and `RS_CS2Host_NotifyStatChanged` all already existed —
 * UPDATE_STAT has been calling the notifier all along — so half the reactive
 * loop was built and the half that registers a listener was not. Nothing fires,
 * and there is no error: the XP drop panel simply never draws.
 *
 * The trigger list is the *stat ids* the hook cares about, the same way a var
 * hook lists varps. The XP drops list all 24, which is why they want a filter at
 * all — without one every skill change would re-run every registered hook.
 */
static int
exec_set_on_stat_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit const* request)
{
    struct RS_CS2StatTransmitHook* hook;

    assert(host);
    if( !request )
        return CS2VM_EXECNO_OK;
    hook = rs_cs2_acquire_stat_transmit_hook(host, request->component_id);
    if( !hook )
        return CS2VM_EXECNO_OK;
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_var_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit const* request)
{
    struct RS_CS2VarTransmitHook* hook;
    assert(host);
    hook = rs_cs2_acquire_var_transmit_hook(host, request->component_id);
    if( !hook )
        return CS2VM_EXECNO_OK;
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
    return CS2VM_EXECNO_OK;
}

/* CC-level transmit hooks: same registration as the IF-level ones, but the
 * component is the VM's active child and args/triggers arrive in the CC
 * request shape. Previously these opcodes were silently discarded, so
 * dynamically-built lists never refreshed on inv/var changes. */
static int
exec_set_on_cc_transmit(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp const* request)
{
    int component_id;

    assert(host);
    assert(vm);
    if( !request )
        return CS2VM_EXECNO_OK;

    /* Dot vs active register — resolved at op time in the VM (see
     * exec_set_on_cc_event). */
    component_id = request->component_id;
    if( component_id < 0 )
        return CS2VM_EXECNO_OK;

    if( kind == CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT )
    {
        struct RS_CS2InvTransmitHook* hook;
        hook = rs_cs2_acquire_inv_transmit_hook(host, component_id);
        if( !hook )
        {
            fprintf(
                stderr,
                "rs_cs2_host: inv_transmit_hooks full (%d), dropping cc script_id=%d "
                "component_id=%d\n",
                RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX,
                request->script_id,
                component_id);
            return CS2VM_EXECNO_OK;
        }
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        hook->trigger_count = request->trigger_count;
        if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
            hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
        if( request->trigger_ids && hook->trigger_count > 0 )
            memcpy(
                hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
        return CS2VM_EXECNO_OK;
    }

    if( kind == CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT )
    {
        struct RS_CS2VarTransmitHook* hook;
        hook = rs_cs2_acquire_var_transmit_hook(host, component_id);
        if( !hook )
            return CS2VM_EXECNO_OK;
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        hook->trigger_count = request->trigger_count;
        if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
            hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
        if( request->trigger_ids && hook->trigger_count > 0 )
            memcpy(
                hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
        return CS2VM_EXECNO_OK;
    }

    return CS2VM_EXECNO_OK;
}

static struct UITreeRuntimeScriptHook*
rs_cs2_runtime_hook_slot(
    struct UITreeComponent* node,
    enum CS2VM_HostRequestKind kind)
{
    struct UITreeRuntimeHooks* hooks;

    assert(node);
    /* Registration is the only writer, so it is also where the block is born. */
    hooks = UITree_HooksMut(node);
    if( !hooks )
        return NULL;
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return &hooks->on_click;
    case CS2VM_HOST_REQUEST_IF_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
        return &hooks->on_hold;
    case CS2VM_HOST_REQUEST_IF_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return &hooks->on_op;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return &hooks->on_mouse_over;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return &hooks->on_mouse_leave;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
        return &hooks->on_mouse_repeat;
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
        return &hooks->on_timer;
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
        return &hooks->on_scroll_wheel;
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return &hooks->on_drag;
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return &hooks->on_drag_complete;
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
        return &hooks->on_resize;
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
        return &hooks->on_sub_change;
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return &hooks->on_key;
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        /* IF_ only — there is no CC_ misc-transmit request kind at this
         * revision, and the CC_SETONMISCTRANSMIT opcode (1422) is parsed into
         * the discard group rather than a request.
         *
         * The "misc" transmits are the ones with no registry of their own:
         * run energy and run weight at this revision. The field existed but
         * nothing resolved to it, so every registration was discarded and the
         * run orb never repainted on its own. */
        return &hooks->on_misc_transmit;
    case CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT:
        /* IF_ only, like misc: CC_SETONFRIENDTRANSMIT (1420) is parsed into
         * the discard group. The friends and ignore panels register this on
         * their root component in scripts 123 / 127, and it is the only thing
         * that ever asks them to repaint. */
        return &hooks->on_friend_transmit;
    default:
        return NULL;
    }
}

#if UITREE_CLICK_DEBUG
static char const*
rs_cs2_seton_kind_str(enum CS2VM_HostRequestKind kind)
{
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
        return "IF_SETONCLICK";
    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return "IF_SETONOP";
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return "IF_SETONMOUSEOVER";
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return "IF_SETONMOUSELEAVE";
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return "IF_SETONKEY";
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return "CC_SETONKEY";
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return "CC_SETONCLICK";
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return "CC_SETONOP";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return "CC_SETONMOUSEOVER";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return "CC_SETONMOUSELEAVE";
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
        return "IF_SETONDRAG";
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
        return "IF_SETONDRAGCOMPLETE";
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
        return "IF_SETONRESIZE";
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
        return "IF_SETONSUBCHANGE";
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return "CC_SETONDRAG";
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return "CC_SETONDRAGCOMPLETE";
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
        return "CC_SETONRESIZE";
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
        return "CC_SETONSUBCHANGE";
    default:
        return "SETON?";
    }
}
#endif

static int
exec_set_on_if_event(
    struct RS_CS2Host* host,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_IF_SetOnOp const* request)
{
    struct UITree* tree;
    struct UITreeComponent* node;
    struct UITreeRuntimeScriptHook* slot;

    assert(host);
    if( !request )
        return CS2VM_EXECNO_OK;

    tree = rs_cs2_tree(host);
    if( !tree )
        return CS2VM_EXECNO_OK;

    node = rs_cs2_node(host, request->component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    slot = rs_cs2_runtime_hook_slot(node, kind);
    if( !slot )
        return CS2VM_EXECNO_OK;

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON %s component_id=%d script_id=%d argc=%d\n",
        rs_cs2_seton_kind_str(kind),
        request->component_id,
        request->script_id,
        request->int_arg_count);
#endif

    {
        char const* strp[CS2VM_SETON_STR_ARG_MAX];
        for( int i = 0; i < CS2VM_SETON_STR_ARG_MAX; i++ )
            strp[i] = request->str_args[i];
        (void)UITree_ApplyRuntimeHook(
            tree,
            request->component_id,
            slot,
            request->script_id,
            request->int_arg_count > 0 ? request->int_args : NULL,
            request->int_arg_count,
            request->str_arg_mask,
            strp,
            request->str_arg_count);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_cc_event(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp const* request)
{
    struct UITree* tree;
    struct UITreeComponent* node;
    struct UITreeRuntimeScriptHook* slot;
    int component_id;

    assert(host);
    assert(vm);
    if( !request )
        return CS2VM_EXECNO_OK;

    tree = rs_cs2_tree(host);
    if( !tree )
        return CS2VM_EXECNO_OK;

    /* Target resolved at op time in the VM (dot vs active register — the
     * scrollbar/dropdown procs attach handlers to several dot children in a
     * row, so re-reading the active register here binds the wrong child). */
    component_id = request->component_id;
    node = rs_cs2_node(host, component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    slot = rs_cs2_runtime_hook_slot(node, kind);
    if( !slot )
        return CS2VM_EXECNO_OK;

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON %s component_id=%d script_id=%d argc=%d\n",
        rs_cs2_seton_kind_str(kind),
        component_id,
        request->script_id,
        request->int_arg_count);
#endif

    {
        char const* strp[CS2VM_SETON_STR_ARG_MAX];
        for( int i = 0; i < CS2VM_SETON_STR_ARG_MAX; i++ )
            strp[i] = request->str_args[i];
        (void)UITree_ApplyRuntimeHook(
            tree,
            component_id,
            slot,
            request->script_id,
            request->int_arg_count > 0 ? request->int_args : NULL,
            request->int_arg_count,
            request->str_arg_mask,
            strp,
            request->str_arg_count);
    }
    return CS2VM_EXECNO_OK;
}

/* =========================================================================
 * Client database (DB_* opcodes 7500..7510)
 *
 * DB_GETROW/DB_GETFIELD/DB_GETROWTABLE read a DBROW (config kind 38, resolved
 * through CacheProvider_DbRowGet). DB_FIND/DB_FINDALL read a table's inverted
 * index (cache table 21) to build the host find-iterator, which DB_FINDNEXT
 * then walks one row at a time.
 *
 * A dbcolumn operand packs (tableId, columnId, tupleIndex). The bit layout
 * below is the widely-referenced OSRS convention. `tupleIndex == 0xF` is the
 * "whole tuple / all fields" sentinel.
 *
 * Stack convention: arguments are popped in reverse push order, so the LAST
 * source argument is popped first. This was previously guessed the other way
 * round for the find family and it was wrong twice over — see below.
 *
 *   db_find*(dbcolumn, value, type_tag)  pops type_tag, value, dbcolumn
 *   db_getfield(row, dbcolumn, index)    pops index, dbcolumn, row
 *   db_getfieldcount(row, dbcolumn)      pops dbcolumn, row
 *   db_getrow(query_index)               pops query_index
 *
 * `type_tag` is how the script says which stack the search value is on: 2 means
 * the string stack, anything else the int stack. Every call site in
 * cache.osrs239 passes a literal, so the type is static — the client never has
 * to consult the index to know where to pop from, which is what the old
 * comment here assumed it did.
 *
 * Getting this wrong is not a quiet failure. `db_find_with_count` takes three
 * arguments and the old code popped two, so the extra one stayed on the stack
 * and every value the rest of the script read was shifted by one; the combat
 * tab asked the weapon-style table for category `0` (the type tag), got no
 * rows, and hid all four attack-style buttons.
 * ========================================================================= */

/*
 * The low nibble selects a field of the column's tuple and it is ONE-based:
 * 0 means the whole tuple, N means field N-1. The corpus settles it — table
 * 166 column 32 is a 4-tuple, and `db_getfield(row, 0xA6200, i)` (nibble 0)
 * assigns four values while `db_getfield(row, 0xA6204, i)` (nibble 4) assigns
 * one. Reading it zero-based gets both ends wrong: the common nibble-0 case
 * pushes one value where the script pops four, and the highest field reads as
 * out-of-range and pushes the whole tuple.
 */
#define DB_TUPLE_WHOLE (-1)

static void
db_unpack_column(
    int packed,
    int* table_id,
    int* column_id,
    int* tuple_index)
{
    *table_id = (packed >> 12) & 0xFFFFF;
    *column_id = (packed >> 4) & 0xFF;
    *tuple_index = (packed & 0xF) - 1;
}

static void
db_set_iterator(
    struct RS_CS2Host* host,
    int const* rows,
    int count)
{
    free(host->db_find_rows);
    host->db_find_rows = NULL;
    host->db_find_count = 0;
    host->db_find_cursor = 0;
    if( count <= 0 || !rows )
        return;
    host->db_find_rows = malloc((size_t)count * sizeof(int));
    assert(host->db_find_rows);
    memcpy(host->db_find_rows, rows, (size_t)count * sizeof(int));
    host->db_find_count = count;
}

/* Park a yield for a DB resource (a row or a table index). Returns the yield
 * exec code; the caller returns it straight up. */
static int
db_yield_load(
    struct RS_CS2Host* host,
    int opcode,
    int load_kind,
    int load_id)
{
    struct CS2VM_HostRequest req = { 0 };
    req.kind = CS2VM_HOST_REQUEST_DB;
    req.u.db.opcode = opcode;
    req.u.db.load_kind = load_kind;
    req.u.db.load_id = load_id;
    return rs_cs2_yield_load(host, &req, load_id, load_kind);
}

/* Resolve a DBROW, loading it once if needed. On the first miss this yields
 * (returns true via *yielded with the yield code in *out_code); on a second
 * miss after the load it returns NULL with *yielded false (genuinely absent). */
static struct RSCache_Dat2ConfigDbRow*
db_row_or_yield(
    struct RS_CS2Host* host,
    int opcode,
    int row_id,
    bool* yielded,
    int* out_code)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct RSCache_Dat2ConfigDbRow* row =
        (provider && row_id >= 0) ? CacheProvider_DbRowGet(provider, row_id) : NULL;

    *yielded = false;
    if( row || row_id < 0 )
        return row;
    if( !rs_cs2_await_spent(host, CS2VM_HOST_REQUEST_DB, row_id, CS2VM_DB_LOAD_ROW) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, opcode, CS2VM_DB_LOAD_ROW, row_id);
    }
    return NULL;
}

static struct ToriRS_DbTableIndex*
db_index_or_yield(
    struct RS_CS2Host* host,
    int opcode,
    int table_id,
    bool* yielded,
    int* out_code)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_DbTableIndex* idx =
        (provider && table_id >= 0) ? CacheProvider_DbTableIndexGet(provider, table_id) : NULL;

    *yielded = false;
    if( idx || table_id < 0 )
        return idx;
    if( !rs_cs2_await_spent(host, CS2VM_HOST_REQUEST_DB, table_id, CS2VM_DB_LOAD_INDEX) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, opcode, CS2VM_DB_LOAD_INDEX, table_id);
    }
    return NULL;
}

static struct RSCache_Dat2ConfigDbTable*
db_table_or_yield(
    struct RS_CS2Host* host,
    int opcode,
    int table_id,
    bool* yielded,
    int* out_code)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct RSCache_Dat2ConfigDbTable* table =
        (provider && table_id >= 0) ? CacheProvider_DbTableGet(provider, table_id) : NULL;

    *yielded = false;
    if( table || table_id < 0 )
        return table;
    if( !rs_cs2_await_spent(host, CS2VM_HOST_REQUEST_DB, table_id, CS2VM_DB_LOAD_TABLE) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, opcode, CS2VM_DB_LOAD_TABLE, table_id);
    }
    return NULL;
}

/*
 * Push the "no value" value for a field type onto that type's stack.
 *
 * -1, not 0. Measured, because it is the difference between an icon and a blank
 * row: across the 9,368 decompiled clientscripts, every guard on a db_getfield
 * of a column that carries no table default compares against -1 — 44 for
 * `dbrow`, 18 for `obj`, 7 each for `stat` and `loc`, 6 each for `graphic` and
 * plain `int`, and so on down. Zero appears only as `> 0` / `!= 0` guards, which
 * a real value satisfies too and so discriminate nothing.
 *
 * Strings keep the empty string: the tables that declare a string default all
 * declare it empty (`defaults=2:0:` on skill_features' `text`).
 */
static int
db_push_default(
    struct CS2VM2_Thread* vm,
    int type)
{
    if( RSCache_DbTypeIsString(type) )
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    return CS2VM2_PushInt(vm, -1);
}

/* The column record that states a column's shape: the row's when the row sets
 * that column, otherwise the table's. Only the table lists a column the row
 * omits, and only the table carries default values. NULL when neither has it. */
static struct RSCache_DbColumn const*
db_column_of(
    struct RSCache_Dat2ConfigDbRow const* row,
    struct RSCache_Dat2ConfigDbTable const* table,
    int col_id)
{
    if( col_id < 0 )
        return NULL;
    if( row && col_id < row->column_count && row->columns[col_id].present )
        return &row->columns[col_id];
    if( table && col_id < table->column_count && table->columns[col_id].present )
        return &table->columns[col_id];
    return NULL;
}

/*
 * Answer a DB_GETFIELD whose row / column / index resolved to no value.
 *
 * The opcode always yields a value, so returning without pushing pops the
 * caller's three arguments and underflows whatever runs next — the failure then
 * surfaces at an unrelated opcode (script 4029 read dbcolumn 48 off a row absent
 * from our partial cache and died on the following BRANCH_EQUALS).
 *
 * The *arity* matters just as much as the value, and it is the half that was
 * missing: a whole-tuple read pushes one value per field, so a 5-field column
 * answered with one integer shifts every local the assignment writes. Only the
 * DBTABLE lists the fields of a column its rows omit, which is why `col` here
 * has to be able to come from the table (see db_column_of).
 */
static int
db_push_missing(
    struct CS2VM2_Thread* vm,
    struct RSCache_DbColumn const* col,
    int tuple)
{
    if( !col || col->type_count <= 0 || !col->types )
        return CS2VM2_PushInt(vm, -1);

    if( tuple >= 0 && tuple < col->type_count )
        return db_push_default(vm, col->types[tuple]);

    for( int i = 0; i < col->type_count; i++ )
    {
        int rc = db_push_default(vm, col->types[i]);
        if( rc != CS2VM_EXECNO_OK )
            return rc;
    }
    return CS2VM_EXECNO_OK;
}

/* Push one field value onto its type's stack. */
static int
db_push_value(
    struct CS2VM2_Thread* vm,
    struct RSCache_DbValue const* value)
{
    if( value->is_string )
        return CS2VM2_PushStr(
            vm, CS2VM2_StrDup(vm, value->string_value ? value->string_value : ""));
    return CS2VM2_PushInt(vm, value->int_value);
}

/* DB_FIND value lookup: scan the column index for an entry matching `value`
 * (int or string), and copy its row-id list into the iterator. A whole-tuple
 * column (nibble 0) matches on any field, which is what makes a single-field
 * query against a multi-field column work; a field selector searches only that
 * field. Sets an empty iterator when nothing matches. */
static void
db_find_value(
    struct RS_CS2Host* host,
    struct ToriRS_DbTableIndex const* idx,
    int column_id,
    int tuple_index,
    bool value_is_string,
    int value_int,
    char const* value_str)
{
    struct RSCache_DbIndexFile* file = ToriRS_DbTableIndex_Column(idx, column_id);
    int first, last;

    db_set_iterator(host, NULL, 0);
    if( !file || file->tuple_size <= 0 )
        return;
    if( tuple_index >= 0 && tuple_index < file->tuple_size )
        first = last = tuple_index;
    else
        first = 0, last = file->tuple_size - 1;

    for( int pos = first; pos <= last; pos++ )
    {
        struct RSCache_DbIndexTuple* tuple = &file->tuples[pos];
        for( int i = 0; i < tuple->entry_count; i++ )
        {
            struct RSCache_DbIndexEntry* entry = &tuple->entries[i];
            bool match;
            if( value_is_string || tuple->base_type == RSCACHE_DB_BASE_STRING )
                match =
                    entry->string_value && value_str && strcmp(entry->string_value, value_str) == 0;
            else
                match = (entry->int_value == value_int);
            if( match )
            {
                db_set_iterator(host, entry->row_ids, entry->row_count);
                return;
            }
        }
    }
}

/* Handles the full DB_* opcode family. All stack manipulation happens here so
 * that a value whose int/string type is only known after the index loads can be
 * popped on the retry (see the stack-convention note above). */
static int
exec_db(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int opcode)
{
    bool yielded;
    int code;

    switch( opcode )
    {
    case CS2_OP_DB_FINDNEXT:
    {
        int row = -1;
        if( host->db_find_cursor < host->db_find_count )
            row = host->db_find_rows[host->db_find_cursor++];
        return CS2VM2_PushInt(vm, row);
    }

    case CS2_OP_DB_GETROW:
    {
        /* Random access into the current find result — the indexed twin of
         * DB_FINDNEXT, not a row lookup: scripts call it as
         * `while ($i < $count) { $row = db_getrow($i); ... }`, so the argument
         * is a position in the query and the result is a row id (-1 past the
         * end). It used to be read as a row id and push nothing at all, which
         * left the assignment reading whatever was under it. */
        int index;
        if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( index < 0 || index >= host->db_find_count || !host->db_find_rows )
            return CS2VM2_PushInt(vm, -1);
        return CS2VM2_PushInt(vm, host->db_find_rows[index]);
    }

    case CS2_OP_DB_GETROWTABLE:
    {
        int row_id;
        struct RSCache_Dat2ConfigDbRow* row;
        if( CS2VM2_PopInt(vm, &row_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        row = db_row_or_yield(host, opcode, row_id, &yielded, &code);
        if( yielded )
            return code;
        return CS2VM2_PushInt(vm, row ? row->table_id : -1);
    }

    case CS2_OP_DB_GETFIELDCOUNT:
    {
        int column, row_id, table, col_id, tuple;
        struct RSCache_Dat2ConfigDbRow* row;
        struct RSCache_Dat2ConfigDbTable* dbtable;
        struct RSCache_DbColumn const* col;
        if( CS2VM2_PopInt(vm, &column) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &row_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        row = db_row_or_yield(host, opcode, row_id, &yielded, &code);
        if( yielded )
            return code;
        db_unpack_column(column, &table, &col_id, &tuple);
        if( row && col_id >= 0 && col_id < row->column_count && row->columns[col_id].present )
            return CS2VM2_PushInt(vm, row->columns[col_id].tuple_count);
        /* Column absent from the row: the table's default block stands in for
         * it, tuples and all — script 9343 walks `skill` this way and guards the
         * first field against 0, which is exactly that column's default.
         * Row-present only, for the ping-pong reason spelled out in
         * DB_GETFIELD below. */
        dbtable = row ? db_table_or_yield(host, opcode, table, &yielded, &code) : NULL;
        if( yielded )
            return code;
        col = db_column_of(row, dbtable, col_id);
        return CS2VM2_PushInt(vm, col ? col->tuple_count : 0);
    }

    case CS2_OP_DB_GETFIELD:
    {
        int index, column, row_id, table, col_id, tuple;
        struct RSCache_Dat2ConfigDbRow* row;
        struct RSCache_Dat2ConfigDbTable* dbtable;
        struct RSCache_DbColumn const* col;
        int arity, base;
        if( CS2VM2_PopInt(vm, &index) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &column) != CS2VM_EXECNO_OK ||
            CS2VM2_PopInt(vm, &row_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        row = db_row_or_yield(host, opcode, row_id, &yielded, &code);
        if( yielded )
            return code;
        db_unpack_column(column, &table, &col_id, &tuple);
        /* A DBROW lists only the columns it sets, so a column it omits falls
         * back to the DBTABLE: its default value block if it declares one, and
         * its field types either way. Load the table before deciding — it is the
         * only record that knows how many values this read owes the stack.
         *
         * Missing row/column/index still has to honour the stack contract: this op
         * always yields a value, so the caller's next opcode underflows if we pop
         * three and push nothing (script 4029 read dbcolumn 48 off a row absent
         * from our partial cache and aborted on the following BRANCH_EQUALS). */
        if( !row || col_id < 0 || col_id >= row->column_count ||
            !row->columns[col_id].present )
        {
            /* Only chase the table when the row itself resolved. Yielding twice
             * from one opcode is safe in that order (the row is resident on the
             * retry, so its lookup cannot yield again), but a row that is
             * genuinely absent would re-arm its own yield each time the table
             * yield overwrote the single `awaited` slot, and the two would
             * ping-pong forever. */
            dbtable = row ? db_table_or_yield(host, opcode, table, &yielded, &code) : NULL;
            if( yielded )
                return code;
            col = db_column_of(row, dbtable, col_id);
            arity = col ? col->type_count : 0;
            if( !col || arity <= 0 || index < 0 || index >= col->tuple_count || !col->values )
                return db_push_missing(vm, col, tuple);
            base = index * arity;
            if( tuple >= 0 && tuple < arity )
                return db_push_value(vm, &col->values[base + tuple]);
            for( int i = 0; i < arity; i++ )
            {
                int rc = db_push_value(vm, &col->values[base + i]);
                if( rc != CS2VM_EXECNO_OK )
                    return rc;
            }
            return CS2VM_EXECNO_OK;
        }
        col = &row->columns[col_id];
        arity = col->type_count;
        if( index < 0 || index >= col->tuple_count || arity <= 0 )
            return db_push_missing(vm, col, tuple);
        base = index * arity;
        if( tuple >= 0 && tuple < arity )
            return db_push_value(vm, &col->values[base + tuple]);
        /* Sentinel / whole tuple: push every component in field order. */
        for( int i = 0; i < arity; i++ )
        {
            int rc = db_push_value(vm, &col->values[base + i]);
            if( rc != CS2VM_EXECNO_OK )
                return rc;
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_DB_FINDALL:
    case CS2_OP_DB_FINDALL_WITH_COUNT:
    {
        int table_id;
        struct ToriRS_DbTableIndex* idx;
        struct RSCache_DbIndexFile* master;
        bool with_count = (opcode == CS2_OP_DB_FINDALL_WITH_COUNT);
        if( CS2VM2_PopInt(vm, &table_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        idx = db_index_or_yield(host, opcode, table_id, &yielded, &code);
        if( yielded )
            return code;
        db_set_iterator(host, NULL, 0);
        master = ToriRS_DbTableIndex_Master(idx);
        /* The master file has one tuple position; every value entry's rows,
         * concatenated, are all the rows in the table. */
        if( master && master->tuple_size > 0 && master->tuples[0].entry_count > 0 )
        {
            struct RSCache_DbIndexTuple* t0 = &master->tuples[0];
            int total = 0;
            for( int i = 0; i < t0->entry_count; i++ )
                total += t0->entries[i].row_count;
            if( total > 0 )
            {
                int* rows = malloc((size_t)total * sizeof(int));
                int n = 0;
                assert(rows);
                for( int i = 0; i < t0->entry_count; i++ )
                    for( int r = 0; r < t0->entries[i].row_count; r++ )
                        rows[n++] = t0->entries[i].row_ids[r];
                db_set_iterator(host, rows, total);
                free(rows);
            }
        }
        if( with_count )
            return CS2VM2_PushInt(vm, host->db_find_count);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_DB_FIND:
    case CS2_OP_DB_FIND_WITH_COUNT:
    case CS2_OP_DB_FIND_FILTER:
    case CS2_OP_DB_FIND_FILTER_WITH_COUNT:
    {
        int column, table, col_id, tuple, type_tag;
        struct ToriRS_DbTableIndex* idx;
        bool with_count =
            (opcode == CS2_OP_DB_FIND_WITH_COUNT || opcode == CS2_OP_DB_FIND_FILTER_WITH_COUNT);
        /* A "filter" narrows the query already in flight rather than starting a
         * new one — that is how a script queries two columns at once
         * (`db_find_with_count(catcol, cat, 0); db_find_filter_with_count(subcol,
         * sub, 0); db_findnext`). */
        bool is_filter =
            (opcode == CS2_OP_DB_FIND_FILTER || opcode == CS2_OP_DB_FIND_FILTER_WITH_COUNT);
        bool value_is_string;
        int value_int = 0;
        char* value_str = NULL;
        int* prior_rows = NULL;
        int prior_count = 0;

        /* Reverse push order: type tag, value, dbcolumn. The tag names the
         * stack the value is on, so nothing here has to wait on the index. */
        if( CS2VM2_PopInt(vm, &type_tag) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        value_is_string = (type_tag == 2);
        if( value_is_string )
        {
            if( CS2VM2_PopStr(vm, &value_str) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
        }
        else if( CS2VM2_PopInt(vm, &value_int) != CS2VM_EXECNO_OK )
        {
            return CS2VM_EXECNO_ERROR;
        }
        if( CS2VM2_PopInt(vm, &column) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;

        db_unpack_column(column, &table, &col_id, &tuple);
        idx = db_index_or_yield(host, opcode, table, &yielded, &code);
        if( yielded )
            return code;

        /* Hold the query being narrowed: db_find_value overwrites the iterator,
         * so a filter has to intersect against a copy taken beforehand. */
        if( is_filter && host->db_find_count > 0 && host->db_find_rows )
        {
            prior_count = host->db_find_count;
            prior_rows = malloc((size_t)prior_count * sizeof(int));
            assert(prior_rows);
            memcpy(prior_rows, host->db_find_rows, (size_t)prior_count * sizeof(int));
        }

        db_find_value(host, idx, col_id, tuple, value_is_string, value_int, value_str);

        if( is_filter )
        {
            int kept = 0;
            for( int i = 0; i < prior_count; i++ )
            {
                for( int j = 0; j < host->db_find_count; j++ )
                {
                    if( host->db_find_rows[j] != prior_rows[i] )
                        continue;
                    prior_rows[kept++] = prior_rows[i];
                    break;
                }
            }
            db_set_iterator(host, prior_rows, kept);
            free(prior_rows);
        }

        if( with_count )
            return CS2VM2_PushInt(vm, host->db_find_count);
        return CS2VM_EXECNO_OK;
    }

    default:
        assert(0 && "exec_db: unexpected opcode");
        return CS2VM_EXECNO_OK;
    }
}

/* =========================================================================
 * Friends / ignore / private chat
 *
 * Everything the friends panel (429) and the ignore panel (432) draw comes
 * through here. Their rows are cc_created by clientscripts 125 and 129 off
 * these accessors — the server addresses none of them and cannot if_settext a
 * single one — so with no handlers the panels drew a correct-looking empty
 * state no matter what the store held.
 *
 * The four mutators do BOTH halves, matching the reference client
 * (Client.ts addFriend/delFriend/addIgnore/delIgnore): the local store is
 * updated immediately and a packet is queued. Neither half is redundant. The
 * server echoes UPDATE_FRIENDLIST for an add, but it sends *nothing at all* for
 * a delete or an ignore-add, so a client that waited for the server would show
 * a deleted friend forever.
 *
 * Nothing here writes a message to the player on a failure path — not a full
 * list, not a duplicate, not a delete of someone who is not there. That is the
 * reference's behaviour on the server side (FriendServerRepository returns
 * bare) and it is also the rule this tree is under: those sentences are
 * game-facing strings, so they are content's, and inventing them in C is the
 * violation. The 2004 client did emit them; at rev 230 that surface belongs to
 * a content proc that does not exist yet. Recorded in
 * docs/FRIENDS_PRIVATE_CHAT.md, not worked around here.
 * ========================================================================= */

static void
social_queue(
    struct RS_CS2Host* host,
    enum RS_CS2SocialSendKind kind,
    char const* name)
{
    struct RS_CS2SocialSend send;

    memset(&send, 0, sizeof(send));
    send.kind = (int)kind;
    if( name )
        snprintf(send.name, sizeof(send.name), "%s", name);
    rs_cs2_social_send_push(host, &send);
}

/* =========================================================================
 * Loot tracker
 * ========================================================================= */

static int
exec_loot(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_Loot const* req)
{
    struct LootStore* loot = host->loot;
    assert(loot && "host->loot must be non-NULL when loot ops are reached");

    switch( req->opcode )
    {
    case CS2_OP_LOOT_SOURCE_COUNT:
        return CS2VM2_PushInt(vm, LootStore_SourceCount(loot));

    case CS2_OP_LOOT_SOURCE_NAME:
    case CS2_OP_LOOT_SOURCE_NAME2:
    {
        const char* name = LootStore_SourceName(loot, req->int_args[0]);
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, name));
    }

    case CS2_OP_LOOT_SOURCE_ITEMCOUNT:
        return CS2VM2_PushInt(vm, LootStore_SourceItemCount(loot, req->name));

    case CS2_OP_LOOT_SOURCE_TOTALVAL:
        return CS2VM2_PushInt(vm, LootStore_SourceKillCount(loot, req->name));

    case CS2_OP_LOOT_BEGIN_QUERY:
        return CS2VM2_PushInt(vm, LootStore_BeginQuery(
            loot, req->int_args[0], req->int_args[1], req->int_args[2]));

    case CS2_OP_LOOT_QUERY_ID:
        return CS2VM2_PushInt(vm, LootStore_QueryId(loot, req->int_args[0]));

    case CS2_OP_LOOT_AUX_COUNT_TOTAL:
        return CS2VM2_PushInt(vm, LootStore_AuxCountTotal(loot));

    case CS2_OP_LOOT_ROW_COUNT_BYNAME:
        return CS2VM2_PushInt(vm, LootStore_RowCountByName(loot, req->name));

    case CS2_OP_LOOT_ROW_COUNT_BYID:
        return CS2VM2_PushInt(vm, LootStore_RowCountById(loot, req->int_args[0]));

    case CS2_OP_LOOT_ROW_BYNAME:
    {
        int obj_id = 0, qty = 0;
        LootStore_RowByName(loot, req->name, req->int_args[0], &obj_id, &qty);
        if( CS2VM2_PushInt(vm, obj_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, qty);
    }

    case CS2_OP_LOOT_ROW_BYID:
    {
        int obj_id = 0, qty = 0;
        LootStore_RowById(loot, req->int_args[0], req->int_args[1], &obj_id, &qty);
        if( CS2VM2_PushInt(vm, obj_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, qty);
    }

    case CS2_OP_LOOT_CLEAR_ALL:
        LootStore_ClearAll(loot);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_CLEAR_SOURCE:
        LootStore_ClearSourceByName(loot, req->name ? req->name : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_REMOVE_BYID:
        LootStore_RemoveById(loot, req->int_args[0]);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_IGNORE_ADD:
        LootStore_ItemIgnoreAdd(loot, req->name ? req->name : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_IGNORE_REMOVE:
        LootStore_ItemIgnoreRemove(loot, req->name ? req->name : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_IGNORE_CLEAR:
        LootStore_ItemIgnoreClear(loot);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_SOURCE_IGNORE_ADD:
        LootStore_SourceIgnoreAdd(loot, req->name ? req->name : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_SOURCE_IGNORE_REMOVE:
        LootStore_SourceIgnoreRemove(loot, req->name ? req->name : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_GROUND_COUNT:
        return CS2VM2_PushInt(vm, LootStore_ItemIgnoreCount(loot));

    case CS2_OP_LOOT_GROUND_NAME:
    {
        const char* name = LootStore_ItemIgnoreName(loot, req->int_args[0]);
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, name));
    }

    case CS2_OP_LOOT_SRCLIST_COUNT:
        return CS2VM2_PushInt(vm, LootStore_SourceIgnoreCount(loot));

    case CS2_OP_LOOT_SRCLIST_NAME:
    {
        const char* name = LootStore_SourceIgnoreName(loot, req->int_args[0]);
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, name));
    }

    /* Aux-list ops (7400-family). */
    case CS2_OP_LOOT_AUX_UPSERT2:
        LootStore_AuxUpsert(loot, req->int_args[0], req->name ? req->name : "", 0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_AUX_UPSERT:
        LootStore_AuxUpsert(loot, req->int_args[0], req->name ? req->name : "", req->int_args[1]);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_AUX_REMOVE:
        LootStore_AuxRemove(loot, req->int_args[0], req->name ? req->name : "", req->int_args[1]);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_AUX_GET:
    {
        const char* s = LootStore_AuxGet(loot, req->int_args[0], req->int_args[1]);
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, s));
    }

    case CS2_OP_LOOT_AUX_COUNT:
        return CS2VM2_PushInt(vm, LootStore_AuxCount(loot, req->int_args[0]));

    case CS2_OP_LOOT_AUX_LOOKUP:
        return CS2VM2_PushInt(vm, LootStore_AuxLookup(
            loot, req->int_args[0], req->name ? req->name : "",
            req->int_args[1], req->int_args[2]));

    case CS2_OP_LOOT_AUX_CLEAR:
        LootStore_AuxClear(loot, req->int_args[0]);
        return CS2VM_EXECNO_OK;

    case CS2_OP_LOOT_ADD:
    {
        /* int_args: [0]=event_id, [1]=qty, [2]=obj (pop order from 7192). */
        int event_id = req->int_args[0];
        int obj_id = req->int_args[2];
        int qty = req->int_args[1];
        int cost = 1;
        struct CacheProvider* provider = rs_cs2_provider(host);
        struct ToriRS_Objtype* obj =
            provider ? CacheProvider_ObjtypeGet(provider, obj_id) : NULL;

        if( obj )
            cost = obj->cost;
        LootStore_AddKillLoot(
            loot, req->name ? req->name : "", obj_id, qty, cost, event_id);
        if( getenv("TORIRS_LOOT_TRACE") )
        {
            fprintf(
                stderr,
                "loot-add: \"%s\" obj=%d qty=%d event=%d\n",
                req->name ? req->name : "",
                obj_id,
                qty,
                event_id);
        }
        return CS2VM_EXECNO_OK;
    }

    default:
        assert(0 && "exec_loot: unexpected opcode");
        return CS2VM_EXECNO_OK;
    }
}

/* =========================================================================
 * Hiscores stubs
 * ========================================================================= */

static int
exec_hiscores(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_Hiscores const* req)
{
    (void)host;

    switch( req->opcode )
    {
    case CS2_OP_HISCORES_STATUS:
        /* Script 7530 switch: 1=pending, 2=success, 3=error. Return 3 so the
         * panel takes its failure path and shows the cache's own
         * "Unable to load hiscores…" message (script 7530 case 3) — no
         * fabricated ranks, no game-facing string in C. */
        return CS2VM2_PushInt(vm, 3);

    case CS2_OP_HISCORES_ERROR:
        /* Optional detail after the cache's "Unable to load…" prefix.
         * Empty is fine: script 7530 falls back to the prefix-only form.
         * A non-empty detail must come from content (varc / future HTTP),
         * never a C literal (PORTING_GUIDE §2.4). */
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    default:
        assert(0 && "exec_hiscores: unexpected opcode");
        return CS2VM_EXECNO_OK;
    }
}

static int
exec_social(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_Social const* req)
{
    struct RS_Social* social = host->social;
    char name[RS_SOCIAL_NAME_LEN];

    switch( req->opcode )
    {
    case CS2_OP_FRIEND_COUNT:
        return CS2VM2_PushInt(vm, RS_Social_FriendCount(social));
    case CS2_OP_IGNORE_COUNT:
        return CS2VM2_PushInt(vm, RS_Social_IgnoreCount(social));

    /*
     * Two strings, in source order: the display name, then the player's
     * PREVIOUS name. There is no rename model here and no wire field carrying
     * one, so the second is always "" — which is the answer that matters,
     * because script 125 branches on `string_length($string1) > 0` and would
     * otherwise offer a "Reveal previous name" op with nothing behind it.
     */
    case CS2_OP_FRIEND_GETNAME:
        RS_Social_FriendName(social, req->index, name, (int)sizeof(name));
        if( CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, name)) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
    case CS2_OP_IGNORE_GETNAME:
        RS_Social_IgnoreName(social, req->index, name, (int)sizeof(name));
        if( CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, name)) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    case CS2_OP_FRIEND_GETWORLD:
        return CS2VM2_PushInt(vm, RS_Social_FriendWorld(social, req->index));
    case CS2_OP_FRIEND_GETRANK:
        /* No rank model: clan ranks come with clan chat, and the friends panel
         * never reads this at rev 230 (only script 1667 does). 0 = no rank. */
        return CS2VM2_PushInt(vm, 0);

    case CS2_OP_FRIEND_TEST:
        return CS2VM2_PushInt(vm, RS_Social_IsFriend(social, req->name) ? 1 : 0);
    case CS2_OP_IGNORE_TEST:
        return CS2VM2_PushInt(vm, RS_Social_IsIgnored(social, req->name) ? 1 : 0);

    case CS2_OP_FRIEND_ADD:
        if( !req->name || !req->name[0] )
            return CS2VM_EXECNO_OK;
        /* World 0 until the server answers with the real one, exactly as the
         * reference does — the row appears immediately, reading "Offline". */
        if( social && RS_Social_AddFriend(social, req->name, 0) )
            RS_CS2Host_NotifyFriendChanged(host);
        social_queue(host, RS_CS2_SOCIAL_SEND_FRIEND_ADD, req->name);
        return CS2VM_EXECNO_OK;
    case CS2_OP_FRIEND_DEL:
        if( !req->name || !req->name[0] )
            return CS2VM_EXECNO_OK;
        if( social && RS_Social_DelFriend(social, req->name) )
            RS_CS2Host_NotifyFriendChanged(host);
        social_queue(host, RS_CS2_SOCIAL_SEND_FRIEND_DEL, req->name);
        return CS2VM_EXECNO_OK;
    case CS2_OP_IGNORE_ADD:
        if( !req->name || !req->name[0] )
            return CS2VM_EXECNO_OK;
        if( social && RS_Social_AddIgnore(social, req->name) )
            RS_CS2Host_NotifyFriendChanged(host);
        social_queue(host, RS_CS2_SOCIAL_SEND_IGNORE_ADD, req->name);
        return CS2VM_EXECNO_OK;
    case CS2_OP_IGNORE_DEL:
        if( !req->name || !req->name[0] )
            return CS2VM_EXECNO_OK;
        if( social && RS_Social_DelIgnore(social, req->name) )
            RS_CS2Host_NotifyFriendChanged(host);
        social_queue(host, RS_CS2_SOCIAL_SEND_IGNORE_DEL, req->name);
        return CS2VM_EXECNO_OK;

    default:
        assert(0 && "exec_social: unexpected opcode");
        return CS2VM_EXECNO_OK;
    }
}

static int
exec_chat(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_Chat const* req)
{
    int* modes = host->chat_filter_mode;

    switch( req->opcode )
    {
    case CS2_OP_CHAT_GETFILTER_PUBLIC:
        return CS2VM2_PushInt(vm, modes ? modes[RS_UI_CHAT_FILTER_PUBLIC] : 0);
    case CS2_OP_CHAT_GETFILTER_PRIVATE:
        return CS2VM2_PushInt(vm, modes ? modes[RS_UI_CHAT_FILTER_PRIVATE] : 0);
    case CS2_OP_CHAT_GETFILTER_TRADE:
        return CS2VM2_PushInt(vm, modes ? modes[RS_UI_CHAT_FILTER_TRADE] : 0);

    case CS2_OP_CHAT_PLAYERNAME:
        /* The local player's display name. Reference `localPlayer.name`; here
         * the same string the chat model echoes with, so a public line and the
         * input line above it cannot spell the player differently. Through the
         * pool like every other VM string — a plain pointer would be freed as
         * one when the script ends. */
        if( host->local_player_name[0] )
            return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, host->local_player_name));
        return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));

    case CS2_OP_CHAT_SETFILTER:
    {
        struct RS_CS2SocialSend send;

        /* Set locally and tell the server, the reference's order
         * (Client.ts chatModeLoop). The server echoes CHAT_FILTER_SETTINGS
         * back, which is what makes the two ends agree if it disagrees. */
        if( modes )
        {
            modes[RS_UI_CHAT_FILTER_PUBLIC] = req->public_mode;
            modes[RS_UI_CHAT_FILTER_PRIVATE] = req->private_mode;
            modes[RS_UI_CHAT_FILTER_TRADE] = req->trade_mode;
        }
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_CHAT_SETMODE;
        send.modes[0] = req->public_mode;
        send.modes[1] = req->private_mode;
        send.modes[2] = req->trade_mode;
        rs_cs2_social_send_push(host, &send);
        RS_CS2Host_NotifyFriendChanged(host);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_CHAT_SENDPRIVATE:
    {
        struct RS_CS2SocialSend send;

        if( !req->name || !req->name[0] || !req->text || !req->text[0] )
            return CS2VM_EXECNO_OK;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_MESSAGE_PRIVATE;
        snprintf(send.name, sizeof(send.name), "%s", req->name);
        snprintf(send.text, sizeof(send.text), "%s", req->text);
        rs_cs2_social_send_push(host, &send);
        return CS2VM_EXECNO_OK;
    }

    case CS2_OP_DOCHEAT:
    {
        struct RS_CS2SocialSend send;

        if( !req->text || !req->text[0] )
            return CS2VM_EXECNO_OK;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_CHEAT;
        snprintf(send.text, sizeof(send.text), "%s", req->text);
        rs_cs2_social_send_push(host, &send);
        return CS2VM_EXECNO_OK;
    }

    default:
        assert(0 && "exec_chat: unexpected opcode");
        return CS2VM_EXECNO_OK;
    }
}

/* =========================================================================
 * Main dispatcher
 * ========================================================================= */

static int
rs_cs2_host_exec_dispatch(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request);

int
RS_CS2Host_Exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct RS_CS2Host* host;
    int result;

    assert(vm);
    assert(request);

    host = (struct RS_CS2Host*)CS2VM_USER(vm);
    assert(host && "CS2VM_USER(thread) must be RS_CS2Host*");

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_CS2_HOST_OPS, 1);
    result = rs_cs2_host_exec_dispatch(vm, request);
    /* The await record only spans the yield -> load -> retry window: a request
     * that completes retires it, so a resource evicted later can be awaited
     * again. */
    if( result != CS2VM_EXECNO_YIELD )
        host->has_awaited = false;
    return result;
}

static int
rs_cs2_host_exec_dispatch(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct RS_CS2Host* host;
    struct UITree* tree;
    struct UITreeComponent* node;

    assert(vm);
    assert(request);

    host = (struct RS_CS2Host*)CS2VM_USER(vm);
    assert(host && "CS2VM_USER(thread) must be RS_CS2Host*");

    tree = rs_cs2_tree(host);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return exec_push_script(host, vm, request->u.push_script.script_id);

    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return CS2VM2_PushInt(vm, rs_cs2_inv_size(host, request->u.invs_get_size.inv_id));

    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_obj(host, request->u.invs_get_obj.inv_id, request->u.invs_get_obj.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_num(host, request->u.invs_get_num.inv_id, request->u.invs_get_num.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_total(
                host, request->u.invs_get_total.inv_id, request->u.invs_get_total.item_id));

    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
        return exec_vars_read_varp(host, vm, request->u.vars_read_varp.varp_id);

    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
        return exec_vars_read_varbit(host, vm, request->u.vars_read_varbit.varbit_id);

    /* POP_VAR / POP_VARBIT — until 2026-08-02 both popped their value and
     * returned OK, so every client-side var write in the cache vanished.
     *
     * This is deliberately fixed here, in the VM's var seam, and not in any one
     * panel: the write is not a world-map feature. 510 of this cache's 9,433
     * clientscripts write a varbit and 77 write a varp; the world map's key
     * panel (script 1718 → varbit 5640) is simply the one that was measured.
     * A per-panel workaround would have had to be written 587 more times.
     *
     * Optimistic: the client's own copy moves now, the server's authoritative
     * copy (var_serv) does not — the reference does the same, and the server
     * re-asserts with VARP_SMALL/LARGE when it disagrees.
     *
     * These deliberately do NOT go through RS_CS2Host_NotifyVarChanged. That
     * feeds the var-transmit ring, which only the three server-packet handlers
     * may touch (VarPManager_ServerUpdateFn's header states why): a widget
     * whose transmit hook writes the same var would re-trigger itself forever
     * — and the world map's key panel is exactly that shape, script 1717
     * registering a transmit hook on varp 1568 while script 1718 writes
     * varbit 5640 over it. VarPManager_SetVarpOptimistic still fires the
     * plain ChangeFn, which is the non-recursive notification (loc transforms,
     * UI invalidation) and is all a script write is entitled to. */
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARP_AKA_POP_VAR:
        if( host->varps )
            VarPManager_SetVarpOptimistic(
                host->varps, request->u.vars_write_varp.varp_id, request->u.vars_write_varp.value);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARBIT:
        if( host->varps )
            VarPManager_SetVarbitOptimistic(
                host->varps,
                request->u.vars_write_varbit.varbit_id,
                request->u.vars_write_varbit.value);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    {
        int id = request->u.vars_read_varc_int.varc_id;
        int value = host->varcs ? VarCManager_GetInt(host->varcs, id) : 0;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_KEYHELD:
    case CS2VM_HOST_REQUEST_KEYPRESSED:
    {
        int key_code = request->u.key_query.key_code;
        unsigned char const* state = request->kind == CS2VM_HOST_REQUEST_KEYHELD
                                         ? host->osrs_key_held
                                         : host->osrs_key_pressed;
        int value = 0;
        if( key_code >= 0 && key_code < TORIRS_OSRSKEY_COUNT )
            value = state[key_code] ? 1 : 0;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char const* value = host->varcs ? VarCManager_GetString(host->varcs, id) : "";
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, value));
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    {
        int id = request->u.vars_write_varc_int.varc_id;
        /* The manager fires its change callback (RS_CS2Host_NotifyVarChanged) on a
         * real change, which flags a var-transmit re-dispatch for the tick. */
        if( host->varcs )
            VarCManager_SetInt(host->varcs, id, request->u.vars_write_varc_int.value);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    {
        int id = request->u.vars_write_varc_string.varc_id;
        if( host->varcs )
            VarCManager_SetString(host->varcs, id, request->u.vars_write_varc_string.value);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        return exec_enum_lookup(host, vm, request->u.enum_lookup);

    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return exec_enum_output_count(host, vm, request->u.enum_get_output_count);

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return exec_struct_param(host, vm, request->u.struct_param);

    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        return exec_oc_int_param(host, vm, request->u.oc_int_param);

    case CS2VM_HOST_REQUEST_OC_NAME:
        return exec_oc_name(host, vm, request->u.oc_name);

    case CS2VM_HOST_REQUEST_NC_NAME:
        return exec_nc_name(host, vm, request->u.nc_name);

    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        return exec_oc_unplaceholder(host, vm, request->u.oc_unplaceholder);

    case CS2VM_HOST_REQUEST_OC_OP:
        return exec_oc_op(host, vm, request->u.oc_op);

    case CS2VM_HOST_REQUEST_OC_IOP:
        return exec_oc_op(host, vm, request->u.oc_iop);

    case CS2VM_HOST_REQUEST_OC_EXAMINE:
        return exec_oc_examine(host, vm, request->u.oc_examine);

    case CS2VM_HOST_REQUEST_OC_PLACEHOLDER:
        return exec_oc_placeholder(vm, request->u.oc_placeholder);

    case CS2VM_HOST_REQUEST_OC_FIND:
        return exec_oc_find(host, vm, request->u.oc_find);

    case CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP:
        return exec_oc_shiftclickiop(vm, request->u.oc_shiftclickiop);

    case CS2VM_HOST_REQUEST_OC_WEARPOS:
        return exec_oc_wearpos(vm, request->u.oc_wearpos);

    case CS2VM_HOST_REQUEST_OC_WEIGHT:
        return exec_oc_weight(vm, request->u.oc_weight);

    case CS2VM_HOST_REQUEST_OC_ISUBOP:
        return exec_oc_isubop(vm, request->u.oc_isubop);

    case CS2VM_HOST_REQUEST_OC_PARAM:
        return exec_oc_param(host, vm, request->u.oc_param);

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return exec_para_height(host, vm, request->u.para_height, 0);

    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return exec_para_height(host, vm, request->u.para_height, 1);

    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return CS2VM2_PushInt(vm, host->client_clock);

    case CS2VM_HOST_REQUEST_STAT:
    case CS2VM_HOST_REQUEST_STAT_BASE:
    case CS2VM_HOST_REQUEST_STAT_XP:
    {
        int stat = request->u.stat.stat;
        int value = 0;

        if( host->stats && stat >= 0 && stat < RS_PLAYER_STATS_SKILL_COUNT )
        {
            if( request->kind == CS2VM_HOST_REQUEST_STAT )
                value = host->stats->current_level[stat];
            else if( request->kind == CS2VM_HOST_REQUEST_STAT_BASE )
                value = host->stats->base_level[stat];
            else
                value = host->stats->xp[stat];
        }
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_SOCIAL:
        return exec_social(host, vm, &request->u.social);

    case CS2VM_HOST_REQUEST_LOOT:
        return exec_loot(host, vm, &request->u.loot);

    case CS2VM_HOST_REQUEST_HISCORES:
        return exec_hiscores(host, vm, &request->u.hiscores);

    case CS2VM_HOST_REQUEST_CHAT:
        return exec_chat(host, vm, &request->u.chat);

    case CS2VM_HOST_REQUEST_MAP_WORLD:
        /* Non-zero, or the friends panel headers read "World 0" and every
         * online friend draws yellow (script 125 compares each friend's world
         * against this to pick the green same-world colour). */
        return CS2VM2_PushInt(vm, host->map_world);

    case CS2VM_HOST_REQUEST_RUNENERGY:
        return CS2VM2_PushInt(vm, host->stats ? host->stats->run_energy : 0);

    case CS2VM_HOST_REQUEST_RUNWEIGHT:
        return CS2VM2_PushInt(vm, host->stats ? host->stats->run_weight : 0);

    case CS2VM_HOST_REQUEST_MOUSE_GETX:
        return CS2VM2_PushInt(vm, host->mouse_x);

    case CS2VM_HOST_REQUEST_MOUSE_GETY:
        return CS2VM2_PushInt(vm, host->mouse_y);

    case CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT:
        host->cam_follow_height = request->u.cam_set_follow_height.height;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT:
        return CS2VM2_PushInt(vm, host->cam_follow_height);

    case CS2VM_HOST_REQUEST_HIGHLIGHT:
        /* HIGHLIGHT_* (7000..7037): no highlight-overlay system in this port yet.
         * The request carries the opcode and its popped args (request->u.highlight)
         * for when the host grows real entity/loc/obj highlight state; for now the
         * GET queries answer "not highlighted" (0) and the rest are no-ops. */
        if( request->u.highlight.query )
            return CS2VM2_PushInt(vm, 0);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CLIENTOP:
        /* CLIENTOP_* (6700..6709): no enhanced client-owned context-menu system
         * yet. Args are already popped into request->u.clientop (slot / script_id
         * / label); discard and continue so scripts wiring interface state do not
         * abort. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_DB:
        return exec_db(host, vm, request->u.db.opcode);

    case CS2VM_HOST_REQUEST_MINIMENU:
        return exec_minimenu(host, vm, request->u.minimenu.opcode);

    case CS2VM_HOST_REQUEST_CLIENT_OPTION:
        return exec_client_option(host, vm, request->u.client_option);

    case CS2VM_HOST_REQUEST_MINIMAP:
        return exec_minimap(host, vm, request->u.minimap);

    case CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION:
        return exec_local_notification(vm, request->u.local_notification);

    case CS2VM_HOST_REQUEST_LOGOUT:
        host->logout_requested = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLOSE:
        host->close_modal_requested = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_RESUME_PAUSEBUTTON:
        /* Last write wins within a tick — a double-fire of the continue
         * listener (key + click) would otherwise queue two resumes for one
         * pause. The server matches on the component uid either way. */
        host->resume_pausebutton_component_id = request->u.resume_pausebutton.component_id;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_SET_WINDOW_MODE:
        /* Reject anything outside the dialect's own domain rather than
         * resizing to a mode nothing names. */
        if( request->u.window_mode.mode != CS2VM_WINDOW_MODE_FIXED &&
            request->u.window_mode.mode != CS2VM_WINDOW_MODE_RESIZABLE )
            return CS2VM_EXECNO_OK;
        if( host->window_mode != request->u.window_mode.mode )
        {
            host->window_mode = request->u.window_mode.mode;
            /* The canvas and the window are the App's; it drains this. */
            host->window_mode_dirty = true;
        }
        /* Display dropdown: settings_client_mode always reaches setwindowmode
         * with $int0 = 0/1/2. Stash that so WINDOW_STATUS can tell Classic from
         * Modern (both resizable). */
        if( vm && vm->frame_sp > 0 )
        {
            struct CS2VM2_Frame* frame = vm->frames[vm->frame_sp - 1];
            if( frame && frame->script &&
                frame->script->script_id == host->script_settings_client_mode )
            {
                int layout = frame->int_locals[0];
                if( layout >= 0 && layout <= 2 )
                {
                    host->client_layout_mode = layout;
                    host->client_layout_dirty = true;
                }
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_SET_DEFAULT_WINDOW_MODE:
        if( request->u.window_mode.mode != CS2VM_WINDOW_MODE_FIXED &&
            request->u.window_mode.mode != CS2VM_WINDOW_MODE_RESIZABLE )
            return CS2VM_EXECNO_OK;
        host->default_window_mode = request->u.window_mode.mode;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG:
    {
        struct RS_CS2SocialSend send;

        /* An empty answer is not a zero: the opcode's callers always push a
         * rendered number, so nothing to send means nothing happened. */
        if( !request->u.resume_countdialog.text || !request->u.resume_countdialog.text[0] )
            return CS2VM_EXECNO_OK;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_RESUME_COUNTDIALOG;
        snprintf(
            send.text, sizeof(send.text), "%s", request->u.resume_countdialog.text);
        rs_cs2_social_send_push(host, &send);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VIEWPORT:
        return exec_viewport(host, vm, request->u.viewport);

    case CS2VM_HOST_REQUEST_UIZOOM:
        return exec_uizoom(host, vm, request->u.uizoom);

    case CS2VM_HOST_REQUEST_SAFEAREA:
        return exec_safearea(vm, request->u.safearea);

    case CS2VM_HOST_REQUEST_CAM_GETYAW:
        return CS2VM2_PushInt(vm, host->cam_yaw);

    /* Pitch is clamped to the range the orbit camera can actually reach, so a
     * script that reads, adjusts and writes back cannot walk the camera out of
     * bounds one call at a time. */
    case CS2VM_HOST_REQUEST_CAM_FORCEANGLE:
    {
        int angle_x = request->u.cam_force_angle.angle_x;
        if( angle_x < 128 )
            angle_x = 128;
        if( angle_x > 383 )
            angle_x = 383;
        host->cam_angle_x = angle_x;
        host->cam_angle_y = request->u.cam_force_angle.angle_y & 0x7ff;
        host->cam_yaw = host->cam_angle_y;
        host->cam_angle_forced = true;
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CAM_GETANGLE_XA:
        return CS2VM2_PushInt(vm, host->cam_angle_x);

    case CS2VM_HOST_REQUEST_CAM_GETANGLE_YA:
        return CS2VM2_PushInt(vm, host->cam_angle_y);

    case CS2VM_HOST_REQUEST_WORLDMAP:
        return exec_worldmap(host, vm, request->u.worldmap);

    case CS2VM_HOST_REQUEST_MEC:
        return exec_mec(host, vm, request->u.mec);

    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
        return CS2VM_EXECNO_OK;

    /* ---- IF getters ---- */
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, request->u.if_get_width.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutHeight(tree, request->u.if_get_height.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, request->u.if_getx.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, request->u.if_get_width.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    {
        int parent =
            tree ? rs_cs2_parent_component_id(tree, request->u.if_get_layer.component_id) : -1;
        return CS2VM2_PushInt(vm, parent >= 0 ? parent : -1);
    }

    case CS2VM_HOST_REQUEST_IF_GETTOP:
        return CS2VM2_PushInt(vm, host->top_interface_id);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
        node = rs_cs2_node(host, request->u.if_get_scroll_x.component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_x : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
        node = rs_cs2_node(host, request->u.if_get_scroll_y.component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_y : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
        node = rs_cs2_node(host, request->u.if_get_scroll_height.component_id);
        return CS2VM2_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_height : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        node = rs_cs2_node(host, request->u.if_getscrollwidth.component_id);
        return CS2VM2_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_width : 0);

    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        node = rs_cs2_node(host, request->u.if_get_width.component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_IF_HASSUB:
    {
        /* A component "has a sub" when an interface group is mounted into it
         * (IF_OPENSUB target). The InterfaceParent map records exactly that. */
        int cid = request->u.if_get_width.component_id;
        int has = tree && UITree_InterfaceParentFind(tree, cid) >= 0;
        if( getenv("TORIRS_HASSUB_DEBUG") )
            fprintf(
                stderr,
                "hassub: query 0x%08x (%d|%d) -> %d  (parent_count=%d)\n",
                (unsigned)cid,
                (cid >> 16) & 0xffff,
                cid & 0xffff,
                has,
                tree ? tree->interface_parent_count : -1);
        return CS2VM2_PushInt(vm, has ? 1 : 0);
    }

    case CS2VM_HOST_REQUEST_IF_HASCHILD:
    {
        /* 2704/2705: widget has the given parent group mounted (rev 634 does
         * not distinguish modal vs overlay on the type field). */
        int cid = request->u.if_has_child.component_id;
        int want = request->u.if_has_child.group_id;
        int idx = tree ? UITree_InterfaceParentFind(tree, cid) : -1;
        int has = 0;
        if( idx >= 0 && tree->interface_parents[idx].group_id == want )
            has = 1;
        return CS2VM2_PushInt(vm, has);
    }

    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            rs_cs2_get_text(tree, request->u.if_gettext.component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, buf));
    }

    case CS2VM_HOST_REQUEST_IF_GETCOLOUR:
        node = rs_cs2_node(host, request->u.if_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->colour : 0);

    case CS2VM_HOST_REQUEST_IF_GETFILLCOLOUR:
        node = rs_cs2_node(host, request->u.if_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->fill_colour : 0);

    case CS2VM_HOST_REQUEST_IF_GETINVOBJECT:
        node = rs_cs2_node(host, request->u.if_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->item_id : 0);

    case CS2VM_HOST_REQUEST_IF_GETINVCOUNT:
        node = rs_cs2_node(host, request->u.if_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->item_count : 0);

    /* ---- IF / CC mutators ---- */
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( tree )
        {

            int was_hidden = 0;
            int32_t hide_idx;
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: IF_SETHIDE component_id=%d hide=%d\n",
                request->u.if_set_hide.component_id,
                request->u.if_set_hide.hidden ? 1 : 0);
#endif
            if( getenv("TORIRS_SETHIDE_DEBUG") )
            {
                int g = (request->u.if_set_hide.component_id >> 16) & 0xffff;
                if( g == 149 || g == 320 || g == 218 ||
                    (g == 161 && (request->u.if_set_hide.component_id & 0xffff) >= 73) )
                    fprintf(
                        stderr,
                        "sethide: component 0x%08x (%d|%d) hide=%d found=%d\n",
                        (unsigned)request->u.if_set_hide.component_id,
                        g,
                        request->u.if_set_hide.component_id & 0xffff,
                        request->u.if_set_hide.hidden ? 1 : 0,
                        UITree_FindByComponentId(tree, request->u.if_set_hide.component_id) >= 0
                            ? 1
                            : 0);
            }
            hide_idx = UITree_FindByComponentId(tree, request->u.if_set_hide.component_id);
            if( hide_idx >= 0 )
                was_hidden = tree->components[hide_idx].behavior.hide ? 1 : 0;
            (void)UITree_ApplyHide(
                tree, request->u.if_set_hide.component_id, request->u.if_set_hide.hidden ? 1 : 0);
            /* Unhide → mark widgets-loaded (TS markWidgetsLoaded). Consumed once per
             * logic tick by RS_CS2_PumpTransmits; per-hook serials keep already-fired
             * hooks from re-running, so this no longer re-dispatches everything. */
            if( was_hidden && !request->u.if_set_hide.hidden )
                host->widgets_loaded_dirty = 1;
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        if( tree )
            (void)UITree_ApplyPositionModes(
                tree,
                request->u.cc_set_position.component_id,
                request->u.cc_set_position.x,
                request->u.cc_set_position.y,
                request->u.cc_set_position.xmode,
                request->u.cc_set_position.ymode);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        if( tree )
        {
            /* TORIRS_DUMP_SETSIZE=<group>: every size write a script makes to
             * that interface, in order. The BOUNDS dump only shows where the
             * layout ended up; when a panel resolves to a few pixels this is
             * the only way to see which call did it and what it asked for. */
            if( getenv("TORIRS_DUMP_SETSIZE") )
            {
                int want = (int)strtol(getenv("TORIRS_DUMP_SETSIZE"), NULL, 0);
                int group = (request->u.cc_set_size.component_id >> 16) & 0xffff;
                if( group == want )
                    fprintf(
                        stderr,
                        "SETSIZE com=0x%08x (%d|%d) %dx%d modes=%d,%d\n",
                        (unsigned)request->u.cc_set_size.component_id,
                        group,
                        request->u.cc_set_size.component_id & 0xffff,
                        request->u.cc_set_size.width,
                        request->u.cc_set_size.height,
                        request->u.cc_set_size.wmode,
                        request->u.cc_set_size.hmode);
            }
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: SETSIZE component_id=%d size=%dx%d modes=%d,%d\n",
                request->u.cc_set_size.component_id,
                request->u.cc_set_size.width,
                request->u.cc_set_size.height,
                request->u.cc_set_size.wmode,
                request->u.cc_set_size.hmode);
#endif
            (void)UITree_ApplySizeModes(
                tree,
                request->u.cc_set_size.component_id,
                request->u.cc_set_size.width,
                request->u.cc_set_size.height,
                request->u.cc_set_size.wmode,
                request->u.cc_set_size.hmode);
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
    {
        int cid = request->u.if_set_scroll_pos.component_id;
        int sx = request->u.if_set_scroll_pos.scroll_x;
        int sy = request->u.if_set_scroll_pos.scroll_y;
        node = rs_cs2_node(host, cid);
        if( node && node->type == UIELEM_RS_LAYER )
        {
            /* Clamp against current computed bounds (reference ensureWidgetLayout
             * before the scroll clamp). */
            UITree_EnsureLayout(tree);
            int max_x = UITree_ScrollMaxX(node);
            int max_y = UITree_ScrollMaxY(node);
            if( sx < 0 )
                sx = 0;
            if( sx > max_x )
                sx = max_x;
            if( sy < 0 )
                sy = 0;
            if( sy > max_y )
                sy = max_y;
            (void)UITree_ApplyScrollPos(tree, cid, sx, sy);
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
        if( tree && UITree_ApplyScrollSize(
                        tree,
                        request->u.if_set_scroll_size.component_id,
                        request->u.if_set_scroll_size.scroll_width,
                        request->u.if_set_scroll_size.scroll_height) )
        {
            /* Reference revalidateWidgetScroll: re-clamp scroll offsets after
             * the scroll area changes. */
            node = rs_cs2_node(host, request->u.if_set_scroll_size.component_id);
            if( node && node->type == UIELEM_RS_LAYER )
            {
                UITree_EnsureLayout(tree);
                UITree_ScrollClampComponent(node);
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return exec_set_graphic(host, vm, request->u.cc_set_graphic);

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
        node = rs_cs2_node(host, request->u.cc_set_graphic2.component_id);
        if( node && node->type == UIELEM_RS_GRAPHIC )
        {
            node->u.rs_graphic.scene_id_active = request->u.cc_set_graphic2.graphic_id;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_graphic2.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETTEXT component_id=%d text=\"%.48s\"\n",
            request->u.cc_set_text.component_id,
            request->u.cc_set_text.text ? request->u.cc_set_text.text : "");
#endif
        if( tree )
            (void)UITree_ApplyText(
                tree, request->u.cc_set_text.component_id, request->u.cc_set_text.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, request->u.if_set_outline.component_id, request->u.if_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, request->u.cc_set_outline.component_id, request->u.cc_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTILING:
        if( tree )
            (void)UITree_ApplyGraphicTiled(
                tree, request->u.cc_set_tiling.component_id, request->u.cc_set_tiling.tiling);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
        if( tree )
            (void)UITree_ApplyGraphicShadow(
                tree,
                request->u.cc_set_graphic_shadow.component_id,
                request->u.cc_set_graphic_shadow.shadow);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
        if( tree )
            (void)UITree_ApplyColour(
                tree, request->u.cc_set_colour.component_id, request->u.cc_set_colour.colour);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETFILL:
        node = rs_cs2_node(host, request->u.cc_set_fill.component_id);
        if( node && node->type == UIELEM_RS_RECT )
        {
            node->u.rs_rect.filled = request->u.cc_set_fill.filled ? 1 : 0;
            UITree_MarkNodeDirty(tree, rs_cs2_find_node(host, request->u.cc_set_fill.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        node = rs_cs2_node(host, request->u.cc_set_trans.component_id);
        if( node )
        {
            node->trans = request->u.cc_set_trans.trans;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_trans.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        node = rs_cs2_node(host, request->u.cc_set_no_click_through.component_id);
        if( node )
        {
            node->no_click_through = request->u.cc_set_no_click_through.enabled ? 1 : 0;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_no_click_through.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        return exec_set_text_font(host, vm, request->u.cc_set_text_font);

    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        if( tree )
            (void)UITree_ApplyTextAlign(
                tree,
                request->u.cc_set_text_align.component_id,
                request->u.cc_set_text_align.x_align,
                request->u.cc_set_text_align.y_align,
                request->u.cc_set_text_align.line_height);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        if( tree )
            (void)UITree_ApplyTextShadow(
                tree,
                request->u.cc_set_text_shadow.component_id,
                request->u.cc_set_text_shadow.shadowed);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLE:
        node = rs_cs2_node(host, request->u.cc_set_draggable.component_id);
        if( node )
        {
            node->draggable = 1;
            node->drag_render_area_uid = request->u.cc_set_draggable.parent_uid;
            node->drag_render_area_child_index = request->u.cc_set_draggable.child_index;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLEBEHAVIOR:
        node = rs_cs2_node(host, request->u.cc_set_draggable_behavior.component_id);
        if( node )
        {
            node->drag_behavior = request->u.cc_set_draggable_behavior.behavior;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable_behavior.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_zone.component_id);
        if( node )
        {
            node->drag_dead_zone = (uint8_t)request->u.cc_set_drag_dead_zone.zone;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_drag_dead_zone.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_time.component_id);
        if( node )
        {
            node->drag_dead_time = (uint8_t)request->u.cc_set_drag_dead_time.time;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_drag_dead_time.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        return exec_set_object(
            host,
            vm,
            request->u.if_set_object.component_id,
            request->u.if_set_object.obj_id,
            request->u.if_set_object.count,
            request->u.if_set_object.num_mode);

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
        return exec_set_object(
            host,
            vm,
            request->u.cc_set_object.component_id,
            request->u.cc_set_object.obj_id,
            request->u.cc_set_object.count,
            request->u.cc_set_object.num_mode);

    case CS2VM_HOST_REQUEST_CC_DELETEALL:
    {
        int32_t parent_idx =
            tree ? UITree_FindByComponentId(tree, request->u.cc_delete_all.component_id) : -1;
        if( parent_idx >= 0 )
            UITree_CcDeleteAll(tree, parent_idx);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_DELETE:
    {
        /* One child, not a parent's whole list. `UITree_CcDelete` frees the
         * node and its subtree and leaves the parent's remaining children in
         * place — the sub-ids of the survivors do not shift, which is what a
         * script deleting row 3 of a list expects. */
        int32_t idx = tree ? UITree_FindByComponentId(tree, request->u.cc_delete.component_id) : -1;
        if( idx >= 0 )
            UITree_CcDelete(tree, idx);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_CREATE:
        return exec_cc_create(host, vm, request->u.cc_create);
    case CS2VM_HOST_REQUEST_CC_COPY:
        return exec_cc_copy(host, vm, request->u.cc_copy);

    case CS2VM_HOST_REQUEST_CC_FIND:
        return exec_cc_find(host, vm, request->u.cc_find);

    case CS2VM_HOST_REQUEST_IF_FIND:
        return exec_if_find(host, vm, request->u.if_find);

    case CS2VM_HOST_REQUEST_CC_FINDROOT:
    {
        int found = 0;
        int parent =
            tree ? rs_cs2_parent_component_id(tree, request->u.cc_findroot.component_id) : -1;
        if( parent >= 0 )
        {
            rs_cs2_set_cc_target(vm, request->u.cc_findroot.dot_operand, parent);
            found = 1;
        }
        return CS2VM2_PushInt(vm, found);
    }

    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return exec_children_find(
            host,
            vm,
            request->u.cc_children_find.parent_id,
            request->u.cc_children_find.start_index,
            0,
            0,
            CS2VM_HOST_REQUEST_CC_CHILDREN_FIND);

    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return exec_children_find(
            host,
            vm,
            request->u.if_children_find.uid,
            request->u.if_children_find.start_index,
            1,
            request->u.if_children_find.dot_operand,
            CS2VM_HOST_REQUEST_IF_CHILDREN_FIND);

    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    {
        int parent =
            tree ? rs_cs2_parent_component_id(tree, request->u.cc_resolve_parent.component_id) : -1;
        if( parent < 0 )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, parent);
    }

    case CS2VM_HOST_REQUEST_CC_GETID:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        assert(node);

        return CS2VM2_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);

    case CS2VM_HOST_REQUEST_CC_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutHeight(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            rs_cs2_get_text(tree, request->u.cc_gettext.component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, buf));
    }

    case CS2VM_HOST_REQUEST_CC_GETCOLOUR:
        node = rs_cs2_node(host, request->u.cc_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->colour : 0);

    case CS2VM_HOST_REQUEST_CC_GETFILLCOLOUR:
        node = rs_cs2_node(host, request->u.cc_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->fill_colour : 0);

    case CS2VM_HOST_REQUEST_CC_GETINVOBJECT:
        node = rs_cs2_node(host, request->u.cc_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->item_id : 0);

    case CS2VM_HOST_REQUEST_CC_GETINVCOUNT:
        node = rs_cs2_node(host, request->u.cc_gettext.component_id);
        return CS2VM2_PushInt(vm, node ? node->item_count : 0);

    case CS2VM_HOST_REQUEST_CC_GETTRANS:
        node = rs_cs2_node(host, request->u.cc_gettrans.component_id);
        return CS2VM2_PushInt(vm, node ? node->trans : 0);

    case CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM:
        return exec_cc_getcomponentparam(host, vm, request->u.cc_component_param);

    case CS2VM_HOST_REQUEST_IF_GETCOMPONENTPARAM:
        return exec_if_getcomponentparam(host, vm, request->u.cc_component_param);

    case CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM:
        if( tree )
            (void)UITree_ApplyComponentParam(
                tree,
                request->u.cc_component_param.component_id,
                request->u.cc_component_param.param_id,
                request->u.cc_component_param.value,
                request->u.cc_component_param.str_value);
        return CS2VM_EXECNO_OK;

    /* ---- Ops ---- */
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOP:
        if( tree )
            rs_cs2_apply_op(
                tree,
                request->u.if_set_op.component_id,
                request->u.if_set_op.index,
                request->u.if_set_op.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
        if( tree )
            (void)UITree_ApplyOpBase(
                tree, request->u.if_set_op_base.component_id, request->u.if_set_op_base.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
        if( tree )
            rs_cs2_apply_op_submenu(
                tree,
                request->u.if_set_op_submenu.component_id,
                request->u.if_set_op_submenu.op_index,
                request->u.if_set_op_submenu.sub_index,
                request->u.if_set_op_submenu.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLEAROPSUBMENU:
        if( tree )
            (void)UITree_ClearOpSubmenu(
                tree,
                request->u.if_clear_op_submenu.component_id,
                request->u.if_clear_op_submenu.op_index);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        if( tree )
            (void)UITree_ApplyTargetPriority(
                tree,
                request->u.if_set_target_priority.component_id,
                request->u.if_set_target_priority.priority);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY:
        if( tree )
            (void)UITree_ApplyOpKey(
                tree,
                request->u.widget_set_opkey.component_id,
                request->u.widget_set_opkey.op_index,
                request->u.widget_set_opkey.key_chars,
                request->u.widget_set_opkey.key_codes,
                request->u.widget_set_opkey.pair_count);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY_RATE:
        if( tree )
        {
            if( request->u.widget_set_opkey_rate.ignore_held )
                (void)UITree_ApplyOpKeyIgnoreHeld(
                    tree,
                    request->u.widget_set_opkey_rate.component_id,
                    request->u.widget_set_opkey_rate.op_index);
            else
                (void)UITree_ApplyOpKeyRate(
                    tree,
                    request->u.widget_set_opkey_rate.component_id,
                    request->u.widget_set_opkey_rate.op_index,
                    request->u.widget_set_opkey_rate.rate,
                    request->u.widget_set_opkey_rate.enabled);
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        if( tree )
            rs_cs2_clear_ops(tree, request->u.if_clear_ops.component_id);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CALLONRESIZE:
        /* Queued, not run: this is reached from inside a running CS2 script and
         * the host has no runner to nest a second one on. See the queue's
         * comment in rs_cs2_host.h for why deferring is safe for every call
         * site in this cache. */
        rs_cs2_call_on_resize_push(host, request->u.if_call_on_resize.component_id);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_TRIGGEROP:
        /* Queued for the same reason as IF_CALLONRESIZE above. */
        rs_cs2_trigger_op_push(
            host,
            request->u.cc_trigger_op.component_id,
            request->u.cc_trigger_op.op_index);
        return CS2VM_EXECNO_OK;

    /* ---- SetOn (hooks / no-ops) ---- */
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return exec_set_on_var_transmit(host, &request->u.if_set_on_var_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return exec_set_on_inv_transmit(host, &request->u.if_set_on_inv_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONSTATTRANSMIT:
        /* Same request shape as the var one — script, captured args, a trigger
         * list — so it shares the payload struct. The trigger ids are stat ids
         * rather than varp ids, which is the only difference and is the
         * dispatcher's business, not this one's. */
        return exec_set_on_stat_transmit(host, &request->u.if_set_on_var_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONHOLD:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        /* Was `return CS2VM_EXECNO_OK` — a well-formed request the VM had
         * already built, thrown away. Registering it is the same call every
         * other SetOn makes; the walker that fires them is
         * CreateTask_CS2MiscTransmitDispatch. */
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT:
        /* The friends/ignore panels' only repaint trigger. Walked by
         * CreateTask_CS2FriendTransmitDispatch. */
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_friend_transmit);
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return exec_set_on_cc_event(host, vm, request->kind, &request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
        return exec_set_on_cc_transmit(host, vm, request->kind, &request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_SETANTIDRAG:
        if( tree )
            tree->anti_drag = request->u.widget_set_int.value ? 1 : 0;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_DRAGPICKUP:
    case CS2VM_HOST_REQUEST_CC_DRAGPICKUP:
        /* Demo: mark component drag_active; full pickup offsets applied by input loop. */
        node = rs_cs2_node(host, request->u.widget_set_int.component_id);
        if( node )
        {
            int32_t const drag_idx =
                rs_cs2_find_node(host, request->u.widget_set_int.component_id);
            node->draggable = 1;
            UITree_SetComponentDragActive(tree, drag_idx, 1);
            UITree_MarkNodeDirty(tree, drag_idx);
        }
        return CS2VM_EXECNO_OK;

    /* ---- Widget extras ---- */
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return exec_widget_set_int(host, vm, request->u.widget_set_int);

    case CS2VM_HOST_REQUEST_WIDGET_SET_INT2:
        /* No UITree fields for paired int setters yet. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        return exec_widget_set_model(host, vm, request->u.widget_set_model);

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE:
        return exec_widget_set_model_angle(host, vm, request->u.widget_set_model_angle);

    case CS2VM_HOST_REQUEST_WIDGET_SET_ARC:
        /* UITree has no arc fields yet. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
        return exec_widget_set_model_kind(host, vm, request->u.widget_set_model_kind);

    case CS2VM_HOST_REQUEST_WIDGET_INPUT_INT:
        /* Input widget fields not on UITree yet. */
        return CS2VM_EXECNO_OK;

    default:
        fprintf(stderr, "RS_CS2Host_Exec: UNHANDLED request kind %d\n", (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}
