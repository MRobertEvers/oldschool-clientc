#ifndef RSCACHE_DATATYPES_NOISE_H
#define RSCACHE_DATATYPES_NOISE_H

int
RSCache_NoisePerlinNoise(
    int x,
    int y,
    int freq);

void
RSCache_NoiseSetCosTable(const int* table);

const int*
RSCache_NoiseGetCosTable(void);

#endif
