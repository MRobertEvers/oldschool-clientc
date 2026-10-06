-- _play_entry: the WHOLE Theatre of Blood, Entry Mode, one raider, end to end
-- through the PLAY LIBRARY (raid seam35e play_tob_entry_relay).  An underscore
-- harness, not a kept room: it enters the Theatre the way a player does (the
-- notice board in Ver Sinhaza, an Entry party of one, the door's ready check:
-- t.party.form / t.party.ready, the same verbs _party_smoke drives), then
-- plays each room through its library plan in order -- Maiden, Bloat,
-- Nylocas, Sotetseg, Xarpus, Verzik -- with ONE raid's supplies, walks the
-- passage between rooms (tob_party.rs2 "The way out": a cleared room's
-- barrier is a gate, the corridor ends at tob_dungeon_walkway_exit_clickbox,
-- Xarpus' at tob_dungeon_xarpus_arena_door_exit), takes the supply chest's
-- bandages after Bloat and Sotetseg (tob_chest.rs2; Entry page E:151, E:197),
-- the Dawnbringer from the skeleton after Xarpus (E:214), and goes down
-- Verzik's trapdoor into the reward room (tob_vault.rs2
-- tob_dungeon_verzik_throne_door_opened) and opens the chest.
--
-- No ::tob* room cheat anywhere: t.raid.enter is not called.  The readers
-- t.raid.state (::tobstate) and the tick log are read-only.
--
-- Each room's pre-fight (barrier, prayer, loadout) is its own harness's
-- (_play_maiden.lua, _play_smoke.lua, _play_nylocas.lua, _play_sotetseg.lua,
-- _play_xarpus.lua, _play_verzik.lua solo branches), with the room-local
-- tiles those harnesses used re-based on the room square the relay arrives
-- in.  The fight is ONE t.raid.play call per room.
--
-- THE KIT: one raid's.  The Entry Mode page (sources/
-- wiki_Theatre_of_Blood_Entry_Mode.wikitext :24-31): "Gear for all three
-- combat styles ... the player should be on the Ancient Magicks", "A method
-- of inflicting poison or venom", "Fast weapons for each attack style", "At
-- least 6 Saradomin brews, and one super restore per three brews", "The
-- remaining inventory space should be high-healing food"; :33 bandages after
-- the second and fourth bosses; :94 insulated boots "a mandatory requirement
-- if doing it in solo"; :17 "your Hitpoints and Prayer are replenished after
-- defeating each boss" (tob_raid.rs2 ~tob_restore).  The weapons and switch
-- sets are the ones each room's plan names (raid_play_tob_<room>.lua), so
-- every room is played by the plan that is green on five names; the armour
-- is ONE ranged set worn throughout (the Maiden plan's ranged_set) instead of
-- each harness's own, so the pack holds the switches and the supplies.
local size = (QD_PARTY and QD_PARTY.size) or 1
assert(size == 1, "_play_entry is the Entry SOLO relay (one raider)")

-- The lobby (the notice board, _party_smoke.lua LOBBY_X/Z)
local LOBBY_X, LOBBY_Z = 3662, 3216

-- The supplies the report counts per room (doses counted per potion).
local SUPPLY_ITEMS = {
    shark = "shark", tob_bandages = "tob_bandages",
    br_4dosepotionofsaradomin = "brew", br_3dosepotionofsaradomin = "brew",
    br_2dosepotionofsaradomin = "brew", br_1dosepotionofsaradomin = "brew",
    br_4dose2restore = "restore", br_3dose2restore = "restore",
    br_2dose2restore = "restore", br_1dose2restore = "restore",
    ["4dose2combat"] = "combat", ["3dose2combat"] = "combat",
    ["2dose2combat"] = "combat", ["1dose2combat"] = "combat",
}
local DOSES = {
    br_4dosepotionofsaradomin = 4, br_3dosepotionofsaradomin = 3, br_2dosepotionofsaradomin = 2, br_1dosepotionofsaradomin = 1,
    br_4dose2restore = 4, br_3dose2restore = 3, br_2dose2restore = 2, br_1dose2restore = 1,
    ["4dose2combat"] = 4, ["3dose2combat"] = 3, ["2dose2combat"] = 2, ["1dose2combat"] = 1,
}

local KIT = {
    "::clearinv",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
    "::setlevel ranged 99", "::setlevel magic 99", "::setlevel hitpoints 99",
    "::setlevel prayer 99", "::setlevel agility 99",
    -- insulated boots need Slayer 37 (_play_verzik.lua's kit; E:94)
    "::setlevel slayer 37",
    -- Ancient Magicks (E:26; Maiden's barrage, Nylocas' rush/burst)
    "::setvar varb4070_spellbook 1",
    -- WORN: the Maiden plan's ranged_set (raid_play_tob_maiden.lua :58) and
    -- the bow Verzik's P2/P3 is shot with (raid_play_tob_verzik.lua :29)
    "::give twisted_bow", "::wield twisted_bow",
    "::give dragon_arrow 1000", "::wield dragon_arrow",
    "::give masori_mask", "::wield masori_mask",
    "::give masori_body", "::wield masori_body",
    "::give masori_chaps", "::wield masori_chaps",
    "::give avas_assembler", "::wield avas_assembler",
    "::give slayer_boots", "::wield slayer_boots",
    "::give magus_ring", "::wield magus_ring",
    -- the neck slot is free: the melee amulet _play_xarpus.lua's kit wears
    -- (zenyte_amulet_enchanted), worn from the start so it costs no slot
    "::give zenyte_amulet_enchanted", "::wield zenyte_amulet_enchanted",
    -- CARRIED SWITCHES (16 slots)
    -- the Maiden plan's magic_set (:57) and flick weapon (:60)
    "::give kodai_wand", "::give ancestral_hat", "::give ancestral_robe_top",
    "::give ancestral_robe_bottom", "::give arcane", "::give abyssal_whip",
    -- the scythe: Bloat, Sotetseg, Xarpus (their plans' weapon)
    "::fullscythe",
    -- the Nylocas plan's ranged and magic loadout (raid_play_tob_nylocas.lua :131-133)
    "::give magic_shortbow", "::give rune_arrow 800", "::give lava_battlestaff",
    -- Ice Barrage (water 6, blood 2, death 4) and Ice Rush/Burst (water, chaos, death)
    "::give water_rune 2000", "::give blood_rune 1000", "::give death_rune 1000", "::give chaos_rune 1000",
    -- a super combat for the first two bosses (E:33; Bloat and Xarpus plans drink it)
    "::give 4dose2combat",
    -- the venom for Verzik's Athanatos (E:28 "A method of inflicting poison or
    -- venom"; raid_play_tob_verzik.lua :508-511 wears it for P2)
    "::give serpentine_helm_charged",
    -- SUPPLIES (12 slots): E:30 "At least 6 Saradomin brews, and one super
    -- restore per three brews", E:31 the rest high-healing food.  Six
    -- restores and no fish: the page counts on the chests' bandages for prayer
    -- (E:7 "act as a ... prayer [potion]"), and this content's bandages
    -- restore none (CONTENT_BUGS.md "From seam13": the prayer restore is not
    -- modelled, the item page gives no figure), so prayer is the budget
    -- (the relay's fourth run: 16 restore doses gone by the end of Sotetseg,
    -- Maiden 7, Bloat 1, Nylocas 6, Sotetseg 2) and the bandages are the food.
    "::give br_4dosepotionofsaradomin 6",
    "::give br_4dose2restore 6",
}

-- The switches no room after this one uses, left on the floor of the
-- corridor so the next supply chest's ten bandages fit the pack (E:151 "this
-- will always contain 10 bandages"; the chest gives only as many as fit:
-- 4 on the relay's second run, with the Maiden's magic set still carried).
local DROP_AFTER = {
    -- (a rune's backpack symbol has no underscore: bloodrune, as
    -- _play_nylocas.lua counts it; the ::give cheat takes either)
    maiden = { "kodai_wand", "ancestral_hat", "ancestral_robe_top", "ancestral_robe_bottom", "arcane", "bloodrune" },
    nylocas = { "magic_shortbow", "lava_battlestaff", "chaosrune", "waterrune", "deathrune", "abyssal_whip" },
}

-- ---------------------------------------------------------------- helpers
local R = {}            -- per room: mark, death, taken, hits, before, after, chest, result
local ORDER = { "maiden", "bloat", "nylocas", "sotetseg", "xarpus", "verzik" }
local PLAN = { maiden = "tob_maiden", bloat = "tob_bloat", nylocas = "tob_nylocas",
    sotetseg = "tob_sotetseg", xarpus = "tob_xarpus", verzik = "tob_verzik" }

local function origin_of(t)
    local _, here = t.world.tile()
    return math.floor(here.x / 64) * 64, math.floor(here.z / 64) * 64, here
end

local function supplies(t)
    local s = { shark = 0, tob_bandages = 0, brew = 0, restore = 0, combat = 0 }
    for item, kind in pairs(SUPPLY_ITEMS) do
        local r, n = t.inv.count(item)
        if r == "ok" and type(n) == "number" and n > 0 then
            s[kind] = s[kind] + n * (DOSES[item] or 1)
        end
    end
    return s
end

local function supplies_text(s)
    return string.format("sharks %d, bandages %d, brew doses %d, restore doses %d, combat doses %d",
        s.shark, s.tob_bandages, s.brew, s.restore, s.combat)
end

local function used_text(a, b)
    return string.format("sharks %d, bandages %d, brew doses %d, restore doses %d, combat doses %d",
        a.shark - b.shark, a.tob_bandages - b.tob_bandages, a.brew - b.brew, a.restore - b.restore, a.combat - b.combat)
end

local function newest_serial(t)
    local r, list = t.msg.last(1)
    if r == "ok" and type(list) == "table" and #list > 0 then return list[#list].serial or 0 end
    return 0
end

local function line_since(t, since, needle)
    local r, list = t.msg.last(80)
    if r ~= "ok" or type(list) ~= "table" then return nil end
    local found = nil
    for i = 1, #list do
        if (list[i].serial or 0) > since and string.find(list[i].text, needle, 1, true) then found = list[i].text end
    end
    return found
end

local function plain(text)
    return (string.gsub(string.gsub(tostring(text), "<br>", " "), "<[^>]*>", ""))
end

-- Wait for the raid's room register to read `name` with its boss in the pool
-- (Nylocas has no boss row: the room alone), the player standing still.
local function await_room(t, name, budget)
    local _, start = t.tick()
    local last_line = "none"
    while true do
        local r, st = t.raid.state()
        if r == "ok" then
            last_line = tostring(st.line)
            -- the Nylocas has no boss row in t.raid.state (raid.lua _ROOMS)
            if st.room == name and not st.cleared and (name == "nylocas" or st.boss_slot ~= nil) then
                -- the walk-in (tob_raid.rs2 [queue,tob_room_walkin]) and the title card settle
                local _, a = t.world.tile()
                t.ticks(2)
                local _, b = t.world.tile()
                if a.x == b.x and a.z == b.z then return "ok", st end
            end
        end
        local _, now = t.tick()
        if now - start > budget then return "timeout", "no " .. name .. " room within " .. budget .. " ticks; last state " .. last_line end
        t.ticks(1)
    end
end

-- The room's end: the corridor passage (or Xarpus' door) clicked until the
-- raid's register moves on.  `before` runs first (the barrier, the chest).
-- The tile beside each passage, local to the room square (tob_party.rs2 "The
-- way out": maiden (40,6), bloat (5,31), nylocas (39,51), sotetseg (15,5),
-- xarpus (33,48)), on the side the corridor reaches it from.  Walking into
-- the passage is the passage (tob_raid.rs2 ~tob_exit_walked: within
-- ^tob_exit_reach 1 of it while the room is cleared).
local PASSAGE_SIDE = { maiden = { 40, 7 }, bloat = { 6, 31 }, nylocas = { 38, 51 }, sotetseg = { 15, 6 }, xarpus = { 33, 47 } }

local function leave_room(t, name, next_name, ox, oz)
    local exit_loc = (name == "xarpus") and "tob_dungeon_xarpus_arena_door_exit" or "tob_dungeon_walkway_exit_clickbox"
    local xr, xd = t.player.click_loc(exit_loc, 1)
    t.ticks(3)
    local ar, ad = await_room(t, next_name, 20)
    if ar ~= "ok" then
        -- the click was covered (svcplayentry: "Examine Chamber" over the
        -- clickbox): walk into the passage instead
        local side = PASSAGE_SIDE[name]
        local wr, wd = t.player.walk_to(ox + side[1], oz + side[2], 40)
        xd = tostring(xd) .. "; walked to its side " .. tostring(wr)
        ar, ad = await_room(t, next_name, 20)
    end
    t.check(name .. ".passage", ar == "ok", "clicked " .. exit_loc .. " (" .. tostring(xr) .. " " .. string.sub(tostring(xd), 1, 160)
        .. "); next room: " .. (ar == "ok" and tostring(ad.line) or tostring(ad)))
    return ar == "ok"
end

local function hits_between(t, a, b)
    local _, rows = t.ticklog.rows({ kind = "hit_player" })
    t.ticks(1)
    local taken, n, big = 0, 0, 0
    for _, h in ipairs(rows or {}) do
        if h.tick >= a and h.tick <= b and (h.damage or 0) > 0 then
            taken = taken + h.damage
            n = n + 1
            if h.damage > big then big = h.damage end
        end
    end
    return taken, n, big
end

-- The fight is over: every prayer the plan left lit goes off.  A plan lights
-- what its room needs and stops when the boss dies, so Bloat's Piety was
-- still draining through the corridor and the whole Nylocas (the relay's
-- second run: prayer 76 at the Nylocas mark, 0 by t950, Vasilias unprayed).
local function prayers_off(t, name)
    local r, d, set = t.prayer.read()
    local off = {}
    if r == "ok" and type(set) == "table" then
        for prayer, lit in pairs(set) do
            if lit then
                t.prayer.set(prayer, false)
                off[#off + 1] = prayer
            end
        end
    end
    local r2, d2 = t.prayer.read()
    t.check(name .. ".prayers_off", r2 == "ok" and string.find(tostring(d2), ": 0 of ", 1, true) ~= nil,
        "switched off [" .. table.concat(off, " ") .. "]; now " .. string.sub(tostring(d2), 1, 120))
end

local function drop_spent(t, name)
    local list = DROP_AFTER[name]
    if list == nil then return end
    local dropped = {}
    for _, item in ipairs(list) do
        -- the Nylocas plan's last swap may leave a switch worn (its staff)
        local wr, worn_n = t.inv.count(item)
        if wr == "ok" and worn_n == 0 then t.player.unequip(item) end
        local r = t.player.drop(item)
        dropped[#dropped + 1] = item .. "=" .. tostring(r)
    end
    t.check(name .. ".drop_spent", true, "left in the corridor: " .. table.concat(dropped, " "))
end

-- Put a switch on unless it is already worn (the scythe stays on from
-- Sotetseg into Xarpus).
local function wear(t, row, item)
    local cr, n = t.inv.count(item)
    if cr == "ok" and n == 0 then
        -- no worn-items reader in the driver: an earlier room's equip row put
        -- it on, and the backpack holds none of it now
        t.check(row, cr == "ok" and n == 0, item .. " not in the backpack (worn since an earlier room's equip row)")
        return
    end
    t.exec(row, t.player.equip, item)
end

-- The barrier's question (tob_arena_barrier op1 -> "Yes, begin the fight.").
-- A press can come back `ok` without the question opening: svaplayentry's
-- Sotetseg press resolved as a floor walk (the shot's hover read "Walk here",
-- the raider stepped 6415,210 -> 6414,210) and the click's `chat_message`
-- was the stamina-expired line. So the row is the question itself: wait for
-- the options page, and press again (the barrier re-found from where the
-- raider now stands) when none opened, up to three presses.
local function begin_fight(t, name)
    local presses, cr, cd, opened = 0, nil, nil, false
    while presses < 3 and not opened do
        presses = presses + 1
        cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.await({ level = function() return t.chat.kind() == "options" end, note = name .. ": the barrier's question" }, 6)
        opened = t.chat.kind() == "options"
    end
    t.check(name .. ".barrier", opened, "the barrier's question open after " .. presses .. " press(es); last press "
        .. tostring(cr) .. " " .. string.sub(tostring(cd), 1, 120))
    local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
    t.check(name .. ".begin", pr == "ok", tostring(pd))
end

-- A stat's reading after a dose, read back until it moves off `from`
-- (the dose landed) or `ticks` server ticks pass.
local function await_stat(t, stat, from, ticks)
    local r, s = t.skill.read(stat)
    local waited = 0
    while waited < ticks and (r ~= "ok" or s.level == from) do
        t.ticks(1)
        waited = waited + 1
        r, s = t.skill.read(stat)
    end
    return s, waited
end

local RESTORE_DOSES = { "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }
local COMBAT_DOSES = { "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }

local function first_held(t, list)
    for _, dose in ipairs(list) do
        local cr, n = t.inv.count(dose)
        if cr == "ok" and n > 0 then return dose end
    end
    return nil
end

-- The super combat before a melee room's barrier, on the stats the raider
-- row reads NOW, not the base it started the raid on: the brews drain attack
-- and strength (E:29 "one super restore per three brews"), and a super
-- combat drunk on a drained stat boosts from there (svaplayentry: 77 + 5 +
-- 15% of 99 = 96, read as "the dose did not land"). So: a super restore
-- while attack or strength reads under its base, each dose read back; then
-- the combat dose, read back; the row asserts both above their base.
local function boost(t, name)
    local _, a0 = t.skill.read("attack")
    local _, s0 = t.skill.read("strength")
    local restored, rtext = 0, ""
    while restored < 3 and (a0.level < a0.base_level or s0.level < s0.base_level) do
        local dose = first_held(t, RESTORE_DOSES)
        if dose == nil then break end
        t.player.inv_op(dose, 1, { quick = true })
        restored = restored + 1
        local s1, w = await_stat(t, "strength", s0.level, 4)
        local _, a1 = t.skill.read("attack")
        rtext = rtext .. string.format(" %s: attack %d -> %d, strength %d -> %d (+%d tick(s));", dose, a0.level, a1.level, s0.level, s1.level, w)
        a0, s0 = a1, s1
    end
    -- A drink pressed inside the restore's potion delay is refused (e1playentry:
    -- the combat press right after the restore answered `timeout` and nothing
    -- moved), so the combat dose is pressed until strength reads above where
    -- it stood, up to three presses.
    local dose = first_held(t, COMBAT_DOSES)
    local r, w, presses = "none", 0, 0
    local s2 = s0
    while dose ~= nil and presses < 3 and s2.level == s0.level do
        presses = presses + 1
        r = t.player.inv_op(dose, 1, { quick = true })
        local ws
        s2, ws = await_stat(t, "strength", s0.level, 4)
        w = w + ws
        dose = s2.level == s0.level and first_held(t, COMBAT_DOSES) or dose
    end
    local _, a2 = t.skill.read("attack")
    local _, def = t.skill.read("defence")
    local _, style = t.var.varp("varp43_com_mode")
    t.check(name .. ".potion", s2.level > s2.base_level and a2.level > a2.base_level,
        "super combat before the barrier: strength " .. s0.level .. " -> " .. s2.level .. "/" .. s2.base_level
        .. ", attack " .. a0.level .. " -> " .. a2.level .. "/" .. a2.base_level .. ", defence " .. tostring(def and def.level)
        .. ", style " .. tostring(style) .. " (" .. tostring(dose) .. " " .. tostring(r) .. ", " .. presses .. " press(es), +" .. w .. " tick(s)); restores "
        .. restored .. ":" .. (rtext == "" and " none needed" or rtext))
end

local function mark_tick(t, label)
    t.ticklog.mark(label)
    local _, rows = t.ticklog.rows({ kind = "mark" })
    local tick = nil
    for i = 1, #(rows or {}) do
        if rows[i].label == label then tick = rows[i].tick end
    end
    return tick
end

-- ------------------------------------------------- each room's pre-fight
-- Each is its harness's solo branch, the local tiles re-based on `ox, oz`.
local PRE = {}

-- _play_maiden.lua :358-372: the barrier and its confirm; the bow is worn.
PRE.maiden = function(t, ox, oz)
    local br, brow = t.npc.nearest("tob_maiden_100_story", 30)
    t.check("maiden.boss", br == "ok", tostring(brow and brow.slot))
    begin_fight(t, "maiden")
    return { weapon = "twisted_bow", max_ticks = 1100 }
end

-- _play_smoke.lua :48-71 (tob_bloat.lua :47-59): the super combat, the
-- scythe, and the crossing when Bloat is on the far row heading west
-- ("enter the barrier when Bloat is on the other side of the pillar", E:134).
PRE.bloat = function(t, ox, oz)
    boost(t, "bloat")
    wear(t, "bloat.equip.scythe", "scythe_of_vitur")
    t.player.walk_to(ox + 42, oz + 31, 1)
    local wait_ticks, wait_x = 0, nil
    while wait_ticks < 60 do
        local wr, wb = t.npc.state("tob_bloat_story")
        if wr == "ok" and wb.z == oz + 24 and wb.x <= ox + 32 and wb.x >= ox + 31 and wait_x ~= nil and wb.x < wait_x then break end
        if wr == "ok" then wait_x = wb.x end
        wait_ticks = wait_ticks + 1
        t.ticks(1)
    end
    begin_fight(t, "bloat")
    return { weapon = "scythe_of_vitur", max_ticks = 1400 }
end

-- _play_nylocas.lua :181-218: rune arrows and the shortbow, rapid, auto
-- retaliate off, the barrier from beside the fight tile.
PRE.nylocas = function(t, ox, oz)
    wear(t, "nylocas.equip.arrows", "rune_arrow")
    t.exec("nylocas.equip.bow", t.player.equip, "magic_shortbow")
    t.ui.tab("combat")
    t.ticks(1)
    local _, sw = t.ui.widget("combat_interface:style_slot_1")
    t.ui.invoke(sw, 1)
    t.ticks(2)
    local _, style = t.var.varp("varp43_com_mode")
    t.check("nylocas.rapid", style == 1, "varp43_com_mode " .. tostring(style))
    local _, before = t.var.varp("varp172_option_nodef")
    if before == 0 then
        local _, rw = t.ui.widget("combat_interface:retaliate")
        t.ui.invoke(rw, 1)
    end
    t.ticks(2)
    local _, after = t.var.varp("varp172_option_nodef")
    t.check("nylocas.retaliate_off", after == 1, "varp172_option_nodef " .. tostring(before) .. " -> " .. tostring(after))
    local fr, fight, ftext = t.raid.start_tile()
    t.check("nylocas.start_tile", fr == "ok", tostring(ftext))
    t.player.walk_to(fight.x + 1, fight.z, 20)
    begin_fight(t, "nylocas")
    return { max_ticks = 2000 }
end

-- _play_sotetseg.lua :318-338: the scythe, Protect from Magic, the barrier
-- from two tiles south of the fight tile.
-- The super combat's second and third doses: Sotetseg and Xarpus (E:33
-- "combat potions ... are not necessary except for the first two bosses" --
-- with bandages to boost; the kit's armour is the bow's, so the scythe rooms
-- take the boost the potion still holds: the relay's fifth run lost Xarpus
-- to a P2 that ran past the harness's 171 ticks, the scythe unboosted).
-- (boost, above the pre-fights, drinks it: the drained stats restored first.)

PRE.sotetseg = function(t, ox, oz)
    wear(t, "sotetseg.equip.scythe", "scythe_of_vitur")
    boost(t, "sotetseg")
    t.exec("sotetseg.prayer", t.prayer.set, "protectfrommagic", true)
    local fr, fight, ftext = t.raid.start_tile()
    t.check("sotetseg.start_tile", fr == "ok", tostring(ftext))
    t.player.walk_to(fight.x, fight.z - 2, 20)
    begin_fight(t, "sotetseg")
    return { weapon = "scythe_of_vitur", max_ticks = 1400 }
end

-- _play_xarpus.lua :321-361: the scythe, the barrier from three tiles south.
PRE.xarpus = function(t, ox, oz)
    wear(t, "xarpus.equip.scythe", "scythe_of_vitur")
    boost(t, "xarpus")
    local fr, fight, ftext = t.raid.start_tile()
    t.check("xarpus.start_tile", fr == "ok", tostring(ftext))
    t.player.walk_to(fight.x, fight.z - 3, 20)
    begin_fight(t, "xarpus")
    return { weapon = "scythe_of_vitur", max_ticks = 900 }
end

-- _play_verzik.lua :47-63: bare fists for P1 (the plan's own opening, its
-- weapon row `fists`), dragon arrows on for the bow, Protect from Magic,
-- the talk and "Yes, begin the fight."
PRE.verzik = function(t, ox, oz)
    wear(t, "verzik.equip.arrows", "dragon_arrow")
    t.exec("verzik.unequip.scythe", t.player.unequip, "scythe_of_vitur")
    t.exec("verzik.prayer", t.prayer.set, "protectfrommagic", true)
    -- The talk is the room's barrier: re-talked when no dialogue opened.
    -- svaplayentry (seam39 survey): from the fight tile the press found no
    -- pixel of Verzik's (pickset held=false, only "Walk here" in the menu),
    -- so each retry first walks four tiles up the carpet towards her.
    local tr, td, talks = nil, nil, 0
    local opened = false
    while talks < 3 and not opened do
        talks = talks + 1
        if talks > 1 then
            local _, here = t.world.tile()
            t.player.walk_to(here.x, here.z + 4, 10)
        end
        tr, td = t.player.talk_to("verzik_initial_story", 1)
        opened = tr == "ok" and t.chat.kind() ~= "none"
    end
    t.check("verzik.talk", opened, "dialogue open after " .. talks .. " talk(s): " .. string.sub(tostring(td), 1, 200))
    local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
    t.check("verzik.begin", cr == "ok", tostring(cd))
    return { max_ticks = 1500, no_barrier = true }
end

-- ------------------------------------------------- each room's way out
-- The supply chest (tob_chest.rs2; E:151 "During Entry Mode this will always
-- contain 10 bandages"; E:197 the second after Sotetseg): found near, walked
-- to, opened; the line says how many fit the pack.
local function take_chest(t, name)
    local chr, chrow = t.world.loc_near("tob_midway_chest_closed", 40)
    t.check(name .. ".chest_found", chr == "ok" and chrow ~= nil, tostring(chr) .. " " .. tostring(chrow and chrow.tile_x) .. "," .. tostring(chrow and chrow.tile_z))
    if chr ~= "ok" or chrow == nil then return 0 end
    local since = newest_serial(t)
    local ccr, ccd = t.player.click_loc("tob_midway_chest_closed", 1)
    t.ticks(3)
    local line = line_since(t, since, "bandages from the chest")
    if line == nil then
        -- not framed from where the walk stopped: stand beside it and press again
        t.player.walk_to(chrow.tile_x + 1, chrow.tile_z - 1, 20)
        ccr, ccd = t.player.click_loc("tob_midway_chest_closed", 1)
        t.ticks(3)
        line = line_since(t, since, "bandages from the chest")
    end
    local got = line and tonumber(string.match(line, "You take (%d+) bandages")) or nil
    t.check(name .. ".chest_bandages", got ~= nil and got >= 1, "chest " .. tostring(ccr) .. ": " .. tostring(line or string.sub(tostring(ccd), 1, 160)))
    t.key("escape")
    return got or 0
end

local POST = {}

-- Maiden's arena has one opening: the barrier she is entered by is the way
-- out, and the corridor round it ends at the passage (tob_raid.rs2
-- ~tob_room_has_exit_gate's note).
POST.maiden = function(t, ox, oz)
    local br, bd = t.player.click_loc("tob_arena_barrier", 1)
    t.check("maiden.exit_gate", br == "ok", tostring(bd))
    t.ticks(2)
    drop_spent(t, "maiden")
    return leave_room(t, "maiden", "bloat", ox, oz)
end

-- tob_bloat.lua :1859-1904: the west barrier (local 23..24,31), the corridor
-- west to the chest, the passage.
POST.bloat = function(t, ox, oz)
    -- each try only while still east of it: the gate answers timeout even
    -- when it stepped the player through, and a second press stepped one
    -- name (svcplayentry, the five-name survey) back into the arena, where
    -- the corridor walk then stalled
    local tries = { { 23, 31 }, { 24, 31 }, { 23, 30 }, { 22, 31 } }
    local result, detail = "not_tried", ""
    for i = 1, #tries do
        local _, at = t.world.tile()
        if at.x >= ox + 23 then
            result, detail = t.player.click_loc("tob_arena_barrier", 1, { at = { ox + tries[i][1], oz + tries[i][2] } })
            t.ticks(2)
        end
    end
    t.ticks(2)
    local _, bt = t.world.tile()
    t.check("bloat.exit_gate", bt.x < ox + 23, "west barrier " .. tostring(result) .. ", at " .. bt.x .. "," .. bt.z .. " " .. string.sub(tostring(detail), 1, 120))
    t.player.walk_to(ox + 6, oz + 31, 40)
    R.bloat.chest = take_chest(t, "bloat")
    return leave_room(t, "bloat", "nylocas", ox, oz)
end

-- Out of the Nylocas: the bow's rapid and auto retaliate are set back the way
-- every other room's harness plays (style 0, retaliate on), then the walkway.
POST.nylocas = function(t, ox, oz)
    t.ui.tab("combat")
    t.ticks(1)
    local _, sw = t.ui.widget("combat_interface:style_slot_0")
    t.ui.invoke(sw, 1)
    local _, ret = t.var.varp("varp172_option_nodef")
    if ret == 1 then
        local _, rw = t.ui.widget("combat_interface:retaliate")
        t.ui.invoke(rw, 1)
    end
    t.ticks(2)
    drop_spent(t, "nylocas")
    -- the passage is north up the walkway, then east (tob_party.rs2 "The way
    -- out": m51_66 (39,51)), on the far side of the barrier the room is
    -- entered by: the cleared barrier is a gate, then the walkway is walked
    -- to the passage's mouth (from the platform a support stands over its
    -- clickbox: the relay's third run, "covered ... Examine Support")
    local br, bd = t.player.click_loc("tob_arena_barrier", 1)
    t.check("nylocas.exit_gate", br == "ok", tostring(bd))
    t.ticks(2)
    t.player.walk_to(ox + 38, oz + 51, 30)
    local ok = leave_room(t, "nylocas", "sotetseg", ox, oz)
    return ok
end

-- _play_sotetseg.lua :550-567: the barrier, the chest, the passage.
POST.sotetseg = function(t, ox, oz)
    local br, bd = t.player.click_loc("tob_arena_barrier", 1)
    t.check("sotetseg.exit_gate", br == "ok", tostring(bd))
    t.ticks(3)
    R.sotetseg.chest = take_chest(t, "sotetseg")
    return leave_room(t, "sotetseg", "xarpus", ox, oz)
end

-- _play_xarpus.lua :526-539: the north gate (local 34,43), the skeleton's
-- Dawnbringer ("a player must pick up the Dawnbringer", E:214), the door.
POST.xarpus = function(t, ox, oz)
    local wr, wd = t.player.walk_to(ox + 34, oz + 42, 12)
    t.check("xarpus.walk_to_gate", wr == "ok", tostring(wr) .. " " .. string.sub(tostring(wd), 1, 120))
    local gr = t.player.click_loc("tob_arena_barrier", 1, { at = { ox + 34, oz + 43 } })
    t.ticks(2)
    local _, gat = t.world.tile()
    t.check("xarpus.exit_gate", gat.z >= oz + 44, "pressed the gate (" .. tostring(gr) .. "), stood on " .. gat.x .. "," .. gat.z)
    local xr, xd = t.player.click_loc("tob_skeleton_with_weapon", 1)
    t.check("xarpus.skeleton", xr == "ok", tostring(xd))
    t.chat.continue_()
    local wok = t.inv.await("verzik_special_weapon", 1, 5)
    t.check("xarpus.dawnbringer", wok == "ok", "verzik_special_weapon in the pack: " .. tostring(wok))
    return leave_room(t, "xarpus", "verzik", ox, oz)
end

-- Verzik's chamber ends the raid: the trapdoor (tob_vault.rs2
-- ~tob_vault_trapdoor_place, op1 ~tob_vault_enter) into the reward room, and
-- the chest there.
POST.verzik = function(t, ox, oz)
    local dr, dd = "not_found", ""
    for _ = 1, 20 do
        dr, dd = t.player.click_loc("tob_dungeon_verzik_throne_door_opened", 1)
        if dr == "ok" then break end
        t.ticks(2)
    end
    t.ticks(4)
    local vr, vrow = "not_found", nil
    local chest_sym = nil
    for k = 0, 4 do
        local sym = "tob_treasureroom_chest_loc" .. k
        local lr, row = t.world.loc_near(sym, 30)
        if lr == "ok" and row ~= nil then vr, vrow, chest_sym = "ok", row, sym break end
    end
    t.check("raid.reward_room", dr == "ok" and vr == "ok", "trapdoor " .. tostring(dr) .. " " .. string.sub(tostring(dd), 1, 120)
        .. "; reward chest " .. tostring(chest_sym) .. " at " .. tostring(vrow and vrow.tile_x) .. "," .. tostring(vrow and vrow.tile_z))
    if chest_sym ~= nil then
        local cr, cd = t.player.click_loc(chest_sym, 1)
        t.ticks(3)
        t.check("raid.reward_chest", cr == "ok", tostring(cd))
        t.shot("raid.reward_chest")
    end
    return vr == "ok"
end

return {
    id = "_play_entry",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = KIT,

    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1, the whole raid")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))
        local kit0 = supplies(t)
        t.check("kit.supplies", kit0.brew == 24 and kit0.restore == 24, "one raid's supplies at the start: " .. supplies_text(kit0))

        -- THE LOBBY: the notice board, an Entry party of one, the door
        t.exec("lobby.goto", t.player.goto_tile, LOBBY_X, LOBBY_Z, 0)
        t.exec("party.form", t.party.form, "entry")
        t.key("escape")
        t.ticks(2)
        local rr, rd = t.party.ready()
        t.check("party.ready", rr == "ok", tostring(rd))
        local raid_start = mark_tick(t, "raid start")

        local reached = 0
        for _, name in ipairs(ORDER) do
            local ar, ast = await_room(t, name, 60)
            t.check(name .. ".arrived", ar == "ok", ar == "ok" and tostring(ast.line) or tostring(ast))
            if ar ~= "ok" then break end
            local ox, oz, here = origin_of(t)
            R[name] = { before = supplies(t) }
            local lv = {}
            for _, sk in ipairs({ "attack", "strength", "defence", "ranged", "magic", "hitpoints", "prayer" }) do
                local _, rd = t.skill.read(sk)
                lv[#lv + 1] = sk .. " " .. tostring(rd and rd.level)
            end
            t.check(name .. ".start", here ~= nil, string.format("arrived at %d,%d (square %d,%d); %s; %s", here.x, here.z, ox, oz,
                supplies_text(R[name].before), table.concat(lv, ", ")))
            local spec = PRE[name](t, ox, oz)
            local since = newest_serial(t)
            R[name].mark = mark_tick(t, name .. " start")

            -- THE FIGHT: the library and the room's plan, nothing else
            local result, detail, rec = t.raid.play(PLAN[name], { mode = "entry", weapon = spec.weapon, max_ticks = spec.max_ticks })
            t.check(name .. ".fight", result == "ok", string.sub(tostring(detail), 1, 600))
            if result == "ok" then prayers_off(t, name) end
            R[name].result = result
            R[name].death = rec and rec.death_tick or nil
            local wave = nil
            for _ = 1, 30 do
                wave = line_since(t, since, "(Entry Mode) complete!")
                if wave ~= nil then break end
                t.ticks(1)
            end
            t.check(name .. ".complete_line", wave ~= nil, "chat line: " .. plain(wave))
            R[name].after = supplies(t)
            local _, now = t.tick()
            local taken, n, big = hits_between(t, R[name].mark or 0, R[name].death or now)
            R[name].taken = taken
            t.check(name .. ".measure", R[name].death ~= nil, string.format("%s ticks (mark %s, death %s); damage taken %d in %d hits (largest %d); used %s; eats %d, drinks %d",
                tostring(R[name].death and R[name].mark and (R[name].death - R[name].mark)), tostring(R[name].mark), tostring(R[name].death),
                taken, n, big, used_text(R[name].before, R[name].after), rec and #(rec.eats or {}) or -1, rec and #(rec.drinks or {}) or -1))
            if result ~= "ok" then break end
            local ok = POST[name](t, ox, oz)
            if not ok then break end
            reached = reached + 1
        end

        -- THE RAID: every room, the reward room, no death, what is left
        t.check("raid.complete", reached == #ORDER, reached .. " of " .. #ORDER .. " rooms cleared and left")
        local left = supplies(t)
        local verzik_death = R.verzik and R.verzik.death or nil
        t.check("raid.supplies_left", left.brew >= 0 and left.restore >= 0 and left.tob_bandages >= 0, "left after the raid: " .. supplies_text(left) .. "; chests gave "
            .. tostring(R.bloat and R.bloat.chest) .. " (Bloat) and " .. tostring(R.sotetseg and R.sotetseg.chest) .. " (Sotetseg)")
        t.check("raid.measure", verzik_death ~= nil and raid_start ~= nil, string.format("raid start tick %s, Verzik's death %s: %s ticks in all",
            tostring(raid_start), tostring(verzik_death), tostring(verzik_death and raid_start and (verzik_death - raid_start))))
        t.finish(0)
    end,
}
