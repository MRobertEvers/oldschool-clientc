#include "rsprot_crypto.h"

#include <string.h>

const int32_t RSPROT_XTEA_KEY_ZERO[4] = { 0, 0, 0, 0 };

void rsprot_cipher_init_nop(RsprotCipher *c)
{
	c->kind = RSPROT_CIPHER_NOP;
	c->isaac = NULL;
}

bool rsprot_cipher_init_isaac(RsprotCipher *c, const int32_t seed[4])
{
	int seed_copy[4];
	int i;

	for (i = 0; i < 4; i++)
		seed_copy[i] = (int)seed[i];

	c->isaac = isaac_new(seed_copy, 4);
	if (!c->isaac) {
		c->kind = RSPROT_CIPHER_NOP;
		return false;
	}
	c->kind = RSPROT_CIPHER_ISAAC;
	return true;
}

void rsprot_cipher_destroy(RsprotCipher *c)
{
	if (!c)
		return;
	if (c->kind == RSPROT_CIPHER_ISAAC && c->isaac)
		isaac_free(c->isaac);
	c->isaac = NULL;
	c->kind = RSPROT_CIPHER_NOP;
}

int32_t rsprot_cipher_next(RsprotCipher *c)
{
	if (c->kind == RSPROT_CIPHER_ISAAC)
		return (int32_t)isaac_next(c->isaac);
	return 0; /* NopStreamCipher.nextInt() */
}

void rsprot_cipher_pair_destroy(RsprotCipherPair *p)
{
	if (!p)
		return;
	rsprot_cipher_destroy(&p->encode);
	rsprot_cipher_destroy(&p->decode);
}

bool rsprot_rsa_init(RsprotRsaKey *key, const char *exponent_hex, const char *modulus_hex)
{
	return rsa_init(key, exponent_hex, modulus_hex) == 0;
}

int32_t rsprot_rsa_decrypt(RsprotRsaKey *key, const void *in, int32_t len, void *out, int32_t out_cap)
{
	/* rsa_crypt writes the big-endian modPow result right-aligned into `out`
	 * and returns the byte count actually produced (BigInteger-style: no
	 * leading zero padding) — matching decipherRsa's `toByteArray()`, which
	 * is exactly as long as the result needs to be. */
	int written = (int)rsa_crypt(key, (void *)(uintptr_t)in, (size_t)len, out, (size_t)out_cap);
	if (written < 0)
		return -1;
	return written;
}

void rsprot_rsa_destroy(RsprotRsaKey *key)
{
	(void)key; /* mp_int cleanup: rsa.h does not expose a destructor today.
	            * tommath ints here are fixed-size server-lifetime keys (one
	            * init at boot), so leaking the mp_int's internal digit array
	            * at process exit is the same shape as every other 3rd/rscache
	            * "load once, live for the process" table. Nothing to free. */
}
