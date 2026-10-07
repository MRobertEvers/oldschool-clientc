-- _play_verzik_slow: Verzik Vitur, Normal trio, the SLOW pace (the whole room).
-- owner_verzik 2026-10-07.  The owner: "Add a slow Verzik script that kills
-- p3 slow enough so that the green orb appears. We are testing content to
-- ensure it's accurate so our raid scripts have to exercise that too."  The
-- team is the one Blert recorded reaching her green ball with no death
-- (build/blert/verzik 0f9abe1a, P3 200 ticks, the ball at P3+187; and
-- 85b10c82): the leader on the scythe, the other two on the NOXIOUS HALBERD
-- in every phase (raid_play_tob_verzik.lua header, QD.raid.verzik_pace).
-- Nothing idles: the raiders fight and answer every mechanic.  CHECKED, one
-- row each: the crabs dealt with, the webs dodged (no snap), the yellows
-- stood on (every raider protected, one a pool), the green ball thrown and
-- landed with nobody dead, the four in rotation order; no deaths; P3 cleared.
-- The shared half (entry, the leader's talk, the P3 start, the rows) is
-- QD.raid.verzik_trio_run in raid_play_tob_verzik.lua.
local role = (QD_PARTY and QD_PARTY.role) or 1
-- The kit: _play_verzik.lua's party kit (::tobkit, oathplate, rancour, the
-- supplies; raid seam45/seam52 from Blert equipmentDeltas), with roles 2 and
-- 3 in the reference team's own worn gear (Blert 0f9abe1a / 85b10c82
-- equipmentDeltas, ids by configs/all.obj.compack): role 2 (rassexdd) the
-- noxious halberd (carried, wielded at P3), oathplate helm, chest and legs, no ring; role 3
-- (valveuni) the noxious halberd, Neitiznot faceguard, fire cape, Bandos
-- chestplate and tassets, no ring.  The rest is the kit's (rancour,
-- ferocious gloves; the insulated boots of the kit for P2's zap).  The
-- swapped-out pieces and the scythe are cleared before the supplies go in.
local kit = { "::clearinv", "::tobkit" }
local worn = {
    [2] = { "oathplate_helm", "oathplate_chest", "oathplate_legs" },
    [3] = { "neitiznot_faceguard", "tzhaar_cape_fire", "bandos_chestplate", "bandos_skirt" },
}
for _, item in ipairs(worn[role] or {}) do
    kit[#kit + 1] = "::give " .. item .. " 1"
    kit[#kit + 1] = "::wield " .. item
end
-- (the insulated boots before the clear, so the treads they replace go too)
for _, c in ipairs({
    "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
    "::clearinv",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel prayer 99",
    "::setlevel magic 99", "::setlevel agility 99",
    "::give serpentine_helm_charged 1",
    "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 4",
}) do kit[#kit + 1] = c end
-- the food: every slot the pack has left (a slow P3 runs 230-370 ticks; the
-- whole-room survey of 2026-10-07 had a member eat its 14th fish in P2 and die
-- at 15 hitpoints in P3 with only potions left).  A team restocks before
-- Verzik at the Theatre's supply chest (tob_chest.rs2; the Normal chest sells
-- food, brews and restores): 16 for every seat, the pack full with the
-- Dawnbringer in it (roles 2 and 3: the two slots their super combat potions
-- do not take, less the slot the ultor ring takes off).
kit[#kit + 1] = "::give anglerfish 16"
-- the BURST dropped (the coordinator, 2026-10-07: "adjust pace by dropping
-- more burst, never by idling"): the reference team dumped specials in P3
-- (Blert 0f9abe1a: claws x2, burning claws x3, crystal halberd x2) and this
-- team spends none there; and roles 2 and 3 carry no super combat potion
-- (Blert records only the recorder's boosted levels, so the members'
-- boosts are not known): the leader alone re-boosts.  The plan's melee-stats
-- rule finds no potion and drinks none.
if role == 1 then kit[#kit + 1] = "::give br_4dose2combat 2" end
-- the P3 weapon of roles 2 and 3, carried (the plan wields it at P3)
if role >= 2 then kit[#kit + 1] = "::give noxious_halberd 1" end
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
return {
    id = "_play_verzik_slow",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 400000,
    setup = kit,
    run = function(t)
        return t.raid.verzik_trio_run(t, { pace = "slow", start = "room", cycle = true, unworn = { [2] = { "ultor_ring" }, [3] = { "ultor_ring" } } })
    end,
}
