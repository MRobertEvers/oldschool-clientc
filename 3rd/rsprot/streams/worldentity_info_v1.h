#ifndef RSPROT_STREAM_WORLDENTITY_INFO_V1_H
#define RSPROT_STREAM_WORLDENTITY_INFO_V1_H

/*
 * WORLDENTITY_INFO_V1 / _V2 -- server -> client, revisions 223-226.
 *
 * RSProx reads both versions with one fixed layout (GenericWorldEntityInfoClient,
 * protocol/src: `version >= 3` takes the V2 path, 1 and 2 the V1 path), plain
 * bytes with no per-revision transform, so this is one hand-written reader with
 * an entry point per revision that carries the prot -- not the generated
 * <stream>_osrs<N> files the V3+ versions have (spec/streams/worldentity_info.json
 * models V3+):
 *
 *   update   g1 count, then per entry of the previous list: g1 keep; when kept,
 *            g1 dx, g1 dz (tiles), g2 angle, g1 move speed
 *   spawn    while 10 bytes remain: g2 index, g1 size x and g1 size z (zones),
 *            g1 x and g1 z in the build area (tiles), g2 angle, g2 (unknown)
 */

#include "rsprot_info.h"

/** The reader both versions run. */
int worldentity_info_v1(const uint8_t *data, int32_t len, RsprotInfoList *out);

int worldentity_info_v1_osrs223(const uint8_t *data, int32_t len, RsprotInfoList *out);
int worldentity_info_v1_osrs224(const uint8_t *data, int32_t len, RsprotInfoList *out);
int worldentity_info_v1_osrs225(const uint8_t *data, int32_t len, RsprotInfoList *out);
int worldentity_info_v1_osrs226(const uint8_t *data, int32_t len, RsprotInfoList *out);
int worldentity_info_v2_osrs225(const uint8_t *data, int32_t len, RsprotInfoList *out);
int worldentity_info_v2_osrs226(const uint8_t *data, int32_t len, RsprotInfoList *out);

/** worldentity_info_v1_osrs<revision> / _v2_, or NULL where it has no such prot. */
RsprotWorldEntityInfoReadFn worldentity_info_v1_osrs(int revision);
RsprotWorldEntityInfoReadFn worldentity_info_v2_osrs(int revision);

#endif
