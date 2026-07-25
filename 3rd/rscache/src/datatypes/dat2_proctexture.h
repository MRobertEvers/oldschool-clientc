#ifndef RSCACHE_DAT2_PROCTEXTURE_H
#define RSCACHE_DAT2_PROCTEXTURE_H

#include "rscache_profile.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Procedural textures — the RS2 (643-era) texture system.
 *
 * ## Why this exists alongside `texture.c`
 *
 * There are **three** texture systems in the lineage, and which one a cache uses is an era
 * question, not a container one. rs-map-viewer's `Dat2CacheLoaderFactory.getTextureLoader`
 * states the gates outright:
 *
 *   SpriteTextureLoader      game == oldschool, OR game == runescape && revision < 474
 *   ProceduralTextureLoader  revision >= 474 AND a materials index (table 26) exists
 *   OldProceduralTextureLoader  otherwise
 *
 * `texture.c` implements the first: a texture is a list of sprite ids that get composited.
 * This file implements the second. A 643 texture is not an image at all — it is a small
 * **program**: a DAG of up to 255 operations (noise fields, gradients, brick and weave
 * generators, arithmetic, colour transforms, a vector rasteriser) which is *evaluated* to
 * produce pixels at whatever size the caller wants.
 *
 * This is why 643 scenery rendered invisible before this landed: `texture.c` rejects a 643
 * record (it is a material program, not a sprite list), all 70 texture loads failed, and the
 * raster skips faces whose texture is absent — so every textured face of all 1,880 scenery
 * models was dropped while the flat-coloured terrain underlays still drew. See B12/B18 in
 * EXCEPTIONS.md.
 *
 * ## Two archives, two structures
 *
 * **Materials (table 26, group 0, file 0)** — one flat record describing *every* material,
 * stored **column-major**: a `u16 count`, then one full pass over all materials per field.
 * So it is not "record 0 then record 1"; it is "every material's `valid` byte, then every
 * material's `small` byte, ...". A field's presence is positional, and later eras append
 * columns, which is why `buffer.remaining > 0` gates the second block.
 *
 * **Texture definitions (table 9, one archive per id)** — the operation graph, then a short
 * tail (flip/repeat/combine/animation). Operations carry their input wiring as *indices into
 * the same array*, so the graph is resolved after all operations are read.
 *
 * ## Era gates within this system
 *
 * Recorded on the profile rather than rediscovered per call site:
 *
 *   revision >= 537  the monochrome ("mod") operation slot is present in the tail
 *   revision >= 555  material animU/animV win over the texture definition's own
 *   revision >= 582  material combine_mode + shader_param2 columns present
 *   revision >= 629  alpha_mode column present, and the `alpha` column is *absent*
 *
 * 643 satisfies all four. They are exposed as flags so a different RS2 revision decodes
 * correctly without editing this file.
 */

