-- quest-driver / raid: enter a raid room as a player arrives in it (the room
-- built by the raid's own debugproc), and read the raid's state back.
-- Raid seam 1 (docs/RAID_ORCHESTRATOR.md section 4, instance resume per room;
-- build/seam_state/<pass>/triage.json key raid_room_entry_verbs).
--
--   t.raid.enter(raid, room, opts) -> ok refused timeout no_row unsupported
--   t.raid.state()                 -> ("ok", {raid, room, room_id, mode, started,
--                                     handle, boss_symbol, boss_slot, fight, line})
--                                     or not_found when no raid is active
--   t.raid.leave()                 -> ok refused timeout
--   t.raid.start_tile()            -> ("ok", {x, z, level}) refused unsupported
--
-- In a party run (raid seam19) every raider calls t.raid.enter the same way:
-- the leader lands with the raid's debugproc, the members join its instance
-- with ::tobjoinroom (THE PARTY BRANCH, above QD.raid.enter). state, leave and
-- start_tile answer `unsupported` on a member: they read the server.
--
-- WHAT "ENTER" IS, AND WHAT IT IS NOT.  A room test is one relay leg per room
-- (RAID_ORCHESTRATOR.md section 6), so it has to begin standing where a player
-- who walked the raid to that room would stand: in the raid's real instance, on
-- the CORRIDOR side of the room's barrier, the room built and its fight
-- UNSTARTED.  enter() issues the raid's own landing debugproc (`::tobmode`,
-- `::toa`, `::coxseed` + `::coxgoto`) and nothing else: it never starts a room
-- (`::tobgo` / `::toago` are not used here and a test must not use them --
-- section 6 rule 1).  The fight is begun by the test, by the player's own click
-- on the barrier: ToB `tob_arena_barrier` op 1 then "Yes, begin the fight."
-- (tob_party.rs2 [oploc1,tob_arena_barrier]); ToA `toa_path_barrier` op 1 Pass
-- or `toa_wardens_barrier` op 1 (toa_barrier.rs2); CoX encounters wake on
-- approach, and Olm's chamber is entered by `raids_bossentrance` op 1 then
-- "Step through the mystical barrier." (cox.rs2).
--
-- THE STATE IS READ FROM THE SERVER'S OWN REGISTERS.  A raid's room, mode and
-- started flag are map-instance registers (`map_instance_var_get`), which no
-- client or varp reader can see, so each raid has one read-only debugproc that
-- prints them as a single `key=value` line (`::tobstate`, `::toastate`,
-- `::coxstate`) and state() parses it.  Which raid is active is read first from
-- the three session varps through t.var.server (they are transmit=no, so that
-- is the server content copy): varp5893_tob_active, varp6910_toa_active,
-- varp5912_cox_active.
--
-- THE SETTLE.  After the landing cheat the player has been teleported into a
-- freshly built instance, and the client is a tick or more behind the server
-- (pointer.lua goto_tile: the npc pool lands a tick after the tile, and the
-- loaded scene may still be the previous region's).  enter() waits for the
-- tile to leave the departure tile and hold for two ticks (Verzik's chamber is
-- entered by a queued walk a tick after the teleport -- tob_raid.rs2
-- ~tob_room_arrive), then the same two scene-settle awaits goto_tile uses (npc
-- pool non-empty, loc pool loaded around the player and the scene settled),
-- and only then reads the state and the boss.

-- Room tables.  `id` is the content's own room number (tob.constant
-- ^tob_room_*, toa.constant ^toa_room_*, cox.constant ^cox_room_*); `boss` is
-- the npc the room's build places (ToB: ~tob_spawn_boss at build time, so it is
-- PRESENT on landing) or the npc ~toa_spawn_boss places when the room STARTS
-- (ToA: so it is UNSPAWNED on landing); nil where the room has no single boss.
-- A list names every form the boss changes type through (npc_changetype:
-- Maiden's 70/50/30, Verzik's phases, Tekton's waiting/fighting), the first
-- form first; the pool is searched for any of them.
QD.raid._ROOMS = {
    tob = {
        -- Entry/Hard Maiden is her mode's record from her first tick (raid seam5,
        -- tob_maiden.rs2 ~tob_maiden_mode_form), so her _story/_hard bodies too.
        maiden = { id = 1, boss = { "tob_maiden_100", "tob_maiden_100_hard", "tob_maiden_100_story",
            "tob_maiden_70", "tob_maiden_70_hard", "tob_maiden_70_story",
            "tob_maiden_50", "tob_maiden_50_hard", "tob_maiden_50_story",
            "tob_maiden_30", "tob_maiden_30_hard", "tob_maiden_30_story" } },
        bloat = { id = 2, boss = { "tob_bloat", "tob_bloat_hard", "tob_bloat_story" } },
        -- No boss until the waves are done: ~tob_spawn_boss returns early.
        nylocas = { id = 3 },
        sotetseg = { id = 4, boss = { "tob_sotetseg_combat", "tob_sotetseg_combat_hard", "tob_sotetseg_combat_story",
            "tob_sotetseg_noncombat", "tob_sotetseg_noncombat_hard", "tob_sotetseg_noncombat_story" } },
        xarpus = { id = 5, boss = { "tob_xarpus_static", "tob_xarpus_static_hard", "tob_xarpus_static_story",
            "tob_xarpus_feeding", "tob_xarpus_feeding_hard", "tob_xarpus_feeding_story",
            "tob_xarpus_combat", "tob_xarpus_combat_hard", "tob_xarpus_combat_story" } },
        -- Entry/Hard Verzik is her mode's record from her first timer tick (raid
        -- seam8, tob_verzik.rs2 ~tob_verzik_mode_form), so her _story/_hard forms too.
        verzik = { id = 6, boss = { "verzik_initial", "verzik_initial_hard", "verzik_initial_story",
            "verzik_phase1", "verzik_phase1_hard", "verzik_phase1_story",
            "verzik_phase1_to2_transition", "verzik_phase1_to2_transition_hard",
            "verzik_phase1_to2_transition_story",
            "verzik_phase2", "verzik_phase2_hard", "verzik_phase2_story",
            "verzik_phase2_to3_transition", "verzik_phase2_to3_transition_hard",
            "verzik_phase2_to3_transition_story",
            "verzik_phase3", "verzik_phase3_hard", "verzik_phase3_story" } },
    },
    toa = {
        nexus = { id = 1 },
        crondis = { id = 2 },
        zebak = { id = 3, boss = "toa_zebak" },
        scabaras = { id = 4 },
        kephri = { id = 5, boss = "toa_kephri_boss_shielded" },
        het = { id = 6, boss = "toa_het_goal" },
        akkha = { id = 7, boss = "akkha_melee" },
        apmeken = { id = 8 },
        baba = { id = 9, boss = "toa_baba" },
        wardens = { id = 10, boss = "toa_wardens_p1_obelisk_npc" },
        wardens_p2 = { id = 11 },
        vault = { id = 12 },
    },
    cox = {
        -- Tekton wakes on approach (cox_tekton.rs2: `waiting` until a player is
        -- in range), and a landing at the room centre is in range.
        tekton = { id = 0, boss = { "raids_tekton_waiting", "raids_tekton_fighting_standard",
            "raids_tekton_fighting_enraged", "raids_tekton_walking_standard",
            "raids_tekton_walking_enraged", "raids_tekton_hammering" } },
        guardians = { id = 1 },
        vespula = { id = 2 },
        icedemon = { id = 3 },
        tightrope = { id = 4 },
        crabs = { id = 5 },
        thieving = { id = 6 },
        resource = { id = 7 },
        shamans = { id = 8 },
        mystics = { id = 9 },
        vasa = { id = 10 },
        vanguards = { id = 11 },
        muttadiles = { id = 12 },
        scavenger_small = { id = 13 },
        scavenger_large = { id = 14 },
        -- Not a grid room: the landing is floor 2's resource room, whose
        -- centre holds `raids_bossentrance` (cox_resource.rs2), and Olm does
        -- not exist until that barrier is confirmed (~cox_olm_enter).
        olm = { id = 7, floor = 1, boss = { "olm_head", "olm_head_spawning" } },
    },
}

-- ToB's three modes (tob.constant ^tob_mode_*), both ways.
QD.raid._TOB_MODES = { entry = 0, normal = 1, hard = 2 }
QD.raid._TOB_MODE_NAMES = { [0] = "entry", [1] = "normal", [2] = "hard" }

-- Per raid: the session varp that says it is active, its read-only state
-- debugproc, and its leave cheat.
QD.raid._RAIDS = {
    tob = { active = "varp5893_tob_active", state = "::tobstate", leave = "::tobout" },
    toa = { active = "varp6910_toa_active", state = "::toastate", leave = "::toaout" },
    cox = { active = "varp5912_cox_active", state = "::coxstate", leave = "::cox_leave" },
}
QD.raid._ORDER = { "tob", "toa", "cox" }

-- The default CoX layout seed: a room test must land in the same raid every
-- run (cox.rs2 `::coxseed`); a test that wants another layout passes opts.seed.
QD.raid._COX_DEFAULT_SEED = 1

-- Budgets, in server ticks.
QD.raid._ARRIVE_TICKS = 15
QD.raid._STATE_TICKS = 5
QD.raid._BOSS_TICKS = 5

-- (result, room_name) for a content room id of `raid`, or nil.
function QD.raid._room_name(raid, id)
    local rooms = QD.raid._ROOMS[raid]
    if not rooms then
        return nil
    end
    local best = nil
    for name, row in pairs(rooms) do
        -- olm shares id 7 with resource; the grid name wins for a state read.
        if row.id == id and name ~= "olm" then
            if best == nil or name < best then
                best = name
            end
        end
    end
    return best
end

-- The raid whose session varp reads 1 on the server, or nil, plus a reading
-- of all three for a detail.
function QD.raid._active()
    local found = nil
    local parts = {}
    for i = 1, #QD.raid._ORDER do
        local raid = QD.raid._ORDER[i]
        local result, value = QD.var.server(QD.raid._RAIDS[raid].active)
        parts[#parts + 1] = raid .. "=" .. (result == "ok" and tostring(value)
            or ("unread(" .. tostring(result) .. ")"))
        if result == "ok" and value == 1 and found == nil then
            found = raid
        end
    end
    return found, table.concat(parts, " ")
end

-- Dispatch a raid's read-only state debugproc and return (ok, fields, line):
-- fields is the line's key=value pairs, numbers as numbers and "x,z,l"
-- triples as {x, z, level}.  The serial is taken BEFORE the cheat, so the
-- line matched is the one this call asked for, never an older copy still in
-- the ring.
function QD.raid._read_state(raid)
    local spec = QD.raid._RAIDS[raid]
    local prefix = raid .. "state "
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, "raid.state: message_serial answered " .. tostring(serial_result)
    end
    local cheat_result, cheat_detail = QD.cheat(spec.state, false)
    if cheat_result ~= "ok" then
        return cheat_result, string.format("raid.state: %s answered %s%s", spec.state,
            tostring(cheat_result), cheat_detail and (" -- " .. tostring(cheat_detail)) or "")
    end
    local line = nil
    local awaited = await({
        level = function()
            local result, list = api_drive.messages()
            if result ~= "ok" then
                return false
            end
            for i = 1, #list do
                if list[i].serial > since and string.find(list[i].text, prefix, 1, true) == 1 then
                    line = list[i].text
                    return true
                end
            end
            return false
        end,
        note = "raid.state " .. spec.state .. " reply",
    }, QD.raid._STATE_TICKS)
    if awaited ~= "ok" or line == nil then
        return "timeout", string.format("raid.state: no '%s...' line within %d tick(s) of %s",
            prefix, QD.raid._STATE_TICKS, spec.state)
    end
    local fields = {}
    for key, value in string.gmatch(line, "(%w+)=([%-%d,]+)") do
        local a, b, c = string.match(value, "^(%-?%d+),(%-?%d+),(%-?%d+)$")
        if a then
            fields[key] = { x = tonumber(a), z = tonumber(b), level = tonumber(c) }
        else
            local rx, rz = string.match(value, "^(%-?%d+),(%-?%d+)$")
            if rx then
                fields[key] = { x = tonumber(rx), z = tonumber(rz) }
            else
                fields[key] = tonumber(value)
            end
        end
    end
    return "ok", fields, line
end

