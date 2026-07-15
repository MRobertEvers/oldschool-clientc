REPO_ROOT := ../..
SRC_DIR   := $(REPO_ROOT)/src
SRC2      := $(REPO_ROOT)/src2
include $(SRC2)/toriauxlib/core/tasks/tasks_sources.mk
PLAT_DIR  := $(SRC2)/platforms
SCRIPT_DIR := $(SRC2)/scripting

OBJ_DIR := runescape_world_sim_build
TARGET  := runescape_world_sim

CC       ?= cc
CFLAGS   ?= -O0 -Wall -Wextra -g
CFLAGS   += -Wno-implicit-fallthrough -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-parameter
CPPFLAGS += -I$(SRC2)/toridraw -I$(SRC_DIR) -I$(SRC2) -I$(SCRIPT_DIR) -I$(REPO_ROOT)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif
ifeq ($(strip $(SDL_LIBS)),)
  SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
  SDL_LIBS   := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
endif

CPPFLAGS += $(SDL_CFLAGS)

LUA_SRC := $(filter-out $(SRC_DIR)/3rd/lua/lua.c $(SRC_DIR)/3rd/lua/luac.c,\
	$(wildcard $(SRC_DIR)/3rd/lua/*.c))

SRCS := main.c \
	$(PLAT_DIR)/asan_compat.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_soft3d.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x_lua.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SRC2)/commands/libtorirs_command_queue.c \
	$(SRC2)/input/libtorirs_input.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SRC2)/toridraw/toridraw_scene.c \
	$(SRC2)/toridraw/toridraw_intrusive_list.c \
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
	$(SRC2)/toridraw/toridraw_model_transform.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(SRC2)/buildcache/dat1_buildcache.c \
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
	$(SRC2)/toriauxlib/core/tasks/task_toriauxlibtd.c \
	$(SRC2)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2)/toridraw/toridraw_animation.c \
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
	$(SRC2)/buildcache/dat1_buildcache_ui.c \
	$(SRC2)/ui/ui_sprite_lookup.c \
	$(TORIAUXLIB_TASK_SOURCES) \
	$(SRC_DIR)/osrs/revconfig/uitree.c \
	$(SRC_DIR)/3rd/ini/ini.c \
	$(SRC_DIR)/osrs/revconfig/revconfig.c \
	$(SRC_DIR)/osrs/revconfig/revconfig_load.c \
	$(SRC2)/games/model_viewer.c \
	$(SRC2)/games/runescape.c \
	$(SRC2)/runescape/appearance.c \
	$(SRC2)/runescape/player_body.c \
	$(SRC_DIR)/osrs/datatypes/appearances.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC2)/toridraw/graphics/shared_tables.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/painters.c \
	$(SRC_DIR)/osrs/texture.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(PLAT_DIR)/platform_x_cache.c \
	$(SRC2)/libtorirs.c \
	$(LUA_SRC)

LDFLAGS := -lm -Wl,-dead_strip $(SDL_LIBS)

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean check test

all: $(OBJ_DIR)/.stamp $(TARGET)

$(OBJ_DIR)/.stamp:
	mkdir -p $(OBJ_DIR)
	touch $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(1) -o $$@
endef

LEGACY_TABLE_WARNINGS := -Wno-unused-function -Wno-unused-variable -Wno-unused-label \
	-Wno-unused-but-set-variable -Wno-pointer-sign -Wno-parentheses

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

THIRD_PARTY_CFLAGS := $(filter-out -Wall -Wextra -Werror,$(CFLAGS)) -w

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) -c $< -o $@

define make-lua-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)/.stamp
	$(CC) $(CPPFLAGS) $(THIRD_PARTY_CFLAGS) -c $(1) -o $$@
endef
$(foreach src,$(LUA_SRC),$(eval $(call make-lua-goal,$(src))))

clean:
	rm -f $(TARGET)
	rm -rf $(OBJ_DIR)

check test: $(TARGET)
	./$(TARGET)
