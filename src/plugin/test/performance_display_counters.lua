-- Appended to the shipped performance_display.lua by plugin_lua_test.c, and
-- run against the REAL Porcelain layer over the shared testbed engine -- not
-- against the stand-in reconciler in performance_display_behavior.lua.
--
-- Three things only a file with the real layer under it can say.
--
-- 1. The PUMP is installed. Nothing in the product and nothing in this file
--    calls fence or commit any more, and yet four rows exist on the engine.
--
-- 2. The fence is FIRST and the commit is LAST. `creates` read at the TOP of
--    the first handler is already 4, which it could not be if the runtime
--    called the handler before fencing; and `revalidates` read at the top of
--    the SECOND handler is already 1, which it could not be if nothing
--    committed after the first.
--
-- 3. The STEADY STATE, from the layer's own counters. The stand-in could only
--    pin what its author believed the layer does; these are what it did.
local FIRST = 1
local SECOND = 2
local RESET_AT = 16
local READ_AT = 36

local seen = 0
-- The third handler the pump installs. This table declares no on_server_tick
-- at all, so the only thing that can fire this timer is the runtime forwarding
-- the cadence -- which is the shape screenshot.lua's delayed captures depend
-- on and which nothing else in the suite reaches.
local ticks = 0
-- The driver sends exactly this many between the reset and the reading.
local TICKS_SENT = 3

return {
    id = 'performance-counters',
    -- The runtime resolves api.config.<key> against the REGISTERED table's own
    -- declarations, and the registered table is this one. Borrowing the
    -- product's list is what makes its settings readable here.
    config = product.config,
    on_start = function(api)
        product.on_start(api)
        api.porcelain.every_server_tick(function() ticks = ticks + 1 end)
    end,
    -- Forwarded, not noted. The product recomposes its strings here and says
    -- nothing to the layer; the only thing that can make the next fence
    -- re-describe is the runtime's own note.
    on_config_changed = function(api, key)
        product.on_config_changed(api, key)
    end,
    on_frame_start = function(api, ev)
        local before = api.porcelain.counters_read()
        seen = seen + 1
        if seen == FIRST then
            assert(before.creates == 4,
                'the pump fences BEFORE the plugin handler: the four rows already ' ..
                'exist the first time the handler is reached')
            assert(before.describe_runs == 2,
                'and the fence ran its second pass to consume the bind its first raised')
        elseif seen == SECOND then
            assert(before.revalidates == 1,
                'the pump commits: the first frame resolved the layout exactly once')
            -- And it commits at the END of the frame, not before the handler.
            -- A missing commit is NOT a missing revalidate -- the next fence
            -- finds the unflushed epoch and flushes it for you -- so the tell
            -- is the finding it records on the way past. Reading the count
            -- here is how "the commit happened" is distinguished from "the
            -- layer cleaned up after a pump that forgot".
            for _, one in ipairs(api.porcelain.findings()) do
                assert(one.detail ~= 'fence without commit',
                    'the pump commits after the plugin handler: a fence whose commit ' ..
                    'never came is a budget finding, not a silent frame')
            end
        end
        product.on_frame_start(api, ev)
        if seen == RESET_AT then
            api.porcelain.counters_reset()
        elseif seen == READ_AT then
            local now = api.porcelain.counters_read()
            api.core.log(string.format(
                'counters engine_calls=%d allocations=%d describe_runs=%d creates=%d ' ..
                'removes=%d setters=%d revalidates=%d property_applies=%d over %d frames',
                now.engine_calls, now.allocations, now.describe_runs, now.creates,
                now.removes, now.setters, now.revalidates, now.property_applies,
                READ_AT - RESET_AT))
            -- The plan's rule, read rather than believed. `property_applies`
            -- is the sharp one: zero means the hash compare short-circuited
            -- the whole property walk, which is the rule. A non-zero here with
            -- zero setters would mean every per-field compare merely happened
            -- to match, which is a second line of defence and not the rule.
            assert(now.describe_runs == 0, 'a settled readout must not re-describe')
            assert(now.engine_calls == 0, 'and must make no engine call at all')
            assert(now.allocations == 0, 'and must allocate nothing')
            assert(now.setters == 0, 'and must write no setter')
            assert(now.creates == 0 and now.removes == 0,
                'and must neither create nor remove a row')
            assert(now.revalidates == 0, 'and must cost no layout resolve')
            assert(now.property_applies == 0,
                'and must not walk a single item property set: an unchanged hash is ' ..
                'one compare and nothing else')
            assert(ticks == TICKS_SENT,
                'the pump forwards the server tick: a timer fired although nothing ' ..
                'declares on_server_tick')
            api.core.log('performance counters verified')
        end
    end,
}
