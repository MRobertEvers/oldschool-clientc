#ifndef ANDROID_PLATFORM_H
#define ANDROID_PLATFORM_H
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include <SDL.h>
#include <jni.h>
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

    struct FrustrumCullmap* frustrum_cullmap;
    int fake_pitch;

    uint64_t start_time;
    uint64_t end_time;
    uint64_t start_painters_time;
    uint64_t end_painters_time;

    uint64_t frame_count;
    uint64_t frame_time_sum;
    uint64_t painters_time_sum;

    struct SceneTile* tiles;

    struct SceneTextures* scene_textures;

    struct CacheTexture* textures;

    struct CacheSpritePack* sprite_packs;

    struct CacheMapLocs* map_locs;

    struct SceneLocs* scene_locs;

    int* sprite_ids;
    int sprite_count;

    int** image_cross_pixels;
    // THese are of known size.

    struct Scene* scene;
    struct World* world;

    struct SceneOp* ops;
    int op_count;
    int op_capacity;

    int max_render_ops;
    int manual_render_ops;

    struct TexturesCache* textures_cache;

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
    struct PlatformSDL2* m_sdl_platform;
    struct Game* m_game;

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
