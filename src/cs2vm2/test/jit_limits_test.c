/*
 * test-cs2-jit-limits: the edges where a fused group must NOT run as a
 * group (docs/CS2_JIT.md 2.3, section 6): the int stack filled to just
 * under, at and over what each shape needs; a full string stack for CSTR;
 * the cycle limit landing on every instruction inside a group; locals at
 * and around the declared count and CS2VM_MAX_LOCALS; a frame reused by a
 * script that reads locals its previous occupant wrote; the build refusals.
 * Every case runs both ways and must agree.
 */

#include "jit_test_util.h"

static struct jt_setup
lim_setup(
    int ints_prefill,
    int strs_prefill)
{
    struct jt_setup s;
    memset(&s, 0, sizeof(s));
    s.ints_prefill = ints_prefill;
    s.strs_prefill = strs_prefill;
    s.locals_count = 8;
    int const v[8] = { 7, -3, 7, 5, 0, 11, -1, 100 };
    memcpy(s.locals, v, sizeof(v));
    return s;
}

static int
ins_opcode(unsigned ins)
{
    static int const ops[] = {
        [CS2VM2_JIT_INS_L] = CS2_OP_PUSH_INT_LOCAL,
        [CS2VM2_JIT_INS_C] = CS2_OP_PUSH_CONSTANT_INT,
        [CS2VM2_JIT_INS_P] = CS2_OP_POP_INT_LOCAL,
        [CS2VM2_JIT_INS_ADD] = CS2_OP_ADD,
        [CS2VM2_JIT_INS_BR] = CS2_OP_BRANCH,
        [CS2VM2_JIT_INS_BREQ] = CS2_OP_BRANCH_EQUALS,
        [CS2VM2_JIT_INS_BRNE] = CS2_OP_BRANCH_NOT,
        [CS2VM2_JIT_INS_BRLT] = CS2_OP_BRANCH_LESS_THAN,
        [CS2VM2_JIT_INS_BRGT] = CS2_OP_BRANCH_GREATER_THAN,
    };
    assert(ins >= CS2VM2_JIT_INS_L && ins <= CS2VM2_JIT_INS_BRGT);
    return ops[ins];
}

enum
{
    LBL_LOOP = 1,
    LBL_END,
};

/*
 * Emit `sh` with the operands that make a branch true against lim_setup's
 * locals (loc0 = 7, loc1 = -3, loc2 = 7) or, for C x; BRcc, against a top
 * of `cbr_top`. Branches go to `label`.
 */
static void
emit_true_shape(
    struct jt_asm* a,
    struct CS2VM2_JitShape const* sh,
    int label,
    int cbr_top)
{
    unsigned const last = sh->ins[sh->k - 1];
    int l_ops[2] = { 0, 1 };
    int c_op = 5;
    if( sh->ins[0] == CS2VM2_JIT_INS_C && last >= CS2VM2_JIT_INS_BREQ )
    {
        /* v = cbr_top (on the stack) against x. */
        c_op = last == CS2VM2_JIT_INS_BREQ   ? cbr_top
               : last == CS2VM2_JIT_INS_BRNE ? cbr_top + 1
               : last == CS2VM2_JIT_INS_BRLT ? cbr_top + 1
                                             : cbr_top - 1;
    }
    else if( sh->ins[1] == CS2VM2_JIT_INS_C && last >= CS2VM2_JIT_INS_BREQ )
    {
        c_op = last == CS2VM2_JIT_INS_BREQ ? 7 : last == CS2VM2_JIT_INS_BRGT ? 6 : 8;
    }
    else if( sh->ins[1] == CS2VM2_JIT_INS_L && last >= CS2VM2_JIT_INS_BREQ )
    {
        if( last == CS2VM2_JIT_INS_BREQ )
            l_ops[1] = 2; /* 7 == 7 */
        else if( last == CS2VM2_JIT_INS_BRLT )
        {
            l_ops[0] = 1; /* -3 < 7 */
            l_ops[1] = 0;
        }
    }
    int nl = 0;
    for( int i = 0; i < sh->k; i++ )
    {
        unsigned const ins = sh->ins[i];
        if( ins >= CS2VM2_JIT_INS_BR )
            jt_br(a, ins_opcode(ins), label);
        else if( ins == CS2VM2_JIT_INS_L )
            jt_op(a, CS2_OP_PUSH_INT_LOCAL, l_ops[nl++ % 2]);
        else if( ins == CS2VM2_JIT_INS_C )
            jt_op(a, CS2_OP_PUSH_CONSTANT_INT, c_op);
        else if( ins == CS2VM2_JIT_INS_P )
            jt_op(a, CS2_OP_POP_INT_LOCAL, 5);
        else
            jt_op(a, CS2_OP_ADD, 0);
    }
}

