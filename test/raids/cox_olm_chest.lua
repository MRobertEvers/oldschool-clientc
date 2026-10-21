-- cox_olm_chest: the end of the raid without the fight -- the head rises,
-- ::coxolmwin runs its death (cox_olm.rs2 ~cox_olm_died): the exit crystal
-- smashes, the reward crystal is the Ancient chest, the loot is rolled into it.
-- Searched, photographed, taken.
--
--   python3 tools/raid_gate/run.py cox_olm_chest --no-publish

return {
    id = "cox_olm_chest",
    fixture = "fresh_lumbridge.ini",
    frames = 8000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.cheat("::coxolmrush", false)
        t.ticks(4)
        local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.chat.play({ "options", "choose:Step through the mystical barrier." })
        t.ticks(24)
        -- the raid's points overlay (cox_points.rs2 ~cox_hud_mount)
        local ov = t.ui.await_open("raids_overlay", 5)
        t.check("overlay", ov == "ok", tostring(ov))
        t.cheat("::coxolmwin", false)
        -- RSProx 842 / 3268: the smash on D+2, the chest and the open way on
        -- D+6 (cox_olm.rs2 ~cox_olm_died; the cheat is D+1)
        t.ticks(2)
        t.drive.camera(0, 300, 1300)
        t.shot("exit crystal smashing")
        t.ticks(5)
        t.shot("exit open, chest")
        local cr, chest = t.world.loc_near("raids_reward_chest", 40)
        t.check("chest", cr == "ok" and chest ~= nil, tostring(cr))
        local lr = t.raid.tob_loc_op("raids_reward_chest", 1)
        t.check("chest.search", lr == "ok", tostring(lr))
        local orr = t.ui.await_open("raids_rewards", 40)
        t.check("rewards.open", orr == "ok", tostring(orr))
        t.ticks(2)
        t.shot("rewards")
        local _, take_all = t.ui.widget("raids_rewards:inventory")
        t.check("take_all", t.ui.invoke(take_all, 1) == "ok", "")
        t.ticks(2)
        t.shot("taken")
        t.check("done", true, "")
    end,
}
