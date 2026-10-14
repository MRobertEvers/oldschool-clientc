#include "mem/torirs_mem.h"
#include "torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

size_t
ToriRS_MapTerrainSizeOf(const struct ToriRS_MapTerrain* terrain)
{
    assert(terrain);
    return sizeof(*terrain);
}

void
ToriRS_MapTerrainFree(struct ToriRS_MapTerrain* terrain)
{
    ToriMem_Free(ToriMem_Assets(), terrain);
}

size_t
ToriRS_MapLocsSizeOf(const struct ToriRS_MapLocs* locs)
{
    assert(locs);
    size_t bytes = sizeof(*locs);
    if( locs->locs )
        bytes += (size_t)locs->locs_count * sizeof(*locs->locs);
    return bytes;
}

void
ToriRS_MapLocsFree(struct ToriRS_MapLocs* locs)
{
    if( !locs )
        return;
    ToriMem_Free(ToriMem_Assets(), locs->locs);
    ToriMem_Free(ToriMem_Assets(), locs);
}

size_t
ToriRS_FlotypeSizeOf(const struct ToriRS_Flotype* flotype)
{
    assert(flotype);
    return sizeof(*flotype);
}

void
ToriRS_FlotypeFree(struct ToriRS_Flotype* flotype)
{
    ToriMem_Free(ToriMem_Assets(), flotype);
}

static void
torirs_location_free_models(struct ToriRS_Location* loc)
{
    assert(loc);
    if( !loc->models )
        return;
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        ToriMem_Free(ToriMem_Assets(), loc->models[i]);
    ToriMem_Free(ToriMem_Assets(), loc->models);
    loc->models = NULL;
}

static size_t
torirs_location_models_sizeof(const struct ToriRS_Location* loc)
{
    assert(loc);
    if( !loc->models )
        return 0;

    size_t bytes = (size_t)loc->shapes_and_model_count * sizeof(*loc->models);
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        bytes += (size_t)loc->lengths[i] * sizeof(**loc->models);
    return bytes;
}