/** Operation type ids, as stored. Names follow rs-map-viewer's factory. */
enum RSCache_ProcTexOpType
{
    RSCACHE_PROCTEX_CONST_MONO = 0,
    RSCACHE_PROCTEX_CONST_COLOUR = 1,
    RSCACHE_PROCTEX_H_GRADIENT = 2,
    RSCACHE_PROCTEX_V_GRADIENT = 3,
    RSCACHE_PROCTEX_BRICKS = 4,
    RSCACHE_PROCTEX_BLUR = 5,
    RSCACHE_PROCTEX_CLAMP = 6,
    RSCACHE_PROCTEX_ARITHMETIC = 7,
    RSCACHE_PROCTEX_CURVE = 8,
    RSCACHE_PROCTEX_MIRROR = 9,
    RSCACHE_PROCTEX_GRADIENT = 10,
    RSCACHE_PROCTEX_COLOUR_STRIP = 11,
    RSCACHE_PROCTEX_DIAGONAL_GRADIENT = 12,
    RSCACHE_PROCTEX_PSEUDO_RANDOM_NOISE = 13,
    RSCACHE_PROCTEX_WEAVE = 14,
    RSCACHE_PROCTEX_VORONOI_NOISE = 15,
    RSCACHE_PROCTEX_HERRINGBONE = 16,
    RSCACHE_PROCTEX_HSL = 17,
    RSCACHE_PROCTEX_TILING_SPRITE = 18,
    RSCACHE_PROCTEX_TRIG_WARP = 19,
    RSCACHE_PROCTEX_TILING = 20,
    RSCACHE_PROCTEX_MIXER = 21,
    RSCACHE_PROCTEX_INVERT = 22,
    RSCACHE_PROCTEX_KALEIDOSCOPE = 23,
    RSCACHE_PROCTEX_GRAYSCALE = 24,
    RSCACHE_PROCTEX_BRIGHTNESS = 25,
    RSCACHE_PROCTEX_BINARY = 26,
    RSCACHE_PROCTEX_SQUARE_WAVEFORM = 27,
    RSCACHE_PROCTEX_IRREGULAR_BRICKS = 28,
    RSCACHE_PROCTEX_RASTERIZER = 29,
    RSCACHE_PROCTEX_RANGE = 30,
    RSCACHE_PROCTEX_MANDELBROT = 31,
    RSCACHE_PROCTEX_EMBOSS = 32,
    RSCACHE_PROCTEX_COLOUR_EDGE_DETECT = 33,
    RSCACHE_PROCTEX_PERLIN_NOISE = 34,
    RSCACHE_PROCTEX_MONO_EDGE_DETECT = 35,
    RSCACHE_PROCTEX_TEXTURE_SOURCE = 36,
    RSCACHE_PROCTEX_OP37 = 37,
    RSCACHE_PROCTEX_LINE_NOISE = 38,
    RSCACHE_PROCTEX_SPRITE_SOURCE = 39,
    RSCACHE_PROCTEX_OP_COUNT = 40,
};

/** Decode flags, from the profile. See the era-gate table above. */
#define RSCACHE_PROCTEX_HAS_MOD_OP 0x1u
#define RSCACHE_PROCTEX_MATERIAL_ANIM_WINS 0x2u
#define RSCACHE_PROCTEX_HAS_COMBINE_MODE 0x4u
#define RSCACHE_PROCTEX_HAS_ALPHA_BLENDING 0x8u

/** Max operations in one graph — the count is a u8, so this is the format's own bound. */
#define RSCACHE_PROCTEX_MAX_OPS 256
/** Max inputs any operation takes (mixer and trig-warp take 3). */
#define RSCACHE_PROCTEX_MAX_INPUTS 4

/** A curve / gradient control point. */
struct RSCache_ProcTexMarker
{
    int32_t key;
    int32_t value;
};

/** A gradient stop: position plus a pre-shifted RGB triple. */
struct RSCache_ProcTexGradientStop
{
    int32_t position;
    int32_t r;
    int32_t g;
    int32_t b;
};

/** One rasteriser primitive. `kind` selects which fields are meaningful. */
struct RSCache_ProcTexShape
{
    /** 0 = line, 1 = bezier, 2 = rectangle, 3 = ellipse. */
    uint8_t kind;
    uint8_t outline_width;
    int32_t x[4];
    int32_t y[4];
    int32_t fill_colour;
    int32_t outline_colour;
};

/**
 * One node of the graph.
 *
 * Fields are a union keyed on `type`. `field_present` records which field ids the stream
 * carried, in `field_order`, so an encoder can reproduce the record: the format writes a
 * count then arbitrary field ids, and a decoder that only kept values could not tell an
 * absent field from one written as its default.
 */
struct RSCache_ProcTexOperation
{
    uint8_t type;
    /** Authored slot id. Not the array index and not used for wiring — kept for round-trip. */
    uint8_t id;
    /** Cache slot count; 0xFF means "one slot per output line". */
    uint8_t cache_size;
    uint8_t input_count;
    uint8_t inputs[RSCACHE_PROCTEX_MAX_INPUTS];
    bool is_monochrome;

    /** Provenance: which field ids appeared, and in what order. */
    uint32_t field_present;
    uint8_t field_order[16];
    uint8_t field_count;

