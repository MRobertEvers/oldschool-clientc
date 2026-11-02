#include "builder_shell.h"

#include "editor/editor_cfg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char*
slot_prefix(enum Builder_Subject subject)
{
    return subject == BUILDER_SUBJECT_NPC ? "multinpc" : "multiloc";
}

/** N for "multiloc<N>" (N >= 1), else 0. */
static int
slot_number(
    const char* prefix,
    const char* key,
    size_t key_length)
{
    size_t prefix_length = strlen(prefix);
    int number = 0;

    if( key_length <= prefix_length || memcmp(key, prefix, prefix_length) != 0 )
        return 0;
    for( size_t i = prefix_length; i < key_length; i++ )
    {
        if( key[i] < '0' || key[i] > '9' )
            return 0;
        number = number * 10 + (key[i] - '0');
        if( number > 100000 )
            return 0;
    }
    if( key[prefix_length] == '0' )
        return 0;
    return number;
}

static int
var_unset(const char* value)
{
    return value[0] == '\0' || strcmp(value, "-1") == 0 || strcmp(value, "default") == 0;
}

static void
field_value(
    const struct Editor_CfgBlock* block,
    int line,
    char* out,
    size_t out_size)
{
    size_t length = 0;
    const char* value = Editor_CfgBlockValue(block, line, &length);

    if( length >= out_size )
        length = out_size - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

int
Builder_ShellParse(
    struct Builder_Shell* out,
    enum Builder_Subject subject,
    const char* block_text,
    size_t length)
{
    struct Editor_CfgBlock block;
    const char* prefix = slot_prefix(subject);
    int header;

    assert(out);
    assert(block_text || length == 0);

    memset(out, 0, sizeof(*out));
    out->subject = subject;
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, block_text ? block_text : "", length);
    header = Editor_CfgBlockHeader(&block);
    if( header >= 0 )
    {
        size_t name_length = block.lines[header].part;
        if( name_length >= sizeof(out->name) )
            name_length = sizeof(out->name) - 1;
        memcpy(out->name, block.text + block.lines[header].start + 1, name_length);
        out->name[name_length] = '\0';
    }

    for( int i = 0; i < block.line_count; i++ )
    {
        const struct Editor_CfgLine* line = &block.lines[i];
        const char* key = block.text + line->start;
        char value[BUILDER_NAME_MAX];
        int number;

        if( line->kind != EDITOR_CFG_LINE_FIELD )
            continue;
        field_value(&block, i, value, sizeof(value));
        if( line->part == 11 && memcmp(key, "multivarbit", 11) == 0 )
        {
            if( !var_unset(value) )
            {
                out->var_kind = BUILDER_SHELL_VAR_VARBIT;
                snprintf(out->var, sizeof(out->var), "%s", value);
            }
        }
        else if( line->part == 9 && memcmp(key, "multivarp", 9) == 0 )
        {
            if( !var_unset(value) )
            {
                out->var_kind = BUILDER_SHELL_VAR_VARP;
                snprintf(out->var, sizeof(out->var), "%s", value);
            }
        }
        else if( line->part == 16 && memcmp(key, "multidefaultnone", 16) == 0 )
            out->default_none = strcmp(value, "yes") == 0;
        else if( (number = slot_number(prefix, key, line->part)) > 0 )
        {
            if( number > BUILDER_SHELL_SLOTS_MAX )
            {
                out->overflow = 1;
                continue;
            }
            while( out->slot_count < number )
                snprintf(out->slots[out->slot_count++].child, BUILDER_NAME_MAX, "-1");
            snprintf(out->slots[number - 1].child, BUILDER_NAME_MAX, "%s", value);
        }
    }
    Editor_CfgBlockFree(&block);
    return out->var_kind != BUILDER_SHELL_VAR_NONE && out->slot_count > 0;
}

int
Builder_ShellResolve(
    const struct Builder_Shell* shell,
    int value)
{
    assert(shell);

    if( shell->slot_count <= 0 )
        return -1;
    if( value >= 0 && value < shell->slot_count - 1 )
        return value;
    return shell->slot_count - 1;
}

int
Builder_ShellSlotHidden(const struct Builder_ShellSlot* slot)
{
    assert(slot);
    return slot->child[0] == '\0' || strcmp(slot->child, "-1") == 0;
}

const char*
Builder_ShellResolveChild(
    const struct Builder_Shell* shell,
    int value)
{
    int slot = Builder_ShellResolve(shell, value);

    if( slot < 0 || Builder_ShellSlotHidden(&shell->slots[slot]) )
        return NULL;
    return shell->slots[slot].child;
}

