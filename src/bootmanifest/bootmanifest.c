#include "ui/torirs_chrome_exec_kind.h"

#include "bootmanifest.h"

#include "app.h"
#include "executor_config.h"
#include "features/features.h"

#include "3rd/ini/ini.h"
#include "3rd/rscache/src/rscache_profile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

_Static_assert(
    BOOTMANIFEST_DEBUG_HOTKEY_MAX <= APP_DEBUG_HOTKEY_MAX,
    "AppConfig must hold every manifest debug-hotkey binding");

/*
 * Is this path already anchored, or is it relative to the manifest?
 *
 * The test used to be a leading '/', which is every anchored path on POSIX and
 * only some of them on Windows: `C:\\cache` begins with a letter, so it read as
 * relative and was joined onto the manifest's directory to produce
 * `build/manifests/C:\\cache`. The drive form and the UNC form are both named
 * here. Both are recognised on every platform -- a manifest is not necessarily
 * read on the machine that wrote it, and a path that is anchored somewhere is
 * never something to join a directory onto.
 */
/*
 * Is this character a path separator?
 *
 * '/' everywhere, and '\\' on Windows as well. The distinction is not
 * pedantry: on POSIX a backslash is an ordinary, legal character in a filename,
 * so treating it as a separator there would split a validly-named file in two.
 * On Windows both spellings name the same file to every API in this tree, and
 * a caller has no reason to expect one to work and the other not.
 */
static int
bm_is_sep(char c)
{
    if( c == '/' )
        return 1;
#ifdef _WIN32
    if( c == '\\' )
        return 1;
#endif
    return 0;
}

static int
bm_path_is_absolute(char const* value)
{
    if( value[0] == '/' || value[0] == '\\' )
        return 1;
    if( value[0] != '\0' && value[1] == ':'
        && (value[2] == '/' || value[2] == '\\') )
        return 1;
    return 0;
}

/*
 * The user's home, resolved the way the 239 client resolves its own.
 *
 * That client tries, in order: the `jagex.userhome` system property, the
 * `user.home` property, then the environment -- USERPROFILE on Windows and
 * HOME everywhere else. TORIRS_USERHOME is the first of those; the middle one
 * is a JVM concept with no C analogue, and on every platform this tree builds
 * for it is derived from the same environment variable the third step reads,
 * so the two collapse into one.
 *
 * Returns 0 when the environment does not say. That is a real state -- a
 * service account, a stripped container -- and the caller falls back to
 * streaming rather than inventing a path to write the user's cache into.
 */
static int
bm_user_home(char* dst, size_t cap)
{
    char const* home = getenv("TORIRS_USERHOME");
    if( home && home[0] )
    {
        snprintf(dst, cap, "%s", home);
        return 1;
    }
    home = getenv(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    if( home && home[0] )
    {
        snprintf(dst, cap, "%s", home);
        return 1;
    }
#ifdef _WIN32
    {
        /* The pair NT sets when USERPROFILE does not survive -- a service, or
         * a shell started without the user's environment. */
        char const* drive = getenv("HOMEDRIVE");
        char const* path = getenv("HOMEPATH");
        if( drive && drive[0] && path && path[0] )
        {
            snprintf(dst, cap, "%s%s", drive, path);
            return 1;
        }
    }
#endif
    return 0;
}

/*
 * Is this cache location a browser database rather than a directory?
 *
 * `idb:<name>` names an IndexedDB database. The web build has no filesystem to
 * put a cache directory in, so its cache location is a database name, and the
 * two have to be tellable apart by everything that handles the value -- a
 * database name joined onto a manifest's directory would be nonsense, and so
 * would one handed to mkdir.
 *
 * Recognised on every platform, not only the web one. A manifest is read by
 * whatever tool is pointed at it, and a value that means "a database" must not
 * quietly become a relative directory in a build that cannot use it.
 */
int
BootManifest_CacheLocationIsIdb(char const* value)
{
    assert(value);
    return strncmp(value, "idb:", 4) == 0;
}

/* Join a manifest-relative value onto the manifest's directory. An anchored
 * value and an empty base copy through unchanged, a leading `~/` is the user's
 * home rather than a directory named "~", and an `idb:` database name is not a
 * path at all. */
static void
bm_join_path(char* dst, size_t cap, char const* manifest_dir, char const* value)
{
    if( BootManifest_CacheLocationIsIdb(value) )
    {
        snprintf(dst, cap, "%s", value);
        return;
    }
    if( value[0] == '~' && (value[1] == '/' || value[1] == '\\') )
    {
        char home[512];
        if( bm_user_home(home, sizeof(home)) )
        {
            snprintf(dst, cap, "%s/%s", home, value + 2);
            return;
        }
        /* No home to expand against. Fall through and treat it literally,
         * which fails visibly at open time rather than silently writing the
         * cache into a directory called "~" beside the manifest. */
    }
    if( bm_path_is_absolute(value) || manifest_dir[0] == '\0' )
    {
        snprintf(dst, cap, "%s", value);
        return;
    }
    snprintf(dst, cap, "%s/%s", manifest_dir, value);
}

/* The manifest's filename without directory or extension -- `rs289lc-xp` from
 * `build/manifests/rs289lc-xp.ini`. It is what a person calls this world, so
 * it is what names the world's cache on disk. */
static void
bm_basename_stem(char* dst, size_t cap, char const* path)
{
    char const* base = path;
    for( char const* p = path; *p; p++ )
        if( bm_is_sep(*p) )
            base = p + 1;

    size_t n = strlen(base);
    char const* dot = strrchr(base, '.');
    if( dot && dot != base )
        n = (size_t)(dot - base);
    if( n >= cap )
        n = cap - 1;

    memcpy(dst, base, n);
    dst[n] = '\0';

    /* Whatever ends up in a directory name has to be safe to put in one. */
    for( char* p = dst; *p; p++ )
        if( *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<'
            || *p == '>' || *p == '|' )
            *p = '_';
}

/* Parse "a,b,c,..." (9 int32s) into out. Returns 1 on exactly-9 success. */
static int
bm_parse_crc_list(char const* value, int32_t out[9])
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", value);

    int n = 0;
    char* save = NULL;
    for( char* tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save) )
    {
        if( n >= 9 )
            return 0; /* too many */
        out[n++] = (int32_t)strtol(tok, NULL, 10);
    }
    return n == 9;
}

/* Parse a non-negative int while allowing surrounding blanks and a trailing
 * comment. Only [ui:gameframe] uses it — see its case in bm_set_kv. */
