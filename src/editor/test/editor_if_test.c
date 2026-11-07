/**
 * The interface codec (editor_if.h) over every interface in the tree.
 *
 *   editor_if_test <content-dir>
 *
 * Read-only. For each `interfaces/<name>.if` with its `.compack`:
 *
 *   ROUND TRIP  the preamble and the blocks are contiguous and cover the file;
 *               the preamble is cachepack's, byte for byte, with the right
 *               count; every block's lines are known keys, once each, in
 *               canonical order, then the blank separator.
 *   LAYER       every `layer=` is `(interface << 16) | child` with the child
 *               a block of this file, not itself, and the parent graph has
 *               no cycle; every `linked=` is a child of this file.
 *   TYPE        the `type=` set, by if3, printed; an IF3 type the decoder has
 *               no branch for fails.
 *   TYPED       every non-empty block reads into an RSCache_Dat2Component and
 *               re-emits its lines byte for byte (editor_if_typed.c); every
 *               hook parses and re-formats to its raw value.
 *   COMPACK     positional compacks are counted; the rest named.
 *   EDITS       per interface that allows them: a field set, add and remove,
 *               a component add, a move, and a planned subtree delete, each
 *               undone, must give back the exact bytes, and the forward edit
 *               must keep every original block's parent by name.
 *
 * Then IF_FIELD / IF_COMPONENT through Editor_CmdApply on a document.
 */

#include "editor/test/editor_test_text.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_if.h"
#include "mem/torirs_mem.h"

#include "datatypes/dat2_component.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        if( g_failures <= 40 )
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
    editor_test_text_lf(data, out_size);
    return data;
}

