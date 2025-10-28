/**
 * Practical Example: Painter's Algorithm with Dynamic Index Buffers
 * 
 * This example shows how to implement CPU-based face sorting for painter's
 * algorithm rendering using the new dynamic EBO system.
 */

#include "osrs/pix3dgl.h"
#include <math.h>
#include <stdlib.h>

// Structure to hold face sorting data
typedef struct {
    int face_idx;
    float depth;
} FaceDepth;

// Comparison function for qsort (back to front)
int compare_face_depths(const void* a, const void* b) {
    FaceDepth* fa = (FaceDepth*)a;
    FaceDepth* fb = (FaceDepth*)b;
    // Sort back to front (larger depth first)
    if (fa->depth > fb->depth) return -1;
    if (fa->depth < fb->depth) return 1;
    return 0;
}

// Compute face depths from camera position
void compute_model_face_depths(
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    float cam_x,
    float cam_y,
    float cam_z,
    float position_x,
    float position_y,
    float position_z,
    float yaw,
    FaceDepth* face_depths)
{
    float cos_yaw = cosf(yaw);
    float sin_yaw = sinf(yaw);
    
    for (int i = 0; i < face_count; i++) {
        // Get face vertices
        int v_a = face_indices_a[i];
        int v_b = face_indices_b[i];
        int v_c = face_indices_c[i];
        
        // Transform vertices to world space
        float wx_a = position_x + vertices_x[v_a] * cos_yaw - vertices_z[v_a] * sin_yaw;
        float wy_a = position_y + vertices_y[v_a];
        float wz_a = position_z + vertices_x[v_a] * sin_yaw + vertices_z[v_a] * cos_yaw;
        
        float wx_b = position_x + vertices_x[v_b] * cos_yaw - vertices_z[v_b] * sin_yaw;
        float wy_b = position_y + vertices_y[v_b];
        float wz_b = position_z + vertices_x[v_b] * sin_yaw + vertices_z[v_b] * cos_yaw;
        
        float wx_c = position_x + vertices_x[v_c] * cos_yaw - vertices_z[v_c] * sin_yaw;
        float wy_c = position_y + vertices_y[v_c];
        float wz_c = position_z + vertices_x[v_c] * sin_yaw + vertices_z[v_c] * cos_yaw;
        
        // Compute face center
        float center_x = (wx_a + wx_b + wx_c) / 3.0f;
        float center_y = (wy_a + wy_b + wy_c) / 3.0f;
        float center_z = (wz_a + wz_b + wz_c) / 3.0f;
        
        // Compute distance from camera
        float dx = center_x - cam_x;
        float dy = center_y - cam_y;
        float dz = center_z - cam_z;
        float depth = sqrtf(dx*dx + dy*dy + dz*dz);
        
        face_depths[i].face_idx = i;
        face_depths[i].depth = depth;
    }
}

// Example: Setup scene with painter's algorithm
void setup_scene_with_painter_algorithm(
    struct Pix3DGL* pix3dgl,
    struct SceneData* scene_data)  // Your scene data structure
{
    // 1. Begin building static scene
    pix3dgl_scene_static_begin(pix3dgl);
    
    // 2. Add all models to the scene
    for (int i = 0; i < scene_data->num_models; i++) {
        struct Model* model = &scene_data->models[i];
        
        pix3dgl_scene_static_add_model_raw(
            pix3dgl,
            model->scene_model_idx,      // Unique identifier for this model instance
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            model->face_count,
            model->face_textures,
            model->face_texture_coords,
            model->textured_p_coordinate,
            model->textured_m_coordinate,
            model->textured_n_coordinate,
            model->face_colors_hsl_a,
            model->face_colors_hsl_b,
            model->face_colors_hsl_c,
            model->face_infos,
            model->position_x,
            model->position_y,
            model->position_z,
            model->yaw_radians
        );
    }
    
    // 3. Finalize scene - creates dynamic EBO and uploads to GPU
    pix3dgl_scene_static_end(pix3dgl);
    
    printf("Scene setup complete with %d models\n", scene_data->num_models);
}

// Example: Render frame with dynamic face ordering
void render_frame_with_painter_algorithm(
    struct Pix3DGL* pix3dgl,
    struct SceneData* scene_data,
    struct Camera* camera)
{
    // 1. Begin frame with camera setup
    pix3dgl_begin_frame(
        pix3dgl,
        camera->x,
        camera->y,
        camera->z,
        camera->pitch,
        camera->yaw,
        camera->screen_width,
        camera->screen_height
    );
    
