# From repo root: make -C src

CC ?= cc
OBJ_DIR = build
TARGET = torirs

REPO_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR      := $(REPO_ROOT)/src
RSCACHE_DIR  := $(REPO_ROOT)/3rd/rscache
TORIDRAW_DIR := $(REPO_ROOT)/3rd/toridraw
BMP_DIR      := $(REPO_ROOT)/3rd/bmp
HMAP_DIR     := $(REPO_ROOT)/3rd/hmap

CFLAGS = -std=c11 -O0 -fno-omit-frame-pointer -Wall -Wextra -g -MMD -MP \
	-I$(SRC_DIR) -I$(SRC_DIR)/cs2vm2 -I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src \
	-I$(TORIDRAW_DIR) -I$(BMP_DIR) -I$(HMAP_DIR)

RSCACHE_CFLAGS = -std=c11 -O0 -fno-omit-frame-pointer -g -MMD -MP \
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

TORIDRAW_CFLAGS = -std=c11 -O0 -fno-omit-frame-pointer -g -MMD -MP \
	-I$(TORIDRAW_DIR) \
	-I$(BMP_DIR)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
ifeq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif
ifeq ($(strip $(SDL_CFLAGS)$(SDL_LIBS)),)
  SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
  SDL_LIBS   := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
endif

CFLAGS += $(SDL_CFLAGS)
LDFLAGS = -lm -Wl,-dead_strip $(SDL_LIBS)

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = main.c app.c platform/platform_x_io.c platform/platform_sdl2.c \
	platform/platform_sdl2_renderer_soft3d.c render/torirs_frame.c \
	render/torirs_pick.c \
	input/torirs_input.c input/torirs_keymap.c input/torirs_input_cmd.c \
	game/task_cs2_script_exec.c $(BMP_DIR)/bmp.c \
	cs2vm2/cs2vm2.c cs2vm2/cs2vm2_script.c cs2vm2/cs2vm2_trigger_args.c \
	cs1vm/cs1vm.c cs1vm/cs1vm_script.c \
	game/rs_player_stats.c game/rs_cs1_host.c game/task_cs1_run.c \
	game/rs_ui_slots.c game/rs_social.c game/rs_clientcode.c game/rs_if1_buttons.c \
	game/rs_chat.c game/rs_gameproto_exec.c \
	game/rs_worldmap.c \
	cs2vm2/cs2_opcode_meta.c engine/cache_provider.c engine/cs2vm2_script_from_rscache.c \
	engine/torirs_types.c engine/torirs_model_from_rscache.c \
	engine/torirs_component_from_rscache.c \
	engine/torirs_objtype_from_rscache.c engine/torirs_npctype_from_rscache.c \
	engine/torirs_idk_from_rscache.c \
	engine/torirs_map_from_rscache.c engine/torirs_location_from_rscache.c \
	engine/torirs_flotype_from_rscache.c \
	engine/torirs_sprite_from_rscache.c engine/torirs_font_from_rscache.c \
	engine/torirs_enum_from_rscache.c engine/torirs_struct_from_rscache.c \
	engine/torirs_paramtype_from_rscache.c \
	engine/torirs_worldmap_from_rscache.c \
	engine/uitree_from_component.c engine/texture_palette_bake.c \
	engine/dat1/dat1_buildcache.c engine/dat1/task_dat1_model_load.c \
	engine/dat1/task_dat1_component_pack_load.c engine/dat1/task_dat1_clientscript_load.c \
	engine/dat1/task_dat1_obj_load.c engine/dat1/task_dat1_npc_load.c \
	engine/dat1/task_dat1_idk_load.c \
	engine/dat1/task_dat1_component_load.c \
	engine/dat1/task_dat1_sprite_load.c engine/dat1/task_dat1_font_load.c \
	engine/dat1/task_dat1_map_load.c engine/dat1/task_dat1_loc_load.c \
	engine/dat1/task_dat1_flotype_load.c engine/dat1/task_dat1_texture_load.c \
	engine/dat1/task_dat1_sequence_load.c \
	engine/dat2/dat2_buildcache.c engine/dat2/task_dat2_model_load.c \
	engine/dat2/task_dat2_component_pack_load.c engine/dat2/task_dat2_clientscript_load.c \
	engine/dat2/task_dat2_obj_load.c engine/dat2/task_dat2_npc_load.c \
	engine/dat2/task_dat2_idk_load.c \
	engine/dat2/task_dat2_map_load.c engine/dat2/task_dat2_loc_load.c \
	engine/dat2/task_dat2_flotype_load.c engine/dat2/task_dat2_underlay_load.c \
	engine/dat2/task_dat2_texture_load.c \
	engine/dat2/task_dat2_sprite_load.c engine/dat2/task_dat2_font_load.c \
	engine/dat2/task_dat2_enum_load.c engine/dat2/task_dat2_struct_load.c \
	engine/dat2/task_dat2_param_load.c \
	engine/dat2/task_dat2_component_load.c \
	engine/dat2/task_dat2_worldmap_load.c \
	engine/dat2/task_dat2_mapelement_load.c \
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
	inv/inv_manager.c \
	cmd/cmdring.c cmd/cmdbus.c \
	net/isaac.c net/rsa.c net/ringbuf.c net/jbase37.c net/wordpack.c \
	net/loginproto.c net/packetbuffer.c net/net.c net/net_out.c net/bitbuffer.c \
	net/rev/gameproto_parse.c net/rev/pkt_player_info.c net/rev/pkt_npc_info.c \
	net/rev/pkt_player_appearance.c \
	net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c \
	platform/sockstream.c platform/platform_socket.c \
	revconfig/revconfig.c revconfig/revconfig_load.c \
	world/entity_pool.c world/world_entity.c world/entity_pathing.c \
	world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
	engine/world_builder/task_world_load.c \
	painters/painters.c
