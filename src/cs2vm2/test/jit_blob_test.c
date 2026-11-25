/*
 * test-cs2-jit-blob: the format (docs/CS2_JIT.md "Hash", "Fingerprint",
 * "Blob", "Validation"). Golden values pin the hash and the fingerprint --
 * they were computed by an independent reference implementation of the doc's
 * definitions, and are never edited without a CS2VM2_JIT_FORMAT_VERSION bump.
 * Then: the fingerprint's sensitivity, serialize/attach round trips, every
 * rejection produced on purpose, and a fuzz in which every blob the
 * validator accepts must run exactly like the plain script.
 */

#include "jit_test_util.h"

#include <limits.h>

/* CS2VM2_JitHash64(bytes (i * 37 + 11) & 0xFF for i < len, len, this seed). */
#define GOLDEN_SEED 0x0123456789ABCDEFull
static uint64_t const g_golden_hash[18] = {
    0x87CBFBFE89022CEAULL, /* 0 */
    0x395FA7DCEC6F8BE3ULL, /* 1 */
    0xCA7CB2D97D0DC698ULL, /* 2 */
    0xCBB1166CC093C3FAULL, /* 3 */
    0xF18B2F35BA6EF2AFULL, /* 4 */
    0xBFEC91B89C11502FULL, /* 5 */
    0x6A9F2D92C21E78E5ULL, /* 6 */
    0x0A8F5558C1320D2EULL, /* 7 */
    0xA01FFA4486FCAF5DULL, /* 8 */
    0xF87C6B17E509CF47ULL, /* 9 */
    0xC15BE586DE958DE6ULL, /* 10 */
    0xE03DC53255C0D88DULL, /* 11 */
    0x301D305E8B1129E3ULL, /* 12 */
    0x75BA22C0297079E6ULL, /* 13 */
    0x7343E1F306317FBBULL, /* 14 */
    0x41816052810488A3ULL, /* 15 */
    0xE3F8B2D323B4371EULL, /* 16 */
    0x96306C7D215449FCULL, /* 17 */
};
/* The fingerprint's stream, written out here from docs/CS2_JIT.md
 * "Fingerprint" and hashed with CS2VM2_JitHash64 (whose golden values are
 * above), so the fingerprint is pinned without pinning the canonical
 * numbering: a script's opcodes are canonical instructions, and
 * regenerating cs2_opcode.h moves their values (the blob format guards that
 * through CS2_OP_NUMBERING_HASH in the dialect tag). */
struct ref_stream
{
    uint8_t b[4096];
    size_t n;
};
static void
ref_u8(struct ref_stream* r, uint32_t v)
{
    assert(r->n < sizeof(r->b));
    r->b[r->n++] = (uint8_t)v;
}
static void
ref_u16(struct ref_stream* r, uint32_t v)
{
    ref_u8(r, v & 0xFFu);
    ref_u8(r, (v >> 8) & 0xFFu);
}
static void
ref_u32(struct ref_stream* r, uint32_t v)
{
    ref_u16(r, v & 0xFFFFu);
    ref_u16(r, v >> 16);
}
static void
ref_str(struct ref_stream* r, char const* text)
{
    if( !text )
    {
        ref_u32(r, 0);
        return;
    }
    size_t const len = strlen(text);
    ref_u32(r, (uint32_t)len + 1u);
    for( size_t i = 0; i < len; i++ )
        ref_u8(r, (uint8_t)text[i]);
}
static uint64_t
ref_fingerprint(struct CS2VM2_Script const* s)
{
    static struct ref_stream r;
    r.n = 0;
    ref_u32(&r, (uint32_t)s->op_count);
    ref_u32(&r, (uint32_t)s->local_int_count);
    ref_u32(&r, (uint32_t)s->local_string_count);
    ref_u32(&r, (uint32_t)s->local_long_count);
    ref_u32(&r, (uint32_t)s->int_argument_count);
    ref_u32(&r, (uint32_t)s->string_argument_count);
    ref_u32(&r, (uint32_t)s->long_argument_count);
    ref_u32(&r, (uint32_t)s->int_stack_depth);
    ref_u32(&r, (uint32_t)s->str_stack_depth);
    ref_str(&r, s->signature);
    for( int pc = 0; pc < s->op_count; pc++ )
        ref_u16(&r, s->opcodes[pc]);
    for( int pc = 0; pc < s->op_count; pc++ )
        ref_u32(&r, (uint32_t)s->int_operands[pc]);
    assert(!s->long_operands);
    ref_u8(&r, 0);
    ref_u8(&r, s->string_operands ? 1u : 0u);
    if( s->string_operands )
        for( int pc = 0; pc < s->op_count; pc++ )
            ref_str(&r, s->string_operands[pc]);
    ref_u32(&r, (uint32_t)s->switch_table_count);
    for( int t = 0; t < s->switch_table_count; t++ )
    {
        ref_u32(&r, (uint32_t)s->switch_tables[t].case_count);
        for( int c = 0; c < s->switch_tables[t].case_count; c++ )
        {
            ref_u32(&r, (uint32_t)s->switch_tables[t].cases[c].key);
            ref_u32(&r, (uint32_t)s->switch_tables[t].cases[c].target_pc);
        }
    }
    return CS2VM2_JitHash64(r.b, r.n, CS2VM2_JIT_FINGERPRINT_SEED);
}

