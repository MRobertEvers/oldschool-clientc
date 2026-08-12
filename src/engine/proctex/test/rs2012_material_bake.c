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
#include <math.h>
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

    /*
     * True when the baked image carries no colour of its own - a greyscale
     * detail map whose surface colour belongs to the face that references it.
     *
     * This is what the RS727 materials on this lane overwhelmingly are: 225 of
     * 256 average under 30 levels of spread between their brightest and
     * darkest channel, against a separate cluster of genuinely coloured maps
     * (ground overlays and the like) up at 90+. Rendering one literally paints
     * the surface grey or, for the bright ones, white - which is exactly the
     * "blown-out white shards" that referencing the valid=0 materials produced
     * before there was a kernel that could multiply them by the face colour.
     *
     * Distinct from blend_layer: that is about coverage (does the alpha vary),
     * this is about colour (does the RGB mean anything). A material can be
     * either, both or neither.
     */
    bool greyscale;
};

/*
 * Mean spread between the brightest and darkest channel, below which a baked
 * material is treated as carrying no colour of its own. The measured
 * distribution is bimodal either side of this; see texture_entry::greyscale.
 */
#define GREYSCALE_CHROMA_LIMIT 30

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
 * Per-face colour bake for the faces the fallback erases.
 *
 * The fallback is right that an HD program is not a texture the SD path can
 * sample - but its baked frame still knows how the surface varied in space.
 * This mode recovers the DC term of that variation per face, in OSRS's native
 * idiom: sample the un-quantised ARGB frame over the face's own UV footprint,
 * alpha-weight the mean (a blend layer's coverage IS its alpha - nothing is
 * thresholded), composite it over the face's original HSL, and only then
 * erase the texture state exactly as the fallback always has.
 *
 * Two composites, chosen per run because the right op depends on what the HD
 * program was, and that is judged from renders, not assumed:
 *
 *   tint      out = mix(base, footprint_mean, coverage * strength)
 *             absolute - faces take the material's colour where it has
 *             coverage. This is what a diffuse-like map wants.
 *   modulate  out = base * mix(1, footprint_mean / material_mean, coverage * strength)
 *             relative - the model keeps its authored tone and gains only the
 *             frame's spatial variation. This is the safe op for the
 *             greyscale effect programs whose absolute level is meaningless
 *             (the "blown-out white" family).
 */
enum face_bake_mode
{
    FACE_BAKE_OFF,
    FACE_BAKE_TINT,
    FACE_BAKE_MODULATE,
};
/* Modulate + alpha bake is the lane's default backport: judged against the
 * HD reference it is the only composite that keeps authored color (every QBD
 * material is a greyscale detail map) while making mostly-transparent
 * membrane materials translucent instead of opaque slabs.
 * --face-color-bake off restores the bare erase-only fallback. */
static enum face_bake_mode g_face_bake_mode = FACE_BAKE_MODULATE;
static int g_face_bake_strength = 100;
static const char* g_models_out_dir = NULL;
static int g_face_bake_faces = 0;
static int g_face_bake_degenerate = 0;
static int g_face_bake_uncovered = 0;
/* --face-bake-debug MODEL: per-material sampling census for one source model,
 * printed while its faces bake. Diagnosis only. */
static int g_face_bake_debug_model = -1;
static int g_face_bake_current_model = -1;

/* --face-dump: one CSV row per erased-material face, written after the alpha
 * gates have run so `final` is what actually ships (-1 = face dropped). */
static FILE* g_face_dump = NULL;
/* --frame-dump DIR: write every mapped material's baked 128x128 frame as
 * mat_N.ppm (RGB) + mat_N_a.pgm (alpha). Diagnosis only. */
static const char* g_frame_dump_dir = NULL;

/* The material table, visible to the face bake so the alpha kernel can read
 * alpha_mode; set once in prepare_model_outputs. */
static const struct RSCache_Dat2MaterialTable* g_materials = NULL;
/* why face_bake_corner_uvs said no, latest call */
enum uv_fail
{
    UV_OK,
    UV_BAD_INDEX,
    UV_ZERO_AREA,
};
static enum uv_fail g_face_bake_uv_fail;
struct face_bake_debug_row
{
    int faces;
    int degenerate;
    int bad_index;
    int zero_area;
    int texco_own;
    double coverage_sum;
};
static struct face_bake_debug_row g_face_bake_debug[MAX_TEXTURES];

/*
 * Carry the frame's coverage into face_alphas as well (on by default; disable
 * with --no-face-alpha-bake). A membrane program whose bake is mostly clear
 * was never an opaque surface; leaving its faces solid is what turns the HD
 * crest's torn translucent frill into an opaque slab. Composes
 * multiplicatively with authored alpha.
 */
static bool g_face_alpha_bake = true;
static int g_face_bake_alpha_faces = 0;
/* Per-face drop marks for the model currently in prepare_model_outputs;
 * non-NULL only while the alpha bake is on. Faces whose composed opacity
 * lands at zero are removed from the model instead of shipped invisible. */
static uint8_t* g_face_drop_marks = NULL;
static int g_face_drop_marks_size = 0;
static int g_face_bake_dropped_faces = 0;

/* Lazy per-material alpha-weighted mean of the whole baked frame; the
 * normaliser for the modulate composite. hard_frac is the fraction of covered
 * texels (a > 0.1) whose alpha is essentially solid (a > 0.9) — the wisp
 * inference below needs to know a frame has no hard silhouette at all. */
static struct face_bake_mean
{
    bool computed;
    double r, g, b;
    double coverage;
    double hard_frac;
} g_face_bake_mean[MAX_TEXTURES];

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
    int authored = 0;
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
        /*
         * `rs2012_model_<id>_<suffix>` is a hand-authored asset that happens to
         * live in the lane's pack, not a model imported from the RS727 cache.
         * It has no source model to re-read and no source materials to remap,
         * so it is skipped rather than rejected - failing on it would mean the
         * bake could not run at all once anyone authored a lane model.
         */
        if( !errno && end && *end == '_' && source >= 0 && source <= INT_MAX )
        {
            authored++;
            continue;
        }
        if( errno || !end || *end || source < 0 || source > INT_MAX ||
            !list_add_unique(models, (int)source) )
        {
            fprintf(stderr, "rs2012_material_bake: malformed model row %s\n", text);
            ok = 0;
        }
    }
    if( ok && authored )
        printf("  skipped %d authored lane model(s) with no RS727 source\n", authored);
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
    static bool used_textures[65536];
    static bool used_sprites[65536];
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

/*
 * RGB <-> HSL16 for the face-colour bake. Same math as the destination
 * client's palette (toridraw/osrs/palette.c), duplicated here so the bake
 * keeps its link set; the inverse hits band midpoints so a round trip is
 * tone-neutral. No brightness curve on either leg - the renderer applies its
 * own gamma to old and new colours alike.
 */
static int
face_bake_rgb_to_hsl16(int rgb)
{
    double r = (double)((rgb >> 16) & 255) / 256.0;
    double g = (double)((rgb >> 8) & 255) / 256.0;
    double b = (double)(rgb & 255) / 256.0;
    double min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double hue = 0.0;
    double saturation = 0.0;
    double lightness = (max + min) / 2.0;
    if( min != max )
    {
        if( lightness < 0.5 )
            saturation = (max - min) / (max + min);
        else
            saturation = (max - min) / (2.0 - max - min);
        if( r == max )
            hue = (g - b) / (max - min);
        else if( g == max )
            hue = 2.0 + (b - r) / (max - min);
        else
            hue = 4.0 + (r - g) / (max - min);
    }
    hue /= 6.0;
    if( hue < 0.0 )
        hue += 1.0;
    int hue_int = (int)(hue * 256.0);
    int sat_int = (int)(saturation * 256.0);
    int light_int = (int)(lightness * 256.0);
    if( sat_int < 0 )
        sat_int = 0;
    else if( sat_int > 255 )
        sat_int = 255;
    if( light_int < 0 )
        light_int = 0;
    else if( light_int > 255 )
        light_int = 255;
    if( light_int > 179 )
        sat_int >>= 1;
    if( light_int > 192 )
        sat_int >>= 1;
    if( light_int > 217 )
        sat_int >>= 1;
    if( light_int > 243 )
        sat_int >>= 1;
    return ((hue_int / 4) << 10) + ((sat_int / 32) << 7) + (light_int / 2);
}

