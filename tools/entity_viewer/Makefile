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
# The server's rig walk runs on a worker thread so a cache switch does not stall
# the accept loop (ev_rigs.c). -pthread rather than -lpthread: it is the flag
# that is right at compile time and link time on both clang and gcc.
CFLAGS  += -pthread
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

# Header dependencies.
#
# Without these a header change rebuilds nothing, and the objects that used the
# old declarations link cleanly against the ones that used the new. That is not
# a build error, it is a program that runs and is wrong -- a struct that grew a
# field reads at the wrong offsets, and an inline function that changed keeps
# its old body in whichever object was not rebuilt. Both have cost real time on
# this tool.
CFLAGS  += -MMD -MP
DEP_CFLAGS += -MMD -MP

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

# ENABLE_ASAN=1 is a distinct object flavor and must not share an objdir with
# the plain one. Compiler flags are not part of make's up-to-date test, so with
# a shared build/ an ASan link happily picks up objects compiled *without*
# -fsanitize=address: the binary loads the ASan runtime, runs to completion and
# reports nothing. A clean run then means nothing at all, which is worse than
# not having the sanitizer -- it retires a hypothesis that was never tested.
# Measured here: `make ENABLE_ASAN=1 ev_hd_sheet` over a warm build/ produced a
# binary linked against libclang_rt.asan with not one instrumented object in it.
# src/Makefile suffixes OBJ_DIR for the same reason.
BUILD := build$(if $(filter 1,$(ENABLE_ASAN)),_asan,)

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
		-install_name @loader_path/$(BUILD)/asan_dyld_shim.dylib -o $@ $<

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
	$(BUILD)/ev_render.o $(BUILD)/ev_caches.o $(BUILD)/ev_rigs.o \
	$(BUILD)/ev_textures.o $(BUILD)/proctex_generator.o \
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

$(BUILD)/ev_rigs.o: ev_rigs.c | $(BUILD)
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

# ---- ev_cache_load_probe: does the loader read this cache, or only open it ----
#
# Detection, index and geometry in one pass, per cache directory. The failure it
# exists to catch has no error attached: a wrong revision profile still lists
# npcs and still returns models, they are just made of the wrong bytes.
$(BUILD)/ev_cache_load_probe.o: test/ev_cache_load_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_cache_load_probe: $(BUILD)/ev_cache_load_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- ev_hd_sheet: HD compositing hypotheses, side by side ----------------
#
# One process renders every variant at identical framing into one contact
# sheet. Judging two renders seen minutes apart is how a change that makes
# things slightly worse gets kept.
$(BUILD)/ev_uv_span.o: test/ev_uv_span.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_uv_span: $(BUILD)/ev_uv_span.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_hd_sheet.o: test/ev_hd_sheet.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_hd_sheet: $(BUILD)/ev_hd_sheet.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_zbuf_probe.o: test/ev_zbuf_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_zbuf_probe: $(BUILD)/ev_zbuf_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_mat_probe.o: test/ev_mat_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_mat_probe: $(BUILD)/ev_mat_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_fmt_probe.o: test/ev_fmt_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_fmt_probe: $(BUILD)/ev_fmt_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_index_cache_probe.o: test/ev_index_cache_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_index_cache_probe: $(BUILD)/ev_index_cache_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

# ---- ev_rig_probe: does a catalog-less cache produce animations -----------
#
# And does a cache switch mid-walk abandon the walk it started? Both failures
# are silent: an empty animation list looks like an npc that has no animations,
# and a late publish from the previous cache looks like a correct one.
$(BUILD)/ev_rig_probe.o: test/ev_rig_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_rig_probe: $(BUILD)/ev_rig_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
		$(ASAN_COMPAT_OBJ) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) $(ASAN_LDFLAGS) -o $@ $^ -lm

$(BUILD)/ev_move_screen_probe.o: test/ev_move_screen_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -c $< -o $@

