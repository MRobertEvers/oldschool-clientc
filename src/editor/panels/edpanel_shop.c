/*
 * The Shop builder (module "shop", View > Shop builder; workbench 6.7,
 * design section 11 "Shop builder"): one coin shop -- its owner npc, the op
 * that opens it, the three per-mille multipliers, the stock rows and the
 * prices they produce -- written as the one .rs2 and one .inv
 * tools/gen_shop_scripts.py would write (editor/editor_shop.h).
 *
 *   Shop        a key; Open reads the builder's own pair under
 *               server/scripts/shop/edited/<key>/, else the generator's (the
 *               catalog's [label,<key>_open], read-only), else starts a new
 *               shop
 *   Owner npc   a name; Pick takes the Spawns panel's selected npc, else the
 *               Catalog's npc pick. op: op1..op5 with the owner's menu text
 *               ("op3 Trade", the default)
 *   Sell / Buy / Delta  per mille; general store (allstock=yes), members
 *   stock       a VLIST with a search box: N, obj, count, rate, and what the
 *               shop sells and buys it for at baseline stock (calc_shop_value
 *               over the obj's catalog `cost`); the row editor below it, Set
 *               row, Remove row, + stock, From wiki catalogue... (the content
 *               tree's wiki/shop_stock.csv; disabled with the reason when the
 *               tree has none)
 *   Test        Save, the Live panel's RUN, then a LIVE_CHEAT ::goto that
 *               puts the play account beside the owner's spawn (the Spawns
 *               selection, else an open square's spawn file), then a
 *               LIVE_CHEAT ::talk <owner> <op> that fires the owner's
 *               [opnpc<op>] and so opens the shop (design section 11)
 *   Check       the model's refusals plus what the catalog says: the owner
 *               and every obj exist, the inv is the cache's and big enough
 *               and no other .inv stocks it, no other file declares the
 *               owner's op trigger
 *   Save        both TEXT documents through the authority as ONE stroke (one
 *               undo step), then saved; never a generated pair
 *
 * Every refusal says why (Editor_UiStatus). The panel reads its files only
 * on Open and after the documents change (an undo puts the old pair back and
 * the panel shows it); catalog requests are asked a few a frame.
 *
 * TORIRS_EDITOR_SHOP=<key> (photographs): the panel opens with the editor on
 * that shop.
 */
#include "edpanel_common.h"
#include "edpanels.h"
#include "editor/builder/builder_text.h"
#include "editor/editor_host.h"
#include "editor/editor_shop.h"
#include "editor/live/live_client.h"
#include "editor/live/live_panel.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "torirsmaped/torirs_maped_live.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SHOP_STOCK_ROWS = 8,
    SHOP_PROBLEM_ROWS = 4,
    SHOP_COSTS_MAX = 128,
    /** Catalog records the stock list asks for in one frame. */
    SHOP_COSTS_PER_TICK = 4,
    SHOP_TEXT_MAX = 16384,
    SHOP_PATH_MAX = 1024,
    /** A cost not asked yet / asked and absent. */
    SHOP_COST_UNKNOWN = -1,
    SHOP_COST_ABSENT = -2,
};

struct shop_cost
{
    char obj[EDITOR_SHOP_NAME_MAX];
    int cost;
};

struct ShopPanel
{
    int panel;
    int input_key;
    int button_open;
    int button_new;
    int label_title;
    int input_owner;
    int button_pick_owner;
    int dropdown_op;
    int input_sell;
    int input_buy;
    int input_delta;
    int check_general;
    int check_members;
    int input_inv;
    int input_title;
    int input_find;
    int list_stock;
    int input_obj;
    int button_pick_obj;
    int input_count;
    int input_rate;
    int button_set_row;
    int button_remove_row;
    int button_add_stock;
    int button_wiki;
    int label_prices;
    int button_test;
    int button_check;
    int button_save;
    int list_problems;
    int label_status;

    /** The shop shown; `open` once a key was opened or begun. */
    struct Editor_Shop shop;
    int open;
    /** Where its pair lives (the builder's own keys, or a generated pair's). */
    char rs2_key[EDITOR_DOC_KEY_MAX];
    char inv_key[EDITOR_DOC_KEY_MAX];
    /** The documents' revisions the model was read at (an undo moves them). */
    uint32_t rs2_revision;
    uint32_t inv_revision;

    /** The stock rows the search shows, as model indices. */
    int filtered[EDITOR_SHOP_STOCK_MAX];
    int filtered_count;
    char shown_find[TORIRS_CHROME_INPUT_MAX];
    int rows_dirty;

    struct shop_cost costs[SHOP_COSTS_MAX];
    int cost_count;

    /** The owner whose ops the dropdown names, and the names. */
    char ops_owner[EDITOR_SHOP_NAME_MAX];
    char op_text[5][48];
    const char* op_labels[5];
    int ops_asked;

    char problems[EDITOR_SHOP_ISSUES_MAX][EDITOR_SHOP_TEXT_MAX];
    int problem_count;

    char content_root[SHOP_PATH_MAX];
    int wiki_present;

    /** Test: RUN sent at this run serial; teleport there when it lands. */
    int test_pending;
    int test_run_serial;
    int test_x;
    int test_z;
    int test_level;

    uint32_t hint_serial;
};

static struct ShopPanel*
shop_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "shop");
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void
shop_say(
    struct EditorUi* ui,
    struct ShopPanel* self,
    const char* format,
    ...)
{
    char text[EDITOR_UI_CONSOLE_LINE_MAX];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ToriRSChrome_SetText(ui->chrome, self->label_status, text);
    Editor_UiStatus(ui, "%s", text);
}

static int
shop_catalog(struct EditorUi* ui)
{
    return ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG);
}

static const char*
shop_record_field(
    const struct EditorHost_CatalogRecord* record,
    const char* key)
{
    const char* value = NULL;
    /* The last source wins: a server overlay restates over the cache. */
    for( int i = 0; i < record->field_count; i++ )
    {
        if( record->fields[i].key && strcmp(record->fields[i].key, key) == 0 )
            value = record->fields[i].value;
    }
    return value;
}

/* ---- the documents ---------------------------------------------------------- */

/** Document (TEXT, key)'s text in the mirror, opening it from the authority
 *  when it is not loaded; NULL with *out_length 0 when it has none. */
static const char*
shop_doc_text(
    struct EditorUi* ui,
    const char* key,
    size_t* out_length,
    uint32_t* out_revision)
{
    struct Editor_DocHandle handle;

    *out_length = 0;
    *out_revision = 0;
    if( !ui->editor )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) || !Editor_DocHandleLoaded(&handle) )
    {
        if( !Editor_OpenDoc(ui->editor, EDITOR_DOC_TEXT, key) )
            return NULL;
        if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) )
            return NULL;
    }
    if( !handle.text )
        return NULL;
    *out_length = handle.text->length;
    *out_revision = handle.text->revision;
    return handle.text->text;
}

/** The revision of an open document, 0 when it is not open. */
static uint32_t
shop_doc_revision(
    struct EditorUi* ui,
    const char* key)
{
    struct Editor_DocHandle handle;
    if( !ui->editor || !key[0] || !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text )
        return 0;
    return handle.text->revision;
}

/* Builder_Submit over the session: the builders' way of changing a file. */

static int
shop_submit_apply(
    void* user,
    const struct Editor_Cmd* command)
{
    struct EditorUi* ui = user;
    return ui->editor && Editor_Apply(ui->editor, command);
}

