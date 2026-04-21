#ifndef FLAT_SCREEN_EDGES_H
#define FLAT_SCREEN_EDGES_H

/**
 * True if for all k in [0, n), affine edges e_a + k*step_a and e_b + k*step_b satisfy
 * horizontal screen interior (no left/right clip): (e_a>>16)>=0 and (e_b>>16)<screen_width.
 * Affine extrema on a segment occur at endpoints k=0 or k=n-1.
 */
static inline int
flat_screen_fixed_edges_no_hclip(
    int e_a0,
    int step_a,
    int e_b0,
    int step_b,
    int n,
    int screen_width)
{
    if( n <= 0 )
        return 1;

    int e_a_first = e_a0;
    int e_a_last = e_a0 + step_a * (n - 1);
    int min_e_a = e_a_first < e_a_last ? e_a_first : e_a_last;

    int e_b_first = e_b0;
    int e_b_last = e_b0 + step_b * (n - 1);
    int max_e_b = e_b_first > e_b_last ? e_b_first : e_b_last;

    return (min_e_a >= 0) && (max_e_b < (screen_width << 16));
}

#endif
