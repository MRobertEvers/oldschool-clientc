-- The quest driver: one Lua coroutine per quest test, resumed from C.
--
-- Owner: core-scheduler (docs/ARCHITECT.md).  This file is the manifest entry
-- and is concatenated LAST, after quest_driver/{core,state,chat,read,pointer,
-- world,ui}.lua -- it is the only file in the chunk with a top-level return.
--
-- The plugin itself does almost nothing.  It exists so the test-only
-- `api.drive` module has a script to be registered into and so the scheduler
-- has a per-frame call site: on_frame_start pumps the drive event ring into
-- whatever await is armed and resumes the coroutine while it is satisfied.
--
-- Why the coroutine is not driven from Lua: the sandbox has no `coroutine`
-- table and `pcall` was removed on purpose, because an instruction-budget
-- error must not be catchable.  C creates the thread, re-arms the step hook on
-- the coroutine's OWN lua_State at every resume (lua_newthread copies the hook
-- only at creation) and owns lua_resume.  A test only ever gets `await`.

---@type torirs.Plugin
local plugin = {
    id = "quest-driver",
    title = "Quest driver",
    version = "0.1.0",
}

-- The concatenation is silent when it goes wrong: a part that failed to read
-- leaves its namespace empty and the first quest step calls nil thirty steps
-- later. Counting them at on_start is what turns that into one line at boot.
local PARTS = { "chat", "scroll", "levelup", "player", "var", "inv", "msg",
                "ui", "npc", "world", "drive", "t" }

function plugin.on_start(api)
    -- The quest coroutine is resumed from C with no `api` of its own (it
    -- only ever receives `t`, see quest_driver/core.lua's QD_ROOT) -- this is
    -- the one place an `api` table is in scope early enough to hand
    -- api.drive to every part before any await can be registered.
    QD.core_bind(api)
    local missing = {}
    for _, name in ipairs(PARTS) do
        local group = QD[name]
        if type(group) ~= "table" or next(group) == nil then
            missing[#missing + 1] = name
        end
    end
    if #missing > 0 then
        api.core.log("quest-driver: EMPTY namespaces: " .. table.concat(missing, ","))
    else
        api.core.log("quest-driver: " .. #PARTS .. " namespaces present")
    end
    api.core.log("quest-driver: loaded")
    local session = api.drive.session()
    if session.script == "" then
        -- A run with no TORIRS_QUEST_SCRIPT still loads the driver: that is
        -- the shape the load gate uses, and it must not look like a failure.
        api.core.log("quest-driver: no TORIRS_QUEST_SCRIPT, idle")
    else
        api.core.log("quest-driver: quest script " .. session.script)
    end
end

function plugin.on_frame_start(api)
    -- The pump.  Reads the drive ring by cursor -- so it sees every event
    -- stamped since the last frame, including the ones raised after this
    -- frame's own pump -- and resumes the coroutine while an await is
    -- satisfied.  Unimplemented: core-scheduler.
    api.drive.pump()
end

return plugin
