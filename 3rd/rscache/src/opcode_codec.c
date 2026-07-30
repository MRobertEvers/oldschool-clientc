#include "opcode_codec.h"

#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_inv.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_config_struct.h"
#include "datatypes/dat2_config_var.h"

#include <stdlib.h>
#include <string.h>

/*
 * The registry.
 *
 * Every row is a wrapper over functions that already exist and are already held to
 * byte-exact round-trip. Nothing here reimplements a layout, and no decoder body was
 * touched to add a row — which is the property that lets this land without moving
 * any fidelity number.
 *
 * The wrappers are macro-generated because they are pure shape adaptation and
 * reading them as a table is what makes an omission obvious. That is the same
 * reasoning `tools/cachepack/cp_codec.c` records for gathering its baseline
 * functions in one file rather than beside each type.
 *
 * ## Three shapes the datatypes come in, and why the macros are plural
 *
 *  - **Profile or no profile.** `loc` and `npc` need the cache profile because an
 *    opcode's *shape* is era dependent; `inv` does not. Passing it everywhere would
 *    be uniform but would also mean an encoder that quietly ignores era when it
 *    should not, which is the failure `dat2_config_npc.h` documents for opcode 102.
 *  - **In-place or allocating.** Most types decode into a caller's struct;
 *    `npc`, `spotanim`, `bas` and `component` only allocate. Those get a shim.
 *  - **Owns memory or does not.** A type with no `Free` in its header owns nothing,
 *    so its `record_free` is NULL rather than an empty function.
 */

/* ---- wrapper generation -------------------------------------------------- */

/** Defaults are a zeroed struct: every field the decoder leaves alone reads 0. */
#define WRAP_INIT_ZERO(TAG, STRUCT)                                                                \
    static void cpc_##TAG##_init(void* record)                                                     \
    {                                                                                              \
        memset(record, 0, sizeof(STRUCT));                                                         \
    }

/**
 * Zero, then apply the type's non-zero defaults.
 *
 * Required for any type whose decoder used to seed defaults at the top of its own
 * loop — `hitsplat` and `healthbar` both set their sprite ids to -1 there. Once
 * the loop moves out to the shared driver, a zeroing init alone would leave those
 * at 0, and sprite 0 is a real sprite: the record would decode with no error and
 * draw the wrong graphic. That failure has no other symptom, which is why the
 * defaults are a named function per type rather than a comment.
 */
#define WRAP_INIT(TAG, STRUCT, FN)                                                                 \
    static void cpc_##TAG##_init(void* record)                                                     \
    {                                                                                              \
        memset(record, 0, sizeof(STRUCT));                                                         \
        FN((STRUCT*)record);                                                                       \
    }

/**
 * Decode into a caller-owned struct, reporting `_consumed`.
 *
 * `_consumed` short of `size` is not an error here and must not be turned into one:
 * these decoders stop on an opcode they do not know rather than guessing its width,
 * and the short return is precisely the signal the round-trip harness asserts on.
 * Reporting it as failure would hide it.
 */
#define WRAP_DECODE_INPLACE(TAG, STRUCT, FN)                                                       \
    static int cpc_##TAG##_decode(                                                                 \
        const struct RSCache* cache, void* record, const uint8_t* src, int size)                   \
    {                                                                                              \
        STRUCT* rec = (STRUCT*)record;                                                             \
        (void)cache;                                                                               \
        if( !rec || !src || size < 0 )                                                             \
            return -1;                                                                             \
        FN(rec, src, size);                                                                        \
        return rec->_consumed;                                                                     \
    }

/**
 * Adapt a typed per-opcode handler to the interface's `void*` form.
 *
 * The typed function is what a server-side record calls to delegate — it takes
 * `&server->base`, so it must stay concretely typed rather than being generated
 * only in this erased shape.
 */
#define WRAP_DECODE_OP(TAG, STRUCT, FN)                                                            \
    static bool cpc_##TAG##_decode_op(                                                             \
        void* record, int opcode, struct RSCache_Buffer* buffer, unsigned flags)                   \
    {                                                                                              \
        return FN((STRUCT*)record, opcode, buffer, flags);                                         \
    }

/**
 * Adapt an era-flags function to the hook's type.
 *
 * Needed because these return `int` while the hook is `unsigned`; the wrapper is
 * where that conversion is stated once rather than cast at each call.
 */
/** Post-decode fixups, adapted to the hook's `void*` form. */
#define WRAP_FINISH(TAG, STRUCT, FN)                                                               \
    static void cpc_##TAG##_finish(void* record, unsigned flags)                                   \
    {                                                                                              \
        FN((STRUCT*)record, flags);                                                                \
    }

