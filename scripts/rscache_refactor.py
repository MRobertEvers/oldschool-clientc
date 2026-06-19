#!/usr/bin/env python3
"""Mechanical RSCache library refactor: move files, rename symbols, update includes."""

import os
import re
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RSCACHE = REPO / "src/osrs/rscache"

# --- File moves: old relative path -> new relative path ---
FILE_MOVES = {
    # shared
    "rsbuf.c": "shared/RSCacheShared_RSBuffer.c",
    "rsbuf.h": "shared/RSCacheShared_RSBuffer.h",
    "bitbuffer.c": "shared/RSCacheShared_BitBuffer.c",
    "bitbuffer.h": "shared/RSCacheShared_BitBuffer.h",
    "compression.c": "shared/RSCacheShared_Compression.c",
    "compression.h": "shared/RSCacheShared_Compression.h",
    "archive.c": "shared/RSCacheShared_Archive.c",
    "archive.h": "shared/RSCacheShared_Archive.h",
    "archive_decompress.c": "shared/RSCacheShared_ArchiveDecompress.c",
    "archive_decompress.h": "shared/RSCacheShared_ArchiveDecompress.h",
    "filelist.c": "shared/RSCacheShared_FileList.c",
    "filelist.h": "shared/RSCacheShared_FileList.h",
    "xtea.c": "shared/RSCacheShared_Xtea.c",
    "xtea.h": "shared/RSCacheShared_Xtea.h",
    "xtea_config.c": "shared/RSCacheShared_XteaConfig.c",
    "xtea_config.h": "shared/RSCacheShared_XteaConfig.h",
    "tables/string_utils.c": "shared/RSCacheShared_StringUtils.c",
    "tables/string_utils.h": "shared/RSCacheShared_StringUtils.h",
    # disk
    "disk.c": "disk/RSCacheDisk.c",
    "disk.h": "disk/RSCacheDisk.h",
    # dat2disk
    "cache.c": "dat2disk/RSCacheDat2Disk.c",
    "cache.h": "dat2disk/RSCacheDat2Disk.h",
    "reference_table.c": "dat2disk/RSCacheDat2Disk_ReferenceTable.c",
    "reference_table.h": "dat2disk/RSCacheDat2Disk_ReferenceTable.h",
    "cache_inet.c": "dat2disk/RSCacheDat2Disk_Inet.c",
    "cache_inet.h": "dat2disk/RSCacheDat2Disk_Inet.h",
    "cache_inet_indexeddb.c": "dat2disk/RSCacheDat2Disk_InetIndexeddb.c",
    "cache_inet_indexeddb.h": "dat2disk/RSCacheDat2Disk_InetIndexeddb.h",
    # dat1disk
    "cache_dat.c": "dat1disk/RSCacheDat1Disk.c",
    "cache_dat.h": "dat1disk/RSCacheDat1Disk.h",
    # dat2a
    "tables/model.c": "dat2a/RSCacheDat2A_Model.c",
    "tables/model.h": "dat2a/RSCacheDat2A_Model.h",
    "tables/maps.c": "dat2a/RSCacheDat2A_Maps.c",
    "tables/maps.h": "dat2a/RSCacheDat2A_Maps.h",
    "tables/maps_dat.c": "dat2a/RSCacheDat2A_MapsDat.c",
    "tables/maps_dat.h": "dat2a/RSCacheDat2A_MapsDat.h",
    "tables/frame.c": "dat2a/RSCacheDat2A_Frame.c",
    "tables/frame.h": "dat2a/RSCacheDat2A_Frame.h",
    "tables/framemap.c": "dat2a/RSCacheDat2A_Framemap.c",
    "tables/framemap.h": "dat2a/RSCacheDat2A_Framemap.h",
    "tables/sprites.c": "dat2a/RSCacheDat2A_Sprites.c",
    "tables/sprites.h": "dat2a/RSCacheDat2A_Sprites.h",
    "tables/textures.c": "dat2a/RSCacheDat2A_Textures.c",
    "tables/textures.h": "dat2a/RSCacheDat2A_Textures.h",
    "tables/component.c": "dat2a/RSCacheDat2A_Component.c",
    "tables/component.h": "dat2a/RSCacheDat2A_Component.h",
    "tables/config_locs.c": "dat2a/RSCacheDat2A_ConfigLocs.c",
    "tables/config_locs.h": "dat2a/RSCacheDat2A_ConfigLocs.h",
    "tables/config_floortype.c": "dat2a/RSCacheDat2A_ConfigFloortype.c",
    "tables/config_floortype.h": "dat2a/RSCacheDat2A_ConfigFloortype.h",
    "tables/config_object.c": "dat2a/RSCacheDat2A_ConfigObject.c",
    "tables/config_object.h": "dat2a/RSCacheDat2A_ConfigObject.h",
    "tables/config_npctype.c": "dat2a/RSCacheDat2A_ConfigNpctype.c",
    "tables/config_npctype.h": "dat2a/RSCacheDat2A_ConfigNpctype.h",
    "tables/config_idk.c": "dat2a/RSCacheDat2A_ConfigIdk.c",
    "tables/config_idk.h": "dat2a/RSCacheDat2A_ConfigIdk.h",
    "tables/config_sequence.c": "dat2a/RSCacheDat2A_ConfigSequence.c",
    "tables/config_sequence.h": "dat2a/RSCacheDat2A_ConfigSequence.h",
    "tables/configs.h": "dat2a/RSCacheDat2A_Configs.h",
    "tables/noise.c": "dat2a/RSCacheDat2A_Noise.c",
    "tables/noise.h": "dat2a/RSCacheDat2A_Noise.h",
    "tables/noise_cos_table.h": "dat2a/RSCacheDat2A_NoiseCosTable.h",
    # dat1a
    "tables_dat/config_obj.c": "dat1a/RSCacheDat1A_ConfigObj.c",
    "tables_dat/config_obj.h": "dat1a/RSCacheDat1A_ConfigObj.h",
    "tables_dat/config_npc.c": "dat1a/RSCacheDat1A_ConfigNpc.c",
    "tables_dat/config_npc.h": "dat1a/RSCacheDat1A_ConfigNpc.h",
    "tables_dat/config_idk.c": "dat1a/RSCacheDat1A_ConfigIdk.c",
    "tables_dat/config_idk.h": "dat1a/RSCacheDat1A_ConfigIdk.h",
    "tables_dat/config_varp.c": "dat1a/RSCacheDat1A_ConfigVarp.c",
    "tables_dat/config_varp.h": "dat1a/RSCacheDat1A_ConfigVarp.h",
    "tables_dat/config_spotanim.c": "dat1a/RSCacheDat1A_ConfigSpotanim.c",
    "tables_dat/config_spotanim.h": "dat1a/RSCacheDat1A_ConfigSpotanim.h",
    "tables_dat/config_component.c": "dat1a/RSCacheDat1A_ConfigComponent.c",
    "tables_dat/config_component.h": "dat1a/RSCacheDat1A_ConfigComponent.h",
    "tables_dat/config_textures.c": "dat1a/RSCacheDat1A_ConfigTextures.c",
    "tables_dat/config_textures.h": "dat1a/RSCacheDat1A_ConfigTextures.h",
    "tables_dat/config_versionlist_mapsquare.c": "dat1a/RSCacheDat1A_VersionListMapsquare.c",
    "tables_dat/config_versionlist_mapsquare.h": "dat1a/RSCacheDat1A_VersionListMapsquare.h",
    "tables_dat/animframe.c": "dat1a/RSCacheDat1A_AnimFrame.c",
    "tables_dat/animframe.h": "dat1a/RSCacheDat1A_AnimFrame.h",
    "tables_dat/pix8.c": "dat1a/RSCacheDat1A_Pix8.c",
    "tables_dat/pix8.h": "dat1a/RSCacheDat1A_Pix8.h",
    "tables_dat/pix32.c": "dat1a/RSCacheDat1A_Pix32.c",
    "tables_dat/pix32.h": "dat1a/RSCacheDat1A_Pix32.h",
    "tables_dat/pixfont.c": "dat1a/RSCacheDat1A_PixFont.c",
    "tables_dat/pixfont.h": "dat1a/RSCacheDat1A_PixFont.h",
    "tables_dat/configs_dat.h": "dat1a/RSCacheDat1A_ConfigsDat.h",
}

