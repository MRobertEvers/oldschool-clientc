#include "parity_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* s_manifest_buf;
static struct ParityCs2Case s_cs2_cases[64];
static int s_cs2_count;
static struct ParitySpriteCase s_sprite_cases[64];
static int s_sprite_count;
static struct ParityTreeCase s_tree_cases[32];
static int s_tree_count;

static const char*
skip_ws(const char* s)
{
    while( *s && isspace((unsigned char)*s) )
        s++;
    return s;
}

static const char*
json_find_key(const char* block, const char* key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = block;
    while( (p = strstr(p, pattern)) != NULL )
    {
        const char* after = p + strlen(pattern);
        after = skip_ws(after);
        if( *after == ':' )
            return p;
        p += strlen(pattern);
    }
    return NULL;
}

static int
json_read_string(const char* start, char* out, int out_cap)
{
    const char* q = strchr(start, '"');
    if( !q )
        return -1;
    q++;
    const char* end = strchr(q, '"');
    if( !end )
        return -1;
    int len = (int)(end - q);
    if( len >= out_cap )
        len = out_cap - 1;
    memcpy(out, q, (size_t)len);
    out[len] = '\0';
    return 0;
}

static int
json_read_int_after_key(const char* block, const char* key, int* out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(block, pattern);
    if( !p )
        return -1;
    p = strchr(p, ':');
    if( !p )
        return -1;
    p++;
    p = skip_ws(p);
    if( strncmp(p, "null", 4) == 0 )
    {
        *out = -1;
        return 0;
    }
    *out = (int)strtol(p, NULL, 10);
    return 0;
}

static bool
json_read_bool_after_key(const char* block, const char* key, bool* out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(block, pattern);
    if( !p )
        return false;
    p = strchr(p, ':');
    if( !p )
        return false;
    p++;
    p = skip_ws(p);
    *out = strncmp(p, "true", 4) == 0;
    return true;
}

static const char*
find_object_by_id(const char* array_start, const char* want_id)
{
    const char* cur = array_start;
    while( (cur = strstr(cur, "\"id\"")) != NULL )
    {
        char id_buf[PARITY_ID_MAX];
        const char* val = strchr(cur, ':');
        if( !val )
            break;
        val++;
        val = skip_ws(val);
        if( *val != '"' )
        {
            cur += 4;
            continue;
        }
        if( json_read_string(val, id_buf, sizeof(id_buf)) != 0 )
        {
            cur += 4;
            continue;
        }
        if( strcmp(id_buf, want_id) == 0 )
        {
            const char* obj = cur;
            while( obj > array_start && *obj != '{' )
                obj--;
            return obj;
        }
        cur += 4;
    }
    return NULL;
}

static const char*
find_array(const char* json, const char* key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if( !p )
        return NULL;
    p = strchr(p, '[');
    return p;
}

static int
parse_int_array(const char* block, const char* key, int* out, int max_count)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(block, pattern);
    if( !p )
        return 0;
    p = strchr(p, '[');
    if( !p )
        return 0;
    p++;
    int count = 0;
    while( *p && *p != ']' && count < max_count )
    {
        p = skip_ws(p);
        if( *p == ']' )
            break;
        out[count++] = (int)strtol(p, (char**)&p, 10);
        p = strchr(p, ',');
        if( !p )
            break;
        p++;
    }
    return count;
}

static int
parse_string_array(const char* block, const char* key, char out[][PARITY_STRING_LEN], int max_count)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(block, pattern);
    if( !p )
        return 0;
    p = strchr(p, '[');
    if( !p )
        return 0;
    p++;
    int count = 0;
    while( *p && *p != ']' && count < max_count )
    {
        p = skip_ws(p);
        if( *p == ']' )
            break;
        if( *p == '"' )
            json_read_string(p, out[count], PARITY_STRING_LEN);
        count++;
        p = strchr(p, ',');
        if( !p )
            break;
        p++;
    }
    return count;
}

