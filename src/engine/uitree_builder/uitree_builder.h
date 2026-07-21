#ifndef UITREE_BUILDER_H
#define UITREE_BUILDER_H

#include "asyncio.h"

#include <stdint.h>

struct CacheProvider;
struct UITree;
struct InvManager;
struct RS_CS2Host;
struct UIBuilderManifest;

#define UITREE_BUILDER_NAME_MAX 64
#define UITREE_BUILDER_ONLOAD_ARGV_MAX 64
/* Mirrors TORIRS_COMPONENT_HOOK_STR_MAX/LEN (hook string-arg pool). */
#define UITREE_BUILDER_ONLOAD_STR_MAX 4
#define UITREE_BUILDER_ONLOAD_STR_LEN 80

struct UIBuilderSpriteEntry
{
    char name[UITREE_BUILDER_NAME_MAX];
    int archive_id;
    int atlas_index;
    int atlas_count;
};

struct UIBuilderFontEntry
{
    char name[UITREE_BUILDER_NAME_MAX];
    int archive_id;
    int cache_font_id;
};

struct UIBuilderOnLoadEntry
{
    int component_id;
    int script_id;
    int argc;
    int argv[UITREE_BUILDER_ONLOAD_ARGV_MAX];
    /* Bit i set = argv[i] is a string arg; strings fill strv[] in position
     * order (k-th set bit -> strv[k]). */
    uint64_t str_mask;
    int str_argc;
    char strv[UITREE_BUILDER_ONLOAD_STR_MAX][UITREE_BUILDER_ONLOAD_STR_LEN];
};

struct UITreeBuilder
{
    struct CacheProvider* provider;
    struct UITree* tree;
    struct InvManager* invs;
    struct RS_CS2Host* host; /* may be NULL — parent task skips CS2 */
    char ini_path[512];
    /** Optional second RevConfig file (sprites/fonts). Empty = unused. */
    char cache_ini_path[512];

    struct UIBuilderSpriteEntry* sprites;
    int sprite_count;
    int sprite_cap;

    struct UIBuilderFontEntry* fonts;
    int font_count;
    int font_cap;

    struct UIBuilderOnLoadEntry* onloads;
    int onload_count;
    int onload_cap;
};

void
UITreeBuilder_Init(
    struct UITreeBuilder* builder,
    struct CacheProvider* provider,
    struct UITree* tree,
    struct InvManager* invs,
    struct RS_CS2Host* host,
    char const* ini_path);

/** Like Init, but also loads a cache/sprites RevConfig INI. */
void
UITreeBuilder_InitEx(
    struct UITreeBuilder* builder,
    struct CacheProvider* provider,
    struct UITree* tree,
    struct InvManager* invs,
    struct RS_CS2Host* host,
    char const* ini_path,
    char const* cache_ini_path);

void
UITreeBuilder_Free(struct UITreeBuilder* builder);

void
UITreeBuilder_RegisterSprite(
    struct UITreeBuilder* builder,
    char const* name,
    int archive_id,
    int atlas_index,
    int atlas_count);

void
UITreeBuilder_RegisterFont(
    struct UITreeBuilder* builder,
    char const* name,
    int archive_id,
    int cache_font_id);

void
UITreeBuilder_AddOnLoad(
    struct UITreeBuilder* builder,
    int component_id,
    int script_id,
    int const* argv,
    int argc,
    uint64_t str_mask,
    char const* const* strs,
    int str_argc);

/** Resolve `name` or `name[index]` → archive/scene id. Returns -1 on miss. */
int
UITreeBuilder_ResolveSpriteRef(
    struct UITreeBuilder const* builder,
    char const* ref,
    int* out_atlas_index);

int
UITreeBuilder_ResolveFontName(
    struct UITreeBuilder const* builder,
    char const* name);

/** Map dat2 archive font id → cache_font_id slot; identity if unknown. */
int
UITreeBuilder_ResolveFontArchive(
    struct UITreeBuilder const* builder,
    int archive_id);

struct ToriRS_Task*
CreateTask_UITreeBuild(struct UITreeBuilder* builder);

struct ToriRS_Task*
CreateTask_UIBuilderAssetsLoad(
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest);

#endif
