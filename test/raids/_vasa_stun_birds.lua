-- _vasa_stun_birds: the raider stays drawn through Vasa's stun.
-- The owner, 2026-10-18: "The birds anim makes the character model disappear
-- ... in the vasa room." Vasa's special stuns the raiders it puts next to him
-- (cox_vasa.rs2 ~cox_vasa_execute_teleport: spotanim_pl(stunned_thieving, 124)
-- at S+3, the birds circling the head) after the teleport cast at S
-- (arceuus_necromancy_anim + arceuus_teleport_spotanim).
--
-- The cause was the cast, not the birds: arceuus_necromancy_anim (3865) ends
-- on the caster vanished and holds that frame (framestep 1), and Jagex stops it
-- with the teleport (`sequence id=-1`, RSProx 3285/820). Ours never stopped it,
-- so the raider landed invisible and the birds were all that was drawn
-- (client: "seq=3865 frame=14 verts=288 radius=10", a collapsed body).
--
-- CHECKED: on S+3 the server stops the raider's seq with the teleport and the
-- birds. Solo, ::god 1. Shots: one a tick from S to the explosion (S+12),
-- camera fixed - the raider is drawn under the birds from S+3.
local SPECIAL = 7409   -- vasa_stun_spawn

return {
    id = "_vasa_stun_birds",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
        "::setlevel ranged 99", "::setlevel agility 99",
        "::god 1",
    },
    run = function(t)
        t.ticklog.start()
        local er, at = t.raid.cox_test_enter("vasa", 1)
        t.check("birds.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
        if er ~= "ok" then t.finish(1) return end
        local vr, v = t.npc.nearest("raids_vasanistirio_dormant", 40)
        t.check("birds.vasa", vr == "ok", tostring(v))
        if vr ~= "ok" then t.finish(1) return end
        -- within 7 of his middle (SW + 2) wakes him
        local wr, wd = t.player.walk_to(v.x + 2, v.z - 3, 60)
        t.check("birds.walk", wr == "ok", tostring(wd))
        local S, shots = nil, 0
        for _ = 1, 80 do
            local _, now = t.tick()
            if S == nil then
                local nr, row = t.npc.nearest("raids_vasanistirio_walking", 40)
                if nr ~= "ok" then nr, row = t.npc.nearest("raids_vasanistirio_dormant", 40) end
                if nr == "ok" and row.anim_id == SPECIAL then S = now end
            end
            if S ~= nil then
                t.drive.camera(0, 383, 1100)
                t.shot(string.format("birds.S%+d", now - S))
                shots = shots + 1
                if now - S >= 12 then break end
            end
            t.ticks(1)
        end
        t.check("birds.special", S ~= nil, "vasa_stun_spawn seen")
        t.check("birds.shots", shots > 0, shots .. " shot(s)")
        -- the server's own log: the seq stop and the birds, both on S+3
        local stop, birds = false, false
        local ar, anims = t.ticklog.rows({ kind = "player_anim" })
        for _, row in ipairs(ar == "ok" and anims or {}) do
            if S and row.tick == S + 3 and row.seq == -1 then stop = true end
        end
        t.ticks(1)
        local sr, spots = t.ticklog.rows({ kind = "player_spotanim" })
        for _, row in ipairs(sr == "ok" and spots or {}) do
            if S and row.tick == S + 3 and row.spotanim == 245 then birds = true end
        end
        t.check("birds.cast_stopped", stop and birds,
            string.format("S+3: seq stop %s, stunned_thieving %s", tostring(stop), tostring(birds)))
        return "ok"
    end,
}
