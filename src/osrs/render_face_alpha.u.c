#ifndef RENDER_FACE_ALPHA_U_C
#define RENDER_FACE_ALPHA_U_C

#include <stddef.h>

static inline int
face_alpha(int* face_alphas_nullable, int face)
{
    if( face_alphas_nullable == NULL )
        return 0xFF;

    // Alpha is a signed byte, but for non-textured
    // faces, we treat it as unsigned.
    // -1 and -2 are reserved. See lighting code.

    return 0xFF - (face_alphas_nullable[face] & 0xff);
}

#endif