/*
 * Unit test for the pure CS2 string opcodes (ESCAPE / LOWERCASE / REMOVETAGS),
 * driven through the real VM dispatch with hand-assembled scripts.
 *
 * These three used to fall through to CS2VM2_Op_StackMetaStub, which discards the
 * input and pushes "" — so they silently blanked their text rather than
 * transforming it (and ESCAPE, lacking a signature entirely, asserted). The
 * checks below therefore care that the *content* survives, not just the arity.
 *
 * ESCAPE's expected output is "<lt>"/"<gt>" — the escapes ToriDraw_Font decodes
 * back to '<'/'>' (toridraw_font.c). The xrsps reference emits HTML entities
 * instead, which is correct for a DOM renderer and wrong for ours.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK_STR(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        char const* gv = (got);                                                                    \
        char const* wv = (want);                                                                   \
        if( gv && strcmp(gv, wv) == 0 )                                                            \
            printf("  ok: %s -> \"%s\"\n", label, gv);                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got \"%s\" want \"%s\"\n", label, gv ? gv : "(null)", wv);           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* Build and run: [PUSH_CONSTANT_STRING text, op, RETURN]; return the stack top. */
static char*
run_str_op(
    int op,
    char const* text)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int const op_count = 3;
    script.script_id = 9998;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup(text);
    script.opcodes[1] = (uint16_t)op;
    script.opcodes[2] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    char* result = NULL;
    CS2VM2_PopStr(thread, &result);
    /* The result lives in the thread's string pool, which CS2VM2_Free releases —
     * so hand back a copy the caller owns. */
    char* owned = result ? strdup(result) : NULL;

    CS2VM2_Free(&vm);
    free(script.string_operands[0]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return owned;
}

static void
check_op(
    int op,
    char const* text,
    char const* want,
    char const* label)
{
    char* got = run_str_op(op, text);
    CHECK_STR(got, want, label);
    free(got);
}

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

/* Build and run: [PUSH_CONSTANT_INT chr, op, RETURN]; return the stack top. */
static int
run_char_op(
    int op,
    int chr)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int const op_count = 3;
    script.script_id = 9997;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = chr;
    script.opcodes[1] = (uint16_t)op;
    script.opcodes[2] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    int result = 0;
    CS2VM2_PopInt(thread, &result);

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

int
main(void)
{
    printf("cs2 string ops:\n");

    /* ESCAPE: only the tag delimiters change; everything else is preserved. */
    check_op(CS2_OP_ESCAPE, "hello", "hello", "ESCAPE(plain)");
    check_op(CS2_OP_ESCAPE, "a<b", "a<lt>b", "ESCAPE(lt)");
    check_op(CS2_OP_ESCAPE, "a>b", "a<gt>b", "ESCAPE(gt)");
    check_op(
        CS2_OP_ESCAPE,
        "<col=ff0000>hi</col>",
        "<lt>col=ff0000<gt>hi<lt>/col<gt>",
        "ESCAPE(neutralises a colour tag)");
    check_op(CS2_OP_ESCAPE, "", "", "ESCAPE(empty)");

    /* LOWERCASE: ASCII only, digits and punctuation untouched. */
    check_op(CS2_OP_LOWERCASE, "AbC XyZ", "abc xyz", "LOWERCASE(mixed)");
    check_op(CS2_OP_LOWERCASE, "123!?", "123!?", "LOWERCASE(non-alpha)");
    check_op(CS2_OP_LOWERCASE, "", "", "LOWERCASE(empty)");

    /* REMOVETAGS: strip <...> runs, keep the text between them. */
    check_op(CS2_OP_REMOVETAGS, "plain", "plain", "REMOVETAGS(plain)");
    check_op(CS2_OP_REMOVETAGS, "<col=ff0000>hi</col>", "hi", "REMOVETAGS(colour tag)");
    check_op(CS2_OP_REMOVETAGS, "a<br>b", "ab", "REMOVETAGS(br)");
    check_op(CS2_OP_REMOVETAGS, "", "", "REMOVETAGS(empty)");

    /* UPPERCASE mirrors LOWERCASE. */
    check_op(CS2_OP_UPPERCASE, "AbC xyZ", "ABC XYZ", "UPPERCASE(mixed)");
    check_op(CS2_OP_UPPERCASE, "123!?", "123!?", "UPPERCASE(non-alpha)");

    /* Character-class predicates: pop a char code, push a bool. */
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 'A'), 1, "CHAR_ISPRINTABLE('A')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, ' '), 1, "CHAR_ISPRINTABLE(space=32)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, '~'), 1, "CHAR_ISPRINTABLE('~'=126)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 31), 0, "CHAR_ISPRINTABLE(31 control)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 127), 0, "CHAR_ISPRINTABLE(127 del)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 160), 1, "CHAR_ISPRINTABLE(160 latin-1)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 255), 1, "CHAR_ISPRINTABLE(255 latin-1)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 0x20AC), 1, "CHAR_ISPRINTABLE(euro)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 0x2014), 1, "CHAR_ISPRINTABLE(em dash)");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISPRINTABLE, 0x0100), 0, "CHAR_ISPRINTABLE(0x100 unmapped)");

    CHECK_INT(run_char_op(CS2_OP_CHAR_ISALPHA, 'z'), 1, "CHAR_ISALPHA('z')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISALPHA, '7'), 0, "CHAR_ISALPHA('7')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISNUMERIC, '7'), 1, "CHAR_ISNUMERIC('7')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISNUMERIC, 'z'), 0, "CHAR_ISNUMERIC('z')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISALPHANUMERIC, 'z'), 1, "CHAR_ISALPHANUMERIC('z')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISALPHANUMERIC, '7'), 1, "CHAR_ISALPHANUMERIC('7')");
    CHECK_INT(run_char_op(CS2_OP_CHAR_ISALPHANUMERIC, '!'), 0, "CHAR_ISALPHANUMERIC('!')");

    if( g_fail )
    {
        printf("cs2 string ops: %d FAILED\n", g_fail);
        return 1;
    }
    printf("cs2 string ops: all passed\n");
    return 0;
}
