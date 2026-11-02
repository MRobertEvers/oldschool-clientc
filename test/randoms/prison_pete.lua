-- prison_pete: the Evil Bob / Prison Pete random event end to end for the tick log, to diff
-- against RSProx index 4462 [3500..3661] (docs/rsprox-analysis/randoms/prison_pete.md):
--
--   ::prisonpete -> Evil Bob arrives -> "Yes, that seems like a good idea." -> ScapeRune (2093,4467)
--   -> Pete's intro -> lever, balloon of the shown shape, key to Pete, three times -> the east
--   hallway -> back on the tile -> the present.
--
-- The balloon of the shown shape is read off the server's state (::prisonstate, ::prisonbal).
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/prison_pete.lua --bots 1 \
--       --name pete0 --ticks 3000 --session /tmp/pete --fixture test/quests/fixtures/fresh_lumbridge.ini
local function state(t)
    t.cheat("::prisonstate", false)
    local r, d = t.msg.await("prisonstate", 4)
    if r ~= "ok" then return nil end
    local s = tostring(d)
    return {
        v = tonumber(s:match("v=(%d+)")),
        shape = tonumber(s:match("shape=(%d+)")),
        key = tonumber(s:match("key=(%d+)")),
        keys = tonumber(s:match("keys=(%d+)")),
        back = tonumber(s:match("back=(%d+)")),
    }
end

local function balloon_for(t, shape)
    t.cheat("::prisonbal " .. shape, false)
    local r, d = t.msg.await("prisonbal", 4)
    if r ~= "ok" then return -1 end
    return tonumber(tostring(d):match("prisonbal (%-?%d+)")) or -1
end

return {
    id = "prison_pete",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()

        -- 1. Evil Bob: talk, "Follow the scary cat?", yes.
        t.ticklog.mark("bob")
        t.cheat("::prisonpete", false)
        t.ticks(3)
        local r = t.npc.nearest("macro_evil_bob_prison", 16)
        t.check("bob.here", r == "ok", "Evil Bob within 16: " .. tostring(r))
        t.exec("bob.talk", t.player.talk_to, "macro_evil_bob_prison", 1)
        local _, talk_at = t.world.tile()
        t.exec("bob.dialog", t.chat.play, {
            "npc:Do you wanna meet my pet?",
            "choose:Yes, that seems like a good idea.",
        })
        t.ticks(9)
        local _, cell = t.world.tile()
        t.check("cell", cell and cell.x == 2093 and cell.z == 4467 and cell.level == 0,
            "in the cell: " .. tostring(cell and (cell.x .. "," .. cell.z .. "," .. cell.level)))
        t.check("welcome", t.msg.expect("Welcome to ScapeRune.") == "ok", "the arrival line")

        -- 2. Pete's intro, "Okay."
        t.ticklog.mark("intro")
        t.ticks(3) -- the first page is A+5 [3549]
        t.exec("intro.dialog", t.chat.play, {
            "npc:So you've been captured too?",
            "player:Captured?",
            "npc:Yes, Evil Bob captured me",
            "player:How do I get out of here?",
            "npc:Some of these balloon animals have keys in them",
            "npc:You need to pull the lever",
            "npc:Bring me any keys you get",
            "choose:Okay.",
            "player:Okay.",
        })
        t.ticks(2)

        -- 3. Three keys: lever, balloon of the shape, Pete.
        for k = 1, 3 do
            t.ticklog.mark("key." .. k)
            t.exec("lever." .. k, t.player.click_loc, "fai_falador_party_neg_room_lever", 1)
            if k == 1 then
                t.exec("lever.page", t.chat.play, { "npc:That's right, pull it." })
            end
            t.ticks(8)
            local st = state(t)
            t.check("shape." .. k, st and st.shape and st.shape >= 1 and st.shape <= 4, "the lever showed a shape")
            local b = balloon_for(t, st and st.shape or 1)
            t.check("balloon." .. k, b >= 1 and b <= 4, "a balloon of that shape: " .. tostring(b))
            t.exec("pop." .. k, t.player.talk_to, "prisonpete_balloon" .. b, 1)
            t.exec("pop.page." .. k, t.chat.play, { "npc:Great, now you've got a key!" })
            t.ticks(3) -- the walk to Pete and his page
            t.exec("give.page." .. k, t.chat.play, { "npc:Ooh, thanks! I'll see if it's the right one..." })
            if k < 3 then
                t.exec("right.page." .. k, t.chat.play, { "npc:Hooray, you got the right one!" })
            else
                t.exec("right.page.3", t.chat.play, {
                    "npc:You did it, you got all the keys right!",
                    "player:Let's get out of here before that cat notices.",
                })
            end
            st = state(t)
            t.check("keys." .. k, st and st.keys == k, "keys given " .. tostring(st and st.keys))
        end

        -- 4. East hallway: out, the present.
        t.ticklog.mark("exit")
        -- the walk is cut short by the teleport home: call it without a row
        t.player.walk_to(2098, 4466, 20)
        t.check("exit.line", t.msg.expect("You quickly escape the prison with Pete.") == "ok", "the escape line [3643]")
        t.ticks(10)
        local _, home = t.world.tile()
        t.check("home", home and talk_at and home.x == talk_at.x and home.z == talk_at.z and home.level == talk_at.level,
            "back where the cat found the player: " .. tostring(home and (home.x .. "," .. home.z)))
        t.exec("present.dialog", t.chat.play, {
            "npc:Thanks a lot for your help!",
        })
        t.check("present.line", t.msg.expect("Your reward is:") == "ok", "the present line [3657]")
        t.exec("present.thanks", t.chat.play, { "player:Thanks! See you around!" })
        t.check("macro_last", select(2, t.var.server("varb10056_macro_last")) == 6, "macro_last = 6")
        t.finish(0)
    end,
}
