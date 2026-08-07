/*
 * The packet executor — the implementation of `3rd/rsprot/include/rsprot_exec.h`.
 *
 * rsprot declares the vocabulary a codec speaks and states what the bytes are.
 * This file is the other half: what to do about them. It lives on the server
 * project side because the four directions are a *host* concern — encoding into
 * a tick's buffer, decoding into a Task's message, dumping a schema for a
 * trace, filling one for a test — and none of that is protocol.
 *
 * The shape to notice: every transfer funnels through `rsprot_x`, which is one
 * `switch` over the direction and one table lookup over the primitive. Adding a
 * primitive is a row in RSPROT_PRIM_LIST plus a row in `k_prim`; the test walks
 * the enum and fails on any row that is missing or moves no bytes, so the two
 * cannot drift apart.
 */
#include "rsprot_exec.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The primitive table                                                 */
/* ------------------------------------------------------------------ */

/*
 * Three primitives need a wrapper and it is worth saying why, because the
 * wrappers are where a transcription error would hide:
 *
 *  - `boolean` because RsprotBuf spells it in C `bool`, and the wire form is
 *    0/1 rather than the value;
 *  - the `com` family because RSProt's `pCombinedId` *is* `p4` — the wrapper
 *    exists so DESCRIBE can name the field a combined id rather than an int,
 *    which matters when one revision addresses a component with two bytes and
 *    the next with four;
 *  - `smartnull` / `smart2or4null` because their -1 encoding is the difference
 *    between "absent" and "zero", and conflating those is a real bug shape.
 */

static void
put_boolean(RsprotBuf *b, int32_t v)
{
    rsprot_pboolean(b, v != 0);
}

static int32_t
get_boolean(RsprotBuf *b)
{
    return rsprot_gboolean(b) ? 1 : 0;
}

typedef struct RsprotPrimDef
{
    const char *name;
    void (*put)(RsprotBuf *, int32_t);
    int32_t (*get)(RsprotBuf *);
    /** Bytes on the wire, or 0 when the width depends on the value. */
    int width;
    /** Value bits FILL may use. */
    int bits;
    int is_signed;
} RsprotPrimDef;

