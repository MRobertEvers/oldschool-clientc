#include "dat2a_mem.h"

#include "dat2a_animaya.h"
#include "dat2a_config_floortype.h"
#include "dat2a_config_idk.h"
#include "dat2a_config_locs.h"
#include "dat2a_config_npctype.h"
#include "dat2a_config_object.h"
#include "dat2a_config_sequence.h"
#include "dat2a_frame.h"
#include "dat2a_framemap.h"
#include "dat2a_maps.h"
#include "dat2a_model.h"

#include <string.h>

int
dat2a_curve_sizeof(const struct RSCacheDat2A_Curve* c)
{
    if( !c )
        return 0;

    size_t bytes = sizeof(*c);
    if( c->points )
        bytes += (size_t)c->point_count * sizeof(*c->points);
    if( c->values )
        bytes += (size_t)(c->end_tick - c->start_tick + 1) * sizeof(*c->values);
    return bytes;
}

size_t
RSCacheDat2A_ModelSizeOf(const struct RSCacheDat2A_Model* model)
{
    assert(model);

    size_t bytes = sizeof(*model);
    if( model->vertices_x )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_x);
    if( model->vertices_y )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_y);
    if( model->vertices_z )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertices_z);
    if( model->vertex_bone_map )
        bytes += (size_t)model->vertex_count * sizeof(*model->vertex_bone_map);
    if( model->face_indices_a )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_a);
    if( model->face_indices_b )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_b);
    if( model->face_indices_c )
        bytes += (size_t)model->face_count * sizeof(*model->face_indices_c);
    if( model->face_colors )
        bytes += (size_t)model->face_count * sizeof(*model->face_colors);
    if( model->face_priorities )
        bytes += (size_t)model->face_count * sizeof(*model->face_priorities);
    if( model->face_alphas )
        bytes += (size_t)model->face_count * sizeof(*model->face_alphas);
    if( model->face_infos )
        bytes += (size_t)model->face_count * sizeof(*model->face_infos);
    if( model->textured_p_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_p_coordinate);
    if( model->textured_m_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_m_coordinate);
    if( model->textured_n_coordinate )
        bytes += (size_t)model->textured_face_count * sizeof(*model->textured_n_coordinate);
    if( model->face_texture_coords )
        bytes += (size_t)model->textured_face_count * sizeof(*model->face_texture_coords);
    if( model->textureRenderTypes )
        bytes += (size_t)model->face_count * sizeof(*model->textureRenderTypes);
    if( model->face_textures )
        bytes += (size_t)model->face_count * sizeof(*model->face_textures);
    if( model->face_bone_map )
        bytes += (size_t)model->face_count * sizeof(*model->face_bone_map);
    if( model->animaya_group_counts )
        bytes += (size_t)model->animaya_vertex_count * sizeof(*model->animaya_group_counts);
    if( model->animaya_groups )
    {
        bytes += (size_t)model->animaya_vertex_count * sizeof(*model->animaya_groups);
        for( int i = 0; i < model->animaya_vertex_count; i++ )
            bytes += (size_t)model->animaya_group_counts[i] * sizeof(**model->animaya_groups);
    }
    if( model->animaya_scales )
    {
        bytes += (size_t)model->animaya_vertex_count * sizeof(*model->animaya_scales);
        for( int i = 0; i < model->animaya_vertex_count; i++ )
            bytes += (size_t)model->animaya_group_counts[i] * sizeof(**model->animaya_scales);
    }
    return bytes;
}

size_t
RSCacheDat2A_MapTerrainSizeOf(const struct RSCacheDat2A_MapTerrain* map_terrain)
{
    if( !map_terrain )
        return 0;
    return sizeof(*map_terrain);
}

size_t
RSCacheDat2A_MapLocsSizeOf(const struct RSCacheDat2A_MapLocs* map_locs)
{
    if( !map_locs )
        return 0;

    size_t bytes = sizeof(*map_locs);
    if( map_locs->locs )
        bytes += (size_t)map_locs->locs_count * sizeof(*map_locs->locs);
    return bytes;
}

