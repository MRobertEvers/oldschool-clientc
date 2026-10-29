-- The Corrupt Lizardman (Tale of the Righteous), the fight alone: tor_boss.rs2 against RSProx
-- index 4425 [1258..1377] (rev 241, the only recorded fight). `::torboss_arrive` (tor_boss.rs2)
-- leaves things where the entrance scene does at [1258] -- the player on 1172,9953 facing the
-- gate at stage 9, the boss on 1172,9948 held -- and `::torboss_engage` is the scene's hand-off
-- at [1266]; the scene itself is tor_cave.rs2's and taleoftherighteous.lua walks it.
--
-- Kit as the recorder's where it changes the boss's hits: Defence 1, no armour but an amulet of
-- glory, a rune scimitar, Prayer 43 (Attack 41, Strength 34 from [924]). Hitpoints 99, not 26,
-- and auto-retaliate off: this test stands unprayed at five tiles through six spits, where the
-- recorder prayed after two.
--
-- The hold: seven ticks with the boss beside a player 5 tiles away and nothing thrown (the
-- recording's [1258..1265]). The hand-off: the face on the engage tick, the first spit on the
-- next ([1266], [1267]). Leg 1, unprayed at five tiles: every attack is a spit, the first that
-- lands poisons ("You have been poisoned!", poison 15). Leg 2, Protect from Missiles and the
-- scimitar until it dies: the spits land 0, the melee comes in at its own rate. Then the
-- recording's death: stage 9 -> 10, Bones and three single Xerician fabric on its tile, no
-- message. The tick log is kept for the wire audit (ticklog_tells.py, rsprox_player_hits.py
-- --ours, rsprox_hit_causes.py).

local BOSS = "shayzienquest_lizardman_boss"
local BOSS_X, BOSS_Z = 1172, 9948

local function obj_rows(t, since, name)
    local target, id_result = t.player.by_symbol("obj", name)
    if target == nil then
        return nil, "no obj symbol " .. name .. " (" .. tostring(id_result) .. ")"
    end
    local id = target.id
    local result, rows = t.ticklog.rows({ kind = "obj_add", since = since })
    if result ~= "ok" then
        return nil, tostring(rows)
    end
    local out = {}
    for _, row in ipairs(rows) do
        if row.obj == id then
            out[#out + 1] = row
        end
    end
    return out
end

return {
    id = "tor_boss",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 41",
        "::setlevel strength 34",
        "::setlevel defence 1",
        "::setlevel hitpoints 99",
        "::setlevel prayer 43",
        "::give rune_scimitar 1",
        "::give amulet_of_glory_4 1",
        "::wield rune_scimitar",
        "::wield amulet_of_glory_4",
        "::setvar option_nodef 1",
    },

    run = function(t)
        local log_result, log_detail = t.ticklog.start()
        t.check("ticklog", log_result, tostring(log_detail))
        local since = 0
        local ranged = t.player.by_symbol("seq", "shay_lizard_warrior_attack_ranged")
        local melee = t.player.by_symbol("seq", "shay_lizard_warrior_attack_melee")

        t.ticklog.mark("leg.hold")
        t.exec("boss.arrive", t.cheat, "::torboss_arrive")
        t.exec("boss.present", t.npc.await_present, BOSS, 10, 5)
        t.ticks(7)
        do
            local _, anims = t.ticklog.rows({ kind = "npc_anim" })
            local _, hits = t.ticklog.rows({ kind = "hit_player" })
            local swings = 0
            for _, row in ipairs(anims or {}) do
                if (ranged and row.seq == ranged.id) or (melee and row.seq == melee.id) then
                    swings = swings + 1
                end
            end
            local named = ranged ~= nil and melee ~= nil
            t.check("boss.held", (named and swings == 0 and #(hits or {}) == 0) and "ok" or "hollow",
                string.format("7 ticks held: %d swing(s), %d splat(s) on the player (seqs named: %s)",
                    swings, #(hits or {}), tostring(named)))
        end

        t.ticklog.mark("leg.unprayed")
        local _, before = t.ticklog.rows({ kind = "projectile" })
        local thrown_before = #(before or {})
        t.exec("boss.engage", t.cheat, "::torboss_engage")
        t.ticks(2)
        t.exec("poisoned", t.msg.expect, "You have been poisoned!")
        do
            local _, faces = t.ticklog.rows({ kind = "npc_face" })
            local _, shots = t.ticklog.rows({ kind = "projectile" })
            local face_tick, spit_tick = nil, nil
            for _, row in ipairs(faces or {}) do
                if row.x == -1 and row.z ~= nil and row.z >= 0 and face_tick == nil then
                    face_tick = row.tick
                end
            end
            if shots and #shots > thrown_before then
                spit_tick = shots[thrown_before + 1].tick
            end
            t.check("boss.handoff", (face_tick ~= nil and spit_tick == face_tick + 1) and "ok" or "hollow",
                "face on the player at " .. tostring(face_tick) .. ", first spit at " .. tostring(spit_tick)
                .. " (recorded [1266], [1267])")
        end
        t.ticks(20)

        t.ticklog.mark("leg.prayed")
        t.exec("pray.missiles", t.prayer.set, "protectfrommissiles", true)
        t.exec("attack", t.player.attack, BOSS, 2, 30)
        t.exec("kill", t.npc.await_dead, BOSS, 400, 12)
        t.ticks(6)

        local stage_result, stage = t.var.server("varb6358_shayzienquest")
        t.check("stage.altar", (stage_result == "ok" and stage == 10) and "ok" or "hollow",
            "shayzienquest after the kill: " .. tostring(stage_result) .. " " .. tostring(stage))

        local fabric, why = obj_rows(t, since, "xeric_fabric")
        local fabric_ok = fabric ~= nil and #fabric == 3
        if fabric_ok then
            for _, row in ipairs(fabric) do
                if row.count ~= 1 or row.x ~= BOSS_X or row.z ~= BOSS_Z then
                    fabric_ok = false
                end
            end
        end
        t.check("drops.fabric", fabric_ok and "ok" or "hollow",
            "xeric_fabric obj_adds: " .. (fabric and tostring(#fabric) or tostring(why))
            .. " (three of count 1 on the boss's tile)")

        local bones, bones_why = obj_rows(t, since, "bones")
        t.check("drops.bones", (bones ~= nil and #bones == 1 and bones[1].count == 1) and "ok" or "hollow",
            "bones obj_adds: " .. (bones and tostring(#bones) or tostring(bones_why)))

        local said = t.msg.expect("corrupt lizardman collapses")
        t.check("death.silent", said ~= "ok" and "ok" or "hollow", "no death message (the recording sends none)")

        t.finish(0)
    end,
}
