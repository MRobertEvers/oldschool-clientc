/*
 * Accessors over the generated opcode/trigger tables.
 *
 * This is the only translation unit that includes ss_meta.gen.h, so the tables
 * exist once in the binary regardless of how many places consult them.
 */

#include "ss_meta.h"
#include <assert.h>

#include <stdio.h>
#include <string.h>

#include "ss_meta.gen.h"

static const struct SSVM_OpcodeMeta k_meta_unknown = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

const char*
SSVM_OpcodeName(int opcode)
{
    /* Callers are diagnostics — a trace line, the loud stub's report, an abort
     * message — so an unknown id has to render as something rather than crash
     * the thing trying to tell you what went wrong. */
    static char fallback[24];

    if( opcode >= 0 && opcode < SS_OPCODE_MAX && g_ss_opcode_names[opcode] )
        return g_ss_opcode_names[opcode];

    snprintf(fallback, sizeof(fallback), "OP_%d", opcode);
    return fallback;
}

const struct SSVM_OpcodeMeta*
SSVM_OpcodeMeta(int opcode)
{
    if( opcode < 0 || opcode >= SS_OPCODE_MAX )
        return &k_meta_unknown;
    return &g_ss_opcode_meta[opcode];
}

int
SSVM_OpcodeMetaKnown(int opcode)
{
    if( opcode < 0 || opcode >= SS_OPCODE_MAX )
        return 0;
    return g_ss_opcode_meta[opcode].known;
}

int
SSVM_OpcodeFromName(const char* name)
{
    int opcode;

    assert(name);
    if( *name == '.' )
        name++;

    for( opcode = 0; opcode < SS_OPCODE_MAX; opcode++ )
    {
        const char* candidate = g_ss_opcode_names[opcode];
        const char* left;
        const char* right;

        if( !candidate )
            continue;

        /* Table names are upper case, source names lower. Compare directly
         * rather than lowering into a buffer — the table is walked once per
         * command in a compile, which is not a hot path. */
        for( left = candidate, right = name; *left && *right; left++, right++ )
        {
            int lc = (*left >= 'A' && *left <= 'Z') ? (*left - 'A' + 'a') : *left;
            int rc = (*right >= 'A' && *right <= 'Z') ? (*right - 'A' + 'a') : *right;

            if( lc != rc )
                break;
        }
        if( !*left && !*right )
            return opcode;
    }
    return -1;
}

const char*
SSVM_TriggerName(int trigger)
{
    if( trigger < 0 || trigger >= SS_TRIGGER_MAX || !g_ss_trigger_names[trigger] )
        return "";
    return g_ss_trigger_names[trigger];
}

int
SSVM_TriggerFromName(const char* name)
{
    int i;

    assert(name);

    for( i = 0; i < SS_TRIGGER_MAX; i++ )
    {
        if( g_ss_trigger_names[i] && strcmp(g_ss_trigger_names[i], name) == 0 )
            return i;
    }
    return -1;
}
