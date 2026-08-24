#include "game/rs_cs2_host.h"

#include "game/rs_chat.h"
#include "game/rs_loot_store.h"
#include "game/rs_player_stats.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_db.h"
#include "engine/torirs_component_hook.h"
#include "engine/torirs_types.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_worldmap.h"
#include "inv/inv_manager.h"
#include "perf/torirs_perf.h"
#include "revconfig/revconfig_refs.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_scroll.h"
#include "toridraw_font.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int clamp_percent(int value);

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

static int
torirs_trace_drag(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* e = getenv("TORIRS_TRACE_DRAG");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached;
}

/* Resolved once: getenv locks and scans the whole environment, and this gate sits
 * on cc_create, which a container rebuild issues once per row. */
static int
torirs_cc_debug(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* e = getenv("TORIRS_CC_DEBUG");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached;
}

/*
 * UIZOOM_RESET / UIZOOM_GETDEFAULT constant.
 *
 * 100, not 1000: the one caller of GETDEFAULT in the cache is script_3334
 * ("Reset interface scaling"), and its whole body is
 * `deviceoption_set(27, uizoom_getdefault)` — it feeds the value straight into
 * the interface-scale option, whose domain script_3054 states as
 * max(~script3333, min(400, v)) with ~script3333 = 100 on desktop. A 1000 here
 * (the value this held while nothing consumed it) made the reset button ask
 * for 1000%, which the clamp then turns into 400%.
 */
#define RS_CS2_UIZOOM_DEFAULT RS_CS2_UI_SCALE_MIN

/*
 * Select the public payload through the request's exact opcode arm.  Shared
 * CS2VM_HostPayload_* layouts are private implementation details; no consumer
 * may recover a layout by naming some other opcode's union member.
 */
static void const*
rs_cs2_exact_payload(struct CS2VM_HostRequest const* request)
{
    assert(request);
    switch( request->kind )
    {
#define CS2VM_HOST_REQUEST_KIND(name, opcode, layout) \
    case CS2VM_HOST_REQUEST_##name:                    \
        return &request->u.name.payload;
#include "cs2vm2/cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
    }
    assert(0 && "unknown exact CS2 host-request kind");
    return NULL;
}

