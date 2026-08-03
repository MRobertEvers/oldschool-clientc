/*
 * The loader sequence. See mock230_boot.h for why the order is a function
 * rather than a comment.
 */

#include "mock230_boot.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_content.h"
#include "mock230_db.h"
#include "mock230_ids.h"
#include "mock230_scene.h"
#include "features/features.h"

#include "rscache_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Where a session starts: the Lumbridge castle courtyard, one tile from Hans.
 *
 * OpenRune calls the same place home (`home-x: 3218, home-z: 3218` in its
 * game.yml) and so does OldSchool. It is the right default because everything
 * worth exercising is within a short walk: goblins and guards east on the Al
 * Kharid road, rats under the castle, cows and chickens north-east, a general
 * store, and stairs, ladders, doors and a trapdoor in the castle itself.
 */
#define DEFAULT_HOME_X 3222
#define DEFAULT_HOME_Z 3218

/*
 * The ../ fallback: the server is run both from the repo root and from src/,
 * and having it work either way is worth more than insisting on one.
 */
static const char*
resolve_content_dir(void)
{
    static char resolved[512];
    const char* configured = getenv("MOCK230_CONTENT");
    struct stat info;

    if( configured )
        return configured;

    snprintf(resolved, sizeof(resolved), "OSRS-Content/osrs239-content");
    if( stat(resolved, &info) == 0 )
        return resolved;

    snprintf(resolved, sizeof(resolved), "../OSRS-Content/osrs239-content");
    return resolved;
}

void
mock230_boot_defaults(struct Mock230BootConfig* config)
{
    static char script_dir[600];
    const char* cache_env = getenv("MOCK230_CACHE");
    const char* script_env = getenv("MOCK230_SCRIPTS");
    const char* home_env = getenv("MOCK230_HOME");

    memset(config, 0, sizeof(*config));
    config->cache_dir = cache_env ? cache_env : MOCK230_CACHE_DIR_DEFAULT;
    config->content_dir = resolve_content_dir();

    if( script_env )
    {
        config->script_dir = script_env;
    }
    else
    {
        snprintf(script_dir, sizeof(script_dir), "%s/server/scripts/build",
                 config->content_dir);
        config->script_dir = script_dir;
    }

    config->home_x = DEFAULT_HOME_X;
    config->home_z = DEFAULT_HOME_Z;
    if( home_env )
        sscanf(home_env, "%d,%d", &config->home_x, &config->home_z);
}

int
mock230_boot_load(const struct Mock230BootConfig* config)
{
    struct ToriRS_FeatureTable const* features;

    mock230_world_set_cache_dir(config->cache_dir);

    /* Era feature table — approach model and op-click nearest fallback. The
     * server and client must agree; this cache is OldSchool/dat2 so OSRS. */
    features = ToriRS_Features_ForCache(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2,
                                        MOCK230_CACHE_REVISION);
    mock230_scene_set_features(features);
    fprintf(stderr, "mock230: features era=%s approach=%s op_nearest=%d\n", features->name,
            features->approach_model == TORIRS_APPROACH_RECT ? "rect" : "legacy",
            features->op_click_nearest_range);

    /* 1. The cache's own tables. The content tree overlays these, so they have
     *    to exist before it is read. */
    mock230_objinfo_load(config->cache_dir);
    mock230_npcinfo_load(config->cache_dir);
    mock230_seqinfo_load(config->cache_dir);
    /* The other two param tables. Nothing seeds content from these — they exist
     * for `lc_param` and `struct_param` — but they belong with the rest of the
     * cache's tables, which is what step 1 means. */
    mock230_locinfo_load(config->cache_dir);
    mock230_structinfo_load(config->cache_dir);
    /* Varbit bit-ranges, from the same cache the client unpacks them with. */
    mock230_varbit_load(config->cache_dir);

    /* 2. The content tree, whose combat bonuses are seeded from the params
     *    step 1 decoded. */
    mock230_content_load(config->content_dir);

    /*
     * 2b. The server band `cachepack pack` wrote (PORTING_GUIDE §3.6 item 1).
     *
     * The band is the preferred source for the npc/loc server fields; the text
     * pass step 2 just ran is, during migration, both the fallback and the
     * proof — nothing from the band is applied until every archive has been
     * held to what the text loaded. Which of the three outcomes happened is
     * worth a line each boot, because "which path loaded" is exactly the
     * question this migration keeps raising.
     */
    {
        struct Mock230BandReport band;

        switch( mock230_content_load_server_band(config->content_dir, &band) )
        {
        case MOCK230_BAND_LOADED:
            fprintf(stderr,
                    "mock230: server band loaded: %d archive(s) verified identical to the "
                    "text parse and applied (%d overlay authored defs, %d field value(s) "
                    "text-only, %d archive(s) over records the runtime never loads)\n",
                    band.archives, band.overlaid, band.text_only, band.unseeded);
            break;
        case MOCK230_BAND_MISSING:
            fprintf(stderr, "mock230: no server/pack — text overlays only; run "
                            "`make -C src mock230-servpack` to build the band\n");
            break;
        case MOCK230_BAND_STALE:
            fprintf(stderr,
                    "mock230: server band is STALE (%d unreadable, %d mismatched archive(s)) "
                    "— text overlays kept; re-run `make -C src mock230-servpack`\n",
                    band.invalid, band.mismatched);
            break;
        }
    }

    /* 3. The db tables, which resolve their own ids and their `^constants` out
     *    of what step 2 loaded. Separate from step 2 because a `.dbrow` also
     *    resolves obj/npc/loc names, so it needs the whole symbol space, not
     *    just the packs. Authored `.dbtable` / `.dbrow` first; then the cache's
     *    own DBTABLE/DBROW binary fills the cache-id half (quest, …). */
    mock230_db_load(config->content_dir);
    mock230_db_load_cache(config->cache_dir);

    /* 4. Every interface, component and varbit the engine addresses is a name
     *    in that tree. */
    mock230_ids_resolve();

    /* 4. Container sizes and varbit bit ranges for the bank, which looks its
     *    container up by an id step 3 resolved. */
    mock230_bank_load(config->cache_dir);

    mock230_world_set_home(config->home_x, config->home_z);

    return mock230_content_error_count();
}

void
mock230_boot_free(void)
{
    mock230_objinfo_free();
    mock230_npcinfo_free();
    mock230_seqinfo_free();
    mock230_locinfo_free();
    mock230_structinfo_free();
    mock230_varbit_free();
    /* Before the content, which owns the `^constants` a dbrow's values expanded
     * from — the strdup'd copies are ours, but the diagnostics on a double free
     * are much clearer when teardown mirrors load order. */
    mock230_db_free();
    mock230_content_free();
}
