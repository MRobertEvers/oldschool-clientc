#include "cachepack.h"

#include "datatypes/dat2_config_db.h"
#include "datatypes/dat2_config_enum.h"
#include "datatypes/dat2_config_flo.h"
#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_idk.h"
#include "datatypes/dat2_config_inv.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_mapelement.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_config_spotanim.h"
#include "datatypes/dat2_config_struct.h"
#include "datatypes/dat2_config_var.h"

#include <stdlib.h>
#include <string.h>

/*
 * The codec baseline: decode a record and encode it straight back, with no text in
 * between.
 *
 * This exists so `verify` can answer the question that actually matters. A record
 * that does not round-trip byte-exactly has lost something — but the loss can come
 * from either of two places, and they call for opposite responses:
 *
 *  - **the library's codec**, which is where most of it lives and is already
 *    measured and documented (EXCEPTIONS.md B2 and B3: ~25 loc opcodes consumed
 *    without storing, enum's opcode 1, param's opcode-8 spelling, obj's opcode 9,
 *    ascending opcode order versus Jagex's). Nothing this tool does can recover
 *    those, and reporting them as its own failures would be misleading.
 *  - **this tool's text layer**, which is a defect to fix.
 *
 * Running both trips over the same records makes the second visible as a
 * *difference*, and it is a difference that shows up sharply: the two overlay and
 * param bugs this file was written to find were 188 and 122 records wide against a
 * codec baseline of 4 and 294. Without the baseline they read as "this type is a bit
 * lossy" and would have shipped.
 *
 * The functions are gathered here rather than beside each type because they are one
 * harness, and reading them as a table is what makes an omission obvious.
 */

#define DECODE_ENCODE_HEAP(decl, decode, encode, release)                                     \
    decl;                                                                                     \
    decode;                                                                                   \
    uint32_t written = encode;                                                                \
    release;                                                                                  \
    return written;

static uint32_t
codec_underlay(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigUnderlay entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
        &entry, (char*)data, size, RSCache_Dat2ConfigFloFlags(&ctx->profile));
    return RSCache_Dat2ConfigUnderlayEncode(&entry, out, cap);
}

static uint32_t
codec_overlay(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigOverlay entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &entry, (char*)data, size, RSCache_Dat2ConfigFloFlags(&ctx->profile));
    uint32_t written = RSCache_Dat2ConfigOverlayEncode(&entry, out, cap);
    RSCache_Dat2ConfigOverlayFreeInplace(&entry);
    return written;
}

static uint32_t
codec_idk(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigIdk entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigIdkDecodeInplace(&entry, (char*)data, size);
    uint32_t written = RSCache_Dat2ConfigIdkEncode(&entry, out, cap);
    /* No FreeInplace exists for this one; see cp_idk.c. */
    free(entry.model_ids);
    free(entry.recolors_from);
    free(entry.recolors_to);
    free(entry.retextures_from);
    free(entry.retextures_to);
    return written;
}

static uint32_t
codec_inv(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigInv entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigInvDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_Dat2ConfigInvEncode(&entry, out, cap);
    RSCache_Dat2ConfigInvFreeInplace(&entry);
    return written;
}

static uint32_t
codec_loc(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigLoc* entry =
        RSCache_Dat2ConfigLocNewDecodeProfile(&ctx->profile, (char*)data, size);
    uint32_t written = RSCache_Dat2ConfigLocEncode(&ctx->profile, entry, out, cap);
    RSCache_Dat2ConfigLocFree(entry);
    return written;
}

static uint32_t
codec_enum(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigEnum entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigEnumDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_Dat2ConfigEnumEncode(&entry, out, cap);
    RSCache_Dat2ConfigEnumFreeInplace(&entry);
    return written;
}

static uint32_t
codec_npc(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigNpc* entry =
        RSCache_Dat2ConfigNpcNewDecodeProfile(&ctx->profile, (char*)data, size);
    uint32_t written = RSCache_Dat2ConfigNpcEncodeProfile(&ctx->profile, entry, out, cap);
    RSCache_Dat2ConfigNpcFree(entry);
    return written;
}

static uint32_t
codec_obj(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigObj* entry =
        RSCache_Dat2ConfigObjNewDecodeProfile(&ctx->profile, (char*)data, size);
    uint32_t written = RSCache_Dat2ConfigObjEncodeProfile(&ctx->profile, entry, out, cap);
    RSCache_Dat2ConfigObjFree(entry);
    return written;
}