#define RS_CS2_PAYLOAD(request, layout) \
    ((struct CS2VM_HostPayload_##layout const*)rs_cs2_exact_payload(request))

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

/*
 * `if_gettargetmask` / `cc_gettargetmask` — deob
 * `method7577(method12093(events, widget))`, i.e. the target-type bits of the
 * component's EFFECTIVE event flags: the server's IF_SETEVENTS override where
 * one exists, the cache's decoded flags where it does not.
 *
 * The decoded half is pre-extracted onto the node (behavior.target_mask) so the
 * two cache generations' different storage is settled once, at decode. Only the
 * override arm has to do the shift here, and it does it on the raw events word
 * because that is what IF_SETEVENTS carries.
 */
static int
rs_cs2_target_mask(
    struct RS_CS2Host* host,
    int component_id)
{
    struct UITreeComponent const* node;
    int events = 0;

    if( host->events_override_for_component &&
        host->events_override_for_component(host->events_user, component_id, &events) )
        return (events >> TORIRS_TARGET_MASK_IF3_SHIFT) & TORIRS_TARGET_MASK_IF3_BITS;
    node = rs_cs2_node(host, component_id);
    return node ? (int)node->behavior.target_mask : 0;
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
 * and records what this VM thread is waiting for; `rs_cs2_await_spent` tells
 * the handler on the retry that this exact wait already happened, so a
 * resource that is still missing is genuinely absent and the handler must
 * complete with a default rather than yield again.
 *
 * The record must be per thread. The host is shared by every queued
 * Task_CS2Run, so a host-global record is overwritten or cleared whenever a
 * different script executes while this one is parked on IO.
 *
 * `id2` carries the second resource of a two-resource request (obj + param
 * type, struct + param type); pass -1 when there is only one.
 */
static bool
rs_cs2_await_spent(
    struct CS2VM2_Thread* thread,
    enum CS2VM_HostRequestKind kind,
    int id,
    int id2)
{
    assert(thread);
    return thread->has_awaited && thread->awaited_kind == kind && thread->awaited_id == id &&
           thread->awaited_id2 == id2;
}

static int
rs_cs2_yield_load(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest const* request,
    int id,
    int id2)
{
    assert(host);
    assert(thread);
    assert(request);
    thread->awaited_kind = request->kind;
    thread->awaited_id = id;
    thread->awaited_id2 = id2;
    thread->has_awaited = true;
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
    struct CS2VM2_Thread* thread,
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

    if( rs_cs2_await_spent(thread, request->kind, group_id, -1) )
        return CS2VM_EXECNO_OK;

    if( getenv("TORIRS_CS2_MOUNT_DEBUG") )
        fprintf(
            stderr,
            "cs2-automount: group %d requested via component 0x%08x (req kind=%d)\n",
            group_id,
            (unsigned)component_id,
            (int)request->kind);
    return rs_cs2_yield_load(host, thread, request, group_id, -1);
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

/* Resolve a resident multiNpc chain under this client's vars. false means a
 * config in the selected chain is still cold; true with -1 means the selected
 * positional entry intentionally hides the NPC. */
static bool
rs_cs2_npc_multi_resolve(
    struct RS_CS2Host* host,
    int npc_id,
    int* out_npc_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);

    assert(out_npc_id);
    *out_npc_id = npc_id;
    for( int depth = 0; depth <= TORIRS_NPC_MULTI_MAX_DEPTH && npc_id >= 0; depth++ )
    {
        struct ToriRS_Npctype* npc;
        int next;

        if( !provider || !CacheProvider_NpctypeHas(provider, npc_id) )
            return false;
        npc = CacheProvider_NpctypeGet(provider, npc_id);
        assert(npc);
        if( npc->transform_count <= 0 || !npc->transforms )
        {
            *out_npc_id = npc_id;
            return true;
        }
        next = host->varps
                   ? VarPManager_ResolveTransform(
                         host->varps,
                         npc->transforms,
                         npc->transform_count,
                         npc->transform_varbit,
                         npc->transform_varp)
                   : npc->transforms[npc->transform_count - 1];
        if( next < 0 )
        {
            *out_npc_id = -1;
            return true;
        }
        if( next == npc_id )
        {
            *out_npc_id = npc_id;
            return true;
        }
        if( depth == TORIRS_NPC_MULTI_MAX_DEPTH )
        {
            *out_npc_id = npc_id;
            return true;
        }
        npc_id = next;
    }
    *out_npc_id = npc_id;
    return true;
}

/* True when the selected npctype is resident and every non-negative chathead
 * model id is resident. Empty/missing heads count as ready so we do not yield
 * forever — EnsureNpcHead still returns -1 and the widget stays unchanged. */
static bool
rs_cs2_npc_head_ready(
    struct RS_CS2Host* host,
    int npc_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Npctype* npc;
    int resolved_npc_id;
    int i;

    if( npc_id < 0 )
        return true;
    if( !rs_cs2_npc_multi_resolve(host, npc_id, &resolved_npc_id) )
        return false;
    if( resolved_npc_id < 0 )
        return true;
    npc = CacheProvider_NpctypeGet(provider, resolved_npc_id);
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
        UITree_MenuOptionsMut(&tree->components[idx])->ops[index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    UITree_MenuOptionsMut(&tree->components[idx])->ops[index - 1][UITREE_MENU_OPTION_LEN - 1] =
        '\0';
    /* TORIRS_OPS_DEBUG=1: which script wrote which verb onto which component.
     * The rev-230 inventory's rows are entirely script-assigned, so a wrong or
     * missing verb there is invisible until the menu is opened. */
    static int ops_debug = -1;
    if( ops_debug < 0 )
        ops_debug = getenv("TORIRS_OPS_DEBUG") != NULL;
    if( ops_debug )
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
        UITree_MenuOptionsMut(&tree->components[idx])->ops[i][0] = '\0';
    UITree_MenuOptionsMut(&tree->components[idx])->option[0] = '\0';
    UITree_MenuSubmenuClear(UITree_MenuOptionsMut(&tree->components[idx]), 0);
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
        UITree_MenuOptionsMut(&tree->components[idx]), op_index, sub_index, text ? text : "");
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
 * The wrapping rules must match the renderer for the same reason, and the same
 * dodge is not available: `ToriDraw2D_WrapLineCount` / `WrapMaxLineWidth` want a
 * `ToriDraw_Font`, this side holds a `ToriRS_Font`, and the bridge between them
 * is a deep copy — not something to do per measurement. So the walk below is a
 * restatement of `font_wrap_segment_line_count` and `font_line_break_at`
 * (3rd/toridraw/toridraw_font.c) and has to stay one: break at `\n`, CRLF, CR,
 * LF and `<br>`; wrap between WORDS, never inside one.
 *
 * What it cost: the journal's summary panel centres each cell's icon+value pair
 * on `parawidth(value)`. Every value is colour-tagged
 * (`<col=0dc10d>0</col>` — 19 bytes rendering one glyph), so the measurement
 * came back 104 instead of 6, and the icon was placed at
 * `centre - (104 + 18 + 4)/2` — eighteen pixels off the left edge of the panel,
 * with the number stranded at the far left of a 104-wide box.
 */
/*
 * Is there an explicit line break at `p`? The renderer's `font_line_break_at`,
 * restated: literal backslash-n, CRLF, a bare CR or LF, and `<br>`.
 */
static int
rs_cs2_line_break_at(char const* p)
{
    assert(p);
    if( !p[0] )
        return 0;
    if( p[0] == '\\' && p[1] == 'n' )
        return 2;
    if( p[0] == '\r' && p[1] == '\n' )
        return 2;
    if( p[0] == '\n' || p[0] == '\r' )
        return 1;
    if( p[0] == '<' && (p[1] == 'b' || p[1] == 'B') && (p[2] == 'r' || p[2] == 'R') &&
        p[3] == '>' )
        return 4;
    return 0;
}

/* The drawn width of one span, markup consumed the way the renderer consumes it. */
static int
rs_cs2_measure_span(
    struct ToriRS_Font const* font,
    char const* text,
    int len)
{
    int width = 0;

    for( int i = 0; i < len; )
    {
        unsigned char emitted = 0;
        int const token = ToriDraw_FontMarkupTokenLength(text, len, i, &emitted);

        if( token > 0 )
        {
            i += token;
            if( emitted )
                width += font->draw_width[emitted];
            continue;
        }
        width += font->draw_width[(unsigned char)text[i]];
        i++;
    }
    return width;
}

static void
rs_cs2_font_wrap(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width,
    int* out_lines,
    int* out_width)
{
    int lines = 0;
    int best = 0;
    char const* rest;

    assert(font);
    *out_lines = 0;
    *out_width = 0;
    assert(text);
    if( !text[0] )
        return;

    rest = text;
    for( ;; )
    {
        char const* p = rest;
        int brk = 0;
        int span_len;
        int cur_w = 0;
        int word_start = 0;
        int space_adv = font->draw_width[(unsigned char)' '];

        while( *p && (brk = rs_cs2_line_break_at(p)) == 0 )
            p++;
        span_len = (int)(p - rest);
        lines++;

        /* Word wrapping, one word at a time, breaking BETWEEN words — which is
         * the only rule the renderer knows. Walking character by character
         * instead fits more onto each line and so reports fewer lines than are
         * drawn, and every caller that sizes a box from `paraheight` then clips
         * its own text. That is what cut the Ancient Curses tooltips off
         * mid-word: five drawn lines measured as four. */
        for( int i = 0; i <= span_len; i++ )
        {
            int word_len;
            int word_w;
            int candidate;

            if( i < span_len && rest[i] != ' ' )
                continue;
            word_len = i - word_start;
            if( word_len <= 0 )
            {
                word_start = i + 1;
                continue;
            }
            word_w = rs_cs2_measure_span(font, rest + word_start, word_len);
            candidate = cur_w == 0 ? word_w : cur_w + space_adv + word_w;
            if( cur_w > 0 && max_width > 0 && candidate > max_width )
            {
                lines++;
                if( cur_w > best )
                    best = cur_w;
                cur_w = word_w;
            }
            else
                cur_w = candidate;
            word_start = i + 1;
        }
        if( cur_w > best )
            best = cur_w;

        if( brk == 0 )
            break;
        rest = p + brk;
    }

    if( max_width > 0 && best > max_width )
        best = max_width;
    *out_lines = lines > 0 ? lines : 1;
    *out_width = best;
}

static int
rs_cs2_font_wrap_line_count(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width)
{
    int lines, width;
    assert(text);
    if( !text[0] )
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

/*
 * A cache id the profile named, or -1.
 *
 * NULL refs is a table with nothing in it, not a reason to substitute a
 * literal: an embedding with no profile gets a host whose id-driven features
 * are all off, which is the only honest answer when nobody has said what
 * cache this is.
 */
static int
cs2_host_ref(
    struct RevConfigRefs const* refs,
    char const* kind,
    char const* name)
{
    assert(kind);
    assert(name);
    return refs ? RevConfigRefs_Get(refs, kind, name) : -1;
}

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RevConfigRefs const* refs)
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
    host->local_coord = 0;
    host->dest_coord = -1;
    host->hover_coord = -1;
    /* The three tile-highlight TRIGGER scripts: trigger_48, trigger_49 and
     * trigger_47 -- see the header for what fires each and why the
     * [clientscript] apply forms beside them are the cache's to run and not
     * this client's. Which ids those are is the profile's answer, because a
     * cache that predates the feature has no such scripts and must not be
     * told to run rev-239's numbers. */
    host->script_highlight_hover_tile = cs2_host_ref(refs, "script", "highlight_hover_tile");
    host->script_highlight_current_tile = cs2_host_ref(refs, "script", "highlight_current_tile");
    host->script_highlight_dest_tile = cs2_host_ref(refs, "script", "highlight_dest_tile");
    /* -1, not 0: 0 is a real player slot, so a zero here would make `_6905`
     * name whichever player the server put in slot 0 before login. */
    host->local_pid = -1;
    host->top_interface_id = -1;
    host->mouse_x = -1;
    host->mouse_y = -1;
    host->cam_follow_height = 0;
    /* The option store is the authority on the boot volumes -- App_Init pushes
     * these at the audio engine and seeds interface 116's varps from them, so
     * all three agree before the first slider update arrives. */
    for( int id = 0; id < RS_CS2_OPTION_MAX; id++ )
    {
        host->game_options[id] = RS_CS2Host_OptionDefault(RS_CS2_OPTION_GAME, id);
        host->device_options[id] = RS_CS2Host_OptionDefault(RS_CS2_OPTION_DEVICE, id);
    }
    host->volume_music = host->game_options[RS_CS2_GAMEOPTION_MUSIC_VOLUME];
    host->volume_sounds = host->game_options[RS_CS2_GAMEOPTION_SOUND_VOLUME];
    host->volume_area_sounds = host->game_options[RS_CS2_GAMEOPTION_AREA_VOLUME];
    /* Start at the low end of the documented 2..8 range; a settings panel or the
     * server can drive it. Real default is TBD once minimap zoom is rendered. */
    host->minimap_zoom = 2;
    host->logout_requested = false;
    host->close_modal_requested = false;
    host->resume_pausebutton_component_id = -1;
    /* Orbit-distance zoom endpoints (reference client.field780/field747, set at
     * client.java:4264). These are read by the follow camera every cycle, so the
     * defaults have to be the reference's, not the old GETZOOM placeholders —
     * 128/896 would have shrunk the orbit distance by half at a fixed viewport
     * and stretched it 3.5x at a tall one. */
    host->viewport_zoom = 256;
    host->viewport_zoom_max = 320;
    /* Reference default for the interpolation endpoints (class159.method5357
     * and VIEWPORT_GETEFFECTIVESIZE read them before any SETFOV has run). */
    host->viewport_zoom_near = 256;
    host->viewport_zoom_far = 256;
    /* The reference's own "unset" values for the two CLAMPFOV ranges, which is
     * also what `viewport_clampfov(0, 0, 0, 0)` restores. Not zero: zero is
     * outside both ranges and would letterbox the viewport away before any
     * script had asked for a clamp. */
    host->viewport_fov_min = 1;
    host->viewport_fov_max_clamp = 32767;
    host->viewport_aspect_min = 1;
    host->viewport_aspect_max = 32767;
    /* The interface scale is device_options[27], already seeded to 100% by the
     * OptionDefault loop above; a boot value is not a player choice, so it must
     * not raise ui_scale_dirty. */
    host->ui_scale_dirty = false;
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
    /* The Display panel's mode/apply pair (decompile names
     * settings_client_mode / settings_client_apply) and the varbit those apply
     * hubs write the pressed setting id into. */
    host->script_settings_client_mode = cs2_host_ref(refs, "script", "settings_client_mode");
    host->script_settings_client_apply = cs2_host_ref(refs, "script", "settings_client_apply");
    host->varbit_settings_last_changed = cs2_host_ref(refs, "varbit", "settings_last_changed");
    /* -1, not 0: script 0 is a real id, so zero would mirror every varbit write
     * made by whatever script happens to be id 0 before the panel is ever used. */
    host->settings_mirror_root_script = -1;
    host->settings_mirror_count = 0;
    /* settings_colour_input_click (the swatch's op) and settings_get_colour
     * (the read hub whose varp read names a row's varp). */
    host->script_settings_colour_click = cs2_host_ref(refs, "script", "settings_colour_click");
    host->script_settings_colour_get = cs2_host_ref(refs, "script", "settings_colour_get");
    host->settings_colour_count = 0;
    host->settings_colour_pending = false;
    RS_HighlightReset(&host->highlight);
    RS_ClientOpReset(&host->clientop);
    host->bridge = NULL;
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
    assert(host);
    /* Advance the serial so already-fired var-transmit hooks re-run, and flag the
     * per-tick pump to re-dispatch (TS parity: value changes bump changedVarpCount,
     * processed once per cycle rather than synchronously mid-script). */
    host->var_change_serial++;
    host->var_transmit_dirty = 1;
    if( torirs_cc_debug() )
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
RS_CS2Host_ScriptWriteVarp(
    struct RS_CS2Host* host,
    int varp_id,
    int value)
{
    int before;

    assert(host);
    if( !host->varps )
        return;
    before = VarPManager_GetVarp(host->varps, varp_id);
    VarPManager_SetVarpOptimistic(host->varps, varp_id, value);
    if( VarPManager_GetVarp(host->varps, varp_id) != before )
        RS_CS2Host_NotifyVarChanged(host, varp_id);
}

void
RS_CS2Host_ScriptWriteVarbit(
    struct RS_CS2Host* host,
    int varbit_id,
    int value)
{
    int base;
    int before;

    assert(host);
    if( !host->varps )
        return;
    /* Transmit hooks are keyed by varp, so the base varp is what "changed" as
     * far as a trigger list is concerned — the varbit id matches nothing. */
    base = VarPManager_VarbitBaseVar(host->varps, varbit_id);
    before = base >= 0 ? VarPManager_GetVarp(host->varps, base) : 0;
    VarPManager_SetVarbitOptimistic(host->varps, varbit_id, value);
    if( base >= 0 && VarPManager_GetVarp(host->varps, base) != before )
        RS_CS2Host_NotifyVarChanged(host, base);
}

void
RS_CS2Host_NotifyInvChanged(
    struct RS_CS2Host* host,
    int container_id)
{
    assert(host);
    host->inv_change_serial++;
    host->inv_transmit_dirty = 1;
    if( torirs_cc_debug() )
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
    assert(host);
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
    assert(host);
    host->misc_transmit_dirty = 1;
}

void
RS_CS2Host_SetSocial(
    struct RS_CS2Host* host,
    struct RS_Social* social,
    int* filter_modes,
    int world)
{
    assert(host);
    host->social = social;
    host->chat_filter_mode = filter_modes;
    host->map_world = world;
}

void
RS_CS2Host_NotifyFriendChanged(struct RS_CS2Host* host)
{
    assert(host);
    host->friend_transmit_dirty = 1;
}

void
RS_CS2Host_SetChat(
    struct RS_CS2Host* host,
    struct RS_Chat* chat)
{
    assert(host);
    host->chat = chat;
}

void
RS_CS2Host_ChatAdd(
    struct RS_CS2Host* host,
    int type,
    char const* name,
    char const* sender,
    char const* text)
{
    assert(host);
    if( !host->chat )
        return;
    RS_Chat_AddMessage(host->chat, type, name, sender, text, host->client_clock);
    host->chat_transmit_dirty = 1;
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
    assert(host);
    if( host->social_send_count <= 0 )
        return false;
    assert(out);
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
    assert(host);
    if( host->call_on_resize_count <= 0 )
        return false;
    assert(out_component_id);
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

/* Queue a sound one of the four SOUND_* opcodes asked the App to play. */
static void
rs_cs2_sound_push(
    struct RS_CS2Host* host,
    int kind,
    const struct CS2VM_HostPayload_Sound* sound)
{
    int slot;

    assert(host);
    assert(sound);
    if( host->sound_count >= RS_CS2_HOST_SOUND_MAX )
    {
        fprintf(
            stderr,
            "cs2: sound queue full (%d), dropped kind %d id %d\n",
            RS_CS2_HOST_SOUND_MAX,
            kind,
            sound->id);
        return;
    }
    slot = (host->sound_head + host->sound_count) % RS_CS2_HOST_SOUND_MAX;
    host->sound[slot].kind = kind;
    host->sound[slot].id = sound->id;
    host->sound[slot].secondary_id = sound->secondary_id;
    host->sound[slot].loops = sound->loops;
    host->sound[slot].delay = sound->delay;
    host->sound[slot].fade_out_delay = sound->fade_out_delay;
    host->sound[slot].fade_out_speed = sound->fade_out_speed;
    host->sound[slot].fade_in_delay = sound->fade_in_delay;
    host->sound[slot].fade_in_speed = sound->fade_in_speed;
    host->sound_count++;
}

bool
RS_CS2Host_TakeSettingsAction(
    struct RS_CS2Host* host,
    int* out_setting_id,
    int* out_value)
{
    assert(host);
    if( host->settings_action_count <= 0 )
        return false;
    assert(out_setting_id);
    assert(out_value);
    *out_setting_id = host->settings_action_id[0];
    *out_value = host->settings_action_value[0];
    host->settings_action_count--;
    for( int i = 0; i < host->settings_action_count; i++ )
    {
        host->settings_action_id[i] = host->settings_action_id[i + 1];
        host->settings_action_value[i] = host->settings_action_value[i + 1];
    }
    return true;
}

int
RS_CS2Host_SettingsColourVarp(
    struct RS_CS2Host const* host,
    int setting_id)
{
    assert(host);
    for( int i = 0; i < host->settings_colour_count; i++ )
    {
        if( host->settings_colour_setting[i] == setting_id )
            return host->settings_colour_varp[i];
    }
    return -1;
}

void
RS_CS2Host_ScriptStarted(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int component_id)
{
    struct CS2VM2_Frame* frame;
    struct CacheProvider* provider;
    struct ToriRS_Struct* setting;
    struct RS_CS2SettingsColourRequest* req;
    char const* label = NULL;
    int struct_id;
    int value;

    assert(host);
    assert(thread);
    if( host->script_settings_colour_click <= 0 || thread->frame_sp <= 0 )
        return;
    frame = thread->frames[thread->frame_sp - 1];
    if( !frame || !frame->script )
        return;
    if( frame->script->script_id != host->script_settings_colour_click )
        return;
    /* `settings_colour_input_click(struct, int)`. A frame with the wrong arity
     * is not this script however its id reads, and int_locals[1] on a
     * one-parameter frame is whatever was left in the slot. */
    if( frame->script->int_argument_count < 2 )
        return;

    /* The row's own gate, and the reason the script's first statement is
     * `if (~settings_op_checker($struct, $int) = 0) return`: a row blocked by
     * a requirement has already said so with a chat message, and opening a
     * picker over the top of that would be this client disagreeing with the
     * cache about whether the row can be used. */
    if( frame->int_locals[1] == 0 )
        return;

    struct_id = frame->int_locals[0];
    provider = rs_cs2_provider(host);
    setting = provider && struct_id >= 0 ? CacheProvider_StructGet(provider, struct_id) : NULL;
    if( !setting )
    {
        /* Never awaited, unlike STRUCT_PARAM's own handler: the panel read this
         * struct's title and description out of the cache to draw the row that
         * was just clicked, so a miss here is not a cold cache. */
        fprintf(stderr, "settings: colour row struct %d is not loaded\n", struct_id);
        return;
    }

    req = &host->settings_colour_request;
    memset(req, 0, sizeof(*req));
    req->component_id = component_id;
    if( !rs_cs2_struct_param_lookup(
            setting, RS_CS2_PARAM_SETTING_ID, NULL, &req->setting_id, NULL) )
    {
        fprintf(stderr, "settings: colour row struct %d carries no setting id\n", struct_id);
        return;
    }
    (void)rs_cs2_struct_param_lookup(
        setting, RS_CS2_PARAM_SETTING_COLOUR_DEFAULT, NULL, &req->default_colour, NULL);
    (void)rs_cs2_struct_param_lookup(
        setting, RS_CS2_PARAM_SETTING_LABEL, NULL, NULL, &label);
    if( label )
    {
        strncpy(req->label, label, sizeof(req->label) - 1);
        req->label[sizeof(req->label) - 1] = '\0';
    }

    req->varp_id = RS_CS2Host_SettingsColourVarp(host, req->setting_id);
    /* The varp stores `colour + 1` so that zero can mean "never chosen", which
     * is what the row's own `~settings_get_colour(id) ! null` test is reading
     * for -- and what makes the panel fall back to param_1230. */
    value = req->varp_id >= 0 && host->varps ? VarPManager_GetVarp(host->varps, req->varp_id) : 0;
    req->colour = value > 0 ? value - 1 : req->default_colour;

    if( torirs_cc_debug() )
        fprintf(
            stderr,
            "SETTINGS_COLOUR click setting=%d varp=%d colour=%06X default=%06X "
            "com=%d group=%d \"%s\"\n",
            req->setting_id,
            req->varp_id,
            (unsigned)req->colour,
            (unsigned)req->default_colour,
            req->component_id,
            req->component_id >> 16,
            req->label);

    host->settings_colour_pending = true;
}

bool
RS_CS2Host_TakeSettingsColourRequest(
    struct RS_CS2Host* host,
    struct RS_CS2SettingsColourRequest* out)
{
    assert(host);
    if( !host->settings_colour_pending )
        return false;
    assert(out);
    *out = host->settings_colour_request;
    host->settings_colour_pending = false;
    return true;
}

bool
RS_CS2Host_TakeSound(
    struct RS_CS2Host* host,
    struct RS_CS2Sound* out)
{
    assert(host);
    if( host->sound_count <= 0 )
        return false;
    assert(out);
    *out = host->sound[host->sound_head];
    host->sound_head = (host->sound_head + 1) % RS_CS2_HOST_SOUND_MAX;
    host->sound_count--;
    return true;
}

/*
 * The two option tables, exactly as the reference enumerates them.
 *
 * rev-239 deob: `class64` is the device table and `class67` the game table,
 * each entry a (handler ordinal, option id) pair. Reproduced here as ids
 * because that is all this client needs — which table an id belongs to, and
 * whether the reference keeps it on disk.
 *
 * `persist` is read off `class79`: an option whose handler calls a `class79`
 * setter is written to preferences<N>.dat, one that does not is session state.
 * Only two are not:
 *
 *   device 6  brightness — the handler calls the gamma helper directly.
 *   device 22 the file still carries a byte for it, but the loader reads that
 *             byte and forces the field false, so the stored value cannot
 *             survive a launch. Writing it would be persistence in name only.
 *
 * device 19 (master volume) is the one place this client is deliberately not
 * the reference: the reference's file has a master-volume field, but the live
 * value the mixer reads is a *different* field that the encoder never writes,
 * so a reference master volume does not come back. This client has one master
 * volume and keeps it, which is what that file field was plainly for.
 */
struct OptionSpec
{
    int id;
    bool persist;
};

static const struct OptionSpec device_option_spec[] = {
    { 2, true },   /* hide username on the login screen */
    { 3, true },   /* stored; nothing in rev 239 reads it back */
    { 4, true },   /* title music disabled */
    { 5, true },
    { 6, false },  /* brightness — applied on the spot, never saved */
    { 14, true },  /* draw distance */
    /* Interface scaling mode (enum_4033: nearest, linear, bicubic). This is
     * device-local and the All Settings scripts read/write it as option 15. */
    { RS_CS2_DEVICEOPTION_UI_SCALE_MODE, true },
    { 19, true },  /* master volume — see above */
    { 22, false }, /* retired: the reference discards it on load */
    /* Interface scaling. Persisted: it is a device preference in exactly the
     * sense the rest of this table means — chosen once, wanted at every
     * launch. Listing it here is also what makes CLIENTOPTION_SET/GET (3209 /
     * 3215, the generic id-keyed spelling) resolve 27 to the device table;
     * before it, RS_CS2Host_ClientOptionKind answered -1 and the write was
     * dropped on the floor. */
    { RS_CS2_DEVICEOPTION_UI_SCALE, true },
};

static const struct OptionSpec game_option_spec[] = {
    { RS_CS2_GAMEOPTION_HIDE_ROOFS, true },   /* 1 */
    { RS_CS2_GAMEOPTION_MUSIC_VOLUME, true }, /* 7 */
    { RS_CS2_GAMEOPTION_SOUND_VOLUME, true }, /* 8 */
    { RS_CS2_GAMEOPTION_AREA_VOLUME, true },  /* 9 */
};

static const struct OptionSpec*
option_spec(
    int kind,
    int option_id)
{
    const struct OptionSpec* table;
    size_t count;

    if( kind == RS_CS2_OPTION_DEVICE )
    {
        table = device_option_spec;
        count = sizeof(device_option_spec) / sizeof(device_option_spec[0]);
    }
    else if( kind == RS_CS2_OPTION_GAME )
    {
        table = game_option_spec;
        count = sizeof(game_option_spec) / sizeof(game_option_spec[0]);
    }
    else
        return NULL;

    for( size_t i = 0; i < count; i++ )
        if( table[i].id == option_id )
            return &table[i];
    return NULL;
}

int
RS_CS2Host_ClientOptionKind(int option_id)
{
    if( option_spec(RS_CS2_OPTION_DEVICE, option_id) )
        return RS_CS2_OPTION_DEVICE;
    if( option_spec(RS_CS2_OPTION_GAME, option_id) )
        return RS_CS2_OPTION_GAME;
    return -1;
}

int
RS_CS2Host_OptionPersists(
    int kind,
    int option_id)
{
    const struct OptionSpec* spec = option_spec(kind, option_id);

    return spec && spec->persist;
}

/* Whether an option id is one of the four the mixer follows. */
static bool
option_is_volume(
    int kind,
    int option_id)
{
    if( kind == RS_CS2_OPTION_GAME )
        return option_id == RS_CS2_GAMEOPTION_MUSIC_VOLUME ||
               option_id == RS_CS2_GAMEOPTION_SOUND_VOLUME ||
               option_id == RS_CS2_GAMEOPTION_AREA_VOLUME;
    return kind == RS_CS2_OPTION_DEVICE && option_id == RS_CS2_DEVICEOPTION_MASTER_VOLUME;
}

int
RS_CS2Host_OptionDefault(
    int kind,
    int option_id)
{
    /*
     * The client boots muted, and it is the master gain that does it.
     *
     * A deliberate divergence from the reference, which comes up at full
     * volume. Muting at the master rather than zeroing all four is what makes
     * one click on interface 116's master mute icon enough to get the
     * reference's mix back -- the three per-bus volumes below keep their full
     * default, so nothing has to be dragged up afterwards.
     *
     * Nothing here is "the mixer is off": App_Init seeds the backend from this
     * value and the panel seeds its varps from it too, so a muted client looks
     * muted rather than showing full sliders over a silent mixer. The moment
     * the player raises it, game/rs_prefs.c writes the new value out and the
     * next launch is no longer muted.
     */
    if( kind == RS_CS2_OPTION_DEVICE && option_id == RS_CS2_DEVICEOPTION_MASTER_VOLUME )
        return 0;
    /* Unscaled. Zero would be the table's answer otherwise, and this id is a
     * divisor — a canvas computed from a 0% scale is not a smaller canvas, it
     * is a division by zero. */
    if( kind == RS_CS2_OPTION_DEVICE && option_id == RS_CS2_DEVICEOPTION_UI_SCALE )
        return RS_CS2_UI_SCALE_MIN;
    /* The cache presents Bicubic as the initial selection. It is value 2 in
     * enum_4033, not the zero an otherwise untouched option would return. */
    if( kind == RS_CS2_OPTION_DEVICE &&
        option_id == RS_CS2_DEVICEOPTION_UI_SCALE_MODE )
        return RS_CS2_UI_SCALE_MODE_BICUBIC;
    /* Full volume for the per-bus ones: an option nothing has written must not
     * read back as silence, or unmuting would restore nothing. Every other
     * option is zero, which is CS2's answer for an option no script has set. */
    return option_is_volume(kind, option_id) ? 100 : 0;
}

static int*
option_table(
    struct RS_CS2Host* host,
    int kind)
{
    switch( kind )
    {
    case RS_CS2_OPTION_GAME:
        return host->game_options;
    case RS_CS2_OPTION_DEVICE:
        return host->device_options;
    default:
        return NULL;
    }
}

int
RS_CS2Host_GetOption(
    struct RS_CS2Host const* host,
    int kind,
    int option_id)
{
    int const* table;

    if( option_id < 0 || option_id >= RS_CS2_OPTION_MAX )
        return 0;
    assert(host);
    table = option_table((struct RS_CS2Host*)host, kind);
    return table ? table[option_id] : 0;
}

void
RS_CS2Host_SetOption(
    struct RS_CS2Host* host,
    int kind,
    int option_id,
    int value)
{
    int* table;

    if( option_id < 0 || option_id >= RS_CS2_OPTION_MAX )
        return;
    assert(host);
    table = option_table(host, kind);
    if( !table )
        return;
    if( option_is_volume(kind, option_id) )
        value = clamp_percent(value);
    /* script_3054 clamps before it writes, so a value out of range here came
     * from somewhere that does not — a hand-edited preferences file, or the
     * generic CLIENTOPTION_SET spelling. Clamp it in the store rather than
     * trust the caller: everything downstream divides by it. */
    if( kind == RS_CS2_OPTION_DEVICE && option_id == RS_CS2_DEVICEOPTION_UI_SCALE )
    {
        if( value < RS_CS2_UI_SCALE_MIN )
            value = RS_CS2_UI_SCALE_MIN;
        if( value > RS_CS2_UI_SCALE_MAX )
            value = RS_CS2_UI_SCALE_MAX;
        if( table[option_id] != value )
            host->ui_scale_dirty = true;
    }
    if( kind == RS_CS2_OPTION_DEVICE &&
        option_id == RS_CS2_DEVICEOPTION_UI_SCALE_MODE )
    {
        if( value < RS_CS2_UI_SCALE_MODE_NEAREST )
            value = RS_CS2_UI_SCALE_MODE_NEAREST;
        if( value > RS_CS2_UI_SCALE_MODE_BICUBIC )
            value = RS_CS2_UI_SCALE_MODE_BICUBIC;
    }
    table[option_id] = value;
    if( !option_is_volume(kind, option_id) )
        return;
    if( option_id == RS_CS2_GAMEOPTION_MUSIC_VOLUME && kind == RS_CS2_OPTION_GAME )
        host->volume_music = value;
    else if( option_id == RS_CS2_GAMEOPTION_SOUND_VOLUME && kind == RS_CS2_OPTION_GAME )
        host->volume_sounds = value;
    else if( option_id == RS_CS2_GAMEOPTION_AREA_VOLUME && kind == RS_CS2_OPTION_GAME )
        host->volume_area_sounds = value;
    host->audio_settings_dirty = true;
}

int
RS_CS2Host_UiScalePercent(
    struct RS_CS2Host const* host)
{
    int value;

    assert(host);
    value = host->device_options[RS_CS2_DEVICEOPTION_UI_SCALE];
    if( value < RS_CS2_UI_SCALE_MIN )
        return RS_CS2_UI_SCALE_MIN;
    if( value > RS_CS2_UI_SCALE_MAX )
        return RS_CS2_UI_SCALE_MAX;
    return value;
}

int
RS_CS2Host_UiScaleMode(
    struct RS_CS2Host const* host)
{
    int value;

    assert(host);
    value = host->device_options[RS_CS2_DEVICEOPTION_UI_SCALE_MODE];
    if( value < RS_CS2_UI_SCALE_MODE_NEAREST )
        return RS_CS2_UI_SCALE_MODE_NEAREST;
    if( value > RS_CS2_UI_SCALE_MODE_BICUBIC )
        return RS_CS2_UI_SCALE_MODE_BICUBIC;
    return value;
}

bool
RS_CS2Host_TakeAudioSettings(
    struct RS_CS2Host* host,
    struct RS_CS2AudioSettings* out)
{
    assert(host);
    if( !host->audio_settings_dirty )
        return false;
    assert(out);
    out->master = host->device_options[RS_CS2_DEVICEOPTION_MASTER_VOLUME];
    out->music = host->game_options[RS_CS2_GAMEOPTION_MUSIC_VOLUME];
    out->sounds = host->game_options[RS_CS2_GAMEOPTION_SOUND_VOLUME];
    out->area_sounds = host->game_options[RS_CS2_GAMEOPTION_AREA_VOLUME];
    host->audio_settings_dirty = false;
    return true;
}

void
RS_CS2Host_SyncAudioVarp(
    struct RS_CS2Host* host,
    int varp_id)
{
    int value;

    assert(host);
    if( !host->varps )
        return;
    switch( varp_id )
    {
    case RS_CS2_VARP_MASTER_VOLUME:
        value = clamp_percent(VarPManager_GetVarp(host->varps, varp_id));
        host->device_options[RS_CS2_DEVICEOPTION_MASTER_VOLUME] = value;
        break;
    case RS_CS2_VARP_MUSIC_VOLUME:
        value = clamp_percent(VarPManager_GetVarp(host->varps, varp_id));
        host->game_options[RS_CS2_GAMEOPTION_MUSIC_VOLUME] = value;
        host->volume_music = value;
        break;
    case RS_CS2_VARP_SOUND_VOLUME:
        value = clamp_percent(VarPManager_GetVarp(host->varps, varp_id));
        host->game_options[RS_CS2_GAMEOPTION_SOUND_VOLUME] = value;
        host->volume_sounds = value;
        break;
    case RS_CS2_VARP_AREA_VOLUME:
    case RS_CS2_VARP_AREA_OVERRIDE_ENABLED:
    case RS_CS2_VARP_AREA_OVERRIDE_VOLUME:
        value = VarPManager_GetVarp(host->varps, RS_CS2_VARP_AREA_OVERRIDE_ENABLED) == 1
                    ? VarPManager_GetVarp(host->varps, RS_CS2_VARP_AREA_OVERRIDE_VOLUME)
                    : VarPManager_GetVarp(host->varps, RS_CS2_VARP_AREA_VOLUME);
        value = clamp_percent(value);
        host->game_options[RS_CS2_GAMEOPTION_AREA_VOLUME] = value;
        host->volume_area_sounds = value;
        break;
    default:
        return;
    }
    host->audio_settings_dirty = true;
}

bool
RS_CS2Host_TakeTriggerOp(
    struct RS_CS2Host* host,
    struct RS_CS2TriggerOp* out)
{
    assert(host);
    if( host->trigger_op_count <= 0 )
        return false;
    assert(out);
    *out = host->trigger_op[host->trigger_op_head];
    host->trigger_op_head = (host->trigger_op_head + 1) % RS_CS2_HOST_TRIGGER_OP_MAX;
    host->trigger_op_count--;
    return true;
}

static void
rs_cs2_triggeroplocal_push(
    struct RS_CS2Host* host,
    int component_id,
    int sub)
{
    int slot;

    assert(host);
    if( host->triggeroplocal_count >= RS_CS2_HOST_TRIGGEROPLOCAL_MAX )
    {
        fprintf(
            stderr,
            "cs2: if_triggeroplocal queue full (%d), dropped component 0x%08x sub %d\n",
            RS_CS2_HOST_TRIGGEROPLOCAL_MAX,
            (unsigned)component_id,
            sub);
        return;
    }
    slot = (host->triggeroplocal_head + host->triggeroplocal_count) %
           RS_CS2_HOST_TRIGGEROPLOCAL_MAX;
    host->triggeroplocal[slot].component_id = component_id;
    host->triggeroplocal[slot].sub = sub;
    host->triggeroplocal_count++;
}

bool
RS_CS2Host_TakeTriggerOpLocal(
    struct RS_CS2Host* host,
    struct RS_CS2TriggerOpLocal* out)
{
    assert(host);
    if( host->triggeroplocal_count <= 0 )
        return false;
    assert(out);
    *out = host->triggeroplocal[host->triggeroplocal_head];
    host->triggeroplocal_head =
        (host->triggeroplocal_head + 1) % RS_CS2_HOST_TRIGGEROPLOCAL_MAX;
    host->triggeroplocal_count--;
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
exec_inv_size(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest const* exact_request,
    int inv_id)
{
    struct CacheProvider* provider;
    int size = 0;

    assert(host);
    assert(thread);

    provider = rs_cs2_provider(host);
    if( inv_id >= 0 && provider && CacheProvider_InvtypeGet(provider, inv_id, &size) )
        return CS2VM2_PushInt(thread, size);

    /* INV_SIZE reads the immutable inventory type, not the live container. A
     * script can ask before UPDATE_INV_FULL has created that container (the
     * equipment and inventory onLoads both do), so a runtime-state miss says
     * nothing about the answer. Await the cache record once; if this revision
     * has no such record/provider, complete the retry with the opcode default
     * instead of yielding forever. */
    if( inv_id >= 0 )
    {
        if( !rs_cs2_await_spent(thread, exact_request->kind, inv_id, -1) )
            return rs_cs2_yield_load(host, thread, exact_request, inv_id, -1);
    }
    return CS2VM2_PushInt(thread, 0);
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
    struct CS2VM_HostRequest const* exact_request,
    int script_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct CS2VM2_Script* script = NULL;

    if( provider )
        script = CacheProvider_ClientScriptGet(provider, script_id);
    if( !script )
    {
        if( rs_cs2_await_spent(thread, exact_request->kind, script_id, -1) )
        {
            /* No degrade possible: the caller expects this script's return
             * values on the stack and we cannot synthesise them. */
            fprintf(stderr, "RS_CS2Host: script %d failed to load\n", script_id);
            return CS2VM_EXECNO_ERROR;
        }
        return rs_cs2_yield_load(host, thread, exact_request, script_id, -1);
    }
    return CS2VM2_PushCallScript(thread, script);
}

static int
exec_para_height(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_ParaHeight request,
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
            /* Font still missing after its load: measure as 0. */
            if( !rs_cs2_await_spent(thread, exact_request->kind, request.font_id, -1) )
                return rs_cs2_yield_load(
                    host, thread, exact_request, request.font_id, -1);
        }
        else
        {
            result = is_width ? rs_cs2_font_wrap_max_line_width(font, text, request.max_width)
                              : rs_cs2_font_wrap_line_count(font, text, request.max_width);
            /* TORIRS_PARA_DEBUG=1 prints the measured string with the number
             * that came back. It is the only way to tell a wrap that measured
             * wrong from a string that arrived wrong — the Ancient Curses
             * tooltips looked like the first and were the second, and one line
             * of this said so. Read once: this runs inside interface builds. */
            static int para_debug = -1;
            if( para_debug < 0 )
                para_debug = getenv("TORIRS_PARA_DEBUG") != NULL;
            if( para_debug )
                fprintf(stderr, "para%s: font=%d max_w=%d lh=%d -> %d  \"%s\"\n",
                        is_width ? "width" : "height", request.font_id, request.max_width,
                        font->line_height, result, text);
        }
    }
    return CS2VM2_PushInt(thread, result);
}

/*
 * Learn which varp a colour row is about, from the read hub reading it.
 *
 * `settings_get_colour` (script_4181) is a switch from setting id to
 * `calc(%var<n> - 1)` and is the ONLY statement of that mapping anywhere --
 * the setting struct does not carry the varp, and the row builder never names
 * it. So the mapping is taken from the hub doing its job: the read that runs
 * inside a frame of that script, with the setting id still sitting in its
 * first local, IS the answer for that id.
 *
 * The timing works out on its own. Clientscript 4182 -- the colour row's own
 * builder -- calls the hub twice while laying the row out, so every row on
 * screen has taught this table before its swatch exists to be clicked.
 *
 * Cheap by construction: a script id compare, and only after the frame is in
 * hand for the common case of an ordinary varp read somewhere else.
 */
static void
rs_cs2_settings_note_colour_varp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int varp_id)
{
    struct CS2VM2_Frame* frame;
    int setting_id;
    int i;

    assert(host);
    assert(vm);
    /* An id of 0 is the feature switched off, and an empty frame stack is a VM
     * that could not push one -- neither is a caller's mistake. */
    if( host->script_settings_colour_get <= 0 || vm->frame_sp <= 0 )
        return;
    frame = vm->frames[vm->frame_sp - 1];
    if( !frame || !frame->script )
        return;
    if( frame->script->script_id != host->script_settings_colour_get )
        return;
    if( frame->script->int_argument_count < 1 )
        return;

    setting_id = frame->int_locals[0];
    for( i = 0; i < host->settings_colour_count; i++ )
    {
        if( host->settings_colour_setting[i] != setting_id )
            continue;
        host->settings_colour_varp[i] = varp_id;
        return;
    }
    if( host->settings_colour_count >= RS_CS2_HOST_SETTINGS_COLOURS_MAX )
    {
        /* Once: the hub is called per row per rebuild, so a full table would
         * otherwise say so several hundred times a panel open. */
        static int reported = 0;
        if( !reported )
        {
            reported = 1;
            fprintf(
                stderr,
                "settings: colour-row table full at %d; setting %d cannot be picked\n",
                RS_CS2_HOST_SETTINGS_COLOURS_MAX,
                setting_id);
        }
        return;
    }
    i = host->settings_colour_count++;
    host->settings_colour_setting[i] = setting_id;
    host->settings_colour_varp[i] = varp_id;
}

static int
exec_vars_read_varp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int varp_id)
{
    int value = 0;
    rs_cs2_settings_note_colour_varp(host, thread, varp_id);
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_EnumLookup request)
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
            if( !rs_cs2_await_spent(thread, exact_request->kind, request.enum_id, -1) )
                return rs_cs2_yield_load(
                    host, thread, exact_request, request.enum_id, -1);
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_WorldMap request)
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
        if( !rs_cs2_await_spent(thread, exact_request->kind, -1, -1) )
            return rs_cs2_yield_load(host, thread, exact_request, -1, -1);
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_MEC request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_MapElement* element =
        provider ? CacheProvider_MapElementGet(provider, request.mec_id) : NULL;

    if( !element )
    {
        if( !rs_cs2_await_spent(thread, exact_request->kind, request.mec_id, -1) )
            return rs_cs2_yield_load(host, thread, exact_request, request.mec_id, -1);
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
 * behind the UITree host bus, which the CS2 host cannot reach -- so the App
 * publishes a per-frame snapshot into host->clientop instead, the same one the
 * `_67xx / _68xx / _69xx` target getters read.
 *
 * This is not a tooltip nicety. Clientscript 5350 is the cache's own
 * "Highlight entities on mouse-over" (setting 190): it asks `_7100` what kind
 * of thing the pointer is on, confirms it with the matching FIND op, and puts
 * the subject into a highlight group. While TYPE answered 0 the script
 * returned on its first branch and the setting did nothing at all.
 */
/*
 * One FIND op: latch the acting row's subject into the kind's active register,
 * and report whether there was one of that kind.
 *
 * A miss CLEARS the register rather than leaving it, which is the reference's
 * behaviour too (its setter writes -1 to both halves when the entry does not
 * resolve): a script that asked "is this a player" and was told no must not
 * then be able to read the player it asked about three rows ago.
 */
static int
minimenu_find(struct RS_CS2Host* host, enum RS_ClientOpKind kind, int menu_type)
{
    assert(host);

    if( host->clientop.mouseover_type != menu_type ||
        host->clientop.mouseover.kind != (int)kind )
    {
        RS_ClientOpActiveSet(&host->clientop, kind, NULL);
        return 0;
    }
    RS_ClientOpActiveSet(&host->clientop, kind, &host->clientop.mouseover);
    if( getenv("TORIRS_CLIENTOP_DEBUG") )
        fprintf(
            stderr,
            "minimenu_find: %s latched uid=%d type=%d '%s'\n",
            RS_ClientOpKindName(kind),
            host->clientop.mouseover.uid,
            host->clientop.mouseover.type,
            host->clientop.mouseover.name);
    return 1;
}

static int
exec_minimenu(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int opcode)
{
    switch( opcode )
    {
    case CS2_OP_MINIMENU_ENTRY:
    {
        /* Two strings, option then target (reference push order). */
        int result = CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, host->clientop.mouseover_op));
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, host->clientop.mouseover_target));
    }
    case CS2_OP_MINIMENU_TYPE:
        /*
         * The acting row's type. A world pick answers for itself; a row about
         * an INTERFACE component is type 7, and that is a row the world pick
         * never sees -- the two publishers are the pick set and the menu the
         * hover line is composed from, and only the second one knows about
         * widgets. Derived here rather than stored so neither publisher has to
         * run after the other. See RS_MINIMENU_TYPE_COMPONENT.
         */
        if( host->clientop.mouseover_type == RS_MINIMENU_TYPE_NONE &&
            host->clientop.mouseover_component >= 0 )
            return CS2VM2_PushInt(thread, RS_MINIMENU_TYPE_COMPONENT);
        return CS2VM2_PushInt(thread, host->clientop.mouseover_type);
    /*
     * The FIND ops LATCH the acting row's subject, then say whether it was of
     * the kind asked for.
     *
     * Latching is the load-bearing half, and it is the reference's own
     * behaviour: `ScriptRunnerImpl::ExecuteCommand7100To7199` takes the menu's
     * selected entry (or the LAST one -- the default left-click row -- when
     * nothing is selected), sets the active npc / loc / obj / player from it,
     * and pushes whether that worked. Clientscript 5350 depends on the order:
     *
     *     if ($int0 = 2 & _7102 = 1) { ~script5951(nc_param(_6753, ...)); }
     *
     * -- `_6753` is read AFTER `_7102`, and it is the entry `_7102` latched
     * that it is about. Answering without latching left that reading whatever
     * the mouseover fallback happened to hold, which is usually the same
     * subject and silently is not when a row outlives the pick that made it.
     *
     * The acting row here is the mouseover the App publishes each frame, which
     * is this client's spelling of "the entry the menu would act on".
     */
    case CS2_OP_MINIMENU_FINDNPC:
        return CS2VM2_PushInt(
            thread, minimenu_find(host, RS_CLIENTOP_NPC, RS_MINIMENU_TYPE_NPC));
    case CS2_OP_MINIMENU_FINDLOC:
        return CS2VM2_PushInt(
            thread, minimenu_find(host, RS_CLIENTOP_LOC, RS_MINIMENU_TYPE_LOC));
    case CS2_OP_MINIMENU_FINDOBJ:
        return CS2VM2_PushInt(
            thread, minimenu_find(host, RS_CLIENTOP_OBJ, RS_MINIMENU_TYPE_OBJ));
    case CS2_OP_MINIMENU_FINDPLAYER:
        return CS2VM2_PushInt(
            thread, minimenu_find(host, RS_CLIENTOP_PLAYER, RS_MINIMENU_TYPE_PLAYER));
    /*
     * The acting row's TILE (`_7106`) and its OBJ id (`_7107`).
     *
     * The reference reads both off the entry: the coord from the entry's own
     * packed x/z when it has one and from the mouseover ground tile when it
     * does not, and the obj id from the entry field its FINDOBJ matches an obj
     * against. Nothing in this cache calls either -- they are routed because
     * the alternative is the stack stub answering a confident zero, which for
     * a COORD is the corner of the map square.
     */
    case CS2_OP__7106:
        return CS2VM2_PushInt(
            thread,
            host->clientop.mouseover.coord >= 0 ? host->clientop.mouseover.coord
                                                : host->hover_coord);
    case CS2_OP__7107:
        return CS2VM2_PushInt(
            thread,
            host->clientop.mouseover_type == RS_MINIMENU_TYPE_OBJ
                ? host->clientop.mouseover.type
                : 0);
    case CS2_OP_MINIMENU_ISOPEN:
        return CS2VM2_PushInt(thread, host->clientop.menu_open ? 1 : 0);
    case CS2_OP_MINIMENU_NUMOPS:
        return CS2VM2_PushInt(thread, host->clientop.mouseover_opcount);
    /*
     * FINDCOMPONENT is the same latch for an INTERFACE row, and what it
     * latches is the VM's active component -- the reference's
     * `ScriptRunnerImpl::SetActiveComponent` from the entry, which is why
     * proc 4728 reads `cc_getlayer` immediately after `_7109 = 1` and expects
     * the hovered widget. It answered a flat 0 here, so the tooltip proc's
     * whole component branch never ran.
     */
    case CS2_OP_MINIMENU_FINDCOMPONENT:
        if( host->clientop.mouseover_component < 0 )
            return CS2VM2_PushInt(thread, 0);
        CS2VM2_SetActiveAndDotComponentId(thread, host->clientop.mouseover_component);
        if( getenv("TORIRS_CLIENTOP_DEBUG") )
            fprintf(
                stderr,
                "minimenu_find: component latched %d\n",
                host->clientop.mouseover_component);
        return CS2VM2_PushInt(thread, 1);
    default:
        fprintf(stderr, "exec_minimenu: unhandled opcode %d\n", opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

static int
clamp_percent(int value)
{
    if( value < 0 )
        return 0;
    if( value > 100 )
        return 100;
    return value;
}

/* Audio volumes (3203..3208) and client/game/device options (3209..3217).
 * Interface 116 writes music/effects/area through game options 7/8/9 and
 * master through device option 19. Store every in-range option for GET
 * round-trips; flag the four audio ids for the App to apply after the script
 * completes, beside the existing deferred sound requests. */
static int
exec_client_option(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_ClientOption request)
{
    switch( request.opcode )
    {
    case CS2_OP_SETVOLUMEMUSIC:
        host->volume_music = clamp_percent(request.value);
        host->game_options[RS_CS2_GAMEOPTION_MUSIC_VOLUME] = host->volume_music;
        host->audio_settings_dirty = true;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEMUSIC:
        return CS2VM2_PushInt(thread, host->volume_music);
    case CS2_OP_SETVOLUMESOUNDS:
        host->volume_sounds = clamp_percent(request.value);
        host->game_options[RS_CS2_GAMEOPTION_SOUND_VOLUME] = host->volume_sounds;
        host->audio_settings_dirty = true;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMESOUNDS:
        return CS2VM2_PushInt(thread, host->volume_sounds);
    case CS2_OP_SETVOLUMEAREASOUNDS:
        host->volume_area_sounds = clamp_percent(request.value);
        host->game_options[RS_CS2_GAMEOPTION_AREA_VOLUME] = host->volume_area_sounds;
        host->audio_settings_dirty = true;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEAREASOUNDS:
        return CS2VM2_PushInt(thread, host->volume_area_sounds);

    /* Hide roofs: the named form of game option 1, stored in the same place so
     * the two spellings cannot disagree. "On" means every roof is hidden; off
     * is the selective, tile-flag removal the world render does anyway
     * (reference roofCheck/roofCheck2, both of which return the player's level
     * outright when this is set). */
    case CS2_OP_SETREMOVEROOFS:
        RS_CS2Host_SetOption(
            host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_HIDE_ROOFS, request.value ? 1 : 0);
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETREMOVEROOFS:
        return CS2VM2_PushInt(
            thread,
            RS_CS2Host_GetOption(host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_HIDE_ROOFS) ? 1
                                                                                         : 0);

    /* The SET cases are the same store with the same clamp and the same volume
     * mirroring, which is why they go through one function: the preferences
     * restore (game/rs_prefs.c) writes the tables too, and a restored volume
     * that skipped the mirroring would leave GETVOLUMEMUSIC disagreeing with
     * GAMEOPTION_GET on the same setting.
     *
     * CLIENTOPTION is the generic form and names no table: the reference looks
     * the id up in the device table, then the game table. It used to land in a
     * private third array here, which meant a script setting the music volume
     * through 3209 changed nothing audible and read back through 3215 as
     * whatever the slider had left. */
    case CS2_OP_CLIENTOPTION_SET:
        RS_CS2Host_SetOption(
            host, RS_CS2Host_ClientOptionKind(request.option_id), request.option_id,
            request.value);
        return CS2VM_EXECNO_OK;
    case CS2_OP_GAMEOPTION_SET:
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_GAME, request.option_id, request.value);
        return CS2VM_EXECNO_OK;
    case CS2_OP_DEVICEOPTION_SET:
        RS_CS2Host_SetOption(host, RS_CS2_OPTION_DEVICE, request.option_id, request.value);
        return CS2VM_EXECNO_OK;
    case CS2_OP_CLIENTOPTION_GET:
        return CS2VM2_PushInt(
            thread,
            RS_CS2Host_GetOption(
                host, RS_CS2Host_ClientOptionKind(request.option_id), request.option_id));
    case CS2_OP_GAMEOPTION_GET:
        return CS2VM2_PushInt(
            thread, RS_CS2Host_GetOption(host, RS_CS2_OPTION_GAME, request.option_id));
    case CS2_OP_DEVICEOPTION_GET:
        return CS2VM2_PushInt(
            thread, RS_CS2Host_GetOption(host, RS_CS2_OPTION_DEVICE, request.option_id));
    case CS2_OP_DEVICEOPTION_GETRANGE:
    {
        /* min then max (reference range order). */
        int max = request.option_id == RS_CS2_DEVICEOPTION_MASTER_VOLUME ? 100 : 255;
        int result = CS2VM2_PushInt(thread, 0);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, max);
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
    struct CS2VM_HostPayload_LocalNotification request)
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
    struct CS2VM_HostPayload_Minimap request)
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

