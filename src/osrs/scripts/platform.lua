local M = {}

-- Suspend until the host provides the file content.
function M.cache_read(table_id, archive_id)
    return coroutine.yield("cache_read", table_id, archive_id)
end

function M.buildcache_store(table_id, archive_id, data)
    return coroutine.yield("buildcache_store", table_id, archive_id, data)
end

-- Suspend for ms milliseconds.
function M.sleep(ms)
    coroutine.yield("sleep", ms)
end

-- Synchronous log (does NOT yield).
function M.log(instance_id, msg)
    if type(_G._lua_log) == "function" then
        _G._lua_log(instance_id, msg)
    else
        io.write(string.format("[%s] %s\n", instance_id, msg))
    end
end


return M
