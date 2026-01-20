#ifndef ARCHIVE_DECOMPRESS_H
#define ARCHIVE_DECOMPRESS_H

#include "archive.h"

#include <stdint.h>

bool
archive_decompress(struct Dat2Archive* archive);
// Decrypt and decompress the archive data using the provided XTEA key
// If xtea_key_nullable is NULL, the data will not be decrypted
bool
archive_decrypt_decompress(
    struct Dat2Archive* archive,
    uint32_t* xtea_key_nullable);

bool
archive_decompress_dat(struct Dat2Archive* archive);

#endif