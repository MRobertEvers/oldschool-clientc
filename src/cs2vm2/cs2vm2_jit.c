/*
 * The load-time jit: build, serialize, validate, copy and free a script's
 * overlay. Pure: no IO, no environment, no clock, no log (cs2vm2_jit.h).
 * Design and the frozen formats: docs/CS2_JIT.md.
 */

#include "cs2vm2_jit.h"

#include "cs2_opcode.h"
#include "cs2vm2.h"
#include "cs2vm2_jit_internal.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- the format's hash ------------------------------------------------- */

#define CS2VM2_JIT_H_LEN 0x9E3779B97F4A7C15ull
#define CS2VM2_JIT_H_W 0xBF58476D1CE4E5B9ull
#define CS2VM2_JIT_H_ROT 0x94D049BB133111EBull
#define CS2VM2_JIT_H_F1 0xFF51AFD7ED558CCDull
#define CS2VM2_JIT_H_F2 0xC4CEB9FE1A85EC53ull

/* Seed of CS2VM2_JitDialectTag ("CS2DIA1"); part of the format. */
#define CS2VM2_JIT_DIALECT_SEED 0x43533244494131ull

static inline uint64_t
cs2vm2_jit_rotl(uint64_t v, unsigned r)
{
    return (v << r) | (v >> (64u - r));
}

static inline uint64_t
cs2vm2_jit_step(uint64_t h, uint64_t w)
{
    w *= CS2VM2_JIT_H_W;
    w ^= w >> 31;
    h ^= w;
    return cs2vm2_jit_rotl(h, 27) * CS2VM2_JIT_H_ROT;
}

static inline uint64_t
cs2vm2_jit_final(uint64_t h)
{
    h ^= h >> 33;
    h *= CS2VM2_JIT_H_F1;
    h ^= h >> 33;
    h *= CS2VM2_JIT_H_F2;
    h ^= h >> 33;
    return h;
}

