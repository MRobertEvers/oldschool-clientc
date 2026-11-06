/*
 * The command palette (module "cmdpalette", Ctrl+K; workbench 6.8, design
 * section 3 "Command palette"): one search over everything the editor can
 * name. edpanels.h has the contract; this is the list model and the modal.
 *
 * The model is pure: items are counted per group, kept up to
 * EDITOR_CMDPAL_GROUP_MAX, and laid out as rows -- a header per group with
 * matches, its items, and a footer "and N more" when it had more than it
 * kept. The module refills it whenever the search text changes (never on a
 * frame where it did not), from the hotkey table, the registry's panels, the
 * catalog (CATALOG_QUERY with TORIRSMAPED_CATALOG_PROGRESS, so a warm build
 * in flight answers from its partial index instead of making the frame
 * wait), and the Accounts panel's list.
 *
 * The rows are a cells VLIST: no picture well, the label, the dim part, the
 * chord as a keycap, the right column (an id, the panel an unbound action
 * opens). The App's dispatcher hands the palette the frame's navigation keys
 * while it is open (Editor_CmdPalette_Feed), as it hands the Hotkeys panel
 * a capture.
 */

#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/accounts/accounts_client.h"
#include "editor/accounts/accounts_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the model ------------------------------------------------------------------ */

static const char* const k_group_names[EDITOR_CMDPAL_GROUPS] = { "Actions", "Panels", "Squares", "Records",
    "Scenes", "Lanes", "Accounts" };

const char*
Editor_CmdPalGroupName(int group)
{
    assert(group >= 0);
    assert(group < EDITOR_CMDPAL_GROUPS);
    return k_group_names[group];
}

void
Editor_CmdPalListInit(struct Editor_CmdPalList* list)
{
    assert(list);
    memset(list, 0, sizeof(*list));
}

void
Editor_CmdPalListFree(struct Editor_CmdPalList* list)
{
    if( !list )
        return;
    ToriMem_Free(ToriMem_Editor(), list->items);
    ToriMem_Free(ToriMem_Editor(), list->row_kind);
    ToriMem_Free(ToriMem_Editor(), list->row_item);
    ToriMem_Free(ToriMem_Editor(), list->row_group);
    memset(list, 0, sizeof(*list));
}

void
Editor_CmdPalListClear(struct Editor_CmdPalList* list)
{
    assert(list);
    list->item_count = 0;
    list->row_count = 0;
    memset(list->total, 0, sizeof(list->total));
    memset(list->kept, 0, sizeof(list->kept));
}

int
Editor_CmdPalListAdd(
    struct Editor_CmdPalList* list,
    const struct Editor_CmdPalItem* item)
{
    assert(list);
    assert(item);
    assert(item->group >= 0);
    assert(item->group < EDITOR_CMDPAL_GROUPS);
    list->total[item->group]++;
    if( list->kept[item->group] >= EDITOR_CMDPAL_GROUP_MAX )
        return 0;
    if( list->item_count == list->item_cap )
    {
        int const cap = list->item_cap ? list->item_cap * 2 : 128;
        struct Editor_CmdPalItem* grown =
            ToriMem_Realloc(ToriMem_Editor(), list->items, (size_t)cap * sizeof(*grown));
        assert(grown);
        list->items = grown;
        list->item_cap = cap;
    }
    list->items[list->item_count++] = *item;
    list->kept[item->group]++;
    return 1;
}

void
Editor_CmdPalListAddUnseen(
    struct Editor_CmdPalList* list,
    int group,
    int count)
{
    assert(list);
    assert(group >= 0);
    assert(group < EDITOR_CMDPAL_GROUPS);
    assert(count >= 0);
    list->total[group] += count;
}

