#include "transcode.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
tdup(const char* s)
{
    if( !s )
        return NULL;
    size_t n = strlen(s);
    char* d = malloc(n + 1);
    if( d )
        memcpy(d, s, n + 1);
    return d;
}

static void
add_w(char*** warnings, int* warning_count, const char* msg)
{
    if( !warnings || !warning_count )
        return;
    char** n = realloc(*warnings, (size_t)(*warning_count + 1) * sizeof(char*));
    if( !n )
        return;
    *warnings = n;
    (*warnings)[*warning_count] = tdup(msg);
    (*warning_count)++;
}

static const char*
tool_model_format_name(int format)
{
    switch( format )
    {
    case RSCACHE_MODEL_FORMAT_OB2:
        return "OB2";
    case RSCACHE_MODEL_FORMAT_OB3:
        return "OB3";
    case RSCACHE_MODEL_FORMAT_V2:
        return "V2";
    case RSCACHE_MODEL_FORMAT_V3:
        return "V3";
    default:
        return "unknown";
    }
}

static int16_t
clamp_i16(int v, int* clamped)
{
    if( v > 32767 )
    {
        if( clamped )
            *clamped = 1;
        return 32767;
    }
    if( v < -32768 )
    {
        if( clamped )
            *clamped = 1;
        return (int16_t)-32768;
    }
    return (int16_t)v;
}

struct RSCache_Dat2Framemap*
tool_transcode_animbase_to_framemap(
    const struct RSCache_Dat1AnimBase* base,
    int framemap_id)
{
    assert(base);
    struct RSCache_Dat2Framemap* fm = calloc(1, sizeof(*fm));
    if( !fm )
        return NULL;
    fm->id = framemap_id;
    fm->length = base->length;
    if( fm->length <= 0 )
        return fm;

    fm->types = calloc((size_t)fm->length, sizeof(int));
    fm->bone_groups = calloc((size_t)fm->length, sizeof(int*));
    fm->bone_groups_lengths = calloc((size_t)fm->length, sizeof(int));
    if( !fm->types || !fm->bone_groups || !fm->bone_groups_lengths )
    {
        RSCache_Dat2FramemapFree(fm);
        return NULL;
    }
    for( int i = 0; i < fm->length; i++ )
    {
        fm->types[i] = base->types[i];
        fm->bone_groups_lengths[i] = base->label_counts[i];
        if( fm->bone_groups_lengths[i] > 0 )
        {
            fm->bone_groups[i] = calloc((size_t)fm->bone_groups_lengths[i], sizeof(int));
            if( !fm->bone_groups[i] )
            {
                RSCache_Dat2FramemapFree(fm);
                return NULL;
            }
            for( int j = 0; j < fm->bone_groups_lengths[i]; j++ )
                fm->bone_groups[i][j] = base->labels[i][j];
        }
    }
    return fm;
}

struct RSCache_Dat1AnimBase*
tool_transcode_framemap_to_animbase(
    const struct RSCache_Dat2Framemap* fm,
    char*** warnings,
    int* warning_count)
{
    assert(fm);
    if( fm->has_transform_actor || fm->has_masks || (fm->tail && fm->tail_size > 0) )
        add_w(warnings, warning_count, "framemap transform_actor/masks/tail dropped for dat1");

    struct RSCache_Dat1AnimBase* base = calloc(1, sizeof(*base));
    if( !base )
        return NULL;
    base->length = fm->length;
    if( base->length <= 0 )
        return base;

    base->types = calloc((size_t)base->length, sizeof(uint8_t));
    base->labels = calloc((size_t)base->length, sizeof(uint8_t*));
    base->label_counts = calloc((size_t)base->length, sizeof(uint16_t));
    if( !base->types || !base->labels || !base->label_counts )
    {
        free(base->types);
        free(base->labels);
        free(base->label_counts);
        free(base);
        return NULL;
    }
    for( int i = 0; i < base->length; i++ )
    {
        base->types[i] = (uint8_t)fm->types[i];
        base->label_counts[i] = (uint16_t)fm->bone_groups_lengths[i];
        if( base->label_counts[i] > 0 )
        {
            base->labels[i] = calloc(base->label_counts[i], sizeof(uint8_t));
            if( !base->labels[i] )
                continue;
            for( int j = 0; j < base->label_counts[i]; j++ )
                base->labels[i][j] = (uint8_t)fm->bone_groups[i][j];
        }
    }
    return base;
}

