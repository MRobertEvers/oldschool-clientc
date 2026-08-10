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
#include "datatypes/dat2_texture.h"
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
#define REASON_RETEXTURE 0x8u

#define REMAP_MARKER "// rs2012-material-ledger-remapped-v1"

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

struct text_output
{
    char path[2048];
    char* bytes;
    size_t size;
    bool present;
};

struct char_buffer
{
    char* bytes;
    size_t size;
    size_t capacity;
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
    bool exact_decode;
    bool attempted;
    bool rendered;
    bool source_transparent;
    bool transparent;
    /*
     * True when the source has no fully-opaque texels, i.e. it is a continuous
     * alpha blend layer rather than a diffuse map or a cutout.
     *
     * OSRS239 textures carry colour-key transparency, not alpha: a texel is
     * either drawn or skipped. A cutout survives that (its alpha is already 0
     * or 255); a blend layer does not, because thresholding a smooth gradient
     * invents holes that were never in the source. Measured on this lane, the
     * two are completely separated - every diffuse map is 100% alpha 255, and
     * every blend layer is 0%.
     */
    bool blend_layer;
};

static struct texture_entry g_textures[MAX_TEXTURES];
static uint8_t g_reasons[MAX_TEXTURES];
static struct material_mapping g_mapping[MAX_TEXTURES];
static int g_ground_mesh_model_faces_fallback;

/*
 * Faces naming a material whose first RS727 flag is clear have their texture
 * erased and fall back to the face's flat HSL colour. This is the correct
 * default and must stay on.
 *
 * It strips 274,715 lane faces, which looks like the reason the QBD renders as
 * untextured grey - but referencing those 204 materials instead was tried and
 * is worse: the arena renders as blown-out white shards, because they are
 * HD-only programs whose baked 128x128 approximation is not a diffuse map. The
 * source client agrees; `TextureLoader.isSd` selects them out and falls back to
 * the face colour, which is exactly what this reproduces.
 *
 * So the grey QBD is not caused by this fallback. If the encounter should show
 * material detail, the fix is upstream - which materials are classified SD-
 * usable, or an HD-capable renderer - not disabling this.
 *
 * --no-ground-mesh-fallback exists only to re-run that experiment.
 */
static bool g_ground_mesh_fallback = true;

/*
 * --alpha-textures: emit blend layers as alpha-blended textures rather than
 * letting them keep the colour-key path.
 *
 * OFF by default because the record is written but the client then fails to
 * publish the texture at all (skip_tex_miss rises to ~218 on the QBD), so the
 * faces are dropped instead of drawn. The raster and loader support is in
 * place and builds; the unresolved link is between the extended texture record
 * and the texture publish. Turn this on to work on that.
 */