/* clang-format off */
static const RsprotPrimDef k_prim[RSPROT_PRIM_COUNT] = {
    [RSPROT_PRIM_U1]            = { "u1",            rsprot_p1,               rsprot_g1,               1, 8,  0 },
    [RSPROT_PRIM_U1_ALT1]       = { "u1_alt1",       rsprot_p1_alt1,          rsprot_g1_alt1,          1, 8,  0 },
    [RSPROT_PRIM_U1_ALT2]       = { "u1_alt2",       rsprot_p1_alt2,          rsprot_g1_alt2,          1, 8,  0 },
    [RSPROT_PRIM_U1_ALT3]       = { "u1_alt3",       rsprot_p1_alt3,          rsprot_g1_alt3,          1, 8,  0 },
    [RSPROT_PRIM_I1]            = { "i1",            rsprot_p1,               rsprot_g1s,              1, 8,  1 },
    [RSPROT_PRIM_U2]            = { "u2",            rsprot_p2,               rsprot_g2,               2, 16, 0 },
    [RSPROT_PRIM_U2_ALT1]       = { "u2_alt1",       rsprot_p2_alt1,          rsprot_g2_alt1,          2, 16, 0 },
    [RSPROT_PRIM_U2_ALT2]       = { "u2_alt2",       rsprot_p2_alt2,          rsprot_g2_alt2,          2, 16, 0 },
    [RSPROT_PRIM_U2_ALT3]       = { "u2_alt3",       rsprot_p2_alt3,          rsprot_g2_alt3,          2, 16, 0 },
    [RSPROT_PRIM_I2]            = { "i2",            rsprot_p2,               rsprot_g2s,              2, 16, 1 },
    [RSPROT_PRIM_U3]            = { "u3",            rsprot_p3,               rsprot_g3,               3, 24, 0 },
    [RSPROT_PRIM_U3_ALT1]       = { "u3_alt1",       rsprot_p3_alt1,          rsprot_g3_alt1,          3, 24, 0 },
    [RSPROT_PRIM_U3_ALT2]       = { "u3_alt2",       rsprot_p3_alt2,          rsprot_g3_alt2,          3, 24, 0 },
    [RSPROT_PRIM_U3_ALT3]       = { "u3_alt3",       rsprot_p3_alt3,          rsprot_g3_alt3,          3, 24, 0 },
    [RSPROT_PRIM_U4]            = { "u4",            rsprot_p4,               rsprot_g4,               4, 32, 1 },
    [RSPROT_PRIM_U4_ALT1]       = { "u4_alt1",       rsprot_p4_alt1,          rsprot_g4_alt1,          4, 32, 1 },
    [RSPROT_PRIM_U4_ALT2]       = { "u4_alt2",       rsprot_p4_alt2,          rsprot_g4_alt2,          4, 32, 1 },
    [RSPROT_PRIM_U4_ALT3]       = { "u4_alt3",       rsprot_p4_alt3,          rsprot_g4_alt3,          4, 32, 1 },
    [RSPROT_PRIM_COM]           = { "com",           rsprot_pcombined_id,     rsprot_gcombined_id,     4, 32, 1 },
    [RSPROT_PRIM_COM_ALT1]      = { "com_alt1",      rsprot_pcombined_id_alt1,rsprot_gcombined_id_alt1,4, 32, 1 },
    [RSPROT_PRIM_COM_ALT2]      = { "com_alt2",      rsprot_pcombined_id_alt2,rsprot_gcombined_id_alt2,4, 32, 1 },
    [RSPROT_PRIM_COM_ALT3]      = { "com_alt3",      rsprot_pcombined_id_alt3,rsprot_gcombined_id_alt3,4, 32, 1 },
    [RSPROT_PRIM_SMART]         = { "smart",         rsprot_psmart1or2,       rsprot_gsmart1or2,       0, 15, 0 },
    [RSPROT_PRIM_SMARTNULL]     = { "smartnull",     rsprot_psmart1or2null,   rsprot_gsmart1or2null,   0, 14, 0 },
    [RSPROT_PRIM_SMART2OR4NULL] = { "smart2or4null", rsprot_psmart2or4null,   rsprot_gsmart2or4null,   0, 14, 0 },
    [RSPROT_PRIM_VARINT2]       = { "varint2",       rsprot_pvarint2,         rsprot_gvarint2,         0, 20, 0 },
    [RSPROT_PRIM_VARINT2S]      = { "varint2s",      rsprot_pvarint2s,        rsprot_gvarint2s,        0, 20, 1 },
    [RSPROT_PRIM_BOOL]          = { "bool",          put_boolean,             get_boolean,             1, 1,  0 },
};
/* clang-format on */

const char *
rsprot_prim_name(RsprotPrim prim)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT)
        return NULL;
    return k_prim[prim].name;
}

int
rsprot_prim_width(RsprotPrim prim)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT)
        return 0;
    return k_prim[prim].width;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void
exec_init(RsprotExec *x, RsprotExecDir dir)
{
    memset(x, 0, sizeof(*x));
    x->dir = dir;
}

void
rsprot_exec_encode(RsprotExec *x, RsprotBuf *buf)
{
    exec_init(x, RSPROT_ENCODE);
    x->buf = buf;
}

void
rsprot_exec_decode(RsprotExec *x, RsprotBuf *buf)
{
    exec_init(x, RSPROT_DECODE);
    x->buf = buf;
}

void
rsprot_exec_describe(RsprotExec *x, RsprotSchema *schema)
{
    exec_init(x, RSPROT_DESCRIBE);
    memset(schema, 0, sizeof(*schema));
    x->schema = schema;
}

void
rsprot_exec_fill(RsprotExec *x, uint32_t seed)
{
    exec_init(x, RSPROT_FILL);
    /* Any odd seed works; 0 would make the LCG a constant zero, filling every
     * field with the same value and hiding a transposition. */
    x->rng = seed | 1u;
}

void
rsprot_exec_fail(RsprotExec *x, const char *reason)
{
    if (x->error)
        return;
    x->error = 1;
    x->error_reason = reason;
}

/* ------------------------------------------------------------------ */
/* FILL                                                                */
/* ------------------------------------------------------------------ */

