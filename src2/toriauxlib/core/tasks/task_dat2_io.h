#ifndef TORIAUXLIB_TASK_DAT2_IO_H
#define TORIAUXLIB_TASK_DAT2_IO_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../ioqueue/libtorirs_io.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/toriauxlibcache.h"

#define DAT2_ENSURE_REFERENCE_TABLE(ctx, thread, bc, table_id) \
    do { \
        if( !dat2_buildcache_reference_table_has((bc), (table_id)) ) \
        { \
            IO_REQUEST((ctx), 0, TAPIDat2_FetchReferenceTable((ctx), (table_id))); \
            PT_YIELD((thread)); \
            struct RSCacheDat2Disk_ReferenceTable* _dat2_ref_table = \
                TAPIDat2_DecodeReferenceTable((ctx), 0, (table_id)); \
            if( !_dat2_ref_table ) \
            { \
                fprintf( \
                    stderr, \
                    "dat2: failed to load reference table id=%d\n", \
                    (int)(table_id)); \
                PT_EXIT((thread)); \
            } \
            dat2_buildcache_reference_table_add((bc), (table_id), _dat2_ref_table); \
            LibToriRS_IOQueueClear((ctx)->io); \
        } \
    } while( 0 )

#define DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, thread, cache) \
    DAT2_ENSURE_REFERENCE_TABLE( \
        (ctx), (thread), dat2((cache)), RSCacheDat2Disk_Table_Configs)

#endif
