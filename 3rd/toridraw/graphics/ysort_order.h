#ifndef TORIDRAW_YSORT_ORDER_H
#define TORIDRAW_YSORT_ORDER_H

/*
 * The y-order permutation table: ONE copy, three readers.
 *
 * When the face sort stashes a triangle for the batched raster walk, it sorts
 * the three screen y values there and then -- they are already in registers,
 * the winding test needed them -- and records WHICH permutation it applied in
 * sm_face_y4[3]. The walk then replays that permutation over any per-vertex
 * data it carries of its own (shades, uvs), because those did not travel
 * through the stash.
 *
 * Producer and consumer therefore have to agree on what index 4 means, exactly,
 * and they live in three different translation-unit halves that cannot see each
 * other's statics -- the bitonic+radix sort, the bucket sort's stash, and the
 * batched walk. Each of them had its own copy of these six rows. Three
 * literals that
 * must be identical, with nothing in the build that would notice if one of them
 * stopped being: editing one row of one of them draws a scrambled triangle
 * wherever the batcher takes a run, and correct triangles everywhere else.
 *
 * The permutation index is produced by the `<=` ladder in the stash function.
 * Those tie-breaks are transcribed from the C wrappers the assembly kernels
 * came from and are part of the contract, not a comparison order to tidy up:
 * two triangles that tie differently stop tiling with each other. This table is
 * the other half of that contract, so it is stated once, here, next to nothing
 * else.
 */
static const unsigned char g_toridraw_ysort_order[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 },
    { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 },
};

#endif /* TORIDRAW_YSORT_ORDER_H */