void
Builder_ShellSlotValueLabel(
    const struct Builder_Shell* shell,
    int index,
    char* out,
    size_t out_size)
{
    assert(shell);
    assert(out);
    assert(index >= 0 && index < shell->slot_count);

    if( index == shell->slot_count - 1 )
        snprintf(out, out_size, "default (%d+)", index);
    else
        snprintf(out, out_size, "%d", index);
}

void
Builder_ShellSummary(
    const struct Builder_Shell* shell,
    char* out,
    size_t out_size)
{
    size_t used = 0;
    int start = 0;

    assert(shell);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    if( shell->slot_count == 0 )
    {
        snprintf(out, out_size, "not a shell");
        return;
    }
    while( start < shell->slot_count )
    {
        int hidden = Builder_ShellSlotHidden(&shell->slots[start]);
        int end = start;
        int written;

        while( end + 1 < shell->slot_count
               && Builder_ShellSlotHidden(&shell->slots[end + 1]) == hidden )
            end++;
        if( end == shell->slot_count - 1 )
        {
            if( start == 0 )
                written = snprintf(out + used, out_size - used, "%s%s at every value",
                                   used ? ", " : "", hidden ? "hidden" : "visible");
            else
                written = snprintf(out + used, out_size - used, "%s%s from %d", used ? ", " : "",
                                   hidden ? "hidden" : "visible", start);
        }
        else if( start == end )
            written = snprintf(out + used, out_size - used, "%s%s at %d", used ? ", " : "",
                               hidden ? "hidden" : "visible", start);
        else
            written = snprintf(out + used, out_size - used, "%s%s at %d-%d", used ? ", " : "",
                               hidden ? "hidden" : "visible", start, end);
        if( written < 0 || (size_t)written >= out_size - used )
            return;
        used += (size_t)written;
        start = end + 1;
    }
}

int
Builder_ShellInsertSlot(
    struct Builder_Shell* shell,
    int index,
    const char* child)
{
    assert(shell);
    assert(child);
    assert(index >= 0 && index <= shell->slot_count);

    if( shell->slot_count >= BUILDER_SHELL_SLOTS_MAX )
        return 0;
    memmove(&shell->slots[index + 1], &shell->slots[index],
            sizeof(shell->slots[0]) * (size_t)(shell->slot_count - index));
    snprintf(shell->slots[index].child, BUILDER_NAME_MAX, "%s", child);
    shell->slot_count++;
    return 1;
}

void
Builder_ShellRemoveSlot(
    struct Builder_Shell* shell,
    int index)
{
    assert(shell);
    assert(index >= 0 && index < shell->slot_count);

    memmove(&shell->slots[index], &shell->slots[index + 1],
            sizeof(shell->slots[0]) * (size_t)(shell->slot_count - index - 1));
    shell->slot_count--;
}

/* ---- commands ------------------------------------------------------------ */

struct command_list
{
    const char* doc_key;
    struct Editor_Cmd* out;
    int max;
    int count;
};

static void
push_field(
    struct command_list* list,
    const char* spec,
    const char* before,
    const char* after)
{
    if( list->count < list->max )
    {
        struct Editor_Cmd* command = &list->out[list->count];
        Editor_CmdInit(command, EDITOR_DOC_CFG, list->doc_key, EDITOR_CMD_CFG_FIELD);
        command->has_before = 1;
        command->has_after = 1;
        snprintf(command->u.cfg_field.key, sizeof(command->u.cfg_field.key), "%s", spec);
        snprintf(command->u.cfg_field.before, sizeof(command->u.cfg_field.before), "%s", before);
        snprintf(command->u.cfg_field.after, sizeof(command->u.cfg_field.after), "%s", after);
    }
    list->count++;
}

/** Set `key` (occurrence 0) to `value` in the simulated block, recording the
 *  command; inserted at `insert_line` when absent. */
static void
set_field(
    struct command_list* list,
    struct Editor_CfgBlock* block,
    const char* key,
    const char* value,
    int insert_line)
{
    int line = Editor_CfgBlockFieldFind(block, key, 0);
    char spec[EDITOR_CMD_NAME_MAX];

    if( line >= 0 )
    {
        char before[EDITOR_CMD_VALUE_MAX];
        field_value(block, line, before, sizeof(before));
        if( strcmp(before, value) == 0 )
            return;
        push_field(list, key, before, value);
        Editor_CfgBlockFieldSet(block, key, 0, value);
        return;
    }
    if( insert_line < 0 )
        insert_line = block->line_count;
    Editor_CfgFieldSpecFormat(key, 0, insert_line, spec, sizeof(spec));
    push_field(list, spec, "", value);
    Editor_CfgBlockFieldInsert(block, key, 0, insert_line, value);
}