INI_SRC = $(REPO_ROOT)/3rd/ini/ini.c
RSCACHE_UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
TORIDRAW_UNITY_SRC = $(TORIDRAW_DIR)/toridraw_unity.c
# Objects derived from SRCS (one .o per source, flat in OBJ_DIR) plus the
# out-of-tree/unity extras that have their own rules below. The world_builder
# pipeline ($(WB_OBJS), incl. wb_minimap.o — the Minimap world.h is written
# against) links into the app; $(sort) dedupes objects shared with WB_OBJS.
OBJS = $(sort $(addprefix $(OBJ_DIR)/,$(notdir $(SRCS:.c=.o))) \
	$(OBJ_DIR)/ini.o $(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o \
	$(OBJ_DIR)/hmap.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
	$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/toridraw_model_sprite.o \
	$(OBJ_DIR)/bzip.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o \
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

.PHONY: all clean run test-varp test-inv test-world test-uitree test-uitree-builder test-uitree-builder-dat1 test-revconfig test-world-builder test-minimap test-cs1vm test-cs1 test-cmdbus test-net-login test-net-loopback test-net-exec test-ui-slots test-walkmerge test-entity-decode check-no-drain test-task-order world-builder

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

NET_REV_SRCS = net/rev/lc245_2/gameproto_rev_lc245_2.c net/rev/lc254/gameproto_rev_lc254.c
$(foreach src,$(filter-out painters/painters.c $(NET_REV_SRCS),$(SRCS)),$(eval $(call make-goal,$(src))))

$(OBJ_DIR)/painters.o: painters/painters.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -c painters/painters.c -o $@

# Rev protocol tables include their own dir for packetin.h/packetout.h.
$(OBJ_DIR)/gameproto_rev_lc245_2.o: net/rev/lc245_2/gameproto_rev_lc245_2.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc245_2 -c $< -o $@
$(OBJ_DIR)/gameproto_rev_lc254.o: net/rev/lc254/gameproto_rev_lc254.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/net/rev/lc254 -c $< -o $@

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

$(OBJ_DIR)/hmap.o: $(HMAP_DIR)/hmap.c
	$(CC) $(CFLAGS) -c $(HMAP_DIR)/hmap.c -o $@

$(OBJ_DIR)/ini.o: $(INI_SRC)
	$(CC) $(CFLAGS) -c $(INI_SRC) -o $@

$(OBJ_DIR)/rscache_unity.o: $(RSCACHE_UNITY_SRC)
	$(CC) $(RSCACHE_CFLAGS) -w -c $(RSCACHE_UNITY_SRC) -o $@

$(OBJ_DIR)/bzip.o: $(REPO_ROOT)/3rd/bzip/bzip.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/miniz.o: $(REPO_ROOT)/3rd/miniz/miniz.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@
$(OBJ_DIR)/xteas.o: $(REPO_ROOT)/3rd/xteas/xteas.c
	$(CC) $(COMPRESS_CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(TORIDRAW_UNITY_SRC)
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_UNITY_SRC) -o $@

$(OBJ_DIR)/toridraw_font.o: $(TORIDRAW_DIR)/toridraw_font.c
	$(CC) $(TORIDRAW_CFLAGS) -I$(REPO_ROOT)/v0 -w -c $(TORIDRAW_DIR)/toridraw_font.c -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(TORIDRAW_DIR)/toridraw_model_sprite.c
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_DIR)/toridraw_model_sprite.c -o $@

run: $(TARGET)
	./$(TARGET)

test-varp: $(OBJ_DIR) varp/varp_manager.c varp/varp_manager_test.c
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/varp_manager_test varp/varp_manager.c varp/varp_manager_test.c
	./$(OBJ_DIR)/varp_manager_test

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
	$(OBJ_DIR)/packetbuffer.o $(OBJ_DIR)/gameproto_rev_lc245_2.o \
	$(OBJ_DIR)/gameproto_rev_lc254.o \
	$(OBJ_DIR)/gameproto_parse.o $(OBJ_DIR)/tommath.o \
	$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
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
		world/test/world_test_unit.c world/test/world_test_sim.c world/test/world_test_main.c \
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
		ui/test/uitree_test_minimenu.c ui/test/uitree_test_keys.c
	./$(OBJ_DIR)/uitree_test

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
		net/rev/pkt_player_info.c net/rev/pkt_npc_info.c net/rev/pkt_player_appearance.c \
		net/jbase37.c $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/miniz.o \
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

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

# Header dependency tracking: -MMD writes a .d next to each .o; include them so
# editing a header rebuilds every object that includes it (stale objects read
# struct fields at old offsets otherwise).
-include $(wildcard $(OBJ_DIR)/*.d)
