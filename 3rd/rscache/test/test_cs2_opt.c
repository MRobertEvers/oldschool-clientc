/*
 * The optimizer's own tests: folding arithmetic, and the passes that move code.
 *
 * These do not need a cache. They build bytecode by hand, run it through the
 * real pipeline — interpret, transform, optimize, lower — and check what comes
 * out, which is exactly what `cachepack cs2opt` does to nine thousand scripts.
 *
 * One thing these tests do NOT do, stated because the opposite is easy to
 * assume. Dead-store elimination once built its flow graph, then deleted
 * instructions while still asking that graph questions, and removed a loop's
 * `$i = $i + 1` — script 4731 then ran until the VM's million-cycle cap cut it
 * off. The loop case below was written to pin that, and **it does not
 * reproduce it**: reintroducing the exact faulty pass leaves all 24 checks
 * here passing. The bug needs a 1,871-instruction script with enough earlier
 * deletions to perturb the block bounds, and shrinking it removes it.
 *
 * So the loop case pins the *shape* — a counted loop keeps its induction store
 * and its back edge — and the thing that actually catches this class is
 * `make -C src cs2-opt-verify`: optimize the whole tree, bake a cache, boot the
 * client headless, and fail on a non-zero `cs2_aborts`. An infinite loop is
 * well-formed bytecode; it lowers, it re-interprets, and only running it says
 * otherwise. Keep that target in the loop when touching a pass that deletes or
 * moves anything.
 */
#include "cs2/cs2_dfa.h"
#include "cs2/cs2_effects.h"
#include "cs2/cs2_interp.h"
#include "cs2/cs2_lower.h"
#include "cs2/cs2_opt.h"
#include "datatypes/clientscript.h"
#include "rscache.h"
#include "rscache_test.h"

#include <stdlib.h>
#include <string.h>

/* ---- building a script by hand ------------------------------------------ */

struct op
{
    int opcode;
    int operand;
    const char* text;
};

struct fixture
{
    struct RSCache_CS2_Script script;
    struct RSCache_CS2_Script* table[64];
    int count;
};

static struct fixture g_fixture;

static void
build(struct RSCache_CS2_Script* script, int id, int int_locals, int int_args,
      const struct op* ops, int count)
{
    RSCache_CS2_ScriptInit(script);
    script->script_id = id;
    script->signature = (char*)calloc(1, 1);
    script->local_int_count = int_locals;
    script->int_argument_count = int_args;
    script->op_count = count;
    script->opcodes = (uint16_t*)calloc((size_t)count, sizeof(uint16_t));
    script->int_operands = (int*)calloc((size_t)count, sizeof(int));
    script->long_operands = (int64_t*)calloc((size_t)count, sizeof(int64_t));
    script->string_operands = (char**)calloc((size_t)count, sizeof(char*));
    for( int i = 0; i < count; i++ )
    {
        script->opcodes[i] = (uint16_t)ops[i].opcode;
        script->int_operands[i] = ops[i].operand;
        if( !ops[i].text )
            continue;
        size_t length = strlen(ops[i].text);
        script->string_operands[i] = (char*)malloc(length + 1);
        memcpy(script->string_operands[i], ops[i].text, length + 1);
    }
}

static const struct RSCache_CS2_Script*
fixture_load(void* user, int script_id)
{
    (void)user;
    for( int i = 0; i < g_fixture.count; i++ )
    {
        if( g_fixture.table[i] && g_fixture.table[i]->script_id == script_id )
            return g_fixture.table[i];
    }
    return NULL;
}

static enum RSCache_CS2_Type
fixture_param(void* user, int param_id)
{
    (void)user;
    (void)param_id;
    return RSCACHE_CS2_TYPE_INT;
}

/**
 * Run the whole pipeline over one script and hand back the result.
 *
 * Returns false when any stage refuses, which is itself a finding — every stage
 * here is meant to accept anything the interpreter accepted.
 */
static bool
optimize_script(struct RSCache_CS2_Script* input, int level, struct RSCache_CS2_Script* out,
                struct RSCache_CS2_OptStats* stats)
{
    g_fixture.table[0] = input;
    g_fixture.count = 1;

    struct RSCache_CS2_DecompileOptions options;
    memset(&options, 0, sizeof(options));
    options.scripts.load = fixture_load;
    options.param_types.load = fixture_param;

    struct RSCache_CS2_FunctionSet fs;
    RSCache_CS2_FunctionSetInit(&fs);
    char error[512] = "";
    int id = input->script_id;
    bool ok = RSCache_CS2_Interpret(&fs, &id, 1, &options, error, (int)sizeof(error)) &&
              RSCache_CS2_TransformCore(&fs, error, (int)sizeof(error));
    struct RSCache_CS2_Function* function = ok ? RSCache_CS2_FunctionSetGet(&fs, id) : NULL;
    if( function )
    {
        struct RSCache_CS2_OptOptions opt;
        RSCache_CS2_OptDefaults(&opt, level);
        opt.callees = options;
        ok = RSCache_CS2_Optimize(&fs, function, &opt, stats, error, (int)sizeof(error));
    }
    else
    {
        ok = false;
    }
    if( ok )
    {
        struct RSCache_CS2_LowerOptions lower;
        memset(&lower, 0, sizeof(lower));
        lower.preserve_frame_counts = true;
        lower.signature = "";
        ok = RSCache_CS2_Lower(&fs, function, &lower, out, error, (int)sizeof(error));
    }
    if( !ok && error[0] )
        printf("   (pipeline: %s)\n", error);
    RSCache_CS2_FunctionSetFree(&fs);
    return ok;
}

