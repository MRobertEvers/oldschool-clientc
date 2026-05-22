print("Render Model")
local model_id = ...

local io_queue = Platform.GetIOQueue()

Game.Dat1_ModelFetchScriptInt(io_queue, model_id)

Platform.LoadIO(io_queue)

if not Game.Dat1_ModelLoad(io_queue) then
    print("Model load failed")
    return
end
print("Model loaded")

Game.Dat1_SubmitGameCacheModelScriptInt(model_id)
print("Game cache model submitted")

Game.ModelViewer_RenderModelScriptInt(model_id)