size_t
ToriRS_LocationSizeOf(const struct ToriRS_Location* loc)
{
    assert(loc);

    size_t bytes = sizeof(*loc);
    if( loc->shapes )
        bytes += (size_t)loc->shapes_and_model_count * sizeof(*loc->shapes);
    bytes += torirs_location_models_sizeof(loc);
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
ToriRS_LocationFree(struct ToriRS_Location* loc)
{
    if( !loc )
        return;
    ToriMem_Free(ToriMem_Assets(), loc->shapes);
    torirs_location_free_models(loc);
    ToriMem_Free(ToriMem_Assets(), loc->lengths);
    ToriMem_Free(ToriMem_Assets(), loc->recolors_from);
    ToriMem_Free(ToriMem_Assets(), loc->recolors_to);
    ToriMem_Free(ToriMem_Assets(), loc->retextures_from);
    ToriMem_Free(ToriMem_Assets(), loc->retextures_to);
    if( loc->params )
    {
        for( int i = 0; i < loc->param_count; i++ )
            ToriMem_Free(ToriMem_Assets(), loc->params[i].string_value);
        ToriMem_Free(ToriMem_Assets(), loc->params);
    }
    ToriMem_Free(ToriMem_Assets(), loc->transforms);
    ToriMem_Free(ToriMem_Assets(), loc->ambient_sound_ids);
    ToriMem_Free(ToriMem_Assets(), loc);
}

void
ToriRS_NpctypeFree(struct ToriRS_Npctype* npctype)
{
    if( !npctype )
        return;
    ToriMem_Free(ToriMem_Assets(), npctype->models);
    ToriMem_Free(ToriMem_Assets(), npctype->heads);
    ToriMem_Free(ToriMem_Assets(), npctype->recolors_from);
    ToriMem_Free(ToriMem_Assets(), npctype->recolors_to);
    ToriMem_Free(ToriMem_Assets(), npctype->retextures_from);
    ToriMem_Free(ToriMem_Assets(), npctype->retextures_to);
    if( npctype->params )
    {
        for( int i = 0; i < npctype->param_count; i++ )
            ToriMem_Free(ToriMem_Assets(), npctype->params[i].string_value);
        ToriMem_Free(ToriMem_Assets(), npctype->params);
    }
    ToriMem_Free(ToriMem_Assets(), npctype->transforms);
    ToriMem_Free(ToriMem_Assets(), npctype);
}

size_t
ToriRS_NpctypeSizeOf(const struct ToriRS_Npctype* npctype)
{
    assert(npctype);

    size_t bytes = sizeof(*npctype);
    if( npctype->models )
        bytes += (size_t)npctype->models_count * sizeof(*npctype->models);
    if( npctype->heads )
        bytes += (size_t)npctype->heads_count * sizeof(*npctype->heads);
    if( npctype->recolors_from )
        bytes += (size_t)npctype->recolor_count * sizeof(*npctype->recolors_from);
    if( npctype->recolors_to )
        bytes += (size_t)npctype->recolor_count * sizeof(*npctype->recolors_to);
    if( npctype->retextures_from )
        bytes += (size_t)npctype->retexture_count * sizeof(*npctype->retextures_from);
    if( npctype->retextures_to )
        bytes += (size_t)npctype->retexture_count * sizeof(*npctype->retextures_to);
    if( npctype->transforms )
        bytes += (size_t)npctype->transform_count * sizeof(*npctype->transforms);
    return bytes;
}

void
ToriRS_IdkFree(struct ToriRS_Idk* idk)
{
    if( !idk )
        return;
    ToriMem_Free(ToriMem_Assets(), idk->model_ids);
    ToriMem_Free(ToriMem_Assets(), idk);
}

size_t
ToriRS_IdkSizeOf(const struct ToriRS_Idk* idk)
{
    assert(idk);

    size_t bytes = sizeof(*idk);
    if( idk->model_ids )
        bytes += (size_t)idk->model_ids_count * sizeof(*idk->model_ids);
    return bytes;
}

void
ToriRS_ObjtypeFree(struct ToriRS_Objtype* objtype)
{
    int i;
    if( !objtype )
        return;
    ToriMem_Free(ToriMem_Assets(), objtype->recolors_from);
    ToriMem_Free(ToriMem_Assets(), objtype->recolors_to);
    if( objtype->params )
    {
        for( i = 0; i < objtype->param_count; i++ )
            ToriMem_Free(ToriMem_Assets(), objtype->params[i].string_value);
        ToriMem_Free(ToriMem_Assets(), objtype->params);
    }
    ToriMem_Free(ToriMem_Assets(), objtype);
}

size_t
ToriRS_ObjtypeSizeOf(const struct ToriRS_Objtype* objtype)
{
    assert(objtype);

    size_t bytes = sizeof(*objtype);
    if( objtype->recolors_from )
        bytes += (size_t)objtype->recolor_count * sizeof(*objtype->recolors_from);
    if( objtype->recolors_to )
        bytes += (size_t)objtype->recolor_count * sizeof(*objtype->recolors_to);
    return bytes;
}

void
ToriRS_SpotanimtypeFree(struct ToriRS_Spotanimtype* spotanimtype)
{
    /* No owned arrays — all fields are fixed-size. */
    ToriMem_Free(ToriMem_Assets(), spotanimtype);
}

size_t
ToriRS_SpotanimtypeSizeOf(const struct ToriRS_Spotanimtype* spotanimtype)
{
    assert(spotanimtype);
    return sizeof(*spotanimtype);
}

static void
torirs_animbase_free(struct ToriRS_AnimBase* base)
{
    if( !base )
        return;

    if( base->bone_groups )
    {
        for( int i = 0; i < base->length; i++ )
            ToriMem_Free(ToriMem_Assets(), base->bone_groups[i]);
        ToriMem_Free(ToriMem_Assets(), base->bone_groups);
    }
    ToriMem_Free(ToriMem_Assets(), base->bone_group_lengths);
    ToriMem_Free(ToriMem_Assets(), base->types);
    ToriMem_Free(ToriMem_Assets(), base);
}

static size_t
torirs_animbase_sizeof(const struct ToriRS_AnimBase* base)
{
    assert(base);

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
ToriRS_AnimayaSkinSizeOf(const struct ToriRS_AnimayaSkin* skin)
{
    assert(skin);

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
ToriRS_AnimayaSkinFree(struct ToriRS_AnimayaSkin* skin)
{
    if( !skin )
        return;
    if( skin->groups )
    {
        for( int i = 0; i < skin->vertex_count; i++ )
            ToriMem_Free(ToriMem_Assets(), skin->groups[i]);
        ToriMem_Free(ToriMem_Assets(), skin->groups);
    }
    if( skin->scales )
    {
        for( int i = 0; i < skin->vertex_count; i++ )
            ToriMem_Free(ToriMem_Assets(), skin->scales[i]);
        ToriMem_Free(ToriMem_Assets(), skin->scales);
    }
    ToriMem_Free(ToriMem_Assets(), skin->group_counts);
    ToriMem_Free(ToriMem_Assets(), skin);
}

size_t
ToriRS_SkeletalAnimSizeOf(const struct ToriRS_SkeletalAnim* skeletal)
{
    assert(skeletal);

    size_t bytes = sizeof(*skeletal);
    if( skeletal->matrices )
        bytes += (size_t)skeletal->frame_count * (size_t)skeletal->bone_count * 16u * sizeof(float);
    return bytes;
}

void
ToriRS_SkeletalAnimFree(struct ToriRS_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return;
    ToriMem_Free(ToriMem_Assets(), skeletal->matrices);
    ToriMem_Free(ToriMem_Assets(), skeletal);
}

static size_t
torirs_animframe_sizeof(const struct ToriRS_AnimFrame* frame)
{
    assert(frame);
    if( frame->length <= 0 )
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
ToriRS_AnimationSizeOf(const struct ToriRS_Animation* anim)
{
    assert(anim);

    size_t bytes = sizeof(*anim);
    bytes += torirs_animbase_sizeof(anim->base);
    if( anim->frames )
    {
        bytes += (size_t)anim->frame_count * sizeof(*anim->frames);
        for( int i = 0; i < anim->frame_count; i++ )
            bytes += torirs_animframe_sizeof(&anim->frames[i]);
    }
    return bytes;
}

void
ToriRS_AnimationFree(struct ToriRS_Animation* anim)
{
    if( !anim )
        return;

    torirs_animbase_free(anim->base);

    if( anim->frames )
    {
        for( int i = 0; i < anim->frame_count; i++ )
        {
            ToriMem_Free(ToriMem_Assets(), anim->frames[i].groups);
            ToriMem_Free(ToriMem_Assets(), anim->frames[i].x);
            ToriMem_Free(ToriMem_Assets(), anim->frames[i].y);
            ToriMem_Free(ToriMem_Assets(), anim->frames[i].z);
        }
        ToriMem_Free(ToriMem_Assets(), anim->frames);
    }

    ToriMem_Free(ToriMem_Assets(), anim);
}

static void
torirs_bones_free(struct ToriRS_Bones* bones)
{
    if( !bones )
        return;
    if( bones->bones )
    {
        for( int i = 0; i < bones->bones_count; i++ )
            ToriMem_Free(ToriMem_Assets(), bones->bones[i]);
        ToriMem_Free(ToriMem_Assets(), bones->bones);
    }
    ToriMem_Free(ToriMem_Assets(), bones->bones_sizes);
    ToriMem_Free(ToriMem_Assets(), bones);
}

static size_t
torirs_bones_sizeof(const struct ToriRS_Bones* bones)
{
    assert(bones);

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
ToriRS_ModelSizeOf(const struct ToriRS_Model* model)
{
    assert(model);

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
    if( model->texture_render_types )
        bytes += (size_t)model->textured_face_count * sizeof(*model->texture_render_types);
    if( model->face_texture_coords )
        bytes += (size_t)model->textured_face_count * sizeof(*model->face_texture_coords);
    bytes += torirs_bones_sizeof(model->vertex_bones);
    bytes += torirs_bones_sizeof(model->face_bones);
    if( model->bounds_cylinder )
        bytes += sizeof(*model->bounds_cylinder);
    bytes += ToriRS_AnimayaSkinSizeOf(model->animaya_skin);
    return bytes;
}

void
ToriRS_ModelReleaseArrays(struct ToriRS_Model* model)
{
    if( !model )
        return;

    ToriMem_Free(ToriMem_Assets(), model->vertices_x);
    model->vertices_x = NULL;
    ToriMem_Free(ToriMem_Assets(), model->vertices_y);
    model->vertices_y = NULL;
    ToriMem_Free(ToriMem_Assets(), model->vertices_z);
    model->vertices_z = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_colors_a);
    model->face_colors_a = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_colors_b);
    model->face_colors_b = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_colors_c);
    model->face_colors_c = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_indices_a);
    model->face_indices_a = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_indices_b);
    model->face_indices_b = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_indices_c);
    model->face_indices_c = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_textures);
    model->face_textures = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_alphas);
    model->face_alphas = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_infos);
    model->face_infos = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_priorities);
    model->face_priorities = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_colors);
    model->face_colors = NULL;
    ToriMem_Free(ToriMem_Assets(), model->textured_p_coordinate);
    model->textured_p_coordinate = NULL;
    ToriMem_Free(ToriMem_Assets(), model->textured_m_coordinate);
    model->textured_m_coordinate = NULL;
    ToriMem_Free(ToriMem_Assets(), model->textured_n_coordinate);
    model->textured_n_coordinate = NULL;
    ToriMem_Free(ToriMem_Assets(), model->texture_render_types);
    model->texture_render_types = NULL;
    ToriMem_Free(ToriMem_Assets(), model->face_texture_coords);
    model->face_texture_coords = NULL;
    torirs_bones_free(model->vertex_bones);
    model->vertex_bones = NULL;
    torirs_bones_free(model->face_bones);
    model->face_bones = NULL;
    ToriMem_Free(ToriMem_Assets(), model->bounds_cylinder);
    model->bounds_cylinder = NULL;
    ToriRS_AnimayaSkinFree(model->animaya_skin);
    model->animaya_skin = NULL;
}

