/*
 * SCRATCH — gate investigation, phase 3. Ground truth (no VM, no compiler):
 * for every weapon name in bas/attack_anims.obj, resolve its obj id through
 * mock230_content_symbol (the SAME runtime symbol table `mock230_obj_param`
 * uses) and print the stabattack_anim/defend_anim rows directly. This is
 * what the storage layer says is true, independent of the ssc compiler's
 * own (separate) symbol table. Delete after use.
 */
#include "mock230.h"
#include "mock230_content.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "gate_names.h"

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

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;
    int stab_id, defend_id;

    cache_dir = resolve_dir(MOCK230_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs239-content", content_scratch,
                              sizeof(content_scratch));

    mock230_objinfo_load(cache_dir);
    mock230_npcinfo_load(cache_dir);
    mock230_seqinfo_load(cache_dir);
    mock230_locinfo_load(cache_dir);
    mock230_structinfo_load(cache_dir);
    mock230_content_load(content_dir);

    if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, "stabattack_anim", &stab_id) )
        stab_id = -1;
    if( !mock230_content_symbol_checked(MOCK230_PACK_PARAM, "defend_anim", &defend_id) )
        defend_id = -1;

    for( int i = 0; i < g_names_count; i++ )
    {
        int obj_id = mock230_content_symbol(MOCK230_PACK_OBJ, g_names[i]);
        const struct Mock230ObjParam* stab_row =
            obj_id >= 0 ? mock230_obj_param(obj_id, stab_id) : NULL;

        printf("%s obj_id=%d stab=%d found=%d\n", g_names[i], obj_id,
               stab_row ? stab_row->ival : -1, stab_row != NULL);
    }
    return 0;
}
