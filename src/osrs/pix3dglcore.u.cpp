#ifndef PIX3DGLCORE_U_CPP
#define PIX3DGLCORE_U_CPP

#include "graphics/uv_pnm.h"
#include <unordered_map>

#include <algorithm>
#include <vector>

// Declare g_hsl16_to_rgb_table (it's defined in shared_tables.c)
// Note: This should match the declaration in shared_tables.h
#ifdef __cplusplus
extern "C" {
#endif
extern int g_hsl16_to_rgb_table[65536];
#ifdef __cplusplus
}
#endif

struct Pix3DGLCoreFaceRange
{
    int start_vertex;
    int face_index;
};

struct Pix3DGLCoreDrawBatch
{
    int texture_id;
    std::vector<int> face_starts;
    std::vector<int> face_counts;
};

struct Pix3DGLCoreModelRange
{
    int scene_model_idx;
    int start_vertex;
    int vertex_count;
    std::vector<Pix3DGLCoreFaceRange> faces;
};

struct Pix3DGLCoreBuffers
{
    std::vector<float> vertices;
    std::vector<float> colors;
    std::vector<float> texCoords;
    std::vector<float> textureIds;
    std::vector<float> textureOpaques;
    std::vector<float> textureAnimSpeeds;
    std::unordered_map<int, int> texture_to_atlas_slot;
    std::unordered_map<int, bool> texture_to_opaque;
    std::unordered_map<int, float> texture_to_animation_speed;
    std::unordered_map<int, Pix3DGLCoreModelRange> model_ranges;

    std::unordered_map<int, Pix3DGLCoreDrawBatch> texture_batches;

    int total_vertex_count;
    int atlas_tiles_per_row;
    int atlas_tile_size;
    int atlas_size;
};

