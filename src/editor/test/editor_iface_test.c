/**
 * The interface editor's modules, headless (editor/iface/).
 *
 *   editor_iface_test <content-dir> <cache-dir> <cs2-bin> <runestar-names|-> <tmp-dir>
 *
 *   MODEL     a real interface's tree (rows, expansion kept by name across a
 *             rebuild, selection, component ids), the inspector's groups,
 *             the structure plans applied through Editor_CmdApply and undone.
 *   PACK      every interface's document read through the typed layer into
 *             the ToriRS_ComponentPack the client mounts
 *             (ToriRS_ComponentPackFromRSCacheDat2), and the document compared
 *             with the cache: each component decoded from the cache emits the
 *             document's lines (what "the document is the truth" rests on).
 *   HOOKS     the script catalog from scripts/<name>.cs2 headers, typed hook
 *             building and description.
 *   COMPILE   a real script through `cs2 compile` in <tmp-dir> with the
 *             content's script names, and a broken one: its decline listed.
 *   CODE      CODE edits as TEXT_SPLICE commands, applied and undone.
 *   PANELS    the panels built on a real ToriRSChrome over an IFACE document:
 *             tree rows, selection through an activation, inspector rows, a
 *             mount request.
 *
 * Writes only under <tmp-dir>.
 */

#include "datatypes/dat2_component.h"
#include "dat2disk.h"
#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_if.h"
#include "editor/iface/iface_cs2.h"
#include "editor/iface/iface_model.h"
#include "editor/iface/iface_panels.h"
#include "engine/torirs_component_from_rscache.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static struct ToriRSChrome g_ui;

/* ---- model ---------------------------------------------------------------- */

