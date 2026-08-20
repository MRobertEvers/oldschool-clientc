/*
 * The loader sequence. See torirs_server_boot.h for why the order is a function
 * rather than a comment.
 */

#include "torirs_server_boot.h"

#include "torirs_server.h"
#include "torirs_server_bank.h"
#include "torirs_server_content.h"
#include "torirs_server_db.h"
#include "torirs_server_ids.h"
#include "torirs_server_scene.h"
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
 * A boolean server flag that is ON unless the environment turns it off.
 *
 * The ordinary `getenv(...) != NULL` idiom cannot express this: a flag that
 * defaults on needs a way to say "no", and "unset the variable" is not
 * available to a launcher that sets its whole environment from a config. `0`,
 * `no`, `off` and `false` all disable; anything else, including unset, leaves
 * it on.
 *
 * It lives here rather than beside the other env reads in torirs_server_main.c
 * because main.c is the standalone server binary only -- the embed, the
 * selftest and embed_test link TORIRSSERVER_CORE_SRCS without it, and all three
 * call this.
 */
int
ToriRSServer_FlagDefaultOn(const char* name)
{
    const char* value = getenv(name);

    if( !value || !*value )
        return 1;
    return !(strcmp(value, "0") == 0 || strcmp(value, "no") == 0 ||
             strcmp(value, "off") == 0 || strcmp(value, "false") == 0);
}

/*
 * The ../ fallback: the server is run both from the repo root and from src/,
 * and having it work either way is worth more than insisting on one.
 */
