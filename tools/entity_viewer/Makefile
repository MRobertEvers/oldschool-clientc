# tools/entity_viewer
#
#   make            -> ev_catalog + ev_server (native)
#   make wasm       -> web/ev_wasm.js + .wasm (needs emcc on PATH)
#   make all-web    -> both
#
# Two halves that never share a binary: the native side links rscache and owns
# the cache, the wasm side links toridraw and owns the raster. ev_wire.c is the
# one file both compile, so the format they speak has a single definition.

CC      ?= cc
ENABLE_ASAN ?= 0
HOST_OS := $(shell uname -s)
REPO    := $(abspath ../..)
RSCACHE := $(REPO)/3rd/rscache
THIRD   := $(REPO)/3rd
COMMON  := $(RSCACHE)/tools/common
PORTLC  := $(RSCACHE)/tools/port_lostcity

TORIDRAW := $(THIRD)/toridraw

INCLUDES := -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) -I$(COMMON) -I$(PORTLC) \
	-I$(TORIDRAW) -I$(THIRD) -I$(REPO) -I$(REPO)/src -I$(THIRD)/hmap \
	-I$(THIRD)/bmp -I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas -I$(THIRD)/ini

# -O2: a catalog run walks every npc, every sequence and the framemap of each.
CFLAGS  ?= -std=c11 -g -O2 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign
CFLAGS  += $(INCLUDES)
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
DEP_CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
ASAN_LDFLAGS := -fsanitize=address
ifeq ($(HOST_OS),Darwin)
ASAN_COMPAT_OBJ = $(BUILD)/asan_compat.o
ASAN_DYLD_SHIM = $(BUILD)/asan_dyld_shim.dylib
endif
endif

ASAN_COMPAT_OBJ ?=
ASAN_DYLD_SHIM ?=

BUILD := build

COMMON_SRCS := \
	$(COMMON)/tool_profile.c \
	$(COMMON)/asset_access.c \
	$(COMMON)/anim_affinity.c \
	$(COMMON)/port_plan.c \
	$(COMMON)/cache_write.c \
	$(COMMON)/transcode.c \
	$(COMMON)/cs2_db_columns.c

COMMON_OBJS := $(patsubst $(COMMON)/%.c,$(BUILD)/common_%.o,$(COMMON_SRCS))

DEP_OBJS := \
	$(BUILD)/rscache_unity.o \
	$(BUILD)/bzip.o \
	$(BUILD)/bzip_encode.o \
	$(BUILD)/miniz.o \
	$(BUILD)/bmp.o \
	$(BUILD)/xteas.o \
	$(BUILD)/ini.o

.PHONY: all clean wasm all-web
all: ev_catalog ev_server ev_swing
all-web: all wasm

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/common_%.o: $(COMMON)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rscache_unity.o: $(RSCACHE)/rscache_unity.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bmp.o: $(THIRD)/bmp/bmp.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ini.o: $(THIRD)/ini/ini.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ev_catalog.o: ev_catalog.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# The tree's own `id=name` pack reader, rather than a second parser for the
# compacks here.
$(BUILD)/lc_pack.o: $(PORTLC)/lc_pack.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/asan_compat.o: $(REPO)/src/platform/asan_compat.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/asan_dyld_shim.dylib: $(REPO)/src/platform/asan_dyld_shim.c | $(BUILD)
	$(CC) -dynamiclib -fblocks -DTORIRS_ENABLE_ASAN=1 \
		-install_name @loader_path/build/asan_dyld_shim.dylib -o $@ $<

ev_catalog: $(BUILD)/ev_catalog.o $(BUILD)/lc_pack.o $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- server: rscache + toridraw, natively --------------------------------

$(BUILD)/toridraw_unity.o: $(TORIDRAW)/toridraw_unity.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

# Not in the unity: the font module needs v0/ on its include path, which is why
# src/makefile compiles it on its own too.
$(BUILD)/toridraw_font.o: $(TORIDRAW)/toridraw_font.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -I$(REPO)/v0 -c $< -o $@

# Also outside the unity. The self-test uses it as an oracle: it is the model
# rasteriser the client itself renders chatheads and interface previews with.
$(BUILD)/toridraw_model_sprite.o: $(TORIDRAW)/toridraw_model_sprite.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ev_wire.o: ev_wire.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ev_build.o: ev_build.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ev_render.o: ev_render.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# The client's own model conversion, rather than a second copy of it in this
# directory. Three self-contained files: no CacheProvider, no task pipeline.
$(BUILD)/torirs_types.o: $(REPO)/src/engine/torirs_types.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

# The real texture pipeline, taken from the client rather than reimplemented:
# the procedural evaluator and the palette/gamma rules. ev_textures.c is a
# synchronous driver over these, so the viewer and the client agree on colour.
$(BUILD)/proctex_generator.o: $(REPO)/src/engine/proctex/proctex_generator.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/texture_palette_bake.o: $(REPO)/src/engine/texture_palette_bake.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

