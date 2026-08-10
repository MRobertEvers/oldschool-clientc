/*
 * Bounded RS727 -> OSRS239 material bridge for the QBD / Tormented Demon lane.
 *
 * RS727 model faces name procedural programs (idx26 metadata + idx9 programs,
 * with idx8 sprite inputs). OSRS239 model faces instead name members of the
 * single sprite-backed texture archive. This tool deliberately performs the
 * semantic conversion as a separate, reproducible approximation pass:
 *
 *   - discover direct materials from the imported model closure;
 *   - include the historical map-floor materials that have no model face;
 *   - render each program at 128x128 through the client-matched evaluator;
 *   - quantise it to a deterministic 6x7x6 sprite palette;
 *   - emit sparse lane sprite/texture records and an exact metadata ledger;
 *   - rewrite imported OB3 face material ids through that ledger.
 *
 * It refuses unsupported graphs, absent dependencies, unsafe id collisions,
 * unknown model formats, and unmapped textured faces. Runtime features the
 * OSRS texture record cannot express are recorded per row in the TSV rather
 * than silently claimed as exact.
 */

#include "engine/proctex/proctex_generator.h"

#include "bmp.h"
#include "datatypes/dat2_proctexture.h"
#include "datatypes/dat2_sprites.h"
#include "datatypes/model.h"
#include "dat2disk.h"
#include "reference_table.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define PORT_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define PORT_MKDIR(path) mkdir(path, 0755)
#endif

#define MAX_TEXTURES 4096
#define MAX_SPRITE_DEPENDENCIES 512
#define BAKE_SIZE 128
#define DEST_TEXTURE_BASE 211
#define DEST_SPRITE_BASE 8535
#define PALETTE_RED_LEVELS 6
#define PALETTE_GREEN_LEVELS 7
#define PALETTE_BLUE_LEVELS 6
#define PALETTE_LENGTH (1 + PALETTE_RED_LEVELS * PALETTE_GREEN_LEVELS * PALETTE_BLUE_LEVELS)

#define REASON_MODEL 0x1u
#define REASON_OVERLAY 0x2u
#define REASON_UNDERLAY 0x4u

/* These five must remain first: an OSRS overlay stores its texture id in a u8. */
static const int g_overlay_materials[] = { 348, 408, 600, 616, 651 };
static const int g_underlay_materials[] = { 400, 441, 491, 495, 504, 519, 521, 929, 980 };

struct int_list
{
    int* values;
    int count;
    int capacity;
};

struct model_output
{
    int source_id;
    uint8_t* bytes;
    uint32_t size;
};

struct material_mapping
{
    int dest_texture;
    int dest_sprite;
    bool present;
};

struct texture_entry
{
    struct RSCache_Dat2ProcTexture* program;
    int32_t* argb;
    bool present;
    bool attempted;
    bool rendered;
    bool transparent;
};

static struct texture_entry g_textures[MAX_TEXTURES];
static uint8_t g_reasons[MAX_TEXTURES];
static struct material_mapping g_mapping[MAX_TEXTURES];

static int
int_compare(const void* lhs, const void* rhs)
{
    int a = *(const int*)lhs;
    int b = *(const int*)rhs;
    return (a > b) - (a < b);
}

static int
list_add_unique(struct int_list* list, int value)
{
    for( int i = 0; i < list->count; i++ )
        if( list->values[i] == value ) return 1;
    if( list->count == list->capacity )
    {
        int capacity = list->capacity ? list->capacity * 2 : 64;
        int* values = realloc(list->values, (size_t)capacity * sizeof(*values));
        if( !values ) return 0;
        list->values = values;
        list->capacity = capacity;
    }
    list->values[list->count++] = value;
    return 1;
}

static void
list_sort(struct int_list* list)
{
    qsort(list->values, (size_t)list->count, sizeof(*list->values), int_compare);
}

static int
mkdir_p(const char* path)
{
    char work[2048];
    if( strlen(path) >= sizeof(work) ) return 0;
    snprintf(work, sizeof(work), "%s", path);
    for( char* cursor = work + 1; *cursor; cursor++ )
    {
        if( *cursor != '/' && *cursor != '\\' ) continue;
        char saved = *cursor;
        *cursor = '\0';
        if( PORT_MKDIR(work) != 0 && errno != EEXIST ) return 0;
        *cursor = saved;
    }
    return PORT_MKDIR(work) == 0 || errno == EEXIST;
}

