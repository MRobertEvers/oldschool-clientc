#ifndef TORIDRAW_TRIANGLE_FACE_ALPHA_U_C
#define TORIDRAW_TRIANGLE_FACE_ALPHA_U_C

#include "toridraw_types.h"

#include <stddef.h>

static inline int
ToriDraw_TriangleFaceAlpha(
    alphaint_t* face_alphas_nullable,
    int face)
{
    if( face_alphas_nullable == NULL )
        return 0xFF;

    return 0xFF - face_alphas_nullable[face];
}

#endif
