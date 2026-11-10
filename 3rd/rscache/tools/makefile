# 3rd/rscache/tools — find_anims + port_npc + port_lostcity
#
#   make -C 3rd/rscache tools
#   make -C 3rd/rscache/tools
#   make -C 3rd/rscache/tools clean

CC      ?= cc
# Windows: one spelling of the drive letter. $(abspath) keeps the case the
# caller's working directory was spelled with (`c:/` from one terminal, `C:/`
# from the map editor's RUN), every -I below is made of these four, and the
# objverify signature further down is made of the -I's: the other spelling
# called every object here stale and rebuilt cachepack. src/makefile does the
# same to its REPO_ROOT, for the same reason.
ifeq ($(OS),Windows_NT)
win_drive_upper = $(or $(strip $(foreach d,a/A b/B c/C d/D e/E f/F g/G h/H i/I j/J k/K l/L m/M \
	n/N o/O p/P q/Q r/R s/S t/T u/U v/V w/W x/X y/Y z/Z,$(patsubst $(firstword $(subst /, ,$d)):/%,$(lastword \
	$(subst /, ,$d)):/%,$(filter $(firstword $(subst /, ,$d)):/%,$1)))),$1)
else
win_drive_upper = $1
endif
REPO    := $(call win_drive_upper,$(abspath ../../..))
RSCACHE := $(call win_drive_upper,$(abspath ..))
THIRD   := $(call win_drive_upper,$(abspath ../..))
TOOLS   := $(call win_drive_upper,$(abspath .))

INCLUDES := -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) \
	-I$(THIRD)/bmp -I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas -I$(THIRD)/ini \
	-I$(TOOLS)/common

# -O3, not -O0. These tools are not debugged more often than they are run: a
# verify walks 173,002 config records and an asset pass 11,857 archives, and at
# -O0 that is slow enough to look hung. `-g` stays, so a crash is still
# debuggable, and the vendored deps were already built -O3 (DEP_CFLAGS below) —
# only the tools' own code was unoptimised.
#
# Override with `make CFLAGS=...` when stepping through something.
CFLAGS  ?= -std=c11 -g -O3 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign
CFLAGS  += $(INCLUDES)
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

# glibc under -std=c11 hides POSIX (strdup, getline, fdopen, ...). An undeclared
# strdup is implicitly `int`, truncates the pointer, and cachepack segfaults on
# its first lane name. macOS exposes them regardless; src/platform/platform.mk
# gives the client the same flag on Linux. Appended, so `make CFLAGS=...` keeps it.
#
# Windows sets $(OS) itself, so it is told apart without a shell: every
# $(shell) here is a sh.exe started at parse time, 0.1-0.2 s each on Windows,
# and the map editor's RUN parses this file once per build
# (src/maped/live-editor.mk, maped-run-tools).
ifneq ($(OS),Windows_NT)
ifeq ($(shell uname -s),Linux)
CFLAGS     += -D_GNU_SOURCE
DEP_CFLAGS += -D_GNU_SOURCE
endif
endif

# The linker writes cachepack.exe on Windows. A rule named for `cachepack`
# finds no such file, relinks on every make, and under the editor's RUN
# (src/maped/live-editor.mk, -j2) relinks while the other branch runs it,
# which Windows refuses ("cannot open output file"). The targets the src
# makefile runs carry the suffix it names them by (CACHEPACK_BIN, CS2_BIN).
ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

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
	$(TOOLS)/common/transcode.c \
	$(TOOLS)/common/cs2_db_columns.c

COMMON_OBJS := $(patsubst $(TOOLS)/common/%.c,$(BUILD)/common_%.o,$(COMMON_SRCS))

LIB_OBJS := \
	$(BUILD)/rscache_unity.o \
	$(BUILD)/bzip.o \
	$(BUILD)/bzip_encode.o \
	$(BUILD)/miniz.o \
	$(BUILD)/bmp.o \
	$(BUILD)/xteas.o \
	$(BUILD)/ini.o

.PHONY: all clean find_anims find_named anim_compare port_npc port_lostcity map_port packfile cs2 cachepack fontbake spritebake audioprobe poser-gl
.DEFAULT_GOAL := all

