-- gemstone_crab: the Gemstone Crab (Tlati Rainforest world boss) through one
-- whole life, its shell and a crawl through the caves to its next nest.
-- Content: OSRS-Content/osrs-content/server/scripts/areas/area_varlamore/
-- scripts/gemstone_crab*.rs2; recordings and rules:
-- docs/rsprox-analysis/gemstone_crab.md (RSProx index 4509, 4508, 1095, 3258).
--
-- The world's crab is replaced at the north nest with a 70-tick life
-- (`::gemcrab_here 70`, gemstone_crab.rs2) so the burrow comes inside the run.
-- The player stands south of it, as the recorder did at 4508 [1107..], fights
-- it to the burrow, mines the shell (alone, so the top crusher and eligible),
-- takes the cave to wherever the next crab rises and checks the blockage line
-- at that nest's own cave once the crab is up there.
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_gemcrab_opt/torirsserver --scriptrun test/varlamore/gemstone_crab.lua \
--     --bots 1 --name gc0 --ticks 1200 --session <dir> --fixture test/fixtures/fresh_lumbridge.ini

local NORTH_CAVE = { 1278, 3167, 0 }
local LANDINGS = {
    [0] = { 1277, 3168 }, -- north
    [1] = { 1352, 3122 }, -- east
    [2] = { 1246, 3038 }, -- south
}
local CAVES = {
    [0] = { 1278, 3167, 0 },
    [1] = { 1350, 3123, 0 },
    [2] = { 1245, 3035, 0 },
}
local GEMS = {
    "uncut_opal", "uncut_jade", "uncut_red_topaz", "uncut_sapphire",
    "uncut_emerald", "uncut_ruby", "uncut_diamond", "uncut_dragonstone",
}

local function gem_count(t)
    local n = 0
    for _, g in ipairs(GEMS) do
        local _, c = t.inv.count(g)
        n = n + (c or 0)
    end
    return n
end

local function server_var(t, name)
    local r, v = t.var.server(name)
    if r ~= "ok" then
        return nil
    end
    return v
end

local function landed_at(t)
    local r, here = t.world.tile()
    if r ~= "ok" or type(here) ~= "table" then
        return nil
    end
    for site, tile in pairs(LANDINGS) do
        if here.x == tile[1] and here.z == tile[2] then
            return site
        end
    end
    return nil
end

local function run(t)
    t.ticks(3)
    t.check("gc.ticklog", t.ticklog.start() == "ok", "")
    t.ticklog.mark("leg.spawn")
    t.cheat("::gemcrab_here 70", true)
    t.ticks(2)
    t.check("gc.crab_up", t.npc.await_present("gemstone_crab", 12, 6) == "ok", "a crab at the north nest")

    -- The fight: the first target 2 ticks after the spawn, a strike every 7.
    t.ticklog.mark("leg.fight")
    local ar, ad = t.player.attack("gemstone_crab", 2, 10)
    t.check("gc.attack", ar == "ok", tostring(ad))
    local br, bd = t.msg.await("The gemstone crab burrows away, leaving a piece of its shell behind.", 110)
    t.check("gc.burrow_line", br == "ok", tostring(bd))
    t.ticklog.mark("leg.burrow")
    t.check("gc.crusher_line", t.msg.expect("The top crab crusher was") == "ok", "one player dealt damage")
    local er, ed = t.msg.await("You gained enough understanding of the crab to mine from its remains.", 6)
    t.check("gc.eligible_line", er == "ok", tostring(ed))
    t.ticks(1)
    t.check("gc.kill_count", server_var(t, "varp4825_total_gemstone_crab_kills") == 1,
        "kills " .. tostring(server_var(t, "varp4825_total_gemstone_crab_kills")))
    local pts = server_var(t, "varb5934_fossil_mine_temp_pts")
    t.check("gc.points", pts ~= nil and pts > 0, "points " .. tostring(pts))
    t.check("gc.shell_up", t.npc.await_present("gemstone_crab_remains", 12, 4) == "ok", "the shell laid")

    -- The shell: three gems two ticks apart, then nothing more.
    t.ticklog.mark("leg.mine")
    local before = gem_count(t)
    t.player.talk_to("gemstone_crab_remains", 1)
    t.ticks(8)
    local sr, sd = t.msg.expect("You swing your pick at the crab shell.")
    t.check("gc.swing_line", sr == "ok", tostring(sd))
    local gr, gd = t.msg.expect("from the crab shell.")
    t.check("gc.gem_line", gr == "ok", tostring(gd))
    t.check("gc.gems", gem_count(t) - before == 3, "gems " .. tostring(gem_count(t) - before))
    t.check("gc.reward_count", server_var(t, "varp4796_varlamore_gemstone_crab_reward_lifetime") == 1,
        "rewards " .. tostring(server_var(t, "varp4796_varlamore_gemstone_crab_reward_lifetime")))
    t.player.talk_to("gemstone_crab_remains", 1)
    t.ticks(3)
    local mr, md = t.msg.expect("You have mined all you can from the shell.")
    t.check("gc.mined_out", mr == "ok", tostring(md))

    -- The strikes: crab_boss_attack every 7 ticks for the whole life.
    local gr, gtext, gaps = t.ticklog.gaps(nil, "npc_anim", { seq = 12483 })
    local every7 = gr == "ok" and type(gaps) == "table" and #gaps >= 5
    if every7 then
        for _, g in ipairs(gaps) do
            if g ~= 7 then
                every7 = false
            end
        end
    end
    t.check("gc.strike_cadence", every7, tostring(gtext))

    -- The shell crumbles at R+150 and is gone at R+156, its blocking loc with
    -- it; the next crab has risen elsewhere at D+29 meanwhile.
    t.ticklog.mark("leg.crumble")
    local cr0, cd0 = t.npc.await_gone("gemstone_crab_remains", 12, 170)
    t.check("gc.shell_gone", cr0 == "ok", tostring(cd0))

    -- The cave: to the nest the next crab rose at.
    t.ticklog.mark("leg.cave")
    t.player.click_loc("cave_rock02_entrance01_gemstone", 1, { at = NORTH_CAVE })
    t.ticks(9)
    local site = landed_at(t)
    t.check("gc.cave_landing", site == 1 or site == 2, "landed at site " .. tostring(site))

    -- The next crab: D+29, at the nest the cave went to; its own cave is then
    -- blocked.
    t.ticklog.mark("leg.next")
    t.check("gc.next_crab", t.npc.await_present("gemstone_crab", 16, 40) == "ok", "the next crab")
    if site ~= nil then
        t.player.click_loc("cave_rock02_entrance01_gemstone", 1, { at = CAVES[site] })
        t.ticks(2)
        local cr, cd = t.chat.expect_text("You look inside the cave, but see a blockage further in.")
        t.check("gc.cave_blocked", cr == "ok", tostring(cd))
        t.chat.close()
        -- Ru Merald at this nest: the crab is with him.
        t.player.talk_to(site == 2 and "gemstone_crab_director" or "gemstone_crab_director_notimer", 3)
        t.ticks(2)
        local dr, dd = t.chat.expect_text("Look! The crab is right here with us!")
        t.check("gc.merald_here", dr == "ok", tostring(dd))
        t.chat.close()
    end
    t.ticks(2)
end

return {
    id = "gemstone_crab",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give bronze_pickaxe 1",
        "::goto 1273 3170 0",
    },
    run = run,
}
