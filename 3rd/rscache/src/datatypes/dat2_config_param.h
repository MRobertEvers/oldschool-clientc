#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_PARAM_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"

/**
 * The fields a param stream can state, for `RSCache_Dat2ConfigParam.present`.
 *
 * Keyed by meaning. The type has two: opcode 1 states it as the ScriptVarType's
 * character key and opcode 8 as its numeric id, and a rev-239 record carries
 * BOTH (`01 69 08 00` -- `i`, then id 0). They are separate fields because the
 * record states them separately; the decoder used to fold them into `type`, so
 * every record with an opcode 8 re-encoded without it.
 */
enum RSCache_Dat2ConfigParamField
{
    RSCACHE_PARAM_FIELD_TYPE = 0,       /* 1: type as a character key */
    RSCACHE_PARAM_FIELD_TYPE_ID,        /* 8: type as a ScriptVarType id */
    RSCACHE_PARAM_FIELD_DEFAULT_INT,    /* 2 */
    RSCACHE_PARAM_FIELD_DEFAULT_STRING, /* 5 */
    RSCACHE_PARAM_FIELD_DEFAULT_LONG,   /* 7 */
    RSCACHE_PARAM_FIELD_AUTO_DISABLE,   /* 4: clears auto_disable */
    RSCACHE_PARAM_FIELD_COUNT
};

struct RSCache_Dat2ConfigParam
{
    /** Which `RSCache_Dat2ConfigParamField`s the stream stated. The encoder writes
     *  exactly these; the values below still hold the client defaults for the
     *  rest, so a reader that only consults values is unaffected. */
    struct RSCache_Presence present;
    int id;
    /** The type character every reader wants: opcode 8's id mapped to its
     *  character when the record carries one (the client reads 8 after 1, and
     *  the later opcode wins), else opcode 1's. */
    char type;
    /** Opcode 1's byte exactly as stored (`RSCACHE_PARAM_FIELD_TYPE`). `type`
     *  is derived from it, but lossily -- 128..159 collapse to '?' -- and opcode 8
     *  may override it, so this is what the encoder writes back. A record built
     *  by hand that states the type sets this and the field bit. */
    int type_key;
    /** Opcode 8's ScriptVarType id exactly as stored (`RSCACHE_PARAM_FIELD_TYPE_ID`). */
    int type_id;
    int default_int;
    long long default_long;
    int auto_disable;
    char* default_string;
    /** Bytes consumed. Equal to the record size for a fully understood record.
     *
     *  Added with the per-opcode split: this decoder used to skip opcodes it did
     *  not know and carry on, so it had no way to report that it had lost the
     *  thread. It now stops, and this is the signal — the same convention the
     *  rest of the library already follows. */
    int _consumed;
};

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call: opcode 4 *clears* `auto_disable`, so its
 * default is 1 and a merely-zeroed record reads as off for every param.
 */
void
RSCache_Dat2ConfigParamInit(struct RSCache_Dat2ConfigParam* entry);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigParamDecodeOp(
    struct RSCache_Dat2ConfigParam* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

void
RSCache_Dat2ConfigParamDecodeInplace(
    struct RSCache_Dat2ConfigParam* entry,
    const void* data,
    int data_size);

/** The type character opcode 1's stored byte stands for: the byte itself,
 *  except 128..159, which collapse to '?'. */
char
RSCache_Dat2ConfigParamCharForTypeKey(int key);

/** The type character opcode 8's ScriptVarType `id` stands for. An id this
 *  table does not know reads as 'i', as it always has here. */
char
RSCache_Dat2ConfigParamCharForTypeId(int id);

/**
 * Encode a param record: exactly the fields `present` names, in the order the
 * record's era uses -- 1, 8, 2, 5, 7, 4 when it states opcode 8 (rev 239), and
 * 4, 1, 2, 5, 7 when it does not (every older cache). Returns bytes written.
 */
uint32_t
RSCache_Dat2ConfigParamEncode(
    const struct RSCache_Dat2ConfigParam* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_Dat2ConfigParamFree(struct RSCache_Dat2ConfigParam* entry);

void
RSCache_Dat2ConfigParamFreeInplace(struct RSCache_Dat2ConfigParam* entry);

/** Bytes needed for `entry`, an upper bound. */
uint32_t
RSCache_Dat2ConfigParamEncodeBound(const struct RSCache_Dat2ConfigParam* entry);

#endif
