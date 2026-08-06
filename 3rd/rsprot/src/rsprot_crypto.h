#ifndef RSPROT_CRYPTO_H
#define RSPROT_CRYPTO_H

/*
 * RSProt's crypto module (ISAAC stream cipher, XTEA, RSA modPow) — see
 * `crypto/src/main/kotlin/net/rsprot/crypto/`.
 *
 * This is an ADAPTER, not a port: this repo already carries the same three
 * algorithms — `src/net/isaac.c` (ISAAC, byte-identical scramble/init to
 * RSProt's `IsaacRandom.kt`, both taken from OpenRS2), `3rd/xteas/xteas.c`
 * (XTEA, same delta/rounds/key-schedule), and `src/net/rsa.c` (RSA modPow over
 * `3rd/tommath`, same operation RSProt's `Rsa.kt` does via BigInteger/JNA-GMP).
 * Re-deriving them here would be a second copy of a security primitive to keep
 * in sync — the one thing worth never doing twice. Instead this header gives
 * the rsprot codecs RSProt's own names (`StreamCipher`, `StreamCipherPair`,
 * `XteaKey`) over the existing implementations, so a transcribed encoder reads
 * the same regardless of which tree its dependency lives in.
 *
 * The encoders/decoders this library generates take an `RsprotCipherPair *`,
 * matching every `MessageEncoder.encode(streamCipher: StreamCipher, ...)`
 * signature in RSProt — `nextInt()` is called once per packet to derive the
 * opcode's ISAAC offset before the wire opcode is written.
 */

/* Pulled in via the build's include path (src/net/) rather than a relative
 * path, so this header works both from 3rd/rsprot's own makefile (which adds
 * -I../../src/net) and from src/Makefile (which already has src/net on its
 * include path for every other translation unit). */
#include "isaac.h"
#include "rsa.h"
#include "xteas.h"

#include <stdbool.h>
#include <stdint.h>

/* --- stream cipher -------------------------------------------------------
 *
 * StreamCipher.kt is an interface with one method; IsaacRandom and
 * NopStreamCipher are its two implementations. C has no interface to model, so
 * this is a tagged union: a login/JS5 exchange that never sets up ISAAC keys
 * (osrs230's classic path, and any future revision that omits the RSA block)
 * uses RSPROT_CIPHER_NOP and gets 0 every call, exactly as NopStreamCipher.kt
 * does — not a null check the caller has to remember.
 */

typedef enum RsprotCipherKind {
	RSPROT_CIPHER_NOP = 0,
	RSPROT_CIPHER_ISAAC = 1,
} RsprotCipherKind;

typedef struct RsprotCipher {
	RsprotCipherKind kind;
	struct Isaac *isaac; /* owned when kind == RSPROT_CIPHER_ISAAC */
} RsprotCipher;

/* A server holds one ISAAC pair per session: encode with the pair seeded one
 * way, decode with the reverse-order seed. StreamCipherPair.kt. */
typedef struct RsprotCipherPair {
	RsprotCipher encode;
	RsprotCipher decode;
} RsprotCipherPair;

void rsprot_cipher_init_nop(RsprotCipher *c);

/* Seeds ISAAC from `seed[0..3]` (the four ints the login block hands over) —
 * mirrors `IsaacRandom(seed: IntArray)`. Returns false on allocation failure. */
bool rsprot_cipher_init_isaac(RsprotCipher *c, const int32_t seed[4]);

void rsprot_cipher_destroy(RsprotCipher *c);

int32_t rsprot_cipher_next(RsprotCipher *c);

void rsprot_cipher_pair_destroy(RsprotCipherPair *p);

/* --- XTEA ------------------------------------------------------------------
 *
 * XteaKey.kt is 4 ints plus equals/hashCode; the C port is the plain array
 * xteas_decrypt/xteas_encrypt already take, so no wrapper type is needed. Kept
 * here only as the ZERO constant RSProt exposes, since a caller transcribing
 * `XteaKey.ZERO` should find the same name.
 */
extern const int32_t RSPROT_XTEA_KEY_ZERO[4];

/* Decrypts `len` bytes at `data` in place (whole 8-byte blocks only, matching
 * xteas_decrypt's trailing-byte behavior — see 3rd/xteas/xteas.h). */
static inline void rsprot_xtea_decrypt(void *data, int32_t len, const int32_t key[4])
{
	xteas_decrypt((char *)data, len, (int32_t *)key);
}

static inline void rsprot_xtea_encrypt(void *data, int32_t len, const int32_t key[4])
{
	xteas_encrypt((char *)data, len, (int32_t *)key);
}

/* --- RSA -------------------------------------------------------------------
 *
 * Rsa.kt's modPow is exp/mod BigIntegers over an arbitrary base; the login
 * block is always a fixed-size envelope, so the adapter exposes exactly that
 * shape rather than a general BigInteger seam. `struct rsa` (src/net/rsa.h) is
 * the exponent+modulus pair; init once at boot from the server's key, decrypt
 * per login.
 */

typedef struct rsa RsprotRsaKey;

/* exponent/modulus as hex strings (no leading "0x"), matching rsa_init's radix.
 * Returns false on a malformed key. */
bool rsprot_rsa_init(RsprotRsaKey *key, const char *exponent_hex, const char *modulus_hex);

/* Deciphers `len` bytes at `in` (the raw RSA block off the wire) into `out`
 * (capacity `out_cap`). Returns the number of bytes written, or -1 if out_cap
 * was too small or the underlying modPow failed. Mirrors
 * `ByteBuf.decipherRsa` minus the buffer allocation — the caller owns `out`. */
int32_t rsprot_rsa_decrypt(RsprotRsaKey *key, const void *in, int32_t len, void *out, int32_t out_cap);

void rsprot_rsa_destroy(RsprotRsaKey *key);

#endif /* RSPROT_CRYPTO_H */