static void
face_bake_hsl16_to_rgb(int hsl16, double* out_r, double* out_g, double* out_b)
{
    double h = ((double)((hsl16 >> 10) & 63) * 4.0 + 2.0) / 256.0;
    double s = ((double)((hsl16 >> 7) & 7) * 32.0 + 16.0) / 256.0;
    double l = ((double)(hsl16 & 127) * 2.0 + 1.0) / 256.0;
    double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    double p = 2.0 * l - q;
    double t[3] = { h + 1.0 / 3.0, h, h - 1.0 / 3.0 };
    double rgb[3];
    for( int i = 0; i < 3; i++ )
    {
        double x = t[i];
        if( x < 0.0 )
            x += 1.0;
        if( x > 1.0 )
            x -= 1.0;
        if( x < 1.0 / 6.0 )
            rgb[i] = p + (q - p) * 6.0 * x;
        else if( x < 0.5 )
            rgb[i] = q;
        else if( x < 2.0 / 3.0 )
            rgb[i] = p + (q - p) * (2.0 / 3.0 - x) * 6.0;
        else
            rgb[i] = p;
    }
    *out_r = rgb[0] * 255.0;
    *out_g = rgb[1] * 255.0;
    *out_b = rgb[2] * 255.0;
}

/*
 * Model-space affine PMN -> UV: solve X - P = u*(M-P) + v*(N-P) via the 2x2
 * Gram system. Exact for texture_render_type 0, the fixed projector every
 * face on this lane uses; a footprint mean does not need the projective
 * correction the rasteriser applies per pixel.
 */
static bool
face_bake_corner_uvs(
    const struct RSCache_Model* model,
    int face,
    double us[3],
    double vs[3])
{
    int a = model->face_indices_a[face];
    int b = model->face_indices_b[face];
    int c = model->face_indices_c[face];
    int p = a, m = b, n = c;
    g_face_bake_uv_fail = UV_OK;
    if( model->face_texture_coords && model->face_texture_coords[face] >= 0 )
    {
        int ti = model->face_texture_coords[face];
        if( ti >= model->textured_face_count )
        {
            g_face_bake_uv_fail = UV_BAD_INDEX;
            return false;
        }
        p = model->textured_p_coordinate[ti];
        m = model->textured_m_coordinate[ti];
        n = model->textured_n_coordinate[ti];
        if( p >= model->vertex_count || m >= model->vertex_count ||
            n >= model->vertex_count )
        {
            g_face_bake_uv_fail = UV_BAD_INDEX;
            return false;
        }
    }
    double px = model->vertices_x[p], py = model->vertices_y[p],
           pz = model->vertices_z[p];
    double ux = model->vertices_x[m] - px, uy = model->vertices_y[m] - py,
           uz = model->vertices_z[m] - pz;
    double vx = model->vertices_x[n] - px, vy = model->vertices_y[n] - py,
           vz = model->vertices_z[n] - pz;
    double uu = ux * ux + uy * uy + uz * uz;
    double vv = vx * vx + vy * vy + vz * vz;
    double uv = ux * vx + uy * vy + uz * vz;
    double det = uu * vv - uv * uv;
    if( det < 1e-9 && det > -1e-9 )
    {
        g_face_bake_uv_fail = UV_ZERO_AREA;
        return false;
    }
    int corners[3] = { a, b, c };
    for( int i = 0; i < 3; i++ )
    {
        double wx = model->vertices_x[corners[i]] - px;
        double wy = model->vertices_y[corners[i]] - py;
        double wz = model->vertices_z[corners[i]] - pz;
        double wu = wx * ux + wy * uy + wz * uz;
        double wv = wx * vx + wy * vy + wz * vz;
        us[i] = (wu * vv - wv * uv) / det;
        vs[i] = (wv * uu - wu * uv) / det;
    }
    return true;
}

static const struct face_bake_mean*
face_bake_material_mean(int source)
{
    struct face_bake_mean* mean = &g_face_bake_mean[source];
    if( mean->computed )
        return mean;
    mean->computed = true;
    const int32_t* argb = g_textures[source].argb;
    double sw = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
    int covered = 0, hard = 0;
    for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
    {
        uint32_t px = (uint32_t)argb[i];
        double w = (double)(px >> 24) / 255.0;
        sw += w;
        sr += w * (double)((px >> 16) & 0xFF);
        sg += w * (double)((px >> 8) & 0xFF);
        sb += w * (double)(px & 0xFF);
        if( w > 0.1 )
        {
            covered++;
            if( w > 0.9 )
                hard++;
        }
    }
    mean->coverage = sw / (BAKE_SIZE * BAKE_SIZE);
    mean->r = sw > 0.0 ? sr / sw : 0.0;
    mean->g = sw > 0.0 ? sg / sw : 0.0;
    mean->b = sw > 0.0 ? sb / sw : 0.0;
    mean->hard_frac = covered > 0 ? (double)hard / covered : 0.0;
    return mean;
}

/* Wisp inference for blend layers whose material row is invalid and whose
 * alpha_mode says opaque (mode 0) — for these the mode is contradicted by the
 * frame itself. An invalid row has no layer stack for its alpha to mask, so
 * the layer's only compositing target is the framebuffer — and a frame that
 * covers almost nothing with no hard silhouette anywhere (the QBD's dorsal
 * filaments, the smoke-effect models) cannot have been an opaque surface in
 * HD. Treat it as screen-blend.
 * The thresholds split the observed lane cleanly: wisps bake at 0.02-0.11
 * coverage with zero hard texels; the solid invalid-row details (collar
 * chips 0.22, speckle 0.17, scale web 0.34) all sit above. */
#define WISP_MAX_COVERAGE 0.12
#define WISP_MAX_HARD_FRAC 0.005
static bool
face_bake_material_is_wisp(int source)
{
    if( !g_textures[source].blend_layer )
        return false;
    if( g_materials && source < g_materials->count &&
        g_materials->materials[source].valid )
        return false;
    const struct face_bake_mean* mean = face_bake_material_mean(source);
    return mean->coverage > 0.0 && mean->coverage < WISP_MAX_COVERAGE &&
           mean->hard_frac < WISP_MAX_HARD_FRAC;
}

/* Cutout membranes: a non-blend material whose row is invalid yet carries
 * alpha_mode == 2 — the table's own statement that HD alpha-composites the
 * surface. Lane-wide this selects exactly three materials (1520, 1688, 2121),
 * each a dense bimodal frame (coverage 0.77-0.87, hard_frac 0.88-1.0): a real
 * surface with hard-edged holes torn in it, e.g. the QBD's ragged dorsal
 * crest membrane. The modulate fallback ships these as the authored placeholder
 * colour, fully opaque — on the QBD a jet-black slab. Bake instead what HD
 * shows: the frame's own covered colour, with the holes averaged into a
 * per-face translucency. Every solid invalid-row detail map in the lane is
 * mode 0/1, so nothing else is touched. */
static bool
face_bake_material_is_cutout(int source)
{
    if( g_textures[source].blend_layer )
        return false;
    if( !g_materials || source >= g_materials->count )
        return false;
    const struct RSCache_Dat2Material* m = &g_materials->materials[source];
    if( m->valid || m->alpha_mode != 2 )
        return false;
    return face_bake_material_mean(source)->coverage > 0.0;
}

/* Alpha-weighted mean over a barycentric sample grid of a UV triangle;
 * repeat-wrapped like the destination sampler. Corner order matches
 * face_bake_corner_uvs (us[0]/vs[0] is vertex a). Returns false when the
 * footprint has no covered texel at all (coverage is still written). */
static bool
face_bake_sample_uv_tri(
    int source,
    const double us[3],
    const double vs[3],
    double* out_coverage,
    double* out_mid_frac,
    double* out_r,
    double* out_g,
    double* out_b)
{
    const int N = 12;
    const int32_t* argb = g_textures[source].argb;
    double sw = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
    int total = 0, mid_cnt = 0;
    for( int i = 0; i <= N; i++ )
        for( int j = 0; j <= N - i; j++ )
        {
            double ba = (double)i / N;
            double bb = (double)j / N;
            double bc = 1.0 - ba - bb;
            double u = ba * us[0] + bb * us[1] + bc * us[2];
            double v = ba * vs[0] + bb * vs[1] + bc * vs[2];
            u -= (double)(int)u;
            if( u < 0.0 )
                u += 1.0;
            v -= (double)(int)v;
            if( v < 0.0 )
                v += 1.0;
            int x = (int)(u * BAKE_SIZE) & (BAKE_SIZE - 1);
            int y = (int)(v * BAKE_SIZE) & (BAKE_SIZE - 1);
            uint32_t px = (uint32_t)argb[y * BAKE_SIZE + x];
            double w = (double)(px >> 24) / 255.0;
            if( w > 0.1 && w < 0.9 )
                mid_cnt++;
            sw += w;
            sr += w * (double)((px >> 16) & 0xFF);
            sg += w * (double)((px >> 8) & 0xFF);
            sb += w * (double)(px & 0xFF);
            total++;
        }
    *out_coverage = sw / total;
    *out_mid_frac = (double)mid_cnt / total;
    if( sw <= 0.0 )
        return false;
    *out_r = sr / sw;
    *out_g = sg / sw;
    *out_b = sb / sw;
    return true;
}

