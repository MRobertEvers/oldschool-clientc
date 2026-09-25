/*
 * ToriDraw unity build -- compile this single file to get the full library.
 *
 * Usage in another project:
 *   1. Add -I<path>/3rd/toridraw to your include path.
 *   2. cc -c -I<path>/3rd/toridraw toridraw_unity.c
 *   3. #include "toridraw.h" where you need the API.
 *
 * Nothing else is required. That last line used to be a claim rather than a
 * fact: toridraw_font.c was not in this list while toridraw_scene.c's font
 * registry called into it, so a standalone link came up short by exactly that
 * object; and toridraw_sprite.c reached 3rd/bmp for a BMP export nothing in
 * the tree calls. The font is in; the export is behind
 * -DTORIDRAW_SPRITE_BMP_EXPORT, which also wants -I3rd/bmp.
 *
 * For a client with a strict memory budget, see MINI.md: the knobs that
 * matter are -DTORIDRAW_PIXEL_FORMAT (a two-byte framebuffer word),
 * -DTORIDRAW_TABLES_PRECOMPUTED (the lookup tables as const, for ROM) and
 * -DTORIDRAW_TEXTURE_ID_CAPACITY.
 */

#include "toridraw.c"
#include "toridraw_animation.c"
#include "toridraw_hsl16.c"
#include "toridraw_intrusive_list.c"
#include "toridraw_light_model.c"
#include "toridraw_lighting.c"
#include "toridraw_map.c"
#include "toridraw_math.c"
#include "toridraw_arena.c"
#include "toridraw_mini.c"
#include "toridraw_model.c"
#include "toridraw_model_transform.c"
#include "toridraw_scene.c"
#include "toridraw_shared_model.c"
#include "toridraw_sprite.c"
#include "toridraw_font.c"
#include "toridraw_2d.c"
#include "toridraw_vec.c"
#include "toridraw_texture_uv.c"
#include "graphics/convex_hull.c"
#include "census/raster_ablate.c"
#include "census/face_census.c"
#include "census/sarea_census.c"
#include "census/proj_census.c"
#include "census/span_census.c"
#include "graphics/shared_tables.c"
#include "osrs/palette.c"
