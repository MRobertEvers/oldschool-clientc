# Standalone gamecache2_test build (no CMake target).
#
# Builds tools/gamecache2_test/gamecache2_test from:
#   - tools/gamecache2_test/main.cpp     — SDL2 window + render loop
#   - src/osrs/gamecache2/gamecache2*.c  — new GameCache2 module
#   - All C sources from C_COMMON        — mirrors uitree_loader_test pattern
#   - Platform C++ sources for SDL2+Soft3D
#
# Usage:
#   cd tools/gamecache2_test && make
#   ./gamecache2_test --cache=../../cache254

ROOT    := $(abspath ../..)
OUTDIR  := $(ROOT)/tools/gamecache2_test
OUT     := $(OUTDIR)/gamecache2_test
OBJDIR  := $(OUTDIR)/obj

CC  ?= cc
CXX ?= c++

CSTD   := -std=c11
CXXSTD := -std=c++17

CPPFLAGS := \
	-I$(ROOT)/src \
	-I$(ROOT)/src/platforms \
	-I$(ROOT)/src/nuklear \
	-I$(ROOT)/src/platforms/ToriRSPlatformKit/include \
	-DGAMECACHE2_TEST_ROOT=\"$(ROOT)\" \
	-DDASH_BUCKET_SORT_MODE=0

SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS   := $(shell sdl2-config --libs)

CPPFLAGS += $(SDL2_CFLAGS)

CFLAGS   := -g -O0 -Wall -D_DARWIN_C_SOURCE
CXXFLAGS := -g -O0 -Wall

LDFLAGS  := -lm $(SDL2_LIBS)

# macOS: link AppKit for SDL2+Soft3D window management
ifeq ($(shell uname),Darwin)
LDFLAGS += -framework AppKit -framework CoreFoundation
endif

