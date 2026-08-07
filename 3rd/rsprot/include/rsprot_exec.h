#ifndef RSPROT_EXEC_H
#define RSPROT_EXEC_H

/*
 * rsprot_exec — the vocabulary a packet codec speaks.
 *
 * **This header declares; it does not implement.** The implementation lives on
 * the server project side (`src/net/exec/pkt_exec.c`). That split is the point:
 * `3rd/rsprot` states *what the bytes are* — one file per packet, every layout
 * version colocated — and the project that links it decides *what to do about
 * them*. A library that also owned the doing would have to know about game
 * ticks, arenas and Tasks, none of which are protocol.
 *
 * ## What a codec is
 *
 * An ordinary C function that names its fields in order and does not know which
 * direction it is running in:
 *
 *     void
 *     packet_if_opensub_v13_out(RsprotExec* x, MsgIfOpenSub* m)
 *     {
 *         RSPROT_U2_ALT3(x, m->interface_id);
 *         RSPROT_COM_ALT3(x, m->dest_uid);
 *         RSPROT_U1(x, m->type);
 *     }
 *
 * The same function encodes, decodes, describes itself and fills itself. So the
 * server's writer and the client's reader are literally one function and cannot
 * drift — which is the failure this exists to remove:
 * `docs/RSPROT_OSRS239_PORT.md` §5a records three of the first six
 * hand-transcribed writers being wrong, caught only by someone checking bytes.
 *
 * The names deliberately mirror JagByteBuf's, minus the direction: Kotlin's
 * `buffer.p2Alt3(message.interfaceId)` reads as `RSPROT_U2_ALT3(x, m->…)`. A
 * codec transcribed from RSProt should read line-for-line against its original,
 * which is the same argument `rsprot_buf.h` makes for keeping `g`/`p`/`Alt`.
 *
 * ## The one rule
 *
 * **A codec may branch only on a field it has already transferred.**
 *
 * Decoding learns values in wire order, so a predicate over a field that has
 * not been read yet works while encoding and reads uninitialised memory while
 * decoding — a packet that encodes correctly and decodes garbage, with nothing
 * wrong at the frame level. Use `RSPROT_BRANCH`, which enforces it.
 *
 * Native `if`/`for`/`switch` are otherwise fully available and are the reason
 * this is C functions rather than a bytecode: a tagged-union payload is a
 * `switch`, and that construct is what blocks most of the code generator's
 * current skips.
 *
 * ## Four directions
 *
 *   RSPROT_ENCODE     fields -> bytes
 *   RSPROT_DECODE     bytes  -> fields
 *   RSPROT_DESCRIBE   records the field list; moves no bytes
 *   RSPROT_FILL       deterministic in-range values into every field
 *
 * The last two are what a data-driven design would have got for free by being
 * data, and are why this one does not need to be. DESCRIBE makes a codec emit
 * its own schema — a field-by-field trace beside the hex, and a diff of one
 * packet across two versions. FILL is the front half of the round-trip test, so
 * there is no per-packet test code to write or to forget.
 *
 * ## Strings borrow
 *
 * Decoding hands back a pointer into the payload, valid only while the buffer
 * is — `rsprot_buf.h`'s stated contract ("a copy per string per tick is a cost
 * with no payer"). A caller that stores a string copies it.
 */

#include "../src/rsprot_buf.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

/*
 * The scalar wire vocabulary, as one list: X(NAME, lower, bits, signed).
 *
 * `bits` is the value width the encoding preserves, which is what FILL masks to
 * so a generated value survives its own round trip.
 *
 * **This set is measured, not aspirational.** It is every `buffer.p*`/`g*` call
 * appearing in RSProt's complete revision-239 desktop encoder and decoder set —
 * all 194 encoders and 95 decoders, not just the ones that generate cleanly
 * today. Notably absent because nothing at 239 calls them: `pSmart1or2extended`,
 * `pMidiVarLen`, `pType`. Adding a row when a revision needs one is two lines;
 * a row nobody calls is a row nobody has checked.
 *
 * Also absent, and deliberately: `pModel`, `pIdentKit`, `pRecolour`, `pFlag`,
 * `pBodyType` and friends. Those are not buffer primitives — they are helper
 * methods on RSProt's appearance encoder, and they belong to the appearance
 * *stream reader* rather than here. Their absence from this list is what says
 * the packet/stream split falls where RSProt already put it.
 */
