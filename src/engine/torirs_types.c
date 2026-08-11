#include "torirs_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

size_t
ToriRS_MapTerrainSizeOf(const struct ToriRS_MapTerrain* terrain)
{
    if( !terrain )
        return 0;
    return sizeof(*terrain);
}

void
ToriRS_MapTerrainFree(struct ToriRS_MapTerrain* terrain)
{
    free(terrain);
}

size_t
ToriRS_MapLocsSizeOf(const struct ToriRS_MapLocs* locs)
{
    if( !locs )
        return 0;
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
    free(locs->locs);
    free(locs);
}

size_t
ToriRS_FlotypeSizeOf(const struct ToriRS_Flotype* flotype)
{
    if( !flotype )
        return 0;
    return sizeof(*flotype);
}

void
ToriRS_FlotypeFree(struct ToriRS_Flotype* flotype)
{
    free(flotype);
}

static void
torirs_location_free_models(struct ToriRS_Location* loc)
{
    assert(loc);
    if( !loc->models )
        return;
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        free(loc->models[i]);
    free(loc->models);
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
    if( !loc )
        return 0;

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
    free(loc->shapes);
    torirs_location_free_models(loc);
    free(loc->lengths);
    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc->transforms);
    free(loc->ambient_sound_ids);
    free(loc);
}

void
ToriRS_NpctypeFree(struct ToriRS_Npctype* npctype)
{
    if( !npctype )
        return;
    free(npctype->models);
    free(npctype->heads);
    free(npctype->recolors_from);
    free(npctype->recolors_to);
    free(npctype->retextures_from);
    free(npctype->retextures_to);
    free(npctype);
}

size_t
ToriRS_NpctypeSizeOf(const struct ToriRS_Npctype* npctype)
{
    if( !npctype )
        return 0;

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
    return bytes;
}

void
ToriRS_IdkFree(struct ToriRS_Idk* idk)
{
    if( !idk )
        return;
    free(idk->model_ids);
    free(idk);
}

size_t
ToriRS_IdkSizeOf(const struct ToriRS_Idk* idk)
{
    if( !idk )
        return 0;

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
    free(objtype->recolors_from);
    free(objtype->recolors_to);
    if( objtype->params )
    {
        for( i = 0; i < objtype->param_count; i++ )
            free(objtype->params[i].string_value);
        free(objtype->params);
    }
    free(objtype);
}

size_t
ToriRS_ObjtypeSizeOf(const struct ToriRS_Objtype* objtype)
{
    if( !objtype )
        return 0;

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
    free(spotanimtype);
}

size_t
ToriRS_SpotanimtypeSizeOf(const struct ToriRS_Spotanimtype* spotanimtype)
{
    if( !spotanimtype )
        return 0;
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
            free(base->bone_groups[i]);
        free(base->bone_groups);
    }
    free(base->bone_group_lengths);
    free(base->types);
    free(base);
}

static size_t
torirs_animbase_sizeof(const struct ToriRS_AnimBase* base)
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
ToriRS_AnimayaSkinSizeOf(const struct ToriRS_AnimayaSkin* skin)
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
ToriRS_AnimayaSkinFree(struct ToriRS_AnimayaSkin* skin)
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
ToriRS_SkeletalAnimSizeOf(const struct ToriRS_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return 0;

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
    free(skeletal->matrices);
    free(skeletal);
}

static size_t
torirs_animframe_sizeof(const struct ToriRS_AnimFrame* frame)
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
ToriRS_AnimationSizeOf(const struct ToriRS_Animation* anim)
{
    if( !anim )
        return 0;

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
torirs_bones_free(struct ToriRS_Bones* bones)
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
torirs_bones_sizeof(const struct ToriRS_Bones* bones)
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
ToriRS_ModelSizeOf(const struct ToriRS_Model* model)
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

    free(model->vertices_x);
    model->vertices_x = NULL;
    free(model->vertices_y);
    model->vertices_y = NULL;
    free(model->vertices_z);
    model->vertices_z = NULL;
    free(model->face_colors_a);
    model->face_colors_a = NULL;
    free(model->face_colors_b);
    model->face_colors_b = NULL;
    free(model->face_colors_c);
    model->face_colors_c = NULL;
    free(model->face_indices_a);
    model->face_indices_a = NULL;
    free(model->face_indices_b);
    model->face_indices_b = NULL;
    free(model->face_indices_c);
    model->face_indices_c = NULL;
    free(model->face_textures);
    model->face_textures = NULL;
    free(model->face_alphas);
    model->face_alphas = NULL;
    free(model->face_infos);
    model->face_infos = NULL;
    free(model->face_priorities);
    model->face_priorities = NULL;
    free(model->face_colors);
    model->face_colors = NULL;
    free(model->textured_p_coordinate);
    model->textured_p_coordinate = NULL;
    free(model->textured_m_coordinate);
    model->textured_m_coordinate = NULL;
    free(model->textured_n_coordinate);
    model->textured_n_coordinate = NULL;
    free(model->texture_render_types);
    model->texture_render_types = NULL;
    free(model->face_texture_coords);
    model->face_texture_coords = NULL;
    torirs_bones_free(model->vertex_bones);
    model->vertex_bones = NULL;
    torirs_bones_free(model->face_bones);
    model->face_bones = NULL;
    free(model->bounds_cylinder);
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

        const int p = model->textured_p_coordinate[texture_face];
        const int m = model->textured_m_coordinate[texture_face];
        const int n = model->textured_n_coordinate[texture_face];
        assert(p >= 0 && p < model->vertex_count);
        assert(m >= 0 && m < model->vertex_count);
        assert(n >= 0 && n < model->vertex_count);
    }
}