void
ToriRS_ModelAssertPnmTextureInvariant(struct ToriRS_Model const* model)
{
    assert(model);

    if( !model->face_texture_coords || model->face_count <= 0 )
        return;

    for( int i = 0; i < model->face_count; i++ )
    {
        const int texture_face = model->face_texture_coords[i];
        if( texture_face == -1 )
            continue;

        assert(model->textured_face_count > 0);
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);
        assert(texture_face >= 0);
        assert(texture_face < model->textured_face_count);

        /* Only render type 0 stores vertex indices here; types 1-3 store a raw
         * projection axis, which is not an index and is normally out of range. */
        const int render_type =
            model->texture_render_types ? (model->texture_render_types[texture_face] & 0xFF) : 0;
        if( render_type != 0 )
            continue;

        assert(model->textured_p_coordinate[texture_face] >= 0);
        assert(model->textured_p_coordinate[texture_face] < model->vertex_count);
        assert(model->textured_m_coordinate[texture_face] >= 0);
        assert(model->textured_m_coordinate[texture_face] < model->vertex_count);
        assert(model->textured_n_coordinate[texture_face] >= 0);
        assert(model->textured_n_coordinate[texture_face] < model->vertex_count);
    }
}

void
ToriRS_ModelFree(struct ToriRS_Model* model)
{
    if( !model )
        return;

    ToriRS_ModelReleaseArrays(model);
    ToriMem_Free(ToriMem_Assets(), model);
}