static void
remove_field(
    struct command_list* list,
    struct Editor_CfgBlock* block,
    const char* key)
{
    int line = Editor_CfgBlockFieldFind(block, key, 0);
    char spec[EDITOR_CMD_NAME_MAX];
    char before[EDITOR_CMD_VALUE_MAX];

    if( line < 0 )
        return;
    field_value(block, line, before, sizeof(before));
    Editor_CfgFieldSpecFormat(key, 0, line, spec, sizeof(spec));
    push_field(list, spec, before, "");
    Editor_CfgBlockFieldRemove(block, key, 0, line);
}

/** The line a new slot line goes at: right after the nearest earlier slot,
 *  else after the var lines, else after the record's last field. */
static int
slot_insert_line(
    const struct Editor_CfgBlock* block,
    const char* prefix,
    int number)
{
    char key[32];

    for( int n = number - 1; n >= 1; n-- )
    {
        int line;
        snprintf(key, sizeof(key), "%s%d", prefix, n);
        line = Editor_CfgBlockFieldFind(block, key, 0);
        if( line >= 0 )
            return line + 1;
    }
    {
        int line = Editor_CfgBlockFieldFind(block, prefix, 0); /* bare multiloc=default */
        if( line >= 0 )
            return line;
        line = Editor_CfgBlockFieldFind(block, "multivarp", 0);
        if( line < 0 )
            line = Editor_CfgBlockFieldFind(block, "multivarbit", 0);
        if( line >= 0 )
            return line + 1;
    }
    {
        int last = -1;
        for( int i = 0; i < block->line_count; i++ )
        {
            if( block->lines[i].kind == EDITOR_CFG_LINE_FIELD
                || block->lines[i].kind == EDITOR_CFG_LINE_HEADER )
                last = i;
        }
        return last + 1;
    }
}

int
Builder_ShellCommands(
    const char* doc_key,
    const char* block_text,
    size_t length,
    const struct Builder_Shell* shell,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_CfgBlock block;
    struct command_list list;
    struct Builder_Shell before;
    const char* prefix;
    const char* cleared;
    int old_count;

    assert(doc_key);
    assert(block_text || length == 0);
    assert(shell);
    assert(out || max == 0);
    assert(shell->slot_count == 0 || shell->var_kind != BUILDER_SHELL_VAR_NONE);

    list.doc_key = doc_key;
    list.out = out;
    list.max = max;
    list.count = 0;
    prefix = slot_prefix(shell->subject);
    cleared = shell->subject == BUILDER_SUBJECT_NPC ? "default" : "-1";

    Builder_ShellParse(&before, shell->subject, block_text, length);
    if( before.overflow )
        return 0;
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, block_text ? block_text : "", length);
    old_count = before.slot_count;

    /* The var. The unused one keeps an "unset" spelling it already has. */
    {
        const char* want_varbit = shell->var_kind == BUILDER_SHELL_VAR_VARBIT ? shell->var : NULL;
        const char* want_varp = shell->var_kind == BUILDER_SHELL_VAR_VARP ? shell->var : NULL;
        const char* keys[2] = { "multivarbit", "multivarp" };
        const char* wants[2] = { want_varbit, want_varp };

        for( int k = 0; k < 2; k++ )
        {
            int line = Editor_CfgBlockFieldFind(&block, keys[k], 0);
            char current[EDITOR_CMD_VALUE_MAX] = "";

            if( line >= 0 )
                field_value(&block, line, current, sizeof(current));
            if( wants[k] )
                set_field(&list, &block, keys[k], wants[k], slot_insert_line(&block, prefix, 1));
            else if( line >= 0 && !var_unset(current) )
                set_field(&list, &block, keys[k], shell->slot_count ? cleared : "default", -1);
            else if( line < 0 && shell->slot_count > 0 )
                set_field(&list, &block, keys[k], cleared, slot_insert_line(&block, prefix, 1));
        }
    }

    /* The slots that both have, then the new ones, then the gone ones. */
    for( int i = 0; i < shell->slot_count; i++ )
    {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", prefix, i + 1);
        /* A slot the text skips reads as hidden; one that is still hidden and
         * still inside the list stays unwritten, so an untouched shell gives
         * no commands. (cachepack fills such a gap with 0, not -1: the open
         * question is recorded in the builder's report.) */
        if( Editor_CfgBlockFieldFind(&block, key, 0) < 0 && strcmp(shell->slots[i].child, "-1") == 0
            && i + 1 < old_count && i + 1 < shell->slot_count )
            continue;
        set_field(&list, &block, key, shell->slots[i].child, slot_insert_line(&block, prefix, i + 1));
    }
    for( int i = old_count; i > shell->slot_count; i-- )
    {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", prefix, i);
        remove_field(&list, &block, key);
    }

    /* The bare `multiloc=default` of a record that was not a shell: gone
     * once there are slots, back when there are none. */
    if( shell->slot_count > 0 )
        remove_field(&list, &block, prefix);
    else if( old_count > 0 && Editor_CfgBlockFieldFind(&block, prefix, 0) < 0 )
        set_field(&list, &block, prefix, "default", slot_insert_line(&block, prefix, 1));

    if( shell->subject == BUILDER_SUBJECT_LOC )
    {
        int line = Editor_CfgBlockFieldFind(&block, "multidefaultnone", 0);
        if( shell->default_none )
            set_field(&list, &block, "multidefaultnone", "yes", -1);
        else if( line >= 0 )
        {
            char current[EDITOR_CMD_VALUE_MAX];
            field_value(&block, line, current, sizeof(current));
            if( strcmp(current, "yes") == 0 )
                set_field(&list, &block, "multidefaultnone", "default", -1);
        }
    }

    Editor_CfgBlockFree(&block);
    return list.count;
}