# torirs_types.c's component teardown calls into this; it is self-contained
# (torirs_types.h plus libc), so it costs nothing to carry and the link fails
# without it.
$(BUILD)/torirs_component_hook.o: $(REPO)/src/engine/torirs_component_hook.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/torirs_model_from_rscache.o: $(REPO)/src/engine/torirs_model_from_rscache.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/toridraw_model_from_torirs.o: $(REPO)/src/engine/toridraw_model_from_torirs.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/toridraw_animation_from_rscache.o: $(REPO)/src/engine/toridraw_animation_from_rscache.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ev_player.o: ev_player.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ev_server.o: ev_server.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Everything both native binaries need. ev_player.c is here and not in the wasm
# list because it links rscache; the merge it would otherwise own lives in
# ev_render.c precisely so the browser can do it too.
NATIVE_CORE := $(BUILD)/ev_build.o $(BUILD)/ev_player.o $(BUILD)/ev_wire.o \
	$(BUILD)/ev_render.o $(BUILD)/ev_caches.o $(BUILD)/ev_textures.o $(BUILD)/proctex_generator.o \
	$(BUILD)/texture_palette_bake.o $(BUILD)/torirs_types.o $(BUILD)/torirs_component_hook.o \
	$(BUILD)/torirs_model_from_rscache.o $(BUILD)/toridraw_model_from_torirs.o \
	$(BUILD)/toridraw_animation_from_rscache.o \
	$(BUILD)/toridraw_unity.o $(BUILD)/toridraw_font.o \
	$(BUILD)/toridraw_model_sprite.o

ev_server: $(BUILD)/ev_server.o $(BUILD)/lc_pack.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- ev_swing: a weapon swing and its attached graphic, measured ----------
#
# Needs no catalog and no browser: it is the same build and the same renderer as
# ev_server, driven from the command line so a placement can be answered with a
# number instead of an opinion.

$(BUILD)/ev_swing.o: ev_swing.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

ev_swing: $(BUILD)/ev_swing.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- ev_export: a subject's models and animations, to disk ---------------
#
# Produces the checked-in set under docs/hdmodels so the viewer's file picker
# has something to open without a 461 MB cache. Models go out as raw archives
# (the viewer decodes them, which is the point); animations as ev_wire, because
# a sequence names frames which name a framemap and a raw export would be inert.
$(BUILD)/ev_textures.o: ev_textures.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ev_caches.o: ev_caches.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ev_export.o: ev_export.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- ev_textures_probe: does the texture loader actually produce pixels ----
#
# Reports the shape of what came out, not just a count: a loader that returns
# 4000 uniformly-white 128x128 buffers "succeeds" by every count it keeps, and
# that is exactly the failure it exists to catch.
$(BUILD)/ev_textures_probe.o: test/ev_textures_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_textures_probe: $(BUILD)/ev_textures_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_move_probe.o: test/ev_move_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_move_probe: $(BUILD)/ev_move_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_array_probe.o: test/ev_array_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_array_probe: $(BUILD)/ev_array_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_texture_render_probe.o: test/ev_texture_render_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_texture_render_probe: $(BUILD)/ev_texture_render_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

ev_export: $(BUILD)/ev_export.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- browser: toridraw only, to WebAssembly ------------------------------
#
# No rscache and no filesystem here. `-sMODULARIZE` gives the page an EVModule()
# factory to await, which is what keeps ev.js free of load-order guesswork.
#
# The whole software raster runs inside this module, so it gets the maximum
# optimization set, not the catalog's -O2:
#   -O3 + -flto        cross-TU inlining of the span kernels into the render loop
#   -msimd128          WASM SIMD; lets LLVM vectorize the pixel loops. Every
#                      browser since 2021 (Chrome 91, Firefox 89, Safari 16.4)
#                      has it — build with SIMD=0 if an older engine matters.
#   -mnontrapping-fptoint  float->int converts (projection) become one
#                      saturating instruction instead of a guarded sequence
#   -sINITIAL_MEMORY   big enough up front that model/anim loads never hit the
#                      growth path mid-session (growth stays on as a backstop)
# NOT -ffast-math: the raster is compared pixel-for-pixel against the native
# client in tests, and reassociated float math breaks that parity.

EMCC ?= emcc
SIMD ?= 1
EM_EXPORTS := _ev_w_init,_ev_w_alloc,_ev_w_release,_ev_w_set_model,_ev_w_set_anim,_ev_w_clear_anim,_ev_w_frame_count,_ev_w_frame_delay,_ev_w_render,_ev_w_model_height,_ev_w_last_cull,_ev_w_set_pan,_ev_w_set_zbuffer,_ev_w_set_frame_height,_ev_w_set_spot_model,_ev_w_set_spot_anim,_ev_w_clear_spot,_ev_w_set_spot_state,_ev_w_spot_frame_count,_ev_w_spot_frame_delay,_ev_w_set_model_hd,_ev_w_clear_model_hd,_ev_w_model_hd_active,_ev_w_hd_stats,_ev_w_hd_stats_count,_ev_w_set_hd_placeholder,_ev_w_set_textures,_ev_w_texture_count,_ev_w_move,_ev_w_move_reset,_malloc,_free

EM_OPT := -O3 -flto -mnontrapping-fptoint
ifeq ($(SIMD),1)
EM_OPT += -msimd128
endif

wasm: web/ev_wasm.js

web/ev_wasm.js: ev_wasm.c ev_render.c ev_wire.c $(TORIDRAW)/toridraw_unity.c
	$(EMCC) $(EM_OPT) -std=c11 -w $(INCLUDES) \
		-I$(REPO)/v0 \
		-o $@ ev_wasm.c ev_render.c ev_wire.c $(TORIDRAW)/toridraw_unity.c \
		$(TORIDRAW)/toridraw_font.c \
		-sMODULARIZE=1 -sEXPORT_NAME=EVModule \
		-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sFILESYSTEM=0 \
		-sASSERTIONS=0 -sENVIRONMENT=web \
		-sEXPORTED_FUNCTIONS='[$(EM_EXPORTS)]' \
		-sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8"]'

clean:
	rm -rf $(BUILD) ev_catalog ev_server ev_swing web/ev_wasm.js web/ev_wasm.wasm
