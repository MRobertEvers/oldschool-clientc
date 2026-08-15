#include "ssvm_script.h"
#include <assert.h>

#include "ss_opcode.h"

#include <rsareabuf.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

void
SSVM_ErrorClear(struct SSVM_Error* err)
{
    assert(err);
    err->script_id = -1;
    err->offset = -1;
    err->message[0] = '\0';
}

int
SSVM_ErrorSet(
    struct SSVM_Error* err,
    int script_id,
    long offset,
    const char* fmt,
    ...)
{
    va_list args;

    assert(err);

    err->script_id = script_id;
    err->offset = offset;
    va_start(args, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Decode helpers                                                      */
/* ------------------------------------------------------------------ */

/* rsab_wrap takes a mutable pointer because one struct serves both reads and
 * writes. Nothing in this file writes through the buffer, so the cast is safe;
 * keeping it in one place keeps that claim checkable. */
static void
wrap_readonly(struct RSAreaBuf* buf, const uint8_t* data, size_t length)
{
    union
    {
        const uint8_t* readable;
        void* writable;
    } alias;

    alias.readable = data;
    rsab_wrap(buf, alias.writable, length);
}

/**
 * Read a NUL-terminated string and hand back an exact-sized copy.
 *
 * rsab_gjstr wants a caller-supplied buffer and a cap, but nothing here has a
 * natural cap — script names, absolute source paths and string operands are all
 * unbounded in principle (the corpus's longest operand is 530 bytes). Scanning
 * for the terminator first means one allocation of exactly the right size and
 * no arbitrary limit to be wrong about later.
 *
 * Returns NULL and latches overflow when the terminator is missing.
 */
static char*
read_cstr(struct RSAreaBuf* buf)
{
    size_t start;
    size_t end;
    size_t length;
    char* out;

    if( buf->overflow )
        return NULL;

    start = buf->pos;
    end = start;
    while( end < buf->capacity && buf->data[end] != 0 )
        end++;

    if( end >= buf->capacity )
    {
        /* Ran off the end without finding a terminator. */
        buf->overflow = 1;
        return NULL;
    }

    length = end - start;
    out = (char*)malloc(length + 1);
    if( !out )
    {
        buf->overflow = 1;
        return NULL;
    }
    memcpy(out, buf->data + start, length);
    out[length] = '\0';

    buf->pos = end + 1; /* step past the terminator */
    return out;
}

/* calloc that latches a flag instead of returning NULL into the caller's face.
 * Every allocation here is sized from a count the decoder has already
 * range-checked, so a failure is genuinely out-of-memory. */
static void*
alloc_zeroed(size_t count, size_t size, int* failed)
{
    void* p;

    if( count == 0 )
        return NULL;
    p = calloc(count, size);
    if( !p )
        *failed = 1;
    return p;
}

/* ------------------------------------------------------------------ */
/* Decode                                                              */
/* ------------------------------------------------------------------ */

int
SSVM_ScriptDecode(
    int32_t id,
    const uint8_t* data,
    size_t length,
    struct SSVM_Script* out,
    struct SSVM_Error* err)
{
    struct RSAreaBuf buf;
    size_t trailer_len;
    size_t trailer_pos;
    int32_t instruction_count;
    int alloc_failed = 0;
    int i;
    int32_t op;

    assert(out);

    memset(out, 0, sizeof(*out));
    out->id = id;
    out->lookup_key = -1;

    if( !data || length < SS_SCRIPT_MIN_BYTES )
        return SSVM_ErrorSet(
            err, id, -1, "script is %zu bytes, minimum is %d", length,
            SS_SCRIPT_MIN_BYTES);

    wrap_readonly(&buf, data, length);

    /* ---- locate the trailer --------------------------------------- */

    buf.pos = length - 2;
    trailer_len = (size_t)rsab_g2(&buf);

    if( trailer_len + SS_SCRIPT_FOOTER_BYTES > length )
        return SSVM_ErrorSet(
            err, id, (long)(length - 2), "trailer length %zu exceeds the script",
            trailer_len);

    trailer_pos = length - trailer_len - SS_SCRIPT_FOOTER_BYTES;
    if( trailer_pos < 1 || trailer_pos >= length )
        return SSVM_ErrorSet(
            err, id, -1, "trailer position %zu is outside a %zu byte script",
            trailer_pos, length);

    /* ---- trailer --------------------------------------------------- */

    buf.pos = trailer_pos;
    instruction_count = rsab_g4(&buf);

    /* Range-check BEFORE allocating: a corrupt count would otherwise ask for
     * gigabytes off a four-byte field. */
    if( instruction_count < 0 || instruction_count > SS_MAX_INSTRUCTIONS )
        return SSVM_ErrorSet(
            err, id, (long)trailer_pos, "instruction count %d out of range",
            instruction_count);

    out->int_local_count = (uint16_t)rsab_g2(&buf);
    out->string_local_count = (uint16_t)rsab_g2(&buf);
    out->int_arg_count = (uint16_t)rsab_g2(&buf);
    out->string_arg_count = (uint16_t)rsab_g2(&buf);

    out->switch_table_count = (uint8_t)rsab_g1(&buf);
    if( out->switch_table_count > SS_MAX_SWITCH_TABLES )
        return SSVM_ErrorSet(
            err, id, (long)buf.pos, "%u switch tables exceeds the cap of %d",
            out->switch_table_count, SS_MAX_SWITCH_TABLES);

    if( out->switch_table_count )
    {
        out->switch_tables = (struct SSVM_SwitchTable*)alloc_zeroed(
            out->switch_table_count, sizeof(*out->switch_tables), &alloc_failed);
        if( alloc_failed )
            goto oom;
    }

    for( i = 0; i < out->switch_table_count; i++ )
    {
        struct SSVM_SwitchTable* table = &out->switch_tables[i];
        int c;

        table->case_count = (uint16_t)rsab_g2(&buf);
        if( table->case_count > SS_MAX_SWITCH_CASES )
            return SSVM_ErrorSet(
                err, id, (long)buf.pos, "switch table %d has %u cases, cap is %d",
                i, table->case_count, SS_MAX_SWITCH_CASES);

        table->cases = (struct SSVM_SwitchCase*)alloc_zeroed(
            table->case_count, sizeof(*table->cases), &alloc_failed);
        if( alloc_failed )
            goto oom;

        for( c = 0; c < table->case_count; c++ )
        {
            table->cases[c].key = rsab_g4(&buf);
            table->cases[c].delta = rsab_g4(&buf);
        }
    }

    if( buf.overflow )
        return SSVM_ErrorSet(err, id, (long)buf.pos, "truncated trailer");

    /* The trailer must consume everything up to the trailing length field. If
     * it stops short, the switch block was misread and every delta in it is
     * suspect. */
    if( buf.pos != length - 2 )
        return SSVM_ErrorSet(
            err, id, (long)buf.pos, "trailer ended at %zu, expected %zu", buf.pos,
            length - 2);

    /* ---- header ---------------------------------------------------- */

    buf.pos = 0;
    out->name = read_cstr(&buf);
    out->source_path = read_cstr(&buf);
    out->lookup_key = rsab_g4(&buf);

    out->param_type_count = (uint8_t)rsab_g1(&buf);
    if( out->param_type_count > SS_MAX_PARAM_TYPES )
        return SSVM_ErrorSet(
            err, id, (long)buf.pos, "%u param types exceeds the cap of %d",
            out->param_type_count, SS_MAX_PARAM_TYPES);
    for( i = 0; i < out->param_type_count; i++ )
    {
        /* Unsigned on purpose: NPC_STAT is 254 and AUTOINT is 255, which a
         * signed char turns negative and every later comparison then misses. */
        out->param_types[i] = (uint8_t)rsab_g1(&buf);
    }

    out->line_count = (uint16_t)rsab_g2(&buf);
    if( out->line_count )
    {
        out->line_pcs =
            (int32_t*)alloc_zeroed(out->line_count, sizeof(int32_t), &alloc_failed);
        out->line_numbers =
            (int32_t*)alloc_zeroed(out->line_count, sizeof(int32_t), &alloc_failed);
        if( alloc_failed )
            goto oom;

        for( i = 0; i < out->line_count; i++ )
        {
            out->line_pcs[i] = rsab_g4(&buf);
            out->line_numbers[i] = rsab_g4(&buf);
        }
    }

    if( buf.overflow || buf.pos > trailer_pos )
        return SSVM_ErrorSet(err, id, (long)buf.pos, "truncated header");

    /* ---- code ------------------------------------------------------ */

    out->opcodes =
        (uint16_t*)alloc_zeroed((size_t)instruction_count, sizeof(uint16_t), &alloc_failed);
    out->int_operands =
        (int32_t*)alloc_zeroed((size_t)instruction_count, sizeof(int32_t), &alloc_failed);
    out->string_operands =
        (char**)alloc_zeroed((size_t)instruction_count, sizeof(char*), &alloc_failed);
    if( alloc_failed )
        goto oom;

    op = 0;
    while( buf.pos < trailer_pos )
    {
        int opcode;

        if( op >= instruction_count )
            return SSVM_ErrorSet(
                err, id, (long)buf.pos,
                "code stream holds more than the declared %d instructions",
                instruction_count);

        opcode = rsab_g2(&buf);
        out->opcodes[op] = (uint16_t)opcode;

        if( opcode == SS_OP_PUSH_CONSTANT_STRING )
        {
            out->string_operands[op] = read_cstr(&buf);
            if( !out->string_operands[op] )
                return SSVM_ErrorSet(
                    err, id, (long)buf.pos, "unterminated string operand at op %d", op);
        }
        else if( ss_is_large_operand(opcode) )
        {
            out->int_operands[op] = rsab_g4(&buf);
        }
        else
        {
            out->int_operands[op] = rsab_g1(&buf);
        }
        op++;
    }

    if( buf.overflow )
        return SSVM_ErrorSet(err, id, (long)buf.pos, "truncated code stream");

    /* Both of these are the whole reason the decoder is trustworthy. Script
     * bodies are concatenated with no internal length field, so a decode that
     * ends in the wrong place produces a plausible-looking script whose every
     * operand is shifted. */
    if( buf.pos != trailer_pos )
        return SSVM_ErrorSet(
            err, id, (long)buf.pos, "code ended at %zu, expected %zu", buf.pos,
            trailer_pos);

    if( op != instruction_count )
        return SSVM_ErrorSet(
            err, id, (long)buf.pos, "decoded %d instructions, trailer declares %d", op,
            instruction_count);

    out->op_count = op;
    return 1;

oom:
    return SSVM_ErrorSet(err, id, -1, "out of memory");
}

/* ------------------------------------------------------------------ */
/* Encode                                                              */
/* ------------------------------------------------------------------ */

int
SSVM_ScriptEncode(
    const struct SSVM_Script* script,
    uint8_t* out,
    size_t capacity,
    size_t* out_length,
    struct SSVM_Error* err)
{
    struct RSAreaBuf buf;
    size_t switch_bytes;
    size_t needed;
    int i;

    if( out_length )
        *out_length = 0;

    assert(script);

    /* Size first. Every term is exact rather than a guess, so the sizing pass
     * doubles as a check that the layout below is the layout being described. */
    switch_bytes = 0;
    for( i = 0; i < script->switch_table_count; i++ )
        switch_bytes += 2 + ((size_t)script->switch_tables[i].case_count * 8);

    needed = 0;
    needed += strlen(script->name ? script->name : "") + 1;
    needed += strlen(script->source_path ? script->source_path : "") + 1;
    needed += 4;                              /* lookup_key */
    needed += 1 + script->param_type_count;   /* param types */
    needed += 2 + ((size_t)script->line_count * 8);

    for( i = 0; i < script->op_count; i++ )
    {
        int opcode = script->opcodes[i];

        needed += 2;
        if( opcode == SS_OP_PUSH_CONSTANT_STRING )
            needed += strlen(script->string_operands[i] ? script->string_operands[i] : "") + 1;
        else if( ss_is_large_operand(opcode) )
            needed += 4;
        else
            needed += 1;
    }

    needed += 4 + 2 + 2 + 2 + 2 + 1; /* instruction count, four counts, table count */
    needed += switch_bytes;
    needed += 2; /* trailer_len */

    if( out_length )
        *out_length = needed;
    if( !out )
        return 1; /* sizing pass */

    if( capacity < needed )
        return SSVM_ErrorSet(
            err, script->id, -1, "encode needs %zu bytes, buffer holds %zu", needed,
            capacity);

    rsab_wrap(&buf, out, capacity);
    buf.pos = 0;

    /* ---- header ---------------------------------------------------- */

    rsab_pjstr(&buf, script->name ? script->name : "", RSAB_JSTR_NUL);
    rsab_pjstr(&buf, script->source_path ? script->source_path : "", RSAB_JSTR_NUL);
    rsab_p4(&buf, script->lookup_key);

    rsab_p1(&buf, script->param_type_count);
    for( i = 0; i < script->param_type_count; i++ )
        rsab_p1(&buf, script->param_types[i]);

    rsab_p2(&buf, script->line_count);
    for( i = 0; i < script->line_count; i++ )
    {
        rsab_p4(&buf, script->line_pcs[i]);
        rsab_p4(&buf, script->line_numbers[i]);
    }

    /* ---- code ------------------------------------------------------ */

    for( i = 0; i < script->op_count; i++ )
    {
        int opcode = script->opcodes[i];

        rsab_p2(&buf, opcode);
        if( opcode == SS_OP_PUSH_CONSTANT_STRING )
        {
            rsab_pjstr(
                &buf, script->string_operands[i] ? script->string_operands[i] : "",
                RSAB_JSTR_NUL);
        }
        else if( ss_is_large_operand(opcode) )
        {
            rsab_p4(&buf, script->int_operands[i]);
        }
        else
        {
            rsab_p1(&buf, script->int_operands[i]);
        }
    }

    /* ---- trailer --------------------------------------------------- */

    rsab_p4(&buf, script->op_count);
    rsab_p2(&buf, script->int_local_count);
    rsab_p2(&buf, script->string_local_count);
    rsab_p2(&buf, script->int_arg_count);
    rsab_p2(&buf, script->string_arg_count);

    rsab_p1(&buf, script->switch_table_count);
    for( i = 0; i < script->switch_table_count; i++ )
    {
        const struct SSVM_SwitchTable* table = &script->switch_tables[i];
        int c;

        rsab_p2(&buf, table->case_count);
        for( c = 0; c < table->case_count; c++ )
        {
            rsab_p4(&buf, table->cases[c].key);
            rsab_p4(&buf, table->cases[c].delta);
        }
    }

    /* The decoder finds the trailer by working back from this value, so the
     * "+ 1" is load-bearing: it is what makes trailer_pos land on the
     * instruction count rather than one byte past it. */
    rsab_p2(&buf, (int32_t)(switch_bytes + 1));

    if( !rsab_ok(&buf) )
        return SSVM_ErrorSet(err, script->id, (long)buf.pos, "encode overflowed");

    if( buf.pos != needed )
        return SSVM_ErrorSet(
            err, script->id, (long)buf.pos, "encode wrote %zu bytes, sizing said %zu",
            buf.pos, needed);

    if( out_length )
        *out_length = buf.pos;
    return 1;
}

/* ------------------------------------------------------------------ */

void
SSVM_ScriptFree(struct SSVM_Script* script)
{
    int i;

    if( !script )
        return;

    free(script->name);
    free(script->source_path);

    if( script->string_operands )
    {
        for( i = 0; i < script->op_count; i++ )
            free(script->string_operands[i]);
    }
    free(script->string_operands);
    free(script->opcodes);
    free(script->int_operands);

    if( script->switch_tables )
    {
        for( i = 0; i < script->switch_table_count; i++ )
            free(script->switch_tables[i].cases);
    }
    free(script->switch_tables);

    free(script->line_pcs);
    free(script->line_numbers);

    memset(script, 0, sizeof(*script));
    script->lookup_key = -1;
}

int32_t
SSVM_ScriptLineNumber(
    const struct SSVM_Script* script,
    int32_t pc)
{
    int i;

    assert(script);
    if( !script->line_count )
        return 0;

    for( i = 0; i < script->line_count; i++ )
    {
        if( script->line_pcs[i] > pc )
        {
            /* The reference returns lines[i - 1] here, which reads lines[-1]
             * when the first entry already exceeds pc — undefined in JS, out of
             * bounds in C. pc == -1 before the first op makes that reachable. */
            return script->line_numbers[i > 0 ? i - 1 : 0];
        }
    }
    return script->line_numbers[script->line_count - 1];
}

const char*
SSVM_ScriptFileName(const struct SSVM_Script* script)
{
    const char* slash;

    assert(script);
    if( !script->source_path )
        return "";

    slash = strrchr(script->source_path, '/');
    if( slash )
        return slash + 1;
    return script->source_path;
}

int
SSVM_ScriptTrigger(const struct SSVM_Script* script)
{
    assert(script);
    if( script->lookup_key < 0 )
        return -1;
    return script->lookup_key & 0xff;
}
