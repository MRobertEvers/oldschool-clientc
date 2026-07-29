#include "cachepack.h"

#include "datatypes/dat2_configs.h"

#include <string.h>

/*
 * The register.
 *
 * One row per config type, and the row is the whole of what the drivers know: the
 * source file name, where the records live, whether the cache names them, and the
 * two functions that move a record between bytes and text.
 *
 * ## Gameval archives
 *
 * Table 24 archive ids, correlated against the config groups by record count and
 * content and then verified per cache at load (cp_names_seed_from_cache):
 *
 *   0 obj   1 npc   2 inv   3 varp   4 varbit
 *   6 loc   7 seq   8 spotanim   9 dbrow   10 dbtable
 *
 * Archives 11, 12 and 13 also exist and hold music, sprite and interface names —
 * none of which is a config type here, so they are not claimed.
 *
 * ## What each flag costs
 *
 * CP_TYPE_LOSSY says the *library's decoder* drops fields, so text is a faithful
 * view of what the client can see but not of the file. Packing such a type produces
 * a shorter, valid record. Both directions are still useful — editing a loc's name
 * or its models works — but a repack is not a copy.
 *
 * CP_TYPE_NO_ENCODER is stronger: rscache decodes the type and has no encoder for
 * it, so pack refuses and the records pass through from the base cache untouched.
 */

/* clang-format off */
static const struct CP_Type g_types[CP_TYPE_COUNT] = {
    [CP_TYPE_UNDERLAY] = {
        "underlay", RSCACHE_TYPE_UNDERLAY, RSCACHE_DAT2_CONFIG_KIND_UNDERLAY, -1, 0,
        cp_unpack_underlay, cp_pack_underlay },
    [CP_TYPE_OVERLAY] = {
        "overlay", RSCACHE_TYPE_OVERLAY, RSCACHE_DAT2_CONFIG_KIND_OVERLAY, -1, 0,
        cp_unpack_overlay, cp_pack_overlay },
    [CP_TYPE_IDK] = {
        "idk", RSCACHE_TYPE_IDK, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, -1, 0,
        cp_unpack_idk, cp_pack_idk },
    [CP_TYPE_INV] = {
        "inv", RSCACHE_TYPE_INV, RSCACHE_DAT2_CONFIG_KIND_INV, 2, 0,
        cp_unpack_inv, cp_pack_inv },
    [CP_TYPE_LOC] = {
        "loc", RSCACHE_TYPE_LOC, RSCACHE_DAT2_CONFIG_KIND_LOCS, 6, CP_TYPE_LOSSY,
        cp_unpack_loc, cp_pack_loc },
    [CP_TYPE_ENUM] = {
        "enum", RSCACHE_TYPE_ENUM, RSCACHE_DAT2_CONFIG_KIND_ENUM, -1, CP_TYPE_LOSSY,
        cp_unpack_enum, cp_pack_enum },
    [CP_TYPE_NPC] = {
        "npc", RSCACHE_TYPE_NPC, RSCACHE_DAT2_CONFIG_KIND_NPC, 1, CP_TYPE_LOSSY,
        cp_unpack_npc, cp_pack_npc },
    [CP_TYPE_OBJ] = {
        "obj", RSCACHE_TYPE_OBJ, RSCACHE_DAT2_CONFIG_KIND_OBJECT, 0, CP_TYPE_LOSSY,
        cp_unpack_obj, cp_pack_obj },
    [CP_TYPE_PARAM] = {
        "param", RSCACHE_TYPE_PARAM, RSCACHE_DAT2_CONFIG_KIND_PARAMS, -1, 0,
        cp_unpack_param, cp_pack_param },
    [CP_TYPE_SEQ] = {
        "seq", RSCACHE_TYPE_SEQUENCE, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE, 7, CP_TYPE_LOSSY,
        cp_unpack_seq, cp_pack_seq },
    [CP_TYPE_SPOTANIM] = {
        "spotanim", RSCACHE_TYPE_SPOTANIM, RSCACHE_DAT2_CONFIG_KIND_SPOTANIM, 8, CP_TYPE_LOSSY,
        cp_unpack_spotanim, cp_pack_spotanim },
    [CP_TYPE_VARBIT] = {
        "varbit", RSCACHE_TYPE_VARBIT, RSCACHE_DAT2_CONFIG_KIND_VARBIT, 4, 0,
        cp_unpack_varbit, cp_pack_varbit },
    [CP_TYPE_VARP] = {
        "varp", RSCACHE_TYPE_VARPLAYER, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER, 3, 0,
        cp_unpack_varp, cp_pack_varp },
    [CP_TYPE_VARC] = {
        "varc", RSCACHE_TYPE_VARCLIENT, RSCACHE_DAT2_CONFIG_KIND_VARCLIENT, -1, 0,
        cp_unpack_varc, cp_pack_varc },
    [CP_TYPE_HITSPLAT] = {
        "hitsplat", RSCACHE_TYPE_HITSPLAT, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT, -1, 0,
        cp_unpack_hitsplat, cp_pack_hitsplat },
    [CP_TYPE_HEALTHBAR] = {
        "healthbar", RSCACHE_TYPE_HEALTHBAR, RSCACHE_DAT2_CONFIG_KIND_HEALTHBAR, -1, 0,
        cp_unpack_healthbar, cp_pack_healthbar },
    [CP_TYPE_STRUCT] = {
        "struct", RSCACHE_TYPE_STRUCT, RSCACHE_DAT2_CONFIG_KIND_STRUCT, -1, 0,
        cp_unpack_struct, cp_pack_struct },
    [CP_TYPE_MAPELEMENT] = {
        "mapelement", RSCACHE_TYPE_MAPELEMENT, RSCACHE_DAT2_CONFIG_KIND_AREA, -1, CP_TYPE_LOSSY,
        cp_unpack_mapelement, cp_pack_mapelement },
    [CP_TYPE_DBROW] = {
        "dbrow", RSCACHE_TYPE_DBROW, RSCACHE_DAT2_CONFIG_KIND_DBROW, 9, CP_TYPE_NO_ENCODER,
        cp_unpack_dbrow, cp_pack_dbrow },
    [CP_TYPE_DBTABLE] = {
        "dbtable", RSCACHE_TYPE_DBTABLE, RSCACHE_DAT2_CONFIG_KIND_DBTABLE, 10, CP_TYPE_NO_ENCODER,
        cp_unpack_dbtable, cp_pack_dbtable },
};
/* clang-format on */

const struct CP_Type*
cp_type(enum CP_TypeId id)
{
    if( id < 0 || id >= CP_TYPE_COUNT )
        return NULL;
    return &g_types[id];
}

int
cp_type_by_name(const char* name)
{
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( strcmp(g_types[i].name, name) == 0 )
            return i;
    }
    return -1;
}
