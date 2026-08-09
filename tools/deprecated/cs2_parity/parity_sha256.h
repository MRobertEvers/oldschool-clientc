#ifndef PARITY_SHA256_H
#define PARITY_SHA256_H

#include <stddef.h>

void
parity_sha256_hex(const unsigned char* data, size_t len, char* out_hex64);

#endif
