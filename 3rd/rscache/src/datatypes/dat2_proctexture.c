#include "dat2_proctexture.h"

#include "rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* g1/g1b/g2/g2b/g3/g4 come from rsbuffer.h, like every other datatype here. */

uint32_t
RSCache_Dat2ProcTextureFlags(const struct RSCache* cache)
{
    assert(cache);
    uint32_t flags = 0u;

    /*
     * These are RS2-lineage revisions, routed through RSCache_RevisionAtLeastRs2
     * so an OldSchool profile never satisfies them (D16). 537/555/582/629 are
     * RuneScape numbers and mean nothing on the OldSchool line.
     */
    if( RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_TEXTURE, 537, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_PROCTEX_HAS_MOD_OP;
    if( RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_TEXTURE, 555, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_PROCTEX_MATERIAL_ANIM_WINS;
    if( RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_TEXTURE, 582, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_PROCTEX_HAS_COMBINE_MODE;
    if( RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_TEXTURE, 629, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_PROCTEX_HAS_ALPHA_BLENDING;

    return flags;
}

bool
RSCache_Dat2UsesProcTextures(
    const struct RSCache* cache,
    bool materials_table_present)
{
    assert(cache);

    /* OldSchool never uses this system, whatever its revision. */
    if( RSCache_IsOsrs(cache) )
        return false;
    if( RSCache_IsDat1(cache) )
        return false;

    /* The reference gates on the materials index existing, not on the revision alone. Keep
     * that: a cache can be new enough and still ship no table 26, and guessing would swap in
     * a decoder that rejects every record. */
    if( !materials_table_present )
        return false;

    /* Procedural textures arrive at RS2 revision 474. A declared revision below
     * that predates the system; 643 clears it. */
    if( !RSCache_RevisionAtLeastRs2(
            cache, RSCACHE_TYPE_TEXTURE, 474, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        return false;

    return RSCache_IsRs2Dat2(cache);
}

/* --- operation signature table -------------------------------------------------------- */

struct proctex_op_info
{
    const char* name;
    uint8_t input_count;
    bool is_monochrome;
    bool known;
};

/* Input counts and monochrome-ness come from each operation's constructor in
 * rs-map-viewer (`super(inputCount, isMonochrome)`). They are part of the *format*, not a
 * rendering choice: the input count decides how many wiring bytes follow the fields, so
 * getting one wrong desyncs the rest of the graph. */
static const struct proctex_op_info PROCTEX_OPS[RSCACHE_PROCTEX_OP_COUNT] = {
    [RSCACHE_PROCTEX_CONST_MONO] = { "const_mono", 0, true, true },
    [RSCACHE_PROCTEX_CONST_COLOUR] = { "const_colour", 0, false, true },
    [RSCACHE_PROCTEX_H_GRADIENT] = { "h_gradient", 0, true, true },
    [RSCACHE_PROCTEX_V_GRADIENT] = { "v_gradient", 0, true, true },
    [RSCACHE_PROCTEX_BRICKS] = { "bricks", 0, true, true },
    [RSCACHE_PROCTEX_BLUR] = { "blur", 1, false, true },
    [RSCACHE_PROCTEX_CLAMP] = { "clamp", 1, false, true },
    [RSCACHE_PROCTEX_ARITHMETIC] = { "arithmetic", 2, false, true },
    [RSCACHE_PROCTEX_CURVE] = { "curve", 1, true, true },
    [RSCACHE_PROCTEX_MIRROR] = { "mirror", 1, false, true },
    [RSCACHE_PROCTEX_GRADIENT] = { "gradient", 1, false, true },
    [RSCACHE_PROCTEX_COLOUR_STRIP] = { "colour_strip", 1, false, true },
    [RSCACHE_PROCTEX_DIAGONAL_GRADIENT] = { "diagonal_gradient", 0, true, true },
    [RSCACHE_PROCTEX_PSEUDO_RANDOM_NOISE] = { "pseudo_random_noise", 0, true, true },
    [RSCACHE_PROCTEX_WEAVE] = { "weave", 0, true, true },
    [RSCACHE_PROCTEX_VORONOI_NOISE] = { "voronoi_noise", 0, true, true },
    [RSCACHE_PROCTEX_HERRINGBONE] = { "herringbone", 0, true, true },
    [RSCACHE_PROCTEX_HSL] = { "hsl", 1, false, true },
    [RSCACHE_PROCTEX_TILING_SPRITE] = { "tiling_sprite", 0, false, true },
    [RSCACHE_PROCTEX_TRIG_WARP] = { "trig_warp", 3, false, true },
    [RSCACHE_PROCTEX_TILING] = { "tiling", 1, false, true },
    [RSCACHE_PROCTEX_MIXER] = { "mixer", 3, false, true },
    [RSCACHE_PROCTEX_INVERT] = { "invert", 1, false, true },
    [RSCACHE_PROCTEX_KALEIDOSCOPE] = { "kaleidoscope", 1, false, true },
    [RSCACHE_PROCTEX_GRAYSCALE] = { "grayscale", 1, true, true },
    [RSCACHE_PROCTEX_BRIGHTNESS] = { "brightness", 1, false, true },
    [RSCACHE_PROCTEX_BINARY] = { "binary", 1, true, true },
    [RSCACHE_PROCTEX_SQUARE_WAVEFORM] = { "square_waveform", 0, true, true },
    [RSCACHE_PROCTEX_IRREGULAR_BRICKS] = { "irregular_bricks", 0, true, true },
    [RSCACHE_PROCTEX_RASTERIZER] = { "rasterizer", 0, true, true },
    [RSCACHE_PROCTEX_RANGE] = { "range", 1, false, true },
    [RSCACHE_PROCTEX_MANDELBROT] = { "mandelbrot", 0, true, true },
    [RSCACHE_PROCTEX_EMBOSS] = { "emboss", 1, true, true },
    [RSCACHE_PROCTEX_COLOUR_EDGE_DETECT] = { "colour_edge_detect", 1, false, true },
    [RSCACHE_PROCTEX_PERLIN_NOISE] = { "perlin_noise", 0, true, true },
    [RSCACHE_PROCTEX_MONO_EDGE_DETECT] = { "mono_edge_detect", 1, true, true },
    [RSCACHE_PROCTEX_TEXTURE_SOURCE] = { "texture_source", 0, false, true },
    [RSCACHE_PROCTEX_OP37] = { "op37", 0, true, true },
    [RSCACHE_PROCTEX_LINE_NOISE] = { "line_noise", 0, true, true },
    [RSCACHE_PROCTEX_SPRITE_SOURCE] = { "sprite_source", 0, false, true },
};

bool
RSCache_ProcTexOpSignature(
    int op_type,
    int* out_input_count,
    bool* out_is_monochrome)
{
    if( op_type < 0 || op_type >= RSCACHE_PROCTEX_OP_COUNT || !PROCTEX_OPS[op_type].known )
        return false;
    if( out_input_count )
        *out_input_count = PROCTEX_OPS[op_type].input_count;
    if( out_is_monochrome )
        *out_is_monochrome = PROCTEX_OPS[op_type].is_monochrome;
    return true;
}

const char*
RSCache_ProcTexOpName(int op_type)
{
    if( op_type < 0 || op_type >= RSCACHE_PROCTEX_OP_COUNT || !PROCTEX_OPS[op_type].name )
        return "unknown";
    return PROCTEX_OPS[op_type].name;
}

/* --- materials ------------------------------------------------------------------------ */

/*
 * The materials record is COLUMN-MAJOR: one pass over every material per field, not one
 * record per material. Each column is also skipped for materials whose `exists` byte was 0,
 * so a column is not a fixed stride — it must be walked with the same predicate the writer
 * used. Reading it row-major would desync on the first absent material.
 */
struct RSCache_Dat2MaterialTable*
RSCache_Dat2MaterialTableNewDecode(
    const char* data,
    int data_size,
    uint32_t flags)
{
    struct RSCache_Dat2MaterialTable* table;
    struct RSCache_Buffer buffer;
    int count;
    int i;

    if( !data || data_size < 2 )
        return NULL;

    buffer.data = (uint8_t*)data;
    buffer.position = 0;
    buffer.size = (uint32_t)data_size;

    count = g2(&buffer);
    if( count < 0 || count > 0xFFFF )
        return NULL;

    table = calloc(1, sizeof(*table));
    if( !table )
        return NULL;
    table->count = count;
    table->materials = calloc((size_t)(count > 0 ? count : 1), sizeof(*table->materials));
    if( !table->materials )
    {
        free(table);
        return NULL;
    }

#define MATERIAL_COLUMN(body)                       \
    for( i = 0; i < count; i++ )                    \
    {                                               \
        struct RSCache_Dat2Material* m = &table->materials[i]; \
        if( !m->exists )                            \
            continue;                               \
        body;                                       \
    }

    for( i = 0; i < count; i++ )
        table->materials[i].exists = (g1(&buffer) == 1);

    MATERIAL_COLUMN(m->valid = (g1(&buffer) == 1))

    /* The `alpha` column exists only BEFORE alpha blending arrives; from 629 it is replaced
     * by the alpha_mode column at the end. Reading it unconditionally shifts every later
     * column by one material-worth of bytes. */
    if( !(flags & RSCACHE_PROCTEX_HAS_ALPHA_BLENDING) )
        MATERIAL_COLUMN(m->alpha = (g1(&buffer) == 1))

    MATERIAL_COLUMN(m->small = (g1(&buffer) == 1))
    MATERIAL_COLUMN(m->disabled = (g1(&buffer) == 1))
    MATERIAL_COLUMN(m->brightness = (int8_t)g1b(&buffer))
    MATERIAL_COLUMN(m->blanch = (int8_t)g1b(&buffer))
    MATERIAL_COLUMN(m->shader_id = (int8_t)g1b(&buffer))
    MATERIAL_COLUMN(m->shader_param = (int8_t)g1b(&buffer))
    MATERIAL_COLUMN(m->average_hsl = g2(&buffer))

    /* Second block is optional and detected by bytes remaining, exactly as the reference
     * does — there is no count or flag for it. */
    if( buffer.position < buffer.size )
    {
        table->has_extended = true;

        MATERIAL_COLUMN(m->anim_u = (int8_t)g1b(&buffer))
        MATERIAL_COLUMN(m->anim_v = (int8_t)g1b(&buffer))
        /* An unused column the reference reads and discards. Kept as a skip so the stride
         * stays right rather than silently absorbing it into the next field. */
        MATERIAL_COLUMN((void)g1b(&buffer))
        MATERIAL_COLUMN(m->flip_v = (g1(&buffer) == 1))
        MATERIAL_COLUMN(m->mipmap = (int8_t)g1b(&buffer))
        MATERIAL_COLUMN(m->repeat_s = (g1(&buffer) == 1))
        MATERIAL_COLUMN(m->repeat_t = (g1(&buffer) == 1))
        MATERIAL_COLUMN(m->float_texture = (g1(&buffer) == 1))

        if( flags & RSCACHE_PROCTEX_HAS_COMBINE_MODE )
        {
            MATERIAL_COLUMN(m->combine_mode = (uint8_t)g1(&buffer))
            MATERIAL_COLUMN(m->shader_param2 = g4(&buffer))
        }
        if( flags & RSCACHE_PROCTEX_HAS_ALPHA_BLENDING )
            MATERIAL_COLUMN(m->alpha_mode = (uint8_t)g1(&buffer))
    }

#undef MATERIAL_COLUMN

    table->_consumed = (int32_t)buffer.position;
    return table;
}

void
RSCache_Dat2MaterialTableFree(struct RSCache_Dat2MaterialTable* table)
{
    if( !table )
        return;
    free(table->materials);
    free(table);
}

/* --- operation defaults --------------------------------------------------------------- */

/*
 * Per-operation field defaults, transcribed from each class's member initialisers in
 * rs-map-viewer.
 *
 * These are NOT merely cosmetic. For perlin noise they are **load-bearing on the stream
 * layout**: field 2, when negative, is followed by `field1` inline u16 amplitudes, and
 * `field1` defaults to 4. A record that carries field 2 but not field 1 therefore has four
 * shorts we must consume. Zero-initialising instead of defaulting made us read none of them
 * and desync 8 bytes into the rest of the graph — which surfaced as absurd "unknown field"
 * ids (255, 196, 234) rather than as a clean failure. That accounted for 74 of 88 broken
 * textures.
 *
 * Everything with a non-zero default is listed even where only the renderer cares, because a
 * silently-zero default is the same class of bug one step later.
 */
static void
proctex_init_defaults(struct RSCache_ProcTexOperation* op)
{
    switch( op->type )
    {
    case RSCACHE_PROCTEX_CONST_MONO:
        op->u.const_mono.constant = 4096;
        break;
    case RSCACHE_PROCTEX_ARITHMETIC:
        op->u.arithmetic.operation = 6;
        break;
    case RSCACHE_PROCTEX_BLUR:
        op->u.blur.h_extent = 1;
        op->u.blur.v_extent = 1;
        break;
    case RSCACHE_PROCTEX_CLAMP:
        op->u.clamp.min = 0;
        op->u.clamp.max = 4096;
        break;
    case RSCACHE_PROCTEX_BINARY:
        op->u.binary.min_value = 0;
        op->u.binary.max_value = 4096;
        break;
    case RSCACHE_PROCTEX_RANGE:
        op->u.range.field0 = 1024;
        op->u.range.field1 = 3072;
        break;
    case RSCACHE_PROCTEX_COLOUR_STRIP:
        op->u.colour_strip.r = 4096;
        op->u.colour_strip.g = 4096;
        op->u.colour_strip.b = 4096;
        break;
    case RSCACHE_PROCTEX_MIRROR:
        op->u.mirror.invert_horizontal = true;
        op->u.mirror.invert_vertical = true;
        break;
    case RSCACHE_PROCTEX_DIAGONAL_GRADIENT:
        op->u.diagonal_gradient.steepness = 1;
        break;
    case RSCACHE_PROCTEX_BRICKS:
        op->u.bricks.field0 = 4;
        op->u.bricks.seed = 8;
        op->u.bricks.field2 = 409;
        op->u.bricks.field3 = 204;
        op->u.bricks.field4 = 1024;
        op->u.bricks.field5 = 0;
        op->u.bricks.field6 = 81;
        op->u.bricks.field7 = 1024;
        break;
    case RSCACHE_PROCTEX_IRREGULAR_BRICKS:
        op->u.irregular_bricks.field1 = 1024;
        op->u.irregular_bricks.field2 = 2048;
        op->u.irregular_bricks.field3 = 409;
        op->u.irregular_bricks.field4 = 819;
        op->u.irregular_bricks.field5 = 1024;
        op->u.irregular_bricks.field7 = 1024;
        op->u.irregular_bricks.field8 = 1024;
        break;
    case RSCACHE_PROCTEX_HERRINGBONE:
        op->u.herringbone.scale_x = 1;
        op->u.herringbone.scale_y = 1;
        op->u.herringbone.ratio = 204;
        break;
    case RSCACHE_PROCTEX_WEAVE:
        op->u.weave.thickness = 585;
        break;
    case RSCACHE_PROCTEX_PERLIN_NOISE:
        /* `octaves` is the one that also drives stream length — see the note above. */
        op->u.perlin.field0 = true;
        op->u.perlin.octaves = 4;
        op->u.perlin.field2 = 1638;
        op->u.perlin.scale_x = 4;
        op->u.perlin.scale_y = 4;
        break;
    case RSCACHE_PROCTEX_VORONOI_NOISE:
        op->u.voronoi.field2 = 2048;
        op->u.voronoi.field3 = 2;
        op->u.voronoi.field4 = 1;
        op->u.voronoi.scale_x = 5;
        op->u.voronoi.scale_y = 5;
        break;
    case RSCACHE_PROCTEX_LINE_NOISE:
        op->u.line_noise.count = 2000;
        op->u.line_noise.length = 16;
        op->u.line_noise.min_angle = 0;
        op->u.line_noise.max_angle = 4096;
        break;
    case RSCACHE_PROCTEX_BRIGHTNESS:
        op->u.brightness.max_value = 409;
        op->u.brightness.red_factor = 4096;
        op->u.brightness.green_factor = 4096;
        op->u.brightness.blue_factor = 4096;
        break;
    case RSCACHE_PROCTEX_COLOUR_EDGE_DETECT:
        op->u.edge_detect.multiplier = 4096;
        op->u.edge_detect.field1 = true;
        break;
    case RSCACHE_PROCTEX_MONO_EDGE_DETECT:
        op->u.edge_detect.multiplier = 4096;
        break;
    case RSCACHE_PROCTEX_EMBOSS:
        op->u.emboss.field0 = 4096;
        op->u.emboss.field1 = 3216;
        op->u.emboss.field2 = 3216;
        break;
    case RSCACHE_PROCTEX_TRIG_WARP:
        op->u.trig_warp.hypotenuse_multiplier = 32768;
        break;
    case RSCACHE_PROCTEX_TILING:
        op->u.tiling.tile_count_h = 4;
        op->u.tiling.tile_count_v = 4;
        break;
    case RSCACHE_PROCTEX_SPRITE_SOURCE:
    case RSCACHE_PROCTEX_TILING_SPRITE:
        op->u.sprite_source.sprite_id = -1;
        break;
    case RSCACHE_PROCTEX_TEXTURE_SOURCE:
        op->u.texture_source.texture_id = -1;
        break;
    case RSCACHE_PROCTEX_SQUARE_WAVEFORM:
        op->u.square_waveform.field0 = 10;
        op->u.square_waveform.field1 = 2048;
        op->u.square_waveform.field2 = 0;
        break;
    case RSCACHE_PROCTEX_MANDELBROT:
        op->u.mandelbrot.field[0] = 1365;
        op->u.mandelbrot.field[1] = 20;
        break;
    case RSCACHE_PROCTEX_OP37:
        op->u.op37.field[0] = 2048;
        op->u.op37.field[3] = 2048;
        op->u.op37.field[4] = 12288;
        op->u.op37.field[5] = 4096;
        op->u.op37.field[6] = 8192;
        break;
    default:
        break;
    }
}

/* --- operation field decode ----------------------------------------------------------- */

static void
proctex_decode_rasterizer(
    struct RSCache_ProcTexOperation* op,
    struct RSCache_Buffer* buffer)
{
    int count = g1(buffer);
    int i;

    op->u.rasterizer.shape_count = 0;
    op->u.rasterizer.shapes = NULL;
    if( count <= 0 )
        return;

    op->u.rasterizer.shapes = calloc((size_t)count, sizeof(*op->u.rasterizer.shapes));
    if( !op->u.rasterizer.shapes )
        return;
    op->u.rasterizer.shape_count = (uint8_t)count;

    for( i = 0; i < count; i++ )
    {
        struct RSCache_ProcTexShape* shape = &op->u.rasterizer.shapes[i];
        int kind = g1(buffer);
        shape->kind = (uint8_t)kind;
        switch( kind )
        {
        /*
         * A line and a bezier carry ONE colour, and it is the stroke: the reference builds
         * them with `super(-1, color, outlineWidth)`, i.e. fill absent. Normalising here
         * rather than at the draw site keeps -1 meaning "not drawn" for every shape.
         */
        case 0: /* line: two points, colour, width */
            shape->x[0] = g2b(buffer);
            shape->y[0] = g2b(buffer);
            shape->x[1] = g2b(buffer);
            shape->y[1] = g2b(buffer);
            shape->fill_colour = -1;
            shape->outline_colour = g3(buffer);
            shape->outline_width = (uint8_t)g1(buffer);
            break;
        case 1: /* cubic bezier: four points, colour, width */
            shape->x[0] = g2b(buffer);
            shape->y[0] = g2b(buffer);
            shape->x[1] = g2b(buffer);
            shape->y[1] = g2b(buffer);
            shape->x[2] = g2b(buffer);
            shape->y[2] = g2b(buffer);
            shape->x[3] = g2b(buffer);
            shape->y[3] = g2b(buffer);
            shape->fill_colour = -1;
            shape->outline_colour = g3(buffer);
            shape->outline_width = (uint8_t)g1(buffer);
            break;
        case 2: /* rectangle: two corners, fill, outline, width */
        case 3: /* ellipse: centre, radii, fill, outline, width — same byte layout */
            shape->x[0] = g2b(buffer);
            shape->y[0] = g2b(buffer);
            shape->x[1] = g2b(buffer);
            shape->y[1] = g2b(buffer);
            shape->fill_colour = g3(buffer);
            shape->outline_colour = g3(buffer);
            shape->outline_width = (uint8_t)g1(buffer);
            break;
        default:
            /* Unknown primitive: we cannot know its width, so stop rather than guess and
             * desync the remaining shapes and the graph tail behind them. */
            op->u.rasterizer.shape_count = (uint8_t)i;
            return;
        }
    }
}

/*
 * Decode one field of one operation.
 *
 * Returns false for a field id this operation does not define. That MUST stop the record:
 * field payload widths vary (u8 / u16 / u24 / inline arrays), so an unknown field cannot be
 * skipped, and continuing would read the following operations out of a mis-aligned cursor.
 */
static bool
proctex_decode_field(
    struct RSCache_ProcTexOperation* op,
    int field,
    struct RSCache_Buffer* buffer)
{
    switch( op->type )
    {
    case RSCACHE_PROCTEX_CONST_MONO:
        if( field == 0 )
        {
            op->u.const_mono.constant = (g1(buffer) << 12) / 255;
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_CONST_COLOUR:
        if( field == 0 )
        {
            int rgb = g3(buffer);
            /* Stored 8-bit per channel, held at 12.4 fixed point like every other colour
             * value in the graph. */
            op->u.const_colour.r = ((rgb >> 16) & 0xFF) << 4;
            op->u.const_colour.g = ((rgb >> 8) & 0xFF) << 4;
            op->u.const_colour.b = (rgb & 0xFF) << 4;
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_ARITHMETIC:
        if( field == 0 )
        {
            op->u.arithmetic.operation = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_BLUR:
        if( field == 0 )
        {
            op->u.blur.h_extent = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.blur.v_extent = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_CLAMP:
        if( field == 0 )
        {
            op->u.clamp.min = g2(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.clamp.max = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_BINARY:
        if( field == 0 )
        {
            op->u.binary.min_value = g2(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.binary.max_value = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_RANGE:
        if( field == 0 )
        {
            op->u.range.field0 = g2(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.range.field1 = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_CURVE:
        if( field == 0 )
        {
            int marker_count;
            int i;
            int max = (int)(sizeof(op->u.curve.markers) / sizeof(op->u.curve.markers[0]));
            op->u.curve.interp_mode = (uint8_t)g1(buffer);
            marker_count = g1(buffer);
            if( marker_count < 0 )
                return false;

            /*
             * EVERY marker must be consumed even when more arrive than we store. Clamping the
             * loop instead of the store under-consumed 4 bytes per surplus marker, and because
             * the graph continues straight after, the next operation's field id was read from
             * the middle of a marker — surfacing as an "unknown field 196" several operations
             * later rather than as a curve problem. That accounted for all six textures still
             * failing after the defaults fix.
             */
            op->u.curve.marker_count = (uint8_t)(marker_count < max ? marker_count : max);
            for( i = 0; i < marker_count; i++ )
            {
                int key = g2(buffer);
                int value = g2(buffer);
                if( i < max )
                {
                    op->u.curve.markers[i].key = key;
                    op->u.curve.markers[i].value = value;
                }
            }
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_GRADIENT:
        if( field == 0 )
        {
            int preset = g1(buffer);
            op->u.gradient.preset = (uint8_t)preset;
            if( preset == 0 )
            {
                int count = g1(buffer);
                int i;
                int max = (int)(sizeof(op->u.gradient.stops) / sizeof(op->u.gradient.stops[0]));
                if( count < 0 )
                    return false;
                /* Consume all stops, store up to `max` — same reason as curve's markers. */
                op->u.gradient.stop_count = (uint8_t)(count < max ? count : max);
                for( i = 0; i < count; i++ )
                {
                    int position = g2(buffer);
                    int r = g1(buffer) << 4;
                    int g = g1(buffer) << 4;
                    int b = g1(buffer) << 4;
                    if( i < max )
                    {
                        op->u.gradient.stops[i].position = position;
                        op->u.gradient.stops[i].r = r;
                        op->u.gradient.stops[i].g = g;
                        op->u.gradient.stops[i].b = b;
                    }
                }
            }
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_COLOUR_STRIP:
        if( field == 0 )
        {
            op->u.colour_strip.r = g2(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.colour_strip.g = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->u.colour_strip.b = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_HSL:
        if( field == 0 )
        {
            op->u.hsl.delta_hue = g2b(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.hsl.delta_saturation = ((int)(int8_t)g1b(buffer) << 12) / 100;
            return true;
        }
        if( field == 2 )
        {
            op->u.hsl.delta_light = ((int)(int8_t)g1b(buffer) << 12) / 100;
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_MIRROR:
        if( field == 0 )
        {
            op->u.mirror.invert_horizontal = (g1(buffer) == 1);
            return true;
        }
        if( field == 1 )
        {
            op->u.mirror.invert_vertical = (g1(buffer) == 1);
            return true;
        }
        if( field == 2 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_DIAGONAL_GRADIENT:
        if( field == 0 )
        {
            op->u.diagonal_gradient.mix_mode = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.diagonal_gradient.interpolation_mode = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 3 )
        {
            op->u.diagonal_gradient.steepness = (uint8_t)g1(buffer);
            return true;
        }
        /*
         * Fields 2, 4, 5 and 6 are bare flags: they consume the field id and nothing else.
         *
         * The reference decodes 0, 1 and 3 only and desyncs on these — harmlessly for it,
         * since it never checks consumption. The width was settled against the operation
         * header instead: ids are sequential, so the byte following the field block must be
         * `previous id + 1`, and the two after it a valid type and cache size. Zero-width is
         * the only reading that satisfies all three, and it then chains correctly through
         * every remaining operation to the end of the archive — in both records that carry
         * these fields (textures 275 and 742). See B18 in EXCEPTIONS.md.
         */
        if( field == 2 || field == 4 || field == 5 || field == 6 )
            return true;
        return false;

    case RSCACHE_PROCTEX_BRICKS:
        switch( field )
        {
        case 0:
            op->u.bricks.field0 = (uint8_t)g1(buffer);
            return true;
        case 1:
            op->u.bricks.seed = (uint8_t)g1(buffer);
            return true;
        case 2:
            op->u.bricks.field2 = g2(buffer);
            return true;
        case 3:
            op->u.bricks.field3 = g2(buffer);
            return true;
        case 4:
            op->u.bricks.field4 = g2(buffer);
            return true;
        case 5:
            op->u.bricks.field5 = g2(buffer);
            return true;
        case 6:
            op->u.bricks.field6 = g2(buffer);
            return true;
        case 7:
            op->u.bricks.field7 = g2(buffer);
            return true;
        default:
            return false;
        }

    case RSCACHE_PROCTEX_IRREGULAR_BRICKS:
        switch( field )
        {
        case 0:
            op->u.irregular_bricks.seed = (uint8_t)g1(buffer);
            return true;
        case 1:
            op->u.irregular_bricks.field1 = g2(buffer);
            return true;
        case 2:
            op->u.irregular_bricks.field2 = g2(buffer);
            return true;
        case 3:
            op->u.irregular_bricks.field3 = g2(buffer);
            return true;
        case 4:
            op->u.irregular_bricks.field4 = g2(buffer);
            return true;
        case 5:
            op->u.irregular_bricks.field5 = g2(buffer);
            return true;
        case 6:
            op->u.irregular_bricks.field6 = (uint8_t)g1(buffer);
            return true;
        case 7:
            op->u.irregular_bricks.field7 = g2(buffer);
            return true;
        case 8:
            op->u.irregular_bricks.field8 = g2(buffer);
            return true;
        default:
            return false;
        }

    case RSCACHE_PROCTEX_HERRINGBONE:
        if( field == 0 )
        {
            op->u.herringbone.scale_x = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.herringbone.scale_y = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->u.herringbone.ratio = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_WEAVE:
        if( field == 0 )
        {
            op->u.weave.thickness = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_PERLIN_NOISE:
        switch( field )
        {
        case 0:
            op->u.perlin.field0 = (g1(buffer) == 1);
            return true;
        case 1:
            op->u.perlin.octaves = (uint8_t)g1(buffer);
            return true;
        case 2:
        {
            /* A negative value means per-octave amplitudes follow inline, `octaves` of them.
             * Field 1 therefore has to have been read already; the format relies on the
             * writer's ordering. */
            int i;
            int max = (int)(sizeof(op->u.perlin.amplitudes) / sizeof(op->u.perlin.amplitudes[0]));
            op->u.perlin.field2 = g2b(buffer);
            if( op->u.perlin.field2 < 0 )
            {
                int n = op->u.perlin.octaves;
                if( n > max )
                    n = max;
                op->u.perlin.amplitude_count = (uint8_t)n;
                for( i = 0; i < n; i++ )
                    op->u.perlin.amplitudes[i] = (int16_t)g2b(buffer);
                /* Any octaves beyond our fixed store still occupy stream bytes. */
                for( ; i < op->u.perlin.octaves; i++ )
                    (void)g2b(buffer);
            }
            return true;
        }
        case 3:
            op->u.perlin.scale_x = op->u.perlin.scale_y = (uint8_t)g1(buffer);
            return true;
        case 4:
            op->u.perlin.seed = (uint8_t)g1(buffer);
            return true;
        case 5:
            op->u.perlin.scale_x = (uint8_t)g1(buffer);
            return true;
        case 6:
            op->u.perlin.scale_y = (uint8_t)g1(buffer);
            return true;
        default:
            return false;
        }

    case RSCACHE_PROCTEX_VORONOI_NOISE:
        switch( field )
        {
        case 0:
            op->u.voronoi.scale_x = op->u.voronoi.scale_y = (uint8_t)g1(buffer);
            return true;
        case 1:
            op->u.voronoi.rng_seed = (uint8_t)g1(buffer);
            return true;
        case 2:
            op->u.voronoi.field2 = g2(buffer);
            return true;
        case 3:
            op->u.voronoi.field3 = (uint8_t)g1(buffer);
            return true;
        case 4:
            op->u.voronoi.field4 = (uint8_t)g1(buffer);
            return true;
        case 5:
            op->u.voronoi.scale_x = (uint8_t)g1(buffer);
            return true;
        case 6:
            op->u.voronoi.scale_y = (uint8_t)g1(buffer);
            return true;
        default:
            return false;
        }

    case RSCACHE_PROCTEX_LINE_NOISE:
        switch( field )
        {
        case 0:
            op->u.line_noise.seed = (uint8_t)g1(buffer);
            return true;
        case 1:
            op->u.line_noise.count = g2(buffer);
            return true;
        case 2:
            op->u.line_noise.length = (uint8_t)g1(buffer);
            return true;
        case 3:
            op->u.line_noise.min_angle = g2(buffer);
            return true;
        case 4:
            op->u.line_noise.max_angle = g2(buffer);
            return true;
        default:
            return false;
        }

    case RSCACHE_PROCTEX_BRIGHTNESS:
        switch( field )
        {
        case 0:
            op->u.brightness.max_value = g2(buffer);
            return true;
        case 1:
            op->u.brightness.blue_factor = g2(buffer);
            return true;
        case 2:
            op->u.brightness.green_factor = g2(buffer);
            return true;
        case 3:
            op->u.brightness.red_factor = g2(buffer);
            return true;
        case 4:
        {
            /* Transcribed from the reference as-is. Note it is visibly odd — the green and
             * blue shifts do not line up with the red, and the blue term masks to 0 so it is
             * always zero. Kept faithful rather than "corrected": if it is a bug it is one
             * the shipped client also has, and the pixels have to match. */
            int rgb = g3(buffer);
            op->u.brightness.colour_delta[0] = (rgb & 0xFF0000) << 4;
            op->u.brightness.colour_delta[1] = (rgb >> 4) & 0xFF0;
            op->u.brightness.colour_delta[2] = 0;
            return true;
        }
        default:
            return false;
        }

    case RSCACHE_PROCTEX_COLOUR_EDGE_DETECT:
        /* Field 0 is unused by the reference; 1 and 2 only. */
        if( field == 1 )
        {
            op->u.edge_detect.multiplier = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->u.edge_detect.field1 = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_MONO_EDGE_DETECT:
        if( field == 0 )
        {
            op->u.edge_detect.multiplier = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_EMBOSS:
        if( field == 0 )
        {
            op->u.emboss.field0 = g2(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.emboss.field1 = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->u.emboss.field2 = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_TRIG_WARP:
        if( field == 0 )
        {
            op->u.trig_warp.hypotenuse_multiplier = g2(buffer) << 4;
            return true;
        }
        if( field == 1 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_TILING:
        if( field == 0 )
        {
            op->u.tiling.tile_count_h = (uint8_t)g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.tiling.tile_count_v = (uint8_t)g1(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_SPRITE_SOURCE:
    case RSCACHE_PROCTEX_TILING_SPRITE:
        if( field == 0 )
        {
            op->u.sprite_source.sprite_id = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_TEXTURE_SOURCE:
        if( field == 0 )
        {
            op->u.texture_source.texture_id = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_SQUARE_WAVEFORM:
        if( field == 0 )
        {
            op->u.square_waveform.field0 = g1(buffer);
            return true;
        }
        if( field == 1 )
        {
            op->u.square_waveform.field1 = g2(buffer);
            return true;
        }
        if( field == 2 )
        {
            op->u.square_waveform.field2 = (uint8_t)g1(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_MANDELBROT:
        if( field >= 0 && field <= 3 )
        {
            op->u.mandelbrot.field[field] = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_OP37:
        if( field >= 0 && field <= 6 )
        {
            op->u.op37.field[field] = g2(buffer);
            return true;
        }
        return false;

    case RSCACHE_PROCTEX_RASTERIZER:
        if( field == 0 )
        {
            proctex_decode_rasterizer(op, buffer);
            return true;
        }
        if( field == 1 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    /* Operations whose only field is the monochrome flag. */
    case RSCACHE_PROCTEX_INVERT:
    case RSCACHE_PROCTEX_KALEIDOSCOPE:
    case RSCACHE_PROCTEX_MIXER:
        if( field == 0 )
        {
            op->is_monochrome = (g1(buffer) == 1);
            return true;
        }
        return false;

    /* Operations with no fields at all. */
    case RSCACHE_PROCTEX_H_GRADIENT:
    case RSCACHE_PROCTEX_V_GRADIENT:
    case RSCACHE_PROCTEX_GRAYSCALE:
    case RSCACHE_PROCTEX_PSEUDO_RANDOM_NOISE:
        return false;

    default:
        return false;
    }
}

/* --- texture graph -------------------------------------------------------------------- */

static int
proctex_sprite_id_of(const struct RSCache_ProcTexOperation* op)
{
    if( op->type == RSCACHE_PROCTEX_SPRITE_SOURCE || op->type == RSCACHE_PROCTEX_TILING_SPRITE )
        return op->u.sprite_source.sprite_id;
    return -1;
}

static int
proctex_texture_id_of(const struct RSCache_ProcTexOperation* op)
{
    if( op->type == RSCACHE_PROCTEX_TEXTURE_SOURCE )
        return op->u.texture_source.texture_id;
    return -1;
}

static void
proctex_dep_add(
    int32_t** list,
    int32_t* count,
    int value)
{
    int i;
    int32_t* grown;

    for( i = 0; i < *count; i++ )
        if( (*list)[i] == value )
            return;

    grown = realloc(*list, (size_t)(*count + 1) * sizeof(int32_t));
    if( !grown )
        return;
    *list = grown;
    (*list)[(*count)++] = value;
}

struct RSCache_Dat2ProcTexture*
RSCache_Dat2ProcTextureNewDecode(
    const char* data,
    int data_size,
    int texture_id,
    uint32_t flags)
{
    struct RSCache_Dat2ProcTexture* texture;
    struct RSCache_Buffer buffer;
    int operation_count;
    int op_index;

    if( !data || data_size < 1 )
        return NULL;

    buffer.data = (uint8_t*)data;
    buffer.position = 0;
    buffer.size = (uint32_t)data_size;

    operation_count = g1(&buffer);
    if( operation_count <= 0 || operation_count >= RSCACHE_PROCTEX_MAX_OPS )
        return NULL;

    texture = calloc(1, sizeof(*texture));
    if( !texture )
        return NULL;
    texture->id = texture_id;
    texture->colour_op = -1;
    texture->alpha_op = -1;
    texture->monochrome_op = -1;
    texture->operations = calloc((size_t)operation_count, sizeof(*texture->operations));
    if( !texture->operations )
    {
        free(texture);
        return NULL;
    }
    texture->operation_count = operation_count;
    texture->_alloc_count = operation_count;

    for( op_index = 0; op_index < operation_count; op_index++ )
    {
        struct RSCache_ProcTexOperation* op = &texture->operations[op_index];
        int input_count = 0;
        bool is_monochrome = false;
        int field_count;
        int i;

        op->id = (uint8_t)g1(&buffer);
        op->type = (uint8_t)g1(&buffer);
        op->cache_size = (uint8_t)g1(&buffer);

        if( !RSCache_ProcTexOpSignature(op->type, &input_count, &is_monochrome) )
        {
            /* An operation type we do not know has an unknown field layout AND an unknown
             * input count, so nothing after it can be located. Stop and let the caller see a
             * short _consumed rather than return a graph built from garbage. */
            texture->operation_count = op_index;
            texture->_consumed = (int32_t)buffer.position;
            return texture;
        }
        op->input_count = (uint8_t)input_count;
        op->is_monochrome = is_monochrome;
        proctex_init_defaults(op);

        field_count = g1(&buffer);
        for( i = 0; i < field_count; i++ )
        {
            int field = g1(&buffer);
            if( !proctex_decode_field(op, field, &buffer) )
            {
                if( getenv("TORIRS_PROCTEX_DEBUG") )
                    fprintf(
                        stderr,
                        "proctex %d: op %d/%d type %d (%s) field %d UNKNOWN at %u/%u\n",
                        texture_id,
                        op_index,
                        operation_count,
                        op->type,
                        RSCache_ProcTexOpName(op->type),
                        field,
                        buffer.position,
                        buffer.size);
                texture->operation_count = op_index;
                texture->_consumed = (int32_t)buffer.position;
                return texture;
            }
            if( op->field_count < (uint8_t)(sizeof(op->field_order) / sizeof(op->field_order[0])) )
                op->field_order[op->field_count++] = (uint8_t)field;
            if( field < 32 )
                op->field_present |= (1u << field);
        }

        if( getenv("TORIRS_PROCTEX_TRACE") )
        {
            fprintf(
                stderr,
                "  proctex %d op %d: type=%d (%s) id=%d cache=%d fields=%d [",
                texture_id,
                op_index,
                op->type,
                RSCache_ProcTexOpName(op->type),
                op->id,
                op->cache_size,
                op->field_count);
            for( i = 0; i < op->field_count; i++ )
                fprintf(stderr, "%s%d", i ? "," : "", op->field_order[i]);
            fprintf(stderr, "] inputs=%d pos=%u\n", op->input_count, buffer.position);
        }

        /* Input wiring: one byte per input, an index into this same operation array. Read
         * after the fields because a field can change the input count in principle; in
         * practice it does not, but the order is the format's. */
        for( i = 0; i < op->input_count && i < RSCACHE_PROCTEX_MAX_INPUTS; i++ )
        {
            int src = g1(&buffer);
            op->inputs[i] = (uint8_t)(src < operation_count ? src : 0);
        }
        for( ; i < op->input_count; i++ )
            (void)g1(&buffer);
    }

    /* Output selectors. */
    texture->colour_op = g1(&buffer);
    texture->alpha_op = g1(&buffer);
    if( flags & RSCACHE_PROCTEX_HAS_MOD_OP )
        texture->monochrome_op = g1(&buffer);

    if( texture->colour_op >= texture->operation_count )
        texture->colour_op = -1;
    if( texture->alpha_op >= texture->operation_count )
        texture->alpha_op = -1;
    if( texture->monochrome_op >= texture->operation_count )
        texture->monochrome_op = -1;

    /* Tail. Older records stop after the selectors, so guard on remaining bytes rather than
     * assuming it is present. */
    if( buffer.position + 7 <= buffer.size )
    {
        texture->bool1 = (g1(&buffer) == 1);
        texture->flip_v = (g1(&buffer) == 1);
        texture->repeat_s = (g1(&buffer) == 1);
        texture->repeat_t = (g1(&buffer) == 1);
        texture->combine_mode = (uint8_t)(g1(&buffer) & 0x3);
        texture->anim_u = (int8_t)g1b(&buffer);
        texture->anim_v = (int8_t)g1b(&buffer);

        /* Three more fields the reference stops short of — see the header note. Consumed so
         * exact consumption stays a usable check, not interpreted. */
        if( buffer.position + 3 <= buffer.size )
        {
            texture->trailing_flags = (uint8_t)g1(&buffer);
            texture->trailing_count = (uint8_t)g1(&buffer);
            texture->trailing_bool = (g1(&buffer) == 1);
            texture->has_trailing = true;
        }
    }

    for( op_index = 0; op_index < texture->operation_count; op_index++ )
    {
        const struct RSCache_ProcTexOperation* op = &texture->operations[op_index];
        int sprite_id = proctex_sprite_id_of(op);
        int tex_id = proctex_texture_id_of(op);
        if( sprite_id >= 0 )
            proctex_dep_add(
                &texture->sprite_dependencies, &texture->sprite_dependency_count, sprite_id);
        if( tex_id >= 0 )
            proctex_dep_add(
                &texture->texture_dependencies, &texture->texture_dependency_count, tex_id);
    }

    texture->_consumed = (int32_t)buffer.position;
    return texture;
}

void
RSCache_Dat2ProcTextureFree(struct RSCache_Dat2ProcTexture* texture)
{
    int i;

    if( !texture )
        return;

    if( texture->operations )
    {
        for( i = 0; i < texture->_alloc_count; i++ )
            if( texture->operations[i].type == RSCACHE_PROCTEX_RASTERIZER )
                free(texture->operations[i].u.rasterizer.shapes);
        free(texture->operations);
    }
    free(texture->sprite_dependencies);
    free(texture->texture_dependencies);
    free(texture);
}
