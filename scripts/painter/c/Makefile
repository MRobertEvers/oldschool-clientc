# From scripts/painter/c: make && ./bench_painter
# Real painter fuzzer: make fuzz_real && ./fuzz_real
#
# Windows cross-builds (requires MinGW-w64 cross toolchain):
#   make winxp   -- 32-bit Windows XP + SSE2  -> dist/winxp/
#   make win64   -- 64-bit modern Windows      -> dist/win64/
#   make windows -- both of the above
#
# Toolchain overrides (use a vendored winlibs tree or a system install):
#   make winxp  WINXP_CC=/path/to/mingw32/bin/gcc.exe
#   make win64  WIN64_CC=/path/to/mingw64/bin/gcc.exe
#   make win64  WIN64_ISA=-mavx2   # enable AVX2 on modern hosts

REPO_ROOT := ../../..
SRC_DIR   := $(REPO_ROOT)/src
SRC2      := $(REPO_ROOT)/src2
include $(SRC2)/toriauxlib/core/tasks/tasks_sources.mk
PLAT_DIR  := $(SRC2)/platforms
SCRIPT_DIR := $(SRC2)/scripting

CC ?= cc
CFLAGS ?= -O3 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lm
BENCH_ITERS ?= 100

CPPFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)/painters -I$(REPO_ROOT)/3rd/toridraw

SOURCES = bench.c painter_common.c painter_bucket.c painter_world3d.c

REAL_COMMON = \
	painter_fuzz_scene.c \
	painter_fuzz_diff.c \
	painter_bench_real.c \
	$(SRC_DIR)/painters/painters.c \
	$(SRC_DIR)/painters/painters_cull_project.c \
	$(REPO_ROOT)/3rd/toridraw/graphics/shared_tables.c

FUZZ_SOURCES = fuzz_real.c $(REAL_COMMON)
REAL_BENCH_SOURCES = bench_real.c $(REAL_COMMON)

# ---------------------------------------------------------------------------
# Host build (unchanged)
# ---------------------------------------------------------------------------
.PHONY: all clean run fuzz fuzz_real bench_real bench_real_cache \
	bench_real_run bench_real_cache_run fuzz_cache fuzz_cache_run winxp win64 windows

all: bench_painter

bench_painter: $(SOURCES) painter_common.h Makefile
	$(CC) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) $(SOURCES) $(LDFLAGS) -o $@

fuzz_real: $(FUZZ_SOURCES) Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) -DTORIRS_PERF_DISABLE $(FUZZ_SOURCES) $(LDFLAGS) -o $@

bench_real: $(REAL_BENCH_SOURCES) Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) -DTORIRS_PERF_DISABLE $(REAL_BENCH_SOURCES) $(LDFLAGS) -o $@

run: all
	./bench_painter

fuzz: fuzz_real
	./fuzz_real

clean:
	rm -f bench_painter bench_real bench_real_cache fuzz_real fuzz_cache
	rm -rf dist

# ---------------------------------------------------------------------------
# Cache-scene differential test (real dat2 world, headless, no SDL)
#   make fuzz_cache           -- build
#   make fuzz_cache_run       -- build + run superset check
#   make fuzz_cache_run CACHE_DIR=/path/to/cache  -- explicit cache dir
#   make fuzz_cache_run CACHE_BENCH=bench         -- also benchmark
#
# Cache loads skip dat2 texture decode (painter-only); world maps/models
# are fetched via the world-rebuild task instead.
# ---------------------------------------------------------------------------

CACHE_DIR ?= $(REPO_ROOT)/cache

