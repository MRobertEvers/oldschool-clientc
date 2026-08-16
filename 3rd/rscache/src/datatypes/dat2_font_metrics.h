#ifndef RSCACHE_DATATYPES_DAT2_FONT_METRICS_H
#define RSCACHE_DATATYPES_DAT2_FONT_METRICS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Dat2 fonts-table metrics blob. Glyph bitmaps live in the sprites table at
 * the same archive id; this is only the per-codepoint advance table and ascent.
 *
 * Wire formats. Pin via cache->codec[RSCACHE_TYPE_FONT], or leave AUTO and let
 * RSCache_Dat2FontMetricsCodecVersion derive from the profile:
 *
 *   V1 — OldSchool: 257 bytes. advance[0..255], ascent at 256.
 *   V2 — RS2 dat2 (634, 643): 263 bytes. 2 reserved bytes (0 in every observed
 *        group), advance at 2..257, ascent at 258, then 4 trailing
 *        vertical-metric bytes the client uses and we do not.
 */
#define RSCACHE_CODEC_FONT_METRICS_V1 1
#define RSCACHE_CODEC_FONT_METRICS_V2 2

#define RSCACHE_DAT2_FONT_METRICS_V1_SIZE 257
#define RSCACHE_DAT2_FONT_METRICS_V2_SIZE 263

struct RSCache_Dat2FontMetrics
{
    int advance[256];
    int ascent;
};

struct RSCache;

/** Which font-metrics codec this cache needs. */
int
RSCache_Dat2FontMetricsCodecVersion(const struct RSCache* cache);

/**
 * Decode with an explicit codec version.
 * Returns false when the blob is too short for that codec (expected miss).
 */
bool
RSCache_Dat2FontMetricsDecodeCodec(
    const uint8_t* data,
    int data_size,
    int codec_version,
    struct RSCache_Dat2FontMetrics* out);

/** Decode using the codec the profile selects. */
bool
RSCache_Dat2FontMetricsDecodeProfile(
    const struct RSCache* cache,
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2FontMetrics* out);

#endif