static bool g_alpha_textures = false;

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
read_text(const char* path, size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    long length;
    char* bytes;
    if( !file ) return NULL;
    if( fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 )
    {
        fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length + 1);
    if( !bytes )
    {
        fclose(file);
        return NULL;
    }
    if( length && fread(bytes, 1, (size_t)length, file) != (size_t)length )
    {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if( fclose(file) != 0 )
    {
        free(bytes);
        return NULL;
    }
    bytes[length] = '\0';
    *out_size = (size_t)length;
    return bytes;
}

static int
buffer_reserve(struct char_buffer* buffer, size_t extra)
{
    if( extra > SIZE_MAX - buffer->size - 1 ) return 0;
    size_t wanted = buffer->size + extra + 1;
    if( wanted <= buffer->capacity ) return 1;
    size_t capacity = buffer->capacity ? buffer->capacity : 4096;
    while( capacity < wanted )
    {
        if( capacity > SIZE_MAX / 2 )
        {
            capacity = wanted;
            break;
        }
        capacity *= 2;
    }
    char* bytes = realloc(buffer->bytes, capacity);
    if( !bytes ) return 0;
    buffer->bytes = bytes;
    buffer->capacity = capacity;
    return 1;
}

static int
buffer_append(struct char_buffer* buffer, const char* bytes, size_t count)
{
    if( !buffer_reserve(buffer, count) ) return 0;
    memcpy(buffer->bytes + buffer->size, bytes, count);
    buffer->size += count;
    buffer->bytes[buffer->size] = '\0';
    return 1;
}

static int
buffer_append_int(struct char_buffer* buffer, int value)
{
    char text[64];
    int count = snprintf(text, sizeof(text), "%d", value);
    return count > 0 && (size_t)count < sizeof(text) &&
           buffer_append(buffer, text, (size_t)count);
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
mapping_source_for_dest(int dest_texture)
{
    int source = -1;
    for( int i = 0; i < MAX_TEXTURES; i++ )
        if( g_mapping[i].present && g_mapping[i].dest_texture == dest_texture )
        {
            if( source >= 0 ) return -2;
            source = i;
        }
    return source;
}

static int
load_material_ledger(const char* path)
{
    FILE* file = fopen(path, "rb");
    bool used_textures[65536] = { false };
    bool used_sprites[65536] = { false };
    char line[8192];
    int line_number = 0;
    if( !file )
    {
        if( errno == ENOENT ) return 1;
        fprintf(stderr, "rs2012_material_bake: cannot read ledger %s\n", path);
        return 0;
    }
    while( fgets(line, sizeof(line), file) )
    {
        line_number++;
        char* text = trim(line);
        if( !*text || *text == '#' || *text == ';' || !isdigit((unsigned char)*text) )
            continue;
        char* fields[3];
        fields[0] = text;
        for( int field = 0; field < 2; field++ )
        {
            char* tab = strchr(fields[field], '\t');
            if( !tab )
            {
                fprintf(stderr, "rs2012_material_bake: %s:%d has fewer than 3 columns\n",
                        path, line_number);
                fclose(file);
                return 0;
            }
            *tab = '\0';
            fields[field + 1] = tab + 1;
        }
        char* end = NULL;
        errno = 0;
        long source = strtol(fields[0], &end, 10);
        if( errno || !end || *trim(end) || source < 0 || source >= MAX_TEXTURES )
            goto bad_row;
        errno = 0;
        long texture = strtol(fields[1], &end, 10);
        if( errno || !end || *trim(end) || texture < DEST_TEXTURE_BASE || texture > 65535 )
            goto bad_row;
        errno = 0;
        long sprite = strtol(fields[2], &end, 10);
        while( end && *end == ' ' ) end++;
        if( errno || !end || (*end && *end != '\t') ||
            sprite < DEST_SPRITE_BASE || sprite > 65535 )
            goto bad_row;
        if( g_mapping[source].present || used_textures[texture] || used_sprites[sprite] )
        {
            fprintf(stderr, "rs2012_material_bake: %s:%d duplicates a source/destination id\n",
                    path, line_number);
            fclose(file);
            return 0;
        }
        g_mapping[source].present = true;
        g_mapping[source].dest_texture = (int)texture;
        g_mapping[source].dest_sprite = (int)sprite;
        used_textures[texture] = true;
        used_sprites[sprite] = true;
        continue;

bad_row:
        fprintf(stderr, "rs2012_material_bake: malformed mapping at %s:%d\n", path, line_number);
        fclose(file);
        return 0;
    }
    if( ferror(file) )
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static int
parse_retexture_line(const char* line, size_t length, int* out_value, size_t* out_equals)
{
    char scratch[512];
    if( length >= sizeof(scratch) ) return 0;
    memcpy(scratch, line, length);
    scratch[length] = '\0';
    char* text = trim(scratch);
    if( strncmp(text, "retex", 5) != 0 ) return 0;
    char* cursor = text + 5;
    if( !isdigit((unsigned char)*cursor) ) return 0;
    while( isdigit((unsigned char)*cursor) ) cursor++;
    if( (*cursor != 's' && *cursor != 'd') || cursor[1] != '=' ) return 0;
    cursor += 2;
    char* end = NULL;
    errno = 0;
    long value = strtol(cursor, &end, 10);
    if( errno || !end || *trim(end) || value < 0 || value >= MAX_TEXTURES ) return 0;
    const char* equals = memchr(line, '=', length);
    if( !equals ) return 0;
    *out_value = (int)value;
    *out_equals = (size_t)(equals - line);
    return 1;
}

static int
config_retexture_source(int value, bool remapped, const char* path)
{
    if( !remapped ) return value;
    int source = mapping_source_for_dest(value);
    if( source < 0 )
        fprintf(stderr,
                "rs2012_material_bake: remapped config %s uses unknown texture %d\n",
                path, value);
    return source;
}

static int
collect_config_retextures(const char* to_tree)
{
    static const char* names[] = { "rs2012.loc", "rs2012.npc", "rs2012.obj",
                                   "rs2012.spotanim" };
    for( size_t file_index = 0; file_index < sizeof(names) / sizeof(names[0]); file_index++ )
    {
        char path[2048];
        size_t size = 0;
        snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/configs/%s", to_tree,
                 names[file_index]);
        char* bytes = read_text(path, &size);
        if( !bytes )
        {
            fprintf(stderr, "rs2012_material_bake: cannot read config %s\n", path);
            return 0;
        }
        bool remapped = strncmp(bytes, REMAP_MARKER, strlen(REMAP_MARKER)) == 0;
        const char* cursor = bytes;
        const char* end = bytes + size;
        while( cursor < end )
        {
            const char* newline = memchr(cursor, '\n', (size_t)(end - cursor));
            const char* line_end = newline ? newline : end;
            int value = 0;
            size_t equals = 0;
            if( parse_retexture_line(cursor, (size_t)(line_end - cursor), &value, &equals) )
            {
                int source = config_retexture_source(value, remapped, path);
                if( source < 0 )
                {
                    free(bytes);
                    return 0;
                }
                g_reasons[source] |= REASON_RETEXTURE;
            }
            (void)equals;
            cursor = newline ? newline + 1 : end;
        }
        free(bytes);
    }
    return 1;
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

static int
allocate_material_mappings(void)
{
    bool used_textures[65536] = { false };
    bool used_sprites[65536] = { false };
    for( int source = 0; source < MAX_TEXTURES; source++ )
    {
        if( !g_mapping[source].present ) continue;
        int texture = g_mapping[source].dest_texture;
        int sprite = g_mapping[source].dest_sprite;
        if( texture < DEST_TEXTURE_BASE || texture > 65535 ||
            sprite < DEST_SPRITE_BASE || sprite > 65535 ||
            used_textures[texture] || used_sprites[sprite] )
        {
            fprintf(stderr, "rs2012_material_bake: invalid retained mapping for %d\n", source);
            return 0;
        }
        used_textures[texture] = true;
        used_sprites[sprite] = true;
    }

    for( size_t i = 0; i < sizeof(g_overlay_materials) / sizeof(g_overlay_materials[0]); i++ )
    {
        int source = g_overlay_materials[i];
        int texture = DEST_TEXTURE_BASE + (int)i;
        int sprite = DEST_SPRITE_BASE + (int)i;
        if( g_mapping[source].present )
        {
            if( g_mapping[source].dest_texture != texture ||
                g_mapping[source].dest_sprite != sprite )
            {
                fprintf(stderr,
                        "rs2012_material_bake: reserved overlay mapping %d must be %d/%d\n",
                        source, texture, sprite);
                return 0;
            }
            continue;
        }
        if( used_textures[texture] || used_sprites[sprite] )
        {
            fprintf(stderr, "rs2012_material_bake: reserved overlay destination collision\n");
            return 0;
        }
        g_mapping[source].present = true;
        g_mapping[source].dest_texture = texture;
        g_mapping[source].dest_sprite = sprite;
        used_textures[texture] = true;
        used_sprites[sprite] = true;
    }

    int next_texture = DEST_TEXTURE_BASE +
                       (int)(sizeof(g_overlay_materials) / sizeof(g_overlay_materials[0]));
    int next_sprite = DEST_SPRITE_BASE +
                      (int)(sizeof(g_overlay_materials) / sizeof(g_overlay_materials[0]));
    for( int source = 0; source < MAX_TEXTURES; source++ )
    {
        if( !g_reasons[source] || g_mapping[source].present ) continue;
        while( next_texture <= 65535 && used_textures[next_texture] ) next_texture++;
        while( next_sprite <= 65535 && used_sprites[next_sprite] ) next_sprite++;
        if( next_texture > 65535 || next_sprite > 65535 )
        {
            fprintf(stderr, "rs2012_material_bake: destination id space exhausted\n");
            return 0;
        }
        g_mapping[source].present = true;
        g_mapping[source].dest_texture = next_texture;
        g_mapping[source].dest_sprite = next_sprite;
        used_textures[next_texture++] = true;
        used_sprites[next_sprite++] = true;
    }
    return 1;
}

static int
load_texture_programs(
    struct RSCache_Dat2Disk* disk,
    int textures_table,
    uint32_t flags)
{
    struct RSCache_Dat2DiskArchive* reference_archive =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk, textures_table);
    if( !reference_archive ) return 0;
    struct RSCache_ReferenceTable* reference =
        RSCache_ReferenceTableNewDecode(reference_archive->data, reference_archive->data_size);
    RSCache_Dat2DiskArchiveFree(reference_archive);
    if( !reference ) return 0;

    int ok = 1;
    for( int i = 0; i < reference->id_count && ok; i++ )
    {
        int id = reference->ids[i];
        if( id < 0 || id >= MAX_TEXTURES ) continue;
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, textures_table, id);
        if( !archive )
        {
            fprintf(stderr, "rs2012_material_bake: texture program %d is absent\n", id);
            ok = 0;
            break;
        }
        struct RSCache_Dat2ProcTexture* program = RSCache_Dat2ProcTextureNewDecode(
            archive->data, archive->data_size, id, flags);
        if( !program )
        {
            fprintf(stderr, "rs2012_material_bake: texture program %d cannot decode\n", id);
            ok = 0;
        }
        else
        {
            g_textures[id].program = program;
            g_textures[id].present = true;
            g_textures[id].exact_decode = program->_consumed == archive->data_size;
            if( !g_textures[id].exact_decode )
            {
                fprintf(stderr,
                        "rs2012_material_bake: texture program %d consumed %d/%d bytes\n",
                        id, program->_consumed, archive->data_size);
                ok = 0;
            }
        }
        RSCache_Dat2DiskArchiveFree(archive);
    }
    RSCache_ReferenceTableFree(reference);
    return ok;
}

static struct RSCache_Dat2MaterialTable*
load_material_table(
    struct RSCache_Dat2Disk* disk,
    int materials_table,
    uint32_t flags)
{
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, materials_table, 0);
    if( !archive ) return NULL;
    struct RSCache_Dat2MaterialTable* table =
        RSCache_Dat2MaterialTableNewDecode(archive->data, archive->data_size, flags);
    if( !table || table->_consumed != archive->data_size )
    {
        fprintf(stderr, "rs2012_material_bake: material table decode is incomplete\n");
        RSCache_Dat2MaterialTableFree(table);
        table = NULL;
    }
    RSCache_Dat2DiskArchiveFree(archive);
    return table;
}

