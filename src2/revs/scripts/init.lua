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

-- UI: cache sprite definitions, then components + layout
Game.UI_Init()

Game.UI_RevConfigFetch(io_queue, "../../revs/configs/ui_min_cache.ini")
Platform.LoadIO(io_queue)
if not Game.UI_RevConfigLoad(io_queue) then
    print("cache config load failed")
    return
end
print("cache config loaded")

Game.UI_RevConfigFetch(io_queue, "../../revs/configs/ui_min.ini")
Platform.LoadIO(io_queue)
if not Game.UI_RevConfigLoad(io_queue) then
    print("ui config load failed")
    return
end
print("ui config loaded")

while not Game.UI_Ready() do
    Game.UI_Process(io_queue)
    Platform.LoadIO(io_queue)
end

Game.UI_Submit()
print("UI load complete")

-- RS interface component (sidebar tab example: inventory panel root)
local rs_component_id = 1497
Game.UI_QueueRSComponentNativeInt(rs_component_id)

while not Game.UI_Ready() do
    Game.UI_Process(io_queue)
    Platform.LoadIO(io_queue)
end

Game.UI_Submit()
print("RS component UI submitted")

-- Fountain 1497
-- Oak 1571
local model_load = 1571
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
