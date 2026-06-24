# Build tiny host that exercises platform_x_lua (sdl2.c name is historical).

REPO_ROOT := ../../..
SRC_DIR   := $(REPO_ROOT)/src
SRC2      := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2)/platforms
SCRIPT_DIR := $(SRC2)/scripting
TRSPK_DIR := $(SRC_DIR)/platforms/ToriRSPlatformKit

OBJ_DIR := sdl2_build
PROG    := sdl2

# ---------------------------------------------------------------------------
# Platform detection
#   UNAME_S is set from uname on Unix; on a bare Windows shell (cmd.exe /
#   PowerShell) OS=Windows_NT is set by the environment.  MSYS2 / MinGW-w64
#   shells expose both, so check UNAME_S first and fall back to OS.
# ---------------------------------------------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
  HOST_OS := Windows
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
  HOST_OS := Windows
else ifeq ($(UNAME_S),Darwin)
  HOST_OS := MacOS
else ifeq ($(UNAME_S),Linux)
  HOST_OS := Linux
else ifeq ($(OS),Windows_NT)
  HOST_OS := Windows
else
  HOST_OS := Linux
endif

# AddressSanitizer: default-on on Linux, default-off on macOS (dyld deadlocks in
# AsanInitFromRtl before main with Apple's dynamic ASan runtime).
ifeq ($(HOST_OS),Linux)
  ENABLE_ASAN ?= 1
else
  ENABLE_ASAN ?= 0
endif

# ---------------------------------------------------------------------------
# Common compiler / flags
# ---------------------------------------------------------------------------
CC       ?= cc
CFLAGS   ?= -O0 -Wall -Wextra -Werror -g
CFLAGS   += -Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-parameter
CPPFLAGS += -I$(SRC2)/toridraw -I$(SRC_DIR) -I$(SRC2) -I$(SCRIPT_DIR) -I$(REPO_ROOT) \
	-I$(TRSPK_DIR)/include -I$(TRSPK_DIR)/src