#define RSPROT_PRIM_LIST(X)                                                                        \
    X(U1, u1, 8, 0)                                                                                \
    X(U1_ALT1, u1_alt1, 8, 0)                                                                      \
    X(U1_ALT2, u1_alt2, 8, 0)                                                                      \
    X(U1_ALT3, u1_alt3, 8, 0)                                                                      \
    X(I1, i1, 8, 1)                                                                                \
    X(U2, u2, 16, 0)                                                                               \
    X(U2_ALT1, u2_alt1, 16, 0)                                                                     \
    X(U2_ALT2, u2_alt2, 16, 0)                                                                     \
    X(U2_ALT3, u2_alt3, 16, 0)                                                                     \
    X(I2, i2, 16, 1)                                                                               \
    X(U3, u3, 24, 0)                                                                               \
    X(U3_ALT1, u3_alt1, 24, 0)                                                                     \
    X(U3_ALT2, u3_alt2, 24, 0)                                                                     \
    X(U3_ALT3, u3_alt3, 24, 0)                                                                     \
    X(U4, u4, 32, 1)                                                                               \
    X(U4_ALT1, u4_alt1, 32, 1)                                                                     \
    X(U4_ALT2, u4_alt2, 32, 1)                                                                     \
    X(U4_ALT3, u4_alt3, 32, 1)                                                                     \
    /* A packed (interface << 16) | child uid. Byte-identical to U4 by                             \
     * construction — RSProt's pCombinedId IS p4 — but a distinct row so a                         \
     * DESCRIBE dump says which of the two a field is. Not cosmetic: rev 230                       \
     * addresses a component with a bare 2-byte id and 239 with this, and the                      \
     * inbound translation this design replaces truncates one to the other. */                     \
    X(COM, com, 32, 1)                                                                             \
    X(COM_ALT1, com_alt1, 32, 1)                                                                   \
    X(COM_ALT2, com_alt2, 32, 1)                                                                   \
    X(COM_ALT3, com_alt3, 32, 1)                                                                   \
    /* Variable width. SMART is 1 byte over [0,127] and 2 over [0,32767];                          \
     * SMARTNULL adds a -1 encoding; SMART2OR4NULL is the wide form. FILL                          \
     * respects those ranges — the buffer latches a range error rather than                        \
     * truncating out of them. */                                                                  \
    X(SMART, smart, 15, 0)                                                                         \
    X(SMARTNULL, smartnull, 14, 0)                                                                 \
    X(SMART2OR4NULL, smart2or4null, 14, 0)                                                         \
    X(VARINT2, varint2, 20, 0)                                                                     \
    X(VARINT2S, varint2s, 20, 1)                                                                   \
    /* One byte, 0 or 1. */                                                                        \
    X(BOOL, boolean, 1, 0)

typedef enum RsprotPrim
{
#define RSPROT_PRIM_ENUM(NAME, lower, bits, sign) RSPROT_PRIM_##NAME,
    RSPROT_PRIM_LIST(RSPROT_PRIM_ENUM)
#undef RSPROT_PRIM_ENUM
        RSPROT_PRIM_COUNT
} RsprotPrim;

/** "u2_alt2" etc., for traces and schema dumps. NULL on a bad index. */
const char *rsprot_prim_name(RsprotPrim prim);

/** Bytes this primitive always occupies, or 0 when the width depends on the
 *  value (the smart/varint forms). */
int rsprot_prim_width(RsprotPrim prim);

/* ------------------------------------------------------------------ */
/* Strings and blobs                                                   */
/* ------------------------------------------------------------------ */

typedef enum RsprotStrKind
{
    /** pjstr / gjstr — NUL-terminated. */
    RSPROT_STR_PLAIN = 0,
    /** pjstrnull / gjstrnull — a lone 0 byte means "absent", not "empty". */
    RSPROT_STR_NULLABLE,
    /** pjstr2 / gjstr2 — a leading 0 byte, then the string. */
    RSPROT_STR_PREFIXED
} RsprotStrKind;

typedef enum RsprotDataOrder
{
    RSPROT_DATA_PLAIN = 0,
    RSPROT_DATA_ALT1,
    RSPROT_DATA_ALT2
} RsprotDataOrder;