-- The room's boss in the client's npc pool: (status, row, symbol) where
-- status is "present" (symbol = the form found) or "unspawned" (symbol = the
-- first form), or "none" for a room with no boss.  `ticks` > 0 waits for it (a ToB boss is placed
-- by the build and lands in the pool a tick or two after the scene); 0 reads
-- once (a ToA boss that is not there yet is the expected answer, not a wait).
function QD.raid._boss(symbols, ticks)
    if symbols == nil then
        return "none", nil, nil
    end
    if type(symbols) ~= "table" then
        symbols = { symbols }
    end
    local function find()
        for i = 1, #symbols do
            local result, row = QD.npc.nearest(symbols[i], 0)
            if result == "ok" and type(row) == "table" then
                return symbols[i], row
            end
        end
        return nil, nil
    end
    if ticks and ticks > 0 then
        await({
            level = function()
                return (find()) ~= nil
            end,
            note = "raid boss " .. symbols[1],
        }, ticks)
    end
    local found, row = find()
    if found ~= nil then
        return "present", row, found
    end
    return "unspawned", nil, symbols[1]
end

-- "boss present (tob_maiden_100 slot 12 at 3172,4422)" / "boss unspawned
-- (toa_zebak)" / "boss none (room has no single boss)".
function QD.raid._boss_text(symbol, status, row)
    if status == "none" then
        return "boss none (room has no single boss)"
    end
    if status == "present" then
        return string.format("boss present (%s slot %s at %s,%s)", symbol,
            tostring(row.slot), tostring(row.x), tostring(row.z))
    end
    return string.format("boss unspawned (%s; pool nearest: %s)", symbol, QD.raid._pool_text(4))
end