struct bake_context
{
    struct RSCache_Dat2Disk* disk;
    int sprites_table;
    struct
    {
        int id;
        int32_t* argb;
        int width;
        int height;
    } sprites[MAX_SPRITE_DEPENDENCIES];
    int sprite_count;
    int depth;
};

static bool
bake_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height);

static bool
bake_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb);

static bool
bake_texture(struct bake_context* context, int texture_id)
{
    if( texture_id < 0 || texture_id >= MAX_TEXTURES ) return false;
    struct texture_entry* entry = &g_textures[texture_id];
    if( entry->attempted ) return entry->rendered;
    entry->attempted = true;
    if( !entry->present || !entry->exact_decode ||
        !ProcTexGenerator_IsFullySupported(entry->program, NULL) || context->depth > 32 )
        return false;

    entry->argb = calloc((size_t)BAKE_SIZE * BAKE_SIZE, sizeof(*entry->argb));
    if( !entry->argb ) return false;
    struct ProcTexGenerator* generator =
        ProcTexGenerator_New(bake_resolve_sprite, bake_resolve_texture, context);
    if( !generator ) return false;
    int unsupported = 0;
    bool transparent = false;
    context->depth++;
    entry->rendered = ProcTexGenerator_Render(
                          generator, entry->program, BAKE_SIZE, 1.0, entry->argb,
                          &unsupported, &transparent) &&
                      unsupported == 0;
    context->depth--;
    ProcTexGenerator_Free(generator);
    if( !entry->rendered )
    {
        free(entry->argb);
        entry->argb = NULL;
        return false;
    }
    entry->source_transparent = transparent;
    return true;
}

static bool
bake_resolve_texture(void* user, int texture_id, int size, const int32_t** out_argb)
{
    struct bake_context* context = user;
    if( size != BAKE_SIZE || !bake_texture(context, texture_id) ) return false;
    *out_argb = g_textures[texture_id].argb;
    return true;
}

static bool
bake_resolve_sprite(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height)
{
    struct bake_context* context = user;
    for( int i = 0; i < context->sprite_count; i++ )
        if( context->sprites[i].id == sprite_id )
        {
            if( !context->sprites[i].argb ) return false;
            *out_argb = context->sprites[i].argb;
            *out_width = context->sprites[i].width;
            *out_height = context->sprites[i].height;
            return true;
        }
    if( context->sprite_count >= MAX_SPRITE_DEPENDENCIES ) return false;
    int slot = context->sprite_count++;
    context->sprites[slot].id = sprite_id;

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(context->disk, context->sprites_table, sprite_id);
    if( !archive ) return false;
    struct RSCache_Dat2SpritePack* pack = RSCache_Dat2SpritePackNewDecode(
        (const unsigned char*)archive->data, archive->data_size,
        RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        RSCache_Dat2SpritePackFree(pack);
        return false;
    }
    struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];
    int count = sprite->width * sprite->height;
    int32_t* pixels = count > 0 ? malloc((size_t)count * sizeof(*pixels)) : NULL;
    if( pixels )
        for( int i = 0; i < count; i++ )
        {
            int index = sprite->palette_pixels[i];
            int rgb = index >= 0 && index < pack->palette_length ? pack->palette[index] : 0;
            int alpha = sprite->pixel_alphas ? sprite->pixel_alphas[i] : (index ? 255 : 0);
            pixels[i] = (int32_t)(((uint32_t)alpha << 24) | ((uint32_t)rgb & 0xFFFFFFu));
        }
    context->sprites[slot].argb = pixels;
    context->sprites[slot].width = sprite->width;
    context->sprites[slot].height = sprite->height;
    RSCache_Dat2SpritePackFree(pack);
    if( !pixels ) return false;
    *out_argb = pixels;
    *out_width = context->sprites[slot].width;
    *out_height = context->sprites[slot].height;
    return true;
}

static int
bake_required_materials(
    struct RSCache_Dat2Disk* disk,
    int sprites_table,
    const struct RSCache_Dat2MaterialTable* materials,
    int* out_count,
    int* out_dependency_count,
    int* out_sprite_dependency_count)
{
    struct bake_context context;
    memset(&context, 0, sizeof(context));
    context.disk = disk;
    context.sprites_table = sprites_table;
    int count = 0;
    for( int source = 0; source < MAX_TEXTURES; source++ )
    {
        if( !g_mapping[source].present ) continue;
        if( source >= materials->count || !materials->materials[source].exists )
        {
            fprintf(stderr, "rs2012_material_bake: material %d has no metadata row\n", source);
            return 0;
        }
        if( !bake_texture(&context, source) )
        {
            fprintf(stderr, "rs2012_material_bake: material %d did not bake\n", source);
            return 0;
        }
        count++;
    }
    int dependencies = 0;
    for( int source = 0; source < MAX_TEXTURES; source++ )
        if( g_textures[source].rendered && !g_mapping[source].present ) dependencies++;
    *out_count = count;
    *out_dependency_count = dependencies;
    *out_sprite_dependency_count = context.sprite_count;
    for( int i = 0; i < context.sprite_count; i++ ) free(context.sprites[i].argb);
    return 1;
}