static int
compare_names(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/* ---- per-interface checks ------------------------------------------------ */

struct stats
{
    int files;
    int components;
    int empty_members;
    int lines;
    int not_exact;
    int bad_keys;
    int bad_order;
    int bad_layer;
    int bad_typed;
    int hand_blocks;
    int bad_hooks;
    int hooks;
    int positional;
    int locked;
    int long_values;
    int delete_refused;
    int type_if3[16];
    int type_if1[16];
    int edit_probes;
    int edit_failures;
    int edits_skipped;
};

static struct stats g_stats;
static char g_where[512];

/** The parent of every block, as a name ("" for none). */
static char**
parent_names(
    const char* text,
    size_t length,
    int* out_count)
{
    struct Editor_IfFile file;
    char** names;

    Editor_IfIndex(&file, text, length);
    *out_count = Editor_IfComponentCount(&file);
    names = calloc((size_t)*out_count + 1, sizeof(char*));
    for( int i = 0; i < *out_count; i++ )
    {
        char self[128];
        char parent[128] = "";
        int p = Editor_IfBlockParent(&file, i);
        char* row;

        Editor_IfBlockName(&file, i, self, sizeof(self));
        if( p >= 0 && p < *out_count )
            Editor_IfBlockName(&file, p, parent, sizeof(parent));
        row = malloc(strlen(self) + strlen(parent) + 2);
        sprintf(row, "%s>%s", self, parent);
        names[i] = row;
    }
    Editor_IfFree(&file);
    return names;
}

static void
free_names(
    char** names,
    int count)
{
    for( int i = 0; i < count; i++ )
        free(names[i]);
    free(names);
}

/** Every original (self>parent) row is still present in `after`, ignoring
 *  the rows of `ignore` (a block the edit added). */
static int
shape_kept(
    const char* before,
    size_t before_length,
    const char* after,
    size_t after_length)
{
    int a_count;
    int b_count;
    char** a = parent_names(before, before_length, &a_count);
    char** b = parent_names(after, after_length, &b_count);
    int kept = 1;

    qsort(b, (size_t)b_count, sizeof(char*), compare_names);
    for( int i = 0; i < a_count && kept; i++ )
        kept = bsearch(&a[i], b, (size_t)b_count, sizeof(char*), compare_names) != NULL;
    free_names(a, a_count);
    free_names(b, b_count);
    return kept;
}

/** Forward then inverse of `command` on `text`: exact bytes back. */
static int
probe(
    const char* text,
    size_t length,
    const struct Editor_Cmd* command,
    int keep_shape,
    const char* what)
{
    size_t forward_length = 0;
    size_t back_length = 0;
    char* forward = Editor_IfEdit(text, length, command, 1, &forward_length);
    char* back;
    int ok;
    char message[700];

    g_stats.edit_probes++;
    if( !forward )
    {
        snprintf(message, sizeof(message), "%s: %s does not apply", g_where, what);
        check(0, message);
        g_stats.edit_failures++;
        return 0;
    }
    back = Editor_IfEdit(forward, forward_length, command, 0, &back_length);
    ok = back && back_length == length && memcmp(back, text, length) == 0;
    snprintf(message, sizeof(message), "%s: %s undoes byte-exact", g_where, what);
    check(ok, message);
    if( ok && keep_shape )
    {
        ok = shape_kept(text, length, forward, forward_length);
        snprintf(message, sizeof(message), "%s: %s keeps every parent by name", g_where, what);
        check(ok, message);
    }
    if( !ok )
        g_stats.edit_failures++;
    ToriMem_Free(ToriMem_Editor(), forward);
    if( back )
        ToriMem_Free(ToriMem_Editor(), back);
    return ok;
}

static void
edit_probes(
    const char* name,
    const char* text,
    size_t length,
    int positional)
{
    struct Editor_IfFile file;
    struct Editor_Cmd command;
    char key[128];
    int count;
    int mid;
    int leaf = -1;

    snprintf(key, sizeof(key), "if:%s", name);
    Editor_IfIndex(&file, text, length);
    count = Editor_IfComponentCount(&file);
    if( count == 0 || file.interface_id < 0 )
    {
        Editor_IfFree(&file);
        g_stats.edits_skipped++;
        return;
    }
    mid = count / 2;

    /* Fields: a set (or insert) of x, an insert of `hidden`, a removal. */
    {
        char current[64];
        int has_x = Editor_IfBlockValue(&file, mid, "x", current, sizeof(current)) == 1;
        if( Editor_IfPlanField(text, length, key, mid, "x", has_x ? "-7" : "13", &command) )
            probe(text, length, &command, 1, has_x ? "set x" : "insert x");
        if( Editor_IfBlockValue(&file, mid, "hidden", current, sizeof(current)) == 0
            && Editor_IfPlanField(text, length, key, mid, "hidden", "yes", &command) )
            probe(text, length, &command, 1, "insert hidden");
        if( Editor_IfBlockValue(&file, mid, "type", current, sizeof(current)) == 1
            && Editor_IfPlanField(text, length, key, mid, "type", NULL, &command) )
            probe(text, length, &command, 1, "remove type");
    }

    if( !positional )
    {
        Editor_IfFree(&file);
        g_stats.edits_skipped++;
        return;
    }

    /* A component added mid-file under the root. */
    Editor_CmdInit(&command, EDITOR_DOC_IFACE, key, EDITOR_CMD_IF_COMPONENT);
    command.has_after = 1;
    command.u.if_component.index_after = mid;
    command.u.if_component.parent_after = count > 1 ? 0 : -1;
    command.u.if_component.parent_before = -1;
    command.u.if_component.index_before = -1;
    if( mid == 0 )
        command.u.if_component.parent_after = -1;
    probe(text, length, &command, 1, "add component");

    /* One appended at the end. */
    command.u.if_component.index_after = count;
    command.u.if_component.parent_after = -1;
    probe(text, length, &command, 1, "append component");

    /* The last block moved to the front (needs a blank-ended file). */
    if( count >= 3 && length >= 2 && text[length - 1] == '\n' && text[length - 2] == '\n' )
    {
        int parent = Editor_IfBlockParent(&file, count - 1);
        if( parent >= -1 )
        {
            Editor_CmdInit(&command, EDITOR_DOC_IFACE, key, EDITOR_CMD_IF_COMPONENT);
            command.has_before = 1;
            command.has_after = 1;
            command.u.if_component.index_before = count - 1;
            command.u.if_component.index_after = 1;
            command.u.if_component.parent_before = parent;
            command.u.if_component.parent_after = parent;
            probe(text, length, &command, 1, "move last to 1");
        }
    }

    /* Delete the last leaf through the plan; undo it command by command. */
    for( int i = count - 1; i >= 0 && leaf < 0; i-- )
    {
        int has_child = 0;
        for( int j = 0; j < count && !has_child; j++ )
            has_child = Editor_IfBlockParent(&file, j) == i;
        if( !has_child )
            leaf = i;
    }
    Editor_IfFree(&file);
    if( leaf >= 0 )
    {
        struct Editor_Cmd* plan = malloc(sizeof(*plan) * 256);
        int planned = Editor_IfPlanDelete(text, length, key, leaf, plan, 256);
        char message[700];

        g_stats.edit_probes++;
        if( planned < 0 )
        {
            /* A value too long for one command: not offered, and counted. */
            g_stats.delete_refused++;
        }
        else
        {
            char* work = malloc(length + 1);
            size_t work_length = length;
            int ok = 1;

            memcpy(work, text, length);
            for( int i = 0; i < planned && ok; i++ )
            {
                size_t next_length = 0;
                char* next = Editor_IfEdit(work, work_length, &plan[i], 1, &next_length);
                ok = next != NULL;
                if( next )
                {
                    free(work);
                    work = malloc(next_length + 1);
                    memcpy(work, next, next_length);
                    work_length = next_length;
                    ToriMem_Free(ToriMem_Editor(), next);
                }
            }
            if( ok )
            {
                struct Editor_IfFile after;
                Editor_IfIndex(&after, work, work_length);
                ok = Editor_IfComponentCount(&after) == count - 1;
                Editor_IfFree(&after);
            }
            for( int i = planned - 1; i >= 0 && ok; i-- )
            {
                size_t next_length = 0;
                char* next = Editor_IfEdit(work, work_length, &plan[i], 0, &next_length);
                ok = next != NULL;
                if( next )
                {
                    free(work);
                    work = malloc(next_length + 1);
                    memcpy(work, next, next_length);
                    work_length = next_length;
                    ToriMem_Free(ToriMem_Editor(), next);
                }
            }
            ok = ok && work_length == length && memcmp(work, text, length) == 0;
            snprintf(message, sizeof(message), "%s: planned delete of leaf %d undoes byte-exact", g_where, leaf);
            check(ok, message);
            if( !ok )
                g_stats.edit_failures++;
            free(work);
        }
        free(plan);
    }
}

static void
check_interface(
    const char* dir,
    const char* name)
{
    char path[2048];
    size_t length = 0;
    size_t compack_length = 0;
    char* text;
    char* compack;
    struct Editor_IfFile file;
    char message[700];
    int count;
    int exact = 1;
    int* parents;

    snprintf(g_where, sizeof(g_where), "%s", name);
    snprintf(path, sizeof(path), "%s/%s.if", dir, name);
    text = slurp(path, &length);
    snprintf(path, sizeof(path), "%s/%s.compack", dir, name);
    compack = slurp(path, &compack_length);
    snprintf(message, sizeof(message), "%s: .if and .compack read", name);
    check(text && compack, message);
    if( !text || !compack )
    {
        free(text);
        free(compack);
        return;
    }
    g_stats.files++;

    Editor_IfIndex(&file, text, length);
    count = Editor_IfComponentCount(&file);
    g_stats.components += count;

    /* Coverage: preamble then contiguous blocks to the end. */
    if( count > 0 )
    {
        exact = file.index.preamble_end == file.index.blocks[0].start
            && file.index.blocks[count - 1].end == length;
        for( int i = 0; i + 1 < count && exact; i++ )
            exact = file.index.blocks[i].end == file.index.blocks[i + 1].start;
    }
    {
        char preamble[1024];
        int written = Editor_IfEmitPreamble(name, file.interface_id, count, preamble, sizeof(preamble));
        int same = file.interface_id >= 0 && file.stated_count == count
            && (size_t)written == file.index.preamble_end
            && memcmp(preamble, text, (size_t)written) == 0;
        snprintf(message, sizeof(message), "%s: preamble is cachepack's (id %d, %d of %d)", name,
                 file.interface_id, file.stated_count, count);
        check(same, message);
        exact = exact && same;
    }

    parents = malloc(sizeof(int) * (size_t)(count + 1));
    for( int position = 0; position < count; position++ )
    {
        struct Editor_CfgBlock block;
        const struct Editor_CfgBlockRef* ref = &file.index.blocks[position];
        int header;
        int last_rank = -1;
        int seen_blank = 0;
        int is_empty = 0;
        int if3 = 1;
        int type = -1;
        int canonical = 1;
        char key[128];

        Editor_CfgBlockInit(&block);
        Editor_CfgBlockSet(&block, text + ref->start, ref->end - ref->start);
        header = Editor_CfgBlockHeader(&block);
        if( header != 0 )
            exact = 0;
        for( int line = 1; line < block.line_count; line++ )
        {
            const struct Editor_CfgLine* parsed = &block.lines[line];
            size_t value_length;
            const char* value;
            int rank;
            int hook_exempt = 0;

            g_stats.lines++;
            if( parsed->kind == EDITOR_CFG_LINE_BLANK )
            {
                seen_blank = 1;
                continue;
            }
            if( parsed->kind != EDITOR_CFG_LINE_FIELD || seen_blank || parsed->eol != EDITOR_CFG_EOL_LF )
            {
                exact = 0;
                continue;
            }
            snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block.text + parsed->start);
            rank = Editor_IfKeyRank(key);
            if( rank < 0 )
            {
                g_stats.bad_keys++;
                snprintf(message, sizeof(message), "%s[%d]: unknown key %s", name, position, key);
                check(0, message);
                continue;
            }
            if( rank <= last_rank )
            {
                /* Hand-ordered: legal for cachepack, so counted, not failed;
                 * the typed layer below still holds it to its meaning. */
                g_stats.bad_order++;
                canonical = 0;
            }
            last_rank = rank;
            value = Editor_CfgBlockValue(&block, line, &value_length);
            if( value_length + 2 > EDITOR_CMD_VALUE_MAX )
                g_stats.long_values++;
            {
                char* raw = malloc(value_length + 1);
                memcpy(raw, value, value_length);
                raw[value_length] = '\0';
                if( strcmp(key, "empty") == 0 )
                    is_empty = 1;
                if( strcmp(key, "if3") == 0 )
                    if3 = strcmp(raw, "yes") == 0;
                if( strcmp(key, "type") == 0 )
                    type = atoi(raw);
                if( !Editor_IfValueValid(key, raw) )
                {
                    /* Not as cachepack's emitter writes it (an unescaped `//`
                     * or trailing blank): cachepack reads less than shows. */
                    canonical = 0;
                    printf("  non-canonical value %s[%d] %s=%s\n", name, position, key, raw);
                    hook_exempt = 1;
                }
                if( Editor_IfKeyAt(Editor_IfKeyLookup(key, NULL))->kind == EDITOR_IF_VALUE_HOOK )
                {
                    struct Editor_IfHook* hook = malloc(sizeof(*hook));
                    char* again = malloc(value_length * 2 + 64);
                    int round = 0;

                    g_stats.hooks++;
                    if( Editor_IfHookParse(raw, hook) )
                    {
                        size_t needed = Editor_IfHookFormat(hook, again, value_length * 2 + 64);
                        round = needed == value_length && strcmp(again, raw) == 0;
                    }
                    if( !round && !hook_exempt )
                    {
                        g_stats.bad_hooks++;
                        snprintf(message, sizeof(message), "%s[%d]: %s hook re-formats", name, position, key);
                        check(0, message);
                    }
                    free(hook);
                    free(again);
                }
                free(raw);
            }
        }
        if( is_empty )
            g_stats.empty_members++;
        else if( type >= 0 && type < 16 )
        {
            if( if3 )
                g_stats.type_if3[type]++;
            else
                g_stats.type_if1[type]++;
        }

        /* Layer: the parent's full id. */
        parents[position] = Editor_IfBlockParent(&file, position);
        {
            char raw[64];
            if( Editor_IfBlockValue(&file, position, "layer", raw, sizeof(raw)) == 1 )
            {
                long full = strtol(raw, NULL, 10);
                int ok = (full >> 16) == file.interface_id && parents[position] >= 0
                    && parents[position] < count && parents[position] != position;
                if( !ok )
                {
                    g_stats.bad_layer++;
                    snprintf(message, sizeof(message), "%s[%d]: layer=%s is (%d << 16) | child", name,
                             position, raw, file.interface_id);
                    check(0, message);
                }
            }
            if( Editor_IfBlockValue(&file, position, "linked", raw, sizeof(raw)) == 1 )
            {
                long linked = strtol(raw, NULL, 10);
                snprintf(message, sizeof(message), "%s[%d]: linked=%s is a child here", name, position, raw);
                check(linked >= 0 && linked < count, message);
            }
        }

        /* Typed: cachepack's reader then its emitter give the lines back. */
        if( !is_empty )
        {
            struct RSCache_Dat2Component component;
            char bad[64] = "";
            int read = Editor_IfBlockReadComponent(&file, position, &component, bad, sizeof(bad));
            size_t field_start = block.lines[header].start + block.lines[header].length + 1;
            size_t field_end = field_start;
            size_t needed;
            char* emitted;
            int same = 0;

            for( int line = 1; line < block.line_count; line++ )
            {
                if( block.lines[line].kind == EDITOR_CFG_LINE_FIELD )
                    field_end = block.lines[line].start + block.lines[line].length + 1;
            }
            if( read )
            {
                needed = Editor_IfComponentEmit(&component, NULL, 0);
                emitted = malloc(needed + 1);
                Editor_IfComponentEmit(&component, emitted, needed + 1);
                if( canonical )
                    same = needed == field_end - field_start
                        && memcmp(emitted, block.text + field_start, needed) == 0;
                else
                {
                    /* A hand-written block: its meaning must survive one more
                     * read and emit unchanged. */
                    char* again_text = malloc(needed + 8);
                    struct Editor_IfFile again;
                    struct RSCache_Dat2Component second;
                    size_t again_needed;
                    char* emitted2;

                    g_stats.hand_blocks++;
                    sprintf(again_text, "[x]\n%s", emitted);
                    Editor_IfIndex(&again, again_text, strlen(again_text));
                    if( Editor_IfBlockReadComponent(&again, 0, &second, NULL, 0) )
                    {
                        again_needed = Editor_IfComponentEmit(&second, NULL, 0);
                        emitted2 = malloc(again_needed + 1);
                        Editor_IfComponentEmit(&second, emitted2, again_needed + 1);
                        same = again_needed == needed && memcmp(emitted, emitted2, needed) == 0;
                        free(emitted2);
                    }
                    RSCache_Dat2ComponentFree(&second);
                    Editor_IfFree(&again);
                    free(again_text);
                }
                if( !same && g_stats.bad_typed < 5 )
                    fprintf(stderr, "--- %s[%d] typed emit:\n%s--- file:\n%.*s", name, position,
                            emitted, (int)(field_end - field_start), block.text + field_start);
                free(emitted);
            }
            if( !same )
            {
                g_stats.bad_typed++;
                snprintf(message, sizeof(message), "%s[%d]: typed read%s%s and emit is byte-exact",
                         name, position, read ? "" : " fails at ", bad);
                check(0, message);
            }
            RSCache_Dat2ComponentFree(&component);
        }
        Editor_CfgBlockFree(&block);
    }

    /* No cycle in the parent graph. */
    for( int position = 0; position < count; position++ )
    {
        int at = parents[position];
        int steps = 0;
        while( at >= 0 && at < count && steps <= count )
        {
            at = parents[at];
            steps++;
        }
        if( steps > count )
        {
            snprintf(message, sizeof(message), "%s[%d]: parent chain cycles", name, position);
            check(0, message);
            g_stats.bad_layer++;
            break;
        }
    }
    free(parents);

    if( !exact )
    {
        g_stats.not_exact++;
        snprintf(message, sizeof(message), "%s: re-emits byte-exact", name);
        check(0, message);
    }

    {
        int positional = Editor_IfCompackPositional(compack, compack_length, text, length);
        char* saved = NULL;
        size_t saved_length = 0;
        int kept;

        if( positional )
            g_stats.positional++;
        else
        {
            g_stats.locked++;
            printf("  compack not positional (structure edits locked): %s\n", name);
        }
        kept = Editor_IfCompackForSave(compack, compack_length, text, length, text, length, &saved,
                                       &saved_length);
        snprintf(message, sizeof(message), "%s: an unchanged .if keeps its compack", name);
        check(kept == 1 && saved == NULL, message);
        Editor_IfFree(&file);
        edit_probes(name, text, length, positional);
    }
    free(text);
    free(compack);
}