static void
cmdpal_row_push(
    struct Editor_CmdPalList* list,
    int kind,
    int item,
    int group)
{
    if( list->row_count == list->row_cap )
    {
        int const cap = list->row_cap ? list->row_cap * 2 : 128;
        unsigned char* kinds = ToriMem_Realloc(ToriMem_Editor(), list->row_kind, (size_t)cap);
        int* items = ToriMem_Realloc(ToriMem_Editor(), list->row_item, (size_t)cap * sizeof(int));
        unsigned char* groups = ToriMem_Realloc(ToriMem_Editor(), list->row_group, (size_t)cap);
        assert(kinds);
        assert(items);
        assert(groups);
        list->row_kind = kinds;
        list->row_item = items;
        list->row_group = groups;
        list->row_cap = cap;
    }
    list->row_kind[list->row_count] = (unsigned char)kind;
    list->row_item[list->row_count] = item;
    list->row_group[list->row_count] = (unsigned char)group;
    list->row_count++;
}

int
Editor_CmdPalListFinish(struct Editor_CmdPalList* list)
{
    assert(list);
    list->row_count = 0;
    for( int g = 0; g < EDITOR_CMDPAL_GROUPS; g++ )
    {
        if( list->total[g] == 0 )
            continue;
        cmdpal_row_push(list, EDITOR_CMDPAL_ROW_HEADER, -1, g);
        for( int i = 0; i < list->item_count; i++ )
            if( list->items[i].group == g )
                cmdpal_row_push(list, EDITOR_CMDPAL_ROW_ITEM, i, g);
        if( list->total[g] > list->kept[g] )
            cmdpal_row_push(list, EDITOR_CMDPAL_ROW_FOOTER, -1, g);
    }
    return list->row_count;
}

void
Editor_CmdPalRowText(
    const struct Editor_CmdPalList* list,
    int row,
    char* out,
    int out_size)
{
    int g;

    assert(list);
    assert(row >= 0);
    assert(row < list->row_count);
    assert(out);
    assert(out_size > 0);
    g = list->row_group[row];
    if( list->row_kind[row] == EDITOR_CMDPAL_ROW_HEADER )
    {
        /* The header in the accent, its count right. */
        snprintf(out, (size_t)out_size, "-%c%s%c%c%c%d%c*", TORIRS_CHROME_CELL_SEP, k_group_names[g],
            TORIRS_CHROME_CELL_SEP, TORIRS_CHROME_CELL_SEP, TORIRS_CHROME_CELL_SEP, list->total[g],
            TORIRS_CHROME_CELL_SEP);
        return;
    }
    if( list->row_kind[row] == EDITOR_CMDPAL_ROW_FOOTER )
    {
        snprintf(out, (size_t)out_size, "-%c%c%s: and %d more (type to narrow)", TORIRS_CHROME_CELL_SEP,
            TORIRS_CHROME_CELL_SEP, k_group_names[g], list->total[g] - list->kept[g]);
        return;
    }
    {
        const struct Editor_CmdPalItem* item = &list->items[list->row_item[row]];
        snprintf(out, (size_t)out_size, "-%c%s%c%s%c%s%c%s", TORIRS_CHROME_CELL_SEP, item->label,
            TORIRS_CHROME_CELL_SEP, item->dim, TORIRS_CHROME_CELL_SEP, item->chord, TORIRS_CHROME_CELL_SEP,
            item->right);
    }
}

int
Editor_CmdPalStep(
    const struct Editor_CmdPalList* list,
    int row,
    int delta)
{
    int first = -1;
    int last = -1;

    assert(list);
    assert(delta == 1 || delta == -1);
    for( int r = 0; r < list->row_count; r++ )
        if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM )
        {
            if( first < 0 )
                first = r;
            last = r;
        }
    if( first < 0 )
        return -1;
    if( row < 0 )
        return delta > 0 ? first : last;
    for( int r = row + delta; r >= 0 && r < list->row_count; r += delta )
        if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM )
            return r;
    return delta > 0 ? last : first;
}

int
Editor_CmdPalNextGroup(
    const struct Editor_CmdPalList* list,
    int row)
{
    int const group = row >= 0 && row < list->row_count ? list->row_group[row] : -1;

    assert(list);
    for( int r = 0; r < list->row_count; r++ )
        if( list->row_kind[r] == EDITOR_CMDPAL_ROW_ITEM && (int)list->row_group[r] > group )
            return r;
    /* Past the last group: round to the first. */
    return Editor_CmdPalStep(list, -1, 1);
}

