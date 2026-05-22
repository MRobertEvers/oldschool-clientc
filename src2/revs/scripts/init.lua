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

Game.Dat1_ModelFetchNativeInt(io_queue, 0)

Platform.LoadIO(io_queue)

if not Game.Dat1_ModelLoad(io_queue) then
    print("Model load failed")
    return
end
print("Model loaded")

Game.Dat1_SubmitGameCacheModelNativeInt(0)
print("Game cache model submitted")

Game.ModelViewer_RenderModelNativeInt(0)