static void
parse_cs2_case(const char* block, struct ParityCs2Case* c)
{
    memset(c, 0, sizeof(*c));
    const char* id_key = json_find_key(block, "id");
    if( id_key )
        json_read_string(strchr(id_key, ':'), c->id, sizeof(c->id));
    json_read_int_after_key(block, "scriptId", &c->script_id);
    json_read_bool_after_key(block, "synthetic", &c->synthetic);
    c->int_argc = parse_int_array(block, "intArgs", c->int_argv, PARITY_ARGS_MAX);
    c->string_argc = parse_string_array(block, "stringArgs", c->string_argv, PARITY_STRING_ARG_MAX);
    json_read_int_after_key(block, "activeComponent", &c->active_component);
    json_read_int_after_key(block, "iface", &c->iface);
    json_read_int_after_key(block, "componentFile", &c->component_file);
}

static void
parse_sprite_case(const char* block, struct ParitySpriteCase* c)
{
    memset(c, 0, sizeof(*c));
    const char* id_key = json_find_key(block, "id");
    if( id_key )
        json_read_string(strchr(id_key, ':'), c->id, sizeof(c->id));
    json_read_int_after_key(block, "spriteId", &c->sprite_id);
}

static void
parse_tree_fixture(const char* block, struct ParityTreeCase* c)
{
    const char* slots = strstr(block, "\"slots\"");
    if( !slots )
        return;
    const char* cur = slots;
    while( (cur = strchr(cur, '"')) != NULL )
    {
        cur++;
        if( !isdigit((unsigned char)*cur) )
            continue;
        char* end = NULL;
        long file_index = strtol(cur, &end, 10);
        if( end == cur || file_index < 0 )
            continue;
        const char* obj_key = strstr(end, "\"obj\"");
        if( !obj_key )
            continue;
        obj_key = strchr(obj_key, ':');
        if( !obj_key )
            continue;
        int obj_id = (int)strtol(obj_key + 1, NULL, 10);
        if( c->fixture_slot_count < PARITY_FIXTURE_SLOTS_MAX )
        {
            c->fixture_slots[c->fixture_slot_count].file_index = (int)file_index;
            c->fixture_slots[c->fixture_slot_count].obj_id = obj_id;
            c->fixture_slot_count++;
        }
        cur = end;
    }
}

static void
parse_tree_case(const char* block, struct ParityTreeCase* c)
{
    memset(c, 0, sizeof(*c));
    const char* id_key = json_find_key(block, "id");
    if( id_key )
        json_read_string(strchr(id_key, ':'), c->id, sizeof(c->id));
    json_read_int_after_key(block, "iface", &c->iface);
    json_read_bool_after_key(block, "panel", &c->panel);
    json_read_int_after_key(block, "rootW", &c->root_w);
    json_read_int_after_key(block, "rootH", &c->root_h);
    if( strstr(block, "\"fixture\"") && strstr(block, "\"slots\"") )
        parse_tree_fixture(block, c);
    const char* mounts = strstr(block, "\"mounts\"");
    if( mounts )
    {
        const char* p = strchr(mounts, '[');
        if( p )
        {
            p++;
            while( *p && *p != ']' && c->mount_count < PARITY_MOUNT_MAX )
            {
                const char* obj = strchr(p, '{');
                if( !obj || obj > strchr(p, ']') )
                    break;
                const char* end = strchr(obj, '}');
                if( !end )
                    break;
                char mount_block[256];
                int len = (int)(end - obj + 1);
                if( len >= (int)sizeof(mount_block) )
                    len = (int)sizeof(mount_block) - 1;
                memcpy(mount_block, obj, (size_t)len);
                mount_block[len] = '\0';
                json_read_int_after_key(mount_block, "childFile", &c->mounts[c->mount_count].child_file);
                json_read_int_after_key(mount_block, "iface", &c->mounts[c->mount_count].iface);
                c->mount_count++;
                p = end + 1;
            }
        }
    }
}

static void
parse_cs2_array(const char* json)
{
    const char* arr = find_array(json, "cs2Cases");
    if( !arr )
        return;
    const char* cur = arr;
    while( (cur = strchr(cur, '{')) != NULL && s_cs2_count < 64 )
    {
        const char* end = strchr(cur, '}');
        if( !end )
            break;
        char block[2048];
        int len = (int)(end - cur + 1);
        if( len >= (int)sizeof(block) )
            len = (int)sizeof(block) - 1;
        memcpy(block, cur, (size_t)len);
        block[len] = '\0';
        if( strstr(block, "\"id\"") )
        {
            parse_cs2_case(block, &s_cs2_cases[s_cs2_count]);
            s_cs2_count++;
        }
        cur = end + 1;
        if( cur >= arr + 50000 )
            break;
    }
}

