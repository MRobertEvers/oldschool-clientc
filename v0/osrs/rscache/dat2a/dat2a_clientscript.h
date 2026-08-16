#ifndef RSCACHE_RSCACHEDAT2A_CLIENTSCRIPT_H
#define RSCACHE_RSCACHEDAT2A_CLIENTSCRIPT_H

#include "dat2a_cs2_script.h"

#include <stdint.h>

#define CLIENTSCRIPT_DECODE_TRAILER_MODERN 0
#define CLIENTSCRIPT_DECODE_TRAILER_LEGACY 1

struct RSCacheDat2A_ClientScript
{
    struct RSCache_CS2_Script script;
};

struct RSCacheDat2A_ClientScript*
RSCacheDat2A_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags);

void
RSCacheDat2A_ClientScriptFree(struct RSCacheDat2A_ClientScript* script);

#endif