size_t
ToriRS_TextureSizeOf(const struct ToriRS_Texture* texture)
{
    assert(texture);

    size_t bytes = sizeof(*texture);
    if( texture->texels )
        bytes += (size_t)texture->width * (size_t)texture->height * sizeof(*texture->texels);
    return bytes;
}

void
ToriRS_TextureFree(struct ToriRS_Texture* texture)
{
    if( !texture )
        return;
    ToriMem_Free(ToriMem_Assets(), texture->texels);
    ToriMem_Free(ToriMem_Assets(), texture);
}

size_t
ToriRS_SequenceSizeOf(const struct ToriRS_Sequence* seq)
{
    assert(seq);

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
                bytes += torirs_animframe_sizeof(&seq->resolved->frames[i]);
        }
    }
    return bytes;
}

void
ToriRS_SequenceFree(struct ToriRS_Sequence* seq)
{
    if( !seq )
        return;
    if( seq->resolved )
    {
        if( seq->resolved->frames )
        {
            for( int i = 0; i < seq->resolved->frame_count; i++ )
            {
                ToriMem_Free(ToriMem_Assets(), seq->resolved->frames[i].groups);
                ToriMem_Free(ToriMem_Assets(), seq->resolved->frames[i].x);
                ToriMem_Free(ToriMem_Assets(), seq->resolved->frames[i].y);
                ToriMem_Free(ToriMem_Assets(), seq->resolved->frames[i].z);
            }
            ToriMem_Free(ToriMem_Assets(), seq->resolved->frames);
        }
        ToriMem_Free(ToriMem_Assets(), seq->resolved);
    }
    ToriMem_Free(ToriMem_Assets(), seq->frames);
    ToriMem_Free(ToriMem_Assets(), seq->iframes);
    ToriMem_Free(ToriMem_Assets(), seq->delay);
    ToriMem_Free(ToriMem_Assets(), seq);
}

