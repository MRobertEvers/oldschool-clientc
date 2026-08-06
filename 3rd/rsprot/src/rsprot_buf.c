/*
 * RsprotBuf — see rsprot_buf.h for the contract.
 *
 * Every function here is transcribed one-for-one from
 * `buffer/src/main/kotlin/net/rsprot/buffer/extensions/JagexByteBufExtensions.kt`.
 * Where a Kotlin line reads `writeByte(value shr 8)` the C line reads
 * `put1(b, v >> 8)` in the same position. That is not stylistic: the alt
 * orderings differ from each other by which byte goes where, several of them
 * round-trip correctly with the wrong neighbour's implementation for small
 * values, and the only cheap way to be sure a writer is right is to read it
 * beside its original.
 */

#include "rsprot_buf.h"

#include <stdlib.h>
#include <string.h>

#define HALF_UBYTE 0x80

/* --- internals ---------------------------------------------------------- */

static bool need_read(RsprotBuf *b, int32_t n)
{
	if (b->rpos + n > b->wpos || n < 0) {
		b->err |= RSPROT_BUF_ERR_UNDERFLOW;
		return false;
	}
	return true;
}

bool rsprot_buf_ensure(RsprotBuf *b, int32_t extra)
{
	int32_t need;
	int32_t cap;
	uint8_t *grown;

	if (extra < 0) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return false;
	}
	need = b->wpos + extra;
	if (need <= b->cap)
		return true;
	if (!b->owned) {
		b->err |= RSPROT_BUF_ERR_OVERFLOW;
		return false;
	}
	cap = b->cap > 0 ? b->cap : 32;
	while (cap < need) {
		if (cap > (int32_t)0x3FFFFFFF) {
			b->err |= RSPROT_BUF_ERR_ALLOC;
			return false;
		}
		cap *= 2;
	}
	grown = (uint8_t *)realloc(b->data, (size_t)cap);
	if (!grown) {
		b->err |= RSPROT_BUF_ERR_ALLOC;
		return false;
	}
	b->data = grown;
	b->cap = cap;
	return true;
}

static bool need_write(RsprotBuf *b, int32_t n) { return rsprot_buf_ensure(b, n); }

static void put1(RsprotBuf *b, int32_t v)
{
	if (!need_write(b, 1))
		return;
	b->data[b->wpos++] = (uint8_t)(v & 0xFF);
}

static int32_t get1u(RsprotBuf *b)
{
	if (!need_read(b, 1))
		return 0;
	return b->data[b->rpos++];
}

/* The signed read. Kotlin's `readByte()` is a signed 8-bit value and several
 * alt orderings negate it before masking; doing that on the unsigned value
 * gives a different answer for inputs above 0x7F. */
static int32_t get1s(RsprotBuf *b)
{
	return (int32_t)(int8_t)get1u(b);
}

/* Peek without consuming — the smart encodings branch on the next byte. */
static int32_t peek1u(const RsprotBuf *b)
{
	if (b->rpos >= b->wpos)
		return 0;
	return b->data[b->rpos];
}

/* --- lifecycle ---------------------------------------------------------- */

void rsprot_buf_wrap(RsprotBuf *b, void *data, int32_t len)
{
	memset(b, 0, sizeof(*b));
	b->data = (uint8_t *)data;
	b->cap = len;
	b->bit_wpos = -1;
	b->bit_rpos = -1;
}

void rsprot_buf_wrap_read(RsprotBuf *b, const void *data, int32_t len)
{
	rsprot_buf_wrap(b, (void *)(uintptr_t)data, len);
	b->wpos = len;
	b->bit_zeroed = len;
}

bool rsprot_buf_alloc(RsprotBuf *b, int32_t cap)
{
	memset(b, 0, sizeof(*b));
	b->bit_wpos = -1;
	b->bit_rpos = -1;
	if (cap < 16)
		cap = 16;
	b->data = (uint8_t *)malloc((size_t)cap);
	if (!b->data) {
		b->err |= RSPROT_BUF_ERR_ALLOC;
		return false;
	}
	b->cap = cap;
	b->owned = true;
	return true;
}