/* HD tints its greyscale detail frames with the face's underlying colour; a
 * flat face colour can't do that per-texel, but it can keep the authored
 * IDENTITY: hue and saturation come from the authored HSL16 (the fin's
 * red/blue/green section keys, the crest's purple), lightness is the
 * geometric mean of the authored and sampled lightness so a dark-authored
 * face under a pale frame meets in the mottled middle the HD screen shows.
 * A grey authored colour (sat bits 0) has no hue to keep — the sample's own
 * hue and saturation carry instead. */
static uint16_t
face_bake_tint_hsl(uint16_t authored_hsl, double sr, double sg, double sb)
{
    int rr = sr < 0.0 ? 0 : (sr > 255.0 ? 255 : (int)(sr + 0.5));
    int gg = sg < 0.0 ? 0 : (sg > 255.0 ? 255 : (int)(sg + 0.5));
    int bb = sb < 0.0 ? 0 : (sb > 255.0 ? 255 : (int)(sb + 0.5));
    uint16_t sample_hsl =
        (uint16_t)face_bake_rgb_to_hsl16((rr << 16) | (gg << 8) | bb);
    /* Near-black near-grey authored colours are the erased-bake placeholder
     * this whole face bake exists to replace (the 1688/2121 "black strip") —
     * they carry no artist intent, so the frame colour stands alone. A dark
     * but SATURATED authored colour (the fin's deep red) is intent and still
     * tints below. */
    if( ((authored_hsl >> 7) & 7) <= 1 && (authored_hsl & 0x7F) < 8 )
        return sample_hsl;
    double la = (double)(authored_hsl & 0x7F) * 2.0 + 1.0;
    double ls = (double)(sample_hsl & 0x7F) * 2.0 + 1.0;
    int light = (int)(sqrt(la * ls) / 2.0 + 0.5);
    if( light > 127 )
        light = 127;
    uint16_t hue_sat = ((authored_hsl >> 7) & 7) ? (authored_hsl & (uint16_t)~0x7F)
                                                 : (sample_hsl & (uint16_t)~0x7F);
    return hue_sat | (uint16_t)light;
}

/* 0 = opaque, 255 = invisible. Composes the frame's coverage into the face's
 * (possibly authored) translucency: opacities multiply.
 *
 * `screen_blend` is the material's own verdict on what its alpha channel IS
 * (RSCache_Dat2Material::alpha_mode == 2, the 629+ alpha-blending column):
 *
 *   screen_blend  the HD engine alpha-blends the material on screen, so a
 *                 texel's alpha is literal screen opacity and the footprint's
 *                 mean coverage maps to face alpha LINEARLY and uncapped —
 *                 the QBD's neck membranes ship genuinely translucent.
 *
 *   otherwise     the alpha channel never reaches the screen (mode 0 draws
 *                 opaque, mode 1 is a cutout test, and a blend-layer's alpha
 *                 is a layer-mask weight — the QBD's neck strap means ~0.2
 *                 yet ships solid in HD). Coverage passes through gamma and
 *                 the derived-translucency cap, and callers only reach here
 *                 with a footprint that landed entirely in a hole.
 *
 * Authored alpha is exempt from both gamma and cap (coverage 1.0 returns it
 * verbatim). A footprint below the drop threshold still drops. */
static double g_face_alpha_gamma = 0.45;
static int g_face_alpha_cap = 64;
static void
face_bake_apply_alpha(
    struct RSCache_Model* model, int face, double coverage, bool screen_blend)
{
    if( !g_face_alpha_bake )
        return;
    if( coverage > 0.98 )
        return;
    if( !model->face_alphas )
    {
        model->face_alphas = calloc((size_t)model->face_count, 1);
        if( !model->face_alphas )
            return;
    }
    double authored_opacity = 1.0 - (double)model->face_alphas[face] / 255.0;
    double cov = coverage < 0.0 ? 0.0 : coverage;
    if( !screen_blend && g_face_alpha_gamma != 1.0 )
        cov = pow(cov, g_face_alpha_gamma);
    double opacity = authored_opacity * cov;
    int alpha = (int)(255.0 * (1.0 - opacity) + 0.5);
    if( alpha < 0 )
        alpha = 0;
    if( alpha > 255 )
        alpha = 255;
    if( !screen_blend && coverage >= 0.02 )
    {
        int limit = model->face_alphas[face] > g_face_alpha_cap
                        ? model->face_alphas[face]
                        : g_face_alpha_cap;
        if( alpha > limit )
            alpha = limit;
    }
    if( alpha >= 255 && g_face_drop_marks )
    {
        /* Fully invisible: drop the face outright instead of shipping a
         * triangle the renderer would only ever skip. */
        g_face_drop_marks[face] = 1;
        return;
    }
    model->face_alphas[face] = (uint8_t)alpha;
    g_face_bake_alpha_faces++;
}

/* --wisp-alpha: how far a wisp's frame coverage is allowed to carry into
 * screen translucency.
 *
 * Being a wisp is an INFERENCE, and it contradicts the one piece of authored
 * evidence available: every material the rule selects on this lane carries
 * alpha_mode 0, HD's own statement that it draws opaque. That inference is
 * right for a standalone filament and wrong for a sparse greyscale detail
 * layer sitting on a solid surface, and no field in the material row separates
 * the two — material 1685 (the QBD crest fringe) and material 214 (the arena
 * rocks) are the same shader, the same greyscale, the same coverage band.
 *
 *   screen  the inference wins outright: sqrt(coverage) becomes face alpha
 *           linearly and uncapped, bypassing both the gamma and the cap. This
 *           was the default through v10-m60 and it is what ghosted the rocks,
 *           the braziers and the dragonbone armour.
 *   capped  same inference, but bounded — it may soften a surface, never erase
 *           it. Coverage takes the ordinary gamma and lands under
 *           --face-alpha-cap, so a guess can no longer overrule the table by
 *           more than the cap allows.
 *   off     DEFAULT. The inference loses: the face is what its row says it is,
 *           an opaque mode-0 blend layer, and only a footprint landing entirely
 *           in a hole produces alpha.
 *
 * `off` is the default because a side-by-side of all 660 lane models showed it
 * restoring every solid object the rule had ghosted (rocks, braziers, the
 * dragonbone set, four locs that rendered as literally nothing) while leaving
 * the QBD itself within 1% of baseline — the crest the rule was written for
 * does not measurably regress — and leaving every genuinely translucent thing
 * pixel-identical, because smoke, spotanims and membranes reach their alpha
 * through the authored alpha_mode 2 and cutout paths, not through this one.
 *
 * The wisp COLOUR treatment (face_bake_tint_hsl, below) is not affected by
 * this at any setting: the placeholder-colour half of the original bug is
 * fixed by the tint, independently of opacity. */
enum wisp_alpha_mode
{
    WISP_ALPHA_SCREEN,
    WISP_ALPHA_CAPPED,
    WISP_ALPHA_OFF,
};
static enum wisp_alpha_mode g_wisp_alpha = WISP_ALPHA_OFF;
static int g_face_bake_wisp_faces = 0;

static void
face_bake_wisp_alpha(struct RSCache_Model* model, int face, double coverage)
{
    g_face_bake_wisp_faces++;
    switch( g_wisp_alpha )
    {
    case WISP_ALPHA_SCREEN:
        /* A wisp's whisper coverages should ghost, not vanish, so the linear
         * map gets a sqrt lift first (fin 0.08 -> 0.28 rather than 0.08). */
        face_bake_apply_alpha(model, face, sqrt(coverage), true);
        break;
    case WISP_ALPHA_CAPPED:
        face_bake_apply_alpha(model, face, coverage, false);
        break;
    case WISP_ALPHA_OFF:
        if( coverage < 0.02 )
            face_bake_apply_alpha(model, face, 0.0, false);
        break;
    }
}

/* Remove marked faces in place, compacting every per-face array. Vertices,
 * bone maps and texture triangles are left alone: unused entries are harmless,
 * vertex skinning is untouched (so animations survive), and keeping
 * textured_face_count intact keeps the provenance tail's complex-payload
 * sizing valid. Returns the number of faces removed. */
