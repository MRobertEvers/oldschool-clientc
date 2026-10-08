-- _vzslowp3: Verzik Vitur, Normal trio, the SLOW pace (P3 only: the leader spends P1 and P2 with ::tobvzskip).
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
    -- the super combat potions: Blert's slow team boosted (0f9abe1a and
    -- 85b10c82: the recorder's Attack and Strength 118 through P1, P2 and P3);
    -- without them the whole room's P2 ran 413-456 ticks (Blert's team 243) and
    -- spent the packs before P3.  Their ball came in the enrage too (P3+187,
    -- enrage +136), and the ball is shared there.
    "::give br_4dose2combat 2",
    -- the enrage's special (W:992 "At this stage, players should dump all
    -- melee special attacks"; Blert 0f9abe1a's slow team: claws on the scythe
    -- seat, burning claws and a crystal halberd on the halberd seats --
    -- dragon claws stand in for all three).  The dump BEFORE the enrage is
    -- what this pace leaves out (the owner: "drop the spec dumping").
    "::give dragon_claws 1",
}) do kit[#kit + 1] = c end
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
-- the P3 weapon of roles 2 and 3, carried (the plan wields it at P3)
if role >= 2 then kit[#kit + 1] = "::give noxious_halberd 1" end
-- the food: every slot left (28: the leader's 11 above and the Dawnbringer;
-- roles 2 and 3 the 11 with the halberd, the ultor ring they take off and a
-- slot for the Dawnbringer they share).  A team restocks before Verzik at the
-- Theatre's supply chest (tob_chest.rs2: the Normal chest sells food, brews
-- and restores).
kit[#kit + 1] = "::give anglerfish " .. ((role == 1) and 16 or 14)
return {
    id = "_vzslowp3",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 400000,
    setup = kit,
    run = function(t)
        return t.raid.verzik_trio_run(t, { pace = "slow", start = "p3", cycle = true, unworn = { [2] = { "ultor_ring" }, [3] = { "ultor_ring" } } })
    end,
}
