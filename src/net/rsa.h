#ifndef RSA_H_
#define RSA_H_

#include "3rd/tommath/tommath.h"

#include <stddef.h>

struct rsa
{
    mp_int exponent;
    mp_int modulus;
};

int
rsa_init(
    struct rsa* rsa,
    const char* exponent,
    const char* modulus);

int
rsa_crypt(
    struct rsa* rsa,
    void* buffer,
    size_t len,
    void* out,
    size_t outlen);

#endif