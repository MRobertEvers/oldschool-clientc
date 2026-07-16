#ifndef RSCACHE_DATATYPES_CLIENTSCRIPT_H
#define RSCACHE_DATATYPES_CLIENTSCRIPT_H

#include "../dat2disk.h"
#include "cs2_script.h"

#include <stdint.h>

#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN 0
#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY 1

struct RSCache_ClientScript
{
    struct RSCache_CS2_Script script;
};

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags);

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int script_id,
    int flags);

void
RSCache_ClientScriptFree(struct RSCache_ClientScript* script);

#endif
