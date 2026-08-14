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

_Static_assert(
    BOOTMANIFEST_DEBUG_HOTKEY_MAX <= APP_DEBUG_HOTKEY_MAX,
    "AppConfig must hold every manifest debug-hotkey binding");

/* Join a manifest-relative value onto the manifest's directory. Absolute
 * values (leading '/') and empty base copy through unchanged. */
static void
bm_join_path(char* dst, size_t cap, char const* manifest_dir, char const* value)
{
    if( value[0] == '/' || manifest_dir[0] == '\0' )
    {
        snprintf(dst, cap, "%s", value);
        return;
    }
    snprintf(dst, cap, "%s/%s", manifest_dir, value);
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
        fprintf(stderr, "bootmanifest: '%s' must be a non-negative int, got '%s'\n", key, value);
        return 0;
    }
    while( *end == ' ' || *end == '\t' )
        end++;
    /* ':' ends the parent id in a qualified [ui:gameframe] key; ';'/'#' start a
     * trailing comment. */
    if( *end != '\0' && *end != ';' && *end != '#' && *end != ':' )
    {
        fprintf(stderr, "bootmanifest: trailing junk after '%s': '%s'\n", key, end);
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
        fprintf(stderr, "bootmanifest: '%s' must be an int, got '%s'\n", key, value);
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
        fprintf(
            stderr,
            "bootmanifest: [%s] %s must be in %d..%d, got '%s'\n",
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
    fprintf(stderr, "bootmanifest: '%s' must be x,y,z ints, got '%s'\n", key, value);
    return 0;
}

enum bm_section
{
    BM_SECTION_NONE = 0,
    BM_SECTION_CLIENT_ARGS,
    BM_SECTION_CACHE,
    BM_SECTION_NET,
    BM_SECTION_JS5,
    BM_SECTION_UI,
    BM_SECTION_UI_GAMEFRAME,
    BM_SECTION_UI_VARC,
    BM_SECTION_ACTION,
    BM_SECTION_DEBUG_HOTKEYS,
    BM_SECTION_FEATURES,
    BM_SECTION_RENDER,
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
                fprintf(
                    stderr,
                    "bootmanifest: [client:args] holds at most %d arguments\n",
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

    case BM_SECTION_CACHE:
        if( strcmp(key, "epoch") == 0 )
        {
            int epoch = RSCache_EpochFromName(value);
            if( epoch == RSCACHE_EPOCH_UNSET )
            {
                fprintf(stderr, "bootmanifest: [cache] epoch must be dat1|dat2, got '%s'\n", value);
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
                fprintf(
                    stderr, "bootmanifest: [cache] game must be rs2|oldschool, got '%s'\n", value);
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
                fprintf(
                    stderr, "bootmanifest: [cache] revision must be a positive int, got '%s'\n",
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
                fprintf(
                    stderr,
                    "bootmanifest: [cache] quirks must be none|kronos|void_rs634_no_xteas, got '%s'\n",
                    value);
                return;
            }
            bm->cache_quirks = quirks;
            bm->cache_quirks_set = 1;
            return;
        }
        if( strcmp(key, "dir") == 0 )
        {
            bm_join_path(bm->cache_dir, sizeof(bm->cache_dir), manifest_dir, value);
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
                fprintf(stderr, "bootmanifest: [cache] spawn must be \"x,z\", got '%s'\n", value);
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
                fprintf(stderr, "bootmanifest: [net] jag_crc needs exactly 9 int32s\n");
            return;
        }
        break;

    case BM_SECTION_JS5:
        if( strcmp(key, "enabled") == 0 )
        {
            if( strcmp(value, "true") == 0 || strcmp(value, "1") == 0 )
                bm->js5_enabled = 1;
            else if( strcmp(value, "false") == 0 || strcmp(value, "0") == 0 )
                bm->js5_enabled = 0;
            else
                fprintf(
                    stderr,
                    "bootmanifest: [js5] enabled must be true|false, got '%s'\n",
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
                fprintf(stderr, "bootmanifest: [ui] logic must be cs1|cs2, got '%s'\n", value);
            return;
        }
        if( strcmp(key, "chrome") == 0 )
        {
            if( strcmp(value, "revconfig") == 0 )
                bm->chrome = 1;
            else if( strcmp(value, "cache") == 0 )
                bm->chrome = 2;
            else
                fprintf(
                    stderr, "bootmanifest: [ui] chrome must be revconfig|cache, got '%s'\n", value);
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
                fprintf(
                    stderr,
                    "bootmanifest: [ui] windowmode must be fixed|resizable, got '%s'\n",
                    value);
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
                fprintf(stderr, "bootmanifest: [ui] window must be WxH, got '%s'\n", value);
            }
            return;
        }
        /* [ui:chatbox] — see BootManifest.chatbox_* for why these are declared
         * rather than discovered. They share BM_SECTION_UI because every
         * `[ui:…]` header that is not gameframe or varc lands there; the key
         * names are distinct, so the sections stay readable in the file without
         * needing a parser branch each. */
        if( strcmp(key, "chatbox_interface") == 0 )
        {
            bm->chatbox_interface = atoi(value);
            return;
        }
        if( strcmp(key, "chatbox_messages") == 0 )
        {
            bm->chatbox_messages = atoi(value);
            return;
        }
        if( strcmp(key, "chatbox_first_line") == 0 )
        {
            bm->chatbox_first_line = atoi(value);
            return;
        }
        if( strcmp(key, "chatbox_line_count") == 0 )
        {
            bm->chatbox_line_count = atoi(value);
            return;
        }
        if( strcmp(key, "chatbox_input") == 0 )
        {
            bm->chatbox_input = atoi(value);
            return;
        }
        if( strcmp(key, "chatbox_line_height") == 0 )
        {
            bm->chatbox_line_height = atoi(value);
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
            fprintf(
                stderr,
                "bootmanifest: [ui:gameframe] holds at most %d mounts; dropping %s=%s\n",
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
            fprintf(
                stderr,
                "bootmanifest: [ui:varc] holds at most %d seeds; dropping %s=%s\n",
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
            "debug_overlay",    "loc_editor_toggle"
        };
        char const* name = section_name + 7;
        struct BootManifestDebugAction* action = NULL;

        if( !name[0] )
        {
            fprintf(stderr, "bootmanifest: [action:<name>] requires a name\n");
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
                fprintf(stderr, "bootmanifest: at most %d debug actions\n",
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
            fprintf(stderr, "bootmanifest: [action:%s] has unknown target '%s'\n", name, value);
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
            fprintf(stderr, "bootmanifest: [debug:hotkeys] has unknown key '%s'\n", key);
            bm->debug_hotkey_error = 1;
            return;
        }
        if( bm->debug_hotkey_count >= BOOTMANIFEST_DEBUG_HOTKEY_MAX )
        {
            fprintf(stderr, "bootmanifest: at most %d debug hotkeys\n",
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
                fprintf(
                    stderr,
                    "bootmanifest: [features] era must be lostcity|osrs|server_routed, got '%s'\n",
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
                fprintf(stderr,
                        "bootmanifest: [features] ground_click_nearest must be "
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
        if( strcmp(key, "painter_draw_distance") == 0 )
        {
            int distance;
            if( !bm_parse_int(key, value, &distance) )
                return;
            if( distance < TORIRS_PAINTER_DRAW_DISTANCE_MIN ||
                distance > TORIRS_PAINTER_DRAW_DISTANCE_MAX )
            {
                fprintf(stderr,
                        "bootmanifest: [features] painter_draw_distance must be "
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
                    fprintf(
                        stderr,
                        "bootmanifest: npc_type_ambient_contrast must be 0|1, got '%s'\n",
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

    fprintf(stderr, "bootmanifest: ignoring unknown key '%s'\n", key);
}

/* Split dirname of `path` into dir (without trailing slash). "" for a bare
 * filename with no directory component. */
static void
bm_dirname(char* dir, size_t cap, char const* path)
{
    char const* slash = strrchr(path, '/');
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
    bm->features_ground_click_unbounded = -1;
    bm->features_ground_click_offmap = -1;
    bm->spawn_x = -1;
    bm->spawn_z = -1;
    /* -1 rather than 0: a chatbox may legitimately have no input line or no
     * scroll layer, and 0 is a real component child id. */
    bm->chatbox_messages = -1;
    bm->chatbox_input = -1;

    FILE* f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "bootmanifest: cannot open '%s'\n", path);
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
    if( !data )
    {
        fclose(f);
        return -1;
    }
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
            else if( section == BM_SECTION_NONE )
                fprintf(
                    stderr,
                    "bootmanifest: ignoring unknown section '[%s]'\n",
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
        fprintf(
            stderr,
            "bootmanifest: parse of '%s' failed (result=%d state=%d offset=%u)\n",
            path,
            parse_result,
            (int)reader.state,
            reader.offset);
        return -1;
    }

    for( int i = 0; i < bm->debug_action_count; i++ )
        if( bm->debug_actions[i].target < 0 )
        {
            fprintf(stderr, "bootmanifest: [action:%s] is missing required t=\n",
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
            fprintf(stderr, "bootmanifest: [debug:hotkeys] references unknown action '%s'\n",
                    bm->debug_hotkeys[i].action);
            bm->debug_hotkey_error = 1;
        }
    }
    if( bm->debug_hotkey_error )
        return -1;

    /* All four identity keys are required. Missing one is user input, not an
     * assert — report and fail the load. */
    if( bm->cache_epoch == RSCACHE_EPOCH_UNSET )
    {
        fprintf(stderr, "bootmanifest: '%s' missing required [cache:boot] epoch=\n", path);
        return -1;
    }
    if( bm->cache_game == RSCACHE_GAME_UNSET )
    {
        fprintf(stderr, "bootmanifest: '%s' missing required [cache:boot] game=\n", path);
        return -1;
    }
    if( bm->cache_revision < 0 )
    {
        fprintf(stderr, "bootmanifest: '%s' missing required [cache:boot] revision=\n", path);
        return -1;
    }
    if( !bm->cache_quirks_set )
    {
        fprintf(stderr, "bootmanifest: '%s' missing required [cache:boot] quirks=\n", path);
        return -1;
    }

    if( bm->client_args_error )
    {
        fprintf(stderr, "bootmanifest: invalid [client:args] in '%s'\n", path);
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
    if( bm->chatbox_interface > 0 )
    {
        cfg->chatbox.interface_id = bm->chatbox_interface;
        cfg->chatbox.messages_child = bm->chatbox_messages;
        cfg->chatbox.first_line = bm->chatbox_first_line;
        cfg->chatbox.line_count = bm->chatbox_line_count;
        cfg->chatbox.input_child = bm->chatbox_input;
        cfg->chatbox.line_height = bm->chatbox_line_height;
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