int
Editor_CmdPalMatches(
    const char* text,
    const char* query)
{
    const char* q = query;

    assert(text);
    assert(query);
    for( ;; )
    {
        int len = 0;
        int found = 0;
        while( *q == ' ' )
            q++;
        if( !*q )
            return 1;
        while( q[len] && q[len] != ' ' )
            len++;
        for( const char* t = text; *t && !found; t++ )
        {
            int i = 0;
            while( i < len && t[i] && tolower((unsigned char)t[i]) == tolower((unsigned char)q[i]) )
                i++;
            found = i == len;
        }
        if( !found )
            return 0;
        q += len;
    }
}

/* ---- the module ------------------------------------------------------------------ */

#define CMDPAL_ROWS 16

struct CmdPalettePanel
{
    int panel;
    int input_search;
    int list;
    int label_keys;
    struct Editor_CmdPalList model;
    /** The search the model was built from, and whether it was built. */
    char built_query[TORIRS_CHROME_INPUT_MAX];
    int built;
    uint32_t built_serial;
    int shown_first;
    int open;
};

static struct CmdPalettePanel*
cmdpal_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "cmdpalette");
}

static void
cmdpal_copy(
    char* out,
    int out_size,
    const char* text)
{
    snprintf(out, (size_t)out_size, "%s", text ? text : "");
}

/** The actions that can run from a row: every edge or repeat action but the
 *  palette itself. A held action (the camera, the qualifiers, the click
 *  accelerators) is a key held over the World, not a command. */
static int
cmdpal_action_runs(int action)
{
    const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(action);
    return info->kind != EDITOR_HK_HELD && action != EDITOR_HK_VIEW_COMMAND_PALETTE;
}

static void
cmdpal_gather_actions(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    const char* query)
{
    for( int a = 0; a < EDITOR_HK_COUNT; a++ )
    {
        const struct Editor_HotkeyActionInfo* info = Editor_HotkeyInfo(a);
        struct Editor_CmdPalItem item;
        char haystack[256];

        if( !cmdpal_action_runs(a) )
            continue;
        snprintf(haystack, sizeof(haystack), "%s %s %s %s", info->name, info->doc, info->group, info->id);
        if( !Editor_CmdPalMatches(haystack, query) )
            continue;
        memset(&item, 0, sizeof(item));
        item.group = EDITOR_CMDPAL_ACTIONS;
        item.kind = EDITOR_CMDPAL_KIND_ACTION;
        item.target = a;
        cmdpal_copy(item.label, (int)sizeof(item.label), info->name);
        cmdpal_copy(item.dim, (int)sizeof(item.dim), info->group);
        Editor_ChordDisplay(ui->hotkeys.bind[a].chord[0], item.chord, (int)sizeof(item.chord));
        /* No chord: the right column names the panel the action opens. */
        if( !item.chord[0] && info->panel )
        {
            struct EditorUiPanel* owner = Editor_UiFindPanel(ui, info->panel);
            cmdpal_copy(item.right, (int)sizeof(item.right), owner && owner->title ? owner->title : info->panel);
        }
        (void)Editor_CmdPalListAdd(model, &item);
    }
}

static void
cmdpal_gather_panels(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    const char* query)
{
    for( int i = 0; i < ui->panel_count; i++ )
    {
        const struct EditorUiPanel* panel = &ui->panels[i];
        struct Editor_CmdPalItem item;
        int action;

        if( !panel->title || panel->chrome_panel < 0 || !Editor_CmdPalMatches(panel->title, query) )
            continue;
        memset(&item, 0, sizeof(item));
        item.group = EDITOR_CMDPAL_PANELS;
        item.kind = EDITOR_CMDPAL_KIND_PANEL;
        item.target = i;
        cmdpal_copy(item.label, (int)sizeof(item.label), panel->title);
        cmdpal_copy(item.dim, (int)sizeof(item.dim),
            ui->chrome->panels[panel->chrome_panel].visible || Editor_UiPanelInStrip(ui, panel->name) ? "open"
                                                                                                         : "panel");
        action = Editor_HotkeyForPanel(panel->name);
        if( action >= 0 )
            Editor_ChordDisplay(ui->hotkeys.bind[action].chord[0], item.chord, (int)sizeof(item.chord));
        (void)Editor_CmdPalListAdd(model, &item);
    }
}

