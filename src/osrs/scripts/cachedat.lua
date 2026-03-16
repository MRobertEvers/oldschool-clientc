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

-- --- Function ID Mapping ---
M.Func = {
    LOAD_ARCHIVE = 1,
    LOAD_ARCHIVES = 17,
}

M.ArchiveIdFlags = {
    MAP_TERRAIN = 1,
    MAP_SCENERY = 2,
}

function M.load_archive(table_id, archive_id, flags)
    return coroutine.yield(M.Func.LOAD_ARCHIVE, table_id, archive_id, flags or 0)
end

function M.load_archives(table_id, archive_ids, flags)
    return coroutine.yield(M.Func.LOAD_ARCHIVES, table_id, #archive_ids, archive_ids, flags or 0)
end

return M
