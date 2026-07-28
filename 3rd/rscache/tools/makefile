# 3rd/rscache/tools — find_anims + port_npc + port_lostcity
#
#   make -C 3rd/rscache tools
#   make -C 3rd/rscache/tools
#   make -C 3rd/rscache/tools clean

CC      ?= cc
REPO    := $(abspath ../../..)
RSCACHE := $(abspath ..)
THIRD   := $(abspath ../..)
TOOLS   := $(abspath .)

INCLUDES := -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) \
	-I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas -I$(THIRD)/ini \
	-I$(TOOLS)/common

CFLAGS  ?= -std=c11 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign
CFLAGS  += $(INCLUDES)
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

# Header dependency tracking. Without it a stale object keeps the struct layout
# it was compiled against while the rest of the build moves on, and every field
# past the changed one is read at the wrong offset — which reads as a decoder
# bug, not a build one.
DEPFLAGS := -MMD -MP

BUILD := $(TOOLS)/build

COMMON_SRCS := \
	$(TOOLS)/common/tool_profile.c \
	$(TOOLS)/common/asset_access.c \
	$(TOOLS)/common/anim_affinity.c \
	$(TOOLS)/common/port_plan.c \
	$(TOOLS)/common/cache_write.c \
	$(TOOLS)/common/transcode.c

COMMON_OBJS := $(patsubst $(TOOLS)/common/%.c,$(BUILD)/common_%.o,$(COMMON_SRCS))

LIB_OBJS := \
	$(BUILD)/rscache_unity.o \
	$(BUILD)/bzip.o \
	$(BUILD)/bzip_encode.o \
	$(BUILD)/miniz.o \
	$(BUILD)/xteas.o \
	$(BUILD)/ini.o

.PHONY: all clean find_anims find_named anim_compare port_npc port_lostcity packfile cs2 poser-gl
.DEFAULT_GOAL := all

# poser-gl is not in `all`: it needs SDL2 and an OpenGL 3.3 context, which the
# other tools do not, and a machine without SDL2 should still build the rest.
# `make poser-gl` builds it.
all: find_anims find_named anim_compare port_npc port_lostcity packfile cs2

find_anims: $(TOOLS)/find_anims/find_anims
find_named: $(TOOLS)/find_named/find_named
anim_compare: $(TOOLS)/anim_compare/anim_compare
port_npc: $(TOOLS)/port_npc/port_npc
port_lostcity: $(TOOLS)/port_lostcity/port_lostcity
packfile: $(TOOLS)/packfile/packfile

LC_SRCS := \
	$(TOOLS)/port_lostcity/main.c \
	$(TOOLS)/port_lostcity/lc_pack.c \
	$(TOOLS)/port_lostcity/lc_manifest.c \
	$(TOOLS)/port_lostcity/lc_out.c \
	$(TOOLS)/port_lostcity/lc_export.c \
	$(TOOLS)/port_lostcity/lc_map.c \
	$(TOOLS)/port_lostcity/lc_texture.c

# `packfile` patches the same `content/pack` files the exporter allocates ids in,
# so it links that reader rather than growing a second one that could disagree
# about what a pack line means.
PACKFILE_SRCS := \
	$(TOOLS)/packfile/main.c \
	$(TOOLS)/port_lostcity/lc_pack.c

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rscache_unity.o: $(RSCACHE)/rscache_unity.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ini.o: $(THIRD)/ini/ini.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/common_%.o: $(TOOLS)/common/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(BUILD)/*.d

$(TOOLS)/find_anims/find_anims: $(TOOLS)/find_anims/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TOOLS)/find_named/find_named: $(TOOLS)/find_named/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TOOLS)/anim_compare/anim_compare: $(TOOLS)/anim_compare/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TOOLS)/port_npc/port_npc: $(TOOLS)/port_npc/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(TOOLS)/port_lostcity/port_lostcity: $(LC_SRCS) $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -I$(TOOLS)/port_lostcity -o $@ $^ -lm

# No cache is opened here — pack files are text — so this links neither the
# rscache unity nor the codecs.
$(TOOLS)/packfile/packfile: $(PACKFILE_SRCS)
	$(CC) $(CFLAGS) -I$(TOOLS)/port_lostcity -o $@ $^

clean:
	rm -rf $(BUILD) $(TOOLS)/find_anims/find_anims $(TOOLS)/find_named/find_named $(TOOLS)/anim_compare/anim_compare $(TOOLS)/port_npc/port_npc \
		$(TOOLS)/port_lostcity/port_lostcity $(TOOLS)/packfile/packfile $(TOOLS)/cs2/cs2 \
		$(TOOLS)/poser-gl-c/poser-gl

# cs2 — the CS2 decompiler/compiler front end. Links the library only; the
# language layer lives inside the unity TU.
cs2: $(TOOLS)/cs2/cs2

$(TOOLS)/cs2/cs2: $(TOOLS)/cs2/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# poser-gl — the animation editor. SDL2 + OpenGL 3.3 core.
#
# GL entry points are resolved at runtime through SDL_GL_GetProcAddress, so
# nothing links against a GL library here; on macOS the framework still has to be
# named for SDL's own use.
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

POSER_SRCS := \
	$(TOOLS)/poser-gl-c/main.c \
	$(TOOLS)/poser-gl-c/pg_math.c \
	$(TOOLS)/poser-gl-c/pg_gl.c \
	$(TOOLS)/poser-gl-c/pg_font.c \
	$(TOOLS)/poser-gl-c/pg_model.c \
	$(TOOLS)/poser-gl-c/pg_cache.c \
	$(TOOLS)/poser-gl-c/pg_anim.c \
	$(TOOLS)/poser-gl-c/pg_render.c \
	$(TOOLS)/poser-gl-c/pg_gizmo.c \
	$(TOOLS)/poser-gl-c/pg_gui.c \
	$(TOOLS)/poser-gl-c/pg_app.c \
	$(TOOLS)/poser-gl-c/pg_ui.c \
	$(TOOLS)/poser-gl-c/pg_transfer.c

poser-gl: $(TOOLS)/poser-gl-c/poser-gl

$(TOOLS)/poser-gl-c/poser-gl: $(POSER_SRCS) $(COMMON_OBJS) $(LIB_OBJS)
ifeq ($(strip $(SDL2_LIBS)),)
	@echo "poser-gl: sdl2-config not found — install SDL2 and retry" >&2; exit 1
endif
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -I$(TOOLS)/poser-gl-c -o $@ $^ $(SDL2_LIBS) -lm