ev_move_screen_probe: $(BUILD)/ev_move_screen_probe.o $(NATIVE_CORE) $(COMMON_OBJS) $(DEP_OBJS) \
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
EM_EXPORTS := _ev_w_init,_ev_w_alloc,_ev_w_release,_ev_w_set_model,_ev_w_set_anim,_ev_w_clear_anim,_ev_w_frame_count,_ev_w_frame_delay,_ev_w_render,_ev_w_model_height,_ev_w_last_cull,_ev_w_set_pan,_ev_w_set_zbuffer,_ev_w_set_frame_height,_ev_w_set_spot_model,_ev_w_set_spot_anim,_ev_w_clear_spot,_ev_w_set_spot_state,_ev_w_spot_frame_count,_ev_w_spot_frame_delay,_ev_w_set_model_hd,_ev_w_clear_model_hd,_ev_w_model_hd_active,_ev_w_hd_stats,_ev_w_hd_stats_count,_ev_w_set_hd_placeholder,_ev_w_set_textures,_ev_w_texture_count,_ev_w_move,_ev_w_move_reset,_ev_w_set_ignore_priorities,_ev_w_set_zbuffer_kernels,_malloc,_free

EM_SIMD := $(if $(filter 1,$(SIMD)),-msimd128,)
EM_OPT := -O3 -flto -mnontrapping-fptoint $(EM_SIMD)

# The sources and the link settings both flavors share. Only the optimization
# set, the sanitizer flags and the memory settings differ below, so the two
# modules cannot drift in what they export or how the page reaches them.
EM_SRCS := ev_wasm.c ev_render.c ev_wire.c $(TORIDRAW)/toridraw_unity.c \
	$(TORIDRAW)/toridraw_font.c
EM_LINK := -sMODULARIZE=1 -sEXPORT_NAME=EVModule \
	-sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -sENVIRONMENT=web \
	-sEXPORTED_FUNCTIONS='[$(EM_EXPORTS)]' \
	-sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8"]'

wasm: web/ev_wasm.js

# The wasm is built from one emcc invocation, so there are no per-object
# depfiles to include -- the header list is explicit instead. A stale wasm is
# the failure run.sh exists to catch, and it is silent: the browser keeps
# running the old renderer while the server is current.
EM_HEADERS := $(wildcard ev_*.h) $(wildcard $(TORIDRAW)/*.h) \
	$(wildcard $(TORIDRAW)/graphics/*.h) $(wildcard $(TORIDRAW)/graphics/raster/*/*.h)

web/ev_wasm.js: $(EM_SRCS) $(EM_HEADERS)
	$(EMCC) $(EM_OPT) -std=c11 -w $(INCLUDES) \
		-I$(REPO)/v0 \
		-o $@ $(EM_SRCS) \
		$(EM_LINK) \
		-sINITIAL_MEMORY=67108864 -sASSERTIONS=0