/* ---- the stack edges --------------------------------------------------- */

static void
test_stack_edges(void)
{
    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = &g_cs2vm2_jit_shapes[i];
        int fills[5];
        int nf = 0;
        for( int d = -1; d <= 1; d++ )
        {
            int f = CS2VM_STACK_MAX - sh->peak + d;
            if( f > CS2VM_STACK_MAX )
                f = CS2VM_STACK_MAX;
            fills[nf++] = f;
        }
        if( sh->need > 0 )
            fills[nf++] = sh->need - 1;
        fills[nf++] = sh->need;
        for( int f = 0; f < nf; f++ )
        {
            struct jt_asm a;
            char label[96];
            jt_asm_init(&a);
            emit_true_shape(&a, sh, LBL_END, 1000 + fills[f] - 1);
            jt_label(&a, LBL_END);
            struct jt_setup const setup = lim_setup(fills[f], 0);
            snprintf(label, sizeof(label), "%s with %d ints on the stack (need %d, peak %d)", sh->name,
                fills[f], sh->need, sh->peak);
            int const code = jt_ab_asm(&a, 8, &setup, 0, label);
            jt_check(code == sh->code, "%s placed (got %d)", sh->name, code);
        }
    }

    /* The singletons at the edges. */
    int const edges[] = { 0, 1, CS2VM_STACK_MAX - 1, CS2VM_STACK_MAX };
    for( int e = 0; e < 4; e++ )
    {
        struct jt_asm a;
        char label[96];
        struct jt_setup const setup = lim_setup(edges[e], 0);
        jt_asm_init(&a);
        jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 2);
        jt_op(&a, CS2_OP_RETURN, 0);
        snprintf(label, sizeof(label), "LD with %d ints on the stack", edges[e]);
        jt_check(jt_ab_asm(&a, 8, &setup, 0, label) == CS2VM2_JIT_LD, "LD placed");
        jt_asm_init(&a);
        jt_op(&a, CS2_OP_POP_INT_LOCAL, 2);
        jt_op(&a, CS2_OP_RETURN, 0);
        snprintf(label, sizeof(label), "PD with %d ints on the stack", edges[e]);
        jt_check(jt_ab_asm(&a, 8, &setup, 0, label) == CS2VM2_JIT_PD, "PD placed");
    }

    /* CSTR against the string stack. */
    int const sedges[] = { 0, CS2VM_STACK_MAX - 1, CS2VM_STACK_MAX };
    for( int e = 0; e < 3; e++ )
    {
        struct jt_asm a;
        char label[96];
        struct jt_setup const setup = lim_setup(0, sedges[e]);
        jt_asm_init(&a);
        jt_str(&a, "edge");
        jt_str(&a, "edge2");
        snprintf(label, sizeof(label), "CSTR x2 with %d strings on the stack", sedges[e]);
        jt_check(jt_ab_asm(&a, 8, &setup, 0, label) == CS2VM2_JIT_CSTR, "CSTR placed");
    }
}

/* ---- the cycle limit --------------------------------------------------- */

/*
 * An endless, stack-neutral loop around `sh` (or LD/PD when sh is NULL and
 * `single` names the code), behind `prefix` one-instruction BRANCH +0s. Each
 * prefix length shifts where the 1,000,000th instruction falls by one, so
 * over prefix = 0 .. body length it falls on every instruction of the group.
 * Returns the body length.
 */
