-- _verzik_throne_rig: Verzik's throne exit draws every frame on the right rig.
-- owner_verzik_anim 2026-10-07.  The owner: "The animation of verzik leaving
-- the throne just corrupts verzik and it shows a corrupted model."
--
-- The cause was the client, not the content: tob_verzik.rs2 plays 8111 (the
-- dismount, rig 1796, priority 11) on 8370, and three ticks later retypes her
-- to 8371 (rig 1808) and sends 8112 (rig 1808, priority 11).  8112 had never
-- played, so the client judged it with the default priority 5 of an unloaded
-- seq and REFUSED it; 8111 (framestep=1: its last frame holds for 99 loops)
-- went on posing the 8371 model for the whole flight -- a torn, frozen mesh on
-- the dais.  src/world/world.c now parks a seq whose record is not resident
-- and judges it the cycle it lands (World_EntityResolvePendingAnimation), and
-- the server sends 65535 for the script's npc_anim(null) (the stop of 8112 one
-- tick later had been dropped).
--
-- CHECKED, every tick from the dismount to P2: the action track the client is
-- DRAWING on her (row.anim_id) is nothing, or a seq on the framemap of the
-- record she is (docs/minigames/theater_of_blood/sources/rig/verzik.tsv); and
-- the transition form drew 8112 at least once.  Solo, Normal, ::god 1, P1
-- skipped by ::tobvzskip.  Shots: one a tick (camera north over the dais).
local RIG = {
    -- 1796: verzik_initial / verzik_phase1
    [8369] = 1796, [8370] = 1796,
    -- 1808: verzik_phase1_to2_transition / verzik_phase2
    [8371] = 1808, [8372] = 1808,
}
local SEQ_RIG = {
    [8051] = 1796, [8107] = 1796, [8109] = 1796, [8110] = 1796, [8111] = 1796,
    [8112] = 1808, [8113] = 1808, [8114] = 1808, [8116] = 1808, [8117] = 1808, [8118] = 1808,
}
local FORMS = { "verzik_initial", "verzik_phase1", "verzik_phase1_to2_transition", "verzik_phase2" }
return {
    id = "_verzik_throne_rig",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
        "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
        "::god 1",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("throne.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("throne.enter", r == "ok", tostring(d))
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("throne.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("throne.begin", cr == "ok", tostring(cd))
        local function her()
            for _, s in ipairs(FORMS) do
                local fr, row = t.npc.nearest(s, 40)
                if fr == "ok" then return row end
            end
            return nil
        end
        local seen = false
        for _ = 1, 120 do
            if t.npc.nearest("verzik_phase1", 40) == "ok" then seen = true break end
            t.ticks(1)
        end
        t.check("throne.p1", seen, "verzik_phase1 up")
        local cam = { 0, 383, 1400 }
        local _, reply = t.cheat("::tobvzskip", true)
        t.check("throne.skip", true, tostring(reply))
        local bad, samples, drew_8112, reached_p2 = {}, 0, false, false
        for _ = 1, 22 do
            t.drive.camera(cam[1], cam[2], cam[3])
            local _, now = t.tick()
            local row = her()
            if row ~= nil then
                samples = samples + 1
                local a = row.anim_id or -1
                local rig = RIG[row.npc_id]
                if a >= 0 and (rig == nil or SEQ_RIG[a] ~= rig) then
                    bad[#bad + 1] = string.format("t%s npc %s (rig %s) drawing seq %d (rig %s) frame %s",
                        tostring(now), tostring(row.npc_id), tostring(rig), a, tostring(SEQ_RIG[a]), tostring(row.anim_frame))
                end
                if row.npc_id == 8371 and a == 8112 then drew_8112 = true end
                if row.npc_id == 8372 then reached_p2 = true end
                t.shot(string.format("throne.t%s.%s.seq%d", tostring(now), tostring(row.npc_id), a))
            end
            if reached_p2 then break end
            t.ticks(1)
        end
        t.check("throne.anim_on_rig", #bad == 0 and samples > 0,
            samples .. " tick(s) sampled; " .. (#bad == 0 and "every drawn action seq on her record's rig" or table.concat(bad, "; ")))
        t.check("throne.drew_8112", drew_8112, "the transition form 8371 drew 8112 (verzik_phase2_spawn)")
        t.check("throne.reached_p2", reached_p2, "verzik_phase2 (8372) reached")
        return "ok"
    end,
}