/* ---- small texts --------------------------------------------------------- */

static const char k_small[] =
    "// Interface 7 \xE2\x80\x94 3 components.\n"
    "// One block per component; small.compack ties each block name to its\n"
    "// file id in the archive. `if3` selects the field set.\n\n"
    "[universe]\nif3=yes\ntype=0\nwidthmode=1\nheightmode=1\nname=\ntargetverb=\n\n"
    "[panel]\nif3=yes\ntype=0\nx=4\nlayer=458752\nname=\ntargetverb=\nonload=i:100,i:458754,s:a\\\\,b\n\n"
    "[label]\nif3=yes\ntype=4\nlayer=458753\ntext=Hello\nname=\ntargetverb=\n\n";

static const char k_small_compack[] = "0=universe\n// the panel\n1=panel\n2=label\n";

static void
small_texts(void)
{
    struct Editor_Doc* doc = calloc(1, sizeof(*doc));
    struct Editor_DocHandle handle;
    struct Editor_Cmd command;
    struct Editor_IfFile file;
    size_t length = strlen(k_small);
    char* next;
    size_t next_length = 0;
    struct Editor_IfHook hook;

    Editor_IfIndex(&file, k_small, length);
    check(file.interface_id == 7 && Editor_IfComponentCount(&file) == 3, "small: preamble and blocks");
    check(Editor_IfBlockParent(&file, 2) == 1, "small: layer=458753 is child 1 of interface 7");
    check(Editor_IfCompackPositional(k_small_compack, strlen(k_small_compack), k_small, length),
          "small: compack with a comment is positional");
    Editor_IfFree(&file);

    /* An empty value is present; "" is absent. */
    check(Editor_IfPlanField(k_small, length, "if:small", 1, "name", NULL, &command)
              && strcmp(command.u.if_field.before, "=") == 0 && command.u.if_field.after[0] == '\0',
          "small: removing `name=` spells before as \"=\"");
    check(!Editor_IfPlanField(k_small, length, "if:small", 1, "x", "four", &command),
          "small: a non-number for an int key is refused");
    check(!Editor_IfPlanField(k_small, length, "if:small", 1, "bogus", "1", &command),
          "small: an unknown key is refused");
    check(!Editor_IfPlanField(k_small, length, "if:small", 1, "@name", "label", &command),
          "small: a rename onto a taken name is refused");

    /* Canonical insert: y goes after x, before layer. */
    check(Editor_IfPlanField(k_small, length, "if:small", 1, "y", "9", &command), "small: plan y");
    next = Editor_IfEdit(k_small, length, &command, 1, &next_length);
    check(next && strstr(next, "x=4\ny=9\nlayer=458752\n") != NULL, "small: y lands between x and layer");
    if( next )
        ToriMem_Free(ToriMem_Editor(), next);

    /* Add at 1 under the root: the panel becomes 2, the label's layer and
     * the panel's hook argument follow it, the count says 4. */
    Editor_CmdInit(&command, EDITOR_DOC_IFACE, "if:small", EDITOR_CMD_IF_COMPONENT);
    command.has_after = 1;
    command.u.if_component.index_after = 1;
    command.u.if_component.parent_after = 0;
    next = Editor_IfEdit(k_small, length, &command, 1, &next_length);
    check(next && strstr(next, "[com_1]\nif3=yes\ntype=0\nlayer=458752\nname=\ntargetverb=\n\n[panel]") != NULL,
          "small: the fresh block lands at 1");
    check(next && strstr(next, "layer=458754\ntext=Hello") != NULL, "small: the label's layer follows the panel");
    check(next && strstr(next, "onload=i:100,i:458755,s:a\\\\,b") != NULL,
          "small: a hook argument naming a moved component follows it");
    check(next && strstr(next, "\xE2\x80\x94 4 components.") != NULL, "small: the preamble counts 4");
    {
        char* saved = NULL;
        size_t saved_length = 0;
        int ok = next && Editor_IfCompackForSave(k_small_compack, strlen(k_small_compack), k_small,
                                                 length, next, next_length, &saved, &saved_length);
        check(ok && saved && strcmp(saved, "0=universe\n// the panel\n1=com_1\n2=panel\n3=label\n") == 0,
              "small: the compack is rewritten positionally, its comment kept");
        if( saved )
            ToriMem_Free(ToriMem_Editor(), saved);
    }
    if( next )
        ToriMem_Free(ToriMem_Editor(), next);

    /* A reparent that would cycle is refused. */
    Editor_CmdInit(&command, EDITOR_DOC_IFACE, "if:small", EDITOR_CMD_IF_COMPONENT);
    command.has_before = 1;
    command.has_after = 1;
    command.u.if_component.index_before = 1;
    command.u.if_component.index_after = 1;
    command.u.if_component.parent_before = 0;
    command.u.if_component.parent_after = 2;
    check(Editor_IfEdit(k_small, length, &command, 1, &next_length) == NULL,
          "small: reparenting the panel under its own child is refused");

    /* A component still referenced cannot be removed. */
    Editor_CmdInit(&command, EDITOR_DOC_IFACE, "if:small", EDITOR_CMD_IF_COMPONENT);
    command.has_before = 1;
    command.u.if_component.index_before = 1;
    command.u.if_component.parent_before = 0;
    check(Editor_IfEdit(k_small, length, &command, 1, &next_length) == NULL,
          "small: removing a non-fresh, referenced block is refused");

    /* Hooks. */
    check(Editor_IfHookParse("i:100,i:458754,s:a\\\\,b", &hook) && hook.arg_count == 3
              && hook.args[2].is_string && strcmp(hook.args[2].text, "a,b") == 0,
          "small: a hook's string argument keeps its comma");
    check(strcmp(Editor_IfEventArgName(-2147483645), "event_com") == 0, "small: event placeholders named");

    /* Through Editor_CmdApply on an IFACE document. */
    Editor_DocInit(doc, NULL);
    check(Editor_DocOpen(doc, EDITOR_DOC_IFACE, "if:small", &handle) && handle.text, "doc: open if:small");
    Editor_TextDocSet(handle.text, k_small, length);
    check(Editor_IfPlanField(k_small, length, "if:small", 2, "text", "Bye", &command), "doc: plan text");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 1, "doc: IF_FIELD applies");
    check(strstr(handle.text->text, "text=Bye\n") != NULL && handle.text->dirty, "doc: text changed, dirty");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD) == 0, "doc: a repeated IF_FIELD does not fit");
    check(Editor_CmdApply(doc, &command, EDITOR_CMD_INVERSE) == 1
              && handle.text->length == length && memcmp(handle.text->text, k_small, length) == 0,
          "doc: IF_FIELD undoes byte-exact");
    {
        struct Editor_Cmd plan[32];
        int planned = Editor_IfPlanDelete(k_small, length, "if:small", 1, plan, 32);
        int ok = planned > 0;
        for( int i = 0; i < planned && ok; i++ )
            ok = Editor_CmdApply(doc, &plan[i], EDITOR_CMD_FORWARD) == 1;
        check(ok && strstr(handle.text->text, "[panel]") == NULL && strstr(handle.text->text, "[label]") == NULL
                  && strstr(handle.text->text, "1 components.") != NULL,
              "doc: the planned delete takes the panel and its child");
        for( int i = planned - 1; i >= 0 && ok; i-- )
            ok = Editor_CmdApply(doc, &plan[i], EDITOR_CMD_INVERSE) == 1;
        check(ok && handle.text->length == length && memcmp(handle.text->text, k_small, length) == 0,
              "doc: undoing the planned delete restores every byte");
    }
    {
        size_t fresh_length = 0;
        char* fresh = Editor_IfNewInterfaceText("made", 1234, &fresh_length);
        struct Editor_IfFile made;
        Editor_IfIndex(&made, fresh, fresh_length);
        check(made.interface_id == 1234 && made.stated_count == 1 && Editor_IfComponentCount(&made) == 1,
              "new interface: preamble and root block");
        Editor_IfFree(&made);
        ToriMem_Free(ToriMem_Editor(), fresh);
    }
    Editor_DocFree(doc);
    free(doc);
}