static uint32_t
next_rand(RsprotExec *x)
{
    /* Numerical Recipes LCG. Deterministic and seedable is the whole
     * requirement — a failing round trip must be reproducible from its seed
     * alone, or the test is a flake report rather than a bug report. */
    x->rng = x->rng * 1664525u + 1013904223u;
    return x->rng;
}

/**
 * A value that survives its own round trip through `prim`.
 *
 * Masking to the primitive's width is what makes that true: `u1` must stay
 * inside a byte or the reader returns something else, and the smart forms must
 * stay inside their range because the buffer latches RSPROT_BUF_ERR_RANGE
 * rather than truncating. A fill ignoring either would report a spurious
 * failure and teach us to distrust the test.
 */
static int32_t
fill_value(RsprotExec *x, RsprotPrim prim)
{
    const RsprotPrimDef *d = &k_prim[prim];
    uint32_t r = next_rand(x);

    if (d->bits >= 32)
        return (int32_t)r;

    int32_t v = (int32_t)(r & ((1u << d->bits) - 1u));
    if (d->is_signed)
        v -= 1 << (d->bits - 1);
    return v;
}

/* ------------------------------------------------------------------ */
/* Schema recording                                                    */
/* ------------------------------------------------------------------ */

/** Bytes a recorded field occupies, or -1 when that is not a fixed number. */
static int
field_width(const RsprotField *f)
{
    if (f->kind != RSPROT_FIELD_PRIM)
        return -1;
    int w = k_prim[f->prim].width;
    return w > 0 ? w : -1;
}

static void
record(RsprotExec *x, const char *name, RsprotFieldKind kind, RsprotPrim prim, int bit_width)
{
    RsprotSchema *s = x->schema;
    if (!s)
        return;
    if (s->count >= RSPROT_SCHEMA_MAX) {
        s->truncated = 1;
        return;
    }
    RsprotField *f = &s->fields[s->count++];
    f->name = name;
    f->kind = kind;
    f->prim = prim;
    f->bit_width = bit_width;

    /*
     * The offset is the sum of every preceding field's width, and is unknown
     * the moment any one of them is. That propagation is the point: a
     * variable-width field early in a packet makes every later offset
     * unknowable, and reporting one anyway is how a schema starts lying.
     */
    f->offset = -1;
    if (kind == RSPROT_FIELD_PRIM && field_width(f) > 0) {
        int at = 0;
        for (int i = 0; i < s->count - 1; i++) {
            int w = field_width(&s->fields[i]);
            if (s->fields[i].offset < 0 || w < 0) {
                at = -1;
                break;
            }
            at += w;
        }
        f->offset = at;
    }
}

/* ------------------------------------------------------------------ */
/* Transfer                                                            */
/* ------------------------------------------------------------------ */

void
rsprot_x(RsprotExec *x, RsprotPrim prim, int32_t *value, const char *name)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT) {
        rsprot_exec_fail(x, "unknown primitive");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        k_prim[prim].put(x->buf, *value);
        break;
    case RSPROT_DECODE:
        *value = k_prim[prim].get(x->buf);
        break;
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_PRIM, prim, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        *value = fill_value(x, prim);
        break;
    }
}

void
rsprot_xconst(RsprotExec *x, RsprotPrim prim, int32_t value, const char *name)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT) {
        rsprot_exec_fail(x, "unknown primitive");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        k_prim[prim].put(x->buf, value);
        break;
    case RSPROT_DECODE: {
        /*
         * Verify, do not skip. A fixed byte is the cheapest sync check a
         * payload carries: if it disagrees, the sender is speaking a layout
         * this codec is not, and every field after it is being read at the
         * wrong offset. Failing on the constant reports that at the byte where
         * it became true, instead of as a nonsense value several fields later.
         */
        int32_t got = k_prim[prim].get(x->buf);
        if (got != value)
            rsprot_exec_fail(x, "fixed field has the wrong value");
        break;
    }
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_PRIM, prim, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        /* Nothing to randomise, and nothing to write: FILL populates a struct,
         * and this value lives in the codec rather than the struct. The
         * encode that follows a fill emits it. */
        break;
    }
}

