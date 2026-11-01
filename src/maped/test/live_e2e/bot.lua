-- The live lane end to end's bot (src/maped/test/live_e2e/run.sh).
--
-- Already logged in and playing when the editor's RUN lands: it talks to Hans
-- and reads his first line, then keeps talking to him every few ticks until
-- the line the editor wrote comes back -- no relog, no restart, the same
-- session. After that it stands in Lumbridge (square m50_50) while the second
-- half of run.sh edits a tile height there and RUNs again; the REBUILD that
-- reload owes the bot is counted from the wire log, not here.
--
-- Then (run.sh RUN (c) and (d)) it keeps pressing an op the records do not
-- have yet: op3 on the blue flower beside the castle (a loc with no ops at
-- all) and op4 on Hans (empty). The editor adds `op3=Search` / `op4=Wave` to
-- the records and the [oploc3,blueflower] / [opnpc4,hans] scripts, RUNs, and
-- the same session -- never relogged -- presses the new row and reads the
-- script's message. The loc row is the bot's own decode of the loc record,
-- dropped on the CACHE_INVALIDATE for config group 2/6; the npc row is the
-- server's npc table (ToriRSServer_NpcInfoRecord), replaced by the reload.
--
-- Rows are the protocol run.sh reads: live_e2e.first, live_e2e.second,
-- live_e2e.loc_op, live_e2e.npc_op.
local OLD = "Hello. What are you doing here?"
local NEW = "Hello. What are you doing here, editor?"
local LOC_MES = "editor op3"
local NPC_MES = "editor op4"

return {
    id = "live_e2e",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        -- The fixture logs the bot in at Lumbridge (a bot is not staff, so a
        -- ::tele would be refused): Hans patrols the castle grounds here.
        t.ticks(2)

        -- Hans's first line, or the reason there is none.
        local function first_line()
            local target, sym_result, sym_name = t.player.by_symbol("npc", "hans")
            if not target then
                return nil, "no hans: " .. tostring(sym_result) .. " " .. tostring(sym_name)
            end
            t.player.walk_near(target)
            local talk_result, talk_detail = t.player.talk_to("hans")
            if talk_result ~= "ok" then
                t.chat.close()
                return nil, "talk_to: " .. tostring(talk_result) .. " " .. tostring(talk_detail)
            end
            local text_result, text = t.chat.text()
            t.chat.close()
            if text_result ~= "ok" then
                return nil, "chat.text: " .. tostring(text_result) .. " " .. tostring(text)
            end
            return text, "ok"
        end

        local first, why = first_line()
        t.check("live_e2e.first", first ~= nil and string.find(first, OLD, 1, true) ~= nil
            and string.find(first, NEW, 1, true) == nil, tostring(first or why))

        -- Until the RUN's reload swaps the script pack under this session.
        local second = nil
        local tries = 0
        for i = 1, 2000 do
            t.ticks(5)
            tries = i
            local line = first_line()
            if line and string.find(line, NEW, 1, true) then
                second = line
                break
            end
        end
        t.check("live_e2e.second", second ~= nil,
            tostring(second) .. " after " .. tostring(tries) .. " talk(s)")

        -- Did a chat line newer than `since` say `text`?
        local function heard(since, text)
            local result, rows = t.chat.lines(since)
            if result ~= "ok" then
                return false
            end
            for _, row in ipairs(rows) do
                if row.text and string.find(row.text, text, 1, true) then
                    return true
                end
            end
            return false
        end
        local function serial()
            local result, rows = t.chat.lines(0)
            local newest = 0
            if result == "ok" then
                for _, row in ipairs(rows) do
                    if row.serial > newest then newest = row.serial end
                end
            end
            return newest
        end

        -- Stay in m50_50 (the tile edit's REBUILD is counted from the wire
        -- log) pressing the op rows the editor will add; run.sh detaches
        -- (which stops this process) when it is done.
        local loc_done, npc_done = false, false
        local loc_tries, npc_tries = 0, 0
        local loc_last, npc_last = "", ""
        for _ = 1, 4000 do
            t.ticks(5)
            if not loc_done then
                loc_tries = loc_tries + 1
                local since = serial()
                local r, d = t.player.click_loc("blueflower", 3)
                loc_last = tostring(r) .. " " .. tostring(d)
                if r == "ok" then
                    for _ = 1, 20 do
                        t.ticks(1)
                        if heard(since, LOC_MES) then loc_done = true break end
                    end
                    t.chat.close()
                    if loc_done then
                        t.check("live_e2e.loc_op", true, "op3 on blueflower read '" .. LOC_MES
                            .. "' after " .. loc_tries .. " press(es), no relog")
                    end
                end
            elseif not npc_done then
                npc_tries = npc_tries + 1
                local since = serial()
                local r, d = t.player.talk_to("hans", 4)
                npc_last = tostring(r) .. " " .. tostring(d)
                for _ = 1, 20 do
                    if heard(since, NPC_MES) then npc_done = true break end
                    t.ticks(1)
                end
                t.chat.close()
                if npc_done then
                    t.check("live_e2e.npc_op", true, "op4 on hans read '" .. NPC_MES
                        .. "' after " .. npc_tries .. " press(es), no relog")
                end
            end
        end
        if not loc_done then
            t.check("live_e2e.loc_op", false, "never read '" .. LOC_MES .. "': " .. loc_last)
        end
        if not npc_done then
            t.check("live_e2e.npc_op", false, "never read '" .. NPC_MES .. "': " .. npc_last)
        end
        t.finish(0)
    end,
}
