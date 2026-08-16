#ifndef XTEAS_H
#define XTEAS_H

#include <stdint.h>

/** XTEA key is 4 x int32 (16 bytes). Decrypts in place; leftover bytes past full 8-byte blocks are
 * unchanged. */
void
xteas_decrypt(
    char* data,
    int data_length,
    int32_t* key);

/** Inverse of xteas_decrypt. Encrypts in place; leftover bytes past full 8-byte
 * blocks are unchanged, so encrypt-then-decrypt reproduces the input exactly for
 * any length, including lengths that are not a multiple of 8. */
void
xteas_encrypt(
    char* data,
    int data_length,
    int32_t* key);

#endif
