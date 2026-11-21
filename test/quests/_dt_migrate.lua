-- Desert Treasure I: a save from before the rebuild (each diamond's progress in a private varp) is
-- carried over to the quest's own varbits. dt_migrate.rs2 runs it on login; ::dtmigrate runs the
-- same proc here.
--
--     python3 tools/quest_gate/run.py --script test/quests/_dt_migrate.lua --name dt_migrate --fixture fresh_lumbridge.ini
return {
    id = "dt_migrate",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv" },
    run = function(t)
        local function var(name) return select(2, t.var.server(name)) end
        local function set(name, v) t.cheat("::setvar " .. name .. " " .. v, false) end
        local function expect(step, want)
            t.ticks(2)
            local bad = {}
            for name, v in pairs(want) do
                if var(name) ~= v then bad[#bad + 1] = name .. " = " .. tostring(var(name)) .. " (want " .. v .. ")" end
            end
            t.check(step, #bad == 0, #bad == 0 and "every variable carried over" or table.concat(bad, "; "))
        end
        -- Mid-hunt: Fareed dead, Dessous dead (diamond not yet collected), the ring in hand, agreed to help the troll child.
        set("varb358_deserttreasure", 10)
        set("varp6614_dt_smoke_stage", 100)
        set("varp6607_dt_blood_stage", 3)
        set("varp6622_dt_shadow_stage", 3)
        set("varp6618_dt_ice_stage", 2)
        t.cheat("::dtmigrate", false)
        expect("midHunt", {
            varb358_deserttreasure = 11, varb364_fd_firechest = 1, varb376_fd_killed_firewarrior = 1, varb386_fd_firewargate = 1,
            varb372_fdvw_cutscene = 1, varb373_fdvw_subquest = 3, varb383_fd_shadowwarrior_quest = 3, varb384_fd_banditchest_disarmed = 1,
            varb382_fd_icewarrior_subquest = 2, varb359_fd_trollchild_intro = 1, varb377_fd_icewarrior_dead = 0,
            varp6614_dt_smoke_stage = 0, varp6607_dt_blood_stage = 0, varp6622_dt_shadow_stage = 0, varp6618_dt_ice_stage = 0,
        })
        -- A second run changes nothing (the old varps are empty), and never lowers what is there.
        set("varb373_fdvw_subquest", 4)
        t.cheat("::dtmigrate", false)
        expect("again", { varb373_fdvw_subquest = 4, varb358_deserttreasure = 11, varb383_fd_shadowwarrior_quest = 3 })
        -- Everything done under the old variables.
        set("varp6614_dt_smoke_stage", 100)
        set("varp6607_dt_blood_stage", 100)
        set("varp6622_dt_shadow_stage", 100)
        set("varp6618_dt_ice_stage", 100)
        t.cheat("::dtmigrate", false)
        expect("allFour", {
            varb373_fdvw_subquest = 4, varb383_fd_shadowwarrior_quest = 5, varb385_fd_shadowwar_intro = 1, varb382_fd_icewarrior_subquest = 5,
            varb377_fd_icewarrior_dead = 1, varb381_fd_icewarrior_mumfree = 1, varb380_fd_icewarrior_dadfree = 1, varb376_fd_killed_firewarrior = 1,
        })
        t.finish(0)
    end,
}
