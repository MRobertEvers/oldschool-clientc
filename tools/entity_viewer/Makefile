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
all: ev_catalog ev_server
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

ev_catalog: $(BUILD)/ev_catalog.o $(BUILD)/lc_pack.o $(COMMON_OBJS) $(DEP_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

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

$(BUILD)/torirs_model_from_rscache.o: $(REPO)/src/engine/torirs_model_from_rscache.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/toridraw_model_from_torirs.o: $(REPO)/src/engine/toridraw_model_from_torirs.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/toridraw_animation_from_rscache.o: $(REPO)/src/engine/toridraw_animation_from_rscache.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ev_server.o: ev_server.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

ev_server: $(BUILD)/ev_server.o $(BUILD)/ev_build.o $(BUILD)/ev_wire.o \
		$(BUILD)/ev_render.o $(BUILD)/torirs_types.o \
		$(BUILD)/torirs_model_from_rscache.o $(BUILD)/toridraw_model_from_torirs.o \
		$(BUILD)/toridraw_animation_from_rscache.o \
		$(BUILD)/toridraw_unity.o $(BUILD)/toridraw_font.o \
		$(BUILD)/toridraw_model_sprite.o $(COMMON_OBJS) $(DEP_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# ---- browser: toridraw only, to WebAssembly ------------------------------
#
# No rscache and no filesystem here. `-sMODULARIZE` gives the page an EVModule()
# factory to await, which is what keeps ev.js free of load-order guesswork.

EMCC ?= emcc
EM_EXPORTS := _ev_w_init,_ev_w_alloc,_ev_w_release,_ev_w_set_model,_ev_w_set_anim,_ev_w_clear_anim,_ev_w_frame_count,_ev_w_frame_delay,_ev_w_render,_ev_w_model_height,_ev_w_last_cull,_malloc,_free

wasm: web/ev_wasm.js

web/ev_wasm.js: ev_wasm.c ev_render.c ev_wire.c $(TORIDRAW)/toridraw_unity.c
	$(EMCC) -O2 -std=c11 -w $(INCLUDES) \
		-I$(REPO)/v0 \
		-o $@ ev_wasm.c ev_render.c ev_wire.c $(TORIDRAW)/toridraw_unity.c \
		$(TORIDRAW)/toridraw_font.c \
		-sMODULARIZE=1 -sEXPORT_NAME=EVModule \
		-sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
		-sEXPORTED_FUNCTIONS='[$(EM_EXPORTS)]' \
		-sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8"]'

clean:
	rm -rf $(BUILD) ev_catalog ev_server web/ev_wasm.js web/ev_wasm.wasm