    union
    {
        struct
        {
            int32_t constant;
        } const_mono;
        struct
        {
            int32_t r;
            int32_t g;
            int32_t b;
        } const_colour;
        struct
        {
            uint8_t operation;
        } arithmetic;
        struct
        {
            uint8_t h_extent;
            uint8_t v_extent;
        } blur;
        struct
        {
            int32_t min;
            int32_t max;
        } clamp;
        struct
        {
            int32_t min_value;
            int32_t max_value;
        } binary;
        struct
        {
            int32_t field0;
            int32_t field1;
        } range;
        struct
        {
            uint8_t interp_mode;
            uint8_t marker_count;
            struct RSCache_ProcTexMarker markers[16];
        } curve;
        struct
        {
            /** 0 means the stops are stored inline; anything else names a built-in preset. */
            uint8_t preset;
            uint8_t stop_count;
            struct RSCache_ProcTexGradientStop stops[32];
        } gradient;
        struct
        {
            int32_t r;
            int32_t g;
            int32_t b;
        } colour_strip;
        struct
        {
            int32_t delta_hue;
            int32_t delta_saturation;
            int32_t delta_light;
        } hsl;
        struct
        {
            bool invert_horizontal;
            bool invert_vertical;
        } mirror;
        struct
        {
            uint8_t mix_mode;
            uint8_t interpolation_mode;
            uint8_t steepness;
            /** Fields 2, 4 and 5. Present in real records but absent from the reference
             *  decode, so their meanings are unattested; kept so consumption stays exact. */
            uint8_t field2;
            uint8_t field4;
            uint8_t field5;
        } diagonal_gradient;
        struct
        {
            uint8_t field0;
            uint8_t seed;
            int32_t field2;
            int32_t field3;
            int32_t field4;
            int32_t field5;
            int32_t field6;
            int32_t field7;
        } bricks;
        struct
        {
            uint8_t seed;
            int32_t field1;
            int32_t field2;
            int32_t field3;
            int32_t field4;
            int32_t field5;
            uint8_t field6;
            int32_t field7;
            int32_t field8;
        } irregular_bricks;
        struct
        {
            uint8_t scale_x;
            uint8_t scale_y;
            int32_t ratio;
        } herringbone;
        struct
        {
            int32_t thickness;
        } weave;
        struct
        {
            bool field0;
            uint8_t octaves;
            int32_t field2;
            uint8_t seed;
            uint8_t scale_x;
            uint8_t scale_y;
            int16_t amplitudes[64];
            uint8_t amplitude_count;
        } perlin;
        struct
        {
            uint8_t rng_seed;
            int32_t field2;
            uint8_t field3;
            uint8_t field4;
            uint8_t scale_x;
            uint8_t scale_y;
        } voronoi;
        struct
        {
            uint8_t seed;
            int32_t count;
            uint8_t length;
            int32_t min_angle;
            int32_t max_angle;
        } line_noise;
        struct
        {
            int32_t max_value;
            int32_t red_factor;
            int32_t green_factor;
            int32_t blue_factor;
            int32_t colour_delta[3];
        } brightness;
        struct
        {
            int32_t multiplier;
            bool field1;
        } edge_detect;
        struct
        {
            int32_t field0;
            int32_t field1;
            int32_t field2;
        } emboss;
        struct
        {
            int32_t hypotenuse_multiplier;
        } trig_warp;
        struct
        {
            uint8_t tile_count_h;
            uint8_t tile_count_v;
        } tiling;
        struct
        {
            int32_t sprite_id;
        } sprite_source;
        struct
        {
            int32_t texture_id;
        } texture_source;
        struct
        {
            int32_t field0;
            int32_t field1;
            uint8_t field2;
        } square_waveform;
        struct
        {
            int32_t field[4];
        } mandelbrot;
        struct
        {
            int32_t field[7];
        } op37;
        struct
        {
            uint8_t shape_count;
            struct RSCache_ProcTexShape* shapes;
        } rasterizer;
    } u;
};

/** A decoded texture program. */
struct RSCache_Dat2ProcTexture
{
    int32_t id;

    /** Usable operations. A short decode lowers this; `_alloc_count` keeps the array size so
     *  the free path still reaches an operation that allocated before it failed. */
    int32_t operation_count;
    int32_t _alloc_count;
    struct RSCache_ProcTexOperation* operations;

