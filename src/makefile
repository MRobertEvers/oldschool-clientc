# From repo root: make -C src
# Optimized build: make -C src release (or make -C src OPT=1 <target>).
# Objects go to build_opt/ so debug and optimized objects never mix; both
# flavors link the same ./torirs, whichever was built last.
#
# Which host to build for is one variable, PLATFORM (see platform/platform.mk):
#   make -C src                 native debug     -> src/torirs
#   make -C src release         native optimized -> src/torirs
#   make -C src web             emscripten       -> build-web/torirs.js
#   make -C src io-server       the web build's cache backend (always native)
# Every (PLATFORM, OPT) pair owns its own OBJ_DIR, so flavors never share .o's.

OPT ?= 0
# MEMTRACE=1 links platform/torirs_memtrace.c, which interposes the C heap and
# writes every alloc/free with a call stack to memtrace.bin. It is a third build
# flavor with its own OBJ_DIR (the interpose needs -fno-builtin-* on every unit,
# so its objects must never mix with a plain build's).
#   make -C src MEMTRACE=1              native, traced   -> src/torirs
#   make -C src MEMTRACE=1 web          web, traced      -> build-web/torirs.js
# See tools/memtrace/README.md.
MEMTRACE ?= 0

REPO_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR      := $(REPO_ROOT)/src

include $(SRC_DIR)/platform/platform.mk

CC := $(PLATFORM_CC)
ifeq ($(MEMTRACE),1)
MEMTRACE_SUFFIX = _mt
else
MEMTRACE_SUFFIX =
endif
ifeq ($(OPT),1)
OBJ_DIR = $(PLATFORM_OBJ_BASE)_opt$(MEMTRACE_SUFFIX)
O_LEVEL = -O3
else
OBJ_DIR = $(PLATFORM_OBJ_BASE)$(MEMTRACE_SUFFIX)
O_LEVEL = -O0
endif
# Debug and release deliberately share ./torirs, but a traced binary must not
# join them: MEMTRACE is a long-running capture, and a plain `make -C src` in
# another terminal would silently replace it mid-session — the symptom being a
# run that writes no trace at all. The web target keeps its name, which the
# page's <script src> is written against.
ifeq ($(MEMTRACE)$(PLATFORM),1native)
TARGET = $(PLATFORM_TARGET)_mt
else
TARGET = $(PLATFORM_TARGET)
endif
RSCACHE_DIR  := $(REPO_ROOT)/3rd/rscache
TORIDRAW_DIR := $(REPO_ROOT)/3rd/toridraw
TRSPK_DIR    := $(REPO_ROOT)/3rd/trspk
BMP_DIR      := $(REPO_ROOT)/3rd/bmp
HMAP_DIR     := $(REPO_ROOT)/3rd/hmap
RSAREABUF_DIR := $(REPO_ROOT)/3rd/rsareabuf

CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -Wall -Wextra -g -MMD -MP \
	-I$(SRC_DIR) -I$(SRC_DIR)/cs2vm2 -I$(SRC_DIR)/serverscript \
	-I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src \
	-I$(TORIDRAW_DIR) -I$(TRSPK_DIR) -I$(BMP_DIR) -I$(HMAP_DIR) -I$(RSAREABUF_DIR) \
	-I$(REPO_ROOT)/3rd/miniz

RSCACHE_CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -g -MMD -MP \
	-I$(RSCACHE_DIR)/include \
	-I$(RSCACHE_DIR)/src \
	-I$(RSCACHE_DIR) \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

COMPRESS_CFLAGS = -std=c11 -O3 -fno-omit-frame-pointer -g -MMD -MP \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

TORIDRAW_CFLAGS = -std=c11 $(O_LEVEL) -fno-omit-frame-pointer -g -MMD -MP \
	-I$(TORIDRAW_DIR) \
	-I$(BMP_DIR)

