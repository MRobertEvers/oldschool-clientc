#ifndef RSPROT_BUF_H
#define RSPROT_BUF_H

/*
 * RsprotBuf — the C port of RSProt's `JagByteBuf` (buffer/JagByteBuf.kt plus
 * extensions/JagexByteBufExtensions.kt).
 *
 * Kotlin's JagByteBuf is a thin value-class wrapper around Netty's ByteBuf that
 * exists only to gather the extension functions into one namespace; every method
 * delegates. There is nothing to preserve in that split, so the C port collapses
 * it into one type.
 *
 * Two things it does NOT inherit from Netty, both deliberate:
 *
 * - **Errors are sticky, not thrown.** Netty throws IndexOutOfBounds; a C
 *   decoder cannot unwind. Every read past `wpos` and every write past `cap`
 *   sets `err` and is otherwise a no-op — a read returns 0. `err` is never
 *   cleared by a subsequent successful call, so one check after a whole decode
 *   is as good as a check per field. A decoder that ignores `err` sees zeroes,
 *   which is the failure mode that is easiest to spot in a capture.
 * - **Ownership is explicit.** `rsprot_buf_wrap` borrows caller memory and never
 *   grows (an over-long write sets `err`); `rsprot_buf_alloc` owns a heap block
 *   and doubles on demand. Encoders should take a buf and not care which they
 *   were handed.
 *
 * Naming follows the wire, not C: `g` reads ("get"), `p` writes ("put"), the
 * digit is the byte width, `s` means signed, and `Alt1..Alt3` are the byte-order
 * / bias modifications the client applies. Those names are the whole point of
 * the port — an encoder transcribed from RSProt must read line-for-line against
 * its Kotlin original, and any renaming here would have to be undone mentally at
 * every one of the ~2,900 call sites the generator emits.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sticky error bits. A buffer accumulates them; none are ever cleared except by
 * rsprot_buf_clear_err(). */
enum RsprotBufErr {
	RSPROT_BUF_OK = 0,
	RSPROT_BUF_ERR_UNDERFLOW = 1 << 0, /* read past wpos */
	RSPROT_BUF_ERR_OVERFLOW = 1 << 1,  /* write past cap on a non-growable buf */
	RSPROT_BUF_ERR_ALLOC = 1 << 2,     /* growth failed */
	RSPROT_BUF_ERR_RANGE = 1 << 3,     /* a value outside what the encoding can carry */
	RSPROT_BUF_ERR_STRING = 1 << 4     /* unterminated string */
};

typedef struct RsprotBuf {
	uint8_t *data;
	int32_t cap;  /* bytes addressable in `data` */
	int32_t rpos; /* reader index */
	int32_t wpos; /* writer index */
	uint32_t err; /* enum RsprotBufErr bits, sticky */
	bool owned;   /* data was malloc'd by this buffer and may be realloc'd */

	/* Bit-access cursor. Non-negative only between rsprot_buf_bit_begin() and
	 * rsprot_buf_bit_end(); -1 otherwise. Held in bits, not bytes, and seeded
	 * from wpos/rpos at begin. Mirrors BitBuf.kt's two indices. */
	int32_t bit_wpos;
	int32_t bit_rpos;

	/* High-water mark of bytes known to hold real data or an explicit zero.
	 * pBits does a read-modify-write on the byte it lands in, so a byte first
	 * touched by the bit writer has to be zeroed before it is OR'd into —
	 * otherwise a grown or reused buffer contributes stale bits to the packet.
	 * Netty hides this behind a zeroed allocation; C does not. */
	int32_t bit_zeroed;
} RsprotBuf;

/* --- lifecycle ---------------------------------------------------------- */

/* Borrow `len` bytes at `data`. Writes beyond `len` set RSPROT_BUF_ERR_OVERFLOW.
 * wpos starts at 0 — use rsprot_buf_wrap_read() to wrap bytes that are already
 * readable. */
void rsprot_buf_wrap(RsprotBuf *b, void *data, int32_t len);

/* Borrow `len` readable bytes at `data` (wpos = len). For decoding. */
void rsprot_buf_wrap_read(RsprotBuf *b, const void *data, int32_t len);

