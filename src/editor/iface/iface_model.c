#include "editor/iface/iface_model.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the tree ------------------------------------------------------------ */

void
EditorIfaceModel_Init(struct EditorIfaceModel* model)
{
    assert(model);
    memset(model, 0, sizeof(*model));
    model->interface_id = -1;
    model->selected = -1;
}

void
EditorIfaceModel_Free(struct EditorIfaceModel* model)
{
    if( !model )
        return;
    ToriMem_Free(ToriMem_Editor(), model->nodes);
    ToriMem_Free(ToriMem_Editor(), model->rows);
    memset(model, 0, sizeof(*model));
    model->interface_id = -1;
    model->selected = -1;
}

static uint64_t
text_hash(
    const char* text,
    size_t length)
{
    uint64_t hash = 1469598103934665603ull;
    for( size_t i = 0; i < length; i++ )
        hash = (hash ^ (uint8_t)text[i]) * 1099511628211ull;
    return hash;
}

/** Append `position` and (when expanded) its subtree to the rows. */
static void
rows_walk(
    struct EditorIfaceModel* model,
    int position,
    int depth,
    int* child_start,
    int* child_list)
{
    struct EditorIfaceNode* node = &model->nodes[position];

    /* A malformed file could loop; depth is bounded by the count. */
    if( depth > model->count )
        return;
    node->depth = depth;
    model->rows[model->row_count++] = position;
    if( !node->expanded )
        return;
    for( int i = child_start[position]; i < child_start[position + 1]; i++ )
        rows_walk(model, child_list[i], depth + 1, child_start, child_list);
}

static void
rows_rebuild(struct EditorIfaceModel* model)
{
    int* child_start;
    int* child_list;
    int* fill;

    model->row_count = 0;
    if( model->count == 0 )
        return;
    /* Children of each node in block order: a counting sort on parents. */
    child_start = ToriMem_Calloc(ToriMem_Editor(), (size_t)model->count + 1, sizeof(int));
    child_list = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)model->count);
    fill = ToriMem_Calloc(ToriMem_Editor(), (size_t)model->count + 1, sizeof(int));
    assert(child_start);
    assert(child_list);
    assert(fill);
    for( int i = 0; i < model->count; i++ )
    {
        int parent = model->nodes[i].parent;
        if( parent >= 0 && parent < model->count )
            child_start[parent + 1]++;
    }
    for( int i = 0; i < model->count; i++ )
        child_start[i + 1] += child_start[i];
    for( int i = 0; i < model->count; i++ )
    {
        int parent = model->nodes[i].parent;
        if( parent >= 0 && parent < model->count )
            child_list[child_start[parent] + fill[parent]++] = i;
    }
    for( int i = 0; i < model->count; i++ )
        model->nodes[i].child_count = child_start[i + 1] - child_start[i];
    for( int i = 0; i < model->count; i++ )
    {
        int parent = model->nodes[i].parent;
        if( parent < 0 || parent >= model->count )
            rows_walk(model, i, 0, child_start, child_list);
    }
    ToriMem_Free(ToriMem_Editor(), child_start);
    ToriMem_Free(ToriMem_Editor(), child_list);
    ToriMem_Free(ToriMem_Editor(), fill);
}