static void
shop_submit_stroke(
    void* user,
    int begin)
{
    struct EditorUi* ui = user;
    if( !ui->editor )
        return;
    if( begin )
        Editor_StrokeBegin(ui->editor);
    else
        Editor_StrokeEnd(ui->editor);
}

static const char*
shop_submit_current(
    void* user,
    enum Editor_DocKind kind,
    const char* key,
    size_t* out_length)
{
    struct EditorUi* ui = user;
    uint32_t revision;

    assert(kind == EDITOR_DOC_TEXT);
    (void)kind;
    if( ui->editor )
        Editor_PumpFacts(ui->editor);
    return shop_doc_text(ui, key, out_length, &revision);
}

/* ---- the widgets ------------------------------------------------------------- */

static void
shop_set_int(
    struct EditorUi* ui,
    int widget,
    int value)
{
    char text[24];
    snprintf(text, sizeof(text), "%d", value);
    ToriRSChrome_SetText(ui->chrome, widget, text);
}

/** A whole decimal number, or `fallback`. */
static int
shop_parse_int(
    const char* text,
    int fallback)
{
    char* end;
    long value;
    if( !text || !text[0] )
        return fallback;
    value = strtol(text, &end, 10);
    if( *end != '\0' || value < -1000000000L || value > 1000000000L )
        return fallback;
    return (int)value;
}

static void
shop_title(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    char text[TORIRS_CHROME_INPUT_MAX];
    if( !self->open )
        snprintf(text, sizeof(text), "No shop: type a key, then Open");
    else if( self->shop.origin == EDITOR_SHOP_ORIGIN_GENERATOR )
        snprintf(text, sizeof(text), "Shop %s \xc2\xb7 generated by gen_shop_scripts.py (read-only)", self->shop.key);
    else
        snprintf(text, sizeof(text), "Shop %s \xc2\xb7 owner: %s", self->shop.key,
            self->shop.owner_count ? self->shop.owners[0].npc : "none yet");
    ToriRSChrome_SetText(ui->chrome, self->label_title, text);
}

/** The model into the widgets (after Open, an undo, a wiki read). */
static void
shop_to_widgets(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    char owners[TORIRS_CHROME_INPUT_MAX] = "";
    const struct Editor_Shop* shop = &self->shop;

    for( int i = 0; i < shop->owner_count; i++ )
    {
        size_t const at = strlen(owners);
        snprintf(owners + at, sizeof(owners) - at, "%s%s", i ? " " : "", shop->owners[i].npc);
    }
    ToriRSChrome_SetText(ui->chrome, self->input_key, shop->key);
    ToriRSChrome_SetText(ui->chrome, self->input_owner, owners);
    shop_set_int(ui, self->input_sell, shop->sell);
    shop_set_int(ui, self->input_buy, shop->buy);
    shop_set_int(ui, self->input_delta, shop->delta);
    ToriRSChrome_SetChecked(ui->chrome, self->check_general, Editor_ShopIsGeneral(shop));
    ToriRSChrome_SetChecked(ui->chrome, self->check_members, shop->members);
    ToriRSChrome_SetText(ui->chrome, self->input_inv, shop->inv);
    ToriRSChrome_SetText(ui->chrome, self->input_title, shop->name);
    if( shop->op >= 1 && shop->op <= 5 )
        ToriRSChrome_DropdownSetSelected(ui->chrome, self->dropdown_op, shop->op - 1);
    self->rows_dirty = 1;
    shop_title(ui, self);
}

/** The widgets into the model, every frame: what the price columns and
 *  Check read is what the fields say now. */
static void
shop_from_widgets(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    struct Editor_Shop* shop = &self->shop;
    const char* owners = ToriRSChrome_Text(ui->chrome, self->input_owner);
    int const sell = shop_parse_int(ToriRSChrome_Text(ui->chrome, self->input_sell), -1);
    int const buy = shop_parse_int(ToriRSChrome_Text(ui->chrome, self->input_buy), -1);
    int const delta = shop_parse_int(ToriRSChrome_Text(ui->chrome, self->input_delta), -1);
    int const general = ToriRSChrome_Checked(ui->chrome, self->check_general);
    int const op = ToriRSChrome_DropdownSelected(ui->chrome, self->dropdown_op) + 1;
    const char* inv = ToriRSChrome_Text(ui->chrome, self->input_inv);
    const char* title = ToriRSChrome_Text(ui->chrome, self->input_title);
    char owner_text[TORIRS_CHROME_INPUT_MAX];
    struct Editor_ShopOwner kept[EDITOR_SHOP_OWNERS_MAX];
    int const kept_count = shop->owner_count < EDITOR_SHOP_OWNERS_MAX ? shop->owner_count : EDITOR_SHOP_OWNERS_MAX;

    if( !self->open || shop->origin == EDITOR_SHOP_ORIGIN_GENERATOR )
        return;
    if( sell != shop->sell || buy != shop->buy || delta != shop->delta )
        self->rows_dirty = 1;
    shop->sell = sell;
    shop->buy = buy;
    shop->delta = delta;
    shop->members = ToriRSChrome_Checked(ui->chrome, self->check_members);
    if( op >= 1 && op <= 5 )
        shop->op = op;
    if( general != Editor_ShopIsGeneral(shop) )
        snprintf(shop->special, sizeof(shop->special), "%s", general ? "General store" : "");
    snprintf(shop->inv, sizeof(shop->inv), "%s", inv ? inv : "");
    snprintf(shop->name, sizeof(shop->name), "%s", title ? title : "");

    /* The owners: space-separated names; each keeps its declared flag. */
    memcpy(kept, shop->owners, sizeof(kept));
    shop->owner_count = 0;
    snprintf(owner_text, sizeof(owner_text), "%s", owners ? owners : "");
    for( char* name = strtok(owner_text, " ,"); name; name = strtok(NULL, " ,") )
    {
        if( !Editor_ShopOwnerAdd(shop, name) )
            continue;
        for( int k = 0; k < kept_count; k++ )
        {
            if( strcmp(kept[k].npc, name) == 0 )
                shop->owners[shop->owner_count - 1].declared = kept[k].declared;
        }
    }
}

/* ---- obj costs ------------------------------------------------------------------ */

static struct shop_cost*
shop_cost_slot(
    struct ShopPanel* self,
    const char* obj)
{
    for( int i = 0; i < self->cost_count; i++ )
    {
        if( strcmp(self->costs[i].obj, obj) == 0 )
            return &self->costs[i];
    }
    if( self->cost_count >= SHOP_COSTS_MAX )
        return NULL;
    snprintf(self->costs[self->cost_count].obj, sizeof(self->costs[0].obj), "%s", obj);
    self->costs[self->cost_count].cost = SHOP_COST_UNKNOWN;
    return &self->costs[self->cost_count++];
}

/** Ask the catalog for `obj`'s record and remember its cost (1, the obj
 *  default, when the record states none). */
static int
shop_fetch_cost(
    struct EditorUi* ui,
    struct shop_cost* slot)
{
    struct EditorHost_CatalogRecord record;
    const char* cost;

