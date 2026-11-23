/*
 * test-cs2-jit-shapes: every fused group, LD, PD and CSTR run against its
 * unfused run (docs/CS2_JIT.md section 6), branches taken and not taken with
 * offsets of both signs, a branch landing on every inner pc of each group of
 * three and four, a builder census over random scripts, and random scripts
 * run both ways.
 */

#include "jit_test_util.h"

#include <limits.h>

/* Entry locals every case starts with. loc[3] is INT_MAX so an ADD on it
 * wraps the way cs2_math_add says. */
static struct jt_setup
shapes_setup(int ints_prefill)
{
    struct jt_setup s;
    memset(&s, 0, sizeof(s));
    s.ints_prefill = ints_prefill;
    s.locals_count = 8;
    int const v[8] = { 7, -3, 7, INT_MAX, 0, 11, -1, 100 };
    memcpy(s.locals, v, sizeof(v));
    return s;
}

static int
ins_opcode(unsigned ins)
{
    switch( ins )
    {
    case CS2VM2_JIT_INS_L:
        return CS2_OP_PUSH_INT_LOCAL;
    case CS2VM2_JIT_INS_C:
        return CS2_OP_PUSH_CONSTANT_INT;
    case CS2VM2_JIT_INS_P:
        return CS2_OP_POP_INT_LOCAL;
    case CS2VM2_JIT_INS_ADD:
        return CS2_OP_ADD;
    case CS2VM2_JIT_INS_BR:
        return CS2_OP_BRANCH;
    case CS2VM2_JIT_INS_BREQ:
        return CS2_OP_BRANCH_EQUALS;
    case CS2VM2_JIT_INS_BRNE:
        return CS2_OP_BRANCH_NOT;
    case CS2VM2_JIT_INS_BRLT:
        return CS2_OP_BRANCH_LESS_THAN;
    case CS2VM2_JIT_INS_BRGT:
        return CS2_OP_BRANCH_GREATER_THAN;
    default:
        assert(0 && "ins");
        return -1;
    }
}

static bool
ins_is_branch(unsigned ins)
{
    return ins >= CS2VM2_JIT_INS_BR;
}

static bool
shape_branches(struct CS2VM2_JitShape const* sh)
{
    return ins_is_branch(sh->ins[sh->k - 1]);
}

/*
 * Emit `sh` with operands[i] for instruction i (a local index for L/P, a
 * value for C; ignored for ADD and branches, which go to `label`). When
 * `inner` >= 0, `inner_label` is placed at that instruction of the group.
 */
static void
emit_shape(
    struct jt_asm* a,
    struct CS2VM2_JitShape const* sh,
    int const* operands,
    int label,
    int inner,
    int inner_label)
{
    for( int i = 0; i < sh->k; i++ )
    {
        if( i == inner )
            jt_label(a, inner_label);
        unsigned const ins = sh->ins[i];
        if( ins_is_branch(ins) )
            jt_br(a, ins_opcode(ins), label);
        else
            jt_op(a, ins_opcode(ins), operands[i]);
    }
}

/* Operands for a shape: Ls read locals 0, 1, 2 in turn, Cs push `c0`, `c1`,
 * Ps store local 5. */
static void
shape_operands(
    struct CS2VM2_JitShape const* sh,
    int l0,
    int l1,
    int c0,
    int c1,
    int* out)
{
    int nl = 0, nc = 0;
    int const ls[3] = { l0, l1, 2 };
    int const cs[2] = { c0, c1 };
    for( int i = 0; i < sh->k; i++ )
    {
        out[i] = 0;
        if( sh->ins[i] == CS2VM2_JIT_INS_L )
            out[i] = ls[nl++ % 3];
        else if( sh->ins[i] == CS2VM2_JIT_INS_C )
            out[i] = cs[nc++ % 2];
        else if( sh->ins[i] == CS2VM2_JIT_INS_P )
            out[i] = 5;
    }
}

enum
{
    LBL_T = 1,
    LBL_END,
    LBL_S,
    LBL_INNER,
};

/* Straight-line: the shape at pc 0, then the end of the script (or a
 * RETURN). */