# Build include path mapping from old paths to new paths
INCLUDE_PATH_MAP = {}
for old, new in FILE_MOVES.items():
    if old.endswith(".h"):
        # various include styles used in the codebase
        basename = os.path.basename(old)
        new_inc = f"osrs/rscache/{new}"
        INCLUDE_PATH_MAP[old] = new_inc
        INCLUDE_PATH_MAP[basename] = new_inc
        INCLUDE_PATH_MAP[f"rscache/{basename}"] = new_inc
        INCLUDE_PATH_MAP[f"rscache/{old}"] = new_inc
        INCLUDE_PATH_MAP[f"osrs/rscache/{basename}"] = new_inc
        INCLUDE_PATH_MAP[f"osrs/rscache/{old}"] = new_inc
        INCLUDE_PATH_MAP[f"src/osrs/rscache/{basename}"] = new_inc
        INCLUDE_PATH_MAP[f"src/osrs/rscache/{old}"] = new_inc
        # tables/ and tables_dat/ paths
        if old.startswith("tables/"):
            INCLUDE_PATH_MAP[f"tables/{basename}"] = new_inc
            INCLUDE_PATH_MAP[f"../tables/{basename}"] = new_inc
            INCLUDE_PATH_MAP[f"../{old}"] = new_inc
        if old.startswith("tables_dat/"):
            INCLUDE_PATH_MAP[f"tables_dat/{basename}"] = new_inc
            INCLUDE_PATH_MAP[f"../tables_dat/{basename}"] = new_inc
            INCLUDE_PATH_MAP[f"../{old}"] = new_inc