extern "C" void
pix3dgl_scene_static_load_model_core(
    struct Pix3DGLCoreBuffers* buffers,
    int scene_model_idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_textures_nullable,
    int* face_texture_coords_nullable,
    int* textured_p_coordinate_nullable,
    int* textured_m_coordinate_nullable,
    int* textured_n_coordinate_nullable,
    int* face_colors_hsl_a,
    int* face_colors_hsl_b,
    int* face_colors_hsl_c,
    int* face_infos_nullable,
    int* face_alphas_nullable,
    float position_x,
    float position_y,
    float position_z,
    float yaw)
{
    // Create transformation matrix for this model instance
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    int current_vertex_index = buffers->total_vertex_count;
    int start_vertex_index = current_vertex_index;

    // Temporary storage for face positions (will be moved to ModelRange later)
    // Pre-allocate with size of face_count to maintain face index mapping
    std::vector<Pix3DGLCoreFaceRange> face_ranges(face_count);

    // Initialize all face ranges with invalid start_vertex (-1)
    for( int i = 0; i < face_count; i++ )
    {
        face_ranges[i].start_vertex = -1;
        face_ranges[i].face_index = i;
    }

    // Process each face
    for( int face = 0; face < face_count; face++ )
    {
        // Check if face should be drawn
        bool should_draw = true;
        if( face_infos_nullable && face_infos_nullable[face] == 2 )
        {
            should_draw = false;
        }
        if( face_colors_hsl_c && face_colors_hsl_c[face] == -2 )
        {
            should_draw = false;
        }

        if( !should_draw )
            continue;

        int face_start_index = current_vertex_index;

        // Get vertex indices
        int v_a = face_indices_a[face];
        int v_b = face_indices_b[face];
        int v_c = face_indices_c[face];

        // Transform and add vertices
        int vertex_indices[] = { v_a, v_b, v_c };
        for( int v = 0; v < 3; v++ )
        {
            int vertex_idx = vertex_indices[v];

            // Get original vertex position
            float x = vertices_x[vertex_idx];
            float y = vertices_y[vertex_idx];
            float z = vertices_z[vertex_idx];

            // Apply transformation (rotation + translation)
            float tx = cos_yaw * x + sin_yaw * z + position_x;
            float ty = y + position_y;
            float tz = -sin_yaw * x + cos_yaw * z + position_z;

            // Append transformed vertex
            buffers->vertices.push_back(tx);
            buffers->vertices.push_back(ty);
            buffers->vertices.push_back(tz);
        }

        // Get and convert colors
        int hsl_a = face_colors_hsl_a[face];
        int hsl_b = face_colors_hsl_b[face];
        int hsl_c = face_colors_hsl_c[face];

        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Calculate face alpha (0xFF = fully opaque, 0x00 = fully transparent)
        // Convert from 0-255 to 0.0-1.0
        float face_alpha = 1.0f;
        int alpha_byte = 0xFF;
        if( face_alphas_nullable )
        {
            alpha_byte = face_alphas_nullable[face];
            // For untextured faces, invert as per render.c
            face_alpha = (0xFF - (alpha_byte & 0xFF)) / 255.0f;
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        buffers->colors.push_back(((rgb_a >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_a >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_a & 0xFF) / 255.0f);
        buffers->colors.push_back(face_alpha);

        buffers->colors.push_back(((rgb_b >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_b >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_b & 0xFF) / 255.0f);
        buffers->colors.push_back(face_alpha);

        buffers->colors.push_back(((rgb_c >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_c >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_c & 0xFF) / 255.0f);
        buffers->colors.push_back(face_alpha);

        // Declare texture variables at face loop scope
        int texture_id = -1;
        int atlas_slot = -1;

        // Compute UV coordinates if textured
        if( face_textures_nullable && face_textures_nullable[face] != -1 )
        {
            // Determine texture space vertices (P, M, N)
            int tp_vertex = v_a;
            int tm_vertex = v_b;
            int tn_vertex = v_c;

            if( face_texture_coords_nullable && face_texture_coords_nullable[face] != -1 )
            {
                int texture_face = face_texture_coords_nullable[face];
                tp_vertex = textured_p_coordinate_nullable[texture_face];
                tm_vertex = textured_m_coordinate_nullable[texture_face];
                tn_vertex = textured_n_coordinate_nullable[texture_face];
            }

            // Get PNM vertices
            float p_x = vertices_x[tp_vertex];
            float p_y = vertices_y[tp_vertex];
            float p_z = vertices_z[tp_vertex];

            float m_x = vertices_x[tm_vertex];
            float m_y = vertices_y[tm_vertex];
            float m_z = vertices_z[tm_vertex];

            float n_x = vertices_x[tn_vertex];
            float n_y = vertices_y[tn_vertex];
            float n_z = vertices_z[tn_vertex];

            // Get face vertices
            float a_x = vertices_x[v_a];
            float a_y = vertices_y[v_a];
            float a_z = vertices_z[v_a];

            float b_x = vertices_x[v_b];
            float b_y = vertices_y[v_b];
            float b_z = vertices_z[v_b];

            float c_x = vertices_x[v_c];
            float c_y = vertices_y[v_c];
            float c_z = vertices_z[v_c];

            struct UVFaceCoords uv_pnm;
            uv_pnm_compute(
                &uv_pnm,
                p_x,
                p_y,
                p_z,
                m_x,
                m_y,
                m_z,
                n_x,
                n_y,
                n_z,
                a_x,
                a_y,
                a_z,
                b_x,
                b_y,
                b_z,
                c_x,
                c_y,
                c_z);

            // Get atlas slot for this texture
            texture_id = face_textures_nullable ? face_textures_nullable[face] : -1;

            if( texture_id != -1 )
            {
                auto tex_it = buffers->texture_to_atlas_slot.find(texture_id);
                if( tex_it != buffers->texture_to_atlas_slot.end() )
                {
                    atlas_slot = tex_it->second;
                }
                else
                {
                    texture_id = -1;
                }
            }

            // Calculate atlas UVs on CPU (once, not per-frame!)
            if( atlas_slot >= 0 )
            {
                float tiles_per_row = (float)buffers->atlas_tiles_per_row;
                float tile_u = (float)(atlas_slot % buffers->atlas_tiles_per_row);
                float tile_v = (float)(atlas_slot / buffers->atlas_tiles_per_row);
                float tile_size = (float)buffers->atlas_tile_size;
                float atlas_size = (float)buffers->atlas_size;

                // Transform UVs to atlas space
                buffers->texCoords.push_back((tile_u + uv_pnm.u1) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v1) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_u + uv_pnm.u2) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v2) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_u + uv_pnm.u3) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v3) * tile_size / atlas_size);
            }
            else
            {
                // No valid atlas slot, store dummy UVs
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
            }

            // Store texture ID (atlas slot) for each vertex
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);

            // Store texture opaque flag for each vertex
            float is_opaque = 1.0f; // Default to opaque
            if( texture_id != -1 )
            {
                auto opaque_it = buffers->texture_to_opaque.find(texture_id);
                if( opaque_it != buffers->texture_to_opaque.end() )
                {
                    is_opaque = opaque_it->second ? 1.0f : 0.0f;
                }
            }
            buffers->textureOpaques.push_back(is_opaque);
            buffers->textureOpaques.push_back(is_opaque);
            buffers->textureOpaques.push_back(is_opaque);

            // Store texture animation speed for each vertex
            float anim_speed = 0.0f; // Default to no animation
            if( texture_id != -1 )
            {
                auto speed_it = buffers->texture_to_animation_speed.find(texture_id);
                if( speed_it != buffers->texture_to_animation_speed.end() )
                {
                    anim_speed = speed_it->second;
                }
            }
            buffers->textureAnimSpeeds.push_back(anim_speed);
            buffers->textureAnimSpeeds.push_back(anim_speed);
            buffers->textureAnimSpeeds.push_back(anim_speed);

            texture_id = (atlas_slot >= 0) ? texture_id : -1;
        }
        else
        {
            // Untextured face
            texture_id = -1;
            atlas_slot = -1;

            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);

            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);

            // Untextured faces: opaque flag doesn't matter, use 1.0
            buffers->textureOpaques.push_back(1.0f);
            buffers->textureOpaques.push_back(1.0f);
            buffers->textureOpaques.push_back(1.0f);

            // Untextured faces: no animation
            buffers->textureAnimSpeeds.push_back(0.0f);
            buffers->textureAnimSpeeds.push_back(0.0f);
            buffers->textureAnimSpeeds.push_back(0.0f);
        }

        Pix3DGLCoreDrawBatch& batch = buffers->texture_batches[texture_id];
        batch.texture_id = texture_id;

        // Merge contiguous faces
        if( !batch.face_starts.empty() &&
            batch.face_starts.back() + batch.face_counts.back() == face_start_index )
        {
            batch.face_counts.back() += 3;
        }
        else
        {
            batch.face_starts.push_back(face_start_index);
            batch.face_counts.push_back(3);
        }

        // Track this face's position in the buffer (use original face index)
        face_ranges[face].start_vertex = face_start_index;
        face_ranges[face].face_index = face;

        current_vertex_index += 3;
    }

    buffers->total_vertex_count = current_vertex_index;

    // Track the model's position in the buffer
    int vertex_count = current_vertex_index - start_vertex_index;
    if( vertex_count > 0 )
    {
        Pix3DGLCoreModelRange range;
        range.scene_model_idx = scene_model_idx;
        range.start_vertex = start_vertex_index;
        range.vertex_count = vertex_count;
        range.faces = std::move(face_ranges);
        buffers->model_ranges[scene_model_idx] = range;
    }
}

