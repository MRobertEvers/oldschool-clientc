-- Draynor Manor's two fights as the recordings play them, for the wire audit
-- (docs/rsprox-analysis/quests/ernest_the_chicken_combat.md):
--   * the undead trees on the path to the front doors (nasty_tree, m48_52):
--     a walk past (3109,3346), standing beside the trunk, then a diagonal.
--   * the closet skeleton (draynor_skeleton): a combat-80 player beside him
--     (left alone), then taking the rubber tube (attacked), the kill and the
--     respawn; then a combat-15 player in the room (hunted at one tile).
-- The tick log (build/quest_gate/<run>/ticklog.tsv, or --session's) is what
-- the audit diffs; the expects below only say each leg ran.
local function hold(t, n)
    t.ticks(n)
end

local function stats(t, att, str, def, hp)
    t.cheat("::setlevel attack " .. att, false)
    t.cheat("::setlevel strength " .. str, false)
    t.cheat("::setlevel defence " .. def, false)
    t.cheat("::setlevel hitpoints " .. hp, false)
end

return {
    id = "ernest_combat_rsprox",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::setlevel defence 30",
        "::setlevel hitpoints 30",
        "::goto 3109 3338 0",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(2)

        -- the trees -----------------------------------------------------
        t.ticklog.mark("leg.tree_walk")
        t.exec("tree.walk", t.player.walk_to, 3109, 3349, 40)
        hold(t, 4)
        t.ticklog.mark("leg.tree_stand")
        t.cheat("::goto 3109 3346 0", false)
        hold(t, 11)
        t.ticklog.mark("leg.tree_diagonal")
        t.cheat("::goto 3109 3347 0", false)
        hold(t, 6)
        t.ticklog.mark("leg.tree_back")
        t.cheat("::goto 3109 3346 0", false)
        hold(t, 12)
        t.cheat("::goto 3109 3352 0", false)
        hold(t, 3)

        -- the skeleton, combat 80: past his hunt, not past the tube -------
        stats(t, 70, 70, 70, 70)
        t.cheat("::give rune_scimitar", false)
        t.ticks(2)
        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")
        t.ticklog.mark("leg.skel_beside")
        t.cheat("::goto 3108 3368 0", false)
        hold(t, 8)
        -- The tube is taken from its own tile; an npc standing on it is a
        -- case the recordings never show, so wait for him to be elsewhere.
        for _ = 1, 30 do
            local r, _, rows = t.npc.tiles("draynor_skeleton", 16)
            local on_tube = false
            if r == "ok" and rows then
                for i = 1, #rows do
                    if rows[i].x == 3111 and rows[i].z == 3367 then
                        on_tube = true
                    end
                end
            end
            if not on_tube then
                break
            end
            t.ticks(1)
        end
        t.ticklog.mark("leg.skel_tube")
        local tube_result, tube_detail = t.player.click_obj("rubber_tube", 3)
        t.expect("skel.tube", tube_result == "ok" and "ok" or tube_result, tostring(tube_detail))
        hold(t, 9)
        t.ticklog.mark("leg.skel_kill")
        t.player.attack("draynor_skeleton", 2, 30)
        hold(t, 20)
        t.ticklog.mark("leg.skel_respawn")
        hold(t, 70)

        -- the skeleton, combat 15: hunted at one tile ---------------------
        stats(t, 1, 1, 30, 30)
        t.exec("unequip.scimitar", t.player.unequip, "rune_scimitar")
        t.ticklog.mark("leg.skel_hunt")
        t.cheat("::goto 3108 3367 0", false)
        hold(t, 2)
        t.exec("skel.walk1", t.player.walk_to, 3111, 3367, 10)
        hold(t, 6)
        t.exec("skel.walk2", t.player.walk_to, 3109, 3368, 10)
        hold(t, 6)
        t.exec("skel.walk3", t.player.walk_to, 3112, 3368, 10)
        hold(t, 6)
        t.exec("skel.walk4", t.player.walk_to, 3108, 3367, 10)
        hold(t, 8)
        -- his wander, untouched: leave past his leash so he drops the fight,
        -- then come back at combat 80, which is past his hunt.
        t.cheat("::goto 3109 3352 0", false)
        hold(t, 5)
        stats(t, 70, 70, 70, 70)
        t.ticklog.mark("leg.skel_wander")
        t.cheat("::goto 3108 3368 0", false)
        hold(t, 150)
        t.ticklog.mark("leg.done")
        t.cheat("::goto 3109 3352 0", false)
        hold(t, 3)
        t.expect("ernest_combat.run", "ok", "every leg ran; the audit reads the tick log")
    end,
}