/* ---- preview ------------------------------------------------------------- */

int
Builder_VarNameId(
    const char* name,
    int* out_kind,
    int* out_id)
{
    int kind;
    int id = 0;
    const char* at;

    assert(name);
    assert(out_kind);
    assert(out_id);

    if( strncmp(name, "varp", 4) == 0 )
        kind = BUILDER_PREVIEW_VARP;
    else if( strncmp(name, "varb", 4) == 0 )
        kind = BUILDER_PREVIEW_VARBIT;
    else
        return 0;
    at = name + 4;
    if( *at < '0' || *at > '9' )
        return 0;
    while( *at >= '0' && *at <= '9' )
    {
        id = id * 10 + (*at - '0');
        if( id > 1000000 )
            return 0;
        at++;
    }
    if( *at != '_' && *at != '\0' )
        return 0;
    *out_kind = kind;
    *out_id = id;
    return 1;
}

int
Builder_PreviewEncode(
    const struct Builder_PreviewVar* vars,
    int count,
    int32_t* out_values,
    int max_values)
{
    assert(vars || count == 0);
    assert(out_values);
    assert(count >= 0 && count <= BUILDER_PREVIEW_MAX);
    assert(max_values >= 1 + 3 * count);
    (void)max_values;

    out_values[0] = count;
    for( int i = 0; i < count; i++ )
    {
        out_values[1 + 3 * i] = vars[i].kind;
        out_values[2 + 3 * i] = vars[i].id;
        out_values[3 + 3 * i] = vars[i].value;
    }
    return 1 + 3 * count;
}

int
Builder_PreviewDecode(
    const int32_t* values,
    int value_count,
    struct Builder_PreviewVar* out_vars,
    int max_vars)
{
    int count;

    assert(values || value_count == 0);
    assert(out_vars);

    if( value_count < 1 )
        return 0;
    count = values[0];
    if( count < 0 || count > BUILDER_PREVIEW_MAX || count > max_vars || value_count < 1 + 3 * count )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        out_vars[i].kind = values[1 + 3 * i];
        out_vars[i].id = values[2 + 3 * i];
        out_vars[i].value = values[3 + 3 * i];
        if( out_vars[i].kind != BUILDER_PREVIEW_VARP && out_vars[i].kind != BUILDER_PREVIEW_VARBIT )
            return 0;
    }
    return count;
}

int
Builder_PreviewSet(
    struct Builder_PreviewVar* vars,
    int count,
    int kind,
    int id,
    int value)
{
    assert(vars);
    assert(count >= 0 && count <= BUILDER_PREVIEW_MAX);

    for( int i = 0; i < count; i++ )
    {
        if( vars[i].kind == kind && vars[i].id == id )
        {
            vars[i].value = value;
            return count;
        }
    }
    if( count == BUILDER_PREVIEW_MAX )
    {
        memmove(&vars[0], &vars[1], sizeof(vars[0]) * (BUILDER_PREVIEW_MAX - 1));
        count--;
    }
    vars[count].kind = kind;
    vars[count].id = id;
    vars[count].value = value;
    return count + 1;
}
