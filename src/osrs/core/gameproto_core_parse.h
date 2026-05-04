#ifndef OSRS_CORE_GAMEPROTO_CORE_PARSE_H
#define OSRS_CORE_GAMEPROTO_CORE_PARSE_H

#include <stdint.h>

/** Parse 4-byte map rebuild payload: two big-endian u16 (zonex, zonez). Returns 1 on success. */
int
gameproto_core_parse_maprebuild8_z16_x16_v1(
    uint8_t* data,
    int n,
    int* out_zonex,
    int* out_zonez);

#endif
