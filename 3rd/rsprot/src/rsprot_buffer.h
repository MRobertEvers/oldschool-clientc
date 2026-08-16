#ifndef RSPROT_BUFFER_H
#define RSPROT_BUFFER_H

/*
 * RSProt_Buffer — the protocol buffer, with the byte order in the name.
 *
 * The library counterpart to `3rd/rscache/src/rsbuffer.h`'s RSCache_Buffer: one
 * place that owns byte access for the net stack, the way rscache owns it for
 * the cache. Before this existed, `src/net` had **40 accessor definitions and
 * every one was `static`**, spread over eight files — the same
 * variable-width "smart" encoding written five times under five names
 * (`gsmart1or2`, `c_gsmart`, `ext_psmart1or2`, `v5_psmart1or2`,
 * `w239_psmart1or2`), none checked against the others. See
 * `docs/BUFFER_ACCESSOR_AUDIT.md` for the full inventory and
 * `tools/audit_buffer_accessors.py` to re-run it.
 *
 * ## Why the names are what they are
 *
 * RSProt's Kotlin spells these `p4`, `p4Alt1`, `p4Alt2`, `p4Alt3`, and
 * `rsprot_buf.h` mirrors that spelling deliberately so a codec transcribed from
 * the Kotlin reads line-for-line against its original. That property is worth
 * keeping and this header does not take it away — `rsprot_buf.h` still has
 * those names, and they now forward here.
 *
 * But `Alt<n>` is an ordinal, not a description. Nothing in `p4Alt2` says which
 * four bytes come out, so checking a transcription means reading the
 * implementation every time. These names say it:
 *
 *     p4_be      1,2,3,4   (big endian)
 *     p4_le      4,3,2,1   (little endian)
 *     p4_3412    3,4,1,2
 *     p4_2143    2,1,4,3
 *
 * Digits are byte significance, 1 = most significant, in the order written.
 *
 * ## `Alt` is two concepts, and the names keep them apart
 *
 * This is the part that matters, and the reason the scheme is not purely
 * positional. At widths 3 and 4 `Alt` is a byte permutation. At width 1 there
 * is no permutation at all — it is a value transform:
 *
 *     p1          v
 *     p1_add128   v + 128
 *     p1_neg      -v
 *     p1_sub128   128 - v
 *
 * At width 2 it is **both at once**:
 *
 *     p2_be           [v>>8, v]           order 1,2
 *     p2_le           [v,    v>>8]        order 2,1
 *     p2_be_add128    [v>>8, v+128]       order 1,2, low byte +128
 *     p2_le_add128    [v+128, v>>8]       order 2,1, low byte +128
 *
 * A name carrying only the byte order would give `p2_be` and `p2_be_add128`
 * the same name for different bytes — precisely the failure this project has
 * already paid for once (`docs/RSPROT_OSRS239_PORT.md` §5a: a packet that
 * "frames perfectly, passes every length assert, and means something else").
 * So the transform is in the name too, and is omitted only when the value
 * passes through unchanged.
 *
 * Every name here was derived from the `put1()` sequence in `rsprot_buf.c`
 * rather than read off the Kotlin, so it can be re-checked mechanically
 * against the code it describes.
 *
 * ## Relationship to RsprotBuf
 *
 * `RSProt_Buffer` IS `RsprotBuf` — a typedef, not a copy. One cursor, one
 * error latch, one bit-mode implementation, two vocabularies over it. A second
 * implementation would be a second place for a smart-encoding edge case to be
 * wrong, which is the problem this header exists to end rather than to
 * relocate.
 *
 * Separate read and write cursors (`rpos`/`wpos`), unlike RSCache_Buffer's
 * single `position`: a packet buffer is written by one side and read by the
 * other, and the login path reads a header while writing a reply.
 */

#include "rsprot_buf.h"

typedef RsprotBuf RSProt_Buffer;

/* ------------------------------------------------------------------ */
/* Lifecycle — the same calls, renamed for this vocabulary             */
/* ------------------------------------------------------------------ */

/** Borrow caller memory for writing. `cap` is a hard capacity. */
static inline void RSProt_BufferWrap(RSProt_Buffer *b, void *data, int32_t cap)
{
	rsprot_buf_wrap(b, data, cap);
}