    /** Indices into `operations`, or -1. */
    int32_t colour_op;
    int32_t alpha_op;
    int32_t monochrome_op;

    /* Tail. */
    bool bool1;
    bool flip_v;
    bool repeat_s;
    bool repeat_t;
    /** 0 = modulate, 1 = add, 2 = subtract, 3 = add-signed. */
    uint8_t combine_mode;
    int32_t anim_u;
    int32_t anim_v;

    /*
     * Three further trailing bytes that rs-map-viewer does not read — its
     * ProceduralTextureDefinition stops after animV, which is safe for it because it never
     * checks consumption. They are unmistakably present and structured: over all 1,158
     * decodable 643 textures, byte 0 is 0x22 in 1,151 of them (else 0x00/0x02/0x20 — a
     * bitmask), byte 1 is a small count 0..13 (935 zero), byte 2 is 0 or 1.
     *
     * Named by observed shape because their MEANING IS UNVERIFIED — no reference in reach
     * reads them. They are consumed so that exact consumption still validates the record;
     * leaving them unread would make every texture look 3 bytes short and mask a real
     * misalignment appearing later.
     *
     * Do not wire these into rendering without a reference. The tail *before* them is
     * confirmed correct: anim_u/anim_v match the independently-decoded materials table for
     * all 26 textures where the material declares a non-zero animation, negative values
     * included.
     */
    uint8_t trailing_flags;
    uint8_t trailing_count;
    bool trailing_bool;
    bool has_trailing;

    /** Deduplicated dependency lists, gathered from the operations. */
    int32_t* sprite_dependencies;
    int32_t sprite_dependency_count;
    int32_t* texture_dependencies;
    int32_t texture_dependency_count;

    /** Bytes consumed. Short of the archive length means an unknown field mis-sized. */
    int32_t _consumed;
};

/** One material: the render-side properties of a texture id. */
struct RSCache_Dat2Material
{
    bool exists;
    bool valid;
    bool alpha;
    bool small;
    bool disabled;
    int8_t brightness;
    int8_t blanch;
    int8_t shader_id;
    int8_t shader_param;
    int32_t average_hsl;

    int8_t anim_u;
    int8_t anim_v;
    bool flip_v;
    int8_t mipmap;
    bool repeat_s;
    bool repeat_t;
    bool float_texture;
    uint8_t combine_mode;
    int32_t shader_param2;
    uint8_t alpha_mode;
};

/** The whole materials table. */
struct RSCache_Dat2MaterialTable
{
    int32_t count;
    struct RSCache_Dat2Material* materials;
    /** True when the optional second column block was present. */
    bool has_extended;
    int32_t _consumed;
};

/** Decode flags for this cache — RSCACHE_PROCTEX_* bitmask. */
uint32_t
RSCache_Dat2ProcTextureFlags(const struct RSCache* cache);

/**
 * Does this cache use procedural textures rather than sprite-backed ones?
 *
 * `materials_table_present` must say whether table 26 exists, because the reference gates on
 * exactly that and not on the revision alone — a cache can be new enough and still lack it.
 */
bool
RSCache_Dat2UsesProcTextures(
    const struct RSCache* cache,
    bool materials_table_present);

struct RSCache_Dat2MaterialTable*
RSCache_Dat2MaterialTableNewDecode(
    const char* data,
    int data_size,
    uint32_t flags);

void
RSCache_Dat2MaterialTableFree(struct RSCache_Dat2MaterialTable* table);

struct RSCache_Dat2ProcTexture*
RSCache_Dat2ProcTextureNewDecode(
    const char* data,
    int data_size,
    int texture_id,
    uint32_t flags);

void
RSCache_Dat2ProcTextureFree(struct RSCache_Dat2ProcTexture* texture);

/** Input count and monochrome-ness for an operation type; false for an unknown type. */
bool
RSCache_ProcTexOpSignature(
    int op_type,
    int* out_input_count,
    bool* out_is_monochrome);

/** Human-readable operation name, for diagnostics. Never NULL. */
const char*
RSCache_ProcTexOpName(int op_type);

#endif