static void
test_model(const char* content)
{
    char path[2048];
    size_t length = 0;
    char* text;
    struct Editor_Doc* doc = calloc(1, sizeof(*doc));
    struct Editor_DocHandle handle;
    struct EditorIfaceModel model;
    struct EditorIfaceField fields[64];
    struct Editor_Cmd command;
    int count;
    int leaf = -1;

    snprintf(path, sizeof(path), "%s/interfaces/bankmain.if", content);
    text = slurp(path, &length);
    check(text != NULL, "model: bankmain.if reads");
    if( !text )
        return;

    EditorIfaceModel_Init(&model);
    check(EditorIfaceModel_Sync(&model, "if:bankmain", text, length) == 1, "model: first sync builds");
    check(EditorIfaceModel_Sync(&model, "if:bankmain", text, length) == 0, "model: unchanged text does not rebuild");
    check(model.interface_id == 12 && model.count > 50, "model: bankmain is interface 12 with its components");
    check(model.row_count > 1 && model.row_count <= model.count, "model: roots open, rows a subset");
    count = model.count;

    /* Expansion by name survives a rebuild. */
    for( int i = 0; i < count; i++ )
    {
        if( model.nodes[i].child_count > 0 && model.nodes[i].parent >= 0 )
        {
            int rows = model.row_count;
            EditorIfaceModel_Toggle(&model, i);
            check(model.row_count != rows, "model: toggling a parent changes the rows");
            EditorIfaceModel_Select(&model, i);
            {
                char* edited = malloc(length + 2);
                memcpy(edited, text, length);
                edited[length] = '\n';
                EditorIfaceModel_Sync(&model, "if:bankmain", edited, length + 1);
                check(model.selected == i && model.nodes[i].expanded,
                      "model: selection and expansion kept by name across a rebuild");
                free(edited);
            }
            break;
        }
    }
    check(EditorIfaceModel_FindComponent(&model, (12 << 16) | 5) == 5, "model: a full id finds its child");
    check(EditorIfaceModel_FindComponent(&model, (13 << 16) | 5) == -1, "model: another interface's id finds nothing");

    {
        int geometry = EditorIfaceModel_Fields(text, length, 5, EDITOR_IF_GROUP_GEOMETRY, 0, fields, 64);
        int with_absent = EditorIfaceModel_Fields(text, length, 5, EDITOR_IF_GROUP_GEOMETRY, 1, fields, 64);
        check(geometry > 0 && with_absent > geometry, "model: geometry group, then its absent keys");
        check(strcmp(EditorIfaceModel_GroupName(EDITOR_IF_GROUP_SCRIPTS), "Scripts") == 0, "model: group names");
    }

    /* Plans through the document: add child, reparent, delete, each undone. */
    Editor_DocInit(doc, NULL);
    check(Editor_DocOpen(doc, EDITOR_DOC_IFACE, "if:bankmain", &handle) && handle.text, "model: doc opens");
    Editor_TextDocSet(handle.text, text, length);
    EditorIfaceModel_Sync(&model, "if:bankmain", text, length);
    check(EditorIfaceModel_PlanAddChild(&model, 0, &command), "model: plan add child");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1, "model: add child applies");
    EditorIfaceModel_Sync(&model, "if:bankmain", handle.text->text, handle.text->length);
    check(model.count == count + 1 && model.nodes[count].parent == 0, "model: the new child is the last id, under 0");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_INVERSE) == 1 && handle.text->length == length
              && memcmp(handle.text->text, text, length) == 0,
          "model: add child undoes byte-exact");
    EditorIfaceModel_Sync(&model, "if:bankmain", text, length);
    for( int i = count - 1; i > 0 && leaf < 0; i-- )
    {
        if( model.nodes[i].child_count == 0 && model.nodes[i].parent > 0 )
            leaf = i;
    }
    if( leaf > 0 )
    {
        struct Editor_Cmd* plan = malloc(sizeof(*plan) * 512);
        int planned;
        int ok = 1;

        check(EditorIfaceModel_PlanReparent(&model, leaf, 0, &command), "model: plan reparent");
        check(Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1, "model: reparent applies");
        check(Editor_CmdApply(doc, &command, EDITOR_CMD_INVERSE) == 1 && handle.text->length == length
                  && memcmp(handle.text->text, text, length) == 0,
              "model: reparent undoes byte-exact");
        planned = EditorIfaceModel_PlanDelete(&model, text, length, leaf, plan, 512);
        check(planned > 0, "model: plan delete of a leaf");
        for( int i = 0; i < planned && ok; i++ )
            ok = Editor_CmdApply(doc, &plan[i], EDITOR_CMD_FORWARD) == 1;
        check(ok, "model: the delete stroke applies");
        for( int i = planned - 1; i >= 0 && ok; i-- )
            ok = Editor_CmdApply(doc, &plan[i], EDITOR_CMD_INVERSE) == 1;
        check(ok && handle.text->length == length && memcmp(handle.text->text, text, length) == 0,
              "model: the delete stroke undoes byte-exact");
        free(plan);
    }
    model.structure_locked = 1;
    check(!EditorIfaceModel_PlanAddChild(&model, 0, &command), "model: a locked structure plans no add");

    EditorIfaceModel_Free(&model);
    Editor_DocFree(doc);
    free(doc);
    free(text);
}

/* ---- the document as the client's pack, against the cache ----------------- */