static void
parse_sprite_array(const char* json)
{
    const char* arr = find_array(json, "spriteCases");
    if( !arr )
        return;
    const char* cur = arr;
    while( (cur = strchr(cur, '{')) != NULL && s_sprite_count < 64 )
    {
        const char* end = strchr(cur, '}');
        if( !end )
            break;
        char block[512];
        int len = (int)(end - cur + 1);
        if( len >= (int)sizeof(block) )
            len = (int)sizeof(block) - 1;
        memcpy(block, cur, (size_t)len);
        block[len] = '\0';
        if( strstr(block, "\"id\"") )
        {
            parse_sprite_case(block, &s_sprite_cases[s_sprite_count]);
            s_sprite_count++;
        }
        cur = end + 1;
    }
}

static void
parse_tree_array(const char* json)
{
    const char* arr = find_array(json, "treeCases");
    if( !arr )
        return;
    const char* cur = arr;
    while( (cur = strchr(cur, '{')) != NULL && s_tree_count < 32 )
    {
        int depth = 0;
        const char* end = cur;
        for( ; *end; end++ )
        {
            if( *end == '{' )
                depth++;
            else if( *end == '}' )
            {
                depth--;
                if( depth == 0 )
                    break;
            }
        }
        if( depth != 0 )
            break;
        char block[4096];
        int len = (int)(end - cur + 1);
        if( len >= (int)sizeof(block) )
            len = (int)sizeof(block) - 1;
        memcpy(block, cur, (size_t)len);
        block[len] = '\0';
        if( strstr(block, "\"id\"") )
        {
            parse_tree_case(block, &s_tree_cases[s_tree_count]);
            s_tree_count++;
        }
        cur = end + 1;
    }
}

int
parity_manifest_load(const char* path)
{
    free(s_manifest_buf);
    s_manifest_buf = NULL;
    s_cs2_count = 0;
    s_sprite_count = 0;
    s_tree_count = 0;

    FILE* fp = fopen(path, "rb");
    if( !fp )
        return -1;
    if( fseek(fp, 0, SEEK_END) != 0 )
    {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if( sz <= 0 )
    {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    s_manifest_buf = malloc((size_t)sz + 1);
    if( !s_manifest_buf )
    {
        fclose(fp);
        return -1;
    }
    if( fread(s_manifest_buf, 1, (size_t)sz, fp) != (size_t)sz )
    {
        fclose(fp);
        return -1;
    }
    s_manifest_buf[sz] = '\0';
    fclose(fp);

    parse_cs2_array(s_manifest_buf);
    parse_sprite_array(s_manifest_buf);
    parse_tree_array(s_manifest_buf);
    return 0;
}

struct ParityCs2Case const*
parity_manifest_find_cs2(const char* id)
{
    for( int i = 0; i < s_cs2_count; i++ )
        if( strcmp(s_cs2_cases[i].id, id) == 0 )
            return &s_cs2_cases[i];
    return NULL;
}

struct ParitySpriteCase const*
parity_manifest_find_sprite(const char* id)
{
    for( int i = 0; i < s_sprite_count; i++ )
        if( strcmp(s_sprite_cases[i].id, id) == 0 )
            return &s_sprite_cases[i];
    return NULL;
}

struct ParityTreeCase const*
parity_manifest_find_tree(const char* id)
{
    for( int i = 0; i < s_tree_count; i++ )
        if( strcmp(s_tree_cases[i].id, id) == 0 )
            return &s_tree_cases[i];
    return NULL;
}

int
parity_manifest_cs2_count(void)
{
    return s_cs2_count;
}

struct ParityCs2Case const*
parity_manifest_cs2_at(int index)
{
    return index >= 0 && index < s_cs2_count ? &s_cs2_cases[index] : NULL;
}

int
parity_manifest_sprite_count(void)
{
    return s_sprite_count;
}

struct ParitySpriteCase const*
parity_manifest_sprite_at(int index)
{
    return index >= 0 && index < s_sprite_count ? &s_sprite_cases[index] : NULL;
}

int
parity_manifest_tree_count(void)
{
    return s_tree_count;
}

struct ParityTreeCase const*
parity_manifest_tree_at(int index)
{
    return index >= 0 && index < s_tree_count ? &s_tree_cases[index] : NULL;
}
