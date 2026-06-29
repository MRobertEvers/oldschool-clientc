#include "toriauxlib/core/toriauxlibcore_types.h"

#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

size_t
ToriAuxLibCore_MapTerrainSizeOf(const struct ToriAuxLibCore_MapTerrain* terrain)
{
    if( !terrain )
        return 0;
    return sizeof(*terrain);
}

void
ToriAuxLibCore_MapTerrainFree(struct ToriAuxLibCore_MapTerrain* terrain)
{
    free(terrain);
}

size_t
ToriAuxLibCore_MapLocsSizeOf(const struct ToriAuxLibCore_MapLocs* locs)
{
    if( !locs )
        return 0;
    size_t bytes = sizeof(*locs);
    if( locs->locs )
        bytes += (size_t)locs->locs_count * sizeof(*locs->locs);
    return bytes;
}

void
ToriAuxLibCore_MapLocsFree(struct ToriAuxLibCore_MapLocs* locs)
{
    if( !locs )
        return;
    free(locs->locs);
    free(locs);
}

size_t
ToriAuxLibCore_FlotypeSizeOf(const struct ToriAuxLibCore_Flotype* flotype)
{
    if( !flotype )
        return 0;
    return sizeof(*flotype);
}

void
ToriAuxLibCore_FlotypeFree(struct ToriAuxLibCore_Flotype* flotype)
{
    free(flotype);
}

static void
ToriAuxLibCore_LocationFree_models(struct ToriAuxLibCore_Location* loc)
{
    if( !loc || !loc->models )
        return;
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        free(loc->models[i]);
    free(loc->models);
    loc->models = NULL;
}

static size_t
toriauxlibcore_location_models_sizeof(const struct ToriAuxLibCore_Location* loc)
{
    if( !loc || !loc->models )
        return 0;

    size_t bytes = (size_t)loc->shapes_and_model_count * sizeof(*loc->models);
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        bytes += (size_t)loc->lengths[i] * sizeof(**loc->models);
    return bytes;
}

size_t
ToriAuxLibCore_LocationSizeOf(const struct ToriAuxLibCore_Location* loc)
{
    if( !loc )
        return 0;

    size_t bytes = sizeof(*loc);
    if( loc->shapes )
        bytes += (size_t)loc->shapes_and_model_count * sizeof(*loc->shapes);
    bytes += toriauxlibcore_location_models_sizeof(loc);
    if( loc->lengths )
        bytes += (size_t)loc->shapes_and_model_count * sizeof(*loc->lengths);
    if( loc->recolors_from )
        bytes += (size_t)loc->recolor_count * sizeof(*loc->recolors_from);
    if( loc->recolors_to )
        bytes += (size_t)loc->recolor_count * sizeof(*loc->recolors_to);
    if( loc->retextures_from )
        bytes += (size_t)loc->retexture_count * sizeof(*loc->retextures_from);
    if( loc->retextures_to )
        bytes += (size_t)loc->retexture_count * sizeof(*loc->retextures_to);
    if( loc->transforms )
        bytes += (size_t)loc->transform_count * sizeof(*loc->transforms);
    return bytes;
}

void
ToriAuxLibCore_LocationFree(struct ToriAuxLibCore_Location* loc)
{
    if( !loc )
        return;
    free(loc->shapes);
    ToriAuxLibCore_LocationFree_models(loc);
    free(loc->lengths);
    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc->transforms);
    free(loc);
}

static void
toriauxlibcore_animbase_free(struct ToriAuxLibCore_AnimBase* base)
{
    if( !base )
        return;

    if( base->bone_groups )
    {
        for( int i = 0; i < base->length; i++ )
            free(base->bone_groups[i]);
        free(base->bone_groups);
    }
    free(base->bone_group_lengths);
    free(base->types);
    free(base);
}

