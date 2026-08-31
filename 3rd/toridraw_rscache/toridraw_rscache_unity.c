/*
 * toridraw_rscache unity build -- compile this one file to get the library.
 *
 *   cc -I3rd/toridraw_rscache/include -I3rd/toridraw -I3rd/rscache/include \
 *      -DTORIDRAW_PIXEL_FORMAT=<the target's> \
 *      -c 3rd/toridraw_rscache/toridraw_rscache_unity.c
 *
 * It needs ToriDraw's and RSCache's headers, and links against both. Nothing
 * else: no client sources, no build system.
 */

#include "src/toridraw_rscache_model.c"
#include "src/toridraw_rscache_light.c"
#include "src/toridraw_rscache_texture.c"
#include "src/toridraw_rscache_anim.c"