-- The `n` nearest npcs in the client's pool as "symbol@x,z" -- what IS there
-- when the boss is not, so an "unspawned" reading names its neighbourhood (a
-- boss out of the client's view range reads the same as one never built).
function QD.raid._pool_text(n)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" or type(rows) ~= "table" then
        return "unread(" .. tostring(result) .. ")"
    end
    if #rows == 0 then
        return "empty"
    end
    local parts = {}
    for i = 1, math.min(n, #rows) do
        local name_result, name = api_drive.symbol_name("npc", rows[i].npc_id)
        parts[#parts + 1] = string.format("%s@%d,%d",
            name_result == "ok" and name or ("npc " .. tostring(rows[i].npc_id)), rows[i].x, rows[i].z)
    end
    return table.concat(parts, " ") .. string.format(" (%d in pool)", #rows)
end

-- The player's tile as "x,z,level", or "?".
function QD.raid._tile_text()
    local result, tile = QD.world.tile()
    if result ~= "ok" or type(tile) ~= "table" then
        return "?"
    end
    return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

-- Wait for the teleport the landing cheat made: the tile leaves `departure`
-- and holds still for two server ticks, then goto_tile's two scene settles.
-- (ok, "x,z,level") or (timeout, detail).  `departure` may be nil when the
-- landing can be the tile already stood on (a second enter of the same room);
-- then only the hold is waited for.
function QD.raid._settle(departure, ticks)
    local last_key = nil
    local held_since = api_drive.tick()
    local arrived = await({
        level = function()
            local result, tile = api_drive.player_tile()
            if result ~= "ok" or type(tile) ~= "table" then
                return false
            end
            local key = string.format("%d,%d,%d", tile.x, tile.z, tile.level)
            if key ~= last_key then
                last_key = key
                held_since = api_drive.tick()
                return false
            end
            if departure ~= nil and key == departure then
                return false
            end
            return api_drive.tick() - held_since >= 2
        end,
        note = "raid arrival",
    }, ticks or QD.raid._ARRIVE_TICKS)
    if arrived ~= "ok" then
        return "timeout", string.format("never arrived: still at %s (left %s) after %d tick(s)",
            tostring(last_key), tostring(departure), ticks or QD.raid._ARRIVE_TICKS)
    end
    -- The same settle goto_tile does after a ::goto (pointer.lua, "THE SCENE
    -- IS ONE TICK BEHIND THE TILE" and SEAM-PRESS-PIXEL), verdicts advisory.
    await({
        level = function()
            local pool_result, rows = api_drive.npcs(0)
            return pool_result == "ok" and #rows > 0
        end,
        note = "raid scene settle",
    }, 3)
    await({
        level = function()
            local loc_result, rows = api_drive.locs(QD.player._goto_scene_radius)
            return loc_result == "ok" and #rows > 0 and api_drive.settled()
        end,
        note = "raid scene rebuild",
    }, QD.player._goto_scene_ticks)
    return "ok", QD.raid._tile_text()
end

-- Issue one landing cheat; (ok) or (result, detail naming it).
function QD.raid._cheat(text)
    local result, detail = QD.cheat(text)
    if result ~= "ok" then
        return result, string.format("%s answered %s%s", text, tostring(result),
            detail and (" -- " .. tostring(detail)) or "")
    end
    return "ok"
end

-- t.raid.enter(raid, room, opts)
--
--   raid  "tob" | "toa" | "cox"
--   room  ToB: maiden bloat nylocas sotetseg xarpus verzik
--         ToA: nexus crondis zebak scabaras kephri het akkha apmeken baba
--              wardens wardens_p2 vault
--         CoX: tekton guardians vespula icedemon tightrope crabs thieving
--              resource shamans mystics vasa vanguards muttadiles
--              scavenger_small scavenger_large olm
--   opts  ToB: mode = "entry" | "normal" (default) | "hard"
--         CoX: seed = 0..1023 (default QD.raid._COX_DEFAULT_SEED),
--              floor = 0 | 1 (default: floor 1 first, then floor 2)
--
-- Leaves any OTHER raid first (its own leave cheat).  ok detail:
--   "in tob maiden (normal) at x,z,level; boss present (tob_maiden_100 slot S at x,z);
--    handle H, started 0, fight x,z,level"
-- refused: an unknown raid/room/mode, the landing cheat refused, or the state
-- read back disagreeing with what was asked (wrong room/mode, already started).
-- THE PARTY BRANCH (raid seam19, tob_party_room_test_shape). In a party run
-- (QD.party.size() > 1: `party = 3,` in the test, or run.py --party 3) every
-- raider calls t.raid.enter with the SAME arguments, in the same order, and it
-- answers on each once all of them are at the room's entrance:
--
--   leader (role 1)  the solo path below (`::tobmode <room> <mode>`, the
--                    settle, the state read), then barrier raid_enter_<k>, then
--                    barrier raid_joined_<k>, then ::tobstate again: the
--                    instance's party must read the run's size (`party=N`).
--   member (role n)  barrier raid_enter_<k> (the leader is in), then its own
--                    typed `::tobjoinroom <mode>` (tob.rs2: the door's join,
--                    ~tob_join_raid + ~tob_party_join, so the seats fill in the
--                    order the members join), read back on its own client: the
--                    room line ("<room>. Cross the barrier to begin ...") and
--                    its tile leaving where it stood within _JOIN_TICKS, then
--                    the settle, then its tile equal to the leader's as this
--                    client sees the leader (a joiner lands on the tile the
--                    leader landed on), then the boss in its own pool; then
--                    barrier raid_joined_<k>.
--
-- `k` counts this client's party enters, so a second enter in one run has its
-- own barrier files (a barrier's files stay in the run dir). Each raider
-- passes both barriers exactly once per call, whatever its own result, so a
-- refused join cannot strand the others. A member's ok detail begins as the
-- leader's does ("in tob bloat (normal) at x,z,level; boss present (...)") and
-- names the seat it took; the handle, the started flag and the fight tile are
-- the leader's to read (a member has no server readers: t.raid.state answers
-- unsupported there). ToB only: ToA and CoX have no member bring-along, so a
-- party enter of either answers unsupported on every raider. A member who is
-- still inside an earlier instance is refused by the join ("You're already
-- inside the Theatre."): it leaves first with t.cheat("::tobout").
QD.raid._party_enters = 0
QD.raid._JOIN_TICKS = 5
QD.raid._PARTY_BARRIER_TICKS = 300
-- ::tobjoinroom's refusals (tob.rs2 [debugproc,tobjoinroom] and tob_raid.rs2
-- ~tob_join_raid), matched as plain substrings.
QD.raid._JOIN_REFUSALS = {
    "You're already inside the Theatre.",
    "Your party leader has not entered the Theatre yet.",
    "That party is running a different mode.",
    "That party is already fighting.",
    "That party is full.",
    "tobjoinroom: mode must be",
}

function QD.raid.enter(raid, room, opts)
    if QD.party.size() <= 1 then
        return QD.raid._enter_here(raid, room, opts)
    end
    return QD.raid._enter_party(raid, room, opts)
end

-- The member's half of a party enter: (ok, detail) or (refused|timeout, detail).
-- Never called by the leader, never in a party of one (refused there: there is
-- no leader's raid to join).
function QD.raid._join(room, row, mode, mode_name)
    local size = QD.party.size()
    local role = QD.party.role()
    if size <= 1 or role == 1 then
        return "refused", string.format("raid.enter: the member's join needs a party and a member seat"
            .. " (party of %d, role %d): the leader's path is ::tobmode", size, role)
    end
    local leader = QD.party.name(1)
    local text = string.format("::tobjoinroom %d", mode)
    local departure = QD.raid._tile_text()
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, "raid.enter: message_serial answered " .. tostring(serial_result)
    end
    local start = api_drive.tick()
    local cheat_result, cheat_detail = QD.cheat(text, false)
    if cheat_result ~= "ok" then
        return cheat_result, string.format("raid.enter: %s answered %s%s", text, tostring(cheat_result),
            cheat_detail and (" -- " .. tostring(cheat_detail)) or "")
    end
    -- The typed command is sent, not run: read its effect back on this client.
    local said = nil
    local refusal = nil
    local landed_tile = nil
    local landed = QD.await({
        level = function()
            local list_result, list = api_drive.messages()
            if list_result == "ok" then
                for i = 1, #list do
                    local m = list[i]
                    if m.serial > since then
                        if said == nil and string.find(m.text, ". Cross the barrier to begin", 1, true) then
                            said = m.text
                        end
                        for j = 1, #QD.raid._JOIN_REFUSALS do
                            if refusal == nil and string.find(m.text, QD.raid._JOIN_REFUSALS[j], 1, true) then
                                refusal = m.text
                            end
                        end
                    end
                end
            end
            if refusal ~= nil then
                return true
            end
            local key = QD.raid._tile_text()
            if said ~= nil and key ~= "?" and key ~= departure then
                landed_tile = key
                return true
            end
            return false
        end,
        note = "raid.enter: " .. text .. " landing",
    }, QD.raid._JOIN_TICKS)
    if refusal ~= nil then
        return "refused", string.format("raid.enter: %s refused: '%s' (p%d at %s)", text, refusal, role, departure)
    end
    if landed ~= "ok" or landed_tile == nil then
        return "timeout", string.format("raid.enter: %s: within %d tick(s) the room line %s and the tile %s (left %s)",
            text, QD.raid._JOIN_TICKS, said and ("came ('" .. said .. "')") or "never came",
            QD.raid._tile_text(), departure)
    end
    local landed_after = api_drive.tick() - start
    local settle_result, settle_detail = QD.raid._settle(nil)
    if settle_result ~= "ok" then
        return settle_result, "raid.enter: after " .. text .. ": " .. settle_detail
    end
    -- The room's entry is where the leader landed: this client's own tile must
    -- be the tile it sees the leader on.
    local leader_row = nil
    local mine = nil
    QD.await({
        level = function()
            local players_result, rows = api_drive.players()
            if players_result ~= "ok" then
                return false
            end
            leader_row, mine = nil, nil
            for _, r in ipairs(rows) do
                if r.me then
                    mine = r
                elseif QD.party._same(r.name, leader) then
                    leader_row = r
                end
            end
            return leader_row ~= nil and mine ~= nil
        end,
        note = "raid.enter: the leader in this client's pool",
    }, 3)
    if leader_row == nil or mine == nil then
        return "refused", string.format("raid.enter: %s landed p%d at %s but the leader %s is not in its pool",
            text, role, settle_detail, tostring(leader))
    end
    local leader_text = string.format("%d,%d,%d", leader_row.x, leader_row.z, leader_row.level)
    if mine.x ~= leader_row.x or mine.z ~= leader_row.z or mine.level ~= leader_row.level then
        return "refused", string.format("raid.enter: %s landed p%d at %d,%d,%d, not on the leader %s's tile %s",
            text, role, mine.x, mine.z, mine.level, tostring(leader), leader_text)
    end
    local status, boss_row, boss_symbol = QD.raid._boss(row.boss, QD.raid._BOSS_TICKS)
    return "ok", string.format("in tob %s (%s) at %s; %s; p%d joined by %s (landed %d tick(s) after it was typed)"
        .. " on the leader %s's tile %s; room line '%s'; handle, started and fight are the leader's to read",
        room, mode_name, settle_detail, QD.raid._boss_text(boss_symbol, status, boss_row), role, text,
        landed_after, tostring(leader), leader_text, said)
end

-- t.raid.enter in a party run (see THE PARTY BRANCH above).
function QD.raid._enter_party(raid, room, opts)
    opts = opts or {}
    local size = QD.party.size()
    local role = QD.party.role()
    assert(size > 1)
    if raid ~= "tob" then
        return "unsupported", string.format("raid.enter: a party of %d enters ToB only -- %s has no member"
            .. " bring-along (raid seam19)", size, tostring(raid))
    end
    local row = QD.raid._ROOMS.tob[room]
    if row == nil then
        local names = {}
        for name in pairs(QD.raid._ROOMS.tob) do
            names[#names + 1] = name
        end
        table.sort(names)
        return "refused", string.format("raid.enter: unknown tob room '%s' (%s)", tostring(room),
            table.concat(names, " "))
    end
    local mode_name = opts.mode or "normal"
    local mode = QD.raid._TOB_MODES[mode_name]
    if mode == nil then
        return "refused", "raid.enter: unknown ToB mode '" .. tostring(mode_name) .. "' (entry, normal, hard)"
    end
    QD.raid._party_enters = QD.raid._party_enters + 1
    local entered = string.format("raid_enter_%d", QD.raid._party_enters)
    local joined = string.format("raid_joined_%d", QD.raid._party_enters)
    local wait = QD.raid._PARTY_BARRIER_TICKS

    if role == 1 then
        local result, detail = QD.raid._enter_here(raid, room, opts)
        local entered_result, entered_detail = QD.party.barrier(entered, wait)
        if entered_result ~= "ok" then
            return entered_result, "raid.enter (leader): " .. entered_detail .. " -- after " .. tostring(detail)
        end
        local joined_result, joined_detail = QD.party.barrier(joined, wait)
        if result ~= "ok" then
            return result, detail
        end
        if joined_result ~= "ok" then
            return joined_result, "raid.enter (leader): " .. detail .. "; " .. joined_detail
        end
        -- The members' joins, read back from the instance: its party count.
        local state_result, fields, line = QD.raid._read_state("tob")
        if state_result ~= "ok" then
            return state_result, "raid.enter (leader): " .. detail .. "; the party read: " .. tostring(fields)
        end
        if fields.party ~= size then
            return "refused", string.format("raid.enter: %s; but the instance's party reads %s of %d once"
                .. " every member joined -- %s", detail, tostring(fields.party), size, line)
        end
        return "ok", string.format("%s; party %d of %d in the instance (tobstate party=%d scale=%s)", detail,
            fields.party, size, fields.party, tostring(fields.scale))
    end

    local entered_result, entered_detail = QD.party.barrier(entered, wait)
    local result, detail
    if entered_result ~= "ok" then
        result, detail = entered_result, "raid.enter (member): " .. entered_detail
    else
        result, detail = QD.raid._join(room, row, mode, mode_name)
    end
    local joined_result, joined_detail = QD.party.barrier(joined, wait)
    if result == "ok" and joined_result ~= "ok" then
        return joined_result, detail .. "; " .. joined_detail
    end
    return result, detail
end

-- The solo path (and the leader's half of a party enter).
function QD.raid._enter_here(raid, room, opts)
    opts = opts or {}
    local rooms = QD.raid._ROOMS[raid]
    if rooms == nil then
        return "refused", "raid.enter: unknown raid '" .. tostring(raid) .. "' (tob, toa, cox)"
    end
    local row = rooms[room]
    if row == nil then
        local names = {}
        for name in pairs(rooms) do
            names[#names + 1] = name
        end
        table.sort(names)
        return "refused", string.format("raid.enter: unknown %s room '%s' (%s)", raid,
            tostring(room), table.concat(names, " "))
    end
    local mode_name = nil
    local mode = nil
    if raid == "tob" then
        mode_name = opts.mode or "normal"
        mode = QD.raid._TOB_MODES[mode_name]
        if mode == nil then
            return "refused", "raid.enter: unknown ToB mode '" .. tostring(mode_name)
                .. "' (entry, normal, hard)"
        end
    elseif opts.mode ~= nil then
        return "refused", "raid.enter: opts.mode is ToB's; " .. raid .. " takes none"
    end

    -- Another raid open: leave it first, through its own cheat.
    local active = QD.raid._active()
    if active ~= nil and active ~= raid then
        local left_result, left_detail = QD.raid.leave()
        if left_result ~= "ok" then
            return left_result, "raid.enter: leaving " .. active .. " first failed -- "
                .. tostring(left_detail)
        end
        active = nil
    end

    local departure = QD.raid._tile_text()
    local issued = {}
    if raid == "tob" then
        local text = string.format("::tobmode %d %d", row.id, mode)
        local result, detail = QD.raid._cheat(text)
        if result ~= "ok" then
            return result, "raid.enter: " .. detail
        end
        issued[#issued + 1] = text
    elseif raid == "toa" then
        local text = string.format("::toa %d", row.id)
        local result, detail = QD.raid._cheat(text)
        if result ~= "ok" then
            return result, "raid.enter: " .. detail
        end
        issued[#issued + 1] = text
    else
        local seed = opts.seed or QD.raid._COX_DEFAULT_SEED
        -- A raid already open on another seed is a different layout: leave it.
        if active == "cox" then
            local state_result, fields = QD.raid._read_state("cox")
            if state_result ~= "ok" or fields.seed ~= seed then
                local left_result, left_detail = QD.raid.leave()
                if left_result ~= "ok" then
                    return left_result, "raid.enter: leaving the open CoX raid first failed -- "
                        .. tostring(left_detail)
                end
                active = nil
            end
        end
        if active ~= "cox" then
            local text = string.format("::coxseed %d", seed)
            local result, detail = QD.raid._cheat(text)
            if result ~= "ok" then
                return result, "raid.enter: " .. detail
            end
            issued[#issued + 1] = text
            local settle_result, settle_detail = QD.raid._settle(departure)
            if settle_result ~= "ok" then
                return settle_result, "raid.enter: after " .. text .. ": " .. settle_detail
            end
            departure = QD.raid._tile_text()
        end
        local floors = opts.floor ~= nil and { opts.floor } or (row.floor ~= nil and { row.floor } or { 0, 1 })
        local placed = false
        local misses = {}
        for i = 1, #floors do
            local text = string.format("::coxgoto %d %d", row.id, floors[i])
            local serial_result, since = api_drive.message_serial()
            local result, detail = QD.raid._cheat(text)
            if result ~= "ok" then
                return result, "raid.enter: " .. detail
            end
            issued[#issued + 1] = text
            -- The debugproc says which: "coxgoto room R floor F" or "That
            -- room is not on that floor in this raid."
            local said = nil
            if serial_result == "ok" then
                local list_result, list = api_drive.messages()
                if list_result == "ok" then
                    for j = 1, #list do
                        if list[j].serial > since then
                            said = list[j].text
                        end
                    end
                end
            end
            if said ~= nil and string.find(said, "coxgoto room", 1, true) == 1 then
                placed = true
                break
            end
            misses[#misses + 1] = text .. " -> " .. tostring(said)
        end
        if not placed then
            return "refused", string.format("raid.enter: cox seed %d has no %s room -- %s",
                seed, room, table.concat(misses, "; "))
        end
    end

    local settle_result, settle_detail = QD.raid._settle(departure)
    if settle_result ~= "ok" then
        return settle_result, "raid.enter: after " .. table.concat(issued, ", ") .. ": " .. settle_detail
    end

    local state_result, fields, line = QD.raid._read_state(raid)
    if state_result ~= "ok" then
        return state_result, "raid.enter: " .. tostring(fields)
    end
    if fields.active ~= 1 then
        return "refused", "raid.enter: " .. table.concat(issued, ", ") .. " left the raid inactive -- " .. line
    end
    local where = settle_detail
    local tail = ""
    if raid == "tob" or raid == "toa" then
        if fields.room ~= row.id then
            return "refused", string.format("raid.enter: asked for %s room %d, the raid is in room %s -- %s",
                raid, row.id, tostring(fields.room), line)
        end
        if fields.started ~= 0 then
            return "refused", "raid.enter: the room is already started on landing -- " .. line
        end
        if raid == "tob" and fields.mode ~= mode then
            return "refused", string.format("raid.enter: asked for mode %d, the instance carries %s -- %s",
                mode, tostring(fields.mode), line)
        end
        local fight = fields.fight
        tail = string.format("; handle %s, started 0, fight %s", tostring(fields.handle),
            type(fight) == "table" and string.format("%d,%d,%d", fight.x, fight.z, fight.level) or "?")
    else
        if room ~= "olm" and fields.room ~= row.id then
            return "refused", string.format("raid.enter: asked for cox room %d, standing in room %s -- %s",
                row.id, tostring(fields.room), line)
        end
        tail = string.format("; seed %s, floor %s, cell %s", tostring(fields.seed), tostring(fields.floor),
            type(fields.cell) == "table" and (fields.cell.x .. "," .. fields.cell.z) or "?")
    end

    -- ToB places its boss at build time; ToA and Olm only when the room
    -- starts, so they are read once and "unspawned" is the expected answer.
    local wait = (raid == "tob" or (raid == "cox" and room ~= "olm")) and QD.raid._BOSS_TICKS or 0
    local status, boss_row, boss_symbol = QD.raid._boss(row.boss, wait)
    local mode_text = raid == "tob" and mode_name
        or (raid == "toa" and ("level " .. tostring(fields.level)))
        or ("seed " .. tostring(fields.seed))
    return "ok", string.format("in %s %s (%s) at %s; %s%s", raid, room, mode_text, where,
        QD.raid._boss_text(boss_symbol, status, boss_row), tail)
end

-- A member's answer from the server readers (raid seam17/19): its process
-- holds no world, so the raid's registers and session varps are the leader's
-- to read. nil on the leader and in a solo run (nothing changes there).
function QD.raid._member_unsupported(verb)
    local role = QD.party.role()
    if role == 1 then
        return nil
    end
    return string.format("%s: p%d is a party member and holds no world; the raid's state is the leader's"
        .. " (role 1) to read%s", verb, role,
        verb == "raid.leave" and " -- a member leaves with t.cheat(\"::tobout\") and a tile read" or "")
end

-- t.raid.state() -> ("ok", {raid, room, room_id, mode, started, handle,
-- boss_symbol, boss_slot, fight, seed, line}) for the active raid, or
-- ("not_found", "<the three active varps>") when none is.  `mode` is ToB's
-- mode name, ToA's "level N", CoX's "seed N"; `started` is a boolean (CoX has
-- no started register and answers false); `boss_slot` is the client pool slot
-- of the room's boss, or nil when it is not in the pool.
function QD.raid.state()
    local member = QD.raid._member_unsupported("raid.state")
    if member ~= nil then
        return "unsupported", member
    end
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "not_found", "raid.state: no raid active (" .. reading .. ")"
    end
    local result, fields, line = QD.raid._read_state(raid)
    if result ~= "ok" then
        return result, fields
    end
    local out = {
        raid = raid,
        room_id = fields.room,
        room = QD.raid._room_name(raid, fields.room),
        handle = fields.handle,
        started = fields.started == 1,
        fight = fields.fight,
        seed = fields.seed,
        line = line,
    }
    if raid == "tob" then
        out.mode = QD.raid._TOB_MODE_NAMES[fields.mode] or tostring(fields.mode)
        out.cleared = fields.cleared == 1
    elseif raid == "toa" then
        out.mode = "level " .. tostring(fields.level)
        out.entry = fields.entry
    else
        out.mode = "seed " .. tostring(fields.seed)
        out.floor = fields.floor
    end
    local room_row = out.room and QD.raid._ROOMS[raid][out.room] or nil
    if room_row and room_row.boss then
        local status, row, symbol = QD.raid._boss(room_row.boss, 0)
        out.boss_symbol = symbol
        if status == "present" then
            out.boss_slot = row.slot
        end
    end
    return "ok", out
end

-- t.raid.leave() -> ok "left <raid> (<leave cheat>): at x,z,level, <active
-- varp> = 0"; refused when no raid is active.  Waits for the server's active
-- varp to read 0 and for the walk-out teleport to settle.
function QD.raid.leave()
    local member = QD.raid._member_unsupported("raid.leave")
    if member ~= nil then
        return "unsupported", member
    end
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "refused", "raid.leave: no raid active (" .. reading .. ")"
    end
    local spec = QD.raid._RAIDS[raid]
    local departure = QD.raid._tile_text()
    local result, detail = QD.raid._cheat(spec.leave)
    if result ~= "ok" then
        return result, "raid.leave: " .. detail
    end
    local var_result, var_detail = QD.var.await_server(spec.active, 0, QD.raid._ARRIVE_TICKS)
    if var_result ~= "ok" then
        return var_result, "raid.leave: " .. spec.leave .. " ran but " .. tostring(var_detail)
    end
    local settle_result, settle_detail = QD.raid._settle(departure)
    if settle_result ~= "ok" then
        return settle_result, "raid.leave: " .. spec.leave .. ": " .. settle_detail
    end
    return "ok", string.format("left %s (%s): at %s, %s = 0", raid, spec.leave, settle_detail, spec.active)
end

-- t.raid.start_tile() -> ("ok", {x, z, level}, "x,z,level"): the room's first
-- tile inside its barrier, from the content (ToB ~tob_room_fight_tile, ToA
-- ~toa_room_fight_coord) -- where a player who crossed it stands, for a test
-- that walks up to the barrier itself.  refused when no ToB/ToA room is open;
-- unsupported for CoX, whose rooms have no barrier tile (its encounters wake
-- on approach, and Olm's way in is the raids_bossentrance loc).
function QD.raid.start_tile()
    local member = QD.raid._member_unsupported("raid.start_tile")
    if member ~= nil then
        return "unsupported", member
    end
    local raid, reading = QD.raid._active()
    if raid == nil then
        return "refused", "raid.start_tile: no raid active (" .. reading .. ")"
    end
    if raid == "cox" then
        return "unsupported", "raid.start_tile: CoX rooms have no barrier tile -- encounters wake on"
            .. " approach; Olm's chamber is entered by clicking raids_bossentrance"
    end
    local result, fields, line = QD.raid._read_state(raid)
    if result ~= "ok" then
        return result, fields
    end
    local fight = fields.fight
    if type(fight) ~= "table" or (fight.x == 0 and fight.z == 0) then
        return "refused", "raid.start_tile: no room open (" .. tostring(line) .. ")"
    end
    return "ok", fight, string.format("%d,%d,%d", fight.x, fight.z, fight.level)
end

-- ===================================================================== party
--
-- t.party: a PARTY RUN (raid seam17 party_run_and_verbs; docs/minigames/
-- raid_loop/DRIVER_NOTES.md "Three raiders in one run"). run.py launches N
-- clients against ONE world (the leader's client hosts it; the others join
-- over the party link, TORIRS_EMBED_PARTY_*), every client runs the SAME
-- test file, and each one learns who it is from the QD_PARTY global run.py
-- writes into its wrapper: {role = n, size = N, names = {...}}. A run that
-- is not a party has no QD_PARTY, and every reader below answers as a party
-- of one (role 1, size 1, the run's own name), so a solo test may call them.
--
--   t.party.role()            -> 1 (the leader) .. N
--   t.party.size()            -> N
--   t.party.names()           -> {"<run>_p1", ..., "<run>_pN"} (account names; a
--                                copy, the n-th is raider n)
--   t.party.name(n)           -> raider n's account name (default: this one)
--   t.party.barrier(name, ticks) -> ok timeout refused
--   t.party.players(radius)   -> ("ok", detail, rows): the OTHER players this
--                                client's entity pool holds within `radius`
--                                tiles (nil: all), rows {name, x, z, level, pid}
--   t.party.see(names, radius, ticks) -> ok timeout: every named raider is in
--                                players(radius); the detail lists where
--   t.party.form(mode)        -> ok ...: the notice board, Make party, Mode
--   t.party.apply(leader)     -> ok ...: the party list, the leader's row, Apply
--   t.party.accept(name)      -> ok ...: the leader accepts an applicant
--   t.party.ready()           -> ok ...: the leader's door click and "Yes, let's go!"
--   t.party.follow_in()       -> ok ...: a member's door click after the leader
--
-- WHAT A MEMBER CAN AND CANNOT DO. A member hosts no world, so everything a
-- client reads (ui, chat, msg, inv, npcs, locs, players, its own tile) and
-- every click works as on the leader; t.cheat goes out as the client's typed
-- ::command packet (handled for that member at the world's next tick); the
-- SERVER readers -- t.tick, t.ticklog, t.var.server, t.raid.state -- answer
-- `unsupported`. The world's one tick log is the leader's: a spec row is the
-- leader's to write.

QD.party._MODES = {
    entry = { label = "Entry", first = "Not very experienced. I'll start with Entry Mode.",
        choose = "Entry Mode - practice, no uniques." },
    normal = { label = "Normal", first = "Quite experienced. I'll start with Normal Mode.",
        choose = "Normal Mode." },
    hard = { label = "Hard", first = "Quite experienced. I'll start with Normal Mode.",
        choose = "Hard Mode - requires a normal completion." },
}

function QD.party._info()
    if type(QD_PARTY) == "table" then
        return QD_PARTY
    end
    return nil
end

function QD.party.role()
    local info = QD.party._info()
    return info and info.role or 1
end

function QD.party.size()
    local info = QD.party._info()
    return info and info.size or 1
end

function QD.party.names()
    local info = QD.party._info()
    local out = {}
    if info then
        for i = 1, #info.names do
            out[i] = info.names[i]
        end
    else
        out[1] = QD.session._user()
    end
    return out
end

function QD.party.name(n)
    local names = QD.party.names()
    return names[n or QD.party.role()]
end

-- A display name as the server prints it and as an account is spelled:
-- compared case-folded, with `_`, `-` and the server's non-breaking space
-- all read as a space (ToriRSServer_SavePath's folding, run.py save_file_stem).
function QD.party._fold(name)
    local text = string.lower(tostring(name or ""))
    text = string.gsub(text, "<[^>]*>", "")
    text = string.gsub(text, "\194\160", " ")
    text = string.gsub(text, "[_%-]", " ")
    return text
end

function QD.party._same(a, b)
    return QD.party._fold(a) == QD.party._fold(b)
end

-- t.party.barrier(name, timeout_ticks): every raider writes
-- <run dir>/barrier.<name>.p<n> (api_drive.barrier_mark) and waits until all
-- N are there. Driver state, not game state: the world never sees the files.
--
-- FRAME-COUNTED (raid seam21 party_determinism_gate). A mark counts only from
-- the lockstep boundary after it was written (torirs_plugin_drive.c
-- lua_drive_barrier_present), so whether it is "there" is a fact of the lock
-- step, and the wait below is counted in this client's own frames: the level
-- is evaluated once when the await is armed and once per frame the await is
-- held (torirs_plugin_drive.c drive_pump_once), so `frames` is the number of
-- frames the raider spent inside the barrier. Nothing here reads a clock; the
-- deadline is the client's world cycle (frames x k cycles). The detail names
-- the frames and the ticks, and both are identical run to run
-- (tools/raid_gate/party_repeat.py compares them).
-- In a party of one it answers ok at once, having waited 0 frames.
function QD.party._await_counted(level, timeout_ticks, note)
    assert(type(level) == "function")
    assert(type(note) == "string")
    local start = api_drive.tick()
    local evaluations = 0
    local result = QD.await({
        level = function()
            evaluations = evaluations + 1
            return level()
        end,
        note = note,
    }, timeout_ticks)
    -- The first evaluation is the arming check, in the frame the verb was
    -- called from; each later one is one frame held.
    local frames = evaluations > 0 and evaluations - 1 or 0
    return result, frames, api_drive.tick() - start
end

function QD.party.barrier(name, timeout_ticks)
    assert(type(name) == "string")
    assert(string.match(name, "^[%w_%-%.]+$"))
    local size = QD.party.size()
    if size <= 1 then
        return "ok", "party.barrier " .. name .. ": a party of one, p1 waited 0 frame(s) (0 tick(s))"
    end
    -- seam22: a member's death is in its ledger at the latest here, the
    -- sync point every raider passes (the member fence at the end of this
    -- file writes player.died once and does not finish). Not on the leader:
    -- its death is the quest rule's, read by its own verbs as before.
    if QD.party.role() ~= 1 then
        QD.player._death_fence("t.party.barrier " .. name)
    end
    local mine = string.format("barrier.%s.p%d", name, QD.party.role())
    local marked = api_drive.barrier_mark(mine)
    if marked ~= "ok" then
        return marked, "party.barrier " .. name .. ": could not write " .. mine .. " (" .. tostring(marked) .. ")"
    end
    local missing = {}
    local result, frames, ticks = QD.party._await_counted(function()
        missing = {}
        for n = 1, size do
            if api_drive.barrier_present(string.format("barrier.%s.p%d", name, n)) ~= "ok" then
                missing[#missing + 1] = "p" .. n
            end
        end
        return #missing == 0
    end, timeout_ticks or 200, "party.barrier " .. name)
    if result ~= "ok" then
        return "timeout", string.format("party.barrier %s: p%d waited %d frame(s) (%d tick(s)); still missing %s",
            name, QD.party.role(), frames, ticks, table.concat(missing, ","))
    end
    return "ok", string.format("party.barrier %s: all %d raiders, p%d waited %d frame(s) (%d tick(s))",
        name, size, QD.party.role(), frames, ticks)
end

function QD.party._rows_text(rows)
    local parts = {}
    for _, r in ipairs(rows) do
        parts[#parts + 1] = string.format("%s pid %d at %d,%d,%d", tostring(r.name), r.pid, r.x, r.z, r.level)
    end
    if #parts == 0 then
        return "nobody"
    end
    return table.concat(parts, "; ")
end

function QD.party.players(radius)
    local result, rows = api_drive.players()
    if result ~= "ok" then
        return result, "party.players: no world in this client yet"
    end
    local me = nil
    for _, r in ipairs(rows) do
        if r.me then
            me = r
        end
    end
    local out = {}
    for _, r in ipairs(rows) do
        local near = true
        if radius ~= nil and me ~= nil then
            near = r.level == me.level and math.abs(r.x - me.x) <= radius and math.abs(r.z - me.z) <= radius
        end
        if not r.me and near then
            out[#out + 1] = r
        end
    end
    local where = me and string.format("%s at %d,%d,%d", tostring(me.name), me.x, me.z, me.level)
        or string.format("own player not among the %d in the pool", #rows)
    return "ok", string.format("%s sees %s", where, QD.party._rows_text(out)), out
end

-- t.party.see(names, radius, ticks): wait until every name in `names` (account
-- or display spelling; default: every other raider) is a player this client
-- holds within `radius` tiles.
function QD.party.see(names, radius, ticks)
    if names == nil then
        names = {}
        for i, n in ipairs(QD.party.names()) do
            if i ~= QD.party.role() then
                names[#names + 1] = n
            end
        end
    end
    local detail = ""
    local missing = {}
    local result = QD.await({
        level = function()
            local r, d, rows = QD.party.players(radius)
            detail = d
            missing = {}
            if r ~= "ok" then
                return false
            end
            for _, want in ipairs(names) do
                local found = false
                for _, row in ipairs(rows) do
                    if QD.party._same(row.name, want) then
                        found = true
                    end
                end
                if not found then
                    missing[#missing + 1] = want
                end
            end
            return #missing == 0
        end,
        note = "party.see",
    }, ticks or 10)
    if result ~= "ok" then
        return "timeout", "party.see: missing " .. table.concat(missing, ", ") .. " -- " .. tostring(detail)
    end
    return "ok", detail
end

-- ------------------------------------------------------------ the ToB lobby
--
-- The party verbs below are real click sequences on the real interfaces
-- (tob_board.rs2, tob_party.rs2; DRIVER_NOTES "The party board and the
-- scoreboard"), each read back from what the client was sent:
--   the notice board  tob_surface_notice_board op 1; a first reading is three
--                     pages (mesbox, the Entry/Normal question, mesbox), then
--                     tob_partylist;
--   Make party        tob_partylist:myparty op 1 -> tob_partydetails;
--   a party's row     tob_partylist:<kk> op 1 "View party" (sub 3 is the
--                     party's name, which is its leader's) -> tob_partydetails;
--   the action button tob_partydetails:action op 1: Apply / Withdraw / Leave /
--                     Disband by role (its text is sub 9);
--   Mode              tob_partydetails:mode op 1, then the three-way choice;
--   members           tob_partydetails:current, row k's cells subs 11k..11k+10;
--   applicants        tob_partydetails:applicants, applicant k's row is sub
--                     20k (op 1 Accept, op 10 Reject) and its name sub 20k+1;
--   the door          tob_surface_raid_entrance op 1: the death warning, then
--                     for the leader "Is your party ready? Members: N. Mode: X."
--                     and "Yes, let's go!"; a member walks in once the leader
--                     is inside (else "Your party leader has not entered the
--                     Theatre yet.").
-- The panel's first push draws only its last row (a client defect, DRIVER_NOTES):
-- every read presses Refresh first.

function QD.party._refresh_panel()
    local r, id = QD.ui.widget("tob_partydetails:refresh")
    if r == "ok" then
        QD.ui.invoke(id, 1)
        QD.ticks(2)
    end
end

-- The panel's member names, in row order (tob_partydetails:current).
function QD.party._panel_members()
    local names = {}
    for k = 0, 4 do
        local best = nil
        for c = 0, 10 do
            local r, text = QD.ui._text_one("tob_partydetails:current", k * 11 + c)
            if r == "ok" and text ~= "" and best == nil and not string.match(text, "^[%d%s/%-]+$") then
                best = text
            end
        end
        if best then
            names[#names + 1] = best
        end
    end
    return names
end

function QD.party._panel_text()
    local _, frame = QD.ui._text_one("tob_partydetails:frame", 1)
    local _, mode = QD.ui._text_one("tob_partydetails:mode", 0)
    local _, action = QD.ui._text_one("tob_partydetails:action", 9)
    return string.format("'%s' | '%s' | action '%s' | members %s", tostring(frame), tostring(mode),
        tostring(action), table.concat(QD.party._panel_members(), ", "))
end

-- Read the notice board and land on tob_partylist; a first reading's three
-- pages are answered with `mode`'s experience line (it sets only the board's
-- default mode, tob_board.rs2 ~tob_board_first_read).
function QD.party._open_board(mode)
    local spec = QD.party._MODES[mode or "entry"]
    assert(spec)
    local r, d = QD.player.click_loc("tob_surface_notice_board", 1)
    if r ~= "ok" then
        return r, "Read the notice board: " .. tostring(d)
    end
    QD.ticks(2)
    if QD.chat.kind() == "mesbox" then
        QD.chat.drain({ stop_at = "options", max_pages = 3 })
        local cr, cd = QD.chat.choose(spec.first)
        if cr ~= "ok" then
            return cr, "the board's first reading: " .. tostring(cd)
        end
        QD.ticks(2)
        -- The last page ("When you form a raiding party, it will be set to
        -- ... Mode") sits under the list; continue it, or it stays open over
        -- every later press.
        for _ = 1, 3 do
            if QD.chat.kind() ~= "mesbox" then
                break
            end
            QD.chat.drain({ max_pages = 2 })
            QD.ticks(1)
        end
    end
    local lr, ld = QD.ui.await_open("tob_partylist", 10)
    if lr ~= "ok" then
        return lr, "the board did not open tob_partylist: " .. tostring(ld)
    end
    return "ok"
end

function QD.party.form(mode)
    mode = mode or "entry"
    local spec = QD.party._MODES[mode]
    assert(spec)
    local r, d = QD.party._open_board(mode)
    if r ~= "ok" then
        return r, "party.form: " .. d
    end
    local wr, wid = QD.ui.widget("tob_partylist:myparty")
    if wr ~= "ok" then
        return wr, "party.form: tob_partylist:myparty: " .. tostring(wid)
    end
    QD.ui.invoke(wid, 1)
    local pr, pd = QD.ui.await_open("tob_partydetails", 10)
    if pr ~= "ok" then
        return pr, "party.form: Make party did not open tob_partydetails: " .. tostring(pd)
    end
    QD.party._refresh_panel()
    QD.party._refresh_panel()
    local _, shown = QD.ui._text_one("tob_partydetails:mode", 0)
    if not string.find(tostring(shown), spec.label, 1, true) then
        local mr, mid = QD.ui.widget("tob_partydetails:mode")
        if mr ~= "ok" then
            return mr, "party.form: tob_partydetails:mode: " .. tostring(mid)
        end
        QD.ui.invoke(mid, 1)
        QD.await({ level = function() return QD.chat.kind() == "options" end, note = "party.form: Mode" }, 5)
        local cr, cd = QD.chat.choose(spec.choose)
        if cr ~= "ok" then
            return cr, "party.form: Mode -> " .. spec.choose .. ": " .. tostring(cd)
        end
        QD.ticks(2)
        QD.party._refresh_panel()
    end
    local er, ed = QD.ui.expect_text("tob_partydetails:mode", "Mode: " .. spec.label, 5, 0)
    local text = QD.party._panel_text()
    if er ~= "ok" then
        return er, "party.form: the panel does not say Mode: " .. spec.label .. " -- " .. text
    end
    local fr = QD.ui.expect_text("tob_partydetails:frame", "Party of", 5, 1)
    if fr ~= "ok" then
        return fr, "party.form: no 'Party of' title -- " .. text
    end
    return "ok", "party.form " .. mode .. ": " .. text
end

-- t.party.apply(leader): open the list, press the leader's party's row,
-- press Apply; read back: the action button now offers Withdraw (role 3,
-- an applicant). `leader` is an account or display name.
function QD.party.apply(leader, ticks)
    assert(leader)
    local r, d = QD.party._open_board("entry")
    if r ~= "ok" then
        return r, "party.apply: " .. d
    end
    local row = nil
    local seen = {}
    local start = api_drive.tick()
    while row == nil and api_drive.tick() - start < (ticks or 20) do
        seen = {}
        for k = 0, 44 do
            local sym = string.format("tob_partylist:%02d", k)
            local tr, text = QD.ui._text_one(sym, 3)
            if tr == "ok" and text ~= "" then
                seen[#seen + 1] = text
                if row == nil and QD.party._same(text, leader) then
                    row = sym
                end
            end
        end
        if row == nil then
            local rr, rid = QD.ui.widget("tob_partylist:refresh")
            if rr == "ok" then
                QD.ui.invoke(rid, 1)
            end
            QD.ticks(2)
        end
    end
    if row == nil then
        return "not_found", "party.apply: no party named " .. tostring(leader) .. " on the list (rows: "
            .. (#seen > 0 and table.concat(seen, ", ") or "none") .. ")"
    end
    local wr, wid = QD.ui.widget(row)
    if wr ~= "ok" then
        return wr, "party.apply: " .. row .. ": " .. tostring(wid)
    end
    QD.ui.invoke(wid, 1)
    local pr, pd = QD.ui.await_open("tob_partydetails", 10)
    if pr ~= "ok" then
        return pr, "party.apply: View party did not open tob_partydetails: " .. tostring(pd)
    end
    QD.party._refresh_panel()
    QD.party._refresh_panel()
    local ar, ad = QD.ui.expect_text("tob_partydetails:action", "Apply", 5, 9)
    if ar ~= "ok" then
        return ar, "party.apply: the action button does not offer Apply -- " .. QD.party._panel_text()
    end
    local br, bid = QD.ui.widget("tob_partydetails:action")
    if br ~= "ok" then
        return br, "party.apply: tob_partydetails:action: " .. tostring(bid)
    end
    QD.ui.invoke(bid, 1)
    QD.ticks(2)
    QD.party._refresh_panel()
    local wr2 = QD.ui.expect_text("tob_partydetails:action", "Withdraw", 5, 9)
    local text = QD.party._panel_text()
    if wr2 ~= "ok" then
        return "refused", "party.apply: after Apply the button does not offer Withdraw -- " .. text
    end
    return "ok", "party.apply " .. row .. ": " .. text
end

-- t.party.accept(name, ticks): the leader, on its own open panel, waits for
-- `name` among the applicants (pressing Refresh) and presses its Accept;
-- read back: `name` is a member row of the panel.
function QD.party.accept(name, ticks)
    assert(name)
    local lr, ld = QD.ui.await_open("tob_partydetails", 2)
    if lr ~= "ok" then
        return lr, "party.accept: the party panel is not open (" .. tostring(ld) .. "); t.party.form first"
    end
    -- A predicate may not wait (a Refresh press is a click and two ticks), so
    -- the loops are written out: press Refresh, read the applicant rows. An
    -- applicant's row is re-laid as others are accepted (applicant k moves to
    -- k-1), and a row just pressed has its ops cleared for 80 client cycles
    -- (torirs_tob_party_ack.cs2), so each press re-reads the index, and a press
    -- that did not take is pressed again -- what a player does -- at most three
    -- times, each named in the detail.
    local seen = {}
    local presses = {}
    local member = false
    local start = api_drive.tick()
    while not member and #presses < 3 and api_drive.tick() - start < (ticks or 30) + 30 do
        local hit = nil
        QD.party._refresh_panel()
        seen = {}
        for k = 0, 9 do
            local tr, text = QD.ui._text_one("tob_partydetails:applicants", k * 20 + 1)
            if tr == "ok" and text ~= "" then
                seen[#seen + 1] = text
                if hit == nil and QD.party._same(text, name) then
                    hit = k
                end
            end
        end
        for _, m in ipairs(QD.party._panel_members()) do
            if QD.party._same(m, name) then
                member = true
            end
        end
        if hit ~= nil and not member then
            local wr, wid = QD.ui.widget("tob_partydetails:applicants", hit * 20)
            if wr ~= "ok" then
                return wr, "party.accept: applicant row " .. hit .. ": " .. tostring(wid)
            end
            local cr = QD.ui.invoke(wid, 1)
            presses[#presses + 1] = string.format("applicant %d at tick %d (%s)", hit,
                api_drive.tick() - start, tostring(cr))
            QD.ticks(4)
        elseif hit == nil and not member and #presses == 0 and api_drive.tick() - start >= (ticks or 30) then
            return "not_found", "party.accept: " .. tostring(name) .. " never applied (applicants: "
                .. (#seen > 0 and table.concat(seen, ", ") or "none") .. ")"
        end
    end
    if not member then
        QD.party._refresh_panel()
        for _, m in ipairs(QD.party._panel_members()) do
            if QD.party._same(m, name) then
                member = true
            end
        end
    end
    local text = QD.party._panel_text()
    if not member then
        return "refused", "party.accept: " .. tostring(name) .. " is not a member row after "
            .. (#presses > 0 and table.concat(presses, ", ") or "no press") .. " -- " .. text
    end
    return "ok", "party.accept " .. tostring(name) .. " (pressed "
        .. (#presses > 0 and table.concat(presses, ", ") or "nothing: already a member") .. "): " .. text
end

-- The door: click, then the death-warning mesbox.
function QD.party._door()
    local mr, since = api_drive.message_serial()
    local r, d = QD.player.click_loc("tob_surface_raid_entrance", 1)
    if r ~= "ok" then
        return r, "the door: " .. tostring(d), since
    end
    QD.ticks(2)
    return "ok", nil, (mr == "ok") and since or 0
end

function QD.party._line_since(since, prefix)
    local r, list = api_drive.messages()
    if r ~= "ok" then
        return nil
    end
    for i = 1, #list do
        if list[i].serial > since and string.find(list[i].text, prefix, 1, true) then
            return list[i].text
        end
    end
    return nil
end

function QD.party._await_line(since, prefix, ticks)
    local line = nil
    QD.await({
        level = function()
            line = QD.party._line_since(since, prefix)
            return line ~= nil
        end,
        note = "party: '" .. prefix .. "'",
    }, ticks or 10)
    return line
end

-- t.party.ready(): the leader's door click, the warning, the ready check
-- ("Is your party ready? Members: N. Mode: X.") and "Yes, let's go!"; read
-- back: the entry line "You enter the Theatre of Blood (X Mode)...". The
-- detail carries the ready check's title verbatim.
function QD.party.ready()
    local r, d, since = QD.party._door()
    if r ~= "ok" then
        return r, "party.ready: " .. d
    end
    QD.chat.drain({ stop_at = "options", max_pages = 3 })
    local tr, title = QD.chat.options_title()
    if tr ~= "ok" then
        local said = QD.party._line_since(since, "")
        return tr, "party.ready: no ready check after the door (" .. tostring(title) .. "); last line: "
            .. tostring(said)
    end
    local cr, cd = QD.chat.choose("Yes, let's go!")
    if cr ~= "ok" then
        return cr, "party.ready: " .. tostring(cd)
    end
    local line = QD.party._await_line(since, "You enter the Theatre of Blood", 10)
    if not line then
        return "timeout", "party.ready: chose Yes, let's go! under '" .. tostring(title)
            .. "' and no entry line followed"
    end
    return "ok", "party.ready: '" .. tostring(title) .. "' -> '" .. line .. "'"
end

-- t.party.follow_in(): a member's door click once its leader is inside
-- (tob_party.rs2 [oploc1,tob_surface_raid_entrance] -> ~tob_enter ->
-- tob_raid.rs2 tob_join_raid); read back: the entry line.
function QD.party.follow_in()
    local r, d, since = QD.party._door()
    if r ~= "ok" then
        return r, "party.follow_in: " .. d
    end
    local early = QD.party._line_since(since, "has not entered the Theatre yet")
    if early then
        QD.chat.drain({ max_pages = 3 })
        return "refused", "party.follow_in: '" .. early .. "'"
    end
    QD.chat.drain({ max_pages = 3 })
    local line = QD.party._await_line(since, "You enter the Theatre of Blood", 10)
    if not line then
        local said = QD.party._line_since(since, "")
        return "timeout", "party.follow_in: no entry line after the door; last line: " .. tostring(said)
    end
    return "ok", "party.follow_in: '" .. line .. "'"
end

-- ============================================== seam22: a dead raider in step
--
-- Raid seam22 (party_death_and_member_readers; docs/minigames/raid_loop/
-- SEAM_TRIAGE_2026-10-05a.md). In the Theatre a raider who dies is caged and
-- watches (tob_spectate.rs2 `~tob_death_after`: the purgatory stance, the
-- walktrigger hold, the orb's death marker) while the party fights on; the
-- room is won or wiped by the others. The driver used to end a MEMBER's run
-- on that death exactly as it ends a quest (combat.lua QD.player._death_fence:
-- row player.died FAIL, then t.finish). A finished member exits, its boundary
-- trace stops, the leader's barriers wait for a mark that never comes, and
-- gate.party_lockstep FAILs the run -- so no party test could hold a death the
-- real game allows (Normal Maiden and Sotetseg wiped on this alone).
--
-- Now, on a MEMBER (t.party.role() > 1):
--   * the death is still read the same way (QD.player._death_seen: the death
--     line, or a stated hitpoints 0), and the FIRST fence that sees it writes
--     row `player.died` with its own kept shot -- and does NOT finish. The
--     verdict is FAIL unless the test said beforehand that a death is part of
--     the plan (t.party.allow_death(reason)); then PASS, quoting the reason.
--   * from then on every fenced verb (a click settle, t.player.attack/cast,
--     npc.await_dead*, a quick held press) answers `refused` with the death
--     text, as on the leader: a caged raider's clicks are not a fight.
--     t.player.alive() answers refused too. Barriers, reads, t.cheat and
--     t.finish still work, so the script runs on to the leader's end, sending
--     READY every F frames like any member.
--   * t.party.barrier runs the fence on a member first, so a death is in the
--     ledger at the latest at the next barrier, on the same frame every run.
-- On the LEADER (and solo) nothing changes: its death ends the world and the
-- run, row player.died FAIL, as before.
--
-- The member's run then ends with everyone else's (the same last barrier), or
-- -- if the leader's process ends first -- at its next boundary, where the
-- party link's loss stops it loudly (net_transport_embed.c party_member_lost).

-- ToB's own death line. tob_spectate.rs2 `~tob_death_message` prints it IN
-- PLACE OF "Oh dear, you are dead!" for a death inside a fight ("You have
-- died. Death count: 1." [video], tob_spectate.rs2:223-232), so the shared
-- reading (state.lua QD.player._death_record, which matches the ordinary line)
-- saw a Theatre death only through the few ticks its hitpoints read 0. The
-- wrap below latches this line the same way: whole ring, once, for good --
-- IN A PARTY ONLY (every raider of a party of two or more). A solo run keeps
-- the shared reading unchanged: the conformance harness dies on purpose in a
-- solo Entry room (seam.tob_death_cage_then_entry_restart, `::die`, the cage,
-- the wipe's restart) and drives on, and a latch there ended the harness at
-- the next click settle (seam22 closer: eleven attempts, each stopped on
-- `player.died` right after that row). A solo room test's death is still read
-- through its hitpoints-0 ticks, as before seam22.
QD.raid.TOB_DEATH_PREFIX = "You have died. Death count: "

QD.player._death_record_shared = QD.player._death_record

function QD.player._death_record()
    local record = QD.player._death_record_shared()
    if record then
        return record
    end
    if QD.party.size() <= 1 then
        return nil
    end
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return nil
    end
    local newest, line = nil, nil
    for i = 1, #rows do
        local text = rows[i].text
        if type(text) == "string" and string.find(text, QD.raid.TOB_DEATH_PREFIX, 1, true)
            and (newest == nil or rows[i].serial > newest) then
            newest, line = rows[i].serial, QD.player._plain_line(text)
        end
    end
    if newest == nil then
        return nil
    end
    local tile_result, tile = api_drive.player_tile()
    local where = (tile_result == "ok" and type(tile) == "table")
        and (tostring(tile.x) .. "," .. tostring(tile.z) .. " L" .. tostring(tile.level))
        or tostring(tile_result)
    QD._death = {
        deaths = 1,
        serial = newest,
        wake = "the Theatre's spectator cage",
        tick = api_drive.tick(),
        where = where,
        hitpoints = "?",
        text = "the character DIED in the Theatre of Blood: '" .. tostring(line) .. "' (tob_spectate.rs2"
            .. " ~tob_death_message), read at " .. where .. " on drive tick " .. tostring(api_drive.tick())
            .. " -- a raider who dies in a fight is caged and watches until the room is won or wiped",
    }
    return QD._death
end

-- t.party.allow_death(reason) -> "ok", detail. Declares that a death of THIS
-- raider is part of the test's plan (a smoke's deliberate death; a Normal room
-- where the sources' trio loses a raider and still clears). The member's
-- `player.died` row is then PASS and quotes `reason`. Without it the row is
-- FAIL -- the run still goes on in lock step, but it is red. The leader may
-- not allow its own death: the leader's death ends the world.
QD.party._death_allowed = nil
QD.party._death_written = false

function QD.party.allow_death(reason)
    assert(type(reason) == "string")
    assert(reason ~= "")
    if QD.party.role() == 1 then
        return "refused", "party.allow_death: p1 is the leader, whose death ends the world and the run"
            .. " (player.died FAIL); only a member's death can be allowed"
    end
    QD.party._death_allowed = reason
    return "ok", "party.allow_death: p" .. QD.party.role() .. " may die: " .. reason
end

QD.player._death_fence_solo = QD.player._death_fence

function QD.player._death_fence(context)
    if QD.party.role() == 1 then
        return QD.player._death_fence_solo(context)
    end
    local record = QD.player._death_seen()
    if not record then
        return false
    end
    if QD.party._death_written then
        return true
    end
    QD.party._death_written = true
    -- combat.lua's own latch, so its comments' "reported once" holds too.
    QD._death_reported = true
    local tick_result, tick = QD.tick()
    local tick_text = tick_result == "ok" and ("world tick " .. tostring(tick)) or "no world tick"
    QD.shot("player.died", true)
    local verdict = QD.party._death_allowed ~= nil and "PASS" or "FAIL"
    -- combat.lua's zero-hitpoints record (deaths = 0) speaks of a quest's
    -- respawn point; a raider's death is read in the Theatre's words.
    local reading = QD.player._death_text(record)
    if record.deaths == 0 then
        reading = "hitpoints read " .. tostring(record.hitpoints) .. " at " .. tostring(record.where)
            .. " on drive tick " .. tostring(record.tick) .. " (the killing blow; the death line follows"
            .. " the death animation)"
    end
    QD.step("player.died", verdict, string.format(
        "p%d died, seen on %s: %s -- read by %s; a party member's death is not the end of its run"
            .. " (seam22): it stays in lock step to the leader's end, and its fenced verbs now answer"
            .. " refused; %s",
        QD.party.role(), tick_text, reading, tostring(context),
        QD.party._death_allowed ~= nil and ("allowed: " .. QD.party._death_allowed)
            or "NOT allowed (t.party.allow_death was not called), so this row is FAIL"))
    return true
end

-- t.ticklog.rows(opts) gains `area` (raid seam22): {x0, z0, x1, z1} or
-- {x0, z0, x1, z1, level}, world tiles, inclusive, either corner first. A row
-- is kept when its own tile is inside: `x, z` (every packed `coord` field is
-- unpacked to them, and npc_tile carries them), else `dst_x, dst_z` (a
-- projectile's landing tile); a row with neither (hit_player, hit_npc, a
-- mark) is DROPPED, so combine `area` with `kind`. Applied after the C side's
-- kind and slot filters, before the row lands in the result: the leader's
-- npc_spawn rows hold every region npc (506 in a Normal Maiden run), and an
-- area of the arena returns the room's own. The rows still cost their Lua
-- tables while they are read, so a whole-log query of a kind the C side does
-- not filter (npc_tile with no slot) can still exhaust the instruction budget
-- (PLUGIN_LUA_STEP_BUDGET, 400000 per resume, torirs_plugin_lua.c): give
-- `slot` or a `since` serial there.
assert(QD.ticklog.rows ~= nil, "raid.lua loads after ticklog.lua (DRIVE_SCRIPT_PARTS, seam22)")
QD.ticklog._rows_unfiltered = QD.ticklog.rows

function QD.ticklog._area_keep(area)
    local x0, z0, x1, z1 = area[1], area[2], area[3], area[4]
    if x0 > x1 then x0, x1 = x1, x0 end
    if z0 > z1 then z0, z1 = z1, z0 end
    local level = area[5]
    return function(row)
        local x, z, l = row.x, row.z, row.level
        if x == nil or z == nil then
            x, z, l = row.dst_x, row.dst_z, row.dst_level
        end
        if x == nil or z == nil then
            return false
        end
        if level ~= nil and l ~= level then
            return false
        end
        return x >= x0 and x <= x1 and z >= z0 and z <= z1
    end
end

function QD.ticklog.rows(opts)
    if opts == nil or opts.area == nil then
        return QD.ticklog._rows_unfiltered(opts)
    end
    local area = opts.area
    if type(area) ~= "table" or math.type(area[1]) ~= "integer" or math.type(area[2]) ~= "integer"
        or math.type(area[3]) ~= "integer" or math.type(area[4]) ~= "integer"
        or (area[5] ~= nil and math.type(area[5]) ~= "integer") then
        return "refused", "t.ticklog.rows: area wants {x0, z0, x1, z1[, level]} in world tiles, got "
            .. tostring(area)
    end
    local inside = QD.ticklog._area_keep(area)
    local copy = {}
    for key, value in pairs(opts) do
        copy[key] = value
    end
    copy.area = nil
    local where = opts.where
    if where ~= nil then
        copy.where = function(row) return inside(row) and where(row) end
    else
        copy.where = inside
    end
    return QD.ticklog._rows_unfiltered(copy)
end

-- ==========================================================================
-- SEAM raid_play_by_tick_intent (raid seam27, 2026-10-05) -- THE PLAY
-- LIBRARY.  Everything below this banner is this seam's.
--
--   t.raid.play(plan_id, opts) -> ok | died | timeout | unsupported, detail, record
--
-- The owner, 2026-10-05: "the driver is not very fast or good. That is not
-- going to work in normal mode. You will need to code up the agents a lot
-- smarter using the actual strategies."  Each room test used to carry its own
-- fight loop: one action per pass, reacting after the fact.  This is ONE loop
-- every room test calls, plus one strategy table per room (the PLAN), each
-- line citing its source; docs/minigames/raid_loop/PLAY_NOTES.md is the
-- table of skills and plans with the same citations.
--
-- THE LOOP (QD.raid._play_tick).  Every server tick: SEE what a person at the
-- screen can see (the boss's animation and tile, the shadows on the floor,
-- its own hitpoints, prayer points, lit prayers, tile, and the swings of its
-- own weapon), DECIDE the tick's whole intent (the plan's decide function),
-- SEND it together (prayers, potion, food and the step in ONE t.together; the
-- attack press after it).  Nothing waits across ticks: a walk is re-issued
-- only when its target changes, never waited out.  What the loop never reads:
-- the server's registers, `::tob*` readouts, the seed, the tick log's hidden
-- columns.  The one tick-log kind it reads is `player_anim` for its own pid
-- (the swing it sees itself make) and, to stop, the boss's `npc_death`; a
-- member (no tick log) counts its swings from its presses and the speed.
--
-- THE SKILLS, each a small function below with its source in a comment
-- (PLAY_NOTES.md "Skills"): _play_attack (attack on cooldown), _play_pray
-- (pray by the telegraph), _play_supplies (eat by the largest hit before the
-- next chance to eat; potions on their own timer), _play_hazard (step off a
-- marked tile by the shortest safe step).  A loadout swap is the plan's
-- `gear` list in the same t.together (Bloat's plan needs none: PLAY_NOTES).
-- ==========================================================================

-- Weapons: attack speed in ticks and the swing animation the player sees.
-- scythe_of_vitur: wiki Scythe of vitur, attack speed 5; seq 8056 measured in
-- build/quest_gate/tob_bloat/ticklog.tsv (player_anim every 5 ticks, 104..124).
QD.RAID_PLAY_WEAPONS = {
    scythe_of_vitur = { speed = 5, seqs = { [8056] = true } },
    scythe_of_vitur_uncharged = { speed = 5, seqs = { [8056] = true } },
}

-- Food, best heal first, at 99 Hitpoints: wiki Anglerfish (n/10 + 13 = 22,
-- overheals), Shark 20.  A brew is a POTION: its own timer and no attack
-- delay ("Potions do not incur the standard 3 tick attack or eat delay",
-- consume_shared.rs2:49), so it combos with a food in one tick ("marlin,
-- Saradomin brew, and halibut - in that order", wiki Food/Fast foods).
-- Saradomin brew heals 2 + 15% = 16 at 99 (wiki Saradomin brew).
QD.RAID_PLAY_FOOD = {
    { item = "anglerfish", heal = 22 },
    { item = "shark", heal = 20 },
}
QD.RAID_PLAY_BREWS = { "br_1dosepotionofsaradomin", "br_2dosepotionofsaradomin",
    "br_3dosepotionofsaradomin", "br_4dosepotionofsaradomin" }
QD.RAID_PLAY_BREW_HEAL = 16
-- Super restore: 8 + 25% of the Prayer level = 32 at 99 (wiki Super restore).
QD.RAID_PLAY_RESTORES = { "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }
QD.RAID_PLAY_RESTORE_AMOUNT = 32
-- Food "adds a 3 tick penalty to when a player may eat again"; a potion
-- "delay[s] your next potion consumption by 3 ticks" (consume_shared.rs2:31-48).
QD.RAID_PLAY_EAT_DELAY = 3
QD.RAID_PLAY_DRINK_DELAY = 3
-- Running moves two tiles a tick (wiki Energy: run); used to time a leave.
QD.RAID_PLAY_RUN_TILES = 2

-- THE PLANS.  One table per room; `modes` holds what changes with the mode.
QD.RAID_PLAY_PLANS = {
    tob_bloat = {
        room = "bloat",
        boss = { entry = "tob_bloat_story", normal = "tob_bloat", hard = "tob_bloat_hard" },
        -- ENCOUNTER_TIMING.md 3.1 (blert BLOAT_DOWN_CYCLE_TICKS = 32): DOWN on T
        -- (seq 8082), attackable T+1..T+28, STOMP T+29, rise T+30..T+32, UP T+33.
        down_seq = 8082, down_ticks = 32, stomp_age = 29, rise_age = 30, up_age = 33,
        -- tob.constant ^tob_bloat_stomp_range = 6 ([M65]: no source gives a
        -- number), a huntall from Bloat's south-west tile.
        stomp_range = 6,
        -- ENCOUNTER_TIMING.md 3.4: graphics 1570-1573 mark the landing tile.
        shadow_lo = 1570, shadow_hi = 1573,
        -- Geometry local to Bloat's 64x64 map square (tob_bloat.lua: floor
        -- 6424..6437 x 89..102, tank 6428..6433 x 93..98 in square 6400,64).
        -- `mirror`: the tile straight behind the tank from Bloat's centre is
        -- (mirror.x - bx, mirror.z - bz) for Bloat's south-west tile bx,bz
        -- (tob_bloat.lua :1206, 12859 - bx and 189 - bz).
        floor = { 24, 25, 37, 38 }, tank = { 28, 29, 33, 34 }, mirror = { 59, 61 },
        ring = { 23, 24, 34, 35 },
        modes = {
            -- tob.constant :752 entry flies 8, :760 entry stomp 40,
            -- tob_bloat.constant :39 entry hand 25 ([video][M62]).  Entry: the
            -- stomp is tick-eaten in place (wiki :675 "It is possible to tick
            -- eat this attack"), the flinch is the click back on the rise.
            entry = { fly = 8, stomp = 40, hand = 25, stomp_plan = "stay" },
            -- tob.constant :746 flies 20, :756 stomp 80, :762 hand 50.  Normal:
            -- "Unless the boss is below 3% health, it is recommended to run
            -- away after the last attack" (wiki :689).
            normal = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave" },
            hard = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave" },
        },
        -- wiki :673 "reduced by 25% if Protect from Missiles are active":
        -- lit on every tick Bloat is up (the flies are sent every tick).
        walk_prayers = { "protectfrommissiles" },
        -- the offensive prayer for the attackable window; no source flicks it here.
        down_prayers = { "piety" },
        decide = "_play_bloat_decide",
    },
    -- Maiden: the full strategy table is PLAY_NOTES.md "Maiden"; the decide
    -- function is the re-author pass's (seam27 proves the library on Bloat).
    tob_maiden = {
        room = "maiden",
        boss = { entry = "tob_maiden_100_story", normal = "tob_maiden_100", hard = "tob_maiden_100_hard" },
        -- spec maiden.cad / maiden.first (grade B): first attack tick 9, every 10.
        attack_first = 9, attack_every = 10,
        modes = { entry = {}, normal = {}, hard = {} },
        walk_prayers = { "protectfrommagic" },
        down_prayers = {},
    },
}

-- t.raid.play(plan_id, opts): play the room by its plan until the boss dies
-- (ok), the player dies (died) or opts.max_ticks server ticks pass (timeout).
--   opts.mode    "entry" | "normal" | "hard" (default "entry")
--   opts.weapon  the worn weapon's symbol (default scythe_of_vitur)
--   opts.max_ticks (default 1500)
-- Returns result, detail, record (PLAY_NOTES.md "The record").
function QD.raid.play(plan_id, opts)
    opts = opts or {}
    local plan = QD.RAID_PLAY_PLANS[plan_id]
    if plan == nil then
        return "unsupported", "raid.play: no plan named " .. tostring(plan_id)
    end
    if plan.decide == nil then
        return "unsupported", "raid.play: plan " .. plan_id
            .. " has its strategy in PLAY_NOTES.md and no decide function yet (the re-author pass)"
    end
    local mode = opts.mode or "entry"
    local numbers = plan.modes[mode]
    assert(numbers, "raid.play: plan " .. plan_id .. " has no mode " .. tostring(mode))
    local weapon_name = opts.weapon or "scythe_of_vitur"
    local weapon = QD.RAID_PLAY_WEAPONS[weapon_name]
    assert(weapon, "raid.play: no weapon row for " .. tostring(weapon_name))
    local st = QD.raid._play_state(plan, plan_id, mode, numbers, weapon, opts)
    local max_ticks = opts.max_ticks or 1500
    local result = "timeout"
    while true do
        local outcome = QD.raid._play_tick(st)
        if outcome ~= nil then
            result = outcome
            break
        end
        local _, now = QD.tick()
        if now - st.start_tick >= max_ticks then
            break
        end
    end
    st.result = result
    return result, QD.raid._play_summary(st), st
end

function QD.raid._play_state(plan, plan_id, mode, numbers, weapon, opts)
    local _, now = QD.tick()
    local _, me = QD.world.tile()
    local st = {
        plan = plan, plan_id = plan_id, mode = mode, numbers = numbers, weapon = weapon,
        boss_symbol = plan.boss[mode], role = QD.party.role(), party = QD.party.size(),
        start_tick = now, origin = { x = math.floor(me.x / 64) * 64, z = math.floor(me.z / 64) * 64 },
        -- what happened, per server tick (the room test's rows read these)
        inputs = {}, hp_at = {}, prayer_at = {}, tile_at = {}, swings = {}, eats = {}, drinks = {},
        downs = {}, flinches = {}, dodges = 0, blocks = {}, refusals = 0, lines = {},
        last_swing = -1000, engaged = false, engaged_tick = -1000, last_eat = -1000, last_drink = -1000,
        walk_target = nil, boss_seen = false, boss_gone = 0, boss_slot = nil, anim_serial = 0,
        death_serial = 0, log = false, my_pid = nil,
    }
    -- The tick log is the leader's; a member reads none (README "A party run").
    local lr = QD.ticklog.rows({ kind = "mark" })
    st.log = (lr == "ok")
    local pr, rows = api_drive.players()
    if pr == "ok" then
        for _, r in ipairs(rows) do
            if r.me then st.my_pid = r.pid end
        end
    end
    return st
end

-- SEE: what a person at the screen reads this tick.
function QD.raid._play_see(st)
    local v = {}
    local _, now = QD.tick()
    v.tick = now
    v.api_now = api_drive.tick()
    local _, me = QD.world.tile()
    v.me = me
    local _, hp = QD.skill.read("hitpoints")
    v.hp = hp.level
    v.hp_base = hp.base or hp.base_level or 99
    local _, pp = QD.skill.read("prayer")
    v.prayer = pp.level
    v.prayer_base = pp.base or pp.base_level or 99
    local _, _, lit = QD.prayer.read()
    v.lit = lit or {}
    local br, b = QD.npc.state(st.boss_symbol)
    if br == "ok" then v.boss = b end
    v.shadows = {}
    local sr, spots = QD.world.spotanims(0)
    if sr == "ok" then
        for k = 1, #spots do
            local sid = spots[k].spotanim_id
            if sid >= (st.plan.shadow_lo or -1) and sid <= (st.plan.shadow_hi or -2) then
                v.shadows[spots[k].x * 100000 + spots[k].z] = true
            end
        end
    end
    -- the swing animation of the player's own weapon (player_anim, own pid)
    if st.log then
        local ar, rows = QD.ticklog.rows({ kind = "player_anim", since = st.anim_serial })
        if ar == "ok" then
            for _, row in ipairs(rows) do
                st.anim_serial = math.max(st.anim_serial, row.serial)
                if (st.party <= 1 or st.my_pid == nil or row.pid == st.my_pid) and st.weapon.seqs[row.seq] then
                    if row.tick > st.last_swing then
                        st.last_swing = row.tick
                        st.swings[#st.swings + 1] = row.tick
                    end
                end
            end
        end
    end
    st.hp_at[now] = v.hp
    st.tile_at[now] = { x = me.x, z = me.z }
    local on = {}
    for name, lit_now in pairs(v.lit) do
        if lit_now then on[#on + 1] = name end
    end
    st.prayer_at[now] = v.lit
    return v
end

-- SKILL: ATTACK ON COOLDOWN.  Once a target is clicked the player swings on
-- its own every `speed` ticks (wiki Attack speed: "the number of ticks
-- between attacks"); a click is needed only to START the fight or after a
-- step cleared it.  So the press goes out when the plan wants a swing and
-- the player is not engaged, or no swing was seen for speed + 2 ticks.  A
-- member with no tick log counts a swing every `speed` ticks of engagement.
-- Returns true when this tick should carry an attack press.
function QD.raid._play_attack(st, v, want)
    if not want then
        return false
    end
    local speed = st.weapon.speed
    if not st.log and st.engaged and v.tick - math.max(st.last_swing, st.engaged_tick) >= speed then
        st.last_swing = v.tick
        st.swings[#st.swings + 1] = v.tick
    end
    if not st.engaged then
        return true
    end
    return v.tick - math.max(st.last_swing, st.engaged_tick) > speed + 2
end

-- The next tick the weapon is ready (the "free" tick to eat on: "If your
-- weapon is ready to attack again then eating does not add any new delay",
-- wiki Food, quoted at consume_shared.rs2:34-38).
function QD.raid._play_next_swing(st, v)
    if not st.engaged then
        return v.tick
    end
    local next_swing = st.last_swing + st.weapon.speed
    if st.last_swing < st.engaged_tick then
        next_swing = st.engaged_tick + 1
    end
    if next_swing < v.tick then
        next_swing = v.tick
    end
    return next_swing
end

-- SKILL: PRAY BY THE TELEGRAPH.  `want` is the set the plan wants lit on the
-- NEXT tick (a prayer pressed between ticks T-1 and T is in force for T:
-- DRIVER_NOTES "Several inputs in one tick", varbit read back +0).  Returns
-- the switches to send: { {name, on}, ... }.
function QD.raid._play_pray(st, v, want, all)
    local out = {}
    if v.prayer <= 0 then
        return out
    end
    for _, name in ipairs(all) do
        local on = want[name] == true
        if (v.lit[name] == true) ~= on then
            out[#out + 1] = { name, on }
        end
    end
    return out
end

-- SKILL: SUPPLIES.  `threat(h)` is the most damage that can land in the next
-- h ticks (the plan's).  Eat when the hitpoints would not survive the hits
-- that can land before the NEXT chance to eat: on a free tick (not attacking,
-- or the weapon is ready) that chance is the next swing (engaged) or the eat
-- delay (3) away; on a tick between swings eating costs the attack 3 ticks,
-- so between swings only a hit that can land before the next tick's bite is
-- read forces one.  A brew rides along when the food alone is short (combo eating).
-- A restore is drunk when the prayer missing is at least one dose's worth
-- (no dose wasted) or prayer is about to run out.  Returns eat, drink names.
function QD.raid._play_supplies(st, v, threat)
    local eat, drink = nil, nil
    local food, heal = nil, 0
    for _, row in ipairs(QD.RAID_PLAY_FOOD) do
        local cr, n = QD.inv.count(row.item)
        if food == nil and cr == "ok" and n > 0 then
            food = row.item
            heal = row.heal
        end
    end
    local brew = nil
    for _, name in ipairs(QD.RAID_PLAY_BREWS) do
        local cr, n = QD.inv.count(name)
        if brew == nil and cr == "ok" and n > 0 then brew = name end
    end
    local next_swing = QD.raid._play_next_swing(st, v)
    local free = (not st.engaged) or next_swing <= v.tick
    local eat_ready = v.tick - st.last_eat >= QD.RAID_PLAY_EAT_DELAY
    local drink_ready = v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY
    -- the next chance to eat: a free tick's next free tick is the next swing
    -- (engaged) or the eat delay away, and a bite pressed then is confirmed
    -- up to QD.TOGETHER_CONFIRM_TICKS later (measured: svbbloat's bite on
    -- the swing two ticks before the stomp was not yet read one tick later),
    -- plus one tick to spare: the seam's margin, no source gives one
    -- (PLAY_NOTES.md "Supplies").  Between swings only a hit that can land
    -- before the next tick's bite is read forces one.
    local horizon = 2
    if free then
        horizon = (st.engaged and st.weapon.speed or QD.RAID_PLAY_EAT_DELAY) + QD.TOGETHER_CONFIRM_TICKS + 1
    end
    local need = threat(horizon)
    if v.hp <= need then
        if eat_ready and food ~= nil then
            eat = food
            if v.hp + heal <= need and drink_ready and brew ~= nil then drink = brew end
        elseif drink_ready and brew ~= nil then
            drink = brew
        end
    end
    if drink == nil and drink_ready then
        local missing = v.prayer_base - v.prayer
        if missing >= QD.RAID_PLAY_RESTORE_AMOUNT or v.prayer <= 2 then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, n = QD.inv.count(name)
                if drink == nil and cr == "ok" and n > 0 then drink = name end
            end
        end
    end
    return eat, drink, need
end

-- SKILL: HAZARDS.  A tile is dangerous from the tick a person can see its
-- marker (Bloat's shadow: ENCOUNTER_TIMING.md 3.4, judged on the previous
-- tick's tile, so a step on the tick it is seen lands in time).  Returns the
-- tile to stand on: `want` if it is safe, else the safe tile nearest it, the
-- shortest step from the player breaking ties.  `ok(x, z)` says a tile is
-- floor the player may stand on.
function QD.raid._play_hazard(st, v, want_x, want_z, ok)
    local key = want_x * 100000 + want_z
    if not v.shadows[key] and ok(want_x, want_z) then
        return want_x, want_z, false
    end
    local best, bx, bz = nil, v.me.x, v.me.z
    for r = 1, 3 do
        for dx = -r, r do
            for dz = -r, r do
                local x, z = want_x + dx, want_z + dz
                if ok(x, z) and not v.shadows[x * 100000 + z] then
                    local score = math.max(math.abs(dx), math.abs(dz)) * 10
                        + math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
                    if best == nil or score < best then
                        best, bx, bz = score, x, z
                    end
                end
            end
        end
        if best ~= nil then break end
    end
    return bx, bz, true
end

-- THE BLOAT PLAN'S DECIDE (PLAY_NOTES.md "Bloat").  Walk: hide straight
-- behind the tank from where Bloat will be ("Hug the pillar and hide from
-- Bloat as it walks around the room", wiki_Theatre_of_Blood_Strategies
-- :687), Protect from Missiles lit, off any shadow ("simply don't stand on
-- the shadows", transcripts/yt_4i4lv-srJkw.md:71).  Down: in at once and
-- swing on cooldown with Piety ("As soon as Bloat deactivates ... begin
-- attacking with melee ... five attacks when close", wiki :689).  The stomp:
-- Entry tick-eats it in place (wiki :675) and clicks back on the rise ("when
-- he starts to get back up ... that's when you click back", the flinch
-- guide, ENCOUNTER_TIMING.md 3.1); Normal/Hard runs out of its reach after
-- the last swing that fits ("run away after the last attack", wiki :689).
function QD.raid._play_bloat_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    local intent = { want = {}, walk = nil, attack = false }
    local b = v.boss
    if b == nil then
        return intent
    end
    local phase, age = "walk", -1
    if b.seq_id == P.down_seq then
        local a = v.api_now - b.seq_tick
        if a >= 0 and a <= P.down_ticks then
            phase, age = "down", a
        end
    end
    if phase == "down" and st.down_key ~= b.seq_tick then
        st.down_key = b.seq_tick
        st.down = { tick = v.tick - age, seen_age = age, bx = b.x, bz = b.z, index = #st.downs + 1 }
        st.downs[#st.downs + 1] = st.down
    end
    st.phase = phase
    local function floor_ok(x, z)
        local inside = x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
        local tank = x >= O.x + P.tank[1] and x <= O.x + P.tank[3] and z >= O.z + P.tank[2] and z <= O.z + P.tank[4]
        return inside and not tank
    end
    -- where Bloat will be two ticks on (its walk is visible); the hide tile
    -- is that tile mirrored through the tank (tob_bloat.lua :1206)
    local fx, fz = b.x, b.z
    if phase == "walk" and st.prev_b ~= nil then
        fx = math.max(O.x + P.ring[1], math.min(O.x + P.ring[3], b.x + 2 * (b.x - st.prev_b.x)))
        fz = math.max(O.z + P.ring[2], math.min(O.z + P.ring[4], b.z + 2 * (b.z - st.prev_b.z)))
    end
    st.prev_b = { x = b.x, z = b.z }
    local hide_x, hide_z = 2 * O.x + P.mirror[1] - fx, 2 * O.z + P.mirror[2] - fz
    local in_stomp = math.max(math.abs(v.me.x - b.x), math.abs(v.me.z - b.z)) <= P.stomp_range
    local hidden = math.max(math.abs(v.me.x - hide_x), math.abs(v.me.z - hide_z)) <= 1
    local on_shadow = v.shadows[v.me.x * 100000 + v.me.z] == true
    local leave_age = P.stomp_age - 1 - math.ceil((P.stomp_range + 1) / QD.RAID_PLAY_RUN_TILES)
    local function threat(h)
        local total = 0
        for k = 1, h do
            if phase == "walk" then
                if not hidden then total = total + N.fly end
            else
                local a = age + k
                if a == P.stomp_age and (N.stomp_plan == "stay" or in_stomp) then total = total + N.stomp end
                if a >= P.up_age then total = total + N.fly end
            end
        end
        if on_shadow then total = total + N.hand end
        return total
    end
    -- prayers for the NEXT tick: down prayers through the attackable window,
    -- the walk's prayer from the tick before the first fly (T+33)
    local list = P.walk_prayers
    if phase == "down" and age < P.up_age - 1 then list = P.down_prayers end
    for _, name in ipairs(list) do intent.want[name] = true end
    local target_x, target_z = nil, nil
    if phase == "walk" then
        target_x, target_z = hide_x, hide_z
    elseif N.stomp_plan == "stay" then
        intent.attack = age < P.stomp_age
        if age >= P.rise_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
        end
    else
        intent.attack = age < leave_age
        if age >= leave_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
        end
    end
    if phase == "down" and age >= P.stomp_age - 2 and age <= P.stomp_age - 1 and st.down.pre_stomp == nil then
        st.down.pre_stomp = v.hp
    end
    if target_x ~= nil then
        local sx, sz, moved = QD.raid._play_hazard(st, v, target_x, target_z, floor_ok)
        if on_shadow then st.dodges = st.dodges + 1 end
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (v.me.x ~= sx or v.me.z ~= sz) and (not same or stuck) then
            intent.walk = { x = sx, z = sz }
            if phase == "down" and st.down.flinch == nil then
                st.down.flinch = { tick = v.tick, age = age, from = { x = v.me.x, z = v.me.z } }
                st.flinches[#st.flinches + 1] = st.down.flinch
            end
        end
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end

-- SEND: the tick's whole intent, together (prayers first, potions and food,
-- the step last: DRIVER_NOTES "How a fight loop is written now"); the attack
-- press after the block (a slow verb may not sit inside one).
function QD.raid._play_send(st, v, intent)
    local all = {}
    for _, name in ipairs(st.plan.walk_prayers) do all[#all + 1] = name end
    for _, name in ipairs(st.plan.down_prayers) do all[#all + 1] = name end
    local switches = QD.raid._play_pray(st, v, intent.want, all)
    local eat, drink, walk = intent.eat, intent.drink, intent.walk
    local n = 0
    if #switches > 0 or eat ~= nil or drink ~= nil or walk ~= nil then
        local r, d = QD.together(function()
            for _, s in ipairs(switches) do QD.prayer.set(s[1], s[2]) end
            if drink ~= nil then QD.player.drink(drink) end
            if eat ~= nil then QD.player.eat(eat) end
            if walk ~= nil then QD.player.walk_to(walk.x, walk.z, 1) end
        end)
        n = #switches + (eat and 1 or 0) + (drink and 1 or 0) + (walk and 1 or 0)
        st.blocks[r] = (st.blocks[r] or 0) + 1
        if r ~= "ok" and r ~= "split" then
            st.refusals = st.refusals + 1
            if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 160) end
        end
        if eat ~= nil then
            st.last_eat = v.tick
            st.eats[#st.eats + 1] = { tick = v.tick, item = eat, hp = v.hp, need = intent.need }
        end
        if drink ~= nil then
            st.last_drink = v.tick
            st.drinks[#st.drinks + 1] = { tick = v.tick, item = drink, hp = v.hp, prayer = v.prayer }
        end
        if walk ~= nil then
            st.engaged = false
            st.walk_target = walk
        end
    end
    if intent.attack then
        local ar = QD.player.attack(st.boss_symbol, 2, 1, { quick = true })
        n = n + 1
        st.attack_presses = (st.attack_presses or 0) + 1
        if ar == "ok" then
            st.engaged = true
            st.engaged_tick = v.tick
            st.walk_target = nil
        elseif #st.lines < 6 then
            st.lines[#st.lines + 1] = "t" .. v.tick .. " attack " .. tostring(ar)
        end
    end
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + n
end

-- One turn of the loop: SEE, stop if the room is over, DECIDE, SEND, then
-- wait for the next server tick (the loop's beat, never a wait for an effect).
function QD.raid._play_tick(st)
    local v = QD.raid._play_see(st)
    for _, f in ipairs(st.flinches) do
        if f.moved == nil and v.tick >= f.tick + 3 then
            f.moved = math.max(math.abs(v.me.x - f.from.x), math.abs(v.me.z - f.from.z))
        end
    end
    if v.hp <= 0 or (st.last_me ~= nil and math.abs(v.me.x - st.last_me.x) + math.abs(v.me.z - st.last_me.z) > 20) then
        st.end_tick = v.tick
        return "died"
    end
    if v.boss ~= nil then
        st.boss_seen = true
        st.boss_gone = 0
        if st.log and st.boss_slot == nil then
            local sr, slot = QD.ticklog.slot(v.boss)
            if sr == "ok" then st.boss_slot = slot end
        end
    elseif st.boss_seen then
        st.boss_gone = st.boss_gone + 1
    end
    if st.log and st.boss_slot ~= nil then
        local dr, rows = QD.ticklog.rows({ kind = "npc_death", slot = st.boss_slot, since = st.death_serial })
        if dr == "ok" and #rows > 0 then
            st.death_tick = rows[1].tick
            st.end_tick = v.tick
            return "ok"
        end
    end
    if st.boss_gone >= 3 then
        st.end_tick = v.tick
        return "ok"
    end
    local intent = QD.raid[st.plan.decide](st, v)
    QD.raid._play_send(st, v, intent)
    st.last_me = { x = v.me.x, z = v.me.z }
    local _, after = QD.tick()
    if after == v.tick then
        QD.ticks(1)
    end
    return nil
end

-- One line: what the play did, and the inputs-per-tick histogram.
function QD.raid._play_summary(st)
    local hist = { 0, 0, 0, 0 }
    local ticks = 0
    for _, n in pairs(st.inputs) do
        if n > 0 then
            ticks = ticks + 1
            local k = math.min(n, 4)
            hist[k] = hist[k] + 1
        end
    end
    local flinch = "none"
    if st.flinches[1] ~= nil then flinch = tostring(st.flinches[1].moved) .. " tiles" end
    local blocks = {}
    for r, c in pairs(st.blocks) do blocks[#blocks + 1] = r .. " " .. c end
    table.sort(blocks)
    return string.format("plan %s mode %s role %d: %s after %d ticks (tick %d..%s); %d downs, %d swings, %d eats, "
        .. "%d drinks, %d shadow steps, first flinch %s; inputs on %d ticks: 1 on %d, 2 on %d, 3 on %d, 4+ on %d; "
        .. "attack presses %d; blocks [%s]; %s",
        st.plan_id, st.mode, st.role, tostring(st.result), (st.end_tick or st.start_tick) - st.start_tick,
        st.start_tick, tostring(st.end_tick), #st.downs, #st.swings, #st.eats, #st.drinks, st.dodges, flinch,
        ticks, hist[1], hist[2], hist[3], hist[4], st.attack_presses or 0, table.concat(blocks, ", "),
        #st.lines > 0 and table.concat(st.lines, " | ") or "no refusals")
end
