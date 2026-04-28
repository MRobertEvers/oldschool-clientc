    case LUA_API_BUILDCACHEDAT_MAP_SCENERY_CACHE_ADD:
        return LuaBuildCacheDat_map_scenery_cache_add(bcd, args);
    case LUA_API_BUILDCACHEDAT_SET_CONFIG_JAGFILE:
        return LuaBuildCacheDat_set_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_INIT_VARP_VARBIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_SET_VERSIONLIST_JAGFILE:
        return LuaBuildCacheDat_set_versionlist_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_MAP_TERRAIN_CACHE_ADD:
        return LuaBuildCacheDat_map_terrain_cache_add(bcd, args);
    case LUA_API_BUILDCACHEDAT_HAS_MAP_TERRAIN:
        return LuaBuildCacheDat_has_map_terrain(bcd, args);
    case LUA_API_BUILDCACHEDAT_HAS_MAP_SCENERY:
        return LuaBuildCacheDat_has_map_scenery(bcd, args);
    case LUA_API_BUILDCACHEDAT_MODEL_CACHE_HAS:
        return LuaBuildCacheDat_model_cache_has(bcd, args);
    case LUA_API_BUILDCACHEDAT_ANIMBASEFRAMES_CACHE_HAS:
        return LuaBuildCacheDat_animbaseframes_cache_has(bcd, args);
    case LUA_API_BUILDCACHEDAT_FLOORTYPES_INIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_floortypes_init_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_INIT_SCENERY_CONFIGS_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_init_scenery_configs_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_ALL_SCENERY_LOCS:
        return LuaBuildCacheDat_get_all_scenery_locs(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_SCENERY_MODEL_IDS:
        return LuaBuildCacheDat_get_scenery_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_ALL_UNIQUE_SCENERY_MODEL_IDS:
        return LuaBuildCacheDat_get_all_unique_scenery_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_NPC_MODEL_IDS:
        return LuaBuildCacheDat_get_npc_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_NPC_HEAD_MODEL_IDS:
        return LuaBuildCacheDat_get_npc_head_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_IDK_MODEL_IDS:
        return LuaBuildCacheDat_get_idk_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_IDK_HEAD_MODEL_IDS:
        return LuaBuildCacheDat_get_idk_head_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_OBJ_MODEL_IDS:
        return LuaBuildCacheDat_get_obj_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_OBJ_HEAD_MODEL_IDS:
        return LuaBuildCacheDat_get_obj_head_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_OBJ:
        return LuaBuildCacheDat_get_obj(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_PLAYER_APPEARANCE_IDS_FROM_PACKET:
        return LuaBuildCacheDat_get_player_appearance_ids_from_packet(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_NPC_IDS_FROM_PACKET:
        return LuaBuildCacheDat_get_npc_ids_from_packet(bcd, args);
    case LUA_API_BUILDCACHEDAT_MODEL_CACHE_ADD:
        return LuaBuildCacheDat_model_cache_add(bcd, args);
    case LUA_API_BUILDCACHEDAT_LOAD_INTERFACES:
        return LuaBuildCacheDat_load_interfaces(bcd, args);
    case LUA_API_BUILDCACHEDAT_SEQUENCES_INIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_sequences_init_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_ANIMBASEFRAMES_COUNT_FROM_VERSIONLIST_JAGFILE:
        return LuaBuildCacheDat_get_animbaseframes_count_from_versionlist_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_ANIMBASEFRAMES_CACHE_ADD:
        return LuaBuildCacheDat_animbaseframes_cache_add(bcd, args);
    case LUA_API_BUILDCACHEDAT_IDKITS_INIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_idkits_init_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_OBJECTS_INIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_objects_init_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_SET_2D_MEDIA_JAGFILE:
        return LuaBuildCacheDat_set_2d_media_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_CACHE_TEXTURES:
        return LuaBuildCacheDat_cache_textures(bcd, game, args);
    case LUA_API_BUILDCACHEDAT_CACHE_TITLE:
        return LuaBuildCacheDat_cache_title(bcd, game, args);
    case LUA_API_BUILDCACHEDAT_CACHE_MEDIA:
        return LuaBuildCacheDat_cache_media(bcd, game, args);
    case LUA_API_BUILDCACHEDAT_LOAD_COMPONENT_SPRITES_FROM_MEDIA:
        return LuaBuildCacheDat_load_component_sprites_from_media(bcd, game, args);
    case LUA_API_BUILDCACHEDAT_FINALIZE_SCENE:
        return LuaBuildCacheDat_finalize_scene(bcd, game, args);
    case LUA_API_BUILDCACHEDAT_COMPONENT_CACHE_CLEAR:
        return LuaBuildCacheDat_component_cache_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_CLEAR_MAP_CHUNKS:
        return LuaBuildCacheDat_clear_map_chunks(bcd, args);
    case LUA_API_BUILDCACHEDAT_MODEL_CACHE_CLEAR:
        return LuaBuildCacheDat_model_cache_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_CLEAR_CONFIG_JAGFILE:
        return LuaBuildCacheDat_clear_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_CLEAR_VERSIONLIST_JAGFILE:
        return LuaBuildCacheDat_clear_versionlist_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_CLEAR_MEDIA_JAGFILE:
        return LuaBuildCacheDat_clear_media_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_CLEAR:
        return LuaBuildCacheDat_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_HAS_CONFIG_JAGFILE:
        return LuaBuildCacheDat_has_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_HAS_VERSIONLIST_JAGFILE:
        return LuaBuildCacheDat_has_versionlist_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_SEQUENCE_ANIMBASEFRAMES_IDS:
        return LuaBuildCacheDat_get_sequence_animbaseframes_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_SCENERY_CONFIG_LOAD_MAPCHUNK_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_scenery_config_load_mapchunk_from_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_SCENERY_CONFIG_GET_MODEL_IDS_MAPCHUNK:
        return LuaBuildCacheDat_scenery_config_get_model_ids_mapchunk(bcd, args);
    case LUA_API_BUILDCACHEDAT_SCENERY_CONFIG_GET_ANIMBASEFRAMES_IDS_MAPCHUNK:
        return LuaBuildCacheDat_scenery_config_get_animbaseframes_ids_mapchunk(bcd, args);
    case LUA_API_BUILDCACHEDAT_ANIMBASEFRAMES_CACHE_CLEAR:
        return LuaBuildCacheDat_animbaseframes_cache_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_SCENERY_CONFIG_CLEAR:
        return LuaBuildCacheDat_scenery_config_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_SEQUENCES_CLEAR:
        return LuaBuildCacheDat_sequences_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_MAP_SCENERY_CACHE_CLEAR:
        return LuaBuildCacheDat_map_scenery_cache_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_MAP_TERRAIN_CACHE_CLEAR:
        return LuaBuildCacheDat_map_terrain_cache_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_FLOORTYPES_CLEAR:
        return LuaBuildCacheDat_floortypes_clear(bcd, args);
    case LUA_API_BUILDCACHEDAT_OBJECTS_CLEAR:
        return LuaBuildCacheDat_objects_clear(bcd, args);
    case LUA_API_GAME_BUILD_SCENE:
        return LuaGame_build_scene(game, args);
    case LUA_API_GAME_BUILD_SCENE_CENTERZONE:
        return LuaGame_build_scene_centerzone(game, args);
    case LUA_API_GAME_EXEC_PKT_PLAYER_INFO:
        return LuaGame_exec_pkt_player_info(game, args);
    case LUA_API_GAME_EXEC_PKT_NPC_INFO:
        return LuaGame_exec_pkt_npc_info(game, args);
    case LUA_API_GAME_EXEC_PKT_IF_SETTAB:
        return LuaGame_exec_pkt_if_settab(game, args);
    case LUA_API_GAME_EXEC_PKT_UPDATE_INV_FULL:
        return LuaGame_exec_pkt_update_inv_full(game, args);
    case LUA_API_GAME_GET_INV_OBJ_IDS:
        return LuaGame_get_inv_obj_ids(game, args);
    case LUA_API_GAME_LOAD_INTERFACES:
        return LuaGame_load_interfaces(game, args);
    case LUA_API_GAME_LOAD_COMPONENT_SPRITES:
        return LuaGame_load_component_sprites(game, args);
    case LUA_API_GAME_GET_INTERFACE_MODEL_IDS:
        return LuaGame_get_interface_model_ids(game, args);
    case LUA_API_GAME_GET_HEAP_USAGE_MB:
        return LuaGame_get_heap_usage_mb(game, args);
    case LUA_API_GAME_REBUILD_CENTERZONE_BEGIN:
        return LuaGame_rebuild_centerzone_begin(game, args);
    case LUA_API_GAME_REBUILD_CENTERZONE_CHUNK:
        return LuaGame_rebuild_centerzone_chunk(game, args);
    case LUA_API_GAME_REBUILD_CENTERZONE_END:
        return LuaGame_rebuild_centerzone_end(game, args);
    case LUA_API_GAME_REBUILD_CENTERZONE_SLOW:
        return LuaGame_rebuild_centerzone_slow(game, args);
    case LUA_API_GAME_SPAWN_ELEMENT:
        return LuaGame_spawn_element(game, args);
    case LUA_API_DASH_LOAD_TEXTURES:
        return LuaDash_load_textures(dash, bcd, game, args);
    case LUA_API_UI_LOAD_REVCONFIG:
        return LuaUI_load_revconfig(game, bcd, args);
    case LUA_API_UI_LOAD_FONTS:
        return LuaUI_load_fonts(game, bcd, args);
    case LUA_API_UI_LOAD_RS_COMPONENTS:
        return LuaUI_load_rs_components(game, bcd, args);
    case LUA_API_UI_RESOLVE_INV_SPRITES:
        return LuaUI_resolve_inv_sprites(game, bcd, args);
    case LUA_API_UI_PARSE_REVCONFIG:
        return LuaUI_parse_revconfig(game, bcd, args);
    case LUA_API_UI_GET_REVCONFIG_INV_OBJ_IDS:
        return LuaUI_get_revconfig_inv_obj_ids(game, bcd, args);
    case LUA_API_UI_LOAD_REVCONFIG_INVENTORIES:
        return LuaUI_load_revconfig_inventories(game, bcd, args);
    case LUA_API_UI_LOAD_REVCONFIG_UI:
        return LuaUI_load_revconfig_ui(game, bcd, args);
    case LUA_API_MISC_READ_CULLMAP_FROM_BLOB:
        return LuaSidecarMisc_read_cullmap_from_blob(game, args);
    case LUA_API_MISC_SAVE_CAMERA:
        return LuaSidecarMisc_save_camera(game, args);
    case LUA_API_MISC_LOAD_CAMERA:
        return LuaSidecarMisc_load_camera(game, args);