/** Borrow caller memory holding `len` bytes already, for reading. */
static inline void RSProt_BufferWrapRead(RSProt_Buffer *b, const void *data, int32_t len)
{
	rsprot_buf_wrap_read(b, data, len);
}

/** Owned mode: heap memory that grows as it is written. */
static inline int RSProt_BufferAlloc(RSProt_Buffer *b, int32_t cap)
{
	return rsprot_buf_alloc(b, cap);
}

static inline void RSProt_BufferFree(RSProt_Buffer *b) { rsprot_buf_free(b); }
static inline void RSProt_BufferClear(RSProt_Buffer *b) { rsprot_buf_clear(b); }

/** Sticky error bits. Non-zero means some earlier access ran off the end or
 *  out of range, and every value read since is suspect. */
static inline uint32_t RSProt_BufferErr(const RSProt_Buffer *b) { return b->err; }
static inline int RSProt_BufferOk(const RSProt_Buffer *b) { return b->err == 0; }

/* ------------------------------------------------------------------ */
/* Width 1 — no byte order; the variants are value transforms          */
/* ------------------------------------------------------------------ */

static inline int32_t RSProt_BufferG1(RSProt_Buffer *b) { return rsprot_g1(b); }
static inline int32_t RSProt_BufferG1_add128(RSProt_Buffer *b) { return rsprot_g1_alt1(b); }
static inline int32_t RSProt_BufferG1_neg(RSProt_Buffer *b) { return rsprot_g1_alt2(b); }
static inline int32_t RSProt_BufferG1_sub128(RSProt_Buffer *b) { return rsprot_g1_alt3(b); }

static inline void RSProt_BufferP1(RSProt_Buffer *b, int32_t v) { rsprot_p1(b, v); }
static inline void RSProt_BufferP1_add128(RSProt_Buffer *b, int32_t v) { rsprot_p1_alt1(b, v); }
static inline void RSProt_BufferP1_neg(RSProt_Buffer *b, int32_t v) { rsprot_p1_alt2(b, v); }
static inline void RSProt_BufferP1_sub128(RSProt_Buffer *b, int32_t v) { rsprot_p1_alt3(b, v); }

/** Signed reads. `s` for signed, matching rscache's `RSCache_BufferG1b`
 *  intent; the transform suffix means the same thing as above. */
static inline int32_t RSProt_BufferG1s(RSProt_Buffer *b) { return rsprot_g1s(b); }
static inline int32_t RSProt_BufferG1s_add128(RSProt_Buffer *b) { return rsprot_g1s_alt1(b); }
static inline int32_t RSProt_BufferG1s_neg(RSProt_Buffer *b) { return rsprot_g1s_alt2(b); }
static inline int32_t RSProt_BufferG1s_sub128(RSProt_Buffer *b) { return rsprot_g1s_alt3(b); }

/* ------------------------------------------------------------------ */
/* Width 2 — byte order AND, for two of them, a low-byte transform     */
/* ------------------------------------------------------------------ */

static inline int32_t RSProt_BufferG2Be(RSProt_Buffer *b) { return rsprot_g2(b); }
static inline int32_t RSProt_BufferG2Le(RSProt_Buffer *b) { return rsprot_g2_alt1(b); }
static inline int32_t RSProt_BufferG2Be_add128(RSProt_Buffer *b) { return rsprot_g2_alt2(b); }
static inline int32_t RSProt_BufferG2Le_add128(RSProt_Buffer *b) { return rsprot_g2_alt3(b); }

static inline void RSProt_BufferP2Be(RSProt_Buffer *b, int32_t v) { rsprot_p2(b, v); }
static inline void RSProt_BufferP2Le(RSProt_Buffer *b, int32_t v) { rsprot_p2_alt1(b, v); }
static inline void RSProt_BufferP2Be_add128(RSProt_Buffer *b, int32_t v) { rsprot_p2_alt2(b, v); }
static inline void RSProt_BufferP2Le_add128(RSProt_Buffer *b, int32_t v) { rsprot_p2_alt3(b, v); }