int
EditorIfaceModel_Sync(
    struct EditorIfaceModel* model,
    const char* doc_key,
    const char* text,
    size_t length)
{
    struct Editor_IfFile file;
    uint64_t hash;
    struct EditorIfaceNode* old_nodes;
    int old_count;
    char selected_name[EDITOR_IFACE_NAME_MAX] = "";

    assert(model);
    assert(doc_key);
    assert(text || length == 0);

    hash = text_hash(text, length);
    if( strcmp(model->doc_key, doc_key) == 0 && model->nodes && model->text_hash == hash
        && model->text_length == length )
        return 0;
    if( strcmp(model->doc_key, doc_key) != 0 )
    {
        EditorIfaceModel_Free(model);
        snprintf(model->doc_key, sizeof(model->doc_key), "%s", doc_key);
    }

    old_nodes = model->nodes;
    old_count = model->count;
    if( model->selected >= 0 && model->selected < old_count )
        snprintf(selected_name, sizeof(selected_name), "%s", old_nodes[model->selected].name);

    Editor_IfIndex(&file, text, length);
    model->interface_id = file.interface_id;
    model->count = Editor_IfComponentCount(&file);
    model->nodes = ToriMem_Calloc(ToriMem_Editor(), (size_t)model->count + 1, sizeof(*model->nodes));
    assert(model->nodes);
    ToriMem_Free(ToriMem_Editor(), model->rows);
    model->rows = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * ((size_t)model->count + 1));
    assert(model->rows);
    model->selected = -1;
    for( int i = 0; i < model->count; i++ )
    {
        struct EditorIfaceNode* node = &model->nodes[i];
        char value[32];

        Editor_IfBlockName(&file, i, node->name, sizeof(node->name));
        node->parent = Editor_IfBlockParent(&file, i);
        node->type = Editor_IfBlockValue(&file, i, "type", value, sizeof(value)) == 1 ? atoi(value) : -1;
        node->if3 = !(Editor_IfBlockValue(&file, i, "if3", value, sizeof(value)) == 1
                      && strcmp(value, "no") == 0);
        node->hidden = Editor_IfBlockValue(&file, i, "hidden", value, sizeof(value)) == 1
            && strcmp(value, "yes") == 0;
        /* Roots start open; everything else keeps what it had, by name. */
        node->expanded = node->parent < 0;
        for( int j = 0; j < old_count; j++ )
        {
            if( strcmp(old_nodes[j].name, node->name) == 0 )
            {
                node->expanded = old_nodes[j].expanded;
                break;
            }
        }
        if( selected_name[0] && strcmp(selected_name, node->name) == 0 )
            model->selected = i;
    }
    Editor_IfFree(&file);
    ToriMem_Free(ToriMem_Editor(), old_nodes);
    model->text_hash = hash;
    model->text_length = length;
    model->generation++;
    rows_rebuild(model);
    return 1;
}

void
EditorIfaceModel_Toggle(
    struct EditorIfaceModel* model,
    int position)
{
    assert(model);
    if( position < 0 || position >= model->count )
        return;
    model->nodes[position].expanded = !model->nodes[position].expanded;
    model->generation++;
    rows_rebuild(model);
}

void
EditorIfaceModel_Select(
    struct EditorIfaceModel* model,
    int position)
{
    int at;
    int steps = 0;

    assert(model);
    if( position < -1 || position >= model->count )
        return;
    model->selected = position;
    at = position >= 0 ? model->nodes[position].parent : -1;
    while( at >= 0 && at < model->count && steps++ <= model->count )
    {
        model->nodes[at].expanded = 1;
        at = model->nodes[at].parent;
    }
    model->generation++;
    rows_rebuild(model);
}

int
EditorIfaceModel_FindComponent(
    const struct EditorIfaceModel* model,
    int component_id)
{
    int base;
    int child;

    assert(model);
    base = model->interface_id > 0 ? model->interface_id : 0;
    if( (component_id >> 16) != base )
        return -1;
    child = component_id & 0xFFFF;
    return child < model->count ? child : -1;
}

/* ---- inspector ----------------------------------------------------------- */

const char*
EditorIfaceModel_GroupName(int group)
{
    static const char* const names[EDITOR_IF_GROUP_COUNT] = {
        "Identity", "Geometry and modes", "Graphic", "Text",
        "Model",    "Inventory",          "Interaction", "Scripts",
    };
    assert(group >= 0);
    assert(group < EDITOR_IF_GROUP_COUNT);
    return names[group];
}