/* Own a heap block of at least `cap` bytes; grows by doubling. */
bool rsprot_buf_alloc(RsprotBuf *b, int32_t cap);

/* Free an owned block. Safe on a wrapped buffer (does nothing) and on a
 * zeroed struct. */
void rsprot_buf_free(RsprotBuf *b);

/* Reset both indices; keeps the memory. */
void rsprot_buf_clear(RsprotBuf *b);

void rsprot_buf_clear_err(RsprotBuf *b);

static inline int32_t rsprot_buf_readable(const RsprotBuf *b) { return b->wpos - b->rpos; }
static inline int32_t rsprot_buf_writable(const RsprotBuf *b) { return b->cap - b->wpos; }
static inline bool rsprot_buf_ok(const RsprotBuf *b) { return b->err == RSPROT_BUF_OK; }

/* Ensure `extra` more bytes can be written; grows an owned buffer. Returns false
 * and sets err if it cannot. Encoders do not need to call this — every p*
 * function calls it — but a caller writing a large blob can call it once. */
bool rsprot_buf_ensure(RsprotBuf *b, int32_t extra);

/* --- 8-bit -------------------------------------------------------------- */

int32_t rsprot_g1(RsprotBuf *b);
int32_t rsprot_g1s(RsprotBuf *b);
void rsprot_p1(RsprotBuf *b, int32_t v);

int32_t rsprot_g1_alt1(RsprotBuf *b);
int32_t rsprot_g1s_alt1(RsprotBuf *b);
void rsprot_p1_alt1(RsprotBuf *b, int32_t v);

int32_t rsprot_g1_alt2(RsprotBuf *b);
int32_t rsprot_g1s_alt2(RsprotBuf *b);
void rsprot_p1_alt2(RsprotBuf *b, int32_t v);

int32_t rsprot_g1_alt3(RsprotBuf *b);
int32_t rsprot_g1s_alt3(RsprotBuf *b);
void rsprot_p1_alt3(RsprotBuf *b, int32_t v);

/* --- 16-bit ------------------------------------------------------------- */

int32_t rsprot_g2(RsprotBuf *b);
int32_t rsprot_g2s(RsprotBuf *b);
void rsprot_p2(RsprotBuf *b, int32_t v);

int32_t rsprot_g2_alt1(RsprotBuf *b);
int32_t rsprot_g2s_alt1(RsprotBuf *b);
void rsprot_p2_alt1(RsprotBuf *b, int32_t v);

int32_t rsprot_g2_alt2(RsprotBuf *b);
int32_t rsprot_g2s_alt2(RsprotBuf *b);
void rsprot_p2_alt2(RsprotBuf *b, int32_t v);

int32_t rsprot_g2_alt3(RsprotBuf *b);
int32_t rsprot_g2s_alt3(RsprotBuf *b);
void rsprot_p2_alt3(RsprotBuf *b, int32_t v);

/* --- 24-bit ------------------------------------------------------------- */

int32_t rsprot_g3(RsprotBuf *b);
int32_t rsprot_g3s(RsprotBuf *b);
void rsprot_p3(RsprotBuf *b, int32_t v);

int32_t rsprot_g3_alt1(RsprotBuf *b);
int32_t rsprot_g3s_alt1(RsprotBuf *b);
void rsprot_p3_alt1(RsprotBuf *b, int32_t v);

int32_t rsprot_g3_alt2(RsprotBuf *b);
int32_t rsprot_g3s_alt2(RsprotBuf *b);
void rsprot_p3_alt2(RsprotBuf *b, int32_t v);

int32_t rsprot_g3_alt3(RsprotBuf *b);
int32_t rsprot_g3s_alt3(RsprotBuf *b);
void rsprot_p3_alt3(RsprotBuf *b, int32_t v);

/* --- 32-bit ------------------------------------------------------------- */

int32_t rsprot_g4(RsprotBuf *b);
void rsprot_p4(RsprotBuf *b, int32_t v);

int32_t rsprot_g4_alt1(RsprotBuf *b);
void rsprot_p4_alt1(RsprotBuf *b, int32_t v);

