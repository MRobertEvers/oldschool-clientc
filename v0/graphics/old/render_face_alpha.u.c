#ifndef RENDER_FACE_ALPHA_U_C
#define RENDER_FACE_ALPHA_U_C

#include <stddef.h>

static inline int
face_alpha(alphaint_t* face_alphas_nullable, int face)
{
    if( face_alphas_nullable == NULL )
        return 0xFF;

    return 0xFF - face_alphas_nullable[face];
}

#endif