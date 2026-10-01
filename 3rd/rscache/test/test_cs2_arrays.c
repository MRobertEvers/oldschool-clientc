/*
 * An array lives in a string local.
 *
 * DEFINE_ARRAY parks a handle in the string local its operand names; the
 * element opcodes reach the array through that local; passing, returning or
 * storing the array moves the handle with PUSH/POP_STRING_LOCAL on the same
 * slot. So one name covers all of it, and a local's type survives as the
 * suffix of a readable name (`$count_int3`, `$label_string0`).
 */

#include "cs2/cs2_compile.h"
#include "cs2/cs2_command.h"
#include "datatypes/clientscript.h"
#include "rscache_test.h"

#include <string.h>

static struct RSCache_ClientScript g_out;
static bool g_out_held;

static bool
compile(const char* source)
{
    static struct RSCache_CS2_CompileOptions options;
    char error[256] = { 0 };
    if( g_out_held )
        RSCache_ClientScriptFreeInplace(&g_out);
    memset(&g_out, 0, sizeof(g_out));
    bool ok = RSCache_CS2_Compile(source, &options, &g_out, error, sizeof(error));
    g_out_held = ok;
    if( !ok )
        fprintf(stderr, "   compile: %s\n", error);
    return ok;
}

/** Index of the n-th (0-based) `opcode`, or -1. */
static int
find_op(int opcode, int n)
{
    for( int i = 0; i < g_out.script.op_count; i++ )
        if( g_out.script.opcodes[i] == opcode && n-- == 0 )
            return i;
    return -1;
}

static int
operand_of(int opcode, int n)
{
    int i = find_op(opcode, n);
    return i < 0 ? -9999 : g_out.script.int_operands[i];
}

int
main(void)
{
    RSCACHE_TEST_GROUP("a defined array occupies its string slot");
    RSCACHE_CHECK(compile("[clientscript,t]\n"
                          "def_int $order_intarray0(3);\n"
                          "def_intarray $copy_intarray1 = $order_intarray0;\n"
                          "$copy_intarray1(0) = 7;\n"));
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_DEFINE_ARRAY, 0), (0 << 16) | 'i');
    /* The bare name moves the handle. */
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_STRING_LOCAL, 0), 0);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_STRING_LOCAL, 0), 1);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_ARRAY_INT, 0), 1);
    RSCACHE_CHECK_EQ(find_op(RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0), -1);
    RSCACHE_CHECK_EQ(g_out.script.local_string_count, 2);
    RSCACHE_CHECK_EQ(g_out.script.local_int_count, 0);

    RSCACHE_TEST_GROUP("an array argument is a string argument");
    RSCACHE_CHECK(compile("[proc,t](intarray $rows_intarray0, int $n_int0)\n"
                          "$n_int0 = $rows_intarray0($n_int0);\n"));
    RSCACHE_CHECK_EQ(g_out.script.string_argument_count, 1);
    RSCACHE_CHECK_EQ(g_out.script.int_argument_count, 1);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_ARRAY_INT, 0), 0);

    RSCACHE_TEST_GROUP("an undeclared local takes its type from its name's suffix");
    RSCACHE_CHECK(compile("[clientscript,t]\n"
                          "def_string $copy_string3 = $label_string2;\n"
                          "def_int $sum_int1 = $count_int4;\n"));
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_STRING_LOCAL, 0), 2);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0), 4);
    /* A head that names no type, and no type suffix, still defaults to int. */
    RSCACHE_CHECK(compile("[clientscript,t]\n"
                          "def_int $sum_int1 = $order5;\n"));
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0), 5);

    RSCACHE_TEST_GROUP("def_<type>array needs an array name");
    RSCACHE_CHECK(!compile("[clientscript,t]\n"
                           "def_intarray $copy_string1 = $x_string0;\n"));

    if( g_out_held )
        RSCache_ClientScriptFreeInplace(&g_out);
    return rscache_test_report("test_cs2_arrays");
}
