#ifndef CS2VM2_JIT_TEST_UTIL_H
#define CS2VM2_JIT_TEST_UTIL_H

/*
 * Shared by the test-cs2-jit-* tests (docs/CS2_JIT.md section 6).
 *
 * A tiny assembler with labels, and the A/B the whole suite rests on: run a
 * script in a fresh VM WITHOUT an overlay, run it again in a fresh VM WITH
 * one, and compare everything a run can show -- return code, the int stack
 * (depth and every live value), the string stack (depth and every string's
 * bytes), the entry frame's int and string locals, last_error_opcode / pc /
 * script_id, frame depth and thread->cycles_run. The plain run is the
 * reference: an overlay may only make the run faster.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_jit.h"
#include "cs2vm2/cs2vm2_jit_internal.h"
#include "cs2vm2/cs2vm2_script.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_jt_fail = 0;
static int g_jt_ok = 0;
static int g_jt_quiet = 0;

static inline void
jt_check(
    bool cond,
    char const* fmt,
    ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if( cond )
    {
        g_jt_ok++;
        if( !g_jt_quiet )
            printf("  ok: %s\n", msg);
    }
    else
    {
        g_jt_fail++;
        printf("  FAIL: %s\n", msg);
    }
}

static inline int
jt_finish(char const* name)
{
    printf("%s: %d ok, %d failed\n", name, g_jt_ok, g_jt_fail);
    return g_jt_fail ? 1 : 0;
}

/* ---- assembler ------------------------------------------------------- */

#define JT_MAX_OPS 512
#define JT_MAX_LABELS 32
#define JT_MAX_FIX 64

struct jt_asm
{
    uint16_t op[JT_MAX_OPS];
    int arg[JT_MAX_OPS];
    char const* str[JT_MAX_OPS];
    int n;
    int label_pc[JT_MAX_LABELS];
    int fix_pc[JT_MAX_FIX];
    int fix_label[JT_MAX_FIX];
    int nfix;
};

static inline void
jt_asm_init(struct jt_asm* a)
{
    memset(a, 0, sizeof(*a));
    for( int i = 0; i < JT_MAX_LABELS; i++ )
        a->label_pc[i] = -1;
}

static inline int
jt_op(
    struct jt_asm* a,
    int op,
    int arg)
{
    assert(a->n < JT_MAX_OPS);
    a->op[a->n] = (uint16_t)op;
    a->arg[a->n] = arg;
    a->str[a->n] = NULL;
    return a->n++;
}

static inline int
jt_str(
    struct jt_asm* a,
    char const* s)
{
    int const pc = jt_op(a, CS2_OP_PUSH_CONSTANT_STRING, 0);
    a->str[pc] = s;
    return pc;
}

static inline void
jt_label(
    struct jt_asm* a,
    int label)
{
    assert(label >= 0 && label < JT_MAX_LABELS);
    a->label_pc[label] = a->n;
}

/* A branch (any of the branch opcodes) to `label`, resolved at jt_build. */
static inline int
jt_br(
    struct jt_asm* a,
    int op,
    int label)
{
    int const pc = jt_op(a, op, 0);
    assert(a->nfix < JT_MAX_FIX);
    a->fix_pc[a->nfix] = pc;
    a->fix_label[a->nfix] = label;
    a->nfix++;
    return pc;
}

/* Into `s` (its arrays from ToriMem_Script, so CS2VM2_ScriptFree frees them). */
static inline void
jt_build(
    struct jt_asm* a,
    struct CS2VM2_Script* s,
    int script_id,
    int local_int_count)
{
    for( int i = 0; i < a->nfix; i++ )
    {
        int const target = a->label_pc[a->fix_label[i]];
        assert(target >= 0);
        a->arg[a->fix_pc[i]] = target - (a->fix_pc[i] + 1);
    }
    CS2VM2_ScriptInit(s);
    s->script_id = script_id;
    s->local_int_count = local_int_count;
    s->local_string_count = 4;
    s->op_count = a->n;
    size_t const n = a->n > 0 ? (size_t)a->n : 1u;
    s->opcodes = ToriMem_Calloc(ToriMem_Script(), n, sizeof(uint16_t));
    s->int_operands = ToriMem_Calloc(ToriMem_Script(), n, sizeof(int));
    s->string_operands = ToriMem_Calloc(ToriMem_Script(), n, sizeof(char*));
    assert(s->opcodes);
    assert(s->int_operands);
    assert(s->string_operands);
    for( int i = 0; i < a->n; i++ )
    {
        s->opcodes[i] = a->op[i];
        s->int_operands[i] = a->arg[i];
        if( a->str[i] )
        {
            s->string_operands[i] = ToriMem_Strdup(ToriMem_Script(), a->str[i]);
            assert(s->string_operands[i]);
        }
    }
}