size_t
RSCacheDat2A_ConfigSequenceSizeOf(const struct RSCacheDat2A_ConfigSequence* def)
{
    if( !def )
        return 0;

    size_t bytes = sizeof(*def);
    if( def->frame_ids )
        bytes += (size_t)def->frame_count * sizeof(*def->frame_ids);
    if( def->frame_lengths )
        bytes += (size_t)def->frame_count * sizeof(*def->frame_lengths);
    if( def->interleave_leave )
        bytes += (size_t)def->frame_count * sizeof(*def->interleave_leave);
    if( def->chat_frame_ids )
        bytes += (size_t)def->frame_count * sizeof(*def->chat_frame_ids);
    if( def->anim_maya_masks )
        bytes += (size_t)def->frame_count * sizeof(*def->anim_maya_masks);
    if( def->debug_name )
        bytes += strlen(def->debug_name) + 1u;
    if( def->frame_sounds.frames )
        bytes += (size_t)def->frame_sounds.count * sizeof(*def->frame_sounds.frames);
    if( def->frame_sounds.sounds )
        bytes += (size_t)def->frame_sounds.count * sizeof(*def->frame_sounds.sounds);
    return bytes;
}

size_t
RSCacheDat2A_ConfigOverlaySizeOf(const struct RSCacheDat2A_ConfigOverlay* overlay)
{
    if( !overlay )
        return 0;

    size_t bytes = sizeof(*overlay);
    if( overlay->flotype_name )
        bytes += strlen(overlay->flotype_name) + 1u;
    return bytes;
}

size_t
RSCacheDat2A_ConfigUnderlaySizeOf(const struct RSCacheDat2A_ConfigUnderlay* underlay)
{
    if( !underlay )
        return 0;
    return sizeof(*underlay);
}

static size_t
dat2a_config_location_models_sizeof(const struct RSCacheDat2A_ConfigLocation* loc)
{
    if( !loc || !loc->models )
        return 0;

    size_t bytes = (size_t)loc->shapes_and_model_count * sizeof(*loc->models);
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        bytes += (size_t)loc->lengths[i] * sizeof(**loc->models);
    return bytes;
}