static size_t
torirs_sprite_frame_sizeof(const struct ToriRS_SpriteFrame* frame)
{
    assert(frame);
    size_t bytes = sizeof(*frame);
    if( frame->pixels_argb && frame->width > 0 && frame->height > 0 )
        bytes += (size_t)frame->width * (size_t)frame->height * sizeof(uint32_t);
    return bytes;
}

void
ToriRS_SpriteFrameFree(struct ToriRS_SpriteFrame* frame)
{
    if( !frame )
        return;
    ToriMem_Free(ToriMem_Assets(), frame->pixels_argb);
    ToriMem_Free(ToriMem_Assets(), frame);
}

size_t
ToriRS_SpriteSizeOf(const struct ToriRS_Sprite* sprite)
{
    assert(sprite);
    size_t bytes = sizeof(*sprite);
    if( sprite->frames )
    {
        bytes += (size_t)sprite->frame_count * sizeof(*sprite->frames);
        for( int i = 0; i < sprite->frame_count; i++ )
            bytes += torirs_sprite_frame_sizeof(&sprite->frames[i]);
    }
    return bytes;
}

void
ToriRS_SpriteFree(struct ToriRS_Sprite* sprite)
{
    if( !sprite )
        return;
    if( sprite->frames )
    {
        for( int i = 0; i < sprite->frame_count; i++ )
            ToriMem_Free(ToriMem_Assets(), sprite->frames[i].pixels_argb);
        ToriMem_Free(ToriMem_Assets(), sprite->frames);
    }
    ToriMem_Free(ToriMem_Assets(), sprite);
}

size_t
ToriRS_FontSizeOf(const struct ToriRS_Font* font)
{
    assert(font);
    size_t bytes = sizeof(*font);
    for( int i = 0; i < TORIRS_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_alpha[i] && font->glyph_width[i] > 0 && font->glyph_height[i] > 0 )
            bytes += (size_t)font->glyph_width[i] * (size_t)font->glyph_height[i];
    }
    return bytes;
}

