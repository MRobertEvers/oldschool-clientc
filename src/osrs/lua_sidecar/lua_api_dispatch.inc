    case LUA_API_BUILDCACHEDAT_MAP_SCENERY_CACHE_ADD:
        return LuaBuildCacheDat_map_scenery_cache_add(bcd, args);
    case LUA_API_BUILDCACHEDAT_SET_CONFIG_JAGFILE:
        return LuaBuildCacheDat_set_config_jagfile(bcd, args);
    case LUA_API_BUILDCACHEDAT_INIT_VARP_VARBIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(bcd, game, args);
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
    case LUA_API_BUILDCACHEDAT_GET_SEQUENCE_HELD_OBJ_IDS:
        return LuaBuildCacheDat_get_sequence_held_obj_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_PLAYER_INFO_SEQUENCE_IDS:
        return LuaBuildCacheDat_get_player_info_sequence_ids(bcd, args);
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
    case LUA_API_BUILDCACHEDAT_GET_LOC_MODEL_IDS:
        return LuaBuildCacheDat_get_loc_model_ids(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_SPOTANIM_MODEL_ID:
        return LuaBuildCacheDat_get_spotanim_model_id(bcd, args);
    case LUA_API_BUILDCACHEDAT_GET_SPOTANIM_SEQ_ID:
        return LuaBuildCacheDat_get_spotanim_seq_id(bcd, args);
    case LUA_API_BUILDCACHEDAT_SPOTANIMS_INIT_FROM_CONFIG_JAGFILE:
        return LuaBuildCacheDat_spotanims_init_from_config_jagfile(bcd, args);
    case LUA_API_GAME_BUILD_SCENE:
        return LuaGame_build_scene(game, args);
    case LUA_API_GAME_BUILD_SCENE_CENTERZONE:
        return LuaGame_build_scene_centerzone(game, args);
    case LUA_API_GAME_POP_NEXT_PACKET:
        return LuaGame_pop_next_packet(game, args);
    case LUA_API_GAME_EXEC_PACKET:
        return LuaGame_exec_packet(game, args);
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
    case LUA_API_GAME_GET_PKT_PLAYER_INFO_DATA:
        return LuaGame_get_pkt_player_info_data(game, args);
    case LUA_API_GAME_GET_PKT_PLAYER_INFO_LENGTH:
        return LuaGame_get_pkt_player_info_length(game, args);
    case LUA_API_GAME_GET_PKT_NPC_INFO_DATA:
        return LuaGame_get_pkt_npc_info_data(game, args);
    case LUA_API_GAME_GET_PKT_NPC_INFO_LENGTH:
        return LuaGame_get_pkt_npc_info_length(game, args);
    case LUA_API_GAME_GET_PKT_REBUILD_NORMAL_ZONEX:
        return LuaGame_get_pkt_rebuild_normal_zonex(game, args);
    case LUA_API_GAME_GET_PKT_REBUILD_NORMAL_ZONEZ:
        return LuaGame_get_pkt_rebuild_normal_zonez(game, args);
    case LUA_API_GAME_GET_PKT_IF_SETOBJECT_OBJ_ID:
        return LuaGame_get_pkt_if_setobject_obj_id(game, args);
    case LUA_API_GAME_GET_PKT_IF_SETMODEL_MODEL_ID:
        return LuaGame_get_pkt_if_setmodel_model_id(game, args);
    case LUA_API_GAME_GET_PKT_IF_SETANIM_ANIM_ID:
        return LuaGame_get_pkt_if_setanim_anim_id(game, args);
    case LUA_API_GAME_GET_PKT_IF_SETNPCHEAD_NPC_ID:
        return LuaGame_get_pkt_if_setnpchead_npc_id(game, args);
    case LUA_API_GAME_GET_PKT_UPDATE_INV_PARTIAL_ENTRIES:
        return LuaGame_get_pkt_update_inv_partial_entries(game, args);
    case LUA_API_GAME_GET_PKT_LOC_ADD_CHANGE_LOC_ID:
        return LuaGame_get_pkt_loc_add_change_loc_id(game, args);
    case LUA_API_GAME_GET_PKT_LOC_ANIM_SEQ_ID:
        return LuaGame_get_pkt_loc_anim_seq_id(game, args);
    case LUA_API_GAME_GET_PKT_P_LOCMERGE_LOC_ID:
        return LuaGame_get_pkt_p_locmerge_loc_id(game, args);
    case LUA_API_GAME_GET_PKT_OBJ_ADD_OBJ_ID:
        return LuaGame_get_pkt_obj_add_obj_id(game, args);
    case LUA_API_GAME_GET_PKT_OBJ_REVEAL_OBJ_ID:
        return LuaGame_get_pkt_obj_reveal_obj_id(game, args);
    case LUA_API_GAME_GET_PKT_MAP_ANIM_SPOTANIM_ID:
        return LuaGame_get_pkt_map_anim_spotanim_id(game, args);
    case LUA_API_GAME_GET_PKT_MAP_PROJANIM_SPOTANIM_ID:
        return LuaGame_get_pkt_map_projanim_spotanim_id(game, args);
    case LUA_API_GAMECACHE_CONVERT_ALL_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_all_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_REFTABLES_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_reftables_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_FLOORTYPES_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_floortypes_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_SEQUENCES_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_sequences_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_ANIMBASEFRAMES_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_animbaseframes_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_NPCS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_npcs_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_OBJS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_objs_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_IDKS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_idks_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_SPOTANIMS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_spotanims_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_COMPONENTS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_components_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_MAP_TERRAIN_CHUNK_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_map_terrain_chunk_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_SCENERY_CHUNK_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_scenery_chunk_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_LOC_CONFIGS_CHUNK_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_loc_configs_chunk_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_MODELS_CHUNK_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_models_chunk_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_NPC_MODELS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_npc_models_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_OBJ_MODELS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_obj_models_from_buildcachedat(game, args);
    case LUA_API_GAMECACHE_CONVERT_IDK_MODELS_FROM_BUILDCACHEDAT:
        return LuaGameCache_convert_idk_models_from_buildcachedat(game, args);
