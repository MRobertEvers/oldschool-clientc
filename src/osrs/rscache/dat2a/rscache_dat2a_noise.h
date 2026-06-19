#ifndef RSCACHE_RSCACHEDAT2A_NOISE_H
#define RSCACHE_RSCACHEDAT2A_NOISE_H

int
RSCacheDat2A_NoisePerlinNoise(
    int x,
    int y,
    int freq);

void
RSCacheDat2A_NoiseSetCosTable(const int* table);
const int*
RSCacheDat2A_NoiseGetCosTable(void);

#endif