-- levelup_op: an op's dialogue against queued level-up boxes, as Jagex runs it
-- (docs/rsprox-analysis/levelup/README.md, "Against an op's dialogue").
--
--   1. A box is up and two more are queued (Attack, then Thieving and combat).
--      Talk-to Hans: the op's packet closes the Attack box; the Thieving box the
--      queue opens behind it is closed again when the op fires (RSProx index
--      4521 [281], 4517 [3992]: a queued box opened and shut, with no input, on
--      the tick a latched op lands), and Hans's page is the one up.
--   2. His conversation runs to its menu: no box replaces or shares the chatbox
--      while it is open (no recorded box ever does, 214 sessions).
--   3. The boxes still queued wait for the conversation to end and open after
--      it (index 4360 [1176] -> [1184]: a Mining box behind the quest scroll,
--      a page and an objectbox, opened the tick after the last one closed).
--
-- Before torirs_server_world.c interaction_close_chat_behind, Hans's script ran
-- while the Thieving box's script was parked on its continue: his page went up
-- over the box, his script was dropped ("dropping [proc,chatnpc...], which
-- suspended while [proc,levelup_box] waits"), and his page's continue resumed
-- the LEVEL-UP script, whose end closed his page for the next box.
--
--   TORIRSSERVER_DEV=1 ./src/build_opt/torirsserver --scriptrun test/levelup/levelup_op.lua \
--       --bots 1 --name levelupop0 --ticks 300 --session /tmp/levelup_op \
--       --fixture test/quests/fixtures/fresh_lumbridge.ini
return {
    id = "levelup_op",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {},
    run = function(t)
        t.ticks(3)
        t.ticklog.start()

        -- ::xp takes tenths, as stat_advance does: Attack 1 -> 20 (raises combat)
        -- and Thieving 1 -> 13, the drops of test/levelup/levelup.lua's chain.
        t.cheat("::xp attack 44700")
        t.cheat("::xp thieving 20000")
        local br, bd = t.levelup.skill()
        t.check("box.attack", br == "ok" and type(bd) == "table" and bd.skill == "attack",
            tostring(br) .. " " .. (type(bd) == "table" and tostring(bd.skill) or tostring(bd)))

        -- The click settles on the first page up, and that can be the Thieving box the
        -- queue opens while the player walks: Hans's page is the one his op puts up
        -- on arrival, and a player waits for it.
        t.exec("hans.talk", t.player.talk_to, "hans")
        local hr = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "Hans's page",
        }, 12)
        t.check("hans.page", hr == "ok", tostring(hr) .. " chat.kind " .. tostring(t.chat.kind()))
        t.exec("hans.dialogue", t.chat.play, { "npc:Hello. What are you doing here?", "options" })
        t.check("hans.menu_up", t.chat.kind() == "options", "chat.kind " .. tostring(t.chat.kind()))

        -- The menu is left open for two ticks: nothing queued may take its place.
        t.ticks(2)
        t.check("hans.menu_held", t.chat.kind() == "options", "chat.kind " .. tostring(t.chat.kind()))

        -- Escape on the menu. chat.close waits for an empty chatbox, which a queued
        -- box never leaves it (it opens on the close's tick), so its own answer is a
        -- note and the menu's absence is the row.
        local cr, cd = t.chat.close()
        t.note("chat.close -> " .. tostring(cr) .. " " .. tostring(cd))
        t.check("hans.closed", t.chat.kind() ~= "options", "chat.kind " .. tostring(t.chat.kind()))
        local ar = t.await({
            level = function()
                return t.chat.kind() == "levelup"
            end,
            note = "a queued box after the conversation",
        }, 6)
        local lr, ld = t.levelup.skill()
        t.check("box.after_conversation", ar == "ok" and lr == "ok" and type(ld) == "table",
            tostring(ar) .. " " .. tostring(lr) .. " " .. (type(ld) == "table" and tostring(ld.skill) or tostring(ld)))
    end,
}
