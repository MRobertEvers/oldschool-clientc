#ifndef PARITY_EXEC_H
#define PARITY_EXEC_H

#include "parity_manifest.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

int
parity_set_clientscript_decode_flags(int flags);

int
parity_exec_case(
    struct RSCacheDat2Disk* cache,
    struct ParityCs2Case const* cs_case,
    char const* out_path);

int
parity_sprite_case(
    struct RSCacheDat2Disk* cache,
    struct ParitySpriteCase const* sp_case,
    char const* out_dir);

int
parity_tree_case(
    struct RSCacheDat2Disk* cache,
    struct ParityTreeCase const* tree_case,
    char const* out_path);

#endif
