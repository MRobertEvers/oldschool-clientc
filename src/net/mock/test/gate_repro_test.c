/*
 * SCRATCH — gate investigation for the weapon-FX slice-3 blocker.
 * Not part of the build; delete after use. Reproduces defect 1
 * (bronze_dagger overlay does not read back via oc_param) at the C layer,
 * bypassing the RS2/CS2 VM and the socket, per the gate-investigation brief.
 */
#include "mock230.h"
#include "mock230_content.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char*
resolve_dir(
    const char* relative,
    char* scratch,
    size_t scratch_size)
{
    struct stat info;

    snprintf(scratch, scratch_size, "%s", relative);
    if( stat(scratch, &info) == 0 )
        return scratch;
    snprintf(scratch, scratch_size, "../%s", relative);
    return scratch;
}

static void
report(const char* name, int param_name_symbol_ok, int obj_id, int param_id)
{
    const struct Mock230ObjParam* row;

    if( obj_id < 0 )
    {
        printf("%-16s obj id NOT RESOLVED\n", name);
        return;
    }
    if( !param_name_symbol_ok )
    {
        printf("%-16s param NOT RESOLVED\n", name);
        return;
    }
    row = mock230_obj_param(obj_id, param_id);
    if( row )
        printf("%-16s obj_id=%-6d param_id=%-6d FOUND ival=%d sval=%s\n", name, obj_id,
               param_id, row->ival, row->sval ? row->sval : "(null)");
    else
        printf("%-16s obj_id=%-6d param_id=%-6d NOT FOUND (falls back to declared default)\n",
               name, obj_id, param_id);
}

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;
    int stab_param_id, defend_param_id, ready_param_id;
    int bronze_id, bronze_p_id, dragon_id, dragon_p_id;

    cache_dir = resolve_dir(MOCK230_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs239-content", content_scratch,
                              sizeof(content_scratch));

    printf("cache=%s content=%s\n", cache_dir, content_dir);

    /* Same order as mock230_boot.c / mock230_pack.c. */
    mock230_objinfo_load(cache_dir);
    mock230_npcinfo_load(cache_dir);
    mock230_seqinfo_load(cache_dir);
    mock230_locinfo_load(cache_dir);
    mock230_structinfo_load(cache_dir);
    mock230_content_load(content_dir);

    if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, "stabattack_anim", &stab_param_id) )
        stab_param_id = -1;
    if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, "defend_anim", &defend_param_id) )
        defend_param_id = -1;
    if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, "ready_baseanim", &ready_param_id) )
        ready_param_id = -1;

    bronze_id = mock230_content_symbol(MOCK230_PACK_OBJ, "bronze_dagger");
    bronze_p_id = mock230_content_symbol(MOCK230_PACK_OBJ, "bronze_dagger_p");
    dragon_id = mock230_content_symbol(MOCK230_PACK_OBJ, "dragon_dagger");
    dragon_p_id = mock230_content_symbol(MOCK230_PACK_OBJ, "dragon_dagger_p");

    printf("resolved ids: bronze_dagger=%d bronze_dagger_p=%d dragon_dagger=%d "
           "dragon_dagger_p=%d\n",
           bronze_id, bronze_p_id, dragon_id, dragon_p_id);
    printf("resolved params: stabattack_anim=%d defend_anim=%d ready_baseanim=%d\n",
           stab_param_id, defend_param_id, ready_param_id);

    printf("\n--- stabattack_anim ---\n");
    report("bronze_dagger", stab_param_id >= 0, bronze_id, stab_param_id);
    report("bronze_dagger_p", stab_param_id >= 0, bronze_p_id, stab_param_id);
    report("dragon_dagger", stab_param_id >= 0, dragon_id, stab_param_id);
    report("dragon_dagger_p", stab_param_id >= 0, dragon_p_id, stab_param_id);

    printf("\n--- defend_anim ---\n");
    report("bronze_dagger", defend_param_id >= 0, bronze_id, defend_param_id);
    report("dragon_dagger", defend_param_id >= 0, dragon_id, defend_param_id);

    printf("\ndone\n");
    return 0;
}
