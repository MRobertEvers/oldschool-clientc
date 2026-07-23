#include "bootmanifest.h"

#include "app.h"

#include "3rd/ini/ini.h"

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

/* Section dispatch state for the pull-parse loop. */
enum bm_section
{
    BM_SECTION_NONE = 0,
    BM_SECTION_CACHE,
    BM_SECTION_NET,
    BM_SECTION_UI,
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
        if( strcmp(key, "kind") == 0 )
        {
            if( strcmp(value, "dat1") == 0 )
                bm->cache_kind = APP_CACHE_DAT1;
            else if( strcmp(value, "dat2") == 0 )
                bm->cache_kind = APP_CACHE_DAT2;
            else
                fprintf(stderr, "bootmanifest: [cache] kind must be dat1|dat2, got '%s'\n", value);
            return;
        }
        if( strcmp(key, "dir") == 0 )
        {
            bm_join_path(bm->cache_dir, sizeof(bm->cache_dir), manifest_dir, value);
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
    bm->cache_kind = -1; /* unset sentinel */

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
    return 0;
}

void
BootManifest_ApplyToConfig(struct BootManifest const* bm, struct AppConfig* cfg)
{
    if( bm->cache_kind >= 0 )
        cfg->cache_kind = (enum AppCacheKind)bm->cache_kind;
    if( bm->cache_dir[0] )
        cfg->cache_dir = bm->cache_dir;

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

    if( bm->ui_logic )
        cfg->ui_logic = bm->ui_logic;
    if( bm->revconfig_ui[0] )
        cfg->revconfig_ui_ini = bm->revconfig_ui;
    if( bm->revconfig_cache[0] )
        cfg->revconfig_cache_ini = bm->revconfig_cache;
    if( bm->interface_id > 0 )
        cfg->interface_id = bm->interface_id;
}
