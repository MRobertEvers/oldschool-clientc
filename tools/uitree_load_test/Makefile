# Standalone uitree_load_test build (no CMake target).
# Same object list as tools/uitree_loader_test but main is uitree_load_test.c
# (calls uitree_from_revconfig_buildcachedat directly).

ROOT := $(abspath ../..)
OUT  := $(ROOT)/tools/uitree_load_test/uitree_load_test
OBJDIR := $(ROOT)/tools/uitree_load_test/obj

CC ?= cc
CFLAGS := -std=c11 -g -O1 -Wall -D_DARWIN_C_SOURCE
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/src/platforms -I$(ROOT)/src/nuklear \
	-I$(ROOT)/src/platforms/ToriRSPlatformKit/include \
	-DUITREE_LOAD_TEST_ROOT=\"$(ROOT)\" \
	-DDASH_BUCKET_SORT_MODE=0
LDFLAGS := -lm

LUA_SRCS := $(filter-out %/lua.c %/luac.c,$(wildcard $(ROOT)/src/3rd/lua/*.c))

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
	3rd/bzip/bzip.c \
	3rd/miniz/miniz.c \
	3rd/tommath/mpi.c \
	3rd/ini/ini.c

COMMON_SRCS := $(addprefix $(ROOT)/src/,$(C_COMMON))
TEST_MAIN := $(ROOT)/tools/uitree_load_test/uitree_load_test.c
UITREE_LOADER := $(ROOT)/src/osrs/revconfig/uitree_loader.c

SRCS := $(TEST_MAIN) $(UITREE_LOADER) $(COMMON_SRCS) $(LUA_SRCS)
OBJS := $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(OUT)

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(OUT)