# poser-gl is not in `all`: it needs SDL2 and an OpenGL 3.3 context, which the
# other tools do not, and a machine without SDL2 should still build the rest.
# `make poser-gl` builds it.
all: find_anims find_named anim_compare port_npc port_lostcity map_port packfile cs2 cachepack fontbake spritebake audioprobe

find_anims: $(TOOLS)/find_anims/find_anims
find_named: $(TOOLS)/find_named/find_named
anim_compare: $(TOOLS)/anim_compare/anim_compare
port_npc: $(TOOLS)/port_npc/port_npc
port_lostcity: $(TOOLS)/port_lostcity/port_lostcity
map_port: $(TOOLS)/map_port/map_port
packfile: $(TOOLS)/packfile/packfile
fontbake: $(TOOLS)/fontbake/fontbake
spritebake: $(TOOLS)/spritebake/spritebake
audioprobe: $(TOOLS)/audioprobe/audioprobe

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

# `cachepack` reuses lc_pack.c for the same reason packfile does: a `<type>.pack`
# file means one thing, and two readers of it would eventually disagree.
CACHEPACK_SRCS := \
	$(TOOLS)/cachepack/main.c \
	$(TOOLS)/cachepack/cp_text.c \
	$(TOOLS)/cachepack/cp_keys.c \
	$(TOOLS)/cachepack/cp_types.c \
	$(TOOLS)/cachepack/cp_names.c \
	$(TOOLS)/cachepack/cp_register.c \
	$(TOOLS)/cachepack/cp_fields.c \
	$(TOOLS)/cachepack/cp_membership.c \
	$(TOOLS)/cachepack/cp_common.c \
	$(TOOLS)/cachepack/cp_value.c \
	$(TOOLS)/cachepack/cp_import.c \
	$(TOOLS)/cachepack/cp_unpack.c \
	$(TOOLS)/cachepack/cp_pack.c \
	$(TOOLS)/cachepack/cp_codec.c \
	$(TOOLS)/cachepack/cp_binary.c \
	$(TOOLS)/cachepack/cp_pack_ident.c \
	$(TOOLS)/cachepack/cp_assets.c \
	$(TOOLS)/cachepack/cp_walk.c \
	$(TOOLS)/cachepack/cp_merge.c \
	$(TOOLS)/cachepack/cp_decode.c \
	$(TOOLS)/cachepack/cp_incremental.c \
	$(TOOLS)/cachepack/config/cp_flo.c \
	$(TOOLS)/cachepack/config/cp_var.c \
	$(TOOLS)/cachepack/config/cp_small.c \
	$(TOOLS)/cachepack/config/cp_idk.c \
	$(TOOLS)/cachepack/config/cp_seq.c \
	$(TOOLS)/cachepack/config/cp_npc.c \
	$(TOOLS)/cachepack/config/cp_obj.c \
	$(TOOLS)/cachepack/config/cp_loc.c \
	$(TOOLS)/cachepack/config/cp_db.c \
	$(TOOLS)/port_lostcity/lc_pack.c

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rscache_unity.o: $(RSCACHE)/rscache_unity.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/bmp.o: $(THIRD)/bmp/bmp.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/ini.o: $(THIRD)/ini/ini.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/common_%.o: $(TOOLS)/common/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Those .d files say which objects a header reaches; they cannot say whether an
# object was actually built from the source on disk. Make settles that by
# mtime, and mtimes here are not trustworthy — a tree copy stamps sources and
# objects alike, leaving an object that is "current" by timestamp and stale by
# content. These tools bake the cache the client then loads, so a stale one
# writes a wrong cache rather than failing. See tools/objverify.py for the
# incident that produced this.
OBJVERIFY := $(abspath $(TOOLS)/../../..)/tools/objverify.py
OBJVERIFY_REPORT := $(shell command -v python3 >/dev/null 2>&1 && \
	python3 '$(OBJVERIFY)' '$(BUILD)' --sig '$(CC)|$(CFLAGS)|$(DEP_CFLAGS)' 2>&1)
$(if $(OBJVERIFY_REPORT),$(info $(OBJVERIFY_REPORT)))