    if( !shop_catalog(ui) )
        return SHOP_COST_UNKNOWN;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_OBJ, -1, slot->obj, &record)
        != EDITOR_HOST_OK )
    {
        slot->cost = SHOP_COST_ABSENT;
        return slot->cost;
    }
    cost = shop_record_field(&record, "cost");
    slot->cost = cost ? shop_parse_int(cost, 1) : 1;
    Editor_HostCatalogRecordFree(&record);
    return slot->cost;
}

/* ---- the stock list ----------------------------------------------------------------- */

static void
shop_filter(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( !find )
        find = "";
    self->filtered_count = 0;
    for( int i = 0; i < self->shop.stock_count; i++ )
    {
        if( !find[0] || strstr(self->shop.stock[i].obj, find) )
            self->filtered[self->filtered_count++] = i;
    }
    snprintf(self->shown_find, sizeof(self->shown_find), "%s", find);
    ToriRSChrome_VListSetCount(ui->chrome, self->list_stock, self->open ? self->filtered_count : 0);
}

static void
shop_fill_rows(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    int first;
    int shown;

    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_stock, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->filtered_count; r++ )
    {
        int const index = self->filtered[r];
        const struct Editor_ShopStock* row = &self->shop.stock[index];
        struct shop_cost* slot = shop_cost_slot(self, row->obj);
        char text[TORIRS_CHROME_INPUT_MAX];
        if( slot && slot->cost >= 0 )
            snprintf(text, sizeof(text), "%d \xc2\xb7 %s \xc2\xb7 %d \xc2\xb7 %d    sells at %d gp \xc2\xb7 buys at %d gp",
                index + 1, row->obj, row->count, row->rate, Editor_ShopSellsAt(&self->shop, index, slot->cost),
                Editor_ShopBuysAt(&self->shop, index, slot->cost));
        else
            snprintf(text, sizeof(text), "%d \xc2\xb7 %s \xc2\xb7 %d \xc2\xb7 %d    %s", index + 1, row->obj, row->count,
                row->rate,
                slot && slot->cost == SHOP_COST_ABSENT ? "no such obj"
                : shop_catalog(ui)                      ? "pricing..."
                                                        : "no catalog: no price");
        ToriRSChrome_VListSetRow(ui->chrome, self->list_stock, r, text);
    }
}

static void
shop_fill_problems(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_problems, self->problem_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_problems, &first, &shown) )
        return;
    for( int r = first; r < first + shown && r < self->problem_count; r++ )
        ToriRSChrome_VListSetRow(ui->chrome, self->list_problems, r, self->problems[r]);
}

/* ---- the owner's ops ------------------------------------------------------------------ */

static void
shop_ops(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    const char* owner = self->shop.owner_count ? self->shop.owners[0].npc : "";
    int const selected = self->shop.op >= 1 && self->shop.op <= 5 ? self->shop.op - 1 : 2;

    if( self->ops_asked && strcmp(owner, self->ops_owner) == 0 )
        return;
    snprintf(self->ops_owner, sizeof(self->ops_owner), "%s", owner);
    self->ops_asked = 1;
    for( int op = 0; op < 5; op++ )
        snprintf(self->op_text[op], sizeof(self->op_text[op]), "op%d", op + 1);
    snprintf(self->op_text[2], sizeof(self->op_text[2]), "op3 Trade");
    if( owner[0] && shop_catalog(ui) )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC, -1, owner, &record)
            == EDITOR_HOST_OK )
        {
            for( int op = 0; op < 5; op++ )
            {
                char key[8];
                const char* value;
                snprintf(key, sizeof(key), "op%d", op + 1);
                value = shop_record_field(&record, key);
                if( value && strcmp(value, "hidden") != 0 )
                    snprintf(self->op_text[op], sizeof(self->op_text[op]), "op%d %s", op + 1, value);
                else
                    snprintf(self->op_text[op], sizeof(self->op_text[op]), "op%d (none)", op + 1);
            }
            Editor_HostCatalogRecordFree(&record);
        }
    }
    for( int op = 0; op < 5; op++ )
        self->op_labels[op] = self->op_text[op];
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->dropdown_op, self->op_labels, 5, selected);
}

/* ---- open ------------------------------------------------------------------------- */

/** The generator's pair of `key`, through the catalog's [label,<key>_open]:
 *  its .rs2 and the .inv beside it (<area>/scripts -> <area>/configs). */
static int
shop_find_generated(
    struct EditorUi* ui,
    const char* key,
    char* out_rs2,
    char* out_inv)
{
    struct EditorHost_CatalogPage page;
    char filter[EDITOR_SHOP_KEY_MAX + 16];
    int found = 0;

    if( !shop_catalog(ui) )
        return 0;
    snprintf(filter, sizeof(filter), "label,%s_open", key);
    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, TORIRSMAPED_CATALOG_EXACT,
            0, 0, &page)
        != EDITOR_HOST_OK )
        return 0;
    for( int i = 0; i < page.row_count && !found; i++ )
    {
        const char* file = NULL;
        for( int f = 0; f < page.rows[i].field_count; f++ )
        {
            if( strcmp(page.rows[i].fields[f].key, "file") == 0 )
                file = page.rows[i].fields[f].value;
        }
        if( file && strncmp(file, "server/scripts/", 15) == 0 )
        {
            const char* scripts = strstr(file, "/scripts/");
            const char* last = scripts;
            /* The LAST "/scripts/": server/scripts/shop/<area>/scripts/<k>.rs2. */
            while( last && (scripts = strstr(last + 1, "/scripts/")) != NULL )
                last = scripts;
            if( last && last > file + 14 )
            {
                snprintf(out_rs2, EDITOR_DOC_KEY_MAX, "rs2:%s", file + 15);
                snprintf(out_inv, EDITOR_DOC_KEY_MAX, "rs2:%.*s/configs/%s.inv", (int)(last - (file + 15)), file + 15,
                    key);
                found = 1;
            }
        }
    }
    Editor_HostCatalogPageFree(&page);
    return found;
}

/** Read the pair at the panel's keys into the model. 1 when it parsed. */
static int
shop_read_pair(
    struct EditorUi* ui,
    struct ShopPanel* self,
    char* why,
    size_t why_size)
{
    size_t rs2_length;
    size_t inv_length;
    uint32_t rs2_revision;
    uint32_t inv_revision;
    const char* rs2 = shop_doc_text(ui, self->rs2_key, &rs2_length, &rs2_revision);
    const char* inv = shop_doc_text(ui, self->inv_key, &inv_length, &inv_revision);
    static struct Editor_Shop parsed;

    self->rs2_revision = rs2_revision;
    self->inv_revision = inv_revision;
    if( rs2_length == 0 && inv_length == 0 )
    {
        snprintf(why, why_size, "%s", "absent");
        return -1;
    }
    if( !Editor_ShopParse(&parsed, rs2, rs2_length, inv, inv_length, why, why_size) )
        return 0;
    self->shop = parsed;
    return 1;
}