# Toolchain, SDL and link flags come from the selected platform block. The
# base subset also reaches the vendored units: a platform whose libc hides
# strdup behind a feature macro hides it from 3rd/ too.
CFLAGS += $(PLATFORM_CFLAGS)
RSCACHE_CFLAGS  += $(PLATFORM_BASE_CFLAGS)
COMPRESS_CFLAGS += $(PLATFORM_BASE_CFLAGS)
TORIDRAW_CFLAGS += $(PLATFORM_BASE_CFLAGS)
LDFLAGS = $(PLATFORM_LDFLAGS)

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

# --- Heap instrumentation (MEMTRACE=1) -----------------------------------
# -fno-builtin-* has to reach every translation unit, not just the tracer: the
# compiler is otherwise free to fold a malloc/free pair away or lower calloc to
# its own inline sequence, and those allocations then never reach the hooks.
# On macOS the tracer defines malloc/free as strong symbols and finds the real
# ones through dlsym(RTLD_NEXT); everywhere else the linker's --wrap does it.
# ASan hooks the same functions, so the two cannot be combined.
ifeq ($(MEMTRACE),1)
ifeq ($(ENABLE_ASAN),1)
$(error MEMTRACE=1 and ENABLE_ASAN=1 both hook malloc — pick one)
endif
MEMTRACE_CFLAGS := -DTORIRS_MEMTRACE=1 \
	-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
CFLAGS          += $(MEMTRACE_CFLAGS)
RSCACHE_CFLAGS  += $(MEMTRACE_CFLAGS)
COMPRESS_CFLAGS += $(MEMTRACE_CFLAGS)
TORIDRAW_CFLAGS += $(MEMTRACE_CFLAGS)
ifeq ($(PLATFORM),web)
# emscripten's musl only routes these four through --wrap; reallocf and
# posix_memalign are not separate symbols there, and strdup is inlined to a
# malloc it already sees.
LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
else ifneq ($(shell uname -s),Darwin)
LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=reallocf -Wl,--wrap=posix_memalign -Wl,--wrap=strdup
endif
endif

# platform/platform_sdl2*.c is shared (emscripten ports SDL2); the IO, audio
# and 3D-renderer backends are per-platform and come in via $(PLATFORM_SRCS).
SRCS = main.c app.c platform/platform_sdl2.c \
	platform/platform_sdl2_renderer_soft3d.c \
	$(PLATFORM_SRCS) \
	render/torirs_frame.c render/torirs_pick.c \
	render/trspk_toridraw.c render/trspk_sprite.c \
	input/torirs_input.c input/torirs_keymap.c input/torirs_input_cmd.c \
	game/task_cs2_script_exec.c $(BMP_DIR)/bmp.c \
	cs2vm2/cs2vm2.c cs2vm2/cs2vm2_script.c cs2vm2/cs2vm2_trigger_args.c \
	cs2vm2/cs2vm2_strpool.c \
	cs1vm/cs1vm.c cs1vm/cs1vm_script.c \
	game/rs_player_stats.c game/rs_cs1_host.c game/task_cs1_run.c \
	game/rs_ui_slots.c game/rs_social.c game/rs_clientcode.c game/rs_if1_buttons.c \
	game/rs_idk_design.c \
	game/rs_chat.c game/rs_gameproto_exec.c \
	game/rs_worldmap.c \
	game/rs_worldmap_render.c \
	cs2vm2/cs2_opcode_meta.c engine/cache_provider.c engine/proctex/proctex_generator.c engine/cs2vm2_script_from_rscache.c \
	engine/cs2_opcode_dialect.c \
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
	engine/dat2/task_dat2_varbit_load.c \
	engine/dat1/task_dat1_varbit_load.c \
	engine/dat2/task_dat2_dbrow_load.c engine/dat2/task_dat2_dbindex_load.c \
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
	game/rs_cs2_host.c game/task_cs2_run.c game/rs_cs2_dispatch.c game/rs_minimenu_build.c \
	game/rs_minimenu_world.c \
	game/task_gameproto_exec.c game/task_exec_entity_info.c game/rs_entity_sync.c game/rs_audio.c \
	engine/uitree_anim.c engine/entity_model_build.c \
	ui/uitree.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
	ui/uitree_input.c ui/uitree_hover.c ui/uitree_scroll.c ui/uitree_emit.c \
	ui/uitree_inv_view.c ui/uitree_cross.c ui/uitree_minimenu.c \
	ui/uitree_hovertext.c \
	ui/uitree_interact.c \
	varp/varp_manager.c \
	varc/varc_manager.c \
	inv/inv_manager.c \
	cmd/cmdring.c cmd/cmdbus.c \
	net/isaac.c net/rsa.c net/ringbuf.c net/jbase37.c net/wordpack.c \
	net/loginproto.c net/packetbuffer.c net/net.c net/net_out.c net/bitbuffer.c \
	net/rev/gameproto_parse.c net/rev/packets/pkt_player_info.c net/rev/packets/pkt_npc_info.c \
	net/rev/packets/pkt_player_appearance.c net/rev/packets/pkt_rebuild_normal.c \
	net/rev/gameproto_rev_params.c \
	net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c \
	bootmanifest/bootmanifest.c \
	platform/sockstream.c platform/platform_socket.c \
	platform/net_transport_tcp.c platform/net_transport_ws.c \
	platform/net_transport_ws_frame.c \
	net/loginproto_xrsps.c net/loginproto_osrs230.c \
	net/rev/xrsps233/gameproto_rev_xrsps233.c \
	net/rev/osrs230/gameproto_rev_osrs230.c net/rev/osrs230/osrs230_parse.c \
	revconfig/revconfig.c revconfig/revconfig_load.c \
	world/entity_pool.c world/world_entity.c world/entity_pathing.c \
	world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
	engine/world_builder/task_world_load.c \
	painters/painters.c
