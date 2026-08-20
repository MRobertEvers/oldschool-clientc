# From repo root: make -C src all
# Optimized build: make -C src release (or make -C src OPT=1 <target>).
# Objects go to build_opt/ so debug and optimized objects never mix; both
# flavors link the same ./torirs, whichever was built last.
#
# Which host to build for is one variable, PLATFORM (see platform/platform.mk):
#   make -C src all             host native debug -> src/torirs
#   make -C src release         host native opt   -> src/torirs
#   make -C src web             emscripten        -> build-web/torirs.js
#   make -C src winxp           Windows XP (i686) -> src/torirs.exe
#   make -C src win64           Windows 10+ (x64) -> src/torirs_win64.exe
#   make -C src io-server       the web build's cache backend (always native)
#   make -C src lane-check-all  assert every lane's invariants still hold
# Every (PLATFORM, OPT[, TORIDRAW_OPT, MEMTRACE, EMBED_SERVER]) tuple owns its
# own OBJ_DIR, so flavors never share .o's.
#
# TORIDRAW_OPT=1 compiles the Soft3D/ToriDraw unity at -O2 while the rest of
# the client stays at OPT's level (default -O0). Same idea as COMPRESS_CFLAGS
# (always -O3) and tommath.o (always -O2): hot kernels get their own flag
# without forcing a full-client release build. The harness uses this so a
# -O0 client can still hit 50 fps. Needs its own OBJ_DIR suffix — mixing
# toridraw_unity.o built at -O0 with one at -O2 is silently wrong.

# Optimised by default. The debug flavor is a deliberate ask (OPT=0, or the
# per-lane `*-debug` targets), not the thing you get by forgetting a flag: an
# -O0 client is slow enough to change what you are looking at, and every
# iteration loop here — boot, load an arena, capture a frame — pays for it.
# The two flavors still keep separate OBJ_DIRs, so switching costs a rebuild
# rather than a silently mixed one.
OPT ?= 1
# MEMTRACE=1 links platform/torirs_memtrace.c, which interposes the C heap and
# writes every alloc/free with a call stack to memtrace.bin. It is a third build
# flavor with its own OBJ_DIR (the interpose needs -fno-builtin-* on every unit,
# so its objects must never mix with a plain build's).
#   make -C src MEMTRACE=1              native, traced   -> src/torirs
#   make -C src MEMTRACE=1 web          web, traced      -> build-web/torirs.js
# See tools/memtrace/README.md.
MEMTRACE ?= 0
# ENABLE_ASAN=1 is a distinct object/link flavor. On macOS it also selects a
# static SDL link and the dyld interpose dylib required by LLVM #182943.
ENABLE_ASAN ?= 0
# ENABLE_UBSAN=1 instruments ToriDraw only, and only for the two checks that
# matter to the rasterizer: signed overflow and out-of-range shifts. Scoped to
# ToriDraw because a whole-client -fsanitize=undefined buries the signal, and
# scoped to those two checks because they are the ones that silently produce a
# wrong pixel rather than a crash. Errors abort rather than recover, so the
# first one names its file and line.
#
# Pair it with TORIDRAW_NO_SIMD=1: UBSan does not instrument vector types or
# NEON/SSE intrinsics at all, so an overflowing lane in a *_simd.*.u.c kernel is
# invisible to it. The scalar fallback is plain C and is checked normally.
ENABLE_UBSAN ?= 0
# TORIDRAW_NO_SIMD=1 forces the portable scalar projection kernels on every
# host. A rendering difference between this and the default build localises a
# defect to the SIMD kernels.
TORIDRAW_NO_SIMD ?= 0
# TORIDRAW_OPT=1: Soft3D/ToriDraw at -O2 regardless of OPT. Default 0.
#   make -C src TORIDRAW_OPT=1 EMBED_SERVER=1 torirs
TORIDRAW_OPT ?= 0
# EMBED_SERVER=1 links the rev-230 server into the client. Its own OBJ_DIR
# suffix (_es) so switching embed ↔ plain never reuses a net_transport_embed.o
# compiled without -DTORIRS_EMBED_SERVER=1 (that symptom is a silent stub).
EMBED_SERVER ?= 0

# platform_check.mk is included before the build rules below, so without an
# explicit default GNU make adopts that include's first target (lane-check).
# A bare `make` must build the client described at the top of this file.
.DEFAULT_GOAL := all

REPO_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR      := $(REPO_ROOT)/src

# Every recipe here is POSIX sh (rm -rf, mkdir -p, cp, `for` loops). On Windows
# GNU make looks for sh.exe on PATH and falls back to cmd.exe when it cannot
# find one; SHELL is then left as the unresolved literal `sh.exe`, and the first
# recipe dies with "p was unexpected at this time" -- which says nothing about
# the actual cause. Catch it here instead. Git for Windows ships a usable sh at
# <Git>\usr\bin; the Windows PowerShell wrappers put it on PATH for you.
ifeq ($(SHELL),sh.exe)
$(error no POSIX sh on PATH — this makefile needs one (Git for Windows ships \
one at "C:\Program Files\Git\usr\bin"). Add it to PATH, or use \
build_winxp.ps1/build_windows.ps1, which do it for you)
endif

include $(SRC_DIR)/platform/platform.mk
# Lane invariant assertions (`make lane-check`). Separate from platform.mk so
# that file stays a pure set of declarations.
include $(SRC_DIR)/platform/platform_check.mk

CC := $(PLATFORM_CC)
ifeq ($(MEMTRACE),1)
MEMTRACE_SUFFIX = _mt
else
MEMTRACE_SUFFIX =
endif
ifeq ($(ENABLE_ASAN),1)
ASAN_SUFFIX = _asan
else
ASAN_SUFFIX =
endif
ifeq ($(ENABLE_UBSAN),1)
UBSAN_SUFFIX = _ubsan
else
UBSAN_SUFFIX =
endif
ifeq ($(TORIDRAW_NO_SIMD),1)
NOSIMD_SUFFIX = _nosimd
else
NOSIMD_SUFFIX =
endif
ifeq ($(TORIDRAW_OPT),1)
TORIDRAW_OPT_SUFFIX = _tdo
else
TORIDRAW_OPT_SUFFIX =
endif
ifeq ($(EMBED_SERVER),1)
EMBED_SERVER_SUFFIX = _es
else
EMBED_SERVER_SUFFIX =
endif
ifeq ($(OPT),1)
OBJ_DIR = $(PLATFORM_OBJ_BASE)_opt$(MEMTRACE_SUFFIX)$(ASAN_SUFFIX)$(UBSAN_SUFFIX)$(NOSIMD_SUFFIX)$(TORIDRAW_OPT_SUFFIX)$(EMBED_SERVER_SUFFIX)
O_LEVEL = -O3
else
OBJ_DIR = $(PLATFORM_OBJ_BASE)$(MEMTRACE_SUFFIX)$(ASAN_SUFFIX)$(UBSAN_SUFFIX)$(NOSIMD_SUFFIX)$(TORIDRAW_OPT_SUFFIX)$(EMBED_SERVER_SUFFIX)
O_LEVEL = -O0
endif
# Debug and release deliberately share ./torirs, but a traced binary must not
# join them: MEMTRACE is a long-running capture, and `make -C src all` in
# another terminal would silently replace it mid-session — the symptom being a
# run that writes no trace at all. Which lanes can afford the rename is
# PLATFORM_TARGET_MEMTRACE_SUFFIX in platform.mk (the web target keeps its
# name, which the page's <script src> is written against).
ifeq ($(MEMTRACE),1)
TARGET = $(PLATFORM_TARGET)$(PLATFORM_TARGET_MEMTRACE_SUFFIX)
else
TARGET = $(PLATFORM_TARGET)
endif
# The compiler for tools that run on THIS machine — cachepack, the port
# helpers, anything under 3rd/*/tools. It is deliberately not $(CC).
#
# $(CC) is the *target* compiler: on the win32 lane that is the i686 XP
# toolchain, and building cachepack with it produced a 32-bit tool — which is
# the one program here that has to chew through a whole cache, and the reason a
# bake ran for tens of minutes. A host tool has no reason to be built for the
# target; XP never runs it.
#
# Preference order: the toolchain vendored at toolchain/mingw64 (see
# tools/toolchain/README.md), then a 64-bit cross-compiler on PATH, then
# whatever the lane uses — so this degrades to the old behaviour rather than
# failing on a machine with neither.
TOOLCHAIN_MINGW64_CC := $(REPO_ROOT)/toolchain/mingw64/bin/gcc.exe
HOST_CC ?= $(strip \
    $(if $(wildcard $(TOOLCHAIN_MINGW64_CC)),$(TOOLCHAIN_MINGW64_CC), \
    $(if $(shell command -v x86_64-w64-mingw32-gcc 2>/dev/null),x86_64-w64-mingw32-gcc, \
    $(CC))))

# Sub-make into a vendored tool tree. Those makefiles default to `CC ?= cc`,
# which exists on macOS and Linux but not in a MinGW toolchain (gcc only), so
# `make -C src mock230-servpack` on Windows died in the child with a bare
# "The system cannot find the file specified". `?=` there means this wins
# without the vendored files needing to change.
SUBMAKE = $(MAKE) CC=$(HOST_CC)

# Host tools are invoked by path, so their executable suffix has to be spelled
# out -- see PLATFORM_EXE_SUFFIX in platform.mk for why the shell's .exe
# fallback is not good enough here.
CACHEPACK_BIN = $(RSCACHE_DIR)/tools/cachepack/cachepack$(PLATFORM_EXE_SUFFIX)
AUDIOPROBE_BIN = $(RSCACHE_DIR)/tools/audioprobe/audioprobe$(PLATFORM_EXE_SUFFIX)
CS2_BIN = $(RSCACHE_DIR)/tools/cs2/cs2$(PLATFORM_EXE_SUFFIX)

RSCACHE_DIR  := $(REPO_ROOT)/3rd/rscache
RSPROT_DIR   := $(REPO_ROOT)/3rd/rsprot
TORIDRAW_DIR := $(REPO_ROOT)/3rd/toridraw
TRSPK_DIR    := $(REPO_ROOT)/3rd/trspk
BMP_DIR      := $(REPO_ROOT)/3rd/bmp
HMAP_DIR     := $(REPO_ROOT)/3rd/hmap
RSAREABUF_DIR := $(REPO_ROOT)/3rd/rsareabuf

# -Werror=incompatible-pointer-types: passing a `T**` where a `T*` is wanted is
# not a style question, it is a wrong pointer that compiles. It bit twice while
# the widget structs were being changed from inline members to lazily-allocated
# ones — every `&c->field` site silently became one type deeper. One of them,
# `menu_has_ops(&node->menu_options)`, dropped 107 components out of the UI tree
# and said nothing; it was found by diffing `torirs_stats` against the previous
# binary, which is not a thing anyone should have to do to catch a type error.
# Scoped rather than a blanket -Werror: the tree still carries unused-variable
# and sign-compare warnings that are not worth failing a build over.
CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -Wall -Wextra \
	-Werror=incompatible-pointer-types -g -MMD -MP \
	-I$(SRC_DIR) -I$(SRC_DIR)/cs2vm2 -I$(SRC_DIR)/serverscript \
	-I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src \
	-I$(TORIDRAW_DIR) -I$(TRSPK_DIR) -I$(BMP_DIR) -I$(HMAP_DIR) -I$(RSAREABUF_DIR) \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas \
	$(RSPROT_HDR_INCLUDES)

# Just the two directories holding rsprot's headers, for the main build.
# Deliberately NOT the full $(RSPROT_INCLUDES): that set also carries -I3rd and
# -I$(SRC_DIR)/net so the library can resolve its own vendored deps, and putting
# those on every translation unit in the project would let an unrelated header
# resolve somewhere new. The net stack needs the buffer, not the search path.
#
# Defined with `=` and used above before it is defined, which make allows for
# recursive variables — CFLAGS is expanded at use, not here.
RSPROT_HDR_INCLUDES = -I$(RSPROT_DIR)/src -I$(RSPROT_DIR)/include -I$(RSPROT_DIR)

RSCACHE_CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -g -MMD -MP \
	-I$(RSCACHE_DIR)/include \
	-I$(RSCACHE_DIR)/src \
	-I$(RSCACHE_DIR) \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

# rsprot (protocol codecs) is a unity TU like rscache. Its include list is the
# authoritative one from 3rd/rsprot/makefile's INCLUDES, rewritten absolute:
# its own include/src/gen/root, plus src/net for the ISAAC/RSA implementations
# it adapts rather than re-ports, 3rd for tommath/xteas, and the repo root
# because src/net/rsa.h includes tommath as "3rd/tommath/tommath.h".
# rsprot's own makefile is where its warnings are visible; here it gets -w like
# everything else under 3rd/.
#
# Two TUs, not one, and the split is load-bearing -- see the header comment in
# rsprot_packets_unity.c. rsprot_unity.o is the self-contained library;
# rsprot_packets_unity.o is the codec set, which only links where an executor
# (src/net/exec/pkt_exec.c) is also linked.
# Factored out because three rules need it: the two rsprot unity TUs (via
# RSPROT_CFLAGS) and pkt_exec.o, which builds under the main CFLAGS -- it is
# this project's source -- but has to find rsprot_exec.h and rsprot_buf.h.
RSPROT_INCLUDES = \
	-I$(RSPROT_DIR)/include \
	-I$(RSPROT_DIR)/src \
	-I$(RSPROT_DIR)/gen \
	-I$(RSPROT_DIR) \
	-I$(SRC_DIR)/net \
	-I$(REPO_ROOT)/3rd \
	-I$(REPO_ROOT) \
	-I$(REPO_ROOT)/3rd/xteas

RSPROT_CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -g -MMD -MP \
	$(RSPROT_INCLUDES)

COMPRESS_CFLAGS = -std=c11 -O3 -fno-omit-frame-pointer -g -MMD -MP \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

# Soft3D/ToriDraw: TORIDRAW_OPT=1 forces -O2 even when the rest of the client
# is -O0 (perf harness). When OPT=1 the whole tree is already -O3, so leave it.
ifeq ($(TORIDRAW_OPT),1)
TORIDRAW_O_LEVEL = -O2
else
TORIDRAW_O_LEVEL = $(O_LEVEL)
endif
TORIDRAW_CFLAGS = -std=c11 $(TORIDRAW_O_LEVEL) -fno-omit-frame-pointer -g -MMD -MP \
	-I$(TORIDRAW_DIR) \
	-I$(BMP_DIR)

# Toolchain, SDL and link flags come from the selected platform block. The
# base subset also reaches the vendored units: a platform whose libc hides
# strdup behind a feature macro hides it from 3rd/ too.
CFLAGS += $(PLATFORM_CFLAGS)
RSCACHE_CFLAGS  += $(PLATFORM_BASE_CFLAGS)
RSPROT_CFLAGS   += $(PLATFORM_BASE_CFLAGS)
COMPRESS_CFLAGS += $(PLATFORM_BASE_CFLAGS)
TORIDRAW_CFLAGS += $(PLATFORM_BASE_CFLAGS)
LDFLAGS = $(PLATFORM_LDFLAGS) $(PLATFORM_STRIP_LDFLAGS)

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
RSCACHE_CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
RSPROT_CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
COMPRESS_CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
TORIDRAW_CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
LDFLAGS += -fsanitize=address
ifeq ($(PLATFORM),macos)
ASAN_DYLD_SHIM := $(OBJ_DIR)/asan_dyld_shim.dylib
ASAN_COMPAT_SRC := platform/asan_compat.c
endif
endif

ifeq ($(TORIDRAW_NO_SIMD),1)
# The selection ladder in graphics/projection16_simd.u.c (and its zdiv twin)
# picks a kernel from the host's ISA macros unless the matching *_DISABLED is
# defined. Define all of them and every host lands on the scalar file.
TORIDRAW_CFLAGS += -DNEON_DISABLED=1 -DAVX2_DISABLED=1 -DSSE41_DISABLED=1 \
	-DSSE2_DISABLED=1 -DSSE_DISABLED=1
endif

ifeq ($(ENABLE_UBSAN),1)
# Deliberately not `shift`: ToriDraw's fixed-point code left-shifts negative
# values in several places (toridraw_lighting.c:170 among them), which is UB by
# the letter of the standard but wraps as intended on every target here. Those
# reports fire first and hide the arithmetic ones. UBSAN_CHECKS is overridable
# on the command line when that class is what is being investigated.
UBSAN_CHECKS ?= signed-integer-overflow
TORIDRAW_CFLAGS += -fsanitize=$(UBSAN_CHECKS) -fno-sanitize-recover=$(UBSAN_CHECKS)
# Only ToriDraw is instrumented, but the runtime still has to be linked in.
LDFLAGS += -fsanitize=undefined
endif

ASAN_DYLD_SHIM ?=

# --- Heap instrumentation (MEMTRACE=1) -----------------------------------
# -fno-builtin-* has to reach every translation unit, not just the tracer: the
# compiler is otherwise free to fold a malloc/free pair away or lower calloc to
# its own inline sequence, and those allocations then never reach the hooks.
# How the hooks are reached differs per platform (dlsym interposition on macOS,
# the linker's --wrap elsewhere, a shorter wrap list on emscripten), so that is
# PLATFORM_MEMTRACE_WRAP_LDFLAGS in platform.mk rather than a test here.
# ASan hooks the same functions, so the two cannot be combined.
ifeq ($(MEMTRACE),1)
ifeq ($(ENABLE_ASAN),1)
$(error MEMTRACE=1 and ENABLE_ASAN=1 both hook malloc — pick one)
endif
MEMTRACE_CFLAGS := -DTORIRS_MEMTRACE=1 \
	-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
CFLAGS          += $(MEMTRACE_CFLAGS)
RSCACHE_CFLAGS  += $(MEMTRACE_CFLAGS)
RSPROT_CFLAGS   += $(MEMTRACE_CFLAGS)
COMPRESS_CFLAGS += $(MEMTRACE_CFLAGS)
TORIDRAW_CFLAGS += $(MEMTRACE_CFLAGS)
LDFLAGS += $(PLATFORM_MEMTRACE_WRAP_LDFLAGS)
endif

# platform/platform_sdl2*.c is shared (emscripten ports SDL2); the IO, audio
# and 3D-renderer backends are per-platform and come in via $(PLATFORM_SRCS).
SRCS = main.c executor_config.c app.c $(PLATFORM_WINDOW_SRC) \
	platform/platform_sdl2_renderer_soft3d.c \
	$(ASAN_COMPAT_SRC) \
	$(PLATFORM_SRCS) \
	$(JS5_SRCS) \
	render/torirs_frame.c render/torirs_pick.c \
	render/trspk_toridraw.c render/trspk_sprite.c \
	input/torirs_input.c input/torirs_keymap.c input/torirs_input_cmd.c \
	game/task_cs2_script_exec.c $(BMP_DIR)/bmp.c \
	cs2vm2/cs2vm2.c cs2vm2/cs2vm2_script.c cs2vm2/cs2vm2_trigger_args.c \
	cs2vm2/cs2vm2_strpool.c \
	cs1vm/cs1vm.c cs1vm/cs1vm_script.c \
	game/rs_player_stats.c game/rs_cs1_host.c game/task_cs1_run.c \
	game/rs_hitsplat.c engine/dat2/task_dat2_hitsplat_load.c \
	game/rs_healthbar.c engine/dat2/task_dat2_healthbar_load.c \
	game/rs_soundscape.c engine/dat2/task_dat2_soundscape_load.c \
	game/rs_ui_slots.c game/rs_social.c game/rs_loot_store.c game/rs_clientcode.c game/rs_if1_buttons.c \
	game/rs_idk_design.c \
	game/rs_chat.c game/rs_chat_widgets.c game/rs_gameproto_exec.c \
	game/rs_worldmap.c \
	game/rs_worldmap_render.c \
	cs2vm2/cs2_opcode_meta.c engine/cache_provider.c engine/proctex/proctex_generator.c engine/cs2vm2_script_from_rscache.c \
	engine/cs2_opcode_dialect.c \
	engine/torirs_lru.c \
	engine/torirs_model_inst_cache.c \
	engine/torirs_types.c engine/torirs_model_from_rscache.c \
	engine/torirs_component_from_rscache.c \
	engine/torirs_objtype_from_rscache.c engine/torirs_npctype_from_rscache.c \
	engine/torirs_spotanimtype_from_rscache.c \
	engine/torirs_sound_from_rscache.c \
	engine/torirs_idk_from_rscache.c \
	engine/torirs_map_from_rscache.c engine/torirs_location_from_rscache.c \
	engine/torirs_flotype_from_rscache.c \
	engine/torirs_sprite_from_rscache.c engine/torirs_font_from_rscache.c \
	engine/torirs_enum_from_rscache.c engine/torirs_struct_from_rscache.c \
	engine/torirs_paramtype_from_rscache.c engine/torirs_db.c \
	engine/torirs_worldmap_from_rscache.c \
	engine/png_decode.c \
	engine/uitree_from_component.c engine/texture_palette_bake.c \
	engine/dat1/dat1_buildcache.c engine/dat1/task_dat1_model_load.c \
	engine/dat1/task_dat1_component_pack_load.c engine/dat1/task_dat1_clientscript_load.c \
	engine/dat1/task_dat1_obj_load.c engine/dat1/task_dat1_npc_load.c \
	engine/dat1/task_dat1_spotanim_load.c engine/dat1/task_dat1_sound_load.c \
	engine/dat1/task_dat1_idk_load.c \
	engine/dat1/task_dat1_component_load.c \
	engine/dat1/task_dat1_sprite_load.c engine/dat1/task_dat1_font_load.c \
	engine/dat1/task_dat1_map_load.c engine/dat1/task_dat1_loc_load.c \
	engine/dat1/task_dat1_flotype_load.c engine/dat1/task_dat1_texture_load.c \
	engine/dat1/task_dat1_sequence_load.c \
	engine/dat2/dat2_buildcache.c engine/dat2/dat2_group_cache.c \
	engine/dat2/task_dat2_model_load.c \
	engine/dat2/task_dat2_component_pack_load.c engine/dat2/task_dat2_clientscript_load.c \
	engine/dat2/task_dat2_obj_load.c engine/dat2/task_dat2_npc_load.c \
	engine/dat2/task_dat2_spotanim_load.c engine/dat2/task_dat2_sound_load.c \
	engine/dat2/task_dat2_idk_load.c \
	engine/dat2/task_dat2_map_load.c engine/dat2/task_dat2_loc_load.c \
	engine/dat2/task_dat2_flotype_load.c engine/dat2/task_dat2_underlay_load.c \
	engine/dat2/task_dat2_texture_load.c \
	engine/dat2/task_dat2_sprite_load.c engine/dat2/task_dat2_font_load.c \
	engine/dat2/task_dat2_enum_load.c engine/dat2/task_dat2_struct_load.c \
	engine/dat2/task_dat2_param_load.c \
	engine/dat2/task_dat2_varbit_load.c engine/dat2/task_dat2_varp_load.c \
	engine/dat1/task_dat1_varbit_load.c \
	engine/dat2/task_dat2_dbrow_load.c engine/dat2/task_dat2_dbindex_load.c \
	engine/dat2/task_dat2_dbtable_load.c \
	engine/dat2/task_dat2_component_load.c \
	engine/dat2/task_dat2_worldmap_load.c \
	engine/dat2/task_dat2_mapelement_load.c \
	engine/dat2/task_dat2_worldmap_geography_load.c \
	engine/uitree_builder/uitree_builder.c \
	engine/uitree_builder/uitree_builder_manifest.c \
	engine/uitree_builder/uitree_builder_bake.c \
	engine/uitree_builder/uitree_builder_inv.c \
	engine/uitree_builder/task_uibuilder_assets_load.c \
	engine/uitree_builder/task_uitree_build.c \
	engine/uitree_builder/task_pack_assets_load.c \
	engine/uitree_builder/task_interface_open.c \
	engine/uitree_builder/task_slot_mount.c \
	engine/task_obj_model_load.c \
	engine/uitree_scene_bridge.c \
	engine/static_sprites.c \
	engine/uitree_builder/task_static_sprites_load.c \
	engine/player_appearance.c \
	engine/toridraw_animation_from_rscache.c \
	engine/dat2/task_dat2_sequence_load.c \
	engine/uitree_cmd_render.c \
	engine/toridraw_sprite_from_torirs.c \
	engine/toridraw_font_from_torirs.c \
	engine/toridraw_model_from_torirs.c \
	game/rs_cs2_host.c game/task_cs2_run.c game/rs_cs2_dispatch.c game/cs2_harness.c game/rs_minimenu_build.c \
	game/rs_minimenu_world.c \
	game/task_gameproto_exec.c game/task_exec_entity_info.c game/rs_entity_sync.c game/rs_audio.c \
	game/rs_prefs.c game/task_prefs_io.c \
	audio/torirs_pcm.c audio/torirs_mixer.c audio/torirs_midi_file.c \
	audio/torirs_soundbank.c audio/torirs_midi_synth.c audio/torirs_music.c \
	engine/dat2/task_dat2_music_load.c \
	engine/uitree_anim.c engine/entity_model_build.c \
	ui/uitree.c ui/uitree_hook.c ui/uitree_component_options.c engine/torirs_component_hook.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
	ui/uitree_input.c ui/uitree_hover.c ui/uitree_scroll.c ui/uitree_emit.c \
	ui/uitree_inv_view.c ui/uitree_obj_cell.c ui/uitree_cross.c ui/uitree_minimenu.c \
	ui/uitree_hovertext.c ui/uitree_debug_overlay.c \
	engine/torirs_debug_font_baked.c \
	ui/uitree_interact.c ui/uitree_iface_stats.c \
	varp/varp_manager.c \
	varc/varc_manager.c \
	inv/inv_manager.c \
	cmd/cmdring.c cmd/cmdbus.c \
	net/isaac.c net/rsa.c net/ringbuf.c net/jbase37.c net/wordpack.c \
	net/loginproto.c net/packetbuffer.c net/net.c net/net_out.c net/bitbuffer.c \
	net/rev/gameproto_parse.c net/rev/packets/pkt_player_info.c net/rev/packets/pkt_npc_info.c \
	net/rev/packets/pkt_player_appearance.c net/rev/packets/pkt_rebuild_normal.c \
	net/rev/packets/pkt_rebuild_region.c \
	net/rev/gameproto_rev_params.c net/rev/rsprot_bridge.c \
	net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c \
	bootmanifest/bootmanifest.c features/features.c \
	platform/sockstream.c platform/platform_socket.c \
	platform/net_transport_tcp.c platform/net_transport_ws.c \
	platform/net_transport_ws_frame.c platform/net_transport_ws_handshake.c \
	platform/net_transport_embed.c \
	net/loginproto_xrsps.c net/loginproto_osrs230.c \
	net/loginproto_osrs239.c net/sha256.c net/pow_sha256.c \
	net/rev/xrsps233/gameproto_rev_xrsps233.c \
	net/rev/osrs230/gameproto_rev_osrs230.c net/rev/osrs230/osrs230_parse.c \
	net/rev/osrs239/gameproto_rev_osrs239.c net/rev/osrs239/osrs239_parse.c \
	net/rev/osrs239/osrs239_entity_info.c \
	revconfig/revconfig.c revconfig/revconfig_load.c \
	world/entity_pool.c world/world_entity.c world/entity_pathing.c \
	world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
	engine/world_builder/task_world_load.c \
	editor/editor.c editor/editor_doc.c editor/editor_jm2.c \
	editor/editor_cmd.c editor/editor_derive.c editor/editor_host_local.c \
	editor/editor_panel.c \
	painters/painters.c \
	painters/painters_cull_project.c \
	painters/scene_occluders.c \
	perf/torirs_perf.c