#define WRAP_FLAGS(TAG, FN)                                                                        \
    static unsigned cpc_##TAG##_flags(const struct RSCache* cache)                                 \
    {                                                                                              \
        return (unsigned)FN(cache);                                                                \
    }

/** An encoder that needs the profile, because its opcode shapes are era-gated. */
#define WRAP_ENCODE_PROFILE(TAG, STRUCT, FN)                                                       \
    static uint32_t cpc_##TAG##_encode(                                                            \
        const struct RSCache* cache, const void* record, uint8_t* out, uint32_t cap)               \
    {                                                                                              \
        return FN(cache, (const STRUCT*)record, out, cap);                                         \
    }

#define WRAP_ENCODE(TAG, STRUCT, FN)                                                               \
    static uint32_t cpc_##TAG##_encode(                                                            \
        const struct RSCache* cache, const void* record, uint8_t* out, uint32_t cap)               \
    {                                                                                              \
        (void)cache;                                                                               \
        return FN((const STRUCT*)record, out, cap);                                                \
    }

#define WRAP_BOUND(TAG, STRUCT, FN)                                                                \
    static uint32_t cpc_##TAG##_bound(const void* record)                                          \
    {                                                                                              \
        return FN((const STRUCT*)record);                                                          \
    }

#define WRAP_FREE(TAG, STRUCT, FN)                                                                 \
    static void cpc_##TAG##_free(void* record)                                                     \
    {                                                                                              \
        FN((STRUCT*)record);                                                                       \
    }

/* ---- dat2: fixed-shape records ------------------------------------------- */

WRAP_INIT_ZERO(
    inv,
    struct RSCache_Dat2ConfigInv)
WRAP_DECODE_OP(
    inv,
    struct RSCache_Dat2ConfigInv,
    RSCache_Dat2ConfigInvDecodeOp)
WRAP_ENCODE(
    inv,
    struct RSCache_Dat2ConfigInv,
    RSCache_Dat2ConfigInvEncode)
WRAP_BOUND(
    inv,
    struct RSCache_Dat2ConfigInv,
    RSCache_Dat2ConfigInvEncodeBound)
WRAP_FREE(
    inv,
    struct RSCache_Dat2ConfigInv,
    RSCache_Dat2ConfigInvFreeInplace)

WRAP_INIT(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatInit)
WRAP_DECODE_OP(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatDecodeOp)
WRAP_ENCODE(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatEncode)
WRAP_BOUND(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatEncodeBound)

WRAP_INIT(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarInit)
WRAP_DECODE_OP(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarDecodeOp)
WRAP_ENCODE(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarEncode)
WRAP_BOUND(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarEncodeBound)

WRAP_INIT(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitInit)
WRAP_DECODE_OP(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitDecodeOp)
WRAP_ENCODE(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitEncode)
WRAP_BOUND(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitEncodeBound)
WRAP_FREE(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitFreeInplace)

WRAP_INIT_ZERO(
    varp,
    struct RSCache_Dat2ConfigVarplayer)
WRAP_DECODE_OP(
    varp,
    struct RSCache_Dat2ConfigVarplayer,
    RSCache_Dat2ConfigVarplayerDecodeOp)
WRAP_ENCODE(
    varp,
    struct RSCache_Dat2ConfigVarplayer,
    RSCache_Dat2ConfigVarplayerEncode)
WRAP_BOUND(
    varp,
    struct RSCache_Dat2ConfigVarplayer,
    RSCache_Dat2ConfigVarplayerEncodeBound)

WRAP_INIT_ZERO(
    varc,
    struct RSCache_Dat2ConfigVarclient)
WRAP_DECODE_OP(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientDecodeOp)
WRAP_ENCODE(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientEncode)
WRAP_BOUND(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientEncodeBound)

WRAP_INIT(
    param,
    struct RSCache_Dat2ConfigParam,
    RSCache_Dat2ConfigParamInit)
WRAP_DECODE_OP(
    param,
    struct RSCache_Dat2ConfigParam,
    RSCache_Dat2ConfigParamDecodeOp)
WRAP_ENCODE(
    param,
    struct RSCache_Dat2ConfigParam,
    RSCache_Dat2ConfigParamEncode)
WRAP_BOUND(
    param,
    struct RSCache_Dat2ConfigParam,
    RSCache_Dat2ConfigParamEncodeBound)
WRAP_FREE(
    param,
    struct RSCache_Dat2ConfigParam,
    RSCache_Dat2ConfigParamFreeInplace)

WRAP_INIT_ZERO(
    cfgstruct,
    struct RSCache_Dat2ConfigStruct)
WRAP_DECODE_OP(
    cfgstruct,
    struct RSCache_Dat2ConfigStruct,
    RSCache_Dat2ConfigStructDecodeOp)