/* Statics.method5659 (rev-239): (int)pow(2, arg/256 + 7), 256 when that is <= 0.
 * The reference feeds SETFOV's args through this before storing them as the
 * near/far endpoints of the viewport-height zoom interpolation. */
static int
rs_cs2_viewport_zoom_decode(int arg)
{
    int zoom = (int)pow(2.0, (double)arg / 256.0 + 7.0);
    return zoom > 0 ? zoom : 256;
}

/* Statics.method9013, the inverse VIEWPORT_GETFOV answers with. Lossy against
 * the decode above, which is the reference's behaviour and not a rounding bug
 * here: 220 decodes to 232 and encodes back to 219. */
static int
rs_cs2_viewport_zoom_encode(int zoom)
{
    if( zoom <= 0 )
        return 0;
    return (int)((log((double)zoom) / log(2.0) - 7.0) * 256.0);
}

/*
 * class159.method5357's sizing half — the reference's "effective viewport".
 *
 * Given the viewport widget's box it interpolates the FOV over the widget's
 * HEIGHT (the two SETFOV endpoints, over the 100px band above the fixed frame's
 * 334), forms `height * fov * 512 / (width * 334)`, and letterboxes whichever
 * axis is needed to pull that back inside the CLAMPFOV range: too small and the
 * sides are cut, too large and the top and bottom are. What remains is what
 * VIEWPORT_GETEFFECTIVESIZE answers.
 *
 * The reference also paints the cut bands black and stores the rect in
 * client.field811/897/813/837; opcode 6203 passes `false` for the drawing and
 * this client's renderer owns its own viewport rect, so only the size is
 * reproduced here.
 *
 * The integer division in the interpolation is the reference's, not a
 * simplification: field976/field801 are shorts and the expression truncates
 * before it is widened.
 */
static void
rs_cs2_viewport_effective_size(
    struct RS_CS2Host const* host,
    int width,
    int height,
    int* out_width,
    int* out_height)
{
    if( width < 1 )
        width = 1;
    if( height < 1 )
        height = 1;

    int const band = height - 334;
    double fov;
    if( band < 0 )
        fov = host->viewport_zoom_near;
    else if( band >= 100 )
        fov = host->viewport_zoom_far;
    else
        fov = ((host->viewport_zoom_far - host->viewport_zoom_near) * band) / 100 +
              host->viewport_zoom_near;

    double const aspect = height * fov * 512.0 / (width * 334);
    if( aspect < host->viewport_aspect_min )
    {
        double const floor_aspect = host->viewport_aspect_min;
        fov = width * floor_aspect * 334.0 / (height * 512);
        if( fov > host->viewport_fov_max_clamp )
        {
            fov = host->viewport_fov_max_clamp;
            double const visible = height * fov * 512.0 / (floor_aspect * 334.0);
            int const cut = (int)((width - visible) / 2.0);
            width -= cut * 2;
        }
    }
    else if( aspect > host->viewport_aspect_max )
    {
        double const ceil_aspect = host->viewport_aspect_max;
        fov = width * ceil_aspect * 334.0 / (height * 512);
        if( fov < host->viewport_fov_min )
        {
            fov = host->viewport_fov_min;
            double const visible = width * ceil_aspect * 334.0 / (fov * 512.0);
            int const cut = (int)((height - visible) / 2.0);
            height -= cut * 2;
        }
    }

    *out_width = width;
    *out_height = height;
}

static int
exec_viewport(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_Viewport request)
{
    switch( request.opcode )
    {
    case CS2_OP_VIEWPORT_SETFOV:
        /* Reference Statics.method6341 case 6200: only the DECODED endpoints are
         * kept (Statics.method5659), each falling back to 256. */
        host->viewport_zoom_near = rs_cs2_viewport_zoom_decode(request.args[0]);
        host->viewport_zoom_far = rs_cs2_viewport_zoom_decode(request.args[1]);
        if( getenv("TORIRS_WEDGE_FOV_DEBUG") )
            fprintf(
                stderr,
                "wedge: VIEWPORT_SETFOV raw=%d,%d decoded near=%d far=%d\n",
                request.args[0], request.args[1],
                host->viewport_zoom_near, host->viewport_zoom_far);
        return CS2VM_EXECNO_OK;
    case CS2_OP_VIEWPORT_GETFOV:
    {
        /* Case 6205 re-encodes with Statics.method9013 rather than answering
         * the arguments SETFOV was given — see the field note in the header. */
        int result = CS2VM2_PushInt(thread, rs_cs2_viewport_zoom_encode(host->viewport_zoom_near));
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, rs_cs2_viewport_zoom_encode(host->viewport_zoom_far));
    }
    case CS2_OP_VIEWPORT_SETZOOM:
        /* Reference Statics.method6341 case 6201: the two args are the NEAR and
         * FAR endpoints of the follow camera's orbit-distance zoom (field780 /
         * field747), stored raw — no method5659 decode, unlike SETFOV — with
         * distinct <= 0 fallbacks. GETZOOM (6204) pushes them back unchanged. */
        host->viewport_zoom = request.args[0] > 0 ? request.args[0] : 256;
        host->viewport_zoom_max = request.args[1] > 0 ? request.args[1] : 320;
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
        /* Reference Statics.method6341 case 6202. Two independent ranges, each
         * argument defaulting when <= 0 and each maximum raised to its own
         * minimum. It deliberately leaves the FOV alone; the old code here read
         * the four as value/min/max, clamped viewport_fov with them and dropped
         * the fourth, which made GETFOV answer the clamp instead of SETFOV and
         * left method5357 with no bounds to letterbox against at all. */
        host->viewport_fov_min = request.args[0] > 0 ? request.args[0] : 1;
        host->viewport_fov_max_clamp = request.args[1] > 0 ? request.args[1] : 32767;
        if( host->viewport_fov_max_clamp < host->viewport_fov_min )
            host->viewport_fov_max_clamp = host->viewport_fov_min;
        host->viewport_aspect_min = request.args[2] > 0 ? request.args[2] : 1;
        host->viewport_aspect_max = request.args[3] > 0 ? request.args[3] : 32767;
        if( host->viewport_aspect_max < host->viewport_aspect_min )
            host->viewport_aspect_max = host->viewport_aspect_min;
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_VIEWPORT_GETEFFECTIVESIZE:
    {
        /*
         * Reference Statics.method6341 case 6203: the size of the VIEWPORT
         * WIDGET (client.field6268 — the clientCode-1337 layer, which
         * method3791 latches while it lays the tree out), run through
         * method5357's letterbox. Not the canvas: at rev 239 the resizable
         * gameframe hands the world a container 42px narrower than the window
         * to leave room for the right-hand icon strip, and answering the canvas
         * made toplevel_resize size interface_161:92/94 to the full window and
         * centre them inside that narrower parent — a 21px left shift that
         * every descendant inherited, the modal slot included.
         *
         * -1,-1 when the open interface has no viewport at all, which is the
         * reference's answer and not an error.
         */
        struct UITree* tree = rs_cs2_tree(host);
        int32_t const idx = tree ? tree->world_index : -1;
        int width = -1;
        int height = -1;

        if( idx >= 0 && (uint32_t)idx < tree->component_count )
        {
            struct UITreeComponent const* c;
            /* The reference reads the widget mid-layout, so its box is current
             * by construction; here a script can ask between a set and the next
             * resolve. */
            UITree_EnsureLayoutFor(tree, idx);
            c = &tree->components[idx];
            rs_cs2_viewport_effective_size(
                host, c->position.abs_w, c->position.abs_h, &width, &height);
        }

        int result = CS2VM2_PushInt(thread, width);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, height);
    }
    default:
        fprintf(stderr, "exec_viewport: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * UI zoom (6210..6214). GETDEFAULT answers the fixed RS_CS2_UIZOOM_DEFAULT
 * constant; SET/GET/RESET are the interface-scale option under a second name.
 *
 * They share one store for the same reason CLIENTOPTION_SET does above: the
 * cache reaches this setting both ways — script_3054 through
 * `deviceoption_set(27, ...)` and this family directly — and a client that
 * kept two copies would answer whichever one the reader happened to use. The
 * settings row reads deviceoption 27, so a UIZOOM_SET into a private field
 * changed nothing the player could see.
 */
static int
exec_uizoom(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_UiZoom request)
{
    switch( request.opcode )
    {
    case CS2_OP_UIZOOM_SET:
        RS_CS2Host_SetOption(
            host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_UI_SCALE, request.value);
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GET:
        return CS2VM2_PushInt(thread, RS_CS2Host_UiScalePercent(host));
    case CS2_OP_UIZOOM_RESET:
        RS_CS2Host_SetOption(
            host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_UI_SCALE,
            RS_CS2_UIZOOM_DEFAULT);
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GETDEFAULT:
        return CS2VM2_PushInt(thread, RS_CS2_UIZOOM_DEFAULT);
    default:
        fprintf(stderr, "exec_uizoom: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Record that the All Settings panel just named a setting.
 *
 * Every one of the panel's four apply hubs opens with
 * `%varbit9657 = <setting id>`, so this write is the panel announcing which
 * row was used, ahead of whatever the hub then does about it -- including the
 * rows it does nothing about, which is every one this client has to implement
 * itself.
 *
 * The chosen value is taken from the hub's own frame rather than guessed. A
 * dropdown/slider hub is `(setting id, value, secondary)` and its second local
 * is the choice; a toggle or button hub takes the id alone and there is no
 * value to report, which is what -1 means here. Reading int_locals[1] off a
 * one-parameter frame would report whatever that script happened to leave in
 * the slot, so the arity is checked and not assumed.
 */
static void
rs_cs2_settings_record_action(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int varbit_id,
    int setting_id)
{
    struct CS2VM2_Frame* frame;
    int value = -1;
    int slot;

    assert(host);
    if( host->varbit_settings_last_changed <= 0 ||
        varbit_id != host->varbit_settings_last_changed )
        return;

    if( vm && vm->frame_sp > 0 )
    {
        frame = vm->frames[vm->frame_sp - 1];
        if( frame && frame->script && frame->script->int_argument_count >= 2 )
            value = frame->int_locals[1];
    }

    if( host->settings_action_count >= RS_CS2_HOST_SETTINGS_ACTIONS_MAX )
    {
        /* Drop the oldest: see the queue's declaration. */
        for( int i = 1; i < host->settings_action_count; i++ )
        {
            host->settings_action_id[i - 1] = host->settings_action_id[i];
            host->settings_action_value[i - 1] = host->settings_action_value[i];
        }
        host->settings_action_count--;
    }
    slot = host->settings_action_count++;
    host->settings_action_id[slot] = setting_id;
    host->settings_action_value[slot] = value;
}

/** The root frame's script id, or -1 when there is no frame. */
static int
rs_cs2_root_script_id(struct CS2VM2_Thread* vm)
{
    if( !vm || vm->frame_sp <= 0 || !vm->frames[0] || !vm->frames[0]->script )
        return -1;
    return vm->frames[0]->script->script_id;
}

/*
 * Mirror an All Settings varbit write to the server.
 *
 * Ten rows of this category are decided server-side and every one of them reads
 * a varbit the panel writes client-only, so without this they read whatever the
 * server last set and the panel's checkbox means nothing. See
 * `settings_mirror_varbit` in the header for why no packet in this revision
 * carries it and what is sent instead.
 *
 * The hub identifies itself: `%varbit9657 = <setting id>` is the first
 * statement of all four apply hubs, so the root frame at that moment IS a hub,
 * and every varbit write under that same root afterwards is this row's own. A
 * list of hub script ids would say the same thing and would have to be kept in
 * step with the cache by hand.
 *
 * 9657 itself is not mirrored. It is the panel telling ITSELF which row is
 * being applied -- `%varbit9657` is read back by nothing on the server, and
 * sending it would spend a packet on a value with no server-side meaning.
 */
static void
rs_cs2_settings_record_mirror(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int varbit_id,
    int value)
{
    int root;

    assert(host);
    root = rs_cs2_root_script_id(vm);

    if( host->varbit_settings_last_changed > 0 &&
        varbit_id == host->varbit_settings_last_changed )
    {
        host->settings_mirror_root_script = root;
        return;
    }

    if( root < 0 || root != host->settings_mirror_root_script )
        return;

    RS_CS2Host_QueueSettingsMirror(host, varbit_id, value);
}

void
RS_CS2Host_QueueSettingsMirror(
    struct RS_CS2Host* host,
    int varbit_id,
    int value)
{
    int slot;

    assert(host);
    if( varbit_id < 0 )
        return;

    /*
     * Coalesce on the varbit, rather than appending.
     *
     * A row toggled twice before the queue drains is one value to send, not
     * two, and the LAST one is the answer -- the opposite of the settings
     * ACTION queue beside this, where two presses of a button row are two
     * events and collapsing them would lose one.
     */
    for( slot = 0; slot < host->settings_mirror_count; slot++ )
    {
        if( host->settings_mirror_varbit[slot] == varbit_id )
        {
            host->settings_mirror_value[slot] = value;
            return;
        }
    }

    if( host->settings_mirror_count >= RS_CS2_HOST_SETTINGS_ACTIONS_MAX )
    {
        /* Drop the oldest, as the action queue does. A dropped mirror is one
         * setting the server keeps its old value for, which the next write of
         * that row corrects; blocking the queue would strand every later one. */
        for( int i = 1; i < host->settings_mirror_count; i++ )
        {
            host->settings_mirror_varbit[i - 1] = host->settings_mirror_varbit[i];
            host->settings_mirror_value[i - 1] = host->settings_mirror_value[i];
        }
        host->settings_mirror_count--;
    }
    slot = host->settings_mirror_count++;
    host->settings_mirror_varbit[slot] = varbit_id;
    host->settings_mirror_value[slot] = value;
}

bool
RS_CS2Host_TakeSettingsMirror(
    struct RS_CS2Host* host,
    int* out_varbit_id,
    int* out_value)
{
    assert(host);
    assert(out_varbit_id);
    assert(out_value);

    if( host->settings_mirror_count <= 0 )
        return false;

    *out_varbit_id = host->settings_mirror_varbit[0];
    *out_value = host->settings_mirror_value[0];
    host->settings_mirror_count--;
    for( int i = 0; i < host->settings_mirror_count; i++ )
    {
        host->settings_mirror_varbit[i] = host->settings_mirror_varbit[i + 1];
        host->settings_mirror_value[i] = host->settings_mirror_value[i + 1];
    }
    return true;
}

/*
 * The All Settings panel's client-side apply hub, script_3967, opens with
 * `%varbit9657 = $int0` and then switches on that same setting id. Setting 12
 * is the client layout, and the switch has no case for it — so a layout picked
 * in All Settings updated the dropdown's own label and nothing else. (The side
 * Display panel's copy of the row works because the *server* arms it and
 * answers with ~gameframe_select_mode; the All Settings copy is armed by
 * nobody, and its enclosing script kind is the one the cache marks
 * client-applied.)
 *
 * The missing case is supplied here, from the one place that can see both
 * halves: the varbit write names the setting, and the writing frame's locals
 * carry the chosen value. The body is script_3998's, which is what the row
 * would have called — window class from the choice, plus the three-way latch
 * WINDOW_STATUS carries so the server remounts the matching gameframe.
 *
 * Returns nothing: a setting id this does not claim simply falls through to
 * the ordinary varbit write, which still has to happen either way.
 */
static void
rs_cs2_settings_apply_client_layout(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    int varbit_id,
    int value)
{
    struct CS2VM2_Frame* frame;
    int want_mode;
    int layout;

    assert(host);
    if( host->varbit_settings_last_changed <= 0 ||
        varbit_id != host->varbit_settings_last_changed )
        return;
    if( value != RS_CS2_SETTING_CLIENT_LAYOUT )
        return;
    if( !vm || vm->frame_sp <= 0 )
        return;
    frame = vm->frames[vm->frame_sp - 1];
    if( !frame || !frame->script ||
        frame->script->script_id != host->script_settings_client_apply )
        return;

    /* script_3967's parameters are (setting id, value, secondary value); the
     * dropdown path fills the second and passes -1 for the third. */
    layout = frame->int_locals[1];
    if( layout < 0 || layout > 2 )
        return;

    want_mode = layout == 0 ? CS2VM_WINDOW_MODE_FIXED : CS2VM_WINDOW_MODE_RESIZABLE;
    if( host->window_mode != want_mode )
    {
        host->window_mode = want_mode;
        host->window_mode_dirty = true;
    }
    /* script_3998 writes the default too, and it is a player choice by the same
     * argument SETDEFAULTWINDOWMODE's handler makes — so it persists. */
    if( host->default_window_mode != want_mode )
    {
        host->default_window_mode = want_mode;
        host->default_window_mode_from_script = true;
    }
    host->client_layout_mode = layout;
    host->client_layout_dirty = true;
}

/* Safe-area bounds (6220..6223, 6231). Desktop client, no notch/home indicator:
 * min corners are 0, max corners are the live canvas size (same source as
 * GETCANVASSIZE / VIEWPORT_GETEFFECTIVESIZE). GETMAXY_ALT (6231) is the same
 * value as GETMAXY per the "alternative opcode used in some contexts" note. */
static int
exec_safearea(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_SafeArea request)
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_EnumGetOutputCount request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Enum* e = provider ? CacheProvider_EnumGet(provider, request.enum_id) : NULL;
    if( !e )
    {
        /* Same unloadable-id rule as exec_enum_lookup: a negative id has no
         * archive to wait for. */
        if( request.enum_id >= 0 )
        {
            if( !rs_cs2_await_spent(thread, exact_request->kind, request.enum_id, -1) )
                return rs_cs2_yield_load(
                    host, thread, exact_request, request.enum_id, -1);
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_StructParam request)
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
        if( !rs_cs2_await_spent(
                thread, exact_request->kind, request.struct_id, request.param_id) )
            return rs_cs2_yield_load(
                host, thread, exact_request, request.struct_id, request.param_id);
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_CC_ComponentParam request)
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
        if( !rs_cs2_await_spent(thread, exact_request->kind, -1, request.param_id) )
            return rs_cs2_yield_load(
                host, thread, exact_request, -1, request.param_id);
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
    struct CS2VM_HostPayload_CC_ComponentParam request)
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
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_OC_Param request)
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
        if( !rs_cs2_await_spent(
                thread, exact_request->kind, request.item_id, request.param_id) )
            return rs_cs2_yield_load(
                host, thread, exact_request, request.item_id, request.param_id);
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

/*
 * NC_PARAM / LC_PARAM: the same answer as exec_oc_param, over an npc or a loc.
 *
 * The yield-and-retry shape is copied from it deliberately -- both the record
 * and the ParamType have to be resident before an answer is possible, and a
 * miss on either is a load rather than a wrong value. A type id below zero is
 * a legitimate script input (an empty target) and is never awaited; the
 * param's own default answers it.
 */
static int
exec_type_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest const* exact_request,
    struct CS2VM_HostPayload_TypeParam request,
    bool is_npc)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Param const* params = NULL;
    int param_count = 0;
    bool have_record = false;
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;

    if( provider && request.type_id >= 0 )
    {
        if( is_npc )
        {
            struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(provider, request.type_id);
            have_record = npc != NULL;
            if( npc )
            {
                params = npc->params;
                param_count = npc->param_count;
            }
        }
        else
        {
            struct ToriRS_Location* loc = CacheProvider_LocationGet(provider, request.type_id);
            have_record = loc != NULL;
            if( loc )
            {
                params = loc->params;
                param_count = loc->param_count;
            }
        }
    }

    if( (!have_record && request.type_id >= 0) || (!param && request.param_id >= 0) )
    {
        if( !rs_cs2_await_spent(
                thread, exact_request->kind, request.type_id, request.param_id) )
            return rs_cs2_yield_load(
                host, thread, exact_request, request.type_id, request.param_id);
        /* Still missing after the load: complete with whatever did arrive. */
    }

    for( int i = 0; i < param_count; i++ )
    {
        if( params[i].key != request.param_id )
            continue;
        if( param && param->is_string )
            return CS2VM2_PushStr(
                thread,
                CS2VM2_StrDup(thread, params[i].string_value ? params[i].string_value : ""));
        if( params[i].string_value )
            continue; /* a string value where an int was asked for is not one. */
        return CS2VM2_PushInt(thread, params[i].int_value);
    }

    if( param && param->is_string )
        return CS2VM2_PushStr(
            thread, CS2VM2_StrDup(thread, param->default_string ? param->default_string : ""));
    return CS2VM2_PushInt(thread, param ? param->default_int : 0);
}

static int
exec_oc_int_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_IntParam request,
    enum CS2VM_HostRequestKind kind)
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
        req.kind = kind;
        req.u.oc_int_param = request;
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
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
    struct CS2VM_HostPayload_OC_Name request)
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
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
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
    struct CS2VM_HostPayload_NC_Name request)
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
        if( !rs_cs2_await_spent(thread, req.kind, request.npc_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.npc_id, -1);
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
    }

    if( npc->name[0] != '\0' )
        name = npc->name;
    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, name));
}

/*
 * OC_PLACEHOLDER / OC_UNPLACEHOLDER: the bank-placeholder form of an obj and
 * back, read off the cache's 148/149 linkage exactly the way the note pair
 * (97/98) is read — an *item* states `placeholder_link` and no template, a
 * *placeholder* states both and its link is the item.
 *
 * Either direction answers the input id when there is no other form, which is
 * what makes `oc_unplaceholder($obj) ! $obj` the client's "is this slot a
 * placeholder" test (script 278 `bankmain_drawitem`); the server's
 * SS_OP_OC_PLACEHOLDER answers the same way from the same two fields.
 */
static int
exec_oc_placeholder_pair(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Unplaceholder request,
    enum CS2VM_HostRequestKind kind)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj = NULL;

    /* item -1 (empty slot) is a valid script input: never yield for it — the
     * yield planner requires a loadable id — and there is nothing to resolve,
     * so pass the id straight through. */
    if( request.item_id < 0 )
        return CS2VM2_PushInt(thread, request.item_id);

    obj = provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = kind;
        req.u.oc_unplaceholder = request;
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
        /* Objtype still missing after its load: pass the id through unresolved. */
        return CS2VM2_PushInt(thread, request.item_id);
    }

    if( obj->placeholder_link > 0 )
    {
        bool is_placeholder = obj->placeholder_template >= 0;
        bool want_placeholder = kind == CS2VM_HOST_REQUEST_OC_PLACEHOLDER;
        if( is_placeholder != want_placeholder )
            return CS2VM2_PushInt(thread, obj->placeholder_link);
    }
    return CS2VM2_PushInt(thread, request.item_id);
}

static int
exec_oc_unplaceholder(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Unplaceholder request)
{
    return exec_oc_placeholder_pair(
        host, thread, request, CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER);
}

/* OC_OP/OC_IOP: ground/inventory right-click action string at a menu slot
 * (op_index 0..4). Real data, following the exact OC_NAME yield-on-miss shape. */
static int
exec_oc_op(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Op request)
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
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
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
    struct CS2VM_HostPayload_OC_Name request)
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
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
        return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
    }

    return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, obj->desc[0] != '\0' ? obj->desc : ""));
}

static int
exec_oc_placeholder(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Unplaceholder request)
{
    return exec_oc_placeholder_pair(host, thread, request, CS2VM_HOST_REQUEST_OC_PLACEHOLDER);
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
    struct CS2VM_HostPayload_OC_Find request,
    enum CS2VM_HostRequestKind kind)
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
            req.kind = kind;
            req.u.oc_find = request;
            if( !rs_cs2_await_spent(thread, req.kind, -1, -1) )
                return rs_cs2_yield_load(host, thread, &req, -1, -1);
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

/*
 * OC_SHIFTCLICKIOP: which inventory op shift-clicking an obj should run.
 *
 * Reference Statics.method9053 (opcode 4213's handler), on ObjType:
 *
 *     if( shiftClickDropIndex >= 0 )  op = inv_actions[i] ? i : -1;
 *     else if( shiftClickDropIndex == -1 )  op = -1;          // opted out
 *     else                                                    // -2, unstated
 *         op = "Drop".equalsIgnoreCase(inv_actions[4]) ? 4 : -1;
 *
 * and the opcode pushes `op + 1` — a 1-based op number, matching CC_GETOP /
 * CC_SETOP — or -1. Both halves matter: script6012 tests the result against
 * -1 and then feeds it to CC_GETOP through enum_4303, so returning the 0-based
 * index would promote the op one slot too early.
 *
 * The -2 fallback is what actually drives shift-drop, since almost no obj
 * states opcode 42: "Drop" being the 5th inventory op is the rule, and the
 * cache field only exists to override it (e.g. shiftclickdrop=2 on items whose
 * 5th op is something else).
 */
static int
exec_oc_shiftclickiop(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj = NULL;
    int index;

    if( request.item_id < 0 )
        return CS2VM2_PushInt(thread, -1);

    obj = provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP;
        req.u.oc_shiftclickiop = request;
        if( !rs_cs2_await_spent(thread, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.item_id, -1);
        /* Objtype still missing after its load: no shift-click op. */
        return CS2VM2_PushInt(thread, -1);
    }

    index = obj->shift_click_drop_index;
    if( index >= 0 )
    {
        if( index >= TORIRS_MENU_ACTION_SLOTS || obj->inv_actions[index][0] == '\0' )
            index = -1;
    }
    else if( index == -1 )
    {
        /* Stated as "no shift-click op". */
    }
    else
    {
        index = strcasecmp(obj->inv_actions[TORIRS_MENU_ACTION_SLOTS - 1], "Drop") == 0
                    ? TORIRS_MENU_ACTION_SLOTS - 1
                    : -1;
    }

    return CS2VM2_PushInt(thread, index < 0 ? -1 : index + 1);
}