int32_t rsprot_g4_alt2(RsprotBuf *b);
void rsprot_p4_alt2(RsprotBuf *b, int32_t v);

int32_t rsprot_g4_alt3(RsprotBuf *b);
void rsprot_p4_alt3(RsprotBuf *b, int32_t v);

/* --- 64-bit and IEEE ---------------------------------------------------- */

int64_t rsprot_g8(RsprotBuf *b);
void rsprot_p8(RsprotBuf *b, int64_t v);

float rsprot_g4f(RsprotBuf *b);
void rsprot_p4f(RsprotBuf *b, float v);

double rsprot_g8d(RsprotBuf *b);
void rsprot_p8d(RsprotBuf *b, double v);

bool rsprot_gboolean(RsprotBuf *b);
void rsprot_pboolean(RsprotBuf *b, bool v);

/* --- strings ------------------------------------------------------------ */

/*
 * Strings on the wire are NUL-terminated Cp1252. The port hands back a pointer
 * INTO the buffer plus a length rather than a copy: the caller usually wants to
 * compare or copy it once, and a malloc per string in a decoder that runs per
 * tick is a cost with no payer. The bytes are only valid while the buffer is.
 *
 * `*out_len` excludes the terminator. Returns NULL and sets
 * RSPROT_BUF_ERR_STRING if no terminator is found before wpos — the one case
 * where returning a plausible short string would be worse than nothing.
 *
 * Note these are the raw Cp1252 bytes. Use rsprot_cp1252_to_utf8() if the
 * consumer needs UTF-8; the two agree on ASCII and disagree on 0x80..0x9F,
 * which is where the game's typographic characters live.
 */
const char *rsprot_gjstr(RsprotBuf *b, int32_t *out_len);

/* gjstrnull: a lone 0 byte means "null" — returns NULL with *out_len == 0 and
 * no error set. Distinguish from the error case with rsprot_buf_ok(). */
const char *rsprot_gjstrnull(RsprotBuf *b, int32_t *out_len);

/* gjstr2: a leading 0 byte, then the string. RSProt throws if the leading byte
 * is not 0; here that sets RSPROT_BUF_ERR_STRING. */
const char *rsprot_gjstr2(RsprotBuf *b, int32_t *out_len);

/* `s` is NUL-terminated Cp1252. pjstrnull accepts NULL and writes only the
 * terminator. */
void rsprot_pjstr(RsprotBuf *b, const char *s);
void rsprot_pjstrnull(RsprotBuf *b, const char *s);
void rsprot_pjstr2(RsprotBuf *b, const char *s);

/* --- smart / variable-width encodings ----------------------------------- */

int32_t rsprot_gsmart1or2(RsprotBuf *b);
void rsprot_psmart1or2(RsprotBuf *b, int32_t v);

int32_t rsprot_gsmart1or2s(RsprotBuf *b);
void rsprot_psmart1or2s(RsprotBuf *b, int32_t v);

int32_t rsprot_gsmart1or2null(RsprotBuf *b);
void rsprot_psmart1or2null(RsprotBuf *b, int32_t v);

int32_t rsprot_gsmart1or2extended(RsprotBuf *b);
void rsprot_psmart1or2extended(RsprotBuf *b, int32_t v);

int32_t rsprot_gsmart2or4(RsprotBuf *b);
void rsprot_psmart2or4(RsprotBuf *b, int32_t v);

int32_t rsprot_gsmart2or4null(RsprotBuf *b);
void rsprot_psmart2or4null(RsprotBuf *b, int32_t v);

int32_t rsprot_gmidivarlen(RsprotBuf *b);
void rsprot_pmidivarlen(RsprotBuf *b, int32_t v);

int32_t rsprot_gvarint2(RsprotBuf *b);
void rsprot_pvarint2(RsprotBuf *b, int32_t v);

int32_t rsprot_gvarint2s(RsprotBuf *b);
void rsprot_pvarint2s(RsprotBuf *b, int32_t v);

/* gType/pType: the 1-or-2 byte "type" encoding, values 0..1023. */
int32_t rsprot_gtype(RsprotBuf *b);
void rsprot_ptype(RsprotBuf *b, int32_t v);

