#ifndef TORIAUXLIB_H
#define TORIAUXLIB_H

#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toriauxlib/td/toridraw_cachesprite.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_scene.h"

struct ToriAuxLib;

struct ToriAuxLib*
ToriAuxLib_New(
    enum ToriAuxLibCacheMode mode,
    struct ToriDraw_Scene* scene);

void
ToriAuxLib_Free(struct ToriAuxLib* tal);

struct ToriAuxLibCore*
ToriAuxLib_Core(struct ToriAuxLib* tal);

struct ToriAuxLibCache*
ToriAuxLib_C(struct ToriAuxLib* tal);

struct ToriAuxLibTD*
ToriAuxLib_TD(struct ToriAuxLib* tal);

struct ToriAuxLibVM*
ToriAuxLib_VM(struct ToriAuxLib* tal);

#endif
