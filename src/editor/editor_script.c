/*
 * RuneScript files for the editor: trigger headers, the op / ap kinds a
 * record carries, the starter template, the "add trigger" plan and the
 * compile diagnostics per line. editor_script.h has the contract.
 */

#include "editor/editor_script.h"

#include "editor/builder/builder_emit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- triggers ------------------------------------------------------------------ */

int
Editor_ScriptParseTriggers(
    const char* text,
    size_t length,
    struct Editor_ScriptTrigger* out,
    int max)
{
    size_t at = 0;
    int line = 0;
    int count = 0;

    assert(text || length == 0);
    assert(out || max == 0);
    while( at < length )
    {
        size_t end = at;
        while( end < length && text[end] != '\n' )
            end++;
        line++;
        if( end > at && text[at] == '[' )
        {
            const char* p = text + at;
            size_t const n = end - at;
            const char* comma = memchr(p, ',', n);
            const char* rb = memchr(p, ']', n);
            if( comma && rb && comma < rb && rb > comma + 1 )
            {
                if( count < max )
                {
                    struct Editor_ScriptTrigger* t = &out[count];
                    size_t const kind_length = (size_t)(comma - (p + 1));
                    size_t const subject_length = (size_t)(rb - (comma + 1));
                    snprintf(t->kind, sizeof(t->kind), "%.*s", (int)kind_length, p + 1);
                    snprintf(t->subject, sizeof(t->subject), "%.*s", (int)subject_length, comma + 1);
                    t->line = line;
                }
                count++;
            }
        }
        at = end < length ? end + 1 : end;
    }
    return count;
}

/* ---- kinds --------------------------------------------------------------------- */

/* The builders compose the loc and npc names as "op"/"ap" + word + N / "u"
 * (builder_emit.c Builder_EmitScript); an obj adds its held-inventory ops.
 * Every name here is a trigger sscompile knows (serverscript/ss_meta.gen.h). */
static const char* const k_loc_kinds[] = {
    "oploc1", "oploc2", "oploc3", "oploc4", "oploc5", "oplocu",
    "aploc1", "aploc2", "aploc3", "aploc4", "aploc5", "aplocu",
};
static const char* const k_npc_kinds[] = {
    "opnpc1", "opnpc2", "opnpc3", "opnpc4", "opnpc5", "opnpcu",
    "apnpc1", "apnpc2", "apnpc3", "apnpc4", "apnpc5", "apnpcu",
};
static const char* const k_obj_kinds[] = {
    "opobj1",  "opobj2",  "opobj3",  "opobj4",  "opobj5",  "opobju",
    "apobj1",  "apobj2",  "apobj3",  "apobj4",  "apobj5",  "apobju",
    "opheld1", "opheld2", "opheld3", "opheld4", "opheld5", "opheldu",
};

static const char* const*
kinds_of(
    const char* type,
    int* out_count)
{
    if( strcmp(type, "loc") == 0 )
    {
        *out_count = (int)(sizeof(k_loc_kinds) / sizeof(k_loc_kinds[0]));
        return k_loc_kinds;
    }
    if( strcmp(type, "npc") == 0 )
    {
        *out_count = (int)(sizeof(k_npc_kinds) / sizeof(k_npc_kinds[0]));
        return k_npc_kinds;
    }
    if( strcmp(type, "obj") == 0 )
    {
        *out_count = (int)(sizeof(k_obj_kinds) / sizeof(k_obj_kinds[0]));
        return k_obj_kinds;
    }
    *out_count = 0;
    return NULL;
}

int
Editor_ScriptTriggerKinds(
    const char* type,
    const char** out,
    int max)
{
    int count;
    const char* const* kinds;

    assert(type);
    assert(out || max == 0);
    kinds = kinds_of(type, &count);
    for( int i = 0; i < count && i < max; i++ )
        out[i] = kinds[i];
    return count;
}

int
Editor_ScriptKindValid(
    const char* type,
    const char* kind)
{
    int count;
    const char* const* kinds;

    assert(type);
    assert(kind);
    kinds = kinds_of(type, &count);
    for( int i = 0; i < count; i++ )
        if( strcmp(kinds[i], kind) == 0 )
            return 1;
    return 0;
}

int
Editor_ScriptKindOpField(
    const char* kind,
    char* out,
    size_t out_size)
{
    size_t length;
    char last;

    assert(kind);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    length = strlen(kind);
    if( length < 3 )
        return 0;
    last = kind[length - 1];
    if( last < '1' || last > '5' )
        return 0;
    if( strncmp(kind, "opheld", 6) == 0 )
        snprintf(out, out_size, "ifop%c", last);
    else if( strncmp(kind, "op", 2) == 0 || strncmp(kind, "ap", 2) == 0 )
        snprintf(out, out_size, "op%c", last);
    else
        return 0;
    return 1;
}