int
Editor_Shop_Open(
    struct EditorUi* ui,
    const char* key)
{
    struct ShopPanel* self;
    char why[256];
    int read;

    assert(ui);
    assert(key);
    self = shop_self(ui);
    assert(self);
    if( !Editor_ShopKeyValid(key) )
    {
        shop_say(ui, self, "Open refused: '%s' is not a shop key (1-47 of a-z, 0-9 and _)", key);
        return 0;
    }
    if( !ui->editor )
    {
        shop_say(ui, self, "Open refused: this window has no editor session");
        return 0;
    }
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 1);
    Editor_ShopDocKeys(key, self->rs2_key, sizeof(self->rs2_key), self->inv_key, sizeof(self->inv_key));
    read = shop_read_pair(ui, self, why, sizeof(why));
    if( read < 0 && shop_find_generated(ui, key, self->rs2_key, self->inv_key) )
        read = shop_read_pair(ui, self, why, sizeof(why));
    if( read == 0 )
    {
        self->open = 0;
        shop_title(ui, self);
        shop_say(ui, self, "Open refused: %s is not the pair the builder writes: %s", key, why);
        return 0;
    }
    if( read < 0 )
    {
        /* A new shop authored here. */
        Editor_ShopDocKeys(key, self->rs2_key, sizeof(self->rs2_key), self->inv_key, sizeof(self->inv_key));
        Editor_ShopInit(&self->shop, key);
        self->open = 1;
        shop_to_widgets(ui, self);
        shop_say(ui, self, "New shop %s: pick its owner, add stock, then Save", key);
        return 1;
    }
    self->open = 1;
    self->problem_count = 0;
    shop_to_widgets(ui, self);
    if( self->shop.origin == EDITOR_SHOP_ORIGIN_GENERATOR )
        shop_say(ui, self, "Opened %s read-only: %s", key, Editor_ShopSaveRefusal(&self->shop));
    else
        shop_say(ui, self, "Opened shop %s: %d stock rows", key, self->shop.stock_count);
    return 1;
}

/* ---- check ---------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void
shop_problem(
    struct ShopPanel* self,
    const char* format,
    ...)
{
    va_list args;
    if( self->problem_count >= EDITOR_SHOP_ISSUES_MAX )
        return;
    va_start(args, format);
    vsnprintf(self->problems[self->problem_count], sizeof(self->problems[0]), format, args);
    va_end(args);
    self->problem_count++;
}

/** The model's refusals and the catalog's. Returns the problem count. */
static int
shop_check(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    struct Editor_Shop* shop = &self->shop;
    const char* rs2_path = self->rs2_key + 4;

    shop_from_widgets(ui, self);
    self->problem_count = Editor_ShopCheck(shop, self->problems, EDITOR_SHOP_ISSUES_MAX);
    if( !shop_catalog(ui) )
    {
        shop_problem(self, "No catalog: the owner, the objs and the inv are not checked.");
        return self->problem_count;
    }
    for( int i = 0; i < shop->owner_count; i++ )
    {
        struct EditorHost_CatalogRecord record;
        struct EditorHost_CatalogPage page;
        char key[8];
        char header[EDITOR_SHOP_NAME_MAX + 16];
        const char* op_text;

        if( !Editor_ShopNameValid(shop->owners[i].npc) )
            continue;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC, -1, shop->owners[i].npc, &record)
            != EDITOR_HOST_OK )
        {
            shop_problem(self, "Owner %s is not an npc the tree defines.", shop->owners[i].npc);
            continue;
        }
        snprintf(key, sizeof(key), "op%d", shop->op);
        op_text = shop_record_field(&record, key);
        if( !op_text || strcmp(op_text, "hidden") == 0 )
            shop_problem(self, "%s has no op%d menu text: players see no option (Record panel).", shop->owners[i].npc,
                shop->op);
        Editor_HostCatalogRecordFree(&record);

        /* Another file declaring the owner's op trigger is a compile error. */
        snprintf(header, sizeof(header), "opnpc%d,%s", shop->op, shop->owners[i].npc);
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&ui->editor->host, TORIRSMAPED_CATALOG_TRIGGER, header,
                TORIRSMAPED_CATALOG_EXACT, 0, 0, &page)
            == EDITOR_HOST_OK )
        {
            for( int r = 0; r < page.row_count; r++ )
            {
                const char* file = "";
                const char* line = "?";
                for( int f = 0; f < page.rows[r].field_count; f++ )
                {
                    if( strcmp(page.rows[r].fields[f].key, "file") == 0 )
                        file = page.rows[r].fields[f].value;
                    else if( strcmp(page.rows[r].fields[f].key, "line") == 0 )
                        line = page.rows[r].fields[f].value;
                }
                if( strncmp(file, "server/scripts/", 15) == 0 && strcmp(file + 15, rs2_path) == 0 )
                    continue;
                shop_problem(self, "[%s] is already declared in %s:%s; a second one will not compile.", header, file,
                    line);
            }
            Editor_HostCatalogPageFree(&page);
        }
    }
    for( int i = 0; i < shop->stock_count; i++ )
    {
        struct shop_cost* slot = shop_cost_slot(self, shop->stock[i].obj);
        if( slot && slot->cost == SHOP_COST_UNKNOWN )
            (void)shop_fetch_cost(ui, slot);
        if( slot && slot->cost == SHOP_COST_ABSENT )
            shop_problem(self, "stock%d: %s is not an obj the tree defines.", i + 1, shop->stock[i].obj);
    }
    if( Editor_ShopNameValid(shop->inv) )
    {
        struct EditorHost_CatalogRecord record;
        memset(&record, 0, sizeof(record));
        if( Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_INV, -1, shop->inv, &record)
            != EDITOR_HOST_OK )
            shop_problem(self, "inv %s is not the cache's: bind a cache inv (SHOPS_PLAN 2.3).", shop->inv);
        else
        {
            const char* size = shop_record_field(&record, "size");
            int const slots = size ? shop_parse_int(size, -1) : -1;
            if( slots >= 0 && shop->stock_count > slots )
                shop_problem(self, "%d stock rows overflow inv %s's %d slots (SHOPS_PLAN 2.4).", shop->stock_count,
                    shop->inv, slots);
            for( int s = 0; s < record.source_count; s++ )
            {
                const char* path = record.sources[s].path;
                if( record.sources[s].provenance != TORIRSMAPED_PROV_OVERLAY || strncmp(path, "server/scripts/", 15) != 0 )
                    continue;
                if( strcmp(path + 15, self->inv_key + 4) == 0 )
                    continue;
                shop_problem(self, "inv %s is already stocked by %s.", shop->inv, path);
            }
            Editor_HostCatalogRecordFree(&record);
        }
    }
    self->rows_dirty = 1;
    return self->problem_count;
}

int
Editor_Shop_Check(struct EditorUi* ui)
{
    struct ShopPanel* self;
    int count;

    assert(ui);
    self = shop_self(ui);
    assert(self);
    if( !self->open )
    {
        shop_say(ui, self, "Check refused: no shop is open (type a key, then Open)");
        return -1;
    }
    count = shop_check(ui, self);
    shop_fill_problems(ui, self);
    if( count == 0 )
        shop_say(ui, self, "Check: shop %s is ready (%d stock rows)", self->shop.key, self->shop.stock_count);
    else
        shop_say(ui, self, "Check: %d problem(s); first: %s", count, self->problems[0]);
    return count;
}

/* ---- save ------------------------------------------------------------------------ */