WRAP_ENCODE(
    cfgstruct,
    struct RSCache_Dat2ConfigStruct,
    RSCache_Dat2ConfigStructEncode)
WRAP_BOUND(
    cfgstruct,
    struct RSCache_Dat2ConfigStruct,
    RSCache_Dat2ConfigStructEncodeBound)
WRAP_FREE(
    cfgstruct,
    struct RSCache_Dat2ConfigStruct,
    RSCache_Dat2ConfigStructFreeInplace)

WRAP_INIT(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcInit)
WRAP_FINISH(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcFinish)
WRAP_FLAGS(
    npc,
    RSCache_Dat2ConfigNpcFlags)
WRAP_DECODE_OP(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcDecodeOp)
WRAP_ENCODE_PROFILE(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcEncodeProfile)
WRAP_BOUND(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcEncodeBound)
WRAP_FREE(
    npc,
    struct RSCache_Dat2ConfigNpc,
    RSCache_Dat2ConfigNpcFreeInplace)

WRAP_INIT(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocInit)
WRAP_FINISH(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocFinish)
WRAP_FLAGS(
    loc,
    RSCache_Dat2ConfigLocFlags)
WRAP_DECODE_OP(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocDecodeOp)
WRAP_ENCODE_PROFILE(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocEncode)
WRAP_BOUND(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocEncodeBound)
WRAP_FREE(
    loc,
    struct RSCache_Dat2ConfigLoc,
    RSCache_Dat2ConfigLocFreeInplace)

/* ---- the table ----------------------------------------------------------- */

/*
 * Designated initialisers, not positional. The first version of this table was
 * positional and broke silently the moment `decode_op` was inserted ahead of
 * `decode`: every row still compiled, with a decode function installed as the
 * flags hook. Naming the fields makes the order of the struct irrelevant, which
 * matters most for the rows where an optional hook is absent.
 */