/** A typed tile ("3222 3218" or "3222,3218"): one row for the picker's Find. */
static void
cmdpal_gather_tile(
    struct Editor_CmdPalList* model,
    const char* query)
{
    char* end = NULL;
    long x = strtol(query, &end, 10);
    long z;
    struct Editor_CmdPalItem item;

    if( end == query || (*end != ' ' && *end != ',') )
        return;
    while( *end == ' ' || *end == ',' )
        end++;
    if( !isdigit((unsigned char)*end) )
        return;
    z = strtol(end, &end, 10);
    while( *end == ' ' )
        end++;
    if( *end || x < 0 || z < 0 || x > 65535 || z > 65535 )
        return;
    memset(&item, 0, sizeof(item));
    item.group = EDITOR_CMDPAL_SQUARES;
    item.kind = EDITOR_CMDPAL_KIND_SQUARE;
    snprintf(item.label, sizeof(item.label), "%ld %ld", x, z);
    snprintf(item.name, sizeof(item.name), "%ld %ld", x, z);
    snprintf(item.dim, sizeof(item.dim), "tile, m%ld_%ld", x / 64, z / 64);
    (void)Editor_CmdPalListAdd(model, &item);
}

/** One CATALOG_QUERY page of `kind` into `group`: its rows as items, the
 *  rest of its total counted. */
static void
cmdpal_gather_catalog(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    uint32_t kind,
    const char* prefix,
    int group,
    const char* filter)
{
    struct EditorHost_CatalogPage page;

    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&ui->editor->host, kind, filter, TORIRSMAPED_CATALOG_PROGRESS, 0,
            EDITOR_CMDPAL_GROUP_MAX, &page)
        != EDITOR_HOST_OK )
    {
        Editor_HostCatalogPageFree(&page);
        return;
    }
    for( int r = 0; r < page.row_count; r++ )
    {
        const struct EditorHost_CatalogRow* row = &page.rows[r];
        struct Editor_CmdPalItem item;
        memset(&item, 0, sizeof(item));
        item.group = group;
        item.kind = group == EDITOR_CMDPAL_SQUARES ? EDITOR_CMDPAL_KIND_SQUARE : EDITOR_CMDPAL_KIND_RECORD;
        item.target = (int)kind;
        snprintf(item.label, sizeof(item.label), "%s%s", prefix, row->name ? row->name : "");
        cmdpal_copy(item.name, (int)sizeof(item.name), row->name);
        cmdpal_copy(item.dim, (int)sizeof(item.dim), row->display);
        if( row->id >= 0 )
            snprintf(item.right, sizeof(item.right), "%d", (int)row->id);
        (void)Editor_CmdPalListAdd(model, &item);
    }
    if( page.total > (uint32_t)page.row_count )
        Editor_CmdPalListAddUnseen(model, group, (int)page.total - page.row_count);
    Editor_HostCatalogPageFree(&page);
}

static void
cmdpal_gather_accounts(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    const char* query)
{
    const struct Editor_AccountsModel* accounts = Editor_AccountsPanelModel(ui);

    if( !accounts )
        return;
    for( int i = 0; i < accounts->row_count; i++ )
    {
        struct Editor_CmdPalItem item;
        if( !Editor_CmdPalMatches(accounts->rows[i].name, query) )
            continue;
        memset(&item, 0, sizeof(item));
        item.group = EDITOR_CMDPAL_ACCOUNTS;
        item.kind = EDITOR_CMDPAL_KIND_ACCOUNT;
        cmdpal_copy(item.label, (int)sizeof(item.label), accounts->rows[i].name);
        cmdpal_copy(item.name, (int)sizeof(item.name), accounts->rows[i].name);
        cmdpal_copy(item.dim, (int)sizeof(item.dim), "account");
        (void)Editor_CmdPalListAdd(model, &item);
    }
}