/* --- bulk data ---------------------------------------------------------- */

void rsprot_gdata(RsprotBuf *b, void *dest, int32_t len);
void rsprot_pdata(RsprotBuf *b, const void *src, int32_t len);

void rsprot_gdata_alt1(RsprotBuf *b, void *dest, int32_t len);
void rsprot_pdata_alt1(RsprotBuf *b, const void *src, int32_t len);

void rsprot_gdata_alt2(RsprotBuf *b, void *dest, int32_t len);
void rsprot_pdata_alt2(RsprotBuf *b, const void *src, int32_t len);

void rsprot_gdata_alt3(RsprotBuf *b, void *dest, int32_t len);
void rsprot_pdata_alt3(RsprotBuf *b, const void *src, int32_t len);

/* --- combined ids ------------------------------------------------------- */

/*
 * A "combined id" packs an interface id and a component id into one 32-bit
 * field, each 16 bits, with 0xFFFF meaning -1. RSProt models it as a value class
 * (CombinedId.kt); here it is a plain int32 plus three accessors, because the
 * only thing the class bought was the named constructor.
 */
static inline int32_t rsprot_combined_id(int32_t interface_id, int32_t component_id)
{
	return (int32_t)(((uint32_t)interface_id & 0xFFFFu) << 16 | ((uint32_t)component_id & 0xFFFFu));
}

static inline int32_t rsprot_combined_interface(int32_t combined)
{
	int32_t v = (int32_t)(((uint32_t)combined >> 16) & 0xFFFFu);
	return v == 0xFFFF ? -1 : v;
}

static inline int32_t rsprot_combined_component(int32_t combined)
{
	int32_t v = combined & 0xFFFF;
	return v == 0xFFFF ? -1 : v;
}

int32_t rsprot_gcombined_id(RsprotBuf *b);
int32_t rsprot_gcombined_id_alt1(RsprotBuf *b);
int32_t rsprot_gcombined_id_alt2(RsprotBuf *b);
int32_t rsprot_gcombined_id_alt3(RsprotBuf *b);

void rsprot_pcombined_id(RsprotBuf *b, int32_t combined);
void rsprot_pcombined_id_alt1(RsprotBuf *b, int32_t combined);
void rsprot_pcombined_id_alt2(RsprotBuf *b, int32_t combined);
void rsprot_pcombined_id_alt3(RsprotBuf *b, int32_t combined);

/* --- bit access --------------------------------------------------------- */

/*
 * Bit mode. Between begin and end the byte indices are frozen and the bit
 * cursors are live; mixing byte and bit calls in between is a bug the port does
 * not police (RSProt does not either — BitBuf holds the ByteBuf hostage).
 *
 * rsprot_buf_bit_end() pads the writer to the next byte boundary with zeroes and
 * rounds the reader up, exactly as BitBuf.close() does. That padding is load
 * bearing: PLAYER_INFO's four bit sections are each byte-aligned, and a section
 * that ends mid-byte would shift everything after it.
 */
void rsprot_buf_bit_begin(RsprotBuf *b);
void rsprot_buf_bit_end(RsprotBuf *b);

/* count must be 1..32. */
void rsprot_pbits(RsprotBuf *b, int32_t count, int32_t value);
int32_t rsprot_gbits(RsprotBuf *b, int32_t count);

static inline int32_t rsprot_buf_readable_bits(const RsprotBuf *b) { return b->bit_wpos - b->bit_rpos; }

/* --- checksum ----------------------------------------------------------- */

/* CRC-32 (the standard reflected polynomial, as CyclicRedundancyCheck.kt uses)
 * over [start, wpos), appended as a p4. Returns the checksum. */
uint32_t rsprot_buf_add_crc32(RsprotBuf *b, int32_t start);

/* Checks the trailing 4 bytes before rpos against a CRC of [0, rpos-4). */
bool rsprot_buf_check_crc32(RsprotBuf *b);

uint32_t rsprot_crc32(const void *data, int32_t len);

#endif /* RSPROT_BUF_H */