/* ------------------------------------------------------------------ */
/* Direction and schema                                                */
/* ------------------------------------------------------------------ */

typedef enum RsprotExecDir
{
    RSPROT_ENCODE = 0,
    RSPROT_DECODE,
    RSPROT_DESCRIBE,
    RSPROT_FILL
} RsprotExecDir;

typedef enum RsprotFieldKind
{
    RSPROT_FIELD_PRIM = 0,
    RSPROT_FIELD_BITS,
    RSPROT_FIELD_STR,
    RSPROT_FIELD_DATA
} RsprotFieldKind;

typedef struct RsprotField
{
    const char *name;
    RsprotFieldKind kind;
    RsprotPrim prim; /* RSPROT_FIELD_PRIM only */
    /**
     * Byte offset from the start of the payload, or **-1 when unknown** —
     * inside a bit section, at a variable-width field, or anywhere after one.
     * Unknown rather than approximate: an offset that is merely plausible is a
     * number someone would later trust.
     */
    int offset;
    int bit_width; /* RSPROT_FIELD_BITS only */
} RsprotField;

#define RSPROT_SCHEMA_MAX 192

/**
 * What a codec says about itself under RSPROT_DESCRIBE.
 *
 * `truncated` rather than a silent stop: a schema that quietly ended early
 * would read as a shorter packet, which is the class of lie this whole design
 * removes.
 */
typedef struct RsprotSchema
{
    RsprotField fields[RSPROT_SCHEMA_MAX];
    int count;
    int truncated;
} RsprotSchema;

/* ------------------------------------------------------------------ */
/* The executor                                                        */
/* ------------------------------------------------------------------ */

typedef struct RsprotExec
{
    RsprotExecDir dir;

    /** ENCODE/DECODE only; NULL in DESCRIBE and FILL. */
    RsprotBuf *buf;

    /** DESCRIBE only. */
    RsprotSchema *schema;

    /**
     * Sticky failure. Set by an out-of-range count, a malformed string, or the
     * branch rule. Never cleared by a later successful call — a codec that went
     * wrong halfway has produced a wrong packet whatever it does after.
     */
    int error;
    /** First error's reason. NULL until `error` is set. */
    const char *error_reason;

    /** FILL: LCG state, seeded so a failing case is reproducible from its seed
     *  alone. */
    uint32_t rng;

    /** DESCRIBE: fields transferred so far, so the branch rule can tell whether
     *  a codec had any business consulting one. */
    int transferred;
} RsprotExec;

/** The shape every generated codec has. `msg` is that packet's message struct. */
typedef void (*RsprotCodecFn)(RsprotExec *x, void *msg);

/* --- lifecycle (implemented by the host project) ------------------------ */

void rsprot_exec_encode(RsprotExec *x, RsprotBuf *buf);
void rsprot_exec_decode(RsprotExec *x, RsprotBuf *buf);
void rsprot_exec_describe(RsprotExec *x, RsprotSchema *schema);
void rsprot_exec_fill(RsprotExec *x, uint32_t seed);

static inline int rsprot_exec_ok(const RsprotExec *x) { return x->error == 0; }

/** Latch a failure from inside a codec — a discriminator with no case, a field
 *  this version cannot represent. Keeps the first reason. */
void rsprot_exec_fail(RsprotExec *x, const char *reason);

/* --- transfer ------------------------------------------------------------ */

/*
 * Codecs call the RSPROT_* macros, never these directly. The macro supplies the
 * field's source-level name from `#field`, so DESCRIBE and the trace report
 * real names with no separate field-name table to fall out of step.
 */

void rsprot_x(RsprotExec *x, RsprotPrim prim, int32_t *value, const char *name);
void rsprot_x8(RsprotExec *x, int64_t *value, const char *name);
void rsprot_xstr(RsprotExec *x, const char **value, RsprotStrKind kind, const char *name);
void rsprot_xdata(
    RsprotExec *x, const uint8_t **value, int32_t length, RsprotDataOrder order, const char *name);

