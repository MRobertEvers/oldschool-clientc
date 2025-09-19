#ifndef ANDROID_PLATFORM_H
#define ANDROID_PLATFORM_H

#include <SDL.h>
#include <memory>
#include <string>


#define LOG_TAG "SceneTileTest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


// Game struct definition (from scene_tile_test.cpp)
struct Game
{
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_x;
    int camera_y;
    int camera_z;

    int hover_model;
    int hover_loc_x;
    int hover_loc_y;
    int hover_loc_level;
    int hover_loc_yaw;

    int mouse_x;
    int mouse_y;

    int mouse_click_x;
    int mouse_click_y;
    int mouse_click_cycle;

    int player_tile_x;
    int player_tile_y;
    int player_yaw;
    int player_animation_id;
    struct SceneModel* player_model;

    struct FrustrumCullmap* frustrum_cullmap;
    int fake_pitch;

    uint64_t start_time;
    uint64_t end_time;

    uint64_t frame_count;
    uint64_t frame_time_sum;

    struct SceneTile* tiles;
    int tile_count;

    struct SceneTextures* scene_textures;

    struct CacheTexture* textures;
    int* texture_ids;
    int texture_count;

    struct CacheSpritePack* sprite_packs;
    int* sprite_ids;
    int sprite_count;

    struct CacheMapLocs* map_locs;

    struct SceneLocs* scene_locs;
    struct SceneTextures* loc_textures;

    int** image_cross_pixels;
    // THese are of known size.

    struct Scene* scene;
    struct World* world;

    struct SceneOp* ops;
    int op_count;

    int max_render_ops;
    int manual_render_ops;

    int show_loc_enabled;
    int show_loc_x;
    int show_loc_y;

    struct TexturesCache* textures_cache;

    int show_debug_tiles;
    bool show_debug_glow;
    int debug_glow_r;
    int debug_glow_g;
    int debug_glow_b;
    int debug_glow_intensity;
    int debug_glow_radius;

    int camera_speed; // Camera movement speed
};

// PlatformSDL2 struct definition (from scene_tile_test.cpp)
struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;
};


class AndroidPlatform
{
public:
    AndroidPlatform();
    ~AndroidPlatform();

    bool init();
    void cleanup();
    void pause();
    void resume();

    void runMainLoop();

private:
    bool m_initialized;
    bool m_running;
    bool m_paused;

    // SDL2 platform wrapper
    PlatformSDL2* m_sdl_platform;
    Game* m_game;

    // Cache and scene data
    struct Cache* m_cache;
    struct Scene* m_scene;
    struct TexturesCache* m_textures_cache;

    // Asset management
    std::string m_cache_path;

    // Initialization helpers
    bool initCache();
    bool initScene();
    bool initGame();
    void cleanupGame();

    // Main loop
    void processEvents();
    void update(float delta_time);
    void render();

    // Additional initialization functions from scene_tile_test.cpp
    void initTables();
    bool loadSpritePacks();
};

#endif // ANDROID_PLATFORM_H