static void
cmdpal_gather_scenes(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    const char* query)
{
    struct Editor_SceneList* list = ToriMem_Alloc(ToriMem_Editor(), sizeof(*list));

    assert(list);
    (void)Editor_ScenesList(ui, list);
    for( int i = 0; i < list->count; i++ )
    {
        struct Editor_CmdPalItem item;
        char haystack[EDITOR_CMDPAL_LABEL_MAX + 200];
        snprintf(haystack, sizeof(haystack), "%s %s scene", list->rows[i].name, list->rows[i].summary);
        if( !Editor_CmdPalMatches(haystack, query) )
            continue;
        memset(&item, 0, sizeof(item));
        item.group = EDITOR_CMDPAL_SCENES;
        item.kind = EDITOR_CMDPAL_KIND_SCENE;
        cmdpal_copy(item.label, (int)sizeof(item.label), list->rows[i].name);
        cmdpal_copy(item.name, (int)sizeof(item.name), list->rows[i].name);
        cmdpal_copy(item.dim, (int)sizeof(item.dim), list->rows[i].summary);
        cmdpal_copy(item.right, (int)sizeof(item.right), "enter");
        (void)Editor_CmdPalListAdd(model, &item);
    }
    Editor_CmdPalListAddUnseen(model, EDITOR_CMDPAL_SCENES, list->more);
    ToriMem_Free(ToriMem_Editor(), list);
}

static void
cmdpal_gather_lanes(
    struct EditorUi* ui,
    struct Editor_CmdPalList* model,
    const char* query)
{
    struct Editor_LaneList* list = ToriMem_Alloc(ToriMem_Editor(), sizeof(*list));

    assert(list);
    (void)Editor_LanesList(ui, list);
    for( int i = 0; i < list->count; i++ )
    {
        struct Editor_CmdPalItem item;
        char haystack[EDITOR_CMDPAL_LABEL_MAX + 200];
        snprintf(haystack, sizeof(haystack), "%s lane", list->rows[i].summary);
        if( !Editor_CmdPalMatches(haystack, query) )
            continue;
        memset(&item, 0, sizeof(item));
        item.group = EDITOR_CMDPAL_LANES;
        item.kind = EDITOR_CMDPAL_KIND_LANE;
        cmdpal_copy(item.label, (int)sizeof(item.label), list->rows[i].name);
        cmdpal_copy(item.name, (int)sizeof(item.name), list->rows[i].name);
        cmdpal_copy(item.dim, (int)sizeof(item.dim), list->rows[i].summary + strlen(list->rows[i].name));
        cmdpal_copy(item.right, (int)sizeof(item.right), list->rows[i].is_this ? "this lane" : "switch (next boot)");
        (void)Editor_CmdPalListAdd(model, &item);
    }
    ToriMem_Free(ToriMem_Editor(), list);
}

/** The model for `query`. A kind prefix ("loc:", "npc:", "obj:") asks the
 *  catalog for that kind alone; otherwise every group, the catalog's only
 *  once two letters are typed (an empty query is the table and the panels,
 *  which is what a fresh Ctrl+K shows). */
