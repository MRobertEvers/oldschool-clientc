/*
 * CACHE_INVALIDATE for the loc, npc and obj config groups (index 2, archives
 * 6 / 9 / 10): CacheProvider_InvalidateArchive drops every resident record of
 * the kind so the next lookup re-decodes the rebuilt group, retires (does not
 * free) what it dropped, tells the IO layer through its hook, and leaves every
 * other config archive DEFERRED. The dat2 split-group LRU's half
 * (Dat2GroupCache_Evict) is checked beside it.
 *
 *   make -C src test-cache-provider-invalidate
 *
 * The records are the client codec's own bytes, written here by hand: a loc
 * "Flower" with no ops, then the same loc after the editor added
 * `op3=Search`; Hans with op1 Talk-to, then with `op4=Wave` as well.
 */
#include "mem/torirs_mem.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_group_cache.h"
#include "engine/torirs_location_from_rscache.h"
#include "engine/torirs_npctype_from_rscache.h"
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
    const char* what)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("cache-provider-invalidate: %-64s FAILED\n", what);
        return;
    }
    printf("cache-provider-invalidate: %-64s ok\n", what);
}

/* config opcodes: 2 name, 30 + n op n+1, 0 end -- the loc and npc codecs agree */
static const uint8_t k_loc_before[] = { 2, 'F', 'l', 'o', 'w', 'e', 'r', 0, 0 };
static const uint8_t k_loc_after[] = { 2,   'F', 'l', 'o', 'w', 'e', 'r', 0,  32, 'S',
                                       'e', 'a', 'r', 'c', 'h', 0,   0 };
static const uint8_t k_npc_before[] = { 2,   'H', 'a', 'n', 's', 0,   30, 'T',
                                        'a', 'l', 'k', '-', 't', 'o', 0,  0 };
static const uint8_t k_npc_after[] = { 2,   'H', 'a', 'n', 's', 0,   30, 'T', 'a', 'l', 'k', '-',
                                       't', 'o', 0,   33,  'W', 'a', 'v', 'e', 0,  0 };

enum
{
    LOC_ID = 1187,
    NPC_ID = 3077,
};

static struct RSCache
profile(void)
{
    struct RSCache p = RSCache_ProfileZero();

    p.game = RSCACHE_GAME_OLDSCHOOL;
    p.epoch = RSCACHE_EPOCH_DAT2;
    p.revision = 241;
    return p;
}

/* What Task_Dat2LocLoad does once the group is in hand. */
static void
load_loc(
    struct CacheProvider* provider,
    const uint8_t* bytes,
    size_t size)
{
    struct RSCache p = profile();
    struct RSCache_Dat2ConfigLoc* raw =
        RSCache_Dat2ConfigLocNewDecodeProfile(&p, (char*)bytes, (int)size);

    assert(raw);
    raw->_id = LOC_ID;
    CacheProvider_LocationAdd(provider, LOC_ID, ToriRS_LocationFromRSCacheDat2(LOC_ID, raw));
    RSCache_Dat2ConfigLocFree(raw);
}

static void
load_npc(
    struct CacheProvider* provider,
    const uint8_t* bytes,
    size_t size)
{
    struct RSCache p = profile();
    struct RSCache_Dat2ConfigNpc* raw =
        RSCache_Dat2ConfigNpcNewDecodeProfile(&p, (char*)bytes, (int)size);

    assert(raw);
    CacheProvider_NpctypeAdd(provider, NPC_ID, ToriRS_NpctypeFromRSCacheDat2(NPC_ID, raw));
    RSCache_Dat2ConfigNpcFree(raw);
}

struct HookLog
{
    int calls;
    int index;
    int archive;
};

static void
hook(
    void* user,
    int index,
    int archive)
{
    struct HookLog* log = user;

    log->calls++;
    log->index = index;
    log->archive = archive;
}

