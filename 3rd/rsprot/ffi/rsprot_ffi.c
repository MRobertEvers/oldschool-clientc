/* rsprot_ffi. See rsprot_ffi.h. */
#include "rsprot_ffi.h"

#include "../gen/rsprot_tables.h"
#include "../include/rsprot_exec.h"
#include "../include/rsprot_registry.h"
#include "../src/rsprot_compression.h"

#include "pkt_codec_storage.h"

#include <stdio.h>
#include <string.h>

int
rsprot_ffi_abi(void)
{
    return RSPROT_FFI_ABI;
}

int
rsprot_ffi_revision_first(void)
{
    return rsprot_all_revisions[0].revision;
}

int
rsprot_ffi_revision_last(void)
{
    return rsprot_all_revisions[RSPROT_REVISION_COUNT - 1].revision;
}

int
rsprot_ffi_prot(int revision, int client, int i, const char **name, int *opcode, int *size,
                const char **codec)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);
    const RsprotProtDef *row;

    if (!t || i < 0 || i >= (client ? t->client_count : t->server_count))
        return -1;
    row = client ? &t->client[i] : &t->server[i];
    *name = row->name;
    *opcode = row->code;
    *size = row->size;
    *codec = row->codec;
    return 0;
}

const char *
rsprot_ffi_zone_prot(int revision, int ordinal)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);

    return t ? rsprot_zone_prot_name(t, ordinal) : NULL;
}

/* The row `prot` names at `revision`, for its codec field; NULL when the
 * table has no such row (a zone sub-prot, or a name a host spells itself). */
static const RsprotProtDef *
row_named(int revision, int client, const char *prot)
{
    const RsprotRevTable *t = rsprot_rev_table(revision);

    if (!t)
        return NULL;
    return client ? rsprot_find_client_prot(t, prot) : rsprot_find_server_prot(t, prot);
}

/* Large enough for every message (CLANSETTINGS_FULL's is ~120 KB). */
static _Thread_local union
{
    long double align;
    uint8_t bytes[1 << 20];
} g_msg;

static int
decode(int revision, int client, const char *prot, const uint8_t *data, int len, int whole,
       int final, uint8_t *out, int cap, int *consumed, char *err, int err_cap)
{
    RsprotRegistryDir dir = client ? RSPROT_REG_IN : RSPROT_REG_OUT;
    const RsprotProtDef *row = row_named(revision, client, prot);
    const char *file = prot;
    RsprotCodecFn fn;
    RsprotExec x;
    RsprotBuf b;
    RsprotTrace trace;

    fn = row ? rsprot_registry_resolve_def(row, dir, revision, &file)
             : rsprot_registry_resolve(prot, dir, revision);
    if (!fn)
        return -1;
    /* Decoding transfers every field before any branch reads it, so the
     * message needs no clearing beyond what the storage provider points at. */
    memset(g_msg.bytes, 0, 4096);
    pkt_codec_storage_provide(file, g_msg.bytes, 0);
    rsprot_buf_wrap_read(&b, data, len);
    rsprot_exec_decode(&x, &b);
    trace.out = out;
    trace.cap = cap;
    if (final)
        rsprot_exec_trace_final(&x, &trace);
    else
        rsprot_exec_trace(&x, &trace);
    fn(&x, g_msg.bytes);
    if (consumed)
        *consumed = b.rpos;
    if (!rsprot_exec_ok(&x) || b.err || (whole && b.rpos != len)) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s at %d: %s (%d of %d bytes)", prot, revision,
                     x.error_reason ? x.error_reason
                     : b.err        ? "the payload ran out"
                                    : "bytes left over",
                     b.rpos, len);
        return -2;
    }
    if (trace.overflow)
        return -3;
    return trace.len;
}

int
rsprot_ffi_decode(int revision, int client, const char *prot, const uint8_t *data, int len,
                  int whole, uint8_t *out, int cap, int *consumed, char *err, int err_cap)
{
    return decode(revision, client, prot, data, len, whole, 0, out, cap, consumed, err, err_cap);
}

int
rsprot_ffi_decode_fields(int revision, int client, const char *prot, const uint8_t *data,
                         int len, int whole, uint8_t *out, int cap, int *consumed, char *err,
                         int err_cap)
{
    return decode(revision, client, prot, data, len, whole, 1, out, cap, consumed, err, err_cap);
}

static RsprotHuffman g_huffman;
static int g_huffman_loaded;

int
rsprot_ffi_huffman_load(const uint8_t *table, int len)
{
    if (len != 256)
        return -1;
    if (g_huffman_loaded) {
        rsprot_huffman_destroy(&g_huffman);
        g_huffman_loaded = 0;
    }
    if (!rsprot_huffman_create(&g_huffman, table))
        return -1;
    g_huffman_loaded = 1;
    return 0;
}

int
rsprot_ffi_huffman_decode(const uint8_t *data, int len, uint8_t *out, int cap)
{
    RsprotBuf b;
    int32_t n;

    if (!g_huffman_loaded)
        return -1;
    rsprot_buf_wrap_read(&b, data, len);
    /* `out` gets the terminator too. */
    n = rsprot_huffman_decode(&g_huffman, &b, (char *)out, cap);
    if (n < 0 || b.err)
        return -2;
    return n;
}