static inline int32_t RSProt_BufferG2sBe(RSProt_Buffer *b) { return rsprot_g2s(b); }
static inline int32_t RSProt_BufferG2sLe(RSProt_Buffer *b) { return rsprot_g2s_alt1(b); }
static inline int32_t RSProt_BufferG2sBe_add128(RSProt_Buffer *b) { return rsprot_g2s_alt2(b); }
static inline int32_t RSProt_BufferG2sLe_add128(RSProt_Buffer *b) { return rsprot_g2s_alt3(b); }

/* ------------------------------------------------------------------ */
/* Width 3 — pure byte permutation                                     */
/* ------------------------------------------------------------------ */

static inline int32_t RSProt_BufferG3Be(RSProt_Buffer *b) { return rsprot_g3(b); }
static inline int32_t RSProt_BufferG3Le(RSProt_Buffer *b) { return rsprot_g3_alt1(b); }
static inline int32_t RSProt_BufferG3_132(RSProt_Buffer *b) { return rsprot_g3_alt2(b); }
static inline int32_t RSProt_BufferG3_213(RSProt_Buffer *b) { return rsprot_g3_alt3(b); }

static inline void RSProt_BufferP3Be(RSProt_Buffer *b, int32_t v) { rsprot_p3(b, v); }
static inline void RSProt_BufferP3Le(RSProt_Buffer *b, int32_t v) { rsprot_p3_alt1(b, v); }
static inline void RSProt_BufferP3_132(RSProt_Buffer *b, int32_t v) { rsprot_p3_alt2(b, v); }
static inline void RSProt_BufferP3_213(RSProt_Buffer *b, int32_t v) { rsprot_p3_alt3(b, v); }

static inline int32_t RSProt_BufferG3sBe(RSProt_Buffer *b) { return rsprot_g3s(b); }
static inline int32_t RSProt_BufferG3sLe(RSProt_Buffer *b) { return rsprot_g3s_alt1(b); }
static inline int32_t RSProt_BufferG3s_132(RSProt_Buffer *b) { return rsprot_g3s_alt2(b); }
static inline int32_t RSProt_BufferG3s_213(RSProt_Buffer *b) { return rsprot_g3s_alt3(b); }

/* ------------------------------------------------------------------ */
/* Width 4 — pure byte permutation                                     */
/* ------------------------------------------------------------------ */

static inline int32_t RSProt_BufferG4Be(RSProt_Buffer *b) { return rsprot_g4(b); }
static inline int32_t RSProt_BufferG4Le(RSProt_Buffer *b) { return rsprot_g4_alt1(b); }
static inline int32_t RSProt_BufferG4_3412(RSProt_Buffer *b) { return rsprot_g4_alt2(b); }
static inline int32_t RSProt_BufferG4_2143(RSProt_Buffer *b) { return rsprot_g4_alt3(b); }

static inline void RSProt_BufferP4Be(RSProt_Buffer *b, int32_t v) { rsprot_p4(b, v); }
static inline void RSProt_BufferP4Le(RSProt_Buffer *b, int32_t v) { rsprot_p4_alt1(b, v); }
static inline void RSProt_BufferP4_3412(RSProt_Buffer *b, int32_t v) { rsprot_p4_alt2(b, v); }
static inline void RSProt_BufferP4_2143(RSProt_Buffer *b, int32_t v) { rsprot_p4_alt3(b, v); }

/*
 * A packed (interface << 16) | child uid. Byte-identical to the 4-byte forms by
 * construction — RSProt's pCombinedId IS p4 — but named separately so a call
 * site says which of the two it means. Not cosmetic: rev 230 addresses a
 * component with a bare 2-byte id and 239 with this.
 */
static inline int32_t RSProt_BufferGCombinedIdBe(RSProt_Buffer *b) { return rsprot_gcombined_id(b); }
static inline int32_t RSProt_BufferGCombinedIdLe(RSProt_Buffer *b) { return rsprot_gcombined_id_alt1(b); }
static inline int32_t RSProt_BufferGCombinedId_3412(RSProt_Buffer *b) { return rsprot_gcombined_id_alt2(b); }
static inline int32_t RSProt_BufferGCombinedId_2143(RSProt_Buffer *b) { return rsprot_gcombined_id_alt3(b); }