LUA_SRC := $(filter-out $(SRC_DIR)/3rd/lua/lua.c $(SRC_DIR)/3rd/lua/luac.c,\
	$(wildcard $(SRC_DIR)/3rd/lua/*.c))

CACHE_SRCS := \
	$(PLAT_DIR)/asan_compat.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x_lua.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cache_path_resolve.c \
	$(SRC2)/libtorirs.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(SRC2)/commands/libtorirs_command_queue.c \
	$(SRC2)/input/libtorirs_input.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SRC2)/games/runescape.c \
	$(SRC2)/games/games/runescape_cs2_host.c \
	$(SRC2)/games/games/runescape_cs2_queue.c \
	$(SRC2)/runescape/appearance.c \
	$(SRC2)/runescape/player_body.c \
	$(SRC_DIR)/osrs/datatypes/appearances.c \
	$(SRC2)/games/model_viewer.c \
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
	$(SRC2)/toridraw/toridraw_scene.c \
	$(SRC2)/toridraw/toridraw_intrusive_list.c \
	$(SRC2)/toridraw/toridraw_model_transform.c \
	$(SRC2)/toridraw/toridraw_animation.c \
	$(SRC2)/toridraw/toridraw_mat4.c \
	$(SRC2)/toridraw/toridraw.c \
	$(SRC2)/toridraw/toridraw_math.c \
	$(SRC2)/toridraw/toridraw_hsl16.c \
	$(SRC2)/toridraw/toridraw_lighting.c \
	$(SRC2)/toridraw/toridraw_model.c \
	$(SRC2)/toridraw/toridraw_map.c \
	$(SRC2)/toridraw/toridraw_vec.c \
	$(SRC2)/toridraw/toridraw_light_model.c \
	$(SRC2)/toridraw/toridraw_sprite.c \
	$(SRC2)/toridraw/toridraw_model_sprite.c \
	$(SRC2)/toridraw/toridraw_font.c \
	$(SRC2)/toridraw/graphics/shared_tables.c \
	$(SRC2)/toriauxlib/toriauxlib.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_model_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_sprite_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_font_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_submit.c \
	$(SRC2)/toriauxlib/td/toriauxlibtd.c \
	$(SRC2)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2)/toriauxlib/vm/toriauxlibvm.c \
	$(SRC2)/vm/cs1vm.c \
	$(SRC2)/buildcache/dat1_buildcache.c \
	$(SRC2)/buildcache/dat2_buildcache.c \
	$(SRC2)/buildcache/dat1_buildcache_ui.c \
	$(SRC2)/ui/ui_sprite_lookup.c \
	$(TORIAUXLIB_TASK_SOURCES) \
	$(SRC2)/ui/uitree.c \
	$(SRC2)/ui/ui_input.c \
	$(SRC2)/ui/ui_behavior.c \
	$(SRC2)/ui/ui_input_adapter.c \
	$(SRC2)/revconfig/revconfig.c \
	$(SRC2)/revconfig/revconfig_load.c \
	$(SRC2)/3rd/ini/ini.c \
	$(SRC_DIR)/painters/painters.c \
	$(SRC_DIR)/painters/painters_cull_project.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/texture.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(LUA_SRC)

CACHE_CPPFLAGS := \
	-I$(SRC2)/toridraw -I$(SRC_DIR) -I$(SRC_DIR)/painters -I$(REPO_ROOT)/3rd/toridraw \
	-I$(SRC2) -I$(SCRIPT_DIR) -I$(REPO_ROOT)

CACHE_CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter \
	-Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable

CACHE_LDLIBS := -lm
ifeq ($(OS),Windows_NT)
CACHE_LDLIBS += -lws2_32
endif

THIRD_PARTY_OBJS_WARN := -w

CACHE_SRCS_NPAINTERS := $(filter-out $(SRC_DIR)/painters/painters.c $(SRC_DIR)/painters/painters_cull_project.c,$(CACHE_SRCS))

# Cache builds use toridraw shared_tables (via CACHE_CPPFLAGS -I order), not duplicated here.
REAL_COMMON_NSHARED := $(filter-out $(REPO_ROOT)/3rd/toridraw/graphics/shared_tables.c,$(REAL_COMMON))

bench_real_cache: bench_real.c $(REAL_COMMON_NSHARED) $(CACHE_SRCS_NPAINTERS) Makefile
	$(CC) $(CACHE_CFLAGS) $(CACHE_CPPFLAGS) -DFUZZ_WITH_CACHE \
		bench_real.c $(REAL_COMMON_NSHARED) $(CACHE_SRCS_NPAINTERS) $(CACHE_LDLIBS) -o bench_real_cache

bench_real_run: bench_real
	./bench_real 1 200 200

bench_real_cache_run: bench_real_cache
	./bench_real_cache cache $(CACHE_DIR) 200

fuzz_cache: fuzz_real.c $(REAL_COMMON_NSHARED) $(CACHE_SRCS_NPAINTERS) Makefile
	$(CC) $(CACHE_CFLAGS) $(CACHE_CPPFLAGS) -DFUZZ_WITH_CACHE \
		fuzz_real.c $(REAL_COMMON_NSHARED) $(CACHE_SRCS_NPAINTERS) $(CACHE_LDLIBS) -o $@

CACHE_BENCH ?=

fuzz_cache_run: fuzz_cache
	./fuzz_cache cache $(CACHE_DIR) $(CACHE_BENCH)

# ---------------------------------------------------------------------------
# Windows cross-compile: shared base flags
# ---------------------------------------------------------------------------
BASE_CFLAGS   := -O3 -Wall -Wextra -Wno-unused-parameter
BASE_CPPFLAGS := -I$(SRC_DIR) -I$(SRC_DIR)/painters -I$(REPO_ROOT)/3rd/toridraw

# 32-bit cross toolchain — defaults to the system MinGW-w64 i686 wrapper.
# Override with WINXP_CC=/path/to/mingw32/bin/gcc.exe for a vendored winlibs build.
WINXP_CC ?= i686-w64-mingw32-gcc

# 64-bit cross toolchain — defaults to the system MinGW-w64 x86_64 wrapper.
# Override with WIN64_CC=/path/to/mingw64/bin/gcc.exe for a vendored build.
WIN64_CC ?= x86_64-w64-mingw32-gcc

# ISA knob for the 64-bit build: empty = baseline (SSE2 implied by x86_64).
# Example: make win64 WIN64_ISA=-mavx2
WIN64_ISA ?=

# Windows XP SP3: Pentium 4 + SSE2, WinAPI level 0x0501, subsystem version 5.01
WINXP_CFLAGS  := $(BASE_CFLAGS) -march=pentium4 -msse2 -mfpmath=sse \
                 -D_WIN32_WINNT=0x0501 -DWINVER=0x0501
WINXP_LDFLAGS := -static -lm -Wl,--subsystem,console:5.01

# Modern 64-bit Windows: Vista+ API level, optional higher ISA
WIN64_CFLAGS  := $(BASE_CFLAGS) -D_WIN32_WINNT=0x0600 $(WIN64_ISA)
WIN64_LDFLAGS := -static -lm

# ---------------------------------------------------------------------------
# Windows XP target  (dist/winxp/)
# ---------------------------------------------------------------------------
winxp: dist/winxp/fuzz_real.exe dist/winxp/bench_painter.exe dist/winxp/bench_real.exe

dist/winxp/fuzz_real.exe: $(FUZZ_SOURCES) Makefile | dist/winxp
	$(WINXP_CC) $(WINXP_CFLAGS) $(BASE_CPPFLAGS) $(FUZZ_SOURCES) $(WINXP_LDFLAGS) -o $@

dist/winxp/bench_painter.exe: $(SOURCES) painter_common.h Makefile | dist/winxp
	$(WINXP_CC) $(WINXP_CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) $(SOURCES) $(WINXP_LDFLAGS) -o $@

dist/winxp/bench_real.exe: $(REAL_BENCH_SOURCES) Makefile | dist/winxp
	$(WINXP_CC) $(WINXP_CFLAGS) $(BASE_CPPFLAGS) $(REAL_BENCH_SOURCES) $(WINXP_LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Modern 64-bit Windows target  (dist/win64/)
# ---------------------------------------------------------------------------
win64: dist/win64/fuzz_real.exe dist/win64/bench_painter.exe dist/win64/bench_real.exe

dist/win64/fuzz_real.exe: $(FUZZ_SOURCES) Makefile | dist/win64
	$(WIN64_CC) $(WIN64_CFLAGS) $(BASE_CPPFLAGS) $(FUZZ_SOURCES) $(WIN64_LDFLAGS) -o $@

dist/win64/bench_painter.exe: $(SOURCES) painter_common.h Makefile | dist/win64
	$(WIN64_CC) $(WIN64_CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) $(SOURCES) $(WIN64_LDFLAGS) -o $@

dist/win64/bench_real.exe: $(REAL_BENCH_SOURCES) Makefile | dist/win64
	$(WIN64_CC) $(WIN64_CFLAGS) $(BASE_CPPFLAGS) $(REAL_BENCH_SOURCES) $(WIN64_LDFLAGS) -o $@

# Convenience: build both Windows variants
windows: winxp win64

dist/winxp dist/win64:
	mkdir -p $@