/* The fingerprint's fixed script: five instructions, one switch table. */
static void
golden_script(struct CS2VM2_Script* s)
{
    struct jt_asm a;
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 5);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 1);
    jt_str(&a, "hi");
    jt_build(&a, s, 900, 3);
    s->local_string_count = 1;
    s->local_long_count = 0;
    s->int_argument_count = 1;
    s->string_argument_count = 0;
    s->long_argument_count = 0;
    s->int_stack_depth = 4;
    s->str_stack_depth = 1;
    s->signature = ToriMem_Strdup(ToriMem_Script(), "i");
    CS2VM2_ScriptAllocSwitches(s, 1);
    CS2VM2_ScriptAllocSwitchCases(s, 0, 2);
    s->switch_tables[0].cases[0].key = 1;
    s->switch_tables[0].cases[0].target_pc = 2;
    s->switch_tables[0].cases[1].key = 7;
    s->switch_tables[0].cases[1].target_pc = -3;
}

/* A script with loops and most kinds of code in it, for round trips. */
enum
{
    LBL_LOOP = 1,
    LBL_END,
};

static void
work_script(struct CS2VM2_Script* s)
{
    struct jt_asm a;
    jt_asm_init(&a);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 0);
    jt_label(&a, LBL_LOOP);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 50);
    jt_br(&a, CS2_OP_BRANCH_GREATER_THAN, LBL_END);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 3);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 1);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
    jt_op(&a, CS2_OP_ADD, 0);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 1);
    jt_str(&a, "tick");
    jt_op(&a, CS2_OP_POP_STRING_DISCARD, 0);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 2);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_br(&a, CS2_OP_BRANCH_EQUALS, LBL_LOOP);
    jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
    jt_op(&a, CS2_OP_POP_INT_LOCAL, 2);
    jt_br(&a, CS2_OP_BRANCH, LBL_LOOP);
    jt_label(&a, LBL_END);
    jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 1);
    jt_str(&a, "done");
    jt_build(&a, s, 901, 4);
}