/** How many times `opcode` appears in a script. */
static int
count_op(const struct RSCache_CS2_Script* script, int opcode)
{
    int seen = 0;
    for( int i = 0; i < script->op_count; i++ )
        seen += script->opcodes[i] == opcode;
    return seen;
}

/* ---- folding ------------------------------------------------------------- */

static void
test_folding(void)
{
    RSCACHE_TEST_GROUP("folding matches the VM's arithmetic");

    int out = 0;
    int add[2] = { 2, 3 };
    RSCACHE_CHECK(RSCache_CS2_EffectFoldInt(RSCACHE_CS2_OP_ADD, add, 2, &out) && out == 5);

    /* Wraps like the JVM's int, rather than being undefined like C's. */
    int overflow[2] = { 2147483647, 1 };
    RSCACHE_CHECK(RSCache_CS2_EffectFoldInt(RSCACHE_CS2_OP_ADD, overflow, 2, &out) &&
                  out == (-2147483647 - 1));

    /* CS2VM2_Op_Div stops the script on a zero divisor. Folding it to a number
     * would replace a halt with an answer, so it must decline. */
    int by_zero[2] = { 7, 0 };
    RSCACHE_CHECK(!RSCache_CS2_EffectFoldInt(RSCACHE_CS2_OP_DIV, by_zero, 2, &out));
    RSCACHE_CHECK(!RSCache_CS2_EffectFoldInt(RSCACHE_CS2_OP_MOD, by_zero, 2, &out));

    /* `scale` reads its arguments in an order its name does not suggest:
     * CS2VM2_Op_Scale pops c, b, a and answers c * a / b. */
    int scale[3] = { 10, 4, 2 };
    RSCACHE_CHECK(RSCache_CS2_EffectFoldInt(4018, scale, 3, &out) && out == 5);

    /* Nothing that reads state or a clock may be folded, whatever it is called. */
    RSCACHE_CHECK(!RSCache_CS2_EffectIsPure(4004)); /* random */
    RSCACHE_CHECK(!RSCache_CS2_EffectIsPure(4005)); /* randominc */
    RSCACHE_CHECK(!RSCache_CS2_EffectIsPure(3300)); /* clientclock */
    RSCACHE_CHECK(!RSCache_CS2_EffectIsPure(RSCACHE_CS2_OP_PUSH_VAR));
    RSCACHE_CHECK(RSCache_CS2_EffectIsPure(RSCACHE_CS2_OP_ADD));
}

/* ---- the counted loop ---------------------------------------------------- */

/*
 *   $i = 0;
 *   while ($i < 3) { $i = calc($i + 1); }
 *   return;
 *
 * in the shape Jagex's compiler emits, which is the shape the decompiler
 * recognises: the test at the top, the taken branch jumping over an
 * unconditional exit.
 */
static const struct op k_loop[] = {
    /*
     * Three dead stores stand in front of the loop on purpose.
     *
     * The bug this pins needed an earlier deletion in the same walk to perturb
     * the block bounds the liveness answers came from, so a loop on its own did
     * not reproduce it — the induction store was judged against a graph nothing
     * had invalidated yet. These give the pass something to remove first.
     */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 9, NULL },  /*  0  $d = 9 (dead) */
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 1, NULL },      /*  1 */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 8, NULL },  /*  2  $e = 8 (dead) */
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 2, NULL },      /*  3 */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 7, NULL },  /*  4  $f = 7 (dead) */
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 3, NULL },      /*  5 */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 0, NULL },  /*  6 */
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 0, NULL },      /*  7  $i = 0        */
    { RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0, NULL },     /*  8  Ltop:         */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 3, NULL },  /*  9 */
    { RSCACHE_CS2_OP_BRANCH_LESS_THAN, 1, NULL },   /* 10  -> body (12)  */
    { RSCACHE_CS2_OP_BRANCH, 7, NULL },             /* 11  -> end (19)   */
    { RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0, NULL },     /* 12  body          */
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 1, NULL },  /* 13 */
    { RSCACHE_CS2_OP_ADD, 0, NULL },                /* 14 */
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 0, NULL },      /* 15  $i = $i + 1   */
    { RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0, NULL },     /* 16 */
    { RSCACHE_CS2_OP_POP_VAR, 7, NULL },            /* 17  %varp7 = $i   */
    { RSCACHE_CS2_OP_BRANCH, -10, NULL },           /* 18  -> Ltop (8)   */
    { RSCACHE_CS2_OP_RETURN, 0, NULL },             /* 19  end           */
};

