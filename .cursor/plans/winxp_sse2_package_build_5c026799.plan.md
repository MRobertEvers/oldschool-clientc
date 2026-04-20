---
name: WinXP SSE2 Package Build
overview: Configure a 32-bit WinXP CMake build with SSE2 flags, build it, and package with package_build.py. All commands are bash for the MINGW64 terminal.
todos:
  - id: verify-toolchain
    content: Verify i686 MinGW toolchain exists and set MINGW_I686 path
    status: pending
  - id: cmake-configure
    content: Run cmake configure with XP + SSE2 flags into build-winxp
    status: pending
  - id: build
    content: Build with cmake --build build-winxp
    status: pending
  - id: package
    content: Run package_build.py with --build-dir build-winxp and DLL paths
    status: pending
isProject: false
---

# WinXP SSE2 Package Build

## Current State

- `build-mingw` uses **64-bit** msys64/mingw64 toolchain (`C:/msys64/mingw64/bin/cc.exe`)
- Flags are just `-O3 -DNDEBUG` -- no SSE or XP targeting (see [flags.make](build-mingw/CMakeFiles/osx.dir/flags.make))
- No `.env` file exists yet (only [.env.template](/.env.template))
- The WinXP docs reference an i686 toolchain at `C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2`

## Toolchain

i686 winlibs at: `C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2`

## Step 1: Set toolchain variable and configure CMake

Run from the repo root in the MINGW64 terminal:

```bash
MINGW_I686="/c/Users/mrobe/Downloads/winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2/mingw32"

cmake -B build-winxp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$MINGW_I686/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="$MINGW_I686/bin/g++.exe" \
  -DCMAKE_PREFIX_PATH="$MINGW_I686" \
  -DCMAKE_C_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_CXX_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--subsystem,console:5.01" \
  -DENABLE_PACKAGE_BUILD=ON

MINGW_I686="/c/Users/mrobe/Downloads/winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2/mingw32"

cmake -B build-winxp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$MINGW_I686/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="$MINGW_I686/bin/g++.exe" \
  -DCMAKE_PREFIX_PATH="$MINGW_I686" \
  -DCMAKE_C_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_CXX_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--subsystem,console:5.01" \
  -DENABLE_PACKAGE_BUILD=ON
```

Key flags:
- `-DCMAKE_PREFIX_PATH="$MINGW_I686"` -- so CMake finds the i686 SDL2 (headers + `libSDL2.dll.a`) in the winlibs tree; without it, config may pick up 64-bit MSYS2 SDL2 and fail
- `-march=pentium4 -msse2 -mfpmath=sse` -- targets Pentium 4+, enables SSE2, uses SSE for FP math
- `-D_WIN32_WINNT=0x0501 -DWINVER=0x0501` -- Windows XP API level
- `-Wl,--subsystem,console:5.01` -- PE subsystem version for XP compatibility
- `-static-libgcc -static-libstdc++` -- avoids shipping GCC runtime DLLs
- `-DENABLE_PACKAGE_BUILD=ON` -- hardcodes relative resource paths for distribution

If you previously configured with the wrong SDL2, remove the build dir before re-running cmake: `rm -rf build-winxp`.

## Step 2: Build

```bash
cmake --build build-winxp -j$(nproc)
```

Or equivalently: `make -C build-winxp -j$(nproc)`

## Step 3: Package with package_build.py

You need SDL2.dll and libwinpthread-1.dll for the i686 build. Adjust paths to match your i686 SDL2 and pthread DLLs:

```bash
python tools/ci/package_build.py \
  --build-dir build-winxp \
  --sdl2-dll "$MINGW_I686/bin/SDL2.dll" \
  --libwinpthread-dll "$MINGW_I686/bin/libwinpthread-1.dll" \
  -o package_build_winxp.zip
```

If SDL2.dll is elsewhere (e.g. you installed it separately for i686), point `--sdl2-dll` to that path instead.

## Notes

- The SIMD codepaths in [projection_simd.u.c](src/graphics/projection_simd.u.c) automatically select SSE2 via `#if defined(__SSE2__) && !defined(SSE2_DISABLED)` -- the `-msse2` flag defines `__SSE2__` so this works out of the box
- AVX2 codepaths will **not** be compiled since `-march=pentium4` does not define `__AVX2__`
- If you want to skip the build step in `package_build.py` and just package an already-built directory, omit `--compile-flags` (it's empty by default)
