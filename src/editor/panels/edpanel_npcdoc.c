#include "edpanel_npcdoc.h"

#include "edpanel_common.h"
#include "editor/editor.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_stamps.h"
#include "editor/editor_ui.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
npcdoc_build(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc)
{
    Editor_CfgRecordFree(&doc->record);
    Editor_CfgRecordInit(&doc->record, "npc", doc->name);
    for( int i = -1; i < doc->overlay_count; i++ )
    {
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        const char* overlay = i < 0 ? NULL : doc->overlays[i];
        const char* text = "";
        size_t length = 0;

        if( !Editor_CfgOverlayKey(key, "npc", doc->name, overlay) )
            continue;
        if( ui->editor && Editor_DocFind(&ui->editor->doc, EDITOR_DOC_CFG, key, &handle) && handle.text
            && handle.text->text )
        {
            text = handle.text->text;
            length = handle.text->length;
        }
        (void)Editor_CfgRecordAddLayer(&doc->record, overlay, text, length);
    }
    Editor_CfgRecordResolve(&doc->record, doc->fields, doc->field_count);
    doc->built = 1;
    doc->built_seq = ui->editor ? ui->editor->last_cmd_seq : 0;
}

int
Editor_NpcDocOpen(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* name)
{
    struct EditorHost_CatalogRecord record;
    char key[EDITOR_DOC_KEY_MAX];
    int authored = 0;

    assert(ui);
    assert(doc);
    assert(name);
    Editor_NpcDocClose(doc);
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "NPC editor: no editor session to open %s's record through", name);
        return 0;
    }
    if( !name[0] || strlen(name) >= sizeof(doc->name) )
        return 0;
    snprintf(doc->name, sizeof(doc->name), "%s", name);

    /* The layers: every server overlay the catalog says states the record. */
    memset(&record, 0, sizeof(record));
    if( (ui->editor->capabilities & TORIRSMAPED_CAP_CATALOG)
        && Editor_HostMapEdCatalogRecord(&ui->editor->host, TORIRSMAPED_CATALOG_NPC, -1, name, &record) == EDITOR_HOST_OK )
        for( int i = 0; i < record.source_count && doc->overlay_count < EDITOR_CFG_RECORD_LAYERS_MAX - 2; i++ )
        {
            const char* path = record.sources[i].path;
            size_t const length = strlen(path);
            if( strncmp(path, "server/scripts/", 15) != 0 || length < 15 + 5 || strcmp(path + length - 4, ".npc") != 0 )
                continue;
            if( strlen(path + 15) >= sizeof(doc->overlays[0]) )
                continue;
            snprintf(doc->overlays[doc->overlay_count++], sizeof(doc->overlays[0]), "%s", path + 15);
        }
    Editor_HostCatalogRecordFree(&record);
    /* No authored file: the editor's own (3.8 rule 3). */
    for( int i = 0; i < doc->overlay_count; i++ )
        authored += Editor_CfgLayerRank(doc->overlays[i]) == 2;
    if( !authored )
    {
        int const n = snprintf(doc->overlays[doc->overlay_count], sizeof(doc->overlays[0]), "edited/%s/configs/%s.npc",
            name, name);
        if( n > 0 && (size_t)n < sizeof(doc->overlays[0]) )
            doc->overlay_count++;
    }
    for( int i = -1; i < doc->overlay_count; i++ )
    {
        if( !Editor_CfgOverlayKey(key, "npc", name, i < 0 ? NULL : doc->overlays[i]) )
            continue;
        if( !edp_ensure_doc(ui, EDITOR_DOC_CFG, key) )
            Editor_UiLog(ui, "editor: could not open %s", key);
    }
    /* The register. */
    doc->fields = ToriMem_Calloc(ToriMem_Editor(), EDITOR_CFG_REGISTER_MAX, sizeof(*doc->fields));
    assert(doc->fields);
    doc->field_count = 0;
    if( edp_ensure_doc(ui, EDITOR_DOC_TEXT, "fields:npc.ini") )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, "fields:npc.ini", &handle) && handle.text && handle.text->text )
            doc->field_count = Editor_CfgRegisterParse("npc", handle.text->text, handle.text->length, doc->fields,
                EDITOR_CFG_REGISTER_MAX);
    }
    doc->open = 1;
    npcdoc_build(ui, doc);
    return 1;
}

