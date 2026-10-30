-- The Black Knight Titan (Holy Grail): quests/quest_grail/scripts/black_knight_titan.rs2
-- and quests/quest_grail/configs/black_knight_titan.npc, against RSProx index 4435
-- [2086..2636] (the owner, 2026-10-30, rev 241; docs/rsprox-analysis/quests/
-- holy_grail.md, "Black Knight Titan").
--
-- The recorder stood on 2792,4722, east of the titan (2791,4722), with Protect
-- from Melee up, and fought with a rune scimitar [2100..2476], then Excalibur
-- [2477..2546]. This test stands there too and fights him twice:
--   leg.scimitar  - empties him with the scimitar. No recording reaches zero
--                   without Excalibur: the wiki's "regenerates back to full
--                   hitpoints" and LostCity's line and pages (UNVERIFIED);
--   leg.excalibur - the kill: "Well done! You have defeated the Black Knight
--                   Titan!" and the glide to 2790,4722 a tick after the
--                   killing splat ([2547]), the del two ticks later ([2549]),
--                   back 4 ticks after that ([2553]).
-- The tick log (t.ticklog) is the wire audit's input: seqs, sounds, splats,
-- the death and the respawn, against the recording.
--
-- Staged: 99 melee, Prayer 99, a rune scimitar and Excalibur, teleported to the
-- bridge's east end (a fight test, not the quest's walk: test/quests/grail.lua
-- walks there).

local function tile_text(result, tile)
    if result ~= "ok" or type(tile) ~= "table" then
        return tostring(result) .. " " .. tostring(tile)
    end
    return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

return {
    id = "grail_titan",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::give rune_scimitar 1",
        "::give excalibur 1",
        "::tele 0_43_73_41_50", -- 2793,4722: the bridge's east end
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(3) -- the setup cheats are not client-side yet
        t.exec("wield.scimitar", t.player.equip, "rune_scimitar")
        t.prayer.set("protectfrommelee", true)

        -- [2154]: the page and the menu; answer 1 as at [2159].
        t.ticklog.mark("leg.talk")
        t.exec("talk", t.player.talk_to, "black_knight_titan", 1)
        t.exec("talk.dialog", t.chat.play, {
            "npc:I am the Black Knight Titan!",
            "choose:Okay, have at ye oh evil knight!",
            "player:Okay, have at ye oh evil knight!",
        })

        -- Leg 1: zero hitpoints without Excalibur.
        t.ticklog.mark("leg.scimitar")
        t.exec("scimitar.attack", t.player.attack, "black_knight_titan", 2, 20)
        local maybe_r, maybe_d = t.msg.await("Maybe you need something more to beat the titan?", 500)
        t.check("scimitar.notDefeated", maybe_r, maybe_d)
        t.exec("scimitar.pages", t.chat.play, {
            "npc:Puny mortal...",
            "npc:INVINCIBLE!!!",
        })
        local well1_r = t.msg.expect("Well done! You have defeated the Black Knight Titan!")
        t.check("scimitar.noWellDone", well1_r == "refused",
            "msg.expect Well done -> " .. tostring(well1_r) .. " (want refused: no kill without Excalibur)")
        -- The heal to full is silent on the wire (no headbar until his next
        -- splat), so the client's bar reads 0/30 here: no row (Open, UNVERIFIED).

        -- Leg 2: the Excalibur kill.
        t.exec("wield.excalibur", t.player.equip, "excalibur")
        t.ticklog.mark("leg.excalibur")
        t.exec("excalibur.attack", t.player.attack, "black_knight_titan", 2, 20)
        local well_r, well_d = t.msg.await("Well done! You have defeated the Black Knight Titan!", 500)
        t.check("excalibur.defeated", well_r, well_d)
        local landed_r, landed = t.world.tile()
        t.check("excalibur.glide", landed_r == "ok" and landed.level == 0 and landed.x == 2790 and landed.z == 4722,
            "t.world.tile() -> " .. tile_text(landed_r, landed) .. " (want 2790,4722,0: [2547])")
        t.ticks(10)
        local back_r, back_d = t.npc.await_present("black_knight_titan", 10, 10)
        t.check("excalibur.respawned", back_r, back_d)
        t.ticklog.mark("leg.end")
    end,
}
