print("Initializing Lua script")
Game.ModelViewer_Init()


local io_queue = Platform.GetIOQueue()
print(io_queue)

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

local model_load = 1497
Game.Dat1_ModelFetchNativeInt(io_queue, model_load)

Platform.LoadIO(io_queue)

if not Game.Dat1_ModelLoad(io_queue) then
    print("Model load failed")
    return
end
print("Model loaded")

Game.Dat1_SubmitGameCacheModelNativeInt(model_load)
Game.Dat1_ModelCleanupNativeInt(model_load)
print("Game cache model submitted")

Game.ModelViewer_RenderModelNativeInt(model_load)