static int
face_bake_drop_marked(struct RSCache_Model* model, const uint8_t* marks)
{
    int w = 0;
    for( int face = 0; face < model->face_count; face++ )
    {
        if( marks[face] )
            continue;
        if( w != face )
        {
            model->face_indices_a[w] = model->face_indices_a[face];
            model->face_indices_b[w] = model->face_indices_b[face];
            model->face_indices_c[w] = model->face_indices_c[face];
            if( model->face_bone_map )
                model->face_bone_map[w] = model->face_bone_map[face];
            if( model->face_alphas )
                model->face_alphas[w] = model->face_alphas[face];
            if( model->face_infos )
                model->face_infos[w] = model->face_infos[face];
            if( model->face_priorities )
                model->face_priorities[w] = model->face_priorities[face];
            if( model->face_colors )
                model->face_colors[w] = model->face_colors[face];
            if( model->face_textures )
                model->face_textures[w] = model->face_textures[face];
            if( model->face_texture_coords )
                model->face_texture_coords[w] = model->face_texture_coords[face];
            if( model->face_z_offsets )
                model->face_z_offsets[w] = model->face_z_offsets[face];
        }
        w++;
    }
    int dropped = model->face_count - w;
    model->face_count = w;
    return dropped;
}

/* --- Matte pass -----------------------------------------------------------
 * OSRS surfaces are matte: mostly flat colour, only light shading. The HD
 * frames this bake samples carry the source engine's baked-in specular, so
 * neighbouring faces of one material can land 30+ lightness steps apart — a
 * gloss gradient the destination style never has. --matte N (0-100, default
 * 0 = off) runs a per-model post-pass over every face this bake coloured:
 * each face's HSL16 lightness is compressed toward the mean lightness of its
 * source material's baked faces by N% (muting the gradient), then anything
 * still above the gloss knee is compressed toward the knee by N% again
 * (shifting sheen toward matte). Hue and saturation are never touched, and
 * faces the bake never coloured (authored geometry, kept textures) are
 * exempt. */
#define FACE_MATTE_KNEE 100
static int g_face_matte = 0;
struct face_matte_rec
{
    int face;
    int source;
};
static struct face_matte_rec* g_face_matte_recs = NULL;
static int g_face_matte_count = 0;
static int g_face_matte_cap = 0;
static long g_face_matte_faces = 0;
static long g_face_matte_delta = 0;

static void
face_matte_record(int face, int source)
{
    if( !g_face_matte )
        return;
    if( g_face_matte_count == g_face_matte_cap )
    {
        int cap = g_face_matte_cap ? g_face_matte_cap * 2 : 1024;
        struct face_matte_rec* p =
            realloc(g_face_matte_recs, (size_t)cap * sizeof *g_face_matte_recs);
        if( !p )
            return;
        g_face_matte_recs = p;
        g_face_matte_cap = cap;
    }
    g_face_matte_recs[g_face_matte_count].face = face;
    g_face_matte_recs[g_face_matte_count].source = source;
    g_face_matte_count++;
}

/* --- Cutout face synthesis ------------------------------------------------
 * A flat per-face colour+alpha reads poorly on the cutout membranes: HD shows
 * a pale scaly surface with hard-edged torn holes, and averaging that into one
 * triangle gives a featureless ghost. Instead, tessellate each cutout face
 * K*K-fold along its own UV footprint, sample the frame per sub-face, DROP the
 * sub-faces that land in holes, and tint the rest — the tears and the mottling
 * become geometry the SD renderer can actually draw. Parents are marked into
 * g_face_drop_marks and removed by the existing drop pass; sub-vertices are
 * deduped by exact rounded coordinate so shared edges stay watertight, and
 * grid corners reuse the parent's own vertices so the membrane still meets the
 * surrounding mesh. Only non-degenerate cutout faces record here (a face
 * without a usable UV projector has no footprint to carve). */
#define FACE_SYNTH_K_MAX 6
static int g_face_synth_k = 3;
static double g_face_synth_hole = 0.2;
static bool g_face_synth = true;
struct face_synth_rec
{
    int face;
    int source;
    uint16_t authored_hsl;
    double us[3];
    double vs[3];
};
static struct face_synth_rec* g_face_synth_recs = NULL;
static int g_face_synth_count = 0;
static int g_face_synth_cap = 0;
static long g_face_synth_parents = 0;
static long g_face_synth_faces = 0;
static long g_face_synth_holes = 0;

static void
face_synth_record(
    struct RSCache_Model* model,
    int face,
    int source,
    uint16_t authored_hsl,
    const double us[3],
    const double vs[3])
{
    (void)model;
    if( !g_face_synth || !g_face_drop_marks )
        return;
    if( g_face_synth_count == g_face_synth_cap )
    {
        int cap = g_face_synth_cap ? g_face_synth_cap * 2 : 256;
        void* p =
            realloc(g_face_synth_recs, (size_t)cap * sizeof *g_face_synth_recs);
        if( !p )
            return;
        g_face_synth_recs = p;
        g_face_synth_cap = cap;
    }
    struct face_synth_rec* rec = &g_face_synth_recs[g_face_synth_count++];
    rec->face = face;
    rec->source = source;
    rec->authored_hsl = authored_hsl;
    memcpy(rec->us, us, sizeof rec->us);
    memcpy(rec->vs, vs, sizeof rec->vs);
}

