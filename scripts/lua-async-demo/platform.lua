-- platform.lua
--
-- Async platform abstraction via Lua coroutine yields.
--
-- All I/O is delegated to the host (JS driver or native C host) so this
-- file contains zero platform-specific code.
--
-- ── Protocol ──────────────────────────────────────────────────────────────
--
--   Async operations YIELD a command tuple to the host driver.
--   The driver performs the operation and RESUMES the coroutine with the
--   result.  From the caller's perspective the call looks synchronous.
--
--   read_file(path)  yields ("read_file", path)
--                    resumed with (content_string)
--
--   sleep(ms)        yields ("sleep", ms)
--                    resumed with nothing
--
-- ── Synchronous calls ─────────────────────────────────────────────────────
--
--   log(id, msg)  calls host-registered global _lua_log(id, msg).
--                 Falls back to io.write() on a plain Lua interpreter.
--
-- ── Host contract ─────────────────────────────────────────────────────────
--
--   The host must set these globals before running core.lua:
--     _lua_log(instance_id, msg)   -- sync
--     INSTANCE_ID                  -- string identifying this instance

local M = {}

-- Suspend until the host provides the file content.
function M.read_file(path)
    return coroutine.yield("read_file", path)
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
