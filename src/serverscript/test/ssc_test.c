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

enum
{
    /* Deliberately wider than SSC_MAX_VARARG_TYPES, so an over-long list would
     * be recorded rather than clipped by the recorder itself. */
    RECORD_VARARG_MAX = 64
};

struct HostRecord
{
    int mes_count;
    char last_message[256];
    int last_int_arg;

    /* RUNCLIENTSCRIPTVARARG, recorded exactly as the mock server's host case
     * pops it (mock230_scripts.c, `case SS_OP_RUNCLIENTSCRIPTVARARG`). */
    int run_count;
    int32_t run_script_id;
    int run_argc;
    char run_types[RECORD_VARARG_MAX + 1];
    int32_t run_intv[RECORD_VARARG_MAX];
    char run_strv[RECORD_VARARG_MAX][64];
    /* Both stack pointers as the handler left them. A variadic that pops the
     * wrong number of values does not fail at the call — it leaves the script
     * running on a skewed stack, and the skew surfaces somewhere else. */
    int32_t run_isp_after;
    int32_t run_ssp_after;
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

    /*
     * `runclientscript*(clientscript)(args...)` — the variadic push.
     *
     * This is a transcription of the mock server's host case, not a
     * simplification of it, because the layout is the thing under test: the
     * compiler emits the declared arguments, then the vararg values, then a
     * type string describing them, so the type string is popped FIRST and
     * walked BACKWARDS (the last value pushed is the first one off).
     *
     * The copy of the type string before the loop is load-bearing: it is a
     * `SSVM_StrPool` pointer, and the loop pops other pool pointers over it.
     */
    case SS_OP_RUNCLIENTSCRIPTVARARG:
    {
        const char* type_string;
        int argc;
        int i;

        if( !SSVM_PopStr(state, &type_string) )
            return 1;
        argc = (int)strlen(type_string);
        if( argc > RECORD_VARARG_MAX )
        {
            SSVM_Abort(state, "recorder holds at most %d vararg values, given %d",
                       RECORD_VARARG_MAX, argc);
            return 1;
        }
        memcpy(record->run_types, type_string, (size_t)argc);
        record->run_types[argc] = '\0';

        for( i = argc - 1; i >= 0; i-- )
        {
            record->run_intv[i] = 0;
            record->run_strv[i][0] = '\0';
            if( record->run_types[i] == 's' )
            {
                const char* text;

                if( !SSVM_PopStr(state, &text) )
                    return 1;
                snprintf(record->run_strv[i], sizeof(record->run_strv[i]), "%s", text);
            }
            else
            {
                if( !SSVM_PopInt(state, &record->run_intv[i]) )
                    return 1;
            }
        }
        if( !SSVM_PopInt(state, &record->run_script_id) )
            return 1;
        record->run_argc = argc;
        record->run_count++;
        record->run_isp_after = state->isp;
        record->run_ssp_after = state->ssp;
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
    /* The stat-name collision the arg_kind_hint exists for, as the real cache
     * has it: `hitpoints` is param 2100 *and* stat 3, and PARAM sorts first.
     * See test_stat_argument_hint. */
    SSC_SymbolsAdd(&fixture->symbols, "hitpoints", 2100, SSC_SYM_PARAM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "hitpoints", 3, SSC_SYM_STAT, NULL);
    /* The seq/spotanim-name collision the SPOTANIM_* arg_kind_hint exists for,
     * as the real cache has it: `tzhaar_rock_smash` is seq 2660 *and* spotanim
     * 451, and SEQ sorts before SPOTANIM. See test_spotanim_argument_hint. */
    SSC_SymbolsAdd(&fixture->symbols, "tzhaar_rock_smash", 2660, SSC_SYM_SEQ, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "tzhaar_rock_smash", 451, SSC_SYM_SPOTANIM, NULL);
    SSC_SymbolsAdd(&fixture->symbols, "max_coins", 0, SSC_SYM_CONSTANT, "2147000000");
    SSC_SymbolsAdd(&fixture->symbols, "greeting", 0, SSC_SYM_CONSTANT, "\"Well met!\"");
    /*
     * The builtin seed — type names, npc modes, loc shapes. `sscompile` calls
     * this AFTER the packs (ssc_main.c), and the order is load-bearing: a type
     * name that a pack has already claimed is exactly the collision
     * test_param_type_shadowing pins, so seeding first here would hide it. The
     * fixture did not call this at all, which is why every header type silently
     * fell back to `int` and no test could see either fact.
     */
    SSC_SymbolsSeedBuiltins(&fixture->symbols);

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
test_coord_subject(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("coord subjects are name-addressed\n");

    /*
     * The zone family's subject is a *place*, and a place is not a type id.
     *
     * Two things are pinned here and both are otherwise permanently silent:
     *
     *  - the compiled `lookup_key` is **-1**. A five-part coord lexes into 28
     *    bits and the key field gives a subject 21, so any key written for one
     *    is either negative (which `ssvm_provider.c` drops from the index) or
     *    wrapped onto an address nothing can reproduce. The wrapped ones *would*
     *    be indexed, and a later reader would reasonably conclude the trigger is
     *    keyed.
     *  - the stored **name** is the header's raw text, character for character.
     *    `mock230_scripts_run_trigger_at` rebuilds that string with an
     *    `snprintf` and asks `getByName`; if the lexer ever normalised a coord
     *    token, or the compiler zero-padded a component, every `[zone]` in the
     *    tree would stop running and nothing would report it.
     */
    if( !fixture_compile(&fixture,
                         "[zone,0_50_50_16_16]\n"
                         "mes(\"in the zone\");\n"
                         "[mapzone,0_50_50]\n"
                         "mes(\"in the square\");\n",
                         "coord") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider, "[zone,0_50_50_16_16]");
    CHECK(script != NULL, "a five-part coord subject keeps its raw text as the name");
    if( script )
        CHECK_EQ(script->lookup_key, -1, "and is name-addressed, not keyed");

    script = SSVM_ProviderGetByName(&fixture.provider, "[mapzone,0_50_50]");
    CHECK(script != NULL, "and so does a three-part one");
    if( script )
        CHECK_EQ(script->lookup_key, -1, "which is also name-addressed");

    /* The three-part form used to compile to `parseInt("0_50_50")` = 0, so this
     * is the lookup that would have found it under a subject of 0. */
    CHECK(SSVM_ProviderGetByTrigger(&fixture.provider, SS_TRIGGER_MAPZONE, 0, -1) == NULL,
          "no coord subject is reachable through the trigger index");

    fixture_close(&fixture);
}

/*
 * `runclientscript*` — a runtime-determined mix of ints and strings.
 *
 * This is the whole of Blocker A end to end in one test: source -> compiler ->
 * bytecode -> reader -> VM -> host callback. What it pins is the *layout*, not
 * the opcode's existence, because the opcode already existed and only its
 * int-only half was ever exercised by content (the two live call sites,
 * `skill_guide.rs2` and `slayer_rewards.rs2`, are four ints and three ints).
 *
 * Four things can be wrong here and only the first is loud on its own:
 *
 *   1. the type string is absent or mis-ordered -> the VM pops the wrong stack
 *      and aborts;
 *   2. the type character is derived from something other than the *static type
 *      of each vararg expression* -> a string is popped as an int, silently,
 *      and the clientscript draws a panel from a pool pointer;
 *   3. the values are handed over reversed -> the panel is drawn from the right
 *      values in the wrong slots, which looks like a content bug;
 *   4. the handler pops a different number of values than were pushed -> the
 *      *rest of the script* runs on a skewed stack and fails elsewhere.
 *
 * So the asserts are: the type string itself, every value in declaration order,
 * and both stack pointers back at zero afterwards.
 */
static void
test_runclientscript_vararg(void)
{
    struct Fixture fixture;
    int32_t args[1] = { 5 };

    printf("runclientscript* (variadic int/string push)\n");

    /*
     * Nothing here is a literal of the type it claims:
     *   $a       an int parameter
     *   $b       an int local computed at run time
     *   $label   a string local
     *   ~answer  a proc call whose *return type* is what makes it an 'i'
     *   ^greeting a string constant, which is a string only after expansion
     * A compiler that guessed the type from the token rather than from the
     * expression gets at least two of these wrong.
     */
    if( !fixture_compile(&fixture,
                         "[proc,answer]()(int)\n"
                         "return(7);\n"
                         "[proc,push_mixed](int $a)\n"
                         "def_int $b = calc($a + 100);\n"
                         "def_string $label = \"hello\";\n"
                         "runclientscript*(1074)($a, $b, $label, ~answer, ^greeting);\n"
                         "[proc,push_ints]\n"
                         "runclientscript*(1902)(1, 2, 3, 4);\n"
                         "[proc,push_strings]\n"
                         "runclientscript*(58)(\"a\", \"b\");\n"
                         "[proc,push_none]\n"
                         "runclientscript*(1749)();\n",
                         "runclientscript") )
        return;

    if( run_script(&fixture, "[proc,push_mixed]", args, 1, NULL, "runclientscript") )
    {
        CHECK_EQ(fixture.record.run_count, 1, "the vararg command reached the host once");
        CHECK_EQ(fixture.record.run_script_id, 1074, "the fixed argument is the clientscript id");
        CHECK_EQ(fixture.record.run_argc, 5, "five vararg values");
        CHECK(strcmp(fixture.record.run_types, "iisis") == 0,
              "the type string is the static type of each expression, in order");
        if( strcmp(fixture.record.run_types, "iisis") != 0 )
            printf("       got \"%s\", want \"iisis\"\n", fixture.record.run_types);
        CHECK_EQ(fixture.record.run_intv[0], 5, "value 0 is the int parameter");
        CHECK_EQ(fixture.record.run_intv[1], 105, "value 1 is the computed int local");
        CHECK(strcmp(fixture.record.run_strv[2], "hello") == 0, "value 2 is the string local");
        CHECK_EQ(fixture.record.run_intv[3], 7, "value 3 is the proc's int return");
        CHECK(strcmp(fixture.record.run_strv[4], "Well met!") == 0,
              "value 4 is the expanded string constant");
        /* The one that catches a handler which pops a wrong count: the values
         * are consumed exactly, so the caller's stacks are empty again. */
        CHECK_EQ(fixture.record.run_isp_after, 0, "the int stack is drained");
        CHECK_EQ(fixture.record.run_ssp_after, 0, "the string stack is drained");
    }

    /* The two shapes content already ships, and the empty list, so a change
     * that only handles the mixed case does not pass. */
    if( run_script(&fixture, "[proc,push_ints]", NULL, 0, NULL, "runclientscript") )
    {
        CHECK(strcmp(fixture.record.run_types, "iiii") == 0, "an all-int list is \"iiii\"");
        CHECK_EQ(fixture.record.run_intv[3], 4, "and its last value is last");
        CHECK_EQ(fixture.record.run_ssp_after, 0, "with nothing left on the string stack");
    }
    if( run_script(&fixture, "[proc,push_strings]", NULL, 0, NULL, "runclientscript") )
    {
        CHECK(strcmp(fixture.record.run_types, "ss") == 0, "an all-string list is \"ss\"");
        CHECK(strcmp(fixture.record.run_strv[0], "a") == 0, "in declaration order");
        CHECK_EQ(fixture.record.run_isp_after, 0, "with nothing left on the int stack");
    }
    if( run_script(&fixture, "[proc,push_none]", NULL, 0, NULL, "runclientscript") )
    {
        CHECK_EQ(fixture.record.run_argc, 0, "an empty vararg list is zero values");
        CHECK_EQ(fixture.record.run_script_id, 1749, "and still carries the script id");
    }

    fixture_close(&fixture);
}

/*
 * The compiler's own guards on the vararg list.
 *
 * `SSC_MAX_VARARG_TYPES` is the first of three caps the same value has to pass
 * (the mock host's `MOCK230_RUNCLIENTSCRIPT_ARG_MAX` and the client's
 * `PKT_RUNCLIENTSCRIPT_ARG_MAX` are the other two), and it is the only one that
 * can refuse *before* anything is on the wire. Refusing is the point: the
 * failure mode it replaces is a clientscript run with some of its arguments,
 * which does not error anywhere — it draws the wrong panel.
 */
/*
 * A queue/timer argument names a script, even when a pack also spells the name.
 *
 * This is the same collision class as `test_param_type_shadowing` above and as
 * the stat/interface hints in `parse_command`, and it is the one that got out.
 * `settimer(poison, 30)` compiled to `settimer(273, 30)` because obj 273 is
 * named `poison` and the script namespace was only consulted *after* the packs.
 * The engine then armed a timer whose "script id" was an obj id and ran whatever
 * script sat at 273 — a `[label,...]` from a quest, whose first statement is a
 * `~chatplayer`. Nothing failed: the name resolved, the opcode was legal, the
 * timer fired, and a player nowhere near that quest got its dialogue box.
 *
 * The fixture seeds `coins` as obj 995, so `[timer,coins]` reproduces it exactly:
 * the operand must be the script's id and must not be 995.
 */
static void
test_script_name_argument(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* arm;
    const struct SSVM_Script* target;
    int found = 0;

    printf("a timer/queue argument that a pack also names\n");

    if( !fixture_compile(&fixture,
                         "[proc,arm]\n"
                         "settimer(coins, 30);\n"
                         "\n"
                         "[timer,coins]\n"
                         "~noop;\n"
                         "\n"
                         "[proc,noop]()\n",
                         "script name argument") )
        return;

    arm = SSVM_ProviderGetByName(&fixture.provider, "[proc,arm]");
    target = SSVM_ProviderGetByName(&fixture.provider, "[timer,coins]");
    if( !arm || !target )
    {
        printf("  FAIL script name argument: no script\n");
        g_fail++;
        fixture_close(&fixture);
        return;
    }

    for( int i = 0; i < arm->op_count; i++ )
    {
        if( arm->opcodes[i] != SS_OP_SETTIMER )
            continue;
        found = 1;
        /* settimer pushes (script, interval) then calls, so the script id is
         * two instructions back. */
        CHECK(i >= 2 && arm->opcodes[i - 2] == SS_OP_PUSH_CONSTANT_INT,
              "the script argument is a constant push");
        if( i >= 2 && arm->opcodes[i - 2] == SS_OP_PUSH_CONSTANT_INT )
        {
            CHECK_EQ(arm->int_operands[i - 2], target->id,
                     "settimer's first argument is the timer script's id");
            CHECK(arm->int_operands[i - 2] != 995,
                  "and not the obj that shares the name");
        }
    }
    CHECK(found, "the command compiled to SETTIMER");

    fixture_close(&fixture);
}

/*
 * A header's declared types survive into `param_types`, even when the type name
 * is also a pack symbol.
 *
 * `inv` is both: the backpack container is *named* `inv`, so the seed loop that
 * registered type names "if nothing else claims the name" never registered the
 * `inv` TYPE at all, and `(inv $x)` recorded its param type as `int`. Nothing in
 * a compiled script could notice — an inv rides the int stack either way — so
 * the only observable is the one consumer that reads `param_types` back:
 * `mock230_scripts_run_debugproc`, which resolves each argument through the pack
 * named by its type. With `inv` recorded as `int` it ran `strtol` over the
 * container's *name* and passed container 0 instead.
 *
 * The fixture already adds `inv` as SSC_SYM_INV, which is exactly the
 * collision — so this fails by construction without the per-kind seed guard in
 * ssc_symbols.c.
 */
/* Every stat command must resolve a bare stat name as a STAT, not as whatever
 * pack symbol shares the word. NPC_BASESTAT is spelled out in the compiler's
 * membership test because its name matches neither the `STAT_` nor the
 * `NPC_STAT` prefix the rest of the family is caught by — and while it did not,
 * `npc_basestat(hitpoints)` compiled to param 2100 and every caller read 0. */
static void
test_stat_argument_hint(void)
{
    struct Fixture fixture;
    static const char* const k_calls[] = {
        "stat(hitpoints)",
        "stat_base(hitpoints)",
        "npc_stat(hitpoints)",
        "npc_basestat(hitpoints)",
    };

    printf("bare stat names in stat-command arguments\n");

    if( !fixture_compile(&fixture,
                         "[proc,s0]()(int)\n"
                         "return(stat(hitpoints));\n"
                         "\n"
                         "[proc,s1]()(int)\n"
                         "return(stat_base(hitpoints));\n"
                         "\n"
                         "[proc,s2]()(int)\n"
                         "return(npc_stat(hitpoints));\n"
                         "\n"
                         "[proc,s3]()(int)\n"
                         "return(npc_basestat(hitpoints));\n",
                         "stat argument hint") )
        return;

    for( int i = 0; i < 4; i++ )
    {
        char script_name[32];
        const struct SSVM_Script* script;
        int pushed = -1;

        snprintf(script_name, sizeof(script_name), "[proc,s%d]", i);
        script = SSVM_ProviderGetByName(&fixture.provider, script_name);
        if( !script )
        {
            printf("  FAIL stat argument hint: no %s\n", script_name);
            g_fail++;
            continue;
        }
        for( int op = 0; op < script->op_count; op++ )
        {
            if( script->opcodes[op] == SS_OP_PUSH_CONSTANT_INT )
            {
                pushed = script->int_operands[op];
                break;
            }
        }
        if( pushed != 3 )
        {
            printf("  FAIL %s pushes %d, want stat 3 (param `hitpoints` is 2100)\n",
                   k_calls[i], pushed);
            g_fail++;
        }
        else
        {
            printf("  ok   %s -> stat 3\n", k_calls[i]);
        }
    }

    fixture_close(&fixture);
}

/*
 * The spotanim family's leading argument names a spotanim, even when a seq
 * shares the name — routine, since a spotanim's `anim=` field is commonly
 * just the spotanim's own name again.
 *
 * This is the same collision class `test_stat_argument_hint` covers, and it
 * is the one that got out: `spotanim_map(tzhaar_rock_smash, coord, 0, 0)`
 * compiled to `spotanim_map(2660, coord, 0, 0)` — 2660 is the SEQ
 * `tzhaar_rock_smash`, not the SPOTANIM (451), because SEQ sorts before
 * SPOTANIM in SSC_SymbolKind and the argument was resolved unhinted. The
 * client spawned whatever spotanim happens to be numbered 2660 instead of
 * the falling-rock graphic, so JalTok-Jad's ranged attack rendered nothing a
 * player could see land.
 */
static void
test_spotanim_argument_hint(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("bare spotanim names in spotanim-command arguments\n");

    if( !fixture_compile(&fixture,
                         "[proc,s0]\n"
                         "spotanim_map(tzhaar_rock_smash, 0_50_50, 0, 0);\n"
                         "\n"
                         "[proc,s1]\n"
                         "spotanim_pl(tzhaar_rock_smash, 0, 0);\n"
                         "\n"
                         "[proc,s2]\n"
                         "spotanim_npc(tzhaar_rock_smash, 0, 0);\n",
                         "spotanim argument hint") )
        return;

    static const struct
    {
        const char* script_name;
        int opcode;
        int argc;
    } k_cases[] = {
        { "[proc,s0]", SS_OP_SPOTANIM_MAP, 4 },
        { "[proc,s1]", SS_OP_SPOTANIM_PL, 3 },
        { "[proc,s2]", SS_OP_SPOTANIM_NPC, 3 },
    };

    for( size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++ )
    {
        int found = 0;

        script = SSVM_ProviderGetByName(&fixture.provider, k_cases[i].script_name);
        if( !script )
        {
            printf("  FAIL spotanim argument hint: no %s\n", k_cases[i].script_name);
            g_fail++;
            continue;
        }
        for( int op = 0; op < script->op_count; op++ )
        {
            if( script->opcodes[op] != k_cases[i].opcode )
                continue;
            found = 1;
            /* Arguments push left to right then the call, so the leading
             * (spotanim) argument sits argc instructions back. */
            CHECK(op >= k_cases[i].argc &&
                      script->opcodes[op - k_cases[i].argc] == SS_OP_PUSH_CONSTANT_INT,
                  "the spotanim argument is a constant push");
            if( op >= k_cases[i].argc &&
                script->opcodes[op - k_cases[i].argc] == SS_OP_PUSH_CONSTANT_INT )
            {
                CHECK_EQ(script->int_operands[op - k_cases[i].argc], 451,
                         "the spotanim argument resolves to spotanim 451");
                CHECK(script->int_operands[op - k_cases[i].argc] != 2660,
                      "and not the seq that shares the name");
            }
        }
        CHECK(found, "the command compiled to its host opcode");
    }

    fixture_close(&fixture);
}

static void
test_param_type_shadowing(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;

    printf("a type name that is also a pack symbol\n");

    if( !fixture_compile(&fixture,
                         "[proc,take_inv](inv $inv, int $slot, namedobj $obj)\n"
                         "~noop;\n"
                         "\n"
                         "[proc,noop]()\n",
                         "param type shadowing") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider, "[proc,take_inv]");
    if( !script )
    {
        printf("  FAIL param type shadowing: no script\n");
        g_fail++;
        fixture_close(&fixture);
        return;
    }
    CHECK_EQ(script->param_type_count, 3, "three declared params");
    /* 118 'v' inv, 105 'i' int, 79 'O' namedobj — ScriptVarType.getTypeChar. */
    CHECK_EQ((int)script->param_types[0], 118, "an `inv` param keeps its type");
    CHECK_EQ((int)script->param_types[1], 105, "an `int` param keeps its type");
    CHECK_EQ((int)script->param_types[2], 79, "a `namedobj` param keeps its type");

    fixture_close(&fixture);
}

static void
test_vararg_limits(void)
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    char dir[256];
    char path[400];
    char command[600];
    FILE* file;
    int i;

    printf("vararg list limits\n");

    snprintf(dir, sizeof(dir), "/tmp/ssc_vararg_%d", (int)getpid());
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", dir, dir);
    if( system(command) != 0 )
        return;

    snprintf(path, sizeof(path), "%s/bad.rs2", dir);
    file = fopen(path, "wb");
    if( !file )
        return;
    fputs("[proc,too_many]\n"
          "runclientscript*(1074)(",
          file);
    for( i = 0; i < SSC_MAX_VARARG_TYPES + 1; i++ )
        fprintf(file, "%s%d", i ? ", " : "", i);
    fputs(");\n"
          /* A command with no `*` form must not silently become its plain
           * form: `mes*` is not `mes`. */
          "[proc,no_star_form]\n"
          "mes*(\"x\")(1);\n",
          file);
    fclose(file);

    SSC_SymbolsInit(&symbols);
    compiler = SSC_New(&symbols);
    memset(&diag, 0, sizeof(diag));

    CHECK(!SSC_CompileDir(compiler, dir, &diag),
          "more vararg values than SSC_MAX_VARARG_TYPES fails the compile");
    CHECK(diag.line > 0, "and reports where");
    printf("  reported: %s:%d: %s\n", diag.file, diag.line, diag.message);

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);

