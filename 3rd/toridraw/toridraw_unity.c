/*
 * ToriDraw unity build — compile this single file to get the full library.
 *
 * Usage in another project:
 *   1. Add -I<path>/src2/toridraw to your include path.
 *   2. Compile toridraw_unity.c once (e.g. cc -c -I<path>/src2/toridraw toridraw_unity.c).
 *   3. #include "toridraw.h" where you need the API.
 *
 * All dependencies live inside the toridraw/ folder; nothing from src/ is required.
 */

#include "toridraw.c"
#include "toridraw_animation.c"
#include "toridraw_hsl16.c"
#include "toridraw_intrusive_list.c"
#include "toridraw_light_model.c"
#include "toridraw_lighting.c"
#include "toridraw_map.c"
#include "toridraw_math.c"
#include "toridraw_model.c"
#include "toridraw_model_transform.c"
#include "toridraw_scene.c"
#include "toridraw_sprite.c"
#include "toridraw_2d.c"
#include "toridraw_vec.c"
#include "toridraw_texture_uv.c"
#include "graphics/convex_hull.c"
#include "graphics/raster/raster_ablate.c"
#include "graphics/proj_census.c"
#include "graphics/raster/span_census.c"
#include "graphics/shared_tables.c"
#include "osrs/palette.c"