/* OC_WEARPOS/WEARPOS2/WEARPOS3: no equip slot data exists on ToriRS_Objtype
 * yet, so every variant answers "not equippable". */
static int
exec_oc_wearpos(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_WearPos request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_WEIGHT: no weight data exists on ToriRS_Objtype yet. */
static int
exec_oc_weight(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Name request)
{
    (void)request;
    return CS2VM2_PushInt(thread, 0);
}

/* oc_isubop(obj, opIndex, subIndex) -> string. No sub-menu nesting exists on
 * ToriRS_Objtype yet. */
static int
exec_oc_isubop(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_OC_Isubop request)
{
    (void)request;
    return CS2VM2_PushStr(thread, CS2VM2_StrEmpty(thread));
}

static int
exec_set_graphic(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostPayload_CC_SetGraphic request,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = rs_cs2_tree(host);
    (void)thread;

    static int objicon_debug = -1;
    if( objicon_debug < 0 )
        objicon_debug = getenv("TORIRS_OBJICON_DEBUG") != NULL;
    if( objicon_debug )
        fprintf(
            stderr,
            "GFXDBG: com=0x%08x gfx=%d\n",
            (unsigned)request.component_id,
            request.graphic_id);

    if( request.graphic_id >= 0 && !rs_cs2_sprite_ready(host, request.graphic_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = kind;
        req.u.cc_set_graphic = request;
        if( !rs_cs2_await_spent(thread, req.kind, request.graphic_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.graphic_id, -1);
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
    int num_mode,
    enum CS2VM_HostRequestKind kind)
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
        req.kind = kind;
        req.u.cc_set_object.component_id = component_id;
        req.u.cc_set_object.obj_id = obj_id;
        req.u.cc_set_object.count = count;
        req.u.cc_set_object.num_mode = num_mode;
        if( provider && !rs_cs2_await_spent(thread, req.kind, obj_id, count) )
            return rs_cs2_yield_load(host, thread, &req, obj_id, count);
        if( !provider )
        {
            (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0, num_mode);
            return CS2VM_EXECNO_OK;
        }
        /* Still incomplete after the load — a texture that failed, say. Build
         * the icon from what did arrive; the raster skips missing faces. */
    }

    /*
     * A type-6 (MODEL) widget draws the obj in 3D, not as its 2D icon.
     *
     * The reference's `IfType.getModel` prefers `objectId` over `modelId` and
     * builds the ObjType's interface model from it; only the type-5 GRAPHIC
     * path uses the baked 32x32 sprite. Handing a MODEL node the sprite id
     * left `rs_model.gamecache_model_id` at the -1 CC_CREATE gives it, and the
     * emit arm reads exactly that field — so the node described as "nothing to
     * draw" and the cell rendered as an empty button.
     *
     * Found on `skillmulti` (the make-menu), whose eighteen product cells are
     * `cc_create(^iftype_model)` + `cc_setobject_nonum` — every cell drew its
     * beige button, its keyboard hint and its tooltip, and no item.
     *
     * `EnsureObjModel` takes no count: it is the base inventory model, so a
     * stackable's cell shows one of the thing rather than the pile variant
     * `count` would select. The panel never draws a number on these cells
     * (num_mode 2), so the pile is decoration the reference happens to have
     * and this does not.
     */
    if( host->bridge )
    {
        int32_t model_idx = tree ? UITree_FindByComponentId(tree, component_id) : -1;

        if( model_idx >= 0 && tree->components[model_idx].type == UIELEM_RS_MODEL )
        {
            /* The stack variant, when the obj has one. Baking the BASE model
             * for a stackable asks for a model the loader never fetched — the
             * make-menu's `arrow_shaft` cell drew nothing while the bows
             * beside it drew fine, because `count` is ^max_32bit_int and the
             * loader had resolved the "many" variant. */
            int render_obj = provider ? ObjModelLoad_RenderObjId(provider, obj_id, count) : obj_id;
            int obj_model = UITreeSceneBridge_EnsureObjModel(host->bridge, render_obj);

            if( obj_model > 0 )
            {
                /* Both: the obj is what the minimenu and the tooltip read, the
                 * model is what the emit walk draws. */
                (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0, num_mode);
                (void)UITree_ApplyModel(tree, component_id, obj_model);
                /*
                 * ...and the objtype's own 2D presentation on top, or the cell
                 * shows a close-up of one face rather than an item.
                 *
                 * CC_CREATE leaves a MODEL node at zoom 100, and 100 in these
                 * units is a camera roughly twenty times too close: `zoom2d`
                 * is what the icon rasteriser uses (default 2000, see
                 * bridge_rasterize_obj_icon) and it is the same scale the
                 * widget draw wants. The angles come from the same three
                 * fields for the same reason — an item drawn at its model's
                 * own orientation is not the shape players recognise.
                 *
                 * This is the CS2 twin of the IF_SETOBJECT packet path in
                 * app.c, which reaches the identical fields through
                 * App_SetInterfaceObjModel. The two differ only in where the
                 * divisor comes from: the packet carries a wire zoom, the
                 * opcode's second argument is a COUNT, so there is nothing to
                 * divide by here.
                 */
                struct ToriRS_Objtype* objtype =
                    provider ? CacheProvider_ObjtypeGet(provider, render_obj) : NULL;

                if( objtype )
                {
                    (void)UITree_ApplyModelAngle(
                        tree,
                        component_id,
                        objtype->xan2d,
                        objtype->yan2d,
                        objtype->zoom2d > 0 ? objtype->zoom2d : 2000);
                    /* yof2d is the other half of the same composition and is
                     * not optional: `arrow_shaft` carries -29, and without it
                     * the shaft projects clean out of its cell.
                     *
                     * xof2d is deliberately NOT passed. The widget transform
                     * has no X translation — every backend maps the emit's
                     * `model_x_offset` onto `orientation`, a ROTATION — so
                     * handing it xof2d would spin the model rather than shift
                     * it. The values are -3..7, small enough that dropping the
                     * shift beats introducing a tilt. */
                    (void)UITree_ApplyModelOffset(tree, component_id, 0, objtype->offset_y2d);
                }
                return CS2VM_EXECNO_OK;
            }
            /* No model for this obj — fall through to the icon, which is at
             * least something the player can identify. */
        }
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
    struct CS2VM_HostPayload_CC_SetTextFont request,
    enum CS2VM_HostRequestKind kind)
{
    (void)thread;

    if( request.font_id >= 0 && !rs_cs2_font_ready(host, request.font_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = kind;
        req.u.cc_set_text_font = request;
        if( !rs_cs2_await_spent(thread, req.kind, request.font_id, -1) )
            return rs_cs2_yield_load(host, thread, &req, request.font_id, -1);
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
    struct CS2VM_HostPayload_CC_Copy request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int const parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_COPY;
    yield_req.u.cc_copy = request;
    yield_res = rs_cs2_yield_if_group_missing(host, vm, parent_id, &yield_req);
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

/*
 * ---------------------------------------------------------------------------
 * Scripted entity overlays (game/rs_entity_overlay.h).
 * ---------------------------------------------------------------------------
 *
 * An overlay is a UITree LAYER parented to the `entity_overlay` builtin, plus a
 * record saying what in the world it hangs off. The CS2 ops address it by
 * INDEX; everything that decorates it (`_103` create, `_104` deleteall, `_203`
 * find) turns the index into that layer's component id and then does exactly
 * what the panel-facing op of the same name does.
 *
 * Where it goes on screen is not decided here: the App projects the anchor and
 * writes the layer's box each frame, because the answer depends on the camera.
 */

/** Which subject the overlay ops for `kind` are about — the same resolution the
 *  `_67xx/_68xx/_69xx` getters use, so an op and the getters beside it in a
 *  script cannot disagree about what "the active loc" is. */
static struct RS_ClientOpContext const*
rs_cs2_overlay_subject(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    enum RS_ClientOpKind kind)
{
    int running = -1;

    assert(host);

    if( vm && vm->frame_sp > 0 && vm->frames[0] && vm->frames[0]->script )
        running = vm->frames[0]->script->script_id;
    return RS_ClientOpSubject(&host->clientop, kind, running);
}

/** The layer's component id for an overlay index, or -1. */
static int
rs_cs2_overlay_component_id(struct RS_CS2Host* host, int index)
{
    struct RS_Overlay const* item;

    assert(host);

    item = RS_OverlayGet(&host->overlay, index);
    return item ? item->component_id : -1;
}

/** Drop an overlay and the layer it owns together. Splitting the two is what
 *  would leave a node drawing for an overlay nothing points at any more. */
static void
rs_cs2_overlay_free(struct RS_CS2Host* host, int index)
{
    struct UITree* tree = rs_cs2_tree(host);
    int component_id;

    assert(host);

    component_id = rs_cs2_overlay_component_id(host, index);
    if( tree && component_id >= 0 )
    {
        int32_t const node = UITree_FindByComponentId(tree, component_id);
        if( node >= 0 )
            UITree_CcDelete(tree, node);
    }
    RS_OverlayDestroy(&host->overlay, index);
}

/** Give a freshly-taken overlay its layer. Returns the index, or -1 (having
 *  released the record) when the tree cannot hold one. */
static int
rs_cs2_overlay_attach_layer(struct RS_CS2Host* host, int index)
{
    struct UITree* tree = rs_cs2_tree(host);
    struct RS_Overlay* item;
    int32_t node;

    assert(host);

    item = RS_OverlayGetMut(&host->overlay, index);
    if( !item )
        return -1;
    if( !tree )
    {
        /* No tree at all — a headless run. The record stands so the GET ops
         * still answer; nothing draws, which is what "no tree" means. */
        return index;
    }
    node = UITree_EntityOverlayCreateLayer(tree, index, item->width, item->height);
    if( node < 0 )
    {
        RS_OverlayDestroy(&host->overlay, index);
        return -1;
    }
    item->component_id = tree->components[node].component_id;
    return index;
}

void
RS_CS2Host_OverlayReap(struct RS_CS2Host* host, int index)
{
    assert(host);
    rs_cs2_overlay_free(host, index);
}

static int
exec_entity_overlay(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_EntityOverlay request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int const* a = request.args;
    struct RS_ClientOpContext const* subject;
    int index = -1;

    assert(host);

    if( getenv("TORIRS_OVERLAY_SCRIPT_DEBUG") )
    {
        fprintf(stderr, "overlay: op %d args", request.opcode);
        for( int i = 0; i < request.arg_count; i++ )
            fprintf(stderr, " %d", a[i]);
        fprintf(stderr, "\n");
    }

    switch( request.opcode )
    {
    /* ---- create ------------------------------------------------------- */
    case CS2_OP_OVERLAY_NPC_CREATE:
    case CS2_OP_OVERLAY_PLAYER_CREATE:
    {
        bool const is_npc = request.opcode == CS2_OP_OVERLAY_NPC_CREATE;
        subject = rs_cs2_overlay_subject(
            host, vm, is_npc ? RS_CLIENTOP_NPC : RS_CLIENTOP_PLAYER);
        if( subject )
            index = RS_OverlayCreateEntity(
                &host->overlay,
                is_npc ? RS_OVERLAY_ANCHOR_NPC : RS_OVERLAY_ANCHOR_PLAYER,
                subject->uid,
                a[0],
                a[1],
                a[2],
                a[3]);
        index = index >= 0 ? rs_cs2_overlay_attach_layer(host, index) : -1;
        return CS2VM2_PushInt(vm, index);
    }
    case CS2_OP_OVERLAY_LOC_CREATE:
    {
        subject = rs_cs2_overlay_subject(host, vm, RS_CLIENTOP_LOC);
        /* The loc's LAYER, not its type, is what makes two locs on one tile
         * separable — the reference keys its static store on (coord, LocLayer)
         * and OverlayTypeFromLocLayer is the identity. */
        if( subject && RS_OverlayTypeValid(subject->layer) )
            index = RS_OverlayCreateStatic(
                &host->overlay, subject->coord, subject->layer, a[0], a[1], a[2], a[3]);
        index = index >= 0 ? rs_cs2_overlay_attach_layer(host, index) : -1;
        return CS2VM2_PushInt(vm, index);
    }
    case CS2_OP_OVERLAY_COORD_CREATE:
        index = RS_OverlayCreateStatic(
            &host->overlay, a[0], RS_OVERLAY_TYPE_COORD, a[1], a[2], a[3], a[4]);
        index = index >= 0 ? rs_cs2_overlay_attach_layer(host, index) : -1;
        return CS2VM2_PushInt(vm, index);

    /* ---- look up ------------------------------------------------------ */
    case CS2_OP_OVERLAY_NPC_GET:
    case CS2_OP_OVERLAY_PLAYER_GET:
    {
        bool const is_npc = request.opcode == CS2_OP_OVERLAY_NPC_GET;
        subject = rs_cs2_overlay_subject(
            host, vm, is_npc ? RS_CLIENTOP_NPC : RS_CLIENTOP_PLAYER);
        if( subject )
            index = RS_OverlayFindEntity(
                &host->overlay,
                is_npc ? RS_OVERLAY_ANCHOR_NPC : RS_OVERLAY_ANCHOR_PLAYER,
                subject->uid,
                a[0]);
        return CS2VM2_PushInt(vm, index);
    }
    case CS2_OP_OVERLAY_LOC_GET:
        subject = rs_cs2_overlay_subject(host, vm, RS_CLIENTOP_LOC);
        if( subject )
            index =
                RS_OverlayFindStatic(&host->overlay, subject->coord, subject->layer, a[0]);
        return CS2VM2_PushInt(vm, index);
    case CS2_OP_OVERLAY_COORD_GET:
        index = RS_OverlayFindStatic(&host->overlay, a[0], RS_OVERLAY_TYPE_COORD, a[1]);
        return CS2VM2_PushInt(vm, index);

    /* ---- destroy ------------------------------------------------------ */
    case CS2_OP_OVERLAY_NPC_DESTROY:
    case CS2_OP_OVERLAY_PLAYER_DESTROY:
    {
        bool const is_npc = request.opcode == CS2_OP_OVERLAY_NPC_DESTROY;
        subject = rs_cs2_overlay_subject(
            host, vm, is_npc ? RS_CLIENTOP_NPC : RS_CLIENTOP_PLAYER);
        if( subject )
            rs_cs2_overlay_free(
                host,
                RS_OverlayFindEntity(
                    &host->overlay,
                    is_npc ? RS_OVERLAY_ANCHOR_NPC : RS_OVERLAY_ANCHOR_PLAYER,
                    subject->uid,
                    a[0]));
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_OVERLAY_LOC_DESTROY:
        subject = rs_cs2_overlay_subject(host, vm, RS_CLIENTOP_LOC);
        if( subject )
            rs_cs2_overlay_free(
                host,
                RS_OverlayFindStatic(&host->overlay, subject->coord, subject->layer, a[0]));
        return CS2VM_EXECNO_OK;
    case CS2_OP_OVERLAY_COORD_DESTROY:
        rs_cs2_overlay_free(
            host, RS_OverlayFindStatic(&host->overlay, a[0], RS_OVERLAY_TYPE_COORD, a[1]));
        return CS2VM_EXECNO_OK;

    /* ---- decorate ------------------------------------------------------ */
    case CS2_OP_OVERLAY_FIND:
    {
        int const component_id = rs_cs2_overlay_component_id(host, a[0]);
        int found = 0;
        if( tree && component_id >= 0 && UITree_FindByComponentId(tree, component_id) >= 0 )
        {
            rs_cs2_set_cc_target(vm, request.dot_operand, component_id);
            found = 1;
        }
        return CS2VM2_PushInt(vm, found);
    }
    case CS2_OP_OVERLAY_CC_FIND:
    {
        int const component_id = rs_cs2_overlay_component_id(host, a[0]);
        int32_t parent = tree && component_id >= 0
                             ? UITree_FindByComponentId(tree, component_id)
                             : -1;
        int32_t child =
            parent >= 0 ? UITree_FindChildBySubid(tree, parent, component_id, a[1]) : -1;
        if( child >= 0 )
            rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child].component_id);
        return CS2VM2_PushInt(vm, child >= 0 ? 1 : 0);
    }
    case CS2_OP_OVERLAY_CC_CREATE:
    {
        int const component_id = rs_cs2_overlay_component_id(host, a[0]);
        int32_t parent = tree && component_id >= 0
                             ? UITree_FindByComponentId(tree, component_id)
                             : -1;
        int32_t child;
        /* "Dynamic layers aren't allowed" — the reference aborts on type 0
         * here, because a layer inside an overlay would need an overlay of its
         * own to be positioned. No script in this cache asks for one. */
        assert(a[1] != 0);
        if( parent < 0 )
            return CS2VM_EXECNO_OK;
        child = UITree_CcCreate(tree, parent, component_id, a[1], a[2]);
        if( child < 0 )
            return CS2VM_EXECNO_ERROR;
        rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child].component_id);
        return CS2VM_EXECNO_OK;
    }
    case CS2_OP_OVERLAY_CC_DELETEALL:
    {
        int const component_id = rs_cs2_overlay_component_id(host, a[0]);
        int32_t parent = tree && component_id >= 0
                             ? UITree_FindByComponentId(tree, component_id)
                             : -1;
        if( parent >= 0 )
            UITree_CcDeleteAll(tree, parent);
        return CS2VM_EXECNO_OK;
    }
    default:
        break;
    }

    fprintf(stderr, "cs2: opcode %d is not a scripted-entity-overlay op\n", request.opcode);
    return CS2VM_EXECNO_ERROR;
}

/* LOC_FIND (6803) and COORD_INSCENE (6951). Both need the SCENE, which this
 * host has no pointer to — the App answers through the callbacks below for the
 * same reason it answers events_override_for_component. */
static int
exec_subject_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_SubjectFind request)
{
    assert(host);

    if( request.opcode == CS2_OP_COORD_INSCENE )
    {
        int const inside =
            host->coord_in_scene ? host->coord_in_scene(host->world_user, request.coord) : 0;
        return CS2VM2_PushInt(vm, inside ? 1 : 0);
    }

    assert(request.opcode == CS2_OP_LOC_FIND);
    {
        struct RS_ClientOpContext found;
        int layer = -1;
        char name[RS_CLIENTOP_NAME_MAX] = { 0 };
        int const hit = host->loc_at_coord ? host->loc_at_coord(
                                                 host->world_user,
                                                 request.coord,
                                                 request.loc_type,
                                                 &layer,
                                                 name,
                                                 (int)sizeof(name))
                                           : 0;
        if( !hit )
        {
            /*
             * Clear the register rather than leaving the last loc in it.
             *
             * The scripts that call this call it in a loop over candidate
             * tiles; a stale answer would put the next tile's overlay on the
             * previous tile's loc, which is exactly the class of silent wrong
             * answer the register exists to avoid.
             */
            RS_ClientOpActiveSet(&host->clientop, RS_CLIENTOP_LOC, NULL);
            return CS2VM2_PushInt(vm, 0);
        }
        memset(&found, 0, sizeof(found));
        found.kind = RS_CLIENTOP_LOC;
        found.uid = -1;
        found.type = request.loc_type;
        found.coord = request.coord;
        found.layer = layer;
        snprintf(found.name, sizeof(found.name), "%s", name);
        RS_ClientOpActiveSet(&host->clientop, RS_CLIENTOP_LOC, &found);
        return CS2VM2_PushInt(vm, 1);
    }
}

static int
exec_cc_create(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_CC_Create request,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = rs_cs2_tree(host);
    int parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = kind;
    yield_req.u.cc_create = request;
    yield_res = rs_cs2_yield_if_group_missing(host, vm, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    assert(tree);

    if( request.parent_is_sibling )
    {
        parent_id = rs_cs2_parent_component_id(tree, request.parent_id);
        if( parent_id < 0 )
            return CS2VM_EXECNO_ERROR;
    }

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

    if( torirs_cc_debug() )
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
    struct CS2VM_HostPayload_CC_Find request,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = rs_cs2_tree(host);
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };
    int32_t parent_idx;

    yield_req.kind = kind;
    yield_req.u.cc_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, vm, request.parent_id, &yield_req);
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
    struct CS2VM_HostPayload_TargetFind request)
{
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_IF_FIND;
    yield_req.u.if_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, vm, request.component_id, &yield_req);
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
    if( kind == CS2VM_HOST_REQUEST_CC_CHILDREN_FIND_COUNT )
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

    yield_res = rs_cs2_yield_if_group_missing(host, vm, parent_id, &yield_req);
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
    struct CS2VM_HostPayload_WidgetSetModel request,
    enum CS2VM_HostRequestKind kind)
{
    if( request.model_id >= 0 && !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = kind;
        req.u.widget_set_model = request;
        if( !rs_cs2_await_spent(vm, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, vm, &req, request.model_id, -1);
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
    struct CS2VM_HostPayload_WidgetSetModelKind request,
    enum CS2VM_HostRequestKind kind)
{
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
        req.kind = kind;
        req.u.widget_set_model_kind = request;
        if( !rs_cs2_await_spent(vm, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, vm, &req, request.model_id, -1);
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
            req.kind = kind;
            req.u.widget_set_model_kind = request;
            if( !rs_cs2_await_spent(vm, req.kind, request.model_id, -1) )
                return rs_cs2_yield_load(host, vm, &req, request.model_id, -1);
            /* Npctype/heads still missing after load: leave the widget as it was. */
            return CS2VM_EXECNO_OK;
        }
        {
            int resolved_npc_id = request.model_id;
            int scene_model;
            bool applied = false;
            (void)rs_cs2_npc_multi_resolve(
                host, request.model_id, &resolved_npc_id);
            scene_model = resolved_npc_id < 0
                              ? -1
                              : UITreeSceneBridge_EnsureNpcHead(
                                    host->bridge, resolved_npc_id);
            if( scene_model >= 0 )
                applied = UITree_ApplyModel(
                    rs_cs2_tree(host), request.component_id, scene_model);
            if( getenv("TORIRS_NPC_HEAD_DEBUG") )
                fprintf(
                    stderr,
                    "npc_head: npc=%d resolved=%d component=0x%08x scene=%d applied=%d\n",
                    request.model_id,
                    resolved_npc_id,
                    (unsigned)request.component_id,
                    scene_model,
                    (int)applied);
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
    struct CS2VM_HostPayload_WidgetSetInt request,
    enum CS2VM_HostRequestKind kind)
{
    struct UITreeComponent* node = rs_cs2_node(host, request.component_id);
    int32_t idx;
    if( !node )
    {
        /* Scripts set properties on other groups (e.g. interface 100's search
         * button targets chatbox 162:36). Sub-mount the group; once it is
         * baked, a still-missing child is a no-op (reference tolerates sets on
         * absent widgets). */
        struct CS2VM_HostRequest req = { 0 };
        req.kind = kind;
        req.u.widget_set_int = request;
        return rs_cs2_yield_if_group_missing(host, vm, request.component_id, &req);
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
        else if( node->type == UIELEM_RS_ARC )
            node->u.rs_arc.line_width = request.value > 0 ? request.value : 1;
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
    /* IF/CC_SET2DANGLE. The only animated user is the world map's marker
     * timer (clientscript 1758 re-states the angle every tick from
     * clientclock), so a no-op here reads as "the You Are Here arrow is drawn
     * but never turns". */
    case CS2VM_WIDGET_INT_ANGLE_2D:
        (void)UITree_ApplyGraphic2DAngle(
            rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
        /* IF/CC_SETMODELORTHOG selects the reference client's orthographic
         * widget-model path.  Treating this as a no-op leaves tall actor
         * models crossing the perspective near-plane, so only disconnected
         * faces render even though the model and animation are complete. */
        if( node->type == UIELEM_RS_MODEL )
            node->u.rs_model.orthog = request.value != 0;
        break;
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

/* CC/IF_SETARC. The two angles are the whole shape of a type-10 widget: with
 * start == end it is a zero-width sector and draws nothing, which is how the
 * countdown pie's wedge starts and how it ends.
 *
 * A set on a component that is not an arc is dropped rather than asserted --
 * the reference writes IfType +0x9c/+0xa0 on whatever the active component is,
 * and every other CC setter here tolerates the same mismatch. */
static int
exec_widget_set_arc(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_WidgetSetArc request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int32_t idx;
    (void)vm;
    if( !tree )
        return CS2VM_EXECNO_OK;
    idx = UITree_FindByComponentId(tree, request.component_id);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_ARC )
        return CS2VM_EXECNO_OK;
    tree->components[idx].u.rs_arc.arc_start = request.arc_start;
    tree->components[idx].u.rs_arc.arc_end = request.arc_end;
    UITree_MarkNodeDirty(tree, idx);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_angle(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_WidgetSetModelAngle request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int32_t idx;
    (void)vm;
    if( !tree )
        return CS2VM_EXECNO_OK;
    idx = UITree_FindByComponentId(tree, request.component_id);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return CS2VM_EXECNO_OK;
    tree->components[idx].u.rs_model.x_offset = request.offset_x;
    tree->components[idx].u.rs_model.y_offset = request.offset_y;
    tree->components[idx].u.rs_model.xan = request.angle_x;
    tree->components[idx].u.rs_model.yan = request.angle_y;
    tree->components[idx].u.rs_model.zan = request.angle_z;
    if( request.zoom > 0 )
        tree->components[idx].u.rs_model.zoom = request.zoom;
    UITree_MarkNodeDirty(tree, idx);
    return CS2VM_EXECNO_OK;
}

/* Acquire the inv-transmit hook slot for component_id. Re-registration for the
 * same component reuses its entry (the new script supersedes the old) while
 * preserving its dispatch state — a transmit script re-registering itself must not
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
    int component_id,
    int create)
{
    int i;
    struct RS_CS2InvTransmitHook* hook;

    for( i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        hook = &host->inv_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            uint8_t const pending_unhide = create ? hook->pending_unhide : 0;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            hook->pending_unhide = pending_unhide;
            return hook;
        }
    }

    /* A null registration (`if_setoninvtransmit(null, com)`) disarms. It must
     * not mint a slot for a component that never had one — a script that
     * disarms unconditionally would fill the registry with dead entries. */
    if( !create )
        return NULL;

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
    int component_id,
    int create)
{
    int i;
    struct RS_CS2VarTransmitHook* hook;

    for( i = 0; i < host->var_transmit_hook_count; i++ )
    {
        hook = &host->var_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            uint8_t const pending_unhide = create ? hook->pending_unhide : 0;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            hook->pending_unhide = pending_unhide;
            return hook;
        }
    }

    /* A null registration (`if_setonvartransmit(null, com)`) disarms. It must
     * not mint a slot for a component that never had one — a script that
     * disarms unconditionally would fill the registry with dead entries. */
    if( !create )
        return NULL;

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

/*
 * A cache-authored hook's arguments, in the shape the transmit tables want.
 *
 * `ToriRS_ScriptHook::argv[0]` is the script id and the rest are the script's
 * arguments — the same split the onload dispatch makes (task_uitree_build.c
 * step 4), including `str_mask >> 1`: position 0 is the script id and is always
 * an int, so the mask that describes the *arguments* is the record's mask with
 * that position shifted off. Getting this wrong does not fail loudly; it feeds
 * a string into an int local.
 */
static void
rs_cs2_cache_hook_args(
    int* int_args,
    int* int_arg_count,
    uint64_t* str_arg_mask,
    int* str_arg_count,
    char str_args[][CS2VM_SETON_STR_ARG_LEN],
    struct ToriRS_ScriptHook const* src)
{
    int argc = src->argc > 0 ? src->argc - 1 : 0;

    if( argc > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        argc = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    if( argc > 0 )
        memcpy(int_args, src->argv + 1, (size_t)argc * sizeof(int));
    *int_arg_count = argc;

    *str_arg_mask = src->str_mask >> 1;
    *str_arg_count =
        src->str_argc > CS2VM_SETON_STR_ARG_MAX ? CS2VM_SETON_STR_ARG_MAX : src->str_argc;
    for( int i = 0; i < CS2VM_SETON_STR_ARG_MAX; i++ )
    {
        snprintf(str_args[i], CS2VM_SETON_STR_ARG_LEN, "%s", src->strv[i]);
    }
}

static void
rs_cs2_cache_hook_triggers(
    int* dst,
    int* dst_count,
    int const* src,
    int count)
{
    if( count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( count > 0 )
        memcpy(dst, src, (size_t)count * sizeof(int));
    *dst_count = count;
}

/* Defined below, beside the var and inv twins it is a copy of. */
static struct RS_CS2StatTransmitHook*
rs_cs2_acquire_stat_transmit_hook(
    struct RS_CS2Host* host,
    int component_id,
    int create);

/*
 * Register the transmit hooks a component record declares in the CACHE.
 *
 * `onVarpTransmit` + `varpTriggers` (and the inv and stat pairs) are the record's own
 * form of `if_setonvartransmit` — the same script, the same captured arguments,
 * the same trigger list — and the decoder has always copied both onto
 * `ToriRS_Component`. Nothing read them, so only a CS2 script could arm a
 * transmit hook: a widget whose handler lives in the cache painted once from
 * its onload and then never again, however many times its varp changed.
 *
 * The combat tab's auto-retaliate button is the visible case. 593:32 declares
 * `onload=i:325`, `onvarptransmit=i:325`, `varptriggers=172`; clicking it sent
 * IF_BUTTON1 593:32, the server flipped `option_nodef` and transmitted varp 172
 * — and the button went on saying "Auto Retaliate (On)", because script 325 had
 * run exactly once, at mount. Every end of that chain was working and the
 * feature still read as broken.
 *
 * Called from both bake paths (boot builder and IF_OPENSUB) before their
 * initial transmit dispatch, so a mount paints from the cache hook on the same
 * pass a CS2-registered one would.
 */
void
RS_CS2_RegisterCacheTransmitHooks(
    struct RS_CS2Host* host,
    struct ToriRS_Component const* src)
{
    /* The three channels are one shape, so they are one loop: which cache hook
     * feeds which host table, and which trigger list goes with it. */
    static struct
    {
        enum ToriRS_ComponentHookKind kind;
        int channel; /* 0 varp, 1 inv, 2 stat */
    } const k_channels[] = {
        { TORIRS_COMPONENT_HOOK_VARP_TRANSMIT, 0 },
        { TORIRS_COMPONENT_HOOK_INV_TRANSMIT, 1 },
        { TORIRS_COMPONENT_HOOK_STAT_TRANSMIT, 2 },
    };

    assert(host);
    assert(src);

    for( size_t i = 0; i < sizeof(k_channels) / sizeof(k_channels[0]); i++ )
    {
        struct ToriRS_ScriptHook const* cache_hook =
            ToriRS_ComponentHookPeek(src, k_channels[i].kind);
        int* trigger_ids;
        int* trigger_count;
        int const* src_triggers;
        int src_trigger_count;
        int* int_args;
        int* int_arg_count;
        uint64_t* str_arg_mask;
        int* str_arg_count;
        char (*str_args)[CS2VM_SETON_STR_ARG_LEN];
        int* component_id;
        int* script_id;

        if( !cache_hook || cache_hook->argc <= 0 || cache_hook->argv[0] <= 0 )
            continue;

        if( k_channels[i].channel == 0 )
        {
            struct RS_CS2VarTransmitHook* hook = rs_cs2_acquire_var_transmit_hook(host, src->id, 1);
            if( !hook )
                continue;
            component_id = &hook->component_id;
            script_id = &hook->script_id;
            int_args = hook->int_args;
            int_arg_count = &hook->int_arg_count;
            str_arg_mask = &hook->str_arg_mask;
            str_arg_count = &hook->str_arg_count;
            str_args = hook->str_args;
            trigger_ids = hook->trigger_ids;
            trigger_count = &hook->trigger_count;
            src_triggers = src->varp_triggers;
            src_trigger_count = src->varp_triggers_count;
        }
        else if( k_channels[i].channel == 1 )
        {
            struct RS_CS2InvTransmitHook* hook = rs_cs2_acquire_inv_transmit_hook(host, src->id, 1);
            if( !hook )
                continue;
            component_id = &hook->component_id;
            script_id = &hook->script_id;
            int_args = hook->int_args;
            int_arg_count = &hook->int_arg_count;
            str_arg_mask = &hook->str_arg_mask;
            str_arg_count = &hook->str_arg_count;
            str_args = hook->str_args;
            trigger_ids = hook->trigger_ids;
            trigger_count = &hook->trigger_count;
            src_triggers = src->inventory_triggers;
            src_trigger_count = src->inventory_triggers_count;
        }
        else
        {
            struct RS_CS2StatTransmitHook* hook = rs_cs2_acquire_stat_transmit_hook(host, src->id, 1);
            if( !hook )
                continue;
            component_id = &hook->component_id;
            script_id = &hook->script_id;
            int_args = hook->int_args;
            int_arg_count = &hook->int_arg_count;
            str_arg_mask = &hook->str_arg_mask;
            str_arg_count = &hook->str_arg_count;
            str_args = hook->str_args;
            trigger_ids = hook->trigger_ids;
            trigger_count = &hook->trigger_count;
            src_triggers = src->stat_triggers;
            src_trigger_count = src->stat_triggers_count;
        }

        *component_id = src->id;
        *script_id = cache_hook->argv[0];
        rs_cs2_cache_hook_args(
            int_args, int_arg_count, str_arg_mask, str_arg_count, str_args, cache_hook);
        rs_cs2_cache_hook_triggers(trigger_ids, trigger_count, src_triggers, src_trigger_count);
    }
}

static int
exec_set_on_inv_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostPayload_IF_SetOnInvTransmit const* request)
{
    struct RS_CS2InvTransmitHook* hook;
    assert(host);
    assert(request);
    hook = rs_cs2_acquire_inv_transmit_hook(host, request->component_id, request->script_id > 0);
    if( !hook )
    {
        /* Two ways to get here now, and only one is a defect: the registry is
         * full, or this was a disarm of a component that had no hook. */
        if( request->script_id > 0 )
            fprintf(
                stderr,
                "rs_cs2_host: inv_transmit_hooks full (%d), dropping script_id=%d "
                "component_id=%d\n",
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
    int component_id,
    int create)
{
    int i;
    struct RS_CS2StatTransmitHook* hook;

    for( i = 0; i < host->stat_transmit_hook_count; i++ )
    {
        hook = &host->stat_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            uint8_t const pending_unhide = create ? hook->pending_unhide : 0;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            hook->pending_unhide = pending_unhide;
            return hook;
        }
    }

    /* A null registration (`if_setonstattransmit(null, com)`) disarms. It must
     * not mint a slot for a component that never had one — a script that
     * disarms unconditionally would fill the registry with dead entries. */
    if( !create )
        return NULL;

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

/*
 * Is anything left in this block that the clear below deliberately kept?
 *
 * These must survive IF_CLOSE on a reused bake — the compass `on_op` is
 * installed once by the gameframe onload and is not re-registered when a
 * sidebar pack remounts.
 *
 * It has to name *every* surviving hook, not just the click-shaped ones, and
 * that is the whole of a bug this cost: the clear carefully preserves
 * mouse-over/leave/repeat and scroll-wheel — there is a comment saying so — and
 * then handed the block to `UITree_FreeHooksAt` because none of them counted
 * here. A node whose only hooks are hover hooks is exactly the case, and
 * interface 182's two labels are exactly that node: "Click here to logout" and
 * "World Switcher" carry `onmouseover`/`onmouseleave` and nothing else, so the
 * text stopped changing colour under the pointer the first time the sidebar
 * group was cleared. The preserved set and the retention test are one decision
 * written twice; keep them together.
 */
static bool
rs_cs2_hooks_survive_clear(struct UITreeRuntimeHooks const* hooks)
{
    assert(hooks);
    return hooks->on_op.script_id > 0 || hooks->on_click.script_id > 0 ||
           hooks->on_hold.script_id > 0 || hooks->on_click_repeat.script_id > 0 ||
           hooks->on_release.script_id > 0 || hooks->on_target_enter.script_id > 0 ||
           hooks->on_target_leave.script_id > 0 || hooks->on_drag.script_id > 0 ||
           hooks->on_drag_complete.script_id > 0 ||
           hooks->on_mouse_over.script_id > 0 || hooks->on_mouse_leave.script_id > 0 ||
           hooks->on_mouse_repeat.script_id > 0 || hooks->on_scroll_wheel.script_id > 0;
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

    /* UITree_HookClear, not memset: a slot owns its argument tails now. */
    UITree_HookClear(&hooks->on_timer);
    UITree_HookClear(&hooks->on_key);
    UITree_HookClear(&hooks->on_key_down);
    UITree_HookClear(&hooks->on_key_up);
    UITree_HookClear(&hooks->on_var_transmit);
    UITree_HookClear(&hooks->on_inv_transmit);
    UITree_HookClear(&hooks->on_misc_transmit);
    UITree_HookClear(&hooks->on_friend_transmit);
    UITree_HookClear(&hooks->on_chat_transmit);
    UITree_HookClear(&hooks->on_dialog_abort);
    UITree_HookClear(&hooks->on_resize);
    UITree_HookClear(&hooks->on_sub_change);
    /* Mouse-over/leave/repeat and scroll-wheel are not interaction clicks but
     * are also not the transmit/timer/resize set ClearHooks is for; leave them.
     * They are cache-authored as often as script-authored, and a remount does
     * NOT put them back — `task_interface_open` reuses an already-baked group
     * rather than re-baking it — so dropping one here loses it for the session.
     * `rs_cs2_hooks_survive_clear` counts them for that reason. */

    if( !rs_cs2_hooks_survive_clear(hooks) )
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
/*
 * Copy a transmit hook's trigger filter into the registry, clamped to its
 * ceiling.
 *
 * The count and the ids are ONE fact, and the assert is the point of the
 * function. A request carrying a count with no ids is not "a hook with no
 * triggers" — the ids array is zeroed, so the hook reads as *filtered to id 0*
 * and everything else is dropped. That is exactly what an uninitialised
 * `trigger_count` in `cs2vm2_op_if_set_on_transmit` did: the XP-drop listener
 * matched stat 0 (attack) and nothing else, so combat drew drops and cooking,
 * prayer and the rest silently drew none. Left as a tolerated NULL, it took a
 * session to find; asserted, it stops at the frame that caused it.
 */
static void
rs_cs2_copy_transmit_triggers(
    int* out_ids,
    int* out_count,
    int const* trigger_ids,
    int trigger_count)
{
    assert(out_ids);
    assert(out_count);
    assert(trigger_count >= 0);
    assert(trigger_count == 0 || trigger_ids);

    if( trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    *out_count = trigger_count;
    if( trigger_count > 0 )
        memcpy(out_ids, trigger_ids, (size_t)trigger_count * sizeof(int));
}

static int
exec_set_on_stat_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostPayload_IF_SetOnVarTransmit const* request)
{
    struct RS_CS2StatTransmitHook* hook;

    assert(host);
    assert(request);
    hook = rs_cs2_acquire_stat_transmit_hook(host, request->component_id, request->script_id > 0);
    if( !hook )
        return CS2VM_EXECNO_OK;
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    rs_cs2_copy_transmit_triggers(
        hook->trigger_ids, &hook->trigger_count, request->trigger_ids, request->trigger_count);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_var_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostPayload_IF_SetOnVarTransmit const* request)
{
    struct RS_CS2VarTransmitHook* hook;
    assert(host);
    /* TORIRS_VAR_HOOK_DEBUG=1: what the VM actually asked for. A hook that is
     * never registered and a hook that is registered and then reclaimed look
     * the same in the dispatch trace; this is the other end of that pair. */
    if( getenv("TORIRS_VAR_HOOK_DEBUG") )
    {
        int t;
        fprintf(
            stderr,
            "VARHOOKSET com=0x%08x script=%d triggers=%d[",
            (unsigned)request->component_id,
            request->script_id,
            request->trigger_count);
        for( t = 0; t < request->trigger_count && t < 32; t++ )
            fprintf(stderr, "%s%d", t ? "," : "", request->trigger_ids[t]);
        fprintf(stderr, "]\n");
    }
    hook = rs_cs2_acquire_var_transmit_hook(host, request->component_id, request->script_id > 0);
    if( !hook )
        return CS2VM_EXECNO_OK;
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    rs_cs2_copy_transmit_triggers(
        hook->trigger_ids, &hook->trigger_count, request->trigger_ids, request->trigger_count);
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
    struct CS2VM_HostPayload_CC_SetOnOp const* request)
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
        hook = rs_cs2_acquire_inv_transmit_hook(host, component_id, request->script_id > 0);
        if( !hook )
        {
            /* Full, or a disarm of a component that had no hook — see
             * exec_set_on_inv_transmit. */
            if( request->script_id > 0 )
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
        hook = rs_cs2_acquire_var_transmit_hook(host, component_id, request->script_id > 0);
        if( !hook )
            return CS2VM_EXECNO_OK;
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        rs_cs2_copy_transmit_triggers(
            hook->trigger_ids, &hook->trigger_count, request->trigger_ids, request->trigger_count);
        return CS2VM_EXECNO_OK;
    }

    if( kind == CS2VM_HOST_REQUEST_CC_SETONSTATTRANSMIT )
    {
        struct RS_CS2StatTransmitHook* hook;
        hook = rs_cs2_acquire_stat_transmit_hook(host, component_id, request->script_id > 0);
        if( !hook )
            return CS2VM_EXECNO_OK;
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        rs_cs2_copy_transmit_triggers(
            hook->trigger_ids, &hook->trigger_count, request->trigger_ids, request->trigger_count);
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
    case CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT:
        return &hooks->on_click_repeat;
    case CS2VM_HOST_REQUEST_IF_SETONRELEASE:
    case CS2VM_HOST_REQUEST_CC_SETONRELEASE:
        return &hooks->on_release;
    case CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT:
    case CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT:
        return &hooks->on_dialog_abort;
    case CS2VM_HOST_REQUEST_IF_SETONTARGETENTER:
    case CS2VM_HOST_REQUEST_CC_SETONTARGETENTER:
        return &hooks->on_target_enter;
    case CS2VM_HOST_REQUEST_IF_SETONTARGETLEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE:
        return &hooks->on_target_leave;
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
    case CS2VM_HOST_REQUEST_IF_SETONITEMONITEM:
    case CS2VM_HOST_REQUEST_CC_SETONITEMONITEM:
        return &hooks->on_key_down;
    case CS2VM_HOST_REQUEST_IF_SETONCLANSETTINGS:
    case CS2VM_HOST_REQUEST_CC_SETONCLANSETTINGS:
        return &hooks->on_key_up;
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
    case CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT:
        return &hooks->on_friend_transmit;
    case CS2VM_HOST_REQUEST_IF_SETONCHATTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONCHATTRANSMIT:
        return &hooks->on_chat_transmit;
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
    case CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT:
        return "IF_SETONCLICKREPEAT";
    case CS2VM_HOST_REQUEST_IF_SETONRELEASE:
        return "IF_SETONRELEASE";
    case CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT:
        return "IF_SETONDIALOGABORT";
    case CS2VM_HOST_REQUEST_IF_SETONTARGETENTER:
        return "IF_SETONTARGETENTER";
    case CS2VM_HOST_REQUEST_IF_SETONTARGETLEAVE:
        return "IF_SETONTARGETLEAVE";
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return "IF_SETONKEY";
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return "CC_SETONKEY";
    case CS2VM_HOST_REQUEST_IF_SETONITEMONITEM:
        return "IF_SETONITEMONITEM";
    case CS2VM_HOST_REQUEST_CC_SETONITEMONITEM:
        return "CC_SETONITEMONITEM";
    case CS2VM_HOST_REQUEST_IF_SETONCLANSETTINGS:
        return "IF_SETONCLANSETTINGS";
    case CS2VM_HOST_REQUEST_CC_SETONCLANSETTINGS:
        return "CC_SETONCLANSETTINGS";
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return "CC_SETONCLICK";
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return "CC_SETONOP";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return "CC_SETONMOUSEOVER";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return "CC_SETONMOUSELEAVE";
    case CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT:
        return "CC_SETONCLICKREPEAT";
    case CS2VM_HOST_REQUEST_CC_SETONRELEASE:
        return "CC_SETONRELEASE";
    case CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT:
        return "CC_SETONDIALOGABORT";
    case CS2VM_HOST_REQUEST_CC_SETONTARGETENTER:
        return "CC_SETONTARGETENTER";
    case CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE:
        return "CC_SETONTARGETLEAVE";
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
    struct CS2VM_HostPayload_IF_SetOnOp const* request)
{
    struct UITree* tree;
    struct UITreeComponent* node;
    struct UITreeRuntimeScriptHook* slot;

    assert(host);
    assert(request);

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
    struct CS2VM_HostPayload_CC_SetOnOp const* request)
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
    struct CS2VM2_Thread* thread,
    int opcode,
    int load_kind,
    int load_id)
{
    struct CS2VM_HostRequest req = { 0 };
    req.kind = (enum CS2VM_HostRequestKind)opcode;
    req.u.db.opcode = opcode;
    req.u.db.load_kind = load_kind;
    req.u.db.load_id = load_id;
    return rs_cs2_yield_load(host, thread, &req, load_id, load_kind);
}

/* Resolve a DBROW, loading it once if needed. On the first miss this yields
 * (returns true via *yielded with the yield code in *out_code); on a second
 * miss after the load it returns NULL with *yielded false (genuinely absent). */
static struct RSCache_Dat2ConfigDbRow*
db_row_or_yield(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
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
    if( !rs_cs2_await_spent(
            thread, (enum CS2VM_HostRequestKind)opcode, row_id, CS2VM_DB_LOAD_ROW) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, thread, opcode, CS2VM_DB_LOAD_ROW, row_id);
    }
    return NULL;
}

static struct ToriRS_DbTableIndex*
db_index_or_yield(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
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
    if( !rs_cs2_await_spent(
            thread, (enum CS2VM_HostRequestKind)opcode, table_id, CS2VM_DB_LOAD_INDEX) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, thread, opcode, CS2VM_DB_LOAD_INDEX, table_id);
    }
    return NULL;
}

static struct RSCache_Dat2ConfigDbTable*
db_table_or_yield(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
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
    if( !rs_cs2_await_spent(
            thread, (enum CS2VM_HostRequestKind)opcode, table_id, CS2VM_DB_LOAD_TABLE) )
    {
        *yielded = true;
        *out_code = db_yield_load(host, thread, opcode, CS2VM_DB_LOAD_TABLE, table_id);
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
    assert(col);
    if( col->type_count <= 0 || !col->types )
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
        row = db_row_or_yield(host, vm, opcode, row_id, &yielded, &code);
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
        row = db_row_or_yield(host, vm, opcode, row_id, &yielded, &code);
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
        dbtable = row ? db_table_or_yield(host, vm, opcode, table, &yielded, &code) : NULL;
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
        row = db_row_or_yield(host, vm, opcode, row_id, &yielded, &code);
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
            dbtable =
                row ? db_table_or_yield(host, vm, opcode, table, &yielded, &code) : NULL;
            if( yielded )
                return code;
            col = db_column_of(row, dbtable, col_id);
            arity = col ? col->type_count : 0;
            /* No column at all in either the row or the table: there is no
             * arity to answer with, so it is a single -1. */
            if( !col )
                return CS2VM2_PushInt(vm, -1);
            if( arity <= 0 || index < 0 || index >= col->tuple_count || !col->values )
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
        idx = db_index_or_yield(host, vm, opcode, table_id, &yielded, &code);
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
        idx = db_index_or_yield(host, vm, opcode, table, &yielded, &code);
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
    struct CS2VM_HostPayload_Loot const* req)
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
    struct CS2VM_HostPayload_Hiscores const* req)
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
    struct CS2VM_HostPayload_Social const* req)
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

/* Push a string, through the pool's shared empty when there is nothing to say.
 * The history opcodes run once per chat line drawn and most of their strings
 * are empty (a server message has no sender), so this is the difference
 * between a rebuild that touches the pool three times per line and one that
 * does not touch it at all. Mirrors CHAT_PLAYERNAME below. */
static int
rs_cs2_push_text(
    struct CS2VM2_Thread* vm,
    char const* text)
{
    assert(vm);
    assert(text);
    if( text[0] )
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, text));
    return CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm));
}

static int
exec_chat(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostPayload_Chat const* req)
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