static void
face_bake_synthesize(struct RSCache_Model* model)
{
    if( !g_face_synth_count || !g_face_drop_marks )
        return;
    if( model->animaya_group_counts )
    {
        /* Per-vertex skeletal weights would need synthesizing too; no lane
         * OB3 carries them, so decline rather than guess. */
        g_face_synth_count = 0;
        return;
    }
    const int K = g_face_synth_k;
    int max_new_faces = g_face_synth_count * K * K;
    int max_new_verts = g_face_synth_count * ((K + 1) * (K + 2) / 2 - 3);
    int base_face = model->face_count;
    int base_vert = model->vertex_count;
#define SYNTH_GROW(ptr, type, count)                                     \
    do                                                                   \
    {                                                                    \
        if( ptr )                                                        \
        {                                                                \
            void* p_ = realloc(ptr, (size_t)(count) * sizeof(type));     \
            if( !p_ )                                                    \
                return;                                                  \
            ptr = (type*)p_;                                             \
        }                                                                \
    } while( 0 )
    SYNTH_GROW(model->vertices_x, int, base_vert + max_new_verts);
    SYNTH_GROW(model->vertices_y, int, base_vert + max_new_verts);
    SYNTH_GROW(model->vertices_z, int, base_vert + max_new_verts);
    SYNTH_GROW(model->vertex_bone_map, uint8_t, base_vert + max_new_verts);
    SYNTH_GROW(model->face_indices_a, int, base_face + max_new_faces);
    SYNTH_GROW(model->face_indices_b, int, base_face + max_new_faces);
    SYNTH_GROW(model->face_indices_c, int, base_face + max_new_faces);
    SYNTH_GROW(model->face_colors, uint16_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_infos, uint8_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_priorities, uint8_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_bone_map, uint8_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_textures, int16_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_texture_coords, int16_t, base_face + max_new_faces);
    SYNTH_GROW(model->face_z_offsets, int8_t, base_face + max_new_faces);
    if( !model->face_alphas )
    {
        model->face_alphas = calloc((size_t)(base_face + max_new_faces), 1);
        if( !model->face_alphas )
            return;
    }
    else
        SYNTH_GROW(model->face_alphas, uint8_t, base_face + max_new_faces);
#undef SYNTH_GROW
    {
        void* p = realloc(g_face_drop_marks, (size_t)(base_face + max_new_faces));
        if( !p )
            return;
        g_face_drop_marks = p;
        memset(g_face_drop_marks + base_face, 0, (size_t)max_new_faces);
    }
    for( int r = 0; r < g_face_synth_count; r++ )
    {
        const struct face_synth_rec* rec = &g_face_synth_recs[r];
        int pa = model->face_indices_a[rec->face];
        int pb = model->face_indices_b[rec->face];
        int pc = model->face_indices_c[rec->face];
        /* Resolve every grid point to a model vertex. Barycentric weight ba
         * belongs to corner a, matching face_bake_corner_uvs order. */
        int vert[FACE_SYNTH_K_MAX + 1][FACE_SYNTH_K_MAX + 1];
        for( int i = 0; i <= K; i++ )
            for( int j = 0; j <= K - i; j++ )
            {
                if( i == K )
                {
                    vert[i][j] = pa;
                    continue;
                }
                if( j == K )
                {
                    vert[i][j] = pb;
                    continue;
                }
                if( i == 0 && j == 0 )
                {
                    vert[i][j] = pc;
                    continue;
                }
                double ba = (double)i / K;
                double bb = (double)j / K;
                double bc = 1.0 - ba - bb;
                int xi = (int)floor(
                    ba * model->vertices_x[pa] + bb * model->vertices_x[pb] +
                    bc * model->vertices_x[pc] + 0.5);
                int yi = (int)floor(
                    ba * model->vertices_y[pa] + bb * model->vertices_y[pb] +
                    bc * model->vertices_y[pc] + 0.5);
                int zi = (int)floor(
                    ba * model->vertices_z[pa] + bb * model->vertices_z[pb] +
                    bc * model->vertices_z[pc] + 0.5);
                int found = -1;
                for( int v = base_vert; v < model->vertex_count; v++ )
                    if( model->vertices_x[v] == xi &&
                        model->vertices_y[v] == yi && model->vertices_z[v] == zi )
                    {
                        found = v;
                        break;
                    }
                if( found < 0 )
                {
                    found = model->vertex_count++;
                    model->vertices_x[found] = xi;
                    model->vertices_y[found] = yi;
                    model->vertices_z[found] = zi;
                    if( model->vertex_bone_map )
                    {
                        int dom = pc;
                        if( ba >= bb && ba >= bc )
                            dom = pa;
                        else if( bb >= bc )
                            dom = pb;
                        model->vertex_bone_map[found] =
                            model->vertex_bone_map[dom];
                    }
                }
                vert[i][j] = found;
            }
        int emitted = 0;
        for( int i = 0; i < K; i++ )
            for( int j = 0; j < K - i; j++ )
                for( int t = 0; t < (j < K - i - 1 ? 2 : 1); t++ )
                {
                    /* t==0 upright (i+1,j),(i,j+1),(i,j); t==1 inverted
                     * (i,j+1),(i+1,j),(i+1,j+1) — both orders preserve the
                     * parent's winding. */
                    int v1 = t == 0 ? vert[i + 1][j] : vert[i][j + 1];
                    int v2 = t == 0 ? vert[i][j + 1] : vert[i + 1][j];
                    int v3 = t == 0 ? vert[i][j] : vert[i + 1][j + 1];
                    if( v1 == v2 || v2 == v3 || v1 == v3 )
                        continue;
                    double c1a = t == 0 ? (double)(i + 1) / K : (double)i / K;
                    double c1b = t == 0 ? (double)j / K : (double)(j + 1) / K;
                    double c2a = t == 0 ? (double)i / K : (double)(i + 1) / K;
                    double c2b = t == 0 ? (double)(j + 1) / K : (double)j / K;
                    double c3a = t == 0 ? (double)i / K : (double)(i + 1) / K;
                    double c3b = t == 0 ? (double)j / K : (double)(j + 1) / K;
                    double sus[3], svs[3];
                    sus[0] = c1a * rec->us[0] + c1b * rec->us[1] +
                             (1.0 - c1a - c1b) * rec->us[2];
                    svs[0] = c1a * rec->vs[0] + c1b * rec->vs[1] +
                             (1.0 - c1a - c1b) * rec->vs[2];
                    sus[1] = c2a * rec->us[0] + c2b * rec->us[1] +
                             (1.0 - c2a - c2b) * rec->us[2];
                    svs[1] = c2a * rec->vs[0] + c2b * rec->vs[1] +
                             (1.0 - c2a - c2b) * rec->vs[2];
                    sus[2] = c3a * rec->us[0] + c3b * rec->us[1] +
                             (1.0 - c3a - c3b) * rec->us[2];
                    svs[2] = c3a * rec->vs[0] + c3b * rec->vs[1] +
                             (1.0 - c3a - c3b) * rec->vs[2];
                    double cov, mid, sr, sg, sb;
                    if( !face_bake_sample_uv_tri(
                            rec->source, sus, svs, &cov, &mid, &sr, &sg, &sb) ||
                        cov < g_face_synth_hole )
                    {
                        /* A real torn hole: emit nothing. */
                        g_face_synth_holes++;
                        continue;
                    }
                    int nf = model->face_count++;
                    model->face_indices_a[nf] = v1;
                    model->face_indices_b[nf] = v2;
                    model->face_indices_c[nf] = v3;
                    model->face_colors[nf] =
                        face_bake_tint_hsl(rec->authored_hsl, sr, sg, sb);
                    face_matte_record(nf, rec->source);
                    int alpha = cov > 0.98
                                    ? 0
                                    : (int)(255.0 * (1.0 - cov) + 0.5);
                    model->face_alphas[nf] = (uint8_t)alpha;
                    if( model->face_infos )
                        model->face_infos[nf] =
                            (uint8_t)(model->face_infos[rec->face] & 1);
                    if( model->face_priorities )
                        model->face_priorities[nf] =
                            model->face_priorities[rec->face];
                    if( model->face_bone_map )
                        model->face_bone_map[nf] =
                            model->face_bone_map[rec->face];
                    if( model->face_textures )
                        model->face_textures[nf] = -1;
                    if( model->face_texture_coords )
                        model->face_texture_coords[nf] = -1;
                    if( model->face_z_offsets )
                        model->face_z_offsets[nf] =
                            model->face_z_offsets[rec->face];
                    emitted++;
                    g_face_synth_faces++;
                    if( alpha > 0 )
                        g_face_bake_alpha_faces++;
                }
        if( emitted > 0 )
        {
            g_face_drop_marks[rec->face] = 1;
            g_face_synth_parents++;
        }
        /* emitted == 0 means the whole footprint was hole: keep whatever the
         * flat bake already decided for the parent (usually a drop). */
    }
    g_face_synth_count = 0;
}

/* The matte post-pass proper; see the --matte comment above. Runs after
 * face_bake_synthesize (so sub-faces exist at their final colours) and before
 * face_bake_drop_marked (so recorded indices are still valid); parents marked
 * for drop are excluded from both the means and the rewrite. */
static void
face_bake_matte(struct RSCache_Model* model)
{
    if( !g_face_matte || !g_face_matte_count || !model->face_colors )
    {
        g_face_matte_count = 0;
        return;
    }
    static double light_sum[MAX_TEXTURES];
    static int light_cnt[MAX_TEXTURES];
    memset(light_sum, 0, sizeof(light_sum));
    memset(light_cnt, 0, sizeof(light_cnt));
    for( int i = 0; i < g_face_matte_count; i++ )
    {
        const struct face_matte_rec* rec = &g_face_matte_recs[i];
        if( g_face_drop_marks && rec->face < g_face_drop_marks_size &&
            g_face_drop_marks[rec->face] )
            continue;
        light_sum[rec->source] += (double)(model->face_colors[rec->face] & 0x7F);
        light_cnt[rec->source]++;
    }
    double m = (double)g_face_matte / 100.0;
    for( int i = 0; i < g_face_matte_count; i++ )
    {
        const struct face_matte_rec* rec = &g_face_matte_recs[i];
        if( g_face_drop_marks && rec->face < g_face_drop_marks_size &&
            g_face_drop_marks[rec->face] )
            continue;
        if( !light_cnt[rec->source] )
            continue;
        uint16_t hsl = model->face_colors[rec->face];
        double mean = light_sum[rec->source] / light_cnt[rec->source];
        double l = mean + ((double)(hsl & 0x7F) - mean) * (1.0 - m);
        if( l > FACE_MATTE_KNEE )
            l = FACE_MATTE_KNEE + (l - FACE_MATTE_KNEE) * (1.0 - m);
        int li = (int)(l + 0.5);
        if( li < 0 )
            li = 0;
        else if( li > 127 )
            li = 127;
        g_face_matte_delta += li - (int)(hsl & 0x7F) > 0
                                  ? li - (int)(hsl & 0x7F)
                                  : (int)(hsl & 0x7F) - li;
        model->face_colors[rec->face] = (uint16_t)((hsl & ~0x7F) | li);
        g_face_matte_faces++;
    }
    g_face_matte_count = 0;
}

static void
face_dump_row(
    struct RSCache_Model* model,
    int face,
    int source,
    int degenerate,
    double coverage,
    double mid_frac,
    int authored)
{
    if( !g_face_dump )
        return;
    int a = model->face_indices_a[face];
    int b = model->face_indices_b[face];
    int c = model->face_indices_c[face];
    double cx =
        (model->vertices_x[a] + model->vertices_x[b] + model->vertices_x[c]) / 3.0;
    double cy =
        (model->vertices_y[a] + model->vertices_y[b] + model->vertices_y[c]) / 3.0;
    double cz =
        (model->vertices_z[a] + model->vertices_z[b] + model->vertices_z[c]) / 3.0;
    int final_alpha = g_face_drop_marks && g_face_drop_marks[face]
                          ? -1
                          : (model->face_alphas ? model->face_alphas[face] : 0);
    /* face_colors[face] is still the authored base at every call site — the
     * composite writes it only after the dump row. */
    int base_hsl = model->face_colors ? model->face_colors[face] : -1;
    double base_r = 0, base_g = 0, base_b = 0;
    if( base_hsl >= 0 )
        face_bake_hsl16_to_rgb((uint16_t)base_hsl, &base_r, &base_g, &base_b);
    fprintf(
        g_face_dump,
        "%d,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%.1f,%.1f,%.1f,%d,%.0f,%.0f,%.0f\n",
        g_face_bake_current_model,
        face,
        source,
        source >= 0 && g_textures[source].blend_layer ? 1 : 0,
        degenerate,
        coverage,
        mid_frac,
        authored,
        final_alpha,
        cx,
        cy,
        cz,
        base_hsl,
        base_r,
        base_g,
        base_b);
}