    /* Second file on its own, so the first diagnostic does not mask it. */
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", dir, dir);
    if( system(command) != 0 )
        return;
    snprintf(path, sizeof(path), "%s/nostar.rs2", dir);
    file = fopen(path, "wb");
    if( !file )
        return;
    fputs("[proc,no_star_form]\n"
          "mes*(\"x\")(1);\n",
          file);
    fclose(file);

    SSC_SymbolsInit(&symbols);
    compiler = SSC_New(&symbols);
    memset(&diag, 0, sizeof(diag));
    CHECK(!SSC_CompileDir(compiler, dir, &diag),
          "a command with no vararg form fails the compile");
    printf("  reported: %s:%d: %s\n", diag.file, diag.line, diag.message);
    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);

    snprintf(command, sizeof(command), "rm -rf %s", dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", dir);
}

static void
test_duplicate_debugproc(void)
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    char dir[256];
    char path[400];
    char command[900];
    FILE* file;

    printf("duplicate global debug commands\n");

    snprintf(dir, sizeof(dir), "/tmp/ssc_debugproc_%d", (int)getpid());
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", dir, dir);
    if( system(command) != 0 )
        return;

    /* A duplicate debugproc used to compile without a diagnostic: both bodies
     * resolved to one global name slot and the later body silently replaced
     * the earlier one. That was the server-side half of the ::crystal_set/Cry
     * incident. A command namespace must never pick a winner by file order. */
    snprintf(path, sizeof(path), "%s/a.rs2", dir);
    file = fopen(path, "wb");
    fputs("[debugproc,same_command]\nmes(\"first\");\n", file);
    fclose(file);
    snprintf(path, sizeof(path), "%s/b.rs2", dir);
    file = fopen(path, "wb");
    fputs("[debugproc,same_command]\nmes(\"second\");\n", file);
    fclose(file);

    SSC_SymbolsInit(&symbols);
    compiler = SSC_New(&symbols);
    memset(&diag, 0, sizeof(diag));
    CHECK(!SSC_CompileDir(compiler, dir, &diag),
          "a duplicate global debugproc fails the compile");
    CHECK(strstr(diag.message, "duplicate global debug command") != NULL,
          "and names the duplicate-command invariant");
    CHECK(strstr(diag.message, "[debugproc,same_command]") != NULL,
          "and identifies the exact command name");
    printf("  reported: %s:%d: %s\n", diag.file, diag.line, diag.message);

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);
    snprintf(command, sizeof(command), "rm -rf %s", dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", dir);
}