uint8_t*
tool_transcode_model_to_ob2(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* prov,
    uint32_t* out_size,
    char*** warnings,
    int* warning_count)
{
    assert(model && out_size);
    *out_size = 0;

    if( prov && prov->format != RSCACHE_MODEL_FORMAT_OB2 )
    {
        int hd_faces = 0;
        if( model->texture_render_types )
        {
            for( int i = 0; i < model->textured_face_count; i++ )
            {
                if( model->texture_render_types[i] != 0 )
                    hd_faces++;
            }
        }
        char buf[512];
        snprintf(
            buf,
            sizeof(buf),
            "model downgraded %s -> OB2: only standard-definition geometry "
            "transcodes (flat/gouraud faces, per-face colour/alpha/priority/"
            "labels, render-type-0 triangle-mapped textures). High-definition "
            "data is dropped: animaya skin, the separate face-texture section, "
            "and %d of %d textured faces using complex/cube/scroll mapping, "
            "which OB2 re-encodes as triangle mapping with unauthored UVs.",
            tool_model_format_name(prov->format),
            hd_faces,
            model->textured_face_count);
        add_w(warnings, warning_count, buf);
    }

    uint32_t bound = RSCache_ModelEncodeBound(model, prov);
    uint8_t* out = malloc(bound);
    if( !out )
        return NULL;
    uint32_t written =
        RSCache_ModelEncodeFormat(model, prov, RSCACHE_MODEL_FORMAT_OB2, out, bound);
    if( written == 0 )
    {
        free(out);
        return NULL;
    }
    *out_size = written;
    return out;
}

int
tool_transcode_dat2_frame_to_dat1(
    const struct RSCache_Dat2Frame* src,
    struct RSCache_Dat1AnimBase* base,
    int dst_frame_id,
    int delay,
    struct RSCache_Dat1AnimFrame* out,
    char*** warnings,
    int* warning_count)
{
    assert(src && out);
    memset(out, 0, sizeof(*out));
    out->id = dst_frame_id;
    out->base = base;
    out->delay = delay < 0 ? 0 : (delay > 255 ? 255 : delay);
    out->length = src->translator_count;
    if( out->length < 0 )
        out->length = 0;

    if( out->length > 0 )
    {
        out->groups = calloc((size_t)out->length, sizeof(int16_t));
        out->x = calloc((size_t)out->length, sizeof(int16_t));
        out->y = calloc((size_t)out->length, sizeof(int16_t));
        out->z = calloc((size_t)out->length, sizeof(int16_t));
        if( !out->groups || !out->x || !out->y || !out->z )
        {
            RSCache_Dat1AnimFrameFreeInplace(out);
            return 0;
        }
        int clamped = 0;
        for( int i = 0; i < out->length; i++ )
        {
            out->groups[i] = clamp_i16(src->index_frame_ids[i], &clamped);
            out->x[i] = clamp_i16(src->translator_arg_x[i], &clamped);
            out->y[i] = clamp_i16(src->translator_arg_y[i], &clamped);
            out->z[i] = clamp_i16(src->translator_arg_z[i], &clamped);
        }
        if( clamped )
        {
            char buf[128];
            snprintf(
                buf,
                sizeof(buf),
                "frame %d: translator args clamped to int16 for dat1",
                dst_frame_id);
            add_w(warnings, warning_count, buf);
        }
    }

    out->flag_count = src->flag_count;
    if( out->flag_count > 0 && src->bone_flags )
    {
        out->bone_flags = malloc((size_t)out->flag_count);
        if( !out->bone_flags )
        {
            RSCache_Dat1AnimFrameFreeInplace(out);
            return 0;
        }
        memcpy(out->bone_flags, src->bone_flags, (size_t)out->flag_count);
    }

    if( out->length > 0 && src->synthesized )
    {
        out->synthesized = malloc((size_t)out->length * sizeof(bool));
        if( !out->synthesized )
        {
            RSCache_Dat1AnimFrameFreeInplace(out);
            return 0;
        }
        memcpy(out->synthesized, src->synthesized, (size_t)out->length * sizeof(bool));
    }
    return 1;
}