/* ---- the A/B ----------------------------------------------------------- */

/* What a run starts with, beside the script. */
struct jt_setup
{
    int ints_prefill;  /* ints pushed before the frame (values 1000 + i) */
    int strs_prefill;  /* strings pushed before the frame ("s<i>") */
    int locals_count;  /* int locals set through CS2VM2_SetIntCurrentFrameLocal */
    int locals[16];
};

#define JT_OBS_STRS 64
#define JT_OBS_STR_BYTES 64
#define JT_OBS_LOCALS 1100

struct jt_obs
{
    int rc;
    int frame_sp;
    int int_top;
    int ints[CS2VM_STACK_MAX];
    int str_top;
    char strs[JT_OBS_STRS][JT_OBS_STR_BYTES];
    bool str_null[JT_OBS_STRS];
    int local_count;
    int locals[JT_OBS_LOCALS];
    char str_locals[4][JT_OBS_STR_BYTES];
    int last_error_opcode;
    int last_error_pc;
    int last_error_script_id;
    uint64_t cycles;
    /* The pointer at the top of the string stack, for identity checks. */
    struct CS2VM2_Str const* str_top_ptr;
};

static struct CS2VM2 g_jt_vm;

static inline void
jt_copy_str(
    char* dst,
    char const* src)
{
    snprintf(dst, JT_OBS_STR_BYTES, "%s", src ? src : "(null)");
}

/* Run `s` once in a fresh VM and record what it shows. The overlay is the
 * caller's: whatever s->jit is, is what runs. */
static inline void
jt_run(
    struct CS2VM2_Script* s,
    struct jt_setup const* setup,
    struct jt_obs* out)
{
    static struct jt_setup const none;
    if( !setup )
        setup = &none;
    memset(out, 0, sizeof(*out));
    CS2VM2_Init(&g_jt_vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&g_jt_vm);
    for( int i = 0; i < setup->ints_prefill; i++ )
        t->ints_stack[t->ints_stack_top++] = 1000 + i;
    for( int i = 0; i < setup->strs_prefill; i++ )
        t->strs_stack[t->strs_stack_top++] = CS2VM2_StrFmt(t, "s%d", i);
    int const pushed = CS2VM2_PushCallScript(t, s);
    assert(pushed == CS2VM_EXECNO_OK);
    for( int i = 0; i < setup->locals_count; i++ )
        CS2VM2_SetIntCurrentFrameLocal(t, i, setup->locals[i]);

    uint64_t const c0 = t->cycles_run;
    out->rc = CS2VM2_RunScript(t);
    out->cycles = t->cycles_run - c0;
    out->frame_sp = t->frame_sp;
    out->int_top = t->ints_stack_top;
    memcpy(out->ints, t->ints_stack, sizeof(int) * (size_t)t->ints_stack_top);
    out->str_top = t->strs_stack_top;
    for( int i = 0; i < t->strs_stack_top && i < JT_OBS_STRS; i++ )
    {
        out->str_null[i] = t->strs_stack[i] == NULL;
        jt_copy_str(out->strs[i], CS2VM2_StrCStr(t->strs_stack[i]));
    }
    out->str_top_ptr = t->strs_stack_top > 0 ? t->strs_stack[t->strs_stack_top - 1] : NULL;
    struct CS2VM2_Frame const* f = t->frames[0];
    int nl = s->local_int_count;
    if( nl < 0 )
        nl = 0;
    if( nl > JT_OBS_LOCALS )
        nl = JT_OBS_LOCALS;
    out->local_count = nl;
    for( int i = 0; i < nl; i++ )
        out->locals[i] = i < f->int_locals_dirty ? f->int_locals[i] : 0;
    for( int i = 0; i < 4; i++ )
        jt_copy_str(out->str_locals[i], i < f->str_locals_dirty ? CS2VM2_StrCStr(f->str_locals[i]) : NULL);
    out->last_error_opcode = t->last_error_opcode;
    out->last_error_pc = t->last_error_pc;
    out->last_error_script_id = t->last_error_script_id;
    CS2VM2_Free(&g_jt_vm);
}