int
Editor_Shop_Save(struct EditorUi* ui)
{
    struct ShopPanel* self;
    struct Builder_Submit submit;
    char* text;
    enum Builder_SyncResult rs2_result;
    enum Builder_SyncResult inv_result = BUILDER_SYNC_UNCHANGED;
    size_t length;
    int saved = 0;
    int applied_before;
    const char* refusal;

    assert(ui);
    self = shop_self(ui);
    assert(self);
    if( !self->open )
    {
        shop_say(ui, self, "Save refused: no shop is open (type a key, then Open)");
        return 0;
    }
    refusal = Editor_ShopSaveRefusal(&self->shop);
    if( refusal )
    {
        shop_say(ui, self, "Save refused: %s %s", self->shop.key, refusal);
        return 0;
    }
    if( !ui->editor || !ui->editor->writable )
    {
        shop_say(ui, self, "Save refused: the editor server is read-only");
        return 0;
    }
    /* Check first: a shop the model or the catalog refuses (a second
     * declaration of the owner's trigger is a compile error) is not
     * written. Without a catalog only the model's refusals count. */
    if( shop_check(ui, self) > 0 && !(self->problem_count == 1 && !shop_catalog(ui)) )
    {
        shop_fill_problems(ui, self);
        shop_say(ui, self, "Save refused: %s", self->problems[0]);
        return 0;
    }
    Editor_ShopDocKeys(self->shop.key, self->rs2_key, sizeof(self->rs2_key), self->inv_key, sizeof(self->inv_key));

    submit.user_data = ui;
    submit.apply = shop_submit_apply;
    submit.stroke = shop_submit_stroke;
    submit.current = shop_submit_current;
    text = ToriMem_Alloc(ToriMem_Editor(), SHOP_TEXT_MAX);
    assert(text);
    /* Both files in ONE stroke: one History row, one undo. */
    applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
    submit.stroke(ui, 1);
    length = Editor_ShopEmitRs2(&self->shop, EDITOR_SHOP_MARK, text, SHOP_TEXT_MAX);
    assert(length < SHOP_TEXT_MAX);
    rs2_result = Builder_TextSync(&submit, self->rs2_key, text, length, 1);
    if( rs2_result != BUILDER_SYNC_REFUSED )
    {
        length = Editor_ShopEmitInv(&self->shop, EDITOR_SHOP_MARK, text, SHOP_TEXT_MAX);
        assert(length < SHOP_TEXT_MAX);
        inv_result = Builder_TextSync(&submit, self->inv_key, text, length, 1);
    }
    submit.stroke(ui, 0);
    ToriMem_Free(ToriMem_Editor(), text);
    if( rs2_result == BUILDER_SYNC_REFUSED || inv_result == BUILDER_SYNC_REFUSED )
    {
        /* Half a splice plan is not a shop (Save all would write it, and
         * Open cannot read it back as the pair): the step goes back whole,
         * as the instance helper's does. */
        if( Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor)) > applied_before )
            (void)Editor_Undo(ui->editor);
        shop_say(ui, self, "Save refused: another window changed %s first; nothing was applied; Open it again",
            self->shop.key);
        return 0;
    }
    Editor_PumpFacts(ui->editor);
    if( Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, self->rs2_key) > 0 )
        saved++;
    if( Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, self->inv_key) > 0 )
        saved++;
    self->rs2_revision = shop_doc_revision(ui, self->rs2_key);
    self->inv_revision = shop_doc_revision(ui, self->inv_key);
    self->problem_count = 0;
    shop_fill_problems(ui, self);
    shop_title(ui, self);
    if( rs2_result == BUILDER_SYNC_UNCHANGED && inv_result == BUILDER_SYNC_UNCHANGED && saved == 0 )
        shop_say(ui, self, "Save: shop %s is already what is on disk", self->shop.key);
    else
        shop_say(ui, self, "Saved shop %s: server/scripts/%s and its .inv (one undo step)", self->shop.key,
            self->rs2_key + 4);
    return 1;
}

/* ---- test ---------------------------------------------------------------------------- */

/** Where the owner stands: the Spawns selection's row of it, else a row in
 *  an open square's spawn file. */
static int
shop_owner_tile(
    struct EditorUi* ui,
    struct ShopPanel* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct Editor_SpawnRow rows[16];
    int const selected = Editor_Spawns_Selection(ui, rows, 16);
    const struct Editor_Doc* doc = &ui->editor->doc;

    for( int o = 0; o < self->shop.owner_count; o++ )
    {
        const char* owner = self->shop.owners[o].npc;
        for( int i = 0; i < selected && i < 16; i++ )
        {
            if( !rows[i].is_obj && !rows[i].suppress && strcmp(rows[i].name, owner) == 0 )
            {
                *out_x = rows[i].abs_x;
                *out_z = rows[i].abs_z;
                *out_level = rows[i].level;
                return 1;
            }
        }
        for( int s = 0; s < doc->spawn_count; s++ )
        {
            const struct Editor_SpawnFile* file = &doc->spawns[s]->file;
            for( int l = 0; l < file->line_count; l++ )
            {
                const struct Editor_SpawnLine* line = &file->lines[l];
                if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.is_obj || line->row.suppress
                    || strcmp(line->row.name, owner) != 0 )
                    continue;
                *out_x = line->row.abs_x;
                *out_z = line->row.abs_z;
                *out_level = line->row.level;
                return 1;
            }
        }
    }
    return 0;
}

int
Editor_Shop_Test(struct EditorUi* ui)
{
    struct ShopPanel* self;
    int run;

    assert(ui);
    self = shop_self(ui);
    assert(self);
    if( !self->open )
    {
        shop_say(ui, self, "Test refused: no shop is open (type a key, then Open)");
        return 0;
    }
    if( !Editor_LivePanelModel(ui) )
    {
        shop_say(ui, self, "Test refused: this window has no Live panel to RUN through");
        return 0;
    }
    shop_from_widgets(ui, self);
    if( !shop_owner_tile(ui, self, &self->test_x, &self->test_z, &self->test_level) )
    {
        shop_say(ui, self, "Test refused: no spawn of %s here; select it in Spawns or open its square",
            self->shop.owner_count ? self->shop.owners[0].npc : "the owner");
        return 0;
    }
    if( self->shop.origin != EDITOR_SHOP_ORIGIN_GENERATOR && !Editor_Shop_Save(ui) )
        return 0;
    run = Editor_Live_Run(ui);
    if( run <= 0 )
    {
        shop_say(ui, self, "Test refused: RUN did not start (a RUN or build is in flight; see Live)");
        return 0;
    }
    self->test_pending = 1;
    self->test_run_serial = Editor_LivePanelModel(ui)->run_serial;
    shop_say(ui, self, "Test: RUN started; the play account goes to %s once it lands",
        self->shop.owners[0].npc);
    return 1;
}