static void
test_straight(
    struct CS2VM2_JitShape const* sh,
    int l0,
    int l1,
    int c0,
    int c1,
    bool trailing_return)
{
    struct jt_asm a;
    int ops[4];
    char label[128];
    jt_asm_init(&a);
    shape_operands(sh, l0, l1, c0, c1, ops);
    emit_shape(&a, sh, ops, LBL_END, -1, 0);
    if( trailing_return )
        jt_op(&a, CS2_OP_RETURN, 0);
    jt_label(&a, LBL_END);
    struct jt_setup const setup = shapes_setup(sh->need + 1);
    snprintf(label, sizeof(label), "%s straight l%d/l%d c%d/c%d%s", sh->name, l0, l1, c0, c1,
        trailing_return ? " +RETURN" : "");
    int const code = jt_ab_asm(&a, 8, &setup, 0, label);
    jt_check(code == sh->code, "%s placed at pc 0 (got %d)", sh->name, code);
}

/*
 * A branching shape both ways. `negative`: the target lies before the
 * group, reached by a backward branch; else after it. Taken lands on a push
 * of 222 (forward) or 333 (backward); not taken falls through to a push of
 * 111.
 */
static void
test_branch(
    struct CS2VM2_JitShape const* sh,
    int l0,
    int l1,
    int c0,
    bool negative,
    int ints_prefill)
{
    struct jt_asm a;
    int ops[4];
    char label[160];
    jt_asm_init(&a);
    shape_operands(sh, l0, l1, c0, c0, ops);
    int start = 0;
    if( negative )
    {
        jt_br(&a, CS2_OP_BRANCH, LBL_S);
        jt_label(&a, LBL_T);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 333);
        jt_br(&a, CS2_OP_BRANCH, LBL_END);
        jt_label(&a, LBL_S);
        start = a.n;
        emit_shape(&a, sh, ops, LBL_T, -1, 0);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 111);
    }
    else
    {
        emit_shape(&a, sh, ops, LBL_T, -1, 0);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 111);
        jt_br(&a, CS2_OP_BRANCH, LBL_END);
        jt_label(&a, LBL_T);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 222);
    }
    jt_label(&a, LBL_END);
    struct jt_setup const setup = shapes_setup(ints_prefill);
    snprintf(label, sizeof(label), "%s %s l%d/l%d c%d prefill %d", sh->name, negative ? "backward" : "forward",
        l0, l1, c0, ints_prefill);
    int const code = jt_ab_asm(&a, 8, &setup, start, label);
    jt_check(code == sh->code, "%s placed at pc %d (got %d)", sh->name, start, code);
    /* Which way it went, so both are known to be covered. */
    int const last = g_jt_plain.int_top > 0 ? g_jt_plain.ints[g_jt_plain.int_top - 1] : -1;
    if( !g_jt_quiet )
        printf("    (%s)\n", last == 111 ? "not taken" : "taken");
}

static int g_taken_seen[CS2VM2_JIT_SHAPE_COUNT][2];

static void
note_direction(struct CS2VM2_JitShape const* sh)
{
    int const last = g_jt_plain.int_top > 0 ? g_jt_plain.ints[g_jt_plain.int_top - 1] : -1;
    g_taken_seen[sh->code - CS2VM2_JIT_FUSED_FIRST][last == 111 ? 0 : 1] = 1;
}

/* A branch lands on instruction `inner` of the group (1 .. k-1). The group
 * is reached by no other way. */
static void
test_inner(
    struct CS2VM2_JitShape const* sh,
    int inner)
{
    struct jt_asm a;
    int ops[4];
    char label[128];
    jt_asm_init(&a);
    shape_operands(sh, 0, 1, 7, 7, ops);
    jt_br(&a, CS2_OP_BRANCH, LBL_INNER);
    emit_shape(&a, sh, ops, LBL_END, inner, LBL_INNER);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 111);
    jt_label(&a, LBL_END);
    struct jt_setup const setup = shapes_setup(4);
    snprintf(label, sizeof(label), "%s entered at inner pc %d", sh->name, inner);
    int const code = jt_ab_asm(&a, 8, &setup, 1, label);
    jt_check(code == sh->code, "%s still placed at its first pc (got %d)", sh->name, code);
}