static int
emit_cycle_loop(
    struct jt_asm* a,
    struct CS2VM2_JitShape const* sh,
    int single,
    int prefix)
{
    jt_asm_init(a);
    for( int i = 0; i < prefix; i++ )
        jt_op(a, CS2_OP_BRANCH, 0);
    jt_label(a, LBL_LOOP);
    int const start = a->n;
    if( !sh )
    {
        if( single == CS2VM2_JIT_LD )
        {
            jt_op(a, CS2_OP_PUSH_INT_LOCAL, 3);
            jt_op(a, CS2_OP_POP_INT_DISCARD, 0);
        }
        else
        {
            jt_op(a, CS2_OP_PUSH_CONSTANT_INT, 4);
            jt_op(a, CS2_OP_ABS, 0);
            jt_op(a, CS2_OP_POP_INT_LOCAL, 4);
            jt_op(a, CS2_OP_PUSH_CONSTANT_INT, 0);
            jt_op(a, CS2_OP_POP_INT_DISCARD, 0);
        }
        jt_br(a, CS2_OP_BRANCH, LBL_LOOP);
        return a->n - start;
    }
    unsigned const last = sh->ins[sh->k - 1];
    bool const branches = last >= CS2VM2_JIT_INS_BR;
    /* What the group needs on the stack, pushed so nothing fuses with it. */
    int const need = sh->need;
    for( int i = 0; i < need; i++ )
    {
        jt_op(a, CS2_OP_PUSH_CONSTANT_INT, 5);
        jt_op(a, CS2_OP_ABS, 0);
    }
    emit_true_shape(a, sh, LBL_LOOP, 5);
    if( branches )
        return a->n - start; /* the group's own branch is the back edge */
    /* Pop what it left. */
    int left = 0;
    switch( sh->code )
    {
    case CS2VM2_JIT_LL:
    case CS2VM2_JIT_LC:
    case CS2VM2_JIT_CL:
    case CS2VM2_JIT_CC:
        left = 2;
        break;
    case CS2VM2_JIT_LLL:
        left = 3;
        break;
    case CS2VM2_JIT_LCA:
    case CS2VM2_JIT_CA:
        left = 1;
        break;
    default:
        left = 0;
        break;
    }
    for( int i = 0; i < left; i++ )
        jt_op(a, CS2_OP_POP_INT_DISCARD, 0);
    jt_br(a, CS2_OP_BRANCH, LBL_LOOP);
    return a->n - start;
}

static void
test_cycle_limit(void)
{
    int const saved_quiet = g_jt_quiet;
    for( int i = -2; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = i >= 0 ? &g_cs2vm2_jit_shapes[i] : NULL;
        int const single = i == -2 ? CS2VM2_JIT_LD : CS2VM2_JIT_PD;
        char const* name = sh ? sh->name : (single == CS2VM2_JIT_LD ? "LD" : "PD");
        struct jt_asm probe;
        int const body = emit_cycle_loop(&probe, sh, single, 0);
        int const fail0 = g_jt_fail;
        int errors = 0;
        g_jt_quiet = 1;
        for( int prefix = 0; prefix <= body; prefix++ )
        {
            struct jt_asm a;
            char label[96];
            emit_cycle_loop(&a, sh, single, prefix);
            struct jt_setup const setup = lim_setup(0, 0);
            snprintf(label, sizeof(label), "%s: cycle limit, prefix %d of body %d", name, prefix, body);
            jt_ab_asm(&a, 8, &setup, -1, label);
            if( g_jt_plain.rc == CS2VM_EXECNO_ERROR && g_jt_plain.last_error_opcode == -1 )
                errors++;
        }
        g_jt_quiet = saved_quiet;
        jt_check(g_jt_fail == fail0 && errors == body + 1,
            "%s: the cycle limit at every one of %d positions agrees both ways (%d hit the limit)", name, body + 1,
            errors);
    }
}