static const char*
resolve_content_dir(void)
{
    static char resolved[512];
    const char* configured = getenv("TORIRSSERVER_CONTENT");
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
ToriRSServer_BootDefaults(struct ToriRSServerBootConfig* config)
{
    static char script_dir[600];
    const char* cache_env = getenv("TORIRSSERVER_CACHE");
    const char* script_env = getenv("TORIRSSERVER_SCRIPTS");
    const char* home_env = getenv("TORIRSSERVER_HOME");

    memset(config, 0, sizeof(*config));
    config->cache_dir = cache_env ? cache_env : TORIRSSERVER_CACHE_DIR_DEFAULT;
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
ToriRSServer_BootLoad(const struct ToriRSServerBootConfig* config)
{
    /* The server's own copy of the era table, so the env override below has
     * somewhere writable to land. Static because ToriRSServer_Scene keeps a pointer
     * to it for the life of the process. */
    static struct ToriRS_FeatureTable features;

    ToriRSServer_WorldSetCacheDir(config->cache_dir);

    /* Era feature table — approach model and nearest fallbacks. The server and
     * client must agree; this cache is OldSchool/dat2 so OSRS. */
    features = *ToriRS_Features_ForCache(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2,
                                         TORIRSSERVER_CACHE_REVISION);
    /* The server half of the client's `[features:boot] ground_click_nearest`.
     * Modern routing happens here, not in the client, so this is the knob that
     * actually decides where an unreachable ground click puts the player. */
    {
        char const* env = getenv("TORIRSSERVER_GROUND_CLICK_NEAREST");
        int model = env && env[0] ? ToriRS_Features_NearestModelByName(env) : -1;
        if( env && env[0] && model < 0 )
            fprintf(stderr,
                    "torirsserver: TORIRSSERVER_GROUND_CLICK_NEAREST must be "
                    "ring3|box10_rect|none, got '%s'\n",
                    env);
        else if( model >= 0 )
            features.ground_click_nearest_model = model;
    }
    /*
     * The unbounded fallback is a routing decision, so under a modern era it
     * is entirely the server's: the client sends a tile and nothing else. It
     * reads the SAME variable name the client half reads (app.c) rather than a
     * TORIRSSERVER_ one on purpose — the two halves disagreeing about how far a
     * click may miss is exactly the failure the shared feature field exists to
     * prevent, and one name makes an embedded boot consistent by construction.
     */
    {
        char const* env = getenv("TORIRS_GROUND_CLICK_UNBOUNDED");
        if( env && env[0] )
            features.ground_click_nearest_unbounded = env[0] != '0';
    }
    /*
     * Walking out from under a large npc. Server-only by construction — the
     * client sends OPNPC with no coordinates and never routes — so unlike the
     * ground-click knobs above there is no client half to keep in step.
     *
     * It exists so the two answers can be run back to back against the same
     * boss: a test that only ever sees the routed exit cannot show that the
     * random step-off was the thing being fixed.
     */
    {
        char const* env = getenv("TORIRSSERVER_UNDER_TARGET_ROUTES_OUT");
        if( env && env[0] )
            features.under_target_routes_out = env[0] != '0';
    }
    /*
     * Run energy. Server-only — the client is sent a percentage and computes
     * nothing — so there is no client half to keep in step, unlike the
     * ground-click knobs above. It exists so the pre- and post-2025 Agility
     * arithmetic can be measured back to back on the same account.
     */
    {
        char const* env = getenv("TORIRSSERVER_RUN_ENERGY");
        int model = env && env[0] ? ToriRS_Features_RunEnergyModelByName(env) : -1;
        if( env && env[0] && model < 0 )
            fprintf(stderr,
                    "torirsserver: TORIRSSERVER_RUN_ENERGY must be classic|osrs2025, got '%s'\n",
                    env);
        else if( model >= 0 )
            features.run_energy_model = model;
    }
    ToriRSServer_SceneSetFeatures(&features);
    fprintf(stderr,
            "torirsserver: features era=%s approach=%s op_nearest=%d ground_nearest=%s "
            "unbounded=%d under_routes_out=%d run_energy=%s\n",
            features.name,
            features.approach_model == TORIRS_APPROACH_RECT ? "rect" : "legacy",
            features.op_click_nearest_range,
            ToriRS_Features_NearestModelName(features.ground_click_nearest_model),
            features.ground_click_nearest_unbounded,
            features.under_target_routes_out,
            ToriRS_Features_RunEnergyModelName(features.run_energy_model));

    /* 1. The cache's own tables. The content tree overlays these, so they have
     *    to exist before it is read. */
    ToriRSServer_ObjInfoLoad(config->cache_dir);
    ToriRSServer_NpcInfoLoad(config->cache_dir);
    ToriRSServer_SeqInfoLoad(config->cache_dir);
    ToriRSServer_HealthbarInfoLoad(config->cache_dir);
    /* The other two param tables. Nothing seeds content from these — they exist
     * for `lc_param` and `struct_param` — but they belong with the rest of the
     * cache's tables, which is what step 1 means. */
    ToriRSServer_LocInfoLoad(config->cache_dir);
    ToriRSServer_StructInfoLoad(config->cache_dir);
    /* Varbit bit-ranges, from the same cache the client unpacks them with. */
    ToriRSServer_VarbitLoad(config->cache_dir);

    /* 2. The content tree, whose combat bonuses are seeded from the params
     *    step 1 decoded. */
    ToriRSServer_ContentLoad(config->content_dir);

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
        struct ToriRSServerBandReport band;

        switch( ToriRSServer_ContentLoadServerBand(config->content_dir, &band) )
        {
        case TORIRSSERVER_BAND_LOADED:
            fprintf(stderr,
                    "torirsserver: server band loaded: %d archive(s) verified identical to the "
                    "text parse and applied (%d overlay authored defs, %d field value(s) "
                    "text-only, %d archive(s) over records the runtime never loads)\n",
                    band.archives, band.overlaid, band.text_only, band.unseeded);
            break;
        case TORIRSSERVER_BAND_MISSING:
            fprintf(stderr, "torirsserver: no server/pack — text overlays only; run "
                            "`make -C src torirsserver-servpack` to build the band\n");
            break;
        case TORIRSSERVER_BAND_STALE:
            fprintf(stderr,
                    "torirsserver: server band is STALE (%d unreadable, %d mismatched archive(s)) "
                    "— text overlays kept; re-run `make -C src torirsserver-servpack`\n",
                    band.invalid, band.mismatched);
            break;
        }
    }

    /* 3. The db tables, which resolve their own ids and their `^constants` out
     *    of what step 2 loaded. Separate from step 2 because a `.dbrow` also
     *    resolves obj/npc/loc names, so it needs the whole symbol space, not
     *    just the packs. Three phases, in this order for two different
     *    reasons: the cache's SCHEMAS first, because an authored `.dbrow` may
     *    name a cache table (`poh_hotspot`) and cannot resolve one that has not
     *    arrived; the tree next; the cache's ROW VALUES last, because that half
     *    merges onto whatever the tree stated, so authored values win. */
    ToriRSServer_DbFree();
    ToriRSServer_DbLoadCacheTables(config->cache_dir);
    ToriRSServer_DbLoad(config->content_dir);
    ToriRSServer_DbLoadCacheRows(config->cache_dir);

    /* 4. Every interface, component and varbit the engine addresses is a name
     *    in that tree. */
    ToriRSServer_IdsResolve();

    /* 4. Container sizes and varbit bit ranges for the bank, which looks its
     *    container up by an id step 3 resolved. */
    ToriRSServer_BankLoad(config->cache_dir);

    ToriRSServer_WorldSetHome(config->home_x, config->home_z);

    return ToriRSServer_ContentErrorCount();
}

void
ToriRSServer_BootFree(void)
{
    ToriRSServer_ObjInfoFree();
    ToriRSServer_NpcInfoFree();
    ToriRSServer_SeqInfoFree();
    ToriRSServer_LocInfoFree();
    ToriRSServer_StructInfoFree();
    ToriRSServer_VarbitFree();
    /* Before the content, which owns the `^constants` a dbrow's values expanded
     * from — the strdup'd copies are ours, but the diagnostics on a double free
     * are much clearer when teardown mirrors load order. */
    ToriRSServer_DbFree();
    ToriRSServer_ContentFree();
}