static void
test_loop_survives(void)
{
    RSCACHE_TEST_GROUP("a loop keeps its induction variable");

    for( int level = RSCACHE_CS2_OPT_LOCAL; level <= RSCACHE_CS2_OPT_FULL; level++ )
    {
        struct RSCache_CS2_Script input;
        build(&input, 100, 4, 0, k_loop, (int)(sizeof(k_loop) / sizeof(k_loop[0])));

        struct RSCache_CS2_Script out;
        struct RSCache_CS2_OptStats stats;
        memset(&out, 0, sizeof(out));
        bool ok = optimize_script(&input, level, &out, &stats);
        RSCACHE_CHECK(ok);
        if( ok )
        {
            /*
             * The store into the induction variable has to survive. It is only
             * ever read at the top of the *next* iteration, so any analysis
             * that loses the back edge calls it dead.
             */
            RSCACHE_CHECK(count_op(&out, RSCACHE_CS2_OP_POP_INT_LOCAL) >= 1);
            /* And the loop is still a loop: something still jumps backwards. */
            int back_edges = 0;
            for( int i = 0; i < out.op_count; i++ )
            {
                if( out.opcodes[i] != RSCACHE_CS2_OP_BRANCH )
                    continue;
                if( i + out.int_operands[i] + 1 <= i )
                    back_edges++;
            }
            RSCACHE_CHECK(back_edges >= 1);
            RSCache_CS2_ScriptFree(&out);
        }
        RSCache_CS2_ScriptFree(&input);
    }
}

/* ---- straight-line folding through the whole pipeline -------------------- */

/*
 *   $a = calc(2 + 3);
 *   %varp7 = $a;
 *   return;
 *
 * The add is constant, so it should fold to a single push and the local should
 * disappear — the store is genuinely dead once its only reader is a constant.
 */
static const struct op k_const[] = {
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 2, NULL },
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 3, NULL },
    { RSCACHE_CS2_OP_ADD, 0, NULL },
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 0, NULL },
    { RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0, NULL },
    { RSCACHE_CS2_OP_POP_VAR, 7, NULL },
    { RSCACHE_CS2_OP_RETURN, 0, NULL },
};

static void
test_constant_pipeline(void)
{
    RSCACHE_TEST_GROUP("a constant expression folds and its local dies");

    struct RSCache_CS2_Script input;
    build(&input, 101, 1, 0, k_const, (int)(sizeof(k_const) / sizeof(k_const[0])));

    struct RSCache_CS2_Script out;
    struct RSCache_CS2_OptStats stats;
    memset(&out, 0, sizeof(out));
    bool ok = optimize_script(&input, RSCACHE_CS2_OPT_LOCAL, &out, &stats);
    RSCACHE_CHECK(ok);
    if( ok )
    {
        RSCACHE_CHECK(count_op(&out, RSCACHE_CS2_OP_ADD) == 0);
        RSCACHE_CHECK(count_op(&out, RSCACHE_CS2_OP_POP_INT_LOCAL) == 0);
        /* The varp write is state and stays, whatever else goes. */
        RSCACHE_CHECK(count_op(&out, RSCACHE_CS2_OP_POP_VAR) == 1);
        RSCACHE_CHECK(out.op_count < input.op_count);
        RSCache_CS2_ScriptFree(&out);
    }
    RSCache_CS2_ScriptFree(&input);
}

/* ---- a store that is really dead ---------------------------------------- */

/*
 *   $a = 1;   <- overwritten before it is read
 *   $a = 2;
 *   %varp7 = $a;
 *
 * The pass has to remove the first store. A test that only proved nothing was
 * removed would pass with the pass disabled entirely.
 */
static const struct op k_dead[] = {
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 1, NULL },
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 0, NULL },
    { RSCACHE_CS2_OP_PUSH_CONSTANT_INT, 2, NULL },
    { RSCACHE_CS2_OP_POP_INT_LOCAL, 0, NULL },
    { RSCACHE_CS2_OP_PUSH_INT_LOCAL, 0, NULL },
    { RSCACHE_CS2_OP_POP_VAR, 7, NULL },
    { RSCACHE_CS2_OP_RETURN, 0, NULL },
};

static void
test_dead_store_is_removed(void)
{
    RSCACHE_TEST_GROUP("an overwritten store is removed");

    struct RSCache_CS2_Script input;
    build(&input, 102, 1, 0, k_dead, (int)(sizeof(k_dead) / sizeof(k_dead[0])));

    struct RSCache_CS2_Script out;
    struct RSCache_CS2_OptStats stats;
    memset(&out, 0, sizeof(out));
    bool ok = optimize_script(&input, RSCACHE_CS2_OPT_LOCAL, &out, &stats);
    RSCACHE_CHECK(ok);
    if( ok )
    {
        RSCACHE_CHECK(count_op(&out, RSCACHE_CS2_OP_POP_VAR) == 1);
        RSCACHE_CHECK(out.op_count <= 3);
        RSCache_CS2_ScriptFree(&out);
    }
    RSCache_CS2_ScriptFree(&input);
}

int
main(void)
{
    test_folding();
    test_loop_survives();
    test_constant_pipeline();
    test_dead_store_is_removed();
    return rscache_test_report("cs2-opt");
}