int
Editor_ScriptOpStated(const char* value)
{
    if( !value )
        return 0;
    while( *value == ' ' )
        value++;
    return value[0] && strcmp(value, "default") != 0 && strcmp(value, "null") != 0;
}

/* ---- paths --------------------------------------------------------------------- */

static int
name_valid(const char* name)
{
    if( !name[0] || strlen(name) >= 64 )
        return 0;
    for( const char* p = name; *p; p++ )
        if( !((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_') )
            return 0;
    return 1;
}

int
Editor_ScriptEditedPath(
    const char* subject,
    char* out,
    size_t out_size)
{
    int written;

    assert(subject);
    assert(out);
    assert(out_size > 0);
    written = snprintf(out, out_size, "edited/%s/scripts/%s.rs2", subject, subject);
    return written > 0 && (size_t)written < out_size && (size_t)written < EDITOR_SCRIPT_PATH_MAX;
}

int
Editor_ScriptFileValid(const char* rel_path)
{
    size_t length;

    assert(rel_path);
    length = strlen(rel_path);
    if( length < 1 || length >= EDITOR_SCRIPT_PATH_MAX || rel_path[0] == '/' || rel_path[length - 1] == '/'
        || strstr(rel_path, "..") || strchr(rel_path, '\\') || strstr(rel_path, "//") )
        return 0;
    for( const char* p = rel_path; *p; p++ )
        if( (unsigned char)*p <= ' ' )
            return 0;
    return 1;
}

int
Editor_ScriptPathValid(const char* rel_path)
{
    size_t length;

    assert(rel_path);
    length = strlen(rel_path);
    return length >= 5 && strcmp(rel_path + length - 4, ".rs2") == 0 && Editor_ScriptFileValid(rel_path);
}

int
Editor_ScriptRelFromContentPath(
    const char* content_path,
    char* out,
    size_t out_size)
{
    static const char k_prefix[] = "server/scripts/";

    assert(content_path);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( strncmp(content_path, k_prefix, sizeof(k_prefix) - 1) != 0 )
        return 0;
    content_path += sizeof(k_prefix) - 1;
    if( !Editor_ScriptPathValid(content_path) || strlen(content_path) >= out_size )
        return 0;
    snprintf(out, out_size, "%s", content_path);
    return 1;
}

/* ---- the template ------------------------------------------------------------- */

size_t
Editor_ScriptTemplate(
    const char* kind,
    const char* subject,
    char* out,
    size_t out_size)
{
    size_t const length = strlen(kind);
    int written;

    assert(kind);
    assert(subject);
    assert(out);
    assert(out_size > 0);
    /* A use-on kind answers with the tree's default (the builders' fallback,
     * builder_emit.c emit_use_trigger); an op / ap with the message OSRS
     * gives an object that does nothing. */
    if( length > 0 && kind[length - 1] == 'u' )
        written = snprintf(out, out_size, "[%s,%s]\n~displaymessage(^dm_default);\n", kind, subject);
    else
        written = snprintf(out, out_size, "[%s,%s]\nmes(\"Nothing interesting happens.\");\n", kind, subject);
    if( written <= 0 || (size_t)written >= out_size )
    {
        out[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

/* ---- the add plan ------------------------------------------------------------- */

static int
count_lines_before(
    const char* text,
    size_t length)
{
    int lines = 0;
    for( size_t i = 0; i < length; i++ )
        lines += text[i] == '\n';
    return lines;
}

enum Editor_ScriptPlanStatus
Editor_ScriptPlanAddTrigger(
    const char* type,
    const char* subject,
    const char* kind,
    const char* op_value,
    const char* op_label,
    const char* script,
    size_t script_length,
    struct Editor_ScriptAddPlan* out)
{
    char field[16];
    char body[EDITOR_SCRIPT_INSERT_MAX];
    size_t body_length;
    size_t at = 0;
    struct Editor_ScriptTrigger triggers[64];
    int trigger_count;

    assert(type);
    assert(subject);
    assert(kind);
    assert(script || script_length == 0);
    assert(out);
    memset(out, 0, sizeof(*out));

    if( !Editor_ScriptKindValid(type, kind) )
        return EDITOR_SCRIPT_PLAN_BAD_KIND;
    if( !name_valid(subject) || !Editor_ScriptEditedPath(subject, out->rel_path, sizeof(out->rel_path)) )
        return EDITOR_SCRIPT_PLAN_BAD_SUBJECT;
    snprintf(out->doc_key, sizeof(out->doc_key), "rs2:%s", out->rel_path);
    if( script_length >= sizeof(BUILDER_HEADER_PREFIX) - 1
        && memcmp(script, BUILDER_HEADER_PREFIX, sizeof(BUILDER_HEADER_PREFIX) - 1) == 0 )
        return EDITOR_SCRIPT_PLAN_GENERATED;
    trigger_count = Editor_ScriptParseTriggers(script, script_length, triggers, 64);
    for( int i = 0; i < trigger_count && i < 64; i++ )
        if( strcmp(triggers[i].kind, kind) == 0 && strcmp(triggers[i].subject, subject) == 0 )
            return EDITOR_SCRIPT_PLAN_DUPLICATE;

    /* The menu entry: written only when the record has none. */
    if( Editor_ScriptKindOpField(kind, field, sizeof(field)) && !Editor_ScriptOpStated(op_value) )
    {
        const char* label = op_label ? op_label : "";
        size_t length;
        while( *label == ' ' )
            label++;
        length = strlen(label);
        while( length > 0 && label[length - 1] == ' ' )
            length--;
        if( length == 0 )
            return EDITOR_SCRIPT_PLAN_NO_LABEL;
        if( length >= sizeof(out->field_value) || memchr(label, '\n', length) || memchr(label, '\r', length) )
            return EDITOR_SCRIPT_PLAN_BAD_TEXT;
        snprintf(out->field, sizeof(out->field), "%s", field);
        snprintf(out->field_value, sizeof(out->field_value), "%.*s", (int)length, label);
    }

    /* The text: a header comment for a new file, a blank line before the
     * trigger otherwise, a line break first when the file lacks one. */
    if( script_length == 0 )
    {
        int written = snprintf(out->insert, sizeof(out->insert),
            "// %s: triggers written by hand, started from the ToriRSEd Record panel.\n\n", subject);
        assert(written > 0);
        at = (size_t)written;
    }
    else if( script[script_length - 1] != '\n' )
    {
        memcpy(out->insert, "\n\n", 2);
        at = 2;
    }
    else
    {
        out->insert[0] = '\n';
        at = 1;
    }
    body_length = Editor_ScriptTemplate(kind, subject, body, sizeof(body));
    if( body_length == 0 || at + body_length >= sizeof(out->insert) )
        return EDITOR_SCRIPT_PLAN_BAD_TEXT;
    memcpy(out->insert + at, body, body_length + 1);
    out->insert_length = at + body_length;
    out->offset = script_length;
    out->header_line = count_lines_before(script, script_length) + count_lines_before(out->insert, at) + 1;
    return EDITOR_SCRIPT_PLAN_OK;
}

const char*
Editor_ScriptPlanStatusText(enum Editor_ScriptPlanStatus status)
{
    switch( status )
    {
    case EDITOR_SCRIPT_PLAN_OK:
        return "ok";
    case EDITOR_SCRIPT_PLAN_BAD_KIND:
        return "that trigger kind is not one this record type carries";
    case EDITOR_SCRIPT_PLAN_BAD_SUBJECT:
        return "the record name is not a script subject";
    case EDITOR_SCRIPT_PLAN_GENERATED:
        return "the edited script is a builder's output; add the trigger in the builder (or Detach it first)";
    case EDITOR_SCRIPT_PLAN_DUPLICATE:
        return "the edited script already has that trigger";
    case EDITOR_SCRIPT_PLAN_NO_LABEL:
        return "the record has no such menu entry: type its text (Op text)";
    case EDITOR_SCRIPT_PLAN_BAD_TEXT:
        return "the menu text cannot be a config value";
    }
    return "?";
}

/* ---- diagnostics -------------------------------------------------------------- */

int
Editor_ScriptDiagLines(
    const struct Builder_SscResult* result,
    const char* rel_path,
    int* out_lines,
    int max)
{
    int count = 0;

    assert(result);
    assert(rel_path);
    assert(out_lines || max == 0);
    for( int i = 0; i < result->diag_count; i++ )
    {
        const struct Builder_SscDiag* diag = &result->diags[i];
        int line;
        int at;
        if( diag->warning || diag->line <= 0 || strcmp(diag->file, rel_path) != 0 )
            continue;
        line = diag->line - 1;
        /* Insert sorted, once; past `max` the lowest lines are kept. */
        at = 0;
        while( at < count && out_lines[at] < line )
            at++;
        if( (at < count && out_lines[at] == line) || at >= max )
            continue;
        if( count == max )
            count--; /* full: the highest line makes room */
        memmove(out_lines + at + 1, out_lines + at, sizeof(int) * (size_t)(count - at));
        out_lines[at] = line;
        count++;
    }
    return count;
}

void
Editor_ScriptDiagFormat(
    const struct Builder_SscDiag* diag,
    char* out,
    size_t out_size)
{
    assert(diag);
    assert(out);
    assert(out_size > 0);
    snprintf(out, out_size, "%s:%d: %s%s", diag->file, diag->line, diag->warning ? "warning: " : "", diag->message);
}
