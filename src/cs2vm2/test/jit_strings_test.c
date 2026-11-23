/*
 * test-cs2-jit-strings: constant strings pushed by pointer (docs/CS2_JIT.md
 * 2.5). A constant goes through UPPERCASE and LOWERCASE in a loop and is
 * pushed again: the results are right, the script's string_operands are
 * byte-identical afterwards, and what PUSH_CONSTANT_STRING pushes IS the
 * script's pointer, on the jit's CSTR and on the general path alike. A NULL
 * operand pushes a fresh "". The debug canary in CS2VM2_JitFree fires when
 * something writes through a constant.
 */

#include "jit_test_util.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

enum
{
    LBL_LOOP = 1,
    LBL_END,
};

static char const* const kMixed = "MiXeD case 123 <b>!";

static void
loop_script(struct CS2VM2_Script* s)
{
    struct jt_asm a;
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 0);
    jt_label(&a, LBL_LOOP);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 3);
    jt_br(&a, CS2_OP_BRANCH_EQUALS, LBL_END);
    jt_str(&a, kMixed);
    jt_op(&a, CS2_OP_UPPERCASE, 0);
    jt_op(&a, CS2_OP_POP_STRING_LOCAL, 0);
    jt_str(&a, kMixed);
    jt_op(&a, CS2_OP_LOWERCASE, 0);
    jt_op(&a, CS2_OP_POP_STRING_LOCAL, 1);
    jt_op(&a, CS2_OP_PUSH_STRING_LOCAL, 0); /* the upper-cased copy, lowered again */
    jt_op(&a, CS2_OP_LOWERCASE, 0);
    jt_op(&a, CS2_OP_POP_STRING_LOCAL, 2);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 0);
    jt_br(&a, CS2_OP_BRANCH, LBL_LOOP);
    jt_label(&a, LBL_END);
    jt_str(&a, kMixed); /* the constant, once more, left on the stack */
    jt_build(&a, s, 801, 2);
}

static void
test_loop(void)
{
    struct CS2VM2_Script s;
    loop_script(&s);
    jt_ab(&s, NULL, "upper/lowercase loop");
    for( int pass = 0; pass < 2; pass++ )
    {
        struct jt_obs const* o = pass ? &g_jt_jitted : &g_jt_plain;
        char const* way = pass ? "jit" : "plain";
        jt_check(o->rc == CS2VM_EXECNO_DONE, "%s: done", way);
        jt_check(!strcmp(o->str_locals[0], "MIXED CASE 123 <B>!"), "%s: uppercase '%s'", way, o->str_locals[0]);
        jt_check(!strcmp(o->str_locals[1], "mixed case 123 <b>!"), "%s: lowercase '%s'", way, o->str_locals[1]);
        jt_check(!strcmp(o->str_locals[2], "mixed case 123 <b>!"), "%s: lowercase of the upper '%s'", way,
            o->str_locals[2]);
        jt_check(o->str_top == 1 && !strcmp(o->strs[0], kMixed), "%s: the constant pushed again is unchanged '%s'",
            way, o->str_top ? o->strs[0] : "");
    }
    bool same = true;
    for( int pc = 0; pc < s.op_count; pc++ )
        if( s.string_operands[pc] && strcmp(s.string_operands[pc], kMixed) )
            same = false;
    jt_check(same, "string_operands byte-identical after both runs");
    struct CS2VM2_JitInfo info;
    CS2VM2_JitGetInfo(&s, &info);
    jt_check(info.constant_string_pcs == 3, "three CSTR pcs (%d)", info.constant_string_pcs);
    CS2VM2_ScriptFree(&s); /* the canary checks the constants once more */
}

/* What PUSH_CONSTANT_STRING leaves on top, with and without an overlay, and
 * on the general path (a traced run takes every instruction there). */