/* ---- locals ------------------------------------------------------------ */

static void
test_locals(void)
{
    struct jt_asm a;
    struct jt_setup setup = lim_setup(0, 0);

    /* local_int_count - 1 and local_int_count (not declared: plain). */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 5);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 8);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 7);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 8);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 7);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 8);
    {
        struct CS2VM2_Script s;
        jt_build(&a, &s, 51, 8);
        jt_ab(&s, &setup, "locals 7 (last declared) and 8 (undeclared)");
        jt_check(s.jit->code[0] == CS2VM2_HOT_PUSH_CONSTANT_INT, "C; P 8 is not CP (got %d)", s.jit->code[0]);
        jt_check(s.jit->code[1] == CS2VM2_HOT_POP_INT_LOCAL, "P 8 is plain (got %d)", s.jit->code[1]);
        jt_check(s.jit->code[2] == CS2VM2_JIT_LD, "L 7 before L 8 is LD, not LL (got %d)", s.jit->code[2]);
        jt_check(s.jit->code[3] == CS2VM2_HOT_PUSH_INT_LOCAL, "L 8 is plain (got %d)", s.jit->code[3]);
        jt_check(s.jit->code[4] == CS2VM2_JIT_AP, "ADD; P 7 is AP (got %d)", s.jit->code[4]);
        CS2VM2_ScriptFree(&s);
    }

    /* local_int_count == CS2VM_MAX_LOCALS: the top local is declared. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 9);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, CS2VM_MAX_LOCALS - 1);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, CS2VM_MAX_LOCALS - 1);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, CS2VM_MAX_LOCALS - 1);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, CS2VM_MAX_LOCALS - 1);
    {
        struct CS2VM2_Script s;
        jt_build(&a, &s, 52, CS2VM_MAX_LOCALS);
        jt_ab(&s, &setup, "local_int_count == CS2VM_MAX_LOCALS");
        jt_check(s.jit->code[0] == CS2VM2_JIT_CP, "CP into local 1023 (got %d)", s.jit->code[0]);
        jt_check(s.jit->code[2] == CS2VM2_JIT_LL, "LL of 1023, 0 (got %d)", s.jit->code[2]);
        CS2VM2_ScriptFree(&s);
    }

    /* local_int_count > CS2VM_MAX_LOCALS: nothing names a local. */
    {
        struct CS2VM2_Script s;
        jt_build(&a, &s, 53, CS2VM_MAX_LOCALS + 1);
        jt_ab(&s, &setup, "local_int_count == CS2VM_MAX_LOCALS + 1");
        struct CS2VM2_JitInfo info;
        CS2VM2_JitGetInfo(&s, &info);
        jt_check(info.declared_local_pcs == 0 && info.fused_group_pcs == 0,
            "no declared-local code above the maximum (%d LD/PD, %d groups)", info.declared_local_pcs,
            info.fused_group_pcs);
        jt_check(s.jit->code[2] == CS2VM2_HOT_PUSH_INT_LOCAL, "L 1023 plain (got %d)", s.jit->code[2]);
        CS2VM2_ScriptFree(&s);
    }

    /* No declared locals at all. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 1);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 3);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 2);
    {
        struct CS2VM2_Script s;
        setup.locals_count = 0;
        jt_build(&a, &s, 54, 0);
        jt_ab(&s, &setup, "local_int_count == 0");
        jt_check(s.jit->code[0] == CS2VM2_HOT_PUSH_INT_LOCAL && s.jit->code[2] == CS2VM2_HOT_PUSH_CONSTANT_INT,
            "plain codes only (got %d, %d)", s.jit->code[0], s.jit->code[2]);
        CS2VM2_ScriptFree(&s);
    }
}

/*
 * A frame block reused by a script that reads locals the previous occupant
 * wrote: zero, both ways, whether the block comes back through one thread's
 * stack or through the shared frame pool.
 */