void rsprot_buf_free(RsprotBuf *b)
{
	if (!b)
		return;
	if (b->owned)
		free(b->data);
	memset(b, 0, sizeof(*b));
	b->bit_wpos = -1;
	b->bit_rpos = -1;
}

void rsprot_buf_clear(RsprotBuf *b)
{
	b->rpos = 0;
	b->wpos = 0;
	b->bit_wpos = -1;
	b->bit_rpos = -1;
	b->bit_zeroed = 0;
}

void rsprot_buf_clear_err(RsprotBuf *b) { b->err = RSPROT_BUF_OK; }

/* --- 8-bit -------------------------------------------------------------- */

int32_t rsprot_g1(RsprotBuf *b) { return get1u(b); }
int32_t rsprot_g1s(RsprotBuf *b) { return get1s(b); }
void rsprot_p1(RsprotBuf *b, int32_t v) { put1(b, v); }

int32_t rsprot_g1_alt1(RsprotBuf *b) { return (get1u(b) - HALF_UBYTE) & 0xFF; }
int32_t rsprot_g1s_alt1(RsprotBuf *b) { return (int32_t)(int8_t)(get1u(b) - HALF_UBYTE); }
void rsprot_p1_alt1(RsprotBuf *b, int32_t v) { put1(b, v + HALF_UBYTE); }

int32_t rsprot_g1_alt2(RsprotBuf *b) { return (-get1s(b)) & 0xFF; }
int32_t rsprot_g1s_alt2(RsprotBuf *b) { return -get1s(b); }
void rsprot_p1_alt2(RsprotBuf *b, int32_t v) { put1(b, -v); }

int32_t rsprot_g1_alt3(RsprotBuf *b) { return (HALF_UBYTE - get1s(b)) & 0xFF; }
int32_t rsprot_g1s_alt3(RsprotBuf *b) { return (int32_t)(int8_t)(HALF_UBYTE - get1s(b)); }
void rsprot_p1_alt3(RsprotBuf *b, int32_t v) { put1(b, HALF_UBYTE - v); }

/* --- 16-bit ------------------------------------------------------------- */

int32_t rsprot_g2(RsprotBuf *b)
{
	int32_t hi = get1u(b);
	return (hi << 8) | get1u(b);
}

int32_t rsprot_g2s(RsprotBuf *b) { return (int32_t)(int16_t)rsprot_g2(b); }

void rsprot_p2(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 8);
	put1(b, v);
}

int32_t rsprot_g2_alt1(RsprotBuf *b)
{
	int32_t lo = get1u(b);
	return lo | (get1u(b) << 8);
}

int32_t rsprot_g2s_alt1(RsprotBuf *b) { return (int32_t)(int16_t)rsprot_g2_alt1(b); }

void rsprot_p2_alt1(RsprotBuf *b, int32_t v)
{
	put1(b, v);
	put1(b, v >> 8);
}

int32_t rsprot_g2_alt2(RsprotBuf *b)
{
	int32_t s = rsprot_g2(b);
	return (s & 0xFF00) | (((s & 0xFF) - HALF_UBYTE) & 0xFF);
}

int32_t rsprot_g2s_alt2(RsprotBuf *b) { return (int32_t)(int16_t)rsprot_g2_alt2(b); }

void rsprot_p2_alt2(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 8);
	put1(b, v + HALF_UBYTE);
}

int32_t rsprot_g2_alt3(RsprotBuf *b)
{
	int32_t s = rsprot_g2(b);
	return ((((uint32_t)s >> 8) - HALF_UBYTE) & 0xFF) | ((s & 0xFF) << 8);
}

int32_t rsprot_g2s_alt3(RsprotBuf *b) { return (int32_t)(int16_t)rsprot_g2_alt3(b); }

void rsprot_p2_alt3(RsprotBuf *b, int32_t v)
{
	put1(b, v + HALF_UBYTE);
	put1(b, v >> 8);
}

/* --- 24-bit ------------------------------------------------------------- */

/* Sign-extend a 24-bit quantity. */
static int32_t sx24(int32_t v) { return v > 0x7FFFFF ? v - 0x1000000 : v; }

