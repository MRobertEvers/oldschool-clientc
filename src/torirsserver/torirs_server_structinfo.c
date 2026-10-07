/*
 * `struct` config records from the cache — which are nothing but a param map.
 *
 * `struct RSCache_Dat2ConfigStruct` is `{ int id; struct RSCache_Params params; }`
 * and that is the whole record, so this file is the param table and nothing
 * else. `struct_param` is the only reader.
 *
 * Same recipe as torirs_server_npcinfo.c: profile, CONFIGS table, the KIND_STRUCT
 * archive, the file list, decode each. The decoder itself
 * (`RSCache_Dat2ConfigStructDecodeInplace`) has been linked into this binary
 * since rscache landed and had no caller anywhere in src/ — PORTING_GUIDE §1's
 * rule that a linked decoder is reused rather than reimplemented is literal
 * here, there was nothing to write.
 *
 * ── Why the whole group is decoded at boot ───────────────────────────
 *
 * "Lazy" is not available as a design. Struct records are individually-
 * addressed files inside *one* dat2 group and the group is the unit of
 * compression, so reaching one record means decompressing all 3,988. The only
 * real choice is what to retain afterwards, and the answer is the params.
 *
 * Measured on cache.osrs239: 3,988 records, 3,278 of them carrying params,
 * 20,751 rows of which 6,115 are strings and none are RSCACHE_PARAM_LONG.
 * Retained is ~486 KB flat plus the strings — the same order as the obj table's
 * 1.26 MB, which this project already accepted. The transient
 * `Dat2DiskNewFromDirectory` is the same cost class boot already pays four
 * times over for objinfo / npcinfo / seqinfo / varbit.
 *
 * ── And the records the cache does not have ──────────────────────────
 *
 * Content allocates struct ids of its own (`pack/struct.alloc`, 8000 and up)
 * for the `.struct` blocks under `server/scripts/` -- Mort'ton's pyres and
 * shades, the gnome cooking trays. Those are not in any dat2 group, so the
 * decode above never sees them and `struct_param(pyre_logs, pyre_log_output)`
 * answered the declared default (`null`): the logs were consumed and the
 * product was named `item`. `torirs_server_content.c`'s `.struct` loader
 * now keeps those rows in a table of its own, and `ToriRSServer_StructParam`
 * asks it FIRST: an authored row wins over the cache's, as every other
 * overlay does. The table lives in content.c, not here, because the pack
 * validator (`ToriRSServer_Pack`) links the content loader without this
 * file; and being separate it survives a `_StructInfoLoad` in either order.
 */

#include "torirs_server_servpack.h"
#include "torirs_server.h"
#include "torirs_server_paramtable.h"

#include <rscache.h>

#include <datatypes/dat2_config_struct.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ToriRSServerParamTable g_struct_params;
static int g_struct_records;

const struct ToriRSServerParamRow*
ToriRSServer_StructParam(
    int struct_id,
    int param_id)
{
    const struct ToriRSServerParamRow* authored = ToriRSServer_ContentStructParam(struct_id, param_id);

    if( authored )
        return authored;
    return ToriRSServer_ParamTableFind(&g_struct_params, struct_id, param_id);
}

int
ToriRSServer_StructInfoCount(void)
{
    return g_struct_records;
}

int
ToriRSServer_StructInfoParamCount(void)
{
    return g_struct_params.count;
}

int
ToriRSServer_StructInfoLoad(struct RSCache_ServerPack* pack)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct ToriRSServerKindRecords records;

    ToriRSServer_StructInfoFree();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

    /* The server pack's client records: the merge of the tree, encoded by the
     * codec this decoder reads. */
    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_STRUCT, &records) )
        return -1;

    for( int i = 0; i < records.count; i++ )
    {
        struct RSCache_Dat2ConfigStruct record;

        if( (int)records.sizes[i] <= 0 )
            continue;
        memset(&record, 0, sizeof(record));
        record.id = records.ids[i];
        RSCache_Dat2ConfigStructDecodeInplace(&record, (char*)records.files[i], (int)records.sizes[i]);
        ToriRSServer_ParamTableRead(&g_struct_params, records.ids[i], &record.params);
        RSCache_Dat2ConfigStructFreeInplace(&record);
    }

    /* Binary-searched, so it has to actually be ordered — and a record's own
     * params do not arrive ordered. 1,847 of the 2,833 struct records carrying
     * two or more params are out of key order in cache.osrs239. */
    ToriRSServer_ParamTableSort(&g_struct_params);

    /* Read the count before the free, not after: the archive owns it. */
    g_struct_records = records.count;

    ToriRSServer_ServPackKindFree(&records);

    fprintf(stderr, "torirsserver: struct params loaded (%d records from %s, %d rows in %zu KB)\n",
            g_struct_records, pack->dir, g_struct_params.count,
            ToriRSServer_ParamTableBytes(&g_struct_params) / 1024);
    return 1;
}

void
ToriRSServer_StructInfoFree(void)
{
    ToriRSServer_ParamTableFree(&g_struct_params);
    g_struct_records = 0;
}