    case CS2_OP_CHAT_SENDPUBLIC:
    {
        struct RS_CS2SocialSend send;

        /* An empty line is not a message. The reference's own submit path
         * never calls this with one -- script 73 tests the typed string first
         * -- so this is the same "nothing to say" no-op the private send
         * above makes, not a guard against a caller bug. */
        if( !req->text || !req->text[0] )
            return CS2VM_EXECNO_OK;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_MESSAGE_PUBLIC;
        send.colour_effect = req->colour_effect;
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

    /* ---- history ------------------------------------------------------
     *
     * The chatbox's whole data source. `[proc,rebuildchatbox]` asks
     * `[proc,script553]` for the newest uid (sweeping every chat type with
     * GETHISTORYLENGTH + GETHISTORYEX_BYTYPEANDLINE), then walks backwards
     * with GETPREVUID, reading each node with GETHISTORYEX_BYUID and writing
     * one line component per message that passes its own filters.
     *
     * A NULL store is a run with no chatbox (a headless harness), and the
     * answers below are the same ones the reference gives for a message that
     * has fallen out of its ring: an empty history rather than a refusal. */
    case CS2_OP_CHAT_GETHISTORYLENGTH:
        return CS2VM2_PushInt(vm, host->chat ? RS_Chat_TypeCount(host->chat, req->type) : 0);

    case CS2_OP_CHAT_GETNEXTUID:
        return CS2VM2_PushInt(vm, host->chat ? RS_Chat_NextUid(host->chat, req->uid) : -1);

    case CS2_OP_CHAT_GETPREVUID:
        return CS2VM2_PushInt(vm, host->chat ? RS_Chat_PrevUid(host->chat, req->uid) : -1);

    case CS2_OP_CHAT_GETHISTORY_BYUID:
    case CS2_OP_CHAT_GETHISTORY_BYTYPEANDLINE:
    case CS2_OP_CHAT_GETHISTORYEX_BYUID:
    case CS2_OP_CHAT_GETHISTORYEX_BYTYPEANDLINE:
    {
        int const by_uid = req->opcode == CS2_OP_CHAT_GETHISTORY_BYUID ||
                           req->opcode == CS2_OP_CHAT_GETHISTORYEX_BYUID;
        int const extended = req->opcode == CS2_OP_CHAT_GETHISTORYEX_BYUID ||
                             req->opcode == CS2_OP_CHAT_GETHISTORYEX_BYTYPEANDLINE;
        struct RS_ChatNode const* node = NULL;

        if( host->chat )
            node = by_uid ? RS_Chat_NodeByUid(host->chat, req->uid)
                          : RS_Chat_NodeByTypeAndLine(host->chat, req->type, req->line);

        /*
         * Six values, or eight for the `ex` forms:
         *
         *   0  the OTHER handle -- the by-uid forms return the type, the
         *      by-type-and-line forms return the uid. They are the same
         *      tuple otherwise, and getting this one backwards puts a chat
         *      type where a script expects a message handle.
         *   1  clock          the client cycle the message arrived on
         *   2  name           printable sender, "" for a system line
         *   3  sender         the account friend/ignore is keyed on
         *   4  text           the message
         *   5  friend state   1 friend, 2 ignored, 0 neither  (ex only)
         *   6  ""             reserved at this revision       (ex only)
         *   7  0              reserved at this revision       (ex only)
         *
         * A missing message answers (-1, 0, "", "", "", 0, "", 0) rather than
         * failing: the walk reads a uid it was handed one call earlier, and a
         * message can fall out of its ring in between.
         */
        if( CS2VM2_PushInt(vm, node ? (by_uid ? node->type : node->uid) : -1) !=
            CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PushInt(vm, node ? node->clock : 0) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( rs_cs2_push_text(vm, node ? node->name : "") != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( rs_cs2_push_text(vm, node ? node->sender : "") != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( rs_cs2_push_text(vm, node ? node->text : "") != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( !extended )
            return CS2VM_EXECNO_OK;
        if( CS2VM2_PushInt(vm, node ? RS_Chat_NodeFriendState(node, host->social) : 0) !=
            CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( CS2VM2_PushStr(vm, CS2VM2_StrEmpty(vm)) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        return CS2VM2_PushInt(vm, 0);
    }

    /* ---- client-side chat settings ------------------------------------ */

    case CS2_OP_CHAT_SETMESSAGEFILTER:
        /* The public-chat search box above the tabs. Client state: the
         * chatbox script reads it back and drops lines that do not match. */
        if( host->chat )
        {
            snprintf(
                host->chat->message_filter,
                sizeof(host->chat->message_filter),
                "%s",
                req->text ? req->text : "");
            /* A filter change re-selects which lines are visible, so the
             * scrollback has to be rebuilt -- the same channel a new message
             * uses, because it is the same redraw. */
            host->chat_transmit_dirty = 1;
        }
        return CS2VM_EXECNO_OK;

    case CS2_OP_CHAT_GETMESSAGEFILTER:
        return rs_cs2_push_text(vm, host->chat ? host->chat->message_filter : "");

    case CS2_OP_CHAT_SETTIMESTAMPS:
        if( host->chat )
        {
            host->chat->timestamps = req->timestamps;
            host->chat_transmit_dirty = 1;
        }
        return CS2VM_EXECNO_OK;

    case CS2_OP_CHAT_GETTIMESTAMPS:
        return CS2VM2_PushInt(vm, host->chat ? host->chat->timestamps : 0);

    case CS2_OP_CHAT_SENDCLAN:
        /* No clan channel at this client. The arguments are popped by the VM
         * so the stack stays balanced; there is nowhere to send them. */
        return CS2VM_EXECNO_OK;

    case CS2_OP_MES:
        /* A script's own line, on the game channel -- `mes` is how the
         * cache's scripts talk to the player, and it lands in the same store
         * as a server message and wakes the same rebuild. */
        RS_CS2Host_ChatAdd(host, RS_CHAT_TYPE_GAME, NULL, NULL, req->text ? req->text : "");
        return CS2VM_EXECNO_OK;

    case CS2_OP_STAFFMODLEVEL:
        /* No staff accounts here: the chatbox asks before offering the moderator
         * options on a line, and 0 is "an ordinary player". */
        return CS2VM2_PushInt(vm, 0);

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
    /* Depth-guarded because a handler may re-enter the VM and reach this
     * function again (CC_CALLONOP and the trigger dispatchers do), and a stage
     * that begins twice before it ends bills the inner span to the outer one.
     * Only the outermost dispatch is timed, so the stage total stays the
     * wall-clock time `cs2` spends inside host work rather than a sum that can
     * exceed its own parent. Not thread-local on purpose: the VM is pumped from
     * one thread and this is measurement scaffolding, not tree state. */
    {
        static int depth = 0;
        int timed = (depth++ == 0);
        if( timed )
            TORIRS_PERF_STAGE_BEGIN(TORIRS_PERF_STAGE_CS2_HOST_OP);
        result = rs_cs2_host_exec_dispatch(vm, request);
        if( timed )
            TORIRS_PERF_STAGE_END(TORIRS_PERF_STAGE_CS2_HOST_OP);
        depth--;
    }
    /* The await record only spans the yield -> load -> retry window: a request
     * that completes retires it, so a resource evicted later can be awaited
     * again. */
    if( result != CS2VM_EXECNO_YIELD )
        vm->has_awaited = false;
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
    case CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS:
        return exec_push_script(host, vm, RS_CS2_PAYLOAD(request, PushScript)->script_id);

    case CS2VM_HOST_REQUEST_INV_SIZE:
        return exec_inv_size(host, vm, RS_CS2_PAYLOAD(request, InvSize)->inv_id);

    case CS2VM_HOST_REQUEST_INV_GETOBJ:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_obj(host, RS_CS2_PAYLOAD(request, InvGetObj)->inv_id, RS_CS2_PAYLOAD(request, InvGetObj)->slot));

    case CS2VM_HOST_REQUEST_INV_GETNUM:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_num(host, RS_CS2_PAYLOAD(request, InvGetNum)->inv_id, RS_CS2_PAYLOAD(request, InvGetNum)->slot));

    case CS2VM_HOST_REQUEST_INV_TOTAL:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_total(
                host, RS_CS2_PAYLOAD(request, InvTotal)->inv_id, RS_CS2_PAYLOAD(request, InvTotal)->item_id));

    case CS2VM_HOST_REQUEST_PUSH_VAR:
        return exec_vars_read_varp(host, vm, RS_CS2_PAYLOAD(request, VarsReadVarp)->varp_id);

    case CS2VM_HOST_REQUEST_PUSH_VARBIT:
        return exec_vars_read_varbit(host, vm, RS_CS2_PAYLOAD(request, VarsReadVarbit)->varbit_id);

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
     * A script write DOES feed the var-transmit ring, but only when the value
     * actually moved. The reference depends on this: the settings mute icon
     * (script 9255) writes %var3796 and then re-syncs only the icon itself
     * (script 9254) — the four slider bobbles are re-coloured by script 7101
     * hanging off var3796's transmit hook, and nothing else ever calls it on
     * that path. The drag handlers (9232/9238/9244/9250) call ~script9256
     * explicitly precisely because they are the case that does not rely on the
     * hook. Suppress the notification and the bobbles keep whatever colour they
     * were built with, which reads as "the sliders are stuck grey".
     *
     * The change gate is what keeps this non-recursive. An earlier attempt
     * wired the ring to the unconditional ChangeFn and had rev230's gameframe
     * rebuilding its popout strip every few frames: a hook that re-asserts the
     * var it watches re-triggered itself forever. VarPManager_SetVarpOptimistic
     * early-returns on an equal write, so a hook that writes the same value
     * back announces nothing and the cascade stops on its own. Only a genuine
     * new value re-dispatches. */
    case CS2VM_HOST_REQUEST_POP_VAR:
        RS_CS2Host_ScriptWriteVarp(
            host, RS_CS2_PAYLOAD(request, VarsWriteVarp)->varp_id, RS_CS2_PAYLOAD(request, VarsWriteVarp)->value);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_POP_VARBIT:
        rs_cs2_settings_record_action(
            host, vm, RS_CS2_PAYLOAD(request, VarsWriteVarbit)->varbit_id,
            RS_CS2_PAYLOAD(request, VarsWriteVarbit)->value);
        rs_cs2_settings_record_mirror(
            host, vm, RS_CS2_PAYLOAD(request, VarsWriteVarbit)->varbit_id,
            RS_CS2_PAYLOAD(request, VarsWriteVarbit)->value);
        rs_cs2_settings_apply_client_layout(
            host, vm, RS_CS2_PAYLOAD(request, VarsWriteVarbit)->varbit_id,
            RS_CS2_PAYLOAD(request, VarsWriteVarbit)->value);
        RS_CS2Host_ScriptWriteVarbit(
            host, RS_CS2_PAYLOAD(request, VarsWriteVarbit)->varbit_id, RS_CS2_PAYLOAD(request, VarsWriteVarbit)->value);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_PUSH_VARC_INT:
    {
        int id = RS_CS2_PAYLOAD(request, VarsReadVarcInt)->varc_id;
        int value = host->varcs ? VarCManager_GetInt(host->varcs, id) : -1;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_KEYHELD:
    case CS2VM_HOST_REQUEST_KEYPRESSED:
    {
        int key_code = RS_CS2_PAYLOAD(request, KeyQuery)->key_code;
        unsigned char const* state = request->kind == CS2VM_HOST_REQUEST_KEYHELD
                                         ? host->osrs_key_held
                                         : host->osrs_key_pressed;
        int value = 0;
        if( key_code >= 0 && key_code < TORIRS_OSRSKEY_COUNT )
            value = state[key_code] ? 1 : 0;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_PUSH_VARC_STRING_OLD:
    case CS2VM_HOST_REQUEST_PUSH_VARC_STRING:
    {
        int id = RS_CS2_PAYLOAD(request, VarsReadVarcString)->varc_id;
        char const* value = host->varcs ? VarCManager_GetString(host->varcs, id) : "";
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, value));
    }

    case CS2VM_HOST_REQUEST_POP_VARC_INT:
    {
        int id = RS_CS2_PAYLOAD(request, VarsWriteVarcInt)->varc_id;
        /*
         * A varc write notifies nothing, and that is the reference's design
         * rather than a gap here.
         *
         * This used to claim the manager fires `RS_CS2Host_NotifyVarChanged` on
         * a real change. It does not — nothing calls
         * `VarCManager_SetChangeCallback` outside its own unit test — and the
         * claim is worth correcting rather than deleting, because it reads as a
         * varc-driven transmit channel that a reader will then go looking for.
         *
         * There is no such channel at this revision. A rev-239 widget record
         * carries exactly three transmit hooks and three trigger arrays — varp,
         * inv, stat (deob `class308`: 18 `method7268` hook reads then
         * `field4024`/`field4041`/`field4137`) — and the CS2 setters stop at
         * 1407/1414/1415 with no varc opcode between them. The client's
         * changed-id ring (`class414`) is fed only from the VARP_SMALL /
         * VARP_LARGE handlers, so a varc never reaches a widget hook at all: a
         * script that writes a varc repaints whatever depends on it itself.
         *
         * `onVarcTransmit` exists only in the older IF3 layout — rscache reads
         * it in `decode_if3_rs2`, gated on `rev_is_643` — so it is a rev-643
         * question, not a 239 one. See app.c's `app_varp_server_update` header
         * for why feeding script-side writes into the ring is also wrong.
         */
        if( host->varcs )
            VarCManager_SetInt(host->varcs, id, RS_CS2_PAYLOAD(request, VarsWriteVarcInt)->value);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_POP_VARC_STRING_OLD:
    case CS2VM_HOST_REQUEST_POP_VARC_STRING:
    {
        int id = RS_CS2_PAYLOAD(request, VarsWriteVarcString)->varc_id;
        if( host->varcs )
            VarCManager_SetString(host->varcs, id, RS_CS2_PAYLOAD(request, VarsWriteVarcString)->value);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_ENUM_STRING:
    case CS2VM_HOST_REQUEST_ENUM:
        return exec_enum_lookup(host, vm, *RS_CS2_PAYLOAD(request, EnumLookup), request->kind);

    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return exec_enum_output_count(host, vm, *RS_CS2_PAYLOAD(request, EnumGetOutputCount));

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return exec_struct_param(host, vm, *RS_CS2_PAYLOAD(request, StructParam), request->kind);

    case CS2VM_HOST_REQUEST_CC_GETPARAM:
        return exec_struct_param(host, vm, *RS_CS2_PAYLOAD(request, StructParam), request->kind);

    case CS2VM_HOST_REQUEST_OC_COST:
    case CS2VM_HOST_REQUEST_OC_STACKABLE:
    case CS2VM_HOST_REQUEST_OC_CERT:
    case CS2VM_HOST_REQUEST_OC_UNCERT:
    case CS2VM_HOST_REQUEST_OC_MEMBERS:
        return exec_oc_int_param(host, vm, *RS_CS2_PAYLOAD(request, OC_IntParam), request->kind);

    case CS2VM_HOST_REQUEST_OC_NAME:
        return exec_oc_name(host, vm, *RS_CS2_PAYLOAD(request, OC_Name));

    case CS2VM_HOST_REQUEST_NC_NAME:
        return exec_nc_name(host, vm, *RS_CS2_PAYLOAD(request, NC_Name));

    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        return exec_oc_unplaceholder(host, vm, *RS_CS2_PAYLOAD(request, OC_Unplaceholder));

    case CS2VM_HOST_REQUEST_OC_OP:
        return exec_oc_op(host, vm, *RS_CS2_PAYLOAD(request, OC_Op));

    case CS2VM_HOST_REQUEST_OC_IOP:
        return exec_oc_op(host, vm, *RS_CS2_PAYLOAD(request, OC_Op));

    case CS2VM_HOST_REQUEST_OC_EXAMINE:
        return exec_oc_examine(host, vm, *RS_CS2_PAYLOAD(request, OC_Name));

    case CS2VM_HOST_REQUEST_OC_PLACEHOLDER:
        return exec_oc_placeholder(host, vm, *RS_CS2_PAYLOAD(request, OC_Unplaceholder));

    case CS2VM_HOST_REQUEST_OC_FIND:
    case CS2VM_HOST_REQUEST_OC_FINDNEXT:
    case CS2VM_HOST_REQUEST_OC_FINDRESET:
        return exec_oc_find(host, vm, *RS_CS2_PAYLOAD(request, OC_Find), request->kind);

    case CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP:
        return exec_oc_shiftclickiop(host, vm, *RS_CS2_PAYLOAD(request, OC_Name));

    case CS2VM_HOST_REQUEST_OC_WEARPOS:
    case CS2VM_HOST_REQUEST_OC_WEARPOS2:
    case CS2VM_HOST_REQUEST_OC_WEARPOS3:
        return exec_oc_wearpos(vm, *RS_CS2_PAYLOAD(request, OC_WearPos));

    case CS2VM_HOST_REQUEST_OC_WEIGHT:
        return exec_oc_weight(vm, *RS_CS2_PAYLOAD(request, OC_Name));

    case CS2VM_HOST_REQUEST_OC_ISUBOP:
        return exec_oc_isubop(vm, *RS_CS2_PAYLOAD(request, OC_Isubop));

    case CS2VM_HOST_REQUEST_OC_PARAM:
        return exec_oc_param(host, vm, *RS_CS2_PAYLOAD(request, OC_Param));

    case CS2VM_HOST_REQUEST_NC_PARAM:
        return exec_type_param(host, vm, *RS_CS2_PAYLOAD(request, TypeParam), true);
    case CS2VM_HOST_REQUEST_LC_PARAM:
        return exec_type_param(host, vm, *RS_CS2_PAYLOAD(request, TypeParam), false);

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return exec_para_height(host, vm, *RS_CS2_PAYLOAD(request, ParaHeight), 0);

    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return exec_para_height(host, vm, *RS_CS2_PAYLOAD(request, ParaHeight), 1);

    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return CS2VM2_PushInt(vm, host->client_clock);

    case CS2VM_HOST_REQUEST_COORD:
        /* MINUS ONE when there is no local player, which is the reference's own
         * answer: `Statics.method9635` returns null (or its validity check
         * fails) and opcode 3308 pushes -1 rather than a tile. Zero is a real
         * tile in the corner of the map and a script comparing against a box
         * cannot tell it from a position. */
        return CS2VM2_PushInt(vm, host->local_coord);

    case CS2VM_HOST_REQUEST__3330:
        return CS2VM2_PushInt(vm, host->dest_coord);

    case CS2VM_HOST_REQUEST_STAT:
    case CS2VM_HOST_REQUEST_STAT_BASE:
    case CS2VM_HOST_REQUEST_STAT_XP:
    {
        int stat = RS_CS2_PAYLOAD(request, Stat)->stat;
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

    case CS2VM_HOST_REQUEST_FRIEND_COUNT:
    case CS2VM_HOST_REQUEST_FRIEND_GETNAME:
    case CS2VM_HOST_REQUEST_FRIEND_GETWORLD:
    case CS2VM_HOST_REQUEST_FRIEND_GETRANK:
    case CS2VM_HOST_REQUEST_FRIEND_ADD:
    case CS2VM_HOST_REQUEST_FRIEND_DEL:
    case CS2VM_HOST_REQUEST_IGNORE_ADD:
    case CS2VM_HOST_REQUEST_IGNORE_DEL:
    case CS2VM_HOST_REQUEST_FRIEND_TEST:
    case CS2VM_HOST_REQUEST_IGNORE_COUNT:
    case CS2VM_HOST_REQUEST_IGNORE_GETNAME:
    case CS2VM_HOST_REQUEST_IGNORE_TEST:
        return exec_social(host, vm, RS_CS2_PAYLOAD(request, Social));

    case CS2VM_HOST_REQUEST_LOOT_AUX_UPSERT2:
    case CS2VM_HOST_REQUEST_LOOT_AUX_UPSERT:
    case CS2VM_HOST_REQUEST_LOOT_AUX_REMOVE:
    case CS2VM_HOST_REQUEST_LOOT_AUX_GET:
    case CS2VM_HOST_REQUEST_LOOT_AUX_COUNT:
    case CS2VM_HOST_REQUEST_LOOT_AUX_LOOKUP:
    case CS2VM_HOST_REQUEST_LOOT_AUX_CLEAR:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_COUNT:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_NAME:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_ITEMCOUNT:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_TOTALVAL:
    case CS2VM_HOST_REQUEST_LOOT_BEGIN_QUERY:
    case CS2VM_HOST_REQUEST_LOOT_QUERY_ID:
    case CS2VM_HOST_REQUEST_LOOT_AUX_COUNT_TOTAL:
    case CS2VM_HOST_REQUEST_LOOT_ROW_COUNT_BYNAME:
    case CS2VM_HOST_REQUEST_LOOT_ROW_COUNT_BYID:
    case CS2VM_HOST_REQUEST_LOOT_ROW_BYNAME:
    case CS2VM_HOST_REQUEST_LOOT_ROW_BYID:
    case CS2VM_HOST_REQUEST_LOOT_CLEAR_ALL:
    case CS2VM_HOST_REQUEST_LOOT_CLEAR_SOURCE:
    case CS2VM_HOST_REQUEST_LOOT_REMOVE_BYID:
    case CS2VM_HOST_REQUEST_LOOT_IGNORE_ADD:
    case CS2VM_HOST_REQUEST_LOOT_IGNORE_REMOVE:
    case CS2VM_HOST_REQUEST_LOOT_GROUND_COUNT:
    case CS2VM_HOST_REQUEST_LOOT_GROUND_NAME:
    case CS2VM_HOST_REQUEST_LOOT_IGNORE_CLEAR:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_IGNORE_ADD:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_IGNORE_REMOVE:
    case CS2VM_HOST_REQUEST_LOOT_SRCLIST_COUNT:
    case CS2VM_HOST_REQUEST_LOOT_SRCLIST_NAME:
    case CS2VM_HOST_REQUEST_LOOT_ADD:
    case CS2VM_HOST_REQUEST_LOOT_SOURCE_NAME2:
        return exec_loot(host, vm, RS_CS2_PAYLOAD(request, Loot));

    case CS2VM_HOST_REQUEST_HISCORES_STATUS:
    case CS2VM_HOST_REQUEST_HISCORES_ERROR:
        return exec_hiscores(host, vm, RS_CS2_PAYLOAD(request, Hiscores));

    case CS2VM_HOST_REQUEST_CHAT_GETFILTER_PUBLIC:
    case CS2VM_HOST_REQUEST_CHAT_SETFILTER:
    case CS2VM_HOST_REQUEST_CHAT_GETHISTORY_BYTYPEANDLINE:
    case CS2VM_HOST_REQUEST_CHAT_GETHISTORY_BYUID:
    case CS2VM_HOST_REQUEST_CHAT_GETFILTER_PRIVATE:
    case CS2VM_HOST_REQUEST_CHAT_SENDPUBLIC:
    case CS2VM_HOST_REQUEST_CHAT_SENDPRIVATE:
    case CS2VM_HOST_REQUEST_CHAT_SENDCLAN:
    case CS2VM_HOST_REQUEST_CHAT_PLAYERNAME:
    case CS2VM_HOST_REQUEST_CHAT_GETFILTER_TRADE:
    case CS2VM_HOST_REQUEST_CHAT_GETHISTORYLENGTH:
    case CS2VM_HOST_REQUEST_CHAT_GETNEXTUID:
    case CS2VM_HOST_REQUEST_CHAT_GETPREVUID:
    case CS2VM_HOST_REQUEST_DOCHEAT:
    case CS2VM_HOST_REQUEST_CHAT_SETMESSAGEFILTER:
    case CS2VM_HOST_REQUEST_CHAT_GETMESSAGEFILTER:
    case CS2VM_HOST_REQUEST_CHAT_SETTIMESTAMPS:
    case CS2VM_HOST_REQUEST_CHAT_GETTIMESTAMPS:
    case CS2VM_HOST_REQUEST_CHAT_GETHISTORYEX_BYTYPEANDLINE:
    case CS2VM_HOST_REQUEST_CHAT_GETHISTORYEX_BYUID:
    case CS2VM_HOST_REQUEST_MES:
    case CS2VM_HOST_REQUEST_STAFFMODLEVEL:
        return exec_chat(host, vm, RS_CS2_PAYLOAD(request, Chat));

    case CS2VM_HOST_REQUEST_MAP_WORLD:
        /* Non-zero, or the friends panel headers read "World 0" and every
         * online friend draws yellow (script 125 compares each friend's world
         * against this to pick the green same-world colour). */
        return CS2VM2_PushInt(vm, host->map_world);

    case CS2VM_HOST_REQUEST_RUNENERGY_VISIBLE:
        return CS2VM2_PushInt(vm, host->stats ? host->stats->run_energy : 0);

    case CS2VM_HOST_REQUEST_RUNWEIGHT_VISIBLE:
        return CS2VM2_PushInt(vm, host->stats ? host->stats->run_weight : 0);

    case CS2VM_HOST_REQUEST_MOUSE_GETX:
        return CS2VM2_PushInt(vm, host->mouse_x);

    case CS2VM_HOST_REQUEST_MOUSE_GETY:
        return CS2VM2_PushInt(vm, host->mouse_y);

    case CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT:
        host->cam_follow_height = RS_CS2_PAYLOAD(request, CamSetFollowHeight)->height;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT:
        return CS2VM2_PushInt(vm, host->cam_follow_height);

    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPC_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPC_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPC_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPC_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPC_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPCTYPE_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPCTYPE_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPCTYPE_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPCTYPE_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_NPCTYPE_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOC_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOC_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOC_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOC_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOC_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOCTYPE_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOCTYPE_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOCTYPE_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOCTYPE_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_LOCTYPE_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJ_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJ_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJ_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJ_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJ_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJTYPE_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJTYPE_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJTYPE_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJTYPE_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_OBJTYPE_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_PLAYER_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_PLAYER_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_PLAYER_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_PLAYER_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_PLAYER_CLEAR:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_TILE_SETUP:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_TILE_ON:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_TILE_OFF:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_TILE_GET:
    case CS2VM_HOST_REQUEST_HIGHLIGHT_TILE_CLEAR:
    case CS2VM_HOST_REQUEST__7040:
    case CS2VM_HOST_REQUEST__7041:
    case CS2VM_HOST_REQUEST__7042:
    case CS2VM_HOST_REQUEST__7043:
    case CS2VM_HOST_REQUEST__7044:
        /*
         * HIGHLIGHT_* (7000..7044): recorded, and drawn by the `nxt-highlight`
         * builtin through the plugin layer. See rs_highlight.h for what the
         * family is and why the cache is the one driving it.
         *
         * A GET is answered from the recorded state and is therefore true --
         * it used to answer 0 unconditionally, which WAS true while nothing
         * ever joined a group and became a lie the moment something did.
         */
        {
            int answer = 0;
            bool handled;
            bool const debug = getenv("TORIRS_HIGHLIGHT_DEBUG") != NULL;
            enum RS_HighlightKind kind;
            bool const known = RS_HighlightOpcodeKind(RS_CS2_PAYLOAD(request, Highlight)->opcode, &kind);

            /* Every call, on request. The family is written entirely by cache
             * scripts, so when a highlight does not appear the first question
             * is always "did the script ask for it" -- and that question has
             * no other way to be answered from outside the VM. */
            if( debug )
            {
                fprintf(
                    stderr,
                    "highlight: op %d (%s)",
                    RS_CS2_PAYLOAD(request, Highlight)->opcode,
                    known ? RS_HighlightKindName(kind) : "?");
                for( int i = 0; i < RS_CS2_PAYLOAD(request, Highlight)->arg_count; i++ )
                    fprintf(stderr, " %d", RS_CS2_PAYLOAD(request, Highlight)->args[i]);
                if( RS_CS2_PAYLOAD(request, Highlight)->name )
                    fprintf(stderr, " '%s'", RS_CS2_PAYLOAD(request, Highlight)->name);
            }

            handled = RS_HighlightApply(
                &host->highlight,
                RS_CS2_PAYLOAD(request, Highlight)->opcode,
                RS_CS2_PAYLOAD(request, Highlight)->args,
                RS_CS2_PAYLOAD(request, Highlight)->arg_count,
                RS_CS2_PAYLOAD(request, Highlight)->name,
                &answer);

            /* The kind's subject count AFTER the op, on the same line.
             *
             * The op trace alone says what was asked for and not what the
             * state became, and the difference between the two is the whole of
             * one failure mode: a group that is re-ADDED to on every edge and
             * never emptied looks identical, op for op, to one that is. It
             * shows up here as a count that only ever climbs. */
            if( debug )
            {
                /* The PLAYER kind counts its own list -- its subjects are
                 * names, and reporting the int-keyed count for it would print
                 * a flat 0 while names were going in and out. */
                if( known )
                    fprintf(
                        stderr,
                        " -> %d %s",
                        kind == RS_HIGHLIGHT_PLAYER ? host->highlight.named_count
                                                    : host->highlight.member_count[kind],
                        RS_HighlightKindName(kind));
                fprintf(stderr, "\n");
            }

            if( !handled )
            {
                /* The one block this cannot own: the unnamed 7041..7043, whose
                 * string subject nothing in the cache ever names -- see the
                 * table in rs_highlight.c. Said once, because a highlight
                 * nobody can key is a real gap rather than a quiet one. */
                static bool announced = false;
                if( !announced )
                {
                    announced = true;
                    fprintf(
                        stderr,
                        "cs2: HIGHLIGHT opcode %d is not recorded -- nothing in this "
                        "cache names a subject for its family\n",
                        RS_CS2_PAYLOAD(request, Highlight)->opcode);
                }
            }
            if( RS_CS2_PAYLOAD(request, Highlight)->query )
                return CS2VM2_PushInt(vm, answer);
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CLIENTOP_NPC_SET:
    case CS2VM_HOST_REQUEST_CLIENTOP_NPC_DEL:
    case CS2VM_HOST_REQUEST_CLIENTOP_LOC_SET:
    case CS2VM_HOST_REQUEST_CLIENTOP_LOC_DEL:
    case CS2VM_HOST_REQUEST_CLIENTOP_OBJ_SET:
    case CS2VM_HOST_REQUEST_CLIENTOP_OBJ_DEL:
    case CS2VM_HOST_REQUEST_CLIENTOP_PLAYER_SET:
    case CS2VM_HOST_REQUEST_CLIENTOP_PLAYER_DEL:
    case CS2VM_HOST_REQUEST_CLIENTOP_TILE_SET:
    case CS2VM_HOST_REQUEST_CLIENTOP_TILE_DEL:
        /*
         * CLIENTOP_* (6700..6709): a right-click row the cache owns.
         *
         * `label` is borrowed from the VM's string pool, so RS_ClientOpApply
         * copies it -- the pool entry is freed when the frame unwinds, and a
         * menu row holding the pointer would draw whatever landed there next.
         */
        if( getenv("TORIRS_CLIENTOP_DEBUG") )
            fprintf(
                stderr,
                "clientop: op %d %s slot %d script %d '%s'\n",
                RS_CS2_PAYLOAD(request, ClientOp)->opcode,
                RS_CS2_PAYLOAD(request, ClientOp)->is_set ? "set" : "del",
                RS_CS2_PAYLOAD(request, ClientOp)->slot,
                RS_CS2_PAYLOAD(request, ClientOp)->script_id,
                RS_CS2_PAYLOAD(request, ClientOp)->label ? RS_CS2_PAYLOAD(request, ClientOp)->label : "");
        if( !RS_ClientOpApply(
                &host->clientop,
                RS_CS2_PAYLOAD(request, ClientOp)->opcode,
                RS_CS2_PAYLOAD(request, ClientOp)->is_set,
                RS_CS2_PAYLOAD(request, ClientOp)->slot,
                RS_CS2_PAYLOAD(request, ClientOp)->label,
                RS_CS2_PAYLOAD(request, ClientOp)->script_id) )
            fprintf(
                stderr,
                "cs2: CLIENTOP opcode %d is not in the 6700..6709 family\n",
                RS_CS2_PAYLOAD(request, ClientOp)->opcode);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST__6750:
    case CS2VM_HOST_REQUEST__6751:
    case CS2VM_HOST_REQUEST__6752:
    case CS2VM_HOST_REQUEST__6753:
    case CS2VM_HOST_REQUEST__6800:
    case CS2VM_HOST_REQUEST__6801:
    case CS2VM_HOST_REQUEST__6802:
    case CS2VM_HOST_REQUEST__6850:
    case CS2VM_HOST_REQUEST__6851:
    case CS2VM_HOST_REQUEST__6852:
    case CS2VM_HOST_REQUEST__6853:
    case CS2VM_HOST_REQUEST__6900:
    case CS2VM_HOST_REQUEST__6950:
    {
        /*
         * The subject of the client op being dispatched. Outside a dispatch
         * these answer -1 / "", which is what "no client op is running" has to
         * look like: answering with the last one would silently act on
         * whatever was clicked before.
         */
        int value = -1;
        char const* text = NULL;
        int running = -1;

        /*
         * The ROOT frame's script, not the innermost one: a client op's script
         * may call a proc, and the proc reading `_6950` on its behalf is still
         * that op's dispatch. Frame 0 is the script the task started with.
         */
        if( vm && vm->frame_sp > 0 && vm->frames[0] && vm->frames[0]->script )
            running = vm->frames[0]->script->script_id;

        if( !RS_ClientOpContextRead(
                &host->clientop, RS_CS2_PAYLOAD(request, ClientOpContext)->opcode, running, &value, &text) )
        {
            fprintf(
                stderr,
                "cs2: opcode %d is not a client-op context getter\n",
                RS_CS2_PAYLOAD(request, ClientOpContext)->opcode);
            return CS2VM_EXECNO_ERROR;
        }

        /*
         * `_6950` is the current TILE TARGET, not only a client op's subject.
         *
         * Clientscript 5197 ("Highlight hovered tile") reads it with no client
         * op anywhere in sight -- it is re-run whenever the tile under the
         * pointer changes, and this is how it learns which tile that is. So
         * outside a dispatch it falls back to the mouseover rather than
         * answering "nothing", which is what it means when a client op IS
         * running and the subject is that op's.
         *
         * Only the tile getter has a standing answer like this. An npc uid or
         * a loc type outside a dispatch is genuinely nothing: no script reads
         * one that way, and inventing a mouseover subject for them would put
         * whatever the pointer grazed into a highlight group.
         */
        if( RS_CS2_PAYLOAD(request, ClientOpContext)->opcode == CS2_OP__6950 && value < 0 )
            value = host->hover_coord;
        /* A non-NULL `text` is what says this opcode is string-valued -- the
         * reader knows which stack each one belongs on and says so, rather than
         * the two sides both deciding and eventually disagreeing. */
        if( text )
            return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, text));
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST__6901:
    case CS2VM_HOST_REQUEST__6902:
    case CS2VM_HOST_REQUEST__6903:
    case CS2VM_HOST_REQUEST__6904:
    case CS2VM_HOST_REQUEST__6905:
    {
        /*
         * The ACTIVE PLAYER's route (`_6902` / `_6903`) and the two uids
         * (`_6904` / `_6905`).
         *
         * Who the active player is comes from the same register the client-op
         * context getters read -- a dispatch, else what an opcode named, else
         * the mouseover -- which is this port's spelling of the reference's
         * `ScriptRunner::m_activePlayer`. The client sets it before firing a
         * per-player trigger, exactly as the reference's SetActivePlayer does.
         *
         * The reference asserts `m_activePlayer != -1` on all but `_6905`.
         * Here a missing active player answers 0 steps and -1 coords instead:
         * the value comes from a cache script's context and not from a caller
         * of this function, which is the same reason a highlight group id out
         * of range is refused rather than fatal (see rs_highlight.c). Every
         * consumer reads "no route" as standing still, which is the truthful
         * reading of "no player is active".
         */
        int const opcode = RS_CS2_PAYLOAD(request, ActivePlayer)->opcode;
        int running = -1;
        struct RS_ClientOpContext const* subject;
        int uid = -1;
        int answer = -1;

        if( vm && vm->frame_sp > 0 && vm->frames[0] && vm->frames[0]->script )
            running = vm->frames[0]->script->script_id;
        subject = RS_ClientOpSubject(&host->clientop, RS_CLIENTOP_PLAYER, running);
        if( subject )
            uid = subject->uid;

        switch( opcode )
        {
        case CS2_OP__6901:
        {
            /*
             * Make the LOCAL player the active one, and say whether there was
             * one to make active.
             *
             * The reference pushes 1 on success and -1 -- not 0 -- when the
             * local player index is out of range or its slot is empty, which
             * is the state before login. Nothing in this cache calls it; it is
             * here because it is the only WRITE in the block and the four
             * getters beside it are useless to a script that was not entered
             * from a per-player trigger without it.
             */
            struct RS_ClientOpContext ctx;
            if( host->local_pid < 0 )
            {
                answer = -1;
                break;
            }
            memset(&ctx, 0, sizeof(ctx));
            ctx.kind = RS_CLIENTOP_PLAYER;
            ctx.script_id = -1;
            ctx.uid = host->local_pid;
            ctx.type = -1;
            ctx.layer = -1;
            ctx.coord = host->local_coord;
            RS_ClientOpActiveSet(&host->clientop, RS_CLIENTOP_PLAYER, &ctx);
            answer = 1;
            break;
        }
        case CS2_OP__6904:
            answer = uid;
            break;
        case CS2_OP__6905:
            answer = host->local_pid;
            break;
        case CS2_OP__6902:
        case CS2_OP__6903:
        {
            int coord = -1;
            int length = -1;

            if( uid >= 0 && host->player_route )
                length = host->player_route(
                    host->world_user, uid, RS_CS2_PAYLOAD(request, ActivePlayer)->index, &coord);
            /* A uid the world has no player for is a player who has left the
             * scene since the register was written. No route, not a fatal. */
            if( length < 0 )
                length = 0;
            answer = opcode == CS2_OP__6902 ? length : coord;
            break;
        }
        default:
            fprintf(stderr, "cs2: opcode %d is not an active-player getter\n", opcode);
            return CS2VM_EXECNO_ERROR;
        }
        /* Same switch as the highlight trace, and for the same reason: when
         * the current-tile marker sits on the wrong tile, the first question
         * is whether 5203 took the ROUTE branch or fell back to `coord`, and
         * that is decided entirely by what these four answered. */
        if( getenv("TORIRS_HIGHLIGHT_DEBUG") )
            fprintf(
                stderr,
                "activeplayer: op %d (uid %d, index %d) -> %d\n",
                opcode,
                uid,
                RS_CS2_PAYLOAD(request, ActivePlayer)->index,
                answer);
        return CS2VM2_PushInt(vm, answer);
    }

    case CS2VM_HOST_REQUEST_DB_FIND_WITH_COUNT:
    case CS2VM_HOST_REQUEST_DB_FINDNEXT:
    case CS2VM_HOST_REQUEST_DB_GETFIELD:
    case CS2VM_HOST_REQUEST_DB_GETFIELDCOUNT:
    case CS2VM_HOST_REQUEST_DB_FINDALL_WITH_COUNT:
    case CS2VM_HOST_REQUEST_DB_GETROWTABLE:
    case CS2VM_HOST_REQUEST_DB_GETROW:
    case CS2VM_HOST_REQUEST_DB_FIND_FILTER_WITH_COUNT:
    case CS2VM_HOST_REQUEST_DB_FIND:
    case CS2VM_HOST_REQUEST_DB_FINDALL:
    case CS2VM_HOST_REQUEST_DB_FIND_FILTER:
        return exec_db(host, vm, RS_CS2_PAYLOAD(request, Db)->opcode);

    case CS2VM_HOST_REQUEST_MINIMENU_TYPE:
    case CS2VM_HOST_REQUEST_MINIMENU_ENTRY:
    case CS2VM_HOST_REQUEST_MINIMENU_FINDNPC:
    case CS2VM_HOST_REQUEST_MINIMENU_FINDLOC:
    case CS2VM_HOST_REQUEST_MINIMENU_FINDOBJ:
    case CS2VM_HOST_REQUEST_MINIMENU_FINDPLAYER:
    case CS2VM_HOST_REQUEST__7106:
    case CS2VM_HOST_REQUEST__7107:
    case CS2VM_HOST_REQUEST_MINIMENU_ISOPEN:
    case CS2VM_HOST_REQUEST_MINIMENU_FINDCOMPONENT:
    case CS2VM_HOST_REQUEST_MINIMENU_NUMOPS:
        return exec_minimenu(host, vm, RS_CS2_PAYLOAD(request, Minimenu)->opcode);

    case CS2VM_HOST_REQUEST_GETREMOVEROOFS:
    case CS2VM_HOST_REQUEST_SETREMOVEROOFS:
    case CS2VM_HOST_REQUEST_SETVOLUMEMUSIC:
    case CS2VM_HOST_REQUEST_GETVOLUMEMUSIC:
    case CS2VM_HOST_REQUEST_SETVOLUMESOUNDS:
    case CS2VM_HOST_REQUEST_GETVOLUMESOUNDS:
    case CS2VM_HOST_REQUEST_SETVOLUMEAREASOUNDS:
    case CS2VM_HOST_REQUEST_GETVOLUMEAREASOUNDS:
    case CS2VM_HOST_REQUEST_CLIENTOPTION_SET:
    case CS2VM_HOST_REQUEST_CLIENTOPTION_GET:
    case CS2VM_HOST_REQUEST_DEVICEOPTION_SET:
    case CS2VM_HOST_REQUEST_GAMEOPTION_SET:
    case CS2VM_HOST_REQUEST_DEVICEOPTION_GET:
    case CS2VM_HOST_REQUEST_GAMEOPTION_GET:
    case CS2VM_HOST_REQUEST_DEVICEOPTION_GETRANGE:
        return exec_client_option(host, vm, *RS_CS2_PAYLOAD(request, ClientOption));

    case CS2VM_HOST_REQUEST_MINIMAP_SETZOOMABLE:
    case CS2VM_HOST_REQUEST_MINIMAP_SETZOOM:
    case CS2VM_HOST_REQUEST_MINIMAP_GETZOOM:
    case CS2VM_HOST_REQUEST_MINIMAP_SETICONZOOMLIMIT:
        return exec_minimap(host, vm, *RS_CS2_PAYLOAD(request, Minimap));

    case CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION:
    case CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION_CANCEL:
    case CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION_CANCELALL:
    case CS2VM_HOST_REQUEST_LOCAL_NOTIFICATION_SUPPORTED:
        return exec_local_notification(vm, *RS_CS2_PAYLOAD(request, LocalNotification));

    case CS2VM_HOST_REQUEST_LOGOUT:
        host->logout_requested = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLOSE:
        host->close_modal_requested = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_RESUME_PAUSEBUTTON:
    case CS2VM_HOST_REQUEST_IF_RESUME_PAUSEBUTTON:
        /* Last write wins within a tick — a double-fire of the continue
         * listener (key + click) would otherwise queue two resumes for one
         * pause. The server matches on the component uid either way. */
        host->resume_pausebutton_component_id = RS_CS2_PAYLOAD(request, ResumePauseButton)->component_id;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_SETWINDOWMODE:
        /* Reject anything outside the dialect's own domain rather than
         * resizing to a mode nothing names. */
        if( RS_CS2_PAYLOAD(request, WindowMode)->mode != CS2VM_WINDOW_MODE_FIXED &&
            RS_CS2_PAYLOAD(request, WindowMode)->mode != CS2VM_WINDOW_MODE_RESIZABLE )
            return CS2VM_EXECNO_OK;
        if( host->window_mode != RS_CS2_PAYLOAD(request, WindowMode)->mode )
        {
            host->window_mode = RS_CS2_PAYLOAD(request, WindowMode)->mode;
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

    case CS2VM_HOST_REQUEST_SETDEFAULTWINDOWMODE:
        if( RS_CS2_PAYLOAD(request, WindowMode)->mode != CS2VM_WINDOW_MODE_FIXED &&
            RS_CS2_PAYLOAD(request, WindowMode)->mode != CS2VM_WINDOW_MODE_RESIZABLE )
            return CS2VM_EXECNO_OK;
        host->default_window_mode = RS_CS2_PAYLOAD(request, WindowMode)->mode;
        /* A script chose it, so it is the player's setting and outlives the
         * launch (game/rs_prefs.c). The boot config setting the same field is
         * not that, which is what this flag separates. */
        host->default_window_mode_from_script = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG:
    {
        struct RS_CS2SocialSend send;

        /* An empty answer is not a zero: the opcode's callers always push a
         * rendered number, so nothing to send means nothing happened. */
        if( !RS_CS2_PAYLOAD(request, ResumeCountDialog)->text || !RS_CS2_PAYLOAD(request, ResumeCountDialog)->text[0] )
            return CS2VM_EXECNO_OK;
        memset(&send, 0, sizeof(send));
        send.kind = RS_CS2_SOCIAL_SEND_RESUME_COUNTDIALOG;
        snprintf(
            send.text, sizeof(send.text), "%s", RS_CS2_PAYLOAD(request, ResumeCountDialog)->text);
        rs_cs2_social_send_push(host, &send);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VIEWPORT_SETFOV:
    case CS2VM_HOST_REQUEST_VIEWPORT_SETZOOM:
    case CS2VM_HOST_REQUEST_VIEWPORT_CLAMPFOV:
    case CS2VM_HOST_REQUEST_VIEWPORT_GETEFFECTIVESIZE:
    case CS2VM_HOST_REQUEST_VIEWPORT_GETZOOM:
    case CS2VM_HOST_REQUEST_VIEWPORT_GETFOV:
        return exec_viewport(host, vm, *RS_CS2_PAYLOAD(request, Viewport));

    case CS2VM_HOST_REQUEST_UIZOOM_SET:
    case CS2VM_HOST_REQUEST_UIZOOM_GET:
    case CS2VM_HOST_REQUEST_UIZOOM_RESET:
    case CS2VM_HOST_REQUEST_UIZOOM_GETDEFAULT:
        return exec_uizoom(host, vm, *RS_CS2_PAYLOAD(request, UiZoom));

    case CS2VM_HOST_REQUEST_SAFEAREA_GETMINX:
    case CS2VM_HOST_REQUEST_SAFEAREA_GETMINY:
    case CS2VM_HOST_REQUEST_SAFEAREA_GETMAXX:
    case CS2VM_HOST_REQUEST_SAFEAREA_GETMAXY:
        return exec_safearea(vm, *RS_CS2_PAYLOAD(request, SafeArea));

    /* Pitch is clamped to the range the orbit camera can actually reach, so a
     * script that reads, adjusts and writes back cannot walk the camera out of
     * bounds one call at a time. */
    case CS2VM_HOST_REQUEST_CAM_FORCEANGLE:
    {
        int angle_x = RS_CS2_PAYLOAD(request, CamForceAngle)->angle_x;
        if( angle_x < 128 )
            angle_x = 128;
        if( angle_x > 383 )
            angle_x = 383;
        host->cam_angle_x = angle_x;
        host->cam_angle_y = RS_CS2_PAYLOAD(request, CamForceAngle)->angle_y & 0x7ff;
        host->cam_yaw = host->cam_angle_y;
        host->cam_angle_forced = true;
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CAM_GETANGLE_XA:
        return CS2VM2_PushInt(vm, host->cam_angle_x);

    case CS2VM_HOST_REQUEST_CAM_GETANGLE_YA:
        return CS2VM2_PushInt(vm, host->cam_angle_y);

    case CS2VM_HOST_REQUEST_WORLDMAP_INIT:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETMAPNAME:
    case CS2VM_HOST_REQUEST_WORLDMAP_SETMAP:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETZOOM:
    case CS2VM_HOST_REQUEST_WORLDMAP_SETZOOM:
    case CS2VM_HOST_REQUEST_WORLDMAP_ISLOADED:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTODISPLAYCOORD:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTODISPLAYCOORD_INSTANT:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTOSOURCECOORD:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTOSOURCECOORD_INSTANT:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISPLAYPOSITION:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETCONFIGORIGIN:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETCONFIGSIZE:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETCONFIGBOUNDS:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETCONFIGZOOM:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISPLAYCOORD_CURRENT:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETCURRENTMAP:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISPLAYCOORD:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETSOURCECOORD:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTOMAP:
    case CS2VM_HOST_REQUEST_WORLDMAP_JUMPTOMAP_INSTANT:
    case CS2VM_HOST_REQUEST_WORLDMAP_COORDINMAP:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETSIZE:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETMAP:
    case CS2VM_HOST_REQUEST_WORLDMAP_SETMAXFLASHCOUNT:
    case CS2VM_HOST_REQUEST_WORLDMAP_RESETMAXFLASHCOUNT:
    case CS2VM_HOST_REQUEST_WORLDMAP_SETCYCLESPERFLASH:
    case CS2VM_HOST_REQUEST_WORLDMAP_RESETCYCLESPERFLASH:
    case CS2VM_HOST_REQUEST_WORLDMAP_PERPETUALFLASH:
    case CS2VM_HOST_REQUEST_WORLDMAP_FLASHELEMENT:
    case CS2VM_HOST_REQUEST_WORLDMAP_FLASHELEMENTCATEGORY:
    case CS2VM_HOST_REQUEST_WORLDMAP_STOPCURRENTFLASHES:
    case CS2VM_HOST_REQUEST_WORLDMAP_DISABLEELEMENTS:
    case CS2VM_HOST_REQUEST_WORLDMAP_DISABLEELEMENT:
    case CS2VM_HOST_REQUEST_WORLDMAP_DISABLEELEMENTCATEGORY:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISABLEELEMENTS:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISABLEELEMENT:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETDISABLEELEMENTCATEGORY:
    case CS2VM_HOST_REQUEST_WORLDMAP_GETNEARESTICON:
    case CS2VM_HOST_REQUEST_WORLDMAP_LISTELEMENT_START:
    case CS2VM_HOST_REQUEST_WORLDMAP_LISTELEMENT_NEXT:
    case CS2VM_HOST_REQUEST_WORLDMAP_ELEMENT:
    case CS2VM_HOST_REQUEST_WORLDMAP_ELEMENTCOORD1:
    case CS2VM_HOST_REQUEST_WORLDMAP_ELEMENTCOORD:
        return exec_worldmap(host, vm, *RS_CS2_PAYLOAD(request, WorldMap), request->kind);

    case CS2VM_HOST_REQUEST_MEC_TEXT:
    case CS2VM_HOST_REQUEST_MEC_TEXTSIZE:
    case CS2VM_HOST_REQUEST_MEC_CATEGORY:
    case CS2VM_HOST_REQUEST_MEC_SPRITE:
        return exec_mec(host, vm, *RS_CS2_PAYLOAD(request, MEC), request->kind);

    /* These listeners are parsed to keep the VM stacks exact, but UITree does
     * not expose their corresponding event sources yet. Each opcode retains
     * its own request kind even though their current host behavior is a no-op. */
    case CS2VM_HOST_REQUEST_CC_SETONCLANTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONMISCTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONSTOCKTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONCLANSETTINGSTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONCLANCHANNELTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONMAPPOST:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETONSUBMIT:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETONABORT:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETONFOCUSCHANGED:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETONUPDATE:
    case CS2VM_HOST_REQUEST_IF_SETONCLANTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONSTOCKTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONCLANSETTINGSTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONCLANCHANNELTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONMAPPOST:
    case CS2VM_HOST_REQUEST_IF_INPUT_SETONSUBMIT:
    case CS2VM_HOST_REQUEST_IF_INPUT_SETONABORT:
    case CS2VM_HOST_REQUEST_IF_INPUT_SETONFOCUSCHANGED:
    case CS2VM_HOST_REQUEST_IF_INPUT_SETONUPDATE:
        return CS2VM_EXECNO_OK;

    /* ---- IF getters ---- */
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
    {
        int cid = RS_CS2_PAYLOAD(request, IF_GetHeight)->component_id;
        int h = tree ? UITree_GetLayoutHeight(tree, cid) : 0;
        if( torirs_trace_drag() )
            fprintf(stderr, "TORIRS_TRACE_DRAG if_getheight id=%d -> %d\n", cid, h);
        return CS2VM2_PushInt(vm, h);
    }

    case CS2VM_HOST_REQUEST_IF_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    {
        /*
         * A component's *declared* layer, which stops at its own interface.
         *
         * The cache stores `layer` per component and a pack's root carries
         * none, so the reference answers -1 there — an interface mounted into
         * another interface's slot does not report that slot. Our tree has no
         * such seam: a mounted pack is baked under its owner, so the raw tree
         * parent walks straight out of the group.
         *
         * `~script5774` is the case that makes this load-bearing. It is the
         * generic dropdown's "where is this button, in the dropdown's own
         * coordinates" walk: recurse on if_getlayer, stop at `null` or at a
         * given layer, and sum if_getx/if_gety on the way back. Walking past
         * the interface root added the gameframe's offsets, so the music tab's
         * "All music" list was positioned at x≈1158 on an 807px canvas — built
         * correctly, mounted correctly, and entirely off-screen.
         */
        int component_id = RS_CS2_PAYLOAD(request, IF_GetLayer)->component_id;
        int parent = tree ? rs_cs2_parent_component_id(tree, component_id) : -1;
        if( parent >= 0 && ((parent >> 16) & 0xffff) != ((component_id >> 16) & 0xffff) )
            parent = -1;
        return CS2VM2_PushInt(vm, parent >= 0 ? parent : -1);
    }

    case CS2VM_HOST_REQUEST_IF_GETTOP:
        return CS2VM2_PushInt(vm, host->top_interface_id);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetLayer)->component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_x : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetLayer)->component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_y : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
    {
        int cid = RS_CS2_PAYLOAD(request, IF_GetLayer)->component_id;
        int sh = 0;
        node = rs_cs2_node(host, cid);
        if( node && node->type == UIELEM_RS_LAYER )
            sh = node->u.rs_layer.scroll_height;
        if( torirs_trace_drag() )
            fprintf(
                stderr,
                "TORIRS_TRACE_DRAG if_getscrollheight id=%d type=%d -> %d\n",
                cid,
                node ? (int)node->type : -1,
                sh);
        return CS2VM2_PushInt(vm, sh);
    }

    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_width : 0);

    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_IF_GETOP:
    case CS2VM_HOST_REQUEST_CC_GETOP:
    {
        char const* op = "";
        int const op_index = RS_CS2_PAYLOAD(request, WidgetGetOp)->op_index - 1;
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, WidgetGetOp)->component_id);
        if( node && op_index >= 0 && op_index < UITREE_MENU_OPTION_SLOTS )
            op = UITree_MenuOptions(node)->ops[op_index];
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, op));
    }

    case CS2VM_HOST_REQUEST_IF_HASSUB:
    {
        /* A component "has a sub" when an interface group is mounted into it
         * (IF_OPENSUB target). The InterfaceParent map records exactly that. */
        int cid = RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id;
        int has = tree && UITree_InterfaceParentFind(tree, cid) >= 0;
        static int hassub_debug = -1;
        if( hassub_debug < 0 )
            hassub_debug = getenv("TORIRS_HASSUB_DEBUG") != NULL;
        if( hassub_debug )
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

    case CS2VM_HOST_REQUEST_IF_HASCHILD_OVERLAY:
    {
        /* 2704/2705: widget has the given parent group mounted (rev 634 does
         * not distinguish modal vs overlay on the type field). */
        int cid = RS_CS2_PAYLOAD(request, IF_HasChild)->component_id;
        int want = RS_CS2_PAYLOAD(request, IF_HasChild)->group_id;
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
            rs_cs2_get_text(tree, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, buf));
    }

    case CS2VM_HOST_REQUEST_IF_GETCOLOUR:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(vm, node ? node->colour : 0);

    case CS2VM_HOST_REQUEST_IF_GETFILLCOLOUR:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(vm, node ? node->fill_colour : 0);

    case CS2VM_HOST_REQUEST_IF_GETINVOBJECT:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(vm, node ? node->item_id : 0);

    case CS2VM_HOST_REQUEST_IF_GETINVCOUNT:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_GetWidth)->component_id);
        return CS2VM2_PushInt(vm, node ? node->item_count : 0);

    /* ---- IF / CC mutators ---- */
    case CS2VM_HOST_REQUEST_CC_SETHIDE:
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( tree )
        {

            int was_hidden = 0;
            int32_t hide_idx;
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: IF_SETHIDE component_id=%d hide=%d\n",
                RS_CS2_PAYLOAD(request, IF_SetHide)->component_id,
                RS_CS2_PAYLOAD(request, IF_SetHide)->hidden ? 1 : 0);
#endif
            static int sethide_debug = -1;
            if( sethide_debug < 0 )
                sethide_debug = getenv("TORIRS_SETHIDE_DEBUG") != NULL;
            if( sethide_debug )
            {
                int g = (RS_CS2_PAYLOAD(request, IF_SetHide)->component_id >> 16) & 0xffff;
                if( g == 149 || g == 320 || g == 218 ||
                    (g == 161 && (RS_CS2_PAYLOAD(request, IF_SetHide)->component_id & 0xffff) >= 73) )
                    fprintf(
                        stderr,
                        "sethide: component 0x%08x (%d|%d) hide=%d found=%d\n",
                        (unsigned)RS_CS2_PAYLOAD(request, IF_SetHide)->component_id,
                        g,
                        RS_CS2_PAYLOAD(request, IF_SetHide)->component_id & 0xffff,
                        RS_CS2_PAYLOAD(request, IF_SetHide)->hidden ? 1 : 0,
                        UITree_FindByComponentId(tree, RS_CS2_PAYLOAD(request, IF_SetHide)->component_id) >= 0
                            ? 1
                            : 0);
            }
            hide_idx = UITree_FindByComponentId(tree, RS_CS2_PAYLOAD(request, IF_SetHide)->component_id);
            if( hide_idx >= 0 )
                was_hidden = tree->components[hide_idx].behavior.hide ? 1 : 0;
            (void)UITree_ApplyHide(
                tree, RS_CS2_PAYLOAD(request, IF_SetHide)->component_id, RS_CS2_PAYLOAD(request, IF_SetHide)->hidden ? 1 : 0);
            /* Unhide → mark widgets-loaded (TS markWidgetsLoaded). Consumed once per
             * logic tick by RS_CS2_PumpTransmits; per-hook serials keep already-fired
             * hooks from re-running, so this no longer re-dispatches everything. */
            if( was_hidden && !RS_CS2_PAYLOAD(request, IF_SetHide)->hidden )
                host->widgets_loaded_dirty = 1;
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        if( tree )
            (void)UITree_ApplyPositionModes(
                tree,
                RS_CS2_PAYLOAD(request, CC_SetPosition)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetPosition)->x,
                RS_CS2_PAYLOAD(request, CC_SetPosition)->y,
                RS_CS2_PAYLOAD(request, CC_SetPosition)->xmode,
                RS_CS2_PAYLOAD(request, CC_SetPosition)->ymode);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        if( tree )
        {
            /* TORIRS_DUMP_SETSIZE=<group>: every size write a script makes to
             * that interface, in order. The BOUNDS dump only shows where the
             * layout ended up; when a panel resolves to a few pixels this is
             * the only way to see which call did it and what it asked for. */
            static int setsize_want = -2;
            if( setsize_want == -2 )
            {
                char const* env = getenv("TORIRS_DUMP_SETSIZE");
                setsize_want = env ? (int)strtol(env, NULL, 0) : -1;
            }
            if( setsize_want >= 0 )
            {
                int want = setsize_want;
                int group = (RS_CS2_PAYLOAD(request, CC_SetSize)->component_id >> 16) & 0xffff;
                if( group == want )
                    fprintf(
                        stderr,
                        "SETSIZE com=0x%08x (%d|%d) %dx%d modes=%d,%d\n",
                        (unsigned)RS_CS2_PAYLOAD(request, CC_SetSize)->component_id,
                        group,
                        RS_CS2_PAYLOAD(request, CC_SetSize)->component_id & 0xffff,
                        RS_CS2_PAYLOAD(request, CC_SetSize)->width,
                        RS_CS2_PAYLOAD(request, CC_SetSize)->height,
                        RS_CS2_PAYLOAD(request, CC_SetSize)->wmode,
                        RS_CS2_PAYLOAD(request, CC_SetSize)->hmode);
            }
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: SETSIZE component_id=%d size=%dx%d modes=%d,%d\n",
                RS_CS2_PAYLOAD(request, CC_SetSize)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetSize)->width,
                RS_CS2_PAYLOAD(request, CC_SetSize)->height,
                RS_CS2_PAYLOAD(request, CC_SetSize)->wmode,
                RS_CS2_PAYLOAD(request, CC_SetSize)->hmode);