/** RUN landed: put the play account beside the owner, then open the shop. */
static void
shop_test_landed(
    struct EditorUi* ui,
    struct ShopPanel* self,
    const struct Editor_LiveModel* live)
{
    char line[96];
    const char* owner = self->shop.owner_count ? self->shop.owners[0].npc : "the owner";

    self->test_pending = 0;
    if( live->run_status != EDITOR_HOST_OK )
    {
        shop_say(ui, self, "Test: RUN stopped at %s: %s", ToriRSMapEd_RunStepName(live->run_last_step),
            live->run_message);
        return;
    }
    if( !live->play_account[0] )
    {
        shop_say(ui, self, "Test: RUN done, but the lane has no play account to send to %s", owner);
        return;
    }
    /* ::goto is the engine's own teleport (torirs_server_world.c); one tile
     * east of the spawn, so the owner is the npc in reach. */
    snprintf(line, sizeof(line), "goto %d %d %d", self->test_x + 1, self->test_z, self->test_level);
    /* Through the Live panel (its console logs the cheat, as its own); it
     * sends one only to an attached lane. */
    if( live->state != TORIRSMAPED_LIVE_ATTACHED )
    {
        shop_say(ui, self, "Test: RUN done, but the live lane is not attached (Live: Attach); teleport %s by hand to %d,%d",
            live->play_account, self->test_x, self->test_z);
        return;
    }
    if( Editor_Live_Cheat(ui, live->play_account, line) <= 0 )
    {
        shop_say(ui, self, "Test: RUN done, but the live link is gone; teleport by hand to %d,%d", self->test_x,
            self->test_z);
        return;
    }
    /* Then open the shop (design section 11: "RUN and open the shop"):
     * ::talk <npc> <op> is the engine's own way to fire [opnpc<op>] on an
     * npc without the right-click (torirs_server_world.c), and the owner's
     * [opnpc<op>] is the trigger this builder wrote to open the shop. */
    if( self->shop.owner_count && self->shop.op >= 1 && self->shop.op <= 5 )
    {
        snprintf(line, sizeof(line), "talk %s %d", owner, self->shop.op);
        if( Editor_Live_Cheat(ui, live->play_account, line) > 0 )
        {
            shop_say(ui, self, "Test: %s is beside %s at %d,%d and its %s opened the shop (::%s)", live->play_account,
                owner, self->test_x, self->test_z, self->shop.op == 3 ? "Trade" : self->op_text[self->shop.op - 1],
                line);
            return;
        }
    }
    shop_say(ui, self, "Test: %s is beside %s at %d,%d; right-click them and choose %s", live->play_account, owner,
        self->test_x, self->test_z, self->shop.op == 3 ? "Trade" : self->op_text[self->shop.op - 1]);
}

/* ---- stock edits ---------------------------------------------------------------------- */

static int
shop_selected_row(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    int const r = ToriRSChrome_VListSelected(ui->chrome, self->list_stock);
    if( r < 0 || r >= self->filtered_count )
        return -1;
    return self->filtered[r];
}

static int
shop_editable(
    struct EditorUi* ui,
    struct ShopPanel* self,
    const char* what)
{
    if( !self->open )
    {
        shop_say(ui, self, "%s refused: no shop is open (type a key, then Open)", what);
        return 0;
    }
    if( self->shop.origin == EDITOR_SHOP_ORIGIN_GENERATOR )
    {
        shop_say(ui, self, "%s refused: %s %s", what, self->shop.key, Editor_ShopSaveRefusal(&self->shop));
        return 0;
    }
    return 1;
}

/** The row editor's fields: 1 with a valid obj, count and rate. */
static int
shop_row_fields(
    struct EditorUi* ui,
    struct ShopPanel* self,
    const char* what,
    struct Editor_ShopStock* out)
{
    const char* obj = ToriRSChrome_Text(ui->chrome, self->input_obj);
    out->count = shop_parse_int(ToriRSChrome_Text(ui->chrome, self->input_count), -1);
    out->rate = shop_parse_int(ToriRSChrome_Text(ui->chrome, self->input_rate), -1);
    if( !obj || !Editor_ShopNameValid(obj) )
    {
        shop_say(ui, self, "%s refused: name the obj (a gameval, or Pick from the Catalog)", what);
        return 0;
    }
    if( out->count < 1 || out->rate < 1 )
    {
        shop_say(ui, self, "%s refused: count and rate must be whole numbers of at least 1", what);
        return 0;
    }
    snprintf(out->obj, sizeof(out->obj), "%s", obj);
    return 1;
}

static int
shop_wiki(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    char path[SHOP_PATH_MAX + 64];
    FILE* f;
    long size;
    char* csv;
    struct Editor_ShopStock rows[EDITOR_SHOP_STOCK_MAX];
    int skipped;
    int count;

    if( !shop_editable(ui, self, "From wiki catalogue") )
        return 0;
    snprintf(path, sizeof(path), "%s/" EDITOR_SHOP_WIKI_STOCK, self->content_root);
    f = self->content_root[0] ? fopen(path, "rb") : NULL;
    if( !f )
    {
        shop_say(ui, self, "From wiki catalogue refused: the tree has no %s", EDITOR_SHOP_WIKI_STOCK);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    csv = ToriMem_Alloc(ToriMem_Editor(), (size_t)(size > 0 ? size : 0) + 1);
    assert(csv);
    if( size < 0 || fread(csv, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    csv[size] = '\0';
    fclose(f);
    count = Editor_ShopWikiStock(csv, (size_t)size, self->shop.key, rows, EDITOR_SHOP_STOCK_MAX, &skipped);
    ToriMem_Free(ToriMem_Editor(), csv);
    if( count < 0 )
    {
        shop_say(ui, self, "From wiki catalogue refused: %s has no shop_key/obj_gameval header", EDITOR_SHOP_WIKI_STOCK);
        return 0;
    }
    if( count == 0 )
    {
        shop_say(ui, self, "From wiki catalogue: no reviewed rows for %s (%d skipped)", self->shop.key, skipped);
        return 0;
    }
    memcpy(self->shop.stock, rows, sizeof(rows[0]) * (size_t)count);
    self->shop.stock_count = count;
    self->rows_dirty = 1;
    shop_say(ui, self, "From wiki catalogue: %d rows for %s (%d needing review skipped); Save writes them", count,
        self->shop.key, skipped);
    return count;
}

/** A name for the owner or an obj from the selection or the Catalog pick. */
static int
shop_pick(
    struct EditorUi* ui,
    int want_kind,
    char* out,
    size_t out_size)
{
    struct EditorHost_CatalogRecord record;

    if( want_kind == TORIRSMAPED_CATALOG_NPC )
    {
        struct Editor_SpawnRow rows[1];
        if( Editor_Spawns_Selection(ui, rows, 1) > 0 && !rows[0].is_obj )
        {
            snprintf(out, out_size, "%s", rows[0].name);
            return 1;
        }
    }
    if( ui->tools.pick_kind != (int)want_kind || !shop_catalog(ui) )
        return 0;
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&ui->editor->host, (uint32_t)want_kind, ui->tools.pick_id, "", &record)
        != EDITOR_HOST_OK )
        return 0;
    snprintf(out, out_size, "%s", record.name ? record.name : "");
    Editor_HostCatalogRecordFree(&record);
    return out[0] != '\0';
}

/* ---- the frame -------------------------------------------------------------------------- */

static void
shop_hints(
    struct EditorUi* ui,
    struct ShopPanel* self)
{
    edp_button_chord(ui, self->button_test, EDITOR_HK_FILE_RUN);
    edp_button_chord(ui, self->button_check, EDITOR_HK_FILE_CHECK_SCRIPTS);
    edp_button_chord(ui, self->button_save, EDITOR_HK_FILE_SAVE_ALL);
    self->hint_serial = ui->hotkeys.serial;
}

static void
shop_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct ShopPanel* self = opaque;
    const char* find;
    int fetched = 0;

    if( self->hint_serial != ui->hotkeys.serial )
        shop_hints(ui, self);
    if( !ui->chrome->panels[self->panel].visible )
        return;
    ToriRSChrome_SetDisabled(ui->chrome, self->button_wiki, !self->wiki_present);
    if( !self->open )
        return;

    /* An undo, a redo or another window moved the pair: show what it holds. */
    if( shop_doc_revision(ui, self->rs2_key) != self->rs2_revision
        || shop_doc_revision(ui, self->inv_key) != self->inv_revision )
    {
        char why[256];
        int const read = shop_read_pair(ui, self, why, sizeof(why));
        if( read > 0 )
            shop_to_widgets(ui, self);
        else if( read < 0 && self->shop.origin == EDITOR_SHOP_ORIGIN_EDITOR )
            shop_say(ui, self, "Shop %s's files are gone (undone?); Save writes them again", self->shop.key);
    }
    shop_from_widgets(ui, self);
    shop_ops(ui, self);

    for( int i = 0; i < self->shop.stock_count && fetched < SHOP_COSTS_PER_TICK; i++ )
    {
        struct shop_cost* slot = shop_cost_slot(self, self->shop.stock[i].obj);
        if( slot && slot->cost == SHOP_COST_UNKNOWN && shop_catalog(ui) )
        {
            (void)shop_fetch_cost(ui, slot);
            fetched++;
            self->rows_dirty = 1;
        }
    }
    find = ToriRSChrome_Text(ui->chrome, self->input_find);
    if( self->rows_dirty || strcmp(find ? find : "", self->shown_find) != 0 )
    {
        shop_filter(ui, self);
        ToriRSChrome_VListSetScroll(ui->chrome, self->list_stock, 0);
        shop_fill_rows(ui, self);
        self->rows_dirty = 0;
    }
    else if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_stock) )
        shop_fill_rows(ui, self);
    if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_problems) )
        shop_fill_problems(ui, self);

    if( self->test_pending )
    {
        const struct Editor_LiveModel* live = Editor_LivePanelModel(ui);
        if( live && live->run_serial != self->test_run_serial )
            shop_test_landed(ui, self, live);
    }
}

