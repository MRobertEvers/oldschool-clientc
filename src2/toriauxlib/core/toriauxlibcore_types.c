#include "toriauxlib/core/toriauxlibcore_types.h"

#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

#include <stdlib.h>
#include <string.h>

void
ToriAuxLibCore_MapTerrainFree(struct ToriAuxLibCore_MapTerrain* terrain)
{
    free(terrain);
}

void
ToriAuxLibCore_MapLocsFree(struct ToriAuxLibCore_MapLocs* locs)
{
    if( !locs )
        return;
    free(locs->locs);
    free(locs);
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
    free(model);
}

void
ToriAuxLibCore_TextureFree(struct ToriAuxLibCore_Texture* texture)
{
    if( !texture )
        return;
    free(texture->texels);
    free(texture);
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

void
ToriAuxLibCore_SpriteFree(struct ToriAuxLibCore_Sprite* sprite)
{
    if( !sprite )
        return;
    free(sprite);
}

void
ToriAuxLibCore_FontFree(struct ToriAuxLibCore_Font* font)
{
    if( !font )
        return;
    free(font);
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

