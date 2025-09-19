# Scene Tile Test - Android Build

This directory contains a minimal Android build setup for the scene_tile_test.cpp application using SDL2.

## Prerequisites

- Android Studio or Android SDK
- Android NDK (Native Development Kit)
- Java Development Kit (JDK) 8 or higher
- CMake 3.18.1 or higher
- SDL2 for Android

## Setup

### Quick Setup (Recommended)

For automatic setup, run one of these scripts:

**Windows:**
```cmd
cd android
quick_setup.bat
```

**Linux/macOS:**
```bash
cd android
chmod +x quick_setup.sh
./quick_setup.sh
```

This will automatically:
- Download and setup SDL2 for Android
- Copy cache files from the main project
- Create all necessary directories
- Verify the setup

### Manual Setup

If you prefer to set up manually or the automatic setup fails:

#### 1. Install SDL2 for Android

**Option A: Automatic Download**
```cmd
# Windows
download_sdl2.bat

# Linux/macOS
chmod +x download_sdl2.sh
./download_sdl2.sh
```

**Option B: Manual Download**
Download SDL2 for Android from the official website:
https://www.libsdl.org/download-2.0.php

Extract the SDL2 Android package and copy the following files to your Android project:
- `SDL2/include/` → `android/src/main/cpp/SDL2/include/`
- `SDL2/libs/` → `android/src/main/cpp/SDL2/libs/`

#### 2. Copy Cache Files

Copy the required cache files from the main project to the Android assets:
```bash
mkdir -p android/src/main/assets/cache
cp -r ../cache/* android/src/main/assets/cache/
```

#### 3. Build the Project

#### Using Android Studio:
1. Open Android Studio
2. Open the `android` directory as a project
3. Sync the project with Gradle files
4. Build and run on a device or emulator

#### Using Command Line:
```bash
cd android
./gradlew assembleDebug
```

## Available Scripts

The Android project includes several setup scripts:

| Script | Purpose | Platform |
|--------|---------|----------|
| `quick_setup.bat/sh` | Complete automatic setup | Windows/Linux/macOS |
| `setup_android.bat/sh` | Interactive setup with manual options | Windows/Linux/macOS |
| `download_sdl2.bat/sh` | Download SDL2 automatically | Windows/Linux/macOS |
| `build_android.bat/sh` | Build the Android project | Windows/Linux/macOS |

### Script Usage

**Quick Setup (Recommended):**
```bash
# Windows
quick_setup.bat

# Linux/macOS
chmod +x quick_setup.sh
./quick_setup.sh
```

**Interactive Setup:**
```bash
# Windows
setup_android.bat

# Linux/macOS
chmod +x setup_android.sh
./setup_android.sh
```

**Build Only:**
```bash
# Windows
build_android.bat

# Linux/macOS
chmod +x build_android.sh
./build_android.sh
```

## Project Structure

```
android/
├── CMakeLists.txt              # CMake configuration for native code
├── build.gradle                # Android build configuration
├── settings.gradle             # Gradle settings
├── gradle.properties           # Gradle properties
├── quick_setup.bat/sh          # Complete automatic setup script
├── setup_android.bat/sh        # Interactive setup script
├── download_sdl2.bat/sh        # SDL2 download script
├── build_android.bat/sh        # Build script
├── src/main/
│   ├── AndroidManifest.xml     # Android manifest
│   ├── java/com/scenetile/test/
│   │   └── MainActivity.java   # Main Android activity
│   ├── cpp/
│   │   ├── android_main.cpp    # JNI entry points
│   │   ├── android_platform.h  # Android platform wrapper header
│   │   └── android_platform.cpp # Android platform wrapper implementation
│   └── res/                    # Android resources
└── gradle/wrapper/             # Gradle wrapper files
```

## Key Features

- **Minimal Setup**: Only includes the essential files needed to run scene_tile_test.cpp on Android
- **SDL2 Integration**: Uses SDL2 for cross-platform graphics and input handling
- **JNI Wrapper**: Provides a clean interface between Java and native C++ code
- **Asset Management**: Handles loading cache files from Android assets
- **Platform Abstraction**: Wraps platform-specific functionality for Android

## Configuration

### Camera Settings
The default camera position is set to the "Lumbridge Kitchen" scene from the original application:
- Position: (390, -1340, 1916)
- Rotation: Pitch=290, Yaw=1538, Roll=0
- FOV: 512

### Cache Path
Cache files are loaded from: `/data/data/com.scenetile.test/files/cache`

## Troubleshooting

### Build Issues
- Ensure SDL2 is properly installed and linked
- Check that all cache files are present in the assets directory
- Verify NDK and CMake versions are compatible

### Runtime Issues
- Check device logs using `adb logcat` for error messages
- Ensure the device has sufficient storage for cache files
- Verify OpenGL ES 2.0 support on the target device

## Limitations

This is a minimal implementation and may not include all features from the original desktop version:
- Some advanced rendering features may be simplified
- Input handling is basic (touch events mapped to mouse events)
- Performance may vary depending on device capabilities

## Development Notes

- The Android platform wrapper (`AndroidPlatform`) handles initialization and cleanup
- JNI functions are defined in `android_main.cpp`
- SDL2 is used for window management, rendering, and input handling
- Cache files are copied from assets to internal storage on first run

## Future Improvements

- Add proper touch input handling
- Implement Android-specific optimizations
- Add configuration options for different devices
- Improve error handling and logging
- Add support for different screen orientations