#endif
            (void)UITree_ApplySizeModes(
                tree,
                RS_CS2_PAYLOAD(request, CC_SetSize)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetSize)->width,
                RS_CS2_PAYLOAD(request, CC_SetSize)->height,
                RS_CS2_PAYLOAD(request, CC_SetSize)->wmode,
                RS_CS2_PAYLOAD(request, CC_SetSize)->hmode);
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
    {
        int cid = RS_CS2_PAYLOAD(request, IF_SetScrollPos)->component_id;
        int sx = RS_CS2_PAYLOAD(request, IF_SetScrollPos)->scroll_x;
        int sy = RS_CS2_PAYLOAD(request, IF_SetScrollPos)->scroll_y;
        int req_sy = sy;
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
            if( torirs_trace_drag() )
                fprintf(
                    stderr,
                    "TORIRS_TRACE_DRAG setscrollpos id=%d req_sy=%d max_y=%d applied_sy=%d "
                    "scroll_h=%d abs_h=%d\n",
                    cid,
                    req_sy,
                    max_y,
                    sy,
                    node->u.rs_layer.scroll_height,
                    node->position.abs_h);
            (void)UITree_ApplyScrollPos(tree, cid, sx, sy);
        }
        else if( torirs_trace_drag() )
        {
            fprintf(
                stderr,
                "TORIRS_TRACE_DRAG setscrollpos SKIP id=%d node=%p type=%d req_sy=%d\n",
                cid,
                (void*)node,
                node ? (int)node->type : -1,
                req_sy);
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
        if( tree && UITree_ApplyScrollSize(
                        tree,
                        RS_CS2_PAYLOAD(request, IF_SetScrollSize)->component_id,
                        RS_CS2_PAYLOAD(request, IF_SetScrollSize)->scroll_width,
                        RS_CS2_PAYLOAD(request, IF_SetScrollSize)->scroll_height) )
        {
            /* Reference revalidateWidgetScroll: re-clamp scroll offsets after
             * the scroll area changes. */
            node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, IF_SetScrollSize)->component_id);
            if( node && node->type == UIELEM_RS_LAYER )
            {
                UITree_EnsureLayout(tree);
                UITree_ScrollClampComponent(node);
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return exec_set_graphic(host, vm, *RS_CS2_PAYLOAD(request, CC_SetGraphic), request->kind);

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC2:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetGraphic)->component_id);
        if( node && node->type == UIELEM_RS_GRAPHIC )
        {
            node->u.rs_graphic.scene_id_active = RS_CS2_PAYLOAD(request, CC_SetGraphic)->graphic_id;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetGraphic)->component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETTEXT component_id=%d text=\"%.48s\"\n",
            RS_CS2_PAYLOAD(request, CC_SetText)->component_id,
            RS_CS2_PAYLOAD(request, CC_SetText)->text ? RS_CS2_PAYLOAD(request, CC_SetText)->text : "");
