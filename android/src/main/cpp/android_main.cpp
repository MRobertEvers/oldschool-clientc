#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include <jni.h>
#include <memory>
#include <string>

extern "C" {
#include "bmp.h"
#include "graphics/render.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/scene.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/world.h"
#include "osrs/xtea_config.h"
#include "screen.h"
#include "shared_tables.h"
}

#include <SDL_main.h>

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "android_platform.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

//#define LOG_TAG "SceneTileTest"
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
//#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" int g_sin_table[2048];
extern "C" int g_cos_table[2048];
extern "C" int g_tan_table[2048];
extern "C" int g_hsl16_to_rgb_table[65536];

// Global variables
static AndroidPlatform* g_platform = nullptr;
static bool g_initialized = false;

// Forward declarations
extern "C" void android_main_init();
extern "C" void android_main_cleanup();
extern "C" void android_main_pause();
extern "C" void android_main_resume();

// SDL_main function declaration
extern "C" int SDL_main(int argc, char* argv[]);

extern "C" JNIEXPORT void JNICALL
Java_com_scenetile_test_MainActivity_nativeInit(JNIEnv* env, jobject thiz)
{
    LOGI("Initializing native code");

    try
    {
        android_main_init();
        g_initialized = true;
        LOGI("Native initialization completed successfully %d", g_initialized);
    }
    catch( const std::exception& e )
    {
        LOGE("Native initialization failed: %s", e.what());
        g_initialized = false;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_scenetile_test_MainActivity_nativeCleanup(JNIEnv* env, jobject thiz)
{
    LOGI("Cleaning up native code");

    if( g_initialized )
    {
        android_main_cleanup();
        g_initialized = false;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_scenetile_test_MainActivity_nativePause(JNIEnv* env, jobject thiz)
{
    LOGI("Pausing native code");

    if( g_initialized )
    {
        android_main_pause();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_scenetile_test_MainActivity_nativeResume(JNIEnv* env, jobject thiz)
{
    LOGI("Resuming native code");

    if( g_initialized )
    {
        android_main_resume();
    }
}

class WowZa
{
public:
    WowZa() { LOGI("WowZa constructor"); }
    ~WowZa() { LOGI("WowZa destructor"); }
};

extern "C" void
android_main_init()
{
    LOGI("Starting android_main_init");

    // Initialize math tables
    // init_hsl16_to_rgb_table();
    // init_sin_table();
    // init_cos_table();
    // init_tan_table();
    // init_reciprocal16();

    LOGI("Initializing platform g_platform = new AndroidPlatform();");

    WowZa* wowza = new WowZa();
    LOGI("WowZa object %p", wowza);
    // Initialize platform
    g_platform = new AndroidPlatform();
    LOGI("AndroidPlatform object %p", g_platform);
    if( !g_platform->init() )
    {
        LOGE("Failed to initialize Android platform");
    }

    LOGI("android_main_init completed successfully");
}

extern "C" void
android_main_cleanup()
{
    LOGI("Starting android_main_cleanup");

    if( g_platform )
    {
        g_platform->cleanup();
        delete g_platform;
        g_platform = nullptr;
    }

    LOGI("android_main_cleanup completed");
}

extern "C" void
android_main_pause()
{
    LOGI("android_main_pause");

    if( g_platform )
    {
        g_platform->pause();
    }
}

extern "C" void
android_main_resume()
{
    LOGI("android_main_resume");

    if( g_platform )
    {
        g_platform->resume();
    }
}

// SDL_main function implementation
extern "C" int
SDL_main(int argc, char* argv[])
{
    LOGI("SDL_main called with %d arguments", argc);

    // Initialize the platform if not already done
    if( !g_initialized )
    {
        LOGI("SDL_main initializing platform");
        try
        {
            android_main_init();
            g_initialized = true;
            LOGI("SDL_main initialization completed successfully");
        }
        catch( const std::exception& e )
        {
            LOGE("SDL_main initialization failed: %s", e.what());
            return -1;
        }
    }

    // Run the main loop
    if( g_platform )
    {
        LOGI("g_platform->runMainLoop - g_platform: %p", g_platform);
        try
        {
            // Test if we can call a simple method first
            LOGI("About to call runMainLoop");
            g_platform->runMainLoop();
            LOGI("runMainLoop returned");
        }
        catch( const std::exception& e )
        {
            LOGE("runMainLoop failed: %s", e.what());
            return -1;
        }
        catch( ... )
        {
            LOGE("runMainLoop failed with unknown exception");
            return -1;
        }
    }
    else
    {
        LOGE("SDL_main no platform");
        return -1;
    }

    LOGI("SDL_main exiting");
    return 0;
}