ifeq ($(MEMTRACE),1)
SRCS += platform/torirs_memtrace.c
endif
INI_SRC = $(REPO_ROOT)/3rd/ini/ini.c
RSCACHE_UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
TORIDRAW_UNITY_SRC = $(TORIDRAW_DIR)/toridraw_unity.c
TRSPK_UNITY_SRC = $(TRSPK_DIR)/trspk_unity.c
# Objects derived from SRCS (one .o per source, flat in OBJ_DIR) plus the
# out-of-tree/unity extras that have their own rules below. The world_builder
# pipeline ($(WB_OBJS), incl. wb_minimap.o — the Minimap world.h is written
# against) links into the app; $(sort) dedupes objects shared with WB_OBJS.
# The desktop-GL binding is native-only; the web build gets the WebGL1 pieces
# the renderer needs instead (index splitting — see webgl1_index16.h).
ifeq ($(PLATFORM),native)
GL_OBJS = $(OBJ_DIR)/opengl3_sdlgl.o
else
WEBGL1_OBJS = $(OBJ_DIR)/webgl1_index16.o
endif
OBJS = $(sort $(addprefix $(OBJ_DIR)/,$(notdir $(SRCS:.c=.o))) \
	$(OBJ_DIR)/ini.o $(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o \
	$(OBJ_DIR)/hmap.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
	$(OBJ_DIR)/trspk_unity.o $(GL_OBJS) $(WEBGL1_OBJS) \
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
	$(OBJ_DIR)/contour_ground.o $(OBJ_DIR)/decor_buildmap.o $(OBJ_DIR)/world_decode_tile.o \
	$(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o $(OBJ_DIR)/wb_minimap.o \
	$(OBJ_DIR)/toridraw_model_from_torirs.o

.PHONY: test-cache-trim all release web web-debug io-server test-io-wire clean run mock230 test-sound test-mock230 bench-uitree test-rsareabuf test-varp test-varc test-inv test-world test-uitree test-uitree-builder test-uitree-builder-dat1 test-revconfig test-bootmanifest test-ws-frame test-world-builder test-minimap test-cs1vm test-cs1 test-cmdbus test-net-login test-net-loopback test-net-exec test-ui-slots test-walkmerge test-entity-decode check-no-drain test-task-order test-db test-cs2-math test-cs2-string test-cs2-dialect world-builder test-ss-meta test-ss-corpus test-ss-roundtrip test-ss-vm test-ss-verify test-ssc sscompile mock230-scripts

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

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
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
web:
	$(MAKE) PLATFORM=web OPT=1 MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/
	@echo "built $(REPO_ROOT)/build-web/torirs.js — serve with: make -C src io-server && ./src/build/io_server <cache_dir>"

web-debug:
	$(MAKE) PLATFORM=web OPT=0 MEMTRACE=$(MEMTRACE) all
	@cp $(WEB_ASSETS) $(REPO_ROOT)/build-web/

# --- IO server -----------------------------------------------------------
# The web client has no disk: every ToriRS_IOItem it would have satisfied
# locally is answered by this process, which opens the cache and runs the
# SAME PlatformX_IO_LoadItem the native build uses. Always built native, even
# when PLATFORM=web, so `make -C src web io-server` produces both halves.
IO_SERVER_OBJ_DIR = build
IO_SERVER_SRCS = ioserver/io_server_main.c ioserver/http_server.c \
	platform/io_wire.c platform/platform_x_io.c \
	bootmanifest/bootmanifest.c $(INI_SRC)
io-server:
	$(MAKE) PLATFORM=native $(IO_SERVER_OBJ_DIR)/io_server
$(IO_SERVER_OBJ_DIR)/io_server: $(IO_SERVER_SRCS) $(IO_SERVER_OBJ_DIR) \
		$(IO_SERVER_OBJ_DIR)/rscache_unity.o $(IO_SERVER_OBJ_DIR)/bzip.o \
		$(IO_SERVER_OBJ_DIR)/bzip_encode.o $(IO_SERVER_OBJ_DIR)/miniz.o \
		$(IO_SERVER_OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/ioserver -o $@ $(IO_SERVER_SRCS) \
		$(IO_SERVER_OBJ_DIR)/rscache_unity.o $(IO_SERVER_OBJ_DIR)/bzip.o \
		$(IO_SERVER_OBJ_DIR)/bzip_encode.o $(IO_SERVER_OBJ_DIR)/miniz.o \
		$(IO_SERVER_OBJ_DIR)/xteas.o -lm
	@echo "built $@"

# IO wire codec round-trip: encode a request/response pair for every item kind
# and assert the decoded item matches field for field.
test-io-wire: $(OBJ_DIR) $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o \
		$(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/io_wire_test \
		platform/io_wire.c platform/platform_x_io.c platform/test/io_wire_test.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	./$(OBJ_DIR)/io_wire_test $(DAT1_CACHE) "$(DAT2_CACHE)" "$(DAT2_REV)"

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

NET_REV_SRCS = net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c \
	net/rev/xrsps233/gameproto_rev_xrsps233.c net/rev/osrs230/gameproto_rev_osrs230.c
$(foreach src,$(filter-out painters/painters.c $(NET_REV_SRCS),$(SRCS)),$(eval $(call make-goal,$(src))))

$(OBJ_DIR)/painters.o: painters/painters.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -c painters/painters.c -o $@

# Rev protocol tables include their own dir for packetin.h/packetout.h.
$(OBJ_DIR)/gameproto_rev_lc245_2.o: net/rev/lc245_2/gameproto_rev_lc245_2.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_lc254.o: net/rev/lc254/gameproto_rev_lc254.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc254 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_xrsps233.o: net/rev/xrsps233/gameproto_rev_xrsps233.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/xrsps233 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_osrs230.o: net/rev/osrs230/gameproto_rev_osrs230.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/osrs230 -c $< -o $@

# tommath amalgam for RSA (login block exptmod).
$(OBJ_DIR)/tommath.o: $(REPO_ROOT)/3rd/tommath/mpi.c
	$(CC) -std=c11 -O2 -w -I$(REPO_ROOT)/3rd/tommath -c $< -o $@

# --- World builder pipeline object rules ---------------------------------
$(OBJ_DIR)/world_builder.o: $(WB_DIR)/world_builder.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/world_builder.c -o $@
$(OBJ_DIR)/blendmap.o: $(WB_DIR)/blendmap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/blendmap.c -o $@
$(OBJ_DIR)/overlaymap.o: $(WB_DIR)/overlaymap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/overlaymap.c -o $@
$(OBJ_DIR)/lightmap.o: $(WB_DIR)/lightmap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/lightmap.c -o $@
$(OBJ_DIR)/shademap.o: $(WB_DIR)/shademap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/shademap.c -o $@
$(OBJ_DIR)/sharelight_map.o: $(WB_DIR)/sharelight_map.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/sharelight_map.c -o $@
$(OBJ_DIR)/terrain_shapemap.o: $(WB_DIR)/terrain_shapemap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/terrain_shapemap.c -o $@
$(OBJ_DIR)/contour_ground.o: $(WB_DIR)/contour_ground.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/contour_ground.c -o $@
$(OBJ_DIR)/decor_buildmap.o: $(WB_DIR)/decor_buildmap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/decor_buildmap.c -o $@
$(OBJ_DIR)/world_decode_tile.o: $(WB_DIR)/world_decode_tile.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/world_decode_tile.c -o $@
$(OBJ_DIR)/heightmap.o: $(WB_DIR)/heightmap.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/heightmap.c -o $@
$(OBJ_DIR)/collision_map.o: $(WB_DIR)/collision_map.c
	$(CC) $(CFLAGS) -c $(WB_DIR)/collision_map.c -o $@
$(OBJ_DIR)/wb_minimap.o: $(WB_DIR)/minimap.c
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

test-cs2-dialect: $(OBJ_DIR) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cs2_opcode_dialect_test \
		cs2vm2/test/opcode_dialect_test.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/cs2_opcode_dialect_test

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

# Sound end-to-end test: cache -> synth render -> game queue -> platform.
# Links the SILENT audio backend instead of the SDL2 one (they define the same
# symbols), so this runs headless with no device.
AUDIO_TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/platform_audio_sdl2.o,$(OBJS))
test-sound: $(OBJ_DIR) $(AUDIO_TEST_OBJS)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rs_audio_test \
		game/test/rs_audio_test.c platform/platform_audio_null.c \
		$(AUDIO_TEST_OBJS) $(LDFLAGS)
	./$(OBJ_DIR)/rs_audio_test

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

$(OBJ_DIR)/hmap.o: $(HMAP_DIR)/hmap.c
	$(CC) $(CFLAGS) -c $(HMAP_DIR)/hmap.c -o $@

$(OBJ_DIR)/rsareabuf.o: $(RSAREABUF_DIR)/rsareabuf.c
	$(CC) $(CFLAGS) -c $(RSAREABUF_DIR)/rsareabuf.c -o $@

# rsareabuf: byte/bit/alt-order round-trips + arena + psize back-patching.
test-rsareabuf: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/rsareabuf_test \
		$(RSAREABUF_DIR)/rsareabuf.c $(RSAREABUF_DIR)/rsareabuf_test.c
	./$(OBJ_DIR)/rsareabuf_test

$(OBJ_DIR)/ini.o: $(INI_SRC)
	$(CC) $(CFLAGS) -c $(INI_SRC) -o $@

$(OBJ_DIR)/rscache_unity.o: $(RSCACHE_UNITY_SRC)
	$(CC) $(RSCACHE_CFLAGS) -w -c $(RSCACHE_UNITY_SRC) -o $@

$(OBJ_DIR)/bzip.o: $(REPO_ROOT)/3rd/bzip/bzip.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
# bzip_encode.c is the compression side (bzip.c is decompress-only). rscache's
# RSCache_CompressionBzipCompress calls into it, so it belongs in every link set
# that includes rscache_unity.o.
$(OBJ_DIR)/bzip_encode.o: $(REPO_ROOT)/3rd/bzip/bzip_encode.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/miniz.o: $(REPO_ROOT)/3rd/miniz/miniz.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/xteas.o: $(REPO_ROOT)/3rd/xteas/xteas.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(TORIDRAW_UNITY_SRC)
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_UNITY_SRC) -o $@

$(OBJ_DIR)/trspk_unity.o: $(TRSPK_UNITY_SRC)
	$(CC) $(CFLAGS) -w -c $(TRSPK_UNITY_SRC) -o $@

$(OBJ_DIR)/webgl1_index16.o: $(TRSPK_DIR)/webgl1/webgl1_index16.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/opengl3_sdlgl.o: $(TRSPK_DIR)/opengl3/opengl3_sdlgl.c
	$(CC) $(CFLAGS) -w -c $(TRSPK_DIR)/opengl3/opengl3_sdlgl.c -o $@

$(OBJ_DIR)/toridraw_font.o: $(TORIDRAW_DIR)/toridraw_font.c
	$(CC) $(TORIDRAW_CFLAGS) -I$(REPO_ROOT)/v0 -w -c $(TORIDRAW_DIR)/toridraw_font.c -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(TORIDRAW_DIR)/toridraw_model_sprite.c
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_DIR)/toridraw_model_sprite.c -o $@

run: $(TARGET)
	./$(TARGET)

test-varp: $(OBJ_DIR) varp/varp_manager.c varp/varp_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/varp_manager_test varp/varp_manager.c varp/varp_manager_test.c
	./$(OBJ_DIR)/varp_manager_test

test-varc: $(OBJ_DIR) varc/varc_manager.c varc/varc_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/varc_manager_test varc/varc_manager.c varc/varc_manager_test.c
	./$(OBJ_DIR)/varc_manager_test

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
SS_SRCS = serverscript/ss_meta.c serverscript/ssvm_script.c \
	serverscript/ssvm_provider.c serverscript/ssvm_strpool.c serverscript/ssvm.c \
	$(RSAREABUF_DIR)/rsareabuf.c

# The compiler is a superset: it links the VM sources so a test can compile a
# script and immediately run it.
SSC_SRCS = $(SS_SRCS) serverscript/ssc_lex.c serverscript/ssc_symbols.c \
	serverscript/ssc_compile.c

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

# Static stack verifier: a CFG walk over every corpus instruction. Proves the
# generated arity table agrees with real compiled code, and turns the VM's stack
# size constants into measurements instead of guesses.
test-ss-verify: $(OBJ_DIR) $(SS_SRCS) serverscript/test/ss_verify_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/ss_verify_test \
		$(SS_SRCS) serverscript/test/ss_verify_test.c
	./$(OBJ_DIR)/ss_verify_test $(SS_CORPUS)

# RuneScript compiler: .rs2 -> script.dat/.idx.
sscompile: $(OBJ_DIR) $(SSC_SRCS) serverscript/ssc_main.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/sscompile $(SSC_SRCS) serverscript/ssc_main.c
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
		ui/uitree.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
		ui/uitree_scroll.c ui/uitree_emit.c ui/uitree_inv_view.c \
		ui/uitree_cross.c ui/uitree_minimenu.c ui/uitree_hovertext.c \
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
	$(OBJ_DIR)/pkt_rebuild_normal.o \
	$(OBJ_DIR)/gameproto_parse.o $(OBJ_DIR)/tommath.o \
	$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
test-net-login: $(OBJ_DIR) $(NET_CORE_OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_login_test \
		net/test/net_login_test.c $(NET_CORE_OBJS) -lm
	./$(OBJ_DIR)/net_login_test

# Full login -> GAME -> parsed-packet pipeline through a scripted mock server.
test-net-loopback: $(OBJ_DIR) $(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_loopback_test \
		net/test/net_loopback_test.c net/mock/mock_server.c \
		$(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o -lm
	./$(OBJ_DIR)/net_loopback_test

test-inv: $(OBJ_DIR) inv/inv_manager.c inv/inv_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/inv_manager_test inv/inv_manager.c inv/inv_manager_test.c
	./$(OBJ_DIR)/inv_manager_test

test-world: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -I$(SRC_DIR)/world -I$(SRC_DIR)/world/test -lm -Wl,-dead_strip -o $(OBJ_DIR)/world_test \
		world/entity_pool.c world/world_entity.c world/entity_pathing.c \
		world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
		engine/world_builder/heightmap.c engine/world_builder/collision_map.c \
		engine/world_builder/minimap.c painters/painters.c \
		world/test/world_test_unit.c world/test/world_test_sim.c \
		world/test/world_test_route.c world/test/world_test_main.c \
		$(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/world_test

test-uitree: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/ui -I$(SRC_DIR)/ui/test -o $(OBJ_DIR)/uitree_test \
		ui/uitree.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
		ui/uitree_input.c ui/uitree_hover.c ui/uitree_scroll.c ui/uitree_emit.c \
		ui/uitree_inv_view.c ui/uitree_cross.c ui/uitree_minimenu.c \
		ui/uitree_hovertext.c \
		ui/uitree_interact.c input/torirs_input.c input/torirs_keymap.c \
		ui/test/uitree_test_main.c ui/test/uitree_test_dirty.c \
		ui/test/uitree_test_walk.c ui/test/uitree_test_hover.c \
		ui/test/uitree_test_layout_build.c ui/test/uitree_test_mutate_emit.c \
		ui/test/uitree_test_drag.c ui/test/uitree_test_scroll_hit.c \
		ui/test/uitree_test_drag_scrolled.c ui/test/uitree_test_emit_icons.c \
		ui/test/uitree_test_emit_golden.c \
		ui/test/uitree_test_minimenu.c ui/test/uitree_test_keys.c \
		ui/test/uitree_test_id_index.c
	./$(OBJ_DIR)/uitree_test

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
		ui/uitree.c ui/uitree_layout.c ui/uitree_scroll.c ui/uitree_host.c \
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

# Serial-FIFO ordering guarantee (head-only task execution across IO yields).
test-task-order: $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/task_order_test game/test/task_order_test.c -lm
	./$(OBJ_DIR)/task_order_test

# No blocking IO outside tests: TaskRunner_Drain must appear only under */test/.
check-no-drain:
	@! grep -rn "TaskRunner_Drain" --include="*.c" --include="*.h" . \
		| grep -v "/test/" | grep -v "task_runner.h" \
		&& echo "check-no-drain: PASS (no runtime Drain)" \
		|| (echo "check-no-drain: FAIL — runtime TaskRunner_Drain found" && exit 1)

# Walkmerge blend (Model.maskAnimate port) + seq-meta plumbing.
test-walkmerge: $(OBJ_DIR) $(OBJ_DIR)/toridraw_unity.o
	$(CC) $(CFLAGS) -lm -Wl,-dead_strip -o $(OBJ_DIR)/walkmerge_test \
		world/test/walkmerge_test.c $(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/walkmerge_test

# Parity of the `scanline` raster family against the shipping `branching`
# kernels: every flat / gouraud / textured variant over a triangle set covering
# interior, clipped, degenerate and inverted-winding cases. Standalone TU, no
# cache or disk.
test-scanline: $(OBJ_DIR)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-I$(TORIDRAW_DIR) -o $(OBJ_DIR)/scanline_parity_test \
		$(TORIDRAW_DIR)/toridraw_scanline_parity_test.c -lm
	./$(OBJ_DIR)/scanline_parity_test

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
	$(CC) $(CFLAGS) -lm -Wl,-dead_strip -o $(OBJ_DIR)/rotate_blit_test \
		render/test/rotate_blit_test.c $(OBJ_DIR)/toridraw_unity.o
	./$(OBJ_DIR)/rotate_blit_test

# Packet exec seam against bare managers (no cache/disk). Links the app
# object set (exec calls into RS_UISlots which references App) minus main.
test-net-exec: $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -o $(OBJ_DIR)/net_exec_test \
		game/test/rs_gameproto_exec_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/net_exec_test

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
MOCK230_SRCS = net/mock/mock230_main.c net/mock/mock230_world.c \
	net/mock/mock230_encode.c net/mock/mock230_objinfo.c \
	net/mock/mock230_npcinfo.c net/mock/mock230_combat.c net/mock/mock230_seqinfo.c \
	net/mock/mock230_scripts.c $(SS_SRCS)
mock230: $(OBJ_DIR) $(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/mock230 $(MOCK230_SRCS) \
		$(OBJ_DIR)/isaac.o $(OBJ_DIR)/rsa.o $(OBJ_DIR)/tommath.o \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o -lm
	@echo "built $(OBJ_DIR)/mock230"

# Mock server game logic with no socket: movement, scene rebuild, equipment
# (including the cache's wearpos_2/3 slot claims), inventory drag, npc roaming.
# Runs from the repo root so the default cache path (cache.osrs230) resolves —
# the equipment checks need the cache's wearpos fields to mean anything.
# Compile the mock's own content. The output is checked in, so building or
# testing the mock needs no compiler — this only runs when the .rs2 sources
# change. Ids here are rev-230 ids from cache.osrs230, NOT LostCity's.
MOCK230_SCRIPT_DIR = $(SRC_DIR)/net/mock/scripts
mock230-scripts: sscompile
	./$(OBJ_DIR)/sscompile --src $(MOCK230_SCRIPT_DIR) \
		--out $(MOCK230_SCRIPT_DIR)/build --pack $(MOCK230_SCRIPT_DIR)/pack

test-mock230: mock230
	cd $(REPO_ROOT) && ./src/$(OBJ_DIR)/mock230 --selftest

# WebSocket frame codec: mask/deframe/ping/incomplete/back-to-back, no socket.
test-ws-frame: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/platform -o $(OBJ_DIR)/ws_frame_test \
		platform/net_transport_ws_frame.c platform/test/ws_frame_test.c
	./$(OBJ_DIR)/ws_frame_test

# Boot-manifest loader: parse a fixture, assert field values + relative-path
# joining + CLI-override composition against a stub AppConfig.
test-bootmanifest: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/bootmanifest -o $(OBJ_DIR)/bootmanifest_test \
		bootmanifest/bootmanifest.c $(INI_SRC) \
		bootmanifest/test/bootmanifest_test.c \
		$(RSCACHE_DIR)/build/rscache_unity.o $(RSCACHE_DIR)/build/bzip.o \
		$(RSCACHE_DIR)/build/bzip_encode.o $(RSCACHE_DIR)/build/miniz.o \
		$(RSCACHE_DIR)/build/xteas.o
	./$(OBJ_DIR)/bootmanifest_test

clean:
	rm -rf build build_opt build_web build_web_opt torirs .last_flavor
	rm -rf build_mt build_opt_mt build_web_mt build_web_opt_mt torirs_mt
	# Only our own outputs: build-web/ is a plain output directory that other
	# experiments have also written into.
	rm -f $(REPO_ROOT)/build-web/torirs.js $(REPO_ROOT)/build-web/torirs.wasm \
		$(REPO_ROOT)/build-web/torirs.data $(REPO_ROOT)/build-web/torirs.wasm.map \
		$(REPO_ROOT)/build-web/index.html $(REPO_ROOT)/build-web/torirs_host.js \
		$(REPO_ROOT)/build-web/viewer.html

# Header dependency tracking: -MMD writes a .d next to each .o; include them so
# editing a header rebuilds every object that includes it (stale objects read
# struct fields at old offsets otherwise).
-include $(wildcard $(OBJ_DIR)/*.d)
