/*
 * See rsprot_compression.h. Transcribed from
 * compression/src/main/kotlin/net/rsprot/compression/{Base37,HuffmanCodec}.kt.
 */

#include "rsprot_compression.h"

#include <stdlib.h>
#include <string.h>

/* --- Base37 --------------------------------------------------------------- */

#define BASE37_MAX 6582952005840035280LL
#define BASE37_MIN 177917621779460413LL
#define BASE37_NBSP 0xA0

static const char BASE37_ALPHABET[37] = "_abcdefghijklmnopqrstuvwxyz0123456789";

int64_t rsprot_base37_encode(const char *s)
{
	int64_t encoded = 0;
	const char *p;

	for (p = s; *p; p++) {
		char c = *p;
		encoded *= 37;
		if (c >= 'A' && c <= 'Z') {
			encoded += (int64_t)(c - 'A' + 1);
		} else if (c >= 'a' && c <= 'z') {
			encoded += (int64_t)(c - 'a' + 1);
		} else if (c >= '0' && c <= '9') {
			encoded += (int64_t)(c - '0' + 27);
		}
		/* anything else contributes 0, same as the Kotlin `when`'s implicit
		 * else branch */
		if (encoded >= BASE37_MIN)
			break;
	}
	while (encoded % 37 == 0 && encoded != 0)
		encoded /= 37;
	return encoded;
}

/* Shared digit-extraction loop for decode/decodeWithCase. Writes the base-37
 * digits (0..36) into `digits`, most-significant first, and returns the
 * count. `digits` must hold at least 12 entries — MAXIMUM_POSSIBLE_12_
 * CHARACTER_VALUE guarantees no more than 12 digits ever come out. */
static int32_t base37_digits(int64_t encoded, int digits[12])
{
	int32_t length = 0;
	int64_t rem = encoded;
	int i;

	while (rem != 0) {
		int64_t cur = rem;
		rem /= 37;
		digits[length++] = (int)(cur - rem * 37);
	}
	/* digits came out least-significant-first; reverse in place. */
	for (i = 0; i < length / 2; i++) {
		int tmp = digits[i];
		digits[i] = digits[length - 1 - i];
		digits[length - 1 - i] = tmp;
	}
	return length;
}

int32_t rsprot_base37_decode(int64_t encoded, char *out, int32_t out_cap)
{
	int digits[12];
	int32_t length;
	int32_t i;

	if (encoded == 0) {
		if (out_cap < 1)
			return -1;
		out[0] = '\0';
		return 0;
	}
	if (encoded < 0 || encoded > BASE37_MAX || encoded % 37 == 0)
		return -1;

	length = base37_digits(encoded, digits);
	if (length + 1 > out_cap)
		return -1;
	for (i = 0; i < length; i++)
		out[i] = BASE37_ALPHABET[digits[i]];
	out[length] = '\0';
	return length;
}

