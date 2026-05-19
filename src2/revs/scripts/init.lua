print("Initializing Lua script")
local io_queue = Platform.GetIOQueue()
print(io_queue)

Game.Dat1_ConfigFileFetch(io_queue)

Platform.LoadIO(io_queue)

Game.Dat1_ConfigFileLoad(io_queue)
print("Config file loaded")