int
EditorIfaceModel_Fields(
    const char* text,
    size_t length,
    int position,
    int group,
    int with_absent,
    struct EditorIfaceField* out,
    int max)
{
    struct Editor_IfFile file;
    struct Editor_CfgBlock block;
    const struct Editor_CfgBlockRef* ref;
    int written = 0;

    assert(text || length == 0);
    assert(out || max == 0);
    assert(group >= 0);
    assert(group < EDITOR_IF_GROUP_COUNT);

    Editor_IfIndex(&file, text, length);
    if( position < 0 || position >= Editor_IfComponentCount(&file) )
    {
        Editor_IfFree(&file);
        return 0;
    }
    ref = &file.index.blocks[position];
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, text + ref->start, ref->end - ref->start);
    Editor_IfFree(&file);

    for( int line = 0; line < block.line_count && written < max; line++ )
    {
        const struct Editor_CfgLine* parsed = &block.lines[line];
        struct EditorIfaceField* field;
        size_t value_length;
        const char* value;
        int row;
        char key[EDITOR_CMD_NAME_MAX];

        if( parsed->kind != EDITOR_CFG_LINE_FIELD || parsed->part >= sizeof(key) )
            continue;
        snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block.text + parsed->start);
        row = Editor_IfKeyLookup(key, NULL);
        if( row < 0 || Editor_IfKeyAt(row)->group != group )
            continue;
        field = &out[written++];
        memset(field, 0, sizeof(*field));
        field->row = row;
        field->present = 1;
        snprintf(field->key, sizeof(field->key), "%s", key);
        value = Editor_CfgBlockValue(&block, line, &value_length);
        field->too_long = value_length + 2 > sizeof(field->raw);
        snprintf(field->raw, sizeof(field->raw), "%.*s", (int)value_length, value);
    }
    if( with_absent )
    {
        for( int row = 0; row < Editor_IfKeyCount() && written < max; row++ )
        {
            const struct Editor_IfKeyInfo* info = Editor_IfKeyAt(row);
            struct EditorIfaceField* field;

            if( info->group != group || info->indexed || info->kind == EDITOR_IF_VALUE_MARKER
                || Editor_CfgBlockFieldFind(&block, info->key, 0) >= 0 )
                continue;
            field = &out[written++];
            memset(field, 0, sizeof(*field));
            field->row = row;
            snprintf(field->key, sizeof(field->key), "%s", info->key);
        }
    }
    Editor_CfgBlockFree(&block);
    return written;
}

/* ---- plans --------------------------------------------------------------- */

int
EditorIfaceModel_PlanField(
    const struct EditorIfaceModel* model,
    const char* text,
    size_t length,
    int position,
    const char* key,
    const char* raw,
    struct Editor_Cmd* out)
{
    assert(model);
    assert(key);
    assert(out);
    return Editor_IfPlanField(text, length, model->doc_key, position, key, raw, out);
}

int
EditorIfaceModel_PlanAddChild(
    const struct EditorIfaceModel* model,
    int parent,
    struct Editor_Cmd* out)
{
    assert(model);
    assert(out);
    if( model->structure_locked || parent < -1 || parent >= model->count || model->interface_id < 0 )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_IFACE, model->doc_key, EDITOR_CMD_IF_COMPONENT);
    out->has_after = 1;
    out->u.if_component.component = model->count;
    out->u.if_component.index_before = -1;
    out->u.if_component.parent_before = -1;
    out->u.if_component.index_after = model->count;
    out->u.if_component.parent_after = parent;
    return 1;
}

int
EditorIfaceModel_PlanDelete(
    const struct EditorIfaceModel* model,
    const char* text,
    size_t length,
    int position,
    struct Editor_Cmd* out,
    int max)
{
    assert(model);
    if( model->structure_locked )
        return -1;
    return Editor_IfPlanDelete(text, length, model->doc_key, position, out, max);
}

int
EditorIfaceModel_PlanReparent(
    const struct EditorIfaceModel* model,
    int position,
    int parent,
    struct Editor_Cmd* out)
{
    assert(model);
    assert(out);
    if( model->structure_locked || position < 0 || position >= model->count || parent < -1
        || parent >= model->count || parent == position || model->nodes[position].parent < -1
        || model->nodes[position].parent == parent )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_IFACE, model->doc_key, EDITOR_CMD_IF_COMPONENT);
    out->has_before = 1;
    out->has_after = 1;
    out->u.if_component.component = position;
    out->u.if_component.index_before = position;
    out->u.if_component.index_after = position;
    out->u.if_component.parent_before = model->nodes[position].parent;
    out->u.if_component.parent_after = parent;
    return 1;
}