void
ToriRS_ModelFree(struct ToriRS_Model* model)
{
    if( !model )
        return;

    ToriRS_ModelReleaseArrays(model);
    free(model);
}

size_t
ToriRS_TextureSizeOf(const struct ToriRS_Texture* texture)
{
    if( !texture )
        return 0;

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
    free(texture->texels);
    free(texture);
}

size_t
ToriRS_SequenceSizeOf(const struct ToriRS_Sequence* seq)
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

static size_t
torirs_sprite_frame_sizeof(const struct ToriRS_SpriteFrame* frame)
{
    if( !frame )
        return 0;
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
    free(frame->pixels_argb);
    free(frame);
}

size_t
ToriRS_SpriteSizeOf(const struct ToriRS_Sprite* sprite)
{
    if( !sprite )
        return 0;
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
            free(sprite->frames[i].pixels_argb);
        free(sprite->frames);
    }
    free(sprite);
}

size_t
ToriRS_FontSizeOf(const struct ToriRS_Font* font)
{
    if( !font )
        return 0;
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
        free(font->glyph_alpha[i]);
    free(font);
}

size_t
ToriRS_SoundSizeOf(const struct ToriRS_Sound* sound)
{
    if( !sound )
        return 0;
    size_t samples = (size_t)(sound->sample_count > 0 ? sound->sample_count : 0);
    return sizeof(*sound) + (sound->pcm16 ? samples * sizeof(*sound->pcm16) : samples);
}

void
ToriRS_SoundFree(struct ToriRS_Sound* sound)
{
    if( !sound )
        return;
    free(sound->pcm);
    free(sound->pcm16);
    free(sound);
}

void
ToriRS_EnumFree(struct ToriRS_Enum* e)
{
    int i;

    if( !e )
        return;
    free(e->default_string);
    free(e->keys);
    free(e->int_values);
    if( e->string_values )
    {
        for( i = 0; i < e->count; i++ )
            free(e->string_values[i]);
        free(e->string_values);
    }
    free(e);
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
            free(s->params[i].string_value);
        free(s->params);
    }
    free(s);
}

void
ToriRS_ParamTypeFree(struct ToriRS_ParamType* p)
{
    if( !p )
        return;
    free(p->default_string);
    free(p);
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
    return bytes;
}

static void
torirs_component_release_owned(struct ToriRS_Component* component)
{
    if( !component || !component->scripts_lengths )
        return;

    if( component->scripts )
    {
        for( int i = 0; i < component->scripts_count; i++ )
            free(component->scripts[i]);
        free(component->scripts);
        component->scripts = NULL;
    }
    free(component->scripts_lengths);
    component->scripts_lengths = NULL;
    free(component->script_comparator);
    component->script_comparator = NULL;
    free(component->script_operand);
    component->script_operand = NULL;
}

void
ToriRS_ComponentFree(struct ToriRS_Component* component)
{
    if( !component )
        return;

    torirs_component_release_owned(component);
    free(component);
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
        free(pack->components);
    }
    free(pack);
}

size_t
ToriRS_ComponentPackSizeOf(const struct ToriRS_ComponentPack* pack)
{
    if( !pack )
        return 0;

    size_t bytes = sizeof(*pack);
    if( pack->components )
    {
        bytes += (size_t)pack->component_count * sizeof(*pack->components);
        for( int i = 0; i < pack->component_count; i++ )
            bytes += ToriRS_ComponentSizeOf(&pack->components[i]);
    }
    return bytes;
}
