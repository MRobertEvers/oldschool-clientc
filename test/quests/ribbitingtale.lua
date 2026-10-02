-- The Ribbiting Tale of a Lily Pad Labour Dispute. Spec: Quest Helper ladder + ribbitingtale.rs2.
return {
    id = "ribbitingtale",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel woodcutting 15",
        "::complete quest_childrenofthesun",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb9844_frog_quest",
            constants = {
                not_started = 0, talk_blue = 2, marcellus2 = 4, gary_leader = 6, yellow = 8,
                chop = 10, sabotage = 12, hopoff = 14, after_hop = 16, marcellus3 = 18,
                blame = 20, chest = 22, plant = 24, cuthbert = 26, marcellus_end = 28,
                gary_end = 30, complete = 32,
            },
            display = "The Ribbiting Tale of a Lily Pad Labour Dispute",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToMarcellus", t.player.goto_tile, 1683, 2973, 0)
        local found, row = t.npc.nearest("frog_quest_marcellus_normal", 30)
        t.check("marcellus.spawn", found == "not_found" or found == "no_row", "nearest(frog_quest_marcellus_normal, 30) -> " .. tostring(found) .. " " .. tostring(row))
        t.exec("goto-blueFrogs", t.player.goto_tile, 1694, 2996, 0)
        local gfound, grow = t.npc.nearest("frog_quest_gary_unnamed", 30)
        t.check("gary.spawn", gfound == "not_found" or gfound == "no_row", "nearest(frog_quest_gary_unnamed, 30) -> " .. tostring(gfound) .. " " .. tostring(grow))
        t.blocked("content_bug: no placement for frog_quest_marcellus_normal / frog_quest_gary_unnamed / yellow frogs -- m26_46.spawn (OSRS-Content/osrs239-content/server/scripts/areas/world/configs/m26_46.spawn) has no frog_quest row; only ribbit_bmp.rs2 debugprocs npc_add them, so the quest cannot be started by click (ribbitingtale.rs2:45). Also ribbitingtale.rs2:322 yellow-frog talk guard starts at ^ribbit_chop (10) while gary_leader sets ^ribbit_yellow (8) and only debugproc ribbitrun (ribbitingtale.rs2:466) writes 10, so stage 8 never reaches the chop/sabotage legs")
        return
    end,
}