static void
test_reused_frame(void)
{
    struct jt_asm a;
    struct CS2VM2_Script writer, reader;

    jt_asm_init(&a);
    for( int i = 0; i < 12; i++ )
    {
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 100 + i);
        jt_op(&a, CS2_OP_POP_INT_LOCAL, i);
    }
    jt_op(&a, CS2_OP_RETURN, 0);
    jt_build(&a, &writer, 61, 12);

    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 1);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 2);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 3);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 4);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 5);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 6);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 7);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 8);  /* above the reader's declared count */
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 11); /* well above it */
    jt_build(&a, &reader, 62, 8);

    for( int jit = 0; jit < 2; jit++ )
    {
        if( jit )
        {
            jt_check(CS2VM2_JitBuild(&writer) == CS2VM2_JIT_BUILT, "writer built");
            jt_check(CS2VM2_JitBuild(&reader) == CS2VM2_JIT_BUILT, "reader built");
        }
        /* One thread: the reader's frame is the writer's block. */
        CS2VM2_Init(&g_jt_vm);
        struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&g_jt_vm);
        CS2VM2_PushCallScript(t, &writer);
        int const rc_w = CS2VM2_RunScript(t);
        jt_check(rc_w == CS2VM_EXECNO_DONE && t->frame_sp == 0, "%s writer done", jit ? "jit" : "plain");
        CS2VM2_PushCallScript(t, &reader);
        int const rc_r = CS2VM2_RunScript(t);
        bool zeros = rc_r == CS2VM_EXECNO_DONE && t->ints_stack_top == 10;
        for( int i = 0; i < t->ints_stack_top; i++ )
            zeros = zeros && t->ints_stack[i] == 0;
        jt_check(zeros, "%s: the reader sees 0 in every local the writer set (top %d)", jit ? "jit" : "plain",
            t->ints_stack_top);
        CS2VM2_Free(&g_jt_vm);

        /* Across VMs: the block goes back to the shared pool and out again. */
        struct jt_obs w, r;
        jt_run(&writer, NULL, &w);
        jt_run(&reader, NULL, &r);
        zeros = r.rc == CS2VM_EXECNO_DONE && r.int_top == 10;
        for( int i = 0; i < r.int_top; i++ )
            zeros = zeros && r.ints[i] == 0;
        jt_check(zeros, "%s: a pooled frame block reads 0 too", jit ? "jit" : "plain");
    }
    CS2VM2_ScriptFree(&writer);
    CS2VM2_ScriptFree(&reader);
}

/* ---- refusals ---------------------------------------------------------- */