static void
test_pack(
    const char* content,
    const char* cache_dir)
{
    char dir[2048];
    DIR* listing;
    struct dirent* entry;
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    int table = -1;
    int files = 0;
    int packed = 0;
    int unmappable = 0;
    int compared = 0;
    int same = 0;
    int differ = 0;
    int differ_files = 0;
    int no_archive = 0;

    check(disk != NULL, "pack: the cache opens read-only");
    if( disk )
    {
        /* The content tree is rev 241 (meta.ini), as the cache beside it. */
        struct RSCache profile = RSCache_ProfileZero();
        profile.game = RSCACHE_GAME_OLDSCHOOL;
        profile.epoch = RSCACHE_EPOCH_DAT2;
        profile.revision = 241;
        RSCache_Dat2DiskSetProfile(disk, &profile);
        table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_INTERFACES);
    }
    printf("pack: cache %s, interfaces table %d\n", disk ? "open" : "missing", table);
    snprintf(dir, sizeof(dir), "%s/interfaces", content);
    listing = opendir(dir);
    while( listing && (entry = readdir(listing)) )
    {
        size_t n = strlen(entry->d_name);
        char path[2300];
        size_t length = 0;
        char* text;
        struct Editor_IfFile file;
        struct RSCache_Dat2ComponentPack* source;
        int count;
        int ok = 1;
        int file_differs = 0;

        if( n < 4 || strcmp(entry->d_name + n - 3, ".if") != 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        text = slurp(path, &length);
        if( !text )
            continue;
        files++;
        Editor_IfIndex(&file, text, length);
        count = Editor_IfComponentCount(&file);
        source = calloc(1, sizeof(*source));
        source->components = calloc((size_t)count + 1, sizeof(*source->components));
        source->component_count = count;
        for( int i = 0; i < count && ok; i++ )
        {
            source->components[i] = calloc(1, sizeof(struct RSCache_Dat2Component));
            ok = Editor_IfBlockReadComponent(&file, i, source->components[i], NULL, 0);
            if( ok && !((source->components[i]->type >= 0 && source->components[i]->type <= 7)
                        || source->components[i]->type == 9) )
            {
                ok = 0;
                unmappable++;
            }
        }
        if( ok && count > 0 )
        {
            struct ToriRS_ComponentPack* pack = ToriRS_ComponentPackFromRSCacheDat2(source);
            if( pack && pack->component_count == count && pack->components[count - 1].id
                                                              == ((file.interface_id > 0 ? file.interface_id : 0) << 16 | (count - 1)) )
                packed++;
            if( pack )
                ToriRS_ComponentPackFree(pack);
        }

        /* The cache's own components, emitted, against the document's lines. */
        if( disk && table >= 0 && file.interface_id >= 0 )
        {
            struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, file.interface_id);
            struct RSCache_Dat2ComponentPack* cached = NULL;

            if( archive && RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
                cached = RSCache_Dat2ComponentPackNewFromArchive(
                    archive, file.interface_id,
                    RSCache_Dat2ComponentDecodeRevFromProfile(
                        RSCache_Dat2DiskProfile(disk), RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN));
            if( archive )
                RSCache_Dat2DiskArchiveFree(archive);
            if( !cached )
                no_archive++;
            for( int i = 0; cached && i < cached->component_count && i < count; i++ )
            {
                size_t needed;
                char* emitted;
                struct RSCache_Dat2Component* doc_component = source->components[i];
                size_t doc_needed;
                char* doc_emitted;

                if( !doc_component || doc_component->type < 0 )
                    continue;
                compared++;
                needed = Editor_IfComponentEmit(cached->components[i], NULL, 0);
                emitted = malloc(needed + 1);
                Editor_IfComponentEmit(cached->components[i], emitted, needed + 1);
                doc_needed = Editor_IfComponentEmit(doc_component, NULL, 0);
                doc_emitted = malloc(doc_needed + 1);
                Editor_IfComponentEmit(doc_component, doc_emitted, doc_needed + 1);
                if( needed == doc_needed && memcmp(emitted, doc_emitted, needed) == 0 )
                    same++;
                else
                {
                    differ++;
                    file_differs = 1;
                }
                free(emitted);
                free(doc_emitted);
            }
            if( cached && cached->component_count != count )
                file_differs = 1;
            if( cached )
                RSCache_Dat2ComponentPackFree(cached);
        }
        if( file_differs )
        {
            differ_files++;
            if( differ_files <= 12 )
                printf("  document ahead of (or behind) the cache: %s\n", entry->d_name);
        }
        for( int i = 0; i < count; i++ )
        {
            if( source->components[i] )
            {
                RSCache_Dat2ComponentFree(source->components[i]);
                free(source->components[i]);
            }
        }
        free(source->components);
        free(source);
        Editor_IfFree(&file);
        free(text);
    }
    if( listing )
        closedir(listing);
    if( disk )
        RSCache_Dat2DiskFree(disk);
    printf("pack: %d interfaces, %d mount from the document, %d hold a type the client does not map\n", files,
           packed, unmappable);
    printf("pack: against %s: %d components compared, %d identical, %d differ (%d interfaces; %d not in the cache)\n",
           cache_dir, compared, same, differ, differ_files, no_archive);
    check(files == 982, "pack: all 982 interfaces read");
    check(packed + unmappable == files, "pack: every mappable interface becomes a client pack");
    check(compared > 20000 && same * 100 >= compared * 95,
          "pack: the cache's components emit the document's lines (95%+)");
}

/* ---- hooks ----------------------------------------------------------------- */

static void
test_hooks(
    const char* content,
    struct EditorCs2Catalog* catalog)
{
    char dir[2048];
    struct EditorCs2Script header;
    const struct EditorCs2Script* found;
    struct Editor_IfHook hook;
    char raw[256];
    char described[256];
    int hits[16];
    static const char sample[] =
        "// 6222\n[clientscript,pvp_arena_board_bind_actions_6222](int $row_int0, component $c1, string $data_string0)\n";

