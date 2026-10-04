-- _party_smoke: three driven clients in one Theatre of Blood (raid seam17,
-- party_run_and_verbs). Not a room test (the leading `_`): the closer runs it
--   python3 tools/raid_gate/run.py _party_smoke --no-build --no-publish
--   python3 tools/quest_gate/gate.py _party_smoke   (the union of the three ledgers)
-- ONE file, run by every client; each reads its seat from t.party.role()
-- (1 = the leader, who hosts the world and holds its one tick log) and
-- branches with if/else. Sync is t.party.barrier (files under the run dir,
-- driver state) plus what a raider reads in game.
--
-- Phase A, Entry: the three meet at the notice board, the leader forms an
-- Entry party, the members apply and are accepted, the leader's ready check
-- reads "Members: 3", the members are called in and follow, everyone reads the
-- other two and the three HUD orbs inside, then everyone leaves.
-- Phase B, Normal: the leader sets the party to Normal, the three enter again,
-- the leader starts the Maiden's fight with all three in the raid and reads
-- her hitpoints and the HUD bar: the scale a party of three fixes.
return {
    id = "_party_smoke",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    -- Bring-alongs, every raider (a member's go out as its typed ::command):
    -- enough hitpoints to stand three ticks in Normal Maiden's room.
    setup = { "::setlevel hitpoints 99", "::setlevel defence 99" },
    max_frames = 60000,
    run = function(t)
        local role = t.party.role()
        local names = t.party.names()
        local leader = names[1]
        local LOBBY_X, LOBBY_Z = 3662, 3216
        local function want(name, ok, detail)
            t.step(name, ok and "PASS" or "FAIL", detail)
            return ok
        end
        t.step("party.role", "PASS", string.format("raider %d of %d (%s); leader %s",
            role, t.party.size(), names[role], leader))
        -- The world's one tick log lives with the leader (it hosts the world);
        -- run.py copies it into every raider's session directory afterwards.
        if role == 1 then
            t.expect("ticklog.start", t.ticklog.start())
        end
        -- The setup list ran on this raider too (a member's lines went out as its
        -- client's typed ::command): read back from the stats the client was sent.
        local hr, hit = t.skill.read("hitpoints")
        local dr, def = t.skill.read("defence")
        want("party.setup_landed", hr == "ok" and dr == "ok" and type(hit) == "table" and type(def) == "table"
            and hit.base_level == 99 and def.base_level == 99,
            string.format("hitpoints %s, defence %s", type(hit) == "table" and tostring(hit.base_level) or tostring(hit),
                type(def) == "table" and tostring(def.base_level) or tostring(def)))

        -- The lobby: everyone walks to the board (plain travel), then meets.
        t.exec("lobby.goto", t.player.goto_tile, LOBBY_X - 1 + role, LOBBY_Z, 0)
        t.expect("party.barrier.lobby", t.party.barrier("lobby", 200))
        t.expect("lobby.sees_the_other_two", t.party.see(nil, 10, 20))

        -- Forming the party.
        if role == 1 then
            t.exec("party.form", t.party.form, "entry")
        end
        t.expect("party.barrier.formed", t.party.barrier("formed", 300))
        if role ~= 1 then
            t.exec("party.apply", t.party.apply, leader)
        end
        t.expect("party.barrier.applied", t.party.barrier("applied", 300))
        if role == 1 then
            for n = 2, t.party.size() do
                t.exec("party.accept." .. n, t.party.accept, names[n])
            end
            local members = t.party._panel_members()
            want("party.members_3", #members == 3, "the panel's member rows: " .. table.concat(members, ", "))
        end
        t.expect("party.barrier.accepted", t.party.barrier("accepted", 300))
        if role ~= 1 then
            t.party._refresh_panel()
            t.expect("party.member_panel", t.ui.expect_text("tob_partydetails:action", "Leave", 10, 9))
        end
        t.key("escape")
        t.ticks(2)
        t.expect("party.barrier.closed", t.party.barrier("closed", 300))

        -- The door: the leader first, then the members it calls in.
        if role == 1 then
            local rr, rd = t.party.ready()
            t.check("party.ready", rr, rd)
            want("raidwide.door.ready_check", string.find(tostring(rd), "Is your party ready? Members: 3. Mode: Entry.", 1, true) ~= nil,
                "measured " .. tostring(string.match(tostring(rd), "'(Is your party ready[^']*)'")) .. " (a party of three; the table's solo Normal reading is 'Members: 1. Mode: Normal.')")
        end
        t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
        if role ~= 1 then
            local mr, md = t.msg.await("has entered the Theatre of Blood (Entry Mode). Step inside to join", 20)
            local _, lines = t.msg.last(20)
            local said = nil
            for _, m in ipairs(lines or {}) do
                if string.find(m.text, "has entered the Theatre of Blood", 1, true) then said = m.text end
            end
            want("raidwide.chat.party_enter_line", mr == "ok" and said ~= nil and t.party._same(string.match(said, "^(.-) has entered") or "", leader),
                "measured " .. tostring(said) .. " (" .. tostring(md) .. ")")
            local fr, fd = t.party.follow_in()
            t.check("party.follow_in", fr, fd)
        end
        t.expect("party.barrier.inside", t.party.barrier("inside", 300))
        t.ticks(3)
        local sr, sd = t.party.see(nil, 20, 20)
        t.check("raid.sees_the_other_two", sr, sd)
        -- The orbs before any fight: recorded, not graded. ~tob_hud_orbs runs on
        -- a raider's own arrival (tob_raid.rs2 [queue,tob_room_settle]) and in
        -- the fight watchdog, so before the first barrier a raider holds the
        -- orbs of those who arrived before it. Graded in phase B, fight running.
        local function orbs_text()
            local parts, all_full = {}, true
            for p = 0, 2 do
                local r, v = t.var.varbit("varb644" .. (2 + p) .. "_tob_client_p" .. p)
                parts[#parts + 1] = "p" .. p .. "=" .. tostring(v)
                if r ~= "ok" or v ~= 27 then all_full = false end
            end
            return table.concat(parts, " "), all_full
        end
        local before = orbs_text()
        t.step("raid.orbs_before_the_fight", "PASS", "orbs " .. before .. " (arrival order p1, p2, p3)")
        t.expect("party.barrier.read_inside", t.party.barrier("read_inside", 300))

        -- Out, back to the lobby.
        t.cheat("::tobout")
        t.await({ level = function()
            local _, tile = t.world.tile()
            return type(tile) == "table" and tile.x > 3600 and tile.x < 3700
        end, note = "back in Ver Sinhaza" }, 20)
        local _, tile_after = t.world.tile()
        want("raid.left", type(tile_after) == "table" and tile_after.x > 3600 and tile_after.x < 3700,
            "after ::tobout at " .. (type(tile_after) == "table" and (tile_after.x .. "," .. tile_after.z) or tostring(tile_after)))
        t.expect("party.barrier.out", t.party.barrier("out", 300))

        -- Phase B, Normal: the same party, Mode set to Normal at the board.
        if role == 1 then
            t.exec("party.form.normal", t.party.form, "normal")
            local members = t.party._panel_members()
            want("party.normal_members_3", #members == 3, "the panel's member rows after the raid: " .. table.concat(members, ", "))
            t.key("escape")
            t.ticks(2)
        end
        t.expect("party.barrier.normal_set", t.party.barrier("normal_set", 300))
        if role == 1 then
            local rr, rd = t.party.ready()
            t.check("party.ready.normal", rr, rd)
            want("raidwide.door.ready_check.normal", string.find(tostring(rd), "Members: 3. Mode: Normal.", 1, true) ~= nil,
                "measured " .. tostring(string.match(tostring(rd), "'(Is your party ready[^']*)'")))
        end
        t.expect("party.barrier.normal_leader_in", t.party.barrier("normal_leader_in", 300))
        if role ~= 1 then
            t.msg.await("has entered the Theatre of Blood (Normal Mode)", 20)
            local fr, fd = t.party.follow_in()
            t.check("party.follow_in.normal", fr, fd)
        end
        t.expect("party.barrier.normal_inside", t.party.barrier("normal_inside", 300))
        t.ticks(3)
        if role == 1 then
            -- The leader starts the Maiden's fight with all three in the raid:
            -- the scale is the party that walked in (tob_party.rs2 ~tob_start_room:
            -- ^tob_var_scale = ~tob_party_size, then ~tob_rescale_boss).
            t.exec("maiden.barrier", t.player.click_loc, "tob_arena_barrier", 1)
            t.await({ level = function() return t.chat.kind() == "options" end, note = "the barrier's question" }, 5)
            t.exec("maiden.begin", t.chat.choose, "Yes, begin the fight.")
            t.ticks(1)
            t.cheat("::tobwhy")
            t.ticks(1)
            local _, lines = t.msg.last(40)
            local hp = nil
            for _, m in ipairs(lines or {}) do
                if hp == nil and string.find(m.text, "Maiden", 1, true) then hp = tonumber(string.match(m.text, "hp=(%d+)")) end
            end
            local _, bar = t.var.varbit("varb6448_tob_client_waveprogress_val")
            local _, kind = t.var.varbit("varb6447_tob_client_waveprogress_type")
            t.step("spec.maiden.hp_3", hp == 2625 and "PASS" or "FAIL", string.format(
                "measured %s (spec 2625 hp, grade A, tol exact); a Normal party of three, ::tobwhy on the tick after the barrier; 3500 x 750 / 1000 (tob.constant ^tob_maiden_hp_5 3500, ^tob_scale_3 750)", tostring(hp)))
            local permille = hp and math.floor(hp * 1000 / 3500) or nil
            t.step("spec.raidwide.scale.party_3_or_fewer", permille == 750 and "PASS" or "FAIL", string.format(
                "measured %s (spec 750 permille, grade D, tol exact); her hitpoints %s over the 5-man 3500 (cache stat4), party of 3 at Normal; 'Players in groups of three will find that the bosses have 75%% of their original hitpoints.' (newsposts/wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:28)", tostring(permille), tostring(hp)))
            -- One npc_spawn row of the world's log, quoted for the closer: the
            -- Normal Maiden's spawn (its tick is the same line in every raider's
            -- copy of the log).
            local _, maiden_row = t.npc.nearest("tob_maiden_100", 0)
            local maiden_id = type(maiden_row) == "table" and maiden_row.npc_id or nil
            local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
            local spawn = nil
            for _, r in ipairs(spawns or {}) do
                if r.type == maiden_id then spawn = r end
            end
            want("ticklog.maiden_spawn", spawn ~= nil, spawn and string.format(
                "npc_spawn serial %s tick %s slot %s type %s (tob_maiden_100) coord %s", tostring(spawn.serial),
                tostring(spawn.tick), tostring(spawn.slot), tostring(spawn.type), tostring(spawn.coord))
                or ("no npc_spawn row of type " .. tostring(maiden_id) .. " among " .. tostring(#(spawns or {}))))
            t.step("spec.raidwide.hud.boss_hp_full", bar == 1000 and "PASS" or "FAIL", string.format(
                "measured %s (spec 1000 permille, grade C, tol exact); varbit 6448 with varbit 6447 = %s, Normal Maiden at %s of 2625", tostring(bar), tostring(kind), tostring(hp)))
        end
        t.expect("party.barrier.fight_started", t.party.barrier("fight_started", 300))
        -- The orbs with the fight running. The fight watchdog that refreshes them
        -- (~tob_watch_room -> ~tob_hud_orbs) is queued on the raider who crossed
        -- the barrier (tob_party.rs2 ~tob_start_room -> ~tob_arm_watchdog,
        -- tob_raid.rs2:1572-1573 `queue(tob_room_watchdog, 0, 0)`), so the
        -- leader's three orbs are the graded reading; every raider's OWN orb is
        -- graded too (each publishes its own slot on arrival). What a member
        -- holds for the others is recorded, not graded: a content finding
        -- (reported for CONTENT_BUGS.md by seam17), not a driver one.
        t.ticks(2)
        local orbs, full = orbs_text()
        if role == 1 then
            want("raidwide.hud.orb_full", full, "measured " .. orbs
                .. " (spec 27 each: fill 26 + 1, tob.constant ^tob_hud_orb_*; the leader's watchdog ~tob_hud_orbs)")
        else
            local _, own = t.var.varbit("varb644" .. (1 + role) .. "_tob_client_p" .. (role - 1))
            want("raidwide.hud.orb_full.own", own == 27, "measured own orb p" .. (role - 1) .. "=" .. tostring(own)
                .. " (spec 27: fill 26 + 1)")
            t.step("raid.orbs_member_view_observed", "PASS", "observed, not graded: " .. orbs
                .. (full and " (all full)" or " -- a member's orbs of raiders who arrived after it stay 0 until its own ~tob_hud_orbs runs again; content finding, seam17"))
        end
        t.cheat("::tobout")
        t.await({ level = function()
            local _, tile = t.world.tile()
            return type(tile) == "table" and tile.x > 3600 and tile.x < 3700
        end, note = "back in Ver Sinhaza" }, 20)
        local _, tile_end = t.world.tile()
        want("raid.left.normal", type(tile_end) == "table" and tile_end.x > 3600 and tile_end.x < 3700,
            "after ::tobout at " .. (type(tile_end) == "table" and (tile_end.x .. "," .. tile_end.z) or tostring(tile_end)))
        -- seam17 three_clients_one_world (the engine seam's row, merged by the
        -- closer): from the first world tick with all three raiders on, every
        -- tick of the world's one log carries exactly one player_tile row per
        -- raider -- the world ticked in lock step with the three clients for
        -- the whole run. The leader's: the log is in its process.
        if role == 1 then
            local lr, rows = t.ticklog.rows({ kind = "player_tile" })
            local per, first, last = {}, nil, nil
            if lr == "ok" then
                for _, row in ipairs(rows) do
                    per[row.tick] = (per[row.tick] or 0) + 1
                end
                for tk, n in pairs(per) do
                    if n >= 3 and (first == nil or tk < first) then first = tk end
                    if last == nil or tk > last then last = tk end
                end
            end
            local bad = nil
            if first ~= nil then
                for tk = first, last do
                    if bad == nil and per[tk] ~= 3 then
                        bad = string.format("tick %d has %s player_tile rows (want 3)", tk, tostring(per[tk]))
                    end
                end
            end
            want("seam.three_clients_one_world", lr == "ok" and first ~= nil and bad == nil and last - first >= 100,
                lr ~= "ok" and ("ticklog: " .. tostring(rows))
                or first == nil and "no world tick with three player_tile rows"
                or bad
                or string.format("ticks %d..%d (%d ticks): 3 player_tile rows on every tick", first, last, last - first + 1))
        end
        t.expect("party.barrier.done", t.party.barrier("done", 300))
        t.finish(0)
    end,
}