# Vendored Lua — embed library, not lua/luac interpreters.
LUA_SRC := $(filter-out $(SRC_DIR)/3rd/lua/lua.c $(SRC_DIR)/3rd/lua/luac.c,\
	$(wildcard $(SRC_DIR)/3rd/lua/*.c))

# ---------------------------------------------------------------------------
# Common source files shared across all platforms
# ---------------------------------------------------------------------------
COMMON_SRCS := sdl2.c \
	$(PLAT_DIR)/asan_compat.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x_lua.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_soft3d.c \
	$(SRC2)/platformkit/core/trspk_atlas.c \
	$(SRC2)/platformkit/core/trspk_drawrangelist.c \
	$(SRC2)/platformkit/core/trspk_drawrangeex.c \
	$(SRC2)/platformkit/core/trspk_ibo.c \
	$(SRC2)/platformkit/core/trspk_modelarena.c \
	$(SRC2)/platformkit/core/trspk_triangles.c \
	$(SRC2)/platformkit/core/trspk_pose.c \
	$(SRC2)/platformkit/core/trspk_vbo.c \
	$(SRC2)/platformkit/core/trspk_vbochain16.c \
	$(PLAT_DIR)/trspk_toridraw.c \
	$(TRSPK_DIR)/src/tools/trspk_batch32.c \
	$(TRSPK_DIR)/src/tools/trspk_resource_cache.c \
	$(TRSPK_DIR)/src/tools/trspk_vertex_buffer.c \
	$(TRSPK_DIR)/src/tools/trspk_vertex_buffer_lifecycle.c \
	$(TRSPK_DIR)/src/tools/trspk_vertex_format.c \
	$(TRSPK_DIR)/src/tools/trspk_facebuffer.c \
	$(TRSPK_DIR)/src/tools/trspk_lru_model_cache.c \
	$(TRSPK_DIR)/src/tools/trspk_d3d8_fvf_bake.c \
	$(TRSPK_DIR)/src/trspk_math.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SRC2)/commands/libtorirs_command_queue.c \
	$(SRC2)/input/libtorirs_input.c \
	$(SRC2)/ioqueue/libtorirs_ioqueue.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SRC2)/toridraw/toridraw_scene.c \
	$(SRC2)/toridraw/toridraw_intrusive_list.c \
	$(SRC2)/world/world.c \
	$(SRC2)/world/world_entity.c \
	$(SRC2)/world/world_pickset.c \
	$(SRC2)/world/world_builder.c \
	$(SRC2)/world/world_decode_tile.c \
	$(SRC2)/world/collision_map.c \
	$(SRC2)/world/heightmap.c \
	$(SRC2)/world/lightmap.c \
	$(SRC2)/world/sharelight_map.c \
	$(SRC2)/world/shademap.c \
	$(SRC2)/world/blendmap.c \
	$(SRC2)/world/overlaymap.c \
	$(SRC2)/world/decor_buildmap.c \
	$(SRC2)/world/minimap.c \
	$(SRC2)/world/terrain_shapemap.c \
	$(SRC2)/world/contour_ground.c \
	$(SRC2)/toridraw/toridraw_model_transform.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(SRC2)/buildcache/dat1_buildcache.c \
	$(SRC2)/toriauxlib/toriauxlib.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2)/toriauxlib/c/toriauxlibc.c \
	$(SRC2)/toriauxlib/c/toriauxlibc_convert.c \
	$(SRC2)/toriauxlib/c/toriauxlibc_model_convert.c \
	$(SRC2)/toriauxlib/c/toriauxlibc_submit.c \
	$(SRC2)/toriauxlib/td/toriauxlibtd.c \
	$(SRC2)/toriauxlib/td/toriauxlibtd_tasks.c \
	$(SRC2)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2)/toridraw/toridraw_animation.c \
	$(SRC2)/toridraw/toridraw.c \
	$(SRC2)/toridraw/toridraw_math.c \
	$(SRC2)/toridraw/toridraw_hsl16.c \
	$(SRC2)/toridraw/toridraw_lighting.c \
	$(SRC2)/toridraw/toridraw_model.c \
	$(SRC2)/toridraw/toridraw_map.c \
	$(SRC2)/toridraw/toridraw_vec.c \
	$(SRC2)/toridraw/toridraw_light_model.c \
	$(SRC2)/toridraw/toridraw_sprite.c \
	$(SRC2)/toridraw/toridraw_font.c \
	$(SRC2)/buildcache/dat1_buildcache_ui.c \
	$(SRC2)/toriauxlib/c/revconfig_ui_build.c \
	$(SRC_DIR)/osrs/revconfig/uitree.c \
	$(SRC2)/core/tasks/core_task.c \
	$(SRC_DIR)/3rd/ini/ini.c \
	$(SRC_DIR)/osrs/revconfig/revconfig.c \
	$(SRC_DIR)/osrs/revconfig/revconfig_load.c \
	$(SRC2)/games/model_viewer.c \
	$(SRC2)/games/runescape.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC2)/toridraw/graphics/shared_tables.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/painters.c \
	$(SRC_DIR)/osrs/texture.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(PLAT_DIR)/platform_x_cache.c \
	$(SRC2)/libtorirs.c \
	$(LUA_SRC)

# ---------------------------------------------------------------------------
# ===== MacOS =====
#   Renderer: OpenGL3 (core 3.2)
#   GL loader: opengl3_sdlgl (SDL_GL_GetProcAddress)
#   GL link:   -framework OpenGL
#   ASan:      opt-in on macOS (make ENABLE_ASAN=1); see ENABLE_ASAN block below
# ---------------------------------------------------------------------------
ifeq ($(HOST_OS),MacOS)

CC       := clang

RENDERER_SRCS := \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_opengl3.c \
	$(SRC2)/platformkit/opengl3/opengl3_sdlgl.c

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
endif
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
  SDL_LIBS   := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
endif

GFX_LIBS := -framework OpenGL
EXE      :=

endif # MacOS

# ---------------------------------------------------------------------------
# ===== Linux =====
#   Renderer: OpenGL3 (core 3.3)
#   GL link:  -lGL
#   ASan:     enabled by default (make ENABLE_ASAN=0 to disable)
# ---------------------------------------------------------------------------
ifeq ($(HOST_OS),Linux)

RENDERER_SRCS := \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_opengl3.c \
	$(SRC2)/platformkit/opengl3/opengl3_sdlgl.c

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
endif
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := -I/usr/include/SDL2
  SDL_LIBS   := -lSDL2
endif

GFX_LIBS := -lGL
EXE      :=

endif # Linux

# ---------------------------------------------------------------------------
# ===== Windows (MinGW-w64) =====
#   Renderer: Direct3D9 (fixed-function FVF pipeline)
#   GL link:  none (D3D9 via -ld3d9 / d3d9.lib)
#   ASan:     disabled (not supported under MinGW)
#   SDL2:     linked as -lmingw32 -lSDL2main -lSDL2 (MSYS2 / MinGW-w64 layout)
#   Output:   sdl2.exe
#
#   Build with:
#     mingw32-make  (from an MSYS2 / MinGW-w64 shell)
#   or:
#     make HOST_OS=Windows CC=x86_64-w64-mingw32-gcc
# ---------------------------------------------------------------------------
ifeq ($(HOST_OS),Windows)

# Vendored 32-bit (i686) MinGW-w64 toolchain — targets Windows XP SP3.
TOOLCHAIN_WIN32 := $(REPO_ROOT)/toolchain/win32/mingw32

# Force the 32-bit compiler (still overridable on the command line via CC=...).
CC := $(TOOLCHAIN_WIN32)/bin/gcc.exe

# Windows XP SP3 / Pentium-4 SSE2 target flags.
CFLAGS  += -march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501
LDFLAGS += -static-libgcc -Wl,--subsystem,console:5.01

RENDERER_SRCS := \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_d3d9.c

# SDL2 from the vendored toolchain (no pkg-config/sdl2-config present there).
SDL_CFLAGS := -I$(TOOLCHAIN_WIN32)/include/SDL2 -Dmain=SDL_main
SDL_LIBS   := -L$(TOOLCHAIN_WIN32)/lib -lmingw32 -lSDL2main -lSDL2

GFX_LIBS := -ld3d9 -lgdi32
EXE      := .exe

# Copy the matching 32-bit runtime DLLs next to the exe (fixes 0xc000007b).
POSTLINK := cp -f $(TOOLCHAIN_WIN32)/bin/SDL2.dll $(TOOLCHAIN_WIN32)/bin/libwinpthread-1.dll .

endif # Windows

# ---------------------------------------------------------------------------
# AddressSanitizer (optional on macOS, default-on on Linux)
# ---------------------------------------------------------------------------
ifeq ($(ENABLE_ASAN),1)
  CFLAGS  += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
  LDFLAGS += -fsanitize=address
  ifeq ($(HOST_OS),MacOS)
    # clang must drive the final link; static SDL2 avoids dylib malloc before ASan.
    CC := clang
    SDL_LIBS := $(shell sdl2-config --static-libs 2>/dev/null)
    ifeq ($(strip $(SDL_LIBS)),)
      SDL_LIBS := $(shell pkg-config --libs --static sdl2 2>/dev/null)
    endif
    ifeq ($(strip $(SDL_LIBS)),)
      SDL_LIBS := -L/opt/homebrew/lib -Wl,-force_load,/opt/homebrew/lib/libSDL2.a \
        -framework CoreAudio -framework AudioToolbox -framework CoreVideo \
        -framework Cocoa -framework Carbon -framework IOKit -framework QuartzCore \
        -framework Metal -lobjc
    endif
    # macOS 26+: interpose dyld_shared_cache_iterate_text before ASan init (LLVM #182943).
    ASAN_DYLD_SHIM := asan_dyld_shim.dylib
  endif
endif

ASAN_DYLD_SHIM ?=

# ---------------------------------------------------------------------------
# ===== Emscripten =====
#   The WebGL1 / browser build is produced by a *separate* Makefile:
#       src2/programs/browser/Makefile
#   Run that makefile (using emcc) to generate dist/browser.js + browser.wasm.
#   Nothing in this Makefile is executed for an Emscripten build.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Final source list, object list, and build rules
# ---------------------------------------------------------------------------
SRCS := $(COMMON_SRCS) $(RENDERER_SRCS)

# Flat object list in OBJ_DIR (avoids path collisions for same-named files).
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

# Post-link step: copy runtime DLLs next to the exe on Windows; no-op elsewhere.
POSTLINK ?=

# clean + all in one invocation must not run in parallel (-j deletes OBJ_DIR mid-build).
ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
.NOTPARALLEL:
endif
endif

.PHONY: all clean asan

all: $(OBJ_DIR)/.stamp $(PROG)$(EXE)

$(OBJ_DIR)/.stamp:
	mkdir -p $(OBJ_DIR)
	touch $@

asan_dyld_shim.dylib: $(PLAT_DIR)/asan_dyld_shim.c
	$(CC) -dynamiclib -o $@ $< -DTORIRS_ENABLE_ASAN=1

$(PROG)$(EXE): $(OBJS) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(SDL_LIBS) $(GFX_LIBS) -lm $(ASAN_DYLD_SHIM)
	$(POSTLINK)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $(1) -o $$@
endef

LEGACY_TABLE_WARNINGS := -Wno-unused-function -Wno-unused-variable -Wno-unused-label \
	-Wno-unused-but-set-variable -Wno-pointer-sign -Wno-parentheses

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

$(OBJ_DIR)/trspk_vertex_buffer.o: $(TRSPK_DIR)/src/tools/trspk_vertex_buffer.c | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -Wno-unused-parameter -c $< -o $@

# 3rd-party vendored libraries: compile with all warnings suppressed.
THIRD_PARTY_CFLAGS := $(filter-out -Wall -Wextra -Werror,$(CFLAGS)) -w

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) $(SDL_CFLAGS) -c $< -o $@

define make-lua-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) $(SDL_CFLAGS) -c $(1) -o $$@
endef
$(foreach src,$(LUA_SRC),$(eval $(call make-lua-goal,$(src))))

clean:
	rm -f $(PROG) $(PROG).exe asan_dyld_shim.dylib
	rm -rf $(OBJ_DIR)

asan:
	$(MAKE) ENABLE_ASAN=1 clean
	$(MAKE) ENABLE_ASAN=1 all