static int
prepare_config_output(const char* to_tree, const char* name, struct text_output* output)
{
    snprintf(output->path, sizeof(output->path),
             "%s/ported/rs2012_qbd_td/configs/%s", to_tree, name);
    size_t size = 0;
    char* bytes = read_text(output->path, &size);
    if( !bytes ) return 0;
    bool remapped = strncmp(bytes, REMAP_MARKER, strlen(REMAP_MARKER)) == 0;
    struct char_buffer buffer = {0};
    if( !buffer_append(&buffer, REMAP_MARKER "\n", strlen(REMAP_MARKER) + 1) )
    {
        free(bytes);
        return 0;
    }
    const char* cursor = bytes;
    const char* end = bytes + size;
    while( cursor < end )
    {
        const char* newline = memchr(cursor, '\n', (size_t)(end - cursor));
        const char* line_end = newline ? newline : end;
        size_t line_length = (size_t)(line_end - cursor);
        if( remapped && line_length == strlen(REMAP_MARKER) &&
            memcmp(cursor, REMAP_MARKER, line_length) == 0 )
        {
            cursor = newline ? newline + 1 : end;
            continue;
        }
        int value = 0;
        size_t equals = 0;
        if( parse_retexture_line(cursor, line_length, &value, &equals) )
        {
            int source = config_retexture_source(value, remapped, output->path);
            if( source < 0 || !g_mapping[source].present ||
                !buffer_append(&buffer, cursor, equals + 1) ||
                !buffer_append_int(&buffer, g_mapping[source].dest_texture) )
            {
                free(buffer.bytes);
                free(bytes);
                return 0;
            }
        }
        else if( !buffer_append(&buffer, cursor, line_length) )
        {
            free(buffer.bytes);
            free(bytes);
            return 0;
        }
        if( newline && !buffer_append(&buffer, "\n", 1) )
        {
            free(buffer.bytes);
            free(bytes);
            return 0;
        }
        cursor = newline ? newline + 1 : end;
    }
    free(bytes);
    output->bytes = buffer.bytes;
    output->size = buffer.size;
    output->present = true;
    return 1;
}

static int
prepare_config_outputs(const char* to_tree, struct text_output outputs[4])
{
    static const char* names[] = { "rs2012.loc", "rs2012.npc", "rs2012.obj",
                                   "rs2012.spotanim" };
    for( int i = 0; i < 4; i++ )
        if( !prepare_config_output(to_tree, names[i], &outputs[i]) ) return 0;
    return 1;
}

static int
prepare_model_outputs(
    struct RSCache_Dat2Disk* disk,
    int model_table,
    const struct RSCache_Dat2MaterialTable* materials,
    const struct int_list* models,
    struct model_output** out_models)
{
    struct model_output* outputs = calloc((size_t)models->count, sizeof(*outputs));
    if( !outputs ) return 0;
    for( int i = 0; i < models->count; i++ )
    {
        struct RSCache_ModelProvenance* provenance = NULL;
        struct RSCache_Model* model =
            load_model(disk, model_table, models->values[i], &provenance);
        if( !model || !provenance || provenance->format != RSCACHE_MODEL_FORMAT_OB3 )
        {
            fprintf(stderr, "rs2012_material_bake: cannot reopen OB3 model %d\n",
                    models->values[i]);
            RSCache_ModelFree(model);
            RSCache_ModelProvenanceFree(provenance);
            goto fail;
        }
        if( model->face_textures )
            for( int face = 0; face < model->face_count; face++ )
            {
                int source = model->face_textures[face];
                if( source < 0 ) continue;
                if( source >= MAX_TEXTURES || !g_mapping[source].present ||
                    g_mapping[source].dest_texture > INT16_MAX )
                {
                    fprintf(stderr,
                            "rs2012_material_bake: model %d face %d has unmapped material %d\n",
                            models->values[i], face, source);
                    RSCache_ModelFree(model);
                    RSCache_ModelProvenanceFree(provenance);
                    goto fail;
                }
                /* RS727's first material flag is the inverse of the source
                 * client's `isGroundMesh`; it is NOT an isSd/HD-only flag.
                 * MeshRasterizer_Sub1 removes an isGroundMesh selector when
                 * its model-render flags include 0x40. OSRS239 has no matching
                 * procedural-material/model-flag contract, so this lane emits
                 * the compatible OB3 fallback: clear texture, UV, and textured
                 * face-info state together, then light the face's original HSL
                 * colour. Merely writing texture=-1 leaves the face encoded as
                 * textured and produced QBD's mouth-only render.
                 *
                 * OB2 is different: it stores the texture id in its colour
                 * slot, so port_lostcity must synthesize an average colour.
                 * The baked material remains available for inspection and a
                 * future renderer that carries the full 727 contract. */
                /*
                 * A blend layer cannot be expressed as an OSRS239 texture.
                 * Destination textures are colour-keyed - a texel is drawn or
                 * skipped - so a continuous alpha gradient has to be
                 * thresholded, which invents holes the source never had. The
                 * geometry behind then shows through them; on the QBD's neck
                 * that was the striping.
                 *
                 * `valid` does not catch these. The QBD's three materials are
                 * all valid=1 yet contain no fully-opaque texel at all (alpha
                 * 255 covers 0.0% of each), while every genuine diffuse map in
                 * the lane is 100% alpha 255. Both take the flat-colour
                 * fallback, which is the same thing the source client's SD path
                 * does with a material it cannot sample.
                 *
                 * Note this keeps genuine cutouts: their alpha is already 0 or
                 * 255, so they key exactly and are not blend layers.
                 */
                if( g_ground_mesh_fallback &&
                    (!materials->materials[source].valid ||
                     (!g_alpha_textures && g_textures[source].blend_layer)) )
                {
                    if( model->face_infos )
                        model->face_infos[face] =
                            (uint8_t)(model->face_infos[face] & 1);
                    if( model->face_texture_coords )
                        model->face_texture_coords[face] = -1;
                    model->face_textures[face] = (int16_t)-1;
                    g_ground_mesh_model_faces_fallback++;
                }
                else
                    model->face_textures[face] =
                        (int16_t)g_mapping[source].dest_texture;
            }
        uint32_t bound = RSCache_ModelEncodeBound(model, provenance);
        uint8_t* encoded = bound ? malloc(bound) : NULL;
        uint32_t written = encoded ? RSCache_ModelEncodeFormat(
            model, provenance, RSCACHE_MODEL_FORMAT_OB3, encoded, bound) : 0;
        if( !written )
        {
            fprintf(stderr, "rs2012_material_bake: cannot encode remapped model %d\n",
                    models->values[i]);
            free(encoded);
            RSCache_ModelFree(model);
            RSCache_ModelProvenanceFree(provenance);
            goto fail;
        }
        struct RSCache_ModelProvenance* check_provenance = NULL;
        struct RSCache_Model* check = RSCache_ModelNewDecodeProvenance(
            encoded, (int)written, &check_provenance);
        if( !check || !check_provenance || check->face_count != model->face_count ||
            check_provenance->format != RSCACHE_MODEL_FORMAT_OB3 )
        {
            fprintf(stderr, "rs2012_material_bake: remapped model %d failed decode check\n",
                    models->values[i]);
            free(encoded);
            RSCache_ModelFree(check);
            RSCache_ModelProvenanceFree(check_provenance);
            RSCache_ModelFree(model);
            RSCache_ModelProvenanceFree(provenance);
            goto fail;
        }
        for( int face = 0; check->face_textures && face < check->face_count; face++ )
            if( check->face_textures[face] >= 0 &&
                mapping_source_for_dest(check->face_textures[face]) < 0 )
            {
                fprintf(stderr,
                        "rs2012_material_bake: remapped model %d retained texture %d\n",
                        models->values[i], check->face_textures[face]);
                free(encoded);
                RSCache_ModelFree(check);
                RSCache_ModelProvenanceFree(check_provenance);
                RSCache_ModelFree(model);
                RSCache_ModelProvenanceFree(provenance);
                goto fail;
            }
        RSCache_ModelFree(check);
        RSCache_ModelProvenanceFree(check_provenance);
        RSCache_ModelFree(model);
        RSCache_ModelProvenanceFree(provenance);
        outputs[i].source_id = models->values[i];
        outputs[i].bytes = encoded;
        outputs[i].size = written;
    }
    *out_models = outputs;
    return 1;

fail:
    for( int i = 0; i < models->count; i++ ) free(outputs[i].bytes);
    free(outputs);
    return 0;
}

