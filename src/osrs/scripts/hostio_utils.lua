local Module = {}

function Module.await(req_id)
    -- 1. Validation
    if not req_id or req_id == 0 then 
        return false, "Invalid Request ID" 
    end

    -- 2. The Polling Loop
    -- This looks like an infinite loop, but it's not!
    while not HostIO.poll(req_id) do
        -- 3. The Magic: Yield
        -- This pauses this Lua function and returns control back to your 
        -- C++ update loop and the Browser's main thread.
        coroutine.yield() 
    end

    -- 4. Retrieval
    -- Once C/JS sets the 'finished' flag, the loop breaks and we get the data.
    local success, param_a, param_b, data_size, data = HostIO.read(req_id)
    return success, param_a, param_b, data_size, data
end

return Module