static void
test_provider(void)
{
    struct CacheProvider provider;
    struct HookLog log;
    struct ToriRS_Location* old_loc;
    struct ToriRS_Location* loc;
    struct ToriRS_Npctype* npc;

    memset(&provider, 0, sizeof(provider));
    memset(&log, 0, sizeof(log));
    CacheProvider_InitEngineCaches(&provider);
    CacheProvider_SetArchiveInvalidateHook(&provider, hook, &log);

    load_loc(&provider, k_loc_before, sizeof(k_loc_before));
    old_loc = CacheProvider_LocationGet(&provider, LOC_ID);
    check(old_loc && strcmp(old_loc->name, "Flower") == 0, "the loctype is resident: Flower");
    check(old_loc && old_loc->actions[2][0] == '\0', "before the edit it offers no op3");

    check(CacheProvider_InvalidateArchive(&provider, 2, RSCACHE_DAT2_CONFIG_KIND_LOCS) ==
              CACHE_PROVIDER_INVALIDATE_EVICTED,
          "CACHE_INVALIDATE 2/6 evicts the loc records");
    check(CacheProvider_LocationGet(&provider, LOC_ID) == NULL,
          "the next lookup misses (the loader re-decodes)");
    check(log.calls == 1 && log.index == 2 && log.archive == RSCACHE_DAT2_CONFIG_KIND_LOCS,
          "the IO layer's hook hears 2/6");
    check(provider.retired_location_count == 1 && provider.retired_locations[0] == old_loc &&
              strcmp(old_loc->name, "Flower") == 0,
          "the old record is retired, still readable by a holder");

    load_loc(&provider, k_loc_after, sizeof(k_loc_after));
    loc = CacheProvider_LocationGet(&provider, LOC_ID);
    check(loc && loc != old_loc, "the re-decode is a new record");
    check(loc && strcmp(loc->actions[2], "Search") == 0, "the re-decoded loctype offers op3 Search");

    load_npc(&provider, k_npc_before, sizeof(k_npc_before));
    npc = CacheProvider_NpctypeGet(&provider, NPC_ID);
    check(npc && strcmp(npc->actions[0], "Talk-to") == 0 && npc->actions[3][0] == '\0',
          "the npctype is resident: Talk-to, no op4");
    check(CacheProvider_InvalidateArchive(&provider, 2, RSCACHE_DAT2_CONFIG_KIND_NPC) ==
              CACHE_PROVIDER_INVALIDATE_EVICTED,
          "CACHE_INVALIDATE 2/9 evicts the npc records");
    check(CacheProvider_NpctypeGet(&provider, NPC_ID) == NULL, "the npc lookup misses");
    check(CacheProvider_LocationGet(&provider, LOC_ID) == loc, "the loc records are left alone");
    load_npc(&provider, k_npc_after, sizeof(k_npc_after));
    npc = CacheProvider_NpctypeGet(&provider, NPC_ID);
    check(npc && strcmp(npc->actions[3], "Wave") == 0, "the re-decoded npctype offers op4 Wave");

    check(CacheProvider_InvalidateArchive(&provider, 2, RSCACHE_DAT2_CONFIG_KIND_OBJECT) ==
              CACHE_PROVIDER_INVALIDATE_EVICTED,
          "CACHE_INVALIDATE 2/10 evicts the obj records (none resident)");
    log.calls = 0;
    check(CacheProvider_InvalidateArchive(&provider, 2, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE) ==
              CACHE_PROVIDER_INVALIDATE_DEFERRED,
          "a seq group (2/12) stays DEFERRED");
    check(log.calls == 0, "and the IO layer is not told");
    check(CacheProvider_InvalidateArchive(&provider, 5, 1234) == CACHE_PROVIDER_INVALIDATE_EVICTED,
          "CACHE_INVALIDATE 5/<square> evicts (the REBUILD re-reads the square)");
    check(log.calls == 1, "and the IO layer's hook hears the map archive");

    CacheProvider_FreeEngineCaches(&provider);
    check(provider.retired_location_count == 0 && provider.retired_npctype_count == 0,
          "the retired records are freed with the provider");
}

static void
test_group_cache(void)
{
    struct Dat2GroupCache* cache = Dat2GroupCache_New(1u << 20);
    struct RSCache_Dat2DiskArchive archive;
    int ids[1] = { LOC_ID };
    char* data = malloc(sizeof(k_loc_before));

    assert(cache);
    assert(data);
    memcpy(data, k_loc_before, sizeof(k_loc_before));
    memset(&archive, 0, sizeof(archive));
    archive.data = data;
    archive.data_size = (int)sizeof(k_loc_before);
    archive.file_count = 1;
    archive.file_ids = ids;
    check(Dat2GroupCache_Put(cache, 2, 6, &archive) != NULL, "the loc group is split and kept");
    check(Dat2GroupCache_Put(cache, 2, 9, &archive) != NULL, "and an npc group beside it");
    check(Dat2GroupCache_Evict(cache, 2, 6) == 1, "Dat2GroupCache_Evict drops 2/6");
    check(Dat2GroupCache_Get(cache, 2, 6) == NULL, "2/6 now misses (the next read re-reads it)");
    check(Dat2GroupCache_Get(cache, 2, 9) != NULL, "2/9 stays");
    check(Dat2GroupCache_Evict(cache, 2, 6) == 0, "evicting a group not resident answers 0");
    Dat2GroupCache_Free(cache);
    free(data);
}

int
main(void)
{
    test_provider();
    test_group_cache();
    printf("cache-provider-invalidate: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
