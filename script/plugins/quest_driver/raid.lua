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
-- In a party of one it answers ok at once.
function QD.party.barrier(name, timeout_ticks)
    assert(type(name) == "string")
    assert(string.match(name, "^[%w_%-%.]+$"))
    local size = QD.party.size()
    if size <= 1 then
        return "ok", "party.barrier " .. name .. ": a party of one"
    end
    local mine = string.format("barrier.%s.p%d", name, QD.party.role())
    local marked = api_drive.barrier_mark(mine)
    if marked ~= "ok" then
        return marked, "party.barrier " .. name .. ": could not write " .. mine .. " (" .. tostring(marked) .. ")"
    end
    local start = api_drive.tick()
    local missing = {}
    local result = QD.await({
        level = function()
            missing = {}
            for n = 1, size do
                if api_drive.barrier_present(string.format("barrier.%s.p%d", name, n)) ~= "ok" then
                    missing[#missing + 1] = "p" .. n
                end
            end
            return #missing == 0
        end,
        note = "party.barrier " .. name,
    }, timeout_ticks or 200)
    if result ~= "ok" then
        return "timeout", string.format("party.barrier %s: p%d waited %d tick(s); still missing %s",
            name, QD.party.role(), api_drive.tick() - start, table.concat(missing, ","))
    end
    return "ok", string.format("party.barrier %s: all %d raiders, p%d waited %d tick(s)",
        name, size, QD.party.role(), api_drive.tick() - start)
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
