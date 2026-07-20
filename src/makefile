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

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC_DIR)/cs2vm2 -I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src \
	-I$(TORIDRAW_DIR) -I$(BMP_DIR) -I$(HMAP_DIR)

RSCACHE_CFLAGS = -std=c11 -g \
	-I$(RSCACHE_DIR)/include \
	-I$(RSCACHE_DIR)/src \
	-I$(RSCACHE_DIR) \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

TORIDRAW_CFLAGS = -std=c11 -g \
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

SRCS = main.c platform/platform_x_io.c platform/platform_sdl2.c \
	platform/platform_sdl2_renderer_soft3d.c render/torirs_frame.c \
	input/torirs_input.c \
	game/task_cs2_script_exec.c $(BMP_DIR)/bmp.c \
	cs2vm2/cs2vm2.c cs2vm2/cs2vm2_script.c cs2vm2/cs2vm2_trigger_args.c \
	cs2vm2/cs2_opcode_meta.c engine/cache_provider.c engine/cs2vm2_script_from_rscache.c \
	engine/torirs_types.c engine/torirs_model_from_rscache.c \
	engine/torirs_component_from_rscache.c \
	engine/torirs_objtype_from_rscache.c engine/torirs_npctype_from_rscache.c \
	engine/torirs_idk_from_rscache.c \
	engine/torirs_map_from_rscache.c engine/torirs_location_from_rscache.c \
	engine/torirs_flotype_from_rscache.c \
	engine/torirs_sprite_from_rscache.c engine/torirs_font_from_rscache.c \
	engine/torirs_enum_from_rscache.c engine/torirs_struct_from_rscache.c \
	engine/uitree_from_component.c \
	engine/dat1/dat1_buildcache.c engine/dat1/task_dat1_model_load.c \
	engine/dat1/task_dat1_component_pack_load.c engine/dat1/task_dat1_clientscript_load.c \
	engine/dat1/task_dat1_obj_load.c engine/dat1/task_dat1_npc_load.c \
	engine/dat1/task_dat1_idk_load.c \
	engine/dat1/task_dat1_component_load.c \
	engine/dat2/dat2_buildcache.c engine/dat2/task_dat2_model_load.c \
	engine/dat2/task_dat2_component_pack_load.c engine/dat2/task_dat2_clientscript_load.c \
	engine/dat2/task_dat2_obj_load.c engine/dat2/task_dat2_npc_load.c \
	engine/dat2/task_dat2_idk_load.c \
	engine/dat2/task_dat2_map_load.c engine/dat2/task_dat2_loc_load.c \
	engine/dat2/task_dat2_flotype_load.c engine/dat2/task_dat2_underlay_load.c \
	engine/dat2/task_dat2_texture_load.c \
	engine/dat2/task_dat2_sprite_load.c engine/dat2/task_dat2_font_load.c \
	engine/dat2/task_dat2_enum_load.c engine/dat2/task_dat2_struct_load.c \
	engine/dat2/task_dat2_component_load.c \
	engine/uitree_builder/uitree_builder.c \
	engine/uitree_builder/uitree_builder_manifest.c \
	engine/uitree_builder/uitree_builder_bake.c \
	engine/uitree_builder/uitree_builder_inv.c \
	engine/uitree_builder/task_uibuilder_assets_load.c \
	engine/uitree_builder/task_uitree_build.c \
	engine/uitree_builder/task_pack_assets_load.c \
	engine/uitree_builder/task_interface_open.c \
	engine/task_obj_model_load.c \
	engine/uitree_scene_bridge.c \
	engine/player_appearance.c \
	engine/toridraw_animation_from_rscache.c \
	engine/dat2/task_dat2_sequence_load.c \
	engine/uitree_cmd_render.c \
	engine/toridraw_sprite_from_torirs.c \
	engine/toridraw_font_from_torirs.c \
	engine/toridraw_model_from_torirs.c \
	game/rs_cs2_host.c game/task_cs2_run.c \
	ui/uitree.c ui/uitree_layout.c ui/uitree_build.c ui/uitree_host.c \
	ui/uitree_input.c ui/uitree_hover.c ui/uitree_scroll.c ui/uitree_emit.c \
	varp/varp_manager.c \
	inv/inv_manager.c \
	revconfig/revconfig.c revconfig/revconfig_load.c \
	world/entity_pool.c world/world_entity.c world/entity_pathing.c \
	world/entity_registry.c world/world_pickset.c world/world.c world/world_cycle.c \
	painters/painters.c \
	engine/minimap/minimap.c
