#include "uitree_builder.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
UITreeBuilder_Init(
    struct UITreeBuilder* builder,
    struct CacheProvider* provider,
    struct UITree* tree,
    struct InvManager* invs,
    struct RS_CS2Host* host,
    char const* ini_path)
{
    UITreeBuilder_InitEx(builder, provider, tree, invs, host, ini_path, NULL);
}

void
UITreeBuilder_InitEx(
    struct UITreeBuilder* builder,
    struct CacheProvider* provider,
    struct UITree* tree,
    struct InvManager* invs,
    struct RS_CS2Host* host,
    char const* ini_path,
    char const* cache_ini_path)
{
    assert(builder);
    assert(provider);
    assert(tree);
    assert(invs);

    memset(builder, 0, sizeof(*builder));
    builder->provider = provider;
    builder->tree = tree;
    builder->invs = invs;
    builder->host = host;
    if( ini_path )
        strncpy(builder->ini_path, ini_path, sizeof(builder->ini_path) - 1);
    if( cache_ini_path )
        strncpy(
            builder->cache_ini_path, cache_ini_path, sizeof(builder->cache_ini_path) - 1);
}

void
UITreeBuilder_Free(struct UITreeBuilder* builder)
{
    assert(builder);
    free(builder->sprites);
    free(builder->fonts);
    free(builder->onloads);
    builder->sprites = NULL;
    builder->fonts = NULL;
    builder->onloads = NULL;
    builder->sprite_count = builder->sprite_cap = 0;
    builder->font_count = builder->font_cap = 0;
    builder->onload_count = builder->onload_cap = 0;
}

static void*
grow(
    void* ptr,
    int* count,
    int* cap,
    size_t elem)
{
    assert(count && cap);
    if( *count < *cap )
        return ptr;
    int n = *cap == 0 ? 8 : *cap * 2;
    void* p = realloc(ptr, (size_t)n * elem);
    assert(p);
    *cap = n;
    return p;
}

void
UITreeBuilder_RegisterSprite(
    struct UITreeBuilder* builder,
    char const* name,
    int archive_id,
    int atlas_index,
    int atlas_count)
{
    assert(builder);
    assert(name && name[0]);

    for( int i = 0; i < builder->sprite_count; i++ )
    {
        if( strcmp(builder->sprites[i].name, name) == 0 )
        {
            builder->sprites[i].archive_id = archive_id;
            builder->sprites[i].atlas_index = atlas_index;
            builder->sprites[i].atlas_count = atlas_count;
            return;
        }
    }

    builder->sprites =
        grow(builder->sprites, &builder->sprite_count, &builder->sprite_cap, sizeof(*builder->sprites));
    struct UIBuilderSpriteEntry* e = &builder->sprites[builder->sprite_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->archive_id = archive_id;
    e->atlas_index = atlas_index;
    e->atlas_count = atlas_count;
}

void
UITreeBuilder_RegisterFont(
    struct UITreeBuilder* builder,
    char const* name,
    int archive_id,
    int cache_font_id)
{
    assert(builder);
    assert(name && name[0]);

    for( int i = 0; i < builder->font_count; i++ )
    {
        if( strcmp(builder->fonts[i].name, name) == 0 )
        {
            builder->fonts[i].archive_id = archive_id;
            builder->fonts[i].cache_font_id = cache_font_id;
            return;
        }
    }

    builder->fonts =
        grow(builder->fonts, &builder->font_count, &builder->font_cap, sizeof(*builder->fonts));
    struct UIBuilderFontEntry* e = &builder->fonts[builder->font_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->archive_id = archive_id;
    e->cache_font_id = cache_font_id;
}

void
UITreeBuilder_AddOnLoad(
    struct UITreeBuilder* builder,
    int component_id,
    int script_id,
    int const* argv,
    int argc,
    uint64_t str_mask,
    char const* const* strs,
    int str_argc)
{
    assert(builder);
    if( script_id <= 0 )
        return;

    builder->onloads =
        grow(builder->onloads, &builder->onload_count, &builder->onload_cap, sizeof(*builder->onloads));
    struct UIBuilderOnLoadEntry* e = &builder->onloads[builder->onload_count++];
    memset(e, 0, sizeof(*e));
    e->component_id = component_id;
    e->script_id = script_id;
    if( argc > UITREE_BUILDER_ONLOAD_ARGV_MAX )
    {
        fprintf(
            stderr,
            "UITreeBuilder: onload argc %d truncated to %d (component 0x%x)\n",
            argc,
            UITREE_BUILDER_ONLOAD_ARGV_MAX,
            (unsigned)component_id);
        argc = UITREE_BUILDER_ONLOAD_ARGV_MAX;
    }
    if( argc < 0 )
        argc = 0;
    e->argc = argc;
    if( argv && argc > 0 )
        memcpy(e->argv, argv, (size_t)argc * sizeof(int));
    e->str_mask = str_mask;
    if( str_argc > UITREE_BUILDER_ONLOAD_STR_MAX )
        str_argc = UITREE_BUILDER_ONLOAD_STR_MAX;
    if( str_argc < 0 || !strs )
        str_argc = 0;
    e->str_argc = str_argc;
    for( int i = 0; i < str_argc; i++ )
    {
        strncpy(e->strv[i], strs[i] ? strs[i] : "", UITREE_BUILDER_ONLOAD_STR_LEN - 1);
        e->strv[i][UITREE_BUILDER_ONLOAD_STR_LEN - 1] = '\0';
    }
}

int
UITreeBuilder_ResolveSpriteRef(
    struct UITreeBuilder const* builder,
    char const* ref,
    int* out_atlas_index)
{
    assert(builder);
    assert(ref);

    char name[UITREE_BUILDER_NAME_MAX];
    int atlas = -1;
    char const* bracket = strchr(ref, '[');
    if( bracket )
    {
        size_t nlen = (size_t)(bracket - ref);
        if( nlen >= sizeof(name) )
            nlen = sizeof(name) - 1;
        memcpy(name, ref, nlen);
        name[nlen] = '\0';
        atlas = atoi(bracket + 1);
    }
    else
    {
        strncpy(name, ref, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    for( int i = 0; i < builder->sprite_count; i++ )
    {
        if( strcmp(builder->sprites[i].name, name) != 0 )
            continue;
        if( out_atlas_index )
        {
            if( atlas >= 0 )
                *out_atlas_index = atlas;
            else
                *out_atlas_index = builder->sprites[i].atlas_index;
        }
        return builder->sprites[i].archive_id;
    }
    return -1;
}

int
UITreeBuilder_ResolveFontName(
    struct UITreeBuilder const* builder,
    char const* name)
{
    assert(builder);
    assert(name);
    for( int i = 0; i < builder->font_count; i++ )
    {
        if( strcmp(builder->fonts[i].name, name) == 0 )
            return builder->fonts[i].cache_font_id;
    }
    return -1;
}

int
UITreeBuilder_ResolveFontArchive(
    struct UITreeBuilder const* builder,
    int archive_id)
{
    assert(builder);
    if( archive_id < 0 )
        return archive_id;
    for( int i = 0; i < builder->font_count; i++ )
    {
        if( builder->fonts[i].archive_id == archive_id )
            return builder->fonts[i].cache_font_id >= 0 ? builder->fonts[i].cache_font_id
                                                        : archive_id;
    }
    return archive_id;
}
