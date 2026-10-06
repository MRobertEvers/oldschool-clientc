-- tob_verzik_normal: Verzik Vitur, Normal Mode, a party of THREE (leader writes every ledger row).
local role0 = (QD_PARTY and QD_PARTY.role) or 1
local kit = {
    "::clearinv",
    "::setlevel prayer 99", "::setlevel attack 99", "::setlevel strength 99",
    -- the P2 and P3 weapon and the ranged set: the content's best-in-slot ranged kit (twisted bow, masori, quiver)
    "::maxrange",
    -- the Dawnbringer: the P1 weapon, exempt from the shield's damage cap (wiki Strategies:871); the raid hands over one (tob_xarpus.rs2:1769) and the team passes it, here every raider holds one (finding)
    "::give verzik_special_weapon 1",
    "::give br_4dosepotionofsaradomin 3", "::give br_4dose2restore 4", "::give br_4doserangerspotion 1",
    "::give anglerfish 17",
}
return {
    id = "tob_verzik_normal",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = kit,
    run = function(t)
        local role = t.party.role()
        if role == 1 then
            local tl_ok, tl_detail = t.ticklog.start()
            t.check("verzik.ticklog", tl_ok == "ok", tostring(tl_detail))
        end
        local er, ed = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("verzik.enter", er == "ok", "p" .. role .. " " .. tostring(ed))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        local boss_slot, M, base_serial = nil, 0, 0
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3")
            local sr, st = t.raid.state()
            t.check("verzik.state", sr == "ok" and st.started == false, tostring(st and st.line))
        end
        t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
        local eqr, eqd = t.player.equip("verzik_special_weapon")
        t.check("p1.dawnbringer", eqr == "ok", "p" .. role .. " " .. tostring(eqd))
        t.expect("party.barrier.ready", t.party.barrier("ready", 300))
        if role == 1 then
            local tr, td = t.player.talk_to("verzik_initial", 1)
            t.check("verzik.talk", tr == "ok", tostring(td))
            local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
            t.check("verzik.begin", cr == "ok", tostring(cd))
            t.ticklog.mark("room start")
            local _, mk = t.ticklog.rows({ kind = "mark" })
            M = mk[#mk].tick
            base_serial = mk[#mk].serial
        end
        t.expect("party.barrier.started", t.party.barrier("started", 300))
        if role == 1 then
            local gr, grow = t.npc.state("verzik_phase1")
            t.check("verzik.geom_boss", true, tostring(gr) .. " " .. (gr == "ok" and t.npc.state_text(grow) or "") .. " size " .. tostring(gr == "ok" and grow.size))
            local lr, lc, rows = t.world.loc_copies("tob_dungeon_verzik_pillar", 40)
            local pl = {}
            if type(rows) == "table" then for _, r in ipairs(rows) do pl[#pl + 1] = r.x .. "," .. r.z end end
            t.check("verzik.geom_pillars", true, tostring(lr) .. " " .. tostring(lc) .. " " .. table.concat(pl, " "))
            local _, myt = t.world.tile()
            t.check("verzik.geom_me", true, myt.x .. "," .. myt.z)
        end
        local HIDE = { { 6426, 93 }, { 6426, 87 }, { 6426, 81 } }
        local AS = { 6428, 93 }
        local bolts, notes = 0, {}
        local ended = "loop"
        local specs = 0
        t.player.walk_to(AS[1], AS[2], 14)
        t.player.attack("verzik_phase1", 2, 1)
        for it = 1, 40 do
            local ar, ad, atick, aseq = t.npc.await_anim("verzik_phase1", 8109, 14)
            local _, hpn = t.skill.read("hitpoints")
            local hpv = hpn.current or hpn.level
            local br, boss = t.npc.state("verzik_phase1")
            if ar ~= "ok" then
                if br ~= "ok" then ended = "p1 gone" break end
                if hpv < 60 then t.player.eat("anglerfish") end
                t.player.attack("verzik_phase1", 2, 1)
            else
                bolts = bolts + 1
                local hi = math.min(3, math.floor((bolts - 1) / 3) + 1)
                local wr = t.player.walk_to(HIDE[hi][1], HIDE[hi][2], 3)
                local _, me1 = t.world.tile()
                notes[#notes + 1] = "w" .. bolts .. " tick=" .. tostring(atick) .. " walk " .. tostring(wr) .. " at " .. me1.x .. "," .. me1.z .. " hp=" .. hpv .. " bossratio=" .. tostring(br == "ok" and (boss.health_ratio .. "/" .. boss.health_scale))
                if hpv < 60 then t.player.eat("anglerfish") else t.ticks(1) end
                t.player.walk_to(AS[1], AS[2], 3)
                if bolts <= 2 then
                    t.ui.tab("combat")
                    local sr, sw = t.ui.widget("orbs:specbutton")
                    if sr == "ok" then t.ui.invoke(sw, 1) end
                    specs = specs + 1
                end
                t.player.attack("verzik_phase1", 2, 1)
            end
            if t.player.alive() ~= "ok" then ended = "dead" break end
        end
        t.check("verzik.p1_probe", true, "p" .. role .. " ended " .. ended .. "; " .. table.concat(notes, "; "))
        if role == 1 then
            t.ticks(12)
            local _, an = t.ticklog.rows({ kind = "npc_anim", since = base_serial })
            local wl = {}
            for _, r in ipairs(an) do
                if r.seq == 8109 then wl[#wl + 1] = r.tick - M end
            end
            local _, pr = t.ticklog.rows({ kind = "projectile", since = base_serial })
            local pl = {}
            for _, r in ipairs(pr) do
                if r.spotanim == 1580 or r.id == 1580 then pl[#pl + 1] = (r.tick - M) .. "->" .. tostring(r.dst_x) .. "," .. tostring(r.dst_z) end
            end
            local _, hp2 = t.ticklog.rows({ kind = "hit_player", since = base_serial })
            local hl = {}
            for _, r in ipairs(hp2) do hl[#hl + 1] = (r.tick - M) .. ":pid" .. tostring(r.pid) .. "=" .. tostring(r.damage) end
            local _, hn = t.ticklog.rows({ kind = "hit_npc", since = base_serial })
            local dealt = 0
            for _, r in ipairs(hn) do dealt = dealt + (r.damage or 0) end
            t.check("verzik.p1_log", true, "windups(rel M) " .. table.concat(wl, ",") .. " | bolts " .. table.concat(pl, ",") .. " | hits " .. table.concat(hl, ",") .. " | npc dealt " .. dealt)
        end
        t.cheat("::tobout")
        t.ticks(3)
        t.finish(0)
    end,
}
