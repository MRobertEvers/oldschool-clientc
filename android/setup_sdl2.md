# SDL2 Setup for Android

This document explains how to set up SDL2 for the Android build of Scene Tile Test.

## Download SDL2 for Android

1. Go to the official SDL2 website: https://www.libsdl.org/download-2.0.php
2. Download the "SDL2-devel-2.x.x-mingw.tar.gz" or "SDL2-devel-2.x.x-VC.zip" package
3. Extract the downloaded archive

## Required SDL2 Files

You need to copy the following files from the SDL2 package to your Android project:

### Include Files
Copy from SDL2 package: `SDL2/include/`
Copy to Android project: `android/src/main/cpp/SDL2/include/`

### Library Files
Copy from SDL2 package: `SDL2/libs/android/`
Copy to Android project: `android/src/main/cpp/SDL2/libs/`

## Directory Structure After Setup

```
android/src/main/cpp/
├── SDL2/
│   ├── include/
│   │   ├── SDL.h
│   │   ├── SDL_audio.h
│   │   ├── SDL_events.h
│   │   ├── SDL_keyboard.h
│   │   ├── SDL_mouse.h
│   │   ├── SDL_render.h
│   │   ├── SDL_surface.h
│   │   ├── SDL_system.h
│   │   ├── SDL_thread.h
│   │   ├── SDL_timer.h
│   │   ├── SDL_version.h
│   │   └── SDL_video.h
│   └── libs/
│       ├── android/
│       │   ├── arm64-v8a/
│       │   ├── armeabi-v7a/
│       │   ├── x86/
│       │   └── x86_64/
│       └── ...
├── android_main.cpp
├── android_platform.cpp
└── android_platform.h
```

## Alternative: Using SDL2 from Source

If you prefer to build SDL2 from source:

1. Clone the SDL2 repository:
   ```bash
   git clone https://github.com/libsdl-org/SDL.git
   ```

2. Build SDL2 for Android:
   ```bash
   cd SDL
   mkdir build-android
   cd build-android
   cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-21 \
            -DCMAKE_BUILD_TYPE=Release
   make -j4
   ```

3. Copy the built libraries and headers to your Android project.

## CMake Configuration

The CMakeLists.txt file is already configured to find SDL2. Make sure the paths are correct:

```cmake
# Find SDL2
find_package(SDL2 REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/imgui
    ${CMAKE_SOURCE_DIR}/android/src/main/cpp
    ${SDL2_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(scene_tile_test_android
    ${SDL2_LIBRARIES}
    android
    log
    GLESv2
    EGL
)
```

## Troubleshooting

### SDL2 Not Found
If CMake can't find SDL2:
1. Verify the SDL2 files are in the correct location
2. Check that the CMakeLists.txt paths are correct
3. Ensure SDL2Config.cmake is present in the SDL2 directory

### Link Errors
If you get link errors:
1. Verify all required SDL2 libraries are present
2. Check that the library architecture matches your target (arm64-v8a, armeabi-v7a, etc.)
3. Ensure the libraries are built for the correct Android API level

### Runtime Errors
If the app crashes on startup:
1. Check device logs: `adb logcat | grep SceneTileTest`
2. Verify SDL2 libraries are included in the APK
3. Ensure the device supports OpenGL ES 2.0

## Testing SDL2 Installation

You can test if SDL2 is properly set up by building a simple SDL2 test:

```cpp
#include <SDL.h>
#include <android/log.h>

#define LOG_TAG "SDL2Test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jint JNICALL
Java_com_scenetile_test_MainActivity_testSDL2(JNIEnv *env, jobject thiz) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOGI("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }
    
    LOGI("SDL2 initialized successfully!");
    SDL_Quit();
    return 0;
}
```

Add this method to your MainActivity.java and call it to verify SDL2 is working.