void
Editor_NpcDocClose(struct Editor_NpcDoc* doc)
{
    assert(doc);
    if( doc->open || doc->fields )
    {
        Editor_CfgRecordFree(&doc->record);
        ToriMem_Free(ToriMem_Editor(), doc->fields);
    }
    memset(doc, 0, sizeof(*doc));
}

const struct Editor_CfgRecord*
Editor_NpcDocRecord(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc)
{
    assert(ui);
    assert(doc);
    if( !doc->open )
        return NULL;
    if( !doc->built || (ui->editor && doc->built_seq != ui->editor->last_cmd_seq) )
        npcdoc_build(ui, doc);
    return &doc->record;
}

int
Editor_NpcDocMovement(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    struct Editor_NpcMovement* out)
{
    const struct Editor_CfgRecord* record;

    assert(out);
    record = Editor_NpcDocRecord(ui, doc);
    if( !record )
        return 0;
    Editor_NpcMovementInit(out);
    for( int r = 0; r < record->row_count; r++ )
    {
        const struct Editor_CfgRecordRow* row = &record->rows[r];
        if( row->layer < 0 )
            continue;
        if( row->sub[0] )
        {
            char value[EDITOR_CMD_VALUE_MAX + EDITOR_CMD_NAME_MAX + 1];
            snprintf(value, sizeof(value), "%s,%s", row->sub, row->value);
            (void)Editor_NpcMovementRead(out, row->key, value);
        }
        else
            (void)Editor_NpcMovementRead(out, row->key, row->value);
    }
    return 1;
}

int
Editor_NpcDocWriteTarget(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* key,
    char* out,
    size_t out_size)
{
    const struct Editor_CfgRecord* record;
    int layer;

    assert(key);
    assert(out || out_size == 0);
    record = Editor_NpcDocRecord(ui, doc);
    if( !record )
        return -1;
    layer = Editor_CfgRecordWriteLayerFor(record, Editor_CfgRecordFindRow(record, key, "", 0));
    if( layer < 0 )
    {
        if( out_size )
            out[0] = '\0';
        return -1;
    }
    snprintf(out, out_size, "%s", record->layers[layer].label);
    return layer;
}

int
Editor_NpcDocSetField(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const char* key,
    const char* sub,
    const char* value,
    const char* what)
{
    const struct Editor_CfgRecord* record;
    struct Editor_Cmd commands[2];
    int layer;
    int count;

    assert(key);
    assert(sub);
    assert(value);
    assert(what);
    record = Editor_NpcDocRecord(ui, doc);
    if( !record )
        return -1;
    layer = Editor_CfgRecordWriteLayerFor(record, Editor_CfgRecordFindRow(record, key, sub, 0));
    if( layer < 0 )
    {
        Editor_UiStatus(ui, "%s refused: %s has no authored overlay to write %s to", what, doc->name, key);
        return -1;
    }
    count = Editor_CfgRecordFieldCommands(record, key, sub, 0, value, layer, commands, 2);
    if( count < 0 )
    {
        Editor_UiStatus(ui, "%s refused: %s = %s is not a line a config can hold", what, key, value);
        return -1;
    }
    if( count == 0 )
        return 0;
    count = edp_apply(ui, commands, count, what);
    if( count > 0 )
        Editor_UiLog(ui, "editor: %s: %s%s%s %s in %s (unsaved; Save writes it)", doc->name, key, sub[0] ? ":" : "", sub,
            value[0] ? "set" : "removed", record->layers[layer].label);
    return count;
}

