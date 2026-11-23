#ifndef CS2_JIT_TEST_SCRIPTS_H
#define CS2_JIT_TEST_SCRIPTS_H

/*
 * Shared by the host-side jit cache tests (test-cs2-jit-cache,
 * test-cs2-jit-pack-tasks): small decoded scripts the VM's builder fuses,
 * made fresh for every "boot" the way the load task makes one per decode,
 * and a lookup by id over a set of them (CS2JitScriptLookup's shape).
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2_jit.h"
#include "cs2vm2/cs2vm2_script.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_ct_fail;
static int g_ct_ok;

#define CT_CHECK(cond, ...)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            g_ct_ok++;                                                                             \
        else                                                                                       \
        {                                                                                          \
            g_ct_fail++;                                                                           \
            printf("FAIL: %s:%d: %s -- ", __FILE__, __LINE__, #cond);                              \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/*
 * Script `id`, content `variant`: a local-add-store body the builder fuses
 * (LCAP), a constant string (CSTR), and a length that grows with the id so
 * no two scripts share a size. A different variant is the same script id with
 * other content: a new fingerprint.
 */
static inline void
ct_make_script(
    struct CS2VM2_Script* s,
    int id,
    int variant)
{
    int const reps = 1 + (id % 5);
    int const n = (reps * 4) + 2;
    int pc = 0;

    CS2VM2_ScriptInit(s);
    s->script_id = id;
    s->local_int_count = 2;
    s->local_string_count = 1;
    s->op_count = n;
    s->opcodes = ToriMem_Calloc(ToriMem_Script(), (size_t)n, sizeof(uint16_t));
    s->int_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)n, sizeof(int));
    s->string_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)n, sizeof(char*));
    assert(s->opcodes);
    assert(s->int_operands);
    assert(s->string_operands);
    for( int r = 0; r < reps; r++ )
    {
        s->opcodes[pc] = CS2_OP_PUSH_INT_LOCAL;
        s->int_operands[pc++] = 0;
        s->opcodes[pc] = CS2_OP_PUSH_CONSTANT_INT;
        s->int_operands[pc++] = 7 + variant + r;
        s->opcodes[pc] = CS2_OP_ADD;
        s->int_operands[pc++] = 0;
        s->opcodes[pc] = CS2_OP_POP_INT_LOCAL;
        s->int_operands[pc++] = 1;
    }
    s->opcodes[pc] = CS2_OP_PUSH_CONSTANT_STRING;
    s->string_operands[pc] = ToriMem_Strdup(ToriMem_Script(), "cs2jit");
    pc++;
    s->opcodes[pc] = CS2_OP_RETURN;
    pc++;
    assert(pc == n);
}

/* One boot's loaded scripts. */
#define CT_MAX_SCRIPTS 32
struct ct_set
{
    struct CS2VM2_Script scripts[CT_MAX_SCRIPTS];
    int count;
};

static inline struct CS2VM2_Script*
ct_add(
    struct ct_set* set,
    int id,
    int variant)
{
    assert(set->count < CT_MAX_SCRIPTS);
    ct_make_script(&set->scripts[set->count], id, variant);
    return &set->scripts[set->count++];
}

static inline void
ct_set_free(struct ct_set* set)
{
    for( int i = 0; i < set->count; i++ )
        CS2VM2_ScriptFree(&set->scripts[i]);
    set->count = 0;
}

static inline struct CS2VM2_Script*
ct_lookup(
    void* user,
    int script_id)
{
    struct ct_set* set = user;
    for( int i = 0; i < set->count; i++ )
        if( set->scripts[i].script_id == script_id )
            return &set->scripts[i];
    return NULL;
}

/* The overlay as bytes (its blob), to compare two overlays' code. Returns
 * a ToriMem_Script block the caller frees, and its size. */
static inline uint8_t*
ct_blob(
    struct CS2VM2_Script const* s,
    size_t* out_size)
{
    size_t const size = CS2VM2_JitBlobSize(s);
    uint8_t* out = ToriMem_Alloc(ToriMem_Script(), size);

    CS2VM2_JitSerialize(s, CS2VM2_JitFingerprint(s), out, size);
    *out_size = size;
    return out;
}

/* Do `a` and `b` carry the same overlay code? */
static inline int
ct_same_code(
    struct CS2VM2_Script const* a,
    struct CS2VM2_Script const* b)
{
    size_t na;
    size_t nb;
    uint8_t* ba;
    uint8_t* bb;
    int same;

    if( !a->jit || !b->jit )
        return 0;
    ba = ct_blob(a, &na);
    bb = ct_blob(b, &nb);
    /* The code is everything after the header; the header differs only if
     * the scripts do. */
    same = na == nb && memcmp(ba + CS2VM2_JIT_BLOB_HEADER_SIZE, bb + CS2VM2_JIT_BLOB_HEADER_SIZE,
                              na - CS2VM2_JIT_BLOB_HEADER_SIZE) == 0;
    ToriMem_Free(ToriMem_Script(), ba);
    ToriMem_Free(ToriMem_Script(), bb);
    return same;
}

static inline uint32_t
ct_rd32(uint8_t const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void
ct_wr32(
    uint8_t* p,
    uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void
ct_wr64(
    uint8_t* p,
    uint64_t v)
{
    ct_wr32(p, (uint32_t)v);
    ct_wr32(p + 4, (uint32_t)(v >> 32));
}

/* Recompute a pack's index checksum after a test edited its index (the
 * pack format's definition, docs/CS2_JIT.md 4.3). */
static inline void
ct_pack_rechecksum(
    uint8_t* pack,
    size_t size)
{
    uint32_t const count = ct_rd32(pack + 16);
    size_t const index = (size_t)count * 24;
    uint8_t* stream = ToriMem_Alloc(ToriMem_Script(), 24 + index);

    assert(40 + index <= size);
    memcpy(stream, pack, 24);
    memcpy(stream + 24, pack + 40, index);
    ct_wr64(pack + 24, CS2VM2_JitHash64(stream, 24 + index, 0x4B504A43u));
    ToriMem_Free(ToriMem_Script(), stream);
}

static inline int
ct_finish(char const* name)
{
    printf("%s: %d ok, %d FAIL\n", name, g_ct_ok, g_ct_fail);
    return g_ct_fail ? 1 : 0;
}

#endif /* CS2_JIT_TEST_SCRIPTS_H */
