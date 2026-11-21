-- Desert Treasure I: Malak's tithe scene in the Hair of the Dog, then his talk. RSProx index 4628
-- [3273..3283] has the scene's start (a werewolf's hit cut it short there); the rest of its pages
-- are the wiki's. dt_malak_scene.rs2 has the frame; the test walks in the way the recorder did.
--
--     python3 tools/quest_gate/run.py --script test/quests/_dt_malak.lua --name dt_malak --fixture fresh_lumbridge.ini
return {
    id = "dt_malak",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        -- The cheat gives the prerequisites; the rows after it put the quest back at the hunts
        -- (stage 11) with the blood diamond's part untouched, north of the tavern's door.
        "::deserttreasure",
        "::setvar varb358_deserttreasure 11",
        "::setvar varb390_fd_column_blood 0",
        "::setvar varb387_fd_column_fire 0",
        "::setvar varb389_fd_column_ice 0",
        "::setvar varb388_fd_column_shadow 0",
        "::setvar varb373_fdvw_subquest 0",
        "::setvar varb372_fdvw_cutscene 0",
        "::setlevel hitpoints 99",
        "::tele 0_54_54_38_30",
    },

    run = function(t)
        t.ticklog.start()
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function var(name) return select(2, t.var.server(name)) end
        t.check("before", var("varb372_fdvw_cutscene") == 0, "fdvw_cutscene = " .. tostring(var("varb372_fdvw_cutscene")) .. " outside the tavern at " .. reading())
        -- In by the north doorway: the recorder ran in at 3494,3479 [3273] and the scene took them [3275].
        t.player.walk_route({ { 3494, 3481 }, { 3494, 3479 } })
        t.ticks(4)
        t.exec("scene.first", t.chat.play, { "npc:M-my Lord Malak, what brings you here?" })
        t.check("scene.varbit", var("varb372_fdvw_cutscene") == 1, "fdvw_cutscene = " .. tostring(var("varb372_fdvw_cutscene")) .. " with the first page up ([3275])")
        t.exec("scene.pages", t.chat.play, {
            "npc:Hmm? You know very well that your tithe is due, Roavar. I am here to collect.",
            "npc:Y-yes of course, sire",
            "npc:Indeed it is Roavar",
            "npc:Of course, my lord, I meant no disrespect.",
            "npc:So you have the tithe ready for me?",
            "npc:Of course, my lord. I trust you will find it all there and in good order.",
            "npc:Hmm... Well, for your sake",
            "npc:Then again... perhaps this was not a total waste of my time after all.",
            "npc:You there!",
            "player:M-me?",
            "npc:Yes, you. Your bearing suggests you are not one of the usual peasants",
            "npc:Come here, I would like to know what you are doing here.",
        })
        -- No click from here: the server walks the player to Malak and his talk opens.
        t.ticks(8)
        t.exec("walkedOver", t.chat.play, { "npc:A human, eh?" })
        local r, tt = t.world.tile()
        local mr, m = t.player.by_symbol("npc", "fourdiamonds_vampire_lord"), nil
        t.check("walkedOver.tile", r == "ok", "the player stands at " .. reading() .. " with Malak's talk open, having pressed nothing")
        t.finish(0)
    end,
}
