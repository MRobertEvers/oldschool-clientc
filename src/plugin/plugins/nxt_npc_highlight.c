#include "plugin/plugins/nxt_activities.h"
#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/*
 * All Settings > Activities > General, the NPC highlight family:
 *
 *   261  NPC highlight                             (the master switch)
 *   416  NPC highlight - Tagging                   (offer Tag/Untag)
 *   260  NPC highlight - Highlight outline         (the model silhouette)
 *   259  NPC highlight - Highlight tile            off / outline / outline+fill
 *   258  NPC highlight - Display name              off / normal / bold
 *   262  NPC highlight - Highlighting colour       (outline and tile)
 *   263  NPC highlight - Text colour               (the tagged name)
 *   267  Clear your highlighted NPCs               (a button)
 *   264  Display all NPC names above their body    off / normal / bold
 *   266  NPC names text colour                     (264's colour)
 *
 * Two features share one plugin because they share their drawing: 264 puts a
 * name over EVERY npc and answers to nothing else in the list, but it is the
 * same text over the same anchor in the same pass, and splitting it would mean
 * two plugins walking the npc list and two names stacked over a tagged npc
 * when both are on. Here 264 is skipped for an npc 258 has already named.
 *
 * Tags are keyed on base_npc_id -- the multinpc SHELL -- and never on npc_id,
 * for the reason `entity_highlighter.lua` states: a multinpc's drawn type
 * changes whenever a varbit flips, and a tag keyed on the drawn type falls off
 * silently the moment it does.
 *
 * ---- "bold" ----
 *
 * Both name rows offer normal and bold. The overlay layer has ONE font -- the
 * hitsplat font, which is what every entity overlay in this client draws in --
 * so bold is drawn as the same glyphs struck twice, one pixel apart. That is a
 * faux bold and it looks like one up close. It is still the right answer here:
 * the row's job is to make a name stand out against a crowd, a second pass
 * does that, and the alternative is a row that visibly does nothing.
 */

/** Tagged shells held at once. Past this, tagging says so. */
#define NXT_NPC_TAGS_MAX 128
#define NXT_NPC_TAGS_ASSET "npcs.txt"
#define NXT_NPC_TAGS_FILE_MAX (NXT_NPC_TAGS_MAX * 8)

/** The wash under "outline and fill". Light enough to leave the ground and
 *  whatever is standing on it readable, which is the whole difference between
 *  it and the outline-only choice. */
#define NXT_NPC_TILE_FILL_ALPHA 64

/** Pixels the name sits above the model's overhead anchor. The anchor is where
 *  a health bar's TOP edge goes; a name drawn exactly there overlaps the bar
 *  of an npc in combat. */
#define NXT_NPC_NAME_LIFT 8

#define NXT_NPC_TAG_TAG 0x4E504354u /* 'NPCT' */

static struct ToriRS_PluginApi const* g_api;
static int g_tags[NXT_NPC_TAGS_MAX];
static int g_tag_count;
static bool g_tags_dirty;

static int
nxt_npc_tag_find(int base_npc_id)
{
    for( int i = 0; i < g_tag_count; i++ )
        if( g_tags[i] == base_npc_id )
            return i;
    return -1;
}

static void
nxt_npc_tag_toggle(struct ToriRS_PluginCtx* ctx, int base_npc_id)
{
    int const at = nxt_npc_tag_find(base_npc_id);

    assert(ctx);

    if( at >= 0 )
    {
        g_tags[at] = g_tags[--g_tag_count];
        g_tags_dirty = true;
        return;
    }
    if( g_tag_count >= NXT_NPC_TAGS_MAX )
    {
        g_api->log(
            ctx,
            "already holding %d tagged npcs, which is all this can hold; clear some "
            "with All Settings > Activities > \"Clear your highlighted NPCs\"",
            NXT_NPC_TAGS_MAX);
        return;
    }
    g_tags[g_tag_count++] = base_npc_id;
    g_tags_dirty = true;
}

/* ---- persistence ------------------------------------------------------- */

static void
nxt_npc_tags_parse(char const* text, int size)
{
    int pos = 0;

    assert(text);

    g_tag_count = 0;
    while( pos < size && g_tag_count < NXT_NPC_TAGS_MAX )
    {
        int id = 0;
        int consumed = 0;

        if( sscanf(&text[pos], "%d%n", &id, &consumed) != 1 )
            break;
        pos += consumed;
        while( pos < size && (text[pos] == '\n' || text[pos] == '\r' || text[pos] == ' ') )
            pos++;
        g_tags[g_tag_count++] = id;
    }
}

static void
nxt_npc_tags_save(struct ToriRS_PluginCtx* ctx)
{
    static char buf[NXT_NPC_TAGS_FILE_MAX];
    int used = 0;

    assert(ctx);

    for( int i = 0; i < g_tag_count; i++ )
    {
        int const wrote =
            snprintf(&buf[used], sizeof(buf) - (size_t)used, "%d\n", g_tags[i]);
        if( wrote <= 0 || wrote >= (int)(sizeof(buf) - (size_t)used) )
            break;
        used += wrote;
    }
    g_api->asset_save(ctx, NXT_NPC_TAGS_ASSET, buf, used);
    g_tags_dirty = false;
}

static enum ToriRS_PluginVerdict
nxt_npc_asset(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvAsset* ev = (struct ToriRS_PluginEvAsset*)event;
    void const* data;
    int size = 0;

    assert(ctx);
    assert(ev);

    if( !ev->name || strcmp(ev->name, NXT_NPC_TAGS_ASSET) != 0 || !ev->ok )
        return TORIRS_PLUGIN_PASS;

    data = g_api->asset_data(ctx, NXT_NPC_TAGS_ASSET, &size);
    if( data && size > 0 )
        nxt_npc_tags_parse((char const*)data, size);
    g_api->asset_release(ctx, NXT_NPC_TAGS_ASSET);
    return TORIRS_PLUGIN_PASS;
}

/* ---- the menu row ------------------------------------------------------ */

static enum ToriRS_PluginVerdict
nxt_npc_menu_build(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvMenuBuild* ev = (struct ToriRS_PluginEvMenuBuild*)event;
    char label[64];

    assert(ctx);
    assert(ev);

    if( ev->hover_pass )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->varbit(ctx, NXT_VARBIT_NPC_TAGGING) )
        return TORIRS_PLUGIN_PASS;
    if( !g_api->key_held(ctx, TORIRS_PLUGIN_KEY_SHIFT) )
        return TORIRS_PLUGIN_PASS;

    /*
     * The npc the menu is already about, rather than whatever the pointer is
     * over: a right-click menu is built once and then stands still while it is
     * read, and the rows in it are the authority on what was clicked.
     *
     * One row per npc in the build, and there is never more than one -- the
     * minimenu carries an npc's ops together -- so the first row with a slot
     * is the npc. A second npc under the same pixel is a different right
     * click.
     */
    for( int i = 0; i < ev->row_count; i++ )
    {
        struct ToriRS_PluginNpcSnap npc;

        if( ev->rows[i].npc_slot < 0 )
            continue;
        if( !g_api->npc_by_slot(ctx, ev->rows[i].npc_slot, &npc) )
            continue;

        snprintf(
            label,
            sizeof(label),
            "%s %s",
            nxt_npc_tag_find(npc.base_npc_id) >= 0 ? "Untag" : "Tag",
            npc.name);
        g_api->menu_add(ctx, ev, label, (uint32_t)npc.base_npc_id ^ NXT_NPC_TAG_TAG);
        return TORIRS_PLUGIN_PASS;
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_npc_menu_select(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvMenuSelect* ev = (struct ToriRS_PluginEvMenuSelect*)event;

    assert(ctx);
    assert(ev);

    if( !ev->owned )
        return TORIRS_PLUGIN_PASS;
    /* The shell id is carried in the tag, xor'd with a constant so a plugin
     * that is handed someone else's zero tag cannot be mistaken for npc 0. */
    nxt_npc_tag_toggle(ctx, (int)(ev->plugin_tag ^ NXT_NPC_TAG_TAG));
    return TORIRS_PLUGIN_CONSUME;
}

static enum ToriRS_PluginVerdict
nxt_npc_setting(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvSetting* ev = (struct ToriRS_PluginEvSetting*)event;

    assert(ctx);
    assert(ev);

    if( ev->setting_id != NXT_SETTING_CLEAR_NPC_TAGS )
        return TORIRS_PLUGIN_PASS;
    g_tag_count = 0;
    g_tags_dirty = true;
    return TORIRS_PLUGIN_PASS;
}

/* ---- drawing ----------------------------------------------------------- */

/** `mode` is NXT_NAME_OFF / _NORMAL / _BOLD. */
static void
nxt_npc_draw_name(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    struct ToriRS_PluginNpcSnap const* npc,
    int mode,
    uint32_t rgb)
{
    int screen_x;
    int screen_y;
    int height;

    assert(ctx);
    assert(npc);

    if( mode == NXT_NAME_OFF || npc->name[0] == '\0' )
        return;

    height = g_api->element_height(ctx, npc->element_id) + NXT_NPC_NAME_LIFT;
    if( !g_api->project(ctx, npc->fine_x, npc->fine_z, height, &screen_x, &screen_y) )
        return;

    g_api->draw_text(ctx, surface, screen_x, screen_y, npc->name, rgb);
    if( mode == NXT_NAME_BOLD )
        g_api->draw_text(ctx, surface, screen_x + 1, screen_y, npc->name, rgb);
}

static void
nxt_npc_draw_tile(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    struct ToriRS_PluginNpcSnap const* npc,
    int mode,
    uint32_t rgb)
{
    int const alpha = mode == NXT_TILE_OUTLINE_FILL ? NXT_NPC_TILE_FILL_ALPHA : 0;

    assert(ctx);
    assert(npc);

    if( mode == NXT_TILE_OFF )
        return;

    /* true_x/true_z is the SW corner of the footprint, so a 2x2 npc gets four
     * tiles and not one under its corner. Drawn as separate tiles rather than
     * one outline of the whole square: draw_tile samples the terrain per tile,
     * which is what keeps the marker coplanar on a slope. */
    for( int dz = 0; dz < npc->size; dz++ )
        for( int dx = 0; dx < npc->size; dx++ )
            g_api->draw_tile(
                ctx,
                surface,
                npc->true_x + dx,
                npc->true_z + dz,
                npc->level,
                rgb,
                rgb,
                alpha);
}

static enum ToriRS_PluginVerdict
nxt_npc_draw(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)userdata;

    struct ToriRS_PluginEvDraw* ev = (struct ToriRS_PluginEvDraw*)event;
    int const highlight_on = g_api->varbit(ctx, NXT_VARBIT_NPC_HIGHLIGHT);
    int const names_all = g_api->varbit(ctx, NXT_VARBIT_NPC_NAMES_ALL);
    int const name_mode = g_api->varbit(ctx, NXT_VARBIT_NPC_NAME);
    int const tile_mode = g_api->varbit(ctx, NXT_VARBIT_NPC_TILE);
    int const outline_on = g_api->varbit(ctx, NXT_VARBIT_NPC_OUTLINE);
    uint32_t highlight_rgb;
    uint32_t text_rgb;
    uint32_t all_rgb;
    int iter = -1;

    assert(ctx);
    assert(ev);

    if( !highlight_on && names_all == NXT_NAME_OFF )
        return TORIRS_PLUGIN_PASS;

    highlight_rgb =
        g_api->setting_color(ctx, NXT_VARP_NPC_HIGHLIGHT_COLOR, NXT_COL_NPC_HIGHLIGHT);
    text_rgb = g_api->setting_color(ctx, NXT_VARP_NPC_TEXT_COLOR, NXT_COL_NPC_HIGHLIGHT);
    all_rgb = g_api->setting_color(ctx, NXT_VARP_NPC_NAMES_COLOR, NXT_COL_NPC_HIGHLIGHT);

    for( ;; )
    {
        struct ToriRS_PluginNpcSnap npc;
        bool tagged;

        iter = g_api->npc_next(ctx, iter, &npc);
        if( iter < 0 )
            break;

        tagged = highlight_on && nxt_npc_tag_find(npc.base_npc_id) >= 0;

        if( tagged )
        {
            if( outline_on && npc.element_id >= 0 )
                g_api->draw_hull(
                    ctx,
                    ev->surface,
                    npc.element_id,
                    highlight_rgb,
                    0,
                    TORIRS_PLUGIN_HULL_MESH);
            nxt_npc_draw_tile(ctx, ev->surface, &npc, tile_mode, highlight_rgb);
            nxt_npc_draw_name(ctx, ev->surface, &npc, name_mode, text_rgb);
        }

        /* 264 names every npc, but never a second time over one 258 has
         * already named: two names in two colours over one head is not what
         * either row asks for. */
        if( !(tagged && name_mode != NXT_NAME_OFF) )
            nxt_npc_draw_name(ctx, ev->surface, &npc, names_all, all_rgb);
    }
    return TORIRS_PLUGIN_PASS;
}

/* ---- lifecycle --------------------------------------------------------- */

static enum ToriRS_PluginVerdict
nxt_npc_frame(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    if( g_tags_dirty )
        nxt_npc_tags_save(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
nxt_npc_start(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    assert(ctx);
    if( g_api->asset_load(ctx, NXT_NPC_TAGS_ASSET) == 1 )
    {
        int size = 0;
        void const* data = g_api->asset_data(ctx, NXT_NPC_TAGS_ASSET, &size);
        if( data && size > 0 )
            nxt_npc_tags_parse((char const*)data, size);
    }
    return TORIRS_PLUGIN_PASS;
}

static void
nxt_npc_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, nxt_npc_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, nxt_npc_asset, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_FRAME_START, nxt_npc_frame, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_BUILD, nxt_npc_menu_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_SELECT, nxt_npc_menu_select, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SETTING, nxt_npc_setting, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, nxt_npc_draw, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_NPC_HIGHLIGHT = {
    .name = "nxt-npc-highlight",
    .title = "NPC highlight (All Settings)",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .hidden = true,
    .init = nxt_npc_init,
    .shutdown = NULL,
};
