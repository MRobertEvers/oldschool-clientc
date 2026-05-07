#ifndef LUA_GAMECACHE_H
#define LUA_GAMECACHE_H

struct GGame;
struct LuaGameType;

struct LuaGameType*
LuaGameCache_convert_all_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_reftables_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_floortypes_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_sequences_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_animbaseframes_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_npcs_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_objs_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_idks_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_spotanims_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_components_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_map_terrain_chunk_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_scenery_chunk_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_loc_configs_chunk_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_one_loc_config_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_models_chunk_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_npc_models_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_obj_models_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

struct LuaGameType*
LuaGameCache_convert_idk_models_from_buildcachedat(struct GGame* game, struct LuaGameType* args);

#endif
