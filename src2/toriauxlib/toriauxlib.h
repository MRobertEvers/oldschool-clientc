#ifndef TORIAUXLIB_H
#define TORIAUXLIB_H

#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"

#include "toridraw/toridraw_scene.h"

struct ToriAuxLib;

struct ToriAuxLib*
ToriAuxLib_New(
    enum ToriAuxLibCMode mode,
    struct ToriDraw_Scene* scene);

void
ToriAuxLib_Free(struct ToriAuxLib* tal);

struct ToriAuxLibCore*
ToriAuxLib_Core(struct ToriAuxLib* tal);

struct ToriAuxLibC*
ToriAuxLib_C(struct ToriAuxLib* tal);

struct ToriAuxLibTD*
ToriAuxLib_TD(struct ToriAuxLib* tal);

#endif