static size_t
toriauxlibcore_animbase_sizeof(const struct ToriAuxLibCore_AnimBase* base)
{
    if( !base )
        return 0;

    size_t bytes = sizeof(*base);
    if( base->bone_groups )
    {
        bytes += (size_t)base->length * sizeof(*base->bone_groups);
        for( int i = 0; i < base->length; i++ )
            bytes += (size_t)base->bone_group_lengths[i] * sizeof(**base->bone_groups);
    }
    if( base->bone_group_lengths )
        bytes += (size_t)base->length * sizeof(*base->bone_group_lengths);
    if( base->types )
        bytes += (size_t)base->length * sizeof(*base->types);
    return bytes;
}

size_t
ToriAuxLibCore_AnimayaSkinSizeOf(const struct ToriAuxLibCore_AnimayaSkin* skin)
{
    if( !skin )
        return 0;

    size_t bytes = sizeof(*skin);
    if( skin->group_counts )
        bytes += (size_t)skin->vertex_count * sizeof(*skin->group_counts);
    if( skin->groups )
    {
        bytes += (size_t)skin->vertex_count * sizeof(*skin->groups);
        for( int i = 0; i < skin->vertex_count; i++ )
            bytes += (size_t)skin->group_counts[i] * sizeof(**skin->groups);
    }
    if( skin->scales )
    {
        bytes += (size_t)skin->vertex_count * sizeof(*skin->scales);
        for( int i = 0; i < skin->vertex_count; i++ )
            bytes += (size_t)skin->group_counts[i] * sizeof(**skin->scales);
    }
    return bytes;
}

void
ToriAuxLibCore_AnimayaSkinFree(struct ToriAuxLibCore_AnimayaSkin* skin)
{
    if( !skin )
        return;
    if( skin->groups )
    {
        for( int i = 0; i < skin->vertex_count; i++ )
            free(skin->groups[i]);
        free(skin->groups);
    }
    if( skin->scales )
    {
        for( int i = 0; i < skin->vertex_count; i++ )
            free(skin->scales[i]);
        free(skin->scales);
    }
    free(skin->group_counts);
    free(skin);
}

size_t
ToriAuxLibCore_SkeletalAnimSizeOf(const struct ToriAuxLibCore_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return 0;

    size_t bytes = sizeof(*skeletal);
    if( skeletal->matrices )
        bytes += (size_t)skeletal->frame_count * (size_t)skeletal->bone_count * 16u * sizeof(float);
    return bytes;
}

void
ToriAuxLibCore_SkeletalAnimFree(struct ToriAuxLibCore_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return;
    free(skeletal->matrices);
    free(skeletal);
}

static size_t
toriauxlibcore_animframe_sizeof(const struct ToriAuxLibCore_AnimFrame* frame)
{
    if( !frame || frame->length <= 0 )
        return 0;

    size_t bytes = 0;
    if( frame->groups )
        bytes += (size_t)frame->length * sizeof(*frame->groups);
    if( frame->x )
        bytes += (size_t)frame->length * sizeof(*frame->x);
    if( frame->y )
        bytes += (size_t)frame->length * sizeof(*frame->y);
    if( frame->z )
        bytes += (size_t)frame->length * sizeof(*frame->z);
    return bytes;
}

size_t
ToriAuxLibCore_AnimationSizeOf(const struct ToriAuxLibCore_Animation* anim)
{
    if( !anim )
        return 0;

    size_t bytes = sizeof(*anim);
    bytes += toriauxlibcore_animbase_sizeof(anim->base);
    if( anim->frames )
    {
        bytes += (size_t)anim->frame_count * sizeof(*anim->frames);
        for( int i = 0; i < anim->frame_count; i++ )
            bytes += toriauxlibcore_animframe_sizeof(&anim->frames[i]);
    }
    return bytes;
}

void
ToriAuxLibCore_AnimationFree(struct ToriAuxLibCore_Animation* anim)
{
    if( !anim )
        return;

    toriauxlibcore_animbase_free(anim->base);

    if( anim->frames )
    {
        for( int i = 0; i < anim->frame_count; i++ )
        {
            free(anim->frames[i].groups);
            free(anim->frames[i].x);
            free(anim->frames[i].y);
            free(anim->frames[i].z);
        }
        free(anim->frames);
    }

    free(anim);
}

