#include "pg_gl.h"

#include <SDL.h>
#include <stdio.h>

#define PG_GL_DEFINE(ret, name, args) ret(*pg_gl##name) args = NULL;
PG_GL_FUNCTIONS(PG_GL_DEFINE)
#undef PG_GL_DEFINE

int
pg_gl_load(void)
{
    int missing = 0;

#define PG_GL_RESOLVE(ret, name, args)                                                            \
    do                                                                                            \
    {                                                                                             \
        void* p = SDL_GL_GetProcAddress("gl" #name);                                              \
        if( !p )                                                                                  \
        {                                                                                         \
            fprintf(stderr, "poser-gl: missing GL entry point gl%s\n", #name);                    \
            missing++;                                                                            \
        }                                                                                         \
        pg_gl##name = (ret(*) args)p;                                                             \
    } while( 0 );
    PG_GL_FUNCTIONS(PG_GL_RESOLVE)
#undef PG_GL_RESOLVE

    return missing;
}