    check(EditorCs2_ParseHeader(sample, strlen(sample), &header) && header.id == 6222 && header.clientscript
              && header.arg_count == 3 && header.arg_kind[1] == EDITOR_CS2_ARG_COMPONENT
              && header.arg_kind[2] == EDITOR_CS2_ARG_STRING,
          "hooks: a header's id, kind and typed args");
    snprintf(dir, sizeof(dir), "%s/scripts", content);
    check(EditorCs2Catalog_Load(catalog, dir) > 9000, "hooks: the catalog reads the scripts' headers");
    printf("hooks: %d scripts in the catalog\n", catalog->count);
    found = EditorCs2Catalog_Find(catalog, 88);
    check(found && strcmp(found->name, "private_op") == 0, "hooks: script 88 is private_op");
    check(EditorCs2Catalog_Search(catalog, "bankmain_", hits, 16) > 0, "hooks: name search");
    {
        const char* values[] = { "event_opindex", "event_opbase" };
        check(found && EditorCs2_BuildHook(found, values, 2, &hook) && hook.arg_count == 3
                  && hook.args[1].value == -2147483644 && hook.args[2].is_string,
              "hooks: event placeholders and a string build a hook");
        Editor_IfHookFormat(&hook, raw, sizeof(raw));
        check(strcmp(raw, "i:88,i:-2147483644,s:event_opbase") == 0, "hooks: spelled as the .if grammar");
    }
    {
        const char* values[] = { "548:12", "x", "y" };
        check(EditorCs2_BuildHook(&header, values, 3, &hook) == 0, "hooks: a bad int is refused");
        check(EditorCs2_BuildHook(&header, values, 2, &hook) == 0, "hooks: a value per argument");
        values[0] = "7";
        values[1] = "548:12";
        check(EditorCs2_BuildHook(&header, values, 3, &hook) && hook.args[1].value == 7
                  && hook.args[2].value == (548 << 16 | 12) && hook.args[3].is_string,
              "hooks: an int, a component as iface:child, a string");
    }
    EditorCs2_DescribeHook(catalog, "i:88,i:-2147483644,s:Hi", described, sizeof(described));
    check(strcmp(described, "private_op(event_opindex, \"Hi\")") == 0, "hooks: described by name");
}

/* ---- compile --------------------------------------------------------------- */

static void
test_compile(
    const char* content,
    const char* cache,
    const char* cs2_bin,
    const char* runestar,
    const char* tmp,
    const struct EditorCs2Catalog* catalog)
{
    char path[2048];
    char names[2048];
    char work[2048];
    size_t length = 0;
    char* source;
    struct EditorCs2CompileJob job;
    struct EditorCs2CompileResult* result = calloc(1, sizeof(*result));
    static const char broken[] = "// 88\n[clientscript,private_op](int $op_int0, string $name_string0)\n"
                                 "if_bogus_op(1);\n";

    snprintf(path, sizeof(path), "%s/scripts/private_op.cs2", content);
    source = slurp(path, &length);
    check(source != NULL, "compile: private_op.cs2 reads");
    snprintf(names, sizeof(names), "%s/names", tmp);
    check(EditorCs2Catalog_WriteNamesDir(catalog, runestar, names), "compile: names dir written");
    snprintf(work, sizeof(work), "%s/compile", tmp);

    memset(&job, 0, sizeof(job));
    job.cs2_bin = cs2_bin;
    job.cache_dir = cache;
    job.names_dir = names;
    job.work_dir = work;
    job.script_name = "private_op";
    job.source = source ? source : "";
    job.source_length = length;
    EditorCs2_Compile(&job, result);
    printf("compile: %s -> compiled %d, failed %d, %d lines\n", job.script_name, result->compiled, result->failed,
           result->line_count);
    check(result->status == EDITOR_CS2_COMPILED && result->compiled == 1 && result->script_id == 88,
          "compile: a real script compiles with the content's names");
    {
        size_t out_length = 0;
        char* out = result->output_path[0] ? slurp(result->output_path, &out_length) : NULL;
        check(out && out_length > 0, "compile: the encoded script is left for the session");
        free(out);
    }

    job.source = broken;
    job.source_length = strlen(broken);
    EditorCs2_Compile(&job, result);
    check(result->status == EDITOR_CS2_DECLINED && result->decline_count == 1
              && strstr(result->lines[result->declines[0]], "if_bogus_op") != NULL,
          "compile: a declined spelling is listed, never a silent .cs2b");

    job.cs2_bin = "/nonexistent/cs2";
    EditorCs2_Compile(&job, result);
    check(result->status == EDITOR_CS2_NOT_RUN && result->line_count == 1, "compile: no binary is said, not hidden");
    free(source);
    free(result);
}