static int
palette_colour(int index)
{
    if( index <= 0 ) return 0;
    int value = index - 1;
    int blue = value % PALETTE_BLUE_LEVELS;
    value /= PALETTE_BLUE_LEVELS;
    int green = value % PALETTE_GREEN_LEVELS;
    int red = value / PALETTE_GREEN_LEVELS;
    int r = (red * 255 + (PALETTE_RED_LEVELS - 1) / 2) / (PALETTE_RED_LEVELS - 1);
    int g = (green * 255 + (PALETTE_GREEN_LEVELS - 1) / 2) / (PALETTE_GREEN_LEVELS - 1);
    int b = (blue * 255 + (PALETTE_BLUE_LEVELS - 1) / 2) / (PALETTE_BLUE_LEVELS - 1);
    int rgb = (r << 16) | (g << 8) | b;
    return rgb ? rgb : 1;
}

/* --alpha-report: where the source alpha actually sits, per material.
 * The destination has colour-key transparency, not alpha, so the shape of this
 * histogram decides the right conversion: alpha that is almost all 0 or 255 is
 * a genuine cutout and keys cleanly, while alpha spread through the middle is
 * a blend the key cannot represent and has to be composited instead. */
static bool g_alpha_report = false;



static void
alpha_report(int source)
{
    struct texture_entry* entry = &g_textures[source];
    int zero = 0, low = 0, high = 0, full = 0;

    if( !entry->argb )
        return;
    for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
    {
        int alpha = (int)(((uint32_t)entry->argb[i]) >> 24);
        if( alpha == 0 )
            zero++;
        else if( alpha < 128 )
            low++;
        else if( alpha < 255 )
            high++;
        else
            full++;
    }
    printf(
        "  material %-5d alpha: 0=%5.1f%%  1-127=%5.1f%%  128-254=%5.1f%%  255=%5.1f%%\n",
        source,
        100.0 * zero / (BAKE_SIZE * BAKE_SIZE),
        100.0 * low / (BAKE_SIZE * BAKE_SIZE),
        100.0 * high / (BAKE_SIZE * BAKE_SIZE),
        100.0 * full / (BAKE_SIZE * BAKE_SIZE));
}

static int
quantize_texture(int source, int32_t* pixels)
{
    if( g_alpha_report )
        alpha_report(source);
    struct texture_entry* entry = &g_textures[source];
    if( !entry->argb ) return 0;
    entry->transparent = false;

    /* A diffuse map or a cutout has fully-opaque texels; a blend layer has
     * none. See struct texture_entry::blend_layer. */
    {
        int opaque = 0;
        for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
            if( (((uint32_t)entry->argb[i]) >> 24) == 255 )
                opaque++;
        entry->blend_layer = opaque * 2 < BAKE_SIZE * BAKE_SIZE;
    }

    for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
    {
        uint32_t argb = (uint32_t)entry->argb[i];
        int alpha = (int)(argb >> 24);
        /* A blend layer is emitted with its coverage intact - thresholding is
         * exactly what destroys it - so only a fully clear texel drops out. */
        if( (g_alpha_textures && entry->blend_layer) ? (alpha == 0) : (alpha < 128) )
        {
            if( pixels ) pixels[i] = 0;
            entry->transparent = true;
            continue;
        }
        int red = (int)((argb >> 16) & 0xFF);
        int green = (int)((argb >> 8) & 0xFF);
        int blue = (int)(argb & 0xFF);
        int r = (red * (PALETTE_RED_LEVELS - 1) + 127) / 255;
        int g = (green * (PALETTE_GREEN_LEVELS - 1) + 127) / 255;
        int b = (blue * (PALETTE_BLUE_LEVELS - 1) + 127) / 255;
        int index = 1 + (r * PALETTE_GREEN_LEVELS + g) * PALETTE_BLUE_LEVELS + b;
        int rgb = palette_colour(index);
        uint32_t out_alpha =
            (g_alpha_textures && entry->blend_layer) ? (uint32_t)alpha : 0xFFu;
        if( pixels ) pixels[i] = (int32_t)((out_alpha << 24) | (uint32_t)rgb);
    }
    return 1;
}

static int
quantize_required_materials(void)
{
    for( int source = 0; source < MAX_TEXTURES; source++ )
        if( g_mapping[source].present && !quantize_texture(source, NULL) ) return 0;
    return 1;
}

static int
validate_destination_sprite_ids(const char* to_tree)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/pack/8_sprites.pack", to_tree);
    FILE* file = fopen(path, "rb");
    if( !file ) return 0;
    char line[4096];
    int line_number = 0;
    while( fgets(line, sizeof(line), file) )
    {
        line_number++;
        char* text = trim(line);
        if( !*text || *text == '#' || *text == ';' || !isdigit((unsigned char)*text) ) continue;
        char* equals = strchr(text, '=');
        if( !equals )
        {
            fclose(file);
            return 0;
        }
        *equals = '\0';
        char* end = NULL;
        long id = strtol(text, &end, 10);
        if( !end || *trim(end) || id < 0 || id > 65535 )
        {
            fprintf(stderr, "rs2012_material_bake: malformed sprite pack row %d\n", line_number);
            fclose(file);
            return 0;
        }
        for( int source = 0; source < MAX_TEXTURES; source++ )
            if( g_mapping[source].present && g_mapping[source].dest_sprite == id )
            {
                fprintf(stderr,
                        "rs2012_material_bake: destination sprite %ld already exists in base\n",
                        id);
                fclose(file);
                return 0;
            }
    }
    int ok = !ferror(file);
    fclose(file);
    return ok;
}

