-- gauntlet_cg_tells: a Corrupted Gauntlet run (op2 on the entrance; ::gauntlethm unlocks it) for the tick log (docs/rsprox-analysis/gauntlet/README.md).
-- The lobby (::gauntlet sets Song of the Elves, Bryn's talk and both entrance prompts), the
-- entrance, one Node door, a node gathered in the revealed room, the prep time cut short
-- (::gauntletboss), the Hunllef watched through its three health thirds (::gauntlethp steps it
-- down: 399 and 199 of 600) and killed, the completion and the chest. The player is kept alive
-- and does not attack, so every Hunllef action is its own. Read with tools/rsprox/ticklog_tells.py.
local function now(t) local _, k = t.tick() return k or -1 end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = 1
        t.ticks(step)
        t.cheat("::boost hitpoints 255 0", false)
        left = left - step
    end
end

local NODES = { "gauntlet_rock_hm", "gauntlet_tree_hm", "gauntlet_fibre_hm", "gauntlet_herb_hm", "gauntlet_pond_hm" }

return {
    id = "gauntlet_cg_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::gauntlethm",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::gauntletrun", false)
        t.ticks(2)
        t.expect("gauntlet.selftest", t.msg.expect("gauntletrun OK"))

        t.ticklog.mark("enter")
        t.exec("enter", t.player.click_loc, "gauntlet_entrance", 2)
        wait(t, 10)
        t.expect("gauntlet.entered", t.msg.expect("You enter the Gauntlet."))

        t.ticklog.mark("door")
        t.exec("door", t.player.click_loc, "prif_gauntlet_door_wall_unlit_01_hm", 1)
        wait(t, 6)
        t.expect("gauntlet.door", t.msg.expect("You light the nodes in the corridor to help guide the way."))

        t.ticklog.mark("gather")
        for _, sym in ipairs(NODES) do
            local result = t.world.loc_near(sym, 24)
            if result == "ok" then
                t.exec("gather." .. sym, t.player.click_loc, sym, 1)
                wait(t, 14)
                break
            end
        end

        t.ticklog.mark("boss")
        t.cheat("::gauntletboss", false)
        wait(t, 70)
        t.ticklog.mark("phase2")
        t.cheat("::gauntlethp 665", false)
        wait(t, 70)
        t.ticklog.mark("phase3")
        t.cheat("::gauntlethp 332", false)
        wait(t, 60)
        t.ticklog.mark("kill")
        t.cheat("::gauntlethp 0", false)
        wait(t, 16)
        t.expect("gauntlet.complete", t.msg.expect("Your Corrupted Gauntlet completion count is:"))
        t.expect("gauntlet.reward_line", t.msg.expect("Your reward awaits you in the nearby chest."))

        t.ticks(3)
        t.ticklog.mark("chest")
        t.exec("chest", t.player.click_loc, "gauntlet_chest", 1)
        wait(t, 6)
        t.expect("gauntlet.chest", t.msg.expect("You find some treasure in the chest!"))
        t.expect("gauntlet.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
