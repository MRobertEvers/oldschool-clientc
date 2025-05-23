#include "load_separate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Model
load_separate(const char* filename)
{
    struct Model model = { 0 };

    // append colors, faces, priorities and vertices to the model name
    // and load them each as a separate file

    // Single buffer for all filenames
    char name_buffer[256];
    FILE* file = NULL;

    // Load vertices file first
    snprintf(name_buffer, sizeof(name_buffer), "%s.vertices", filename);

    file = fopen(name_buffer, "rb");
    if( !file )
        return model;

    // Read vertex count and allocate vertex arrays
    fread(&model.vertex_count, sizeof(int), 1, file);

    // Malloc vertex arrays
    model.vertices_x = (int*)malloc(model.vertex_count * sizeof(int));
    model.vertices_y = (int*)malloc(model.vertex_count * sizeof(int));
    model.vertices_z = (int*)malloc(model.vertex_count * sizeof(int));
    model.vertex_bone_map = (int*)malloc(model.vertex_count * sizeof(int));
    memset(model.vertex_bone_map, 0, model.vertex_count * sizeof(int));

    if( !model.vertices_x || !model.vertices_y || !model.vertices_z )
    {
        fclose(file);
        goto cleanup_colors;
    }

    // Read vertices
    fread(model.vertices_x, sizeof(int), model.vertex_count, file);
    fread(model.vertices_y, sizeof(int), model.vertex_count, file);
    fread(model.vertices_z, sizeof(int), model.vertex_count, file);
    fread(model.vertex_bone_map, sizeof(int), model.vertex_count, file);
    fclose(file);

    // Load faces file
    snprintf(name_buffer, sizeof(name_buffer), "%s.faces", filename);

    file = fopen(name_buffer, "rb");
    if( !file )
        goto cleanup_vertices;

    // Read face count and allocate face arrays
    fread(&model.face_count, sizeof(int), 1, file);

    // Malloc face arrays
    model.face_indices_a = (int*)malloc(model.face_count * sizeof(int));
    model.face_indices_b = (int*)malloc(model.face_count * sizeof(int));
    model.face_indices_c = (int*)malloc(model.face_count * sizeof(int));

    if( !model.face_indices_a || !model.face_indices_b || !model.face_indices_c )
    {
        fclose(file);
        goto cleanup_colors;
    }

    // Read faces
    fread(model.face_indices_a, sizeof(int), model.face_count, file);
    fread(model.face_indices_b, sizeof(int), model.face_count, file);
    fread(model.face_indices_c, sizeof(int), model.face_count, file);

    fclose(file);

    // Load colors file
    snprintf(name_buffer, sizeof(name_buffer), "%s.colors", filename);

    file = fopen(name_buffer, "rb");
    if( !file )
        goto cleanup_faces;

    fread(&model.face_count, sizeof(int), 1, file);

    // Malloc and read colors
    model.face_colors = (int*)malloc(model.face_count * sizeof(int));
    if( !model.face_colors )
    {
        fclose(file);
        goto cleanup_faces;
    }

    fread(model.face_colors, sizeof(int), model.face_count, file);
    fclose(file);

    // Load priorities file
    snprintf(name_buffer, sizeof(name_buffer), "%s.priorities", filename);

    file = fopen(name_buffer, "rb");
    if( !file )
        goto cleanup_colors;

    fread(&model.face_count, sizeof(int), 1, file);

    // Malloc and read priorities
    model.face_priorities = (int*)malloc(model.face_count * sizeof(int));
    if( !model.face_priorities )
    {
        fclose(file);
        goto cleanup_colors;
    }

    fread(model.face_priorities, sizeof(int), model.face_count, file);
    fclose(file);

    return model;

cleanup_colors:
    free(model.face_colors);
cleanup_faces:
    free(model.face_indices_a);
    free(model.face_indices_b);
    free(model.face_indices_c);
cleanup_vertices:
    free(model.vertices_x);
    free(model.vertices_y);
    free(model.vertices_z);

    memset(&model, 0, sizeof(struct Model));

    return model;
}