static uint32_t
codec_param(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigParam entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigParamDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_Dat2ConfigParamEncode(&entry, out, cap);
    RSCache_Dat2ConfigParamFreeInplace(&entry);
    return written;
}

static uint32_t
codec_seq(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigSequence entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigSequenceDecodeProfile(&entry, &ctx->profile, (char*)data, size);
    uint32_t written = RSCache_Dat2ConfigSequenceEncode(&ctx->profile, &entry, out, cap);
    RSCache_Dat2ConfigSequenceFreeInplace(&entry);
    return written;
}

static uint32_t
codec_spotanim(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigSpotanim* entry =
        RSCache_Dat2ConfigSpotanimNewDecodeProfile(&ctx->profile, (char*)data, size);
    uint32_t written =
        RSCache_Dat2ConfigSpotanimEncodeRevision(ctx->profile.revision, entry, out, cap);
    RSCache_Dat2ConfigSpotanimFree(entry);
    return written;
}

static uint32_t
codec_varbit(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigVarbit entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarbitDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_Dat2ConfigVarbitEncode(&entry, out, cap);
    RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    return written;
}

static uint32_t
codec_varp(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigVarplayer entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarplayerDecodeInplace(&entry, data, size);
    return RSCache_Dat2ConfigVarplayerEncode(&entry, out, cap);
}

static uint32_t
codec_varc(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigVarclient entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigVarclientDecodeInplace(&entry, data, size);
    return RSCache_Dat2ConfigVarclientEncode(&entry, out, cap);
}

static uint32_t
codec_hitsplat(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigHitsplat entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHitsplatDecodeInplace(&entry, data, size);
    return RSCache_Dat2ConfigHitsplatEncode(&entry, out, cap);
}

static uint32_t
codec_healthbar(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigHealthbar entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigHealthbarDecodeInplace(&entry, data, size);
    return RSCache_Dat2ConfigHealthbarEncode(&entry, out, cap);
}

static uint32_t
codec_struct(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_Dat2ConfigStruct entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigStructDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_Dat2ConfigStructEncode(&entry, out, cap);
    RSCache_Dat2ConfigStructFreeInplace(&entry);
    return written;
}

static uint32_t
codec_mapelement(struct CP_Ctx* ctx, const uint8_t* data, int size, uint8_t* out, uint32_t cap)
{
    struct RSCache_MapElement entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_MapElementDecodeInplace(&entry, data, size);
    uint32_t written = RSCache_MapElementEncode(&entry, out, cap);
    RSCache_MapElementFreeInplace(&entry);
    return written;
}

#undef DECODE_ENCODE_HEAP

static cp_codec_fn g_codecs[CP_TYPE_COUNT] = {
    [CP_TYPE_UNDERLAY] = codec_underlay,   [CP_TYPE_OVERLAY] = codec_overlay,
    [CP_TYPE_IDK] = codec_idk,             [CP_TYPE_INV] = codec_inv,
    [CP_TYPE_LOC] = codec_loc,             [CP_TYPE_ENUM] = codec_enum,
    [CP_TYPE_NPC] = codec_npc,             [CP_TYPE_OBJ] = codec_obj,
    [CP_TYPE_PARAM] = codec_param,         [CP_TYPE_SEQ] = codec_seq,
    [CP_TYPE_SPOTANIM] = codec_spotanim,   [CP_TYPE_VARBIT] = codec_varbit,
    [CP_TYPE_VARP] = codec_varp,           [CP_TYPE_VARC] = codec_varc,
    [CP_TYPE_HITSPLAT] = codec_hitsplat,   [CP_TYPE_HEALTHBAR] = codec_healthbar,
    [CP_TYPE_STRUCT] = codec_struct,       [CP_TYPE_MAPELEMENT] = codec_mapelement,
    [CP_TYPE_DBROW] = NULL,                [CP_TYPE_DBTABLE] = NULL,
};

cp_codec_fn
cp_codec_roundtrip(enum CP_TypeId type)
{
    if( type < 0 || type >= CP_TYPE_COUNT )
        return NULL;
    return g_codecs[type];
}