static int
shop_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct ShopPanel* self = opaque;

    if( widget == self->button_open || widget == self->input_key )
    {
        const char* key = ToriRSChrome_Text(ui->chrome, self->input_key);
        (void)Editor_Shop_Open(ui, key ? key : "");
        return 1;
    }
    if( widget == self->button_new )
    {
        const char* key = ToriRSChrome_Text(ui->chrome, self->input_key);
        char rs2[EDITOR_DOC_KEY_MAX];
        char inv[EDITOR_DOC_KEY_MAX];
        size_t length;
        uint32_t revision;
        if( !key || !Editor_ShopKeyValid(key) )
        {
            shop_say(ui, self, "New refused: type a key first (1-47 of a-z, 0-9 and _)");
            return 1;
        }
        Editor_ShopDocKeys(key, rs2, sizeof(rs2), inv, sizeof(inv));
        if( shop_doc_text(ui, rs2, &length, &revision) && length > 0 )
        {
            shop_say(ui, self, "New refused: shop %s exists; Open it", key);
            return 1;
        }
        snprintf(self->rs2_key, sizeof(self->rs2_key), "%s", rs2);
        snprintf(self->inv_key, sizeof(self->inv_key), "%s", inv);
        self->rs2_revision = shop_doc_revision(ui, rs2);
        self->inv_revision = shop_doc_revision(ui, inv);
        Editor_ShopInit(&self->shop, key);
        self->open = 1;
        shop_to_widgets(ui, self);
        shop_say(ui, self, "New shop %s: pick its owner, add stock, then Save", key);
        return 1;
    }
    if( widget == self->button_pick_owner || widget == self->button_pick_obj )
    {
        int const owner = widget == self->button_pick_owner;
        char name[EDITOR_SHOP_NAME_MAX];
        if( !shop_editable(ui, self, "Pick") )
            return 1;
        if( !shop_pick(ui, owner ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_OBJ, name, sizeof(name)) )
        {
            shop_say(ui, self, "Pick refused: select %s in the Catalog%s first", owner ? "an npc" : "an obj",
                owner ? " or Spawns" : "");
            return 1;
        }
        ToriRSChrome_SetText(ui->chrome, owner ? self->input_owner : self->input_obj, name);
        return 1;
    }
    if( widget == self->list_stock )
    {
        int const index = shop_selected_row(ui, self);
        if( index >= 0 )
        {
            ToriRSChrome_SetText(ui->chrome, self->input_obj, self->shop.stock[index].obj);
            shop_set_int(ui, self->input_count, self->shop.stock[index].count);
            shop_set_int(ui, self->input_rate, self->shop.stock[index].rate);
        }
        return 1;
    }
    if( widget == self->button_add_stock || widget == self->button_set_row )
    {
        int const add = widget == self->button_add_stock;
        struct Editor_ShopStock row;
        int index;
        if( !shop_editable(ui, self, add ? "+ stock" : "Set row") || !shop_row_fields(ui, self, add ? "+ stock" : "Set row", &row) )
            return 1;
        index = add ? Editor_ShopStockAdd(&self->shop, row.obj, row.count, row.rate) : shop_selected_row(ui, self);
        if( index < 0 )
        {
            shop_say(ui, self, add ? "+ stock refused: the shop holds %d rows already" : "Set row refused: pick a row first",
                EDITOR_SHOP_STOCK_MAX);
            return 1;
        }
        self->shop.stock[index] = row;
        self->rows_dirty = 1;
        shop_say(ui, self, "stock%d = %s,%d,%d; Save writes it", index + 1, row.obj, row.count, row.rate);
        return 1;
    }
    if( widget == self->button_remove_row )
    {
        int const index = shop_selected_row(ui, self);
        if( !shop_editable(ui, self, "Remove row") )
            return 1;
        if( index < 0 )
        {
            shop_say(ui, self, "Remove row refused: pick a row first");
            return 1;
        }
        Editor_ShopStockRemove(&self->shop, index);
        self->rows_dirty = 1;
        shop_say(ui, self, "Removed stock%d; Save writes the rest renumbered", index + 1);
        return 1;
    }
    if( widget == self->button_wiki )
    {
        (void)shop_wiki(ui, self);
        return 1;
    }
    if( widget == self->button_check )
    {
        (void)Editor_Shop_Check(ui);
        return 1;
    }
    if( widget == self->button_save )
    {
        (void)Editor_Shop_Save(ui);
        return 1;
    }
    if( widget == self->button_test )
    {
        (void)Editor_Shop_Test(ui);
        return 1;
    }
    if( widget == self->dropdown_op || widget == self->check_general || widget == self->check_members
        || widget == self->input_owner || widget == self->input_sell || widget == self->input_buy
        || widget == self->input_delta || widget == self->input_inv || widget == self->input_title
        || widget == self->input_find || widget == self->input_obj || widget == self->input_count
        || widget == self->input_rate || widget == self->list_problems )
    {
        self->rows_dirty = 1;
        return 1;
    }
    return 0;
}

static void
shop_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct ShopPanel* self = opaque;
    const char* key = getenv("TORIRS_EDITOR_SHOP");

    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, 0);
    if( visible && key && key[0] && ui->editor )
        (void)Editor_Shop_Open(ui, key);
}

static void
shop_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), opaque);
}

void
Editor_Shop_SetPaths(
    struct EditorUi* ui,
    const char* content_root)
{
    struct ShopPanel* self;
    char path[SHOP_PATH_MAX + 64];
    FILE* f;

    assert(ui);
    assert(content_root);
    self = shop_self(ui);
    if( !self )
        return;
    snprintf(self->content_root, sizeof(self->content_root), "%s", content_root);
    snprintf(path, sizeof(path), "%s/" EDITOR_SHOP_WIKI_STOCK, content_root);
    f = fopen(path, "rb");
    self->wiki_present = f != NULL;
    if( f )
        fclose(f);
    ToriRSChrome_SetTooltip(ui->chrome, self->button_wiki,
        self->wiki_present ? "replace the stock with the wiki's reviewed rows"
                           : "disabled: the tree has no wiki/shop_stock.csv");
}