static void
put32(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void
put64(
    uint8_t* p,
    uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* The doc's checksum, recomputed with the one-shot hash over header[0, 40)
 * then the code (so it also checks the streaming hasher against it). */
static uint64_t
blob_checksum(
    uint8_t const* blob,
    size_t size)
{
    static uint8_t tmp[65536];
    assert(size >= 48 && size - 8 <= sizeof(tmp));
    memcpy(tmp, blob, 40);
    memcpy(tmp + 40, blob + 48, size - 48);
    return CS2VM2_JitHash64(tmp, size - 8, CS2VM2_JIT_BLOB_MAGIC);
}

static void
reseal(
    uint8_t* blob,
    size_t size)
{
    put64(blob + 40, blob_checksum(blob, size));
}

/* A copy of `src` without an overlay. */
static void
copy_plain(
    struct CS2VM2_Script const* src,
    struct CS2VM2_Script* dst)
{
    bool const ok = CS2VM2_ScriptCopy(src, dst);
    assert(ok);
    CS2VM2_JitFree(dst);
}

static uint8_t*
make_blob(
    struct CS2VM2_Script* s,
    size_t* size)
{
    assert(!s->jit);
    jt_check(CS2VM2_JitBuild(s) == CS2VM2_JIT_BUILT, "script %d builds", s->script_id);
    *size = CS2VM2_JitBlobSize(s);
    uint8_t* blob = malloc(*size);
    assert(blob);
    CS2VM2_JitSerialize(s, CS2VM2_JitFingerprint(s), blob, *size);
    return blob;
}

static enum CS2VM2_JitAttachResult
attach_to_copy(
    struct CS2VM2_Script const* src,
    uint8_t const* blob,
    size_t size)
{
    struct CS2VM2_Script c;
    copy_plain(src, &c);
    enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&c, CS2VM2_JitFingerprint(&c), blob, size);
    if( r != CS2VM2_JIT_ATTACHED )
        assert(!c.jit);
    CS2VM2_ScriptFree(&c);
    return r;
}

/* ---- golden values ------------------------------------------------------ */

static void
test_golden(void)
{
    uint8_t data[18];
    for( int i = 0; i < 18; i++ )
        data[i] = (uint8_t)((i * 37 + 11) & 0xFF);
    for( int len = 0; len < 18; len++ )
    {
        uint64_t const h = CS2VM2_JitHash64(len ? data : NULL, (size_t)len, GOLDEN_SEED);
        jt_check(h == g_golden_hash[len], "hash of %d bytes = %016llx (golden %016llx)", len,
            (unsigned long long)h, (unsigned long long)g_golden_hash[len]);
    }
    struct CS2VM2_Script s;
    golden_script(&s);
    uint64_t const fp = CS2VM2_JitFingerprint(&s);
    uint64_t const want = ref_fingerprint(&s);
    jt_check(fp == want, "golden fingerprint %016llx (the documented stream %016llx)", (unsigned long long)fp,
        (unsigned long long)want);
    CS2VM2_ScriptFree(&s);
    jt_check(CS2VM2_JitDialectTag(NULL) == CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS),
        "NULL dialect tags as OSRS (%08x)", CS2VM2_JitDialectTag(NULL));
    jt_check(CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS) != CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS_230) &&
                 CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS_230) != CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS_229) &&
                 CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS) != CS2VM2_JitDialectTag(&CS2VM2_DIALECT_RS2_634),
        "the dialects tag apart");
}

/* ---- fingerprint sensitivity ------------------------------------------ */

static void
test_fingerprint(void)
{
    struct CS2VM2_Script base;
    golden_script(&base);
    uint64_t const fp0 = CS2VM2_JitFingerprint(&base);

#define FP_MUTATE(what, stmt)                                                                      \
    do                                                                                             \
    {                                                                                              \
        struct CS2VM2_Script m;                                                                    \
        copy_plain(&base, &m);                                                                     \
        stmt;                                                                                      \
        jt_check(CS2VM2_JitFingerprint(&m) != fp0, "fingerprint changes with %s", what);           \
        CS2VM2_ScriptFree(&m);                                                                     \
    } while( 0 )

    FP_MUTATE("one opcode", m.opcodes[2] = CS2_OP_SUB);
    FP_MUTATE("one int operand", m.int_operands[0] = 6);
    FP_MUTATE("a string byte", m.string_operands[4][1] = 'o');
    FP_MUTATE("a string's presence", m.string_operands[0] = ToriMem_Strdup(ToriMem_Script(), ""));
    FP_MUTATE("a string's absence", (ToriMem_Free(ToriMem_Script(), m.string_operands[4]), m.string_operands[4] = NULL));
    FP_MUTATE("a switch key", m.switch_tables[0].cases[1].key = 8);
    FP_MUTATE("a switch target", m.switch_tables[0].cases[1].target_pc = -2);
    FP_MUTATE("local_int_count", m.local_int_count++);
    FP_MUTATE("local_string_count", m.local_string_count++);
    FP_MUTATE("local_long_count", m.local_long_count++);
    FP_MUTATE("int_argument_count", m.int_argument_count++);
    FP_MUTATE("string_argument_count", m.string_argument_count++);
    FP_MUTATE("long_argument_count", m.long_argument_count++);
    FP_MUTATE("int_stack_depth", m.int_stack_depth++);
    FP_MUTATE("str_stack_depth", m.str_stack_depth++);
    FP_MUTATE("the signature", m.signature[0] = 's');
    FP_MUTATE("no signature", (ToriMem_Free(ToriMem_Script(), m.signature), m.signature = NULL));
    FP_MUTATE("long operands' presence",
        m.long_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)m.op_count, sizeof(int64_t)));
