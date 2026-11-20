-- home_teleport: the Home Teleport channel, and walking out of it, as Jagex
-- sends them (RSProx index 4618 "Home Tele Break", rev 242; the schedule is in
-- the header of skill_magic/scripts/spells/home_teleport.rs2).
--
--   click  seq -1, spot -1
--   +0     chalk 4847 + 800      +6  sit 4850     +12 book 4853 + 802
--   +16    recite 4855 + 803 at height -10        +20 teleporting 4857 + 804
--   +23    the teleport, seq -1, spot -1          +24 seq -1
--
-- A walk breaks it ON THE STEP: the tick of the first step carries seq -1 and
-- spot -1 (4618 [128] in the chalk stage, [157] reciting) and nothing else
-- follows -- no stand-up (seq 4852 is in no recording), no later stage. The
-- bug this pins: a p_delay chain noticed the walk at its next resume, up to four
-- ticks on, and then played aide_standing_up (a DELAYMOVE seq) on the walker.
local SEQ = { chalk = 4847, sit = 4850, standup = 4852, book = 4853, recite = 4855, teleporting = 4857 }
local SPOT = { chalk = 800, book = 802, portal = 803, teleport = 804 }
local AIDE = { [4847] = true, [4850] = true, [4852] = true, [4853] = true, [4855] = true, [4857] = true }

return {
    id = "home_teleport",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    run = function(t)
        t.ticklog.start()
        t.ui.tab("magic")
        t.ticks(1)
        -- Where the fixture stands the player: Lumbridge, but off the landing
        -- square (the respawn coord, spread 2), so the landing is a move.
        local _, spawn = t.world.tile()
        local HOME = { x = spawn.x, z = spawn.z }

        local function rows(kind, since)
            local _, r = t.ticklog.rows({ kind = kind, since = since })
            return r or {}
        end
        local function cast(label)
            t.ticklog.mark(label)
            local marks = rows("mark")
            local since = marks[#marks].serial
            local _, w = t.ui.widget("magic_spellbook:teleport_home_standard")
            t.ui.invoke(w, 1)
            return since
        end
        local function at_tick(list, tick, field, value)
            for _, r in ipairs(list) do
                if r.tick == tick and r[field] == value then return true end
            end
            return false
        end
        local function describe(since)
            local parts = {}
            for _, r in ipairs(rows("player_anim", since)) do
                parts[#parts + 1] = "[" .. r.tick .. "] seq " .. tostring(r.seq)
            end
            for _, r in ipairs(rows("player_spotanim", since)) do
                parts[#parts + 1] = "[" .. r.tick .. "] spot " .. tostring(r.spotanim)
                    .. " h" .. tostring(r.height)
            end
            return table.concat(parts, ", ")
        end
        -- The tick the chalk stage started on, and the click's own seq -1 before it.
        local function chalk_tick(since)
            for _, r in ipairs(rows("player_anim", since)) do
                if r.seq == SEQ.chalk then return r.tick end
            end
            return nil
        end
        -- The first tick the player stands off `from`.
        local function step_tick(since, from)
            for _, r in ipairs(rows("player_tile", since)) do
                if r.x ~= from.x or r.z ~= from.z then return r.tick end
            end
            return nil
        end

        local function walk_out(label, wait)
            local since = cast(label)
            t.ticks(wait)
            t.player.walk_to(HOME.x + 1, HOME.z, 4)
            t.ticks(8)
            local K = chalk_tick(since)
            local S = step_tick(since, HOME)
            local anims = rows("player_anim", since)
            local spots = rows("player_spotanim", since)
            local late = {}
            for _, r in ipairs(anims) do
                if S and r.tick > S and AIDE[r.seq] then late[#late + 1] = r.seq end
                if r.seq == SEQ.standup then late[#late + 1] = "standup" end
            end
            t.check(label .. ".stops_on_step", K ~= nil and S ~= nil and S > K
                and at_tick(anims, S, "seq", -1) and at_tick(spots, S, "spotanim", -1) and #late == 0,
                "chalk [" .. tostring(K) .. "], first step [" .. tostring(S) .. "] (want seq -1 and spot -1"
                .. " on it, nothing after): " .. describe(since))
            t.player.walk_to(HOME.x, HOME.z, 4)
            t.ticks(2)
        end

        -- 4618 [124] chalk, [128] the step: four ticks into the chalk stage.
        walk_out("break.chalk", 4)
        -- 4618 [139] chalk, [157] the step: two ticks into the recite stage.
        walk_out("break.recite", 18)

        local since = cast("complete")
        t.ticks(30)
        local K = chalk_tick(since)
        local anims = rows("player_anim", since)
        local spots = rows("player_spotanim", since)
        local S = step_tick(since, HOME)
        local ok = K ~= nil
            and at_tick(anims, K - 1, "seq", -1) and at_tick(spots, K - 1, "spotanim", -1)
            and at_tick(spots, K, "spotanim", SPOT.chalk)
            and at_tick(anims, K + 6, "seq", SEQ.sit)
            and at_tick(anims, K + 12, "seq", SEQ.book) and at_tick(spots, K + 12, "spotanim", SPOT.book)
            and at_tick(anims, K + 16, "seq", SEQ.recite) and at_tick(spots, K + 16, "spotanim", SPOT.portal)
            and at_tick(anims, K + 20, "seq", SEQ.teleporting) and at_tick(spots, K + 20, "spotanim", SPOT.teleport)
            and S == K + 23
            and at_tick(anims, K + 23, "seq", -1) and at_tick(spots, K + 23, "spotanim", -1)
            and at_tick(anims, K + 24, "seq", -1)
        local portal_height = nil
        for _, r in ipairs(spots) do
            if r.spotanim == SPOT.portal then portal_height = r.height end
        end
        t.check("complete.schedule", ok and portal_height == -10,
            "chalk [" .. tostring(K) .. "], landed [" .. tostring(S) .. "] (want +23), portal height "
            .. tostring(portal_height) .. " (want -10): " .. describe(since))
    end,
}