    // 2. Sort faces for each model (CPU-side)
    for (int i = 0; i < scene_data->num_models; i++) {
        struct Model* model = &scene_data->models[i];
        
        // Allocate face depth array (or reuse static buffer)
        FaceDepth* face_depths = (FaceDepth*)malloc(model->face_count * sizeof(FaceDepth));
        
        // Compute face depths from camera
        compute_model_face_depths(
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            model->face_count,
            camera->x,
            camera->y,
            camera->z,
            model->position_x,
            model->position_y,
            model->position_z,
            model->yaw_radians,
            face_depths
        );
        
        // Sort faces back-to-front
        qsort(face_depths, model->face_count, sizeof(FaceDepth), compare_face_depths);
        
        // Extract sorted face indices
        int* sorted_indices = (int*)malloc(model->face_count * sizeof(int));
        for (int j = 0; j < model->face_count; j++) {
            sorted_indices[j] = face_depths[j].face_idx;
        }
        
        // Upload sorted face order to renderer
        pix3dgl_scene_static_set_model_face_order(
            pix3dgl,
            model->scene_model_idx,
            sorted_indices,
            model->face_count
        );
        
        free(face_depths);
        free(sorted_indices);
    }
    
    // 3. Set model draw order (optional - for model-level sorting)
    // For now, draw in scene order
    int* scene_model_indices = (int*)malloc(scene_data->num_models * sizeof(int));
    for (int i = 0; i < scene_data->num_models; i++) {
        scene_model_indices[i] = scene_data->models[i].scene_model_idx;
    }
    
    pix3dgl_scene_static_set_draw_order(
        pix3dgl,
        scene_model_indices,
        scene_data->num_models
    );
    
    free(scene_model_indices);
    
    // 4. Draw scene (single glDrawElements call!)
    pix3dgl_scene_static_draw(pix3dgl);
    
    // 5. End frame
    pix3dgl_end_frame(pix3dgl);
}

// OPTIMIZED VERSION: Reuse buffers to avoid allocations
typedef struct {
    FaceDepth* face_depths_buffer;
    int* sorted_indices_buffer;
    int max_face_count;
} SortingBuffers;

SortingBuffers* create_sorting_buffers(int max_face_count) {
    SortingBuffers* buffers = (SortingBuffers*)malloc(sizeof(SortingBuffers));
    buffers->face_depths_buffer = (FaceDepth*)malloc(max_face_count * sizeof(FaceDepth));
    buffers->sorted_indices_buffer = (int*)malloc(max_face_count * sizeof(int));
    buffers->max_face_count = max_face_count;
    return buffers;
}

void free_sorting_buffers(SortingBuffers* buffers) {
    free(buffers->face_depths_buffer);
    free(buffers->sorted_indices_buffer);
    free(buffers);
}

void render_frame_optimized(
    struct Pix3DGL* pix3dgl,
    struct SceneData* scene_data,
    struct Camera* camera,
    SortingBuffers* buffers)  // Pre-allocated buffers
{
    pix3dgl_begin_frame(
        pix3dgl,
        camera->x, camera->y, camera->z,
        camera->pitch, camera->yaw,
        camera->screen_width, camera->screen_height
    );
    
    for (int i = 0; i < scene_data->num_models; i++) {
        struct Model* model = &scene_data->models[i];
        
        // Reuse pre-allocated buffers (no malloc!)
        compute_model_face_depths(
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            model->face_count,
            camera->x, camera->y, camera->z,
            model->position_x, model->position_y, model->position_z,
            model->yaw_radians,
            buffers->face_depths_buffer  // Reuse!
        );
        
        qsort(buffers->face_depths_buffer, model->face_count, 
              sizeof(FaceDepth), compare_face_depths);
        
        for (int j = 0; j < model->face_count; j++) {
            buffers->sorted_indices_buffer[j] = buffers->face_depths_buffer[j].face_idx;
        }
        
        pix3dgl_scene_static_set_model_face_order(
            pix3dgl,
            model->scene_model_idx,
            buffers->sorted_indices_buffer,
            model->face_count
        );
    }
    
    // Set draw order
    int* scene_model_indices = (int*)malloc(scene_data->num_models * sizeof(int));
    for (int i = 0; i < scene_data->num_models; i++) {
        scene_model_indices[i] = scene_data->models[i].scene_model_idx;
    }
    pix3dgl_scene_static_set_draw_order(pix3dgl, scene_model_indices, scene_data->num_models);
    free(scene_model_indices);
    
    // Draw! (Single GPU call)
    pix3dgl_scene_static_draw(pix3dgl);
    
    pix3dgl_end_frame(pix3dgl);
}

