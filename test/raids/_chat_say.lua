-- _chat_say: real public chat between two seats (api_drive.say ->
-- MESSAGE_PUBLIC -> the server's player-info chat block -> api_drive.messages),
-- the channel the relay's supply board talks over. Not a room test (the
-- leading `_`). Seat 1 speaks; both seats must read the line, under seat 1's
-- name, as chatbox type 2 (public), and say on which server tick they first
-- did. The refusals (empty, "::", over 80, a character chat cannot carry) are
-- checked on seat 2.
--
--   ./src/build_tob2w_opt/torirsserver --scriptrun test/raids/_chat_say.lua --bots 2 \
--       --session /tmp/chat_say --fixture tests/raids/fixtures/fresh_lumbridge.ini
local LINE = "supply: seat 3 has 2 brews"

local function tick_now(t)
    local r, tick = t.tick()
    return r == "ok" and tick or -1
end

local function run(t)
    local role = t.party.role()
    local names = t.party.names()
    t.ticks(3)
    t.expect("chat.barrier.ready", t.party.barrier("chat_ready", 200))
    local _, _, since = t.chat.public(0)
    local said_tick = -1
    if role == 1 then
        local r, detail = t.chat.say(LINE)
        said_tick = tick_now(t)
        t.check("chat.say", r == "ok", string.format("%s %s at tick %d", tostring(r), tostring(detail), said_tick))
    end
    local seen_tick, seen = -1, nil
    for _ = 1, 12 do
        local r, rows, newest = t.chat.public(since)
        since = newest
        if r == "ok" then
            for _, m in ipairs(rows) do
                if string.lower(m.text) == LINE then
                    seen, seen_tick = m, tick_now(t)
                end
            end
        end
        if seen then
            break
        end
        t.ticks(1)
    end
    t.check("chat.heard", seen ~= nil and seen.sender == names[1],
        string.format("seat %d read %s from %s at tick %d (said by %s%s)", role,
            seen and string.format("%q", seen.text) or "nothing", seen and seen.sender or "-", seen_tick, names[1],
            role == 1 and string.format(" at tick %d", said_tick) or ""))
    if role == 2 then
        local cases = { { "", "empty" }, { "::tele 3200 3200", "a command" },
                        { string.rep("a", 81), "81 characters" }, { "caf\u{e9}", "a non-table character" } }
        for _, c in ipairs(cases) do
            local r = t.chat.say(c[1])
            t.check("chat.refuses." .. c[2]:gsub("[^%w]", "_"), r == "refused", c[2] .. ": " .. tostring(r))
        end
    end
    t.expect("chat.barrier.done", t.party.barrier("chat_done", 200))
    t.finish(0)
end

return {
    id = "_chat_say",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    max_frames = 4000,
    setup = {},
    run = run,
}