#undef FP_MUTATE

    /* One long operand, against a script that has them. */
    {
        struct CS2VM2_Script l1, l2;
        copy_plain(&base, &l1);
        l1.long_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)l1.op_count, sizeof(int64_t));
        copy_plain(&l1, &l2);
        l2.long_operands[3] = INT64_MIN;
        jt_check(CS2VM2_JitFingerprint(&l1) != CS2VM2_JitFingerprint(&l2), "fingerprint changes with one long");
        CS2VM2_ScriptFree(&l1);
        CS2VM2_ScriptFree(&l2);
    }
    /* The switch table count. */
    {
        struct CS2VM2_Script m;
        golden_script(&m);
        m.switch_tables[0].case_count = 1;
        jt_check(CS2VM2_JitFingerprint(&m) != fp0, "fingerprint changes with a case count");
        m.switch_tables[0].case_count = 2;
        CS2VM2_ScriptFree(&m);
    }
    /* Not changed by what is not content. */
    {
        struct CS2VM2_Script m;
        copy_plain(&base, &m);
        m.script_id = 12345;
        m.dialect = &CS2VM2_DIALECT_OSRS_229;
        jt_check(CS2VM2_JitFingerprint(&m) == fp0, "script_id and dialect do not change the fingerprint");
        jt_check(CS2VM2_JitBuild(&m) == CS2VM2_JIT_BUILT, "golden script builds");
        jt_check(CS2VM2_JitFingerprint(&m) == fp0, "an attached overlay does not change it either");
        CS2VM2_ScriptFree(&m);
    }
    CS2VM2_ScriptFree(&base);
}

/* ---- round trips -------------------------------------------------------- */

static void
test_round_trip(void)
{
    struct CS2VM2_Script s;
    work_script(&s);
    size_t size;
    struct jt_obs plain;
    jt_run(&s, NULL, &plain);
    uint8_t* blob = make_blob(&s, &size);
    jt_check(size == 48u + (size_t)s.op_count, "blob size %zu = 48 + %d", size, s.op_count);

    /* Deterministic. */
    uint8_t* again = malloc(size);
    CS2VM2_JitSerialize(&s, CS2VM2_JitFingerprint(&s), again, size);
    jt_check(!memcmp(blob, again, size), "serialize is deterministic");
    free(again);
    uint64_t sum = (uint64_t)blob[40] | ((uint64_t)blob[41] << 8) | ((uint64_t)blob[42] << 16) |
                   ((uint64_t)blob[43] << 24) | ((uint64_t)blob[44] << 32) | ((uint64_t)blob[45] << 40) |
                   ((uint64_t)blob[46] << 48) | ((uint64_t)blob[47] << 56);
    jt_check(sum == blob_checksum(blob, size), "the streamed checksum equals the one-shot hash");

    struct CS2VM2_Script c;
    copy_plain(&s, &c);
    enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&c, CS2VM2_JitFingerprint(&c), blob, size);
    jt_check(r == CS2VM2_JIT_ATTACHED, "attach to a fresh copy: %s", CS2VM2_JitAttachResultName(r));
    jt_check(c.jit && !memcmp(c.jit->code, s.jit->code, (size_t)s.op_count), "identical code");
    struct jt_obs attached, built;
    jt_run(&c, NULL, &attached);
    jt_run(&s, NULL, &built);
    char buf[256];
    char const* d1 = jt_obs_diff(&plain, &attached, buf, sizeof(buf));
    jt_check(!d1, "attached run == plain run%s%s", d1 ? ": " : "", d1 ? d1 : "");
    char const* d2 = jt_obs_diff(&plain, &built, buf, sizeof(buf));
    jt_check(!d2, "built run == plain run%s%s", d2 ? ": " : "", d2 ? d2 : "");

    /* CS2VM2_ScriptCopy carries the overlay. */
    struct CS2VM2_Script cc;
    CS2VM2_ScriptCopy(&c, &cc);
    jt_check(cc.jit && cc.jit != c.jit && !memcmp(cc.jit->code, c.jit->code, (size_t)c.op_count),
        "ScriptCopy copies the overlay");
    struct CS2VM2_JitInfo i1, i2;
    CS2VM2_JitGetInfo(&c, &i1);
    CS2VM2_JitGetInfo(&cc, &i2);
    jt_check(!memcmp(&i1, &i2, sizeof(i1)) && i1.fused_group_pcs > 0 && i1.constant_string_pcs == 2 &&
                 i1.blob_size == size,
        "info: %d general, %d plain, %d LD/PD, %d CSTR, %d groups over %d ops", i1.general_pcs, i1.plain_pcs,
        i1.declared_local_pcs, i1.constant_string_pcs, i1.fused_group_pcs, i1.fused_group_ops);
    CS2VM2_ScriptFree(&cc);
    CS2VM2_ScriptFree(&c);
    CS2VM2_ScriptFree(&s);
    free(blob);
}