const struct Editor_Shop*
Editor_Shop_Model(struct EditorUi* ui)
{
    struct ShopPanel* self;
    assert(ui);
    self = shop_self(ui);
    return self && self->open ? &self->shop : NULL;
}

int
Editor_Shop_ProblemCount(struct EditorUi* ui)
{
    struct ShopPanel* self;
    assert(ui);
    self = shop_self(ui);
    assert(self);
    return self->problem_count;
}

const char*
Editor_Shop_Problem(
    struct EditorUi* ui,
    int index)
{
    struct ShopPanel* self;
    assert(ui);
    self = shop_self(ui);
    assert(self);
    assert(index >= 0);
    assert(index < self->problem_count);
    return self->problems[index];
}

void
Editor_Shop_Register(struct EditorUi* ui)
{
    struct ShopPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    static const char* const k_ops[] = { "op1", "op2", "op3 Trade", "op4", "op5" };

    assert(ui);
    if( !ui->editor )
        return;
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->hint_serial = UINT32_MAX;
    self->shop.op = 3;

    self->panel = ToriRSChrome_PanelAdd(
        chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, EDP_COL_X + 16), edp_px(ui, EDP_TOP_Y), edp_px(ui, 400),
        "Shop builder");
    assert(self->panel >= 0);
    self->input_key = ToriRSChrome_TextInput(chrome, self->panel, "Shop", "");
    ToriRSChrome_SetTooltip(chrome, self->input_key, "the shop key: a-z, 0-9 and _");
    self->button_open = ToriRSChrome_Button(chrome, self->panel, "Open");
    ToriRSChrome_SetTooltip(chrome, self->button_open, "read the shop's .rs2 and .inv");
    self->button_new = ToriRSChrome_Button(chrome, self->panel, "New");
    ToriRSChrome_SetTooltip(chrome, self->button_new, "begin a shop under shop/edited/<key>/");
    self->label_title = ToriRSChrome_Label(chrome, self->panel, "No shop: type a key, then Open");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_owner = ToriRSChrome_TextInput(chrome, self->panel, "Owner npc", "");
    ToriRSChrome_SetTooltip(chrome, self->input_owner, "npc names, space-separated; each opens the shop");
    self->button_pick_owner = ToriRSChrome_Button(chrome, self->panel, "Pick owner");
    ToriRSChrome_SetTooltip(chrome, self->button_pick_owner, "the Spawns selection's npc, else the Catalog's");
    self->dropdown_op = ToriRSChrome_Dropdown(chrome, self->panel, "op", k_ops, 5, 2);
    self->input_sell = ToriRSChrome_TextInput(chrome, self->panel, "Sell \xe2\x80\xb0", "1300");
    ToriRSChrome_SetTooltip(chrome, self->input_sell, "what the player pays, per mille of the obj's cost");
    self->input_buy = ToriRSChrome_TextInput(chrome, self->panel, "Buy \xe2\x80\xb0", "400");
    ToriRSChrome_SetTooltip(chrome, self->input_buy, "what the shop pays, per mille of the obj's cost");
    self->input_delta = ToriRSChrome_TextInput(chrome, self->panel, "Delta \xe2\x80\xb0", "30");
    ToriRSChrome_SetTooltip(chrome, self->input_delta, "per mille the price moves per unit off baseline");
    self->check_general = ToriRSChrome_Checkbox(chrome, self->panel, "general store", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_general, "accepts every tradeable obj (allstock=yes)");
    self->check_members = ToriRSChrome_Checkbox(chrome, self->panel, "members", 0);
    self->input_inv = ToriRSChrome_TextInput(chrome, self->panel, "Cache inv", "");
    ToriRSChrome_SetTooltip(chrome, self->input_inv, "the cache inv that backs the shop (SHOPS_PLAN 2.3)");
    self->input_title = ToriRSChrome_TextInput(chrome, self->panel, "Title", "");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find stock", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "filters the stock rows by obj name");
    self->list_stock = ToriRSChrome_VList(chrome, self->panel, "stockN = obj, count, rate   sells at, buys at",
        SHOP_STOCK_ROWS, 0);
    assert(self->list_stock >= 0);
    self->input_obj = ToriRSChrome_TextInput(chrome, self->panel, "obj", "");
    self->button_pick_obj = ToriRSChrome_Button(chrome, self->panel, "Pick obj");
    ToriRSChrome_SetTooltip(chrome, self->button_pick_obj, "the Catalog's obj pick");
    self->input_count = ToriRSChrome_TextInput(chrome, self->panel, "count", "1");
    ToriRSChrome_SetTooltip(chrome, self->input_count, "baseline stock, and the starting count");
    self->input_rate = ToriRSChrome_TextInput(chrome, self->panel, "rate", "100");
    ToriRSChrome_SetTooltip(chrome, self->input_rate, "ticks between +-1 nudges toward baseline");
    self->button_set_row = ToriRSChrome_Button(chrome, self->panel, "Set row");
    ToriRSChrome_SetTooltip(chrome, self->button_set_row, "the picked row takes obj, count and rate");
    self->button_remove_row = ToriRSChrome_Button(chrome, self->panel, "Remove row");
    self->button_add_stock = ToriRSChrome_Button(chrome, self->panel, "+ stock");
    ToriRSChrome_SetTooltip(chrome, self->button_add_stock, "a new row from obj, count and rate");
    self->button_wiki = ToriRSChrome_Button(chrome, self->panel, "From wiki catalogue...");
    ToriRSChrome_SetTooltip(chrome, self->button_wiki, "disabled: the tree has no wiki/shop_stock.csv");
    ToriRSChrome_SetDisabled(chrome, self->button_wiki, 1);
    self->label_prices = ToriRSChrome_Label(chrome, self->panel, "prices from calc_shop_value at baseline stock");
    ToriRSChrome_Separator(chrome, self->panel);
    self->button_test = ToriRSChrome_Button(chrome, self->panel, "Test");
    ToriRSChrome_SetTooltip(chrome, self->button_test, "Save, RUN, then go to the owner and Trade");
    self->button_check = ToriRSChrome_Button(chrome, self->panel, "Check");
    ToriRSChrome_SetTooltip(chrome, self->button_check, "the owner, the objs, the inv and the triggers");
    self->button_save = ToriRSChrome_Button(chrome, self->panel, "Save");
    ToriRSChrome_SetTooltip(chrome, self->button_save, "write the .rs2 and the .inv: one undo step");
    self->list_problems = ToriRSChrome_VList(chrome, self->panel, "Problems", SHOP_PROBLEM_ROWS, 0);
    assert(self->list_problems >= 0);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    shop_hints(ui, self);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetScrollable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "shop";
    desc.title = "Shop builder";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = shop_tick;
    desc.activate = shop_activate;
    desc.set_visible = shop_set_visible;
    desc.destroy = shop_destroy;
    Editor_UiAddPanel(ui, &desc);
    {
        const char* content = getenv("TORIRS_BUILDER_CONTENT");
        if( content && content[0] )
            Editor_Shop_SetPaths(ui, content);
    }
}
