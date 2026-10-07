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
#include "torirs_server_servpack.h"
#include "features/features.h"

#include "rscache_profile.h"

#include <assert.h>
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
 * Where a character that has never logged in before starts: the front room of
 * the RuneScape Guide's house on Tutorial Island (map square 48_48, local
 * 22,34).
 *
 * This is a *second* tile, not a replacement for the home one, because the two
 * answer different questions. `home` is where a session that has nowhere else
 * to be is put -- a respawn, a rescue off an expired map instance, every
 * selftest fixture. `tutorial_home` is only ever read once per account, on the
 * login that finds no save file.
 *
 * WHY THIS IS IN C AND NOT IN CONTENT. `[login,_]` running a `p_telejump` was
 * the obvious content-side answer and it is the wrong one: the suite's own
 * "[login] must not move the player" check (torirs_server_world_selftest.c)
 * exists because `~gauntlet_login` did exactly that for two weeks and put
 * every account on the server in the Gauntlet lobby. Logging in is not an
 * event that relocates anybody. Where a character STARTS is a different fact,
 * it belongs beside the home tile, and answering it here means the scene is
 * built on the island in the first place rather than rebuilt a tick later.
 *
 * Everything else about Tutorial Island is content
 * (`OSRS-Content/.../server/scripts/tutorial/`): the steps, the guides, the
 * gates, what the player is given, and where a graduate is put. This is one
 * coordinate, and `TORIRSSERVER_TUTORIAL_HOME=x,z` overrides it the same way
 * `TORIRSSERVER_HOME` overrides the other. Setting it to the home tile is how
 * a world opts out of the tutorial: content's `~newplayer_setup` asks whether
 * the character it is seeding is standing on the island, and deals this
 * world's opening kit immediately when it is not.
 */
#define DEFAULT_TUTORIAL_HOME_X 3094
#define DEFAULT_TUTORIAL_HOME_Z 3106

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
    const char* tutorial_env = getenv("TORIRSSERVER_TUTORIAL_HOME");

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

    config->tutorial_home_x = DEFAULT_TUTORIAL_HOME_X;
    config->tutorial_home_z = DEFAULT_TUTORIAL_HOME_Z;
    if( tutorial_env )
        sscanf(tutorial_env, "%d,%d", &config->tutorial_home_x, &config->tutorial_home_z);
}

int
ToriRSServer_BootLoadContent(
    const char* content_dir,
    const char* cache_dir)
{
    struct RSCache_ServerPack pack;

    assert(content_dir);
    /*
     * 1. The server pack: every config record the server holds -- npc, loc, obj,
     *    seq, healthbar, struct, varbit, param, enum, idk, varp, inv, db -- is the
     *    pack's client record (the tree merged by cachepack) plus its server band.
     *    It is REQUIRED: without one, or with an archive that does not validate,
     *    there is nothing to boot from, and config text is not read in its place.
     *    The cache directory is still read for world data (map squares, models).
     */
    {
        char pack_dir[1024];
        int failed = 0;

        ToriRSServer_ServPackDir(content_dir, cache_dir, pack_dir, sizeof(pack_dir));
        if( !ToriRSServer_ServPackOpen(&pack, pack_dir) )
            return TORIRSSERVER_BOOT_NO_PACK;
        if( !ToriRSServer_ServPackFresh(pack_dir, content_dir) )
        {
            RSCache_ServerPackClose(&pack);
            return TORIRSSERVER_BOOT_NO_PACK;
        }
        failed |= ToriRSServer_ObjInfoLoad(&pack) < 0;
        failed |= !ToriRSServer_NpcInfoLoad(&pack);
        failed |= ToriRSServer_SeqInfoLoad(&pack) < 0;
        failed |= ToriRSServer_HealthbarInfoLoad(&pack) < 0;
        failed |= !ToriRSServer_LocInfoLoad(&pack);
        failed |= ToriRSServer_StructInfoLoad(&pack) < 0;
        failed |= ToriRSServer_VarbitLoad(&pack) < 0;
        failed |= ToriRSServer_SceneLocConfigsLoad(&pack) < 0;

        /* 2. The content tree's non-config data (symbols, registers, constants,
         *    spawns, maps) and the config records still above: param types, enums,
         *    idk, varps and shops. */
        if( !failed )
            ToriRSServer_ContentLoad(content_dir, &pack);

        /* 2b. npc, loc and obj server bands, and the db. */
        failed |= !failed && ToriRSServer_ContentLoadPack(&pack) != 0;

        /* 3. Every interface, component and varbit the engine addresses is a name
         *    in that tree; then the bank's container sizes and varbit ranges. */
        if( !failed )
        {
            ToriRSServer_IdsResolve();
            failed |= ToriRSServer_BankLoad(&pack) < 0;
        }
        RSCache_ServerPackClose(&pack);
        if( failed )
        {
            fprintf(stderr, "torirsserver: the server pack at %s was refused — rebuild it with "
                            "`%s`\n",
                    pack_dir, TORIRSSERVER_SERVPACK_FIX);
            return TORIRSSERVER_BOOT_NO_PACK;
        }
    }

    return 0;
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

    if( ToriRSServer_BootLoadContent(config->content_dir, config->cache_dir) != 0 )
        return TORIRSSERVER_BOOT_NO_PACK;

    ToriRSServer_WorldSetHome(config->home_x, config->home_z);
    ToriRSServer_WorldSetTutorialHome(config->tutorial_home_x, config->tutorial_home_z);

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