static void
test_refusals(void)
{
    struct jt_asm a;
    struct CS2VM2_Script s;

    jt_asm_init(&a);
    jt_build(&a, &s, 71, 0);
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_REFUSED_EMPTY && !s.jit, "an empty script is refused");
    CS2VM2_ScriptFree(&s);

    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_build(&a, &s, 72, 0);
    s.dialect = &CS2VM2_DIALECT_RS2_634;
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_REFUSED_DIALECT && !s.jit, "an rs2-dialect script is refused");
    CS2VM2_ScriptFree(&s);

    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_BRANCH, 1); /* to pc 3 of 2 */
    jt_build(&a, &s, 73, 0);
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_REFUSED_BRANCH_RANGE && !s.jit,
        "a branch past the end is refused");
    CS2VM2_ScriptFree(&s);

    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_BRANCH_LESS_THAN_OR_EQUALS, -4); /* to pc -1 */
    jt_build(&a, &s, 74, 0);
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_REFUSED_BRANCH_RANGE && !s.jit,
        "a branch before the start is refused");
    CS2VM2_ScriptFree(&s);

    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_BRANCH, 0); /* to pc 2 == op_count: the end */
    jt_build(&a, &s, 75, 0);
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_BUILT && s.jit, "a branch to op_count builds");
    CS2VM2_ScriptFree(&s);

    for( int bad = 0; bad < 2; bad++ )
    {
        jt_asm_init(&a);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
        jt_op(&a, CS2_OP_SWITCH, 0);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 2);
        jt_build(&a, &s, 76, 0);
        CS2VM2_ScriptAllocSwitches(&s, 1);
        CS2VM2_ScriptAllocSwitchCases(&s, 0, 2);
        s.switch_tables[0].cases[0].key = 1;
        s.switch_tables[0].cases[0].target_pc = 1; /* to pc 3 == op_count */
        s.switch_tables[0].cases[1].key = 2;
        s.switch_tables[0].cases[1].target_pc = bad ? 2 : 0; /* to pc 4, or pc 2 */
        enum CS2VM2_JitBuildResult const r = CS2VM2_JitBuild(&s);
        if( bad )
            jt_check(r == CS2VM2_JIT_REFUSED_BRANCH_RANGE && !s.jit, "a switch case past the end is refused");
        else
        {
            jt_check(r == CS2VM2_JIT_BUILT, "switch cases inside the script build");
            CS2VM2_JitFree(&s);
            jt_ab(&s, NULL, "switch taken to the end");
        }
        CS2VM2_ScriptFree(&s);
    }

    /* The jumps the hot loop does not run: the six long compares and
     * BRANCH_IF_ONE move the pc by their operand too (CS2VM2_JumpRelative),
     * so a target outside [0, op_count] refuses the build exactly as a hot
     * branch's does, before or after the script, and op_count builds. */
    {
        int const far_ops[] = {
            CS2_OP_LONG_BRANCH_NOT, CS2_OP_LONG_BRANCH_EQUALS, CS2_OP_LONG_BRANCH_LESS_THAN,
            CS2_OP_LONG_BRANCH_GREATER_THAN, CS2_OP_LONG_BRANCH_LESS_THAN_OR_EQUALS,
            CS2_OP_LONG_BRANCH_GREATER_THAN_OR_EQUALS, CS2_OP_BRANCH_IF_ONE,
        };
        /* Script: PUSH 1; <jump operand>; RETURN. The jump is at pc 1, so
         * target = 2 + operand: -3 -> -1, 2 -> 4, 1 -> 3 == op_count. */
        int const operands[] = { -3, -9, 2, 40, 1, -2 };
        int const legal[] = { 0, 0, 0, 0, 1, 1 };
        for( int o = 0; o < (int)(sizeof(far_ops) / sizeof(far_ops[0])); o++ )
        {
            for( int k = 0; k < (int)(sizeof(operands) / sizeof(operands[0])); k++ )
            {
                jt_asm_init(&a);
                jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
                jt_op(&a, far_ops[o], operands[k]);
                jt_op(&a, CS2_OP_RETURN, 0);
                jt_build(&a, &s, 78, 0);
                enum CS2VM2_JitBuildResult const r = CS2VM2_JitBuild(&s);
                if( legal[k] )
                    jt_check(r == CS2VM2_JIT_BUILT && s.jit, "opcode %d to pc %d builds", far_ops[o],
                        2 + operands[k]);
                else
                    jt_check(r == CS2VM2_JIT_REFUSED_BRANCH_RANGE && !s.jit,
                        "opcode %d to pc %d is refused", far_ops[o], 2 + operands[k]);
                if( s.jit && far_ops[o] == CS2_OP_BRANCH_IF_ONE )
                {
                    /* Taken (the 1 pushed above): to the end, or back to pc 0
                     * once, which pushes 1 and takes it again; the cycle
                     * limit ends that loop the same way on both paths. */
                    CS2VM2_JitFree(&s);
                    jt_ab(&s, NULL, "branch_if_one inside the script");
                }
                CS2VM2_ScriptFree(&s);
            }
        }
    }

    jt_check(!strcmp(CS2VM2_JitBuildResultName(CS2VM2_JIT_REFUSED_BRANCH_RANGE), "refused_branch_range"),
        "refusal names");
}

int
main(void)
{
    printf("test-cs2-jit-limits\n");
    test_stack_edges();
    test_cycle_limit();
    test_locals();
    test_reused_frame();
    test_refusals();
    return jt_finish("test-cs2-jit-limits");
}