static void
material_animation(
    const struct RSCache_Dat2Material* material,
    int* out_direction,
    int* out_speed)
{
    int u = material->anim_u;
    int v = material->anim_v;
    int abs_u = u < 0 ? -u : u;
    int abs_v = v < 0 ? -v : v;
    int direction = 0;
    int speed = 0;
    if( abs_u >= abs_v && u )
    {
        direction = u < 0 ? RSCACHE_TEXTURE_DIRECTION_U_DOWN : RSCACHE_TEXTURE_DIRECTION_U_UP;
        speed = abs_u;
    }
    else if( v )
    {
        direction = v < 0 ? RSCACHE_TEXTURE_DIRECTION_V_DOWN : RSCACHE_TEXTURE_DIRECTION_V_UP;
        speed = abs_v;
    }
    *out_direction = direction;
    *out_speed = speed;
}

static void
append_flag(char* output, size_t capacity, const char* flag)
{
    size_t length = strlen(output);
    if( length && length + 1 < capacity ) output[length++] = ';';
    if( length < capacity ) snprintf(output + length, capacity - length, "%s", flag);
}

static void
material_approximation(
    int source,
    const struct RSCache_Dat2Material* material,
    char* output,
    size_t capacity)
{
    const struct RSCache_Dat2ProcTexture* program = g_textures[source].program;
    output[0] = '\0';
    append_flag(output, capacity, "procedural-frame-baked-128x128");
    append_flag(output, capacity, "palette-6x7x6");
    append_flag(output, capacity, "runtime-gamma-0.8");
    if( g_textures[source].source_transparent )
        append_flag(output, capacity, "alpha-threshold-128");
    if( material->anim_u && material->anim_v )
        append_flag(output, capacity, "dual-axis-animation-dominant-axis-only");
    if( !material->valid )
        append_flag(output, capacity, "hd-only-asset-retained;sd-model-fallback");
    if( !program->repeat_s || !program->repeat_t )
        append_flag(output, capacity, "repeat-clamp-semantics-baked-frame-only");
    if( material->mipmap ) append_flag(output, capacity, "mipmap-metadata-only");
    if( material->float_texture ) append_flag(output, capacity, "float-metadata-only");
    if( material->shader_id || material->shader_param || material->shader_param2 )
        append_flag(output, capacity, "shader-metadata-only");
    if( material->combine_mode != program->combine_mode )
        append_flag(output, capacity, "material-combine-metadata-only");
    if( material->brightness ) append_flag(output, capacity, "material-brightness-metadata-only");
    if( material->blanch ) append_flag(output, capacity, "material-blanch-metadata-only");
    if( program->trailing_flags || program->trailing_count || program->trailing_bool )
        append_flag(output, capacity, "unverified-program-tail-preserved-as-metadata");
}

static void
reason_text(int source, char* output, size_t capacity)
{
    output[0] = '\0';
    if( g_reasons[source] & REASON_MODEL ) append_flag(output, capacity, "model");
    if( g_reasons[source] & REASON_RETEXTURE ) append_flag(output, capacity, "retexture");
    if( g_reasons[source] & REASON_OVERLAY ) append_flag(output, capacity, "overlay");
    if( g_reasons[source] & REASON_UNDERLAY ) append_flag(output, capacity, "underlay");
    if( !g_reasons[source] ) append_flag(output, capacity, "retained");
}

static int
write_material_ledger(
    const char* path,
    const struct RSCache_Dat2MaterialTable* materials)
{
    if( !ensure_parent(path) ) return 0;
    FILE* file = fopen(path, "wb");
    if( !file ) return 0;
    fprintf(file,
            "source_material\tdest_texture\tdest_sprite\treasons\taverage_hsl\t"
            "valid\talpha\tsmall\tdisabled\tbrightness\tblanch\tshader_id\t"
            "shader_param\tmaterial_anim_u\tmaterial_anim_v\tmaterial_flip_v\t"
            "mipmap\tmaterial_repeat_s\tmaterial_repeat_t\tfloat_texture\t"
            "material_combine_mode\tshader_param2\talpha_mode\tprogram_anim_u\t"
            "program_anim_v\tprogram_flip_v\tprogram_repeat_s\tprogram_repeat_t\t"
            "program_combine_mode\tprogram_trailing_flags\tprogram_trailing_count\t"
            "program_trailing_bool\tsource_transparent\tbaked_transparent\t"
            "dest_animation_direction\tdest_animation_speed\tapproximation\n");
    for( int source = 0; source < MAX_TEXTURES; source++ )
    {
        if( !g_mapping[source].present ) continue;
        const struct RSCache_Dat2Material* material = &materials->materials[source];
        const struct RSCache_Dat2ProcTexture* program = g_textures[source].program;
        char reasons[128], approximation[1024];
        int direction = 0, speed = 0;
        reason_text(source, reasons, sizeof(reasons));
        material_approximation(source, material, approximation, sizeof(approximation));
        material_animation(material, &direction, &speed);
        fprintf(file,
                "%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%s\n",
                source, g_mapping[source].dest_texture, g_mapping[source].dest_sprite,
                reasons, material->average_hsl, material->valid, material->alpha,
                material->small, material->disabled, material->brightness, material->blanch,
                material->shader_id, material->shader_param, material->anim_u,
                material->anim_v, material->flip_v, material->mipmap, material->repeat_s,
                material->repeat_t, material->float_texture, material->combine_mode,
                material->shader_param2, material->alpha_mode, program->anim_u,
                program->anim_v, program->flip_v, program->repeat_s, program->repeat_t,
                program->combine_mode, program->trailing_flags, program->trailing_count,
                program->trailing_bool, g_textures[source].source_transparent,
                g_textures[source].transparent, direction, speed, approximation);
    }
    int ok = !ferror(file) && fclose(file) == 0;
    return ok;
}

