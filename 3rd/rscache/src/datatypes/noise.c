#include "noise.h"

#include <assert.h>
#include <stdint.h>

// clang-format off
#include "noise_cos_table.h"
// clang-format on

static const int* g_noise_cos_table = RSCache_NoiseCosTableBuiltin;

static inline int
cosine2048(int index)
{
    assert(index >= 0 && index < 2048);
    return g_noise_cos_table[index];
}

static int
cosine_interpolate(
    int x,
    int y,
    int fraction,
    int freq)
{
    int cos_interp = (65536 - cosine2048((fraction * 1024) / freq)) >> 1;
    return ((cos_interp * y) >> 16) + (((65536 - cos_interp) * x) >> 16);
}

/* The reference's int hash wraps on purpose; in uint32_t the wrap is
 * defined. The masked result is the same non-negative int. */
static inline int
fade(int n)
{
    uint32_t u = (uint32_t)n;
    u = (u << 13) ^ u;
    return (int)((u * (u * u * 15731u + 789221u) + 1376312589u) & 0x7fffffffu);
}

static inline int
fadexy(
    int x,
    int y)
{
    return (fade(y * 57 + x) >> 19) & 0xff;
}

static int
smooth_fadexy(
    int x,
    int y)
{
    int corners =
        fadexy(x - 1, y - 1) + fadexy(x + 1, y - 1) + fadexy(x - 1, y + 1) + fadexy(x + 1, y + 1);
    int sides = fadexy(x - 1, y) + fadexy(x + 1, y) + fadexy(x, y - 1) + fadexy(x, y + 1);
    int center = fadexy(x, y);
    return (corners / 16) + (sides / 8) + (center / 4);
}

int
RSCache_NoisePerlinNoise(
    int x,
    int y,
    int freq)
{
    assert(freq > 0);

    int period_x = x / freq;
    int frac_x = x & (freq - 1);
    int period_y = y / freq;
    int frac_y = y & (freq - 1);
    int v1 = smooth_fadexy(period_x, period_y);
    int v2 = smooth_fadexy(period_x + 1, period_y);
    int v3 = smooth_fadexy(period_x, period_y + 1);
    int v4 = smooth_fadexy(period_x + 1, period_y + 1);
    int i1 = cosine_interpolate(v1, v2, frac_x, freq);
    int i2 = cosine_interpolate(v3, v4, frac_x, freq);
    int value = cosine_interpolate(i1, i2, frac_y, freq);
    return value;
}

void
RSCache_NoiseSetCosTable(const int* table)
{
    g_noise_cos_table = table ? table : RSCache_NoiseCosTableBuiltin;
}

const int*
RSCache_NoiseGetCosTable(void)
{
    return g_noise_cos_table;
}
