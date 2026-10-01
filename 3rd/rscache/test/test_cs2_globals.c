/*
 * Global variables by name: `%cutscene_status` compiles to the same push or pop
 * as `%varbit542`, a varc to its int or string opcode by the table that names
 * it, and a vartransmit trigger list names varps bare.
 */

#include "cs2/cs2_command.h"
#include "cs2/cs2_compile.h"
#include "cs2/cs2_names.h"
#include "datatypes/clientscript.h"
#include "rscache_test.h"

#include <string.h>

static struct RSCache_ClientScript g_out;
static bool g_out_held;
static struct RSCache_CS2_Names g_names;

static bool
compile(const char* source)
{
    static struct RSCache_CS2_CompileOptions options;
    options.names = &g_names;
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

static int
operand_of(int opcode)
{
    for( int i = 0; i < g_out.script.op_count; i++ )
        if( g_out.script.opcodes[i] == opcode )
            return g_out.script.int_operands[i];
    return -9999;
}

int
main(void)
{
    RSCache_CS2_NamesInit(&g_names);
    RSCache_CS2_NamesSet(&g_names, RSCACHE_CS2_NAMES_VARBIT, 542, "cutscene_status");
    RSCache_CS2_NamesSet(&g_names, RSCACHE_CS2_NAMES_VARP, 3079, "map_clock");
    RSCache_CS2_NamesSet(&g_names, RSCACHE_CS2_NAMES_VARCINT, 181, "raids_listsort_details");
    RSCache_CS2_NamesSet(&g_names, RSCACHE_CS2_NAMES_VARCSTRING, 3, "chatout_readpos");

    RSCACHE_TEST_GROUP("a named global compiles to its numbered opcode");
    RSCACHE_CHECK(compile("[clientscript,t]\n"
                          "%cutscene_status = %map_clock;\n"
                          "%raids_listsort_details = 2;\n"
                          "%chatout_readpos = \"x\";\n"));
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_VAR), 3079);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_VARBIT), 542);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_VARC_INT), 181);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_VARC_STRING), 3);

    RSCACHE_TEST_GROUP("the numbered spelling still compiles, and wins");
    RSCACHE_CHECK(compile("[clientscript,t]\n%varbit542 = %var3079;\n"));
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_POP_VARBIT), 542);
    RSCACHE_CHECK_EQ(operand_of(RSCACHE_CS2_OP_PUSH_VAR), 3079);

    RSCACHE_TEST_GROUP("an unknown name is refused");
    RSCACHE_CHECK(!compile("[clientscript,t]\n%no_such_var = 1;\n"));

    if( g_out_held )
        RSCache_ClientScriptFreeInplace(&g_out);
    RSCache_CS2_NamesFree(&g_names);
    return rscache_test_report("test_cs2_globals");
}
