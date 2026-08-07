#ifndef RSPROT_COMPRESSION_H
#define RSPROT_COMPRESSION_H

/*
 * RSProt's `compression` module — see `compression/src/main/kotlin/net/rsprot/
 * compression/`. Despite the name it is not bzip/gzip (this repo already has
 * those in 3rd/bzip and 3rd/miniz, used by rscache for cache archives, and
 * this library has no reason to touch them): it is the **client-chat Huffman
 * codec** and **Base37 name encoding**, two independent small algorithms that
 * happen to be gathered under this one Gradle module upstream.
 *
 * This repo already has a base-37 codec (`src/net/jbase37.c`) used for
 * friends-list names. It is NOT reused here: RSProt's `Base37.encode` breaks
 * out of the accumulation loop once the value passes
 * MINIMUM_POSSIBLE_12_CHARACTER_VALUE (silently truncating characters beyond
 * that point) and `decodeWithCase` reconstructs mixed-case names from the
 * lone `_` separator — behaviour the existing `strtobase37`/`base37tostr`
 * do not claim to replicate and were never asked to. A port of a 100-line
 * algorithm is cheaper and safer here than proving two independent
 * implementations agree on every edge case.
 */

#include "rsprot_buf.h"

#include <stdbool.h>
#include <stdint.h>

/* --- Base37 --------------------------------------------------------------
 *
 * Player/clan names on the wire are packed into a base-37 int64: `_` maps to
 * 0, `a`-`z` to 1-26, `0`-`9` to 27-36. Case is not part of the alphabet — the
 * client display-cases the decode by upper-casing the first letter and every
 * letter after an `_` (which decodeWithCase renders as a non-breaking space).
 */

/* Encodes up to 12 valid characters (a-z, A-Z, 0-9, _); characters beyond the
 * point the accumulator would overflow a 12-character value are silently
 * dropped, matching Base37.kt's early break — this is deliberately not
 * flagged as an error, since RSProt itself treats it as truncation, not
 * failure. */
int64_t rsprot_base37_encode(const char *s);

/* Decodes into `out` (capacity `out_cap`, NUL-terminated). Returns the string
 * length written (excluding the NUL), or -1 if `encoded` is out of range or
 * not a valid base-37 value (divisible by 37) — matching Base37.decode's two
 * `require`s. */
int32_t rsprot_base37_decode(int64_t encoded, char *out, int32_t out_cap);

/* As above, but restoring display case: first letter and the letter after
 * each `_` are upper-cased, and `_` itself renders as U+00A0 (encoded as the
 * single Cp1252 byte 0xA0, matching Kotlin's NBSP=160 char literal) rather
 * than a literal underscore. `out_cap` must be large enough for the
 * substitution (same character count as the plain decode). */
int32_t rsprot_base37_decode_with_case(int64_t encoded, char *out, int32_t out_cap);

/* --- Huffman chat codec ----------------------------------------------------
 *
 * The table is 256 bytes read from the cache (one bit-length per byte value,
 * "huffman" file 12/xxx on osrs), the same 256 bytes on every revision this
 * project's cache pack carries. `rsprot_huffman_create` builds the codeword +
 * lookup-tree tables from it once at boot; `encode`/`decode` are per-message.
 */

typedef struct RsprotHuffman {
	int32_t bits[256];
	int32_t codewords[256];
	int32_t *lookup_tree; /* grows as create() discovers it needs to */
	int32_t lookup_tree_len;
	int32_t max_bits_per_char;
} RsprotHuffman;

/* `table` must be exactly 256 bytes (the cache's huffman table, one bit-count
 * per byte value). Returns false if it is not, or on allocation failure. */
bool rsprot_huffman_create(RsprotHuffman *h, const uint8_t table[256]);

void rsprot_huffman_destroy(RsprotHuffman *h);

/* Encodes `text` (Cp1252 bytes, NUL-terminated) into `buf` as
 * pSmart1or2(byteLen) followed by the bit-packed codewords. Sets
 * RSPROT_BUF_ERR_RANGE on the buffer if a byte has no codeword (bits[byte]==0)
 * or if the encoded length would reach 32768. */
void rsprot_huffman_encode(const RsprotHuffman *h, RsprotBuf *buf, const char *text);

/* Decodes a Huffman-coded string from `buf` into `out` (capacity `out_cap`,
 * NUL-terminated). Returns the byte length written (excluding the NUL), or -1
 * if `out_cap` was too small — the caller should size `out` from a
 * conservative estimate (the wire length is 1 codeword-bit minimum per byte,
 * so it never decodes to more bytes than the encoded bit count, but a decoder
 * that wants an exact bound should read the smart-encoded length itself
 * first: it is the first thing on the wire). */
int32_t rsprot_huffman_decode(const RsprotHuffman *h, RsprotBuf *buf, char *out, int32_t out_cap);

#endif /* RSPROT_COMPRESSION_H */
