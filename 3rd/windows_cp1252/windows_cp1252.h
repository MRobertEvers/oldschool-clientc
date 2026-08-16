#ifndef WINDOWS_CP1252_H
#define WINDOWS_CP1252_H

#include <stdbool.h>
#include <stdint.h>

// https://en.wikipedia.org/wiki/Windows-1252

// input is a basic char array (Latin-1/ASCII)
uint32_t
cp1252_encode_string(
    const char* src,
    uint8_t* dst,
    uint32_t dst_offset,
    uint32_t len);

uint16_t
cp1252_decode_to_utf16(uint8_t b);

bool
cp1252_inrange(uint16_t c);

uint8_t
cp1252_encode_from_utf16(uint16_t c);

#endif