int32_t rsprot_g3(RsprotBuf *b)
{
	int32_t v = get1u(b) << 16;
	v |= get1u(b) << 8;
	return v | get1u(b);
}

int32_t rsprot_g3s(RsprotBuf *b) { return sx24(rsprot_g3(b)); }

void rsprot_p3(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 16);
	put1(b, v >> 8);
	put1(b, v);
}

int32_t rsprot_g3_alt1(RsprotBuf *b)
{
	int32_t v = get1u(b);
	v |= get1u(b) << 8;
	return v | (get1u(b) << 16);
}

int32_t rsprot_g3s_alt1(RsprotBuf *b) { return sx24(rsprot_g3_alt1(b)); }

void rsprot_p3_alt1(RsprotBuf *b, int32_t v)
{
	put1(b, v);
	put1(b, v >> 8);
	put1(b, v >> 16);
}

int32_t rsprot_g3_alt2(RsprotBuf *b)
{
	int32_t m = rsprot_g3(b);
	return ((m & 0xFF) << 8) | (((uint32_t)m >> 8) & 0xFF) | ((((uint32_t)m >> 16) & 0xFF) << 16);
}

int32_t rsprot_g3s_alt2(RsprotBuf *b) { return sx24(rsprot_g3_alt2(b)); }

void rsprot_p3_alt2(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 16);
	put1(b, v);
	put1(b, v >> 8);
}

int32_t rsprot_g3_alt3(RsprotBuf *b)
{
	int32_t m = rsprot_g3(b);
	return (m & 0xFF) | ((((uint32_t)m >> 8) & 0xFF) << 16) | ((((uint32_t)m >> 16) & 0xFF) << 8);
}

int32_t rsprot_g3s_alt3(RsprotBuf *b) { return sx24(rsprot_g3_alt3(b)); }

void rsprot_p3_alt3(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 8);
	put1(b, v >> 16);
	put1(b, v);
}

/* --- 32-bit ------------------------------------------------------------- */

int32_t rsprot_g4(RsprotBuf *b)
{
	uint32_t v = (uint32_t)get1u(b) << 24;
	v |= (uint32_t)get1u(b) << 16;
	v |= (uint32_t)get1u(b) << 8;
	return (int32_t)(v | (uint32_t)get1u(b));
}

void rsprot_p4(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 24);
	put1(b, v >> 16);
	put1(b, v >> 8);
	put1(b, v);
}

int32_t rsprot_g4_alt1(RsprotBuf *b)
{
	uint32_t v = (uint32_t)get1u(b);
	v |= (uint32_t)get1u(b) << 8;
	v |= (uint32_t)get1u(b) << 16;
	return (int32_t)(v | ((uint32_t)get1u(b) << 24));
}

void rsprot_p4_alt1(RsprotBuf *b, int32_t v)
{
	put1(b, v);
	put1(b, v >> 8);
	put1(b, v >> 16);
	put1(b, v >> 24);
}

int32_t rsprot_g4_alt2(RsprotBuf *b)
{
	uint32_t v = (uint32_t)get1u(b) << 8;
	v |= (uint32_t)get1u(b);
	v |= (uint32_t)get1u(b) << 24;
	return (int32_t)(v | ((uint32_t)get1u(b) << 16));
}

void rsprot_p4_alt2(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 8);
	put1(b, v);
	put1(b, v >> 24);
	put1(b, v >> 16);
}

int32_t rsprot_g4_alt3(RsprotBuf *b)
{
	uint32_t v = (uint32_t)get1u(b) << 16;
	v |= (uint32_t)get1u(b) << 24;
	v |= (uint32_t)get1u(b);
	return (int32_t)(v | ((uint32_t)get1u(b) << 8));
}

void rsprot_p4_alt3(RsprotBuf *b, int32_t v)
{
	put1(b, v >> 16);
	put1(b, v >> 24);
	put1(b, v);
	put1(b, v >> 8);
}

/* --- 64-bit and IEEE ---------------------------------------------------- */

