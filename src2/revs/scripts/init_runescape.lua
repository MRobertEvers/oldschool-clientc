print("Initializing RuneScape world (Lua)")

Game.Runescape_Init()

local io_queue = Platform.GetIOQueue()

if Game.GetCacheMode() == "dat2" then
    if not Game.Dat2_TexturesLoad() then
        print("Dat2 textures load failed")
        return
    end
    print("Dat2 textures loaded")
    Game.Dat2_SubmitTextures()
    print("Dat2 textures submitted")
else
    Game.Dat1_ConfigFileFetch(io_queue)
    Platform.LoadIO(io_queue)
    if not Game.Dat1_ConfigFileLoad(io_queue) then
        print("Config file load failed")
        return
    end
    print("Config file loaded")

    Game.Dat1_TexturesFetch(io_queue)
    Platform.LoadIO(io_queue)
    if not Game.Dat1_TexturesLoad(io_queue) then
        print("Textures load failed")
        return
    end
    print("Textures loaded")

    Game.Dat1_SubmitTextures()
    print("Textures submitted")
end

while not Game.RunTasks() do
    Platform.LoadIO(io_queue)
end

if Game.GetCacheMode() == "dat2" then
    print("Building world chunk list")
    Game.Runescape_BuildWorldChunkList({ 35, 83 }, 1)
else
    Game.Runescape_BuildWorldCenterzone(313, 437, 104)
end
print("World built")