# --- Symbol renames: old -> new (longest first when applying) ---
SYMBOL_RENAMES = {
    # Shared
    "struct ArchiveBuffer": "struct RSCacheShared_ArchiveBuffer",
    "enum ArchiveFormat": "enum RSCacheShared_ArchiveFormat",
    "ARCHIVE_FORMAT_DAT2": "RSCacheShared_ArchiveFormat_Dat2",
    "ARCHIVE_FORMAT_DAT": "RSCacheShared_ArchiveFormat_Dat",
    "ARCHIVE_FORMAT_DAT_MULTIFILE": "RSCacheShared_ArchiveFormat_DatMultifile",
    "archive_name_hash_dat2": "RSCacheShared_ArchiveNameHashDat2",
    "archive_name_hash_dat": "RSCacheShared_ArchiveNameHashDat",
    "archive_decompress_dat": "RSCacheShared_ArchiveDecompressDat",
    "archive_decrypt_decompress": "RSCacheShared_ArchiveDecryptDecompress",
    "archive_decompress": "RSCacheShared_ArchiveDecompress",
    "struct FileListDatIndexed": "struct RSCacheShared_FileListDatIndexed",
    "filelist_dat_indexed_new_from_decode": "RSCacheShared_FileListDatIndexedNewFromDecode",
    "filelist_dat_indexed_free": "RSCacheShared_FileListDatIndexedFree",
    "struct FileListDat": "struct RSCacheShared_FileListDat",
    "filelist_dat_new_from_cache_dat_archive": "RSCacheShared_FileListDatNewFromCacheDatArchive",
    "filelist_dat_new_from_decode": "RSCacheShared_FileListDatNewFromDecode",
    "filelist_dat_free": "RSCacheShared_FileListDatFree",
    "filelist_dat_find_file_by_name": "RSCacheShared_FileListDatFindFileByName",
    "struct FileList": "struct RSCacheShared_FileList",
    "filelist_new_from_cache_archive": "RSCacheShared_FileListNewFromCacheArchive",
    "filelist_new_from_decode": "RSCacheShared_FileListNewFromDecode",
    "filelist_free": "RSCacheShared_FileListFree",
    "struct RSBuffer": "struct RSCacheShared_RSBuffer",
    "struct Params": "struct RSCacheShared_Params",
    "rsbuf_init": "RSCacheShared_RSBufferInit",
    "rsbuf_g1": "RSCacheShared_RSBufferG1",
    "rsbuf_g1b": "RSCacheShared_RSBufferG1b",
    "rsbuf_g2": "RSCacheShared_RSBufferG2",
    "rsbuf_g2b": "RSCacheShared_RSBufferG2b",
    "rsbuf_g3": "RSCacheShared_RSBufferG3",
    "rsbuf_g4": "RSCacheShared_RSBufferG4",
    "rsbuf_g8": "RSCacheShared_RSBufferG8",
    "rsbuf_p1": "RSCacheShared_RSBufferP1",
    "rsbuf_p2": "RSCacheShared_RSBufferP2",
    "rsbuf_p4": "RSCacheShared_RSBufferP4",
    "rsbuf_read_usmart": "RSCacheShared_RSBufferReadUsmart",
    "rsbuf_read_big_smart": "RSCacheShared_RSBufferReadBigSmart",
    "rsbuf_read_string_null_terminated": "RSCacheShared_RSBufferReadStringNullTerminated",
    "rsbuf_read_string_newline_terminated": "RSCacheShared_RSBufferReadStringNewlineTerminated",
    "rsbuf_read_unsigned_int_smart_short_compat": "RSCacheShared_RSBufferReadUnsignedIntSmartShortCompat",
    "rsbuf_read_short_smart": "RSCacheShared_RSBufferReadShortSmart",
    "rsbuf_read_unsigned_short_smart": "RSCacheShared_RSBufferReadUnsignedShortSmart",
    "rsbuf_read_params": "RSCacheShared_RSBufferReadParams",
    "rsbuf_readto": "RSCacheShared_RSBufferReadto",
    "rsbuf_pjstr": "RSCacheShared_RSBufferPjstr",
    "rsbuf_pwrite": "RSCacheShared_RSBufferPwrite",
    "struct BitBuffer": "struct RSCacheShared_BitBuffer",
    "bitbuffer_init_from_rsbuf": "RSCacheShared_BitBufferInitFromRsbuf",
    "bitbuffer_bits": "RSCacheShared_BitBufferBits",
    "bitbuffer_gbits": "RSCacheShared_BitBufferGbits",
    "cache_gzip_decompress": "RSCacheShared_CompressionGzipDecompress",
    "cache_gzip_uncompressed_size": "RSCacheShared_CompressionGzipUncompressedSize",
    "cache_bzip_decompress": "RSCacheShared_CompressionBzipDecompress",
    "xtea_decrypt": "RSCacheShared_XteaDecrypt",
    "xtea_config_load_keys": "RSCacheShared_XteaConfigLoadKeys",
    "xtea_config_find_key": "RSCacheShared_XteaConfigFindKey",
    "xtea_config_cleanup_keys": "RSCacheShared_XteaConfigCleanupKeys",
    "str_ascii_toupper": "RSCacheShared_StrAsciiToupper",
    "strncpy_trimmed": "RSCacheShared_StrncpyTrimmed",
    # Disk
    "struct IndexRecord": "struct RSCacheDisk_IndexRecord",
    "disk_dat2file_read_archive": "RSCacheDisk_Dat2FileReadArchive",
    "disk_datfile_read_archive": "RSCacheDisk_DatFileReadArchive",
    "disk_dat2file_append_archive": "RSCacheDisk_Dat2FileAppendArchive",
    "disk_indexfile_read_record": "RSCacheDisk_IndexFileReadRecord",
    "disk_indexfile_write_record": "RSCacheDisk_IndexFileWriteRecord",
    # Dat2Disk
    "struct ReferenceTable": "struct RSCacheDat2Disk_ReferenceTable",
    "struct ArchiveReference": "struct RSCacheDat2Disk_ArchiveReference",
    "struct ArchiveFileReference": "struct RSCacheDat2Disk_ArchiveFileReference",
    "reference_table_new_decode": "RSCacheDat2Disk_ReferenceTableNewDecode",
    "reference_table_free": "RSCacheDat2Disk_ReferenceTableFree",
    "enum CacheTable": "enum RSCacheDat2Disk_Table",
    "CACHE_ANIMATIONS": "RSCacheDat2Disk_Table_Animations",
    "CACHE_SKELETONS": "RSCacheDat2Disk_Table_Skeletons",
    "CACHE_CONFIGS": "RSCacheDat2Disk_Table_Configs",
    "CACHE_INTERFACES": "RSCacheDat2Disk_Table_Interfaces",
    "CACHE_SOUNDEFFECTS": "RSCacheDat2Disk_Table_SoundEffects",
    "CACHE_MAPS": "RSCacheDat2Disk_Table_Maps",
    "CACHE_MUSIC_TRACKS": "RSCacheDat2Disk_Table_MusicTracks",
    "CACHE_MODELS": "RSCacheDat2Disk_Table_Models",
    "CACHE_SPRITES": "RSCacheDat2Disk_Table_Sprites",
    "CACHE_TEXTURES": "RSCacheDat2Disk_Table_Textures",
    "CACHE_BINARY": "RSCacheDat2Disk_Table_Binary",
    "CACHE_MUSIC_JINGLES": "RSCacheDat2Disk_Table_MusicJingles",
    "CACHE_CLIENTSCRIPT": "RSCacheDat2Disk_Table_Clientscript",
    "CACHE_FONTS": "RSCacheDat2Disk_Table_Fonts",
    "CACHE_MUSIC_SAMPLES": "RSCacheDat2Disk_Table_MusicSamples",
    "CACHE_MUSIC_PATCHES": "RSCacheDat2Disk_Table_MusicPatches",
    "CACHE_WORLDMAP_GEOGRAPHY": "RSCacheDat2Disk_Table_WorldmapGeography",
    "CACHE_WORLDMAP": "RSCacheDat2Disk_Table_Worldmap",
    "CACHE_WORLDMAP_GROUND": "RSCacheDat2Disk_Table_WorldmapGround",
    "CACHE_DBTABLEINDEX": "RSCacheDat2Disk_Table_DbtableIndex",
    "CACHE_ANIMAYAS": "RSCacheDat2Disk_Table_Animayas",
    "CACHE_GAMEVALS": "RSCacheDat2Disk_Table_Gamevals",
    "CACHE_TABLE_COUNT": "RSCacheDat2Disk_Table_Count",
    "enum CacheMode": "enum RSCacheDat2Disk_Mode",
    "CACHE_MODE_LOCAL_ONLY": "RSCacheDat2Disk_Mode_LocalOnly",
    "CACHE_MODE_INET": "RSCacheDat2Disk_Mode_Inet",
    "struct CacheArchive": "struct RSCacheDat2Disk_Archive",
    "struct Cache": "struct RSCacheDat2Disk",
    "cache_is_valid_table_id": "RSCacheDat2Disk_IsValidTableId",
    "cache_new_from_directory": "RSCacheDat2Disk_NewFromDirectory",
    "cache_new_inet": "RSCacheDat2Disk_NewInet",
    "cache_new_uninitialized": "RSCacheDat2Disk_NewUninitialized",
    "cache_free": "RSCacheDat2Disk_Free",
    "cache_archive_new_reference_table_load": "RSCacheDat2Disk_ArchiveNewReferenceTableLoad",
    "cache_archive_new_load_decrypted": "RSCacheDat2Disk_ArchiveNewLoadDecrypted",
    "cache_archive_new_load_uninitialized_metadata": "RSCacheDat2Disk_ArchiveNewLoadUninitializedMetadata",
    "cache_archive_new_load": "RSCacheDat2Disk_ArchiveNewLoad",
    "cache_archive_init_metadata": "RSCacheDat2Disk_ArchiveInitMetadata",
    "cache_archive_xtea_key": "RSCacheDat2Disk_ArchiveXteaKey",
    "cache_archive_free": "RSCacheDat2Disk_ArchiveFree",
    "struct CacheInetPayload": "struct RSCacheDat2Disk_InetPayload",
    "struct CacheInet": "struct RSCacheDat2Disk_Inet",
    "enum CacheInetRequestType": "enum RSCacheDat2Disk_InetRequestType",
    "cache_inet_new_connect": "RSCacheDat2Disk_InetNewConnect",
    "cache_inet_free": "RSCacheDat2Disk_InetFree",
    "cache_inet_payload_new_archive_request": "RSCacheDat2Disk_InetPayloadNewArchiveRequest",
    "cache_inet_payload_free": "RSCacheDat2Disk_InetPayloadFree",
    "indexeddb_get_archive": "RSCacheDat2Disk_InetIndexeddbGetArchive",
    "indexeddb_put_archive": "RSCacheDat2Disk_InetIndexeddbPutArchive",
    "indexeddb_init": "RSCacheDat2Disk_InetIndexeddbInit",
    # Dat1Disk
    "enum CacheDatTable": "enum RSCacheDat1Disk_Table",
    "CACHE_DAT_CONFIGS": "RSCacheDat1Disk_Table_Configs",
    "CACHE_DAT_MODELS": "RSCacheDat1Disk_Table_Models",
    "CACHE_DAT_ANIMATIONS": "RSCacheDat1Disk_Table_Animations",
    "CACHE_DAT_SOUNDS": "RSCacheDat1Disk_Table_Sounds",
    "CACHE_DAT_MAPS": "RSCacheDat1Disk_Table_Maps",
    "struct CacheDatArchive": "struct RSCacheDat1Disk_Archive",
    "struct CacheDat": "struct RSCacheDat1Disk",
    "struct CacheMapSquares": "struct RSCacheDat1A_MapSquares",
    "cache_dat_new_from_directory": "RSCacheDat1Disk_NewFromDirectory",
    "cache_dat_free": "RSCacheDat1Disk_Free",
    "cache_dat_archive_new_load": "RSCacheDat1Disk_ArchiveNewLoad",
    "cache_dat_archive_free": "RSCacheDat1Disk_ArchiveFree",
    # Dat2A
    "struct CacheModel": "struct RSCacheDat2A_Model",
    "enum CacheModelFlags": "enum RSCacheDat2A_ModelFlags",
    "enum LightingFlags": "enum RSCacheDat2A_LightingFlags",
    "enum FaceRenderKind": "enum RSCacheDat2A_FaceRenderKind",
    "struct ModelBones": "struct RSCacheDat2A_ModelBones",
    "model_new_from_cache": "RSCacheDat2A_ModelNewFromCache",
    "model_new_from_dat_archive": "RSCacheDat2A_ModelNewFromDatArchive",
    "model_new_decode": "RSCacheDat2A_ModelNewDecode",
    "modelbones_new_decode": "RSCacheDat2A_ModelBonesNewDecode",
    "modelbones_free": "RSCacheDat2A_ModelBonesFree",
    "model_new_from_archive": "RSCacheDat2A_ModelNewFromArchive",
    "model_new_copy": "RSCacheDat2A_ModelNewCopy",
    "model_new_merge": "RSCacheDat2A_ModelNewMerge",
    "model_free": "RSCacheDat2A_ModelFree",
    "struct CacheMapLoc": "struct RSCacheDat2A_MapLoc",
    "struct CacheMapLocs": "struct RSCacheDat2A_MapLocs",
    "enum FloorFlags": "enum RSCacheDat2A_FloorFlags",
    "enum FloorShape": "enum RSCacheDat2A_FloorShape",
    "struct CacheMapFloor": "struct RSCacheDat2A_MapFloor",
    "struct CacheMapTerrain": "struct RSCacheDat2A_MapTerrain",
    "struct ChunkOffset": "struct RSCacheDat2A_ChunkOffset",
    "map_terrain_new_from_cache": "RSCacheDat2A_MapTerrainNewFromCache",
    "map_terrain_new_from_cache_dat": "RSCacheDat2A_MapTerrainNewFromCacheDat",
    "map_terrain_new_decode": "RSCacheDat2A_MapTerrainNewDecode",
    "map_terrain_free": "RSCacheDat2A_MapTerrainFree",
    "map_locs_new_from_cache": "RSCacheDat2A_MapLocsNewFromCache",
    "map_locs_new_decode": "RSCacheDat2A_MapLocsNewDecode",
    "map_locs_free": "RSCacheDat2A_MapLocsFree",
    "map_tile_coord_to_chunk_coord": "RSCacheDat2A_MapTileCoordToChunkCoord",
    "map_floor_vis_below_draw_level": "RSCacheDat2A_MapFloorVisBelowDrawLevel",
    "map_terrain_archive_new_load": "RSCacheDat2A_MapTerrainArchiveNewLoad",
    "map_terrain_new_from_archive": "RSCacheDat2A_MapTerrainNewFromArchive",
    "map_terrain_new_from_decode_flags": "RSCacheDat2A_MapTerrainNewFromDecodeFlags",
    "map_locs_archive_new_load": "RSCacheDat2A_MapLocsArchiveNewLoad",
    "map_terrain_archive_new_load_from_cache_dat": "RSCacheDat2A_MapTerrainArchiveNewLoadFromCacheDat",
    "map_loc_archive_new_load_from_cache_dat": "RSCacheDat2A_MapLocArchiveNewLoadFromCacheDat",
    "struct CacheFrame": "struct RSCacheDat2A_Frame",
    "frame_new_from_cache": "RSCacheDat2A_FrameNewFromCache",
    "frame_new_decode2": "RSCacheDat2A_FrameNewDecode2",
    "frame_framemap_id_from_file": "RSCacheDat2A_FrameFramemapIdFromFile",
    "frame_free": "RSCacheDat2A_FrameFree",
    "struct CacheFramemap": "struct RSCacheDat2A_Framemap",
    "framemap_new_from_cache": "RSCacheDat2A_FramemapNewFromCache",
    "framemap_id_from_frame_archive": "RSCacheDat2A_FramemapIdFromFrameArchive",
    "framemap_new_decode2": "RSCacheDat2A_FramemapNewDecode2",
    "framemap_free": "RSCacheDat2A_FramemapFree",
    "struct CacheSprite": "struct RSCacheDat2A_Sprite",
    "struct CacheSpritePack": "struct RSCacheDat2A_SpritePack",
    "enum SpiteLoaderFlags": "enum RSCacheDat2A_SpriteLoaderFlags",
    "sprite_pack_new_from_cache": "RSCacheDat2A_SpritePackNewFromCache",
    "sprite_pack_new_decode": "RSCacheDat2A_SpritePackNewDecode",
    "sprite_pack_free": "RSCacheDat2A_SpritePackFree",
    "sprite_get_pixels": "RSCacheDat2A_SpriteGetPixels",
    "sprite_texture_get_pixels": "RSCacheDat2A_SpriteTextureGetPixels",
    "enum TextureDirection": "enum RSCacheDat2A_TextureDirection",
    "TEXTURE_U_DIRECTION": "RSCacheDat2A_TextureUDirection",
    "TEXTURE_V_DIRECTION": "RSCacheDat2A_TextureVDirection",
    "struct CacheTexture": "struct RSCacheDat2A_Texture",
    "texture_definition_new_from_cache": "RSCacheDat2A_TextureDefinitionNewFromCache",
    "texture_definition_free": "RSCacheDat2A_TextureDefinitionFree",
    "texture_definition_free_inplace": "RSCacheDat2A_TextureDefinitionFreeInplace",
    "texture_definition_new_decode": "RSCacheDat2A_TextureDefinitionNewDecode",
    "texture_definition_decode_inplace": "RSCacheDat2A_TextureDefinitionDecodeInplace",
    "struct CacheConfigUnderlay": "struct RSCacheDat2A_ConfigUnderlay",
    "struct CacheConfigOverlay": "struct RSCacheDat2A_ConfigOverlay",
    "config_underlay_new_decode": "RSCacheDat2A_ConfigUnderlayNewDecode",
    "config_underlay_free": "RSCacheDat2A_ConfigUnderlayFree",
    "config_overlay_new_decode": "RSCacheDat2A_ConfigOverlayNewDecode",
    "config_overlay_free": "RSCacheDat2A_ConfigOverlayFree",
    "struct CacheConfigLocation": "struct RSCacheDat2A_ConfigLocation",
    "enum LocShape": "enum RSCacheDat2A_LocShape",
    "enum LocParamType": "enum RSCacheDat2A_LocParamType",
    "config_location_new_decode": "RSCacheDat2A_ConfigLocationNewDecode",
    "config_location_free": "RSCacheDat2A_ConfigLocationFree",
    "config_location_print": "RSCacheDat2A_ConfigLocationPrint",
    "struct CacheConfigObject": "struct RSCacheDat2A_ConfigObject",
    "config_object_new_decode": "RSCacheDat2A_ConfigObjectNewDecode",
    "config_object_free": "RSCacheDat2A_ConfigObjectFree",
    "config_object_decode_inplace": "RSCacheDat2A_ConfigObjectDecodeInplace",
    "config_idk_decode_inplace": "RSCacheDat2A_ConfigIdkDecodeInplace",
    "config_sequence_free_inplace": "RSCacheDat2A_ConfigSequenceFreeInplace",
    "config_sequence_decode_inplace": "RSCacheDat2A_ConfigSequenceDecodeInplace",
    "config_dat_sequence_decode_inplace": "RSCacheDat1A_ConfigSequenceDecodeInplace",
    "struct CacheConfigNPCType": "struct RSCacheDat2A_ConfigNpctype",
    "config_npctype_new_decode": "RSCacheDat2A_ConfigNpctypeNewDecode",
    "config_npctype_free": "RSCacheDat2A_ConfigNpctypeFree",
    "print_npc_type": "RSCacheDat2A_ConfigNpctypePrint",
    "free_npc_type": "RSCacheDat2A_ConfigNpctypeFreeLegacy",
    "struct CacheConfigIdk": "struct RSCacheDat2A_ConfigIdk",
    "config_idk_new_decode": "RSCacheDat2A_ConfigIdkNewDecode",
    "config_idk_free": "RSCacheDat2A_ConfigIdkFree",
    "struct CacheConfigFrameSound": "struct RSCacheDat2A_ConfigFrameSound",
    "struct CacheConfigFrameSoundMap": "struct RSCacheDat2A_ConfigFrameSoundMap",
    "struct CacheConfigSequence": "struct RSCacheDat2A_ConfigSequence",
    "config_sequence_new_decode": "RSCacheDat2A_ConfigSequenceNewDecode",
    "config_sequence_free": "RSCacheDat2A_ConfigSequenceFree",
    "enum ConfigKind": "enum RSCacheDat2A_ConfigKind",
    "CONFIG_UNDERLAY": "RSCacheDat2A_ConfigKind_Underlay",
    "CONFIG_OVERLAY": "RSCacheDat2A_ConfigKind_Overlay",
    "CONFIG_LOCS": "RSCacheDat2A_ConfigKind_Locs",
    "CONFIG_OBJECT": "RSCacheDat2A_ConfigKind_Object",
    "CONFIG_VARBIT": "RSCacheDat2A_ConfigKind_Varbit",
    "CONFIG_VARP": "RSCacheDat2A_ConfigKind_Varp",
    "CONFIG_PARAMS": "RSCacheDat2A_ConfigKind_Params",
    "CONFIG_STRUCTS": "RSCacheDat2A_ConfigKind_Structs",
    "CONFIG_ENUMS": "RSCacheDat2A_ConfigKind_Enums",
    "CONFIG_IDENTKIT": "RSCacheDat2A_ConfigKind_Identkit",
    "CONFIG_INV": "RSCacheDat2A_ConfigKind_Inv",
    "CONFIG_ENUM": "RSCacheDat2A_ConfigKind_Enum",
    "CONFIG_NPC": "RSCacheDat2A_ConfigKind_Npc",
    "CONFIG_SEQUENCE": "RSCacheDat2A_ConfigKind_Sequence",
    "CONFIG_SPOTANIM": "RSCacheDat2A_ConfigKind_Spotanim",
    "CONFIG_VARBIT": "RSCacheDat2A_ConfigKind_Varbit",
    "CONFIG_VARCLIENT": "RSCacheDat2A_ConfigKind_Varclient",
    "CONFIG_VARCLIENTSTRING": "RSCacheDat2A_ConfigKind_VarclientString",
    "CONFIG_VARPLAYER": "RSCacheDat2A_ConfigKind_Varplayer",
    "CONFIG_HITSPLAT": "RSCacheDat2A_ConfigKind_Hitsplat",
    "CONFIG_HEALTHBAR": "RSCacheDat2A_ConfigKind_Healthbar",
    "CONFIG_STRUCT": "RSCacheDat2A_ConfigKind_Struct",
    "CONFIG_AREA": "RSCacheDat2A_ConfigKind_Area",
    "CONFIG_DBROW": "RSCacheDat2A_ConfigKind_Dbrow",
    "CONFIG_DBTABLE": "RSCacheDat2A_ConfigKind_Dbtable",
    "perlin_noise": "RSCacheDat2A_NoisePerlinNoise",
    "g_cos_table": "RSCacheDat2A_NoiseCosTable",
    "component_new_decode": "RSCacheDat2A_ComponentNewDecode",
    "component_free": "RSCacheDat2A_ComponentFree",
    "component_print": "RSCacheDat2A_ComponentPrint",
    "component_load_all": "RSCacheDat2A_ComponentLoadAll",
    "component_load_one": "RSCacheDat2A_ComponentLoadOne",
    "component_load_from_archive": "RSCacheDat2A_ComponentLoadFromArchive",
    "component_load_from_data": "RSCacheDat2A_ComponentLoadFromData",
    # Dat1A
    "struct CacheDatSequence": "struct RSCacheDat1A_ConfigSequence",
    "config_dat_sequence_new_decode": "RSCacheDat1A_ConfigSequenceNewDecode",
    "config_dat_sequence_free": "RSCacheDat1A_ConfigSequenceFree",
    "config_dat_sequence_get_frame_count": "RSCacheDat1A_ConfigSequenceGetFrameCount",
    "config_dat_sequence_get_frame_id": "RSCacheDat1A_ConfigSequenceGetFrameId",
    "struct CacheDatConfigObj": "struct RSCacheDat1A_ConfigObj",
    "struct CacheDatConfigObjList": "struct RSCacheDat1A_ConfigObjList",
    "struct CacheDatConfigNpc": "struct RSCacheDat1A_ConfigNpc",
    "struct CacheDatConfigNpcList": "struct RSCacheDat1A_ConfigNpcList",
    "struct CacheDatConfigIdk": "struct RSCacheDat1A_ConfigIdk",
    "struct CacheDatConfigIdkList": "struct RSCacheDat1A_ConfigIdkList",
    "struct CacheDatConfigVarp": "struct RSCacheDat1A_ConfigVarp",
    "struct CacheDatConfigVarpList": "struct RSCacheDat1A_ConfigVarpList",
    "struct CacheDatConfigSpotAnim": "struct RSCacheDat1A_ConfigSpotanim",
    "enum CacheDatConfigComponentType": "enum RSCacheDat1A_ConfigComponentType",
    "enum CacheDatConfigComponentButtonType": "enum RSCacheDat1A_ConfigComponentButtonType",
    "struct CacheDatConfigComponent": "struct RSCacheDat1A_ConfigComponent",
    "struct CacheDatConfigComponentList": "struct RSCacheDat1A_ConfigComponentList",
    "struct CacheDatTexture": "struct RSCacheDat1A_ConfigTexture",
    "struct CacheMapSquare": "struct RSCacheDat1A_MapSquare",
    "struct MapSquareCoord": "struct RSCacheDat1A_MapSquareCoord",
    "cache_map_squares_new_decode": "RSCacheDat1A_MapSquaresNewDecode",
    "cache_map_squares_free": "RSCacheDat1A_MapSquaresFree",
    "cache_map_square_id": "RSCacheDat1A_MapSquareId",
    "struct CacheAnimBase": "struct RSCacheDat1A_AnimBase",
    "struct CacheAnimframe": "struct RSCacheDat1A_AnimFrame",
    "struct CacheDatAnimBaseFrames": "struct RSCacheDat1A_AnimBaseFrames",
    "struct CacheDatPix8Palette": "struct RSCacheDat1A_Pix8Palette",
    "struct CacheDatPix32": "struct RSCacheDat1A_Pix32",
    "struct CacheDatPixfont": "struct RSCacheDat1A_PixFont",
    "cache_dat_config_obj_list_new_decode": "RSCacheDat1A_ConfigObjListNewDecode",
    "cache_dat_config_obj_decode_one": "RSCacheDat1A_ConfigObjDecodeOne",
    "cache_dat_config_obj_free": "RSCacheDat1A_ConfigObjFree",
    "cache_dat_config_npc_list_new_decode": "RSCacheDat1A_ConfigNpcListNewDecode",
    "cache_dat_config_npc_decode_one": "RSCacheDat1A_ConfigNpcDecodeOne",
    "cache_dat_config_npc_free": "RSCacheDat1A_ConfigNpcFree",
    "cache_dat_config_idk_list_new_decode": "RSCacheDat1A_ConfigIdkListNewDecode",
    "cache_dat_config_idk_free": "RSCacheDat1A_ConfigIdkFree",
    "cache_dat_config_varp_list_new_decode": "RSCacheDat1A_ConfigVarpListNewDecode",
    "cache_dat_config_varp_list_free": "RSCacheDat1A_ConfigVarpListFree",
    "cache_dat_config_spotanim_decode_one": "RSCacheDat1A_ConfigSpotanimDecodeOne",
    "cache_dat_config_spotanim_free": "RSCacheDat1A_ConfigSpotanimFree",
    "cache_dat_config_component_list_new_decode": "RSCacheDat1A_ConfigComponentListNewDecode",
    "cache_dat_config_component_free": "RSCacheDat1A_ConfigComponentFree",
    "cache_dat_texture_new_from_filelist_dat": "RSCacheDat1A_ConfigTextureNewFromFilelistDat",
    "cache_dat_texture_free": "RSCacheDat1A_ConfigTextureFree",
    "cache_map_square_coord": "RSCacheDat1A_MapSquareCoord",
    "cache_dat_animbaseframes_new_decode": "RSCacheDat1A_AnimBaseFramesNewDecode",
    "cache_dat_animbase_new_decode": "RSCacheDat1A_AnimBaseNewDecode",
    "cache_dat_animframe_free": "RSCacheDat1A_AnimFrameFree",
    "cache_dat_animframe_free_inplace": "RSCacheDat1A_AnimFrameFreeInplace",
    "cache_dat_animbaseframes_free": "RSCacheDat1A_AnimBaseFramesFree",
    "cache_dat_pix8_palette_new": "RSCacheDat1A_Pix8PaletteNew",
    "cache_dat_pix8_palette_free": "RSCacheDat1A_Pix8PaletteFree",
    "cache_dat_pix32_new": "RSCacheDat1A_Pix32New",
    "cache_dat_pix32_free": "RSCacheDat1A_Pix32Free",
    "cache_dat_pixfont_new_decode": "RSCacheDat1A_PixFontNewDecode",
    "cache_dat_pixfont_free": "RSCacheDat1A_PixFontFree",
    "pixfont_draw_text": "RSCacheDat1A_PixFontDrawText",
    "CONFIG_DAT_CHAT_SYSTEM": "RSCacheDat1A_ConfigKind_ChatSystem",
    "enum ConfigDatKind": "enum RSCacheDat1A_ConfigKind",
    "CONFIG_DAT_TITLE_AND_FONTS": "RSCacheDat1A_ConfigKind_TitleAndFonts",
    "CONFIG_DAT_CONFIGS": "RSCacheDat1A_ConfigKind_Configs",
    "CONFIG_DAT_TEXTURES": "RSCacheDat1A_ConfigKind_Textures",
    "CONFIG_DAT_VERSION_LIST": "RSCacheDat1A_ConfigKind_VersionList",
    "CONFIG_DAT_INTERFACES": "RSCacheDat1A_ConfigKind_Interfaces",
    "CONFIG_DAT_MEDIA_2D_GRAPHICS": "RSCacheDat1A_ConfigKind_Media2dGraphics",
    "CONFIG_DAT_SOUND_EFFECTS": "RSCacheDat1A_ConfigKind_SoundEffects",
}

