-- _play_verzik: Verzik Vitur played through the PLAY LIBRARY (t.raid.play and
-- raid_play_tob_verzik.lua, raid seam30 play_tob_verzik).  An underscore
-- harness, not a kept room: the setup (copied from test/raids/tob_verzik.lua
-- :6-31) and the entry are the kept room's; the fight is one call; everything
-- after it reads the tick log.  The kept room's technique and room-complete
-- rows are COPIED UNCHANGED (each block names its kept lines), computed here
-- from the tick log after the fight instead of inside the kept room's loop.
-- Not copied: tech.p3_prayer_at_landing (K :2728).  It needs unprayed P3 autos
-- (p3.um_max, K :2369-2383: the kept room dropped its protections on purpose
-- to measure the read), which a plan that prays every auto never takes; the
-- plan's row play.p3_prayer_on_hit asserts the ruling it plays by instead.
-- raid seam34v play_tob_verzik_normal: THE NORMAL TRIO (run.py --party 3,
-- seed_survey.py _play_verzik --party 3).  The solo table below is Entry and
-- is unchanged; a party run of this id is Normal, three raiders, the plan's
-- `normal` mode (PLAY_NOTES.md "Verzik, Normal trio").  No `party` field: a
-- declared party would make every run of this id a party run (run.py
-- PARTY_RE), and the Entry solo must stay one.
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
-- The kit (raid seam45 play_tob_verzik_melee_follows_blert: MELEE, as every
-- recorded Normal trio plays her -- Blert verzik_normal_3.json, 20 death-free
-- rooms: the scythe in P2 and P3 in 16-18 of 20 rooms per role, melee 83-92%
-- of attacks).  Worn: ::tobkit (::maxmelee's charged scythe, the oathplate: W:891 "weapon1 =
-- Scythe of vitur"; W:881 "players should use their melee weapons"),
-- insulated boots (the zap 48 -> 25, V p2_zap_max; Entry_Mode.wikitext:94).
-- Carried: a charged serpentine helm (the Athanatos "has to be hit with
-- poison or venom", W:923); four brews, four super restores and two super
-- combat potions (a brew drains the Attack and Strength the scythe swings
-- with: the plan restores and re-boosts them), fourteen anglerfish.  THE DAWNBRINGER: one per raid, taken from the
-- skeleton after Xarpus (tob_xarpus.rs2 [oploc1,tob_skeleton_with_weapon],
-- [proc,tob_dawnbringer_take]: the searcher's backpack); the room before
-- Verzik is not played here, so the first raider is handed it as that search
-- would (::give, one copy, p1 only), and the trio shares it by drop and take
-- (W:875 "drop the Dawnbringer for the next player (in orb order)").  p2 and
-- p3 keep one backpack slot free for it.
local party_kit = {
    -- (raid seam52 melee_damage_per_swing: ::tobkit = ::maxmelee, then the
    -- recorded raiders' radiant oathplate body and legs and the amulet of
    -- rancour -- Blert equipmentDeltas, build/blert/verzik, 66/81 rooms;
    -- cheat_max_gear.rs2; the wiki's swing on her P2 28.9, P3 34.7)
    "::clearinv", "::tobkit",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel prayer 99",
    "::setlevel magic 99", "::setlevel agility 99",
    "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
    "::give serpentine_helm_charged 1",
    "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 4", "::give br_4dose2combat 2",
    "::give anglerfish 14",
}
if role == 1 then party_kit[#party_kit + 1] = "::give verzik_special_weapon 1" end

local function party_run(t)
    local mode = "normal"
    if role == 1 then
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("verzik.ticklog", tl_ok == "ok", tostring(tl_detail))
    end
    local r, d = t.raid.enter("tob", "verzik", { mode = mode })
    t.check("verzik.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    -- (the scythe is worn: ::maxmelee wields it charged)
    -- W:871 "All players must have Protect from Magic on before starting the fight."
    t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
    if size > 1 then t.expect("party.barrier.ready", t.party.barrier("ready", 300)) end
    local M = nil
    local boss_slot = nil
    if role == 1 then
        -- her row before the talk retypes it (the Entry entry's order)
        local _, brow = t.npc.nearest("verzik_initial", 30)
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("verzik.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        t.ticklog.mark("room start")
        local slot_res, bs = t.ticklog.slot(brow)
        boss_slot = bs
        t.check("verzik.slot", slot_res == "ok", "server slot " .. tostring(bs))
        local _, mk = t.ticklog.rows({ kind = "mark" })
        M = mk[#mk].tick
    end
    if size > 1 then t.expect("party.barrier.started", t.party.barrier("started", 900)) end

    -- THE FIGHT: the library and the room's plan, nothing else
    local result, detail, rec = t.raid.play("tob_verzik", { mode = mode, max_ticks = 2400 })
    t.check("play.fight", result == "ok", "p" .. role .. " " .. tostring(detail))
    local vz = rec.vz or {}
    local dw = vz.dawn or { specs = {}, refused = {} }
    local hides = {}
    for _, h in ipairs(vz.hides or {}) do hides[#hides + 1] = "L" .. h.L .. "@" .. h.from end
    -- every raider spends both specials on the shield (the owner: "share it
    -- using their special attack"; W:875), seen as the 350 each spends
    t.check("play.dawn", #dw.specs >= 2,
        "p" .. role .. " Dawnbringer specials seen spent at t" .. table.concat(dw.specs, ",t") .. "; took t" .. tostring(dw.took)
        .. ", dropped t" .. tostring(dw.dropped) .. " (" .. tostring(dw.drop_result) .. "); floor appearances " .. tostring(dw.appear)
        .. "; refusals " .. table.concat(dw.refused or {}, " | ") .. "; hides " .. table.concat(hides, " "))
    t.check("play.measure_raider", true, string.format("p%d: %s; eats %d, drinks %d, swings %d, add presses %s, kites %s, tornado runs %s",
        role, tostring(detail), #rec.eats, #rec.drinks, #rec.swings, tostring(vz.add_presses), tostring(vz.kites), tostring(vz.tornado_runs)))
    -- raid seam45: the melee clock as the plan read it (P2 and P3)
    local m2, m3 = vz.m2 or {}, vz.m3 or {}
    -- (raid seam51: the late-seen attacks the clocks re-dated, and P2's late ticks)
    t.check("play.melee_clock", true, string.format("p%d P2 outs %s late %s [%s] snaps %s waits %s add presses %s kites %s; P3 outs %s late %s snaps %s holds %s; P2 log %s",
        role, tostring(m2.outs), tostring(m2.late), table.concat(m2.late_ticks or {}, " "), tostring(vz.m2_snaps or 0), tostring(m2.waits), tostring(m2.add_presses), tostring(m2.kites),
        tostring(m3.outs), tostring(m3.late), tostring(vz.m3_snaps or 0), tostring(m3.holds), table.concat(m2.log or {}, " ")) .. "; P3 dodges " .. tostring(m3.dodges) .. "; enrage log " .. table.concat(m3.log or {}, " ") .. "; webs log " .. table.concat(m3.log2 or {}, " ")
        -- (raid seam52: each summon's red policy: tick, K kill / L last, her bar %)
        .. "; red policy " .. table.concat(m2.red_policy or {}, " ") .. "; P3 apart " .. tostring(m3.apart or 0) .. " bound " .. tostring(m3.bound or 0) .. " bound walks " .. tostring(m3.bound_walks or 0))
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 9000))
        t.finish(0)
        return
    end

    -- THE LEADER'S TICK LOG: every raider's rows are in it
    t.ticks(1)
    local death_tick = rec.death_tick
    local R = {}
    local kinds = { anim = "npc_anim", proj = "projectile", hitp = "hit_player", hitn = "hit_npc", retype = "npc_retype" }
    for k, kind in pairs(kinds) do
        local _, rows = t.ticklog.rows({ kind = kind })
        R[k] = rows or {}
        t.ticks(1)
    end
    local ptile = {}
    local _, ptiles = t.ticklog.rows({ kind = "player_tile" })
    for i, row in ipairs(ptiles or {}) do
        ptile[row.pid] = ptile[row.pid] or {}
        ptile[row.pid][row.tick] = row
        if i % 1500 == 0 then t.ticks(1) end
    end
    t.ticks(1)
    -- her forms, by the retype rows of her slot (V verzik.av.npc_form_normal)
    local form_at = {}
    for _, rw in ipairs(R.retype) do
        if rw.to_type ~= nil and rw.to_type >= 8369 and rw.to_type <= 8375 and form_at[rw.to_type] == nil then form_at[rw.to_type] = rw.tick end
    end
    local p1s, p2s, p3s = form_at[8370] or M, form_at[8372], form_at[8374]
    t.ticks(1)
    -- damage per raider, bombs on a raider's held tile, P1 bolts aimed at a raider
    local taken, worst = {}, {}
    for _, h in ipairs(R.hitp) do
        taken[h.pid] = (taken[h.pid] or 0) + h.damage
        if h.damage > (worst[h.pid] or 0) then worst[h.pid] = h.damage end
    end
    t.ticks(1)
    local bombs, bombs_on, bolts_player, bolts_pillar, balls = 0, {}, 0, 0, 0
    for _, p in ipairs(R.proj) do
        if p.spotanim == 1583 then
            bombs = bombs + 1
            local land = p.tick + math.floor(((p.end_cycle or 0) - (p.start_cycle or 0)) / 30)
            for pid, tl in pairs(ptile) do
                local at = tl[land - 1]
                if at ~= nil and at.x == p.dst_x and at.z == p.dst_z then bombs_on[pid] = (bombs_on[pid] or 0) + 1 end
            end
        elseif p.spotanim == 1580 then
            if p.target == -1 then bolts_player = bolts_player + 1 else bolts_pillar = bolts_pillar + 1 end
        elseif p.spotanim == 1598 then
            balls = balls + 1
        end
    end
    -- P1: the Dawnbringer's splats on the shield (anything over the melee cap
    -- of 10 is the Dawnbringer: tob_damage.rs2 ~tob_verzik_p1_cap), and the
    -- specials among them (75-150, W:875)
    t.ticks(1)
    local dawn_hits, dawn_specs, dawn_dealt, p1_dealt = 0, 0, 0, 0
    for _, h in ipairs(R.hitn) do
        if h.slot == boss_slot and p1s ~= nil and h.tick >= p1s and (form_at[8371] == nil or h.tick <= form_at[8371]) then
            p1_dealt = p1_dealt + h.damage
            if h.damage > 10 then dawn_hits = dawn_hits + 1 dawn_dealt = dawn_dealt + h.damage end
            if h.damage >= 75 then dawn_specs = dawn_specs + 1 end
        end
    end
    -- the near row's pillars are hidden behind (W:883 "hide behind one pillar
    -- together"; W:885); past them the plan tanks (W:887), so bolts at a
    -- raider are reported, not asserted
    t.check("tech.p1_pillar_cover", bolts_pillar >= 4,
        "P1 bolts aimed at a pillar (a raider hid): " .. bolts_pillar .. "; aimed at a raider (tanked under Protect from Magic past the near row): " .. bolts_player)
    t.check("tech.dawn_shared", dawn_specs >= 4,
        "P1 splats of 75 or more (Dawnbringer specials, W:875 75-150): " .. dawn_specs .. "; Dawnbringer splats " .. dawn_hits .. " for " .. dawn_dealt .. " of P1's " .. p1_dealt)
    local bomb_txt, dmg_txt = {}, {}
    for pid, n in pairs(taken) do dmg_txt[#dmg_txt + 1] = "pid" .. pid .. " " .. n .. " (max " .. worst[pid] .. ")" end
    for pid, n in pairs(bombs_on) do bomb_txt[#bomb_txt + 1] = "pid" .. pid .. " " .. n end
    table.sort(dmg_txt)
    table.sort(bomb_txt)
    t.check("play.p2_bombs_dodged", bombs >= 1,
        bombs .. " urnbombs thrown; on a raider's held tile the tick before landing: " .. (#bomb_txt > 0 and table.concat(bomb_txt, ", ") or "none"))
    -- THE ROOM COMPLETE: the raid's death counter (read-only ::tobjail)
    if death_tick ~= nil then
        t.ticks(10)
        t.cheat("::tobjail")
        t.ticks(2)
        local _, jl = t.msg.last(40)
        local jail_line = ""
        for _, m in ipairs(jl) do
            local jt = tostring(m.text)
            if jail_line == "" and jt:find("tobjail jailed=", 1, true) then jail_line = jt end
        end
        t.check("verzik.deathless", jail_line:find("jailed=0 died_in=0 deaths=0", 1, true) ~= nil, "party: " .. jail_line)
    end
    local forms = {}
    for _, f in ipairs(vz.forms or {}) do forms[#forms + 1] = f.symbol .. "@" .. f.tick end
    t.check("play.measure", true, string.format("room %s ticks (mark %s; P1 t%s, P2 t%s, P3 t%s, death t%s); P1 %s ticks, P2 %s, P3 %s; taken %s; bolts at raiders %d, at pillars %d; balls %d; forms %s; summons %s",
        tostring(death_tick and M and (death_tick - M)), tostring(M), tostring(p1s), tostring(p2s), tostring(p3s), tostring(death_tick),
        tostring(form_at[8371] and p1s and (form_at[8371] - p1s)), tostring(p2s and form_at[8373] and (form_at[8373] - p2s)), tostring(p3s and death_tick and (death_tick - p3s)),
        table.concat(dmg_txt, ", "), bolts_player, bolts_pillar, balls, table.concat(forms, " "), tostring(vz.summons)))
    t.expect("party.barrier.done", t.party.barrier("done", 9000))
    t.finish(0)
end

return {
    id = "_play_verzik",
    fixture = "fresh_lumbridge.ini",
    -- raid seam34v: a Normal trio runs about 1,000 room ticks (one cap for both: run.py reads one frame cap per file)
    max_frames = 400000,
    setup = (size > 1) and party_kit or {
        "::clearinv",
        -- combat stats an Entry-mode Verzik player has
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel ranged 99", "::setlevel magic 99",
        -- no armour is carried: the backpack has 28 slots and the rest is food; the P1 melee is bare fists, a 4-tick punch
        -- the Dawnbringer: the raid hands it over at the Xarpus exit (tob_xarpus.rs2); the room before Verzik is not played here
        "::give armadyl_helmet 1", "::wield armadyl_helmet", "::give armadyl_chestplate 1", "::wield armadyl_chestplate",
        "::give armadyl_skirt 1", "::wield armadyl_skirt",
        -- insulated boots take 40 percent off the P2 lightning ball and the Entry page makes them mandatory solo (Entry_Mode.wikitext:94); they need Slayer 37 and are worn, so they take no backpack slot
        "::setlevel slayer 37", "::give slayer_boots 1", "::wield slayer_boots",
        -- the cape slot is worn too (a raider does not leave it bare); its defence bonus costs no backpack slot
        "::give infernal_cape 1", "::wield infernal_cape",
        "::give verzik_special_weapon 1",
        -- the P2 and P3 weapon and its ammunition (swapped to once the shield is down)
        "::give twisted_bow 1", "::give dragon_arrow 500", "::wield dragon_arrow",
        -- a brew and two prayer restores: Protect from Missiles/Magic and Rigour drain 99 prayer points by about tick 420 and a restore dose is worth more than a fish
        "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 1", "::give br_4dose2restore 1",
        -- food for the fight: anglerfish heal 31 at 99 hitpoints (28 slots minus the five items above)
        "::give anglerfish 16", "::give br_4doserangerspotion 1", "::give staff_of_air 1", "::give death_rune 12",
        -- a charged serpentine helm makes a hit poisonous (tob_damage.rs2 ~tob_hit_is_poisonous): worn for one shot at the second Athanatos only
        "::give serpentine_helm_charged 1",
        -- the ranged set a Theatre raider shoots in for the P2 and P3 bow: worn at the P1 -> P2 transition, not before (the Dawnbringer wants the bare magic bonus)
        -- (worn from the start so the three backpack slots hold food: the fight is food-limited)
        -- ranging potion for the bow (drunk at the same transition)
    },
    run = function(t)
        if size > 1 then return party_run(t) end
        -- THE ENTRY, as the kept room does it (K :34-43, :75-86; its read-only
        -- ::tobboss / ::tobpillars readouts are not part of the entry)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("verzik.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "entry" })
        t.check("verzik.enter", r == "ok", tostring(d))
        local sr, st = t.raid.state()
        t.check("verzik.state", sr == "ok" and st.started == false, tostring(st and st.line))
        local boss_found, boss = t.npc.nearest("verzik_initial_story", 30)
        t.check("verzik.boss_present", boss_found == "ok", "boss row")
        t.exec("p1.prayer", t.prayer.set, "protectfrommagic", true)
        local tr, td = t.player.talk_to("verzik_initial_story", 1)
        t.check("verzik.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        t.ticklog.mark("room start")
        local slot_res, boss_slot = t.ticklog.slot(boss)
        t.check("verzik.slot", slot_res == "ok", "server slot " .. tostring(boss_slot))
        local _, mk = t.ticklog.rows({ kind = "mark" })
        local M = mk[#mk].tick

        -- THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_verzik", { mode = "entry", max_ticks = 1500 })
        t.check("play.fight", result == "ok", tostring(detail))
        local death_tick = rec.death_tick
        local vz = rec.vz or {}

        -- THE TICK LOG, read after the fight (the kinds the kept room's loop read, K :92-93)
        t.ticks(1)
        local R = {}
        local kinds = { anim = "npc_anim", proj = "projectile", hitp = "hit_player", hitn = "hit_npc", retype = "npc_retype" }
        for k, kind in pairs(kinds) do
            local _, rows = t.ticklog.rows({ kind = kind })
            R[k] = rows or {}
            t.ticks(1)
        end
        local ptile_at = {}
        local _, ptiles = t.ticklog.rows({ kind = "player_tile" })
        for _, row in ipairs(ptiles or {}) do ptile_at[row.tick] = row end
        t.ticks(1)
        local HIDE = { { 6426, 93 }, { 6426, 87 }, { 6426, 81 } }
        local pillar_retypes = {}
        for _, row in ipairs(R.retype) do
            if row.slot ~= boss_slot and row.to_type == 8377 and row.from_type == 8379 then
                pillar_retypes[#pillar_retypes + 1] = row
            end
        end
        local out_rows = {}

        -- P1 (K :1181-1186, :1213-1240, :1288-1302, :1356-1360, unchanged)
        local w1, bolts = {}, {}
        for _, r in ipairs(R.anim) do
            if r.slot == boss_slot and r.seq == 8109 then w1[#w1 + 1] = r end
        end
        for _, r in ipairs(R.proj) do
            if r.spotanim == 1580 then bolts[#bolts + 1] = r end
        end
                -- bolts aimed at the player (target -1) and their hit rows; bolts aimed at a pillar (target 0)
                local imp, imp_seen, bolt_hits, bolt_max, n_player_bolts = {}, {}, {}, 0, 0
                local verdict_n, verdict_ok, cover_n = 0, 0, 0
                local collapse_ticks = {}
                for _, pr in ipairs(pillar_retypes) do collapse_ticks[pr.tick] = true end
                for _, b in ipairs(bolts) do
                    if b.target == -1 then
                        n_player_bolts = n_player_bolts + 1
                        for _, h in ipairs(R.hitp) do
                            if h.npc_slot == -1 and h.npc_type == -1 and h.tick >= b.tick + 1 and h.tick <= b.tick + 6 and not collapse_ticks[h.tick] then
                                local d = h.tick - b.tick
                                if not imp_seen[d] then imp_seen[d] = true imp[#imp + 1] = d end
                                bolt_hits[#bolt_hits + 1] = h.damage
                                if h.damage > bolt_max then bolt_max = h.damage end
                                break
                            end
                        end
                    else
                        local at_launch, at_impact = ptile_at[b.tick - 1], ptile_at[b.tick + 3]
                        if at_launch and at_impact and at_launch.x == HIDE[1][1] and at_launch.z == HIDE[1][2] then
                            cover_n = cover_n + 1
                            local hit = false
                            for _, h in ipairs(R.hitp) do
                                if h.tick >= b.tick + 1 and h.tick <= b.tick + 7 and not collapse_ticks[h.tick] then hit = true end
                            end
                            if at_impact.x ~= HIDE[1][1] or at_impact.z ~= HIDE[1][2] then
                                verdict_n = verdict_n + 1
                                if not hit then verdict_ok = verdict_ok + 1 end
                            end
                        end
                    end
                end
                -- fist and arrow hits against the cap
                local melee_max, ranged_max, melee_n, ranged_n = 0, 0, 0, 0
                local magic_max, magic_n = 0, 0
                local _, cap_anims_rows = t.ticklog.rows({ kind = "player_anim" })
                local cap_anims = {}
                for _, pa in ipairs(cap_anims_rows or {}) do cap_anims[#cap_anims + 1] = { tick = pa.tick, seq = tonumber(pa.seq or pa.a or pa.b) } end
                local cap_diag = ""
                for _, h in ipairs(R.hitn) do
                    if h.slot == boss_slot and h.type == 10831 then
                        -- the hit belongs to the swing that launched it: fists land +1, the Dawnbringer +4, a bow or a spell the latest 426 / 1162 within 2..6 ticks
                        local wp = "other"
                        local anim_at = {}
                        for _, pa in ipairs(cap_anims) do anim_at[pa.tick] = pa.seq end
                        if anim_at[h.tick - 4] == 1167 then wp = "dawnbringer"
                        elseif anim_at[h.tick - 1] == 422 then wp = "fists"
                        else
                            for back = 2, 6 do
                                local sq = anim_at[h.tick - back]
                                if wp == "other" and sq == 426 then wp = "twisted_bow" end
                                if wp == "other" and sq == 1162 then wp = "staff_of_air" end
                            end
                        end
                        if wp == "fists" then melee_n = melee_n + 1 if h.damage > melee_max then melee_max = h.damage end end
                        if wp == "twisted_bow" then ranged_n = ranged_n + 1 if h.damage > ranged_max then ranged_max = h.damage end cap_diag = cap_diag .. h.tick .. ":" .. h.damage .. " " end
                        if wp == "staff_of_air" then magic_n = magic_n + 1 if h.damage > magic_max then magic_max = h.damage end end
                    end
                end
                out_rows[#out_rows + 1] = { "tech.p1_cap_melee_ranged", melee_max == 10 and ranged_max == 3,
                    "largest shield hit with bare fists " .. melee_max .. " over " .. melee_n .. " hits (cap 10), with the Twisted bow " .. ranged_max .. " over " .. ranged_n .. " hits (cap 3, bow hits tick:damage " .. cap_diag .. "); wind bolt hits ".. magic_n .. " with the largest " .. magic_max .. " (cap 3)" }
                out_rows[#out_rows + 1] = { "tech.p1_pillar_cover", cover_n >= 1 and verdict_ok == verdict_n,
                    "bolts aimed at the pillar while I stood behind it: " .. cover_n .. ", hit_player rows on me from them: 0; bolts aimed at me while exposed: " .. n_player_bolts }
                out_rows[#out_rows + 1] = { "tech.p1_prayer_halves", #bolt_hits >= 1 and bolt_max <= 30,
                    "bolt hits on me under Protect from Magic: " .. table.concat(bolt_hits, ",") .. " (unprayed ceiling 60, halved 30)" }
        -- P2 (K :625-631, :911-937, :982-983, unchanged)
                local A2, heals2 = {}, {}
                for _, r in ipairs(R.anim) do
                    if r.slot == boss_slot and r.type == 10833 then
                        if r.seq == 8114 or r.seq == 8116 then A2[#A2 + 1] = r end
                        if r.seq == 8117 then heals2[#heals2 + 1] = r end
                    end
                end
                -- every P2 attack classed by where I stood at the end of the tick before it (distance to her 3x3 at 6431..6433, 89..91)
                local cls = { adj = { 0, 0 }, inside = { 0, 0 }, far = { 0, 0 }, on = { 0, 0 } }
                local first_slam, first_stomp, scythe
                for _, a in ipairs(A2) do
                    local p0, p1 = ptile_at[a.tick - 1], ptile_at[a.tick]
                    if p0 and p1 then
                        local d0 = math.max(math.max(6431 - p0.x, p0.x - 6433, 0), math.max(89 - p0.z, p0.z - 91, 0))
                        local d1 = math.max(math.max(6431 - p1.x, p1.x - 6433, 0), math.max(89 - p1.z, p1.z - 91, 0))
                        local slam = (a.seq == 8116) and 1 or 0
                        if d0 == 1 then cls.adj[1] = cls.adj[1] + 1 cls.adj[2] = cls.adj[2] + slam
                            if slam == 1 and not first_slam then first_slam = a end
                        elseif d0 == 0 then cls.inside[1] = cls.inside[1] + 1 cls.inside[2] = cls.inside[2] + slam
                            if slam == 1 and not first_stomp then first_stomp = a end
                        else
                            cls.far[1] = cls.far[1] + 1 cls.far[2] = cls.far[2] + slam
                            if d1 == 1 then cls.on[1] = cls.on[1] + 1 cls.on[2] = cls.on[2] + slam end
                            -- a scythe walk: adjacent on T, T+1 and T+2, off again on T+3, no slam on T
                            local q1, q2, q3 = ptile_at[a.tick + 1], ptile_at[a.tick + 2], ptile_at[a.tick + 3]
                            if d1 == 1 and slam == 0 and q1 and q2 and q3 and not scythe then
                                local e1d = math.max(math.max(6431 - q1.x, q1.x - 6433, 0), math.max(89 - q1.z, q1.z - 91, 0))
                                local e2d = math.max(math.max(6431 - q2.x, q2.x - 6433, 0), math.max(89 - q2.z, q2.z - 91, 0))
                                local e3d = math.max(math.max(6431 - q3.x, q3.x - 6433, 0), math.max(89 - q3.z, q3.z - 91, 0))
                                if e1d == 1 and e2d == 1 and e3d >= 2 then scythe = a end
                            end
                        end
                    end
                end
                out_rows[#out_rows + 1] = { "tech.p2_no_slam_out_of_reach", cls.far[2] == 0,
                    "attacks with me two or more tiles out on T-1: " .. cls.far[1] .. ", slams among them: " .. cls.far[2] .. "; adjacent on T-1: " .. cls.adj[1] .. " attacks, " .. cls.adj[2] .. " slams" }
        for _, rw in ipairs(out_rows) do t.check(rw[1], rw[2], rw[3]) end
        out_rows = {}
        -- THE ROOM COMPLETE (K :2628-2648, unchanged)
        if death_tick ~= nil then
            t.ticks(10)
            local _, ml = t.msg.last(12)
            local mt2 = {}
            for _, m in ipairs(ml) do mt2[#mt2 + 1] = tostring(m.text) end
            local xr, xt = t.world.tile()
            t.check("verzik.exit", true, "boss npc_death row on tick " .. death_tick .. "; my tile " .. tostring(xt and (xt.x .. "," .. xt.z)) .. "; messages " .. table.concat(mt2, " | "))
            -- the room must be won deathless: the raid's death counter (read-only ::tobjail) is 0, nothing was caged, and no death line was shown
            t.cheat("::tobjail")
            t.ticks(2)
            local _, jl = t.msg.last(40)
            local jail_line, died_lines = "", 0
            for _, m in ipairs(jl) do
                local jt = tostring(m.text)
                if jail_line == "" and jt:find("tobjail jailed=", 1, true) then jail_line = jt end
                if jt:find("You have died", 1, true) then died_lines = died_lines + 1 end
            end
            local _, hp_end = t.skill.read("hitpoints")
            t.check("verzik.deathless", jail_line:find("jailed=0 died_in=0 deaths=0", 1, true) ~= nil and died_lines == 0 and (hp_end.current or hp_end.level or 0) > 0,
                "deathless: " .. jail_line .. "; 'You have died' lines in the last 40 messages: " .. died_lines .. "; hitpoints at the end " .. tostring(hp_end.current or hp_end.level))
        end

        -- THE PLAY'S OWN ROWS (seam30): the rulings it plays by, from the log
        -- P3: every ranged or magic auto that hit me landed under its protection
        -- (the owner's ruling, V verzik.p3_prayer_read: read ON HIT); a prayed
        -- auto is at most half the unprayed 34 once she is enraged (W:986;
        -- W:951 "up to 33 damage, reduced to 16 if prayed against"; V
        -- verzik.entry_p3_auto_max 20 before the enrage: s30 svd saw a 12
        -- under the prayer while enraged), and at most one in four lands on
        -- a tick the plan did not read its protection lit
        local p3_autos, p3_max, p3_unprayed = 0, 0, 0
        for _, a in ipairs(R.anim) do
            if a.slot == boss_slot and a.type == 10835 and (a.seq == 8124 or a.seq == 8125) then
                local style = nil
                for _, p in ipairs(R.proj) do
                    if p.tick == a.tick and (p.spotanim == 1593 or p.spotanim == 1594) then style = p.spotanim end
                end
                if style ~= nil then
                    for _, h in ipairs(R.hitp) do
                        if h.npc_slot == boss_slot and h.tick >= a.tick + 1 and h.tick <= a.tick + 5 and h.counted == nil then
                            h.counted = true
                            p3_autos = p3_autos + 1
                            if h.damage > p3_max then p3_max = h.damage end
                            local lit = rec.prayer_at[h.tick] or {}
                            local want = (style == 1593) and "protectfrommissiles" or "protectfrommagic"
                            if lit[want] ~= true then p3_unprayed = p3_unprayed + 1 end
                            break
                        end
                    end
                end
            end
        end
        t.check("play.p3_prayer_on_hit", p3_autos >= 1 and p3_max <= 17 and p3_unprayed * 4 <= p3_autos,
            p3_autos .. " P3 ranged/magic autos landed on me, the largest " .. p3_max .. " (prayed at most 17 enraged, Entry unprayed 20 before it); "
            .. p3_unprayed .. " of them landed on a tick the plan did not read the matching protection lit")
        -- P2: no urnbomb landed on me (W:909 "dodged by simply avoiding the tile")
        local bombs_on_me, bombs = 0, 0
        for _, p in ipairs(R.proj) do
            if p.spotanim == 1583 then
                bombs = bombs + 1
                local land = p.tick + math.floor(((p.end_cycle or 0) - (p.start_cycle or 0)) / 30)
                local at = ptile_at[land - 1]
                if at ~= nil and at.x == p.dst_x and at.z == p.dst_z then bombs_on_me = bombs_on_me + 1 end
            end
        end
        t.check("play.p2_bombs_dodged", bombs >= 1 and bombs_on_me * 4 <= bombs,
            bombs .. " urnbombs thrown, " .. bombs_on_me .. " of them on the tile I held the tick before they landed")

        -- THE MEASURE (reported against the kept tob_verzik run)
        local taken = 0
        for _, h in ipairs(R.hitp) do taken = taken + h.damage end
        local hist = { 0, 0, 0, 0 }
        for _, n in pairs(rec.inputs) do
            if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
        end
        local forms = {}
        for _, f in ipairs(vz.forms or {}) do forms[#forms + 1] = f.symbol .. "@" .. f.tick end
        local specs = {}
        for _, s in ipairs(vz.specs or {}) do specs[#specs + 1] = s.tick .. ":" .. s.bar .. "/" .. s.press end
        t.check("play.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %d; food %d, drinks %d; swings %d (fists %s, bow %s, dawnbringer %s, rapid %s); specials %s; add presses %s; inputs per tick: 1 on %d, 2 on %d, 3 on %d, 4+ on %d; forms %s; summons %s, holds %s, tornado runs %s, tornadoes seen %s",
            tostring(death_tick and (death_tick - M)), tostring(M), tostring(death_tick), taken, #rec.eats, #rec.drinks, #rec.swings,
            tostring(vz.n and vz.n.fists), tostring(vz.n and vz.n.bow_accurate), tostring(vz.n and vz.n.dawnbringer), tostring(vz.n and vz.n.bow_rapid),
            table.concat(specs, ","), tostring(vz.add_presses), hist[1], hist[2], hist[3], hist[4], table.concat(forms, " "),
            tostring(vz.summons), tostring(vz.holds), tostring(vz.tornado_runs), tostring(vz.tornado_seen)))
        t.finish(0)
    end,
}
