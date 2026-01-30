# Building for Windows XP

This guide explains how to build `scene_tile_test.exe` and `model_viewer.exe` for Windows XP using MinGW-w64 i686 toolchain.

## Prerequisites

### 1. MinGW-w64 i686 Toolchain

You already have this at:
```
C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2
```

This is a 32-bit GCC 15.2.0 toolchain that can target Windows XP.

### 2. SDL2 for MinGW i686

You need SDL2 development libraries for 32-bit MinGW. There are several options:

#### Option A: Download SDL2-devel (Recommended)

1. Go to: https://github.com/libsdl-org/SDL/releases
2. Download: `SDL2-devel-2.x.x-mingw.tar.gz` (latest version)
3. Extract the archive
4. Copy contents from `SDL2-x.x.x/i686-w64-mingw32/` to your MinGW root:
   ```
   SDL2-x.x.x/i686-w64-mingw32/include/  →  mingw32/include/
   SDL2-x.x.x/i686-w64-mingw32/lib/      →  mingw32/lib/
   SDL2-x.x.x/i686-w64-mingw32/bin/      →  mingw32/bin/
   ```

#### Option B: Use Pre-built DLL

1. Download SDL2 runtime from: https://github.com/libsdl-org/SDL/releases
2. Download: `SDL2-2.x.x-win32-x86.zip`
3. Extract and place `SDL2.dll` in the project's `lib/` directory

### 3. CMake

Ensure CMake is installed and available in your PATH:
```batch
cmake --version
```

If not installed, download from: https://cmake.org/download/

## Building

### Quick Build

Simply run the provided batch script:

```batch
build_winxp.bat
```

This will:
1. Verify the MinGW toolchain
2. Configure CMake with Windows XP compatibility flags
3. Build both executables
4. Copy required DLLs to the build directory

### Manual Build

If you prefer to build manually:

```batch
set MINGW_BIN=C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2\mingw32\bin
set PATH=%MINGW_BIN%;%PATH%

cmake -B build-winxp ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -O3 -march=pentium4 -msse2" ^
    -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -O3 -march=pentium4 -msse2" ^
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++"

cmake --build build-winxp --config Release
```

## Output

After a successful build, you'll find in `build-winxp/`:

- `scene_tile_test.exe` - Main scene tile test application
- `model_viewer.exe` - Model viewer application
- `SDL2.dll` - SDL2 runtime (required)
- `libwinpthread-1.dll` - MinGW pthread runtime (required)
- `libgcc_s_dw2-1.dll` - GCC runtime (if dynamically linked)
- `libstdc++-6.dll` - C++ standard library (if dynamically linked)

## Deploying to Windows XP

1. Copy the entire `build-winxp/` directory to your Windows XP machine
2. Ensure all DLL files are in the same directory as the EXE files
3. Run `scene_tile_test.exe` or `model_viewer.exe`

### Windows XP System Requirements

- Windows XP SP3 (32-bit)
- Pentium 4 or later CPU (SSE2 support required)
- At least 512 MB RAM recommended
- DirectX 9.0c or later (for SDL2)

## Compiler Flags Explained

### Windows XP Compatibility
- `-D_WIN32_WINNT=0x0501` - Target Windows XP API level
- `-DWINVER=0x0501` - Set Windows version to XP

### Optimization Flags
- `-O3` - Maximum optimization
- `-march=pentium4` - Target Pentium 4 architecture (supports most XP-era CPUs)
- `-msse2` - Use SSE2 instructions (available on Pentium 4+)
- `-mfpmath=sse` - Use SSE for floating-point math

### Linking Flags
- `-static-libgcc` - Statically link GCC runtime (no libgcc DLL needed)
- `-static-libstdc++` - Statically link C++ standard library (no libstdc++ DLL needed)

## Troubleshooting

### "SDL2 not found" during CMake configuration

Make sure SDL2 development files are installed in your MinGW toolchain directory or use the `-DCMAKE_PREFIX_PATH` option:

```batch
cmake -B build-winxp ... -DCMAKE_PREFIX_PATH=C:\path\to\SDL2
```

### "procedure entry point not found" on Windows XP

This means a Windows API function newer than XP is being used. Verify:
1. The `-D_WIN32_WINNT=0x0501` flag is set
2. All dependencies (SDL2.dll) are also built for Windows XP

### Missing DLL errors

Ensure these DLLs are in the same directory as the EXE:
- `SDL2.dll`
- `libwinpthread-1.dll`

If you get errors about `libgcc_s_dw2-1.dll` or `libstdc++-6.dll`, either:
1. Copy those DLLs from MinGW's bin directory, or
2. Add `-static-libgcc -static-libstdc++` to linker flags

### Performance Issues

If performance is poor on older Windows XP machines:

1. Try building without SSE2:
   ```batch
   cmake ... -DCMAKE_C_FLAGS="... -march=pentium3 -mno-sse2"
   ```

2. Or use conservative optimization:
   ```batch
   cmake ... -DCMAKE_C_FLAGS="... -O2 -march=i686"
   ```

## Compiler Target Architectures

| Flag | CPU Requirement | SSE | Notes |
|------|----------------|-----|-------|
| `-march=i686` | Pentium Pro+ | No | Most compatible |
| `-march=pentium3` | Pentium III+ | SSE | Good compatibility |
| `-march=pentium4` | Pentium 4+ | SSE2 | **Default**, best performance |
| `-march=prescott` | Pentium 4 Prescott+ | SSE3 | Newer XP systems only |

## Static vs Dynamic Linking

### Dynamic Linking (Default)
**Pros:**
- Smaller EXE files
- Can update DLLs independently

**Cons:**
- Must distribute DLL files
- DLL version conflicts possible

### Static Linking
To build with fully static linking (no runtime DLLs):

```batch
cmake -B build-winxp-static ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0501 -O3 -march=pentium4 -msse2" ^
    -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"

cmake --build build-winxp-static
```

**Note:** You'll still need `SDL2.dll` as SDL2 doesn't typically provide static libraries for MinGW.

## Additional Resources

- [MinGW-w64 Documentation](https://www.mingw-w64.org/)
- [SDL2 Documentation](https://wiki.libsdl.org/)
- [CMake MinGW Support](https://cmake.org/cmake/help/latest/generator/MinGW%20Makefiles.html)
- [Windows API Versions](https://docs.microsoft.com/en-us/windows/win32/winprog/using-the-windows-headers)

## Known Limitations

1. **No AVX/AVX2**: Windows XP era CPUs don't support these instructions
2. **Limited DirectX**: XP maxes out at DirectX 9.0c
3. **Memory Limits**: 32-bit Windows XP has a 2GB per-process limit (3GB with /3GB boot flag)
4. **OpenGL**: Limited to whatever the GPU driver supports (usually up to OpenGL 3.3)

## Testing

Before deploying to a real Windows XP machine, you can test in a virtual machine:

1. Install Windows XP in VirtualBox or VMware
2. Install Visual C++ 2005-2010 Redistributables (may be needed by some components)
3. Copy the build directory to the VM
4. Run the executables

## Version History

- **Initial version**: GCC 15.2.0 MinGW-w64 i686 build script for Windows XP