static int
ensure_parent(const char* path)
{
    char work[2048];
    if( strlen(path) >= sizeof(work) ) return 0;
    snprintf(work, sizeof(work), "%s", path);
    char* slash = strrchr(work, '/');
#if defined(_WIN32)
    char* backslash = strrchr(work, '\\');
    if( backslash && (!slash || backslash > slash) ) slash = backslash;
#endif
    if( !slash ) return 1;
    *slash = '\0';
    return mkdir_p(work);
}

static int
write_bytes(const char* path, const void* data, size_t size)
{
    if( !ensure_parent(path) ) return 0;
    FILE* file = fopen(path, "wb");
    if( !file ) return 0;
    int ok = size == 0 || fwrite(data, 1, size, file) == size;
    if( fclose(file) != 0 ) ok = 0;
    return ok;
}

static char*
trim(char* text)
{
    while( isspace((unsigned char)*text) ) text++;
    size_t length = strlen(text);
    while( length && isspace((unsigned char)text[length - 1]) ) text[--length] = '\0';
    return text;
}

static int
parse_model_pack(const char* path, struct int_list* models)
{
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr, "rs2012_material_bake: cannot read model pack %s\n", path);
        return 0;
    }
    char line[2048];
    int ok = 1;
    while( ok && fgets(line, sizeof(line), file) )
    {
        char* text = trim(line);
        if( !*text || *text == '#' || *text == ';' ||
            (text[0] == '/' && text[1] == '/') )
            continue;
        char* equals = strchr(text, '=');
        if( !equals )
        {
            ok = 0;
            break;
        }
        char* name = trim(equals + 1);
        char* marker = strstr(name, "rs2012_model_");
        if( !marker )
        {
            fprintf(stderr, "rs2012_material_bake: unexpected model name %s\n", name);
            ok = 0;
            break;
        }
        char* end = NULL;
        errno = 0;
        long source = strtol(marker + strlen("rs2012_model_"), &end, 10);
        if( errno || !end || *end || source < 0 || source > INT_MAX ||
            !list_add_unique(models, (int)source) )
        {
            fprintf(stderr, "rs2012_material_bake: malformed model row %s\n", text);
            ok = 0;
        }
    }
    if( ferror(file) ) ok = 0;
    fclose(file);
    return ok && models->count > 0;
}

static struct RSCache_Model*
load_model(
    struct RSCache_Dat2Disk* disk,
    int table,
    int source_id,
    struct RSCache_ModelProvenance** provenance)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table, source_id);
    if( !archive ) return NULL;
    struct RSCache_Model* model = RSCache_ModelNewDecodeProvenance(
        (uint8_t*)archive->data, archive->data_size, provenance);
    RSCache_Dat2DiskArchiveFree(archive);
    return model;
}

static int
collect_model_materials(
    struct RSCache_Dat2Disk* disk,
    int model_table,
    const struct int_list* models)
{
    for( int i = 0; i < models->count; i++ )
    {
        struct RSCache_ModelProvenance* provenance = NULL;
        struct RSCache_Model* model =
            load_model(disk, model_table, models->values[i], &provenance);
        if( !model || !provenance )
        {
            fprintf(stderr, "rs2012_material_bake: cannot decode model %d\n", models->values[i]);
            RSCache_ModelFree(model);
            RSCache_ModelProvenanceFree(provenance);
            return 0;
        }
        if( provenance->format != RSCACHE_MODEL_FORMAT_OB3 )
        {
            fprintf(
                stderr,
                "rs2012_material_bake: model %d is format %d, expected OB3\n",
                models->values[i],
                provenance->format);
            RSCache_ModelFree(model);
            RSCache_ModelProvenanceFree(provenance);
            return 0;
        }
        if( model->face_textures )
            for( int face = 0; face < model->face_count; face++ )
            {
                int material = model->face_textures[face];
                if( material < 0 ) continue;
                if( material >= MAX_TEXTURES )
                {
                    fprintf(
                        stderr,
                        "rs2012_material_bake: model %d material %d exceeds tool bound\n",
                        models->values[i],
                        material);
                    RSCache_ModelFree(model);
                    RSCache_ModelProvenanceFree(provenance);
                    return 0;
                }
                g_reasons[material] |= REASON_MODEL;
            }
        RSCache_ModelFree(model);
        RSCache_ModelProvenanceFree(provenance);
    }
    for( size_t i = 0; i < sizeof(g_overlay_materials) / sizeof(g_overlay_materials[0]); i++ )
        g_reasons[g_overlay_materials[i]] |= REASON_OVERLAY;
    for( size_t i = 0; i < sizeof(g_underlay_materials) / sizeof(g_underlay_materials[0]); i++ )
        g_reasons[g_underlay_materials[i]] |= REASON_UNDERLAY;
    return 1;
}
