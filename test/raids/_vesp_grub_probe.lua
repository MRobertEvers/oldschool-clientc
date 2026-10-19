-- _vesp_grub_probe: the grubs as the CLIENT shows them (scratch probe): each
-- grub's yaw in template terms (instance yaw - 512 x the room's turn) and
-- whether its health bar is drawn, while Vespula tags them (nobody attacks).
-- RSProx (820, 843, 3389): each grub keeps the angle it was added with
-- (facing her hover), and a stung grub's bar (headbar id 0) is sent on every
-- fill step -- drawn throughout the drain.
--   python3 tools/raid_gate/run.py --script test/raids/_vesp_grub_probe.lua --name vgrubprobe5 --no-publish
local SAFE = { [0] = { 13, 12 }, [1] = { 14, 8 }, [2] = { 17, 12 } }
local FORMS = { "raids_vespula_caterpillar_healthy", "raids_vespula_caterpillar_sickly",
                "raids_vespula_caterpillar_infected", "raids_vespula_caterpillar_dead" }

local function run(t)
    local user = string.gsub(t.session._user() or "", "_p%d+$", "")
    local n = tonumber(string.match(user, "(%d+)$") or "0") or 0
    local er = t.raid.cox_test_enter("vespula", n)
    t.check("probe.enter", er == "ok", tostring(er))
    t.cheat("::god 1", false)
    t.ticks(3)
    local lr, row = t.world.loc_near("raids_vespula_tendrils_entrance", 40)
    local room = (lr == "ok" and row) and t.COX.room_at(row.tile_x, row.tile_z, row.level or 0) or nil
    t.check("probe.room", room ~= nil, room and string.format("v%d t%d", room.variant, room.turn) or "")
    if room == nil then t.finish(1) return end
    -- into the room (she wakes on a raider inside), then stand: she tags
    local s = SAFE[room.variant]
    local x, z = t.COX.world(room, s[1], s[2])
    t.player.goto_tile(x, z, room.level)
    for i = 1, 9 do
        local parts = {}
        for _, sym in ipairs(FORMS) do
            local tr, _, rows = t.npc.tiles(sym, 40)
            for _, r in ipairs((tr == "ok" and rows) or {}) do
                local lx, lz = t.COX.template(room, r.x, r.z)
                local yaw = r.yaw and ((r.yaw - 512 * room.turn) % 2048) or -1
                parts[#parts + 1] = string.format("%s@%d,%d yaw %s->tmpl %d bar %s %s/%s",
                    string.sub(sym, 28), lx, lz, tostring(r.yaw), yaw, tostring(r.health_active),
                    tostring(r.health_ratio), tostring(r.health_scale))
            end
        end
        t.check("probe.grubs", true, string.format("s%d %s", i, table.concat(parts, " | ")))
        t.ticks(8)
    end
    t.finish(0)
end

return { id = "_vesp_grub_probe", fixture = "fresh_lumbridge.ini", max_frames = 60000,
    setup = { "::clearinv", "::setlevel hitpoints 99" }, run = run }