void
ToriRS_FontFree(struct ToriRS_Font* font)
{
    if( !font )
        return;
    for( int i = 0; i < TORIRS_FONT_GLYPH_COUNT; i++ )
        ToriMem_Free(ToriMem_Assets(), font->glyph_alpha[i]);
    ToriMem_Free(ToriMem_Assets(), font);
}

size_t
ToriRS_SoundSizeOf(const struct ToriRS_Sound* sound)
{
    assert(sound);
    size_t samples = (size_t)(sound->sample_count > 0 ? sound->sample_count : 0);
    return sizeof(*sound) + (sound->pcm16 ? samples * sizeof(*sound->pcm16) : samples);
}

void
ToriRS_SoundFree(struct ToriRS_Sound* sound)
{
    if( !sound )
        return;
    ToriMem_Free(ToriMem_Assets(), sound->pcm);
    ToriMem_Free(ToriMem_Assets(), sound->pcm16);
    ToriMem_Free(ToriMem_Assets(), sound);
}

void
ToriRS_EnumFree(struct ToriRS_Enum* e)
{
    int i;

    if( !e )
        return;
    ToriMem_Free(ToriMem_Assets(), e->default_string);
    ToriMem_Free(ToriMem_Assets(), e->keys);
    ToriMem_Free(ToriMem_Assets(), e->int_values);
    if( e->string_values )
    {
        for( i = 0; i < e->count; i++ )
            ToriMem_Free(ToriMem_Assets(), e->string_values[i]);
        ToriMem_Free(ToriMem_Assets(), e->string_values);
    }
    ToriMem_Free(ToriMem_Assets(), e);
}

void
ToriRS_StructFree(struct ToriRS_Struct* s)
{
    int i;

    if( !s )
        return;
    if( s->params )
    {
        for( i = 0; i < s->param_count; i++ )
            ToriMem_Free(ToriMem_Assets(), s->params[i].string_value);
        ToriMem_Free(ToriMem_Assets(), s->params);
    }
    ToriMem_Free(ToriMem_Assets(), s);
}

void
ToriRS_ParamTypeFree(struct ToriRS_ParamType* p)
{
    if( !p )
        return;
    ToriMem_Free(ToriMem_Assets(), p->default_string);
    ToriMem_Free(ToriMem_Assets(), p);
}

void
ToriRS_ComponentApplyWalkLayout(
    struct ToriRS_Component* component,
    int parent_id,
    int rel_x,
    int rel_y)
{
    assert(component);
    component->parent_id = parent_id;
    component->rel_x = rel_x;
    component->rel_y = rel_y;
}

size_t
ToriRS_ComponentSizeOf(const struct ToriRS_Component* component)
{
    assert(component);

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
    if( component->inv_slots )
        bytes += sizeof(*component->inv_slots);
    return bytes;
}

struct ToriRS_ComponentInvSlots*
ToriRS_ComponentInvSlotsEnsure(struct ToriRS_Component* component)
{
    assert(component);

    if( !component->inv_slots )
    {
        component->inv_slots = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*component->inv_slots));
        assert(component->inv_slots);
    }
    return component->inv_slots;
}

static void
torirs_component_release_owned(struct ToriRS_Component* component)
{
    if( !component )
        return;

    /* Before the CS1 early-out below: the hooks are owned whether or not this
     * component also carries CS1 scripts, and most that have hooks have none. */
    ToriRS_ComponentHooksFree(component);

    /* Ahead of the CS1 early-out below too: an inventory grid need not carry
     * any scripts, and the columns are owned either way. */
    ToriMem_Free(ToriMem_Assets(), component->inv_slots);
    component->inv_slots = NULL;

    if( !component->scripts_lengths )
        return;

    if( component->scripts )
    {
        for( int i = 0; i < component->scripts_count; i++ )
            ToriMem_Free(ToriMem_Assets(), component->scripts[i]);
        ToriMem_Free(ToriMem_Assets(), component->scripts);
        component->scripts = NULL;
    }
    ToriMem_Free(ToriMem_Assets(), component->scripts_lengths);
    component->scripts_lengths = NULL;
    ToriMem_Free(ToriMem_Assets(), component->script_comparator);
    component->script_comparator = NULL;
    ToriMem_Free(ToriMem_Assets(), component->script_operand);
    component->script_operand = NULL;
}