static void
cmdpal_build(
    struct EditorUi* ui,
    struct CmdPalettePanel* self,
    const char* query)
{
    static const struct
    {
        const char* prefix;
        uint32_t kind;
    } k_kinds[] = {
        { "loc:", TORIRSMAPED_CATALOG_LOC },
        { "npc:", TORIRSMAPED_CATALOG_NPC },
        { "obj:", TORIRSMAPED_CATALOG_OBJ },
    };
    int const catalog = ui->editor != NULL;
    int prefixed = -1;

    Editor_CmdPalListClear(&self->model);
    for( int k = 0; k < (int)(sizeof(k_kinds) / sizeof(k_kinds[0])); k++ )
        if( strncmp(query, k_kinds[k].prefix, strlen(k_kinds[k].prefix)) == 0 )
            prefixed = k;
    if( prefixed >= 0 )
    {
        if( catalog )
            cmdpal_gather_catalog(ui, &self->model, k_kinds[prefixed].kind, k_kinds[prefixed].prefix,
                EDITOR_CMDPAL_RECORDS, query + strlen(k_kinds[prefixed].prefix));
    }
    else
    {
        cmdpal_gather_actions(ui, &self->model, query);
        cmdpal_gather_panels(ui, &self->model, query);
        if( query[0] )
            cmdpal_gather_tile(&self->model, query);
        if( catalog && strlen(query) >= 2 )
        {
            cmdpal_gather_catalog(ui, &self->model, TORIRSMAPED_CATALOG_SQUARE, "", EDITOR_CMDPAL_SQUARES, query);
            for( int k = 0; k < (int)(sizeof(k_kinds) / sizeof(k_kinds[0])); k++ )
                cmdpal_gather_catalog(
                    ui, &self->model, k_kinds[k].kind, k_kinds[k].prefix, EDITOR_CMDPAL_RECORDS, query);
        }
        cmdpal_gather_scenes(ui, &self->model, query);
        cmdpal_gather_lanes(ui, &self->model, query);
        cmdpal_gather_accounts(ui, &self->model, query);
    }
    (void)Editor_CmdPalListFinish(&self->model);
    ToriRSChrome_VListSetCount(ui->chrome, self->list, self->model.row_count);
    ToriRSChrome_VListSetScroll(ui->chrome, self->list, 0);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, Editor_CmdPalStep(&self->model, -1, 1));
    snprintf(self->built_query, sizeof(self->built_query), "%s", query);
    self->built = 1;
    self->built_serial = ui->hotkeys.serial;
    self->shown_first = -1;
}

static void
cmdpal_fill(
    struct EditorUi* ui,
    struct CmdPalettePanel* self)
{
    int first = 0;
    int count = 0;

    (void)ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &count);
    if( first == self->shown_first && !ToriRSChrome_VListNeedsRows(ui->chrome, self->list) )
        return;
    for( int r = first; r < first + count && r < self->model.row_count; r++ )
    {
        char text[TORIRS_CHROME_INPUT_MAX];
        Editor_CmdPalRowText(&self->model, r, text, (int)sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list, r, text);
    }
    self->shown_first = first;
}

static void
cmdpal_refresh(
    struct EditorUi* ui,
    struct CmdPalettePanel* self)
{
    const char* query = ToriRSChrome_Text(ui->chrome, self->input_search);

    if( !query )
        query = "";
    /* Only a changed search (or a rebind, for the chords) rebuilds: the
     * catalog is asked once per keystroke, never once per frame. */
    if( !self->built || strcmp(query, self->built_query) != 0 || self->built_serial != ui->hotkeys.serial )
        cmdpal_build(ui, self, query);
    cmdpal_fill(ui, self);
}

static void
cmdpal_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct CmdPalettePanel* self = opaque;

    /* Closed from the chrome (Escape with nothing focused, the close box). */
    if( self->open && !ui->chrome->panels[self->panel].visible )
        self->open = 0;
    if( !self->open )
        return;
    cmdpal_refresh(ui, self);
}

static void
cmdpal_select(
    struct EditorUi* ui,
    struct CmdPalettePanel* self,
    int row)
{
    int first = 0;
    int count = 0;

    if( row < 0 )
        return;
    ToriRSChrome_VListSetSelected(ui->chrome, self->list, row);
    (void)ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &count);
    if( row < first )
        ToriRSChrome_VListSetScroll(ui->chrome, self->list, row);
    else if( count > 0 && row >= first + count )
        ToriRSChrome_VListSetScroll(ui->chrome, self->list, row - count + 1);
}