-include $(BUILD)/*.d

$(TOOLS)/find_anims/find_anims: $(TOOLS)/find_anims/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

$(TOOLS)/find_named/find_named: $(TOOLS)/find_named/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

$(TOOLS)/anim_compare/anim_compare: $(TOOLS)/anim_compare/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

$(TOOLS)/port_npc/port_npc: $(TOOLS)/port_npc/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

# fontbake writes a C file, so it needs no renderer — only the cache decoders.
$(TOOLS)/fontbake/fontbake: $(TOOLS)/fontbake/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

# spritebake — fontbake's sibling: cache sprites to compiled-in ARGB arrays.
# bmp.o already rides in the common objects, for the --sheet contact sheet.
$(TOOLS)/spritebake/spritebake: $(TOOLS)/spritebake/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

# audioprobe — the audio tables, read as bytes. See its header for why it exists.
$(TOOLS)/audioprobe/audioprobe: $(TOOLS)/audioprobe/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

$(TOOLS)/port_lostcity/port_lostcity: $(LC_SRCS) $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -I$(TOOLS)/port_lostcity -o $@ $^ -lm -lpthread

# Profile-aware whole-square transcoder. Unlike cachepack's same-revision map
# codec this joins RS2's separately named mX_Z/lX_Z archives, rewrites scenery
# ids through an import ledger and emits modern LostCity jm2/jl2 source.
$(TOOLS)/map_port/map_port: $(TOOLS)/map_port/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

# No cache is opened here — pack files are text — so this links neither the
# rscache unity nor the codecs.
$(TOOLS)/packfile/packfile: $(PACKFILE_SRCS)
	$(CC) $(CFLAGS) -I$(TOOLS)/port_lostcity -o $@ $^

cachepack: $(TOOLS)/cachepack/cachepack$(EXE)

# cachepack keys its incremental state on a hash of its own executable
# (cp_incremental.c, executable_hash), and a PE link stamps the wall clock
# into the header: a relink of unchanged code was "a new binary", and every
# editor lane's next build repacked every type (30 s instead of 2). A zero
# stamp makes two links of the same objects the same bytes (checked
# 2026-11-10: two links, cmp equal).
ifeq ($(OS),Windows_NT)
CACHEPACK_LDFLAGS := -Wl,--no-insert-timestamp
else
CACHEPACK_LDFLAGS :=
endif

$(TOOLS)/cachepack/cachepack$(EXE): $(CACHEPACK_SRCS) $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -I$(TOOLS)/cachepack -I$(TOOLS)/port_lostcity -o $@ $^ -lm -lpthread $(CACHEPACK_LDFLAGS)

clean:
	rm -rf $(BUILD) $(TOOLS)/find_anims/find_anims $(TOOLS)/find_named/find_named $(TOOLS)/anim_compare/anim_compare $(TOOLS)/port_npc/port_npc \
		$(TOOLS)/port_lostcity/port_lostcity $(TOOLS)/map_port/map_port $(TOOLS)/packfile/packfile $(TOOLS)/cs2/cs2 \
		$(TOOLS)/cachepack/cachepack $(TOOLS)/fontbake/fontbake $(TOOLS)/spritebake/spritebake $(TOOLS)/audioprobe/audioprobe \
		$(TOOLS)/poser-gl-c/poser-gl

# cs2 — the CS2 decompiler/compiler front end. Links the library only; the
# language layer lives inside the unity TU.
cs2: $(TOOLS)/cs2/cs2$(EXE)

$(TOOLS)/cs2/cs2$(EXE): $(TOOLS)/cs2/main.c $(COMMON_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -lpthread

# poser-gl — the animation editor. SDL2 + OpenGL 3.3 core.
#
# GL entry points are resolved at runtime through SDL_GL_GetProcAddress, so
# nothing links against a GL library here; on macOS the framework still has to be
# named for SDL's own use.
#
# Asked only by a make that was asked for poser-gl (nothing else here uses SDL,
# and `all` does not build it): the two sdl2-config runs at parse time were
# 0.4 s of every `make cachepack` on Windows, where there is none to find.
ifneq ($(filter poser-gl %/poser-gl,$(MAKECMDGOALS)),)
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif

POSER_SRCS := \
	$(TOOLS)/poser-gl-c/main.c \
	$(TOOLS)/poser-gl-c/pg_math.c \
	$(TOOLS)/poser-gl-c/pg_gl.c \
	$(TOOLS)/poser-gl-c/pg_font.c \
	$(TOOLS)/poser-gl-c/pg_model.c \
	$(TOOLS)/poser-gl-c/pg_rig.c \
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