int
Editor_NpcDocSetRoute(
    struct EditorUi* ui,
    struct Editor_NpcDoc* doc,
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    enum Editor_NpcMode mode,
    const char* what)
{
    const struct Editor_CfgRecord* record;
    struct Editor_Cmd* commands;
    int capacity;
    int layer;
    int planned;
    int applied = 0;
    int was_open;
    int applied_before;
    int ok = 1;
    char ledger_key[EDITOR_DOC_KEY_MAX];
    const char* word;

    assert(waypoints || count == 0);
    assert(what);
    record = Editor_NpcDocRecord(ui, doc);
    if( !record || !ui->editor )
        return -1;
    layer = Editor_CfgRecordWriteLayerFor(record, Editor_CfgRecordFindRow(record, "patrol1", "", 0));
    if( layer < 0 )
    {
        Editor_UiStatus(ui, "%s refused: %s has no authored overlay to write the route to", what, doc->name);
        return -1;
    }
    commands = edp_scratch(&capacity);
    planned = Editor_NpcRoutePlan(record, waypoints, count, mode, layer, commands, capacity);
    if( planned < 0 )
    {
        Editor_UiStatus(ui, "%s refused: the route does not fit a config (%d waypoints)", what, count);
        return -1;
    }
    /* The ledger's word for what the server will now do. */
    if( count > 0 )
        word = "patrols";
    else
    {
        struct Editor_NpcMovement after;
        struct Editor_NpcEffective effective;
        (void)Editor_NpcDocMovement(ui, doc, &after);
        after.patrol_count = 0;
        after.defaultmode_stated = 1;
        after.defaultmode = mode;
        Editor_NpcEffectiveMode(&after, &effective);
        word = effective.mode == EDITOR_NPC_MODE_WANDER ? "wanders" : "stationary";
    }
    Editor_NpcMoveDocKey(ledger_key, doc->name);

    applied_before = Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor));
    was_open = ui->tools.stroke_open;
    if( !was_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, what);
    }
    ui->tools.stroke_open = 1;
    if( planned > 0 )
    {
        int const n = edp_apply(ui, commands, planned, what);
        if( n != planned )
            ok = 0;
        applied += n;
    }
    /* The ledger: the document (absent on disk: empty) spliced to its new
     * text, in the same step. */
    if( ok && edp_ensure_doc(ui, EDITOR_DOC_TEXT, ledger_key) )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, ledger_key, &handle) && handle.text )
        {
            const char* old = handle.text->text ? handle.text->text : "";
            size_t const old_length = handle.text->text ? handle.text->length : 0;
            size_t new_length = 0;
            char* next;
            if( Editor_NpcMoveTextSet(old, old_length, doc->name, word, NULL, NULL, 0, &new_length) )
            {
                next = ToriMem_Alloc(ToriMem_Editor(), new_length + 1);
                assert(next);
                Editor_NpcMoveTextSet(old, old_length, doc->name, word, NULL, next, new_length + 1, &new_length);
                {
                    int const splices = Editor_StampSplices(ledger_key, old, old_length, next, new_length, commands, capacity);
                    if( splices > 0 )
                    {
                        int const n = edp_apply(ui, commands, splices, what);
                        if( n != splices )
                            ok = 0;
                        applied += n;
                    }
                    else if( splices < 0 )
                        ok = 0;
                }
                ToriMem_Free(ToriMem_Editor(), next);
            }
        }
    }
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
    if( !ok )
    {
        if( !was_open && Editor_HistoryAppliedCommands(Editor_GetHistory(ui->editor)) > applied_before )
            (void)Editor_Undo(ui->editor);
        Editor_UiStatus(ui, "%s refused: the authority did not take every edit; nothing was applied", what);
        return -1;
    }
    if( applied > 0 )
        Editor_UiLog(ui, "editor: %s: route of %d waypoint%s in %s, ledger %s (unsaved; Save writes both)", doc->name, count,
            count == 1 ? "" : "s", record->layers[layer].label, word);
    return applied;
}