/* The first difference between two observations, or NULL. */
static inline char const*
jt_obs_diff(
    struct jt_obs const* a,
    struct jt_obs const* b,
    char* buf,
    size_t cap)
{
    if( a->rc != b->rc )
        return snprintf(buf, cap, "rc %d vs %d", a->rc, b->rc), buf;
    if( a->cycles != b->cycles )
        return snprintf(buf, cap, "cycles %llu vs %llu", (unsigned long long)a->cycles,
                   (unsigned long long)b->cycles), buf;
    if( a->frame_sp != b->frame_sp )
        return snprintf(buf, cap, "frame_sp %d vs %d", a->frame_sp, b->frame_sp), buf;
    if( a->int_top != b->int_top )
        return snprintf(buf, cap, "int top %d vs %d", a->int_top, b->int_top), buf;
    for( int i = 0; i < a->int_top; i++ )
        if( a->ints[i] != b->ints[i] )
            return snprintf(buf, cap, "int[%d] %d vs %d", i, a->ints[i], b->ints[i]), buf;
    if( a->str_top != b->str_top )
        return snprintf(buf, cap, "str top %d vs %d", a->str_top, b->str_top), buf;
    for( int i = 0; i < a->str_top && i < JT_OBS_STRS; i++ )
        if( a->str_null[i] != b->str_null[i] || strcmp(a->strs[i], b->strs[i]) )
            return snprintf(buf, cap, "str[%d] '%s' vs '%s'", i, a->strs[i], b->strs[i]), buf;
    for( int i = 0; i < a->local_count; i++ )
        if( a->locals[i] != b->locals[i] )
            return snprintf(buf, cap, "local[%d] %d vs %d", i, a->locals[i], b->locals[i]), buf;
    for( int i = 0; i < 4; i++ )
        if( strcmp(a->str_locals[i], b->str_locals[i]) )
            return snprintf(buf, cap, "str local[%d] '%s' vs '%s'", i, a->str_locals[i], b->str_locals[i]),
                   buf;
    if( a->last_error_opcode != b->last_error_opcode || a->last_error_pc != b->last_error_pc ||
        a->last_error_script_id != b->last_error_script_id )
        return snprintf(buf, cap, "last_error %d/%d/%d vs %d/%d/%d", a->last_error_opcode,
                   a->last_error_pc, a->last_error_script_id, b->last_error_opcode, b->last_error_pc,
                   b->last_error_script_id), buf;
    return NULL;
}

static struct jt_obs g_jt_plain;
static struct jt_obs g_jt_jitted;

/*
 * The A/B: `s` (no overlay on entry) runs plain, then with a fresh overlay
 * from CS2VM2_JitBuild, which must build. The overlay is left attached so the
 * caller can inspect it; it is freed with the script. Returns whether the
 * two runs agree, and checks it under `label`.
 */
static inline bool
jt_ab(
    struct CS2VM2_Script* s,
    struct jt_setup const* setup,
    char const* label)
{
    assert(!s->jit);
    jt_run(s, setup, &g_jt_plain);
    enum CS2VM2_JitBuildResult const built = CS2VM2_JitBuild(s);
    if( built != CS2VM2_JIT_BUILT )
    {
        jt_check(false, "%s: build %s", label, CS2VM2_JitBuildResultName(built));
        return false;
    }
    jt_run(s, setup, &g_jt_jitted);
    char buf[256];
    char const* diff = jt_obs_diff(&g_jt_plain, &g_jt_jitted, buf, sizeof(buf));
    jt_check(diff == NULL, "%s: jitted == plain (rc %d, cycles %llu, top %d)%s%s", label, g_jt_plain.rc,
        (unsigned long long)g_jt_plain.cycles, g_jt_plain.int_top, diff ? ": " : "", diff ? diff : "");
    return diff == NULL;
}

/* jt_ab on a script freshly built from `a`, then freed. Returns the overlay
 * code at `pc_probe` (or -1 when pc_probe < 0). */
static inline int
jt_ab_asm(
    struct jt_asm* a,
    int local_int_count,
    struct jt_setup const* setup,
    int pc_probe,
    char const* label)
{
    struct CS2VM2_Script s;
    jt_build(a, &s, 4242, local_int_count);
    jt_ab(&s, setup, label);
    int code = -1;
    if( s.jit && pc_probe >= 0 && pc_probe < s.op_count )
        code = s.jit->code[pc_probe];
    CS2VM2_ScriptFree(&s);
    return code;
}

#endif /* CS2VM2_JIT_TEST_UTIL_H */