/* ---- every rejection ---------------------------------------------------- */

static void
test_rejections(void)
{
    struct CS2VM2_Script s;
    work_script(&s);
    size_t size;
    uint8_t* blob = make_blob(&s, &size);
    uint8_t* b = malloc(size + 1);
    int const n = s.op_count;

    bool all_trunc = true;
    for( size_t len = 0; len < size; len++ )
        all_trunc = all_trunc && attach_to_copy(&s, blob, len) == CS2VM2_JIT_REJECT_TRUNCATED;
    jt_check(all_trunc, "every prefix 0..%zu is REJECT_TRUNCATED", size - 1);

#define REJECT(what, want, mutate)                                                                 \
    do                                                                                             \
    {                                                                                              \
        size_t bsize = size;                                                                       \
        memcpy(b, blob, size);                                                                     \
        mutate;                                                                                    \
        enum CS2VM2_JitAttachResult const got = attach_to_copy(&s, b, bsize);                      \
        jt_check(got == (want), "%s: %s (want %s)", what, CS2VM2_JitAttachResultName(got),         \
            CS2VM2_JitAttachResultName(want));                                                     \
    } while( 0 )

    REJECT("bad magic", CS2VM2_JIT_REJECT_MAGIC, b[0] ^= 1);
    REJECT("version + 1", CS2VM2_JIT_REJECT_VERSION, b[4] = (uint8_t)(CS2VM2_JIT_FORMAT_VERSION + 1));
    REJECT("another dialect's tag", CS2VM2_JIT_REJECT_DIALECT,
        put32(b + 8, CS2VM2_JitDialectTag(&CS2VM2_DIALECT_OSRS_229)));
    REJECT("another script id", CS2VM2_JIT_REJECT_SCRIPT_ID, put32(b + 12, 902));
    REJECT("another fingerprint", CS2VM2_JIT_REJECT_STALE, b[16] ^= 0x80);
    REJECT("another op_count", CS2VM2_JIT_REJECT_STALE, put32(b + 24, (uint32_t)n + 1));
    REJECT("another local_int_count", CS2VM2_JIT_REJECT_STALE, put32(b + 28, 3));
    REJECT("header size 47", CS2VM2_JIT_REJECT_SHAPE, b[6] = 47);
    REJECT("code length != op_count", CS2VM2_JIT_REJECT_SHAPE, put32(b + 32, (uint32_t)n - 1));
    REJECT("flags 1", CS2VM2_JIT_REJECT_SHAPE, put32(b + 36, 1));
    REJECT("one trailing byte", CS2VM2_JIT_REJECT_SHAPE, (b[size] = 0, bsize = size + 1));
    REJECT("a flipped code byte", CS2VM2_JIT_REJECT_CHECKSUM, b[48 + 3] ^= 0x01);
    REJECT("a flipped checksum byte", CS2VM2_JIT_REJECT_CHECKSUM, b[47] ^= 0x10);
    REJECT("an illegal code byte, resealed", CS2VM2_JIT_REJECT_ILLEGAL_CODE, (b[48 + 3] = 200, reseal(b, size)));
    REJECT("a fused code at the wrong pc, resealed", CS2VM2_JIT_REJECT_ILLEGAL_CODE,
        (b[48 + 0] = CS2VM2_JIT_LLL, reseal(b, size)));
    REJECT("a group over instructions it does not name, resealed", CS2VM2_JIT_REJECT_ILLEGAL_CODE,
        (b[48 + n - 2] = CS2VM2_JIT_LC, reseal(b, size)));
    REJECT("CSTR on an int push, resealed", CS2VM2_JIT_REJECT_ILLEGAL_CODE,
        (b[48 + 0] = CS2VM2_JIT_CSTR, reseal(b, size)));
    REJECT("a plain code for another opcode, resealed", CS2VM2_JIT_REJECT_ILLEGAL_CODE,
        (b[48 + 0] = CS2VM2_HOT_SUB, reseal(b, size)));
    REJECT("a general-path code (legal), resealed", CS2VM2_JIT_ATTACHED, (b[48 + 2] = 0, reseal(b, size)));
#undef REJECT

    /* A local above the declared count, in a blob made for a script where
     * it was declared: same fingerprint is impossible, so craft the header. */
    {
        struct CS2VM2_Script m;
        copy_plain(&s, &m);
        m.local_int_count = 1; /* local 1 is no longer declared */
        memcpy(b, blob, size);
        put64(b + 16, CS2VM2_JitFingerprint(&m));
        put32(b + 28, 1);
        reseal(b, size);
        enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&m, CS2VM2_JitFingerprint(&m), b, size);
        jt_check(r == CS2VM2_JIT_REJECT_ILLEGAL_CODE, "a group naming an undeclared local: %s",
            CS2VM2_JitAttachResultName(r));
        CS2VM2_ScriptFree(&m);
    }
    /* An rs2 script, the header made to match it: its instructions are
     * canonical like any other dialect's once loaded, so it attaches. */
    {
        struct CS2VM2_Script m;
        copy_plain(&s, &m);
        m.dialect = &CS2VM2_DIALECT_RS2_634;
        memcpy(b, blob, size);
        put32(b + 8, CS2VM2_JitDialectTag(m.dialect));
        reseal(b, size);
        enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&m, CS2VM2_JitFingerprint(&m), b, size);
        jt_check(r == CS2VM2_JIT_ATTACHED, "an rs2 script: %s", CS2VM2_JitAttachResultName(r));
        CS2VM2_ScriptFree(&m);
    }
    /* A script with a branch out of range. */
    {
        struct CS2VM2_Script m;
        copy_plain(&s, &m);
        m.int_operands[n - 3] = 1000; /* the back-edge BRANCH */
        memcpy(b, blob, size);
        put64(b + 16, CS2VM2_JitFingerprint(&m));
        reseal(b, size);
        enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&m, CS2VM2_JitFingerprint(&m), b, size);
        jt_check(r == CS2VM2_JIT_REJECT_UNJITTABLE, "a branch out of range: %s", CS2VM2_JitAttachResultName(r));
        CS2VM2_ScriptFree(&m);
    }
    /* A blob for another script entirely, attached by mistake. */
    {
        struct CS2VM2_Script g;
        golden_script(&g);
        enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&g, CS2VM2_JitFingerprint(&g), blob, size);
        jt_check(r == CS2VM2_JIT_REJECT_SCRIPT_ID, "another script's blob: %s", CS2VM2_JitAttachResultName(r));
        CS2VM2_ScriptFree(&g);
    }
    /* A group whose instructions match but would run past op_count. */
    {
        struct jt_asm a;
        struct CS2VM2_Script t;
        jt_asm_init(&a);
        jt_op(&a, CS2_OP_PUSH_CONSTANT_INT, 1);
        jt_op(&a, CS2_OP_PUSH_INT_LOCAL, 0);
        jt_build(&a, &t, 905, 1);
        size_t tsize;
        uint8_t* tb = make_blob(&t, &tsize);
        jt_check(t.jit->code[1] == CS2VM2_JIT_LD, "the last L is LD (got %d)", t.jit->code[1]);
        tb[48 + 1] = CS2VM2_JIT_LL;
        reseal(tb, tsize);
        jt_check(attach_to_copy(&t, tb, tsize) == CS2VM2_JIT_REJECT_ILLEGAL_CODE, "LL on the last pc is illegal");
        tb[48 + 0] = CS2VM2_JIT_CL;
        tb[48 + 1] = CS2VM2_HOT_PUSH_INT_LOCAL;
        reseal(tb, tsize);
        jt_check(attach_to_copy(&t, tb, tsize) == CS2VM2_JIT_ATTACHED, "CL ending exactly at op_count is legal");
        free(tb);
        CS2VM2_ScriptFree(&t);
    }
    /* A zero-length blob is input like any other. */
    {
        static uint8_t const nothing[1];
        jt_check(attach_to_copy(&s, nothing, 0) == CS2VM2_JIT_REJECT_TRUNCATED, "zero bytes: truncated");
    }
    free(b);
    free(blob);
    CS2VM2_ScriptFree(&s);
}

