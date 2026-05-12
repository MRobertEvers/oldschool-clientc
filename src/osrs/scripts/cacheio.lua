local M = {}

M.Func = {
    CACHEIO_LOAD_ARCHIVES = 5,
}

--- @param request_list lightuserdata LuaCacheIORequestList*
function M.load_request_list(request_list)
    return { coroutine.yield(M.Func.CACHEIO_LOAD_ARCHIVES, request_list) }
end

return M
