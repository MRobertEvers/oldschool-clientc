#include "bootmanifest.h"

#include "app.h"

#include "3rd/ini/ini.h"
#include "3rd/rscache/src/rscache_profile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Parse a non-negative int for [spawn:hotkeys]. Returns 1 and writes *out on
 * success; on failure prints a named stderr line and returns 0. */
static int
bm_parse_nonneg_int(char const* key, char const* value, int* out)
{
    char* end = NULL;
    long v;

    assert(key && value && out);
    v = strtol(value, &end, 10);
    if( end == value || *end != '\0' || v < 0 )
    {
        fprintf(
            stderr,
            "bootmanifest: [spawn:hotkeys] %s must be a non-negative int, got '%s'\n",
            key,
            value);
        return 0;
    }
    *out = (int)v;
    return 1;
}
enum bm_section
{
    BM_SECTION_NONE = 0,
    BM_SECTION_CACHE,
    BM_SECTION_NET,
    BM_SECTION_UI,
    BM_SECTION_SPAWN,
};

static enum bm_section
bm_section_of(char const* header)
{
    /* Headers are "type:name"; we only care about the type prefix. */
    if( strncmp(header, "cache:", 6) == 0 )
        return BM_SECTION_CACHE;
    if( strncmp(header, "net:", 4) == 0 )
        return BM_SECTION_NET;
    if( strncmp(header, "ui:", 3) == 0 )
        return BM_SECTION_UI;
    if( strncmp(header, "spawn:", 6) == 0 )
        return BM_SECTION_SPAWN;
    return BM_SECTION_NONE;
}

static void
bm_set_kv(
    struct BootManifest* bm,
    char const* manifest_dir,
    enum bm_section section,
    char const* key,
    char const* value)
{
    if( key[0] == '\0' )
        return;

    switch( section )
    {
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
        break;

    case BM_SECTION_SPAWN:
        if( strcmp(key, "npc") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_npc_id = v;
            return;
        }
        if( strcmp(key, "obj") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_obj_id = v;
            return;
        }
        if( strcmp(key, "spotanim") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_spotanim_id = v;
            return;
        }
        if( strcmp(key, "spotanim_height") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_spotanim_height = v;
            return;
        }
        if( strcmp(key, "spotanim_delay") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_spotanim_delay = v;
            return;
        }
        if( strcmp(key, "proj_model") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_proj_model_id = v;
            return;
        }
        if( strcmp(key, "proj_seq") == 0 )
        {
            int v;
            if( bm_parse_nonneg_int(key, value, &v) )
                bm->spawn_proj_seq_id = v;
            return;
        }
        break;

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
    bm->spawn_x = -1;
    bm->spawn_z = -1;
    bm->spawn_npc_id = -1;
    bm->spawn_obj_id = -1;
    bm->spawn_spotanim_id = -1;
    bm->spawn_spotanim_height = -1;
    bm->spawn_spotanim_delay = -1;
    bm->spawn_proj_model_id = -1;
    bm->spawn_proj_seq_id = -1;

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
    struct INIElement element = { 0 };
    int parse_result = TORI_INI_ERR_OK;
    while( (parse_result = ini_reader_next(
                &reader, (uint8_t*)data, (uint32_t)file_size, &element)) == TORI_INI_ERR_OK )
    {
        switch( element.kind )
        {
        case INI_ELEMENT_SECTION:
            section = bm_section_of(element._section.name);
            if( section == BM_SECTION_NONE )
                fprintf(
                    stderr,
                    "bootmanifest: ignoring unknown section '[%s]'\n",
                    element._section.name);
            break;
        case INI_ELEMENT_KEYVAL:
            bm_set_kv(bm, manifest_dir, section, element._keyval.name, element._keyval.value);
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

    if( bm->ui_logic )
        cfg->ui_logic = bm->ui_logic;
    if( bm->revconfig_ui[0] )
        cfg->revconfig_ui_ini = bm->revconfig_ui;
    if( bm->revconfig_cache[0] )
        cfg->revconfig_cache_ini = bm->revconfig_cache;
    if( bm->interface_id > 0 )
        cfg->interface_id = bm->interface_id;
    if( bm->spawn_x >= 0 && bm->spawn_z >= 0 )
    {
        cfg->spawn_x = bm->spawn_x;
        cfg->spawn_z = bm->spawn_z;
    }
    if( bm->spawn_npc_id >= 0 )
        cfg->spawn_npc_id = bm->spawn_npc_id;
    if( bm->spawn_obj_id >= 0 )
        cfg->spawn_obj_id = bm->spawn_obj_id;
    if( bm->spawn_spotanim_id >= 0 )
        cfg->spawn_spotanim_id = bm->spawn_spotanim_id;
    if( bm->spawn_spotanim_height >= 0 )
        cfg->spawn_spotanim_height = bm->spawn_spotanim_height;
    if( bm->spawn_spotanim_delay >= 0 )
        cfg->spawn_spotanim_delay = bm->spawn_spotanim_delay;
    if( bm->spawn_proj_model_id >= 0 )
        cfg->spawn_proj_model_id = bm->spawn_proj_model_id;
    if( bm->spawn_proj_seq_id >= 0 )
        cfg->spawn_proj_seq_id = bm->spawn_proj_seq_id;
}