/* ---- the fuzz ------------------------------------------------------------ */

static uint32_t g_rng = 0xC0FFEE11u;

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
    CS2_OP_BRANCH_GREATER_THAN, CS2_OP_SUB, CS2_OP_PUSH_CONSTANT_STRING, CS2_OP_POP_INT_DISCARD,
};

static void
random_script(
    struct CS2VM2_Script* s,
    int id)
{
    struct jt_asm a;
    int const n = rnd_range(1, 24);
    int const nl = rnd_range(0, 6);
    /* A backward branch often loops to the cycle limit (a million
     * instructions a run), so only one script in four may have them. */
    bool const backward = (rnd() % 4u) == 0;
    jt_asm_init(&a);
    for( int pc = 0; pc < n; pc++ )
    {
        int const op = g_rand_ops[rnd() % (sizeof(g_rand_ops) / sizeof(g_rand_ops[0]))];
        int arg = 0;
        if( op == CS2_OP_PUSH_INT_LOCAL || op == CS2_OP_POP_INT_LOCAL )
            arg = rnd_range(0, nl + 1);
        else if( op == CS2_OP_PUSH_CONSTANT_INT )
            arg = rnd_range(-2, 2);
        else if( op >= CS2_OP_BRANCH && op <= CS2_OP_BRANCH_GREATER_THAN )
            arg = backward ? rnd_range(-(pc + 1), n - pc - 1) : rnd_range(0, n - pc - 1);
        int const at = jt_op(&a, op, arg);
        if( op == CS2_OP_PUSH_CONSTANT_STRING && (rnd() & 1u) )
            a.str[at] = "fz";
    }
    jt_build(&a, s, id, nl);
}