INI_SRC = $(REPO_ROOT)/3rd/ini/ini.c
RSCACHE_UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
TORIDRAW_UNITY_SRC = $(TORIDRAW_DIR)/toridraw_unity.c
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/platform_x_io.o $(OBJ_DIR)/platform_sdl2.o \
	$(OBJ_DIR)/platform_sdl2_renderer_soft3d.o $(OBJ_DIR)/torirs_frame.o \
	$(OBJ_DIR)/torirs_input.o $(OBJ_DIR)/task_cs2_script_exec.o $(OBJ_DIR)/bmp.o \
	$(OBJ_DIR)/cs2vm2.o $(OBJ_DIR)/cs2vm2_script.o $(OBJ_DIR)/cs2vm2_trigger_args.o \
	$(OBJ_DIR)/cs2_opcode_meta.o $(OBJ_DIR)/cache_provider.o $(OBJ_DIR)/cs2vm2_script_from_rscache.o \
	$(OBJ_DIR)/torirs_types.o $(OBJ_DIR)/torirs_model_from_rscache.o \
	$(OBJ_DIR)/torirs_component_from_rscache.o \
	$(OBJ_DIR)/torirs_objtype_from_rscache.o $(OBJ_DIR)/torirs_npctype_from_rscache.o \
	$(OBJ_DIR)/torirs_idk_from_rscache.o \
	$(OBJ_DIR)/torirs_map_from_rscache.o $(OBJ_DIR)/torirs_location_from_rscache.o \
	$(OBJ_DIR)/torirs_flotype_from_rscache.o \
	$(OBJ_DIR)/torirs_sprite_from_rscache.o $(OBJ_DIR)/torirs_font_from_rscache.o \
	$(OBJ_DIR)/torirs_enum_from_rscache.o $(OBJ_DIR)/torirs_struct_from_rscache.o \
	$(OBJ_DIR)/uitree_from_component.o \
	$(OBJ_DIR)/dat1_buildcache.o $(OBJ_DIR)/task_dat1_model_load.o \
	$(OBJ_DIR)/task_dat1_component_pack_load.o $(OBJ_DIR)/task_dat1_clientscript_load.o \
	$(OBJ_DIR)/task_dat1_obj_load.o $(OBJ_DIR)/task_dat1_npc_load.o \
	$(OBJ_DIR)/task_dat1_idk_load.o \
	$(OBJ_DIR)/task_dat1_component_load.o \
	$(OBJ_DIR)/dat2_buildcache.o $(OBJ_DIR)/task_dat2_model_load.o \
	$(OBJ_DIR)/task_dat2_component_pack_load.o $(OBJ_DIR)/task_dat2_clientscript_load.o \
	$(OBJ_DIR)/task_dat2_obj_load.o $(OBJ_DIR)/task_dat2_npc_load.o \
	$(OBJ_DIR)/task_dat2_idk_load.o \
	$(OBJ_DIR)/task_dat2_map_load.o $(OBJ_DIR)/task_dat2_loc_load.o \
	$(OBJ_DIR)/task_dat2_flotype_load.o $(OBJ_DIR)/task_dat2_underlay_load.o \
	$(OBJ_DIR)/task_dat2_texture_load.o \
	$(OBJ_DIR)/task_dat2_sprite_load.o $(OBJ_DIR)/task_dat2_font_load.o \
	$(OBJ_DIR)/task_dat2_enum_load.o $(OBJ_DIR)/task_dat2_struct_load.o \
	$(OBJ_DIR)/task_dat2_component_load.o \
	$(OBJ_DIR)/uitree_builder.o \
	$(OBJ_DIR)/uitree_builder_manifest.o \
	$(OBJ_DIR)/uitree_builder_bake.o \
	$(OBJ_DIR)/uitree_builder_inv.o \
	$(OBJ_DIR)/task_uibuilder_assets_load.o \
	$(OBJ_DIR)/task_uitree_build.o \
	$(OBJ_DIR)/task_pack_assets_load.o \
	$(OBJ_DIR)/task_interface_open.o \
	$(OBJ_DIR)/task_obj_model_load.o \
	$(OBJ_DIR)/uitree_scene_bridge.o \
	$(OBJ_DIR)/player_appearance.o \
	$(OBJ_DIR)/toridraw_animation_from_rscache.o \
	$(OBJ_DIR)/task_dat2_sequence_load.o \
	$(OBJ_DIR)/uitree_cmd_render.o \
	$(OBJ_DIR)/toridraw_sprite_from_torirs.o \
	$(OBJ_DIR)/toridraw_font_from_torirs.o \
	$(OBJ_DIR)/toridraw_model_from_torirs.o \
	$(OBJ_DIR)/rs_cs2_host.o $(OBJ_DIR)/task_cs2_run.o \
	$(OBJ_DIR)/uitree.o $(OBJ_DIR)/uitree_layout.o $(OBJ_DIR)/uitree_build.o \
	$(OBJ_DIR)/uitree_host.o $(OBJ_DIR)/uitree_input.o $(OBJ_DIR)/uitree_hover.o \
	$(OBJ_DIR)/uitree_scroll.o $(OBJ_DIR)/uitree_emit.o \
	$(OBJ_DIR)/varp_manager.o \
	$(OBJ_DIR)/inv_manager.o \
	$(OBJ_DIR)/revconfig.o $(OBJ_DIR)/revconfig_load.o $(OBJ_DIR)/ini.o \
	$(OBJ_DIR)/entity_pool.o $(OBJ_DIR)/world_entity.o $(OBJ_DIR)/entity_pathing.o \
	$(OBJ_DIR)/entity_registry.o $(OBJ_DIR)/world_pickset.o $(OBJ_DIR)/world.o \
	$(OBJ_DIR)/world_cycle.o \
	$(OBJ_DIR)/painters.o \
	$(OBJ_DIR)/minimap.o \
	$(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o \
	$(OBJ_DIR)/hmap.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o \
	$(OBJ_DIR)/toridraw_font.o $(OBJ_DIR)/toridraw_model_sprite.o

# World builder pipeline objects (compiled as standalone objects; world_builder.c is a single
# translation unit that #includes its .u.c units). Built via the `world-builder` target.
WB_DIR = engine/world_builder
WB_OBJS = $(OBJ_DIR)/world_builder.o \
	$(OBJ_DIR)/blendmap.o $(OBJ_DIR)/overlaymap.o $(OBJ_DIR)/lightmap.o \
	$(OBJ_DIR)/shademap.o $(OBJ_DIR)/sharelight_map.o $(OBJ_DIR)/terrain_shapemap.o \
	$(OBJ_DIR)/contour_ground.o $(OBJ_DIR)/decor_buildmap.o $(OBJ_DIR)/world_decode_tile.o \
	$(OBJ_DIR)/heightmap.o $(OBJ_DIR)/collision_map.o $(OBJ_DIR)/wb_minimap.o \
	$(OBJ_DIR)/toridraw_model_from_torirs.o

.PHONY: all clean run test-varp test-inv test-world test-uitree test-uitree-builder test-revconfig test-world-builder test-minimap world-builder

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(filter-out painters/painters.c engine/minimap/minimap.c,$(SRCS)),$(eval $(call make-goal,$(src))))

