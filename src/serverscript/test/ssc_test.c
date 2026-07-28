/*
 * The compiler, end to end: .rs2 source -> bytecode -> reader -> VM.
 *
 * Every case writes real RuneScript to a temporary file, compiles it, loads the
 * result through the same container the mock server uses, and *runs* it. That
 * closes the loop — a compiler bug that produced structurally valid but
 * semantically wrong bytecode would pass a "does it compile" test and fail
 * here, which is the only failure mode worth worrying about.
 *
 * No corpus and no cache needed.
 */

#include "ss_opcode.h"
#include "ssc.h"
#include "ssvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

struct HostRecord
{
    int mes_count;
    char last_message[256];
    int last_int_arg;
};

static int
record_command(struct SSVM_State* state, int opcode, int dot)
{
    struct HostRecord* record = (struct HostRecord*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    case SS_OP_MES:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        record->mes_count++;
        snprintf(record->last_message, sizeof(record->last_message), "%s", text);
        return 1;
    }

    case SS_OP_P_TELEJUMP:
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        record->last_int_arg = value;
        return 1;
    }

    default:
        return 0;
    }
}

struct Fixture
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSVM_Provider provider;
    struct SSVM_Env env;
    struct HostRecord record;
    char dir[256];
};

static int
fixture_compile(struct Fixture* fixture, const char* source, const char* label)
{
    char src_dir[300];
    char path[400];
    struct SSC_Diag diag;
    struct SSVM_Error err;
    FILE* file;

    memset(fixture, 0, sizeof(*fixture));
    snprintf(fixture->dir, sizeof(fixture->dir), "/tmp/ssc_test_%d", (int)getpid());
    snprintf(src_dir, sizeof(src_dir), "%s/src", fixture->dir);

    {
        char command[900];

        snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", fixture->dir, src_dir);
        if( system(command) != 0 )
        {
            printf("  FAIL %s: cannot create %s\n", label, fixture->dir);
            g_fail++;
            return 0;
        }
    }

    snprintf(path, sizeof(path), "%s/test.rs2", src_dir);
    file = fopen(path, "wb");
    if( !file )
    {
        printf("  FAIL %s: cannot write %s\n", label, path);
        g_fail++;
        return 0;
    }
    fputs(source, file);
    fclose(file);

    SSC_SymbolsInit(&fixture->symbols);
    /* A few symbols so subjects and typed arguments resolve. Real builds load
     * these from .pack files; the compiler cannot tell the difference. */
    SSC_SymbolsAdd(&fixture->symbols, "hans", 3105, SSC_SYM_NPC, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "coins", 995, SSC_SYM_OBJ, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "inv", 93, SSC_SYM_INV, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "quest_progress", 42, SSC_SYM_VARP, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "max_coins", 0, SSC_SYM_CONSTANT, "2147000000");
    SSC_SymbolsAdd(&fixture->symbols, "greeting", 0, SSC_SYM_CONSTANT, "\"Well met!\"");

    fixture->compiler = SSC_New(&fixture->symbols);
    memset(&diag, 0, sizeof(diag));

    if( !SSC_CompileDir(fixture->compiler, src_dir, &diag) )
    {
        printf("  FAIL %s: %s:%d: %s\n", label, diag.file, diag.line, diag.message);
        g_fail++;
        return 0;
    }
    if( !SSC_Write(fixture->compiler, fixture->dir, &diag) )
    {
        printf("  FAIL %s: write: %s\n", label, diag.message);
        g_fail++;
        return 0;
    }

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(&fixture->provider, fixture->dir, &err) )
    {
        printf("  FAIL %s: reading back what we just wrote: %s\n", label, err.message);
        g_fail++;
        return 0;
    }

    SSVM_EnvInit(&fixture->env, &fixture->provider);
    SSVM_EnvBindHost(&fixture->env, &fixture->record, record_command);
    return 1;
}

