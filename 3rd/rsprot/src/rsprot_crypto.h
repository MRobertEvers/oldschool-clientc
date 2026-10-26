#ifndef RSPROT_CRYPTO_H
#define RSPROT_CRYPTO_H

/*
 * RSProt's crypto module (ISAAC stream cipher, XTEA, RSA modPow) -- a port of
 * RSProt's `crypto/src/main/kotlin/net/rsprot/crypto/`, self-contained: the
 * library depends on nothing outside itself.
 *
 *   ISAAC  IsaacRandom.kt (from OpenRS2): the same init and scramble.
 *   XTEA   XteaKey / xteaDecrypt: delta 0x9E3779B9, 32 rounds, whole 8-byte
 *          blocks in place.
 *   RSA    Rsa.kt's modPow, on a small bignum of its own (src/rsprot_crypto.c):
 *          the login block's fixed-size envelope, keys up to 4096 bits.
 *
 * The encoders/decoders take an `RsprotCipherPair *`, matching every
 * `MessageEncoder.encode(streamCipher: StreamCipher, ...)` signature in RSProt
 * -- `nextInt()` is called once per packet to derive the opcode's ISAAC offset
 * before the wire opcode is written.
 */

#include <stdbool.h>
#include <stdint.h>

/* --- stream cipher -------------------------------------------------------
 *
 * A session that never sent a login seed (a js5 or update-server connection)
 * uses RSPROT_CIPHER_NOP and gets 0 every call, exactly as NopStreamCipher.kt
 * does.
 */

typedef enum RsprotCipherKind {
	RSPROT_CIPHER_NOP = 0,
	RSPROT_CIPHER_ISAAC = 1,
} RsprotCipherKind;

/* IsaacRandom's state. */
typedef struct RsprotIsaac {
	int32_t count;
	uint32_t rsl[256];
	uint32_t mem[256];
	uint32_t a;
	uint32_t b;
	uint32_t c;
} RsprotIsaac;

typedef struct RsprotCipher {
	RsprotCipherKind kind;
	RsprotIsaac isaac; /* when kind == RSPROT_CIPHER_ISAAC */
} RsprotCipher;

/* A server holds one ISAAC pair per session: encode with the pair seeded one
 * way, decode with the other (seed[i] + 50 on one side, as the login block
 * says). */
typedef struct RsprotCipherPair {
	RsprotCipher encode;
	RsprotCipher decode;
} RsprotCipherPair;

void rsprot_cipher_init_nop(RsprotCipher *c);
/* Seeds ISAAC from `seed[0..3]` (the four ints the login block hands over). */
bool rsprot_cipher_init_isaac(RsprotCipher *c, const int32_t seed[4]);
void rsprot_cipher_destroy(RsprotCipher *c);
int32_t rsprot_cipher_next(RsprotCipher *c);
void rsprot_cipher_pair_destroy(RsprotCipherPair *p);

/* --- XTEA ------------------------------------------------------------------ */

extern const int32_t RSPROT_XTEA_KEY_ZERO[4];

/* Decrypts `len` bytes at `data` in place (whole 8-byte blocks only; a tail
 * shorter than a block is left as it is). */
void rsprot_xtea_decrypt(void *data, int32_t len, const int32_t key[4]);
void rsprot_xtea_encrypt(void *data, int32_t len, const int32_t key[4]);

/* --- RSA -------------------------------------------------------------------
 *
 * Rsa.kt's modPow is exp/mod BigIntegers over an arbitrary base; the login
 * block is always a fixed-size envelope, so this exposes exactly that shape.
 * Init once from the server's key, decrypt per login.
 */

#define RSPROT_RSA_LIMBS 128 /* 4096 bits */

typedef struct RsprotRsaKey {
	uint32_t exponent[RSPROT_RSA_LIMBS];
	uint32_t modulus[RSPROT_RSA_LIMBS];
	int32_t exponent_limbs;
	int32_t modulus_limbs;
} RsprotRsaKey;

/* exponent/modulus as hex strings (no leading "0x"). Returns false on a
 * malformed key, an even modulus or one wider than 4096 bits. */
bool rsprot_rsa_init(RsprotRsaKey *key, const char *exponent_hex, const char *modulus_hex);

/* Deciphers `len` bytes at `in` (the raw RSA block off the wire, big-endian)
 * into `out` (capacity `out_cap`): the modPow result, big-endian, as many bytes
 * as it needs and no leading zeros. Returns the number of bytes written, or -1
 * if out_cap was too small or the block is wider than the key. */
int32_t rsprot_rsa_decrypt(const RsprotRsaKey *key, const void *in, int32_t len, void *out,
                           int32_t out_cap);

void rsprot_rsa_destroy(RsprotRsaKey *key);

#endif /* RSPROT_CRYPTO_H */
