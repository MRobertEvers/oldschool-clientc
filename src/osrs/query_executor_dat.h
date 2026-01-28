#ifndef QUERY_EXECUTOR_DAT_H
#define QUERY_EXECUTOR_DAT_H

#include "buildcachedat.h"
#include "gio.h"
#include "query_engine.h"

enum QEDatDT
{
    QEDAT_DT_NONE,
    QEDAT_DT_MAPS_SCENERY,
    QEDAT_DT_MAPS_TERRAIN,
    QEDAT_DT_MODELS,
    QEDAT_DT_TEXTURES,
    QEDAT_DT_FLOTYPE,
    QEDAT_DT_CONFIG_LOCIDS,
    QEDAT_DT_CONFIG_NPCS,
    QEDAT_DT_CONFIG_IDKS,
    QEDAT_DT_CONFIG_OBJS,
};

enum QEQueryState
query_executor_dat_step(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat);

#endif