static inline uint64_t
cs2vm2_jit_le64(uint8_t const* p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

uint64_t
CS2VM2_JitHash64(
    void const* data,
    size_t size,
    uint64_t seed)
{
    if( size > 0 )
        assert(data);

    uint8_t const* p = (uint8_t const*)data;
    uint64_t h = seed ^ ((uint64_t)size * CS2VM2_JIT_H_LEN);
    size_t const blocks = size / 8u;
    for( size_t i = 0; i < blocks; i++ )
        h = cs2vm2_jit_step(h, cs2vm2_jit_le64(p + i * 8u));
    size_t const rem = size % 8u;
    if( rem )
    {
        uint64_t w = 0;
        for( size_t i = 0; i < rem; i++ )
            w |= (uint64_t)p[blocks * 8u + i] << (8u * i);
        w |= (uint64_t)rem << 56;
        h = cs2vm2_jit_step(h, w);
    }
    return cs2vm2_jit_final(h);
}

/*
 * The same hash fed in pieces. The total length is mixed in first, so it is
 * stated up front; jh_end asserts the pieces added up to it. Bytes collect
 * in a 64-byte block, which is consumed as eight little-endian words, so the
 * result equals CS2VM2_JitHash64 over the concatenation.
 */
struct cs2vm2_jit_hasher
{
    uint64_t h;
    size_t total;
    size_t fed;
    unsigned fill;
    uint8_t buf[64];
};

static void
jh_init(
    struct cs2vm2_jit_hasher* jh,
    uint64_t seed,
    size_t total)
{
    jh->h = seed ^ ((uint64_t)total * CS2VM2_JIT_H_LEN);
    jh->total = total;
    jh->fed = 0;
    jh->fill = 0;
}

static void
jh_block(struct cs2vm2_jit_hasher* jh)
{
    for( unsigned i = 0; i < 64u; i += 8u )
        jh->h = cs2vm2_jit_step(jh->h, cs2vm2_jit_le64(jh->buf + i));
    jh->fed += 64u;
    jh->fill = 0;
}

static inline void
jh_u8(
    struct cs2vm2_jit_hasher* jh,
    unsigned v)
{
    jh->buf[jh->fill++] = (uint8_t)v;
    if( jh->fill == 64u )
        jh_block(jh);
}

static inline void
jh_u16(
    struct cs2vm2_jit_hasher* jh,
    unsigned v)
{
    jh_u8(jh, v & 0xFFu);
    jh_u8(jh, (v >> 8) & 0xFFu);
}

static inline void
jh_u32(
    struct cs2vm2_jit_hasher* jh,
    uint32_t v)
{
    jh_u8(jh, v & 0xFFu);
    jh_u8(jh, (v >> 8) & 0xFFu);
    jh_u8(jh, (v >> 16) & 0xFFu);
    jh_u8(jh, (v >> 24) & 0xFFu);
}

static inline void
jh_u64(
    struct cs2vm2_jit_hasher* jh,
    uint64_t v)
{
    jh_u32(jh, (uint32_t)v);
    jh_u32(jh, (uint32_t)(v >> 32));
}

static void
jh_bytes(
    struct cs2vm2_jit_hasher* jh,
    void const* data,
    size_t n)
{
    uint8_t const* p = (uint8_t const*)data;
    while( n > 0 )
    {
        size_t take = 64u - jh->fill;
        if( take > n )
            take = n;
        memcpy(jh->buf + jh->fill, p, take);
        jh->fill += (unsigned)take;
        p += take;
        n -= take;
        if( jh->fill == 64u )
            jh_block(jh);
    }
}

/* str: u32 0 for NULL, else u32 len + 1 then len bytes. */
static void
jh_str(
    struct cs2vm2_jit_hasher* jh,
    char const* s)
{
    if( !s )
    {
        jh_u32(jh, 0);
        return;
    }
    size_t const len = strlen(s);
    jh_u32(jh, (uint32_t)(len + 1u));
    jh_bytes(jh, s, len);
}

static size_t
jh_str_size(char const* s)
{
    return 4u + (s ? strlen(s) : 0u);
}

static uint64_t
jh_end(struct cs2vm2_jit_hasher* jh)
{
    unsigned const words = jh->fill / 8u;
    for( unsigned i = 0; i < words; i++ )
        jh->h = cs2vm2_jit_step(jh->h, cs2vm2_jit_le64(jh->buf + i * 8u));
    unsigned const rem = jh->fill % 8u;
    if( rem )
    {
        uint64_t w = 0;
        for( unsigned i = 0; i < rem; i++ )
            w |= (uint64_t)jh->buf[words * 8u + i] << (8u * i);
        w |= (uint64_t)rem << 56;
        jh->h = cs2vm2_jit_step(jh->h, w);
    }
    jh->fed += jh->fill;
    assert(jh->fed == jh->total);
    return cs2vm2_jit_final(jh->h);
}

/* ---- fingerprint and dialect tag -------------------------------------- */

static size_t
cs2vm2_jit_fingerprint_size(struct CS2VM2_Script const* script)
{
    size_t const n = script->op_count > 0 ? (size_t)script->op_count : 0u;
    size_t size = 9u * 4u;
    size += jh_str_size(script->signature);
    size += n * 2u + n * 4u;
    size += 1u + (script->long_operands ? n * 8u : 0u);
    size += 1u;
    if( script->string_operands )
    {
        for( size_t pc = 0; pc < n; pc++ )
            size += jh_str_size(script->string_operands[pc]);
    }
    size += 4u;
    for( int t = 0; t < script->switch_table_count; t++ )
    {
        int const cases = script->switch_tables[t].case_count;
        size += 4u + (cases > 0 ? (size_t)cases * 8u : 0u);
    }
    return size;
}

uint64_t
CS2VM2_JitFingerprint(struct CS2VM2_Script const* script)
{
    assert(script);

    size_t const n = script->op_count > 0 ? (size_t)script->op_count : 0u;
    if( n > 0 )
    {
        assert(script->opcodes);
        assert(script->int_operands);
    }
    if( script->switch_table_count > 0 )
        assert(script->switch_tables);

    struct cs2vm2_jit_hasher jh;
    jh_init(&jh, CS2VM2_JIT_FINGERPRINT_SEED, cs2vm2_jit_fingerprint_size(script));

    jh_u32(&jh, (uint32_t)script->op_count);
    jh_u32(&jh, (uint32_t)script->local_int_count);
    jh_u32(&jh, (uint32_t)script->local_string_count);
    jh_u32(&jh, (uint32_t)script->local_long_count);
    jh_u32(&jh, (uint32_t)script->int_argument_count);
    jh_u32(&jh, (uint32_t)script->string_argument_count);
    jh_u32(&jh, (uint32_t)script->long_argument_count);
    jh_u32(&jh, (uint32_t)script->int_stack_depth);
    jh_u32(&jh, (uint32_t)script->str_stack_depth);
    jh_str(&jh, script->signature);
    for( size_t pc = 0; pc < n; pc++ )
        jh_u16(&jh, script->opcodes[pc]);
    for( size_t pc = 0; pc < n; pc++ )
        jh_u32(&jh, (uint32_t)script->int_operands[pc]);
    jh_u8(&jh, script->long_operands ? 1u : 0u);
    if( script->long_operands )
    {
        for( size_t pc = 0; pc < n; pc++ )
            jh_u64(&jh, (uint64_t)script->long_operands[pc]);
    }
    jh_u8(&jh, script->string_operands ? 1u : 0u);
    if( script->string_operands )
    {
        for( size_t pc = 0; pc < n; pc++ )
            jh_str(&jh, script->string_operands[pc]);
    }
    jh_u32(&jh, (uint32_t)script->switch_table_count);
    for( int t = 0; t < script->switch_table_count; t++ )
    {
        struct CS2VM2_ScriptSwitch const* sw = &script->switch_tables[t];
        jh_u32(&jh, (uint32_t)sw->case_count);
        if( sw->case_count > 0 )
            assert(sw->cases);
        for( int c = 0; c < sw->case_count; c++ )
        {
            jh_u32(&jh, (uint32_t)sw->cases[c].key);
            jh_u32(&jh, (uint32_t)sw->cases[c].target_pc);
        }
    }
    return jh_end(&jh);
}

uint32_t
CS2VM2_JitDialectTag(struct CS2VM2_Dialect const* dialect)
{
    if( !dialect )
        dialect = &CS2VM2_DIALECT_OSRS;
    assert(dialect->name);

    struct cs2vm2_jit_hasher jh;
    jh_init(&jh, CS2VM2_JIT_DIALECT_SEED, jh_str_size(dialect->name) + 3u);
    jh_str(&jh, dialect->name);
    jh_u8(&jh, dialect->cc_create_nested ? 1u : 0u);
    jh_u8(&jh, dialect->array_handles ? 1u : 0u);
    jh_u8(&jh, dialect->rs2_commands ? 1u : 0u);
    uint64_t const h = jh_end(&jh);
    return (uint32_t)(h ^ (h >> 32));
}

/* ---- what is legal at a pc --------------------------------------------- */

/*
 * Whether every jump the script can make lands in [0, op_count]: each
 * operand jump's `pc + 1 + operand` (cs2vm2_op_jumps_by_operand: hot int
 * branches, long compares, BRANCH_IF_ONE), and each case of each SWITCH whose
 * table exists (`pc + 1 + target_pc`, CS2VM2_JumpRelative after the pc moved
 * on).
 * The hot loop is entered at whatever pc such a jump leaves, and indexes the
 * overlay there, so a script that fails this gets no overlay at all.
 */
static bool
cs2vm2_jit_jumps_in_range(struct CS2VM2_Script const* script)
{
    int64_t const n = script->op_count;
    for( int pc = 0; pc < script->op_count; pc++ )
    {
        int const op = script->opcodes[pc];
        if( cs2vm2_op_jumps_by_operand(op) )
        {
            int64_t const target = (int64_t)pc + 1 + (int64_t)script->int_operands[pc];
            if( target < 0 || target > n )
                return false;
        }
        else if( op == CS2_OP_SWITCH )
        {
            int const table = script->int_operands[pc];
            if( table < 0 || table >= script->switch_table_count )
                continue; /* the handler ends the script; no jump */
            struct CS2VM2_ScriptSwitch const* sw = &script->switch_tables[table];
            for( int c = 0; c < sw->case_count; c++ )
            {
                int64_t const target = (int64_t)pc + 1 + (int64_t)sw->cases[c].target_pc;
                if( target < 0 || target > n )
                    return false;
            }
        }
    }
    return true;
}

static inline bool
cs2vm2_jit_declared_local(
    struct CS2VM2_Script const* script,
    int operand)
{
    return script->local_int_count <= CS2VM_MAX_LOCALS && operand >= 0 &&
           operand < script->local_int_count;
}

/* Whether the instruction at `pc` (which exists) is `ins`. */
static inline bool
cs2vm2_jit_ins_is(
    struct CS2VM2_Script const* script,
    int pc,
    unsigned ins)
{
    unsigned const op = script->opcodes[pc];
    switch( ins )
    {
    case CS2VM2_JIT_INS_L:
        return op == CS2_OP_PUSH_INT_LOCAL && cs2vm2_jit_declared_local(script, script->int_operands[pc]);
    case CS2VM2_JIT_INS_C:
        return op == CS2_OP_PUSH_CONSTANT_INT;
    case CS2VM2_JIT_INS_P:
        return op == CS2_OP_POP_INT_LOCAL && cs2vm2_jit_declared_local(script, script->int_operands[pc]);
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

static bool
cs2vm2_jit_shape_matches(
    struct CS2VM2_Script const* script,
    int pc,
    struct CS2VM2_JitShape const* shape)
{
    if( (int64_t)pc + shape->k > (int64_t)script->op_count )
        return false;
    for( int i = 0; i < shape->k; i++ )
    {
        if( !cs2vm2_jit_ins_is(script, pc + i, shape->ins[i]) )
            return false;
    }
    return true;
}

static inline unsigned
cs2vm2_jit_plain_code(
    struct CS2VM2_Script const* script,
    int pc)
{
    unsigned const op = script->opcodes[pc];
    return op < CS2VM2_HOT_TABLE_SIZE ? g_cs2vm2_hot_op[op] : CS2VM2_HOT_NONE;
}

/*
 * Whether `code` may stand at `pc` of `script`: the validator's rule, and
 * what the builder's every choice satisfies. Every code but 0 names the
 * instruction(s) it replaces, so a legal code is semantics-preserving on its
 * own, whoever chose it.
 */
static bool
cs2vm2_jit_code_legal(
    struct CS2VM2_Script const* script,
    int pc,
    unsigned code)
{
    if( code == CS2VM2_HOT_NONE )
        return true;
    if( code <= CS2VM2_HOT_PLAIN_LAST )
        return cs2vm2_jit_plain_code(script, pc) == code;
    if( code == CS2VM2_JIT_LD )
        return cs2vm2_jit_ins_is(script, pc, CS2VM2_JIT_INS_L);
    if( code == CS2VM2_JIT_PD )
        return cs2vm2_jit_ins_is(script, pc, CS2VM2_JIT_INS_P);
    if( code == CS2VM2_JIT_CSTR )
        return script->opcodes[pc] == CS2_OP_PUSH_CONSTANT_STRING && script->string_operands &&
               script->string_operands[pc];
    struct CS2VM2_JitShape const* shape = cs2vm2_jit_shape(code);
    if( !shape )
        return false;
    return cs2vm2_jit_shape_matches(script, pc, shape);
}

/* The builder's priority (vmfuse_pre.inc, the lab's measured order). */
static const uint8_t g_cs2vm2_jit_build_order[CS2VM2_JIT_SHAPE_COUNT] = {
    CS2VM2_JIT_LCAP,
    CS2VM2_JIT_LCBREQ, CS2VM2_JIT_LCBRNE, CS2VM2_JIT_LCBRLT, CS2VM2_JIT_LCBRGT,
    CS2VM2_JIT_LLBREQ, CS2VM2_JIT_LLBRNE, CS2VM2_JIT_LLBRLT, CS2VM2_JIT_LLBRGT,
    CS2VM2_JIT_LCA,
    CS2VM2_JIT_LLL,
    CS2VM2_JIT_LL,
    CS2VM2_JIT_LC,
    CS2VM2_JIT_LP,
    CS2VM2_JIT_CBREQ, CS2VM2_JIT_CBRNE, CS2VM2_JIT_CBRLT, CS2VM2_JIT_CBRGT,
    CS2VM2_JIT_CA,
    CS2VM2_JIT_CP,
    CS2VM2_JIT_CL,
    CS2VM2_JIT_CC,
    CS2VM2_JIT_AP,
    CS2VM2_JIT_PBR,
};

static uint8_t
cs2vm2_jit_choose(
    struct CS2VM2_Script const* script,
    int pc)
{
    unsigned const op = script->opcodes[pc];
    /* Every shape starts with L, C, P or ADD: skip the table for the rest. */
    if( op == CS2_OP_PUSH_INT_LOCAL || op == CS2_OP_PUSH_CONSTANT_INT || op == CS2_OP_POP_INT_LOCAL ||
        op == CS2_OP_ADD )
    {
        for( int i = 0; i < CS2VM2_JIT_SHAPE_COUNT; i++ )
        {
            struct CS2VM2_JitShape const* shape = cs2vm2_jit_shape(g_cs2vm2_jit_build_order[i]);
            if( cs2vm2_jit_shape_matches(script, pc, shape) )
                return shape->code;
        }
    }
    if( op == CS2_OP_PUSH_CONSTANT_STRING && script->string_operands && script->string_operands[pc] )
        return CS2VM2_JIT_CSTR;
#ifndef CS2VM2_JIT_NO_SINGLETONS /* a rig-only switch: docs/CS2_JIT.md section 7 step 2 */
    if( cs2vm2_jit_ins_is(script, pc, CS2VM2_JIT_INS_L) )
        return CS2VM2_JIT_LD;
    if( cs2vm2_jit_ins_is(script, pc, CS2VM2_JIT_INS_P) )
        return CS2VM2_JIT_PD;
#endif
    return (uint8_t)cs2vm2_jit_plain_code(script, pc);
}

/* ---- the debug canary -------------------------------------------------- */

static uint64_t
cs2vm2_jit_strings_hash(struct CS2VM2_Script const* script)
{
#ifdef NDEBUG
    (void)script;
    return 0;
#else
    uint64_t h = CS2VM2_JIT_H_LEN;
    if( !script->string_operands )
        return h;
    for( int pc = 0; pc < script->op_count; pc++ )
    {
        char const* s = script->string_operands[pc];
        if( s )
            h = (h ^ CS2VM2_JitHash64(s, strlen(s), (uint64_t)pc)) * CS2VM2_JIT_H_ROT;
    }
    return h;
#endif
}

/* ---- build, attach, copy, free ----------------------------------------- */

static struct CS2VM2_Jit*
cs2vm2_jit_alloc(int op_count)
{
    assert(op_count > 0);
    struct CS2VM2_Jit* jit =
        (struct CS2VM2_Jit*)ToriMem_Alloc(ToriMem_Script(), sizeof(struct CS2VM2_Jit) + (size_t)op_count);
    assert(jit);
    return jit;
}

static enum CS2VM2_JitBuildResult
cs2vm2_jit_refusal(struct CS2VM2_Script const* script)
{
    if( script->op_count <= 0 )
        return CS2VM2_JIT_REFUSED_EMPTY;
    assert(script->opcodes);
    assert(script->int_operands);
    if( script->dialect && script->dialect->rs2_commands )
        return CS2VM2_JIT_REFUSED_DIALECT;
    if( script->switch_table_count > 0 )
        assert(script->switch_tables);
    if( !cs2vm2_jit_jumps_in_range(script) )
        return CS2VM2_JIT_REFUSED_BRANCH_RANGE;
    return CS2VM2_JIT_BUILT;
}

enum CS2VM2_JitBuildResult
CS2VM2_JitBuild(struct CS2VM2_Script* script)
{
    assert(script);
    assert(!script->jit);

    enum CS2VM2_JitBuildResult const refusal = cs2vm2_jit_refusal(script);
    if( refusal != CS2VM2_JIT_BUILT )
        return refusal;

    struct CS2VM2_Jit* jit = cs2vm2_jit_alloc(script->op_count);
    jit->op_count = script->op_count;
    jit->local_int_count = script->local_int_count;
    for( int pc = 0; pc < script->op_count; pc++ )
        jit->code[pc] = cs2vm2_jit_choose(script, pc);
    jit->strings_hash = cs2vm2_jit_strings_hash(script);
    script->jit = jit;
    return CS2VM2_JIT_BUILT;
}

size_t
CS2VM2_JitBlobSize(struct CS2VM2_Script const* script)
{
    assert(script);
    assert(script->jit);
    return (size_t)CS2VM2_JIT_BLOB_HEADER_SIZE + (size_t)script->jit->op_count;
}

static inline void
cs2vm2_jit_w16(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void
cs2vm2_jit_w32(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void
cs2vm2_jit_w64(
    uint8_t* p,
    uint64_t v)
{
    cs2vm2_jit_w32(p, (uint32_t)v);
    cs2vm2_jit_w32(p + 4, (uint32_t)(v >> 32));
}

static inline uint32_t
cs2vm2_jit_r16(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t
cs2vm2_jit_r32(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The blob checksum: the hash, seeded with the magic, over header bytes
 * [0, 40) followed by the code. */
static uint64_t
cs2vm2_jit_blob_checksum(
    uint8_t const* header,
    uint8_t const* code,
    size_t code_len)
{
    struct cs2vm2_jit_hasher jh;
    jh_init(&jh, CS2VM2_JIT_BLOB_MAGIC, 40u + code_len);
    jh_bytes(&jh, header, 40u);
    jh_bytes(&jh, code, code_len);
    return jh_end(&jh);
}

void
CS2VM2_JitSerialize(
    struct CS2VM2_Script const* script,
    uint64_t fingerprint,
    uint8_t* out,
    size_t out_size)
{
    assert(script);
    assert(script->jit);
    assert(out);
    assert(out_size == CS2VM2_JitBlobSize(script));
    (void)out_size; /* the asserts are its only reader */
    assert(script->jit->op_count == script->op_count);
    assert(fingerprint == CS2VM2_JitFingerprint(script));

    uint32_t const n = (uint32_t)script->jit->op_count;
    cs2vm2_jit_w32(out + 0, CS2VM2_JIT_BLOB_MAGIC);
    cs2vm2_jit_w16(out + 4, CS2VM2_JIT_FORMAT_VERSION);
    cs2vm2_jit_w16(out + 6, CS2VM2_JIT_BLOB_HEADER_SIZE);
    cs2vm2_jit_w32(out + 8, CS2VM2_JitDialectTag(script->dialect));
    cs2vm2_jit_w32(out + 12, (uint32_t)script->script_id);
    cs2vm2_jit_w64(out + 16, fingerprint);
    cs2vm2_jit_w32(out + 24, n);
    cs2vm2_jit_w32(out + 28, (uint32_t)script->local_int_count);
    cs2vm2_jit_w32(out + 32, n);
    cs2vm2_jit_w32(out + 36, 0u);
    memcpy(out + CS2VM2_JIT_BLOB_HEADER_SIZE, script->jit->code, n);
    cs2vm2_jit_w64(out + 40, cs2vm2_jit_blob_checksum(out, out + CS2VM2_JIT_BLOB_HEADER_SIZE, n));
}

enum CS2VM2_JitAttachResult
CS2VM2_JitAttachBlob(
    struct CS2VM2_Script* script,
    uint64_t fingerprint,
    uint8_t const* blob,
    size_t blob_size)
{
    assert(script);
    assert(blob);
    assert(!script->jit);

    /* 1..5: is this a blob for this script at all. */
    if( blob_size < CS2VM2_JIT_BLOB_HEADER_SIZE )
        return CS2VM2_JIT_REJECT_TRUNCATED;
    if( cs2vm2_jit_r32(blob + 0) != CS2VM2_JIT_BLOB_MAGIC )
        return CS2VM2_JIT_REJECT_MAGIC;
    if( cs2vm2_jit_r16(blob + 4) != CS2VM2_JIT_FORMAT_VERSION )
        return CS2VM2_JIT_REJECT_VERSION;
    if( cs2vm2_jit_r32(blob + 8) != CS2VM2_JitDialectTag(script->dialect) )
        return CS2VM2_JIT_REJECT_DIALECT;
    if( cs2vm2_jit_r32(blob + 12) != (uint32_t)script->script_id )
        return CS2VM2_JIT_REJECT_SCRIPT_ID;

    /* 6: for this version of it. */
    uint64_t const blob_fp = (uint64_t)cs2vm2_jit_r32(blob + 16) | ((uint64_t)cs2vm2_jit_r32(blob + 20) << 32);
    uint32_t const header_ops = cs2vm2_jit_r32(blob + 24);
    if( blob_fp != fingerprint || header_ops != (uint32_t)script->op_count ||
        cs2vm2_jit_r32(blob + 28) != (uint32_t)script->local_int_count )
        return CS2VM2_JIT_REJECT_STALE;

    /* 7, 8: self-consistent. */
    if( cs2vm2_jit_r16(blob + 6) != CS2VM2_JIT_BLOB_HEADER_SIZE || cs2vm2_jit_r32(blob + 32) != header_ops ||
        cs2vm2_jit_r32(blob + 36) != 0u )
        return CS2VM2_JIT_REJECT_SHAPE;
    size_t const want = (size_t)CS2VM2_JIT_BLOB_HEADER_SIZE + (size_t)header_ops;
    if( blob_size < want )
        return CS2VM2_JIT_REJECT_TRUNCATED;
    if( blob_size > want )
        return CS2VM2_JIT_REJECT_SHAPE;

    /* 9: unchanged since it was written. */
    uint8_t const* code = blob + CS2VM2_JIT_BLOB_HEADER_SIZE;
    uint64_t const sum = (uint64_t)cs2vm2_jit_r32(blob + 40) | ((uint64_t)cs2vm2_jit_r32(blob + 44) << 32);
    if( sum != cs2vm2_jit_blob_checksum(blob, code, header_ops) )
        return CS2VM2_JIT_REJECT_CHECKSUM;

    /* 10, 11: the safety check. Every byte re-proven against THIS script. */
    if( cs2vm2_jit_refusal(script) != CS2VM2_JIT_BUILT )
        return CS2VM2_JIT_REJECT_UNJITTABLE;
    for( int pc = 0; pc < script->op_count; pc++ )
    {
        if( !cs2vm2_jit_code_legal(script, pc, code[pc]) )
            return CS2VM2_JIT_REJECT_ILLEGAL_CODE;
    }

    /* 12. */
    struct CS2VM2_Jit* jit = cs2vm2_jit_alloc(script->op_count);
    jit->op_count = script->op_count;
    jit->local_int_count = script->local_int_count;
    memcpy(jit->code, code, (size_t)script->op_count);
    jit->strings_hash = cs2vm2_jit_strings_hash(script);
    script->jit = jit;
    return CS2VM2_JIT_ATTACHED;
}

void
CS2VM2_JitCopy(
    struct CS2VM2_Script const* src,
    struct CS2VM2_Script* dst)
{
    assert(src);
    assert(dst);
    assert(src->jit);
    assert(!dst->jit);
    assert(src->op_count == dst->op_count);
    assert(src->jit->op_count == src->op_count);

    struct CS2VM2_Jit* jit = cs2vm2_jit_alloc(src->op_count);
    memcpy(jit, src->jit, sizeof(struct CS2VM2_Jit) + (size_t)src->op_count);
    /* The copy's strings are its own buffers with the same bytes. */
    jit->strings_hash = cs2vm2_jit_strings_hash(dst);
    dst->jit = jit;
}

void
CS2VM2_JitFree(struct CS2VM2_Script* script)
{
    assert(script);
    if( !script->jit )
        return;
    /* Constants are pushed by pointer: a write through one would have changed
     * this script for every later run. Nothing may (docs/CS2_JIT.md 2.5). */
    assert(cs2vm2_jit_strings_hash(script) == script->jit->strings_hash);
    ToriMem_Free(ToriMem_Script(), script->jit);
    script->jit = NULL;
}

void
CS2VM2_JitGetInfo(
    struct CS2VM2_Script const* script,
    struct CS2VM2_JitInfo* out)
{
    assert(script);
    assert(script->jit);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->op_count = script->jit->op_count;
    for( int pc = 0; pc < script->jit->op_count; pc++ )
    {
        unsigned const code = script->jit->code[pc];
        struct CS2VM2_JitShape const* shape = cs2vm2_jit_shape(code);
        if( code == CS2VM2_HOT_NONE )
            out->general_pcs++;
        else if( code <= CS2VM2_HOT_PLAIN_LAST )
            out->plain_pcs++;
        else if( code == CS2VM2_JIT_LD || code == CS2VM2_JIT_PD )
            out->declared_local_pcs++;
        else if( code == CS2VM2_JIT_CSTR )
            out->constant_string_pcs++;
        else if( shape )
        {
            out->fused_group_pcs++;
            out->fused_group_ops += shape->k;
        }
    }
    out->blob_size = CS2VM2_JitBlobSize(script);
}

char const*
CS2VM2_JitBuildResultName(enum CS2VM2_JitBuildResult result)
{
    static char const* const names[CS2VM2_JIT_BUILD_RESULT_COUNT] = {
        [CS2VM2_JIT_BUILT] = "built",
        [CS2VM2_JIT_REFUSED_EMPTY] = "refused_empty",
        [CS2VM2_JIT_REFUSED_DIALECT] = "refused_dialect",
        [CS2VM2_JIT_REFUSED_BRANCH_RANGE] = "refused_branch_range",
    };
    assert((int)result >= 0);
    assert(result < CS2VM2_JIT_BUILD_RESULT_COUNT);
    return names[result];
}

char const*
CS2VM2_JitAttachResultName(enum CS2VM2_JitAttachResult result)
{
    static char const* const names[CS2VM2_JIT_ATTACH_RESULT_COUNT] = {
        [CS2VM2_JIT_ATTACHED] = "attached",
        [CS2VM2_JIT_REJECT_TRUNCATED] = "reject_truncated",
        [CS2VM2_JIT_REJECT_MAGIC] = "reject_magic",
        [CS2VM2_JIT_REJECT_VERSION] = "reject_version",
        [CS2VM2_JIT_REJECT_DIALECT] = "reject_dialect",
        [CS2VM2_JIT_REJECT_SCRIPT_ID] = "reject_script_id",
        [CS2VM2_JIT_REJECT_STALE] = "reject_stale",
        [CS2VM2_JIT_REJECT_SHAPE] = "reject_shape",
        [CS2VM2_JIT_REJECT_CHECKSUM] = "reject_checksum",
        [CS2VM2_JIT_REJECT_ILLEGAL_CODE] = "reject_illegal_code",
        [CS2VM2_JIT_REJECT_UNJITTABLE] = "reject_unjittable",
    };
    assert((int)result >= 0);
    assert(result < CS2VM2_JIT_ATTACH_RESULT_COUNT);
    return names[result];
}