static void
test_pointer(void)
{
    struct jt_asm a;
    struct CS2VM2_Script s;
    jt_asm_init(&a);
    jt_str(&a, "by pointer");
    jt_build(&a, &s, 802, 0);

    struct jt_obs o;
    jt_run(&s, NULL, &o);
    jt_check(o.str_top_ptr == s.string_operands[0], "general path pushes the script's own pointer");
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_BUILT && s.jit->code[0] == CS2VM2_JIT_CSTR, "CSTR built");
    jt_run(&s, NULL, &o);
    jt_check(o.str_top_ptr == s.string_operands[0], "CSTR pushes the script's own pointer");

    struct CS2VM2_TraceRecord records[8];
    CS2VM2_TraceCaptureBegin(records, 8);
    jt_run(&s, NULL, &o);
    (void)CS2VM2_TraceCaptureEnd();
    jt_check(o.str_top_ptr == s.string_operands[0] && !strcmp(o.strs[0], "by pointer"),
        "an observed (traced) run, which never enters the hot loop, pushes it too");
    CS2VM2_ScriptFree(&s);

    /* A NULL operand: a fresh, writable "" each time. */
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_STRING, 0);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_STRING, 0);
    jt_build(&a, &s, 803, 0);
    jt_check(CS2VM2_JitBuild(&s) == CS2VM2_JIT_BUILT && s.jit->code[0] == CS2VM2_HOT_NONE,
        "a NULL constant stays on the general path");
    CS2VM2_Init(&g_jt_vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&g_jt_vm);
    CS2VM2_PushCallScript(t, &s);
    int const rc = CS2VM2_RunScript(t);
    jt_check(rc == CS2VM_EXECNO_DONE && t->strs_stack_top == 2 && t->strs_stack[0] && t->strs_stack[1] &&
                 t->strs_stack[0][0] == '\0' && t->strs_stack[1][0] == '\0' &&
                 t->strs_stack[0] != t->strs_stack[1],
        "two NULL operands push two distinct \"\"");
    CS2VM2_Free(&g_jt_vm);
    CS2VM2_ScriptFree(&s);
}

/* The canary: a child writes through a constant of a script with an
 * overlay; freeing the overlay must abort. Only where asserts are live. */
static void
test_canary(void)
{
#ifdef NDEBUG
    printf("  skip: the canary is a debug-build assert and this build has NDEBUG\n");
#else
    fflush(stdout);
    pid_t const pid = fork();
    if( pid == 0 )
    {
        /* Quiet the child's assert message: it is the expected outcome. */
        (void)freopen("/dev/null", "w", stderr);
        struct jt_asm a;
        struct CS2VM2_Script s;
        jt_asm_init(&a);
        jt_str(&a, "canary");
        jt_build(&a, &s, 804, 0);
        if( CS2VM2_JitBuild(&s) != CS2VM2_JIT_BUILT )
            _exit(3);
        s.string_operands[0][0] = 'C'; /* what no host or opcode may do */
        CS2VM2_JitFree(&s);
        _exit(0); /* reached only if the canary did not fire */
    }
    int status = 0;
    waitpid(pid, &status, 0);
    jt_check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
        "CS2VM2_JitFree aborts when a constant was written through (status %d)", status);

    /* And it stays quiet when nothing was. */
    struct jt_asm a;
    struct CS2VM2_Script s;
    jt_asm_init(&a);
    jt_str(&a, "canary");
    jt_build(&a, &s, 805, 0);
    CS2VM2_JitBuild(&s);
    CS2VM2_JitFree(&s);
    jt_check(s.jit == NULL, "an untouched script frees quietly");
    CS2VM2_JitFree(&s); /* a deallocator: no overlay is a no-op */
    CS2VM2_ScriptFree(&s);
#endif
}

int
main(void)
{
    printf("test-cs2-jit-strings\n");
    test_loop();
    test_pointer();
    test_canary();
    return jt_finish("test-cs2-jit-strings");
}
