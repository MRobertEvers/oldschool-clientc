/*
 * The provider's objtype index (engine/cache_provider_obj_index.h) and the
 * per-item icon memo (engine/obj_icon_memo.h), across every event that may
 * invalidate them: an add, a replace, a retire, an archive eviction, a
 * cleanup, a model trim or replacement, a texture cleanup.
 *
 *   make -C src test-cache-provider-obj-index
 *
 * The index must answer exactly what the map answers after every event, and
 * the drop revision (what makes a remembered "ready" stale) must move on every
 * event that takes something away and on none that only adds.
 */
#include "mem/torirs_mem.h"
#include "engine/cache_provider.h"
#include "engine/obj_icon_memo.h"
#include "engine/torirs_types.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int ok,
    char const* what)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("obj-index-memo: %-64s FAILED\n", what);
        return;
    }
    printf("obj-index-memo: %-64s ok\n", what);
}

static struct ToriRS_Objtype*
new_obj(char const* name)
{
    struct ToriRS_Objtype* obj = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*obj));
    assert(obj);
    snprintf(obj->name, sizeof(obj->name), "%s", name);
    obj->cert_template = -1;
    obj->cert_link = -1;
    obj->placeholder_template = -1;
    obj->placeholder_link = -1;
    return obj;
}

static struct ToriRS_Model*
new_model(void)
{
    struct ToriRS_Model* model = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model));
    assert(model);
    return model;
}