static void
test_duplicate_login(void)
{
    struct SSC_Symbols symbols;
    struct SSC_Compiler* compiler;
    struct SSC_Diag diag;
    char dir[256];
    char path[400];
    char command[900];
    FILE* file;

    printf("duplicate global login trigger\n");

    snprintf(dir, sizeof(dir), "/tmp/ssc_login_%d", (int)getpid());
    snprintf(command, sizeof(command), "rm -rf %s && mkdir -p %s", dir, dir);
    if( system(command) != 0 )
        return;

    /* A second [login,_] used to replace the first body by sorted source path.
     * Losing the canonical body also loses its IF_SETEVENTS setup, so ordinary
     * inventory clicks are silently rejected by the client permission mask. */
    snprintf(path, sizeof(path), "%s/a.rs2", dir);
    file = fopen(path, "wb");
    fputs("[login,_]\nmes(\"canonical\");\n", file);
    fclose(file);
    snprintf(path, sizeof(path), "%s/b.rs2", dir);
    file = fopen(path, "wb");
    fputs("[login,_]\nmes(\"replacement\");\n", file);
    fclose(file);

    SSC_SymbolsInit(&symbols);
    compiler = SSC_New(&symbols);
    memset(&diag, 0, sizeof(diag));
    CHECK(!SSC_CompileDir(compiler, dir, &diag),
          "a duplicate global login trigger fails the compile");
    CHECK(strstr(diag.message, "duplicate global login trigger") != NULL,
          "and names the singleton-trigger invariant");
    CHECK(strstr(diag.message, "[login,_]") != NULL,
          "and identifies the exact trigger name");
    printf("  reported: %s:%d: %s\n", diag.file, diag.line, diag.message);

    SSC_Free(compiler);
    SSC_SymbolsFree(&symbols);
    snprintf(command, sizeof(command), "rm -rf %s", dir);
    if( system(command) != 0 )
        printf("  note: could not clean up %s\n", dir);
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
test_npc_runtime_primitives(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;
    int saw_get = 0;
    int saw_set = 0;
    int saw_projanim_map = 0;

    printf("npc runtime primitive commands\n");

    if( !fixture_compile(&fixture,
                         "[proc,npc_runtime_primitives]\n"
                         "npc_var_set(3, 41);\n"
                         "def_int $value = npc_var_get(3);\n"
                         "projanim_map(0_50_50_10_10, 0_50_50_12_11, 123, "
                         "1, 2, 3, 4, 5, 6);\n",
                         "npc runtime primitives") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider,
                                    "[proc,npc_runtime_primitives]");
    CHECK(script != NULL, "the npc runtime primitive script compiled");
    if( script )
    {
        for( int op = 0; op < script->op_count; op++ )
        {
            saw_get += script->opcodes[op] == SS_OP_NPC_VAR_GET;
            saw_set += script->opcodes[op] == SS_OP_NPC_VAR_SET;
            saw_projanim_map += script->opcodes[op] == SS_OP_PROJANIM_MAP;
        }
    }
    CHECK_EQ(saw_get, 1, "npc_var_get compiles to its host opcode");
    CHECK_EQ(saw_set, 1, "npc_var_set compiles to its host opcode");
    CHECK_EQ(saw_projanim_map, 1, "projanim_map compiles to its host opcode");

    fixture_close(&fixture);
}

static void
test_player_action_lock_primitives(void)
{
    struct Fixture fixture;
    const struct SSVM_Script* script;
    int saw_lock = 0;
    int saw_unlock = 0;

    printf("player action-lock primitive commands\n");

    if( !fixture_compile(&fixture,
                         "[proc,player_action_lock_primitives]\n"
                         "player_lock();\n"
                         "player_unlock();\n",
                         "player action-lock primitives") )
        return;

    script = SSVM_ProviderGetByName(&fixture.provider,
                                    "[proc,player_action_lock_primitives]");
    CHECK(script != NULL, "the player action-lock primitive script compiled");
    if( script )
    {
        for( int op = 0; op < script->op_count; op++ )
        {
            saw_lock += script->opcodes[op] == SS_OP_PLAYER_LOCK;
            saw_unlock += script->opcodes[op] == SS_OP_PLAYER_UNLOCK;
        }
    }
    CHECK_EQ(saw_lock, 1, "player_lock compiles to its host opcode");
    CHECK_EQ(saw_unlock, 1, "player_unlock compiles to its host opcode");

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
    test_coord_subject();
    test_implicit_return();
    test_npc_runtime_primitives();
    test_player_action_lock_primitives();
    test_runclientscript_vararg();
    test_vararg_limits();
    test_duplicate_debugproc();
    test_duplicate_login();
    test_stat_argument_hint();
    test_spotanim_argument_hint();
    test_param_type_shadowing();
    test_script_name_argument();
    test_errors();

    if( g_fail )
    {
        printf("ssc test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ssc test: all passed\n");
    return 0;
}
