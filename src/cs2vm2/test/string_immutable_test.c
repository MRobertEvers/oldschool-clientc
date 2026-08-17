/*
 * Unit test for immutable VM strings (docs/CS2_OPTIMIZER_PLAN.md §11.4).
 *
 * UPPERCASE and LOWERCASE used to rewrite the buffer their operand pointed at.
 * That one fact forced every other string in the VM to be a private copy:
 * pushing a string local StrDup'd it, PUSH_CONSTANT_STRING StrDup'd the
 * script's operand, and even the empty string needed distinct storage per push,
 * all so those two loops could not reach an alias. Removing the mutation is
 * what pays for all of it — so this file pins the mutation being gone rather
 * than the copies being gone, because the copies are safe to remove only while
 * this holds.
 *
 * Three properties, each of which fails LOUDLY if the mutators go back to
 * writing through their operand:
 *
 *   1. uppercase($s) does not change $s. With aliasing, `$s` reads back
 *      uppercased and every later use of the local is wrong — the failure is
 *      remote from the opcode that caused it, which is why it is worth a test.
 *   2. uppercasing a string CONSTANT does not rewrite the script. This is the
 *      worse of the two: the script is decoded once and shared by every run, so
 *      a rewrite here is permanent for the session. The test runs the same
 *      script twice and checks the second run still sees the original text —
 *      and checks the operand bytes in the script itself.
 *   3. the same constant pushed twice compares equal, and a produced string
 *      (uppercase's result) is not the same buffer as the constant it came
 *      from.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

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

#define CHECK_STR(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        char const* gv = (got) ? (got) : "(null)";                                                 \
        char const* wv = (want);                                                                   \
        if( strcmp(gv, wv) == 0 )                                                                  \
            printf("  ok: %s == \"%s\"\n", label, gv);                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got \"%s\" want \"%s\"\n", label, gv, wv);                          \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

#define MAX_OPS 32

struct TestScript
{
    struct CS2VM2_Script script;
    uint16_t opcodes[MAX_OPS];
    int int_operands[MAX_OPS];
    char* string_operands[MAX_OPS];
    int n;
};

static void
script_begin(
    struct TestScript* ts,
    int id,
    int int_locals,
    int str_locals)
{
    memset(ts, 0, sizeof(*ts));
    CS2VM2_ScriptInit(&ts->script);
    ts->script.script_id = id;
    ts->script.local_int_count = int_locals;
    ts->script.local_string_count = str_locals;
    ts->script.opcodes = ts->opcodes;
    ts->script.int_operands = ts->int_operands;
    ts->script.string_operands = ts->string_operands;
}

static void
emit(
    struct TestScript* ts,
    int op,
    int operand)
{
    ts->opcodes[ts->n] = (uint16_t)op;
    ts->int_operands[ts->n] = operand;
    ts->n++;
    ts->script.op_count = ts->n;
}

static void
emit_str(
    struct TestScript* ts,
    int op,
    char* text)
{
    ts->opcodes[ts->n] = (uint16_t)op;
    ts->string_operands[ts->n] = text;
    ts->n++;
    ts->script.op_count = ts->n;
}

/* --- 1: uppercase does not write through a string local ------------------- */

static void
test_local_not_aliased(void)
{
    struct TestScript top;
    struct CS2VM2 vm;

    printf("uppercase does not rewrite the local it read:\n");

    /* $s = "mix"; uppercase($s); push $s   -> stack: "MIX" under, "mix" on top */
    char* operand = strdup("mix");
    script_begin(&top, 700, 0, 1);
    emit_str(&top, CS2_OP_PUSH_CONSTANT_STRING, operand);
    emit(&top, CS2_OP_POP_STRING_LOCAL, 0);
    emit(&top, CS2_OP_PUSH_STRING_LOCAL, 0);
    emit(&top, CS2_OP_UPPERCASE, 0);
    emit(&top, CS2_OP_PUSH_STRING_LOCAL, 0);
    emit(&top, CS2_OP_RETURN, 0);

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "run completes");

    char* after = NULL;
    char* upper = NULL;
    CS2VM2_PopStr(t, &after);
    CS2VM2_PopStr(t, &upper);
    CHECK_STR(upper, "MIX", "uppercase produced the uppercased string");
    CHECK_STR(after, "mix", "the local still reads as it was written");
    CHECK_INT(upper != after, 1, "the result is not the local's buffer");

    CS2VM2_Free(&vm);
    free(operand);
}