/* A code byte worth trying at `pc`: legal ones mostly, anything sometimes. */
static uint8_t
candidate_code(
    struct CS2VM2_Script const* s,
    int pc)
{
    uint32_t const r = rnd() % 10u;
    if( r < 3 )
        return s->jit->code[pc];
    if( r < 5 )
        return 0;
    if( r < 6 )
        return g_cs2vm2_hot_op[s->opcodes[pc]];
    if( r < 9 )
    {
        static uint8_t const pool[] = { 32, 33, 34, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
                                        76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87 };
        return pool[rnd() % sizeof(pool)];
    }
    return (uint8_t)rnd();
}

/* The test's own statement of what is legal at a pc (docs/CS2_JIT.md
 * "Validation" step 11), to draw codes the validator must accept. */
static bool
fz_ins_is(
    struct CS2VM2_Script const* s,
    int pc,
    unsigned ins)
{
    int const op = s->opcodes[pc];
    int const v = s->int_operands[pc];
    bool const declared = s->local_int_count <= CS2VM_MAX_LOCALS && v >= 0 && v < s->local_int_count;
    switch( ins )
    {
    case CS2VM2_JIT_INS_L:
        return op == CS2_OP_PUSH_INT_LOCAL && declared;
    case CS2VM2_JIT_INS_C:
        return op == CS2_OP_PUSH_CONSTANT_INT;
    case CS2VM2_JIT_INS_P:
        return op == CS2_OP_POP_INT_LOCAL && declared;
    case CS2VM2_JIT_INS_ADD:
        return op == CS2_OP_ADD;
    case CS2VM2_JIT_INS_BR:
        return op == CS2_OP_BRANCH;
    case CS2VM2_JIT_INS_BREQ:
        return op == CS2_OP_BRANCH_EQUALS;
    case CS2VM2_JIT_INS_BRNE:
        return op == CS2_OP_BRANCH_NOT;
    case CS2VM2_JIT_INS_BRLT:
        return op == CS2_OP_BRANCH_LESS_THAN;
    case CS2VM2_JIT_INS_BRGT:
        return op == CS2_OP_BRANCH_GREATER_THAN;
    default:
        return false;
    }
}