int64_t rsprot_g8(RsprotBuf *b)
{
	uint64_t hi = (uint32_t)rsprot_g4(b);
	uint64_t lo = (uint32_t)rsprot_g4(b);
	return (int64_t)((hi << 32) | lo);
}

void rsprot_p8(RsprotBuf *b, int64_t v)
{
	rsprot_p4(b, (int32_t)((uint64_t)v >> 32));
	rsprot_p4(b, (int32_t)(uint64_t)v);
}

float rsprot_g4f(RsprotBuf *b)
{
	int32_t bits = rsprot_g4(b);
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

void rsprot_p4f(RsprotBuf *b, float v)
{
	int32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	rsprot_p4(b, bits);
}

double rsprot_g8d(RsprotBuf *b)
{
	int64_t bits = rsprot_g8(b);
	double d;
	memcpy(&d, &bits, sizeof(d));
	return d;
}

void rsprot_p8d(RsprotBuf *b, double v)
{
	int64_t bits;
	memcpy(&bits, &v, sizeof(bits));
	rsprot_p8(b, bits);
}

bool rsprot_gboolean(RsprotBuf *b) { return (get1u(b) & 0x1) != 0; }
void rsprot_pboolean(RsprotBuf *b, bool v) { put1(b, v ? 0x1 : 0); }

/* --- strings ------------------------------------------------------------ */

const char *rsprot_gjstr(RsprotBuf *b, int32_t *out_len)
{
	int32_t start = b->rpos;
	int32_t i;

	if (out_len)
		*out_len = 0;
	for (i = start; i < b->wpos; i++) {
		if (b->data[i] == 0) {
			b->rpos = i + 1;
			if (out_len)
				*out_len = i - start;
			return (const char *)(b->data + start);
		}
	}
	/* Netty's forEachByte(FIND_NUL) returning -1 is a hard `require` failure in
	 * RSProt. Consuming nothing here keeps the reader where the caller can see
	 * what it choked on. */
	b->err |= RSPROT_BUF_ERR_STRING;
	return NULL;
}

const char *rsprot_gjstrnull(RsprotBuf *b, int32_t *out_len)
{
	if (out_len)
		*out_len = 0;
	if (b->rpos >= b->wpos) {
		b->err |= RSPROT_BUF_ERR_UNDERFLOW;
		return NULL;
	}
	if (b->data[b->rpos] == 0) {
		b->rpos++;
		return NULL;
	}
	return rsprot_gjstr(b, out_len);
}

const char *rsprot_gjstr2(RsprotBuf *b, int32_t *out_len)
{
	if (out_len)
		*out_len = 0;
	if (get1u(b) != 0) {
		b->err |= RSPROT_BUF_ERR_STRING;
		return NULL;
	}
	return rsprot_gjstr(b, out_len);
}

void rsprot_pjstr(RsprotBuf *b, const char *s)
{
	size_t n = s ? strlen(s) : 0;
	if (!need_write(b, (int32_t)n + 1))
		return;
	if (n)
		memcpy(b->data + b->wpos, s, n);
	b->wpos += (int32_t)n;
	b->data[b->wpos++] = 0;
}

void rsprot_pjstrnull(RsprotBuf *b, const char *s) { rsprot_pjstr(b, s); }

void rsprot_pjstr2(RsprotBuf *b, const char *s)
{
	put1(b, 0);
	rsprot_pjstr(b, s);
}

/* --- smart / variable-width encodings ----------------------------------- */

int32_t rsprot_gsmart1or2(RsprotBuf *b)
{
	if (peek1u(b) < HALF_UBYTE)
		return get1u(b);
	return rsprot_g2(b) - 32768;
}

void rsprot_psmart1or2(RsprotBuf *b, int32_t v)
{
	if (v >= 0 && v <= 0x7F) {
		put1(b, v);
	} else if (v >= 0 && v <= 0x7FFF) {
		rsprot_p2(b, 0x8000 | v);
	} else {
		b->err |= RSPROT_BUF_ERR_RANGE;
	}
}

int32_t rsprot_gsmart1or2s(RsprotBuf *b)
{
	if (peek1u(b) < HALF_UBYTE)
		return get1u(b) - 64;
	return rsprot_g2(b) - 49152;
}

void rsprot_psmart1or2s(RsprotBuf *b, int32_t v)
{
	if (v >= -0x40 && v <= 0x3F) {
		put1(b, v + 0x40);
	} else if (v >= -0x4000 && v <= 0x3FFF) {
		rsprot_p2(b, 0x8000 | (v + 0x4000));
	} else {
		b->err |= RSPROT_BUF_ERR_RANGE;
	}
}

int32_t rsprot_gsmart1or2null(RsprotBuf *b)
{
	if (peek1u(b) < HALF_UBYTE)
		return get1u(b) - 1;
	return rsprot_g2(b) - 32769;
}

void rsprot_psmart1or2null(RsprotBuf *b, int32_t v)
{
	/* Kotlin's `in -1..<0x7F` is half-open: -1 through 0x7E. */
	if (v >= -1 && v < 0x7F) {
		put1(b, v + 1);
	} else if (v >= -1 && v < 0x7FFF) {
		rsprot_p2(b, 0x8000 | (v + 1));
	} else {
		b->err |= RSPROT_BUF_ERR_RANGE;
	}
}

int32_t rsprot_gsmart1or2extended(RsprotBuf *b)
{
	int32_t total = 0;
	int32_t num = rsprot_gsmart1or2(b);
	while (num == 0x7FFF) {
		total += 0x7FFF;
		num = rsprot_gsmart1or2(b);
		if (!rsprot_buf_ok(b))
			return total;
	}
	return total + num;
}

void rsprot_psmart1or2extended(RsprotBuf *b, int32_t v)
{
	int32_t remaining = v;
	while (remaining >= 0x7FFF) {
		rsprot_psmart1or2(b, 0x7FFF);
		remaining -= 0x7FFF;
	}
	rsprot_psmart1or2(b, remaining);
}

int32_t rsprot_gsmart2or4(RsprotBuf *b)
{
	if ((int8_t)peek1u(b) < 0)
		return rsprot_g4(b) & 0x7FFFFFFF;
	return rsprot_g2(b);
}

void rsprot_psmart2or4(RsprotBuf *b, int32_t v)
{
	if (v >= 0 && v <= 0x7FFF) {
		rsprot_p2(b, v);
	} else if (v >= 0) {
		rsprot_p4(b, (int32_t)(0x80000000u | (uint32_t)v));
	} else {
		b->err |= RSPROT_BUF_ERR_RANGE;
	}
}

int32_t rsprot_gsmart2or4null(RsprotBuf *b)
{
	int32_t num;
	if ((int8_t)peek1u(b) < 0)
		return rsprot_g4(b) & 0x7FFFFFFF;
	num = rsprot_g2(b);
	return num == 32767 ? -1 : num;
}

void rsprot_psmart2or4null(RsprotBuf *b, int32_t v)
{
	if (v < -1) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	if (v == -1) {
		rsprot_p2(b, 0x7FFF);
	} else if (v < 32767) {
		rsprot_p2(b, v);
	} else {
		/* RSProt writes the int and then adds 128 to the byte it just wrote,
		 * rather than OR-ing 0x80 in first. For v <= 0x7FFFFFFF the two agree;
		 * the add is transcribed rather than simplified because the difference
		 * would only show for a value this encoding is not allowed to carry. */
		int32_t at;
		rsprot_p4(b, v);
		at = b->wpos - 4;
		if (at >= 0 && at < b->cap)
			b->data[at] = (uint8_t)(b->data[at] + HALF_UBYTE);
	}
}

int32_t rsprot_gmidivarlen(RsprotBuf *b)
{
	int32_t value = 0;
	int32_t byte;
	do {
		byte = get1u(b);
		value = (value << 7) | (byte & 0x7F);
		if (!rsprot_buf_ok(b))
			break;
	} while ((byte & HALF_UBYTE) != 0);
	return value;
}

void rsprot_pmidivarlen(RsprotBuf *b, int32_t v)
{
	if ((v & ~0x7F) != 0) {
		if ((v & ~0x3FFF) != 0) {
			if ((v & ~0x1FFFFF) != 0) {
				if ((v & ~0xFFFFFFF) != 0)
					put1(b, (int32_t)(((uint32_t)v >> 28) & 0x7F) | HALF_UBYTE);
				put1(b, (int32_t)(((uint32_t)v >> 21) & 0x7F) | HALF_UBYTE);
			}
			put1(b, (int32_t)(((uint32_t)v >> 14) & 0x7F) | HALF_UBYTE);
		}
		put1(b, (int32_t)(((uint32_t)v >> 7) & 0x7F) | HALF_UBYTE);
	}
	put1(b, v & 0x7F);
}

int32_t rsprot_gvarint2(RsprotBuf *b)
{
	uint32_t value = 0;
	int32_t bits = 0;
	int32_t temp;
	do {
		temp = get1u(b);
		if (bits < 32)
			value |= (uint32_t)(temp & 0x7F) << bits;
		bits += 7;
		if (!rsprot_buf_ok(b))
			break;
	} while (temp > 0x7F);
	return (int32_t)value;
}

void rsprot_pvarint2(RsprotBuf *b, int32_t v)
{
	uint32_t remaining = (uint32_t)v;
	while (remaining > 0x7F) {
		put1(b, (int32_t)(0x80u | (remaining & 0x7F)));
		remaining >>= 7;
	}
	put1(b, (int32_t)remaining);
}

int32_t rsprot_gvarint2s(RsprotBuf *b)
{
	uint32_t unsigned_value = (uint32_t)rsprot_gvarint2(b);
	return (int32_t)((unsigned_value >> 1) ^ (uint32_t)(-(int32_t)(unsigned_value & 0x1)));
}

void rsprot_pvarint2s(RsprotBuf *b, int32_t v)
{
	rsprot_pvarint2(b, (int32_t)(((uint32_t)v << 1) ^ (uint32_t)(v >> 31)));
}

int32_t rsprot_gtype(RsprotBuf *b)
{
	int32_t type = get1u(b);
	if (type < 252)
		return type;
	return ((type - 252) << 8) + get1u(b);
}

void rsprot_ptype(RsprotBuf *b, int32_t v)
{
	if (v < 0 || v >= 1024) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	if (v < 252) {
		put1(b, v);
	} else {
		put1(b, 252 + ((int32_t)((uint32_t)v >> 8)));
		put1(b, v & 0xFF);
	}
}

/* --- bulk data ---------------------------------------------------------- */

void rsprot_gdata(RsprotBuf *b, void *dest, int32_t len)
{
	if (!need_read(b, len))
		return;
	memcpy(dest, b->data + b->rpos, (size_t)len);
	b->rpos += len;
}

void rsprot_pdata(RsprotBuf *b, const void *src, int32_t len)
{
	if (len < 0) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	if (!need_write(b, len))
		return;
	if (len)
		memcpy(b->data + b->wpos, src, (size_t)len);
	b->wpos += len;
}

void rsprot_gdata_alt1(RsprotBuf *b, void *dest, int32_t len)
{
	uint8_t *d = (uint8_t *)dest;
	int32_t i;
	if (!need_read(b, len))
		return;
	for (i = len - 1; i >= 0; i--)
		d[i] = b->data[b->rpos++];
}

void rsprot_pdata_alt1(RsprotBuf *b, const void *src, int32_t len)
{
	const uint8_t *s = (const uint8_t *)src;
	int32_t i;
	if (len < 0 || !need_write(b, len))
		return;
	for (i = len - 1; i >= 0; i--)
		b->data[b->wpos++] = s[i];
}

void rsprot_gdata_alt2(RsprotBuf *b, void *dest, int32_t len)
{
	uint8_t *d = (uint8_t *)dest;
	int32_t i;
	if (!need_read(b, len))
		return;
	for (i = 0; i < len; i++)
		d[i] = (uint8_t)(b->data[b->rpos++] - HALF_UBYTE);
}

void rsprot_pdata_alt2(RsprotBuf *b, const void *src, int32_t len)
{
	const uint8_t *s = (const uint8_t *)src;
	int32_t i;
	if (len < 0 || !need_write(b, len))
		return;
	for (i = 0; i < len; i++)
		b->data[b->wpos++] = (uint8_t)(s[i] + HALF_UBYTE);
}

void rsprot_gdata_alt3(RsprotBuf *b, void *dest, int32_t len)
{
	uint8_t *d = (uint8_t *)dest;
	int32_t i;
	if (!need_read(b, len))
		return;
	for (i = len - 1; i >= 0; i--)
		d[i] = (uint8_t)(b->data[b->rpos++] - HALF_UBYTE);
}

void rsprot_pdata_alt3(RsprotBuf *b, const void *src, int32_t len)
{
	const uint8_t *s = (const uint8_t *)src;
	int32_t i;
	if (len < 0 || !need_write(b, len))
		return;
	for (i = len - 1; i >= 0; i--)
		b->data[b->wpos++] = (uint8_t)(s[i] + HALF_UBYTE);
}

/* --- combined ids ------------------------------------------------------- */

int32_t rsprot_gcombined_id(RsprotBuf *b) { return rsprot_g4(b); }
int32_t rsprot_gcombined_id_alt1(RsprotBuf *b) { return rsprot_g4_alt1(b); }
int32_t rsprot_gcombined_id_alt2(RsprotBuf *b) { return rsprot_g4_alt2(b); }
int32_t rsprot_gcombined_id_alt3(RsprotBuf *b) { return rsprot_g4_alt3(b); }

void rsprot_pcombined_id(RsprotBuf *b, int32_t c) { rsprot_p4(b, c); }
void rsprot_pcombined_id_alt1(RsprotBuf *b, int32_t c) { rsprot_p4_alt1(b, c); }
void rsprot_pcombined_id_alt2(RsprotBuf *b, int32_t c) { rsprot_p4_alt2(b, c); }
void rsprot_pcombined_id_alt3(RsprotBuf *b, int32_t c) { rsprot_p4_alt3(b, c); }

/* --- bit access --------------------------------------------------------- */

#define BITS_PER_BYTE 8
#define LOG_BITS_PER_BYTE 3
#define MASK_BITS_PER_BYTE 7

static int32_t bitmask(int32_t pos) { return (int32_t)(((uint32_t)1 << pos) - 1); }

void rsprot_buf_bit_begin(RsprotBuf *b)
{
	b->bit_wpos = b->wpos << LOG_BITS_PER_BYTE;
	b->bit_rpos = b->rpos << LOG_BITS_PER_BYTE;
	if (b->bit_zeroed < b->wpos)
		b->bit_zeroed = b->wpos;
}

void rsprot_buf_bit_end(RsprotBuf *b)
{
	int32_t pad;
	if (b->bit_wpos < 0)
		return;
	pad = ((b->bit_wpos + MASK_BITS_PER_BYTE) & ~MASK_BITS_PER_BYTE) - b->bit_wpos;
	if (pad != 0)
		rsprot_pbits(b, pad, 0);
	b->bit_rpos = (b->bit_rpos + MASK_BITS_PER_BYTE) & ~MASK_BITS_PER_BYTE;
	b->wpos = (int32_t)((uint32_t)b->bit_wpos >> LOG_BITS_PER_BYTE);
	b->rpos = (int32_t)((uint32_t)b->bit_rpos >> LOG_BITS_PER_BYTE);
	b->bit_wpos = -1;
	b->bit_rpos = -1;
}

/* Make bytes [0, upto) addressable, zeroing anything the bit writer has not
 * already accounted for. See the `bit_zeroed` note in the header. */
static bool bit_reserve(RsprotBuf *b, int32_t upto)
{
	if (upto > b->cap) {
		int32_t save = b->wpos;
		bool ok;
		b->wpos = upto;
		ok = rsprot_buf_ensure(b, 0);
		b->wpos = save;
		if (!ok)
			return false;
	}
	while (b->bit_zeroed < upto)
		b->data[b->bit_zeroed++] = 0;
	return true;
}

void rsprot_pbits(RsprotBuf *b, int32_t count, int32_t value)
{
	int32_t rem;
	int32_t byte_pos;
	int32_t bit_pos;

	if (count < 1 || count > 32) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	if (b->bit_wpos < 0) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	if (!bit_reserve(b, (b->bit_wpos + count + MASK_BITS_PER_BYTE) >> LOG_BITS_PER_BYTE))
		return;

	rem = count;
	byte_pos = (int32_t)((uint32_t)b->bit_wpos >> LOG_BITS_PER_BYTE);
	bit_pos = BITS_PER_BYTE - (b->bit_wpos & MASK_BITS_PER_BYTE);
	b->bit_wpos += count;
	while (rem > bit_pos) {
		int32_t ending = (int32_t)((uint32_t)value >> (rem - bit_pos)) & bitmask(bit_pos);
		b->data[byte_pos] = (uint8_t)((b->data[byte_pos] & ~bitmask(bit_pos)) | ending);
		byte_pos++;
		rem -= bit_pos;
		bit_pos = BITS_PER_BYTE;
	}
	{
		int32_t mask = bitmask(rem);
		int32_t offset = bit_pos - rem;
		int32_t cur = b->data[byte_pos];
		b->data[byte_pos] = (uint8_t)((cur & ~(mask << offset)) | ((value & mask) << offset));
	}
}

int32_t rsprot_gbits(RsprotBuf *b, int32_t count)
{
	int32_t remaining;
	int32_t byte_pos;
	int32_t bit_pos;
	int32_t value = 0;

	if (count < 1 || count > 32) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return 0;
	}
	if (b->bit_rpos < 0 || b->bit_rpos + count > b->bit_wpos) {
		b->err |= RSPROT_BUF_ERR_UNDERFLOW;
		return 0;
	}

	remaining = count;
	byte_pos = b->bit_rpos >> LOG_BITS_PER_BYTE;
	bit_pos = BITS_PER_BYTE - (b->bit_rpos & MASK_BITS_PER_BYTE);
	b->bit_rpos += remaining;
	while (remaining > bit_pos) {
		value += (b->data[byte_pos++] & bitmask(bit_pos)) << (remaining - bit_pos);
		remaining -= bit_pos;
		bit_pos = BITS_PER_BYTE;
	}
	return value + (int32_t)(((uint32_t)b->data[byte_pos] >> (bit_pos - remaining)) & (uint32_t)bitmask(remaining));
}