int
EditorIfaceModel_PlanMove(
    const struct EditorIfaceModel* model,
    int position,
    int index,
    struct Editor_Cmd* out)
{
    assert(model);
    assert(out);
    if( model->structure_locked || position < 0 || position >= model->count || index < 0
        || index >= model->count || index == position || model->nodes[position].parent < -1 )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_IFACE, model->doc_key, EDITOR_CMD_IF_COMPONENT);
    out->has_before = 1;
    out->has_after = 1;
    out->u.if_component.component = position;
    out->u.if_component.index_before = position;
    out->u.if_component.index_after = index;
    out->u.if_component.parent_before = model->nodes[position].parent;
    out->u.if_component.parent_after = model->nodes[position].parent;
    return 1;
}

/* ---- the CS2 script catalog ---------------------------------------------- */

static const char*
skip_space(const char* p)
{
    while( *p == ' ' || *p == '\t' )
        p++;
    return p;
}

int
EditorCs2_ParseHeader(
    const char* text,
    size_t length,
    struct EditorCs2Script* out)
{
    char buf[4096];
    const char* p;
    const char* comma;
    const char* close;

    assert(text || length == 0);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->id = -1;
    snprintf(buf, sizeof(buf), "%.*s", (int)(length < sizeof(buf) - 1 ? length : sizeof(buf) - 1), text);
    p = buf;
    /* `// <id>` first, as cachepack's and the compiler's pre-scan read it. */
    p = skip_space(p);
    while( *p == '\n' || *p == '\r' )
        p++;
    if( p[0] == '/' && p[1] == '/' )
    {
        p = skip_space(p + 2);
        if( *p >= '0' && *p <= '9' )
            out->id = atoi(p);
    }
    while( *p && *p != '[' )
    {
        const char* newline = strchr(p, '\n');
        if( !newline )
            return 0;
        p = newline + 1;
    }
    if( *p != '[' )
        return 0;
    p++;
    if( strncmp(p, "clientscript,", 13) == 0 )
    {
        out->clientscript = 1;
        p += 13;
    }
    else if( strncmp(p, "proc,", 5) == 0 )
        p += 5;
    else
        return 0;
    close = strchr(p, ']');
    if( !close )
        return 0;
    snprintf(out->name, sizeof(out->name), "%.*s", (int)(close - p), p);
    p = close + 1;
    if( *p != '(' )
        return 1;
    p++;
    while( *p && *p != ')' && *p != '\n' && out->arg_count < EDITOR_CS2_ARGS_MAX )
    {
        const char* end;
        int i = out->arg_count;
        char type[32];
        size_t type_length;

        p = skip_space(p);
        comma = strchr(p, ',');
        close = strchr(p, ')');
        end = comma && (!close || comma < close) ? comma : close;
        if( !end )
            break;
        snprintf(out->arg_decl[i], sizeof(out->arg_decl[i]), "%.*s", (int)(end - p), p);
        type_length = strcspn(p, " $),");
        snprintf(type, sizeof(type), "%.*s", (int)(type_length < sizeof(type) - 1 ? type_length : sizeof(type) - 1), p);
        if( strcmp(type, "string") == 0 )
            out->arg_kind[i] = EDITOR_CS2_ARG_STRING;
        else if( strcmp(type, "component") == 0 )
            out->arg_kind[i] = EDITOR_CS2_ARG_COMPONENT;
        else if( strcmp(type, "long") == 0 )
            out->arg_kind[i] = EDITOR_CS2_ARG_LONG;
        else
            out->arg_kind[i] = EDITOR_CS2_ARG_INT;
        out->arg_count++;
        p = *end == ',' ? end + 1 : end;
    }
    return 1;
}

static int
script_by_id(
    const void* a,
    const void* b)
{
    const struct EditorCs2Script* x = a;
    const struct EditorCs2Script* y = b;
    return (x->id > y->id) - (x->id < y->id);
}

