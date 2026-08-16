#include "dat2_font_metrics.h"

#include "../rscache_profile.h"

#include <assert.h>
#include <string.h>

int
RSCache_Dat2FontMetricsCodecVersion(const struct RSCache* cache)
{
    assert(cache);
    /* RS2 dat2 fonts carry a 263-byte blob (2 reserved + advances + ascent +
     * 4 trailing vertical metrics). OldSchool never took that change. */
    int derived = RSCache_IsRs2Dat2(cache) ? RSCACHE_CODEC_FONT_METRICS_V2
                                           : RSCACHE_CODEC_FONT_METRICS_V1;
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_FONT, derived);
}

bool
RSCache_Dat2FontMetricsDecodeCodec(
    const uint8_t* data,
    int data_size,
    int codec_version,
    struct RSCache_Dat2FontMetrics* out)
{
    assert(data);
    assert(out);

    int advance_base;
    int ascent_off;
    int min_size;

    if( codec_version == RSCACHE_CODEC_FONT_METRICS_V2 )
    {
        advance_base = 2;
        ascent_off = 258;
        min_size = RSCACHE_DAT2_FONT_METRICS_V2_SIZE;
    }
    else if( codec_version == RSCACHE_CODEC_FONT_METRICS_V1 )
    {
        advance_base = 0;
        ascent_off = 256;
        min_size = RSCACHE_DAT2_FONT_METRICS_V1_SIZE;
    }
    else
    {
        return false;
    }

    if( data_size < min_size )
        return false;

    memset(out, 0, sizeof(*out));
    for( int i = 0; i < 256; i++ )
        out->advance[i] = data[advance_base + i] & 0xFF;
    out->ascent = data[ascent_off] & 0xFF;
    return true;
}

bool
RSCache_Dat2FontMetricsDecodeProfile(
    const struct RSCache* cache,
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2FontMetrics* out)
{
    assert(cache);
    return RSCache_Dat2FontMetricsDecodeCodec(
        data, data_size, RSCache_Dat2FontMetricsCodecVersion(cache), out);
}
