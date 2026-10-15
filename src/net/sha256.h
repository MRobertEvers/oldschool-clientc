#ifndef SRC_NET_SHA256_H
#define SRC_NET_SHA256_H

/*
 * SHA-256, here only because revision 239's login handshake needs it.
 *
 * The server may answer GAMELOGIN with a proof-of-work challenge instead of a
 * response: a version, a difficulty and a salt. The client must find a 64-bit
 * value whose hex, appended to `hex(version) + hex(difficulty) + salt`, hashes
 * to at least `difficulty` leading zero BITS. Both ends need the hash — the
 * client to search, the server to check — so it lives in net/ rather than in
 * either driver.
 */

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_BYTES 32
#define SHA256_BLOCK_BYTES 64

/*
 * The same hash, fed in pieces. The server's password hashing needs it: HMAC
 * hashes the same padded key ahead of every message, so PBKDF2 keeps the state
 * after the key block and copies it per iteration -- two compressions an
 * iteration instead of four. A context is plain data and may be copied.
 */
struct sha256_ctx
{
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[SHA256_BLOCK_BYTES];
    size_t buf_len;
};

void
sha256_init(struct sha256_ctx* ctx);

void
sha256_update(
    struct sha256_ctx* ctx,
    uint8_t const* data,
    size_t len);

void
sha256_final(
    struct sha256_ctx* ctx,
    uint8_t out[SHA256_DIGEST_BYTES]);

void
sha256(
    uint8_t const* data,
    size_t len,
    uint8_t out[SHA256_DIGEST_BYTES]);

/** Leading zero bits of a digest, counted the way the client counts them:
 *  whole-byte runs first, then the remaining bits of the first non-zero byte. */
int
sha256_leading_zero_bits(uint8_t const digest[SHA256_DIGEST_BYTES]);

#endif