/* ---- code ------------------------------------------------------------------ */

static void
test_code(void)
{
    struct Editor_Doc* doc = calloc(1, sizeof(*doc));
    struct Editor_DocHandle handle;
    struct EditorCs2Code code;
    struct Editor_Cmd command;
    static const char text[] = "// 1\n[clientscript,x]\nif (1 = 1) {\n\treturn;\n}\n";
    struct EditorCs2Span spans[16];
    char line[128];

    Editor_DocInit(doc, NULL);
    check(Editor_DocOpen(doc, EDITOR_DOC_TEXT, "cs2:x", &handle) && handle.text, "code: cs2 doc opens");
    Editor_TextDocSet(handle.text, text, strlen(text));
    EditorCs2Code_Init(&code);
    EditorCs2Code_Sync(&code, "cs2:x", handle.text->text, handle.text->length);
    check(code.line_count == 5, "code: five lines");
    EditorCs2Code_Line(&code, handle.text->text, 3, line, sizeof(line));
    check(strcmp(line, "\treturn;") == 0, "code: line text without its ending");

    check(EditorCs2Code_PlanReplace(&code, handle.text->text, 3, "\treturn 5;", &command), "code: plan replace");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1 && strstr(handle.text->text, "return 5;"),
          "code: replace applies");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_INVERSE) == 1 && strcmp(handle.text->text, text) == 0,
          "code: replace undoes");
    check(EditorCs2Code_PlanSplit(&code, handle.text->text, 2, 4, &command)
              && Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1,
          "code: split applies");
    EditorCs2Code_Sync(&code, "cs2:x", handle.text->text, handle.text->length);
    check(code.line_count == 6, "code: split makes a line");
    check(EditorCs2Code_PlanJoin(&code, handle.text->text, 2, &command)
              && Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1 && strcmp(handle.text->text, text) == 0,
          "code: join takes it back");
    check(EditorCs2_Highlight("if ($a = %varp5) { ~f(\"s\", 3); } // c", spans, 16) >= 6, "code: highlight spans");

    EditorCs2Code_Free(&code);
    Editor_DocFree(doc);
    free(doc);
}

/* ---- watch, trace, harness -------------------------------------------------- */

static void
test_small(const char* tmp)
{
    struct EditorCs2Watch watch;
    int kind = -1;
    int id = -1;
    char line[128];
    char path[2048];
    size_t length = 0;
    char* written;
    const int ints[] = { 3, 4 };
    const char* strings[] = { "a\"b" };

    memset(&watch, 0, sizeof(watch));
    check(EditorCs2Watch_Parse("varbit 4153", &kind, &id) && kind == EDITOR_CS2_WATCH_VARBIT && id == 4153,
          "watch: parse varbit");
    check(!EditorCs2Watch_Parse("varq 1", &kind, &id), "watch: unknown kind refused");
    check(EditorCs2Watch_Add(&watch, EDITOR_CS2_WATCH_VARP, 173) == 0, "watch: add");
    check(EditorCs2Watch_Add(&watch, EDITOR_CS2_WATCH_VARP, 173) == -1, "watch: no duplicate");
    EditorCs2Watch_Set(&watch, 0, 7, NULL);
    EditorCs2Watch_Format(&watch, 0, line, sizeof(line));
    check(strcmp(line, "varp 173 = 7  *") == 0, "watch: a change is marked");
    EditorCs2Watch_Set(&watch, 0, 7, NULL);
    EditorCs2Watch_Format(&watch, 0, line, sizeof(line));
    check(strcmp(line, "varp 173 = 7") == 0, "watch: a steady value is not");

    snprintf(path, sizeof(path), "%s/case.json", tmp);
    check(EditorCs2_WriteHarnessCase(path, "c1", 915, ints, 2, strings, 1, 1), "harness: case written");
    written = slurp(path, &length);
    check(written && strstr(written, "\"scriptId\": 915, \"intArgs\": [3, 4], \"stringArgs\": [\"a\\\"b\"]")
              && strstr(written, "\"screenshot\": true"),
          "harness: tools/perf/cs2_cases' format");
    free(written);
}