# ---- browser, instrumented: `make wasm-san` ------------------------------
#
# The same module built with AddressSanitizer and UndefinedBehaviorSanitizer.
# Both work under emscripten: ASan gets a real shadow map over linear memory
# and reports heap-buffer-overflow / use-after-free / double-free with a wasm
# stack trace; UBSan reports carry their own file:line.
#
# This is the only way to see a heap defect in this module at all. Wasm linear
# memory has no unmapped pages and no guard bands, so an overrun, a stale
# pointer or a read past a freed model does not trap -- it returns ordinary
# addressable bytes and the renderer carries on with them. That is why the
# symptom is "the animation stops at frame 37" or "the model draws wrong after
# a switch" rather than a crash, and why reasoning about it from the browser
# alone does not converge. See the frames==NULL note in README.md, which is
# exactly this shape.
#
# It is a SEPARATE output on purpose: web/ev_wasm.js and web/ev_wasm.wasm are
# tracked files, and the instrumented module is several times their size, so
# building over them would stage the sanitized build into a commit.
# web/ev_wasm_san.* is gitignored, and index.html loads it instead when the
# page URL carries ?san.
#
# The settings that are not simply -fsanitize=:
#   no -flto, -O1  inlining is what turns a usable trace into "somewhere in the
#                  render loop". Override with EM_SAN_OPT.
#   -g2            function names in the wasm name section, which is what ASan
#                  prints its stack frames from. (ASan file:line additionally
#                  needs full DWARF and a browser that reads it; UBSan carries
#                  its source location in the check either way.)
#   INITIAL_MEMORY ASan's shadow map lives below the heap in the same linear
#                  memory, so the module needs materially more than the 64MB
#                  the plain build starts at.
#   STACK_SIZE     every instrumented frame carries redzones; the default 64KB
#                  stack overflows inside the raster, and so did 4MB — ev_render
#                  holds the raster's scratch on the stack, so the redzone
#                  multiplier lands on a frame that is already large. 16MB is
#                  the headroom, not a measurement; raise it again rather than
#                  reading "stack overflow in ev_render" as a finding, because
#                  in this lane it is an artifact of the instrumentation.
#   STACK_OVERFLOW_CHECK=2  a wasm stack overflow otherwise walks silently into
#                  static data, which is itself memory corruption and would be
#                  attributed to whatever it landed on.
#
# ASAN/UBSAN below are this lane's knobs (ENABLE_ASAN is the native side's, and
# does not reach emcc). Both default on: the pair is what makes a report
# trustworthy, and each catches what the other cannot.
ASAN  ?= 1
UBSAN ?= 1
EM_SAN_OPT ?= -O1 -g2 -mnontrapping-fptoint $(EM_SIMD)
# `undefined` minus the two classes toridraw trips deliberately: its fixed-point
# code left-shifts negative values and relies on signed wrap in the span
# kernels, so leaving those in fires first and buries every other report.
# src/Makefile excludes the same two, for the same reason. Pass
# UBSAN_FLAGS='-fsanitize=undefined' when arithmetic is what is suspected.
UBSAN_FLAGS ?= -fsanitize=undefined -fno-sanitize=shift,signed-integer-overflow

EM_SAN_CFLAGS := $(if $(filter 1,$(ASAN)),-fsanitize=address,) \
	$(if $(filter 1,$(UBSAN)),$(UBSAN_FLAGS),)

ifneq ($(filter wasm-san web/ev_wasm_san.js,$(MAKECMDGOALS)),)
ifeq ($(ASAN)$(UBSAN),00)
$(error wasm-san with ASAN=0 and UBSAN=0 builds an UNinstrumented module under \
the sanitized name — a clean run would then mean nothing. Pick at least one)
endif
endif

# A flavour stamp. ASAN=1 and ASAN=1 UBSAN=1 write the same file, and make
# compares timestamps rather than flags -- so without this, a second build with
# different sanitizers is a no-op that reports success while the browser keeps
# running the previous instrumentation. This tool has already lost time twice to
# a stale artifact (see the header-dependency note above); a silently
# uninstrumented module is the same failure with a worse conclusion attached.
EM_SAN_STAMP := $(BUILD)/.wasm_san_flavour
EM_SAN_FLAVOUR := asan=$(ASAN) ubsan=$(UBSAN) simd=$(SIMD) opt=$(EM_SAN_OPT) ub=$(UBSAN_FLAGS)

.PHONY: wasm-san FORCE
FORCE:

$(EM_SAN_STAMP): FORCE | $(BUILD)
	@printf '%s\n' '$(EM_SAN_FLAVOUR)' > $@.tmp
	@cmp -s $@.tmp $@ || mv -f $@.tmp $@
	@rm -f $@.tmp

wasm-san: web/ev_wasm_san.js

web/ev_wasm_san.js: $(EM_SRCS) $(EM_HEADERS) $(EM_SAN_STAMP)
	$(EMCC) $(EM_SAN_OPT) $(EM_SAN_CFLAGS) -std=c11 -w $(INCLUDES) \
		-I$(REPO)/v0 \
		-o $@ $(EM_SRCS) \
		$(EM_LINK) \
		-sINITIAL_MEMORY=268435456 -sASSERTIONS=1 \
		-sSTACK_SIZE=16777216 -sSTACK_OVERFLOW_CHECK=2

-include $(wildcard $(BUILD)/*.d)

clean:
	rm -rf $(BUILD) ev_catalog ev_server ev_swing web/ev_wasm.js web/ev_wasm.wasm \
		web/ev_wasm_san.js web/ev_wasm_san.wasm