static void
fixture_close(struct Fixture* fixture)
{
    char command[600];

    SSVM_EnvFree(&fixture->env);
    SSVM_ProviderFree(&fixture->provider);
    SSC_Free(fixture->compiler);
    SSC_SymbolsFree(&fixture->symbols);

    snprintf(command, sizeof(command), "rm -rf %s", fixture->dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", fixture->dir);
}

/** Compile, run the named script, and report the top of the int stack. */
static int
run_script(
    struct Fixture* fixture,
    const char* name,
    const int32_t* args,
    int arg_count,
    int32_t* out,
    const char* label)
{
    const struct SSVM_Script* script = SSVM_ProviderGetByName(&fixture->provider, name);
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int ok = 0;

    if( !script )
    {
        printf("  FAIL %s: no script named %s\n", label, name);
        g_fail++;
        return 0;
    }

    state = SSVM_StateAlloc(&fixture->env, script, args, arg_count, NULL, 0);
    if( !state )
    {
        printf("  FAIL %s: could not allocate a state (argument count mismatch?)\n", label);
        g_fail++;
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, fixture);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    status = SSVM_Execute(state);
    if( status != SSVM_FINISHED )
    {
        printf("  FAIL %s: %s\n", label, SSVM_Backtrace(state));
        g_fail++;
    }
    else
    {
        if( out )
            *out = state->isp > 0 ? state->int_stack[state->isp - 1] : 0;
        ok = 1;
    }

    SSVM_StateRelease(state);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Cases                                                               */
/* ------------------------------------------------------------------ */

static void
test_minimal(void)
{
    struct Fixture fixture;
    int32_t result = 0;

    printf("minimal script\n");

    if( !fixture_compile(&fixture,
                         "[proc,answer]()(int)\n"
                         "return(42);\n",
                         "minimal") )
        return;

    CHECK_EQ(SSC_ScriptCount(fixture.compiler), 1, "one script compiled");
    if( run_script(&fixture, "[proc,answer]", NULL, 0, &result, "minimal") )
        CHECK_EQ(result, 42, "return value");

    fixture_close(&fixture);
}

static void
test_arguments_and_locals(void)
{
    struct Fixture fixture;
    int32_t args[2] = { 10, 3 };
    int32_t result = 0;

    printf("arguments and locals\n");

    /* Argument order is the point: `~sub(10, 3)` must give $a == 10. Getting
     * it backwards is invisible for a symmetric operation. */
    if( !fixture_compile(&fixture,
                         "[proc,subtract](int $a, int $b)(int)\n"
                         "def_int $result = calc($a - $b);\n"
                         "return($result);\n",
                         "arguments") )
        return;

    if( run_script(&fixture, "[proc,subtract]", args, 2, &result, "arguments") )
        CHECK_EQ(result, 7, "arguments bind in source order");

    fixture_close(&fixture);
}

static void
test_calc_precedence(void)
{
    struct Fixture fixture;
    int32_t result = 0;

    printf("calc precedence\n");

    if( !fixture_compile(&fixture,
                         "[proc,arith]()(int)\n"
                         "return(calc(2 + 3 * 4));\n",
                         "calc") )
        return;

    if( run_script(&fixture, "[proc,arith]", NULL, 0, &result, "calc") )
        CHECK_EQ(result, 14, "multiplication binds tighter than addition");

    fixture_close(&fixture);
}

static void
test_if_else(void)
{
    struct Fixture fixture;
    int32_t args[1];
    int32_t result = 0;

    printf("if / else if / else\n");

    if( !fixture_compile(&fixture,
                         "[proc,grade](int $score)(int)\n"
                         "if ($score >= 90) {\n"
                         "    return(1);\n"
                         "} else if ($score >= 50) {\n"
                         "    return(2);\n"
                         "} else {\n"
                         "    return(3);\n"
                         "}\n",
                         "if_else") )
        return;

    args[0] = 95;
    if( run_script(&fixture, "[proc,grade]", args, 1, &result, "if") )
        CHECK_EQ(result, 1, "the first arm");
    args[0] = 60;
    if( run_script(&fixture, "[proc,grade]", args, 1, &result, "else if") )
        CHECK_EQ(result, 2, "the else-if arm");
    args[0] = 10;
    if( run_script(&fixture, "[proc,grade]", args, 1, &result, "else") )
        CHECK_EQ(result, 3, "the else arm");

    fixture_close(&fixture);
}

static void
test_while(void)
{
    struct Fixture fixture;
    int32_t args[1] = { 5 };
    int32_t result = 0;

    printf("while\n");

    /* A backward branch with a negative delta, which is where a compiler that
     * treated deltas as byte offsets or forgot the +1 would loop forever. */
    if( !fixture_compile(&fixture,
                         "[proc,sum_to](int $n)(int)\n"
                         "def_int $total = 0;\n"
                         "def_int $i = 1;\n"
                         "while ($i <= $n) {\n"
                         "    $total = calc($total + $i);\n"
                         "    $i = calc($i + 1);\n"
                         "}\n"
                         "return($total);\n",
                         "while") )
        return;

    if( run_script(&fixture, "[proc,sum_to]", args, 1, &result, "while") )
        CHECK_EQ(result, 15, "1+2+3+4+5");

    fixture_close(&fixture);
}

static void
test_switch(void)
{
    struct Fixture fixture;
    int32_t args[1];
    int32_t result = 0;

    printf("switch\n");

    if( !fixture_compile(&fixture,
                         "[proc,pick](int $choice)(int)\n"
                         "switch_int ($choice) {\n"
                         "    case 1 : return(100);\n"
                         "    case 2, 3 : return(200);\n"
                         "    case default : return(999);\n"
                         "}\n",
                         "switch") )
        return;

    args[0] = 1;
    if( run_script(&fixture, "[proc,pick]", args, 1, &result, "switch 1") )
        CHECK_EQ(result, 100, "a single-value case");
    args[0] = 3;
    if( run_script(&fixture, "[proc,pick]", args, 1, &result, "switch 3") )
        CHECK_EQ(result, 200, "a multi-value case");
    args[0] = 77;
    if( run_script(&fixture, "[proc,pick]", args, 1, &result, "switch default") )
        CHECK_EQ(result, 999, "the default case");

    fixture_close(&fixture);
}

static void
test_proc_call(void)
{
    struct Fixture fixture;
    int32_t result = 0;

    printf("proc calls\n");

    /* The caller is declared first and calls a proc defined later in the file,
     * which only works because names are collected in a pass of their own. */
    if( !fixture_compile(&fixture,
                         "[proc,outer]()(int)\n"
                         "return(~inner(6, 7));\n"
                         "\n"
                         "[proc,inner](int $a, int $b)(int)\n"
                         "return(calc($a * $b));\n",
                         "proc") )
        return;

    CHECK_EQ(SSC_ScriptCount(fixture.compiler), 2, "two scripts compiled");
    if( run_script(&fixture, "[proc,outer]", NULL, 0, &result, "proc call") )
        CHECK_EQ(result, 42, "a proc defined later still resolves");

    fixture_close(&fixture);
}

static void
test_strings(void)
{
    struct Fixture fixture;
    int32_t args[1] = { 17 };

    printf("strings and interpolation\n");

    if( !fixture_compile(&fixture,
                         "[proc,greet](int $count)\n"
                         "mes(\"you have <$count> coins\");\n",
                         "strings") )
        return;

    if( run_script(&fixture, "[proc,greet]", args, 1, NULL, "strings") )
    {
        CHECK_EQ(fixture.record.mes_count, 1, "mes ran once");
        CHECK_EQ(strcmp(fixture.record.last_message, "you have 17 coins"), 0,
                 "an int interpolates into the literal");
    }

    fixture_close(&fixture);
}

static void
test_symbols_and_constants(void)
{
    struct Fixture fixture;

    printf("symbols and constants\n");

    /* A named symbol resolves to its id, and a constant expands to source text
     * that is then compiled in place — which is why a constant can hold a
     * string as easily as a number. */
    if( !fixture_compile(&fixture,
                         "[proc,use_symbols]\n"
                         "p_telejump(coins);\n"
                         "mes(^greeting);\n",
                         "symbols") )
        return;

    if( run_script(&fixture, "[proc,use_symbols]", NULL, 0, NULL, "symbols") )
    {
        CHECK_EQ(fixture.record.last_int_arg, 995, "a symbol resolves to its id");
        CHECK_EQ(strcmp(fixture.record.last_message, "Well met!"), 0,
                 "a constant expands to a string literal");
    }

    fixture_close(&fixture);
}

static void
test_varp(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("varp access\n");

    if( !fixture_compile(&fixture,
                         "[proc,bump_quest]\n"
                         "%quest_progress = 3;\n",
                         "varp") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider, "[proc,bump_quest]");
    CHECK(script != NULL, "the script compiled");
    if( script )
    {
        CHECK_EQ(script->opcodes[1], SS_OP_POP_VARP, "assignment emits POP_VARP");
        CHECK_EQ(script->int_operands[1], 42, "with the varp's id as the operand");
    }

    fixture_close(&fixture);
}

static void
test_trigger_subject(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("trigger headers\n");

    if( !fixture_compile(&fixture,
                         "[opnpc1,hans]\n"
                         "mes(\"Hello there.\");\n",
                         "trigger") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider, "[opnpc1,hans]");
    CHECK(script != NULL, "the script compiled");
    if( script )
    {
        CHECK_EQ(script->lookup_key & 0xff, SS_TRIGGER_OPNPC1, "the trigger byte");
        CHECK_EQ((script->lookup_key >> 8) & 3, SS_LOOKUP_TYPE, "addressed by type");
        CHECK_EQ(script->lookup_key >> 10, 3105, "the subject is the npc's id");
    }

    /* And it resolves through the same trigger lookup the mock server uses. */
    CHECK(SSVM_ProviderGetByTrigger(&fixture.provider, SS_TRIGGER_OPNPC1, 3105, -1) != NULL,
          "GetByTrigger finds it");
    CHECK(SSVM_ProviderGetByTrigger(&fixture.provider, SS_TRIGGER_OPNPC1, 9999, -1) == NULL,
          "and does not find it under another npc");

    fixture_close(&fixture);
}

static void
test_implicit_return(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("implicit return\n");

    /* The VM errors when pc runs past the last instruction, so a script without
     * a trailing RETURN could never complete. Content is not required to write
     * one. */
    if( !fixture_compile(&fixture,
                         "[proc,no_return]\n"
                         "mes(\"done\");\n",
                         "implicit return") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider, "[proc,no_return]");
    CHECK(script != NULL, "the script compiled");
    if( script )
        CHECK_EQ(script->opcodes[script->op_count - 1], SS_OP_RETURN,
                 "a RETURN is appended");

    if( run_script(&fixture, "[proc,no_return]", NULL, 0, NULL, "implicit return") )
        CHECK_EQ(fixture.record.mes_count, 1, "and the script completes");

    fixture_close(&fixture);
}

static void
test_errors(void)
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    char dir[256];
    char path[400];
    char command[900];
    FILE* file;

    printf("compile errors\n");

    snprintf(dir, sizeof(dir), "/tmp/ssc_err_%d", (int)getpid());
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", dir, dir);
    if( system(command) != 0 )
        return;

    snprintf(path, sizeof(path), "%s/bad.rs2", dir);
    file = fopen(path, "wb");
    fputs("[proc,broken]\n"
          "definitely_not_a_command(1);\n",
          file);
    fclose(file);

    SSC_SymbolsInit(&symbols);
    compiler = SSC_New(&symbols);
    memset(&diag, 0, sizeof(diag));

    /* An unknown command must be a compile error, not something that silently
     * becomes a stub call at run time. */
    CHECK(!SSC_CompileDir(compiler, dir, &diag), "an unknown command fails the compile");
    CHECK(diag.message[0] != '\0', "and reports a message");
    CHECK(diag.line > 0, "with a line number");
    printf("  reported: %s:%d: %s\n", diag.file, diag.line, diag.message);

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);
    snprintf(command, sizeof(command), "rm -rf %s", dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", dir);
}

int
main(void)
{
    test_minimal();
    test_arguments_and_locals();
    test_calc_precedence();
    test_if_else();
    test_while();
    test_switch();
    test_proc_call();
    test_strings();
    test_symbols_and_constants();
    test_varp();
    test_trigger_subject();
    test_implicit_return();
    test_errors();

    if( g_fail )
    {
        printf("ssc test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ssc test: all passed\n");
    return 0;
}
