print("Initializing RuneScape world (Lua)")

Game.Runescape_Init()

local io_queue = Platform.GetIOQueue()

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

while not Game.RunTasks() do
    Platform.LoadIO(io_queue)
end

Game.Runescape_BuildWorld()
print("World built")
