#include "opcode_codec.h"

#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_inv.h"
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

WRAP_INIT_ZERO(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat)
WRAP_DECODE_INPLACE(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatDecodeInplace)
WRAP_ENCODE(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatEncode)
WRAP_BOUND(
    hitsplat,
    struct RSCache_Dat2ConfigHitsplat,
    RSCache_Dat2ConfigHitsplatEncodeBound)

WRAP_INIT_ZERO(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar)
WRAP_DECODE_INPLACE(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarDecodeInplace)
WRAP_ENCODE(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarEncode)
WRAP_BOUND(
    healthbar,
    struct RSCache_Dat2ConfigHealthbar,
    RSCache_Dat2ConfigHealthbarEncodeBound)

WRAP_INIT_ZERO(
    varbit,
    struct RSCache_Dat2ConfigVarbit)
WRAP_DECODE_INPLACE(
    varbit,
    struct RSCache_Dat2ConfigVarbit,
    RSCache_Dat2ConfigVarbitDecodeInplace)
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
WRAP_DECODE_INPLACE(
    varp,
    struct RSCache_Dat2ConfigVarplayer,
    RSCache_Dat2ConfigVarplayerDecodeInplace)
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
WRAP_DECODE_INPLACE(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientDecodeInplace)
WRAP_ENCODE(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientEncode)
WRAP_BOUND(
    varc,
    struct RSCache_Dat2ConfigVarclient,
    RSCache_Dat2ConfigVarclientEncodeBound)

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
      .record_free = FREE,                                                                         \
      .flags_for = NULL,                                                                           \
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
    ROW_WHOLE(
        "hitsplat",
        RSCACHE_TYPE_HITSPLAT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigHitsplat,
        hitsplat,
        NULL),
    ROW_WHOLE(
        "healthbar",
        RSCACHE_TYPE_HEALTHBAR,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigHealthbar,
        healthbar,
        NULL),
    ROW_WHOLE(
        "varbit",
        RSCACHE_TYPE_VARBIT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarbit,
        varbit,
        cpc_varbit_free),
    ROW_WHOLE(
        "varp",
        RSCACHE_TYPE_VARPLAYER,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarplayer,
        varp,
        NULL),
    ROW_WHOLE(
        "varc",
        RSCACHE_TYPE_VARCLIENT,
        RSCACHE_EPOCH_DAT2,
        struct RSCache_Dat2ConfigVarclient,
        varc,
        NULL),
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