/**
 * A field whose value the layout FIXES — a format byte, a reserved zero, a
 * discriminator a version pins to one value.
 *
 * RSProt spells these as a literal argument (`buffer.p1(1)`), which has no
 * field behind it and so nothing for the decode direction to read into. A
 * generated codec cannot express it with `rsprot_x`, and the two obvious
 * workarounds are both wrong: giving it a struct field lets a caller set it to
 * something the layout does not allow, and skipping it on decode silently
 * accepts a payload that says something else.
 *
 * So it is its own operation, with a direction-dependent meaning that is the
 * honest one in each case:
 *
 *   ENCODE    writes `value`
 *   DECODE    reads, and FAILS the exec if the byte is not `value` — a wire
 *             that disagrees about a format byte is a framing error, and
 *             finding out here beats finding out five fields later
 *   DESCRIBE  records the field with its fixed value
 *   FILL      moves the cursor; there is nothing to randomise
 */
void rsprot_xconst(RsprotExec *x, RsprotPrim prim, int32_t value, const char *name);

/**
 * A length-prefixed count, transferred as `prim` and then bounded.
 *
 * Returns the count to loop on. A decoded count above `max` is an error and
 * returns 0, so a corrupt length cannot drive a loop — today every inbound
 * handler hand-checks this, and the ones that forget are the interesting ones.
 * FILL clamps to `max` too, so a fill/encode/decode cycle stays bounded.
 */
int32_t rsprot_xcount(
    RsprotExec *x, RsprotPrim prim, int32_t *value, int32_t max, const char *name);

/* Bit mode, for packed property bytes. Whole bit *streams* are not codecs —
 * see 3rd/rsprot/streams/ — but a packed byte inside a flat packet is. */
void rsprot_xbits_begin(RsprotExec *x);
void rsprot_xbits_end(RsprotExec *x);
void rsprot_xbits(RsprotExec *x, int width, int32_t *value, const char *name);

/**
 * Read a field a codec is about to branch on.
 *
 * `switch (RSPROT_BRANCH(x, m->kind))` rather than `switch (m->kind)`. In
 * ENCODE, DECODE and FILL this is just the value. In DESCRIBE it is the
 * enforcement point for the rule at the top of this header.
 */
int32_t rsprot_branch(RsprotExec *x, const int32_t *value);

/* --- schema reporting ---------------------------------------------------- */

/** One field per line — the disassembler a bytecode design would have had.
 *  Diffing two versions' output for one packet shows a moved field directly. */
void rsprot_schema_print(const RsprotSchema *schema, const char *title);

/** Total fixed byte width, or -1 when the codec has a variable-width field.
 *  Cross-checks a codec against its revision's `{opcode, size}` table. */
int rsprot_schema_fixed_size(const RsprotSchema *schema);

/* ------------------------------------------------------------------ */
/* Call macros                                                         */
/* ------------------------------------------------------------------ */