int32_t
rsprot_remaining(RsprotExec *x)
{
    if (x->dir != RSPROT_DECODE || !x->buf)
        return 0;
    return x->buf->cap - x->buf->rpos;
}

int
rsprot_more(RsprotExec *x, int32_t *count, int32_t i, int32_t cap)
{
    switch (x->dir) {
    case RSPROT_ENCODE:
    case RSPROT_FILL:
        return i < *count;

    case RSPROT_DECODE:
        if (x->error || x->buf->err)
            return 0;
        if (rsprot_remaining(x) <= 0) {
            /* The array ended because the payload did. Tell the caller how
             * many arrived — nothing else on the wire says. */
            *count = i;
            return 0;
        }
        if (i >= cap) {
            /*
             * More elements than the caller's buffer holds. Fail rather than
             * stop quietly: stopping would report a short array that framed
             * cleanly, and the bytes left over would be read as the next
             * packet.
             */
            rsprot_exec_fail(x, "count-less array exceeds the caller's capacity");
            *count = i;
            return 0;
        }
        return 1;

    case RSPROT_DESCRIBE:
        /* One element: enough to show its shape, without claiming a length the
         * layout does not carry. */
        return i < 1;
    }
    return 0;
}

void
rsprot_xform_add(
    RsprotExec *x, RsprotPrim prim, int32_t *value, int32_t delta, const char *name)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT) {
        rsprot_exec_fail(x, "unknown primitive");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        k_prim[prim].put(x->buf, *value + delta);
        break;
    case RSPROT_DECODE:
        *value = k_prim[prim].get(x->buf) - delta;
        break;
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_PRIM, prim, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        /*
         * Leave room for the offset. A filled value at the top of the
         * primitive's range plus a positive delta would wrap on encode and the
         * round trip would fail for a reason that has nothing to do with the
         * codec being wrong.
         */
        *value = fill_value(x, prim);
        if (delta > 0 && *value > 0)
            *value -= delta;
        else if (delta < 0)
            *value -= delta;
        break;
    }
}

void
rsprot_xclamp(
    RsprotExec *x, RsprotPrim prim, int32_t *value, int32_t *source, int32_t cap,
    const char *name)
{
    if (prim < 0 || prim >= RSPROT_PRIM_COUNT) {
        rsprot_exec_fail(x, "unknown primitive");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        /*
         * Recompute rather than trust `*value`. It is a derived field, so a
         * caller that set it inconsistently with `*source` would otherwise put
         * a length on the wire that disagrees with the payload — and storing it
         * back is what lets the codec's own following branch test the byte that
         * actually went out.
         */
        *value = (*source < cap) ? *source : cap;
        k_prim[prim].put(x->buf, *value);
        break;
    case RSPROT_DECODE:
        *value = k_prim[prim].get(x->buf);
        /* Below the cap the short field IS the value; at the cap an escape
         * field follows and overwrites this. */
        if (*value < cap)
            *source = *value;
        break;
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_PRIM, prim, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        *source = fill_value(x, prim);
        *value = (*source < cap) ? *source : cap;
        break;
    }
}