/* Composite the material's footprint mean over one face's colour. Returns
 * false when there is nothing usable to sample; the caller's fallback then
 * behaves exactly as it always has. */
static bool
face_bake_apply(struct RSCache_Model* model, int face, int source)
{
    if( g_face_bake_mode == FACE_BAKE_OFF )
        return false;
    if( !model->face_colors || !g_textures[source].argb )
        return false;
    struct face_bake_debug_row* dbg = NULL;
    if( g_face_bake_debug_model >= 0 &&
        g_face_bake_current_model == g_face_bake_debug_model && source >= 0 &&
        source < MAX_TEXTURES )
    {
        dbg = &g_face_bake_debug[source];
        dbg->faces++;
        if( !model->face_texture_coords || model->face_texture_coords[face] < 0 )
            dbg->texco_own++;
    }
    const struct face_bake_mean* global = face_bake_material_mean(source);
    if( global->coverage <= 0.0 )
    {
        /* The program bakes to nothing at all: the face was never a visible
         * surface. Colour is left alone; coverage 0 makes it transparent
         * when the alpha bake is on. */
        int authored0 = model->face_alphas ? model->face_alphas[face] : 0;
        face_bake_apply_alpha(model, face, 0.0, false);
        face_dump_row(model, face, source, 0, 0.0, -1.0, authored0);
        g_face_bake_uncovered++;
        return false;
    }

    double fm_r, fm_g, fm_b, coverage;
    double mid_frac = 1.0;
    bool degenerate = false;
    double us[3], vs[3];
    if( face_bake_corner_uvs(model, face, us, vs) )
    {
        if( !face_bake_sample_uv_tri(
                source, us, vs, &coverage, &mid_frac, &fm_r, &fm_g, &fm_b) )
        {
            int authored0 = model->face_alphas ? model->face_alphas[face] : 0;
            face_bake_apply_alpha(model, face, 0.0, false);
            face_dump_row(model, face, source, 0, 0.0, -1.0, authored0);
            g_face_bake_uncovered++;
            return false;
        }
    }
    else
    {
        /* Degenerate projector: fall back to the material's global mean. */
        degenerate = true;
        g_face_bake_degenerate++;
        if( dbg )
        {
            dbg->degenerate++;
            if( g_face_bake_uv_fail == UV_BAD_INDEX )
                dbg->bad_index++;
            else if( g_face_bake_uv_fail == UV_ZERO_AREA )
                dbg->zero_area++;
        }
        fm_r = global->r;
        fm_g = global->g;
        fm_b = global->b;
        coverage = global->coverage;
    }
    if( dbg )
        dbg->coverage_sum += coverage;
    int authored = model->face_alphas ? model->face_alphas[face] : 0;
    int alpha_mode = 0;
    if( g_materials && source < g_materials->count )
        alpha_mode = g_materials->materials[source].alpha_mode;
    bool wisp = face_bake_material_is_wisp(source);
    bool cutout = face_bake_material_is_cutout(source);
    if( degenerate )
    {
        /* The global frame coverage is a guess about an animated projector,
         * not evidence about this face; inventing translucency from it turned
         * whole surfaces (QBD neck shells, belt) into ghosts. Keep the
         * authored alpha and let the colour composite carry the detail —
         * except for wisp and cutout materials, whose authored state never
         * reached the HD screen: there the global coverage is exactly the
         * right guess (the QBD's dorsal filament fin bakes every face
         * degenerate, and keeping authored shipped it as an opaque slab in
         * placeholder colours). A wisp's linear coverage lands near-invisible
         * (fin 0.08 -> alpha 234); sqrt lifts it to a visible ghost while
         * still vanishing with the frame. */
        if( cutout )
            face_bake_apply_alpha(model, face, coverage, true);
        else if( wisp )
            face_bake_wisp_alpha(model, face, coverage);
    }
    else if( cutout )
    {
        /* Alpha-composited cutout surface (invalid row, mode 2): the frame's
         * torn holes average into per-face translucency exactly like a
         * mode-2 blend membrane — high coverage keeps the membrane mostly
         * solid, footprints over holes fade. */
        face_bake_apply_alpha(model, face, coverage, true);
    }
    else if( wisp )
    {
        /* A wisp is a blend layer whose translucency is INFERRED against the
         * material row's own alpha_mode 0; how far that inference is allowed
         * to go is --wisp-alpha. Ordered before the alpha_mode 2 branch so a
         * material that is both still answers to the wisp setting. */
        face_bake_wisp_alpha(model, face, coverage);
    }
    else if( g_textures[source].blend_layer && alpha_mode == 2 )
    {
        /* The HD engine alpha-blends this material on screen, so its texel
         * alpha is literal opacity — the QBD's neck membranes. The footprint's
         * mean coverage becomes face alpha linearly and uncapped; see
         * face_bake_apply_alpha. This is authored evidence, not an inference,
         * which is why it is not bounded the way a wisp now is. */
        face_bake_apply_alpha(model, face, coverage, true);
    }
    else
    {
        /* The alpha channel never reaches the screen: mode 0 draws opaque,
         * mode 1 is a cutout test, and a blend-layer's alpha under either is
         * a layer-mask weight — the QBD's neck strap means ~0.2 yet ships
         * solid in HD (this gate replaces v5's mid_frac bimodality guess,
         * which read the footprint when the answer was in the material row).
         * On screen these faces are opaque; only a footprint landing entirely
         * in a hole matters. */
        if( coverage < 0.02 )
            face_bake_apply_alpha(model, face, 0.0, false);
    }
    face_dump_row(
        model, face, source, degenerate ? 1 : 0, coverage, mid_frac, authored);

    if( wisp || cutout )
    {
        /* A wisp's or cutout's authored colour never reached the HD screen
         * as-is: HD tinted its greyscale frame with it (the fin's authored
         * red/blue/green sections, the crest's purple). Keep the authored
         * hue/sat and meet the frame's lightness halfway — see
         * face_bake_tint_hsl. */
        if( cutout && !degenerate )
            face_synth_record(model, face, source, model->face_colors[face], us, vs);
        model->face_colors[face] =
            face_bake_tint_hsl(model->face_colors[face], fm_r, fm_g, fm_b);
        face_matte_record(face, source);
        g_face_bake_faces++;
        return true;
    }
    double k = coverage * (double)g_face_bake_strength / 100.0;
    if( k <= 0.0 )
    {
        g_face_bake_uncovered++;
        return false;
    }
    if( k > 1.0 )
        k = 1.0;
    double base_r, base_g, base_b;
    face_bake_hsl16_to_rgb(model->face_colors[face], &base_r, &base_g, &base_b);
    double out_r, out_g, out_b;
    if( g_face_bake_mode == FACE_BAKE_TINT )
    {
        out_r = base_r * (1.0 - k) + fm_r * k;
        out_g = base_g * (1.0 - k) + fm_g * k;
        out_b = base_b * (1.0 - k) + fm_b * k;
    }
    else
    {
        double ratio_r = global->r > 0.0 ? fm_r / global->r : 1.0;
        double ratio_g = global->g > 0.0 ? fm_g / global->g : 1.0;
        double ratio_b = global->b > 0.0 ? fm_b / global->b : 1.0;
        out_r = base_r * (1.0 - k + k * ratio_r);
        out_g = base_g * (1.0 - k + k * ratio_g);
        out_b = base_b * (1.0 - k + k * ratio_b);
    }
    int r = out_r < 0.0 ? 0 : (out_r > 255.0 ? 255 : (int)(out_r + 0.5));
    int g = out_g < 0.0 ? 0 : (out_g > 255.0 ? 255 : (int)(out_g + 0.5));
    int b = out_b < 0.0 ? 0 : (out_b > 255.0 ? 255 : (int)(out_b + 0.5));
    model->face_colors[face] =
        (uint16_t)face_bake_rgb_to_hsl16((r << 16) | (g << 8) | b);
    face_matte_record(face, source);
    g_face_bake_faces++;
    return true;
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
    g_materials = materials;
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
        g_face_bake_current_model = models->values[i];
        g_face_synth_count = 0;
        if( g_face_alpha_bake && model->face_textures && model->face_count > 0 )
        {
            g_face_drop_marks = calloc((size_t)model->face_count, 1);
            g_face_drop_marks_size = model->face_count;
            if( !g_face_drop_marks )
            {
                RSCache_ModelFree(model);
                RSCache_ModelProvenanceFree(provenance);
                goto fail;
            }
        }
        if( model->face_textures )
            for( int face = 0; face < model->face_count; face++ )
            {
                int source = model->face_textures[face];
                if( source < 0 )
                {
                    /* Plain authored geometry: no material was ever attached,
                     * so the backport leaves it alone. Dump it anyway
                     * (mid_frac -2 marks the row) so face audits see the whole
                     * model, not just the erased-material faces. */
                    face_dump_row(model, face, -1, 0, 0.0, -2.0,
                                  model->face_alphas ? model->face_alphas[face] : 0);
                    continue;
                }
                if( source >= MAX_TEXTURES || !g_mapping[source].present ||
                    g_mapping[source].dest_texture > INT16_MAX )
                {
                    fprintf(stderr,
                            "rs2012_material_bake: model %d face %d has unmapped material %d\n",
                            models->values[i], face, source);
                    free(g_face_drop_marks);
                    g_face_drop_marks = NULL;
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
                /*
                 * Can this lane render the material at all?
                 *
                 * `valid` stays the gate, and the 204 materials that fail it
                 * stay erased. Do not be tempted by the fact that they are
                 * greyscale: they are, and that is exactly why referencing
                 * them gives blown-out white - but tinting them does not save
                 * them. Measured, not assumed: with the modulate kernel in
                 * place and the fallback lifted for every greyscale material,
                 * lane fallback faces drop 274,715 -> 26,294 and the arena
                 * renders as white and green shards, the same failure
                 * RS2012_BACKPORT.md §2 recorded before the kernel existed.
                 * Being a greyscale detail map is necessary for a mask and
                 * nowhere near sufficient: these are HD effect programs whose
                 * baked 128x128 frame is not a surface map at any tint.
                 *
                 * A blend layer cannot be colour-keyed either, so without the
                 * alpha kernel it stays unrenderable whatever `valid` says.
                 */
                bool renderable = materials->materials[source].valid;
                if( g_textures[source].blend_layer )
                    renderable = false;

                if( g_ground_mesh_fallback && !renderable )
                {
                    /* Recover the frame's DC term into the face colour and
                     * its alpha coverage into face translucency before the
                     * texture state is erased; see face_bake_apply. This is
                     * the shipped backport (modulate + alpha bake);
                     * --face-color-bake off restores the bare erase. */
                    face_bake_apply(model, face, source);
                    if( model->face_infos )
                        model->face_infos[face] =
                            (uint8_t)(model->face_infos[face] & 1);
                    if( model->face_texture_coords )
                        model->face_texture_coords[face] = -1;
                    model->face_textures[face] = (int16_t)-1;
                    g_ground_mesh_model_faces_fallback++;
                }
                else
                {
                    /* Renderable material kept as a destination texture; the
                     * face never enters the bake. mid_frac -3 marks the row. */
                    face_dump_row(model, face, source, 0, 0.0, -3.0,
                                  model->face_alphas ? model->face_alphas[face] : 0);
                    model->face_textures[face] =
                        (int16_t)g_mapping[source].dest_texture;
                }
            }
        int synth_base_verts = model->vertex_count;
        if( g_face_drop_marks && g_face_synth_count > 0 )
            face_bake_synthesize(model);
        face_bake_matte(model);
        if( g_face_drop_marks )
        {
            int dropped = face_bake_drop_marked(model, g_face_drop_marks);
            free(g_face_drop_marks);
            g_face_drop_marks = NULL;
            if( model->vertex_count != synth_base_verts )
                provenance->vertex_count = model->vertex_count;
            if( dropped > 0 )
            {
                /* The provenance mirrors the pre-drop face count. Left alone,
                 * the encoder would reject it as describing a different model
                 * and re-derive the header - losing the tail and, with it, the
                 * complex-texture payloads the engine's OB3 loader sizes from
                 * textured_face_count (the QBD then decodes to garbage and
                 * renders blank). Re-align it instead: the header flags and
                 * tail still describe this model, only the per-face streams
                 * are stale. Dropping the recorded index types makes every
                 * face encode as a full triple, which is always legal. */
                free(provenance->face_index_types);
                provenance->face_index_types = NULL;
                free(provenance->face_info_bytes);
                provenance->face_info_bytes = NULL;
                provenance->face_count = model->face_count;
                g_face_bake_dropped_faces += dropped;
            }
        }
        /* The alpha bake can add a face_alphas array to a model whose source
         * header said has_alpha=0. The encoder trusts the provenance's verbatim
         * flag byte over the array's presence, which would silently drop the
         * baked translucency; flip the flag to match the model. */
        if( g_face_alpha_bake && model->face_alphas &&
            provenance->header_flag_count > 2 && provenance->header_flags[2] == 0 )
            provenance->header_flags[2] = 1;
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

    /* Does the image carry colour, or only detail? See ::greyscale. Measured
     * over covered texels only - a clear region has no colour to speak of. */
    {
        long spread = 0;
        int covered = 0;
        for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
        {
            uint32_t argb = (uint32_t)entry->argb[i];
            if( (argb >> 24) == 0 )
                continue;
            int r = (int)((argb >> 16) & 0xFF);
            int g = (int)((argb >> 8) & 0xFF);
            int b = (int)(argb & 0xFF);
            int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
            spread += hi - lo;
            covered++;
        }
        entry->greyscale = covered > 0 && spread / covered < GREYSCALE_CHROMA_LIMIT;
    }


    for( int i = 0; i < BAKE_SIZE * BAKE_SIZE; i++ )
    {
        uint32_t argb = (uint32_t)entry->argb[i];
        int alpha = (int)(argb >> 24);
        /* A blend layer is emitted with its coverage intact - thresholding is
         * exactly what destroys it - so only a fully clear texel drops out. */
        if( alpha < 128 )
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
        if( pixels ) pixels[i] = (int32_t)(0xFF000000u | (uint32_t)rgb);
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

/* --models-out: the same OB3 bytes, flat in a standalone directory. Lets an
 * experiment (e.g. --face-color-bake) be rendered and judged without
 * overwriting the lane, whose models are the only surviving remap output. */
static int
write_model_outputs_dir(
    const char* dir,
    const struct model_output* outputs,
    int count)
{
    PORT_MKDIR(dir);
    for( int i = 0; i < count; i++ )
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/rs2012_model_%d.ob3", dir,
                 outputs[i].source_id);
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
            "         [--face-color-bake off|tint|modulate] [--face-color-strength 0-100]\n"
            "         [--no-face-alpha-bake] [--face-bake-debug MODEL] [--models-out DIR]\n"
            "         [--matte 0-100] [--wisp-alpha screen|capped|off]\n"
            "Defaults: cache.rs727_preeoc and OSRS-Content/osrs239-content.\n"
            "The default backport composites each erased material's baked frame into\n"
            "the face colours it falls back to (modulate) and turns the frame's alpha\n"
            "coverage into face translucency, dropping faces that end up invisible.\n"
            "--face-color-bake off restores the bare erase-only fallback and\n"
            "--no-face-alpha-bake keeps erased faces opaque; --face-bake-debug prints\n"
            "a per-material census for one model; --models-out writes the remapped\n"
            "OB3s flat into DIR without touching the lane (works without --apply).\n"
            "--matte N compresses each baked face's lightness toward its material's\n"
            "mean by N%% and rolls highlights off at the gloss knee — mutes the HD\n"
            "frames' baked-in specular gradients toward the OSRS matte look.\n"
            "--wisp-alpha bounds the wisp inference, which overrides a material\n"
            "row's own alpha_mode 0: off (default) trusts the row and keeps the\n"
            "surface opaque, capped lets the guess soften a surface but not erase\n"
            "it, screen restores the uncapped v10-m60 behaviour that ghosted the\n"
            "arena rocks. Authored translucency (alpha_mode 2, cutout) is reached\n"
            "on other paths and is unaffected at every setting.\n",
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
        else if( strcmp(argv[i], "--face-color-bake") == 0 && i + 1 < argc )
        {
            i++;
            if( strcmp(argv[i], "tint") == 0 )
                g_face_bake_mode = FACE_BAKE_TINT;
            else if( strcmp(argv[i], "modulate") == 0 )
                g_face_bake_mode = FACE_BAKE_MODULATE;
            else if( strcmp(argv[i], "off") == 0 )
                g_face_bake_mode = FACE_BAKE_OFF;
            else
            {
                usage(argv[0]);
                return 1;
            }
        }
        else if( strcmp(argv[i], "--face-color-strength") == 0 && i + 1 < argc )
            g_face_bake_strength = atoi(argv[++i]);
        else if( strcmp(argv[i], "--face-alpha-bake") == 0 )
            g_face_alpha_bake = true;
        else if( strcmp(argv[i], "--no-face-alpha-bake") == 0 )
            g_face_alpha_bake = false;
        else if( strcmp(argv[i], "--face-alpha-gamma") == 0 && i + 1 < argc )
            g_face_alpha_gamma = atof(argv[++i]);
        else if( strcmp(argv[i], "--face-alpha-cap") == 0 && i + 1 < argc )
            g_face_alpha_cap = atoi(argv[++i]);
        else if( strcmp(argv[i], "--wisp-alpha") == 0 && i + 1 < argc )
        {
            const char* mode = argv[++i];
            if( strcmp(mode, "screen") == 0 )
                g_wisp_alpha = WISP_ALPHA_SCREEN;
            else if( strcmp(mode, "capped") == 0 )
                g_wisp_alpha = WISP_ALPHA_CAPPED;
            else if( strcmp(mode, "off") == 0 )
                g_wisp_alpha = WISP_ALPHA_OFF;
            else
            {
                fprintf(stderr, "--wisp-alpha: expected screen|capped|off\n");
                return 2;
            }
        }
        else if( strcmp(argv[i], "--face-synth") == 0 && i + 1 < argc )
            g_face_synth = strcmp(argv[++i], "off") != 0;
        else if( strcmp(argv[i], "--face-synth-k") == 0 && i + 1 < argc )
        {
            g_face_synth_k = atoi(argv[++i]);
            if( g_face_synth_k < 2 )
                g_face_synth_k = 2;
            if( g_face_synth_k > FACE_SYNTH_K_MAX )
                g_face_synth_k = FACE_SYNTH_K_MAX;
        }
        else if( strcmp(argv[i], "--face-synth-hole") == 0 && i + 1 < argc )
            g_face_synth_hole = atof(argv[++i]);
        else if( strcmp(argv[i], "--matte") == 0 && i + 1 < argc )
        {
            g_face_matte = atoi(argv[++i]);
            if( g_face_matte < 0 )
                g_face_matte = 0;
            else if( g_face_matte > 100 )
                g_face_matte = 100;
        }
        else if( strcmp(argv[i], "--face-bake-debug") == 0 && i + 1 < argc )
            g_face_bake_debug_model = atoi(argv[++i]);
        else if( strcmp(argv[i], "--face-dump") == 0 && i + 1 < argc )
        {
            g_face_dump = fopen(argv[++i], "w");
            if( !g_face_dump )
            {
                fprintf(stderr, "rs2012_material_bake: cannot open %s\n", argv[i]);
                return 1;
            }
            fprintf(
                g_face_dump,
                "model,face,source,blend,degenerate,coverage,mid_frac,"
                "authored,final,cx,cy,cz,base_hsl,base_r,base_g,base_b\n");
        }
        else if( strcmp(argv[i], "--frame-dump") == 0 && i + 1 < argc )
            g_frame_dump_dir = argv[++i];
        else if( strcmp(argv[i], "--models-out") == 0 && i + 1 < argc )
            g_models_out_dir = argv[++i];
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

    if( ok && g_face_dump )
    {
        /* Sidecar: the render-side material row for every mapped source, so a
         * face-dump analysis can join footprint stats to shader semantics. */
        fprintf(
            g_face_dump,
            "#mat,source,valid,alpha,small,disabled,brightness,blanch,shader_id,"
            "shader_param,shader_param2,combine_mode,alpha_mode,anim_u,anim_v,"
            "greyscale,blend_layer,frame_coverage,mean_r,mean_g,mean_b\n");
        for( int source = 0; source < MAX_TEXTURES; source++ )
        {
            if( !g_mapping[source].present || source >= materials->count )
                continue;
            const struct RSCache_Dat2Material* m = &materials->materials[source];
            const struct face_bake_mean* mean = face_bake_material_mean(source);
            fprintf(
                g_face_dump,
                "#mat,%d,%d,%d,%d,%d,%d,%d,%d,%d,%ld,%u,%u,%d,%d,%d,%d,%.4f,"
                "%.0f,%.0f,%.0f\n",
                source, m->valid, m->alpha, m->small, m->disabled, m->brightness,
                m->blanch, m->shader_id, m->shader_param, (long)m->shader_param2,
                m->combine_mode, m->alpha_mode, m->anim_u, m->anim_v,
                g_textures[source].greyscale, g_textures[source].blend_layer,
                mean->coverage, mean->r, mean->g, mean->b);
        }
    }
    if( ok && g_frame_dump_dir )
        for( int source = 0; source < MAX_TEXTURES; source++ )
        {
            if( !g_mapping[source].present || !g_textures[source].argb )
                continue;
            char path[1024];
            snprintf(path, sizeof(path), "%s/mat_%d.ppm", g_frame_dump_dir, source);
            FILE* rgb = fopen(path, "wb");
            snprintf(path, sizeof(path), "%s/mat_%d_a.pgm", g_frame_dump_dir, source);
            FILE* alpha = fopen(path, "wb");
            if( rgb && alpha )
            {
                fprintf(rgb, "P6\n%d %d\n255\n", BAKE_SIZE, BAKE_SIZE);
                fprintf(alpha, "P5\n%d %d\n255\n", BAKE_SIZE, BAKE_SIZE);
                for( int p = 0; p < BAKE_SIZE * BAKE_SIZE; p++ )
                {
                    uint32_t px = (uint32_t)g_textures[source].argb[p];
                    uint8_t row[3] = { (uint8_t)(px >> 16), (uint8_t)(px >> 8),
                                       (uint8_t)px };
                    fwrite(row, 1, 3, rgb);
                    uint8_t a = (uint8_t)(px >> 24);
                    fwrite(&a, 1, 1, alpha);
                }
            }
            if( rgb ) fclose(rgb);
            if( alpha ) fclose(alpha);
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
        if( g_face_bake_mode != FACE_BAKE_OFF )
            printf("  face_color_bake=%s strength=%d baked_faces=%d degenerate=%d "
                   "uncovered=%d alpha_faces=%d dropped_faces=%d\n",
                   g_face_bake_mode == FACE_BAKE_TINT ? "tint" : "modulate",
                   g_face_bake_strength, g_face_bake_faces, g_face_bake_degenerate,
                   g_face_bake_uncovered, g_face_bake_alpha_faces,
                   g_face_bake_dropped_faces);
        if( g_face_bake_mode != FACE_BAKE_OFF )
            printf("  wisp_alpha=%s wisp_faces=%d\n",
                   g_wisp_alpha == WISP_ALPHA_SCREEN   ? "screen"
                   : g_wisp_alpha == WISP_ALPHA_CAPPED ? "capped"
                                                       : "off",
                   g_face_bake_wisp_faces);
        if( g_face_synth_parents > 0 )
            printf("  face_synth: parents=%ld sub_faces=%ld holes=%ld (K=%d hole=%.2f)\n",
                   g_face_synth_parents, g_face_synth_faces, g_face_synth_holes,
                   g_face_synth_k, g_face_synth_hole);
        if( g_face_matte && g_face_matte_faces > 0 )
            printf("  matte=%d%%: faces=%ld mean_light_shift=%.2f (knee=%d)\n",
                   g_face_matte, g_face_matte_faces,
                   (double)g_face_matte_delta / (double)g_face_matte_faces,
                   FACE_MATTE_KNEE);
        if( g_face_bake_debug_model >= 0 )
        {
            printf("  face bake census for model %d "
                   "(material: faces deg bad_idx zero_area own_texco mean_cov "
                   "frame_cov mean_rgb greyscale blend):\n",
                   g_face_bake_debug_model);
            for( int source = 0; source < MAX_TEXTURES; source++ )
            {
                const struct face_bake_debug_row* row = &g_face_bake_debug[source];
                if( !row->faces ) continue;
                const struct face_bake_mean* mean = face_bake_material_mean(source);
                printf("    %-5d %5d %5d %5d %5d %5d  %5.2f  %5.2f  (%.0f,%.0f,%.0f)"
                       "  %d %d\n",
                       source, row->faces, row->degenerate, row->bad_index,
                       row->zero_area, row->texco_own,
                       row->faces ? row->coverage_sum / row->faces : 0.0,
                       mean->coverage, mean->r, mean->g, mean->b,
                       g_textures[source].greyscale, g_textures[source].blend_layer);
            }
        }
    }

    if( ok && g_models_out_dir )
        ok = write_model_outputs_dir(g_models_out_dir, model_outputs, models.count);

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
