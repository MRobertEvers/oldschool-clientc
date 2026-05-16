Host.Print("Hello from Lua!")
local result = coroutine.yield()
Host.Print("Result: " .. result)

local io_queue = Platform.GetIOQueue()
print(io_queue)

local result2 = Platform.LoadIO(io_queue)
print(result2)