int
EditorCs2Catalog_Load(
    struct EditorCs2Catalog* catalog,
    const char* scripts_dir)
{
    DIR* dir;
    struct dirent* entry;

    assert(catalog);
    assert(scripts_dir);

    memset(catalog, 0, sizeof(*catalog));
    dir = opendir(scripts_dir);
    if( !dir )
        return 0;
    while( (entry = readdir(dir)) )
    {
        size_t n = strlen(entry->d_name);
        char path[2048];
        char head[2048];
        size_t got;
        FILE* f;
        struct EditorCs2Script script;

        if( n < 5 || strcmp(entry->d_name + n - 4, ".cs2") != 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", scripts_dir, entry->d_name);
        f = fopen(path, "rb");
        if( !f )
            continue;
        got = fread(head, 1, sizeof(head) - 1, f);
        fclose(f);
        head[got] = '\0';
        if( !EditorCs2_ParseHeader(head, got, &script) || script.id < 0 )
            continue;
        if( catalog->count == catalog->capacity )
        {
            catalog->capacity = catalog->capacity ? catalog->capacity * 2 : 4096;
            catalog->scripts = ToriMem_Realloc(
                ToriMem_Editor(), catalog->scripts, sizeof(*catalog->scripts) * (size_t)catalog->capacity);
            assert(catalog->scripts);
        }
        catalog->scripts[catalog->count++] = script;
    }
    closedir(dir);
    if( catalog->count > 1 )
        qsort(catalog->scripts, (size_t)catalog->count, sizeof(*catalog->scripts), script_by_id);
    return catalog->count;
}

void
EditorCs2Catalog_Free(struct EditorCs2Catalog* catalog)
{
    if( !catalog )
        return;
    ToriMem_Free(ToriMem_Editor(), catalog->scripts);
    memset(catalog, 0, sizeof(*catalog));
}

int
EditorCs2Catalog_WriteNamesDir(
    const struct EditorCs2Catalog* catalog,
    const char* runestar_dir,
    const char* out_dir)
{
    char path[2048];
    FILE* f;

    assert(catalog);
    assert(out_dir);

#if defined(_WIN32)
    if( mkdir(out_dir) != 0 && errno != EEXIST )
        return 0;
#else
    if( mkdir(out_dir, 0755) != 0 && errno != EEXIST )
        return 0;
    if( runestar_dir && runestar_dir[0] )
    {
        DIR* dir = opendir(runestar_dir);
        struct dirent* entry;

        while( dir && (entry = readdir(dir)) )
        {
            size_t n = strlen(entry->d_name);
            char target[2048];

            if( n < 5 || strcmp(entry->d_name + n - 4, ".tsv") != 0
                || strcmp(entry->d_name, "script-names.tsv") == 0 )
                continue;
            snprintf(target, sizeof(target), "%s/%s", runestar_dir, entry->d_name);
            snprintf(path, sizeof(path), "%s/%s", out_dir, entry->d_name);
            unlink(path);
            if( symlink(target, path) != 0 )
            {
                closedir(dir);
                return 0;
            }
        }
        if( dir )
            closedir(dir);
    }
#endif
    snprintf(path, sizeof(path), "%s/script-names.tsv", out_dir);
    f = fopen(path, "wb");
    if( !f )
        return 0;
    for( int i = 0; i < catalog->count; i++ )
    {
        const struct EditorCs2Script* script = &catalog->scripts[i];
        fprintf(f, "%d\t[%s,%s]\n", script->id, script->clientscript ? "clientscript" : "proc", script->name);
    }
    return fclose(f) == 0;
}

const struct EditorCs2Script*
EditorCs2Catalog_Find(
    const struct EditorCs2Catalog* catalog,
    int id)
{
    int lo = 0;
    int hi;

    assert(catalog);
    hi = catalog->count - 1;
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        if( catalog->scripts[mid].id == id )
            return &catalog->scripts[mid];
        if( catalog->scripts[mid].id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int
EditorCs2Catalog_Search(
    const struct EditorCs2Catalog* catalog,
    const char* text,
    int* out,
    int max)
{
    int written = 0;

    assert(catalog);
    assert(text);
    assert(out || max == 0);

    for( int pass = 0; pass < 2; pass++ )
    {
        for( int i = 0; i < catalog->count && written < max; i++ )
        {
            const struct EditorCs2Script* script = &catalog->scripts[i];
            if( script->clientscript != (pass == 0) )
                continue;
            if( text[0] && !strstr(script->name, text) )
                continue;
            out[written++] = i;
        }
    }
    return written;
}

int
EditorCs2_BuildHook(
    const struct EditorCs2Script* script,
    const char* const* values,
    int value_count,
    struct Editor_IfHook* out)
{
    assert(script);
    assert(values || value_count == 0);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( value_count != script->arg_count || script->arg_count + 1 > EDITOR_IF_HOOK_ARGS_MAX )
        return 0;
    out->args[0].value = script->id;
    out->arg_count = 1;
    for( int i = 0; i < value_count; i++ )
    {
        struct Editor_IfHookArg* arg = &out->args[out->arg_count++];
        const char* value = values[i] ? values[i] : "";
        char* end = NULL;
        long number;
        int found = 0;

        if( script->arg_kind[i] == EDITOR_CS2_ARG_LONG )
            return 0;
        if( script->arg_kind[i] == EDITOR_CS2_ARG_STRING )
        {
            if( strlen(value) >= sizeof(arg->text) )
                return 0;
            arg->is_string = 1;
            snprintf(arg->text, sizeof(arg->text), "%s", value);
            continue;
        }
        for( int e = 0; e < Editor_IfEventArgCount(); e++ )
        {
            if( strcmp(value, Editor_IfEventArgAt(e)->name) == 0 )
            {
                arg->value = Editor_IfEventArgAt(e)->value;
                found = 1;
                break;
            }
        }
        if( found )
            continue;
        if( script->arg_kind[i] == EDITOR_CS2_ARG_COMPONENT && strchr(value, ':') )
        {
            int interface_id = 0;
            int child = 0;
            if( sscanf(value, "%d:%d", &interface_id, &child) != 2 || interface_id < 0
                || child < 0 || child > 0xFFFF )
                return 0;
            arg->value = (interface_id << 16) | child;
            continue;
        }
        number = strtol(value, &end, 0);
        if( !value[0] || !end || *end != '\0' || number < -2147483647L - 1 || number > 2147483647L )
            return 0;
        arg->value = (int)number;
    }
    return 1;
}

void
EditorCs2_DescribeHook(
    const struct EditorCs2Catalog* catalog,
    const char* raw,
    char* out,
    size_t out_size)
{
    struct Editor_IfHook* hook;
    size_t w = 0;

    assert(raw);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    hook = ToriMem_Alloc(ToriMem_Editor(), sizeof(*hook));
    assert(hook);
    if( !Editor_IfHookParse(raw, hook) || hook->arg_count == 0 || hook->args[0].is_string )
    {
        snprintf(out, out_size, "%s", raw);
        ToriMem_Free(ToriMem_Editor(), hook);
        return;
    }
    {
        const struct EditorCs2Script* script =
            catalog ? EditorCs2Catalog_Find(catalog, hook->args[0].value) : NULL;
        if( script )
            w += (size_t)snprintf(out + w, out_size - w, "%s(", script->name);
        else
            w += (size_t)snprintf(out + w, out_size - w, "script%d(", hook->args[0].value);
    }
    for( int i = 1; i < hook->arg_count && w < out_size; i++ )
    {
        const struct Editor_IfHookArg* arg = &hook->args[i];
        const char* event = arg->is_string ? NULL : Editor_IfEventArgName(arg->value);
        const char* sep = i > 1 ? ", " : "";

        if( arg->is_string )
            w += (size_t)snprintf(out + w, out_size - w, "%s\"%s\"", sep, arg->text);
        else if( event )
            w += (size_t)snprintf(out + w, out_size - w, "%s%s", sep, event);
        else if( arg->value > 0xFFFF )
            w += (size_t)snprintf(out + w, out_size - w, "%s%d:%d", sep, arg->value >> 16, arg->value & 0xFFFF);
        else
            w += (size_t)snprintf(out + w, out_size - w, "%s%d", sep, arg->value);
    }
    if( w < out_size )
        snprintf(out + w, out_size - w, ")");
    ToriMem_Free(ToriMem_Editor(), hook);
}