/* --- checksum ----------------------------------------------------------- */

/* The table CyclicRedundancyCheck.kt builds at class-init: the reflected
 * CRC-32 polynomial 0xEDB88320 (Kotlin spells it -0x12477ce0). */
static uint32_t g_crc32[256];
static bool g_crc32_ready;

static void crc32_init(void)
{
	uint32_t i;
	for (i = 0; i < 256; i++) {
		uint32_t crc = i;
		int bit;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
		g_crc32[i] = crc;
	}
	g_crc32_ready = true;
}

uint32_t rsprot_crc32(const void *data, int32_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t checksum = 0xFFFFFFFFu;
	int32_t i;

	if (!g_crc32_ready)
		crc32_init();
	for (i = 0; i < len; i++)
		checksum = (checksum >> 8) ^ g_crc32[(checksum ^ p[i]) & 0xFF];
	return ~checksum;
}

uint32_t rsprot_buf_add_crc32(RsprotBuf *b, int32_t start)
{
	uint32_t checksum;
	if (start < 0 || start > b->wpos) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return 0;
	}
	checksum = rsprot_crc32(b->data + start, b->wpos - start);
	rsprot_p4(b, (int32_t)checksum);
	return checksum;
}

bool rsprot_buf_check_crc32(RsprotBuf *b)
{
	int32_t length = b->rpos;
	uint32_t checksum;
	if (length < 4) {
		b->err |= RSPROT_BUF_ERR_RANGE;
		return false;
	}
	checksum = rsprot_crc32(b->data, length - 4);
	b->rpos = length - 4;
	return checksum == (uint32_t)rsprot_g4(b);
}