size_t
RSCacheDat2A_ConfigLocationSizeOf(const struct RSCacheDat2A_ConfigLocation* loc)
{
    if( !loc )
        return 0;

    size_t bytes = sizeof(*loc);
    for( int i = 0; i < 10; i++ )
        if( loc->actions[i] )
            bytes += strlen(loc->actions[i]) + 1u;
    if( loc->name )
        bytes += strlen(loc->name) + 1u;
    if( loc->desc )
        bytes += strlen(loc->desc) + 1u;
    if( loc->shapes )
        bytes += (size_t)loc->shapes_and_model_count * sizeof(*loc->shapes);
    bytes += dat2a_config_location_models_sizeof(loc);
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

size_t
RSCacheDat2A_AnimMayaSizeOf(const struct RSCacheDat2A_AnimMaya* maya)
{
    if( !maya )
        return 0;

    size_t bytes = sizeof(*maya);
    if( maya->bone_curves )
    {
        bytes += (size_t)maya->bone_curve_count * sizeof(*maya->bone_curves);
        for( int b = 0; b < maya->bone_curve_count; b++ )
            for( int s = 0; s < 9; s++ )
                bytes += dat2a_curve_sizeof(maya->bone_curves[b].curves[s]);
    }
    return bytes;
}

size_t
RSCacheDat2A_ConfigIdkSizeOf(const struct RSCacheDat2A_ConfigIdk* idk)
{
    if( !idk )
        return 0;
    return sizeof(*idk);
}

size_t
RSCacheDat2A_ConfigObjectSizeOf(const struct RSCacheDat2A_ConfigObject* object)
{
    if( !object )
        return 0;

    size_t bytes = sizeof(*object);
    if( object->name )
        bytes += strlen(object->name) + 1u;
    if( object->examine )
        bytes += strlen(object->examine) + 1u;
    return bytes;
}

size_t
RSCacheDat2A_ConfigNpctypeSizeOf(const struct RSCacheDat2A_ConfigNpctype* npc)
{
    if( !npc )
        return 0;

    size_t bytes = sizeof(*npc);
    if( npc->name )
        bytes += strlen(npc->name) + 1u;
    if( npc->models )
        bytes += (size_t)npc->models_count * sizeof(*npc->models);
    if( npc->recolor_to_find )
        bytes += (size_t)npc->recolor_count * sizeof(*npc->recolor_to_find);
    if( npc->recolor_to_replace )
        bytes += (size_t)npc->recolor_count * sizeof(*npc->recolor_to_replace);
    if( npc->retexture_to_find )
        bytes += (size_t)npc->retexture_count * sizeof(*npc->retexture_to_find);
    if( npc->retexture_to_replace )
        bytes += (size_t)npc->retexture_count * sizeof(*npc->retexture_to_replace);
    if( npc->chathead_models )
        bytes += (size_t)npc->chathead_models_count * sizeof(*npc->chathead_models);
    if( npc->head_icon_archive_ids )
        bytes += (size_t)npc->head_icon_count * sizeof(*npc->head_icon_archive_ids);
    if( npc->head_icon_sprite_index )
        bytes += (size_t)npc->head_icon_count * sizeof(*npc->head_icon_sprite_index);
    for( int i = 0; i < 5; i++ )
        if( npc->actions[i] )
            bytes += strlen(npc->actions[i]) + 1u;
    if( npc->configs )
        bytes += (size_t)npc->configs_count * sizeof(*npc->configs);
    if( npc->params.keys )
        bytes += (size_t)npc->params.count * sizeof(*npc->params.keys);
    if( npc->params.values )
    {
        bytes += (size_t)npc->params.count * sizeof(*npc->params.values);
        for( int i = 0; i < npc->params.count; i++ )
            if( npc->params.is_string[i] && npc->params.values[i] )
                bytes += strlen((const char*)npc->params.values[i]) + 1u;
    }
    if( npc->params.is_string )
        bytes += (size_t)npc->params.count * sizeof(*npc->params.is_string);
    return bytes;
}

size_t
RSCacheDat2A_FrameSizeOf(const struct RSCacheDat2A_Frame* def)
{
    if( !def )
        return 0;

    size_t bytes = sizeof(*def);
    if( def->index_frame_ids )
        bytes += (size_t)def->translator_count * sizeof(*def->index_frame_ids);
    if( def->translator_arg_x )
        bytes += (size_t)def->translator_count * sizeof(*def->translator_arg_x);
    if( def->translator_arg_y )
        bytes += (size_t)def->translator_count * sizeof(*def->translator_arg_y);
    if( def->translator_arg_z )
        bytes += (size_t)def->translator_count * sizeof(*def->translator_arg_z);
    return bytes;
}

size_t
RSCacheDat2A_FramemapSizeOf(const struct RSCacheDat2A_Framemap* def)
{
    if( !def )
        return 0;

    size_t bytes = sizeof(*def);
    if( def->types )
        bytes += (size_t)def->length * sizeof(*def->types);
    if( def->bone_groups )
    {
        bytes += (size_t)def->length * sizeof(*def->bone_groups);
        for( int i = 0; i < def->length; i++ )
            if( def->bone_groups[i] )
                bytes += (size_t)def->bone_groups_lengths[i] * sizeof(**def->bone_groups);
    }
    if( def->bone_groups_lengths )
        bytes += (size_t)def->length * sizeof(*def->bone_groups_lengths);
    return bytes;
}