static void
test_singletons(void)
{
    struct jt_asm a;
    struct jt_setup setup = shapes_setup(1);

    /* LD: a declared-local push not followed by anything it fuses with. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 3);
    jt_op(&a, CS2_OP_RETURN, 0);
    jt_check(jt_ab_asm(&a, 8, &setup, 0, "LD of local 3") == CS2VM2_JIT_LD, "LD placed");
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 7);
    jt_check(jt_ab_asm(&a, 8, &setup, 0, "LD of local 7 (last declared) at the end") == CS2VM2_JIT_LD,
        "LD placed on the last declared local");

    /* PD. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 4);
    jt_op(&a, CS2_OP_RETURN, 0);
    jt_check(jt_ab_asm(&a, 8, &setup, 0, "PD into local 4") == CS2VM2_JIT_PD, "PD placed");
    setup.ints_prefill = 0;
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 4);
    jt_check(jt_ab_asm(&a, 8, &setup, 0, "PD on an empty stack (general path's error)") == CS2VM2_JIT_PD,
        "PD placed on an empty-stack script");

    /* CSTR, and a NULL constant that gets no CSTR. */
    jt_asm_init(&a);
    jt_str(&a, "constant");
    jt_str(&a, "second");
    jt_op(&a, CS2_OP_RETURN, 0);
    jt_check(jt_ab_asm(&a, 8, NULL, 0, "CSTR x2") == CS2VM2_JIT_CSTR, "CSTR placed");
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_STRING, 0); /* no string operand */
    jt_check(jt_ab_asm(&a, 8, NULL, 0, "PUSH_CONSTANT_STRING with a NULL operand") == CS2VM2_HOT_NONE,
        "NULL constant left on the general path");

    /* Plain codes are exactly the old table. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 5);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 0);
    jt_op(&a, CS2_OP_DIVIDE, 0); /* divide by zero: the plain case stops, general path errors */
    setup.ints_prefill = 0;
    jt_check(jt_ab_asm(&a, 8, &setup, 2, "divide by zero") == CS2VM2_HOT_DIVIDE, "plain DIVIDE code");
}

/* ---- the builder census ------------------------------------------------ */