/* A random code that is legal at `pc`, not necessarily the builder's. */
static uint8_t
legal_code(
    struct CS2VM2_Script const* s,
    int pc)
{
    uint8_t opts[40];
    int n = 0;
    opts[n++] = 0;
    if( g_cs2vm2_hot_op[s->opcodes[pc]] )
        opts[n++] = g_cs2vm2_hot_op[s->opcodes[pc]];
    if( fz_ins_is(s, pc, CS2VM2_JIT_INS_L) )
        opts[n++] = CS2VM2_JIT_LD;
    if( fz_ins_is(s, pc, CS2VM2_JIT_INS_P) )
        opts[n++] = CS2VM2_JIT_PD;
    if( s->opcodes[pc] == CS2_OP_PUSH_CONSTANT_STRING && s->string_operands[pc] )
        opts[n++] = CS2VM2_JIT_CSTR;
    for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
    {
        struct CS2VM2_JitShape const* sh = &g_cs2vm2_jit_shapes[i];
        bool ok = pc + sh->k <= s->op_count;
        for( int j = 0; ok && j < sh->k; j++ )
            ok = fz_ins_is(s, pc + j, sh->ins[j]);
        if( ok )
            opts[n++] = sh->code;
    }
    return opts[rnd() % (uint32_t)n];
}

static void
test_fuzz(void)
{
    int const saved_quiet = g_jt_quiet;
    int const fail0 = g_jt_fail;
    int accepted = 0, rejected = 0, other = 0, differ = 0, trials = 0, legal_rejected = 0;
    g_jt_quiet = 1;
    for( int iter = 0; iter < 600; iter++ )
    {
        struct CS2VM2_Script s;
        random_script(&s, 3000 + iter);
        struct jt_setup setup;
        memset(&setup, 0, sizeof(setup));
        setup.ints_prefill = (int)(rnd() % 4u);
        setup.locals_count = s.local_int_count;
        for( int i = 0; i < setup.locals_count; i++ )
            setup.locals[i] = rnd_range(-3, 3);
        struct jt_obs plain;
        jt_run(&s, &setup, &plain);
        size_t size;
        uint8_t* blob = make_blob(&s, &size);
        uint8_t* b = malloc(size);
        for( int t = 0; t < 12; t++ )
        {
            memcpy(b, blob, size);
            if( t % 3 == 2 )
            {
                /* every pc a legal code, chosen at random: must be accepted */
                for( int pc = 0; pc < s.op_count; pc++ )
                    b[48 + pc] = legal_code(&s, pc);
            }
            else if( t % 3 == 1 )
            {
                /* flip bits in one to three code bytes */
                int const flips = rnd_range(1, 3);
                for( int f = 0; f < flips; f++ )
                    b[48 + rnd() % (uint32_t)s.op_count] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            else
            {
                for( int pc = 0; pc < s.op_count; pc++ )
                    b[48 + pc] = candidate_code(&s, pc);
            }
            reseal(b, size);
            struct CS2VM2_Script c;
            copy_plain(&s, &c);
            enum CS2VM2_JitAttachResult const r = CS2VM2_JitAttachBlob(&c, CS2VM2_JitFingerprint(&c), b, size);
            trials++;
            if( r == CS2VM2_JIT_ATTACHED )
            {
                accepted++;
                struct jt_obs got;
                jt_run(&c, &setup, &got);
                char buf[256];
                char const* d = jt_obs_diff(&plain, &got, buf, sizeof(buf));
                if( d )
                {
                    differ++;
                    printf("  FAIL: fuzz script %d trial %d: accepted blob differs: %s\n", s.script_id, t, d);
                }
            }
            else if( r == CS2VM2_JIT_REJECT_ILLEGAL_CODE )
            {
                rejected++;
                if( t % 3 == 2 )
                    legal_rejected++;
            }
            else
                other++;
            CS2VM2_ScriptFree(&c);
        }
        free(b);
        free(blob);
        CS2VM2_ScriptFree(&s);
    }
    g_jt_quiet = saved_quiet;
    jt_check(g_jt_fail == fail0 && differ == 0 && other == 0 && accepted > 0 && rejected > 0 &&
                 legal_rejected == 0,
        "fuzz: %d resealed blobs, %d accepted and run identically, %d illegal, %d other, %d legal ones refused",
        trials, accepted, rejected, other, legal_rejected);
}

int
main(void)
{
    printf("test-cs2-jit-blob\n");
    test_golden();
    test_fingerprint();
    test_round_trip();
    test_rejections();
    test_fuzz();
    return jt_finish("test-cs2-jit-blob");
}