static void
test_index_follows_the_map(void)
{
    struct CacheProvider provider;
    uint64_t rev;

    memset(&provider, 0, sizeof(provider));
    CacheProvider_InitEngineCaches(&provider);

    check(CacheProvider_ObjtypeGet(&provider, 995) == NULL, "empty provider: no objtype");
    rev = CacheProvider_AssetDropRevision(&provider);

    struct ToriRS_Objtype* coins = new_obj("Coins");
    CacheProvider_ObjtypeAdd(&provider, 995, coins);
    check(CacheProvider_ObjtypeGet(&provider, 995) == coins, "add: found by id");
    check(provider.objtype_index.cap > 995 && provider.objtype_index.by_id[995] == coins,
        "add: the index slot holds it");
    check(CacheProvider_ObjtypeGet(&provider, 996) == NULL, "add: a neighbour id is still absent");
    check(CacheProvider_AssetDropRevision(&provider) == rev, "add: drop revision unmoved");

    /* An id far past the index's first size grows it. */
    struct ToriRS_Objtype* far = new_obj("Far");
    CacheProvider_ObjtypeAdd(&provider, 30000, far);
    check(CacheProvider_ObjtypeGet(&provider, 30000) == far, "add past the end: found");
    check(CacheProvider_ObjtypeGet(&provider, 995) == coins, "add past the end: earlier id kept");

    /* An id the index does not cover is answered from the map. */
    struct ToriRS_Objtype* huge = new_obj("Huge");
    CacheProvider_ObjtypeAdd(&provider, CACHE_PROVIDER_OBJ_INDEX_MAX + 7, huge);
    check(CacheProvider_ObjtypeGet(&provider, CACHE_PROVIDER_OBJ_INDEX_MAX + 7) == huge,
        "uncovered id: answered from the map");
    check(CacheProvider_ObjtypeGet(&provider, -1) == NULL, "negative id: none");

    /* Replace: the new one, and the drop revision moves. */
    rev = CacheProvider_AssetDropRevision(&provider);
    struct ToriRS_Objtype* coins2 = new_obj("Coins");
    CacheProvider_ObjtypeReplace(&provider, 995, coins2);
    check(CacheProvider_ObjtypeGet(&provider, 995) == coins2, "replace: the new objtype");
    check(CacheProvider_AssetDropRevision(&provider) != rev, "replace: drop revision moved");

    /* Add over a resident one (a second decode) replaces it too. */
    rev = CacheProvider_AssetDropRevision(&provider);
    struct ToriRS_Objtype* coins3 = new_obj("Coins");
    CacheProvider_ObjtypeAdd(&provider, 995, coins3);
    check(CacheProvider_ObjtypeGet(&provider, 995) == coins3, "add over: the new objtype");
    check(CacheProvider_AssetDropRevision(&provider) != rev, "add over: drop revision moved");

    /* Retire: gone from both. */
    rev = CacheProvider_AssetDropRevision(&provider);
    check(CacheProvider_RecordRetire(&provider, CACHEPROVIDER_CATALOG_OBJ, 995) == 1, "retire: done");
    check(CacheProvider_ObjtypeGet(&provider, 995) == NULL, "retire: absent by id");
    check(CacheProvider_AssetDropRevision(&provider) != rev, "retire: drop revision moved");
    check(CacheProvider_ObjtypeGet(&provider, 30000) == far, "retire: another id untouched");

    /* The obj archive invalidated: every objtype evicted. */
    rev = CacheProvider_AssetDropRevision(&provider);
    CacheProvider_InvalidateArchive(&provider, RSCACHE_DAT2_TABLE_CONFIGS, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    check(CacheProvider_ObjtypeGet(&provider, 30000) == NULL, "archive invalidated: evicted");
    check(CacheProvider_AssetDropRevision(&provider) != rev, "archive invalidated: drop revision moved");

    /* Cleanup after a fresh add. */
    struct ToriRS_Objtype* again = new_obj("Again");
    CacheProvider_ObjtypeAdd(&provider, 12, again);
    check(CacheProvider_ObjtypeGet(&provider, 12) == again, "re-add after eviction: found");
    rev = CacheProvider_AssetDropRevision(&provider);
    CacheProvider_ObjtypesCleanup(&provider);
    check(CacheProvider_ObjtypeGet(&provider, 12) == NULL, "cleanup: absent");
    check(CacheProvider_AssetDropRevision(&provider) != rev, "cleanup: drop revision moved");

    /* Models: an add does not move it, a replace and a cleanup do. */
    rev = CacheProvider_AssetDropRevision(&provider);
    CacheProvider_ModelAdd(&provider, 40, new_model());
    check(CacheProvider_AssetDropRevision(&provider) == rev, "model add: drop revision unmoved");
    CacheProvider_ModelAdd(&provider, 40, new_model());
    check(CacheProvider_AssetDropRevision(&provider) != rev, "model replaced: drop revision moved");
    rev = CacheProvider_AssetDropRevision(&provider);
    CacheProvider_ModelsCleanup(&provider);
    check(CacheProvider_AssetDropRevision(&provider) != rev, "model cleanup: drop revision moved");

    CacheProvider_FreeEngineCaches(&provider);
    check(provider.objtype_index.by_id == NULL && provider.objtype_index.cap == 0,
        "free: the index is released");
}

static void
test_icon_memo(void)
{
    struct ObjIconMemo memo;

    memset(&memo, 0, sizeof(memo));
    check(ObjIconMemo_Get(&memo, 995, 1, 0) == 0, "empty memo: miss");
    ObjIconMemo_Put(&memo, 995, 250, 7, 42);
    check(ObjIconMemo_Get(&memo, 995, 250, 7) == 42, "same obj, count, revision: hit");
    check(ObjIconMemo_Get(&memo, 995, 251, 7) == 0, "another count: miss");
    check(ObjIconMemo_Get(&memo, 995, 250, 8) == 0, "another revision (something left): miss");
    check(ObjIconMemo_Get(&memo, 994, 250, 7) == 0, "another obj: miss");
    ObjIconMemo_Put(&memo, 20000, 1, 7, 43);
    check(ObjIconMemo_Get(&memo, 20000, 1, 7) == 43, "grown: the far id");
    check(ObjIconMemo_Get(&memo, 995, 250, 7) == 42, "grown: the near id kept");
    ObjIconMemo_Put(&memo, 995, 1, 9, 44);
    check(ObjIconMemo_Get(&memo, 995, 250, 7) == 0, "rewritten: the old answer is gone");
    check(ObjIconMemo_Get(&memo, 995, 1, 9) == 44, "rewritten: the new answer");
    ObjIconMemo_Put(&memo, OBJ_ICON_MEMO_MAX_ID, 1, 9, 45);
    check(ObjIconMemo_Get(&memo, OBJ_ICON_MEMO_MAX_ID, 1, 9) == 0, "an id past the limit is not kept");
    ObjIconMemo_Free(&memo);
    check(memo.by_id == NULL && memo.cap == 0, "free");
}

int
main(void)
{
    test_index_follows_the_map();
    test_icon_memo();
    printf("obj-index-memo: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