# Header guard renames
GUARD_RENAMES = {
    "DISK_H": "RSCACHE_DISK_H",
    "OSRS_CACHE_H_": "RSCACHE_DAT2DISK_H",
    "CACHE_DAT_H": "RSCACHE_DAT1DISK_H",
    "ARCHIVE_H": "RSCACHE_SHARED_ARCHIVE_H",
    "ARCHIVE_DECOMPRESS_H": "RSCACHE_SHARED_ARCHIVE_DECOMPRESS_H",
    "REFERENCE_TABLE_H": "RSCACHE_DAT2DISK_REFERENCE_TABLE_H",
    "COMPRESSION_H": "RSCACHE_SHARED_COMPRESSION_H",
    "FILELIST_H": "RSCACHE_SHARED_FILELIST_H",
    "RSBUF_H": "RSCACHE_SHARED_RSBUF_H",
    "BITBUFFER_H": "RSCACHE_SHARED_BITBUFFER_H",
    "XTEA_H": "RSCACHE_SHARED_XTEA_H",
    "XTEA_CONFIG_H": "RSCACHE_SHARED_XTEA_CONFIG_H",
}


def move_files():
    """Move files to new regime subdirectories."""
    for old, new in FILE_MOVES.items():
        src = RSCACHE / old
        dst = RSCACHE / new
        if not src.exists():
            print(f"SKIP (missing): {old}")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        print(f"MOVED: {old} -> {new}")