static uint32_t g_rng = 0x12345678u;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rnd_range(
    int lo,
    int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static int const g_rand_ops[] = {
    CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_INT_LOCAL, CS2_OP_POP_INT_LOCAL, CS2_OP_ADD,
    CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_INT_LOCAL, CS2_OP_POP_INT_LOCAL, CS2_OP_ADD,
    CS2_OP_BRANCH, CS2_OP_BRANCH_EQUALS, CS2_OP_BRANCH_NOT, CS2_OP_BRANCH_LESS_THAN,
    CS2_OP_BRANCH_GREATER_THAN, CS2_OP_BRANCH_LESS_THAN_OR_EQUALS, CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS,
    CS2_OP_SUB, CS2_OP_MULTIPLY, CS2_OP_PUSH_CONSTANT_STRING, CS2_OP_POP_INT_DISCARD,
    CS2_OP_POP_STRING_DISCARD,
};
#define RAND_OP_COUNT ((int)(sizeof(g_rand_ops) / sizeof(g_rand_ops[0])))

/*
 * A random script of `n` instructions over `g_rand_ops`. `runnable`: every
 * local index in [0, CS2VM_MAX_LOCALS) and every branch inside the script,
 * so it may be run (a local outside that range is a debug assert on the
 * general path). Otherwise anything goes, for the census.
 */
static void
random_script(
    struct jt_asm* a,
    int n,
    int nl,
    bool runnable)
{
    /* A runnable script with a backward branch often loops to the cycle
     * limit (a million instructions a run): one in four may have them. */
    bool const backward = (rnd() % 4u) == 0;
    jt_asm_init(a);
    for( int pc = 0; pc < n; pc++ )
    {
        int const op = g_rand_ops[rnd() % RAND_OP_COUNT];
        int arg = 0;
        if( op == CS2_OP_PUSH_INT_LOCAL || op == CS2_OP_POP_INT_LOCAL )
        {
            arg = runnable ? rnd_range(0, nl + 2) : rnd_range(-2, nl + 2);
            if( runnable && arg >= CS2VM_MAX_LOCALS )
                arg = CS2VM_MAX_LOCALS - 1;
        }
        else if( op == CS2_OP_PUSH_CONSTANT_INT )
            arg = rnd_range(-3, 3);
        else if( (op >= CS2_OP_BRANCH && op <= CS2_OP_BRANCH_GREATER_THAN) ||
                 op == CS2_OP_BRANCH_LESS_THAN_OR_EQUALS || op == CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS )
            arg = !runnable ? ((rnd() % 16u) == 0 ? rnd_range(-n - 3, n + 3) : rnd_range(-(pc + 1), n - pc - 1))
                  : backward ? rnd_range(-(pc + 1), n - pc - 1)
                             : rnd_range(0, n - pc - 1);
        int const at = jt_op(a, op, arg);
        if( op == CS2_OP_PUSH_CONSTANT_STRING && (rnd() & 1u) )
            a->str[at] = "rs";
    }
}

static bool
named_local_ok(
    struct CS2VM2_Script const* s,
    int pc)
{
    int const v = s->int_operands[pc];
    return s->local_int_count <= CS2VM_MAX_LOCALS && v >= 0 && v < s->local_int_count;
}

static bool
script_jumps_ok(struct CS2VM2_Script const* s)
{
    for( int pc = 0; pc < s->op_count; pc++ )
    {
        int const op = s->opcodes[pc];
        bool const br = op == CS2_OP_BRANCH || op == CS2_OP_BRANCH_EQUALS || op == CS2_OP_BRANCH_NOT ||
                        op == CS2_OP_BRANCH_LESS_THAN || op == CS2_OP_BRANCH_GREATER_THAN ||
                        op == CS2_OP_BRANCH_LESS_THAN_OR_EQUALS || op == CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS ||
                        (op >= CS2_OP_LONG_BRANCH_NOT && op <= CS2_OP_LONG_BRANCH_GREATER_THAN_OR_EQUALS) ||
                        op == CS2_OP_BRANCH_IF_ONE;
        if( br )
        {
            long const t = (long)pc + 1 + s->int_operands[pc];
            if( t < 0 || t > s->op_count )
                return false;
        }
    }
    return true;
}

static void
test_census(void)
{
    int scripts = 0, refused = 0, bad = 0;
    long fused = 0, singles = 0, cstr = 0;
    int const nls[] = { 0, 1, 3, 8, CS2VM_MAX_LOCALS, CS2VM_MAX_LOCALS + 1, 5000 };
    for( int iter = 0; iter < 3000; iter++ )
    {
        struct jt_asm a;
        struct CS2VM2_Script s;
        int const nl = nls[iter % (int)(sizeof(nls) / sizeof(nls[0]))];
        random_script(&a, rnd_range(1, 40), nl, false);
        jt_build(&a, &s, 77, nl);
        enum CS2VM2_JitBuildResult const r = CS2VM2_JitBuild(&s);
        scripts++;
        if( r != CS2VM2_JIT_BUILT )
        {
            refused++;
            if( r != CS2VM2_JIT_REFUSED_BRANCH_RANGE || script_jumps_ok(&s) )
                bad++;
            CS2VM2_ScriptFree(&s);
            continue;
        }
        if( !script_jumps_ok(&s) )
            bad++;
        for( int pc = 0; pc < s.op_count; pc++ )
        {
            unsigned const code = s.jit->code[pc];
            struct CS2VM2_JitShape const* sh = cs2vm2_jit_shape(code);
            if( sh )
            {
                fused++;
                if( pc + sh->k > s.op_count )
                    bad++;
                for( int i = 0; i < sh->k && pc + i < s.op_count; i++ )
                {
                    unsigned const ins = sh->ins[i];
                    if( (ins == CS2VM2_JIT_INS_L || ins == CS2VM2_JIT_INS_P) && !named_local_ok(&s, pc + i) )
                        bad++;
                    if( pc + i < s.op_count && s.opcodes[pc + i] != ins_opcode(ins) )
                        bad++;
                }
            }
            else if( code == CS2VM2_JIT_LD || code == CS2VM2_JIT_PD )
            {
                singles++;
                if( !named_local_ok(&s, pc) ||
                    s.opcodes[pc] != (code == CS2VM2_JIT_LD ? CS2_OP_PUSH_INT_LOCAL : CS2_OP_POP_INT_LOCAL) )
                    bad++;
            }
            else if( code == CS2VM2_JIT_CSTR )
            {
                cstr++;
                if( s.opcodes[pc] != CS2_OP_PUSH_CONSTANT_STRING || !s.string_operands[pc] )
                    bad++;
            }
            else if( code > CS2VM2_HOT_PLAIN_LAST || (code != 0 && code != g_cs2vm2_hot_op[s.opcodes[pc]]) )
                bad++;
            else if( code == 0 && g_cs2vm2_hot_op[s.opcodes[pc]] != 0 )
                bad++; /* the builder leaves no plain instruction on the general path */
        }
        if( nl > CS2VM_MAX_LOCALS )
        {
            struct CS2VM2_JitInfo info;
            CS2VM2_JitGetInfo(&s, &info);
            if( info.declared_local_pcs != 0 )
                bad++;
        }
        CS2VM2_ScriptFree(&s);
    }
    jt_check(bad == 0, "census: %d scripts (%d refused), %ld fused groups, %ld LD/PD, %ld CSTR, %d violations",
        scripts, refused, fused, singles, cstr, bad);
}

/* Random runnable scripts, both ways. Some loop to the cycle limit, some
 * underflow the stack: every outcome must match. */
static void
test_random_runs(void)
{
    int const saved_quiet = g_jt_quiet;
    int const fail0 = g_jt_fail;
    int runs = 0;
    g_jt_quiet = 1;
    for( int iter = 0; iter < 400; iter++ )
    {
        struct jt_asm a;
        int const nl = (int)(rnd() % 10u);
        random_script(&a, rnd_range(1, 30), nl, true);
        struct jt_setup setup = shapes_setup((int)(rnd() % 4u));
        setup.locals_count = nl < 8 ? nl : 8;
        char label[64];
        snprintf(label, sizeof(label), "random script %d", iter);
        jt_ab_asm(&a, nl, &setup, -1, label);
        runs++;
    }
    g_jt_quiet = saved_quiet;
    jt_check(g_jt_fail == fail0, "random runs: %d scripts agree both ways", runs);
}

int
main(void)
{
    printf("test-cs2-jit-shapes\n");
    jt_check(CS2VM2_JIT_SHAPE_COUNT == CS2VM2_JIT_FUSED_LAST - CS2VM2_JIT_FUSED_FIRST + 1, "24 codes, 24 shapes");
    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
        jt_check(g_cs2vm2_jit_shapes[i].code == CS2VM2_JIT_FUSED_FIRST + i, "shape %s at its code",
            g_cs2vm2_jit_shapes[i].name);

    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = &g_cs2vm2_jit_shapes[i];
        if( !shape_branches(sh) )
        {
            test_straight(sh, 0, 1, 5, -9, false);
            test_straight(sh, 0, 1, 5, -9, true);
            test_straight(sh, 3, 3, 1, INT_MAX, false); /* the wrapping add */
            test_straight(sh, 6, 2, INT_MIN, -1, false);
            continue;
        }
        for( int neg = 0; neg < 2; neg++ )
        {
            if( sh->ins[0] == CS2VM2_JIT_INS_C )
            {
                /* C x; BRcc: the stack top is 1000 (one prefilled int). */
                for( int x = 999; x <= 1001; x++ )
                {
                    test_branch(sh, 0, 1, x, neg, 1);
                    note_direction(sh);
                }
            }
            else if( sh->ins[1] == CS2VM2_JIT_INS_C )
            {
                for( int x = 6; x <= 8; x++ )
                {
                    test_branch(sh, 0, 1, x, neg, 0);
                    note_direction(sh);
                }
            }
            else if( sh->ins[1] == CS2VM2_JIT_INS_L )
            {
                int const pairs[3][2] = { { 0, 1 }, { 0, 2 }, { 1, 0 } };
                for( int p = 0; p < 3; p++ )
                {
                    test_branch(sh, pairs[p][0], pairs[p][1], 0, neg, 0);
                    note_direction(sh);
                }
            }
            else
            {
                test_branch(sh, 0, 1, 0, neg, 1); /* PBR: P b; BR */
                note_direction(sh);
            }
        }
    }
    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = &g_cs2vm2_jit_shapes[i];
        if( !shape_branches(sh) )
            continue;
        bool const conditional = sh->ins[sh->k - 1] != CS2VM2_JIT_INS_BR;
        jt_check(g_taken_seen[i][1] && (!conditional || g_taken_seen[i][0]), "%s: %s", sh->name,
            conditional ? "taken and not taken both run" : "taken run");
    }

    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = &g_cs2vm2_jit_shapes[i];
        if( sh->k < 3 )
            continue;
        for( int inner = 1; inner < sh->k; inner++ )
            test_inner(sh, inner);
    }

    test_singletons();
    test_census();
    test_random_runs();
    return jt_finish("test-cs2-jit-shapes");
}