static inline void RSProt_BufferPCombinedIdBe(RSProt_Buffer *b, int32_t v) { rsprot_pcombined_id(b, v); }
static inline void RSProt_BufferPCombinedIdLe(RSProt_Buffer *b, int32_t v) { rsprot_pcombined_id_alt1(b, v); }
static inline void RSProt_BufferPCombinedId_3412(RSProt_Buffer *b, int32_t v) { rsprot_pcombined_id_alt2(b, v); }
static inline void RSProt_BufferPCombinedId_2143(RSProt_Buffer *b, int32_t v) { rsprot_pcombined_id_alt3(b, v); }

/* ------------------------------------------------------------------ */
/* Width 8 and IEEE — one order each, so no suffix beyond endianness   */
/* ------------------------------------------------------------------ */

static inline int64_t RSProt_BufferG8Be(RSProt_Buffer *b) { return rsprot_g8(b); }
static inline void RSProt_BufferP8Be(RSProt_Buffer *b, int64_t v) { rsprot_p8(b, v); }
static inline float RSProt_BufferG4f(RSProt_Buffer *b) { return rsprot_g4f(b); }
static inline void RSProt_BufferP4f(RSProt_Buffer *b, float v) { rsprot_p4f(b, v); }
static inline double RSProt_BufferG8d(RSProt_Buffer *b) { return rsprot_g8d(b); }
static inline void RSProt_BufferP8d(RSProt_Buffer *b, double v) { rsprot_p8d(b, v); }

static inline bool RSProt_BufferGBoolean(RSProt_Buffer *b) { return rsprot_gboolean(b); }
static inline void RSProt_BufferPBoolean(RSProt_Buffer *b, bool v) { rsprot_pboolean(b, v); }

/* ------------------------------------------------------------------ */
/* Variable width — no byte order to state; the encoding IS the name   */
/* ------------------------------------------------------------------ */

/*
 * These are the five-times-duplicated ones. Every private copy in src/net was a
 * separate chance to get the 1-vs-2-byte boundary or the -1 encoding wrong, and
 * the boundary is where they differ: Smart1or2 is one byte over [0,127] and two
 * over [0,32767]; the `null` forms add a -1; the `s` form is signed.
 */
static inline int32_t RSProt_BufferGSmart1or2(RSProt_Buffer *b) { return rsprot_gsmart1or2(b); }
static inline void RSProt_BufferPSmart1or2(RSProt_Buffer *b, int32_t v) { rsprot_psmart1or2(b, v); }
static inline int32_t RSProt_BufferGSmart1or2s(RSProt_Buffer *b) { return rsprot_gsmart1or2s(b); }
static inline void RSProt_BufferPSmart1or2s(RSProt_Buffer *b, int32_t v) { rsprot_psmart1or2s(b, v); }
static inline int32_t RSProt_BufferGSmart1or2Null(RSProt_Buffer *b) { return rsprot_gsmart1or2null(b); }
static inline void RSProt_BufferPSmart1or2Null(RSProt_Buffer *b, int32_t v) { rsprot_psmart1or2null(b, v); }
static inline int32_t RSProt_BufferGSmart1or2Extended(RSProt_Buffer *b) { return rsprot_gsmart1or2extended(b); }
static inline void RSProt_BufferPSmart1or2Extended(RSProt_Buffer *b, int32_t v) { rsprot_psmart1or2extended(b, v); }
static inline int32_t RSProt_BufferGSmart2or4(RSProt_Buffer *b) { return rsprot_gsmart2or4(b); }
static inline void RSProt_BufferPSmart2or4(RSProt_Buffer *b, int32_t v) { rsprot_psmart2or4(b, v); }
static inline int32_t RSProt_BufferGSmart2or4Null(RSProt_Buffer *b) { return rsprot_gsmart2or4null(b); }
static inline void RSProt_BufferPSmart2or4Null(RSProt_Buffer *b, int32_t v) { rsprot_psmart2or4null(b, v); }