int
main(
    int argc,
    char** argv)
{
    char dir[2048];
    DIR* listing;
    struct dirent* entry;
    char** names = NULL;
    int name_count = 0;
    int name_capacity = 0;
    clock_t started;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: editor_if_test <content-dir>\n");
        return 2;
    }
    small_texts();

    snprintf(dir, sizeof(dir), "%s/interfaces", argv[1]);
    listing = opendir(dir);
    check(listing != NULL, "interfaces/ opens");
    while( listing && (entry = readdir(listing)) )
    {
        size_t n = strlen(entry->d_name);
        if( n < 4 || strcmp(entry->d_name + n - 3, ".if") != 0 )
            continue;
        if( name_count == name_capacity )
        {
            name_capacity = name_capacity ? name_capacity * 2 : 1024;
            names = realloc(names, sizeof(char*) * (size_t)name_capacity);
        }
        names[name_count] = malloc(n - 2);
        memcpy(names[name_count], entry->d_name, n - 3);
        names[name_count][n - 3] = '\0';
        name_count++;
    }
    if( listing )
        closedir(listing);
    qsort(names, (size_t)name_count, sizeof(char*), compare_names);

    started = clock();
    for( int i = 0; i < name_count; i++ )
    {
        check_interface(dir, names[i]);
        free(names[i]);
    }
    free(names);

    printf("interfaces: %d files, %d components (%d empty members), %d lines\n", g_stats.files,
           g_stats.components, g_stats.empty_members, g_stats.lines);
    printf("  round trip: %d not byte-exact, %d unknown keys, %d bad layer\n", g_stats.not_exact,
           g_stats.bad_keys, g_stats.bad_layer);
    printf("  typed: %d blocks failed through RSCache_Dat2Component (byte-exact for canonical blocks; "
           "%d hand-written blocks, %d lines out of canonical order, held to read-emit-read)\n",
           g_stats.bad_typed, g_stats.hand_blocks, g_stats.bad_order);
    printf("  hooks: %d, %d not re-formatting to their raw value\n", g_stats.hooks, g_stats.bad_hooks);
    printf("  layer= is (interface << 16) | child on every line that has one\n");
    printf("  type set, IF3:");
    for( int t = 0; t < 16; t++ )
        if( g_stats.type_if3[t] )
            printf(" %d(%d)", t, g_stats.type_if3[t]);
    printf("; IF1:");
    for( int t = 0; t < 16; t++ )
        if( g_stats.type_if1[t] )
            printf(" %d(%d)", t, g_stats.type_if1[t]);
    printf("\n");
    {
        static const int decoder_if3[] = { 0, 3, 4, 5, 6, 9 };
        int unknown = 0;
        for( int t = 0; t < 16; t++ )
        {
            int known = 0;
            for( size_t k = 0; k < sizeof(decoder_if3) / sizeof(decoder_if3[0]); k++ )
                known = known || decoder_if3[k] == t;
            if( g_stats.type_if3[t] && !known )
                unknown++;
        }
        check(unknown == 0, "every IF3 type= is one the dat2 IF3 decoder branches on");
    }
    printf("  compack: %d positional, %d locked for structure edits\n", g_stats.positional, g_stats.locked);
    printf("  edits: %d probes, %d failed, %d interfaces field-only or skipped\n", g_stats.edit_probes,
           g_stats.edit_failures, g_stats.edits_skipped);
    printf("  %d lines hold a value longer than one IF_FIELD carries (%d bytes); %d leaf deletes "
           "not offered for that reason\n",
           g_stats.long_values, EDITOR_CMD_VALUE_MAX - 2, g_stats.delete_refused);
    printf("  %.1f s\n", (double)(clock() - started) / CLOCKS_PER_SEC);
    check(g_stats.files == 982, "all 982 interfaces read");
    check(g_stats.not_exact == 0, "every interface re-emits byte-exact");
    check(g_stats.bad_typed == 0, "every block is byte-exact through the typed layer");
    printf("editor_if_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