static void
toriauxlibcore_bones_free(struct ToriAuxLibCore_Bones* bones)
{
    if( !bones )
        return;
    if( bones->bones )
    {
        for( int i = 0; i < bones->bones_count; i++ )
            free(bones->bones[i]);
        free(bones->bones);
    }
    free(bones->bones_sizes);
    free(bones);
}

static size_t
toriauxlibcore_bones_sizeof(const struct ToriAuxLibCore_Bones* bones)
{
    if( !bones )
        return 0;

    size_t bytes = sizeof(*bones);
    if( bones->bones )
    {
        bytes += (size_t)bones->bones_count * sizeof(*bones->bones);
        for( int i = 0; i < bones->bones_count; i++ )
            bytes += (size_t)bones->bones_sizes[i] * sizeof(**bones->bones);
    }
    if( bones->bones_sizes )
        bytes += (size_t)bones->bones_count * sizeof(*bones->bones_sizes);
    return bytes;
}

size_t
ToriAuxLibCore_ModelSizeOf(const struct ToriAuxLibCore_Model* model)
{
    if( !model )
        return 0;

    size_t bytes = sizeof(*model);
    if( model->vertices_x )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_x);
    if( model->vertices_y )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_y);
    if( model->vertices_z )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_z);
    if( model->face_colors_a )
        bytes += (size_t)model->face_count * sizeof(*model->face_colors_a);
    if( model->face_colors_b )
        bytes += (size_t)model->face_count * sizeof(*model->face_colors_b);
    if( model->face_colors_c )
        bytes += (size_t)model->face_count * sizeof(*model->face_colors_c);
    if( model->face_indices_a )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_a);
    if( model->face_indices_b )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_b);
    if( model->face_indices_c )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_c);
    if( model->face_textures )
        bytes += (size_t)model->face_count * sizeof(*model->face_textures);
    if( model->face_alphas )
        bytes += (size_t)model->face_count * sizeof(*model->face_alphas);
    if( model->face_infos )
        bytes += (size_t)model->face_count * sizeof(*model->face_infos);
    if( model->face_priorities )
        bytes += (size_t)model->face_count * sizeof(*model->face_priorities);
    if( model->face_colors )
        bytes += (size_t)model->face_count * sizeof(*model->face_colors);
    if( model->textured_p_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_p_coordinate);
    if( model->textured_m_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_m_coordinate);
    if( model->textured_n_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_n_coordinate);
    if( model->face_texture_coords )
        bytes += (size_t)model->textured_face_count * sizeof(*model->face_texture_coords);
    bytes += toriauxlibcore_bones_sizeof(model->vertex_bones);
    bytes += toriauxlibcore_bones_sizeof(model->face_bones);
    if( model->bounds_cylinder )
        bytes += sizeof(*model->bounds_cylinder);
    bytes += ToriAuxLibCore_AnimayaSkinSizeOf(model->animaya_skin);
    return bytes;
}

void
ToriAuxLibCore_ModelFree(struct ToriAuxLibCore_Model* model)
{
    if( !model )
        return;

    free(model->vertices_x);
    free(model->vertices_y);
    free(model->vertices_z);
    free(model->face_colors_a);
    free(model->face_colors_b);
    free(model->face_colors_c);
    free(model->face_indices_a);
    free(model->face_indices_b);
    free(model->face_indices_c);
    free(model->face_textures);
    free(model->face_alphas);
    free(model->face_infos);
    free(model->face_priorities);
    free(model->face_colors);
    free(model->textured_p_coordinate);
    free(model->textured_m_coordinate);
    free(model->textured_n_coordinate);
    free(model->face_texture_coords);
    toriauxlibcore_bones_free(model->vertex_bones);
    toriauxlibcore_bones_free(model->face_bones);
    free(model->bounds_cylinder);
    ToriAuxLibCore_AnimayaSkinFree(model->animaya_skin);
    free(model);
}