ifeq ($(MEMTRACE),1)
SRCS += platform/torirs_memtrace.c
endif

# Defined up here, above both the EMBED_SERVER block and the $(TARGET) rule.
# $(TARGET): $(OBJS) expands its prerequisites when the makefile is *read*,
# while its recipe expands them when it *runs* — so a definition placed after
# that rule links objects it never listed as prerequisites, and the build fails
# with "no such file or directory: build/ssvm.o" for things it never compiled.
SS_SRCS = serverscript/ss_meta.c serverscript/ssvm_script.c \
	serverscript/ssvm_provider.c serverscript/ssvm_strpool.c serverscript/ssvm.c \
	$(RSAREABUF_DIR)/rsareabuf.c

MOCK230_CORE_SRCS = net/mock/mock230_world.c \
	net/mock/mock230_cox_sim.c \
	net/mock/mock230_encode.c net/mock/mock230_wire.c js5/server/js5_server_cache.c \
	net/mock/mock230_interface_state.c \
	net/mock/mock230_poh.c \
	net/mock/mock239_playerinfo.c \
	net/mock/mock239_interface_setters.c \
	net/mock/mock239_varp.c \
	net/mock/mock239_interface_inbound.c \
	net/mock/mock239_runclientscript.c \
	net/mock/mock239_inbound.c \
	net/mock/mock230_objinfo.c \
	net/mock/mock230_split.c \
	net/mock/mock230_runenergy.c \
	net/mock/mock230_healthbarinfo.c \
	net/mock/mock230_npcinfo.c net/mock/mock230_combat.c net/mock/mock230_seqinfo.c \
	net/mock/mock230_scripts.c net/mock/mock230_ws.c net/mock/mock230_content.c \
	net/mock/mock230_servercodec.c net/mock/mock230_servpack.c \
	net/mock/mock230_db.c \
	net/mock/mock230_paramtable.c \
	net/mock/mock230_locinfo.c net/mock/mock230_structinfo.c \
	net/mock/mock230_dbinfo.c \
	net/mock/mock230_ops_db.c \
	net/mock/mock230_ops_param.c \
	net/mock/mock230_ops_loc.c \
	net/mock/mock230_ops_npc.c \
	net/mock/mock230_ops_obj.c \
	net/mock/mock230_ops_inv.c \
	net/mock/mock230_ops_player.c \
	net/mock/mock230_ops_poh.c \
	net/mock/mock230_bank.c net/mock/mock230_bank_tables.c net/mock/mock230_container.c net/mock/mock230_equipment.c \
	net/mock/mock230_shop.c \
	net/mock/mock230_friends.c \
	net/mock/mock230_ids.c \
	net/mock/mock230_boot.c net/mock/mock230_save.c net/mock/mock230_transport.c net/mock/mock230_session.c \
	net/mock/mock230_embed.c content/content_register.c content/content_fields.c $(INI_SRC) \
	net/mock/mock230_scene.c net/mock/mock230_loc_ops.c net/mock/mock230_mapinstance.c \
	net/mock/mock230_zone.c net/mock/mock230_zone_index.c net/mock/mock230_worldmap.c net/mock/mock230_varbit.c \
	engine/world_builder/collision_map.c features/features.c \
	platform/net_transport_ws_frame.c platform/net_transport_ws_handshake.c $(SS_SRCS)

# EMBED_SERVER=1: link the rev-230 server into the client, so a manifest saying
# `[net:boot] transport=embed` runs the whole game in one process with no socket.
#
# Opt-in rather than default, and the reason is scope: it puts the server's tick,
# its script VM and its content loaders inside the *client* binary, and makes the
# client's build depend on a content tree it otherwise never reads. That is the
# wrong default for a client and the right one for anyone who wants one process.
#
# Without it, selecting the embed transport fails loudly rather than silently
# falling back to TCP — see platform/net_transport_embed.c.
ifeq ($(EMBED_SERVER),1)
CFLAGS += -DTORIRS_EMBED_SERVER=1
# These units are already in the client SRCS or have dedicated object rules
# below.  Adding them again generates duplicate recipes for the same flat
# OBJ_DIR basename (and, depending on rule order, can select the wrong recipe).
EMBED_SERVER_DUP_SRCS = features/features.c \
	platform/net_transport_ws_frame.c \
	platform/net_transport_ws_handshake.c \
	engine/world_builder/collision_map.c \
	$(RSAREABUF_DIR)/rsareabuf.c \
	$(INI_SRC)