#define RSPROT_PRIM_DECL(NAME, lower, bits, sign)                                                  \
    static inline void rsprot_x_##lower(RsprotExec *x, int32_t *v, const char *n)                  \
    {                                                                                              \
        rsprot_x(x, RSPROT_PRIM_##NAME, v, n);                                                     \
    }
RSPROT_PRIM_LIST(RSPROT_PRIM_DECL)
#undef RSPROT_PRIM_DECL

#define RSPROT_U1(x, f) rsprot_x_u1((x), &(f), #f)
#define RSPROT_U1_ALT1(x, f) rsprot_x_u1_alt1((x), &(f), #f)
#define RSPROT_U1_ALT2(x, f) rsprot_x_u1_alt2((x), &(f), #f)
#define RSPROT_U1_ALT3(x, f) rsprot_x_u1_alt3((x), &(f), #f)
#define RSPROT_I1(x, f) rsprot_x_i1((x), &(f), #f)
#define RSPROT_U2(x, f) rsprot_x_u2((x), &(f), #f)
#define RSPROT_U2_ALT1(x, f) rsprot_x_u2_alt1((x), &(f), #f)
#define RSPROT_U2_ALT2(x, f) rsprot_x_u2_alt2((x), &(f), #f)
#define RSPROT_U2_ALT3(x, f) rsprot_x_u2_alt3((x), &(f), #f)
#define RSPROT_I2(x, f) rsprot_x_i2((x), &(f), #f)
#define RSPROT_U3(x, f) rsprot_x_u3((x), &(f), #f)
#define RSPROT_U3_ALT1(x, f) rsprot_x_u3_alt1((x), &(f), #f)
#define RSPROT_U3_ALT2(x, f) rsprot_x_u3_alt2((x), &(f), #f)
#define RSPROT_U3_ALT3(x, f) rsprot_x_u3_alt3((x), &(f), #f)
#define RSPROT_U4(x, f) rsprot_x_u4((x), &(f), #f)
#define RSPROT_U4_ALT1(x, f) rsprot_x_u4_alt1((x), &(f), #f)
#define RSPROT_U4_ALT2(x, f) rsprot_x_u4_alt2((x), &(f), #f)
#define RSPROT_U4_ALT3(x, f) rsprot_x_u4_alt3((x), &(f), #f)
#define RSPROT_COM(x, f) rsprot_x_com((x), &(f), #f)
#define RSPROT_COM_ALT1(x, f) rsprot_x_com_alt1((x), &(f), #f)
#define RSPROT_COM_ALT2(x, f) rsprot_x_com_alt2((x), &(f), #f)
#define RSPROT_COM_ALT3(x, f) rsprot_x_com_alt3((x), &(f), #f)
#define RSPROT_SMART(x, f) rsprot_x_smart((x), &(f), #f)
#define RSPROT_SMARTNULL(x, f) rsprot_x_smartnull((x), &(f), #f)
#define RSPROT_SMART2OR4NULL(x, f) rsprot_x_smart2or4null((x), &(f), #f)
#define RSPROT_VARINT2(x, f) rsprot_x_varint2((x), &(f), #f)
#define RSPROT_VARINT2S(x, f) rsprot_x_varint2s((x), &(f), #f)
#define RSPROT_BOOL(x, f) rsprot_x_boolean((x), &(f), #f)

#define RSPROT_U8(x, f) rsprot_x8((x), &(f), #f)
#define RSPROT_JSTR(x, f) rsprot_xstr((x), &(f), RSPROT_STR_PLAIN, #f)
#define RSPROT_JSTRNULL(x, f) rsprot_xstr((x), &(f), RSPROT_STR_NULLABLE, #f)
#define RSPROT_JSTR2(x, f) rsprot_xstr((x), &(f), RSPROT_STR_PREFIXED, #f)
#define RSPROT_DATA(x, f, len) rsprot_xdata((x), &(f), (len), RSPROT_DATA_PLAIN, #f)
#define RSPROT_DATA_ALT1(x, f, len) rsprot_xdata((x), &(f), (len), RSPROT_DATA_ALT1, #f)
#define RSPROT_DATA_ALT2(x, f, len) rsprot_xdata((x), &(f), (len), RSPROT_DATA_ALT2, #f)
/* A fixed value the layout pins. The name is the literal itself, since there is
 * no field to take one from: RSPROT_CONST(x, U1, 1) describes as "=1". */
#define RSPROT_CONST(x, prim, v) rsprot_xconst((x), RSPROT_PRIM_##prim, (v), "=" #v)
#define RSPROT_COUNT(x, f, max) rsprot_xcount((x), RSPROT_PRIM_U1, &(f), (max), #f)
#define RSPROT_COUNT2(x, f, max) rsprot_xcount((x), RSPROT_PRIM_U2, &(f), (max), #f)
#define RSPROT_BITS(x, width, f) rsprot_xbits((x), (width), &(f), #f)
#define RSPROT_BRANCH(x, f) rsprot_branch((x), &(f))

/* ------------------------------------------------------------------ */
/* Versioning                                                          */
/* ------------------------------------------------------------------ */

/**
 * Which layout version a revision speaks.
 *
 * A *version* is the identity of a layout, not its ordinal: version numbers are
 * assigned by content hash and recorded append-only in gen/packet_versions.txt,
 * because 31 packet classes have a layout that disappears and later reappears,
 * and a number that silently renumbers is a number that silently mismatches.
 *
 * Ranges rather than a row per revision because 207 of 238 packet classes have
 * contiguous version runs — most of these tables are two or three rows.
 */
typedef struct RsprotVersionRange
{
    int rev_lo;
    int rev_hi;
    RsprotCodecFn fn;
} RsprotVersionRange;

/** The codec for `revision`, or NULL when this revision has no such packet —
 *  a real answer, not an error. */
RsprotCodecFn rsprot_version_pick(const RsprotVersionRange *ranges, int count, int revision);

#endif