static int
cmdpal_run_item(
    struct EditorUi* ui,
    const struct Editor_CmdPalItem* item)
{
    if( item->kind == EDITOR_CMDPAL_KIND_ACTION )
    {
        if( Editor_HotkeyRun(ui, (enum Editor_HotkeyAction)item->target) )
            return 1;
        /* The camera, the eyedroppers, Place at the hovered tile: they act
         * on where the pointer is in the World. */
        Editor_UiStatus(ui, "%s refused from the palette: it acts on the World under the pointer; press its chord there",
            item->label);
        return 0;
    }
    if( item->kind == EDITOR_CMDPAL_KIND_PANEL )
    {
        const struct EditorUiPanel* panel = &ui->panels[item->target];
        if( ui->group_plus1[item->target] )
            Editor_UiGroupShow(ui, panel->name);
        else if( !ui->chrome->panels[panel->chrome_panel].visible )
            (void)Editor_UiTogglePanel(ui, panel->name);
        return 1;
    }
    if( item->kind == EDITOR_CMDPAL_KIND_SQUARE )
    {
        if( !Editor_UiFindPanel(ui, "squares") )
        {
            Editor_UiStatus(ui, "Open %s refused: this window has no Squares panel", item->name);
            return 0;
        }
        if( !Editor_SquarePicker_Find(ui, item->name) )
        {
            Editor_UiStatus(ui, "Open %s refused: the picker's Find matched no square", item->name);
            return 0;
        }
        return 1;
    }
    if( item->kind == EDITOR_CMDPAL_KIND_RECORD )
        return Editor_Catalog_PickRecord(ui, (uint32_t)item->target, item->name);
    if( item->kind == EDITOR_CMDPAL_KIND_SCENE )
        return Editor_Scenes_Enter(ui, item->name);
    if( item->kind == EDITOR_CMDPAL_KIND_LANE )
        return Editor_Lanes_Switch(ui, item->name);
    assert(item->kind == EDITOR_CMDPAL_KIND_ACCOUNT);
    if( !Editor_Accounts_Show(ui, item->name) )
    {
        Editor_UiStatus(ui, "Show %s refused: this window has no Accounts panel", item->name);
        return 0;
    }
    if( !ui->chrome->panels[Editor_UiFindPanel(ui, "accounts")->chrome_panel].visible )
        (void)Editor_UiTogglePanel(ui, "accounts");
    return 1;
}

static int
cmdpal_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct CmdPalettePanel* self = opaque;

    /* Enter in the search box is the Feed's (the dispatcher sees the key on
     * the same frame); a click on a row runs it. */
    if( widget == self->input_search )
        return 1;
    if( widget == self->list )
    {
        int const row = ToriRSChrome_VListSelected(ui->chrome, widget);
        if( row >= 0 && row < self->model.row_count && self->model.row_kind[row] == EDITOR_CMDPAL_ROW_ITEM )
            (void)Editor_CmdPalette_RunRow(ui, row);
        return 1;
    }
    return 0;
}

static void
cmdpal_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct CmdPalettePanel* self = opaque;
    (void)visible;
    /* Never part of a set: Ctrl+K opens it. */
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->open = 0;
}

static void
cmdpal_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct CmdPalettePanel* self = opaque;
    (void)ui;
    Editor_CmdPalListFree(&self->model);
    ToriMem_Free(ToriMem_Editor(), self);
}

int
Editor_CmdPalette_Open(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;

    assert(ui);
    self = cmdpal_self(ui);
    if( !self )
        return 0;
    ToriRSChrome_SetText(ui->chrome, self->input_search, "");
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_search, 0);
    self->open = 1;
    self->built = 0;
    cmdpal_refresh(ui, self);
    return 1;
}

int
Editor_CmdPalette_IsOpen(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;

    assert(ui);
    self = cmdpal_self(ui);
    return self && self->open && ui->chrome->panels[self->panel].visible;
}

void
Editor_CmdPalette_Close(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;

    assert(ui);
    self = cmdpal_self(ui);
    assert(self);
    if( ui->chrome->focus == self->input_search )
        (void)ToriRSChrome_FocusWidget(ui->chrome, -1, 0);
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    self->open = 0;
}

void
Editor_CmdPalette_SetQuery(
    struct EditorUi* ui,
    const char* query)
{
    struct CmdPalettePanel* self;

    assert(ui);
    assert(query);
    self = cmdpal_self(ui);
    assert(self);
    ToriRSChrome_SetText(ui->chrome, self->input_search, query);
    cmdpal_refresh(ui, self);
}

const struct Editor_CmdPalList*
Editor_CmdPalette_List(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;

    assert(ui);
    self = cmdpal_self(ui);
    return self ? &self->model : NULL;
}

int
Editor_CmdPalette_Selected(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;

    assert(ui);
    self = cmdpal_self(ui);
    return self ? ToriRSChrome_VListSelected(ui->chrome, self->list) : -1;
}

