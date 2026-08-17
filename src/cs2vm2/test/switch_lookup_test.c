/*
 * Unit test for the sorted switch table (docs/CS2_OPTIMIZER_PLAN.md §12.4).
 *
 * SWITCH used to scan its case table. The tables in this cache are not small —
 * script 7300 carries 1,960 cases, and the skill guides and catalogues are the
 * same shape — so a miss read every one of them. Sorting the table at decode
 * and binary-searching it at run time is the same answer in O(log n), and this
 * file is the proof of "the same answer": for a table built from shuffled keys,
 * every hit and every miss must land exactly where the linear scan would.
 *
 * Two things it deliberately covers that a small happy-path table would not:
 *
 *   - keys spanning the whole int range, INT_MIN and INT_MAX included. The
 *     obvious comparator is `a->key - b->key`, which overflows for those and
 *     sorts them backwards — after which the search misses cases that exist.
 *   - an UNSORTED table still working. Scripts are built in two places: the
 *     cache decoder, which sorts, and by hand (these tests, harnesses), which
 *     does not. An unsorted table must stay correct, so the `sorted` flag gates
 *     the search and the linear scan remains the fallback.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK_INT(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        int gv = (got), wv = (want);                                                               \
        if( gv == wv )                                                                             \
            printf("  ok: %s == %d\n", label, gv);                                                 \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got %d want %d\n", label, gv, wv);                                  \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/*
 * A script shaped like a compiled `switch`:
 *
 *   pc 0: push_constant_int <key>
 *   pc 1: switch table 0
 *   pc 2: push_constant_int 0          <- the default arm
 *   pc 3: return
 *   pc 4+i: push_constant_int (100+i); return     <- one arm per case
 *
 * The jump is relative to the pc AFTER the switch, so an arm at absolute pc P
 * has target_pc P - 2.
 */
#define CASE_COUNT 64
#define ARM_BASE 4

static void
build(
    struct CS2VM2_Script* script,
    uint16_t* opcodes,
    int* int_operands,
    char** string_operands,
    struct CS2VM2_ScriptSwitch* table,
    struct CS2VM2_ScriptSwitchCase* cases,
    int const* keys)
{
    CS2VM2_ScriptInit(script);
    script->script_id = 7300;
    script->op_count = ARM_BASE + CASE_COUNT * 2;
    script->opcodes = opcodes;
    script->int_operands = int_operands;
    script->string_operands = string_operands;

    opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    opcodes[1] = (uint16_t)CS2_OP_SWITCH;
    int_operands[1] = 0;
    opcodes[2] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    int_operands[2] = 0;
    opcodes[3] = (uint16_t)CS2_OP_RETURN;

    for( int i = 0; i < CASE_COUNT; i++ )
    {
        int arm = ARM_BASE + i * 2;
        opcodes[arm] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        int_operands[arm] = 100 + i;
        opcodes[arm + 1] = (uint16_t)CS2_OP_RETURN;
        cases[i].key = keys[i];
        cases[i].target_pc = arm - 2;
    }

    table->case_count = CASE_COUNT;
    table->cases = cases;
    table->sorted = 0;
    script->switch_table_count = 1;
    script->switch_tables = table;
}

static int
run_with_key(
    struct CS2VM2_Script* script,
    int key)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);

    script->int_operands[0] = key;

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, script);
    CS2VM2_RunScript(t);

    int out = -1;
    CS2VM2_PopInt(t, &out);
    CS2VM2_Free(&vm);
    return out;
}

int
main(void)
{
    /*
     * Shuffled, wide, and deliberately including both ends of the int range and
     * a run of adjacent values (which is what a real jump table looks like).
     */
    int keys[CASE_COUNT];
    {
        unsigned seed = 12345u;
        for( int i = 0; i < CASE_COUNT; i++ )
        {
            seed = seed * 1103515245u + 12345u;
            keys[i] = (int)(seed >> 8) - 4000000;
        }
        keys[0] = INT_MIN;
        keys[1] = INT_MAX;
        keys[2] = 0;
        keys[3] = -1;
        keys[4] = 1;
        keys[5] = INT_MIN + 1;
        keys[6] = INT_MAX - 1;
    }

    uint16_t opcodes[ARM_BASE + CASE_COUNT * 2];
    int int_operands[ARM_BASE + CASE_COUNT * 2];
    char* string_operands[ARM_BASE + CASE_COUNT * 2];
    struct CS2VM2_ScriptSwitch table;
    struct CS2VM2_ScriptSwitchCase cases[CASE_COUNT];
    struct CS2VM2_Script script;

    memset(opcodes, 0, sizeof(opcodes));
    memset(int_operands, 0, sizeof(int_operands));
    memset(string_operands, 0, sizeof(string_operands));

    /* Pass 1: unsorted table, linear scan. This is the oracle. */
    printf("unsorted table (linear scan) — the oracle:\n");
    build(&script, opcodes, int_operands, string_operands, &table, cases, keys);
    int linear_hit[CASE_COUNT];
    int linear_ok = 1;
    for( int i = 0; i < CASE_COUNT; i++ )
    {
        linear_hit[i] = run_with_key(&script, keys[i]);
        if( linear_hit[i] != 100 + i )
            linear_ok = 0;
    }
    CHECK_INT(linear_ok, 1, "every case reaches its own arm");

    /* Misses: values chosen to sit between, below and above the keys. */
    int const misses[6] = { -3999999, 7, 123456789, -123456789, 2, -2 };
    int linear_miss[6];
    for( int i = 0; i < 6; i++ )
        linear_miss[i] = run_with_key(&script, misses[i]);

    /* Pass 2: the same table, sorted, searched. */
    printf("sorted table (binary search):\n");
    CS2VM2_ScriptSortSwitches(&script);
    CHECK_INT(table.sorted, 1, "the table reports itself sorted");

    int ascending = 1;
    for( int i = 1; i < CASE_COUNT; i++ )
    {
        if( cases[i - 1].key >= cases[i].key )
            ascending = 0;
    }
    CHECK_INT(ascending, 1, "cases are in ascending key order (no overflow in the compare)");

    int same_hits = 1;
    for( int i = 0; i < CASE_COUNT; i++ )
    {
        if( run_with_key(&script, keys[i]) != linear_hit[i] )
            same_hits = 0;
    }
    CHECK_INT(same_hits, 1, "every hit lands where the linear scan sent it");

    int same_misses = 1;
    for( int i = 0; i < 6; i++ )
    {
        if( run_with_key(&script, misses[i]) != linear_miss[i] )
            same_misses = 0;
    }
    CHECK_INT(same_misses, 1, "every miss falls through to the same default arm");
    CHECK_INT(linear_miss[0], 0, "a miss really does reach the default arm");

    /* The ends of the range specifically, since they are what an overflowing
     * comparator gets wrong. */
    CHECK_INT(run_with_key(&script, INT_MIN), 100, "INT_MIN finds its case");
    CHECK_INT(run_with_key(&script, INT_MAX), 101, "INT_MAX finds its case");

    if( g_fail )
    {
        printf("switch lookup test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("switch lookup test: all passed\n");
    return 0;
}
