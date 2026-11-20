-- levelup_combat: a level-up box that opens in the middle of a fight, as Jagex
-- runs it (docs/rsprox-analysis/levelup/README.md, "In a fight").
--
-- The recordings (every session holding interface 233, 689 boxes):
--   * The box holds the fight. A latched attack does not swing while the box
--     is up: index 4555 [3371]/[3375]/[3379] Torag's hammers every 4 ticks, the
--     box [3381], no swing at [3383], the next swing [3387] -- the tick the box
--     closed. No box, in any session, stayed up across a tick on which the
--     recorder swung.
--   * A hit on the player closes it, on the hit's tick: 78 boxes closed with no
--     client input on a tick the recorder took a hit (22 sessions; index 4517
--     [3992] the Hitpoints box opened and shut on a spider's 0, 4521 [281]), and
--     not one box stayed up across a tick on which the recorder was hit.
--   * So a swing does not close it: the one swing that shares a closing tick
--     with no hit is a client click (index 4429 [1789] OPNPCT -> [1790]).
--
-- Both rungs a swing runs in: melee (op, adjacent) and ranged (ap, at range).
-- For each: start the fight, raise Cooking for a box, then watch the server's
-- tick log until the box goes: no swing of the player's between the box and
-- the first hit on the player, and the box gone on that hit's tick.
--
--   TORIRSSERVER_DEV=1 ./src/build_opt/torirsserver --scriptrun test/levelup/levelup_combat.lua \
--       --bots 1 --name levelupcombat0 --ticks 600 --session /tmp/levelup_combat \
--       --fixture test/quests/fixtures/fresh_lumbridge.ini

local function tick_of(mark_detail)
    return tonumber(string.match(tostring(mark_detail), "at tick (%d+)"))
end

-- One leg: `style` names the rows. A swing is a `player_anim` row of the
-- player's other than the block animation it plays on the ticks it is hit.
local function leg(t, style, xp)
    local ar, ad = t.player.attack("man", 2, 30)
    t.check(style .. ".attack", ar == "ok", tostring(ar) .. " " .. tostring(ad))
    -- The box is raised just after one of the man's hits, so it opens between
    -- two of them (a hit on the box's own tick shuts it at once, as Jagex's
    -- does: index 4517 [3992]).
    local _, mw = t.ticklog.mark(style .. ".wait_hit")
    local wait_since = tonumber(string.match(tostring(mw), "serial (%d+)"))
    for _ = 1, 12 do
        t.ticks(1)
        local _, hs = t.ticklog.rows({ kind = "hit_player", since = wait_since })
        if type(hs) == "table" and #hs > 0 then
            break
        end
    end
    local _, m0 = t.ticklog.mark(style .. ".xp")
    local since = tonumber(string.match(tostring(m0), "serial (%d+)"))
    t.cheat("::xp cooking " .. tostring(xp))
    local br, bd = t.levelup.skill()
    t.check(style .. ".box", br == "ok" and type(bd) == "table" and bd.skill == "cooking",
        tostring(br) .. " " .. (type(bd) == "table" and tostring(bd.skill) or tostring(bd)))

    -- The box's tick: its jingle goes out with it (levelup.rs2's display).
    local _, jingles = t.ticklog.rows({ kind = "jingle", since = since })
    local open_tick = type(jingles) == "table" and jingles[1] and jingles[1].tick or nil
    t.check(style .. ".open_tick", open_tick ~= nil, "jingle rows " .. tostring(type(jingles) == "table" and #jingles))

    -- Watch up to 20 ticks: the last tick the client still shows the box.
    local last_up = nil
    for _ = 1, 20 do
        local _, md = t.ticklog.mark(style .. ".probe")
        local now = tick_of(md)
        if t.chat.kind() == "levelup" then
            last_up = now
        else
            break
        end
        t.ticks(1)
    end

    local _, hits = t.ticklog.rows({ kind = "hit_player", since = since })
    local first_hit = nil
    for _, row in ipairs(type(hits) == "table" and hits or {}) do
        if open_tick and row.tick >= open_tick then
            first_hit = row.tick
            break
        end
    end
    local hit_ticks, blocks = {}, {}
    for _, row in ipairs(type(hits) == "table" and hits or {}) do
        hit_ticks[row.tick] = true
    end
    local _, anims = t.ticklog.rows({ kind = "player_anim", since = since })
    anims = type(anims) == "table" and anims or {}
    for _, row in ipairs(anims) do
        if hit_ticks[row.tick] then
            blocks[row.seq] = true
        end
    end
    local held = {}
    for _, row in ipairs(anims) do
        if open_tick and first_hit and row.tick >= open_tick and row.tick < first_hit
            and row.seq ~= -1 and not blocks[row.seq] then
            held[#held + 1] = tostring(row.tick) .. ":" .. tostring(row.seq)
        end
    end
    local detail = "box " .. tostring(open_tick) .. ", last seen up " .. tostring(last_up)
        .. ", first hit on the player " .. tostring(first_hit)
    t.check(style .. ".hit_closes", first_hit ~= nil and last_up ~= nil
        and last_up >= first_hit - 1 and last_up <= first_hit, detail)
    t.check(style .. ".swing_held", first_hit ~= nil and #held == 0,
        detail .. "; swings while up " .. table.concat(held, ","))
end

return {
    id = "levelup_combat",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    -- Enough hitpoints to sit out a man's fists; a bow and arrows for the
    -- ranged leg (the test gives what it spends).
    setup = {
        "::setlevel hitpoints 60",
        "::give shortbow 1",
        "::give bronze_arrow 200",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()

        -- Melee: unarmed, the op rung.
        leg(t, "melee", 3000)
        t.ticks(6)

        -- Ranged: the ap rung.
        t.exec("ranged.wield_bow", t.player.inv_op, "shortbow", 2)
        t.exec("ranged.wield_arrows", t.player.inv_op, "bronze_arrow", 2)
        t.ticks(2)
        leg(t, "ranged", 7000)
    end,
}