void
ToriRS_ComponentFree(struct ToriRS_Component* component)
{
    if( !component )
        return;

    torirs_component_release_owned(component);
    ToriMem_Free(ToriMem_Assets(), component);
}

void
ToriRS_ComponentPackFree(struct ToriRS_ComponentPack* pack)
{
    if( !pack )
        return;

    if( pack->components )
    {
        for( int i = 0; i < pack->component_count; i++ )
            torirs_component_release_owned(&pack->components[i]);
        ToriMem_Free(ToriMem_Assets(), pack->components);
    }
    ToriMem_Free(ToriMem_Assets(), pack);
}

size_t
ToriRS_ComponentPackSizeOf(const struct ToriRS_ComponentPack* pack)
{
    assert(pack);

    size_t bytes = sizeof(*pack);
    if( pack->components )
    {
        bytes += (size_t)pack->component_count * sizeof(*pack->components);
        for( int i = 0; i < pack->component_count; i++ )
            bytes += ToriRS_ComponentSizeOf(&pack->components[i]);
    }
    return bytes;
}

void
ToriRS_NpctypeEntityFacts(
    struct ToriRS_Npctype const* drawn,
    struct ToriRS_Npctype const* shell,
    struct ToriRS_NpcEntityFacts* out)
{
    assert(drawn);
    assert(out);

    out->size = drawn->size > 0 ? drawn->size : 1;
    out->readyanim = drawn->readyanim;
    out->walkanim = drawn->walkanim;
    out->walkanim_b = drawn->walkanim_b;
    out->walkanim_l = drawn->walkanim_l;
    out->walkanim_r = drawn->walkanim_r;
    out->turnanim = drawn->turnanim_l;
    out->turnanim_r = drawn->turnanim_r;
    out->runanim = drawn->runanim;
    out->runanim_b = drawn->runanim_b;
    out->runanim_l = drawn->runanim_l;
    out->runanim_r = drawn->runanim_r;
    out->crawlanim = drawn->crawlanim;
    out->crawlanim_b = drawn->crawlanim_b;
    out->crawlanim_l = drawn->crawlanim_l;
    out->crawlanim_r = drawn->crawlanim_r;

    /* No shell, or a rung that IS its own shell, has nothing to fill from. */
    if( !shell || shell == drawn )
        return;

    if( out->size <= 1 && shell->size > 1 )
        out->size = shell->size;
    if( out->readyanim < 0 )
        out->readyanim = shell->readyanim;
    if( out->walkanim < 0 )
        out->walkanim = shell->walkanim;
    if( out->walkanim_b < 0 )
        out->walkanim_b = shell->walkanim_b;
    if( out->walkanim_l < 0 )
        out->walkanim_l = shell->walkanim_l;
    if( out->turnanim < 0 )
        out->turnanim = shell->turnanim_l;
    if( out->turnanim_r < 0 )
        out->turnanim_r = shell->turnanim_r;
    if( out->crawlanim < 0 )
        out->crawlanim = shell->crawlanim;
    if( out->crawlanim_b < 0 )
        out->crawlanim_b = shell->crawlanim_b;
    if( out->crawlanim_l < 0 )
        out->crawlanim_l = shell->crawlanim_l;
    if( out->crawlanim_r < 0 )
        out->crawlanim_r = shell->crawlanim_r;
    if( out->runanim < 0 )
        out->runanim = shell->runanim;
    if( out->runanim_b < 0 )
        out->runanim_b = shell->runanim_b;
    if( out->runanim_l < 0 )
        out->runanim_l = shell->runanim_l;
    if( out->runanim_r < 0 )
        out->runanim_r = shell->runanim_r;
    if( out->walkanim_r < 0 )
        out->walkanim_r = shell->walkanim_r;
}
