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

-- Await multiple requests concurrently. Polls all until every one is ready, then reads each.
-- req_ids: array of request IDs (e.g. from multiple asset_model_load calls).
-- Returns: array of { success, param_a, param_b, data_size, data } in same order as req_ids.
function Module.await_all(req_ids)
    if not req_ids or #req_ids == 0 then
        return {}
    end
    while true do
        local all_done = true
        for _, req_id in ipairs(req_ids) do
            if req_id and req_id ~= 0 and not HostIO.poll(req_id) then
                all_done = false
                break
            end
        end
        if all_done then break end
        coroutine.yield()
    end
    local results = {}
    for _, req_id in ipairs(req_ids) do
        if req_id and req_id ~= 0 then
            local success, param_a, param_b, data_size, data = HostIO.read(req_id)
            results[#results + 1] = { success, param_a, param_b, data_size, data }
        else
            results[#results + 1] = { false, nil, nil, nil, nil }
        end
    end
    return results
end

return Module