static int
write_sprite_asset(const char* to_tree, int source)
{
    char directory[2048], path[2300];
    snprintf(directory, sizeof(directory),
             "%s/sprites/ported/rs2012_qbd_td/rs2012_material_%d", to_tree, source);
    if( !mkdir_p(directory) ) return 0;
    int32_t* pixels = malloc((size_t)BAKE_SIZE * BAKE_SIZE * sizeof(*pixels));
    if( !pixels || !quantize_texture(source, pixels) )
    {
        free(pixels);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/0.bmp", directory);
    bmp_write_file(path, (int*)pixels, BAKE_SIZE, BAKE_SIZE);
    free(pixels);
    FILE* check = fopen(path, "rb");
    if( !check ) return 0;
    fclose(check);

    snprintf(path, sizeof(path), "%s/pack.meta", directory);
    FILE* meta = fopen(path, "wb");
    if( !meta ) return 0;
    fprintf(meta, "// RS727 procedural material %d baked for OSRS239.\n", source);
    fprintf(meta, "count=1\npalette=%d\n", PALETTE_LENGTH);
    for( int index = 0; index < PALETTE_LENGTH; index++ )
        fprintf(meta, "p%d=0x%06X\n", index, palette_colour(index));
    fprintf(meta, "sprite0=%d,%d,%d,%d,0,0\n", BAKE_SIZE, BAKE_SIZE,
            BAKE_SIZE, BAKE_SIZE);
    return !ferror(meta) && fclose(meta) == 0;
}

static int
write_sprite_pack(const char* to_tree)
{
    char path[2048];
    snprintf(path, sizeof(path),
             "%s/ported/rs2012_qbd_td/pack/8_sprites.pack", to_tree);
    char** preserved = calloc(65536, sizeof(*preserved));
    if( !preserved ) return 0;
    FILE* existing = fopen(path, "rb");
    if( existing )
    {
        char line[4096];
        int ok = 1;
        while( ok && fgets(line, sizeof(line), existing) )
        {
            char* text = trim(line);
            if( !*text || *text == '#' || *text == ';' ) continue;
            char* equals = strchr(text, '=');
            if( !equals ) { ok = 0; break; }
            *equals = '\0';
            char* end = NULL;
            long id = strtol(text, &end, 10);
            char* name = trim(equals + 1);
            if( !end || *trim(end) || id < 0 || id > 65535 || !*name )
            { ok = 0; break; }
            if( strstr(name, "ported/rs2012_qbd_td/rs2012_material_") == name )
                continue;
            if( preserved[id] ) { ok = 0; break; }
            preserved[id] = strdup(name);
            if( !preserved[id] ) ok = 0;
        }
        if( ferror(existing) ) ok = 0;
        fclose(existing);
        if( !ok )
        {
            for( int id = 0; id <= 65535; id++ ) free(preserved[id]);
            free(preserved);
            fprintf(stderr, "rs2012_material_bake: malformed existing lane sprite pack\n");
            return 0;
        }
    }
    for( int source = 0; source < MAX_TEXTURES; source++ )
        if( g_mapping[source].present && preserved[g_mapping[source].dest_sprite] )
        {
            fprintf(stderr,
                    "rs2012_material_bake: destination sprite %d collides with retained lane row\n",
                    g_mapping[source].dest_sprite);
            for( int id = 0; id <= 65535; id++ ) free(preserved[id]);
            free(preserved);
            return 0;
        }
    if( !ensure_parent(path) )
    {
        for( int id = 0; id <= 65535; id++ ) free(preserved[id]);
        free(preserved);
        return 0;
    }
    FILE* file = fopen(path, "wb");
    if( !file )
    {
        for( int id = 0; id <= 65535; id++ ) free(preserved[id]);
        free(preserved);
        return 0;
    }
    for( int destination = 0; destination <= 65535; destination++ )
    {
        if( preserved[destination] )
            fprintf(file, "%d=%s\n", destination, preserved[destination]);
        for( int source = 0; source < MAX_TEXTURES; source++ )
            if( g_mapping[source].present && g_mapping[source].dest_sprite == destination )
                fprintf(file, "%d=ported/rs2012_qbd_td/rs2012_material_%d\n",
                        destination, source);
    }
    int ok = !ferror(file) && fclose(file) == 0;
    for( int id = 0; id <= 65535; id++ ) free(preserved[id]);
    free(preserved);
    return ok;
}

static int
write_texture_sources(
    const char* to_tree,
    const struct RSCache_Dat2MaterialTable* materials)
{
    char directory[2048], text_path[2200], index_path[2200], pack_path[2200];
    snprintf(directory, sizeof(directory), "%s/ported/rs2012_qbd_td/textures", to_tree);
    if( !mkdir_p(directory) ) return 0;
    snprintf(text_path, sizeof(text_path), "%s/texture_0.texture", directory);
    snprintf(index_path, sizeof(index_path), "%s/texture_0.compack", directory);
    FILE* text_file = fopen(text_path, "wb");
    FILE* index_file = fopen(index_path, "wb");
    if( !text_file || !index_file )
    {
        if( text_file ) fclose(text_file);
        if( index_file ) fclose(index_file);
        return 0;
    }
    for( int destination = DEST_TEXTURE_BASE; destination <= 65535; destination++ )
        for( int source = 0; source < MAX_TEXTURES; source++ )
        {
            if( !g_mapping[source].present ||
                g_mapping[source].dest_texture != destination ) continue;
            const struct RSCache_Dat2Material* material = &materials->materials[source];
            int direction = 0, speed = 0;
            material_animation(material, &direction, &speed);
            fprintf(index_file, "%d=rs2012_material_%d\n", destination, source);
            fprintf(text_file, "[rs2012_material_%d]\n", source);
            fprintf(text_file, "averagehsl=%d\n", material->average_hsl);
            if( !g_textures[source].transparent ) fprintf(text_file, "opaque=yes\n");
            /* A blend layer keeps its coverage instead of being colour-keyed;
             * the raster blends it per texel. See
             * ToriDraw_Texture::alpha_blended. */
            if( g_alpha_textures && g_textures[source].blend_layer )
                fprintf(text_file, "alpha=yes\n");
            fprintf(text_file, "sprite1=%d,0,0\n", g_mapping[source].dest_sprite);
            if( direction ) fprintf(text_file, "direction=%d\n", direction);
            if( speed ) fprintf(text_file, "speed=%d\n", speed);
            fputc('\n', text_file);
        }
    int ok = !ferror(text_file) && !ferror(index_file) &&
             fclose(text_file) == 0 && fclose(index_file) == 0;
    snprintf(pack_path, sizeof(pack_path),
             "%s/ported/rs2012_qbd_td/pack/9_textures.pack", to_tree);
    ok = ok && write_bytes(pack_path, "0=texture_0\n", strlen("0=texture_0\n"));
    return ok;
}

static int
write_model_outputs(
    const char* to_tree,
    const struct model_output* outputs,
    int count)
{
    for( int i = 0; i < count; i++ )
    {
        char path[2048];
        snprintf(path, sizeof(path),
                 "%s/models/ported/rs2012_qbd_td/rs2012_model_%d.ob3",
                 to_tree, outputs[i].source_id);
        if( !write_bytes(path, outputs[i].bytes, outputs[i].size) ) return 0;
    }
    return 1;
}

static int
write_config_outputs(const struct text_output outputs[4])
{
    for( int i = 0; i < 4; i++ )
        if( outputs[i].present && !write_bytes(outputs[i].path, outputs[i].bytes, outputs[i].size) )
            return 0;
    return 1;
}

static void
free_all(
    struct RSCache_Dat2Disk* disk,
    struct RSCache_Dat2MaterialTable* materials,
    struct int_list* models,
    struct model_output* model_outputs,
    struct text_output config_outputs[4])
{
    if( model_outputs )
        for( int i = 0; i < models->count; i++ ) free(model_outputs[i].bytes);
    free(model_outputs);
    for( int i = 0; i < 4; i++ ) free(config_outputs[i].bytes);
    free(models->values);
    RSCache_Dat2MaterialTableFree(materials);
    for( int source = 0; source < MAX_TEXTURES; source++ )
    {
        free(g_textures[source].argb);
        RSCache_Dat2ProcTextureFree(g_textures[source].program);
    }
    RSCache_Dat2DiskFree(disk);
}

static void
usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [--cache DIR] [--tree DIR] [--apply]\n"
            "Defaults: cache.rs727_preeoc and OSRS-Content/osrs239-content.\n",
            argv0);
}

