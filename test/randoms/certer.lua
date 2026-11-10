-- certer: the Certer random event (macro_certer/scripts/macro_certer.rs2):
-- answered right, answered wrong, left alone. To diff against RSProx index
-- 4513 [3000..3010], 4498 [6500..6510] and 4437 [500..510].
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/certer.lua --bots 1 \
--       --name certer0 --ticks 1500 --session /tmp/certer --fixture test/quests/fixtures/fresh_lumbridge.ini
local BROTHERS = { "macro_giles", "macro_miles", "macro_niles" }

local function certer(t)
    for _, sym in ipairs(BROTHERS) do
        local r, _, rows = t.npc.tiles(sym, 20)
        if r == "ok" and rows and rows[1] then return sym end
    end
    return nil
end

local function ask(t, step)
    local sym = certer(t)
    t.check(step .. ".here", sym ~= nil, "a certer within 20")
    if sym == nil then return nil end
    local r, d = t.player.talk_to(sym, 1)
    t.check(step .. ".op", r == "ok", tostring(r) .. " " .. tostring(d))
    t.exec(step .. ".question", t.ui.await_open, "macro_certer", 10)
    -- The right answer is not on the wire: read the server's own copy of
    -- varp9597 (right button x 16 + question).
    local vr, v = t.var.server("varp9597_macro_certer_answer")
    t.check(step .. ".state", vr == "ok" and v and v >= 16, "answer state " .. tostring(v))
    if vr ~= "ok" or not v or v < 16 then return nil end
    return math.floor(v / 16)
end

local function press(t, step, button)
    local letter = ({ "a", "b", "c" })[button]
    local _, w = t.ui.widget("macro_certer:macro_certer_" .. letter)
    local r = t.ui.invoke(w, 1)
    t.check(step .. ".press", r == "ok", "pressed macro_certer_" .. letter .. " -> " .. tostring(r))
end

return {
    id = "certer",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()

        -- 1. Right: the reward line and item, the page, gone two ticks on.
        t.ticklog.mark("certer.right")
        t.cheat("::certer", false)
        t.ticks(3)
        local right = ask(t, "right")
        if right then
            press(t, "right", right)
            t.ticks(1)
            t.expect("right.reward", t.msg.expect("Your reward is:"))
            t.exec("right.page", t.chat.play, { "npc:Thank you, I hope you like your present" })
            t.ticks(4)
            t.check("right.gone", certer(t) == nil, "the certer left after the answer")
        end

        -- 2. Wrong: nothing given, gone two ticks on.
        t.ticklog.mark("certer.wrong")
        t.cheat("::certer", false)
        t.ticks(3)
        right = ask(t, "wrong")
        if right then
            press(t, "wrong", (right % 3) + 1)
            t.ticks(4)
            t.check("wrong.gone", certer(t) == nil, "the certer left after a wrong answer")
        end

        -- 3. Left alone: gone by S+103.
        t.ticklog.mark("certer.ignore")
        t.cheat("::certer", false)
        t.ticks(60)
        t.check("ignore.stays", certer(t) ~= nil, "the certer still waiting at ~S+60")
        t.ticks(50)
        t.check("ignore.gone", certer(t) == nil, "the certer left by ~S+110")

        t.finish(0)
    end,
}