$(OBJ_DIR)/painters.o: painters/painters.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/painters -c painters/painters.c -o $@

$(OBJ_DIR)/minimap.o: engine/minimap/minimap.c
	$(CC) $(CFLAGS) -I$(SRC_DIR)/engine/minimap -c engine/minimap/minimap.c -o $@

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

# Reuse the main link set, but swap the engine minimap for the world_builder minimap
# (world.h / world_builder are written against engine/world_builder/minimap.h) and add the
# world_builder pipeline objects. $(sort) dedupes objects shared by OBJS and WB_OBJS.
WB_TEST_OBJS = $(sort $(filter-out $(OBJ_DIR)/main.o $(OBJ_DIR)/minimap.o,$(OBJS)) $(WB_OBJS))
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
		ui/test/uitree_test_main.c ui/test/uitree_test_dirty.c \
		ui/test/uitree_test_walk.c ui/test/uitree_test_hover.c \
		ui/test/uitree_test_layout_build.c ui/test/uitree_test_mutate_emit.c
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

test-revconfig: $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR)/revconfig -I$(SRC_DIR)/revconfig/test -o $(OBJ_DIR)/revconfig_test \
		revconfig/revconfig.c revconfig/revconfig_load.c $(INI_SRC) \
		revconfig/test/revconfig_test_main.c revconfig/test/revconfig_test_buffer.c \
		revconfig/test/revconfig_test_items.c revconfig/test/revconfig_test_load.c \
		revconfig/test/revconfig_test_parse.c
	./$(OBJ_DIR)/revconfig_test

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