#define ROW(NAME, TYPE, EPOCH, STRUCT, TAG, FREE)                                                  \
    { .name = NAME,                                                                                \
      .type = TYPE,                                                                                \
      .epoch = EPOCH,                                                                              \
      .record_size = sizeof(STRUCT),                                                               \
      .record_init = cpc_##TAG##_init,                                                             \
      .record_finish = NULL,                                                                       \
      .record_free = FREE,                                                                         \
      .flags_for = NULL,                                                                           \
      .decode_op = cpc_##TAG##_decode_op,                                                          \
      .decode = NULL,                                                                              \
      .encode = cpc_##TAG##_encode,                                                                \
      .encode_bound = cpc_##TAG##_bound }

/** Like ROW, for a type whose opcodes are era-gated and so needs `flags_for`. */
#define ROW_ERA(NAME, TYPE, EPOCH, STRUCT, TAG, FREE)                                              \
    { .name = NAME,                                                                                \
      .type = TYPE,                                                                                \
      .epoch = EPOCH,                                                                              \
      .record_size = sizeof(STRUCT),                                                               \
      .record_init = cpc_##TAG##_init,                                                             \
      .record_finish = cpc_##TAG##_finish,                                                         \
      .record_free = FREE,                                                                         \
      .flags_for = cpc_##TAG##_flags,                                                              \
      .decode_op = cpc_##TAG##_decode_op,                                                          \
      .decode = NULL,                                                                              \
      .encode = cpc_##TAG##_encode,                                                                \
      .encode_bound = cpc_##TAG##_bound }

/** A type whose record is not purely a stream, so it keeps its own whole-record
 *  decode instead of taking the shared driver. */
#define ROW_WHOLE(NAME, TYPE, EPOCH, STRUCT, TAG, FREE)                                            \
    { .name = NAME,                                                                                \
      .type = TYPE,                                                                                \
      .epoch = EPOCH,                                                                              \
      .record_size = sizeof(STRUCT),                                                               \
      .record_init = cpc_##TAG##_init,                                                             \
      .record_finish = NULL,                                                                       \
      .record_free = FREE,                                                                         \
      .flags_for = NULL,                                                                           \
      .decode_op = NULL,                                                                           \
      .decode = cpc_##TAG##_decode,                                                                \
      .encode = cpc_##TAG##_encode,                                                                \
      .encode_bound = cpc_##TAG##_bound }

static const struct RSCache_OpcodeCodec k_codecs[] = {
    ROW("inv",
        RSCACHE_TYPE_INV,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigInv,
        inv,
        cpc_inv_free),
    ROW(
        "hitsplat",
        RSCACHE_TYPE_HITSPLAT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigHitsplat,
        hitsplat,
        NULL),
    ROW(
        "healthbar",
        RSCACHE_TYPE_HEALTHBAR,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigHealthbar,
        healthbar,
        NULL),
    ROW(
        "varbit",
        RSCACHE_TYPE_VARBIT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarbit,
        varbit,
        cpc_varbit_free),
    ROW(
        "varp",
        RSCACHE_TYPE_VARPLAYER,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarplayer,
        varp,
        NULL),
    ROW(
        "varc",
        RSCACHE_TYPE_VARCLIENT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarclient,
        varc,
        NULL),
    ROW(
        "param",
        RSCACHE_TYPE_PARAM,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigParam,
        param,
        cpc_param_free),
    ROW(
        "struct",
        RSCACHE_TYPE_STRUCT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigStruct,
        cfgstruct,
        cpc_cfgstruct_free),
    ROW_ERA(
        "npc",
        RSCACHE_TYPE_NPC,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigNpc,
        npc,
        cpc_npc_free),
    ROW_ERA(
        "loc",
        RSCACHE_TYPE_LOC,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigLoc,
        loc,
        cpc_loc_free),
};

#define CODEC_COUNT ((int)(sizeof(k_codecs) / sizeof(k_codecs[0])))

const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecFor(
    const struct RSCache* cache,
    enum RSCache_Type type)
{
    int epoch = cache ? cache->epoch : RSCACHE_EPOCH_DAT2;

    for( int i = 0; i < CODEC_COUNT; i++ )
    {
        if( k_codecs[i].type == type && (int)k_codecs[i].epoch == epoch )
            return &k_codecs[i];
    }
    return NULL;
}

const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecByName(
    const struct RSCache* cache,
    const char* name)
{
    int epoch = cache ? cache->epoch : RSCACHE_EPOCH_DAT2;

    assert(name != NULL);
    for( int i = 0; i < CODEC_COUNT; i++ )
    {
        if( strcmp(k_codecs[i].name, name) == 0 && (int)k_codecs[i].epoch == epoch )
            return &k_codecs[i];
    }
    return NULL;
}

int
RSCache_OpcodeCodecCount(void)
{
    return CODEC_COUNT;
}

const struct RSCache_OpcodeCodec*
RSCache_OpcodeCodecAt(int index)
{
    assert(index >= 0 && index < CODEC_COUNT);
    return &k_codecs[index];
}

int
RSCache_OpcodeStreamDecode(
    const struct RSCache_OpcodeCodec* codec,
    const struct RSCache* cache,
    void* record,
    const uint8_t* src,
    int size)
{
    struct RSCache_Buffer buffer;
    unsigned flags;

    assert(codec != NULL);
    assert(codec->decode_op != NULL);
    assert(record != NULL);
    assert(src != NULL);
    assert(size > 0);

    flags = codec->flags_for ? codec->flags_for(cache) : 0u;
    RSCache_BufferInit(&buffer, (uint8_t*)src, (uint32_t)size);

    for( ;; )
    {
        int opcode;

        if( buffer.position >= buffer.size )
            break;
        opcode = g1(&buffer);
        if( opcode == 0 )
            break;
        /* An opcode the codec declines ends the record: its payload width is
         * unknown, so skipping it would resynchronise on garbage. The caller sees
         * the shortfall in the return value. */
        if( !codec->decode_op(record, opcode, &buffer, flags) )
            break;
    }
    /* Runs even on a short stream: a record that stopped early still needs its
     * derived fields, and leaving them unset is the difference between a record
     * that re-encodes and one that quietly does not. */
    if( codec->record_finish )
        codec->record_finish(record, flags);
    return (int)buffer.position;
}

void*
RSCache_OpcodeCodecRecordDecode(
    const struct RSCache_OpcodeCodec* codec,
    const struct RSCache* cache,
    const uint8_t* src,
    int size,
    int* out_consumed)
{
    void* record;
    int consumed;

    assert(codec != NULL);
    assert(cache != NULL);
    record = calloc(1, codec->record_size);
    assert(record != NULL);

    if( codec->record_init )
        codec->record_init(record);
    /* A row that supplies `decode_op` takes the shared loop; `decode` is the
     * override for records that are not purely a stream. */
    consumed = codec->decode_op ? RSCache_OpcodeStreamDecode(codec, cache, record, src, size)
                                : codec->decode(cache, record, src, size);
    if( consumed < 0 )
    {
        /* Never hand back a half-decoded record: a caller that checked only for
         * NULL would read fields the decoder never reached. */
        if( codec->record_free )
            codec->record_free(record);
        free(record);
        return NULL;
    }
    if( out_consumed )
        *out_consumed = consumed;
    return record;
}

void
RSCache_OpcodeCodecRecordFree(
    const struct RSCache_OpcodeCodec* codec,
    void* record)
{
    assert(record != NULL);
    if( codec && codec->record_free )
        codec->record_free(record);
    free(record);
}
