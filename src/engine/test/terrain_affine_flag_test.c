/*
 * A textured terrain tile asks for the affine texture kernels; an untextured
 * one asks for nothing.
 *
 * world_decode_tile is the one place that knows a ToriDraw_Model is terrain,
 * so it is where TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES is set -- and the raster
 * reads the flag per model (toridraw_affine_flag_test.c proves that half).
 * This pins the other half: the tile decoder sets exactly that bit, on exactly
 * the tiles that carry a texture, and touches no other render flag. An
 * untextured tile keeping flags == 0 is the negative control; it is also the
 * z-buffer regression guard's concern (bit 0 must never leak onto terrain).
 *
 * Build and run:
 *   make -C src test-terrain-affine-flag
 */
#include "engine/world_builder/world_decode_tile.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while( 0 )

static struct ToriDraw_Model*
decode(int shape, int rotation, int texture_id)
{
    /* Flat, evenly lit, an underlay and an overlay colour: every shape builds
     * with these, and nothing about the flag depends on them. */
    return world_decode_tile(shape, rotation, texture_id, 0, 0, 0, 0, 96, 96, 96, 96, 1000, 2000);
}

int
main(void)
{
    /* Every shape, every rotation: the textured branch is shared, but the
     * shapes differ in which faces exist, and a decoder that set the flag in
     * a shape-specific arm would pass on shape 0 alone. */
    for( int shape = 0; shape < 13; shape++ )
        for( int rotation = 0; rotation < 4; rotation++ )
        {
            struct ToriDraw_Model* textured = decode(shape, rotation, 5);
            struct ToriDraw_Model* plain = decode(shape, rotation, -1);

            CHECK(textured, "shape %d rot %d: textured tile did not build", shape, rotation);
            CHECK(plain, "shape %d rot %d: untextured tile did not build", shape, rotation);
            if( textured )
            {
                CHECK(
                    textured->face_textures != NULL,
                    "shape %d rot %d: textured tile carries no face textures",
                    shape,
                    rotation);
                CHECK(
                    textured->flags == TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES,
                    "shape %d rot %d: textured tile flags are 0x%02x, expected only "
                    "TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES (0x%02x)",
                    shape,
                    rotation,
                    textured->flags,
                    TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES);
                ToriDraw_ModelFree(textured);
            }
            if( plain )
            {
                CHECK(
                    plain->face_textures == NULL,
                    "shape %d rot %d: untextured tile carries face textures",
                    shape,
                    rotation);
                CHECK(
                    plain->flags == 0,
                    "shape %d rot %d: untextured tile flags are 0x%02x, expected 0",
                    shape,
                    rotation,
                    plain->flags);
                ToriDraw_ModelFree(plain);
            }
        }

    if( failures )
    {
        fprintf(stderr, "terrain affine flag: %d failure(s)\n", failures);
        return 1;
    }
    printf("terrain affine flag: all checks passed\n");
    return 0;
}