size_t
ToriAuxLibCore_TextureSizeOf(const struct ToriAuxLibCore_Texture* texture)
{
    if( !texture )
        return 0;

    size_t bytes = sizeof(*texture);
    if( texture->texels )
        bytes += (size_t)texture->width * (size_t)texture->height * sizeof(*texture->texels);
    return bytes;
}

void
ToriAuxLibCore_TextureFree(struct ToriAuxLibCore_Texture* texture)
{
    if( !texture )
        return;
    free(texture->texels);
    free(texture);
}

size_t
ToriAuxLibCore_SequenceSizeOf(const struct ToriAuxLibCore_Sequence* seq)
{
    if( !seq )
        return 0;

    size_t bytes = sizeof(*seq);
    if( seq->frames )
        bytes += (size_t)seq->frame_count * sizeof(*seq->frames);
    if( seq->iframes )
        bytes += (size_t)seq->frame_count * sizeof(*seq->iframes);
    if( seq->delay )
        bytes += (size_t)seq->frame_count * sizeof(*seq->delay);
    if( seq->resolved )
    {
        bytes += sizeof(*seq->resolved);
        if( seq->resolved->frames )
        {
            bytes += (size_t)seq->resolved->frame_count * sizeof(*seq->resolved->frames);
            for( int i = 0; i < seq->resolved->frame_count; i++ )
                bytes += toriauxlibcore_animframe_sizeof(&seq->resolved->frames[i]);
        }
    }
    return bytes;
}

void
ToriAuxLibCore_SequenceFree(struct ToriAuxLibCore_Sequence* seq)
{
    if( !seq )
        return;
    if( seq->resolved )
    {
        if( seq->resolved->frames )
        {
            for( int i = 0; i < seq->resolved->frame_count; i++ )
            {
                free(seq->resolved->frames[i].groups);
                free(seq->resolved->frames[i].x);
                free(seq->resolved->frames[i].y);
                free(seq->resolved->frames[i].z);
            }
            free(seq->resolved->frames);
        }
        free(seq->resolved);
    }
    free(seq->frames);
    free(seq->iframes);
    free(seq->delay);
    free(seq);
}

size_t
ToriAuxLibCore_SpriteSizeOf(const struct ToriAuxLibCore_Sprite* sprite)
{
    if( !sprite )
        return 0;
    return sizeof(*sprite);
}

void
ToriAuxLibCore_SpriteFree(struct ToriAuxLibCore_Sprite* sprite)
{
    if( !sprite )
        return;
    free(sprite);
}

size_t
ToriAuxLibCore_FontSizeOf(const struct ToriAuxLibCore_Font* font)
{
    if( !font )
        return 0;
    return sizeof(*font);
}

void
ToriAuxLibCore_FontFree(struct ToriAuxLibCore_Font* font)
{
    if( !font )
        return;
    free(font);
}

size_t
ToriAuxLibCore_ComponentSizeOf(const struct ToriAuxLibCore_Component* component)
{
    if( !component )
        return 0;

    size_t bytes = sizeof(*component);
    if( component->scripts )
    {
        bytes += (size_t)component->scripts_count * sizeof(*component->scripts);
        for( int i = 0; i < component->scripts_count; i++ )
            bytes += (size_t)component->scripts_lengths[i] * sizeof(**component->scripts);
    }
    if( component->scripts_lengths )
        bytes += (size_t)component->scripts_count * sizeof(*component->scripts_lengths);
    if( component->script_comparator )
        bytes += (size_t)component->scripts_count * sizeof(*component->script_comparator);
    if( component->script_operand )
        bytes += (size_t)component->scripts_count * sizeof(*component->script_operand);
    return bytes;
}

void
ToriAuxLibCore_ComponentFree(struct ToriAuxLibCore_Component* component)
{
    if( !component )
        return;

    if( component->scripts )
    {
        for( int i = 0; i < component->scripts_count; i++ )
            free(component->scripts[i]);
        free(component->scripts);
    }
    free(component->scripts_lengths);
    free(component->script_comparator);
    free(component->script_operand);
    free(component);
}