/* ---- panels ---------------------------------------------------------------- */

static void
test_panels(
    const char* content,
    const char* tmp)
{
    char path[2048];
    size_t length = 0;
    char* text;
    struct Editor* editor = calloc(1, sizeof(*editor));
    struct Editor_DocHandle handle;
    struct EditorIfacePanels* panels = calloc(1, sizeof(*panels));
    struct EditorIfaceRequest request;

    snprintf(path, sizeof(path), "%s/interfaces/bankmain.if", content);
    text = slurp(path, &length);
    Editor_DocInit(&editor->doc, NULL);
    check(text && Editor_DocOpen(&editor->doc, EDITOR_DOC_IFACE, "if:bankmain", &handle) && handle.text,
          "panels: an IFACE document in the mirror");
    Editor_TextDocSet(handle.text, text, length);

    ToriRSChrome_Init(&g_ui);
    EditorIfacePanels_Init(panels);
    EditorIfacePanels_Configure(panels, content, "", "", "", tmp);
    EditorIfacePanels_Build(panels, &g_ui);
    check(panels->built && panels->panel_iface >= 0 && panels->panel_cs2 >= 0 && panels->code >= 0,
          "panels: built");
    snprintf(panels->model.doc_key, sizeof(panels->model.doc_key), "if:bankmain");
    snprintf(panels->iface_name, sizeof(panels->iface_name), "bankmain");
    EditorIfacePanels_ShowInterface(panels, &g_ui, 1);
    EditorIfacePanels_Tick(panels, &g_ui, editor);
    check(panels->model.count > 50 && panels->tree_row_count == panels->model.row_count,
          "panels: one TREE row per visible node");

    check(EditorIfacePanels_OnActivate(panels, &g_ui, editor, panels->tree_rows[1]) == 1, "panels: a tree row is ours");
    check(panels->model.selected == panels->tree_row_position[1], "panels: activating a row selects its node");
    EditorIfacePanels_Tick(panels, &g_ui, editor);
    check(panels->inspector_selected == panels->model.selected, "panels: the inspector follows the selection");
    check(panels->field_count > 0, "panels: the inspector lists the identity group");

    check(EditorIfacePanels_OnActivate(panels, &g_ui, editor, panels->bt_mount) == 1, "panels: Mount is ours");
    check(EditorIfacePanels_TakeRequest(panels, &request) && request.kind == EDITOR_IFACE_REQUEST_MOUNT_ROOT
              && request.interface_id == 12,
          "panels: Mount queues App_OpenRootInterface(12)");
    check(!EditorIfacePanels_TakeRequest(panels, &request), "panels: the queue drains");
    check(EditorIfacePanels_OnActivate(panels, &g_ui, editor, 4095) == 0, "panels: a foreign widget is not ours");

    EditorIfacePanels_SelectComponent(panels, &g_ui, (12 << 16) | 3);
    check(panels->model.selected == 3, "panels: a pick selects by full id");

    EditorIfacePanels_Free(panels);
    free(panels);
    Editor_DocFree(&editor->doc);
    free(editor);
    free(text);
}

int
main(
    int argc,
    char** argv)
{
    struct EditorCs2Catalog catalog;

    if( argc < 6 )
    {
        fprintf(stderr, "usage: editor_iface_test <content> <cache> <cs2-bin> <runestar-names|-> <tmp>\n");
        return 2;
    }
    test_model(argv[1]);
    test_pack(argv[1], argv[2]);
    test_hooks(argv[1], &catalog);
    test_compile(argv[1], argv[2], argv[3], strcmp(argv[4], "-") == 0 ? NULL : argv[4], argv[5], &catalog);
    test_code();
    test_small(argv[5]);
    test_panels(argv[1], argv[5]);
    EditorCs2Catalog_Free(&catalog);
    printf("editor_iface_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