def apply_renames_to_text(text):
    """Apply include path and symbol renames to text."""
    # Include path renames - longest paths first
    for old_path, new_path in sorted(INCLUDE_PATH_MAP.items(), key=lambda x: -len(x[0])):
        text = text.replace(f'"{old_path}"', f'"{new_path}"')
        text = text.replace(f"'{old_path}'", f"'{new_path}'")

    # Guard renames
    for old, new in GUARD_RENAMES.items():
        text = text.replace(old, new)

    # Symbol renames - longest first to avoid partial matches
    for old, new in sorted(SYMBOL_RENAMES.items(), key=lambda x: -len(x[0])):
        text = text.replace(old, new)

    return text


def rename_symbols_in_repo():
    """Apply symbol and include renames across the entire repo."""
    skip_dirs = {".git", "build", "build_release", "build.em", "build_sse1", "node_modules", "scripts", ".cursor"}
    extensions = {".c", ".h", ".cpp", ".hpp", ".u.c", ".md", "CMakeLists.txt", "Makefile"}

    for root, dirs, files in os.walk(REPO):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for fname in files:
            fpath = Path(root) / fname
            if fpath.suffix not in extensions and fname not in ("CMakeLists.txt", "Makefile"):
                continue
            try:
                content = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception:
                continue
            new_content = apply_renames_to_text(content)
            if new_content != content:
                fpath.write_text(new_content, encoding="utf-8")
                print(f"UPDATED: {fpath.relative_to(REPO)}")