int32_t rsprot_base37_decode_with_case(int64_t encoded, char *out, int32_t out_cap)
{
	int digits[12];
	int32_t length;
	int32_t i;
	bool uppercase_next = true;

	if (encoded == 0) {
		if (out_cap < 1)
			return -1;
		out[0] = '\0';
		return 0;
	}
	if (encoded < 0 || encoded > BASE37_MAX || encoded % 37 == 0)
		return -1;

	length = base37_digits(encoded, digits);
	if (length + 1 > out_cap)
		return -1;

	/* Kotlin builds this back to front (appending while `rem` shrinks, then
	 * reversing), which is why the underscore-uppercases-the-PRECEDING-
	 * character rule reads oddly in the original — by the time it runs, that
	 * character is the last one appended so far. Walking the already-
	 * forward-ordered digit list left to right and tracking "uppercase the
	 * next letter" produces the identical output more directly. */
	for (i = 0; i < length; i++) {
		int d = digits[i];
		if (d == 0) {
			out[i] = (char)(uint8_t)BASE37_NBSP;
			uppercase_next = true;
		} else {
			char c = BASE37_ALPHABET[d];
			if (uppercase_next && c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			out[i] = c;
			uppercase_next = false;
		}
	}
	out[length] = '\0';
	return length;
}

/* --- Huffman chat codec ----------------------------------------------------
 *
 * A straight transcription of HuffmanCodec.create()/encode()/decode(). The
 * lookup tree doubles in size exactly when the Kotlin does, so the two stay
 * in lockstep node-for-node — that matters because a divergence would only
 * show up as a codec that decodes to the wrong characters, never a crash.
 */

static bool tree_ensure(RsprotHuffman *h, int32_t node)
{
	while (node >= h->lookup_tree_len) {
		int32_t new_len = h->lookup_tree_len * 2;
		int32_t *grown = (int32_t *)realloc(h->lookup_tree, (size_t)new_len * sizeof(int32_t));
		if (!grown)
			return false;
		memset(grown + h->lookup_tree_len, 0, (size_t)(new_len - h->lookup_tree_len) * sizeof(int32_t));
		h->lookup_tree = grown;
		h->lookup_tree_len = new_len;
	}
	return true;
}

bool rsprot_huffman_create(RsprotHuffman *h, const uint8_t table[256])
{
	int32_t next_codewords[32] = { 0 };
	int32_t next_free_node = 0;
	int chr;
	int32_t max_bits = 0;

	memset(h, 0, sizeof(*h));
	h->lookup_tree_len = 8;
	h->lookup_tree = (int32_t *)calloc((size_t)h->lookup_tree_len, sizeof(int32_t));
	if (!h->lookup_tree)
		return false;

	for (chr = 0; chr < 256; chr++) {
		h->bits[chr] = table[chr];
		if (table[chr] > max_bits)
			max_bits = table[chr];
	}
	h->max_bits_per_char = max_bits;

	for (chr = 0; chr < 256; chr++) {
		int32_t num_bits = h->bits[chr];
		int32_t codeword;
		int32_t node;
		int i;

		if (num_bits == 0)
			continue;

		codeword = next_codewords[num_bits - 1];
		h->codewords[chr] = codeword;

		for (i = num_bits; i >= 1; i--) {
			int32_t next_codeword = next_codewords[i - 1];
			int32_t bit;

			if (codeword != next_codeword)
				break;

			bit = (int32_t)((uint32_t)1 << (32 - i));
			if ((next_codeword & bit) == 0) {
				next_codewords[i - 1] = next_codeword | bit;
			} else if (i != 1) {
				next_codewords[i - 1] = next_codewords[i - 2];
			} else {
				next_codewords[0] = 0;
			}
		}

		for (i = num_bits + 1; i <= 32; i++) {
			if (next_codewords[i - 1] == codeword)
				next_codewords[i - 1] = next_codewords[num_bits - 1];
		}

		node = 0;
		for (i = 1; i <= num_bits; i++) {
			int32_t bit = (int32_t)((uint32_t)1 << (32 - i));
			if ((codeword & bit) == 0) {
				node++;
			} else {
				if (h->lookup_tree[node] == 0)
					h->lookup_tree[node] = next_free_node;
				node = h->lookup_tree[node];
			}
			if (!tree_ensure(h, node)) {
				free(h->lookup_tree);
				h->lookup_tree = NULL;
				return false;
			}
		}

		h->lookup_tree[node] = ~chr;
		if (node >= next_free_node)
			next_free_node = node + 1;
	}

	for (chr = 0; chr < 256; chr++) {
		if (h->bits[chr] != 0)
			h->codewords[chr] = (int32_t)((uint32_t)h->codewords[chr] >> (32 - h->bits[chr]));
	}

	return true;
}

void rsprot_huffman_destroy(RsprotHuffman *h)
{
	if (!h)
		return;
	free(h->lookup_tree);
	memset(h, 0, sizeof(*h));
}

void rsprot_huffman_encode(const RsprotHuffman *h, RsprotBuf *buf, const char *text)
{
	size_t len = strlen(text);
	size_t i;

	if (len >= 32768) {
		buf->err |= RSPROT_BUF_ERR_RANGE;
		return;
	}
	rsprot_psmart1or2(buf, (int32_t)len);
	if (!rsprot_buf_ok(buf))
		return;

	/* Netty's ensureWritable equivalent: worst case every char costs
	 * max_bits_per_char bits. */
	rsprot_buf_ensure(buf, (int32_t)(((int64_t)h->max_bits_per_char * (int64_t)len + 8) / 8));

	rsprot_buf_bit_begin(buf);
	for (i = 0; i < len; i++) {
		int32_t byte_val = (uint8_t)text[i];
		int32_t num_bits = h->bits[byte_val];
		if (num_bits == 0) {
			buf->err |= RSPROT_BUF_ERR_RANGE;
			break;
		}
		rsprot_pbits(buf, num_bits, h->codewords[byte_val]);
	}
	rsprot_buf_bit_end(buf);
}

int32_t rsprot_huffman_decode(const RsprotHuffman *h, RsprotBuf *buf, char *out, int32_t out_cap)
{
	int32_t len = rsprot_gsmart1or2(buf);
	int32_t node = 0;
	int32_t i = 0;

	if (!rsprot_buf_ok(buf))
		return -1;
	if (len + 1 > out_cap)
		return -1;

	rsprot_buf_bit_begin(buf);
	while (i < len) {
		int32_t chr;
		if (rsprot_gbits(buf, 1) == 1)
			node = h->lookup_tree[node];
		else
			node++;
		if (!rsprot_buf_ok(buf))
			break;

		chr = h->lookup_tree[node];
		if (chr & (int32_t)0x80000000u) {
			out[i++] = (char)(uint8_t)(~chr);
			node = 0;
		}
	}
	rsprot_buf_bit_end(buf);
	out[i] = '\0';
	return i;
}