# ---------------------------------------------------------------------------
# Lua sources (exclude standalone interpreter / compiler)
# ---------------------------------------------------------------------------
LUA_SRCS := $(filter-out %/lua.c %/luac.c,$(wildcard $(ROOT)/src/3rd/lua/*.c))

# ---------------------------------------------------------------------------
# Core C sources (identical to uitree_loader_test's C_COMMON)
# ---------------------------------------------------------------------------
C_COMMON := \
	osrs/isaac.c \
	jbase37.c \
	tori_rs.c \
	bmp.c \
	tori_rs_render.c \
	LibToriRSPlatformC.c \
	server/server.c \
	server/prot.c \
	graphics/dash.c \
	graphics/dash_bench.c \
	graphics/dash_model.c \
	graphics/dash_minimap.c \
	graphics/dashmap.c \
	datastruct/list.c \
	datastruct/vec.c \
	datastruct/ringbuf.c \
	platforms/common/sockstream.c \
	platforms/common/platform_memory.c \
	platforms/common/mem_format.c \
	platforms/common/torirs_sprite_argb_rgba.c \
	platforms/common/torirs_font_glyph_quad.c \
	nuklear/nuklear_impl.c \
	osrs/ginput.c \
	osrs/buildcachedat.c \
	osrs/gamecache/gamecache.c \
	osrs/gamecache/gamecache_from_buildcachedat.c \
	osrs/buildcachedat_loader.c \
	osrs/buildcache.c \
	osrs/buildcache_loader.c \
	graphics/shared_tables.c \
	graphics/lighting.c \
	osrs/rscache/tables/model.c \
	osrs/rscache/tables/maps.c \
	osrs/rscache/tables/config_locs.c \
	osrs/rscache/tables/config_floortype.c \
	osrs/rscache/tables/config_object.c \
	osrs/rscache/tables/config_sequence.c \
	osrs/rscache/tables/config_npctype.c \
	osrs/rscache/tables/noise.c \
	osrs/rscache/tables/framemap.c \
	osrs/rscache/tables/frame.c \
	osrs/rscache/tables/textures.c \
	osrs/rscache/tables/sprites.c \
	osrs/rscache/tables_dat/config_textures.c \
	osrs/rscache/tables_dat/config_idk.c \
	osrs/rscache/tables_dat/config_varp.c \
	osrs/rscache/tables_dat/config_obj.c \
	osrs/rscache/tables_dat/animframe.c \
	osrs/rscache/tables_dat/pix8.c \
	osrs/rscache/tables_dat/pix32.c \
	osrs/rscache/tables_dat/pixfont.c \
	osrs/painters.c \
	osrs/painters_cullmap_baked_path.c \
	osrs/cache_utils.c \
	osrs/minimap.c \
	osrs/minimenu.c \
	osrs/minimenu_game.c \
	osrs/model_transforms.c \
	osrs/texture.c \
	osrs/collision_map.c \
	osrs/world_pickset.c \
	osrs/world_options.c \
	osrs/entity_pathing_draw.c \
	osrs/collisionmap_overlay_draw.c \
	osrs/entity_hitsplat_emit.c \
	osrs/ui.c \
	osrs/rscache/filelist.c \
	osrs/rscache/archive.c \
	osrs/revconfig/revconfig.c \
	osrs/revconfig/revconfig_load.c \
	osrs/revconfig/uitree.c \
	osrs/revconfig/uitree_textpool.c \
	osrs/revconfig/uitree_load.c \
	osrs/rs_component_gfx.c \
	osrs/revconfig/uiscene.c \
	osrs/rscache/tables_dat/config_component.c \
	osrs/rscache/tables_dat/config_spotanim.c \
	osrs/interface.c \
	osrs/interface_state.c \
	osrs/rs_component_state.c \
	osrs/chat.c \
	osrs/ui_scrollbar_emit.c \
	osrs/clientscript_vm.c \
	osrs/obj_icon.c \
	osrs/gio_cache_dat.c \
	osrs/entity_vec.c \
	osrs/world.c \
	osrs/decor_buildmap.c \
	osrs/scene2.c \
	osrs/heightmap.c \
	osrs/contour_ground.c \
	osrs/lightmap.c \
	osrs/blendmap.c \
	osrs/overlaymap.c \
	osrs/terrain_shapemap.c \
	osrs/sharelight_map.c \
	osrs/shademap.c \
	osrs/world_scenebuild.c \
	osrs/configmap.c \
	osrs/filepack.c \
	osrs/dash_utils.c \
	osrs/script_queue.c \
	osrs/core/game_cache_tag.c \
	osrs/core/revision.c \
	osrs/core/clientprot_core.c \
	osrs/core/clientprot_move_path.c \
	osrs/core/clientprot_send.c \
	osrs/core/gamenet_core_send.c \
	osrs/core/serverprot_core_parse.c \
	osrs/revs/lc245_2/gamenet_rev245_2_send.c \
	osrs/core/ui_loader.c \
	osrs/core/gameproto_core_exec.c \
	osrs/player_stats.c \
	osrs/revs/lc245_2/gamenet_rev245_2_exec.c \
	osrs/revs/lc245_2/serverprot_netrev245_2_parse.c \
	osrs/gamenet_exec.c \
	osrs/gamenet_parse.c \
	osrs/gamenet_process.c \
	osrs/gamenet_send.c \
	osrs/game.c \
	osrs/game_entity.c \
	osrs/wordpack.c \
	osrs/revs/lc245_2/loginproto_rev245_2.c \
	osrs/datatypes/appearances.c \
	osrs/datatypes/player_appearance.c \
	osrs/core/serverprot_pkt_playerinfo.c \
	osrs/core/serverprot_pkt_npcinfo.c \
	osrs/core/packetbuffer.c \
	osrs/rscache/compression.c \
	osrs/rscache/tables/string_utils.c \
	osrs/rscache/tables_dat/config_versionlist_mapsquare.c \
	osrs/rscache/tables_dat/config_npc.c \
	osrs/rscache/cache.c \
	osrs/rscache/cache_dat.c \
	osrs/frustrum_cullmap.c \
	osrs/palette.c \
	osrs/rscache/archive_decompress.c \
	osrs/rscache/disk.c \
	osrs/rscache/reference_table.c \
	osrs/rscache/xtea_config.c \
	osrs/rsa.c \
	osrs/rscache/rsbuf.c \
	osrs/rscache/rsbuf_isaac.c \
	osrs/rscache/tables/component.c \
	osrs/rscache/bitbuffer.c \
	osrs/rscache/xtea.c \
	osrs/revs/lc245_2/revision_lc245_2.c \
	osrs/entity_scenebuild.c \
	osrs/lua_sidecar/luac_sidecar_cachedat.c \
	osrs/lua_sidecar/lua_buildcachedat.c \
	osrs/lua_sidecar/lua_dash.c \
	osrs/lua_sidecar/lua_sidecar_misc.c \
	osrs/lua_sidecar/lua_game.c \
	osrs/lua_sidecar/lua_gamecache.c \
	osrs/lua_sidecar/lua_ui.c \
	osrs/lua_sidecar/lua_gametypes.c \
	osrs/lua_sidecar/luac_gametypes.c \
	osrs/lua_sidecar/luac_sidecar.c \
	osrs/lua_sidecar/lua_api.c \
	osrs/lua_sidecar/lua_api_ht.c \
	osrs/lua_sidecar/lua_platform.c \
	osrs/lua_sidecar/luac_sidecar_config.c \
	osrs/revconfig/uitree_loader.c \
	platforms/common/tori_rs_sdl2_gameinput.c \
	3rd/bzip/bzip.c \
	3rd/miniz/miniz.c \
	3rd/tommath/mpi.c \
	3rd/ini/ini.c

# ---------------------------------------------------------------------------
# GameCache2 module sources
# ---------------------------------------------------------------------------
GC2_C_SRCS := \
	osrs/gamecache2/gamecache2.c \
	osrs/gamecache2/gamecache2_iorequest.c \
	osrs/gamecache2/dat1/gamecache2_dat1.c

# ---------------------------------------------------------------------------
# Test-tool Lua bindings (lives in tools/gamecache2_test, not under src/)
# ---------------------------------------------------------------------------
TEST_C_SRCS := $(ROOT)/tools/gamecache2_test/lua_gc2_test.c

# ---------------------------------------------------------------------------
# C++ platform sources required for SDL2 + Soft3D rendering
# ---------------------------------------------------------------------------
PLATFORM_CPP_SRCS := \
	platforms/platform_impl2_sdl2.cpp \
	platforms/platform_impl2_sdl2_port_osx.cpp \
	platforms/platform_impl2_sdl2_renderer_soft3d.cpp \
	platforms/platform_impl2_sdl2_renderer_soft3d_shared.cpp \
	platforms/common/torirs_nk_ui_bridge.cpp \
	platforms/common/torirs_nk_sdl_input.cpp \
	platforms/common/tori_rs_sdl2_gameinput_nuklear.cpp \
	platforms/common/torirs_nuklear_debug_panel.cpp \
	platforms/common/torirs_sprite_draw_cpu.cpp

# ---------------------------------------------------------------------------
# Assemble source lists
# ---------------------------------------------------------------------------
COMMON_C_SRCS  := $(addprefix $(ROOT)/src/, $(C_COMMON))
GC2_C_FULL     := $(addprefix $(ROOT)/src/, $(GC2_C_SRCS))
PLATFORM_CPP   := $(addprefix $(ROOT)/src/, $(PLATFORM_CPP_SRCS))
TEST_MAIN_CPP  := $(ROOT)/tools/gamecache2_test/main.cpp

ALL_C_SRCS   := $(COMMON_C_SRCS) $(GC2_C_FULL) $(LUA_SRCS) $(TEST_C_SRCS)
ALL_CPP_SRCS := $(PLATFORM_CPP) $(TEST_MAIN_CPP)

C_OBJS   := $(patsubst $(ROOT)/%.c,  $(OBJDIR)/%.o, $(ALL_C_SRCS))
CPP_OBJS := $(patsubst $(ROOT)/%.cpp,$(OBJDIR)/%.o, $(ALL_CPP_SRCS))
ALL_OBJS := $(C_OBJS) $(CPP_OBJS)

.PHONY: all clean run

all: $(OUT)

$(OUT): $(ALL_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(ALL_OBJS)

$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CSTD) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXSTD) $(CXXFLAGS) -c -o $@ $<

run: $(OUT)
	$(OUT) --cache=$(ROOT)/cache254

clean:
	rm -rf $(OBJDIR) $(OUT)
