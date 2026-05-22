# Build tiny host that exercises platform_x_lua (sdl2.c name is historical).

REPO_ROOT := ../../..
SRC_DIR   := $(REPO_ROOT)/src
SRC2      := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2)/platforms
SCRIPT_DIR := $(SRC2)/scripting

OBJ_DIR := sdl2_build
PROG    := sdl2

CC      ?= cc
CFLAGS  ?= -O0 -Wall -Wextra -Werror -g
CFLAGS  += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
CPPFLAGS += -I$(SRC_DIR) -I$(SRC2) -I$(SCRIPT_DIR) -I$(REPO_ROOT)

# Vendored Lua — embed library, not lua/luac interpreters.
LUA_SRC := $(filter-out $(SRC_DIR)/3rd/lua/lua.c $(SRC_DIR)/3rd/lua/luac.c,\
	$(wildcard $(SRC_DIR)/3rd/lua/*.c))

SRCS := sdl2.c \
	$(PLAT_DIR)/platform_x_lua.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(PLAT_DIR)/platform_sdl2.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SRC2)/commands/libtorirs_command_queue.c \
	$(SRC2)/input/libtorirs_input.c \
	$(SRC2)/ioqueue/libtorirs_ioqueue.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(SRC2)/buildcache/dat1_buildcache.c \
	$(SRC2)/gamecache/gamecache.c \
	$(SRC2)/toripix/toridraw.c \
	$(SRC2)/toripix/toridraw_math.c \
	$(SRC2)/toripix/toridraw_hsl16.c \
	$(SRC2)/toripix/toridraw_lighting.c \
	$(SRC2)/toripix/toridraw_model.c \
	$(SRC2)/toripix/toridraw_light_model.c \
	$(SRC2)/games/model_viewer.c \
	$(SRC_DIR)/graphics/dash.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/dash_model.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC_DIR)/graphics/shared_tables.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/dash_utils.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(SRC_DIR)/osrs/rscache/cache.c \
	$(SRC_DIR)/osrs/rscache/cache_dat.c \
	$(SRC_DIR)/osrs/rscache/xtea_config.c \
	$(SRC_DIR)/osrs/rscache/reference_table.c \
	$(SRC_DIR)/osrs/rscache/filelist.c \
	$(SRC_DIR)/osrs/rscache/disk.c \
	$(SRC_DIR)/osrs/rscache/rsbuf.c \
	$(SRC_DIR)/osrs/rscache/archive.c \
	$(SRC_DIR)/osrs/rscache/archive_decompress.c \
	$(SRC_DIR)/osrs/rscache/compression.c \
	$(SRC_DIR)/osrs/rscache/xtea.c \
	$(SRC_DIR)/osrs/rscache/tables/string_utils.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/pix8.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/pix32.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_versionlist_mapsquare.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_textures.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_idk.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_npc.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_obj.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_spotanim.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_varp.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/animframe.c \
	$(SRC_DIR)/osrs/rscache/tables/model.c \
	$(PLAT_DIR)/platform_x_cache.c \
	$(SRC2)/libtorirs.c \
	$(LUA_SRC)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)
endif
ifeq ($(strip $(SDL_LIBS)),)
ifeq ($(shell uname -s),Darwin)
SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
SDL_LIBS := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
else
SDL_CFLAGS := -I/usr/include/SDL2
SDL_LIBS := -lSDL2
endif
endif

# Flat object list in OBJ_DIR (same pattern as src/osrs/revconfig/Makefile)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean

all: $(OBJ_DIR) $(PROG)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(SDL_LIBS) -lm

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -f $(PROG)
	rm -rf $(OBJ_DIR)
