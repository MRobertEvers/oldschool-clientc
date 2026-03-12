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
    ConfigUnderlay = 4,
    ConfigOverlay = 5,
    TextureDefinitions = 6,
    Spritepack = 7,
    FrameBlob = 8,
    Framemap = 9,
}

M.ContainerTypes = {
    -- e.g. constructed from "seq.dat" (no "seq.idx")
    JagFile_Legacy = 1,
    -- e.g. constructed from "loc.dat" and "loc.idx" in the CONFIG_DAT_CONFIGS archive.
    JagFile_Indexed = 2,
    -- e.g. OSRS TODO:
    JagFile = 3,
    -- e.g. the Config Archive contains "media.dat".
    NamedFileArchive = 4
}

return M