int
Editor_CmdPalette_RunRow(
    struct EditorUi* ui,
    int row)
{
    struct CmdPalettePanel* self;
    struct Editor_CmdPalItem item;

    assert(ui);
    self = cmdpal_self(ui);
    assert(self);
    assert(row >= 0);
    assert(row < self->model.row_count);
    if( self->model.row_kind[row] != EDITOR_CMDPAL_ROW_ITEM )
        return 0;
    /* A copy: running it may rebuild the model (a workspace switch, a
     * rebind). */
    item = self->model.items[self->model.row_item[row]];
    Editor_CmdPalette_Close(ui);
    return cmdpal_run_item(ui, &item);
}

int
Editor_CmdPalette_Feed(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* input)
{
    struct CmdPalettePanel* self;
    int row;
    int first = 0;
    int count = 0;

    assert(ui);
    assert(input);
    self = cmdpal_self(ui);
    if( !self || !Editor_CmdPalette_IsOpen(ui) )
        return 0;
    cmdpal_refresh(ui, self);
    row = ToriRSChrome_VListSelected(ui->chrome, self->list);
    (void)ToriRSChrome_VListWindow(ui->chrome, self->list, &first, &count);
    if( input->down[TORIRSK_ESCAPE] )
    {
        Editor_CmdPalette_Close(ui);
        return 1;
    }
    if( input->down[TORIRSK_RETURN] )
    {
        if( row >= 0 && row < self->model.row_count && self->model.row_kind[row] == EDITOR_CMDPAL_ROW_ITEM )
            (void)Editor_CmdPalette_RunRow(ui, row);
        else
            Editor_UiStatus(ui, "Nothing to run: no row matches \"%s\"", self->built_query);
        return 1;
    }
    if( input->down[TORIRSK_DOWN] )
        cmdpal_select(ui, self, Editor_CmdPalStep(&self->model, row, 1));
    else if( input->down[TORIRSK_UP] )
        cmdpal_select(ui, self, Editor_CmdPalStep(&self->model, row, -1));
    else if( input->down[TORIRSK_TAB] )
        cmdpal_select(ui, self, Editor_CmdPalNextGroup(&self->model, row));
    else if( input->down[TORIRSK_PAGE_DOWN] || input->down[TORIRSK_PAGE_UP] )
    {
        int const delta = input->down[TORIRSK_PAGE_DOWN] ? 1 : -1;
        int target = row;
        for( int i = 0; i < (count > 1 ? count - 1 : 1); i++ )
            target = Editor_CmdPalStep(&self->model, target, delta);
        cmdpal_select(ui, self, target);
    }
    cmdpal_fill(ui, self);
    return 1;
}

void
Editor_CmdPalette_Register(struct EditorUi* ui)
{
    struct CmdPalettePanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;

    assert(ui);
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    Editor_CmdPalListInit(&self->model);
    self->shown_first = -1;

    self->panel = ToriRSChrome_ModalAdd(chrome, "Command palette", edp_px(ui, 420));
    assert(self->panel >= 0);
    self->input_search = ToriRSChrome_TextInput(chrome, self->panel, ">", "");
    /* Under TORIRS_CHROME_LABEL_MAX (63 bytes): a longer tooltip is cut. */
    ToriRSChrome_SetTooltip(chrome, self->input_search, "Actions, panels, squares, loc: npc: obj:, scenes, lanes, accts");
    self->list = ToriRSChrome_VListCells(chrome, self->panel, "", CMDPAL_ROWS, 0);
    assert(self->list >= 0);
    /* The keys in words: the Small face has no arrow glyphs. */
    self->label_keys = ToriRSChrome_Label(chrome, self->panel, "Up/Down move  Enter run  Tab next group  Esc close");
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "cmdpalette";
    /* Off the View menu: Ctrl+K and the palette's own action open it. */
    desc.title = NULL;
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = cmdpal_tick;
    desc.activate = cmdpal_activate;
    desc.set_visible = cmdpal_set_visible;
    desc.destroy = cmdpal_destroy;
    Editor_UiAddPanel(ui, &desc);
}