static inline int32_t RSProt_BufferGVarInt2(RSProt_Buffer *b) { return rsprot_gvarint2(b); }
static inline void RSProt_BufferPVarInt2(RSProt_Buffer *b, int32_t v) { rsprot_pvarint2(b, v); }
static inline int32_t RSProt_BufferGVarInt2s(RSProt_Buffer *b) { return rsprot_gvarint2s(b); }
static inline void RSProt_BufferPVarInt2s(RSProt_Buffer *b, int32_t v) { rsprot_pvarint2s(b, v); }
static inline int32_t RSProt_BufferGMidiVarLen(RSProt_Buffer *b) { return rsprot_gmidivarlen(b); }
static inline void RSProt_BufferPMidiVarLen(RSProt_Buffer *b, int32_t v) { rsprot_pmidivarlen(b, v); }
static inline int32_t RSProt_BufferGType(RSProt_Buffer *b) { return rsprot_gtype(b); }
static inline void RSProt_BufferPType(RSProt_Buffer *b, int32_t v) { rsprot_ptype(b, v); }

/* ------------------------------------------------------------------ */
/* Strings and blobs                                                   */
/* ------------------------------------------------------------------ */

/*
 * Reads BORROW: the returned pointer aims into the buffer and is valid only
 * while it is. A caller that keeps a string copies it. (`rsprot_buf.h`: "a copy
 * per string per tick is a cost with no payer".) This is the one place
 * RSProt_Buffer differs in contract from RSCache_Buffer, whose `gjstr` hands
 * back malloc'd memory — a difference worth knowing before moving code between
 * the two, because the compiler will not catch it.
 */
static inline const char *RSProt_BufferGJStr(RSProt_Buffer *b, int32_t *len_out)
{
	return rsprot_gjstr(b, len_out);
}
static inline void RSProt_BufferPJStr(RSProt_Buffer *b, const char *s) { rsprot_pjstr(b, s); }
static inline const char *RSProt_BufferGJStrNull(RSProt_Buffer *b, int32_t *len_out)
{
	return rsprot_gjstrnull(b, len_out);
}
static inline void RSProt_BufferPJStrNull(RSProt_Buffer *b, const char *s) { rsprot_pjstrnull(b, s); }
static inline const char *RSProt_BufferGJStr2(RSProt_Buffer *b, int32_t *len_out)
{
	return rsprot_gjstr2(b, len_out);
}
static inline void RSProt_BufferPJStr2(RSProt_Buffer *b, const char *s) { rsprot_pjstr2(b, s); }

/* Bulk bytes. The suffix is the order the BLOCK is traversed, not any byte's
 * internal order: Le walks it backwards, the _add128 forms bias each byte. */
static inline void RSProt_BufferGData(RSProt_Buffer *b, uint8_t *dst, int32_t n) { rsprot_gdata(b, dst, n); }
static inline void RSProt_BufferPData(RSProt_Buffer *b, const uint8_t *src, int32_t n) { rsprot_pdata(b, src, n); }
static inline void RSProt_BufferGData_add128(RSProt_Buffer *b, uint8_t *dst, int32_t n) { rsprot_gdata_alt1(b, dst, n); }
static inline void RSProt_BufferPData_add128(RSProt_Buffer *b, const uint8_t *src, int32_t n) { rsprot_pdata_alt1(b, src, n); }
static inline void RSProt_BufferGDataLe(RSProt_Buffer *b, uint8_t *dst, int32_t n) { rsprot_gdata_alt2(b, dst, n); }
static inline void RSProt_BufferPDataLe(RSProt_Buffer *b, const uint8_t *src, int32_t n) { rsprot_pdata_alt2(b, src, n); }
static inline void RSProt_BufferGDataLe_add128(RSProt_Buffer *b, uint8_t *dst, int32_t n) { rsprot_gdata_alt3(b, dst, n); }
static inline void RSProt_BufferPDataLe_add128(RSProt_Buffer *b, const uint8_t *src, int32_t n) { rsprot_pdata_alt3(b, src, n); }

/* ------------------------------------------------------------------ */
/* Bit mode                                                            */
/* ------------------------------------------------------------------ */

static inline void RSProt_BufferBitBegin(RSProt_Buffer *b) { rsprot_buf_bit_begin(b); }
static inline void RSProt_BufferBitEnd(RSProt_Buffer *b) { rsprot_buf_bit_end(b); }
static inline int32_t RSProt_BufferGBits(RSProt_Buffer *b, int32_t n) { return rsprot_gbits(b, n); }
static inline void RSProt_BufferPBits(RSProt_Buffer *b, int32_t n, int32_t v) { rsprot_pbits(b, n, v); }

#endif
