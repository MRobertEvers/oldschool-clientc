/*
 * The builder panels bound to the client's editor session: Builder_Services
 * over struct Editor (Editor_Apply, the mirror, the catalog through the
 * session's ToriRSMapEd connection). The panels themselves are
 * builder_panels.c; this file is what links them to editor.c, which a panel
 * test does not want.
 */
#include "builder_panels.h"

#include "editor/editor.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct Editor*
session_editor(void* user_data)
{
    struct EditorUi* ui = user_data;
    assert(ui);
    return ui->editor;
}

static int
session_apply(
    void* user_data,
    const struct Editor_Cmd* command)
{
    struct Editor* editor = session_editor(user_data);
    return editor && Editor_Apply(editor, command);
}

static void
session_stroke(
    void* user_data,
    int begin)
{
    struct Editor* editor = session_editor(user_data);
    if( !editor )
        return;
    if( begin )
        Editor_StrokeBegin(editor);
    else
        Editor_StrokeEnd(editor);
}

static const char*
session_current(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key,
    size_t* out_length)
{
    struct Editor* editor = session_editor(user_data);
    struct Editor_DocHandle handle;

    assert(key);
    assert(out_length);
    *out_length = 0;
    if( !editor )
        return NULL;
    /* The echoes of what this session just sent land on the next pump; a
     * regeneration reads the text it diffs against right after. */
    Editor_PumpFacts(editor);
    if( !Editor_DocFind(&editor->doc, kind, key, &handle) || !Editor_DocHandleLoaded(&handle) )
    {
        if( !Editor_OpenDoc(editor, kind, key) )
            return NULL;
        if( !Editor_DocFind(&editor->doc, kind, key, &handle) )
            return NULL;
    }
    if( !handle.text )
        return NULL;
    *out_length = handle.text->length;
    return handle.text->text ? handle.text->text : "";
}

static int
session_save(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor* editor = session_editor(user_data);
    return editor ? Editor_SaveDoc(editor, kind, key) : -1;
}

