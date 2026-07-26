#include "transcode.h"

#include <assert.h>
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
        add_w(
            warnings,
            warning_count,
            "model re-encoded to OB2; texture render types / animaya skin may be lost");

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