void
rsprot_x8(RsprotExec *x, int64_t *value, const char *name)
{
    switch (x->dir) {
    case RSPROT_ENCODE:
        rsprot_p8(x->buf, *value);
        break;
    case RSPROT_DECODE:
        *value = rsprot_g8(x->buf);
        break;
    case RSPROT_DESCRIBE:
        /* Two u4 rows rather than one u8 row: the schema is a byte layout, and
         * "8 bytes big-endian" is exactly what two u4s say. */
        record(x, name, RSPROT_FIELD_PRIM, RSPROT_PRIM_U4, 0);
        record(x, name, RSPROT_FIELD_PRIM, RSPROT_PRIM_U4, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        *value = ((int64_t)(int32_t)next_rand(x) << 32) | (uint32_t)next_rand(x);
        break;
    }
}

/*
 * The fixed text FILL puts in every string field.
 *
 * Deliberately carries a non-ASCII Cp1252 byte and no terminator character, so
 * a writer that mishandles either fails rather than passing on plain ASCII.
 * 0xA9 is inside the 0x80..0x9F-adjacent range where Cp1252 and UTF-8 disagree,
 * which rsprot_buf.h calls out as the interesting region.
 */
static const char k_fill_string[] = "rsprot\xA9";

void
rsprot_xstr(RsprotExec *x, const char **value, RsprotStrKind kind, const char *name)
{
    switch (x->dir) {
    case RSPROT_ENCODE:
        switch (kind) {
        case RSPROT_STR_PLAIN:
            rsprot_pjstr(x->buf, *value ? *value : "");
            break;
        case RSPROT_STR_NULLABLE:
            /* NULL is meaningful here — "absent", not "empty" — so it is passed
             * through rather than defaulted. */
            rsprot_pjstrnull(x->buf, *value);
            break;
        case RSPROT_STR_PREFIXED:
            rsprot_pjstr2(x->buf, *value ? *value : "");
            break;
        }
        break;
    case RSPROT_DECODE: {
        /*
         * Borrowed, not copied — rsprot_buf.h's contract. The pointer is valid
         * only while the buffer is, so a Task that stores a string copies it.
         */
        int32_t len = 0;
        const char *s = NULL;
        switch (kind) {
        case RSPROT_STR_PLAIN:
            s = rsprot_gjstr(x->buf, &len);
            break;
        case RSPROT_STR_NULLABLE:
            s = rsprot_gjstrnull(x->buf, &len);
            break;
        case RSPROT_STR_PREFIXED:
            s = rsprot_gjstr2(x->buf, &len);
            break;
        }
        /* gjstrnull legitimately returns NULL for "absent" and sets no error;
         * the other two return NULL only on a malformed string. Distinguishing
         * them is what rsprot_buf_ok is for. */
        if (!s && kind != RSPROT_STR_NULLABLE)
            rsprot_exec_fail(x, "malformed string");
        *value = s;
        break;
    }
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_STR, RSPROT_PRIM_U1, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        *value = k_fill_string;
        break;
    }
}

int32_t
rsprot_xcount(RsprotExec *x, RsprotPrim prim, int32_t *value, int32_t max, const char *name)
{
    if (x->dir == RSPROT_FILL) {
        /* Clamped before the transfer, not after, so the value the caller loops
         * on and the value that reaches the wire are the same number. A fill
         * that wrote an unclamped count would encode a length the decode side
         * then rejects, and the round-trip test would blame the codec. */
        *value = (int32_t)(next_rand(x) % (uint32_t)(max > 0 ? max : 1));
        return *value;
    }

    rsprot_x(x, prim, value, name);

    if (x->dir == RSPROT_DECODE && (*value < 0 || *value > max)) {
        rsprot_exec_fail(x, "count out of range");
        *value = 0;
        return 0;
    }
    return *value;
}

void
rsprot_xdata(
    RsprotExec *x, const uint8_t **value, int32_t length, RsprotDataOrder order, const char *name)
{
    if (length < 0) {
        rsprot_exec_fail(x, "negative blob length");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        switch (order) {
        case RSPROT_DATA_PLAIN:
            rsprot_pdata(x->buf, *value, length);
            break;
        case RSPROT_DATA_ALT1:
            rsprot_pdata_alt1(x->buf, *value, length);
            break;
        case RSPROT_DATA_ALT2:
            rsprot_pdata_alt2(x->buf, *value, length);
            break;
        }
        break;
    case RSPROT_DECODE:
        /*
         * Borrowed like strings, and for the same reason. The entity streams
         * already work exactly this way — they take the raw stream once and
         * decode it later — so this is the supported mode, not a fallback.
         */
        if (rsprot_buf_readable(x->buf) < length) {
            rsprot_exec_fail(x, "blob runs past the payload");
            *value = NULL;
            return;
        }
        *value = x->buf->data + x->buf->rpos;
        x->buf->rpos += length;
        break;
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_DATA, RSPROT_PRIM_U1, 0);
        x->transferred++;
        break;
    case RSPROT_FILL:
        /* Left alone: the caller owns the storage, and inventing a pointer here
         * would make the round-trip test compare against memory this file
         * allocated and nobody frees. */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Bit mode                                                            */
/* ------------------------------------------------------------------ */

void
rsprot_xbits_begin(RsprotExec *x)
{
    if (x->dir == RSPROT_ENCODE || x->dir == RSPROT_DECODE)
        rsprot_buf_bit_begin(x->buf);
}

void
rsprot_xbits_end(RsprotExec *x)
{
    if (x->dir == RSPROT_ENCODE || x->dir == RSPROT_DECODE)
        rsprot_buf_bit_end(x->buf);
}

void
rsprot_xbits(RsprotExec *x, int width, int32_t *value, const char *name)
{
    if (width <= 0 || width > 32) {
        rsprot_exec_fail(x, "bit width out of range");
        return;
    }

    switch (x->dir) {
    case RSPROT_ENCODE:
        rsprot_pbits(x->buf, width, *value);
        break;
    case RSPROT_DECODE:
        *value = rsprot_gbits(x->buf, width);
        break;
    case RSPROT_DESCRIBE:
        record(x, name, RSPROT_FIELD_BITS, RSPROT_PRIM_U1, width);
        x->transferred++;
        break;
    case RSPROT_FILL:
        *value = (int32_t)(next_rand(x) & (width == 32 ? 0xffffffffu : ((1u << width) - 1u)));
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Branching                                                           */
/* ------------------------------------------------------------------ */

int32_t
rsprot_branch(RsprotExec *x, const int32_t *value)
{
    if (x->dir != RSPROT_DESCRIBE)
        return *value;

    /*
     * The enforcement point for rsprot_exec.h's one rule.
     *
     * In DESCRIBE nothing has been transferred into the message, so `*value` is
     * whatever the caller's struct happened to hold. Returning it would let a
     * codec take a branch here that it would not take while decoding — and
     * decoding is exactly where branching on an unread field is wrong.
     * Returning 0 makes DESCRIBE walk the packet's first arm consistently, and
     * `transferred` says whether the codec had any business asking.
     *
     * A codec that reads a field before transferring it is not a style problem:
     * it encodes correctly and decodes garbage with no frame-level symptom.
     * That is the failure this subsystem exists to make impossible, so it is an
     * error rather than a warning.
     */
    if (x->transferred == 0)
        rsprot_exec_fail(x, "branched on a field before transferring it");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Versioning                                                          */
/* ------------------------------------------------------------------ */

RsprotCodecFn
rsprot_version_pick(const RsprotVersionRange *ranges, int count, int revision)
{
    for (int i = 0; i < count; i++) {
        if (revision >= ranges[i].rev_lo && revision <= ranges[i].rev_hi)
            return ranges[i].fn;
    }
    /* Not an error: "this revision has no such packet" is a real answer, and
     * the caller reports it once rather than writing a wrong layout. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Schema reporting                                                    */
/* ------------------------------------------------------------------ */

void
rsprot_schema_print(const RsprotSchema *schema, const char *title)
{
    printf("%s: %d field%s%s\n",
           title ? title : "packet",
           schema->count,
           schema->count == 1 ? "" : "s",
           schema->truncated ? " (TRUNCATED)" : "");
    for (int i = 0; i < schema->count; i++) {
        const RsprotField *f = &schema->fields[i];
        char at[16];
        if (f->offset >= 0)
            snprintf(at, sizeof(at), "@%d", f->offset);
        else
            snprintf(at, sizeof(at), "@?");

        switch (f->kind) {
        case RSPROT_FIELD_BITS:
            printf("  %3d  %-14s %2d bits  %-6s %s\n", i, "bits", f->bit_width, at, f->name);
            break;
        case RSPROT_FIELD_STR:
            printf("  %3d  %-14s %8s  %-6s %s\n", i, "str", "", at, f->name);
            break;
        case RSPROT_FIELD_DATA:
            printf("  %3d  %-14s %8s  %-6s %s\n", i, "data", "", at, f->name);
            break;
        case RSPROT_FIELD_PRIM:
            printf("  %3d  %-14s %8s  %-6s %s\n", i, k_prim[f->prim].name, "", at, f->name);
            break;
        }
    }
}

int
rsprot_schema_fixed_size(const RsprotSchema *schema)
{
    int total = 0;
    if (schema->truncated)
        return -1;
    for (int i = 0; i < schema->count; i++) {
        int w = field_width(&schema->fields[i]);
        if (w < 0)
            return -1;
        total += w;
    }
    return total;
}
