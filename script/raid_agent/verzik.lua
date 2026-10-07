-- raid_agent / verzik: the Normal Verzik trio, as a policy (raid seam55).
--
-- step(world, me, mem, seats, mems) -> intent | "quit" | nil, once per bot
-- per tick.  The setup is the _vzslow kit (test/raids/_vzslow.lua) spelled as
-- cheats; the fight is Measure (world.lua) -> Decide (here) -> Act (act.lua).

local World = require("world")
local V = {}

local VERZIK = {
    verzik_initial = true, verzik_phase1 = true, verzik_phase1_to2_transition = true,
    verzik_phase2 = true, verzik_phase2_to3_transition = true, verzik_phase3 = true,
}
local ATTACKABLE = { verzik_phase1 = true, verzik_phase2 = true, verzik_phase3 = true }

local function kit(seat)
    local k = { "clearinv", "tobkit" }
    local worn = {
        [2] = { "oathplate_helm", "oathplate_chest", "oathplate_legs" },
        [3] = { "neitiznot_faceguard", "tzhaar_cape_fire", "bandos_chestplate", "bandos_skirt" },
    }
    for _, item in ipairs(worn[seat] or {}) do
        k[#k + 1] = "give " .. item .. " 1"
        k[#k + 1] = "wield " .. item
    end
    for _, c in ipairs({
        "setlevel slayer 37", "give slayer_boots 1", "wield slayer_boots", "clearinv",
        "setlevel attack 99", "setlevel strength 99", "setlevel prayer 99", "setlevel magic 99",
        "setlevel agility 99", "give serpentine_helm_charged 1",
        "give br_4dosepotionofsaradomin 4", "give br_4dose2restore 4", "give br_4dose2combat 2",
        "give dragon_claws 1",
    }) do k[#k + 1] = c end
    if seat == 1 then k[#k + 1] = "give verzik_special_weapon 1" end
    if seat >= 2 then k[#k + 1] = "give noxious_halberd 1" end
    k[#k + 1] = "give anglerfish " .. ((seat == 1) and 16 or 14)
    return k
end

-- The run's report line, once, to stderr.
local function report(world, mems, verdict)
    local p = {}
    for pid, m in pairs(mems) do
        local pl = world.players[pid]
        p[#p + 1] = "s" .. m.seat .. " hp" .. tostring(pl and pl.hp) .. (m.died and (" died t" .. m.died) or "")
    end
    table.sort(p)
    io.stderr:write("verzik: ", verdict, " at t", world.tick, " | ", table.concat(p, " | "), "\n")
end

function V.step(world, me, m, seats, mems)
    local t = world.tick
    m.t0 = m.t0 or t
    local age = t - m.t0
    -- SETUP: kit on tick 1, the leader enters on 3, members join on 5, the leader starts on 7
    if age == 1 then return { cheat = kit(m.seat), why = "kit" } end
    if age == 3 and m.seat == 1 then return { cheat = { "tobmode 6 1" }, why = "enter" } end
    if age == 5 and m.seat > 1 then return { cheat = { "tobjoinroom 1" }, why = "join" } end
    if age == 7 and m.seat == 1 then return { cheat = { "tobgo" }, why = "start" } end
    if age < 9 then return nil end

    -- the run ends when every raider is dead, or Verzik is gone after being seen
    if me.died_tick ~= nil and not m.died then m.died = me.died_tick end
    local vz = world:find(VERZIK, me.x, me.z)[1]
    if vz ~= nil then m.saw_verzik = true end
    if m.seat == 1 then
        local alive = 0
        for _, pid in ipairs(seats) do if mems[pid].died == nil then alive = alive + 1 end end
        if alive == 0 then report(world, mems, "WIPE") return "quit" end
        if m.saw_verzik and vz == nil then report(world, mems, "VERZIK GONE") return "quit" end
        if age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    end
    if m.died ~= nil then return nil end

    local intent = { why = "" }
    -- prayers once: protect from magic and piety (a press toggles)
    if not m.prayed then
        intent.pray = { "protectfrommagic", "piety" }
        m.prayed = true
    end
    -- eat under half
    if me.hp ~= nil and me.hpmax ~= nil and me.hp < me.hpmax / 2 and (m.ate or -9) + 3 <= t then
        local slot, it = World.inv_slot(me, "anglerfish")
        if slot ~= nil then
            intent.op = { { 1, it.obj, slot } }
            m.ate = t
            intent.why = intent.why .. "eat "
        end
    end
    -- attack whatever form is attackable
    if vz ~= nil and ATTACKABLE[vz.name] and me.target ~= vz.slot then
        intent.attack = vz.slot
        intent.why = intent.why .. "attack " .. vz.name
    end
    return intent
end

return V
