local BuildCache = require("buildcache")

local function print_table(t, label)
    label = label or "table"
    local parts = {}
    for i, v in ipairs(t) do
        table.insert(parts, tostring(v))
    end
    print(string.format("%s (%d): [%s]", label, #t, table.concat(parts, ", ")))
end

local function world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)
    local map_sw_x = math.floor(wx_sw / 64)
    local map_sw_z = math.floor(wz_sw / 64)
    local map_ne_x = math.floor(wx_ne / 64)
    local map_ne_z = math.floor(wz_ne / 64)
    local chunks = {}
    for x = map_sw_x, map_ne_x do
        for z = map_sw_z, map_ne_z do
            table.insert(chunks, { x = x, z = z })
        end
    end
    return chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z
end

function load_scene_dat(wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z)
    local chunks, msw_x, msw_z, mne_x, mne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    print(string.format("Loading scene dat for world coords SW(%d, %d) NE(%d, %d)", wx_sw, wz_sw, wx_ne, wz_ne))

    print("=== Step 0: Load Configs & VersionList ===")
    local config_ptr = BuildCache.load_archive(BuildCache.Tables.CACHE_DAT_CONFIGS, BuildCache.ConfigDatKind.CONFIG_DAT_CONFIGS)
    BuildCache.store_container_jagfile("config_jagfile", config_ptr)
    
    local vlist_ptr = BuildCache.load_archive(BuildCache.Tables.CACHE_DAT_CONFIGS, BuildCache.ConfigDatKind.CONFIG_DAT_VERSION_LIST)
    BuildCache.store_container_jagfile("versionlist_jagfile", vlist_ptr)

    print("=== Step 1: Load Terrain (Batched) ===")
    local terrain_ids = {}
    for _, chunk in ipairs(chunks) do
        local map_id = (chunk.x << 16) | chunk.z
        if not BuildCache.has_datatype(BuildCache.DataTypes.MapTerrain, map_id) then
            table.insert(terrain_ids, map_id)
        end
    end
    if #terrain_ids > 0 then
        print(string.format("Loading %d terrain chunks...", #terrain_ids))
        print_table(terrain_ids, "terrain_ids")
        local t_ptrs = BuildCache.load_archives(BuildCache.Tables.CACHE_DAT_MAPS, terrain_ids)
        for i, ptr in ipairs(t_ptrs) do
            BuildCache.store_datatype_from_raw(terrain_ids[i], ptr, BuildCache.DataTypes.MapTerrain)
        end
    end

    print("=== Step 2: Load Floor Types ===")
    -- Initialize floor types from the loaded config container
    BuildCache.store_container_jagfilepack_from_jagfile("config_flotypes", "config_jagfile", "flo.dat")

    print("=== Step 3: Load Scenery (Batched) ===")
    local scenery_ids = {}
    for _, chunk in ipairs(chunks) do
        local map_id = (chunk.x << 16) | chunk.z
        if not BuildCache.has_datatype(BuildCache.DataTypes.MapScenery, map_id) then
            table.insert(scenery_ids, map_id + 1) -- Archive ID offset
        end
    end
    if #scenery_ids > 0 then
        local s_ptrs = BuildCache.load_archives(BuildCache.Tables.CACHE_DAT_MAPS, scenery_ids)
        for i, ptr in ipairs(s_ptrs) do
            BuildCache.store_datatype_from_raw(scenery_ids[i] - 1, ptr, BuildCache.DataTypes.MapScenery)
        end
    end

    print("=== Step 4: Load Scenery Configs ===")
    -- Tells C++ to parse loc.dat/idx inside the config_jagfile
    BuildCache.store_container_jagfilepack_indexed_from_jagfile("config_loctypes", "config_jagfile", "loc.dat", "loc.idx")
    BuildCache.store_datatype_from_container_all("config_loctypes", BuildCache.DataTypes.ConfigMapScenery) 

    print("=== Step 5: Queue and Load Models (Batched) ===")
    local all_model_ids = BuildCache.list_datatypes_field(BuildCache.DataTypes.MapScenery, "model_id")
    print_table(all_model_ids, "all_model_ids")
    local missing_model_ids = {}
    for _, mid in ipairs(all_model_ids) do
        if mid > 0 and not BuildCache.has_datatype(BuildCache.DataTypes.Model, mid) then
            table.insert(missing_model_ids, mid)
        end
    end
    if #missing_model_ids > 0 then
        print(string.format("Fetching %d missing models...", #missing_model_ids))
        local m_ptrs = BuildCache.load_archives(BuildCache.Tables.CACHE_DAT_MODELS, missing_model_ids)
        for i, ptr in ipairs(m_ptrs) do
            BuildCache.store_datatype_from_raw(missing_model_ids[i], ptr, BuildCache.DataTypes.Model)
        end
    end

    print("=== Step 6: Load Textures ===")
    local tex_ptr = BuildCache.load_archive(BuildCache.Tables.CACHE_DAT_CONFIGS, BuildCache.ConfigDatKind.CONFIG_DAT_TEXTURES)
    BuildCache.store_container_jagfile("texture_jagfile", tex_ptr)
    for i = 0, 49 do
        BuildCache.store_datatype_from_container_with_name(
            tostring(i)..".dat", "texture_jagfile", BuildCache.DataTypes.TextureDefinitions)
    end

    print("=== Step 7: Load Sequences ===")
    -- Assuming sequences are handled as a specific datatype within configs
    BuildCache.store_datatype_from_container_all("config_jagfile", BuildCache.DataTypes.FrameBlob) 

    print("=== Step 8: Load Animation Frame Index ===")
    -- This step is usually implicit once version_list is stored as a container, 
    -- but we can trigger a list refresh here.
    local anim_ids = BuildCache.list_datatypes_ids(BuildCache.DataTypes.FrameBlob)

    print("=== Step 9: Load Animation Base Frames (Batched) ===")
    local missing_anim_ids = {}
    for _, aid in ipairs(anim_ids) do
        if not BuildCache.has_datatype(BuildCache.DataTypes.FrameBlob, aid) then
            table.insert(missing_anim_ids, aid)
        end
    end
    if #missing_anim_ids > 0 then
        local a_ptrs = BuildCache.load_archives(BuildCache.Tables.CACHE_DAT_ANIMATIONS, missing_anim_ids)
        for i, ptr in ipairs(a_ptrs) do
            BuildCache.store_datatype_from_raw(missing_anim_ids[i], ptr, BuildCache.DataTypes.FrameBlob)
        end
    end

    print("=== Step 11: Load Media ===")
    local media_ptr = BuildCache.load_archive(BuildCache.Tables.CACHE_DAT_CONFIGS, BuildCache.ConfigDatKind.CONFIG_DAT_MEDIA_2D_GRAPHICS)
    BuildCache.store_container_jagfile("media_jagfile", media_ptr)
    BuildCache.store_datatype_from_container1(0, "media_jagfile", BuildCache.DataTypes.Spritepack)
 

    print("=== Step 12: Load Title/Fonts ===")
    local title_ptr = BuildCache.load_archive(BuildCache.Tables.CACHE_DAT_CONFIGS, BuildCache.ConfigDatKind.CONFIG_DAT_TITLE_AND_FONTS)
    BuildCache.store_container_jagfile("title_jagfile", title_ptr)

    print("=== Step 13: Load IDKits & Objects ===")
    -- Initialize remaining config types
    BuildCache.store_container_jagfilepack_from_jagfile("idktypes", "config_jagfile", "idk.dat") -- Frame maps
    -- Assuming Object definitions are mapped to a specific DataType
    BuildCache.store_datatype_from_container_all("idktypes", BuildCache.DataTypes.ConfigIdKit)

    BuildCache.store_container_jagfilepack_indexed_from_jagfile("objtypes", "config_jagfile", "obj.dat", "obj.idx") 
    BuildCache.store_datatype_from_container_all("objtypes", BuildCache.DataTypes.ConfigObject)

    print("=== Scene Loading Complete ===")
    coroutine.yield(99, msw_x, msw_z, mne_x, mne_z)
end


local msw_x, msw_z, mne_x, mne_z = ...
load_scene_dat(msw_x, msw_z, mne_x, mne_z)