#endif
        if( tree )
            (void)UITree_ApplyText(
                tree, RS_CS2_PAYLOAD(request, CC_SetText)->component_id, RS_CS2_PAYLOAD(request, CC_SetText)->text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, RS_CS2_PAYLOAD(request, IF_SetOutline)->component_id, RS_CS2_PAYLOAD(request, IF_SetOutline)->outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, RS_CS2_PAYLOAD(request, CC_SetOutline)->component_id, RS_CS2_PAYLOAD(request, CC_SetOutline)->outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTILING:
    case CS2VM_HOST_REQUEST_IF_SETTILING:
        if( tree )
            (void)UITree_ApplyGraphicTiled(
                tree, RS_CS2_PAYLOAD(request, CC_SetTiling)->component_id, RS_CS2_PAYLOAD(request, CC_SetTiling)->tiling);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHICSHADOW:
        if( tree )
            (void)UITree_ApplyGraphicShadow(
                tree,
                RS_CS2_PAYLOAD(request, CC_SetGraphicShadow)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetGraphicShadow)->shadow);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
    case CS2VM_HOST_REQUEST_IF_SETCOLOUR:
        if( tree )
            (void)UITree_ApplyColour(
                tree, RS_CS2_PAYLOAD(request, CC_SetColour)->component_id, RS_CS2_PAYLOAD(request, CC_SetColour)->colour);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETFILL:
    case CS2VM_HOST_REQUEST_IF_SETFILL:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetFill)->component_id);
        if( node && node->type == UIELEM_RS_RECT )
        {
            /* Compare before marking. Scripts re-assert the same value every
             * frame far more often than they change it, and an unconditional
             * mark costs a repaint the client does not need plus a dirty_gen
             * bump that defeats the emit retention gate (Opt 11). This is the
             * pattern UITree_SetHide and set_node_text already use. */
            uint8_t const filled = RS_CS2_PAYLOAD(request, CC_SetFill)->filled ? 1 : 0;
            if( node->u.rs_rect.filled != filled )
            {
                node->u.rs_rect.filled = filled;
                UITree_MarkNodeDirty(
                    tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetFill)->component_id));
            }
        }
        /* An arc reads the same flag as "whole disc" vs "band along the arc"
         * (reference DrawCircularArc's inner radius); 5480's ring and wedge set
         * it and its outline clears it. */
        else if( node && node->type == UIELEM_RS_ARC )
        {
            node->u.rs_arc.filled = RS_CS2_PAYLOAD(request, CC_SetFill)->filled ? 1 : 0;
            UITree_MarkNodeDirty(tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetFill)->component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTRANS:
    case CS2VM_HOST_REQUEST_IF_SETTRANS:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetTrans)->component_id);
        if( node && node->trans != RS_CS2_PAYLOAD(request, CC_SetTrans)->trans )
        {
            node->trans = RS_CS2_PAYLOAD(request, CC_SetTrans)->trans;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetTrans)->component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetNoClickThrough)->component_id);
        if( node )
        {
            uint8_t const enabled = RS_CS2_PAYLOAD(request, CC_SetNoClickThrough)->enabled ? 1 : 0;
            if( node->no_click_through != enabled )
            {
                node->no_click_through = enabled;
                UITree_MarkNodeDirty(
                    tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetNoClickThrough)->component_id));
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
    case CS2VM_HOST_REQUEST_IF_SETTEXTFONT:
        return exec_set_text_font(host, vm, *RS_CS2_PAYLOAD(request, CC_SetTextFont), request->kind);

    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
    case CS2VM_HOST_REQUEST_IF_SETTEXTALIGN:
        if( tree )
            (void)UITree_ApplyTextAlign(
                tree,
                RS_CS2_PAYLOAD(request, CC_SetTextAlign)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetTextAlign)->x_align,
                RS_CS2_PAYLOAD(request, CC_SetTextAlign)->y_align,
                RS_CS2_PAYLOAD(request, CC_SetTextAlign)->line_height);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
    case CS2VM_HOST_REQUEST_IF_SETTEXTSHADOW:
        if( tree )
            (void)UITree_ApplyTextShadow(
                tree,
                RS_CS2_PAYLOAD(request, CC_SetTextShadow)->component_id,
                RS_CS2_PAYLOAD(request, CC_SetTextShadow)->shadowed);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLE:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetDraggable)->component_id);
        if( node )
        {
            int const parent_uid = RS_CS2_PAYLOAD(request, CC_SetDraggable)->parent_uid;
            int const child_index = RS_CS2_PAYLOAD(request, CC_SetDraggable)->child_index;
            int area_uid = parent_uid;
            /* Resolve the render-area child at set time (reference WidgetOps /
             * InterfaceList.method1418(parent, childIndex)). ~scrollbar_vertical
             * does cc_setdraggable(bar, 0) so the area is the track, not the bar.
             * Script 35 places caps at event_mousey+16 and converts scroll from
             * the same value — that only works when event_mouse is track-relative
             * (0 at thumb-top). Storing the child's uid (child_index=-1) keeps
             * ResolveDragRenderArea on the track even if a later FindChildBySubid
             * miss would otherwise fall back to the bar and offset caps by +16. */
            if( tree && parent_uid >= 0 && child_index >= 0 )
            {
                int32_t const parent_idx = UITree_FindByComponentId(tree, parent_uid);
                if( parent_idx >= 0 )
                {
                    int32_t const child = UITree_FindChildBySubid(
                        tree, parent_idx, parent_uid, child_index);
                    if( child >= 0 )
                        area_uid = tree->components[child].component_id;
                }
            }
            if( !node->draggable || node->drag_render_area_uid != area_uid ||
                node->drag_render_area_child_index != -1 )
            {
                node->draggable = 1;
                node->drag_render_area_uid = area_uid;
                node->drag_render_area_child_index = -1;
                UITree_MarkNodeDirty(
                    tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetDraggable)->component_id));
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLEBEHAVIOR:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetDraggableBehavior)->component_id);
        if( node )
        {
            if( node->drag_behavior != RS_CS2_PAYLOAD(request, CC_SetDraggableBehavior)->behavior )
            {
                node->drag_behavior = RS_CS2_PAYLOAD(request, CC_SetDraggableBehavior)->behavior;
                UITree_MarkNodeDirty(
                    tree,
                    rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetDraggableBehavior)->component_id));
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetDragDeadZone)->component_id);
        if( node )
        {
            if( node->drag_dead_zone != (uint8_t)RS_CS2_PAYLOAD(request, CC_SetDragDeadZone)->zone )
            {
                node->drag_dead_zone = (uint8_t)RS_CS2_PAYLOAD(request, CC_SetDragDeadZone)->zone;
                UITree_MarkNodeDirty(
                    tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetDragDeadZone)->component_id));
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_SetDragDeadTime)->component_id);
        if( node )
        {
            if( node->drag_dead_time != (uint8_t)RS_CS2_PAYLOAD(request, CC_SetDragDeadTime)->time )
            {
                node->drag_dead_time = (uint8_t)RS_CS2_PAYLOAD(request, CC_SetDragDeadTime)->time;
                UITree_MarkNodeDirty(
                    tree, rs_cs2_find_node(host, RS_CS2_PAYLOAD(request, CC_SetDragDeadTime)->component_id));
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT_NONUM:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT_ALWAYS_NUM:
        return exec_set_object(
            host,
            vm,
            RS_CS2_PAYLOAD(request, IF_SetObject)->component_id,
            RS_CS2_PAYLOAD(request, IF_SetObject)->obj_id,
            RS_CS2_PAYLOAD(request, IF_SetObject)->count,
            RS_CS2_PAYLOAD(request, IF_SetObject)->num_mode,
            request->kind);

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
    case CS2VM_HOST_REQUEST_CC_SETOBJECT_NONUM:
    case CS2VM_HOST_REQUEST_CC_SETOBJECT_ALWAYS_NUM:
        return exec_set_object(
            host,
            vm,
            RS_CS2_PAYLOAD(request, CC_SetObject)->component_id,
            RS_CS2_PAYLOAD(request, CC_SetObject)->obj_id,
            RS_CS2_PAYLOAD(request, CC_SetObject)->count,
            RS_CS2_PAYLOAD(request, CC_SetObject)->num_mode,
            request->kind);

    case CS2VM_HOST_REQUEST_CC_DELETEALL:
    {
        int32_t parent_idx =
            tree ? UITree_FindByComponentId(tree, RS_CS2_PAYLOAD(request, CC_DeleteAll)->component_id) : -1;
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
        int32_t idx = tree ? UITree_FindByComponentId(tree, RS_CS2_PAYLOAD(request, CC_DeleteAll)->component_id) : -1;
        if( idx >= 0 )
            UITree_CcDelete(tree, idx);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_CREATE:
    case CS2VM_HOST_REQUEST_CC_CREATECHILD:
    case CS2VM_HOST_REQUEST_CC_CREATESIBLING:
        return exec_cc_create(host, vm, *RS_CS2_PAYLOAD(request, CC_Create), request->kind);
    case CS2VM_HOST_REQUEST_CC_COPY:
        return exec_cc_copy(host, vm, *RS_CS2_PAYLOAD(request, CC_Copy));

    case CS2VM_HOST_REQUEST_CC_FIND:
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FINDNEXT:
        return exec_cc_find(host, vm, *RS_CS2_PAYLOAD(request, CC_Find), request->kind);

    case CS2VM_HOST_REQUEST_IF_FIND:
        return exec_if_find(host, vm, *RS_CS2_PAYLOAD(request, TargetFind));

    case CS2VM_HOST_REQUEST_OVERLAY_CC_CREATE:
    case CS2VM_HOST_REQUEST_OVERLAY_CC_DELETEALL:
    case CS2VM_HOST_REQUEST_OVERLAY_FIND:
    case CS2VM_HOST_REQUEST_OVERLAY_CC_FIND:
    case CS2VM_HOST_REQUEST_OVERLAY_NPC_CREATE:
    case CS2VM_HOST_REQUEST_OVERLAY_LOC_CREATE:
    case CS2VM_HOST_REQUEST_OVERLAY_PLAYER_CREATE:
    case CS2VM_HOST_REQUEST_OVERLAY_COORD_CREATE:
    case CS2VM_HOST_REQUEST_OVERLAY_NPC_GET:
    case CS2VM_HOST_REQUEST_OVERLAY_LOC_GET:
    case CS2VM_HOST_REQUEST_OVERLAY_PLAYER_GET:
    case CS2VM_HOST_REQUEST_OVERLAY_COORD_GET:
    case CS2VM_HOST_REQUEST_OVERLAY_NPC_DESTROY:
    case CS2VM_HOST_REQUEST_OVERLAY_LOC_DESTROY:
    case CS2VM_HOST_REQUEST_OVERLAY_PLAYER_DESTROY:
    case CS2VM_HOST_REQUEST_OVERLAY_COORD_DESTROY:
        return exec_entity_overlay(host, vm, *RS_CS2_PAYLOAD(request, EntityOverlay));

    case CS2VM_HOST_REQUEST_LOC_FIND:
    case CS2VM_HOST_REQUEST_COORD_INSCENE:
        return exec_subject_find(host, vm, *RS_CS2_PAYLOAD(request, SubjectFind));

    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND_COUNT:
        return exec_children_find(
            host,
            vm,
            RS_CS2_PAYLOAD(request, CC_ChildrenFind)->parent_id,
            RS_CS2_PAYLOAD(request, CC_ChildrenFind)->start_index,
            0,
            0,
            request->kind);

    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
    case CS2VM_HOST_REQUEST_IF_CHILDREN_COLLECT:
        return exec_children_find(
            host,
            vm,
            RS_CS2_PAYLOAD(request, IF_ChildrenFind)->uid,
            RS_CS2_PAYLOAD(request, IF_ChildrenFind)->start_index,
            1,
            RS_CS2_PAYLOAD(request, IF_ChildrenFind)->dot_operand,
            request->kind);

    case CS2VM_HOST_REQUEST_CC_GETID:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        assert(node);

        return CS2VM2_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);

    case CS2VM_HOST_REQUEST_CC_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, RS_CS2_PAYLOAD(request, CC_GetId)->component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, RS_CS2_PAYLOAD(request, CC_GetId)->component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, RS_CS2_PAYLOAD(request, CC_GetId)->component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutHeight(tree, RS_CS2_PAYLOAD(request, CC_GetId)->component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            rs_cs2_get_text(tree, RS_CS2_PAYLOAD(request, CC_GetId)->component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, buf));
    }

    case CS2VM_HOST_REQUEST_CC_GETCOLOUR:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node ? node->colour : 0);

    case CS2VM_HOST_REQUEST_CC_GETFILLCOLOUR:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node ? node->fill_colour : 0);

    case CS2VM_HOST_REQUEST_CC_GETINVOBJECT:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node ? node->item_id : 0);

    case CS2VM_HOST_REQUEST_CC_GETINVCOUNT:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node ? node->item_count : 0);

    case CS2VM_HOST_REQUEST_CC_GETTRANS:
        node = rs_cs2_node(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id);
        return CS2VM2_PushInt(vm, node ? node->trans : 0);

    case CS2VM_HOST_REQUEST_CC_GETTARGETMASK:
    case CS2VM_HOST_REQUEST_IF_GETTARGETMASK:
        return CS2VM2_PushInt(
            vm, rs_cs2_target_mask(host, RS_CS2_PAYLOAD(request, CC_GetId)->component_id));

    case CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM:
        return exec_cc_getcomponentparam(host, vm, *RS_CS2_PAYLOAD(request, CC_ComponentParam));

    case CS2VM_HOST_REQUEST_IF_GETCOMPONENTPARAM:
        return exec_if_getcomponentparam(host, vm, *RS_CS2_PAYLOAD(request, CC_ComponentParam));

    case CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM:
    case CS2VM_HOST_REQUEST_IF_SETPARAM:
        if( tree )
            (void)UITree_ApplyComponentParam(
                tree,
                RS_CS2_PAYLOAD(request, CC_ComponentParam)->component_id,
                RS_CS2_PAYLOAD(request, CC_ComponentParam)->param_id,
                RS_CS2_PAYLOAD(request, CC_ComponentParam)->value,
                RS_CS2_PAYLOAD(request, CC_ComponentParam)->str_value);
        return CS2VM_EXECNO_OK;

    /* ---- Ops ---- */
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOP:
        if( tree )
            rs_cs2_apply_op(
                tree,
                RS_CS2_PAYLOAD(request, IF_SetOp)->component_id,
                RS_CS2_PAYLOAD(request, IF_SetOp)->index,
                RS_CS2_PAYLOAD(request, IF_SetOp)->text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOPBASE:
    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
        if( tree )
            (void)UITree_ApplyOpBase(
                tree, RS_CS2_PAYLOAD(request, IF_SetOpBase)->component_id, RS_CS2_PAYLOAD(request, IF_SetOpBase)->text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTARGETVERB:
    case CS2VM_HOST_REQUEST_IF_SETTARGETVERB:
        if( tree )
            (void)UITree_ApplyTargetVerb(
                tree,
                RS_CS2_PAYLOAD(request, IF_SetTargetVerb)->component_id,
                RS_CS2_PAYLOAD(request, IF_SetTargetVerb)->text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOPSUBMENU:
    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
        if( tree )
            rs_cs2_apply_op_submenu(
                tree,
                RS_CS2_PAYLOAD(request, IF_SetOpSubmenu)->component_id,
                RS_CS2_PAYLOAD(request, IF_SetOpSubmenu)->op_index,
                RS_CS2_PAYLOAD(request, IF_SetOpSubmenu)->sub_index,
                RS_CS2_PAYLOAD(request, IF_SetOpSubmenu)->text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_CLEAROPSUBMENU:
        if( tree )
            (void)UITree_ClearOpSubmenu(
                tree,
                RS_CS2_PAYLOAD(request, IF_ClearOpSubmenu)->component_id,
                RS_CS2_PAYLOAD(request, IF_ClearOpSubmenu)->op_index);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTARGETPRIORITY:
    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        if( tree )
            (void)UITree_ApplyTargetPriority(
                tree,
                RS_CS2_PAYLOAD(request, IF_SetTargetPriority)->component_id,
                RS_CS2_PAYLOAD(request, IF_SetTargetPriority)->priority);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOPKEY:
    case CS2VM_HOST_REQUEST_CC_SETOPTKEY:
    case CS2VM_HOST_REQUEST_IF_SETOPKEY:
    case CS2VM_HOST_REQUEST_IF_SETOPTKEY:
        if( tree )
            (void)UITree_ApplyOpKey(
                tree,
                RS_CS2_PAYLOAD(request, WidgetSetOpKey)->component_id,
                RS_CS2_PAYLOAD(request, WidgetSetOpKey)->op_index,
                RS_CS2_PAYLOAD(request, WidgetSetOpKey)->key_chars,
                RS_CS2_PAYLOAD(request, WidgetSetOpKey)->key_codes,
                RS_CS2_PAYLOAD(request, WidgetSetOpKey)->pair_count);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOPKEYRATE:
    case CS2VM_HOST_REQUEST_CC_SETOPTKEYRATE:
    case CS2VM_HOST_REQUEST_CC_SETOPKEYIGNOREHELD:
    case CS2VM_HOST_REQUEST_CC_SETOPTKEYIGNOREHELD:
    case CS2VM_HOST_REQUEST_IF_SETOPKEYRATE:
    case CS2VM_HOST_REQUEST_IF_SETOPTKEYRATE:
    case CS2VM_HOST_REQUEST_IF_SETOPKEYIGNOREHELD:
    case CS2VM_HOST_REQUEST_IF_SETOPTKEYIGNOREHELD:
        if( tree )
        {
            if( RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->ignore_held )
                (void)UITree_ApplyOpKeyIgnoreHeld(
                    tree,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->component_id,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->op_index);
            else
                (void)UITree_ApplyOpKeyRate(
                    tree,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->component_id,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->op_index,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->rate,
                    RS_CS2_PAYLOAD(request, WidgetSetOpKeyRate)->enabled);
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_CLEAROPS:
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        if( tree )
            rs_cs2_clear_ops(tree, RS_CS2_PAYLOAD(request, IF_ClearOps)->component_id);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CALLONRESIZE:
        /* Queued, not run: this is reached from inside a running CS2 script and
         * the host has no runner to nest a second one on. See the queue's
         * comment in rs_cs2_host.h for why deferring is safe for every call
         * site in this cache. */
        rs_cs2_call_on_resize_push(host, RS_CS2_PAYLOAD(request, IF_CallOnResize)->component_id);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_TRIGGEROP:
        /* Queued for the same reason as IF_CALLONRESIZE above. */
        rs_cs2_trigger_op_push(
            host,
            RS_CS2_PAYLOAD(request, CC_TriggerOp)->component_id,
            RS_CS2_PAYLOAD(request, CC_TriggerOp)->op_index);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_SOUND_SYNTH:
        rs_cs2_sound_push(host, RS_CS2_SOUND_SYNTH, RS_CS2_PAYLOAD(request, Sound));
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_SOUND_SONG:
        rs_cs2_sound_push(host, RS_CS2_SOUND_SONG, RS_CS2_PAYLOAD(request, Sound));
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_SOUND_JINGLE:
        rs_cs2_sound_push(host, RS_CS2_SOUND_JINGLE, RS_CS2_PAYLOAD(request, Sound));
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_SOUND_SONG_WITHSECONDARY:
        rs_cs2_sound_push(host, RS_CS2_SOUND_SONG_WITHSECONDARY, RS_CS2_PAYLOAD(request, Sound));
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_TRIGGEROPLOCAL:
        /* Queued so the App can turn it into IF_BUTTON1 on the wire. */
        rs_cs2_triggeroplocal_push(
            host,
            RS_CS2_PAYLOAD(request, IF_TriggerOpLocal)->component_id,
            RS_CS2_PAYLOAD(request, IF_TriggerOpLocal)->sub);
        return CS2VM_EXECNO_OK;

    /* ---- SetOn (hooks / no-ops) ---- */
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return exec_set_on_var_transmit(host, RS_CS2_PAYLOAD(request, IF_SetOnVarTransmit));

    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return exec_set_on_inv_transmit(host, RS_CS2_PAYLOAD(request, IF_SetOnInvTransmit));

    case CS2VM_HOST_REQUEST_IF_SETONSTATTRANSMIT:
        /* Same request shape as the var one — script, captured args, a trigger
         * list — so it shares the payload struct. The trigger ids are stat ids
         * rather than varp ids, which is the only difference and is the
         * dispatcher's business, not this one's. */
        return exec_set_on_stat_transmit(host, RS_CS2_PAYLOAD(request, IF_SetOnVarTransmit));

    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONHOLD:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONRELEASE:
    case CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONTARGETENTER:
    case CS2VM_HOST_REQUEST_IF_SETONTARGETLEAVE:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_IF_SETONITEMONITEM:
    case CS2VM_HOST_REQUEST_IF_SETONCLANSETTINGS:
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        /* Was `return CS2VM_EXECNO_OK` — a well-formed request the VM had
         * already built, thrown away. Registering it is the same call every
         * other SetOn makes; the walker that fires them is
         * CreateTask_CS2MiscTransmitDispatch. */
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT:
        /* The friends/ignore panels' only repaint trigger. Walked by
         * CreateTask_CS2FriendTransmitDispatch. */
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_IF_SETONCHATTRANSMIT:
        /* The chatbox's only repaint trigger -- the cache's own scripts own
         * every line in it. Walked by CreateTask_CS2ChatTransmitDispatch. */
        return exec_set_on_if_event(host, request->kind, RS_CS2_PAYLOAD(request, IF_SetOnOp));
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONRELEASE:
    case CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT:
    case CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONCHATTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONTARGETENTER:
    case CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONITEMONITEM:
    case CS2VM_HOST_REQUEST_CC_SETONCLANSETTINGS:
        return exec_set_on_cc_event(host, vm, request->kind, RS_CS2_PAYLOAD(request, CC_SetOnOp));
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONSTATTRANSMIT:
        return exec_set_on_cc_transmit(host, vm, request->kind, RS_CS2_PAYLOAD(request, CC_SetOnOp));
    case CS2VM_HOST_REQUEST_SETANTIDRAG:
        if( tree )
            tree->anti_drag = RS_CS2_PAYLOAD(request, WidgetSetInt)->value ? 1 : 0;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_DRAGPICKUP:
    case CS2VM_HOST_REQUEST_CC_DRAGPICKUP:
        /* Stage for InteractFrame — reference Client.dragTryPickup. The host
         * has the tree but not UIInteraction::input_state, so we park the
         * request here the same way SETANTIDRAG parks anti_drag. */
        if( tree )
        {
            int const cid = RS_CS2_PAYLOAD(request, DragPickup)->component_id;
            node = rs_cs2_node(host, cid);
            if( node )
            {
                /* getDragLayer null → no-op. A resolvable render area (or
                 * clickmask drag depth) is required; otherwise track/list
                 * clicks would stage a pickup on the wrong node. */
                if( node->drag_render_area_uid < 0 &&
                    UITree_ClickMaskDragDepth(node->behavior.click_mask) == 0 )
                    return CS2VM_EXECNO_OK;
                tree->pending_drag_pickup = 1;
                tree->pending_drag_pickup_id = cid;
                tree->pending_drag_pickup_x = RS_CS2_PAYLOAD(request, DragPickup)->pickup_x;
                tree->pending_drag_pickup_y = RS_CS2_PAYLOAD(request, DragPickup)->pickup_y;
            }
        }
        return CS2VM_EXECNO_OK;

    /* ---- Widget payloads shared by exact CC/IF opcode kinds ---- */
    case CS2VM_HOST_REQUEST_CC_SETPINCH:
    case CS2VM_HOST_REQUEST_CC_SETNOSCROLLTHROUGH:
    case CS2VM_HOST_REQUEST_CC_SETLINEWID:
    case CS2VM_HOST_REQUEST_CC_SET2DANGLE:
    case CS2VM_HOST_REQUEST_CC_SETMODELANIM:
    case CS2VM_HOST_REQUEST_CC_SETMODELORTHOG:
    case CS2VM_HOST_REQUEST_CC_SETVFLIP:
    case CS2VM_HOST_REQUEST_CC_SETHFLIP:
    case CS2VM_HOST_REQUEST_CC_SETFILLCOLOUR:
    case CS2VM_HOST_REQUEST_CC_SETTRANSBOT:
    case CS2VM_HOST_REQUEST_CC_SETFILLMODE:
    case CS2VM_HOST_REQUEST_CC_SETLINEDIRECTION:
    case CS2VM_HOST_REQUEST_CC_SETMODELTRANSPARENT:
    case CS2VM_HOST_REQUEST_CC_SETOPFORCELEFTCLICK:
    case CS2VM_HOST_REQUEST_IF_SETPINCH:
    case CS2VM_HOST_REQUEST_IF_SETNOCLICKTHROUGH:
    case CS2VM_HOST_REQUEST_IF_SETNOSCROLLTHROUGH:
    case CS2VM_HOST_REQUEST_IF_SETLINEWID:
    case CS2VM_HOST_REQUEST_IF_SET2DANGLE:
    case CS2VM_HOST_REQUEST_IF_SETMODELANIM:
    case CS2VM_HOST_REQUEST_IF_SETMODELORTHOG:
    case CS2VM_HOST_REQUEST_IF_SETVFLIP:
    case CS2VM_HOST_REQUEST_IF_SETHFLIP:
    case CS2VM_HOST_REQUEST_IF_SETFILLCOLOUR:
    case CS2VM_HOST_REQUEST_IF_SETTRANSBOT:
    case CS2VM_HOST_REQUEST_IF_SETFILLMODE:
    case CS2VM_HOST_REQUEST_IF_SETLINEDIRECTION:
    case CS2VM_HOST_REQUEST_IF_SETMODELTRANSPARENT:
    case CS2VM_HOST_REQUEST_IF_SETDRAGDEADZONE:
    case CS2VM_HOST_REQUEST_IF_SETDRAGDEADTIME:
    case CS2VM_HOST_REQUEST_IF_SETCLICKMASK:
        return exec_widget_set_int(host, vm, *RS_CS2_PAYLOAD(request, WidgetSetInt), request->kind);

    case CS2VM_HOST_REQUEST_CC_SETMODEL:
    case CS2VM_HOST_REQUEST_IF_SETMODEL:
        return exec_widget_set_model(host, vm, *RS_CS2_PAYLOAD(request, WidgetSetModel), request->kind);

    case CS2VM_HOST_REQUEST_CC_SETMODELANGLE:
    case CS2VM_HOST_REQUEST_IF_SETMODELANGLE:
        return exec_widget_set_model_angle(host, vm, *RS_CS2_PAYLOAD(request, WidgetSetModelAngle));

    case CS2VM_HOST_REQUEST_CC_SETARC:
    case CS2VM_HOST_REQUEST_IF_SETARC:
        return exec_widget_set_arc(host, vm, *RS_CS2_PAYLOAD(request, WidgetSetArc));

    case CS2VM_HOST_REQUEST_CC_SETNPCHEAD:
    case CS2VM_HOST_REQUEST_CC_SETPLAYERHEAD_SELF:
    case CS2VM_HOST_REQUEST_CC_SETPLAYERMODEL_SELF:
    case CS2VM_HOST_REQUEST_CC_SETMODEL_PLAYERCHATHEAD:
    case CS2VM_HOST_REQUEST_IF_SETNPCHEAD:
    case CS2VM_HOST_REQUEST_IF_SETPLAYERHEAD_SELF:
    case CS2VM_HOST_REQUEST_IF_SETMODEL_PLAYERCHATHEAD:
        return exec_widget_set_model_kind(
            host, vm, *RS_CS2_PAYLOAD(request, WidgetSetModelKind), request->kind);

    case CS2VM_HOST_REQUEST_CC_INPUT_SETSUBMITMODE:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETSELECTCOLOUR:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETACCEPTMODE:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETWRAPMODE:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETLINEWRAPPINGWIDTH:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETSELECTBGCOLOUR:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETLINECOUNTLIMIT:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORCOLOUR:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORTRANS:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORWIDTH:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORHEIGHT:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSOROFFSET:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETLINEWIDTHLIMIT:
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCHARFILTER:
        /* Input widget fields not on UITree yet. */
        return CS2VM_EXECNO_OK;

    default:
        fprintf(stderr, "RS_CS2Host_Exec: UNHANDLED request kind %d\n", (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}