def fix_internal_includes():
    """Fix includes within rscache files to use relative paths."""
    regime_dirs = ["shared", "disk", "dat2disk", "dat1disk", "dat2a", "dat1a"]
    for regime in regime_dirs:
        regime_path = RSCACHE / regime
        if not regime_path.exists():
            continue
        for fpath in regime_path.iterdir():
            if fpath.suffix not in (".c", ".h"):
                continue
            content = fpath.read_text(encoding="utf-8")
            # Replace osrs/rscache/... includes with relative paths where possible
            for old, new in FILE_MOVES.items():
                if not old.endswith(".h"):
                    continue
                new_basename = os.path.basename(new)
                new_regime = new.split("/")[0]
                target = f"osrs/rscache/{new}"
                if new_regime == regime:
                    rel = f'"{new_basename}"'
                else:
                    rel = f'"../{new_regime}/{new_basename}"'
                content = content.replace(f'"{target}"', rel)
            fpath.write_text(content, encoding="utf-8")


def remove_stale_hmap():
    """Remove stale hmap.h include from textures."""
    tex = RSCACHE / "dat2a/RSCacheDat2A_Textures.c"
    if tex.exists():
        content = tex.read_text(encoding="utf-8")
        content = re.sub(r'#include\s+"\.\./datastruct/hmap\.h"\s*\n', "", content)
        tex.write_text(content, encoding="utf-8")


def cleanup_old_dirs():
    """Remove empty old directories."""
    for d in ["tables", "tables_dat"]:
        p = RSCACHE / d
        if p.exists():
            # Remove any remaining files (java files etc)
            for f in p.iterdir():
                if f.suffix == ".java":
                    # Move java files to dat2a
                    shutil.move(str(f), str(RSCACHE / "dat2a" / f.name))
            if not any(p.iterdir()):
                p.rmdir()
                print(f"REMOVED empty dir: {d}")


def main():
    import sys
    step = sys.argv[1] if len(sys.argv) > 1 else "all"
    if step in ("move", "all"):
        move_files()
    if step in ("rename", "all"):
        rename_symbols_in_repo()
    if step in ("fix", "all"):
        fix_internal_includes()
        remove_stale_hmap()
        cleanup_old_dirs()


if __name__ == "__main__":
    main()