extern "C" void
pix3dgl_scene_static_load_tile_core(
    struct Pix3DGLCoreBuffers* buffers,
    int scene_tile_idx,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int vertex_count,
    int* faces_a,
    int* faces_b,
    int* faces_c,
    int face_count,
    int* face_texture_ids,
    int* face_color_hsl_a,
    int* face_color_hsl_b,
    int* face_color_hsl_c,
    std::vector<Pix3DGLCoreFaceRange>& face_ranges_out)
{
    int current_vertex_index = buffers->total_vertex_count;
    int start_vertex_index = current_vertex_index;

    // Pre-allocate face ranges with size of face_count to maintain face index mapping
    face_ranges_out.resize(face_count);

    // Initialize all face ranges with invalid start_vertex (-1)
    for( int i = 0; i < face_count; i++ )
    {
        face_ranges_out[i].start_vertex = -1;
        face_ranges_out[i].face_index = i;
    }

    // Process each face
    for( int face = 0; face < face_count; face++ )
    {
        int face_start_index = current_vertex_index;

        // Track this face's position in the buffer
        face_ranges_out[face].start_vertex = face_start_index;
        face_ranges_out[face].face_index = face;

        // Get vertex indices
        int v_a = faces_a[face];
        int v_b = faces_b[face];
        int v_c = faces_c[face];

        // Add the 3 vertices for this face
        for( int v = 0; v < 3; v++ )
        {
            int vertex_idx;
            if( v == 0 )
                vertex_idx = v_a;
            else if( v == 1 )
                vertex_idx = v_b;
            else
                vertex_idx = v_c;

            // Store vertex position (tiles are already in world space)
            buffers->vertices.push_back(vertex_x[vertex_idx]);
            buffers->vertices.push_back(vertex_y[vertex_idx]);
            buffers->vertices.push_back(vertex_z[vertex_idx]);
        }

        // Get and convert colors
        int hsl_a = face_color_hsl_a[face];
        int hsl_b = face_color_hsl_b[face];
        int hsl_c = face_color_hsl_c[face];

        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        // Tiles don't have face alphas, so use 1.0 (fully opaque)
        buffers->colors.push_back(((rgb_a >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_a >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_a & 0xFF) / 255.0f);
        buffers->colors.push_back(1.0f);

        buffers->colors.push_back(((rgb_b >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_b >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_b & 0xFF) / 255.0f);
        buffers->colors.push_back(1.0f);

        buffers->colors.push_back(((rgb_c >> 16) & 0xFF) / 255.0f);
        buffers->colors.push_back(((rgb_c >> 8) & 0xFF) / 255.0f);
        buffers->colors.push_back((rgb_c & 0xFF) / 255.0f);
        buffers->colors.push_back(1.0f);

        // Declare texture variables at face loop scope
        int texture_id = -1;
        int atlas_slot = -1;

        // For tiles, we'll compute UVs if needed
        if( face_texture_ids && face_texture_ids[face] != -1 )
        {
            // Compute UV coordinates using PNM method for tiles
            // P = vertex 0 (SW corner), M = vertex 1 (NW corner), N = vertex 3 (SE corner)
            int tp_vertex = 0;
            int tm_vertex = 1;
            int tn_vertex = 3;

            float p_x = vertex_x[tp_vertex];
            float p_y = vertex_y[tp_vertex];
            float p_z = vertex_z[tp_vertex];

            float m_x = vertex_x[tm_vertex];
            float m_y = vertex_y[tm_vertex];
            float m_z = vertex_z[tm_vertex];

            float n_x = vertex_x[tn_vertex];
            float n_y = vertex_y[tn_vertex];
            float n_z = vertex_z[tn_vertex];

            float a_x = vertex_x[v_a];
            float a_y = vertex_y[v_a];
            float a_z = vertex_z[v_a];

            float b_x = vertex_x[v_b];
            float b_y = vertex_y[v_b];
            float b_z = vertex_z[v_b];

            float c_x = vertex_x[v_c];
            float c_y = vertex_y[v_c];
            float c_z = vertex_z[v_c];

            struct UVFaceCoords uv_pnm;
            uv_pnm_compute(
                &uv_pnm,
                p_x,
                p_y,
                p_z,
                m_x,
                m_y,
                m_z,
                n_x,
                n_y,
                n_z,
                a_x,
                a_y,
                a_z,
                b_x,
                b_y,
                b_z,
                c_x,
                c_y,
                c_z);

            // Get atlas slot for this texture
            texture_id = face_texture_ids[face];

            if( texture_id != -1 )
            {
                auto tex_it = buffers->texture_to_atlas_slot.find(texture_id);
                if( tex_it != buffers->texture_to_atlas_slot.end() )
                {
                    atlas_slot = tex_it->second;
                }
                else
                {
                    texture_id = -1;
                }
            }

            // Calculate atlas UVs on CPU (once, not per-frame!)
            if( atlas_slot >= 0 )
            {
                float tiles_per_row = (float)buffers->atlas_tiles_per_row;
                float tile_u = (float)(atlas_slot % buffers->atlas_tiles_per_row);
                float tile_v = (float)(atlas_slot / buffers->atlas_tiles_per_row);
                float tile_size = (float)buffers->atlas_tile_size;
                float atlas_size = (float)buffers->atlas_size;

                // Transform UVs to atlas space
                buffers->texCoords.push_back((tile_u + uv_pnm.u1) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v1) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_u + uv_pnm.u2) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v2) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_u + uv_pnm.u3) * tile_size / atlas_size);
                buffers->texCoords.push_back((tile_v + uv_pnm.v3) * tile_size / atlas_size);
            }
            else
            {
                // No valid atlas slot, store dummy UVs
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
                buffers->texCoords.push_back(0.0f);
            }

            // Store texture ID (atlas slot) for each vertex
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);

            // Store texture opaque flag for each vertex
            float is_opaque = 1.0f; // Default to opaque
            if( texture_id != -1 )
            {
                auto opaque_it = buffers->texture_to_opaque.find(texture_id);
                if( opaque_it != buffers->texture_to_opaque.end() )
                {
                    is_opaque = opaque_it->second ? 1.0f : 0.0f;
                }
            }
            buffers->textureOpaques.push_back(is_opaque);
            buffers->textureOpaques.push_back(is_opaque);
            buffers->textureOpaques.push_back(is_opaque);

            // Store texture animation speed for each vertex
            float anim_speed = 0.0f; // Default to no animation
            if( texture_id != -1 )
            {
                auto speed_it = buffers->texture_to_animation_speed.find(texture_id);
                if( speed_it != buffers->texture_to_animation_speed.end() )
                {
                    anim_speed = speed_it->second;
                }
            }
            buffers->textureAnimSpeeds.push_back(anim_speed);
            buffers->textureAnimSpeeds.push_back(anim_speed);
            buffers->textureAnimSpeeds.push_back(anim_speed);

            texture_id = (atlas_slot >= 0) ? texture_id : -1;
        }
        else
        {
            // Untextured face
            texture_id = -1;
            atlas_slot = -1;

            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);
            buffers->texCoords.push_back(0.0f);

            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);
            buffers->textureIds.push_back(atlas_slot);

            // Untextured faces: opaque flag doesn't matter, use 1.0
            buffers->textureOpaques.push_back(1.0f);
            buffers->textureOpaques.push_back(1.0f);
            buffers->textureOpaques.push_back(1.0f);

            // Untextured faces: no animation
            buffers->textureAnimSpeeds.push_back(0.0f);
            buffers->textureAnimSpeeds.push_back(0.0f);
            buffers->textureAnimSpeeds.push_back(0.0f);
        }

        Pix3DGLCoreDrawBatch& batch = buffers->texture_batches[texture_id];
        batch.texture_id = texture_id;

        // Merge contiguous faces: if the last entry in this batch is immediately before this face,
        // just increase the count instead of adding a new entry
        if( !batch.face_starts.empty() &&
            batch.face_starts.back() + batch.face_counts.back() == face_start_index )
        {
            // Extend the last batch entry
            batch.face_counts.back() += 3;
        }
        else
        {
            // Start a new batch entry
            batch.face_starts.push_back(face_start_index);
            batch.face_counts.push_back(3);
        }

        current_vertex_index += 3;
    }

    buffers->total_vertex_count = current_vertex_index;
}

#endif