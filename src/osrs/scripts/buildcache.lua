local M = {}

M.Tables = {
    CACHE_DAT_CONFIGS = 0,
    CACHE_DAT_MODELS = 1,
    CACHE_DAT_ANIMATIONS = 2,
    CACHE_DAT_SOUNDS = 3,
    CACHE_DAT_MAPS = 4,
}

M.ConfigDatKind = {
    CONFIG_DAT_TITLE_AND_FONTS = 1,
    CONFIG_DAT_CONFIGS = 2,
    CONFIG_DAT_INTERFACES = 3,
    CONFIG_DAT_MEDIA_2D_GRAPHICS = 4,
    CONFIG_DAT_VERSION_LIST = 5,
    CONFIG_DAT_TEXTURES = 6,
    CONFIG_DAT_CHAT_SYSTEM = 7,
    CONFIG_DAT_SOUND_EFFECTS = 8,
}

M.DataTypes = {
    Model = 1,
    MapTerrain = 2,
    MapScenery = 3,
    ConfigMapScenery = 4,
    ConfigUnderlay = 5,
    ConfigOverlay = 6,
    ConfigObject = 7,
    ConfigIdKit = 8,
    ConfigSequence = 9,
    TextureDefinitions = 10,
    Spritepack = 11,
    FrameBlob = 12,
    Framemap = 13,
}

M.ContainerTypes = {
    -- e.g. constructed from "seq.dat" (no "seq.idx")
    JagFilePack = 1,
    -- e.g. constructed from "loc.dat" and "loc.idx" in the CONFIG_DAT_CONFIGS archive.
    JagFilePackIndexed = 2,
    -- e.g. 
    JagFile = 3,
}


-- // BuildCacheInterface
-- // load archive(table id, archive id)
-- // store_container_namedarchive("name", archive_data_ptr)
-- // store_container_jagfile("name", archive_data_ptr, "jagfile.dat")
-- // store_container_indexedjagfile("name", archive_data_ptr, "jagfile.dat", "jagfile.idx")
-- // store_container_jagfile_from_archivename("name", "archivename", "jagfile.dat")
-- // store_container_indexedjagfile_from_archivename("name", "archivename", "jagfile.dat",
-- // "jagfile.idx") store_datatype_from_raw(id, datatype_data_ptr, datatype_type)
-- // store_datatype_from_container1(id, "container_name",  datatype_type)
-- // store_datatype_from_container2(id, "container_name",  datatype_type, ids)
-- // has_datatype(datatype_type, id)
-- // has_container(container_type, "container_name")
-- // list_datatypes_ids(datatype_type)
-- // list_datatypes_field(datatype_type, "fieldname")
-- //

-- --- Function ID Mapping ---
M.Func = {
    LOAD_ARCHIVE = 1,
    STORE_CONTAINER_JAGFILE = 2,
    STORE_CONTAINER_JAGFILEPACK = 4,
    STORE_CONTAINER_JAGFILEPACK_INDEXED = 5,
    STORE_CONTAINER_JAGFILEPACK_FROM_JAGFILE = 6,
    STORE_CONTAINER_JAGFILEPACK_INDEXED_FROM_JAGFILE = 7,
    STORE_DATA_RAW = 8,
    STORE_DATA_FROM_CONTAINER_WITH_ID = 9,
    STORE_DATA_FROM_CONTAINER_WITH_IDS = 10,
    STORE_DATA_FROM_CONTAINER_WITH_NAME = 11,
    STORE_DATA_FROM_CONTAINER_ALL = 12,
    HAS_DATA = 13,
    HAS_CONT = 14,
    LIST_IDS = 15,
    LIST_FIELD = 16,
    LOAD_ARCHIVES = 17,
}

function M.load_archive(table_id, archive_id)
    return coroutine.yield(M.Func.LOAD_ARCHIVE, table_id, archive_id)
end

function M.load_archives(table_id, archive_ids)
    return coroutine.yield(M.Func.LOAD_ARCHIVES, table_id, archive_ids)
end

function M.store_container_jagfile(name, ptr)
    return coroutine.yield(M.Func.STORE_CONTAINER_JAGFILE, name, ptr)
end

function M.store_container_jagfilepack(name, ptr)
    return coroutine.yield(M.Func.STORE_CONTAINER_JAGFILEPACK, name, ptr)
end

function M.store_container_jagfilepack_indexed(name, ptr)
    return coroutine.yield(M.Func.STORE_CONTAINER_JAGFILEPACK_INDEXED, name, ptr)
end

function M.store_container_jagfilepack_from_jagfile(name, arc, datname)
    return coroutine.yield(M.Func.STORE_CONTAINER_JAGFILEPACK_FROM_JAGFILE, name, arc, datname)
end

function M.store_container_jagfilepack_indexed_from_jagfile(name, arc, datname, idxname)
    return coroutine.yield(M.Func.STORE_CONTAINER_JAGFILEPACK_INDEXED_FROM_JAGFILE, name, arc, datname, idxname)
end

function M.store_datatype_from_raw(id, ptr, dtype)
    return coroutine.yield(M.Func.STORE_DATA_RAW, id, ptr, dtype)
end

function M.store_datatype_with_name(name, cname, dtype)
    return coroutine.yield(M.Func.STORE_DATA_FROM_CONTAINER_WITH_NAME, name, cname, dtype)
end

function M.store_datatype_with_id(id, cname, dtype)
    return coroutine.yield(M.Func.STORE_DATA_FROM_CONTAINER_WITH_ID, id, cname, dtype)
end

function M.store_datatype_with_ids(ids, cname, dtype)
    return coroutine.yield(M.Func.STORE_DATA_FROM_CONTAINER_WITH_IDS, ids, cname, dtype)
end

function M.store_datatype_from_container_all(cname, dtype)
    return coroutine.yield(M.Func.STORE_DATA_FROM_CONTAINER_ALL, cname, dtype)
end

function M.has_datatype(dtype, id)
    return coroutine.yield(M.Func.HAS_DATA, dtype, id)
end

function M.has_container(ctype, name)
    return coroutine.yield(M.Func.HAS_CONT, ctype, name)
end

function M.list_datatypes_ids(dtype)
    return coroutine.yield(M.Func.LIST_IDS, dtype)
end

function M.list_datatypes_field(dtype, field)
    return coroutine.yield(M.Func.LIST_FIELD, dtype, field)
end

return M