int
main(int argc, char** argv)
{
    const char* cache_path = "cache.rs727_preeoc";
    const char* to_tree = "OSRS-Content/osrs239-content";
    bool apply = false;
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc ) cache_path = argv[++i];
        else if( strcmp(argv[i], "--tree") == 0 && i + 1 < argc ) to_tree = argv[++i];
        else if( strcmp(argv[i], "--apply") == 0 ) apply = true;
        else if( strcmp(argv[i], "--no-ground-mesh-fallback") == 0 )
            g_ground_mesh_fallback = false;
        else if( strcmp(argv[i], "--alpha-report") == 0 )
            g_alpha_report = true;
        else if( strcmp(argv[i], "--alpha-textures") == 0 )
            g_alpha_textures = true;
        else if( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    struct RSCache profile;
    if( !RSCache_ProfileByName("rs727", &profile) )
    {
        fprintf(stderr, "rs2012_material_bake: rs727 profile is unavailable\n");
        return 1;
    }
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_path);
    if( !disk )
    {
        fprintf(stderr, "rs2012_material_bake: cannot open source cache %s\n", cache_path);
        return 1;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    int model_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MODELS);
    int texture_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_TEXTURES);
    int material_table_id = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MATERIALS);
    int sprite_table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SPRITES);
    if( model_table < 0 || texture_table < 0 || material_table_id < 0 || sprite_table < 0 )
    {
        fprintf(stderr, "rs2012_material_bake: source cache lacks a required table\n");
        RSCache_Dat2DiskFree(disk);
        return 1;
    }

    char model_pack_path[2048], ledger_path[2048];
    snprintf(model_pack_path, sizeof(model_pack_path),
             "%s/ported/rs2012_qbd_td/pack/7_models.pack", to_tree);
    snprintf(ledger_path, sizeof(ledger_path),
             "%s/port/rs2012_qbd_td.materials.tsv", to_tree);
    struct int_list models = {0};
    struct model_output* model_outputs = NULL;
    struct text_output config_outputs[4] = {{0}};
    struct RSCache_Dat2MaterialTable* materials = NULL;
    uint32_t flags = RSCache_Dat2ProcTextureFlags(&profile);
    int ok = load_material_ledger(ledger_path) && parse_model_pack(model_pack_path, &models);
    if( ok ) list_sort(&models);
    if( ok ) ok = collect_model_materials(disk, model_table, &models);
    if( ok ) ok = collect_config_retextures(to_tree);
    if( ok ) ok = allocate_material_mappings();
    if( ok ) ok = validate_destination_sprite_ids(to_tree);
    if( ok ) ok = load_texture_programs(disk, texture_table, flags);
    if( ok ) materials = load_material_table(disk, material_table_id, flags);
    if( ok && !materials ) ok = 0;

    int baked_count = 0, dependency_count = 0, sprite_dependency_count = 0;
    if( ok ) ok = bake_required_materials(
        disk, sprite_table, materials, &baked_count, &dependency_count,
        &sprite_dependency_count);
    if( ok ) ok = quantize_required_materials();
    if( ok ) ok = prepare_model_outputs(
        disk, model_table, materials, &models, &model_outputs);
    if( ok ) ok = prepare_config_outputs(to_tree, config_outputs);

    int mappings = 0, model_materials = 0, retextures = 0, ground_mesh = 0;
    int animated = 0, dual_axis = 0, source_transparent = 0, baked_transparent = 0;
    if( ok )
        for( int source = 0; source < MAX_TEXTURES; source++ )
        {
            if( !g_mapping[source].present ) continue;
            const struct RSCache_Dat2Material* material = &materials->materials[source];
            mappings++;
            if( g_reasons[source] & REASON_MODEL ) model_materials++;
            if( g_reasons[source] & REASON_RETEXTURE ) retextures++;
            if( !material->valid ) ground_mesh++;
            if( material->anim_u || material->anim_v ) animated++;
            if( material->anim_u && material->anim_v ) dual_axis++;
            if( g_textures[source].source_transparent ) source_transparent++;
            if( g_textures[source].transparent ) baked_transparent++;
        }

    if( ok )
    {
        printf("rs2012_material_bake (%s): models=%d materials=%d model_materials=%d "
               "retexture_materials=%d transitive_programs=%d source_sprites=%d\n",
               apply ? "apply" : "dry-run", models.count, mappings, model_materials,
               retextures, dependency_count, sprite_dependency_count);
        printf("  baked=%d ground_mesh=%d animated=%d dual_axis=%d "
               "source_transparent=%d threshold_transparent=%d "
               "ground_mesh_fallback_faces=%d\n",
               baked_count, ground_mesh, animated, dual_axis, source_transparent,
               baked_transparent, g_ground_mesh_model_faces_fallback);
        printf("  reserved overlays: 348->211 408->212 600->213 616->214 651->215\n");
    }

    if( ok && apply )
    {
        for( int source = 0; ok && source < MAX_TEXTURES; source++ )
            if( g_mapping[source].present ) ok = write_sprite_asset(to_tree, source);
        if( ok ) ok = write_sprite_pack(to_tree);
        if( ok ) ok = write_texture_sources(to_tree, materials);
        if( ok ) ok = write_material_ledger(ledger_path, materials);
        if( ok ) ok = write_model_outputs(to_tree, model_outputs, models.count);
        if( ok ) ok = write_config_outputs(config_outputs);
    }
    if( !ok ) fprintf(stderr, "rs2012_material_bake: failed; no fidelity claim made\n");
    free_all(disk, materials, &models, model_outputs, config_outputs);
    return ok ? 0 : 1;
}