static const char*
record_field(
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

static int
session_record(
    void* user_data,
    enum Builder_Subject subject,
    const char* name,
    int id,
    struct Builder_RecordInfo* out)
{
    struct Editor* editor = session_editor(user_data);
    struct EditorHost_CatalogRecord record;
    uint32_t kind = subject == BUILDER_SUBJECT_NPC ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_LOC;

    assert(name);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
    {
        /* No catalog: trust the name; the compile check is the backstop. */
        if( !name[0] )
            return 0;
        out->found = 1;
        out->id = -1;
        snprintf(out->name, sizeof(out->name), "%s", name);
        return 1;
    }
    memset(&record, 0, sizeof(record));
    if( Editor_HostMapEdCatalogRecord(&editor->host, kind, id, name, &record) != EDITOR_HOST_OK )
        return 0;
    out->found = 1;
    out->id = record.id;
    snprintf(out->name, sizeof(out->name), "%s", record.name ? record.name : name);
    snprintf(out->display, sizeof(out->display), "%s", record.display ? record.display : "");
    for( int op = 0; op < 5; op++ )
    {
        char key[8];
        const char* value;
        snprintf(key, sizeof(key), "op%d", op + 1);
        value = record_field(&record, key);
        if( value && strcmp(value, "default") != 0 && strcmp(value, "hidden") != 0 )
            snprintf(out->ops[op], sizeof(out->ops[op]), "%s", value);
    }
    Editor_HostCatalogRecordFree(&record);
    return 1;
}

static int
session_triggers(
    void* user_data,
    const char* subject,
    struct Builder_ExistingList* out)
{
    struct Editor* editor = session_editor(user_data);
    struct EditorHost_CatalogPage page;
    char filter[160];
    uint32_t page_index = 0;

    assert(subject);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return 0;
    snprintf(filter, sizeof(filter), "subject=%s", subject);
    for( ;; )
    {
        memset(&page, 0, sizeof(page));
        if( Editor_HostMapEdCatalogQuery(&editor->host, TORIRSMAPED_CATALOG_TRIGGER, filter, 0,
                                         page_index, 0, &page)
            != EDITOR_HOST_OK )
            return 0;
        for( int i = 0; i < page.row_count && out->count < BUILDER_EXISTING_MAX; i++ )
        {
            const struct EditorHost_CatalogRow* row = &page.rows[i];
            const char* comma = row->name ? strchr(row->name, ',') : NULL;
            /* The filter also matches a longer subject sharing the prefix. */
            if( !comma || strcmp(comma + 1, subject) != 0 )
                continue;
            snprintf(out->names[out->count], sizeof(out->names[0]), "%s", row->name);
            snprintf(out->files[out->count], sizeof(out->files[0]), "%s",
                     row->display ? row->display : "");
            out->rows[out->count].name = out->names[out->count];
            out->rows[out->count].file = out->files[out->count];
            out->count++;
        }
        page_index++;
        if( page.row_count == 0 || page_index * page.page_size >= page.total )
        {
            Editor_HostCatalogPageFree(&page);
            break;
        }
        Editor_HostCatalogPageFree(&page);
    }
    return 1;
}

/** Is `name` one exact record of catalog kind `kind`? 1, 0, or -1 when
 *  the catalog cannot be asked. */
static int
session_catalog_has(
    struct Editor* editor,
    uint32_t kind,
    const char* name)
{
    struct EditorHost_CatalogPage page;
    int found = 0;

    memset(&page, 0, sizeof(page));
    if( Editor_HostMapEdCatalogQuery(&editor->host, kind, name, TORIRSMAPED_CATALOG_EXACT, 0, 0, &page)
        != EDITOR_HOST_OK )
        return -1;
    for( int i = 0; i < page.row_count && !found; i++ )
        found = page.rows[i].name && strcmp(page.rows[i].name, name) == 0;
    Editor_HostCatalogPageFree(&page);
    return found;
}

/** The lint's names through the catalog: a var is a varp or a varbit
 *  record, a proc a [proc,<name>] trigger. */
static int
session_name_known(
    void* user_data,
    int kind,
    const char* name)
{
    struct Editor* editor = session_editor(user_data);
    char trigger[BUILDER_NAME_MAX + 8];
    int found;

    assert(name);
    if( !editor || !(editor->capabilities & TORIRSMAPED_CAP_CATALOG) )
        return -1;
    if( kind == BUILDER_NAME_VAR )
    {
        found = session_catalog_has(editor, TORIRSMAPED_CATALOG_VARP, name);
        if( found == 0 )
            found = session_catalog_has(editor, TORIRSMAPED_CATALOG_VARBIT, name);
        return found;
    }
    snprintf(trigger, sizeof(trigger), "proc,%s", name);
    return session_catalog_has(editor, TORIRSMAPED_CATALOG_TRIGGER, trigger);
}

static int
session_state_set(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct EditorUi* ui = user_data;
    return Editor_UiPublish(ui, key, values, count);
}

static int
session_copy_record(
    void* user_data,
    enum Builder_Subject subject,
    const char* name,
    const char* new_name)
{
    struct EditorUi* ui = user_data;
    return Editor_Catalog_CopyRecord(
        ui, subject == BUILDER_SUBJECT_NPC ? TORIRSMAPED_CATALOG_NPC : TORIRSMAPED_CATALOG_LOC, name, new_name);
}

static void
session_services(
    struct EditorUi* ui,
    struct Builder_Services* out)
{
    memset(out, 0, sizeof(*out));
    out->user_data = ui;
    out->submit.user_data = ui;
    out->submit.apply = session_apply;
    out->submit.stroke = session_stroke;
    out->submit.current = session_current;
    out->save = session_save;
    out->record = session_record;
    out->triggers = session_triggers;
    out->state_set = session_state_set;
    out->copy_record = session_copy_record;
    out->name_known = session_name_known;
}

/** TORIRS_BUILDER_SSCOMPILE / _CONTENT / _WORK: the compile check's paths
 *  when the host has not set them (Builder_PanelsSetCompile). */
static void
compile_from_env(struct EditorUi* ui)
{
    const char* sscompile = getenv("TORIRS_BUILDER_SSCOMPILE");
    const char* content = getenv("TORIRS_BUILDER_CONTENT");
    const char* work = getenv("TORIRS_BUILDER_WORK");

    if( sscompile && content && work )
        Builder_PanelsSetCompile(ui, sscompile, content, work);
}

void
Editor_LocBuilder_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_LocPanelRegister(ui, &services);
    compile_from_env(ui);
}

void
Editor_NpcBuilder_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_NpcPanelRegister(ui, &services);
    compile_from_env(ui);
}

void
Editor_ShellEditor_Register(struct EditorUi* ui)
{
    struct Builder_Services services;

    assert(ui);
    session_services(ui, &services);
    Builder_ShellPanelRegister(ui, &services);
}
