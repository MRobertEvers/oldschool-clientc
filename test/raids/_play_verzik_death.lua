-- _play_verzik_death: Verzik Vitur, Normal trio, a raider DIES in P1 and the
-- other two finish the room.  owner_verzik 2026-10-07, the owner: "Check what
-- happens when a player dies in the raid, and ensure they are put in the
-- observation until the end of the raid."  Role 3 dies by ::die in P1 (the
-- whole death sequence, player/death.rs2), the leader spends P1 and P2 with
-- ::tobvzskip, the fast pair kills P3.  Checked: the death animation, caged
-- at every reading through P1, P2 and P3 until the room is won, the raid's
-- death counter, out of the cage after the win, P3 cleared by the two.
-- QD.raid.verzik_trio_run (cfg.kill_role) in raid_play_tob_verzik.lua.
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
    -- owner_verzik: the fast pace's enrage special (W:992 "dump all melee
    -- special attacks"; Blert's fast trios: dragon claws, CLAW_SPEC 1-2 a
    -- raider in P3), the pack's last free slot
    "::give dragon_claws 1",
}
if role == 1 then kit[#kit + 1] = "::give verzik_special_weapon 1" end
return {
    id = "_play_verzik_death",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 400000,
    setup = kit,
    run = function(t)
        return t.raid.verzik_trio_run(t, { pace = "fast", start = "p3", cycle = false, ball_check = false, kill_role = 3 })
    end,
}
