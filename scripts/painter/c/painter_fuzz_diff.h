#ifndef PAINTER_FUZZ_DIFF_H
#define PAINTER_FUZZ_DIFF_H

#include "painters.h"

#include <stdint.h>

#define PAINTER_FUZZ_MAX_TERRAIN 16384
#define PAINTER_FUZZ_MAX_ELEMENT 16384

typedef struct
{
    uint64_t terrain[PAINTER_FUZZ_MAX_TERRAIN];
    int terrain_n;
    uint32_t elements[PAINTER_FUZZ_MAX_ELEMENT];
    int element_n;
} PainterFuzzDrawnSet;

void painter_fuzz_drawn_from_buffer(PainterFuzzDrawnSet* d, const struct PaintersBuffer* buf);

int painter_fuzz_compare_superset(
    const PainterFuzzDrawnSet* reference,
    const PainterFuzzDrawnSet* bucket,
    int* missing_terrain,
    int* missing_elements);

void painter_fuzz_print_drawn_miss(
    const PainterFuzzDrawnSet* reference,
    const PainterFuzzDrawnSet* bucket,
    int max_print);

/* out[0]=scenery, [1]=wall, [2]=walldecor, [3]=grounddecor, [4]=groundobj. */
void painter_fuzz_drawn_counts_from_set(const PainterFuzzDrawnSet* d, int out[5]);

#endif /* PAINTER_FUZZ_DIFF_H */