/* --- 2: lowercase does not write through a script operand ----------------- */

static void
test_operand_not_rewritten(void)
{
    struct TestScript top;
    struct CS2VM2 vm;

    printf("lowercase does not rewrite the script's operand:\n");

    char* operand = strdup("SHOUT");
    script_begin(&top, 701, 0, 0);
    emit_str(&top, CS2_OP_PUSH_CONSTANT_STRING, operand);
    emit(&top, CS2_OP_LOWERCASE, 0);
    emit(&top, CS2_OP_RETURN, 0);

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "first run completes");
    char* first = NULL;
    CS2VM2_PopStr(t, &first);
    CHECK_STR(first, "shout", "first run lowercased it");
    /* The decoded script is shared by every execution: if the opcode wrote
     * through its operand, this byte string is now permanently "shout". */
    CHECK_STR(operand, "SHOUT", "the script's own operand bytes are untouched");
    CHECK_INT(first != operand, 1, "the result is not the operand's buffer");

    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "second run completes");
    char* second = NULL;
    CS2VM2_PopStr(t, &second);
    CHECK_STR(second, "shout", "second run sees the same input, not a rewritten one");
    CHECK_STR(operand, "SHOUT", "still untouched after two runs");

    CS2VM2_Free(&vm);
    free(operand);
}

/* --- 3: sharing is observable and correct --------------------------------- */

static void
test_constant_is_shared(void)
{
    struct TestScript top;
    struct CS2VM2 vm;

    printf("a constant pushed twice is one string:\n");

    char* operand = strdup("label");
    script_begin(&top, 702, 0, 0);
    emit_str(&top, CS2_OP_PUSH_CONSTANT_STRING, operand);
    emit_str(&top, CS2_OP_PUSH_CONSTANT_STRING, operand);
    emit(&top, CS2_OP_RETURN, 0);

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "run completes");

    char* b = NULL;
    char* a = NULL;
    CS2VM2_PopStr(t, &b);
    CS2VM2_PopStr(t, &a);
    CHECK_STR(a, "label", "first push");
    CHECK_STR(b, "label", "second push");
    CHECK_INT(a == b, 1, "both pushes are the same pointer (no copy per push)");
    CHECK_INT(a == operand, 1, "and that pointer is the script's own operand");

    CS2VM2_Free(&vm);
    free(operand);
}

static void
test_empty_is_shared(void)
{
    struct CS2VM2 vm;

    printf("the empty string is one shared buffer:\n");

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    char* a = CS2VM2_StrEmpty(t);
    char* b = CS2VM2_StrEmpty(t);
    CHECK_STR(a, "", "StrEmpty gives an empty string");
    CHECK_INT(a == b, 1, "and the same one every time");

    /* A produced string is still its own storage — sharing "" must not make
     * every short string alias. */
    char* made = CS2VM2_StrDup(t, "");
    CHECK_STR(made, "", "StrDup(\"\") is empty too");
    CHECK_INT(made != a, 1, "but is its own buffer, not the shared empty");

    CS2VM2_Free(&vm);
}

int
main(void)
{
    test_local_not_aliased();
    test_operand_not_rewritten();
    test_constant_is_shared();
    test_empty_is_shared();

    if( g_fail )
    {
        printf("string immutable test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("string immutable test: all passed\n");
    return 0;
}
