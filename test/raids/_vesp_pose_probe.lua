-- _vesp_pose_probe: Vespula's pose on the CLIENT once enraged (scratch probe).
-- RSProx 820/843: enraged she keeps her tile; her enraged form's readyanim is
-- vespula_attack_melee_flying (the cache), so she jabs on the spot. This reads
-- what the client actually poses her with, every two ticks.
--   python3 tools/raid_gate/run.py --script test/raids/_vesp_pose_probe.lua --name vposeprobe5 --no-publish
local SWING = { [0] = { 17, 12 }, [1] = { 14, 12 }, [2] = { 13, 12 } }

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
    local s = SWING[room.variant]
    local x, z = t.COX.world(room, s[1], s[2])
    t.player.goto_tile(x, z, room.level)
    t.player.attack("raids_vespula_portal", 2, 3, { quick = true })
    for i = 1, 14 do
        t.ticks(2)
        for _, sym in ipairs({ "raids_vespula_enraged", "raids_vespula_flying" }) do
            local pr, detail = t.npc.pose(sym)
            if pr == "ok" then t.check("probe.pose", true, detail) end
        end
    end
    t.finish(0)
end

return { id = "_vesp_pose_probe", fixture = "fresh_lumbridge.ini", max_frames = 60000,
    setup = { "::clearinv", "::setlevel magic 99", "::setlevel hitpoints 99", "::~maxmage" }, run = run }
