-- quizmaster: the Quiz Master random event end to end, for the tick log, to diff against RSProx index 4425
-- [1500..1582] (docs/rsprox-analysis/quests/tale_of_the_righteous.md, "The Quiz Master"):
--
--   ::quizmaster -> the invitation arrives -> "Yes please, I'm so excited!" -> the seat in the quiz room ->
--   the host's three pages -> one wrong answer, then four right in a row -> "Mystery Box" -> home -> the box
--   opened.
--
-- The odd one is read off the server's quiz state (varp9501_macro_quiz_state: the odd button + 1, times 8).
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/quizmaster.lua --bots 1 \
--       --name quiz0 --ticks 3000 --session /tmp/quiz --fixture test/quests/fixtures/fresh_lumbridge.ini
local SEAT_X, SEAT_Z = 1952, 4764

-- The open question's odd button, from ::quizodd (-1 between questions).
local function odd_button(t)
    t.cheat("::quizodd", false)
    local r, d = t.msg.await("quizodd", 4)
    if r ~= "ok" then return -1 end
    return tonumber(tostring(d):match("quizodd (%-?%d+)")) or -1
end

local function await_question(t, n)
    for _ = 1, n do
        local odd = odd_button(t)
        if odd >= 0 then return odd end
    end
    return -1
end

-- The Select on a spinning model is the [if_button1,macro_quizshow:buttons] trigger; the models are
-- the clientscript's cc_create children, which a headless run never builds, so ::quizanswer runs the
-- trigger's own proc.
local function answer(t, slot)
    t.cheat("::quizanswer " .. slot, false)
end

return {
    id = "quizmaster",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()
        local _, home0 = t.world.tile()
        t.ticklog.mark("invitation")
        t.cheat("::quizmaster", false)
        t.ticks(3)
        local r = t.npc.nearest("macro_magneson_invitation", 16)
        t.check("invitation.here", r == "ok", "the invitation within 16: " .. tostring(r))
        t.exec("invitation.talk", t.player.talk_to, "macro_magneson_invitation", 1)
        t.exec("invitation.dialog", t.chat.play, {
            "npc:It's your lucky day,",
            "choose:Yes please, I'm so excited!",
            "player:Yes please, I'm so excited!",
        })
        t.ticks(3)
        local _, seat = t.world.tile()
        t.check("seat", seat and seat.x == SEAT_X and seat.z == SEAT_Z and seat.level == 1,
            "in the quiz seat: " .. tostring(seat and (seat.x .. "," .. seat.z .. "," .. seat.level)))
        t.check("rules", t.msg.expect("Answer four questions correctly in a row to be teleported back where you came from.") == "ok", "the rules line [1510]")

        t.ticklog.mark("quiz")
        local odd = await_question(t, 15)
        t.check("question.1", odd >= 0, "the first question by A+16")
        -- one wrong answer first, as recorded [1537]: the run starts again
        t.note("q1 odd " .. odd .. ": answering " .. ((odd + 1) % 3))
        answer(t, (odd + 1) % 3)
        t.ticks(3) -- the verdict, and the next question three ticks after it
        for q = 1, 4 do
            odd = await_question(t, 8)
            t.check("question.right." .. q, odd >= 0, "question " .. q .. " of the run")
            answer(t, odd)
            t.ticks(3)
        end
        t.ticklog.mark("prize")
        t.check("prize.won", t.msg.expect("CONGRATULATIONS") == "ok", "the win page's echo [1564]")
        t.ticks(2) -- the prize menu three ticks after the win page [1567]
        t.exec("prize.menu", t.chat.drain, { stop_at = "options" })
        t.exec("prize.choose", t.chat.choose, "Mystery Box")
        t.ticks(3)
        local _, home = t.world.tile()
        t.check("home", home and home0 and home.x == home0.x and home.z == home0.z and home.level == home0.level,
            "back where the invitation found the player: " .. tostring(home and (home.x .. "," .. home.z)))
        t.check("prize.line", t.msg.expect("Your reward is: <col=ff0000>1</col> x <col=ff0000>Mystery box</col>.") == "ok", "the reward line")
        t.expect("prize.box", t.inv.expect_has("macro_quiz_mystery_box", 1))
        t.check("macro_last", select(2, t.var.server("varb10056_macro_last")) == 3, "macro_last = 3")

        t.ticklog.mark("box")
        t.exec("box.open", t.player.inv_op, "macro_quiz_mystery_box", 1)
        t.ticks(2)
        local _, left = t.inv.count("macro_quiz_mystery_box")
        t.check("box.opened", (tonumber(left) or 0) == 0, "the box became its prize")
        t.finish(0)
    end,
}
