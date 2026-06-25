#include "toriauxlib.h"

#include <stdlib.h>

struct ToriAuxLib
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibC* c;
    struct ToriAuxLibTD* td;
    struct ToriAuxLibVM* vm;
};

struct ToriAuxLib*
ToriAuxLib_New(
    enum ToriAuxLibCMode mode,
    struct ToriDraw_Scene* scene)
{
    struct ToriAuxLib* tal = calloc(1, sizeof(struct ToriAuxLib));
    if( !tal )
        return NULL;

    tal->core = ToriAuxLibCore_New();
    if( !tal->core )
        goto fail;

    tal->c = ToriAuxLibC_New(mode, tal->core);
    if( !tal->c )
        goto fail;

    tal->td = ToriAuxLibTD_New(tal->core, tal->c, scene);
    if( !tal->td )
        goto fail;

    tal->vm = ToriAuxLibVM_New();
    if( !tal->vm )
        goto fail;

    ToriAuxLibC_SetVarPVarBit(tal->c, ToriAuxLibVM_VarPVarBit(tal->vm));

    return tal;

fail:
    ToriAuxLib_Free(tal);
    return NULL;
}

void
ToriAuxLib_Free(struct ToriAuxLib* tal)
{
    if( !tal )
        return;
    if( tal->td )
        ToriAuxLibTD_Free(tal->td);
    if( tal->vm )
        ToriAuxLibVM_Free(tal->vm);
    if( tal->c )
        ToriAuxLibC_Free(tal->c);
    if( tal->core )
        ToriAuxLibCore_Free(tal->core);
    free(tal);
}

struct ToriAuxLibCore*
ToriAuxLib_Core(struct ToriAuxLib* tal)
{
    return tal ? tal->core : NULL;
}

struct ToriAuxLibC*
ToriAuxLib_C(struct ToriAuxLib* tal)
{
    return tal ? tal->c : NULL;
}

struct ToriAuxLibTD*
ToriAuxLib_TD(struct ToriAuxLib* tal)
{
    return tal ? tal->td : NULL;
}

struct ToriAuxLibVM*
ToriAuxLib_VM(struct ToriAuxLib* tal)
{
    return tal ? tal->vm : NULL;
}