SRCS += $(filter-out $(EMBED_SERVER_DUP_SRCS),$(MOCK230_CORE_SRCS))
EMBED_SERVER_OBJS = $(OBJ_DIR)/rsareabuf.o
endif
INI_SRC = $(REPO_ROOT)/3rd/ini/ini.c
RSCACHE_UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
RSPROT_UNITY_SRC = $(RSPROT_DIR)/rsprot_unity.c
RSPROT_PACKETS_UNITY_SRC = $(RSPROT_DIR)/rsprot_packets_unity.c
TORIDRAW_UNITY_SRC = $(TORIDRAW_DIR)/toridraw_unity.c
TRSPK_UNITY_SRC = $(TRSPK_DIR)/trspk_unity.c
# Objects derived from SRCS (one .o per source, flat in OBJ_DIR) plus the
# out-of-tree/unity extras that have their own rules below. The world_builder
# pipeline ($(WB_OBJS), incl. wb_minimap.o — the Minimap world.h is written
# against) links into the app; $(sort) dedupes objects shared with WB_OBJS.
# Which GPU-binding objects to link is the platform's call, not this file's --
# platform.mk names them (PLATFORM_GPU_OBJ_NAMES) and OBJ_DIR is prefixed here
# because it does not exist yet when that file is read. The previous "native or
# else" test silently gave win32, which has no GPU path at all, the WebGL one.
GPU_OBJS = $(addprefix $(OBJ_DIR)/,$(PLATFORM_GPU_OBJ_NAMES))
OBJS = $(sort $(addprefix $(OBJ_DIR)/,$(notdir $(SRCS:.c=.o))) \
	$(OBJ_DIR)/ini.o $(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o \
	$(OBJ_DIR)/hmap.o $(EMBED_SERVER_OBJS) \
	$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
	$(OBJ_DIR)/rsprot_unity.o $(OBJ_DIR)/rsprot_buf.o $(OBJ_DIR)/rsprot_packets_unity.o \
	$(OBJ_DIR)/pkt_exec.o \
	$(OBJ_DIR)/trspk_unity.o $(GPU_OBJS) \
	$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/toridraw_model_sprite.o \
	$(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
	$(OBJ_DIR)/tommath.o \
	$(WB_OBJS))

# World builder pipeline objects (compiled as standalone objects; world_builder.c is a single
# translation unit that #includes its .u.c units). Built via the `world-builder` target.
WB_DIR = engine/world_builder
WB_OBJS = $(OBJ_DIR)/world_builder.o \
	$(OBJ_DIR)/blendmap.o $(OBJ_DIR)/overlaymap.o $(OBJ_DIR)/lightmap.o \
	$(OBJ_DIR)/shademap.o $(OBJ_DIR)/sharelight_map.o $(OBJ_DIR)/terrain_shapemap.o \
	$(OBJ_DIR)/contour_ground.o $(OBJ_DIR)/decor_buildmap.o $(OBJ_DIR)/occluder_buildmap.o \
	$(OBJ_DIR)/world_decode_tile.o \
	$(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o $(OBJ_DIR)/wb_minimap.o \
	$(OBJ_DIR)/toridraw_model_from_torirs.o

# Make does not otherwise notice compiler/linker flag changes: an object built
# with old lane flags could be silently relinked after its ABI contract
# changed. Keep each Windows lane's effective flags in a timestamped
# prerequisite and update it only when their contents actually change.
ifneq ($(filter $(PLATFORM),win32 win64),)
WINDOWS_FLAGS_STAMP := $(OBJ_DIR)/.effective_flags
.PHONY: FORCE_WINDOWS_FLAGS
FORCE_WINDOWS_FLAGS:

$(WINDOWS_FLAGS_STAMP): FORCE_WINDOWS_FLAGS | $(OBJ_DIR)
	@{ printf '%s\n' '$(CC)'; printf '%s\n' '$(CFLAGS)'; printf '%s\n' '$(LDFLAGS)'; } > '$@.tmp'
	@if ! cmp -s '$@.tmp' '$@'; then mv '$@.tmp' '$@'; else rm -f '$@.tmp'; fi

$(OBJS): $(WINDOWS_FLAGS_STAMP)
endif

.PHONY: test-font-markup test-seal-rig mock230-cache-check mock230-bank test-mock230-bank mock230-alt test-mock230-alt mock230-dev test-mock230-dev test-cache-trim all release web web-debug web-idb web-idb-debug winxp winxp-debug win64 win64-debug clean-lane io-server servers test-io-wire test-js5 test-js5-server js5-server mock-js5 clean run mock230 test-sound test-mock230 test-mock230-poh test-poh-servants check-crystal-set-contract check-godwars-manifest check-godwars-contract check-quest-combat-manifest check-quest-combat-contract check-charter-contract check-tob-pillar-contract test-construction-catalog test-poh-quest-status bench-uitree test-rsareabuf test-varp test-npctype-multi test-varc test-loot-store test-inv test-world test-uitree test-uitree-builder test-uitree-builder-dat1 test-revconfig test-bootmanifest test-light-model test-animation-object-step test-scene-profiles test-ws-frame test-world-builder test-minimap test-cs1vm test-cs1 test-cmdbus test-net-login test-net-loopback test-net-exec test-ui-slots test-walkmerge test-entity-decode check-no-drain test-task-order test-cs2-frame-settle test-db test-cs2-math test-cs2-string test-cs2-component-param test-cs2-text-align test-cs2-triggerop test-cs2-options test-cs2-dialect world-builder test-ss-meta test-ss-corpus test-ss-roundtrip test-ss-vm test-ss-verify test-ss-provider test-ssc sscompile mock230-scripts mock230-scripts-summoning mock230-scripts-curses mock230-servpack mock230-cache mock230-pack test-social test-cs2-transmit-pump test-minimenu-world test-entity-info-shrink test-minimap-dots test-painters-occluders test-pick-level test-painters-ground-decor test-summoning-cs2 test-summoning-isolation test-summoning-import test-summoning-byte-identity mock230-cache-summoning mock230-cache-curses mock230-cache-all mock230-scripts-summoning-curses test-curses-cs2 test-curses-isolation test-summoning-phase1 test-summoning-phase3 test-summoning-phase4 test-summoning-phase4c mock230-cache-rs2012-summoning
.PHONY: test-server-clean test-painters-terrain-levels test-mock239-inbound test-mock239-runclientscript test-mock239-interface-setters test-mock239-varp test-mock230-interface-state rsprot test-rsprot test-pktexec test-pktpackets test-rsprot-bridge test-net-out-resume test-midi-packets test-cs2-target-verb test-cs2-world-click-responsiveness test-summoning-phase4d test-summoning-phase4e test-summoning-phase4f test-summoning-phase5a test-summoning-phase5b test-summoning-phase5c-corpus test-summoning-scroll-assets test-summoning-pet-guide-assets test-summoning-phase5b-runtime test-summoning-phase6 test-summoning-phase6-cache test-summoning-phase7d test-summoning-phase7f summoning-texture-review
.PHONY: test-summoning-specials test-summoning-source-provenance test-summoning-interact test-summoning-leash test-summoning-instance summoning-dialogue
.PHONY: check-dks-contract check-boss-contract
.PHONY: check-gauntlet-contract test-poh-menagerie test-poh-league-hall test-poh-house-styles
.PHONY: mock230-scripts-lanes mock230-lanes

all: $(OBJ_DIR) $(TARGET)

release:
	$(MAKE) OPT=1 all

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Debug and release share ./torirs; .last_flavor records which flavor linked
# it so switching flavors forces a relink even when all objects are current.
ifneq ($(shell cat .last_flavor 2>/dev/null),$(OBJ_DIR))
$(TARGET): FORCE
FORCE:
endif

$(OBJ_DIR)/asan_dyld_shim.dylib: platform/asan_dyld_shim.c | $(OBJ_DIR)
	$(CC) -dynamiclib -fblocks -DTORIRS_ENABLE_ASAN=1 \
		-install_name @loader_path/$(notdir $(OBJ_DIR))/asan_dyld_shim.dylib \
		-o $@ $<

# The web lane alone can put ~300 object paths on this line, and emcc.bat is a
# batch file -- invoking it routes through cmd.exe's own argument buffer, whose
# practical ceiling (~8191 chars) is far below CreateProcess's real 32767, so a
# link line that would run fine calling gcc.exe directly still dies here with
# the literal, unhelpful "The command line is too long." $(OBJS) is what makes
# the line big (a few hundred plain paths, ~9KB, versus ~2KB for everything
# else combined) and, unlike $(CFLAGS)/$(LDFLAGS), it is safe to move into a
# GCC/Clang/emcc response file with zero escaping: paths, no spaces, no quotes.
#
# Writing that file with a recipe-line `printf ... > file` just traded one
# oversized command line for another: mingw32-make's own recipe-to-shell
# handoff silently dropped the trailing `> file` off a line that long, so the
# object list spilled to stdout instead of landing in the response file, and
# the very next line failed with "response file not found". $(file >...) is
# GNU Make 4.0+'s own builtin for this -- it writes the file from inside make
# itself, no shell process and no command line involved, so there is nothing
# left for any OS argument-length limit to truncate.
#
# $(file ...) really is 4.0+ only, and macOS still ships GNU Make 3.81 as
# /usr/bin/make. There the call expands to nothing WITHOUT an error, so the
# response file is simply never written and the link dies on
# "no such file or directory: @build.../link_objs.rsp". Only the constrained
# lanes (cmd.exe, mingw32-make) ever needed the indirection, so on an older
# make just put the objects back on the link line -- the platforms that ship
# 3.81 have a megabyte-plus ARG_MAX and never had the problem.
# firstword(sort) is the usual "is MAKE_VERSION >= 4.0" test: sort is lexical
# and dedupes, so 4.0 stays first only when the running make is 4.0 or newer.
ifeq ($(firstword $(sort 4.0 $(MAKE_VERSION))),4.0)
LINK_OBJS_ARG = @$(OBJ_DIR)/link_objs.rsp
else
LINK_OBJS_ARG = $(OBJS)
endif

$(TARGET): $(OBJS) $(ASAN_DYLD_SHIM)
	@mkdir -p $(dir $@)
	$(file >$(OBJ_DIR)/link_objs.rsp,$(OBJS))
	$(CC) $(CFLAGS) -o $@ $(LINK_OBJS_ARG) $(LDFLAGS) $(ASAN_DYLD_SHIM)
	@echo $(OBJ_DIR) > .last_flavor

# --- Web target ----------------------------------------------------------
# `make -C src web` builds the emscripten flavor and copies the JS host
# harness next to the module, so build-web/ is a complete servable directory.
# The IO server (below) serves it, and answers the client's cache reads.
#
# With MEMTRACE=1 the standalone trace viewer is copied in beside them, so the
# .bin the page downloads can be opened without leaving build-web/.
WEB_ASSETS = $(SRC_DIR)/web/index.html $(SRC_DIR)/web/torirs_host.js
ifeq ($(MEMTRACE),1)
WEB_ASSETS += $(REPO_ROOT)/tools/memtrace/viewer.html
endif
# --- Servers -------------------------------------------------------------
# The two processes a browser run needs, built together because needing both
# and being told about one is how a web build ends up half-runnable. They stay
# separate executables — they have different jobs, different lifetimes and
# different exposure — but there is one command that produces them.
#
#   io_server    HTTP: the page, the module, GET /boot/<path>, POST /io
#   js5_server   the cache, over raw TCP or WebSocket on one port
#
# Both are always native, even from a web build. See docs/WEB_SERVERS.md.
.PHONY: servers
servers:
	$(MAKE) io-server
	$(MAKE) js5-server
	@echo "servers: src/build/io_server and src/$(OBJ_DIR)/js5_server"

web: servers
	$(MAKE) PLATFORM=web OPT=1 CACHE=$(CACHE) MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/
	@echo "built $(REPO_ROOT)/build-web/torirs.js — run it with:"
	@echo "  ./src/build/io_server --manifest <boot.ini> --root build-web"

web-debug: servers
	$(MAKE) PLATFORM=web OPT=0 CACHE=$(CACHE) MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/

# --- Web target, IndexedDB cache -----------------------------------------
# The same module with its cache underneath it instead of across a socket:
# archive records in IndexedDB behind a dat2 facade, filled over JS5. The page
# is then self-contained — it needs a JS5 server and a static file server, and
# no io_server at all.
#
# Separate targets rather than a flag on `web` because the two link different
# IO backends and therefore must not share an object directory; platform.mk
# gives this lane its own (build_web_idb).
web-idb: servers
	$(MAKE) PLATFORM=web OPT=1 CACHE=idb MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/
	@echo "built $(REPO_ROOT)/build-web/torirs.js (IndexedDB cache) — run it with:"
	@echo "  ./src/$(OBJ_DIR)/js5_server --cache <cache_dir> --revision <rev> &"
	@echo "  ./src/build/io_server --root build-web --boot-root ."

web-idb-debug: servers
	$(MAKE) PLATFORM=web OPT=0 CACHE=idb MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/

# --- Windows XP target ---------------------------------------------------
# `make -C src winxp` is the XP lane's entry point, shaped like `web` above so
# every platform is reached the same way. It needs an i686 MinGW toolchain on
# PATH; build_winxp.ps1 is the wrapper that puts one there and stages the
# result. lane-check runs first so a wrong-architecture toolchain fails here
# rather than as an unhelpful loader error on the XP box.
winxp:
	$(MAKE) PLATFORM=win32 lane-check
	$(MAKE) PLATFORM=win32 OPT=1 all

winxp-debug:
	$(MAKE) PLATFORM=win32 lane-check
	$(MAKE) PLATFORM=win32 OPT=0 all

# --- Modern Windows target -----------------------------------------------
# The wrapper selects the repository's x86_64 compiler and stages the unique
# source artifact. Keeping this lane explicit also prevents a native Windows
# invocation from accidentally inheriting the XP ABI floor.
win64:
	$(MAKE) PLATFORM=win64 lane-check
	$(MAKE) PLATFORM=win64 OPT=1 all

win64-debug:
	$(MAKE) PLATFORM=win64 lane-check
	$(MAKE) PLATFORM=win64 OPT=0 all

# --- IO server -----------------------------------------------------------
# The web client has no disk: every ToriRS_IOItem it would have satisfied
# locally is answered by this process, which opens the cache and runs the
# SAME PlatformX_IO_LoadItem the native build uses. Always built native, even
# when PLATFORM=web, so `make -C src web io-server` produces both halves.
IO_SERVER_OBJ_DIR = build
# The path is fixed — `src/build/io_server` is what docs/WEB_SERVERS.md and the
# echoes above name — so the sub-make has to land in the *unsuffixed* native
# objdir, and every variable that appends a flavour suffix to OBJ_DIR is pinned
# off here. It is a sub-make of a build that may be any flavour: `make -C src
# OPT=1 web` runs `servers` first, the OPT=1 propagated through MAKEFLAGS, and
# OBJ_DIR became build_opt while this path stayed `build` — so nothing created
# `build` and nothing had a rule for `build/rscache_unity.o` either. On a tree
# that had one from an earlier debug build it linked stale objects; on a clean
# one it died with "No rule to make target `build'".
#
# OBJ_DIR=$(IO_SERVER_OBJ_DIR) below is the same fix applied to the OTHER thing
# that can move OBJ_DIR off `build`: PLATFORM_OBJ_BASE itself. PLATFORM=native
# resolves to whatever this host is, and that stem is only literally `build` on
# macOS (platform.mk) — win32 is build_win32, win64 is build_win64. Without
# this, a Windows build hit the identical symptom the OPT=1 case above already
# describes, just from PLATFORM_OBJ_BASE instead of a suffix: the rscache_unity.o
# rule lands under build_win64/ while this path still asks for build/.
IO_SERVER_FLAVOR = PLATFORM=native OPT=0 MEMTRACE=0 ENABLE_ASAN=0 ENABLE_UBSAN=0 \
	TORIDRAW_NO_SIMD=0 TORIDRAW_OPT=0 EMBED_SERVER=0 OBJ_DIR=$(IO_SERVER_OBJ_DIR)
# features/features.c: BootManifest_LoadFile resolves `[features:boot]` names
# through it, so a server that reads a manifest needs it too — without it the
# link fails on ToriRS_Features_ByName.
IO_SERVER_SRCS = ioserver/io_server_main.c ioserver/http_server.c \
	platform/io_wire.c platform/platform_x_io.c \
	bootmanifest/bootmanifest.c features/features.c $(INI_SRC)
# http_server.c talks Winsock directly (see JS5_SOCKET_LIBS below for the same
# split on sockstream.c's side) -- closesocket/WSAStartup/select/etc live in
# ws2_32, not the CRT, so a native Windows link needs it named explicitly.
ifeq ($(OS),Windows_NT)
IO_SERVER_SOCKET_LIBS := -lws2_32
else
IO_SERVER_SOCKET_LIBS :=
endif
io-server:
	$(MAKE) $(IO_SERVER_FLAVOR) $(IO_SERVER_OBJ_DIR)/io_server
# The objdir is order-only: it is a prerequisite so that it exists, and every
# object written into it bumps its mtime, which as a normal prerequisite relinked
# the server on every unrelated build.
$(IO_SERVER_OBJ_DIR)/io_server: $(IO_SERVER_SRCS) \
		$(IO_SERVER_OBJ_DIR)/rscache_unity.o $(IO_SERVER_OBJ_DIR)/bzip.o \
		$(IO_SERVER_OBJ_DIR)/bzip_encode.o $(IO_SERVER_OBJ_DIR)/miniz.o \
		$(IO_SERVER_OBJ_DIR)/xteas.o | $(IO_SERVER_OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/ioserver -o $@ $(IO_SERVER_SRCS) \
		-DTORIRS_PLATFORM_X_IO_NO_JS5=1 \
		$(IO_SERVER_OBJ_DIR)/rscache_unity.o $(IO_SERVER_OBJ_DIR)/bzip.o \
		$(IO_SERVER_OBJ_DIR)/bzip_encode.o $(IO_SERVER_OBJ_DIR)/miniz.o \
		$(IO_SERVER_OBJ_DIR)/xteas.o -lm $(IO_SERVER_SOCKET_LIBS)
	@echo "built $@"

# IO wire codec round-trip: encode a request/response pair for every item kind
# and assert the decoded item matches field for field.
test-io-wire: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -DTORIRS_PLATFORM_X_IO_NO_JS5=1 -o $(OBJ_DIR)/io_wire_test \
		platform/io_wire.c platform/platform_x_io.c platform/test/io_wire_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	./$(OBJ_DIR)/io_wire_test $(DAT1_CACHE) "$(DAT2_CACHE)" "$(DAT2_REV)"

# Protocol + persistence integration against a caller-supplied, read-only
# canonical cache. The binary creates and owns its disposable target directory;
# only the mock response builder reads JS5_TEST_CACHE.
JS5_TEST_CACHE ?= $(REPO_ROOT)/cache.osrs239
ifeq ($(OS),Windows_NT)
JS5_SOCKET_LIBS := -lws2_32
JS5_EXE_SUFFIX := .exe
else
JS5_SOCKET_LIBS :=
JS5_EXE_SUFFIX :=
endif
JS5_TEST_PYTHON ?= python
test-js5: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/js5_test \
		js5/js5.c js5/js5_rscache.c platform/sockstream.c \
		js5/server/js5_server_cache.c js5/test/js5_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(JS5_SOCKET_LIBS)
	./$(OBJ_DIR)/js5_test "$(JS5_TEST_CACHE)"

test-js5-server: js5-server $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/js5_server_test \
		js5/server/js5_server_session.c js5/server/js5_server_cache.c \
		js5/server/test/js5_server_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/js5_server_client_test \
		js5/js5.c js5/js5_rscache.c platform/sockstream.c \
		js5/server/test/js5_server_client_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(JS5_SOCKET_LIBS)
	./$(OBJ_DIR)/js5_server_test "$(JS5_TEST_CACHE)"
	$(JS5_TEST_PYTHON) $(REPO_ROOT)/tools/test_js5_server.py \
		--server $(SRC_DIR)/$(OBJ_DIR)/js5_server$(JS5_EXE_SUFFIX) \
		--client $(SRC_DIR)/$(OBJ_DIR)/js5_server_client_test$(JS5_EXE_SUFFIX) \
		--cache "$(JS5_TEST_CACHE)"

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

NET_REV_SRCS = net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c \
	net/rev/xrsps233/gameproto_rev_xrsps233.c net/rev/osrs230/gameproto_rev_osrs230.c \
	net/rev/osrs239/gameproto_rev_osrs239.c

# rsprot_bridge.c is excluded for the same reason the rev tables are: it has an
# explicit rule below carrying extra include paths, and letting the generic
# rule also claim it makes make warn about overriding commands.
$(foreach src,$(filter-out painters/painters.c net/rev/rsprot_bridge.c $(NET_REV_SRCS),$(SRCS)),$(eval $(call make-goal,$(src))))

$(OBJ_DIR)/painters.o: painters/painters.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -c painters/painters.c -o $@

# Rev protocol tables include their own dir for packetin.h/packetout.h.
$(OBJ_DIR)/gameproto_rev_lc245_2.o: net/rev/lc245_2/gameproto_rev_lc245_2.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_lc254.o: net/rev/lc254/gameproto_rev_lc254.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc254 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_xrsps233.o: net/rev/xrsps233/gameproto_rev_xrsps233.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/xrsps233 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_osrs230.o: net/rev/osrs230/gameproto_rev_osrs230.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/osrs230 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_osrs239.o: net/rev/osrs239/gameproto_rev_osrs239.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/osrs239 -c $< -o $@

# tommath amalgam for RSA (login block exptmod).
$(OBJ_DIR)/tommath.o: $(REPO_ROOT)/3rd/tommath/mpi.c | $(OBJ_DIR)
	$(CC) -std=c11 -O2 -w -I$(REPO_ROOT)/3rd/tommath -c $< -o $@

# --- World builder pipeline object rules ---------------------------------
$(OBJ_DIR)/world_builder.o: $(WB_DIR)/world_builder.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/world_builder.c -o $@
$(OBJ_DIR)/blendmap.o: $(WB_DIR)/blendmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/blendmap.c -o $@
$(OBJ_DIR)/overlaymap.o: $(WB_DIR)/overlaymap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/overlaymap.c -o $@
$(OBJ_DIR)/lightmap.o: $(WB_DIR)/lightmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/lightmap.c -o $@
$(OBJ_DIR)/shademap.o: $(WB_DIR)/shademap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/shademap.c -o $@
$(OBJ_DIR)/sharelight_map.o: $(WB_DIR)/sharelight_map.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/sharelight_map.c -o $@
$(OBJ_DIR)/terrain_shapemap.o: $(WB_DIR)/terrain_shapemap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/terrain_shapemap.c -o $@
$(OBJ_DIR)/contour_ground.o: $(WB_DIR)/contour_ground.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/contour_ground.c -o $@
$(OBJ_DIR)/decor_buildmap.o: $(WB_DIR)/decor_buildmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/decor_buildmap.c -o $@
$(OBJ_DIR)/occluder_buildmap.o: $(WB_DIR)/occluder_buildmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/occluder_buildmap.c -o $@
$(OBJ_DIR)/world_decode_tile.o: $(WB_DIR)/world_decode_tile.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/world_decode_tile.c -o $@
$(OBJ_DIR)/heightmap.o: $(WB_DIR)/heightmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/heightmap.c -o $@
$(OBJ_DIR)/collision_map.o: $(WB_DIR)/collision_map.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/collision_map.c -o $@
$(OBJ_DIR)/wb_minimap.o: $(WB_DIR)/minimap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(WB_DIR)/minimap.c -o $@

world-builder: $(OBJ_DIR) $(WB_OBJS)

# Reuse the main link set (which now includes the world_builder pipeline) minus main.o.
WB_TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
WB_TEST_SRCS = engine/world_builder/test/world_builder_test_unit.c \
	engine/world_builder/test/world_builder_test_cache.c \
	engine/world_builder/test/world_builder_test_main.c

test-world-builder: $(OBJ_DIR) $(WB_TEST_OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -I$(SRC_DIR)/engine/world_builder \
		-I$(SRC_DIR)/engine/world_builder/test -I$(SRC_DIR)/world \
		-o $(OBJ_DIR)/world_builder_test \
		$(WB_TEST_SRCS) $(WB_TEST_OBJS) $(LDFLAGS)
	mkdir -p $(OBJ_DIR)
	./$(OBJ_DIR)/world_builder_test

# CS2 math/bit opcode unit test (pure VM): reuse the main link set minus main.o.
test-cs2-math: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_math_ops_test \
		cs2vm2/test/math_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_math_ops_test

test-cs2-string: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_string_ops_test \
		cs2vm2/test/string_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_string_ops_test

test-cs2-component-param: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_component_param_ops_test \
		cs2vm2/test/component_param_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_component_param_ops_test

test-cs2-text-align: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_text_align_ops_test \
		cs2vm2/test/text_align_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_text_align_ops_test

test-cs2-target-verb: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_target_verb_ops_test \
		cs2vm2/test/target_verb_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_target_verb_ops_test

test-cs2-options: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_client_option_ops_test \
		cs2vm2/test/client_option_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_client_option_ops_test

test-cs2-triggerop: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_trigger_op_test \
		cs2vm2/test/trigger_op_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_trigger_op_test
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_triggeroplocal_test \
		cs2vm2/test/triggeroplocal_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_triggeroplocal_test

test-cs2-dialect: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_opcode_dialect_test \
		cs2vm2/test/opcode_dialect_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_opcode_dialect_test

# RESUME_COUNTDIALOG (3104) — the opcode the bank PIN keypad answers with.
# Declared on its own line rather than appended to the shared .PHONY list, which
# several branches are editing at once.
.PHONY: test-cs2-resume-countdialog
test-cs2-resume-countdialog: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_resume_countdialog_test \
		cs2vm2/test/resume_countdialog_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_resume_countdialog_test

# POP_INT_DISCARD / POP_STRING_DISCARD (38/39) — the operand is not a count.
# Own .PHONY line for the same reason as the target above it.
.PHONY: test-cs2-discard
test-cs2-discard: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_discard_ops_test \
		cs2vm2/test/discard_ops_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_discard_ops_test

# Procedural-texture coverage report over a 643-era cache. Not a pass/fail test — it
# names the unported operation blocking the most textures, which is the number to work
# against while porting. Run from the repo root so the default cache path resolves.
test-proctex-coverage: $(OBJ_DIR) $(OBJ_DIR)/proctex_generator.o $(OBJ_DIR)/rscache_unity.o \
		$(OBJ_DIR)/bmp.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o \
		$(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/proctex_coverage \
		engine/proctex/test/proctex_coverage.c \
		$(OBJ_DIR)/proctex_generator.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bmp.o \
		$(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(LDFLAGS)
	cd .. && ./src/$(OBJ_DIR)/proctex_coverage

# Reproducible, explicitly approximate RS727 procedural-material bridge used by
# the isolated QBD/TD cache overlay. The binary defaults to dry-run; --apply is
# required before it writes sprites, texture records, the ledger or remapped OB3s.
.PHONY: rs2012-qbd-kernel-survey

# Which raster kernels the RS727 QBD *source* models demand: texture render
# types, face shading, face alpha, and the per-material compositing properties.
# Reads the RS727 cache read-only and writes nothing.
rs2012-qbd-kernel-survey: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_qbd_kernel_survey \
		engine/proctex/test/rs2012_qbd_kernel_survey.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

rs2012-material-bake: $(OBJ_DIR) $(OBJ_DIR)/proctex_generator.o $(OBJ_DIR)/rscache_unity.o \
		$(OBJ_DIR)/bmp.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o \
		$(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_material_bake \
		engine/proctex/test/rs2012_material_bake.c \
		$(OBJ_DIR)/proctex_generator.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bmp.o \
		$(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(LDFLAGS)

.PHONY: rs2012-lane-bake rs2012-lane-bake-check

# The RS727 source cache the bake reads. It is ~461MB and matched by
# .gitignore's `cache.*/`, so it is NOT carried by a clone and every machine
# must obtain it out of band; the recipe fails with that instruction rather
# than baking something wrong. Override with RS2012_SRC_CACHE=<dir>.
RS2012_SRC_CACHE ?= $(REPO_ROOT)/cache.rs727_preeoc
# The lane ships matte 60. This is the tool's `--matte` flag, not its default
# (which is 0), which is why it must live somewhere a build can read rather
# than in a branch name.
RS2012_LANE_MATTE ?= 60

define RS2012_REQUIRE_SRC_CACHE
	@test -f "$(RS2012_SRC_CACHE)/main_file_cache.dat2" || { \
		echo "rs2012-lane-bake: no RS727 source cache at $(RS2012_SRC_CACHE)" >&2; \
		echo "  It is ~461MB and deliberately not in the repo (.gitignore: cache.*/)." >&2; \
		echo "  Copy it there, or pass RS2012_SRC_CACHE=<dir>." >&2; exit 1; }
endef

# A bake is not a compile: it reads the lane's directories directly, so it names
# its own path here rather than discovering it from lane.ini the way the script
# compile does. Recursive `=` so MOCK230_CONTENT_DIR (defined further down, and
# overridable) is expanded when a recipe runs.
RS2012_QBD_TD_LANE = $(MOCK230_CONTENT_DIR)/ported/rs2012_qbd_td

# Both the source cache and the tree are usually supplied from outside, and on
# Windows the usual mistake is a backslash path, which make's shell eats
# silently -- the bake then reports a missing pack under a mangled name. Check
# the lane up front and say which variable is wrong.
define RS2012_REQUIRE_LANE
	@test -d "$(RS2012_QBD_TD_LANE)" || { \
		echo "rs2012-lane-bake: no lane at $(RS2012_QBD_TD_LANE)" >&2; \
		echo "  MOCK230_CONTENT_DIR=$(MOCK230_CONTENT_DIR)" >&2; \
		echo "  is not a content tree carrying ported/rs2012_qbd_td." >&2; \
		case '$(MOCK230_CONTENT_DIR)' in *\\*) \
			echo "  (that path contains backslashes -- use forward slashes)" >&2;; esac; \
		exit 1; }
endef

# Re-backport the QBD/TD lane from the RS727 cache into the content tree, then
# tell you to re-pack. Writes $(MOCK230_CONTENT_DIR) in place -- point that at
# the tree you mean (see the note at MOCK230_CONTENT_DIR).
#
# This exists because the bake was previously a hand-typed command whose one
# load-bearing flag (--matte) lived only in a branch name, so no two machines
# ran it the same way. It is byte-reproducible as of the p[256] fix in
# proctex_ops.u.c; `rs2012-lane-bake-check` is the gate that keeps it that way.
rs2012-lane-bake: rs2012-material-bake
	$(RS2012_REQUIRE_SRC_CACHE)
	$(RS2012_REQUIRE_LANE)
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/rs2012_material_bake \
		--cache $(RS2012_SRC_CACHE) --tree $(MOCK230_CONTENT_DIR) \
		--matte $(RS2012_LANE_MATTE) --apply
	@echo "rs2012-lane-bake: wrote $(MOCK230_CONTENT_DIR); now run mock230-cache-rs2012"

# Determinism gate. Two bakes into disposable directories must be byte-identical;
# if they are not, the bake has picked up uninitialised or address-dependent
# state again and no two machines will agree on the lane. Writes nothing to the
# content tree.
rs2012-lane-bake-check: rs2012-material-bake
	$(RS2012_REQUIRE_SRC_CACHE)
	$(RS2012_REQUIRE_LANE)
	@tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
		cd $(REPO_ROOT) && for run in a b; do \
			mkdir -p "$$tmp/$$run"; \
			./src/$(OBJ_DIR)/rs2012_material_bake \
				--cache $(RS2012_SRC_CACHE) --tree $(MOCK230_CONTENT_DIR) \
				--matte $(RS2012_LANE_MATTE) --models-out "$$tmp/$$run" >/dev/null || exit 1; \
		done; \
		if diff -r "$$tmp/a" "$$tmp/b" >/dev/null; then \
			echo "rs2012-lane-bake-check: deterministic ($$(ls "$$tmp/a" | wc -l) models identical across two runs)"; \
		else \
			echo "rs2012-lane-bake-check: NOT deterministic -- two runs differ:" >&2; \
			diff -rq "$$tmp/a" "$$tmp/b" | head -20 >&2; exit 1; \
		fi

.PHONY: rs2012-strip-priorities

# RS2012 models were authored against a z-buffered client, so their face render
# priorities never ordered anything; honouring them overrides ToriDraw's depth
# sort. Rewrites the lane's OB3 assets, so re-run mock230-cache-rs2012 after.
rs2012-strip-priorities: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_strip_priorities \
		engine/proctex/test/rs2012_strip_priorities.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

.PHONY: rs2012-model-nudge

# Applies bounded per-vertex-group geometry moves (inflate/translate) to an
# OB3 and re-encodes it. The APPLY half of the audit driver's z-fighting
# repair: tools/rs2012_backport_audit proposes moves off measured tie pixels
# and judges them through the real renderer; this only executes them.
rs2012-model-nudge: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_model_nudge \
		engine/proctex/test/rs2012_model_nudge.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

.PHONY: rs2012-model-decimate

# Rig-preserving vertex decimation for imported OB3s: half-edge collapses
# restricted to same-bone-label pairs (so sequences still drive the result),
# QEM + label-centroid-drift cost, seeded jitter for outer searches, and a
# JSON old->new vertex/face mapping that ports per-vertex data onto the
# reduced model. The REDUCE half of the osrsify pipeline
# (tools/osrs_stylizer); rs2012-model-nudge is the SCULPT half.
rs2012-model-decimate: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_model_decimate \
		engine/proctex/test/rs2012_model_decimate.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

.PHONY: rs2012-model-defight

# Multi-view z-fighting repair for OB3 part sets: renders a yaw x pitch view
# grid with a top-two depth raster, finds coplanar face pairs that tie in the
# z-buffer (the priority layering the painter's path honors is dropped under
# TORIDRAW_MODEL_FLAG_ZBUFFER), and pushes the authored-on-top layer outward
# along its normal, duplicating shared vertices so rigging and animations are
# untouched. The pre-DECIMATION pass of the osrsify pipeline.
rs2012-model-defight: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_model_defight \
		engine/proctex/test/rs2012_model_defight.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

.PHONY: rs2012-model-faces

# Per-face attribute dump (JSON lines): geometry, color, priority, alpha,
# texture ids. The inspection end of the defight/priority tooling.
rs2012-model-faces: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_model_faces \
		engine/proctex/test/rs2012_model_faces.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

.PHONY: rs2012-model-view rs2012-face-priorities

# OB3 viewer: renders a model through the real project/sort/raster path and
# writes a multi-angle contact sheet. The iteration loop for face-priority
# authoring -- no cache, no window, no game.
RS2012_MODEL_TOOL_DEPS = $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
	$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/toridraw_model_sprite.o \
	$(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
	$(OBJ_DIR)/bmp.o
rs2012-model-view: $(OBJ_DIR) $(RS2012_MODEL_TOOL_DEPS)
	$(CC) $(CFLAGS) -I../tools/entity_viewer -o $(OBJ_DIR)/rs2012_model_view \
		engine/proctex/test/rs2012_model_view.c \
		engine/torirs_model_from_rscache.c engine/toridraw_model_from_torirs.c \
		engine/toridraw_animation_from_rscache.c engine/torirs_types.c \
		../tools/entity_viewer/ev_wire.c \
		$(RS2012_MODEL_TOOL_DEPS) $(LDFLAGS)

.PHONY: rs2012-wake-dump

# Prints decoded per-bone transform values for every frame of a QBD sequence,
# straight out of the shipped cache — no rendering. For finding which specific
# keyframes of a sequence decode to garbage (see docs/rs2012_qbd_wakeup).
rs2012-wake-dump: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_wake_dump \
		engine/proctex/test/rs2012_wake_dump.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

# Face-priority authoring for the imported lane: splits a model into connected
# features, reports them, and writes a priority band per feature.
# Links the model and animation adaptors: sampling a sequence's frames means
# driving the same ToriDraw_ModelAnimateFrame the client drives, not a second
# implementation of the rig that could disagree with it.
# -fopenmp: the slow search (simulated annealing over bands + vertex fuzz)
# evaluates its candidate renders across views in parallel.
rs2012-face-priorities: $(OBJ_DIR) $(RS2012_MODEL_TOOL_DEPS)
	$(CC) $(CFLAGS) -fopenmp -o $(OBJ_DIR)/rs2012_face_priorities \
		engine/proctex/test/rs2012_face_priorities.c \
		engine/torirs_model_from_rscache.c engine/toridraw_model_from_torirs.c \
		engine/toridraw_animation_from_rscache.c engine/torirs_types.c \
		$(RS2012_MODEL_TOOL_DEPS) $(LDFLAGS)

.PHONY: rs2012-model-diff
# Field-by-field decode diff of two OB3s: did a decode-modify-encode trip
# corrupt anything it was not supposed to touch? The write tools verify counts
# and priorities; this verifies colors, textures, uv, bones, alpha, vertices.
rs2012-model-diff: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o 		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs2012_model_diff 		engine/proctex/test/rs2012_model_diff.c 		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o 		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)

# Sound end-to-end test: cache -> synth render -> game queue -> platform.
# Links the SILENT audio backend instead of the SDL2 one (they define the same
# symbols), so this runs headless with no device.
AUDIO_TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/platform_audio_sdl2.o,$(OBJS))
test-sound: $(OBJ_DIR) $(AUDIO_TEST_OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_audio_test \
		game/test/rs_audio_test.c platform/platform_audio_null.c \
		$(AUDIO_TEST_OBJS) $(LDFLAGS)
	./$(OBJ_DIR)/rs_audio_test

# Audio engine, with no device: the mixer's retained contract (assets, voices,
# loops, fades, buses, streams) and a real song rendered out of a real cache.
# Links only the audio units and rscache -- nothing here needs the client.
AUDIO_ENGINE_SRCS = audio/torirs_pcm.c audio/torirs_mixer.c audio/torirs_midi_file.c \
	audio/torirs_soundbank.c audio/torirs_midi_synth.c audio/torirs_music.c
test-audio: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/bmp.o \
		$(OBJ_DIR)/ini.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/audio_test \
		audio/test/audio_test.c $(AUDIO_ENGINE_SRCS) \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/bmp.o $(OBJ_DIR)/ini.o \
		$(LDFLAGS)
	./$(OBJ_DIR)/audio_test ..

# audioharness -- the audio engine with nothing else attached.
#
# Links audio/ and rscache and NOTHING else: no toridraw, no renderer, no game,
# no SDL. Audio work kept being blocked by unrelated build breaks elsewhere in
# the client, and a subsystem you cannot exercise is one you cannot fix. See the
# header of audio/tools/audioharness.c.
AUDIO_TOOL_SRCS = audio/tools/audioharness.c audio/tools/audio_songload.c \
	audio/tools/audio_analyse.c
audio-harness: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/bmp.o \
		$(OBJ_DIR)/ini.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/audioharness \
		$(AUDIO_TOOL_SRCS) $(AUDIO_ENGINE_SRCS) \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/bmp.o $(OBJ_DIR)/ini.o \
		-lm
	@echo "built $(OBJ_DIR)/audioharness"

# Derived-cache LRU eviction. Never fires during a boot (one scene is well under
# the keep threshold), so it needs a test of its own or it ships unexercised.
CACHE_TRIM_TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
test-cache-trim: $(OBJ_DIR) $(CACHE_TRIM_TEST_OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cache_trim_test \
		engine/test/cache_trim_test.c $(CACHE_TRIM_TEST_OBJS) $(LDFLAGS)
	./$(OBJ_DIR)/cache_trim_test

# DB_* opcode integration test: reuse the main link set minus main.o.
DB_TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
test-db: $(OBJ_DIR) $(DB_TEST_OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/db_cache_test \
		game/test/db_cache_test.c $(DB_TEST_OBJS) $(LDFLAGS)
	./$(OBJ_DIR)/db_cache_test

$(OBJ_DIR)/hmap.o: $(HMAP_DIR)/hmap.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(HMAP_DIR)/hmap.c -o $@

$(OBJ_DIR)/rsareabuf.o: $(RSAREABUF_DIR)/rsareabuf.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(RSAREABUF_DIR)/rsareabuf.c -o $@

# rsareabuf: byte/bit/alt-order round-trips + arena + psize back-patching.
test-rsareabuf: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rsareabuf_test \
		$(RSAREABUF_DIR)/rsareabuf.c $(RSAREABUF_DIR)/rsareabuf_test.c
	./$(OBJ_DIR)/rsareabuf_test

$(OBJ_DIR)/ini.o: $(INI_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(INI_SRC) -o $@

$(OBJ_DIR)/rscache_unity.o: $(RSCACHE_UNITY_SRC) | $(OBJ_DIR)
	$(CC) $(RSCACHE_CFLAGS) -w -c $(RSCACHE_UNITY_SRC) -o $@

$(OBJ_DIR)/rsprot_unity.o: $(RSPROT_UNITY_SRC) | $(OBJ_DIR)
	$(CC) $(RSPROT_CFLAGS) -w -c $(RSPROT_UNITY_SRC) -o $@

# The buffer, standing alone. Split out of the unity TU because it is the only
# dependency-free part and the part everything wants; see rsprot_unity.c's
# header for the link failures that caused.
$(OBJ_DIR)/rsprot_buf.o: $(RSPROT_DIR)/src/rsprot_buf.c | $(OBJ_DIR)
	$(CC) $(RSPROT_CFLAGS) -w -c $< -o $@

# The codec set. Warnings stay ON here, unlike the rest of 3rd/: these files are
# this project's own transcriptions of RSProt's Kotlin rather than vendored
# third-party source, and an implicit conversion in a codec is a wire bug.
$(OBJ_DIR)/rsprot_packets_unity.o: $(RSPROT_PACKETS_UNITY_SRC) | $(OBJ_DIR)
	$(CC) $(RSPROT_CFLAGS) -Wall -Wextra -Wno-unused-parameter -c $(RSPROT_PACKETS_UNITY_SRC) -o $@

# The executor implementing 3rd/rsprot/include/rsprot_exec.h. Host-side by
# design (see the file header): the four directions are a host concern, so the
# library declares them and this project supplies them.
$(OBJ_DIR)/pkt_exec.o: net/exec/pkt_exec.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -c $< -o $@

# The bridge asks rsprot for a packet's layout, so it needs rsprot's headers on
# top of the main build's. Own rule for the same reason pkt_exec.o has one.
$(OBJ_DIR)/rsprot_bridge.o: net/rev/rsprot_bridge.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -c $< -o $@

# `make -C src rsprot` is the compile gate for all three, so a codec or an
# executor change is caught without building a whole client.
rsprot: $(OBJ_DIR) $(OBJ_DIR)/rsprot_unity.o $(OBJ_DIR)/rsprot_buf.o $(OBJ_DIR)/rsprot_packets_unity.o \
		$(OBJ_DIR)/pkt_exec.o

# rsprot's own unit tests. Shelled out because the test binaries link against
# rsprot's private build/ objects (its makefile builds its own isaac/rsa/mpi).
test-rsprot:
	$(SUBMAKE) -C $(RSPROT_DIR) test

# The executor's tests, and the packet-file convention tests. These live here
# and not in 3rd/rsprot/test/ for the same reason pkt_exec.c does: a codec
# cannot run without a host, so testing them here tests them as they are used.
#
# test-pktexec walks RSPROT_PRIM_LIST and fails on any primitive that is
# missing from the executor's table or that moves no bytes -- the two cannot
# drift. test-pktpackets pins each layout version's actual bytes, which is the
# only check that catches the wrong version being selected: two versions of the
# same packet are frequently the same width and each round-trips through itself
# perfectly.
# The isaac/rsa/tommath objects are on these link lines because rsprot_unity.o
# is one TU: it carries rsprot_crypto's adapters alongside rsprot_buf, and the
# adapters call into src/net's ISAAC and RSA rather than re-porting them. A
# buffer test does not use crypto, but it does have to resolve it.
RSPROT_TEST_DEPS = $(OBJ_DIR)/pkt_exec.o $(OBJ_DIR)/rsprot_unity.o $(OBJ_DIR)/rsprot_buf.o \
	$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o

test-pktexec: $(OBJ_DIR) $(RSPROT_TEST_DEPS)
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -o $(OBJ_DIR)/pkt_exec_test \
		net/exec/test/pkt_exec_test.c $(RSPROT_TEST_DEPS) -lm
	./$(OBJ_DIR)/pkt_exec_test

test-pktpackets: $(OBJ_DIR) $(RSPROT_TEST_DEPS) $(OBJ_DIR)/rsprot_packets_unity.o
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -o $(OBJ_DIR)/pkt_packets_test \
		net/exec/test/pkt_packets_test.c \
		$(OBJ_DIR)/rsprot_packets_unity.o $(RSPROT_TEST_DEPS) -lm
	./$(OBJ_DIR)/pkt_packets_test

$(OBJ_DIR)/bzip.o: $(REPO_ROOT)/3rd/bzip/bzip.c | $(OBJ_DIR)
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
# bzip_encode.c is the compression side (bzip.c is decompress-only). rscache's
# RSCache_CompressionBzipCompress calls into it, so it belongs in every link set
# that includes rscache_unity.o.
$(OBJ_DIR)/bzip_encode.o: $(REPO_ROOT)/3rd/bzip/bzip_encode.c | $(OBJ_DIR)
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/miniz.o: $(REPO_ROOT)/3rd/miniz/miniz.c | $(OBJ_DIR)
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/xteas.o: $(REPO_ROOT)/3rd/xteas/xteas.c | $(OBJ_DIR)
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(TORIDRAW_UNITY_SRC) | $(OBJ_DIR)
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_UNITY_SRC) -o $@

$(OBJ_DIR)/trspk_unity.o: $(TRSPK_UNITY_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $(TRSPK_UNITY_SRC) -o $@

$(OBJ_DIR)/webgl1_index16.o: $(TRSPK_DIR)/webgl1/webgl1_index16.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/opengl3_sdlgl.o: $(TRSPK_DIR)/opengl3/opengl3_sdlgl.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $(TRSPK_DIR)/opengl3/opengl3_sdlgl.c -o $@

$(OBJ_DIR)/toridraw_font.o: $(TORIDRAW_DIR)/toridraw_font.c | $(OBJ_DIR)
	$(CC) $(TORIDRAW_CFLAGS) -I$(REPO_ROOT)/v0 -w -c $(TORIDRAW_DIR)/toridraw_font.c -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(TORIDRAW_DIR)/toridraw_model_sprite.c | $(OBJ_DIR)
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_DIR)/toridraw_model_sprite.c -o $@

run: $(TARGET)
	./$(TARGET)

test-varp: $(OBJ_DIR) varp/varp_manager.c varp/varp_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/varp_manager_test varp/varp_manager.c varp/varp_manager_test.c
	./$(OBJ_DIR)/varp_manager_test

# ---- map editor ------------------------------------------------------------
#
# The content root the editor edits. Both map-editor tests are read-only over
# it; neither writes into a content tree.
EDITOR_CONTENT_DIR ?= $(REPO_ROOT)/OSRS-Content/osrs239-content
EDITOR_CACHE_DIR   ?= $(REPO_ROOT)/cache.osrs239

# Document and command layer: undo restoring an authored *absence*, strokes
# undoing as one step, edits at a square edge reaching the neighbour. Pure
# data — no cache, no window, runs in milliseconds.
test-editor-cmd: $(OBJ_DIR) editor/editor_jm2.c editor/editor_doc.c editor/editor_cmd.c \
		editor/test/editor_cmd_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_cmd_test \
		editor/editor_jm2.c editor/editor_doc.c editor/editor_cmd.c \
		editor/test/editor_cmd_test.c
	./$(OBJ_DIR)/editor_cmd_test

# Codec fidelity over the whole content tree: every .jm2/.jl2 must survive
# parse -> emit byte for byte. The editor's save path rewrites the file it
# loaded, so any asymmetry here is a diff the user never asked for, landing
# across thousands of squares at once.
test-editor-jm2: $(OBJ_DIR) editor/editor_jm2.c editor/editor_doc.c \
		editor/test/editor_jm2_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_jm2_test \
		editor/editor_jm2.c editor/editor_doc.c editor/test/editor_jm2_test.c
	./$(OBJ_DIR)/editor_jm2_test $(EDITOR_CONTENT_DIR)

# The editor renders what the game renders: a square loaded from text derives
# to exactly the terrain the cache decodes, resolved heights included. This is
# the one test that exercises the Perlin height fixup end to end.
test-editor-derive: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		editor/editor_jm2.c editor/editor_doc.c editor/editor_derive.c \
		editor/test/editor_derive_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_derive_test \
		editor/editor_jm2.c editor/editor_doc.c editor/editor_derive.c \
		editor/test/editor_derive_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)
	./$(OBJ_DIR)/editor_derive_test $(EDITOR_CONTENT_DIR) $(EDITOR_CACHE_DIR) 600

# Convex hull (ToriDraw): the silhouette a loc highlight is drawn from, plus the
# degenerate shapes real footprints produce -- edge-on, collinear, duplicated
# corners shared between adjacent tiles.
test-convex-hull: $(OBJ_DIR) $(TORIDRAW_DIR)/graphics/convex_hull.c \
		$(TORIDRAW_DIR)/graphics/test/convex_hull_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/convex_hull_test \
		$(TORIDRAW_DIR)/graphics/convex_hull.c \
		$(TORIDRAW_DIR)/graphics/test/convex_hull_test.c
	./$(OBJ_DIR)/convex_hull_test

# ToriDbgUI dropdown interaction, driven through the real input entry points:
# opening, choosing, dismissing by clicking away, wheel scrolling and clamping,
# and swapping the option list out from under an open one.
test-editor-dropdown: $(OBJ_DIR) ui/uitree_debug_overlay.c ui/test/uitree_test_dropdown.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/uitree_test_dropdown \
		ui/uitree_debug_overlay.c ui/test/uitree_test_dropdown.c
	./$(OBJ_DIR)/uitree_test_dropdown

test-editor: test-editor-cmd test-editor-jm2 test-editor-derive test-editor-dropdown \
	test-convex-hull

# NpcType.multiNpc (opcode 106) decode -> ToriRS_Npctype.transform_* -- pure
# struct plumbing, no cache file or App needed.
test-npctype-multi: $(OBJ_DIR) engine/torirs_npctype_from_rscache.c \
		engine/test/torirs_npctype_from_rscache_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/torirs_npctype_from_rscache_test \
		engine/torirs_npctype_from_rscache.c \
		engine/test/torirs_npctype_from_rscache_test.c
	./$(OBJ_DIR)/torirs_npctype_from_rscache_test

test-varc: $(OBJ_DIR) varc/varc_manager.c varc/varc_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/varc_manager_test varc/varc_manager.c varc/varc_manager_test.c
	./$(OBJ_DIR)/varc_manager_test

test-loot-store: $(OBJ_DIR) game/rs_loot_store.c game/test/rs_loot_store_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_loot_store_test game/rs_loot_store.c game/test/rs_loot_store_test.c
	./$(OBJ_DIR)/rs_loot_store_test

# ---------------------------------------------------------------------------
# ServerScript (LostCity RuneScript) reader / compiler / VM.
#
# Engine-agnostic by construction: libc plus 3rd/rsareabuf and nothing else, so
# it links into the standalone mock230 server without dragging in SDL, the task
# queue or the client's UI tree. Every target here links only the subsystem's
# own sources. See docs/serverscript.md.
#
# The generated tables (ss_opcode.h, ss_trigger.h, ss_meta.gen.h) are checked
# in. Regenerate them from the reference server with:
#     python3 serverscript/gen_opcode_meta.py [path-to-LostCity_Server]
# ---------------------------------------------------------------------------

# The compiler is a superset: it links the VM sources so a test can compile a
# script and immediately run it.
SSC_SRCS = $(SS_SRCS) serverscript/ssc_lex.c serverscript/ssc_symbols.c \
	content/content_register.c content/content_fields.c $(INI_SRC) \
	serverscript/ssc_compile.c serverscript/ssc_lane.c

# Generated opcode/trigger tables: arities, pointer masks, ap->op trigger
# relation, and the 64-bit lookup key that a rev-230 component uid overflows.
test-ss-meta: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_meta_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_meta_test \
		$(SS_SRCS) serverscript/test/ss_meta_test.c
	./$(OBJ_DIR)/ss_meta_test

# Reader + container against LostCity's real 5MB compiled corpus: exact byte
# consumption, names against script.pack, branch/switch deltas, index integrity.
# SKIPs when the reference server is not checked out beside this repo.
SS_CORPUS ?= $(REPO_ROOT)/../LostCity_Server
test-ss-corpus: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_corpus_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_corpus_test \
		$(SS_SRCS) serverscript/test/ss_corpus_test.c
	./$(OBJ_DIR)/ss_corpus_test $(SS_CORPUS)

# decode -> encode -> memcmp over the whole corpus. A decoder can pass every
# structural check and still drop or normalise a field; reproducing the original
# bytes exactly is the only evidence it did not.
test-ss-roundtrip: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_roundtrip_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_roundtrip_test \
		$(SS_SRCS) serverscript/test/ss_roundtrip_test.c
	./$(OBJ_DIR)/ss_roundtrip_test $(SS_CORPUS)

# The VM: core language, arithmetic, strings, frames, limits, suspend/resume.
# Scripts are assembled in memory and loaded through the real container, so
# these cases cover the encoder and reader on the way past. No corpus needed.
test-ss-vm: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_vm_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_vm_test \
		$(SS_SRCS) serverscript/test/ss_vm_test.c
	./$(OBJ_DIR)/ss_vm_test

# The trigger lookup order — exact type, then category, then the bare `_`, and
# nothing after that. Synthetic scripts, because every way of getting the order
# wrong still finds *a* script: swapping the first two rungs runs the category's
# script for an item that had its own, which looks entirely reasonable in a log.
test-ss-provider: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_provider_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_provider_test \
		$(SS_SRCS) serverscript/test/ss_provider_test.c
	./$(OBJ_DIR)/ss_provider_test

# Static stack verifier: a CFG walk over every corpus instruction. Proves the
# generated arity table agrees with real compiled code, and turns the VM's stack
# size constants into measurements instead of guesses.
test-ss-verify: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_verify_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_verify_test \
		$(SS_SRCS) serverscript/test/ss_verify_test.c
	./$(OBJ_DIR)/ss_verify_test $(SS_CORPUS)

# RuneScript compiler: .rs2 -> script.dat/.idx.
#
# Always -O3, whatever flavor the client is being built in. sscompile is a build
# tool, not the program under test: it reads ~15k scripts on every content
# change, and inheriting OPT=0 from a debug client build turns a five-second
# step into a much longer one for no diagnostic gain — nobody debugs the client
# by stepping through the script compiler. $(CFLAGS) is still the base, so the
# include paths, the C standard and the warning set stay identical; only the
# optimisation level is pinned. The trailing -O3 wins because the last -O on a
# command line is the one gcc and clang obey.
#
# The sanitizers are deliberately NOT stripped the same way. ENABLE_ASAN=1 is an
# explicit ask, and a compiler that reads 15k files is a good place for ASan to
# find something; -O3 and -fsanitize=address compose. What is being pinned here
# is the accident (a debug client build slowing the content loop), not the
# deliberate choice.
SSC_CFLAGS = $(CFLAGS) -O3

sscompile: $(OBJ_DIR) $(SSC_SRCS) serverscript/ssc_main.c
	$(CC) $(SSC_CFLAGS) -o $(OBJ_DIR)/sscompile $(SSC_SRCS) serverscript/ssc_main.c
	@echo "built $(OBJ_DIR)/sscompile"

# Compiler end to end: each case writes real RuneScript, compiles it, loads it
# back through the container and runs it on the VM. Bytecode that is structurally
# valid but semantically wrong only shows up when it runs.
test-ssc: $(OBJ_DIR) $(SSC_SRCS) serverscript/test/ssc_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ssc_test $(SSC_SRCS) serverscript/test/ssc_test.c
	./$(OBJ_DIR)/ssc_test

# CS1 VM core is engine-agnostic, so the suite links only the VM sources.
test-cs1vm: $(OBJ_DIR) cs1vm/cs1vm.c cs1vm/cs1vm_script.c cs1vm/test/cs1vm_test.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/cs1vm -o $(OBJ_DIR)/cs1vm_test \
		cs1vm/cs1vm.c cs1vm/cs1vm_script.c cs1vm/test/cs1vm_test.c
	./$(OBJ_DIR)/cs1vm_test

# CS1 end-to-end: build path -> tree -> game host -> emit. Links the ui + state
# managers the host reads, but no cache/provider (it never touches disk).
test-cs1: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs1_integration_test \
		cs1vm/cs1vm.c cs1vm/cs1vm_script.c \
		game/rs_cs1_host.c game/rs_player_stats.c \
		inv/inv_manager.c varp/varp_manager.c \
		ui/uitree.c ui/uitree_hook.c ui/uitree_component_options.c engine/torirs_component_hook.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
		ui/uitree_scroll.c ui/uitree_emit.c ui/uitree_inv_view.c \
		ui/uitree_cross.c ui/uitree_minimenu.c ui/uitree_hovertext.c \
		perf/torirs_perf.c \
		cs1vm/test/cs1_integration_test.c -lm
	./$(OBJ_DIR)/cs1_integration_test

test-cmdbus: $(OBJ_DIR) cmd/cmdring.c cmd/cmdbus.c cmd/test/cmdbus_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cmdbus_test cmd/cmdring.c cmd/cmdbus.c cmd/test/cmdbus_test.c
	./$(OBJ_DIR)/cmdbus_test

# Login handshake + packet framing, deterministic (fixed seed + tommath RSA).
NET_CORE_OBJS = $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/ringbuf.o \
	$(OBJ_DIR)/jbase37.o $(OBJ_DIR)/wordpack.o $(OBJ_DIR)/loginproto.o \
	$(OBJ_DIR)/loginproto_xrsps.o $(OBJ_DIR)/loginproto_osrs230.o \
	$(OBJ_DIR)/packetbuffer.o $(OBJ_DIR)/gameproto_rev_lc245_2.o \
	$(OBJ_DIR)/gameproto_rev_lc254.o $(OBJ_DIR)/gameproto_rev_xrsps233.o \
	$(OBJ_DIR)/gameproto_rev_osrs230.o $(OBJ_DIR)/osrs230_parse.o \
	$(OBJ_DIR)/gameproto_rev_osrs239.o $(OBJ_DIR)/osrs239_parse.o \
	$(OBJ_DIR)/osrs239_entity_info.o $(OBJ_DIR)/pkt_player_appearance.o \
	$(OBJ_DIR)/loginproto_osrs239.o $(OBJ_DIR)/sha256.o $(OBJ_DIR)/pow_sha256.o \
	$(OBJ_DIR)/pkt_rebuild_normal.o $(OBJ_DIR)/pkt_rebuild_region.o \
	$(OBJ_DIR)/bitbuffer.o \
	$(OBJ_DIR)/gameproto_parse.o $(OBJ_DIR)/tommath.o \
	$(RSPROT_PARSE_OBJS) \
	$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o

# The rsprot inbound path, as one name. net.c calls rsprot_bridge_parse before
# either hand-written parser, so anything linking net.o needs all four: the
# bridge, the codecs, the executor they call, and rsprot_buf underneath.
RSPROT_PARSE_OBJS = $(OBJ_DIR)/rsprot_bridge.o $(OBJ_DIR)/rsprot_packets_unity.o $(OBJ_DIR)/rsprot_buf.o \
	$(OBJ_DIR)/pkt_exec.o $(OBJ_DIR)/rsprot_unity.o
# PLAYER_INFO v5 round-trip: the encoder against a decoder transcribed from
# RSProt's own reference client, written in the opposite direction. Catches the
# skip-run off-by-ones and misaligned bit sections that otherwise decode into
# the wrong players rather than erroring.
test-mock239-playerinfo: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock239_playerinfo_test \
		net/mock/test/mock239_playerinfo_test.c net/mock/mock239_playerinfo.c \
		net/rev/osrs239/osrs239_entity_info.c net/bitbuffer.c \
		$(RSAREABUF_DIR)/rsareabuf.c -lm
	./$(OBJ_DIR)/mock239_playerinfo_test

# Golden client->server interface callback layouts. Kept independent of the
# world/server link so byte order, inner framing and typed-tail losslessness can
# fail directly rather than only as a click that appears to do nothing.
test-mock239-inbound: $(OBJ_DIR) $(OBJ_DIR)/jbase37.o $(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock239_interface_inbound_test \
		net/mock/test/mock239_interface_inbound_test.c \
		net/mock/mock239_interface_inbound.c net/mock/mock239_inbound.c \
		$(OBJ_DIR)/jbase37.o $(OBJ_DIR)/rsprot_buf.o $(RSAREABUF_DIR)/rsareabuf.c
	./$(OBJ_DIR)/mock239_interface_inbound_test

# Full revision-239 RUNCLIENTSCRIPT argument alphabet. Literal bytes cover the
# scalar, array, string-array and long paths rather than round-tripping through
# a second implementation with the same possible mistake.
test-mock239-runclientscript: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock239_runclientscript_test \
		net/mock/test/mock239_runclientscript_test.c \
		net/mock/mock239_runclientscript.c $(RSAREABUF_DIR)/rsareabuf.c
	./$(OBJ_DIR)/mock239_runclientscript_test

# Every additional fixed-body rev239 model/interface setter. The expected
# bodies are independent literal RSProt fixtures, not a decode round-trip.
test-mock239-interface-setters: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock239_interface_setters_test \
		net/mock/test/mock239_interface_setters_test.c \
		net/mock/mock239_interface_setters.c $(RSAREABUF_DIR)/rsareabuf.c
	./$(OBJ_DIR)/mock239_interface_setters_test

test-mock239-varp: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock239_varp_test \
		net/mock/test/mock239_varp_test.c \
		net/mock/mock239_varp.c $(RSAREABUF_DIR)/rsareabuf.c
	./$(OBJ_DIR)/mock239_varp_test

# Server->client revision-239 interface authority and golden packet bodies.
# Expected bytes are literal fixtures taken from RSProt's encoders rather than
# decoded through a mirror of this implementation.
test-mock230-interface-state: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock230_interface_state_test \
		net/mock/test/mock230_interface_state_test.c \
		net/mock/mock230_interface_state.c $(RSAREABUF_DIR)/rsareabuf.c
	./$(OBJ_DIR)/mock230_interface_state_test

test-net-login: $(OBJ_DIR) $(NET_CORE_OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_login_test \
		net/test/net_login_test.c $(NET_CORE_OBJS) -lm
	./$(OBJ_DIR)/net_login_test

# Full login -> GAME -> parsed-packet pipeline through a scripted mock server.
# The migrated packets, decoded by BOTH the generated rsprot codecs and the
# hand-written osrs239_parse.c, over the same bytes. Two independent
# transcriptions of one Kotlin source agreeing is what licenses deleting the
# hand-written arm; see the test's header.
#
# This rule lives BELOW $(NET_CORE_OBJS)'s definition and has to: make expands a
# rule's prerequisites when it READS the rule, so the same rule placed above
# line 1011 gets an empty prerequisite list, links against objects nothing built,
# and fails with "no such file or directory" on an object the recipe names.
test-rsprot-bridge: $(OBJ_DIR) $(NET_CORE_OBJS)
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -o $(OBJ_DIR)/rsprot_bridge_test \
		net/rev/test/rsprot_bridge_test.c $(NET_CORE_OBJS) -lm
	./$(OBJ_DIR)/rsprot_bridge_test

test-net-out-resume: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/net_out_resume_test \
		net/test/net_out_resume_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/net_out_resume_test

test-net-loopback: $(OBJ_DIR) $(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_loopback_test \
		net/test/net_loopback_test.c net/mock/mock_server.c \
		$(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o -lm
	./$(OBJ_DIR)/net_loopback_test

test-inv: $(OBJ_DIR) inv/inv_manager.c inv/inv_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/inv_manager_test inv/inv_manager.c inv/inv_manager_test.c
	./$(OBJ_DIR)/inv_manager_test

test-world: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/world -I$(SRC_DIR)/world/test -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/world_test \
		world/entity_pool.c world/world_entity.c world/entity_pathing.c \
		world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
		engine/world_builder/heightmap.c engine/world_builder/collision_map.c \
		engine/world_builder/minimap.c painters/painters.c painters/scene_occluders.c \
		features/features.c \
		world/test/world_test_unit.c world/test/world_test_sim.c \
		world/test/world_test_route.c world/test/world_test_main.c \
		$(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/world_test

test-pick: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/pick_face_test \
		render/test/pick_face_test.c \
		$(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	./$(OBJ_DIR)/pick_face_test

# The terrain pick's level guard. Links the world the same way test-world does,
# because the guard's whole subject is a bridge column's tile flags.
test-pick-level: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/pick_level_test \
		render/test/pick_level_test.c render/torirs_pick.c \
		world/entity_pool.c world/world_entity.c world/entity_pathing.c \
		world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
		engine/world_builder/heightmap.c engine/world_builder/collision_map.c \
		engine/world_builder/minimap.c painters/painters.c painters/scene_occluders.c \
		features/features.c \
		$(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/pick_level_test

test-trig-tables-share: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/trig_tables_share_test \
		render/test/trig_tables_share_test.c \
		$(TORIDRAW_DIR)/graphics/shared_tables.c $(TORIDRAW_DIR)/toridraw_math.c \
		$(RSCACHE_DIR)/src/datatypes/noise.c -lm
	./$(OBJ_DIR)/trig_tables_share_test

test-painters-occluders: $(OBJ_DIR) $(OBJ_DIR)/occluder_buildmap.o $(OBJ_DIR)/heightmap.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -I$(SRC_DIR)/engine/world_builder \
		-o $(OBJ_DIR)/painters_test_occluders \
		painters/test/painters_test_occluders.c \
		painters/scene_occluders.c \
		$(OBJ_DIR)/occluder_buildmap.o $(OBJ_DIR)/heightmap.o
	./$(OBJ_DIR)/painters_test_occluders

test-painters-terrain-levels: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters \
		-o $(OBJ_DIR)/painters_test_terrain_levels \
		painters/test/painters_test_terrain_levels.c \
		painters/painters.c painters/painters_cull_project.c painters/scene_occluders.c \
		perf/torirs_perf.c \
		$(TORIDRAW_DIR)/graphics/shared_tables.c
	./$(OBJ_DIR)/painters_test_terrain_levels

test-painters-ground-decor: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters \
		-o $(OBJ_DIR)/painters_test_ground_decor_dynamic \
		painters/test/painters_test_ground_decor_dynamic.c \
		painters/painters.c painters/painters_cull_project.c painters/scene_occluders.c \
		perf/torirs_perf.c \
		$(TORIDRAW_DIR)/graphics/shared_tables.c
	./$(OBJ_DIR)/painters_test_ground_decor_dynamic

test-uitree: $(OBJ_DIR)
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(SRC_DIR)/ui -I$(SRC_DIR)/ui/test -o $(OBJ_DIR)/uitree_test \
		ui/uitree.c ui/uitree_hook.c ui/uitree_component_options.c engine/torirs_component_hook.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
		ui/uitree_input.c ui/uitree_hover.c ui/uitree_scroll.c ui/uitree_emit.c \
		ui/uitree_inv_view.c ui/uitree_obj_cell.c ui/uitree_cross.c ui/uitree_minimenu.c \
		ui/uitree_hovertext.c ui/uitree_debug_overlay.c \
		ui/uitree_interact.c input/torirs_input.c input/torirs_keymap.c \
		perf/torirs_perf.c \
		ui/test/uitree_test_main.c ui/test/uitree_test_dirty.c \
		ui/test/uitree_test_walk.c ui/test/uitree_test_mounted_world.c \
		ui/test/uitree_test_hover.c \
		ui/test/uitree_test_layout_build.c ui/test/uitree_test_mutate_emit.c \
		ui/test/uitree_test_drag.c ui/test/uitree_test_scroll_hit.c \
		ui/test/uitree_test_drag_scrolled.c ui/test/uitree_test_emit_icons.c \
		ui/test/uitree_test_emit_golden.c \
		ui/test/uitree_test_minimenu.c ui/test/uitree_test_keys.c \
		ui/test/uitree_test_id_index.c ui/test/uitree_test_component_params.c \
		ui/test/uitree_test_open_close.c ui/test/uitree_test_live_sets.c \
		ui/test/uitree_test_debug_overlay.c \
		ui/test/uitree_test_entity_overlay_order.c
	./$(OBJ_DIR)/uitree_test

# Font markup grammar + the rules it strokes (<str>, <u>), rasterised into a
# plain buffer with the baked font. Pixel-level, but needs no cache and no
# renderer — ToriDraw2D_DrawString writes the buffer itself.
test-font-markup: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/font_markup_test \
		ui/test/font_markup_test.c engine/torirs_debug_font_baked.c \
		$(BMP_DIR)/bmp.c \
		$(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o -lm
	./$(OBJ_DIR)/font_markup_test

# Debug-overlay raster tests: display list -> emit desc -> ToriRS_Frame -> Soft3D,
# with the *baked* fonts, one BMP per feature into build/. This is the only test
# that proves the pictures are right (baselines, scissor boxes, the slot -> font
# mapping); test-uitree covers the model. It links the real frame translator and
# the real software rasteriser but NOT world.c — torirs_frame.c only reaches the
# two World_* symbols on the UITREE_EMIT_WORLD path, which an overlay-only emit
# buffer never takes, and the test closes the link itself rather than dragging
# painters/collision/heightmap/minimap into a rectangles-and-text test.
test-debug-overlay-visual: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/toridraw_model_sprite.o
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(BMP_DIR) -o $(OBJ_DIR)/debug_overlay_visual \
		ui/test/uitree_debug_overlay_visual.c \
		ui/uitree_debug_overlay.c engine/torirs_debug_font_baked.c \
		render/torirs_frame.c render/torirs_pick.c \
		platform/platform_sdl2_renderer_soft3d.c \
		$(BMP_DIR)/bmp.c \
		$(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o \
		$(OBJ_DIR)/toridraw_model_sprite.o -lm
	./$(OBJ_DIR)/debug_overlay_visual

# The rev-230 widget chatbox: line order, bottom anchoring, scroll overflow.
test-chat-widgets: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/ui -o $(OBJ_DIR)/chat_widgets_test \
		game/rs_chat.c game/rs_chat_widgets.c game/rs_social.c net/jbase37.c \
		ui/uitree.c ui/uitree_hook.c ui/uitree_component_options.c engine/torirs_component_hook.c ui/uitree_layout.c ui/uitree_scroll.c ui/uitree_host.c \
		perf/torirs_perf.c \
		game/test/rs_chat_widgets_test.c -lm
	./$(OBJ_DIR)/chat_widgets_test

# Widget-rebuild burst benchmark (cc_deleteall + a run of cc_create). Always
# optimized — the point is the algorithm, not -O0 call overhead. Override the
# shape with BENCH_ROWS / BENCH_ITERS / BENCH_STATIC_CHILDREN.
BENCH_ROWS ?= 400
BENCH_ITERS ?= 20
BENCH_STATIC_CHILDREN ?= 20
bench-uitree: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -I$(SRC_DIR) -I$(SRC_DIR)/ui -I$(REPO_ROOT) \
		-I$(RSCACHE_DIR)/include -I$(TORIDRAW_DIR) -I$(BMP_DIR) -I$(HMAP_DIR) \
		-DBENCH_ROWS=$(BENCH_ROWS) -DBENCH_ITERS=$(BENCH_ITERS) \
		-DBENCH_STATIC_CHILDREN=$(BENCH_STATIC_CHILDREN) \
		-o $(OBJ_DIR)/uitree_bench \
		ui/uitree.c ui/uitree_hook.c ui/uitree_component_options.c engine/torirs_component_hook.c ui/uitree_layout.c ui/uitree_scroll.c ui/uitree_host.c \
		perf/torirs_perf.c \
		ui/test/uitree_bench.c -lm
	./$(OBJ_DIR)/uitree_bench

test-uitree-builder: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/engine/uitree_builder -o $(OBJ_DIR)/uitree_builder_test \
		engine/uitree_builder/uitree_builder_manifest.c \
		revconfig/revconfig.c revconfig/revconfig_load.c $(INI_SRC) \
		engine/uitree_builder/test/uitree_builder_test_manifest.c
	./$(OBJ_DIR)/uitree_builder_test

test-minimap: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/engine/minimap -I$(BMP_DIR) -o $(OBJ_DIR)/minimap_test \
		engine/minimap/minimap.c engine/torirs_types.c $(BMP_DIR)/bmp.c \
		engine/minimap/minimap_test.c
	./$(OBJ_DIR)/minimap_test

# PLAYER_INFO / NPC_INFO bit decode + appearance blob.
test-entity-decode: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/entity_decode_test \
		net/test/entity_decode_test.c net/bitbuffer.c \
		net/rev/packets/pkt_player_info.c net/rev/packets/pkt_npc_info.c \
		net/rev/packets/pkt_player_appearance.c \
		net/jbase37.c $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o \
		$(OBJ_DIR)/xteas.o -lm
	./$(OBJ_DIR)/entity_decode_test

# RUNCLIENTSCRIPT (op 84): the encoder's layout read back by osrs230_parse —
# the mixed int/string payload, and the argument-count guard that used to
# accept an over-long packet as PKT_RUNCLIENTSCRIPT_ARG_MAX garbage arguments.
.PHONY: test-runclientscript
test-runclientscript: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/rsprot_buf.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/runclientscript_test \
		net/test/runclientscript_test.c net/rev/osrs230/osrs230_parse.c \
		net/rev/packets/pkt_rebuild_normal.c net/rev/packets/pkt_rebuild_region.c \
		net/bitbuffer.c \
		$(RSAREABUF_DIR)/rsareabuf.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/rsprot_buf.o -lm
	./$(OBJ_DIR)/runclientscript_test

# Serial-FIFO ordering guarantee (head-only task execution across IO yields).
test-task-order: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/task_order_test game/test/task_order_test.c -lm
	./$(OBJ_DIR)/task_order_test

# CS2 visual transactions: ready work drains past the old per-frame caps, while
# genuine external waits retain the last complete display list until settlement.
test-cs2-frame-settle: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_frame_settle_test game/test/cs2_frame_settle_test.c -lm
	./$(OBJ_DIR)/cs2_frame_settle_test

# No blocking IO outside tests: TaskRunner_Drain must appear only under */test/.
check-no-drain:
	@! grep -rn "TaskRunner_Drain" --include="*.c" --include="*.h" . \
		| grep -v "/test/" | grep -v "task_runner.h" \
		&& echo "check-no-drain: PASS (no runtime Drain)" \
		|| (echo "check-no-drain: FAIL — runtime TaskRunner_Drain found" && exit 1)

# Walkmerge blend (Model.maskAnimate port) + seq-meta plumbing.
test-walkmerge: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/walkmerge_test \
		world/test/walkmerge_test.c $(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/walkmerge_test

# Parity of the `scanline` raster family against the shipping `branching`
# kernels: every flat / gouraudhsllightness / textured variant over a triangle
# set covering interior, clipped, degenerate and inverted-winding cases.
# Standalone TU, no cache or disk.
test-scanline: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-I$(TORIDRAW_DIR) -o $(OBJ_DIR)/scanline_parity_test \
		$(TORIDRAW_DIR)/toridraw_scanline_parity_test.c -lm
	./$(OBJ_DIR)/scanline_parity_test

# The `gouraudrgb` family: per-channel RGB interpolation, as opposed to
# `gouraudhsllightness`, which walks a packed HSL16 word through the palette.
# No reference rasterizer produces the same pixels, so the checks are
# properties - coverage against the HSL twin (same walker), the colour plane
# against a double-precision plane, the channel clamp, and the alpha algebra.
# Standalone TU, no cache or disk.
test-gouraudrgb: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-I$(TORIDRAW_DIR) -o $(OBJ_DIR)/gouraudrgb_test \
		$(TORIDRAW_DIR)/toridraw_gouraudrgb_test.c -lm
	./$(OBJ_DIR)/gouraudrgb_test

.PHONY: test-scanline test-gouraudrgb test-texture-matrix test-texmap test-render-hd

# The `texplane` compositing matrix on the `branching` family:
# gate {texopaque,textrans,texalpha} x facealpha x modulate, plus clamp
# addressing. Twelve variants, each driven into a configuration where it must
# equal a plain SIMD kernel bit-exactly, with the algebra chained off that
# anchor. Also covers the arctangent table. Standalone TU, no cache or disk.
test-texture-matrix: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-I$(TORIDRAW_DIR) -o $(OBJ_DIR)/texture_matrix_test \
		$(TORIDRAW_DIR)/toridraw_texture_matrix_test.c -lm
	./$(OBJ_DIR)/texture_matrix_test

# The mapped texture families - texcylinder / texcube / texsphere, twelve
# variants each. Checks the projections against a libm reference, the
# fixed-point uv plane setup against a double reference at real pixels, the
# compositing identity chain, that the three projections actually differ, and
# the cube face selection. Standalone TU, no cache or disk.
test-texmap: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-I$(TORIDRAW_DIR) -o $(OBJ_DIR)/texmap_test \
		$(TORIDRAW_DIR)/toridraw_texmap_test.c -lm
	./$(OBJ_DIR)/texmap_test

# ToriDraw_RenderHD's per-face routing: does each face reach the kernel its
# render type, material gate and alpha name? Asserts on ToriDraw_HDRenderStats,
# because a mis-routed face still produces pixels and the image cannot tell.
test-render-hd: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/render_hd_test \
		$(TORIDRAW_DIR)/toridraw_render_hd_test.c $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/toridraw_font.o
	./$(OBJ_DIR)/render_hd_test

# Side-by-side viewer: a textured, alpha-blended model out of a dat1 cache
# rendered with both raster families plus an amplified difference pane.
# Run: ./build/scanline_compare ../cache254 [model_id]
scanline-compare: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/scanline_compare \
		render/test/scanline_compare_sdl.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	@echo "built $(OBJ_DIR)/scanline_compare — run: $(OBJ_DIR)/scanline_compare $(DAT1_CACHE)"

# Angle-unit guard for the compass/minimap rotated blit (camera yaw, 2048/turn).
test-rotate-blit: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/rotate_blit_test \
		render/test/rotate_blit_test.c render/trspk_sprite.c $(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/rotate_blit_test

# Packet exec seam against bare managers (no cache/disk). Links the app
# object set (exec calls into RS_UISlots which references App) minus main.
test-net-exec: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_exec_test \
		game/test/rs_gameproto_exec_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/net_exec_test

# The CLIENT half of friends / ignore / private chat: the store, the CS2 host
# ops, the friend-transmit registration, and the generated stack shapes. The
# server half already has two gates (`mock230 --selftest` and
# `test-mock230-embed`); every bug this feature actually hit was on this side of
# the wire, and it had none. Needs no cache and no server on purpose — a gate
# that can SKIP is a gate that eventually only ever skips.
test-social: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_social_test \
		game/test/rs_social_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/rs_social_test

# Player-pick stack expansion + local-skip (Client-TS addViewportOptions).
test-minimenu-world: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/world -I$(SRC_DIR)/world/test -o $(OBJ_DIR)/rs_minimenu_world_test \
		game/test/rs_minimenu_world_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/rs_minimenu_world_test

# NPC_INFO / PLAYER_INFO count-shrinkage despawn (plane-change orphans).
test-entity-info-shrink: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/world -I$(SRC_DIR)/world/test -o $(OBJ_DIR)/task_exec_entity_info_test \
		game/test/task_exec_entity_info_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/task_exec_entity_info_test

# NpcType.minimap (config opcode 93) gating the yellow npc dot.
test-minimap-dots: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/world -I$(SRC_DIR)/world/test -o $(OBJ_DIR)/minimap_dots_test \
		game/test/minimap_dots_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/minimap_dots_test

# RS_CS2_PumpTransmits' early-return guard, one dirty flag at a time. The guard
# was a hand-written condition list that had drifted from the clear-down in the
# same function: `stat_transmit_dirty` was cleared but was never a guard key, so
# a tick carrying only UPDATE_STAT — an xp gain — early-returned AND kept the
# flag, and the XP-drop panel (interface 122) drew nothing. That was mis-blamed
# on the CS2 VM for two lanes. Each case sets exactly ONE flag, because with any
# second flag set the guard opens and the bug is invisible. Needs no cache and
# no server.
test-cs2-transmit-pump: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_cs2_transmit_pump_test \
		game/test/rs_cs2_transmit_pump_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/rs_cs2_transmit_pump_test

# Runtime interface-slot API contract test (IF_OPENMAIN/IF_OPENSIDE/IF_SETTAB
# surface) against the real dat1 cache + rev_245_2 RevConfig build.
test-ui-slots: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_ui_slots_test \
		game/test/rs_ui_slots_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/rs_ui_slots_test $(DAT1_CACHE)

# End-to-end dat1 RevConfig build against a dat1 cache (default ../cache254).
# Links the app objects (minus main) since the build task reaches CS2/game code.
DAT1_CACHE ?= ../cache254
test-uitree-builder-dat1: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/uitree_builder_dat1_test \
		engine/uitree_builder/test/uitree_builder_test_dat1.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/uitree_builder_dat1_test $(DAT1_CACHE)

test-revconfig: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/revconfig -I$(SRC_DIR)/revconfig/test -o $(OBJ_DIR)/revconfig_test \
		revconfig/revconfig.c revconfig/revconfig_load.c $(INI_SRC) \
		revconfig/test/revconfig_test_main.c revconfig/test/revconfig_test_buffer.c \
		revconfig/test/revconfig_test_items.c revconfig/test/revconfig_test_load.c \
		revconfig/test/revconfig_test_parse.c
	./$(OBJ_DIR)/revconfig_test

# Standalone TCP mock server for the OSRS rev-230 protocol: login handshake,
# on-login burst, and a 600 ms game tick driving movement, npcs, inventory and
# equipment. Run: src/build/mock230 [port]. See docs/osrs230_mockserver.md.
#
# Everything but main(). Split out so the same server can be linked into a host
# that has its own entry point — the in-process build (test-mock230-embed) is
# the first, and src/platform/net_transport_embed.c is the second.
# jbase37 and wordpack are here rather than in MOCK230_CORE_SRCS because the
# embed test already links both as objects out of NET_CORE_OBJS, and the client
# under EMBED_SERVER=1 already has both in SRCS — listing them in both places is
# a duplicate symbol. The appearance encoder needs jbase37: a player's name goes
# on the wire base-37 packed. MESSAGE_PRIVATE needs wordpack in *both*
# directions: the client's outbound builder wordpacks, so the server unpacks to
# read the message and packs again to relay it (docs/FRIENDS_PRIVATE_CHAT.md
# §3.4 — a plaintext override on the inbound side was rejected because it would
# make the two halves of net_out.c/mock230 disagree).
MOCK230_SRCS = net/mock/mock230_main.c net/jbase37.c net/wordpack.c $(MOCK230_CORE_SRCS)
# Dedicated read-only, multi-client JS5 cache service.
#
# It is separate from mock230 because a client uses ONE port for JS5 and the
# game, choosing with its first byte — so the two cannot both own 43594 in a
# finished deployment, and the branch belongs in mock230_session's handshake.
# This binary exists because it is the half that can be verified today against
# a real OldSchool client and against Jagex's own service:
#
#   src/build/js5_server --cache cache.osrs239 --revision 239
#   tools/js5_probe.py --host 127.0.0.1 --rev 239 --archive 255 --group 255
#
# See docs/RSPROT_OSRS239_PORT.md.
# The two ws_ units are what lets a browser reach this server at all: an
# emscripten client's connect() is a WebSocket, so the reactor sniffs the first
# byte and speaks either raw JS5 or RFC 6455 on the same port.
JS5_SERVER_SRCS = js5/server/js5_server_main.c js5/server/js5_server.c \
	js5/server/js5_server_conn.c \
	js5/server/js5_server_session.c js5/server/js5_server_cache.c \
	platform/net_transport_ws_frame.c platform/net_transport_ws_handshake.c

js5-server: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/js5_server $(JS5_SERVER_SRCS) \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm $(JS5_SOCKET_LIBS)
	@echo "built $(OBJ_DIR)/js5_server"

mock-js5: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock_js5 $(JS5_SERVER_SRCS) \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm $(JS5_SOCKET_LIBS)
	@echo "built $(OBJ_DIR)/mock_js5 (compatibility name)"

mock230: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock230 $(MOCK230_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm $(JS5_SOCKET_LIBS)
	@echo "built $(OBJ_DIR)/mock230"

# A second binary from the same sources, listening on 43597 rather than 43595.
# It exists so two people (or two agents) can each hold a live session against
# their own server without fighting over the port or over one output file.
# manifest_osrs230_dev.ini points at it; everything else is identical.
#
# The name is `dev_mock230`, not `mock230_dev`, on purpose: the usual way to
# stop a stray server is `pkill -f build/mock230`, and that pattern is a prefix
# match — anything named `mock230*` in the same directory dies with it.
mock230-dev: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -DMOCK230_DEFAULT_PORT=43597 -o $(OBJ_DIR)/dev_mock230 $(MOCK230_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm $(JS5_SOCKET_LIBS)
	@echo "built $(OBJ_DIR)/dev_mock230"

# MallocScribble is not a debugging flourish here, it is what makes this
# suite a gate at all.
#
# Measured 2026-08-06: six identical runs produce exactly TWO distinct failure
# sets, three each, differing in a pair of npc-pathing checks. Under
# MallocScribble/MallocPreScribble the result is the same every run. That
# signature — varies with heap contents, stabilises when the heap is poisoned —
# is a read of dead memory, and it is somewhere under the interaction re-flood
# path (`collision_flood` initialises its own dir/dist maps, so it is not those).
#
# **The site was found on 2026-08-08, and it was a use-after-free in the
# selftest's own fixture, not an uninitialised read in the engine.** The
# re-flood stanza took `mock230_scene_find_loc` for the castle door, then called
# `selftest_park_player`, then dereferenced the loc. A park ends in
# `maybe_rebuild` -> `mock230_scene_build` -> `mock230_scene_free`, which
# `free()`s `g_locs` — so both the slot index and the pointer died at the park,
# and the interaction was latched on whatever the freed page happened to hold.
# Under scribble that reads 0x55555555 every time, which is why poisoning made
# it deterministic. When the garbage happened to spell a routable loc the checks
# PASSED for the wrong reason, routing 74 steps to a loc 34 tiles away.
#
# The fixture now resolves the loc after the park (the same rule `p_oploc`
# states for itself in mock230_ops_player.c: a rebuild reallocates the array
# underneath anything holding into it). Both checks were mutation-tested
# afterwards — skipping `queue_path_as_waypoints` in the re-flood turns exactly
# those two red — so they measure the engine path they name rather than the heap.
#
# Scribble stays, because it turned a 1-in-8 flake into a reproducible failure
# and that is what made this findable. It deliberately errs the loud way:
# poisoned memory makes an affected check FAIL consistently rather than pass
# consistently, so the bug stays visible and the count stays comparable between
# runs. A suite whose failure count wandered 10..14 could not answer "did I
# break something", which is exactly how four regressions reached a user with
# "baseline, no regression" attached to them.
test-mock230-dev: mock230-dev
	cd $(REPO_ROOT) && MallocScribble=1 MallocPreScribble=1 \
		./src/$(OBJ_DIR)/dev_mock230 --selftest

# A third binary on 43599, same deal as mock230-dev above — one more parallel
# session that owns its own port and its own output file.
# manifest_osrs230_alt.ini points at it.
mock230-alt: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -DMOCK230_DEFAULT_PORT=43599 -o $(OBJ_DIR)/alt_mock230 $(MOCK230_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm
	@echo "built $(OBJ_DIR)/alt_mock230"

test-mock230-alt: mock230-alt
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/alt_mock230 --selftest

# A fourth binary on 43601, for the bank work. Same sources, same reason as the
# two above: a live session that owns its own port and its own output file.
# manifest_osrs230_bank.ini points at it.
# `rsprot_buf.o` is in this list because $(MOCK230_SRCS) includes
# mock239_interface_inbound.c, which wraps the rev-239 buffer readers. `mock230`
# links it and this target did not, so `mock230-bank` had been failing to link
# since that file joined the sources — a build break nothing ran often enough to
# notice.
mock230-bank: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -DMOCK230_DEFAULT_PORT=43601 -o $(OBJ_DIR)/bank_mock230 $(MOCK230_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm
	@echo "built $(OBJ_DIR)/bank_mock230"

test-mock230-bank: mock230-bank
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/bank_mock230 --selftest

# Mock server game logic with no socket: movement, scene rebuild, equipment
# (including the cache's wearpos_2/3 slot claims), inventory drag, npc roaming.
# Runs from the repo root so the default cache path (cache.osrs230) resolves —
# the equipment checks need the cache's wearpos fields to mean anything.
# Compile the mock's own content. The content itself lives in the OSRS-Content
# submodule, so a checkout without `git submodule update --init` has none of it.
# The output lands in mock230/scripts/build, which that repo's .gitignore
# excludes (`build/`), so a fresh checkout has no script pack until this runs.
# That IS fatal now, in the sense that matters: the engine's fallbacks are gated
# on a pack being loaded (enum Mock230Fallback), so a server without one prints
# a banner and then does almost nothing, rather than answering every trigger out
# of C and playing a second, silently different game.
# Ids are rev-230 ids from cache.osrs239, NOT LostCity's.
#
# `?=` so a content tree elsewhere (a second clone parked on a porting branch,
# which is how the ported/ lanes are usually carried) can be selected by
# exporting MOCK230_CONTENT_DIR once, instead of repeating it on every make
# command line. A command-line assignment still wins over both.
MOCK230_CONTENT_DIR ?= $(REPO_ROOT)/OSRS-Content/osrs239-content
# The four ported/ lanes used to be spelled out here — SUMMONING_CLIENT_LANE,
# RS2012_QBD_TD_CLIENT_LANE, CURSES_CLIENT_LANE, HERBLORE_ITEMS_LANE — and
# threaded through every compile recipe as --pack and --component-root roots.
# Each lane now describes itself in `<content>/ported/<lane>/lane.ini`, which is
# what makes adding a fifth lane, or leaving one out, a content edit rather than
# a makefile edit. See the mock230-scripts-lanes recipe below and
# src/serverscript/ssc_lane.h. The cache bakes and the per-lane cs2 tests still
# name their own lane paths (SUMMONING_LANE, CURSES_LANE, RS2012_QBD_TD_LANE):
# a bake is not a compile and reads different directories.
#
# `pack/` is the one symbol file per namespace (docs/CONTENT_PACK_PLAN.md §3).
# `names/` is the layer it replaced and may not exist; sscompile skips a missing
# --pack directory with a warning rather than failing, so both layouts build.
# This cross-boundary gate exists because ::crystal_set once failed on both
# sides at once: CS2 prefix-matched it as the local `cry` emote, and content had
# two global debugprocs with the same name. Run it before either half is built so
# neither a server-only script rebuild nor a client-cache bake can ship half a
# fix. The cache-independent command is ::~crystal_set; the server must remove
# that marker before both debugproc and built-in dispatch. Full incident:
# docs/CRYSTAL_SET_COMMAND.md.
check-crystal-set-contract:
	@python3 $(REPO_ROOT)/tools/check_crystal_set_contract.py --self-test \
		--content $(MOCK230_CONTENT_DIR)

check-gauntlet-contract:
	@cd $(REPO_ROOT) && python3 tools/check_gauntlet_contract.py

# Every Agility course against the per-lap experience its own wiki page states.
# The XP lives in triggers rather than a table, so ::agilityrun cannot see it
# and only this can: four courses were paying the wrong amount when it was
# written, and nothing in the tree noticed.
check-agility-xp:
	@cd $(REPO_ROOT) && python3 tools/agility_xp_audit.py --tree $(MOCK230_CONTENT_DIR)

.PHONY: check-wintertodt-rewards
check-wintertodt-rewards:
	@cd $(REPO_ROOT) && python3 tools/wintertodt_reward_audit.py --tree $(MOCK230_CONTENT_DIR)

check-godwars-manifest:
	@cd $(REPO_ROOT) && python3 tools/generate_godwars_combat_manifest.py --check

check-godwars-contract:
	@cd $(REPO_ROOT) && python3 tools/check_godwars_contract.py

# These targets consume the already-compiled full content pack. Keeping the
# script build separate lets a God Wars evidence run diagnose its own lane even
# when an unrelated content contract blocks the aggregate mock230-scripts gate.
.PHONY: godwars-evidence check-godwars-evidence test-godwars-restart
godwars-evidence: mock230 check-godwars-manifest check-godwars-contract
	@cd $(REPO_ROOT) && python3 tools/capture_godwars_evidence.py --skip-contracts \
		--server src/$(OBJ_DIR)/mock230

check-godwars-evidence: mock230 check-godwars-manifest check-godwars-contract
	@cd $(REPO_ROOT) && python3 tools/capture_godwars_evidence.py --check --skip-contracts \
		--server src/$(OBJ_DIR)/mock230

test-godwars-restart: mock230
	@cd $(REPO_ROOT) && python3 tools/test_godwars_restart.py \
		--server src/$(OBJ_DIR)/mock230 \
		--scripts OSRS-Content/osrs239-content/server/scripts/build

check-quest-combat-manifest:
	@cd $(REPO_ROOT) && python3 tools/generate_quest_combat_manifest.py --check

check-quest-combat-contract:
	@cd $(REPO_ROOT) && python3 tools/check_quest_combat_contract.py

check-charter-contract:
	@cd $(REPO_ROOT) && python3 tools/check_charter_contract.py

# The Dagannoth Kings are four authored npc params and nothing else, and those
# cannot be asserted from a script: `nc_param` reads the cache's param table
# while an authored `param=` row lands in the server's own def, which only
# `npc_param` sees — and that needs a live npc, which the C-driven selftest has
# no scene to provide. So the config is held to the pinned wiki here instead.
check-dks-contract:
	@cd $(REPO_ROOT) && python3 tools/check_dks_contract.py

# The solo bosses whose whole encounter is a combat contract — attack rate,
# attack style, and a hitpoints cross-check against the cache record. Same
# reason as check-dks-contract: an authored npc param cannot be asserted from a
# script here. The hitpoints check is on the CACHE rather than the config and
# catches a record picked by the wrong name, which is the likeliest mistake in a
# batch and the hardest to see afterwards.
check-corpbane:
	@cd $(REPO_ROOT) && python3 tools/gen_corpbane_weapons.py --check

check-agility-courses:
	@cd $(REPO_ROOT) && python3 tools/check_agility_course_contract.py

check-spellbooks:
	@cd $(REPO_ROOT) && python3 tools/gen_spellbook_tables.py --check

check-dbtable-rows:
	@cd $(REPO_ROOT) && python3 tools/check_dbtable_rows.py --check

check-agility-shortcuts:
	@cd $(REPO_ROOT) && python3 tools/gen_agility_shortcuts.py --check

check-krystilia-tasks:
	@cd $(REPO_ROOT) && python3 tools/gen_krystilia_tasks.py --check

check-chompy-hats:
	@cd $(REPO_ROOT) && python3 tools/gen_chompy_hats.py --check

check-konar-locations:
	@cd $(REPO_ROOT) && python3 tools/gen_konar_locations.py --check

check-clan-crystals:
	@cd $(REPO_ROOT) && python3 tools/gen_clan_crystals.py --check

check-boss-contract:
	@cd $(REPO_ROOT) && python3 tools/check_boss_contract.py

# The Theatre's Nylocas pillars are the one npc in the raid whose hitpoints go UP
# as the party shrinks (Jagex published 130 at five players and 330 solo). That
# inverts the convention every boss here uses, and the failure mode is silent:
# `npc_statheal` cannot raise an npc above its config base, so a support authored
# at the five-man 130 would hand every smaller party a five-man pillar with the
# scaling line still compiling and still running. No script command can read
# another npc type's base, so `~tobrun` cannot assert this and it lives here.
check-tob-pillar-contract:
	@cd $(REPO_ROOT) && python3 tools/check_tob_pillar_contract.py

test-construction-catalog:
	@cd $(REPO_ROOT) && python3 tools/check_construction_catalog.py --self-test \
		--content $(MOCK230_CONTENT_DIR)
	@cd $(REPO_ROOT) && python3 tools/generate_construction_crosswalk.py --check \
		--content $(MOCK230_CONTENT_DIR)
	@cd $(REPO_ROOT) && python3 tools/generate_poh_quest_status.py --check

test-poh-quest-status:
	@cd $(REPO_ROOT) && python3 tools/generate_poh_quest_status.py --check

test-poh-menagerie:
	@cd $(REPO_ROOT) && python3 tools/check_poh_menagerie.py

test-poh-league-hall:
	@cd $(REPO_ROOT) && python3 tools/check_poh_league_hall.py

test-poh-house-styles:
	@cd $(REPO_ROOT) && python3 tools/check_poh_house_styles.py

test-mock230-poh: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock230_poh_test \
		net/mock/test/mock230_poh_test.c net/mock/mock230_poh.c
	./$(OBJ_DIR)/mock230_poh_test

test-poh-servants:
	@cd $(REPO_ROOT) && python3 tools/check_poh_servants.py

# ---------------------------------------------------------------------------
# The script pack, and which content lanes go into it
# ---------------------------------------------------------------------------
#
# One recipe, parameterised by the lane list. It used to be four near-identical
# ones — default, summoning-on, curses-on, both-on — each repeating seven --pack
# roots, three --component-roots and a python step that staged a copy of the
# whole constant tree just to write one `^flag = 0`. A fifth lane meant a fifth
# copy of all of it, and there was no way to spell "compile without this lane":
# every recipe passed every lane's symbols and compiled every lane's scripts,
# with only the flag telling them apart at runtime.
#
# Now a lane describes itself, in `<content>/ported/<lane>/lane.ini`, and
# sscompile reads that (src/serverscript/ssc_lane.h). Nothing below names a lane
# path, a lane's packs or a lane's components; MOCK230_SCRIPT_LANES names lanes,
# and a lane not named is genuinely not in the pack.
#
# The two --pack defaults sscompile applies with no --pack at all are exactly
# $(MOCK230_CONTENT_DIR)/pack and /configs, and --constants defaults to --src,
# which is the real tree rather than a staged copy of it. So the whole command
# is the source, the output and the lanes.
MOCK230_SCRIPT_LANES ?=
MOCK230_SCRIPT_OUT ?= $(MOCK230_CONTENT_DIR)/server/scripts/build

# Allocate first, then compile: a `.enum` or `.dbtable` block declares a record
# by name and the compiler needs a number, so tools/ss_allocate.py has to have
# run before sscompile sees the tree. It only ever appends below its marker, so
# running it on every build is idempotent.
# check-crystal-set-contract on every pack, lane build included: it is the one
# gate that spans the client and server halves at once (docs/CRYSTAL_SET_COMMAND.md),
# so a lane build that skipped it could ship half a fix. The heavier content
# contracts hang off mock230-scripts alone — see there.
mock230-scripts-lanes: sscompile check-crystal-set-contract
	@mkdir -p $(MOCK230_SCRIPT_OUT)
	@python3 $(REPO_ROOT)/tools/ss_allocate.py --tree $(MOCK230_CONTENT_DIR)
	./$(OBJ_DIR)/sscompile$(PLATFORM_EXE_SUFFIX) \
		--src $(MOCK230_CONTENT_DIR)/server/scripts \
		--out $(MOCK230_SCRIPT_OUT) \
		--content-root $(MOCK230_CONTENT_DIR) \
		$(foreach lane,$(MOCK230_SCRIPT_LANES),--lane $(lane))

# What lanes this tree declares, and which of them a bare build would compile.
mock230-lanes: sscompile
	@./$(OBJ_DIR)/sscompile$(PLATFORM_EXE_SUFFIX) \
		--src $(MOCK230_CONTENT_DIR)/server/scripts \
		--content-root $(MOCK230_CONTENT_DIR) --list-lanes

# The ordinary pack: the base tree plus the lanes whose descriptors say
# `default=on` — the additive ones base content already names (the QBD's records,
# herb tar). The gated lanes are off, and off now means absent rather than
# present-but-flagged.
#
# This is the target every content contract hangs off, so it carries them all;
# the lane variants below are development builds of the same tree and do not
# re-run gates the default build has already passed.
mock230-scripts: check-dks-contract check-boss-contract check-clan-crystals check-konar-locations check-chompy-hats check-krystilia-tasks check-agility-shortcuts check-dbtable-rows check-spellbooks check-agility-courses check-corpbane check-crystal-set-contract check-agility-xp check-wintertodt-rewards check-godwars-manifest check-godwars-contract check-quest-combat-manifest check-quest-combat-contract check-charter-contract check-tob-pillar-contract
	@$(MAKE) mock230-scripts-lanes \
		MOCK230_SCRIPT_LANES= \
		MOCK230_SCRIPT_OUT=$(MOCK230_CONTENT_DIR)/server/scripts/build

# Summoning on. Same sources, same symbol roots; the lane is added rather than
# unlocked, so nothing about the default pack can drift into this one.
mock230-scripts-summoning:
	@$(MAKE) mock230-scripts-lanes \
		MOCK230_SCRIPT_LANES=scape2009_summoning \
		MOCK230_SCRIPT_OUT=$(MOCK230_CONTENT_DIR)/server/scripts/build_summoning

# Ancient Curses on, Summoning off: the two lanes are independent, and a build
# that wants both is the target below rather than this one twice.
mock230-scripts-curses:
	@$(MAKE) mock230-scripts-lanes \
		MOCK230_SCRIPT_LANES=rs558_ancient_curses \
		MOCK230_SCRIPT_OUT=$(MOCK230_CONTENT_DIR)/server/scripts/build_curses

test-summoning-phase3: mock230-scripts mock230-scripts-summoning test-mock239-playerinfo
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase3.py

# The server band, `mock230-scripts`' sibling (PORTING_GUIDE §3.6 item 1): the
# npc/loc server fields the boot reads from <content>/server/pack instead of
# re-parsing the text overlays. `--server-only` is the cheap form — no cache is
# copied or opened; a full `cachepack pack` writes the identical bands. Without
# this the boot warns and falls back to the text parse — which is still a real
# fallback, unlike the script pack's, whose absence now turns the engine's
# trigger fallbacks off rather than on (osrs230_mockserver.md §3.18).
.PHONY: test-rs2012-server-overlay mock230-cache-rs2012 test-cache-overlay-stale \
	test-droptable-versions test-loc-var-audit check-loc-var-carriers

# The freshness predicate the launchers (run-live.sh, run-live.bat) apply before
# asking for a marked-lane bake at all. It decides whether a lane's records are
# in the cache the client is about to boot, so its failure mode is a session run
# against content that was never packed. Hermetic and sub-second, and a
# prerequisite of both bakes so it cannot rot alongside a lane's layout.
test-cache-overlay-stale:
	@cd $(REPO_ROOT) && python3 tools/test_cache_overlay_stale.py

# tools/loc_var_audit.py's own tests. Hermetic and sub-second.
test-loc-var-audit:
	@cd $(REPO_ROOT) && python3 tools/test_loc_var_audit.py

# A varbit is a bit range inside a varp, and only the varp goes on the wire: a
# script that sets a loc's transform varbit whose carrier is not declared
# `transmit=yes` updates the server and nothing else, and the multiloc never
# moves for anybody. Nothing else in the build can see it — sscompile only
# checks that the varbit resolves, and mock230_world_mark_varp drops the packet
# without a word. 204 written varbits were in that state, over 1,811 loc
# placements, before this ran.
check-loc-var-carriers:
	@cd $(REPO_ROOT) && python3 tools/loc_var_audit.py --tree $(MOCK230_CONTENT_DIR) \
		--check-carriers

# The two drop-table invariants that shipped broken and that nothing else can
# see: every roster npc must *state* `param=death_drop` rather than inherit the
# tree-wide `[default] param=death_drop,bones`, and a page carrying several
# `dropversion` tables must bind each npc to its own rather than to the first
# one written. Reads the generated files, so it catches a bad regeneration as
# well as a bad generator. Hermetic and sub-second; see
# docs/NPC_WIKI_DROPTABLES_PLAN.md §9.
test-droptable-versions:
	@cd $(REPO_ROOT) && python3 tools/test_droptable_versions.py

test-rs2012-server-overlay:
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack
	@cd $(REPO_ROOT) && python3 tools/test_rs2012_server_overlay.py \
		--cachepack $(CACHEPACK_BIN)

mock230-servpack: test-rs2012-server-overlay
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack
	cd $(REPO_ROOT) && $(CACHEPACK_BIN) pack \
		--src $(MOCK230_CONTENT_DIR) --server-only

# The client half of the same bake: the tree written back into a cache the
# client can boot (PORTING_GUIDE §1, `cachepack pack ──► client cache (--out)`).
#
# `cache.osrs239` is the pristine unpack source and stays frozen, so nothing the
# tree says about a *client-visible* record reached a running client until this
# existed — a `.cs2` edit packed into no cache is an edit that ships nowhere, and
# the Display panel's layout dropdown was the case that found it. Server-side
# `.rs2` needs no bake because the ServerScript VM loads `script.dat` directly;
# CS2, interfaces and configs are read out of the cache and do.
#
# When is a bake required at all? Only for CLIENT-visible edits: CS2 recompiles,
# interface imports, sprites, native config fields, authored client patches.
# Pure server work — scripts, db tables, params, varps, npc/loc server fields —
# travels by `mock230-scripts` + `mock230-servpack` alone, and the world boots
# from the pristine cache by default (the one-cache rule: the world reads what
# JS5 serves; MOCK230_CACHE_DIR_DEFAULT in mock230.h). When you do bake, point
# BOTH the world and JS5 at the result.
#
# `--base` copies the pristine cache first, so every record the tree does not
# change keeps the bytes it had, and the output is a fresh 440M rather than an
# in-place append that grows without bound.
#
# Base and output are variables, not literals, so a bake can be aimed somewhere
# else without editing this file:
#
#   make -C src mock230-cache MOCK230_CACHE_DIR=$(REPO_ROOT)/cache.osrs239_packed
#
# The default output stays `cache.osrs239.baked` because manifest_osrs239_net.ini
# and the rev-230 manifests name that directory. Previously `--base`/`--out`
# were spelled literally in the recipe while MOCK230_CACHE_DIR was used only by
# the `rm -rf` above them, so the two could disagree and the wrong tree would be
# cleared.
MOCK230_CACHE_BASE ?= $(REPO_ROOT)/cache.osrs239
MOCK230_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs239.baked
# Optional RuneStar CS2 name tables (script/graphic/…). Without them, ~proc
# calls into .cs2b-only scripts cannot resolve (PROC no longer falls back to a
# sole clientscript alias — that rebound trampoline wrappers to themselves).
# Pack then keeps the base bytes. With the tables, trampolines recompile to the
# real proc id.
RUNESTAR_CS2_NAMES ?= $(HOME)/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2
mock230-cache: check-crystal-set-contract
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack
	@# --base is optional. With one, the pristine cache is copied and every record
	@# the tree does not change keeps the bytes it had. Without one, cachepack
	@# creates the cache and what lands is exactly what the content tree states --
	@# so a missing base is a narrower cache, not an error.
	rm -rf $(MOCK230_CACHE_DIR)
	cd $(REPO_ROOT) && \
		if [ -z "$$CACHEPACK_CS2_NAMES" ] && [ -d "$(RUNESTAR_CS2_NAMES)" ]; then \
			export CACHEPACK_CS2_NAMES="$(RUNESTAR_CS2_NAMES)"; \
		fi; \
		$(CACHEPACK_BIN) pack \
		--src $(MOCK230_CONTENT_DIR) \
		$(if $(wildcard $(MOCK230_CACHE_BASE)),--base $(MOCK230_CACHE_BASE),) \
		--out $(MOCK230_CACHE_DIR) \
		--rev osrs239 --assets --binary --gamevals
	@$(MAKE) --no-print-directory mock230-cache-check MOCK230_CACHE_DIR=$(MOCK230_CACHE_DIR)

# Every dat2 table an OldSchool cache carries, by idx id — the OSRS column of
# DAT2_TABLE_IDS_OSRS in 3rd/rscache/src/dat2disk.c. 16/17 and 23 are unused in
# this era; 255 is the reference-table index, without which nothing else in the
# cache is reachable.
#
# A bake is only "the whole cache" if every one of these lands. They come from
# three different writers (configs from the pack itself, the asset tables from
# --assets/--binary, gamevals from --gamevals), so a missing flag silently costs
# a table rather than failing — which is exactly what --gamevals did before it
# was added above.
MOCK230_CACHE_TABLES = 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 18 19 20 21 22 24 255

mock230-cache-check:
	@test -f "$(MOCK230_CACHE_DIR)/main_file_cache.dat2" || { \
		echo "mock230-cache-check: no cache at $(MOCK230_CACHE_DIR)" >&2; exit 1; }
	@missing=""; for t in $(MOCK230_CACHE_TABLES); do \
		[ -f "$(MOCK230_CACHE_DIR)/main_file_cache.idx$$t" ] || missing="$$missing $$t"; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "mock230-cache-check: $(MOCK230_CACHE_DIR) is missing idx:$$missing" >&2; \
		echo "  A table with no .idx file is a table the client cannot read." >&2; \
		exit 1; \
	fi; \
	echo "mock230-cache-check: all $(words $(MOCK230_CACHE_TABLES)) tables present in $(MOCK230_CACHE_DIR)"

# The cache-727 QBD/TD client records are isolated below `ported/` and therefore
# do not enter an ordinary `mock230-cache` bake. Compose that marked lane through
# its audited sparse stager, then verify the client-facing interface/sprite/script
# archives against the resulting cache. `manifest_osrs239_rs2012.ini` points the
# client at this directory; main.c hands the same resolved path to the embedded
# mock, so scene/NPC reads and JS5 cannot fall back to pristine cache.osrs239.
RS2012_OVERLAY_DIR = $(REPO_ROOT)/build/rs2012-overlay
RS2012_CACHE_BASE ?= $(REPO_ROOT)/cache.osrs239
RS2012_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs239.rs2012

mock230-cache-rs2012: check-crystal-set-contract test-rs2012-server-overlay test-cache-overlay-stale
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack audioprobe
	@# --base is optional, exactly as in mock230-cache. With a base cache the
	@# stager emits the sparse lane overlay and the pack resolves everything
	@# else from the base -- much less to copy and to write. Without one it
	@# stages the base tree behind the lane (--full) and unions the pack
	@# indexes, so the content repo alone produces a complete cache. The two
	@# routes must agree on the lane; `mock230-cache-rs2012-check` proves it.
	@cd $(REPO_ROOT) && python3 tools/test_stage_rs2012_overlay.py
	@cd $(REPO_ROOT) && python3 tools/stage_rs2012_overlay.py \
		--tree $(MOCK230_CONTENT_DIR) --out $(RS2012_OVERLAY_DIR) \
		$(if $(wildcard $(RS2012_CACHE_BASE)),,--full)
	@cd $(REPO_ROOT) && python3 tools/test_rs2012_audio_bridge.py \
		--content $(MOCK230_CONTENT_DIR) --staged $(RS2012_OVERLAY_DIR)
	@test "$(abspath $(RS2012_CACHE_DIR))" != "$(REPO_ROOT)" -a \
		"$(abspath $(RS2012_CACHE_DIR))" != "$(abspath $(RS2012_CACHE_BASE))" -a \
		"$(abspath $(RS2012_CACHE_DIR))" != "/" || { \
		echo "mock230-cache-rs2012: unsafe output $(RS2012_CACHE_DIR)" >&2; exit 1; }
	rm -rf "$(RS2012_CACHE_DIR)"
	@cd $(REPO_ROOT) && $(CACHEPACK_BIN) pack \
		--src $(RS2012_OVERLAY_DIR) \
		$(if $(wildcard $(RS2012_CACHE_BASE)),--base $(RS2012_CACHE_BASE),) \
		--out $(RS2012_CACHE_DIR) --rev osrs239 --assets --binary --gamevals
	@$(MAKE) --no-print-directory mock230-cache-check MOCK230_CACHE_DIR=$(RS2012_CACHE_DIR)
	@tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
		cd $(REPO_ROOT) && $(CACHEPACK_BIN) verify \
			--cache $(RS2012_CACHE_DIR) --rev osrs239 --src $(RS2012_OVERLAY_DIR) \
			--assets=interfaces,sprites,scripts,synth,songs,samples,patches --tmp "$$tmp"
	@cd $(REPO_ROOT) && python3 tools/test_rs2012_audio_bridge.py \
		--content $(MOCK230_CONTENT_DIR) \
		--cache $(RS2012_CACHE_DIR) --audioprobe $(AUDIOPROBE_BIN)
	@echo "mock230-cache-rs2012: use ./run-live.sh manifest_osrs239_rs2012.ini"

# The 2009scape Summoning client records are deliberately absent from the
# ordinary content walk.  A feature-on cache is a second pass over the explicit
# marked-lane staging tree.  Its CS2 gate is separate because the committed
# ordinary tree has known stale sources: cachepack keeps base bytes when a
# source declines, so only an exact non-zero `compiled N, failed 0` is evidence.
SUMMONING_LANE = $(MOCK230_CONTENT_DIR)/ported/scape2009_summoning
SUMMONING_OVERLAY_DIR = $(REPO_ROOT)/build/summoning-overlay
SUMMONING_CACHE_BASE ?= $(REPO_ROOT)/cache.osrs239.baked
SUMMONING_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs239.summoning

test-summoning-cs2:
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cs2
	@test -d "$(RUNESTAR_CS2_NAMES)" || { \
		echo "test-summoning-cs2: missing RuneStar names: $(RUNESTAR_CS2_NAMES)" >&2; exit 1; }
	@tmp=$$(mktemp -d); log=$$(mktemp); trap 'rm -rf "$$tmp" "$$log"' EXIT; \
		expected=$$(find "$(SUMMONING_LANE)/scripts" -type f -name '*.cs2' | wc -l | tr -d ' '); \
		[ "$$expected" -gt 0 ] || { echo "test-summoning-cs2: zero scripts" >&2; exit 1; }; \
		$(CS2_BIN) compile --src "$(SUMMONING_LANE)/scripts" \
			--raw "$(MOCK230_CACHE_BASE)" --rev osrs239 \
			--names "$(RUNESTAR_CS2_NAMES)" --out "$$tmp" >"$$log" 2>&1; \
		cat "$$log"; \
		grep -qx "compiled $$expected, failed 0" "$$log" || { \
			echo "test-summoning-cs2: expected compiled $$expected, failed 0" >&2; exit 1; }

test-summoning-isolation:
	@cd $(REPO_ROOT) && python3 tools/check_summoning_isolation.py \
		--tree $(MOCK230_CONTENT_DIR) --check

# Phase 5a establishes whether a generated roster candidate may enter the
# feature-on cache at all. It deliberately has no roster render/summon proof;
# that bounded cohort acceptance is Phase 5b.
test-summoning-phase5a:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase5a.py \
		--tree $(MOCK230_CONTENT_DIR)

# Phase 5b admits one exact closure beside the held review-only experiment.
# It runs before a feature-on bake, never before the ordinary cache path.
test-summoning-phase5b:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase5b.py \
		--tree $(MOCK230_CONTENT_DIR)

# Superseded by the 77 per-familiar cohorts merged from v3, which cover all 45
# of this cohort's sources and claim its destination ranges. Kept, but out of
# the gates: it cannot pass until the corpus cohort is re-based onto free ranges
# (npc/obj 27248+/47248+) or retired.
test-summoning-phase5c-corpus:
	@cd $(REPO_ROOT) && python3 tools/generate_summoning_corpus.py --check
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase5c_corpus.py

# Phase 6a adds only a cache-native, player-owned BoB inventory foundation.
# UI, actual familiar transfer rules, and shared shop scope remain separate.
test-summoning-phase6:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase6.py \
		--tree $(MOCK230_CONTENT_DIR)

test-summoning-phase6-cache:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase6_cache.py \
		--cache $(SUMMONING_CACHE_DIR)

# Every active familiar has a source scroll mapping. Shared scrolls collapse
# those 78 mappings to 67 rev-530 objects; the guide's later Fetch Casket row
# adds one rev-727 object, for 68 packed icons and 63 inventory models total.
test-summoning-scroll-assets:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_scroll_assets.py \
		--tree $(MOCK230_CONTENT_DIR)

# The Pets subsection has 33 rows sourced from the rev-727 guide.  Each row
# owns a packed display object and inventory model so script 1904 can render it.
test-summoning-pet-guide-assets:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_pet_guide_assets.py \
		--tree $(MOCK230_CONTENT_DIR)

test-summoning-specials:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_specials.py
	@cd $(REPO_ROOT) && python3 tools/test_summoning_source_provenance.py

test-summoning-source-provenance:
	@cd $(REPO_ROOT) && python3 tools/test_summoning_source_provenance.py

# Runtime proof is intentionally separate from the cache and port gates. It
# retains the Phase-5b structural/staging fingerprint as a prerequisite, builds
# a normal non-ASAN embedded client, and drives a fresh real pouch UI.
test-summoning-phase5b-runtime: test-summoning-phase5b mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 ENABLE_ASAN=0 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase5b_runtime.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase6b: mock230-cache-summoning mock230-scripts-summoning
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase6b.py

test-summoning-phase7d: mock230-cache-summoning mock230-scripts-summoning
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase7d.py

# Familiar npc op1. Needs the client because the proof is the real OPNPC1
# packet; --static-only drops that and checks the bindings and source values.
test-summoning-interact: mock230-cache-summoning mock230-scripts-summoning
	@cd $(REPO_ROOT) && python3 tools/test_summoning_interact.py

# The familiar leash: an owner who teleports away gets their familiar back.
# Needs the client because where an npc stands is only observable through a
# live session; --static-only drops that and checks the shared leash range.
test-summoning-leash: mock230-cache-summoning mock230-scripts-summoning
	@cd $(REPO_ROOT) && python3 tools/test_summoning_leash.py

# The same leash across a map instance, for both kinds of follower. Nobody walks
# into a private copy of a square, so this is the crossing `playerfollow` cannot
# make — and a pet had no tick to leash from at all. Two client runs.
test-summoning-instance: mock230-cache-summoning mock230-scripts-summoning
	@cd $(REPO_ROOT) && python3 tools/test_summoning_instance.py

# Recompile summoning_dialogue.rs2 from the checked-in transcript corpus. The
# corpus itself is a one-off wiki pull — `build_familiar_dialogue_corpus.py
# --fetch` refreshes it, and its diff wants reading before it is committed. This
# target never reaches the network.
summoning-dialogue:
	@cd $(REPO_ROOT) && python3 tools/generate_familiar_dialogue_script.py

test-summoning-phase7f: mock230-pack
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase7f.py
	@cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230_pack$(PLATFORM_EXE_SUFFIX) \
		--content $(MOCK230_CONTENT_DIR) --cache $(MOCK230_CACHE_BASE) --check-only

summoning-texture-review: mock230-cache-summoning
	@cd $(REPO_ROOT) && python3 tools/build_summoning_texture_review.py

test-summoning-import:
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack
	@cd $(REPO_ROOT) && python3 tools/test_summoning_import.py

# Supply two snapshots around any ordinary/flag-off operation.  The helper
# compares every regular cache file and refuses a vacuous zero-check pass.
test-summoning-byte-identity:
	@test -n "$(SUMMONING_CACHE_BEFORE)" -a -n "$(SUMMONING_CACHE_AFTER)" || { \
		echo "usage: make test-summoning-byte-identity SUMMONING_CACHE_BEFORE=... SUMMONING_CACHE_AFTER=..." >&2; exit 1; }
	@test "$(abspath $(SUMMONING_CACHE_BEFORE))" != "$(abspath $(SUMMONING_CACHE_AFTER))" || { \
		echo "test-summoning-byte-identity: before and after must be distinct snapshots" >&2; exit 1; }
	@cd $(REPO_ROOT) && python3 tools/stage_summoning_overlay.py \
		--compare-cache "$(SUMMONING_CACHE_BEFORE)" "$(SUMMONING_CACHE_AFTER)"

# ------------------------------------------------------------------
# Ancient Curses feature-on cache
# ------------------------------------------------------------------
#
# Based on cache.osrs239.baked, so the flag-on cache is the ordinary content
# plus the lane rather than the pristine cache plus the lane.
#
# The two assertions after the pack are the ones that matter. `cachepack pack`
# reports `script failed` and `codec-declined` counts and still exits zero: a
# clientscript that fails to compile silently ships the BASE cache's bytes for
# that id, so the only proof the patches landed is to decompile them back out
# and look for the third book's arm.
CURSES_CACHE_BASE ?= $(REPO_ROOT)/cache.osrs239.baked
CURSES_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs239.curses
CURSES_OVERLAY_DIR = $(REPO_ROOT)/build/curses-overlay
# As with SUMMONING_LANE: the cs2 test compiles the lane's own scripts, so it
# needs the path, not the lane.ini discovery the server-script build uses.
CURSES_LANE = $(MOCK230_CONTENT_DIR)/ported/rs558_ancient_curses

mock230-cache-curses: check-crystal-set-contract test-curses-isolation test-curses-cs2
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack cs2
	@test -f "$(CURSES_CACHE_BASE)/main_file_cache.dat2" || { \
		echo "mock230-cache-curses: no base cache at $(CURSES_CACHE_BASE); run mock230-cache first" >&2; exit 1; }
	@cd $(REPO_ROOT) && python3 tools/stage_curses_overlay.py \
		--tree $(MOCK230_CONTENT_DIR) --out $(CURSES_OVERLAY_DIR)
	rm -rf "$(CURSES_CACHE_DIR)"
	@cd $(REPO_ROOT) && CACHEPACK_CS2_NAMES="$(RUNESTAR_CS2_NAMES)" \
		$(CACHEPACK_BIN) pack --src $(CURSES_OVERLAY_DIR) \
		--base $(CURSES_CACHE_BASE) --out $(CURSES_CACHE_DIR) \
		--rev osrs239 --assets --binary --gamevals
	@$(MAKE) --no-print-directory mock230-cache-check MOCK230_CACHE_DIR=$(CURSES_CACHE_DIR)
	@tmp=$$(mktemp -d); log=$$(mktemp); trap 'rm -rf "$$tmp" "$$log"' EXIT; \
		$(CS2_BIN) decompile --cache "$(CURSES_CACHE_DIR)" --rev osrs239 \
		--names "$(RUNESTAR_CS2_NAMES)" --out "$$tmp" 463 547 6723 7053 7054 7055 7823 >"$$log" 2>&1; \
		cat "$$log"; \
		grep -Rqs 'return(6035)' "$$tmp" || { echo "mock230-cache-curses: script 7823 has no curses arm in the baked cache" >&2; exit 1; }; \
		grep -Rqs 'varbit20412' "$$tmp" || { echo "mock230-cache-curses: the active-mask varbit did not reach the baked cache" >&2; exit 1; }; \
		echo "curses cs2: third-book arm and mask varbit present in $(CURSES_CACHE_DIR)"

# ------------------------------------------------------------------
# All lanes on: the combination manifest_osrs239_torirs.ini boots
# ------------------------------------------------------------------
#
# The default manifest carries rs2012 + Summoning + Ancient Curses, so it needs
# a script pack with BOTH content flags set and a cache with all three lanes
# staged. The lanes are independent, so this is a chain rather than a merge:
# rs2012-summoning is baked first and the curses overlay goes on top of it.
CURSES_ALL_CACHE_BASE = $(REPO_ROOT)/cache.osrs239.rs2012-summoning
CURSES_ALL_CACHE_DIR = $(REPO_ROOT)/cache.osrs239.rs2012-summoning-curses

# Both gated lanes at once, for manifest_osrs239_torirs.ini. Two names on one
# line rather than a third recipe: that the combination is expressible at all,
# without restating either lane's inputs, is the point of the descriptors.
mock230-scripts-summoning-curses:
	@$(MAKE) mock230-scripts-lanes \
		MOCK230_SCRIPT_LANES="scape2009_summoning rs558_ancient_curses" \
		MOCK230_SCRIPT_OUT=$(MOCK230_CONTENT_DIR)/server/scripts/build_summoning_curses

# The curses overlay on top of the rs2012+Summoning cache. Same recipe as
# mock230-cache-curses with the base and output moved, which is why the id
# bases were picked clear of every other lane in the first place.
#
# The rs2012+Summoning cache is treated as an INPUT that must already exist,
# not as a target to rebuild. Rebuilding it runs test-summoning-phase5a, which
# is currently red for reasons that predate this lane: port/summoning_530.map is
# committed with 311 seq and 21 synth rows while the test's preserved
# fingerprint still expects 305 and 8 (the "summoning based on Nocturne"
# commit moved one and not the other). That is the Summoning lane's to fix —
# muting it here to green this build is exactly what
# .cursor/rules/no-park-sibling-content.mdc forbids — but neither should a
# stale fingerprint in another lane block this one from baking.
#
#   make -C src mock230-cache-rs2012-summoning   # when that test is green again
mock230-cache-all:
	@test -f "$(CURSES_ALL_CACHE_BASE)/main_file_cache.dat2" || { \
		echo "mock230-cache-all: no base cache at $(CURSES_ALL_CACHE_BASE)." >&2; \
		echo "  build it with: make -C src mock230-cache-rs2012-summoning" >&2; exit 1; }
	@$(MAKE) --no-print-directory mock230-cache-curses \
		CURSES_CACHE_BASE=$(CURSES_ALL_CACHE_BASE) \
		CURSES_CACHE_DIR=$(CURSES_ALL_CACHE_DIR)

# The lane's clientscripts must all compile before anything bakes: a decline
# ships base-cache bytes with only a counter to say so.
test-curses-cs2:
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cs2
	@test -d "$(RUNESTAR_CS2_NAMES)" || { \
		echo "test-curses-cs2: missing RuneStar names: $(RUNESTAR_CS2_NAMES)" >&2; exit 1; }
	@tmp=$$(mktemp -d); log=$$(mktemp); trap 'rm -rf "$$tmp" "$$log"' EXIT; \
		expected=$$(find "$(CURSES_LANE)/scripts" -type f -name '*.cs2' | wc -l | tr -d ' '); \
		[ "$$expected" -gt 0 ] || { echo "test-curses-cs2: zero scripts" >&2; exit 1; }; \
		$(CS2_BIN) compile --src "$(CURSES_LANE)/scripts" \
			--cache "$(MOCK230_CACHE_BASE)" --rev osrs239 \
			--names "$(RUNESTAR_CS2_NAMES)" --out "$$tmp" >"$$log" 2>&1; \
		cat "$$log"; \
		grep -qx "compiled $$expected, failed 0" "$$log" || { \
			echo "test-curses-cs2: expected compiled $$expected, failed 0" >&2; exit 1; }

test-curses-isolation:
	@cd $(REPO_ROOT) && python3 tools/check_curses_isolation.py \
		--tree $(MOCK230_CONTENT_DIR) --check

mock230-cache-summoning: check-crystal-set-contract test-summoning-isolation test-summoning-phase5a test-summoning-phase5b test-summoning-phase6 test-summoning-scroll-assets test-summoning-pet-guide-assets test-summoning-cs2 test-cache-overlay-stale
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack cs2
	@test -f "$(SUMMONING_CACHE_BASE)/main_file_cache.dat2" || { \
		echo "mock230-cache-summoning: no base cache at $(SUMMONING_CACHE_BASE)" >&2; exit 1; }
	@cd $(REPO_ROOT) && python3 tools/stage_summoning_overlay.py \
		--tree $(MOCK230_CONTENT_DIR) --out $(SUMMONING_OVERLAY_DIR)
	rm -rf "$(SUMMONING_CACHE_DIR)"
	@cd $(REPO_ROOT) && CACHEPACK_CS2_NAMES="$(RUNESTAR_CS2_NAMES)" \
		$(CACHEPACK_BIN) pack --src $(SUMMONING_OVERLAY_DIR) \
		--base $(SUMMONING_CACHE_BASE) --out $(SUMMONING_CACHE_DIR) \
		--rev osrs239 --assets --binary --gamevals
	@$(MAKE) --no-print-directory mock230-cache-check MOCK230_CACHE_DIR=$(SUMMONING_CACHE_DIR)
	@$(MAKE) --no-print-directory test-summoning-phase6-cache
	@tmp=$$(mktemp -d); log=$$(mktemp); trap 'rm -rf "$$tmp" "$$log"' EXIT; \
		$(CS2_BIN) decompile --cache "$(SUMMONING_CACHE_DIR)" --rev osrs239 \
		--names "$(RUNESTAR_CS2_NAMES)" --out "$$tmp" 393 1198 1904 8950 12000 12001 12002 12003 12004 >"$$log" 2>&1; \
		cat "$$log"; grep -qx 'decompiled 9, failed 0, total 9' "$$log"; \
		grep -Rqs '^\[clientscript,.*\](component .*int .*int .*int .*)' "$$tmp"; \
		grep -Rqs 'case 24 :' "$$tmp"; grep -Rqs 'varbit20411' "$$tmp"; \
		echo "mock230-cache-summoning: verified 9 clientscript archives"

# Both marked lanes in one cache, for manifest_osrs239_rs2012_summoning.ini.
#
# Neither lane's own bake carries the other's records — `ported/` is out of the
# ordinary content walk precisely so that a lane reaches a client through one
# route only — so a session that wants a familiar fighting the dragon needs the
# two composed. Composition is layering, not a third stager: `cachepack pack`
# keeps every base byte a source does not claim, so the Summoning overlay
# packed over the QBD/TD cache leaves the imported rev-727 npc/loc/model ids
# exactly as that bake wrote them.
#
# Order is QBD/TD first. The Summoning lane's CS2 is authored and gated against
# the pristine cache (test-summoning-cs2 compiles with --raw MOCK230_CACHE_BASE),
# so it is the layer that must land last, on top of whatever the other lane
# staged.
#
# Base and output must be different directories: mock230-cache-summoning deletes
# its output before packing, so aiming both at one path would delete the base
# mid-recipe. The QBD/TD cache is reused as-is rather than rebuilt into a
# private copy — it is the same bytes manifest_osrs239_rs2012.ini boots, which
# is what makes a defect seen here reproducible there.
RS2012_SUMMONING_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs239.rs2012-summoning

.PHONY: mock230-cache-rs2012-summoning
mock230-cache-rs2012-summoning:
	@test -f "$(RS2012_CACHE_DIR)/main_file_cache.dat2" || { \
		echo "mock230-cache-rs2012-summoning: no QBD/TD base at $(RS2012_CACHE_DIR)" >&2; \
		echo "  That cache is this composition's first layer; bake it with" >&2; \
		echo "    $(MAKE) -C src mock230-cache-rs2012" >&2; \
		exit 1; }
	@test "$(abspath $(RS2012_SUMMONING_CACHE_DIR))" != "$(abspath $(RS2012_CACHE_DIR))" || { \
		echo "mock230-cache-rs2012-summoning: output must differ from its base" >&2; exit 1; }
	@$(MAKE) --no-print-directory mock230-cache-summoning \
		SUMMONING_CACHE_BASE=$(RS2012_CACHE_DIR) \
		SUMMONING_CACHE_DIR=$(RS2012_SUMMONING_CACHE_DIR)
	@echo "mock230-cache-rs2012-summoning: use ./run-live.sh manifest_osrs239_rs2012_summoning.ini"

test-summoning-phase1: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase1.py \
		--client $(SRC_DIR)/torirs --flag-on $(SUMMONING_CACHE_DIR) \
		--flag-off $(REPO_ROOT)/cache.osrs239 --scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase4: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase4.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase4c: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase4c.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase4d: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase4d.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

# Ordinary world replies must not be mistaken for unfinished CS2/UI ticks, and
# a real settlement pause must not discard its pending mouse-up. This burst used
# to inject ten releases but reach the world-click path only six or seven times.
test-cs2-world-click-responsiveness: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_cs2_world_click_responsiveness.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase4e: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase4e.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

test-summoning-phase4f: mock230-cache-summoning mock230-scripts-summoning mock230-servpack
	@$(MAKE) --no-print-directory PLATFORM_OBJ_BASE=$(PLATFORM_OBJ_BASE) \
		EMBED_SERVER=1 TORIDRAW_OPT=1 all
	@cd $(REPO_ROOT) && python3 tools/test_summoning_phase4f.py \
		--client $(SRC_DIR)/torirs --cache $(SUMMONING_CACHE_DIR) \
		--scripts $(MOCK230_CONTENT_DIR)/server/scripts/build_summoning \
		--manifest $(REPO_ROOT)/manifest_osrs239.ini

# Content validator + cache exporter. Separate binary from the server: it needs
# the same content and cache readers but none of the socket or tick code, and it
# is run by hand rather than as part of a build.
#
# `cp_membership.c` comes in from cachepack rather than being re-implemented.
# `pack/<ns>.client` / `.server` have one parser, and the two tools that read them
# — cachepack's provenance check and this validator's id check — are two callers
# of it. A second reader is how the two halves of one check end up disagreeing
# about which lines a file holds. It depends on nothing but stdio, which is why
# this direction of borrowing is the safe one: cachepack still links nothing from
# src/ (see cp_register.h).
MOCK230_PACK_SRCS = net/mock/mock230_pack.c net/mock/mock230_content.c \
	net/mock/mock230_db.c \
	net/mock/mock230_ids.c \
	net/mock/mock230_objinfo.c net/mock/mock230_npcinfo.c net/mock/mock230_seqinfo.c \
	net/mock/mock230_dbinfo.c net/mock/mock230_healthbarinfo.c \
	net/mock/mock230_locinfo.c net/mock/mock230_paramtable.c \
	net/mock/mock230_servercodec.c net/mock/mock230_servpack.c \
	net/mock/mock230_zone_index.c net/mock/mock230_varbit.c \
	net/mock/mock230_scene.c net/mock/mock230_loc_ops.c net/mock/mock230_mapinstance.c \
	net/mock/mock230_shop.c net/mock/mock230_bank_tables.c \
	engine/world_builder/collision_map.c features/features.c \
	net/mock/mock230_pack_stubs.c \
	$(RSCACHE_DIR)/tools/cachepack/cp_membership.c \
	content/content_register.c content/content_fields.c $(INI_SRC)
mock230-pack: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(RSCACHE_DIR)/tools/cachepack \
		-o $(OBJ_DIR)/mock230_pack $(MOCK230_PACK_SRCS) \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	@echo "built $(OBJ_DIR)/mock230_pack"

# `mock230-scripts` and not just `mock230`: combat, prayer and the interface
# arming are content now, so an assertion about them is an assertion about
# `script.dat`. Without this the selftest happily runs a binary built from
# today's C against a script pack built days ago — which is not a stale-result
# hazard in theory. Removing the prayer multiplier from
# `[proc,combat_effective_stat]` and re-running left the suite fully green,
# because the .rs2 edit had never been compiled. A negative control that cannot
# go red is worse than no negative control, since it reads as proof.
test-mock230: mock230 mock230-scripts mock230-servpack check-crystal-set-contract check-gauntlet-contract
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230 --selftest

# The opcode-coverage table is derived from the `case SS_OP_*:` labels in the VM
# core and the host command seam, so it cannot disagree with them — provided it
# is regenerated when either changes. This fails when it is not.
#
# The failure direction is safe either way: a stale table over-reports, so the
# load-time gap report nags about an opcode that is actually implemented rather
# than hiding one that is not.
test-mock230-coverage:
	python3 net/mock/gen_opcode_coverage.py --check

# The `*_param` family: the loc and struct tables, and the stack the result
# lands on. Links the server minus its main(), plus the three compiler-only
# sources — `MOCK230_CORE_SRCS` already carries the VM, the register and ini,
# which is why `SSC_SRCS` cannot be used whole here.
#
# It needs the real cache and the real content tree, and it is exhaustive over
# both param groups, so it is a second-long test rather than an instant one.
# `net/wordpack.c` for the same reason MOCK230_SRCS carries it: MESSAGE_PRIVATE
# put wordpack calls into mock230_encode.c and mock230_world.c, both of which are
# in MOCK230_CORE_SRCS, so every consumer of CORE needs it. These three targets
# were authored on a branch where that was not yet true, which is why the merge
# was textually clean and then failed at `_wordpack_pack, referenced from`.
PARAM_TEST_SRCS = $(MOCK230_CORE_SRCS) net/jbase37.c net/wordpack.c \
	serverscript/ssc_lex.c serverscript/ssc_symbols.c serverscript/ssc_compile.c
.PHONY: test-mock230-param
test-mock230-param: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/param_test \
		net/mock/test/param_test.c $(PARAM_TEST_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/param_test

# The two run-energy models the era table selects between, and the era/name
# wiring that selects them. Two source files and no fixtures: the formulas were
# split out of the world tick precisely so they could be checked without a
# server, a player or a cache.
.PHONY: test-run-energy
test-run-energy: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/run_energy_test \
		net/mock/test/run_energy_test.c net/mock/mock230_runenergy.c features/features.c
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/run_energy_test

# Shared dynamic-map session registers: bounded slots, signed values and the
# important handle-reuse invariant (a new tenant never inherits the old
# tenant's game phase, score or prize balance).
.PHONY: test-mock230-mapinstance
test-mock230-mapinstance: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/mock230_mapinstance_test \
		net/mock/test/mock230_mapinstance_test.c net/mock/mock230_mapinstance.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230_mapinstance_test

# `split_init`: which markup a continuation row inherits. Links the splitter and
# the string pool only -- the world and the VM are supplied as two stub seams in
# the test itself, so this stays instant despite needing the real cache font.
.PHONY: test-mock230-split
test-mock230-split: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/split_test \
		net/mock/test/split_test.c net/mock/mock230_split.c \
		serverscript/ssvm_strpool.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/split_test

# The collision map the server routes on, against real map squares: wall bits
# paired on both tiles of every edge, closed doors blocking their own edge, no
# diagonal step across any doorway open or shut, and a door that opens and
# closes leaving the map byte-identical. That last one is the regression — a
# door swinging flush against a wall used to clear the wall's bits when it shut,
# on 75 of the game's doors.
#
# Only the scene builder and the collision map link here: no content, no VM, no
# world. `mock230_varbit_get` is stubbed in the test for the multiloc resolve.
# TORIRS_COLLISION_DOORS_FULL=1 sweeps the whole overworld instead of the
# curated squares (~45s).
.PHONY: test-collision-doors
test-collision-doors: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/collision_doors_test \
		net/mock/test/collision_doors_test.c \
		net/mock/mock230_scene.c net/mock/mock230_loc_ops.c net/mock/mock230_mapinstance.c \
		engine/world_builder/collision_map.c features/features.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/collision_doors_test

# "Is this tile standable, and what is standable near it?" — the geometry
# question authoring an agility course asks nine times per course. Same link
# line as test-collision-doors (scene builder + collision map, no content, no
# world), because it asks the same collision map the server routes on.
.PHONY: loc-placement-probe
# "Where is this loc placed, and which way does it face?" — the companion to
# walkable-probe, and the other half of what authoring an obstacle handler
# needs. Same link line: it builds a real scene out of real map squares.
loc-placement-probe: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/loc_placement_probe \
		net/mock/test/loc_placement_probe.c \
		net/mock/mock230_scene.c net/mock/mock230_loc_ops.c net/mock/mock230_mapinstance.c \
		engine/world_builder/collision_map.c features/features.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	@echo "built $(OBJ_DIR)/loc_placement_probe"

.PHONY: walkable-probe
walkable-probe: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/walkable_probe \
		net/mock/test/walkable_probe.c \
		net/mock/mock230_scene.c net/mock/mock230_loc_ops.c net/mock/mock230_mapinstance.c \
		engine/world_builder/collision_map.c features/features.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	@echo "built $(OBJ_DIR)/walkable_probe"

# The `loc_*` / `lc_*` config reads. Same two halves as test-mock230-param and
# the same link line, plus one thing that test does not need: it builds a real
# scene and adds a real loc to it, because `loc_param` and `loc_name` read the
# *active* loc and a fixture that set the pointer to a made-up slot would prove
# nothing about the slot-not-pointer convention that exists to survive a rebuild.
.PHONY: test-mock230-loc
test-mock230-loc: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/loc_test \
		net/mock/test/loc_test.c $(PARAM_TEST_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/loc_test

# The `npc_*` / `nc_*` reads and the hunt searches. Same two halves and the same
# link line again, plus the one thing the other two do not need: a real (zeroed,
# heap-allocated) `struct Mock230Server`, because `npc_hunt` and `npc_huntall`
# walk the npc roster and write the shared iterator. Still no world, no socket
# and no pack — the npcs go straight into the pool.
.PHONY: test-mock230-npc
test-mock230-npc: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/mock -o $(OBJ_DIR)/npc_test \
		net/mock/test/npc_test.c $(PARAM_TEST_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
		$(OBJ_DIR)/rsprot_buf.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/npc_test

# The namespace register: defaults, [namespace:*] overlay, pack-filename
# resolution. Its failure mode is silence — a misspelled content.ini loads
# perfectly with everything it meant to declare ignored — so the assertions that
# matter are the ones proving the file was read.
# Every content bar in one command.
#
# There was no aggregate target, and that is the second reason the sscompile
# regression stayed invisible for a day: each bar was a separate thing to remember
# to run, so the one that would have caught it was simply never run. A regression
# that only shows up in a command nobody types is not covered.
#
# Deliberately does not include `make -C 3rd/rscache test` — that needs a cache and
# takes minutes; it is the other half and is run on its own.
.PHONY: test-servercodec test-content test-port test-membership test-dbindex
test-content: test-content-register test-servercodec test-ss-symbols test-construction-catalog mock230-scripts mock230-servpack test-membership mock230-pack test-server-clean test-port
	@echo "--- content: validating the tree against the cache ---"
	@cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230_pack$(PLATFORM_EXE_SUFFIX)
	@echo "test-content: all content bars passed"

# The separation property, pinned as a command: pure server work touches no
# client-side file. After the full server pipeline (allocator, compiler, band —
# all run by this target's prerequisites), the machine-owned client mirrors must
# be exactly as git left them: configs/ (the cache's member indexes and text
# exports), pack/*.pack (layer-0 name tables) and pack/*.client (explicit client
# claims). The allocator writes pack/<ns>.alloc, the compiler writes
# server/scripts/build, the band writes server/pack — and nothing else moves.
# This is the gate that turns "server content stays out of the client cache"
# from a design intent into a failure you can see, and it fails on the tree
# this refactor started from (ss_allocate appended to the compacks).
test-server-clean: mock230-scripts mock230-servpack
	@echo "--- server-clean: a server build must not touch client-side files ---"
	@cd $(MOCK230_CONTENT_DIR) && \
	DIRTY=$$(git status --porcelain -- configs 'pack/*.pack' 'pack/*.client' 2>/dev/null); \
	if [ -n "$$DIRTY" ]; then \
		echo "test-server-clean: the server pipeline modified client-side files:"; \
		echo "$$DIRTY"; \
		exit 1; \
	fi
	@echo "test-server-clean: configs/ and client packs untouched"

# The entity split's agreement checks (docs/PACK_ENTITY_SPLIT_PLAN.md §3.3).
#
# `pack/<ns>.client` and `pack/<ns>.server` say which *entities* have a half on
# each side. That fact is stated twice on purpose, and these are the two readings
# of the second statement:
#
#   membership vs provenance   here. Every name in a file resolves to a record
#                              some layer states; every name in `<ns>.server` is
#                              one the tree either authored a block for or routes
#                              a field to; and every record the tree authors is
#                              named by a file. Needs the two-rank merge, which is
#                              the packer's, so it runs in cachepack.
#   membership vs the id range in `mock230_pack`, below — `server_base` is the
#                              register's and cachepack links nothing from src/.
#
# Split across two binaries because each half runs where its authority lives, and
# neither half restates the other's fact. Neither is what *enforces* membership —
# `cachepack pack` routes on the same files (§4 step 3) and fails on the plan's
# cells (a) and (c) itself. These two are the second reading: the packer can only
# ask what the files say, and these ask whether what they say is true of the tree.
test-membership:
	@echo "--- membership: entities against provenance ---"
	$(SUBMAKE) -C $(RSCACHE_DIR)/tools cachepack
	@cd $(REPO_ROOT) && $(CACHEPACK_BIN) membership \
		--src $(MOCK230_CONTENT_DIR) --check-only

# The port's name-resolution gate (docs/LOSTCITY_PORT_TRIAGE.md §9 step 2).
#
# Two bars the compilers and the validator cannot state:
#
#   ss_unresolved --check   every name an *authored config* references resolves,
#                           and no content file carries a bare id (§13 bars 1
#                           and 5). The script half is already exact — sscompile
#                           refuses an unresolved name — so this is the half that
#                           had no checker.
#   port_name_diff --check  every name present in BOTH trees has a signed row
#                           saying the two records are the same thing (§4.1,
#                           bar 6). A name resolving proves a spelling exists;
#                           only this proves it means what it meant there.
#                           `obj rock_sample1` is id 671 in both trees and is
#                           'Rock sample 1' there and 'Animal skull' here — every
#                           other check in this file passes on it.
#   port_category_crawl     `category` is the tree's one DERIVED namespace — the
#   --check                 reference never authors it, it crawls the `category=`
#                           key out of every record (CategoryType.ts). So the
#                           port re-runs that crawl — for BOTH domains, npc into
#                           `port/categories.map` and loc into
#                           `port/categories_loc.map` (config opcode 61, readable
#                           since 2026-08-02) — and holds the tree to what each
#                           file states about each of the reference's 53 npc and
#                           91 loc categories: a minted name whose
#                           members no longer agree on an id, and — the one that
#                           matters — a name held back for a human that got
#                           minted anyway. `black_demon`'s members all read 275
#                           and 275 is the *demon* category; minting it gives 33
#                           lesser and greater demons the black demon's drops.
#   port_constant_diff      the same bar for `.constant`, which the two above are
#   --check                 structurally blind to: a constant names nothing in any
#                           pack, so it has nothing to fail to resolve. 14 of the
#                           111 constants both trees spell agree on the name and
#                           disagree on the number (§9 step 3a); this holds the
#                           tree to `port/constants.map`'s statement about each.
#   port_config_diff        and the same bar for the five namespaces whose ids
#   --check                 are *ours* — param, struct, enum, dbtable, dbrow
#                           (§9 step 3c). Those resolve against nothing in the
#                           cache by design, so the name gate is blind to them
#                           too; what `port/configs.map` states instead is what
#                           this tree does with each of the reference's 1,619
#                           blocks, and this fails when the tree and that
#                           statement drift. The row that matters most is
#                           `collision`: `music` is dbtable 44 here with fifteen
#                           columns starting `sortname` and four starting `name`
#                           there, and 195 LostCity dbrows name it.
#   port_vars_diff          and the same bar for `%name` (§9 step 3d / §7.5),
#   --check                 which is the one namespace where resolving is not
#                           evidence of anything: osrs239 kept the legacy
#                           `prayerN` labels on ids it reused, so `%prayer9`
#                           resolves — to the Clan Wars block. `port/vars.map`
#                           states what each of the reference's 357 `%name`
#                           references becomes here, and this fails on a new one,
#                           on a resolution that changed under a decision already
#                           made, and on a false friend this tree's own scripts
#                           have started to spell. The *mechanism* half is not
#                           here: sscompile refuses a whole-varp write to a
#                           carrier and the selftest asserts the engine made none.
#   port_names_diff         and the last of the six, for the namespaces where the
#   --check                 reference names a record this tree has no spelling for
#                           (§9 step 3e). Every one of those *does* fail loudly
#                           the moment a script naming it is ported, which is bar
#                           1 working — so what this checks is the other side: the
#                           524 rows of `port/names.map` claiming two records are
#                           the same thing, each with facts re-derived from both
#                           trees on every run. It fails on a new reference name
#                           with no row, on a target whose display/size/frames/
#                           model no longer agree, on a port-manifest id that
#                           stopped holding its record, and on two names claiming
#                           one target. `npc macro_bigfish` is why it exists:
#                           unique display, same vislevel, injective, no
#                           collision — and this tree's only 'Big Fish' is a
#                           Shadow-of-the-Storm troll.
#
# Folded into `test-content` rather than given its own top-level target, for the
# reason stated above it: a bar in a command nobody types is not covered. The
# name diff needs the LostCity checkout and says so and passes when it is absent
# — a bar that cannot run has not failed.
test-port: test-summoning-phase5a test-summoning-phase5b test-summoning-phase6 test-summoning-scroll-assets test-summoning-pet-guide-assets
	@echo "--- port: the name-resolution gate ---"
	@cd $(REPO_ROOT) && python3 tools/ss_unresolved.py \
		--src $(MOCK230_CONTENT_DIR)/server/scripts \
		--pack $(MOCK230_CONTENT_DIR)/pack \
		--pack $(MOCK230_CONTENT_DIR)/configs \
		--check
	@cd $(REPO_ROOT) && python3 tools/port_name_diff.py \
		--tree $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/port_constant_diff.py \
		--tree $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/port_category_crawl.py \
		--tree $(MOCK230_CONTENT_DIR) --check -v
	@cd $(REPO_ROOT) && python3 tools/port_config_diff.py \
		--tree $(MOCK230_CONTENT_DIR) --check -v
	@cd $(REPO_ROOT) && python3 tools/port_vars_diff.py --check
	@cd $(REPO_ROOT) && python3 tools/cs2_varp_audit.py --check
	@cd $(REPO_ROOT) && python3 tools/port_names_diff.py \
		--tree $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/port_droptables_check.py \
		--tree $(MOCK230_CONTENT_DIR) --check -v
	@cd $(REPO_ROOT) && python3 tools/port_summoning_ids.py \
		--tree $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/gen_dbindex.py --check
	@cd $(REPO_ROOT) && python3 tools/check_summoning_isolation.py \
		--tree $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/ladder_import.py \
		--content $(MOCK230_CONTENT_DIR) --check
	@cd $(REPO_ROOT) && python3 tools/bank_import.py \
		--content $(MOCK230_CONTENT_DIR) --check

test-dbindex:
	@cd $(REPO_ROOT) && python3 tools/test_gen_dbindex.py

# The compiler's symbol table, against the register and the live tree.
#
# Exists because `pack/npc.pack` moving to `configs/all.npc.compack` and three
# namespaces being renamed silently cost sscompile almost every symbol, and the
# stale `script.dat` on disk meant the server kept booting. The corpus tests run
# against a vendored LostCity tree and cannot see any of that.
test-ss-symbols: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ssc_symbols_test \
		serverscript/test/ssc_symbols_test.c $(SSC_SRCS)
	./$(OBJ_DIR)/ssc_symbols_test $(OBJ_DIR) $(MOCK230_CONTENT_DIR)

# The npc server-band codec, and its agreement with `fields/npc.ini`.
#
# The round trip is the easy half. The half that matters is the cross-check: the
# register declares each field's opcode and width, and the codec holds a C table
# saying the same. If they drift, the packer writes a field under one opcode and
# the server reads it under another — and nothing errors, because both streams
# are well-formed. The value just lands somewhere else.
test-servercodec: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/servercodec_test \
		net/mock/test/mock230_servercodec_test.c net/mock/mock230_servercodec.c \
		$(RSCACHE_DIR)/src/rsbuffer.c
	@cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/servercodec_test \
		OSRS-Content/osrs239-content/fields

# LOC_ADD_CHANGE_V2's per-placement right-click menu, server writer against
# client reader.
#
# The slot index is offset-encoded (the wire byte is the zero-based slot; the
# codec's XFORM puts the one-based op number back on decode), and an off-by-one
# in either direction is silent: the packet still frames and the door still
# swings, the label just lands on the wrong row — or on a row the op mask hid,
# which reads as a door that cannot be clicked. Only a round trip catches it.
.PHONY: test-loc-ops-wire
test-loc-ops-wire: $(OBJ_DIR) $(RSPROT_TEST_DEPS) $(OBJ_DIR)/rsprot_packets_unity.o \
		$(OBJ_DIR)/jbase37.o
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -o $(OBJ_DIR)/loc_ops_wire_test \
		net/mock/test/loc_ops_wire_test.c \
		net/mock/mock230_wire.c net/mock/mock230_loc_ops.c \
		net/mock/mock230_interface_state.c net/mock/mock239_interface_setters.c \
		net/mock/mock239_runclientscript.c net/mock/mock239_varp.c \
		net/mock/mock239_inbound.c net/mock/mock239_interface_inbound.c \
		net/wordpack.c $(RSCACHE_DIR)/src/rsbuffer.c \
		$(RSAREABUF_DIR)/rsareabuf.c \
		$(OBJ_DIR)/rsprot_packets_unity.o $(RSPROT_TEST_DEPS) \
		$(OBJ_DIR)/jbase37.o -lm
	./$(OBJ_DIR)/loc_ops_wire_test

test-content-register: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/content_register_test \
		content/test/content_register_test.c content/content_register.c $(INI_SRC)
	./$(OBJ_DIR)/content_register_test

# The server hosted in-process: a real client subsystem and a real server joined
# by two byte buffers, no socket and no thread. Links the server core (no
# main()) against the client's net stack. If this hangs, the login handshake has
# gone back to blocking — see net/mock/test/embed_test.c.
# net_out.c, pkt_player_info.c and pkt_npc_info.c are here because the
# two-client half of the test drives the *client's* encoder for a ground click
# and the client's own readers for PLAYER_INFO and NPC_INFO — asserting on the
# server's own view of its stream would be asserting the encoder against itself.
#
# `net/bitbuffer.c` is NOT in that list even though those readers need it: it is
# already in $(NET_CORE_OBJS) as $(OBJ_DIR)/bitbuffer.o, and naming it here too
# compiles it a second time into the same link — four duplicate symbols and a
# link failure. This target had been unbuildable for that reason, which is worse
# than it sounds: it is the only test that asserts the two-peer zone replay (a
# door one client opens being open for a client that connects later), so the
# whole multi-client half of the zone work was going unverified with nothing
# saying so.
test-mock230-embed: $(OBJ_DIR) $(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock230_embed_test \
		net/mock/test/embed_test.c $(MOCK230_CORE_SRCS) \
		net/net_out.c net/rev/packets/pkt_player_info.c \
		net/rev/packets/pkt_npc_info.c \
		$(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o -lm
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230_embed_test

# WebSocket frame codec: mask/deframe/ping/incomplete/back-to-back, no socket.
test-ws-frame: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/platform -o $(OBJ_DIR)/ws_frame_test \
		platform/net_transport_ws_frame.c platform/test/ws_frame_test.c
	./$(OBJ_DIR)/ws_frame_test

# Boot-manifest loader: parse a fixture, assert field values + relative-path
# joining + CLI-override composition against a stub AppConfig.
test-bootmanifest: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/bootmanifest -o $(OBJ_DIR)/bootmanifest_test \
		bootmanifest/bootmanifest.c executor_config.c features/features.c $(INI_SRC) \
		bootmanifest/test/bootmanifest_test.c \
		$(RSCACHE_DIR)/build/rscache_unity.o $(RSCACHE_DIR)/build/bzip.o \
		$(RSCACHE_DIR)/build/bzip_encode.o $(RSCACHE_DIR)/build/miniz.o \
		$(RSCACHE_DIR)/build/xteas.o
	./$(OBJ_DIR)/bootmanifest_test

# Actor vs scene model-light regimes (toridraw profiles + LightSetProfiles).
test-light-model: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/light_model_test \
		$(TORIDRAW_DIR)/toridraw_light_model_test.c $(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/light_model_test

# DynamicObject sequence termination: frameStep governs the final location pose.
test-animation-object-step: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/animation_object_step_test \
		$(TORIDRAW_DIR)/toridraw_animation_test.c $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o
	./$(OBJ_DIR)/animation_object_step_test

# Public scene scratch-capacity profile contract and independent SMALL/lazy
# texture behavior.
test-scene-profiles: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/scene_profile_test \
		$(TORIDRAW_DIR)/toridraw_scene_profile_test.c $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	./$(OBJ_DIR)/scene_profile_test

# Depth-tested raster family: a model tagged TORIDRAW_MODEL_FLAG_ZBUFFER
# resolves its own faces per pixel, the result does not depend on face order,
# and the buffer is reset per model so nothing leaks between them.
test-zbuffer: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/zbuffer_test \
		$(TORIDRAW_DIR)/toridraw_zbuffer_test.c $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	./$(OBJ_DIR)/zbuffer_test

# The ToriRS -> ToriDraw adaptor must not forward decode flags into the render
# flag word: the two `flags` bytes are different namespaces, and bit 0 on the
# ToriDraw side is TORIDRAW_MODEL_FLAG_ZBUFFER, which drops face priorities.
test-model-render-flags: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o \
		$(OBJ_DIR)/bmp.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/model_render_flags_test \
		engine/test/model_render_flags_test.c engine/toridraw_model_from_torirs.c \
		engine/torirs_types.c $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o \
		$(OBJ_DIR)/bmp.o
	./$(OBJ_DIR)/model_render_flags_test

# Client-TS near-plane parity: sentinel faces bypass the meaningless pre-clip
# winding, then the clipped polygon receives the real front-face test.
# bmp.o: ToriDraw_SpriteWriteBmpFile is in the unity, so every target that links
# it needs bmp_write_file. Without it these two fail to LINK, which reads as a
# failing test rather than a missing object.
test-near-clip: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o $(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	$(CC) $(CFLAGS) -lm $(PLATFORM_STRIP_LDFLAGS) -o $(OBJ_DIR)/near_clip_test \
		$(TORIDRAW_DIR)/toridraw_near_clip_test.c $(OBJ_DIR)/toridraw_unity.o \
		$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/bmp.o
	./$(OBJ_DIR)/near_clip_test

# CPU-only, but deliberately compiled with the Win32 compiler: it links the
# real fixed-function D3D9 command dispatcher and drives its retained resource
# path without creating a window or D3D device.
.PHONY: test-retained-renderer-leak
ifeq ($(PLATFORM),win32)
test-retained-renderer-leak:
	$(MAKE) -C $(REPO_ROOT)/test/trspk_leak check CC="$(CC)"
else
test-retained-renderer-leak:
	@echo "test-retained-renderer-leak requires PLATFORM=win32"
	@false
endif

# Real Win32 message/timer regressions with a hidden HWND. These are standalone
# so they run under either repository MinGW lane without booting the client.
.PHONY: test-win32-platform
ifneq ($(filter $(PLATFORM),win32 win64),)
WIN32_PLATFORM_TEST_CFLAGS := -std=c11 -O2 -Wall -Wextra -g -I$(SRC_DIR) \
	$(PLATFORM_BASE_CFLAGS)
WIN32_PLATFORM_TEST_LDFLAGS := -static -static-libgcc \
	-lgdi32 -luser32 -lwinmm -lkernel32
test-win32-platform: $(OBJ_DIR)
	$(CC) $(WIN32_PLATFORM_TEST_CFLAGS) -DTORIRS_WIN32_GDI_TEST_API=1 \
		-o $(OBJ_DIR)/win32_gdi_test$(PLATFORM_EXE_SUFFIX) \
		platform/test/win32_gdi_test.c platform/platform_win32gdi.c \
		platform/platform_win32_timing.c cmd/cmdbus.c cmd/cmdring.c \
		input/torirs_keymap.c $(WIN32_PLATFORM_TEST_LDFLAGS)
	./$(OBJ_DIR)/win32_gdi_test$(PLATFORM_EXE_SUFFIX)
	$(CC) $(WIN32_PLATFORM_TEST_CFLAGS) \
		-o $(OBJ_DIR)/win32_timing_test$(PLATFORM_EXE_SUFFIX) \
		platform/test/win32_timing_test.c platform/platform_win32_timing.c \
		$(WIN32_PLATFORM_TEST_LDFLAGS)
	./$(OBJ_DIR)/win32_timing_test$(PLATFORM_EXE_SUFFIX)
else
test-win32-platform:
	@echo "test-win32-platform requires PLATFORM=win32 or PLATFORM=win64"
	@false
endif

# Recursing over PLATFORM_LIST rather than spelling the objdirs out keeps this
# correct when a platform is added -- the old hardcoded list had already gone
# stale and never removed build_win32*/, build_win64*/, or their executables.
# Each pass re-includes
# platform.mk with that PLATFORM, so $(PLATFORM_OBJ_BASE)/$(PLATFORM_TARGET)
# below are that lane's, and the glob covers every flavor suffix
# (_opt/_mt/_tdo/_es) OBJ_DIR can grow.
clean:
	@for p in $(PLATFORM_LIST); do $(MAKE) --no-print-directory PLATFORM=$$p clean-lane; done
	rm -f .last_flavor
	# Only our own outputs: build-web/ is a plain output directory that other
	# experiments have also written into.
	rm -f $(REPO_ROOT)/build-web/torirs.js $(REPO_ROOT)/build-web/torirs.wasm \
		$(REPO_ROOT)/build-web/torirs.data $(REPO_ROOT)/build-web/torirs.wasm.map \
		$(REPO_ROOT)/build-web/index.html $(REPO_ROOT)/build-web/torirs_host.js \
		$(REPO_ROOT)/build-web/viewer.html

# Every flavor suffix OBJ_DIR can grow, in the order the top of this file
# appends them. Enumerated rather than globbed on purpose: PLATFORM_OBJ_BASE
# values are prefixes of one another (build, build_web, build_win32,
# build_win64), so `build_*` from the macos lane would delete other lanes too.
CLEAN_FLAVOR_SUFFIXES := _opt _mt _opt_mt _tdo _opt_tdo _es _opt_es \
	_mt_es _opt_mt_es _tdo_es _opt_tdo_es

# One lane's objdirs and link outputs. `clean` below runs this once per lane.
clean-lane:
	rm -rf $(PLATFORM_OBJ_BASE) \
		$(foreach s,$(CLEAN_FLAVOR_SUFFIXES),$(PLATFORM_OBJ_BASE)$(s)) \
		$(PLATFORM_TARGET) $(PLATFORM_TARGET)$(PLATFORM_TARGET_MEMTRACE_SUFFIX)

# Those .d files answer "which objects does this header reach". They cannot
# answer "is this object actually built from the source on disk" -- make settles
# that by comparing mtimes, and mtimes in this tree are not trustworthy. A tree
# copy (rsync, tar, a worktree synced back) stamps sources AND objects with
# whole-second times from the copy, so an object can carry an mtime equal to its
# source while holding pre-copy code, and make will never rebuild it.
#
# It has already shipped a wrong binary: on 2026-08-15 run-live.sh aborted in
# ToriRS_ModelAssertPnmTextureInvariant on an assert the source on disk cannot
# reach, because build_asan_opt_asan_ubsan_nosimd_es/torirs_types.o predated the
# commit that added the render-type gate while every neighbouring object was
# current. Nine objects in that directory were in that state.
#
# So verify by content before make evaluates anything: objverify.py hashes each
# object's whole prerequisite set plus the flag signature, remembers the verdict
# in $(OBJ_DIR)/.objverify, and deletes any object whose sources have moved
# underneath it. $(shell) at parse time is the point of it -- the deletions have
# to land before make builds its DAG. Silent when the directory is clean.
# tools/test_objverify.py covers the cases.
OBJVERIFY := $(REPO_ROOT)/tools/objverify.py
# CC and the compile flag sets only: LDFLAGS cannot invalidate an object, and
# putting it here would rebuild the world every time a link option moved.
OBJVERIFY_SIG := $(CC)|$(CFLAGS)|$(RSCACHE_CFLAGS)|$(RSPROT_CFLAGS)|$(TORIDRAW_CFLAGS)
OBJVERIFY_REPORT := $(shell command -v python3 >/dev/null 2>&1 && \
	python3 '$(OBJVERIFY)' '$(OBJ_DIR)' --sig '$(OBJVERIFY_SIG)' 2>&1)
$(if $(OBJVERIFY_REPORT),$(info $(OBJVERIFY_REPORT)))

# Its cases are built by hand in a temp directory: the failure being tested is a
# timestamp relationship between an object and its sources, and no real compile
# can be made to produce one on demand.
.PHONY: test-objverify
test-objverify:
	python3 $(REPO_ROOT)/tools/test_objverify.py

# Header dependency tracking: -MMD writes a .d next to each .o; include them so
# editing a header rebuilds every object that includes it (stale objects read
# struct fields at old offsets otherwise).
-include $(wildcard $(OBJ_DIR)/*.d)

# The three revision-239 music packets: encode as RSProt does, parse back.
test-midi-packets: $(OBJ_DIR) $(RSPROT_TEST_DEPS) $(OBJ_DIR)/rsprot_packets_unity.o \
		$(OBJ_DIR)/jbase37.o
	$(CC) $(CFLAGS) $(RSPROT_INCLUDES) -o $(OBJ_DIR)/midi_packet_test \
		net/rev/test/midi_packet_test.c net/rev/osrs239/osrs239_parse.c \
		$(OBJ_DIR)/rsprot_packets_unity.o $(RSPROT_TEST_DEPS) \
		$(OBJ_DIR)/jbase37.o -lm
	./$(OBJ_DIR)/midi_packet_test

# seal_rig_scan: which sequence can drive which model, computed from the cache.
# Model labels vs framemap bone groups — see src/engine/test/seal_rig_scan.c.
test-seal-rig: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/seal_rig_scan \
		engine/test/seal_rig_scan.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(LDFLAGS)
	cd .. && ./src/$(OBJ_DIR)/seal_rig_scan
