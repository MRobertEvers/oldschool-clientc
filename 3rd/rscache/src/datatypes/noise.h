#ifndef RSCACHE_DATATYPES_NOISE_H
#define RSCACHE_DATATYPES_NOISE_H

int
RSCache_NoisePerlinNoise(
    int x,
    int y,
    int freq);

/**
 * Select the 2,048-entry 16.16 cosine table used for noise interpolation.
 * RSCache does not take ownership. Passing NULL restores its built-in table.
 */
void
RSCache_NoiseSetCosTable(const int* table);

/** Return the cosine-table pointer currently used by RSCache. */
const int*
RSCache_NoiseGetCosTable(void);

#endif
