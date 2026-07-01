#ifndef RSCACHE_RSCACHEDAT2A_CLIENTSCRIPT_H
#define RSCACHE_RSCACHEDAT2A_CLIENTSCRIPT_H

#include "vm/cs2_script.h"

#include <stdint.h>

struct RSCacheDat2A_ClientScript
{
    struct CS2_Script script;
};

struct RSCacheDat2A_ClientScript*
RSCacheDat2A_ClientScriptNewDecode(
    int script_id,
    const uint8_t* data,
    int data_size);

void
RSCacheDat2A_ClientScriptFree(struct RSCacheDat2A_ClientScript* script);

#endif