// ADVANCED: Only sort when camera moves significantly
typedef struct {
    float last_cam_x, last_cam_y, last_cam_z;
    float last_cam_yaw, last_cam_pitch;
    int frames_since_sort;
} CameraTracker;

int camera_moved_significantly(
    struct Camera* camera,
    CameraTracker* tracker,
    float distance_threshold,
    float angle_threshold)
{
    float dx = camera->x - tracker->last_cam_x;
    float dy = camera->y - tracker->last_cam_y;
    float dz = camera->z - tracker->last_cam_z;
    float dist_moved = sqrtf(dx*dx + dy*dy + dz*dz);
    
    float angle_diff = fabsf(camera->yaw - tracker->last_cam_yaw) +
                       fabsf(camera->pitch - tracker->last_cam_pitch);
    
    return (dist_moved > distance_threshold || angle_diff > angle_threshold);
}

void render_frame_adaptive(
    struct Pix3DGL* pix3dgl,
    struct SceneData* scene_data,
    struct Camera* camera,
    SortingBuffers* buffers,
    CameraTracker* tracker)
{
    pix3dgl_begin_frame(
        pix3dgl,
        camera->x, camera->y, camera->z,
        camera->pitch, camera->yaw,
        camera->screen_width, camera->screen_height
    );
    
    // Only resort if camera moved significantly
    if (camera_moved_significantly(camera, tracker, 50.0f, 0.1f)) {
        printf("Camera moved - resorting faces\n");
        
        for (int i = 0; i < scene_data->num_models; i++) {
            struct Model* model = &scene_data->models[i];
            
            compute_model_face_depths(
                model->vertices_x, model->vertices_y, model->vertices_z,
                model->face_indices_a, model->face_indices_b, model->face_indices_c,
                model->face_count,
                camera->x, camera->y, camera->z,
                model->position_x, model->position_y, model->position_z,
                model->yaw_radians,
                buffers->face_depths_buffer
            );
            
            qsort(buffers->face_depths_buffer, model->face_count,
                  sizeof(FaceDepth), compare_face_depths);
            
            for (int j = 0; j < model->face_count; j++) {
                buffers->sorted_indices_buffer[j] = buffers->face_depths_buffer[j].face_idx;
            }
            
            pix3dgl_scene_static_set_model_face_order(
                pix3dgl,
                model->scene_model_idx,
                buffers->sorted_indices_buffer,
                model->face_count
            );
        }
        
        // Update tracker
        tracker->last_cam_x = camera->x;
        tracker->last_cam_y = camera->y;
        tracker->last_cam_z = camera->z;
        tracker->last_cam_yaw = camera->yaw;
        tracker->last_cam_pitch = camera->pitch;
        tracker->frames_since_sort = 0;
    } else {
        tracker->frames_since_sort++;
    }
    
    // Set draw order
    int* scene_model_indices = (int*)malloc(scene_data->num_models * sizeof(int));
    for (int i = 0; i < scene_data->num_models; i++) {
        scene_model_indices[i] = scene_data->models[i].scene_model_idx;
    }
    pix3dgl_scene_static_set_draw_order(pix3dgl, scene_model_indices, scene_data->num_models);
    free(scene_model_indices);
    
    // Draw (uses cached indices if camera didn't move)
    pix3dgl_scene_static_draw(pix3dgl);
    
    pix3dgl_end_frame(pix3dgl);
}

// Main example usage
void example_main() {
    // Initialize renderer
    struct Pix3DGL* pix3dgl = pix3dgl_new();
    
    // Load your scene data
    struct SceneData* scene_data = load_scene_data();
    
    // Setup scene (once)
    setup_scene_with_painter_algorithm(pix3dgl, scene_data);
    
    // Create reusable sorting buffers (once)
    int max_faces = 1000;  // Adjust to your largest model
    SortingBuffers* buffers = create_sorting_buffers(max_faces);
    
    // Create camera tracker
    CameraTracker tracker = {0};
    
    // Game loop
    struct Camera camera = {0};
    while (game_running()) {
        update_camera(&camera);
        
        // Render with adaptive sorting
        render_frame_adaptive(pix3dgl, scene_data, &camera, buffers, &tracker);
        
        swap_buffers();
    }
    
    // Cleanup
    free_sorting_buffers(buffers);
    pix3dgl_cleanup(pix3dgl);
}

