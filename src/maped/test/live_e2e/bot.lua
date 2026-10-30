-- The live lane end to end's bot (src/maped/test/live_e2e/run.sh).
--
-- Already logged in and playing when the editor's RUN lands: it talks to Hans
-- and reads his first line, then keeps talking to him every few ticks until
-- the line the editor wrote comes back -- no relog, no restart, the same
-- session. After that it stands in Lumbridge (square m50_50) while the second
-- half of run.sh edits a tile height there and RUNs again; the REBUILD that
-- reload owes the bot is counted from the wire log, not here.
--
-- Rows are the protocol run.sh reads: live_e2e.first, live_e2e.second.
local OLD = "Hello. What are you doing here?"
local NEW = "Hello. What are you doing here, editor?"

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

        -- Stand in m50_50 for the tile edit; run.sh detaches (which stops
        -- this process) when it has counted the REBUILD.
        for _ = 1, 4000 do
            t.ticks(10)
        end
        t.finish(0)
    end,
}
