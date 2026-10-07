-- _play_verzik_p3: Verzik Vitur, Normal trio, the FAST pace, P3 only (the
-- leader spends P1 and P2 with ::tobvzskip).  owner_verzik 2026-10-07: the
-- three-scythe team (_play_verzik.lua's kit) that kills her before her green
-- ball; the rotation rows report, p3.fast_before_ball is checked.  The
-- shared half is QD.raid.verzik_trio_run in raid_play_tob_verzik.lua.
local role = (QD_PARTY and QD_PARTY.role) or 1
-- The kit: _play_verzik.lua's party kit (::tobkit, oathplate, rancour, the
-- supplies; raid seam45/seam52 from Blert equipmentDeltas)
local kit = {
    "::clearinv", "::tobkit",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel prayer 99",
    "::setlevel magic 99", "::setlevel agility 99",
    "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
    "::give serpentine_helm_charged 1",
    "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 4", "::give br_4dose2combat 2",
    "::give anglerfish 14",
}
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
return {
    id = "_play_verzik_p3",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 400000,
    setup = kit,
    run = function(t)
        return t.raid.verzik_trio_run(t, { pace = "fast", start = "p3", cycle = false })
    end,
}