static int
bm_parse_padded_int(char const* key, char const* value, int* out)
{
    char* end = NULL;
    long v;

    assert(key && value && out);
    while( *value == ' ' || *value == '\t' )
        value++;
    v = strtol(value, &end, 10);
    if( end == value || v < 0 )
    {
        TORIRS_LOG("bootmanifest: '%s' must be a non-negative int, got '%s'\n", key, value);
        return 0;
    }
    while( *end == ' ' || *end == '\t' )
        end++;
    /* ':' ends the parent id in a qualified [ui:gameframe] key; ';'/'#' start a
     * trailing comment. */
    if( *end != '\0' && *end != ';' && *end != '#' && *end != ':' )
    {
        TORIRS_LOG("bootmanifest: trailing junk after '%s': '%s'\n", key, end);
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* Signed int — light components and ambient/attenuation offsets may be negative. */
static int
bm_parse_int(char const* key, char const* value, int* out)
{
    char* end = NULL;
    long v;

    assert(key && value && out);
    v = strtol(value, &end, 10);
    if( end == value || *end != '\0' )
    {
        TORIRS_LOG("bootmanifest: '%s' must be an int, got '%s'\n", key, value);
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int
bm_parse_bounded_int(
    char const* section,
    char const* key,
    char const* value,
    int min,
    int max,
    int* out)
{
    char* end = NULL;
    long parsed;

    assert(section && key && value && out);
    parsed = strtol(value, &end, 10);
    if( end == value || *end != '\0' || parsed < min || parsed > max )
    {
        TORIRS_LOG("bootmanifest: [%s] %s must be in %d..%d, got '%s'\n",
            section,
            key,
            min,
            max,
            value);
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

/* "x,y,z" signed triple for actor_light / scene_light. */
static int
bm_parse_xyz(char const* key, char const* value, int* x, int* y, int* z)
{
    char* end = NULL;
    long vx, vy, vz;

    assert(key && value && x && y && z);
    vx = strtol(value, &end, 10);
    if( end == value || *end != ',' )
        goto bad;
    value = end + 1;
    vy = strtol(value, &end, 10);
    if( end == value || *end != ',' )
        goto bad;
    value = end + 1;
    vz = strtol(value, &end, 10);
    if( end == value || *end != '\0' )
        goto bad;
    *x = (int)vx;
    *y = (int)vy;
    *z = (int)vz;
    return 1;
bad:
    TORIRS_LOG("bootmanifest: '%s' must be x,y,z ints, got '%s'\n", key, value);
    return 0;
}

enum bm_section
{
    BM_SECTION_NONE = 0,
    BM_SECTION_CLIENT_ARGS,
    BM_SECTION_CACHE,
    BM_SECTION_NET,
    BM_SECTION_JS5,
    BM_SECTION_IO,
    BM_SECTION_UI,
    BM_SECTION_UI_GAMEFRAME,
    BM_SECTION_UI_VARC,
    BM_SECTION_ACTION,
    BM_SECTION_DEBUG_HOTKEYS,
    BM_SECTION_FEATURES,
    BM_SECTION_RENDER,
    BM_SECTION_CONTENT,
    BM_SECTION_EDITOR,
    BM_SECTION_CHROME,
    /* Inline RevConfig. Recognised so the keys are not reported as unknown, but
     * never decoded here — revconfig_load_fields_from_ini_prefixed re-reads the
     * file for them, because only the RevConfig parser knows that dialect. */
    BM_SECTION_REVCONFIG,
};

static enum bm_section
bm_section_of(char const* header)
{
    /* Headers are "type:name"; we only care about the type prefix. */
    if( strcmp(header, "client:args") == 0 )
        return BM_SECTION_CLIENT_ARGS;
    if( strncmp(header, "cache:", 6) == 0 )
        return BM_SECTION_CACHE;
    if( strncmp(header, "net:", 4) == 0 )
        return BM_SECTION_NET;
    if( strncmp(header, "js5:", 4) == 0 )
        return BM_SECTION_JS5;
    if( strncmp(header, "io:", 3) == 0 )
        return BM_SECTION_IO;
    if( strcmp(header, "ui:gameframe") == 0 )
        return BM_SECTION_UI_GAMEFRAME;
    if( strcmp(header, "ui:varc") == 0 )
        return BM_SECTION_UI_VARC;
    if( strncmp(header, "ui:", 3) == 0 )
        return BM_SECTION_UI;
    if( strncmp(header, "action:", 7) == 0 )
        return BM_SECTION_ACTION;
    if( strcmp(header, "debug:hotkeys") == 0 )
        return BM_SECTION_DEBUG_HOTKEYS;
    if( strncmp(header, "features:", 9) == 0 )
        return BM_SECTION_FEATURES;
    if( strncmp(header, "render:", 7) == 0 )
        return BM_SECTION_RENDER;
    if( strncmp(header, "content:", 8) == 0 )
        return BM_SECTION_CONTENT;
    if( strncmp(header, "editor:", 7) == 0 )
        return BM_SECTION_EDITOR;
    if( strncmp(header, "chrome", 6) == 0 )
        return BM_SECTION_CHROME;
    if( strncmp(header, "revconfig:", 10) == 0 )
        return BM_SECTION_REVCONFIG;
    return BM_SECTION_NONE;
}

static void
bm_set_kv(
    struct BootManifest* bm,
    char const* manifest_dir,
    enum bm_section section,
    char const* section_name,
    char const* key,
    char const* value)
{
    if( key[0] == '\0' )
        return;

    switch( section )
    {
    case BM_SECTION_CLIENT_ARGS:
        if( strcmp(key, "arg") == 0 )
        {
            if( bm->client_arg_count >= BOOTMANIFEST_CLIENT_ARG_MAX )
            {
                TORIRS_LOG("bootmanifest: [client:args] holds at most %d arguments\n",
                    BOOTMANIFEST_CLIENT_ARG_MAX);
                bm->client_args_error = 1;
                return;
            }
            snprintf(
                bm->client_args[bm->client_arg_count],
                sizeof(bm->client_args[bm->client_arg_count]),
                "%s",
                value);
            bm->client_arg_count++;
            return;
        }
        break;

    case BM_SECTION_CONTENT:
        /* Read and kept, not acted on: the lanes are a BUILD input (which
         * content went into the script pack `scripts=` names), and the launcher
         * is what reads them back out. Parsing them here rather than only in
         * the shell keeps one spelling of the key, and keeps the section from
         * being reported as unknown by every client that boots the file. */
        if( strcmp(key, "lane") == 0 )
        {
            if( bm->lane_count >= BOOTMANIFEST_LANE_MAX )
            {
                TORIRS_LOG("bootmanifest: [content:lanes] holds at most %d lanes\n",
                    BOOTMANIFEST_LANE_MAX);
                bm->lanes_error = 1;
                return;
            }
            snprintf(
                bm->lanes[bm->lane_count],
                sizeof(bm->lanes[bm->lane_count]),
                "%s",
                value);
            bm->lane_count++;
            return;
        }
        break;

    case BM_SECTION_EDITOR:
        /* The map editor edits the content tree's `.jm2`/`.jl2` map sources, so
         * it needs to be pointed at one. Stating the directory is what turns
         * the editor on — there is no separate enable, because an editor with
         * nothing to edit is not a mode worth booting into. */
        if( strcmp(key, "content_dir") == 0 )
        {
            bm_join_path(
                bm->editor_content_dir, sizeof(bm->editor_content_dir), manifest_dir, value);
            return;
        }
        /* Where a bake would run from. Absent means baking is unavailable this
         * session, which is the right default for a manifest that only wants to
         * look at the map. */
        if( strcmp(key, "repo_root") == 0 )
        {
            bm_join_path(bm->editor_repo_root, sizeof(bm->editor_repo_root), manifest_dir, value);
            return;
        }
        /* Which ToriRSMapEd deployment this session's edits go through — the
         * editor's counterpart of [net:boot] naming the game server. Named
         * values only, and an unknown one is a hard error rather than a
         * fallback: silently editing a different tree than the manifest
         * asked for is the worst possible reading of a typo. */
        if( strcmp(key, "server") == 0 )
        {
            if( strcmp(value, "embed") == 0 )
                bm->editor_server = BOOTMANIFEST_EDITOR_SERVER_EMBED;
            else if( strcmp(value, "tcp") == 0 )
                bm->editor_server = BOOTMANIFEST_EDITOR_SERVER_TCP;
            else
            {
                TORIRS_LOG("bootmanifest: [editor:boot] server must be embed|tcp, got '%s'\n",
                    value);
                bm->editor_server_error = 1;
            }
            return;
        }
        /* Where the torirsmaped daemon listens; only server=tcp reads them. */
        if( strcmp(key, "host") == 0 )
        {
            snprintf(bm->editor_server_host, sizeof(bm->editor_server_host), "%s", value);
            return;
        }
        if( strcmp(key, "port") == 0 )
        {
            bm->editor_server_port = atoi(value);
            return;
        }
        /* Which Client to join — the handle a running session printed. */
        if( strcmp(key, "client") == 0 )
        {
            bm->editor_client_id = atoi(value);
            return;
        }
        /* Which binding draws the command panel. Named values only, and an
         * unknown one is a hard error rather than a fallback to the default:
         * a manifest that asks for a panel this build cannot open should say
         * so at load time, next to the line that asked. */
        if( strcmp(key, "panel") == 0 )
        {
            if( strcmp(value, "inprocess") == 0 )
                bm->editor_panel = BOOTMANIFEST_EDITOR_PANEL_INPROCESS;
            else if( strcmp(value, "tab") == 0 )
                bm->editor_panel = BOOTMANIFEST_EDITOR_PANEL_TAB;
            else
            {
                TORIRS_LOG("bootmanifest: [editor:boot] panel must be inprocess|tab, got '%s'\n",
                    value);
                bm->editor_panel_error = 1;
            }
            return;
        }
        break;

    case BM_SECTION_CHROME:
        /*
         * Which presentation the plugin window uses. Named values only, and an
         * unknown one is a hard error rather than a silent fallback: a manifest
         * that asks for an executor by a name that does not exist should say so
         * at load time, next to the line that asked.
         *
         * A name this BUILD has no executor for is NOT an error -- every lane
         * carries a different set, and the chooser answers that with the
         * in-canvas one and a message. TORIRS_CHROME_EXECUTOR overrides this,
         * matching TORIRS_CHROME_THEME beside it.
         */
        if( strcmp(key, "executor") == 0 )
        {
            int const kind = ToriRSChromeExec_KindFromName(value);
            if( kind < 0 )
            {
                TORIRS_LOG("bootmanifest: [chrome] executor must be "
                    "web|browser, got '%s'\n",
                    value);
                bm->chrome_executor_error = 1;
            }
            else
            {
                bm->chrome_executor = kind;
                bm->chrome_executor_set = 1;
            }
            return;
        }
        break;

    case BM_SECTION_CACHE:
        if( strcmp(key, "epoch") == 0 )
        {
            int epoch = RSCache_EpochFromName(value);
            if( epoch == RSCACHE_EPOCH_UNSET )
            {
                TORIRS_LOG("bootmanifest: [cache] epoch must be dat1|dat2, got '%s'\n", value);
                return;
            }
            bm->cache_epoch = epoch;
            bm->cache_kind = epoch == RSCACHE_EPOCH_DAT1 ? APP_CACHE_DAT1 : APP_CACHE_DAT2;
            return;
        }
        if( strcmp(key, "game") == 0 )
        {
            int game = RSCache_GameFromName(value);
            if( game == RSCACHE_GAME_UNSET )
            {
                TORIRS_LOG("bootmanifest: [cache] game must be rs2|oldschool, got '%s'\n", value);
                return;
            }
            bm->cache_game = game;
            return;
        }
        if( strcmp(key, "revision") == 0 )
        {
            int rev = atoi(value);
            if( rev <= 0 )
            {
                TORIRS_LOG("bootmanifest: [cache] revision must be a positive int, got '%s'\n",
                    value);
                return;
            }
            bm->cache_revision = rev;
            return;
        }
        if( strcmp(key, "quirks") == 0 )
        {
            uint32_t quirks = RSCACHE_QUIRK_NONE;
            if( !RSCache_QuirksFromList(value, &quirks) )
            {
                TORIRS_LOG("bootmanifest: [cache] quirks must be none|kronos|void_rs634_no_xteas, got '%s'\n",
                    value);
                return;
            }
            bm->cache_quirks = quirks;
            bm->cache_quirks_set = 1;
            return;
        }
        if( strcmp(key, "dir") == 0 )
        {
            /* Stating it at all opts out of the default below, INCLUDING
             * stating it empty. `dir=` with nothing after it is the way a
             * world says "stream every boot, write nothing down" now that
             * saying nothing means the opposite. */
            bm->cache_dir_stated = 1;
            if( value[0] == '\0' )
            {
                bm->cache_dir[0] = '\0';
                return;
            }
            bm_join_path(bm->cache_dir, sizeof(bm->cache_dir), manifest_dir, value);
            return;
        }
        if( strcmp(key, "source") == 0 )
        {
            if( strcmp(value, "disk") == 0 )
            {
                bm->cache_on_demand = 0;
                return;
            }
            if( strcmp(value, "ondemand") == 0 )
            {
                bm->cache_on_demand = 1;
                return;
            }
            TORIRS_LOG("bootmanifest: [cache] source must be disk|ondemand, got '%s'\n",
                value);
            return;
        }
        if( strcmp(key, "spawn") == 0 )
        {
            int spawn_x = -1;
            int spawn_z = -1;
            if( sscanf(value, "%d,%d", &spawn_x, &spawn_z) == 2 && spawn_x >= 0 && spawn_z >= 0 )
            {
                bm->spawn_x = spawn_x;
                bm->spawn_z = spawn_z;
            }
            else
            {
                TORIRS_LOG("bootmanifest: [cache] spawn must be \"x,z\", got '%s'\n", value);
            }
            return;
        }
        break;

    case BM_SECTION_NET:
        if( strcmp(key, "rev") == 0 )
        {
            snprintf(bm->rev_name, sizeof(bm->rev_name), "%s", value);
            return;
        }
        if( strcmp(key, "transport") == 0 )
        {
            snprintf(bm->transport, sizeof(bm->transport), "%s", value);
            return;
        }
        if( strcmp(key, "host") == 0 )
        {
            snprintf(bm->host, sizeof(bm->host), "%s", value);
            return;
        }
        if( strcmp(key, "port") == 0 )
        {
            bm->port = atoi(value);
            return;
        }
        if( strcmp(key, "ws_host") == 0 )
        {
            snprintf(bm->ws_host, sizeof(bm->ws_host), "%s", value);
            return;
        }
        if( strcmp(key, "ws_port") == 0 )
        {
            bm->ws_port = atoi(value);
            return;
        }
        if( strcmp(key, "client_version") == 0 )
        {
            bm->client_version = atoi(value);
            return;
        }
        if( strcmp(key, "user") == 0 )
        {
            snprintf(bm->user, sizeof(bm->user), "%s", value);
            return;
        }
        if( strcmp(key, "pass") == 0 )
        {
            snprintf(bm->pass, sizeof(bm->pass), "%s", value);
            return;
        }
        if( strcmp(key, "scripts") == 0 )
        {
            bm_join_path(
                bm->server_scripts, sizeof(bm->server_scripts), manifest_dir, value);
            return;
        }
        if( strcmp(key, "cheat") == 0 )
        {
            snprintf(bm->cheat, sizeof(bm->cheat), "%s", value);
            return;
        }
        if( strcmp(key, "rsa_exp") == 0 )
        {
            snprintf(bm->rsa_exp, sizeof(bm->rsa_exp), "%s", value);
            return;
        }
        if( strcmp(key, "rsa_mod") == 0 )
        {
            snprintf(bm->rsa_mod, sizeof(bm->rsa_mod), "%s", value);
            return;
        }
        if( strcmp(key, "jag_crc") == 0 )
        {
            if( bm_parse_crc_list(value, bm->jag_crc) )
                bm->jag_crc_set = 1;
            else
                TORIRS_LOG("bootmanifest: [net] jag_crc needs exactly 9 int32s\n");
            return;
        }
        break;

    case BM_SECTION_IO:
        /*
         * The file server this client falls back to.
         *
         * Not the game server and not the cache server -- this one answers
         * GET /boot/<path> for anything the client would otherwise fopen and
         * not find: the plugin manifest, the plugin scripts it names, and each
         * shipped plugin asset AS A PLUGIN ASKS FOR IT. Nothing is fetched up
         * front, so a client that never opens a plugin never pays for one.
         *
         * Stating it is what makes a client usable away from the tree it was
         * built in. Without it a deployment has to carry script/ beside the
         * binary, and a deployment that forgets comes up holding only the
         * statically linked C plugins -- with no orbs on the minimap, no
         * gameframe tabs and nothing anywhere saying why, because a missing
         * plugin manifest is deliberately silent.
         *
         * TORIRS_IO_SERVER still wins; it is the older spelling and the one a
         * one-off debugging run reaches for.
         */
        if( strcmp(key, "host") == 0 )
        {
            snprintf(bm->io_host, sizeof(bm->io_host), "%s", value);
            return;
        }
        if( strcmp(key, "port") == 0 )
        {
            bm->io_port = atoi(value);
            return;
        }
        TORIRS_LOG("bootmanifest: [io] ignoring unknown key '%s'\n", key);
        return;

    case BM_SECTION_JS5:
        if( strcmp(key, "enabled") == 0 )
        {
            if( strcmp(value, "true") == 0 || strcmp(value, "1") == 0 )
                bm->js5_enabled = 1;
            else if( strcmp(value, "false") == 0 || strcmp(value, "0") == 0 )
                bm->js5_enabled = 0;
            else
                TORIRS_LOG("bootmanifest: [js5] enabled must be true|false, got '%s'\n",
                    value);
            return;
        }
        if( strcmp(key, "host") == 0 )
        {
            snprintf(bm->js5_host, sizeof(bm->js5_host), "%s", value);
            return;
        }
        if( strcmp(key, "port") == 0 )
        {
            bm_parse_bounded_int("js5", key, value, 1, 65535, &bm->js5_port);
            return;
        }
        if( strcmp(key, "fallback_port") == 0 )
        {
            if( bm_parse_bounded_int(
                    "js5", key, value, 0, 65535, &bm->js5_fallback_port) )
                bm->js5_fallback_port_set = 1;
            return;
        }
        if( strcmp(key, "revision") == 0 )
        {
            if( bm_parse_bounded_int(
                    "js5", key, value, 1, 2147483647, &bm->js5_revision) )
                bm->js5_revision_set = 1;
            return;
        }
        break;

    case BM_SECTION_UI:
        if( strcmp(key, "logic") == 0 )
        {
            if( strcmp(value, "cs1") == 0 )
                bm->ui_logic = APP_UI_LOGIC_CS1;
            else if( strcmp(value, "cs2") == 0 )
                bm->ui_logic = APP_UI_LOGIC_CS2;
            else
                TORIRS_LOG("bootmanifest: [ui] logic must be cs1|cs2, got '%s'\n", value);
            return;
        }
        if( strcmp(key, "chrome") == 0 )
        {
            if( strcmp(value, "revconfig") == 0 )
                bm->chrome = 1;
            else if( strcmp(value, "cache") == 0 )
                bm->chrome = 2;
            else
                TORIRS_LOG("bootmanifest: [ui] chrome must be revconfig|cache, got '%s'\n", value);
            return;
        }
        if( strcmp(key, "revconfig_ui") == 0 )
        {
            bm_join_path(bm->revconfig_ui, sizeof(bm->revconfig_ui), manifest_dir, value);
            return;
        }
        if( strcmp(key, "revconfig_cache") == 0 )
        {
            bm_join_path(bm->revconfig_cache, sizeof(bm->revconfig_cache), manifest_dir, value);
            return;
        }
        if( strcmp(key, "interface_id") == 0 )
        {
            bm->interface_id = atoi(value);
            return;
        }
        if( strcmp(key, "windowmode") == 0 )
        {
            bm->window_mode = CS2VM_WindowModeFromName(value);
            if( !bm->window_mode )
                TORIRS_LOG("bootmanifest: [ui] windowmode must be fixed|resizable, got '%s'\n",
                    value);
            return;
        }
        if( strcmp(key, "chrome_scale") == 0 )
        {
            int scale;
            /* `dynamic`: follow the CANVAS, not the display -- a fullscreen
             * canvas at 2x the classic frame gets 2x chrome, so the panels
             * keep their proportion of the screen instead of shrinking into a
             * corner of it. Carried as -1; the frame loop derives the value. */
            if( strcmp(value, "dynamic") == 0 )
            {
                bm->chrome_scale = -1;
                return;
            }
            scale = atoi(value);
            if( scale < 1 || scale > 4 )
            {
                TORIRS_LOG("bootmanifest: [ui] chrome_scale must be 1..4 or dynamic, got '%s'\n",
                    value);
                return;
            }
            bm->chrome_scale = scale;
            return;
        }
        if( strcmp(key, "chrome_checkbox") == 0 )
        {
            /* Which of the interfaces' two booleans the chrome's checkboxes
             * wear -- enum ToriRSChromeCheckStyle. Named rather than numbered
             * because the file is read by people: `box` is the bordered well,
             * `tick` the settings page's green tick and red cross. */
            if( strcmp(value, "tick") == 0 )
                bm->chrome_checkbox = 1;
            else if( strcmp(value, "box") == 0 )
                bm->chrome_checkbox = 2;
            else
                TORIRS_LOG("bootmanifest: [ui] chrome_checkbox must be tick|box, got '%s'\n",
                    value);
            return;
        }
        if( strcmp(key, "hidpi") == 0 )
        {
            /* Tri-state, so "the manifest said no" is distinguishable from
             * "the manifest did not say" -- the same shape chrome_scale uses,
             * and for the same reason: a default that a boot can only turn ON
             * is a default nobody can turn off from the file. */
            bm->hidpi = atoi(value) != 0 ? 1 : -1;
            return;
        }
        if( strcmp(key, "plugins") == 0 )
        {
            /* Tri-state, like hidpi above and for the same reason. */
            bm->plugins = atoi(value) != 0 ? 1 : -1;
            return;
        }
        if( strcmp(key, "clienttype") == 0 )
        {
            bm->clienttype = atoi(value);
            return;
        }
        if( strcmp(key, "on_mobile") == 0 )
        {
            /* Tri-state, like plugins above and for the same reason. */
            bm->on_mobile = atoi(value) != 0 ? 1 : -1;
            return;
        }
        if( strcmp(key, "window") == 0 )
        {
            char* sep = NULL;
            long w = strtol(value, &sep, 10);
            long h = (sep && (*sep == 'x' || *sep == 'X' || *sep == ',')) ? strtol(sep + 1, NULL, 10) : 0;
            if( w > 0 && h > 0 )
            {
                bm->window_w = (int)w;
                bm->window_h = (int)h;
            }
            else
            {
                TORIRS_LOG("bootmanifest: [ui] window must be WxH, got '%s'\n", value);
            }
            return;
        }
        break;

    case BM_SECTION_UI_GAMEFRAME:
    {
        /* Free-form: every key is a component index on the root interface. Order
         * is preserved because mounting order is the server's, and a later mount
         * can depend on an earlier one being in place.
         *
         * Both sides are bare numbers, so this is the one section that tolerates
         * surrounding spaces and a trailing `;`/`#` comment — two dozen numeric
         * pairs are unreadable without a name beside each one. */
        int component;
        int iface;
        int parent = 0;
        char const* component_text = strchr(key, ':');
        if( component_text )
        {
            if( !bm_parse_padded_int(key, key, &parent) )
                return;
            component_text++;
        }
        else
            component_text = key;
        if( !bm_parse_padded_int(key, component_text, &component) )
            return;
        if( !bm_parse_padded_int(key, value, &iface) )
            return;
        if( bm->gameframe_count >= BOOTMANIFEST_GAMEFRAME_MAX )
        {
            TORIRS_LOG("bootmanifest: [ui:gameframe] holds at most %d mounts; dropping %s=%s\n",
                BOOTMANIFEST_GAMEFRAME_MAX, key, value);
            return;
        }
        bm->gameframe[bm->gameframe_count].parent_interface_id = parent;
        bm->gameframe[bm->gameframe_count].component = component;
        bm->gameframe[bm->gameframe_count].interface_id = iface;
        bm->gameframe_count++;
        return;
    }

    case BM_SECTION_UI_VARC:
    {
        /* Same free-form shape as [ui:gameframe]: every key is a varc id. */
        int varc_id;
        int varc_value;
        if( !bm_parse_padded_int(key, key, &varc_id) )
            return;
        if( !bm_parse_padded_int(key, value, &varc_value) )
            return;
        if( bm->varc_count >= BOOTMANIFEST_GAMEFRAME_MAX )
        {
            TORIRS_LOG("bootmanifest: [ui:varc] holds at most %d seeds; dropping %s=%s\n",
                BOOTMANIFEST_GAMEFRAME_MAX, key, value);
            return;
        }
        bm->varc[bm->varc_count].id = varc_id;
        bm->varc[bm->varc_count].value = varc_value;
        bm->varc_count++;
        return;
    }

    case BM_SECTION_ACTION:
    {
        static char const* const target_names[APP_DEBUG_HOTKEY_COUNT] = {
            "camera_forward",   "camera_back",    "camera_left",     "camera_right",
            "camera_up",        "camera_down",    "camera_unlock",   "world_reload",
            "paint_toggle",     "paint_more",     "paint_less",      "paint_more_100",
            "paint_less_100",   "spawn_player",   "spawn_npc",       "spawn_obj",
            "spawn_projectile", "spawn_spotanim", "entity_spotanim", "damage_test",
            "debug_overlay",    "loc_editor_toggle", "hover_footprint",
            "map_editor_toggle", "plugin_panel_toggle"
        };
        char const* name = section_name + 7;
        struct BootManifestDebugAction* action = NULL;

        if( !name[0] )
        {
            TORIRS_LOG("bootmanifest: [action:<name>] requires a name\n");
            bm->debug_hotkey_error = 1;
            return;
        }
        for( int i = 0; i < bm->debug_action_count; i++ )
            if( strcmp(bm->debug_actions[i].name, name) == 0 )
            {
                action = &bm->debug_actions[i];
                break;
            }
        if( !action )
        {
            if( bm->debug_action_count >= BOOTMANIFEST_DEBUG_ACTION_MAX )
            {
                TORIRS_LOG("bootmanifest: at most %d debug actions\n",
                        BOOTMANIFEST_DEBUG_ACTION_MAX);
                bm->debug_hotkey_error = 1;
                return;
            }
            action = &bm->debug_actions[bm->debug_action_count++];
            snprintf(action->name, sizeof(action->name), "%s", name);
            action->target = -1;
        }
        if( strcmp(key, "t") == 0 )
        {
            for( int i = 0; i < APP_DEBUG_HOTKEY_COUNT; i++ )
                if( strcmp(value, target_names[i]) == 0 )
                {
                    action->target = i;
                    return;
                }
            TORIRS_ERR("bootmanifest: [action:%s] has unknown target '%s'\n", name, value);
            bm->debug_hotkey_error = 1;
            return;
        }
        if( strcmp(key, "a") == 0 )
        {
            snprintf(action->args, sizeof(action->args), "%s", value);
            return;
        }
        break;
    }

    case BM_SECTION_DEBUG_HOTKEYS:
    {
        enum LibToriRS_KeyCode parsed = TORIRSK_UNKNOWN;
        static struct
        {
            char const* name;
            enum LibToriRS_KeyCode key;
        } const named[] = {
            { "escape",    TORIRSK_ESCAPE    }, { "enter",   TORIRSK_RETURN    },
            { "return",    TORIRSK_RETURN    }, { "backspace", TORIRSK_BACKSPACE },
            { "insert",    TORIRSK_INSERT    }, { "delete",  TORIRSK_DELETE    },
            { "shift",     TORIRSK_SHIFT     }, { "ctrl",    TORIRSK_CTRL      },
            { "tab",       TORIRSK_TAB       }, { "space",   TORIRSK_SPACE     },
            { "left",      TORIRSK_LEFT      }, { "right",   TORIRSK_RIGHT     },
            { "up",        TORIRSK_UP        }, { "down",    TORIRSK_DOWN      },
            { "page_up",   TORIRSK_PAGE_UP   }, { "page_down", TORIRSK_PAGE_DOWN },
            { "comma",     TORIRSK_COMMA     },
        };

        if( key[0] && !key[1] && key[0] >= 'a' && key[0] <= 'z' )
            parsed = (enum LibToriRS_KeyCode)(TORIRSK_A + key[0] - 'a');
        else if( key[0] && !key[1] && key[0] >= '0' && key[0] <= '9' )
            parsed = (enum LibToriRS_KeyCode)(TORIRSK_0 + key[0] - '0');
        else
            for( size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++ )
                if( strcmp(key, named[i].name) == 0 )
                {
                    parsed = named[i].key;
                    break;
                }
        if( parsed == TORIRSK_UNKNOWN )
        {
            TORIRS_ERR("bootmanifest: [debug:hotkeys] has unknown key '%s'\n", key);
            bm->debug_hotkey_error = 1;
            return;
        }
        if( bm->debug_hotkey_count >= BOOTMANIFEST_DEBUG_HOTKEY_MAX )
        {
            TORIRS_LOG("bootmanifest: at most %d debug hotkeys\n",
                    BOOTMANIFEST_DEBUG_HOTKEY_MAX);
            bm->debug_hotkey_error = 1;
            return;
        }
        bm->debug_hotkeys[bm->debug_hotkey_count].key = parsed;
        snprintf(bm->debug_hotkeys[bm->debug_hotkey_count].action,
                 sizeof(bm->debug_hotkeys[bm->debug_hotkey_count].action), "%s", value);
        bm->debug_hotkey_count++;
        return;
    }

    case BM_SECTION_FEATURES:
        if( strcmp(key, "era") == 0 )
        {
            /* Validated here rather than at App_Init so a typo names itself at
             * load time, next to the file it came from. */
            if( !ToriRS_Features_ByName(value) )
            {
                TORIRS_LOG("bootmanifest: [features] era must be lostcity|osrs|server_routed, got '%s'\n",
                    value);
                return;
            }
            snprintf(bm->features_era, sizeof(bm->features_era), "%s", value);
            return;
        }
        if( strcmp(key, "ground_click_nearest") == 0 )
        {
            int model = ToriRS_Features_NearestModelByName(value);
            if( model < 0 )
            {
                TORIRS_LOG("bootmanifest: [features] ground_click_nearest must be "
                        "ring3|box10_rect|none, got '%s'\n",
                        value);
                return;
            }
            bm->features_ground_click_nearest = model;
            return;
        }
        /* The two permissive extensions. Off in every era table by design, so
         * a boot that wants one has to say so here. */
        if( strcmp(key, "ground_click_unbounded") == 0 )
        {
            int on;
            if( bm_parse_int(key, value, &on) )
                bm->features_ground_click_unbounded = on ? 1 : 0;
            return;
        }
        if( strcmp(key, "ground_click_offmap") == 0 )
        {
            int on;
            if( bm_parse_int(key, value, &on) )
                bm->features_ground_click_offmap = on ? 1 : 0;
            return;
        }
        if( strcmp(key, "mover") == 0 )
        {
            int model = ToriRS_Features_MoverModelByName(value);
            if( model < 0 )
            {
                TORIRS_LOG("bootmanifest: [features] mover must be cycle|frame, got '%s'\n",
                        value);
                return;
            }
            bm->features_mover_model = model;
            return;
        }
        if( strcmp(key, "painter_draw_distance") == 0 )
        {
            int distance;
            if( !bm_parse_int(key, value, &distance) )
                return;
            if( distance < TORIRS_PAINTER_DRAW_DISTANCE_MIN ||
                distance > TORIRS_PAINTER_DRAW_DISTANCE_MAX )
            {
                TORIRS_LOG("bootmanifest: [features] painter_draw_distance must be "
                        "%d..%d, got '%s'\n",
                        TORIRS_PAINTER_DRAW_DISTANCE_MIN,
                        TORIRS_PAINTER_DRAW_DISTANCE_MAX,
                        value);
                return;
            }
            bm->features_painter_draw_distance = distance;
            return;
        }
        break;

    case BM_SECTION_RENDER:
        if( strcmp(key, "actor_ambient") == 0 )
        {
            if( bm_parse_int(key, value, &bm->actor_ambient) )
                bm->actor_ambient_set = 1;
            return;
        }
        if( strcmp(key, "actor_attenuation") == 0 )
        {
            if( bm_parse_int(key, value, &bm->actor_attenuation) )
                bm->actor_attenuation_set = 1;
            return;
        }
        if( strcmp(key, "actor_light") == 0 )
        {
            if( bm_parse_xyz(
                    key, value, &bm->actor_light_x, &bm->actor_light_y, &bm->actor_light_z) )
                bm->actor_light_set = 1;
            return;
        }
        if( strcmp(key, "scene_ambient") == 0 )
        {
            if( bm_parse_int(key, value, &bm->scene_ambient) )
                bm->scene_ambient_set = 1;
            return;
        }
        if( strcmp(key, "scene_attenuation") == 0 )
        {
            if( bm_parse_int(key, value, &bm->scene_attenuation) )
                bm->scene_attenuation_set = 1;
            return;
        }
        if( strcmp(key, "scene_light") == 0 )
        {
            if( bm_parse_xyz(
                    key, value, &bm->scene_light_x, &bm->scene_light_y, &bm->scene_light_z) )
                bm->scene_light_set = 1;
            return;
        }
        if( strcmp(key, "npc_type_ambient_contrast") == 0 )
        {
            int v;
            if( bm_parse_int(key, value, &v) )
            {
                if( v != 0 && v != 1 )
                {
                    TORIRS_LOG("bootmanifest: npc_type_ambient_contrast must be 0|1, got '%s'\n",
                        value);
                    return;
                }
                bm->npc_type_ambient_contrast = v;
                bm->npc_type_ambient_contrast_set = 1;
            }
            return;
        }
        if( strcmp(key, "player_head_ambient") == 0 )
        {
            if( bm_parse_int(key, value, &bm->player_head_ambient) )
                bm->player_head_ambient_set = 1;
            return;
        }
        break;

    case BM_SECTION_REVCONFIG:
    case BM_SECTION_NONE:
        return;
    }

    TORIRS_ERR("bootmanifest: ignoring unknown key '%s'\n", key);
}

/* Split dirname of `path` into dir (without trailing slash). "" for a bare
 * filename with no directory component. */
static void
bm_dirname(char* dir, size_t cap, char const* path)
{
    /*
     * The LAST separator of either kind, not the last '/'.
     *
     * A path that mixes them is normal on Windows and this tree produces both:
     * launch.cmd passes `build\\manifests\\x.ini` and the benchmark harness
     * passes `build/manifests/x.ini`. Searching for '/' alone returned no
     * directory at all for the first, and every manifest-relative value --
     * the revconfig layout among them -- then resolved against the working
     * directory and was not there.
     */
    char const* slash = NULL;
    for( char const* p = path; *p; p++ )
        if( bm_is_sep(*p) )
            slash = p;

    if( !slash )
    {
        dir[0] = '\0';
        return;
    }
    size_t len = (size_t)(slash - path);
    if( len >= cap )
        len = cap - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';
}

int
BootManifest_LoadFile(struct BootManifest* bm, char const* path)
{
    memset(bm, 0, sizeof(*bm));
    bm->cache_kind = -1;
    bm->cache_game = RSCACHE_GAME_UNSET;
    bm->cache_epoch = RSCACHE_EPOCH_UNSET;
    bm->cache_revision = -1;
    bm->js5_enabled = -1;
    /* -1 = "not stated": 0 is TORIRS_NEAREST_RING3_STEPS, a real model, so a
     * zeroed struct must not read as an override to it. */
    bm->features_ground_click_nearest = -1;
    bm->features_mover_model = -1;
    bm->features_ground_click_unbounded = -1;
    bm->features_ground_click_offmap = -1;
    bm->spawn_x = -1;
    bm->spawn_z = -1;

    FILE* f = fopen(path, "rb");
    if( !f )
    {
        TORIRS_ERR("bootmanifest: cannot open '%s'\n", path);
        return -1;
    }

    long file_size = 0;
    if( fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return -1;
    }

    char* data = malloc((size_t)file_size + 1);
    assert(data);
    if( fread(data, 1, (size_t)file_size, f) != (size_t)file_size )
    {
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);
    data[file_size] = '\0';

    char manifest_dir[512];
    bm_dirname(manifest_dir, sizeof(manifest_dir), path);

    struct INIReader reader = { 0 };
    ini_reader_init(&reader);

    enum bm_section section = BM_SECTION_NONE;
    char section_name[BOOTMANIFEST_DEBUG_NAME_CAP + 8] = { 0 };
    struct INIElement element = { 0 };
    int parse_result = TORI_INI_ERR_OK;
    while( (parse_result = ini_reader_next(
                &reader, (uint8_t*)data, (uint32_t)file_size, &element)) == TORI_INI_ERR_OK )
    {
        switch( element.kind )
        {
        case INI_ELEMENT_SECTION:
            snprintf(section_name, sizeof(section_name), "%s", element._section.name);
            section = bm_section_of(element._section.name);
            if( section == BM_SECTION_REVCONFIG )
                snprintf(bm->revconfig_inline, sizeof(bm->revconfig_inline), "%s", path);
            else if( section == BM_SECTION_NONE
                     && strncmp(element._section.name, "derived:", 8) != 0 )
                /* `[derived:*]` is addressed to the LAUNCHER, not to this
                 * loader: it states what the world's cache and script pack are
                 * built from, so a stale one is rebuilt before the client
                 * starts. The client neither reads it nor needs to, but it is
                 * a known section rather than a typo — warning about it would
                 * print two lines of noise on every boot of every migrated
                 * manifest, which is how a warning stops being read at all. */
                TORIRS_ERR("bootmanifest: ignoring unknown section '[%s]'\n",
                    element._section.name);
            break;
        case INI_ELEMENT_KEYVAL:
            bm_set_kv(
                bm,
                manifest_dir,
                section,
                section_name,
                element._keyval.name,
                element._keyval.value);
            break;
        case INI_ELEMENT_SECTION_END:
        case INI_ELEMENT_UNDEFINED:
            break;
        }
    }

    free(data);

    if( parse_result != TORI_INI_ERR_NONE || reader.state != INI_READER_STATE_DONE )
    {
        TORIRS_ERR("bootmanifest: parse of '%s' failed (result=%d state=%d offset=%u)\n",
            path,
            parse_result,
            (int)reader.state,
            reader.offset);
        return -1;
    }

    /*
     * Where a streamed cache is written down, when the manifest does not say.
     *
     * An ondemand world reads its cache off the server, and `dir=` is the
     * directory it HYDRATES into -- not a cache to read instead of the server.
     * The client still asks the server for its nine checksums every boot and
     * wipes the directory if they disagree, so a stale copy can never be
     * served; what the directory buys is not re-streaming what has not
     * changed. Absent, every launch re-streamed the world -- measured at 494
     * containers and 721,091 bytes for a boot to the design screen, none of it
     * asked for twice within a session and all of it asked for again on the
     * next one.
     *
     * ## The shape is the 239 client's, under our name
     *
     *     ~/jagexcache/oldschool/LIVE/     the reference client
     *     ~/torirs_cache/rs2/rs289lc-xp/   this one
     *
     * Same three decisions, and worth taking the same way rather than
     * inventing a layout: the cache belongs to the USER (so it survives a
     * rebuild, a branch switch, a deleted worktree, and a redeployed client
     * directory -- none of which a path beside the binary survives); it is
     * segmented by which game's cache format it holds; and then by which world
     * it came from.
     *
     * Both segments are load-bearing. The per-world one is what stops two
     * worlds evicting each other: the checksum stamp guards CORRECTNESS, so a
     * shared directory would still never serve the wrong bytes -- it would
     * wipe itself on every alternating boot, which is the re-streaming this
     * default exists to stop. The per-game one keeps a dat1 cache and a dat2
     * cache from meeting, which the stamp does not cover because the two carry
     * different file layouts, not different contents.
     *
     * <home> is resolved as the reference client resolves it; see
     * bm_user_home. On the web there is no home and no filesystem, so the
     * default is an IndexedDB database named on the same axes.
     *
     * A manifest that states `dir=` wins, and so does one that states it
     * empty. Only silence is defaulted.
     */
    if( bm->cache_on_demand && !bm->cache_dir[0] && !bm->cache_dir_stated )
    {
        char stem[128];
        char const* game = RSCache_GameName(bm->cache_game);

        bm_basename_stem(stem, sizeof(stem), path);
        if( !stem[0] )
            snprintf(stem, sizeof(stem), "world");
        /* RSCache_GameName answers NULL for an unset game. The manifest is
         * required to state one, so this is belt and braces rather than a case
         * that reaches a user -- but a NULL here would format as "(null)" and
         * make a directory by that name. */
        if( !game || !game[0] )
            game = "unknown";

#if defined(TORIRS_PLATFORM_WEB)
        /* No filesystem, no home. The location is a database name, and it is
         * segmented on the same axes a native path is -- a browser holding two
         * worlds keeps two databases, for the same reason two worlds get two
         * directories. */
        snprintf(bm->cache_dir, sizeof(bm->cache_dir), "idb:torirs_cache/%s/%s",
            game, stem);
#else
        {
            char home[512];
            if( bm_user_home(home, sizeof(home)) )
                snprintf(bm->cache_dir, sizeof(bm->cache_dir),
                    "%s/torirs_cache/%s/%s", home, game, stem);
            else
                /* Nothing named a home. The client streams from the server
                 * exactly as it did before this default existed: slower, and
                 * correct. Inventing a path would put the user's cache
                 * somewhere they did not choose and cannot find. */
                TORIRS_LOG("bootmanifest: no home directory (TORIRS_USERHOME, "
                    "USERPROFILE, HOME all unset); the streamed cache will not "
                    "be written down\n");
        }
#endif
    }

    for( int i = 0; i < bm->debug_action_count; i++ )
        if( bm->debug_actions[i].target < 0 )
        {
            TORIRS_ERR("bootmanifest: [action:%s] is missing required t=\n",
                    bm->debug_actions[i].name);
            bm->debug_hotkey_error = 1;
        }
    for( int i = 0; i < bm->debug_hotkey_count; i++ )
    {
        int found = 0;
        for( int j = 0; j < bm->debug_action_count; j++ )
            if( strcmp(bm->debug_hotkeys[i].action, bm->debug_actions[j].name) == 0 )
            {
                found = 1;
                break;
            }
        if( !found )
        {
            TORIRS_ERR("bootmanifest: [debug:hotkeys] references unknown action '%s'\n",
                    bm->debug_hotkeys[i].action);
            bm->debug_hotkey_error = 1;
        }
    }
    if( bm->debug_hotkey_error )
        return -1;
    if( bm->editor_panel_error )
        return -1;
    if( bm->editor_server_error )
        return -1;

    /* server=embed serves `content_dir`, so leaving it out means there is no
     * tree to serve — a manifest mistake to report at load, not a blank
     * square browser to puzzle over. (server=tcp is different: the daemon
     * owns the tree, so content_dir is optional there.) */
    if( bm->editor_server == BOOTMANIFEST_EDITOR_SERVER_EMBED
        && (bm->editor_server_host[0] || bm->editor_server_port > 0
            || bm->editor_client_id > 0) )
    {
        TORIRS_LOG("bootmanifest: '%s' sets [editor:boot] host/port/client without "
            "server=tcp; the embedded ToriRSMapEd has no address and no other "
            "process to share a Client with\n",
            path);
        return -1;
    }

    /* A tab panel needs a browser to open the tab in. Caught here rather than
     * at the open call, so a native run of a web manifest fails at load with
     * the reason, instead of booting into an editor whose panel never appears
     * and looks merely broken. */
#if !defined(__EMSCRIPTEN__)
    if( bm->editor_panel == BOOTMANIFEST_EDITOR_PANEL_TAB )
    {
        TORIRS_LOG("bootmanifest: '%s' asks for [editor:boot] panel=tab, which only a web "
            "build can open. Use panel=inprocess for a native boot.\n",
            path);
        return -1;
    }
#endif

    /* All four identity keys are required. Missing one is user input, not an
     * assert — report and fail the load. */
    if( bm->cache_epoch == RSCACHE_EPOCH_UNSET )
    {
        TORIRS_ERR("bootmanifest: '%s' missing required [cache:boot] epoch=\n", path);
        return -1;
    }
    if( bm->cache_game == RSCACHE_GAME_UNSET )
    {
        TORIRS_ERR("bootmanifest: '%s' missing required [cache:boot] game=\n", path);
        return -1;
    }
    if( bm->cache_revision < 0 )
    {
        TORIRS_ERR("bootmanifest: '%s' missing required [cache:boot] revision=\n", path);
        return -1;
    }
    if( !bm->cache_quirks_set )
    {
        TORIRS_ERR("bootmanifest: '%s' missing required [cache:boot] quirks=\n", path);
        return -1;
    }

    if( bm->client_args_error )
    {
        TORIRS_ERR("bootmanifest: invalid [client:args] in '%s'\n", path);
        return -1;
    }

    return 0;
}

void
BootManifest_ApplyToConfig(struct BootManifest const* bm, struct AppConfig* cfg)
{
    if( bm->cache_kind >= 0 )
        cfg->cache_kind = (enum AppCacheKind)bm->cache_kind;
    if( bm->cache_dir[0] )
        cfg->cache_dir = bm->cache_dir;
    if( bm->io_host[0] )
        snprintf(cfg->io_host, sizeof(cfg->io_host), "%s", bm->io_host);
    if( bm->io_port > 0 )
        cfg->io_port = bm->io_port;
    cfg->cache_on_demand = bm->cache_on_demand;
    /* ws_port is the server's HTTP endpoint, which for LostCity also serves
     * the jag archives and /crc. The on-demand cache source is the only native
     * reader of it; a web boot still reaches it through ApplyWebEndpoint. */
    if( bm->ws_port > 0 )
        cfg->web_port = bm->ws_port;
    if( bm->editor_content_dir[0] )
        cfg->editor_content_dir = bm->editor_content_dir;
    if( bm->editor_repo_root[0] )
        cfg->editor_repo_root = bm->editor_repo_root;
    cfg->editor_panel = (int)bm->editor_panel;
    cfg->editor_server = (int)bm->editor_server;
    if( bm->editor_server_host[0] )
        cfg->editor_server_host = bm->editor_server_host;
    if( bm->editor_server_port > 0 )
        cfg->editor_server_port = bm->editor_server_port;
    if( bm->editor_client_id > 0 )
        cfg->editor_client_id = bm->editor_client_id;

    if( bm->cache_quirks_set && bm->cache_game != 0 && bm->cache_epoch != 0 &&
        bm->cache_revision >= 0 )
    {
        cfg->cache_game = bm->cache_game;
        cfg->cache_epoch = bm->cache_epoch;
        cfg->cache_revision = bm->cache_revision;
        cfg->cache_quirks = bm->cache_quirks;
        cfg->cache_identity_set = 1;
    }

    if( bm->rev_name[0] )
        cfg->rev_name = bm->rev_name;
    if( bm->host[0] )
        cfg->connect_target = bm->host;
    if( bm->port > 0 )
        cfg->connect_port = bm->port;
    if( bm->client_version > 0 )
        cfg->client_version = bm->client_version;
    if( bm->rsa_exp[0] )
        cfg->rsa_exp = bm->rsa_exp;
    if( bm->rsa_mod[0] )
        cfg->rsa_mod = bm->rsa_mod;
    if( bm->jag_crc_set )
    {
        memcpy(cfg->jag_crc, bm->jag_crc, sizeof(cfg->jag_crc));
        cfg->jag_crc_set = 1;
    }
    if( bm->user[0] )
        cfg->connect_user = bm->user;
    if( bm->pass[0] )
        cfg->connect_pass = bm->pass;
    if( bm->server_scripts[0] )
        cfg->net_server_scripts = bm->server_scripts;
    if( bm->cheat[0] )
        cfg->net_cheat = bm->cheat;

    if( bm->features_era[0] )
        cfg->features_era = bm->features_era;
    if( bm->features_ground_click_nearest >= 0 )
    {
        cfg->features_ground_click_nearest = bm->features_ground_click_nearest;
        cfg->features_ground_click_nearest_set = 1;
    }
    if( bm->features_ground_click_unbounded >= 0 )
        cfg->features_ground_click_unbounded = bm->features_ground_click_unbounded;
    if( bm->features_ground_click_offmap >= 0 )
        cfg->features_ground_click_offmap = bm->features_ground_click_offmap;
    if( bm->features_mover_model >= 0 )
    {
        cfg->features_mover_model = bm->features_mover_model;
        cfg->features_mover_model_set = 1;
    }
    if( bm->features_painter_draw_distance > 0 )
    {
        cfg->features_painter_draw_distance = bm->features_painter_draw_distance;
        cfg->features_painter_draw_distance_set = 1;
    }

    if( bm->actor_ambient_set )
    {
        cfg->light_actor_ambient = bm->actor_ambient;
        cfg->light_actor_ambient_set = 1;
    }
    if( bm->actor_attenuation_set )
    {
        cfg->light_actor_attenuation = bm->actor_attenuation;
        cfg->light_actor_attenuation_set = 1;
    }
    if( bm->actor_light_set )
    {
        cfg->light_actor_x = bm->actor_light_x;
        cfg->light_actor_y = bm->actor_light_y;
        cfg->light_actor_z = bm->actor_light_z;
        cfg->light_actor_set = 1;
    }
    if( bm->scene_ambient_set )
    {
        cfg->light_scene_ambient = bm->scene_ambient;
        cfg->light_scene_ambient_set = 1;
    }
    if( bm->scene_attenuation_set )
    {
        cfg->light_scene_attenuation = bm->scene_attenuation;
        cfg->light_scene_attenuation_set = 1;
    }
    if( bm->scene_light_set )
    {
        cfg->light_scene_x = bm->scene_light_x;
        cfg->light_scene_y = bm->scene_light_y;
        cfg->light_scene_z = bm->scene_light_z;
        cfg->light_scene_set = 1;
    }
    if( bm->npc_type_ambient_contrast_set )
    {
        cfg->light_npc_type_ambient_contrast = bm->npc_type_ambient_contrast;
        cfg->light_npc_type_ambient_contrast_set = 1;
    }
    if( bm->player_head_ambient_set )
    {
        cfg->light_player_head_ambient = bm->player_head_ambient;
        cfg->light_player_head_ambient_set = 1;
    }

    if( bm->ui_logic )
        cfg->ui_logic = bm->ui_logic;
    if( bm->revconfig_ui[0] )
        cfg->revconfig_ui_ini = bm->revconfig_ui;
    if( bm->revconfig_cache[0] )
        cfg->revconfig_cache_ini = bm->revconfig_cache;
    if( bm->revconfig_inline[0] )
        cfg->revconfig_inline_ini = bm->revconfig_inline;
    if( bm->interface_id > 0 )
        cfg->interface_id = bm->interface_id;
    if( bm->window_mode )
        cfg->window_mode = bm->window_mode;
    if( bm->chrome_scale )
        cfg->chrome_scale = bm->chrome_scale;
    /* Stored one past the enum so "the manifest said tick" is distinguishable
     * from "the manifest said nothing" -- the same tri-state shape hidpi uses,
     * and for the same reason: a key that can only turn the non-default ON is
     * a key nobody can use to turn it off again. */
    if( bm->chrome_checkbox )
        cfg->chrome_checkbox = bm->chrome_checkbox - 1;
    if( bm->hidpi )
        cfg->hidpi = bm->hidpi;
    if( bm->plugins )
        cfg->plugins = bm->plugins;
    if( bm->clienttype > 0 )
        cfg->clienttype = bm->clienttype;
    if( bm->on_mobile )
        cfg->on_mobile = bm->on_mobile;
    if( bm->window_w > 0 && bm->window_h > 0 )
    {
        cfg->window_w = bm->window_w;
        cfg->window_h = bm->window_h;
    }
    if( bm->gameframe_count > 0 )
    {
        cfg->gameframe_mounts = bm->gameframe;
        cfg->gameframe_mount_count = bm->gameframe_count;
    }
    if( bm->varc_count > 0 )
    {
        cfg->varc_seeds = bm->varc;
        cfg->varc_seed_count = bm->varc_count;
    }
    if( bm->spawn_x >= 0 && bm->spawn_z >= 0 )
    {
        cfg->spawn_x = bm->spawn_x;
        cfg->spawn_z = bm->spawn_z;
    }
    if( bm->debug_hotkey_count > 0 )
    {
        cfg->debug_hotkey_count = 0;
        for( int i = 0; i < bm->debug_hotkey_count; i++ )
            for( int j = 0; j < bm->debug_action_count; j++ )
                if( strcmp(bm->debug_hotkeys[i].action, bm->debug_actions[j].name) == 0 )
                {
                    struct AppDebugHotkeyBinding* binding =
                        &cfg->debug_hotkeys[cfg->debug_hotkey_count++];
                    binding->key = (enum LibToriRS_KeyCode)bm->debug_hotkeys[i].key;
                    binding->target = (enum AppDebugHotkey)bm->debug_actions[j].target;
                    snprintf(binding->args, sizeof(binding->args), "%s",
                             bm->debug_actions[j].args);
                    break;
                }
    }
}

void
BootManifest_ApplyToExecutorConfig(
    struct BootManifest const* bm,
    struct ToriRS_ExecutorConfig* cfg)
{
    assert(bm);
    assert(cfg);

    if( bm->js5_enabled >= 0 )
        cfg->js5_enabled = bm->js5_enabled;
    if( bm->js5_host[0] )
        snprintf(cfg->js5_host, sizeof(cfg->js5_host), "%s", bm->js5_host);
    if( bm->js5_port > 0 )
        cfg->js5_port = bm->js5_port;
    if( bm->js5_fallback_port_set )
    {
        cfg->js5_fallback_port = bm->js5_fallback_port;
        cfg->js5_fallback_port_set = 1;
    }
    if( bm->js5_revision_set )
    {
        cfg->js5_revision = bm->js5_revision;
        cfg->js5_revision_explicit = 1;
    }
}

void
BootManifest_ApplyWebEndpoint(
    struct BootManifest const* bm,
    struct AppConfig* cfg)
{
    char const* env_host = getenv("TORIRS_WS_HOST");
    char const* env_port = getenv("TORIRS_WS_PORT");

    assert(bm);
    assert(cfg);

    if( env_host && env_host[0] )
        cfg->connect_target = env_host;
    else if( bm->ws_host[0] )
        cfg->connect_target = bm->ws_host;

    if( env_port && env_port[0] )
        cfg->connect_port = atoi(env_port);
    else if( bm->ws_port > 0 )
        cfg->connect_port = bm->ws_port;
}
