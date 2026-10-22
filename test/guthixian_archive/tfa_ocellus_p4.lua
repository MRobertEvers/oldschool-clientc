-- tfa_ocellus_p4: Ocellus's phase 4 with three raiders (--bots 3), for the
-- tick log to diff against RSProx index 4301 (three raiders, docs/rsprox-
-- analysis/guthixian_archive/ocellus_plan.md §5 and "Phase 4, every hidden
-- rule"; tools/oc_p4.py): three cycles of 60 and his death.
--
--   seat 1 the solo test's way in, ::tfaroom straight to Ocellus, ::tfahost,
--   then `::tfaocphase 4` (his own transitions to the last quadrant);
--   seats 2 and 3 ::tfajoin and, once he is on it, onto the last quadrant
--   all three shoot him and keep off the void with ::tfaocorb (their orbs as
--   they come ready), Protect from Magic up throughout: the hybrid is prayed
--   when it is magic and not otherwise
--   seat 1 holds him at his last point after three cycles; the next shot
--   ends him
--
--   ./src/build_oc4_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocellus_p4.lua --bots 3 \
--       --name tfa0 --ticks 6000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 60)
    return r == "ok"
end

local EAT = { eat = { item = "shark", below = 40 } }

-- Shoot him and keep off the void (::tfaocorb walks, sent after the attack
-- so its walk wins); health kept full: the bots pray Magic only, so from the
-- third cycle a hybrid of the other kinds hits them for 40..60, and a death
-- here wipes the room (ours takes any death for the party's).
local function fight4(t, ticks, stop)
    local deadline = now(t) + ticks
    while now(t) < deadline do
        if stop and stop() then return true end
        local hr, hv = t.skill.read("hitpoints")
        if hr == "ok" and type(hv) == "table" and (hv.level or 99) < 99 then
            t.cheat("::tfaheal", false)
        end
        -- His rifts first: one left standing detonates (39..41 a rift on
        -- everyone, 30 ticks from its add), as every recorded party knew.
        if present(t, "tfa_ocellus_p3_bombportal_basic") then
            t.player.attack("tfa_ocellus_p3_bombportal_basic", 2, 1, { quick = true, eat = EAT.eat })
        elseif present(t, "tfa_ocellus_p3") then
            t.player.attack("tfa_ocellus_p3", 2, 1, { quick = true, eat = EAT.eat })
        end
        t.cheat("::tfaocorb", false)
        t.ticks(1)
    end
    return false
end

local function run(t)
    local role = t.party.role()
    t.player._death_fence = function() return false end
    if role == 1 then
        t.check("party.ticklog", t.ticklog.start() == "ok", "")
    end
    t.ticks(2)
    if role == 1 then
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.chat.choose("Form or join a party.")
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(10)
        t.cheat("::tfaroom 6 4", false)
        ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "the hall" }, 30)
        t.check("ocellus.hall", ok == "ok", "not in the hall")
        t.cheat("::tfahost", false)
    end
    local br, bd = t.party.barrier("oc_built", 600)
    t.check("party.built", br == "ok", tostring(bd))
    if role ~= 1 then
        t.cheat("::tfajoin", false)
        t.ticks(4)
    end
    br, bd = t.party.barrier("oc_in", 300)
    t.check("party.in", br == "ok", tostring(bd))
    if role == 1 then
        t.cheat("::tfaocphase 4", false)
        -- His phase 3/4 form, then the three quadrants' fall (2 ticks).
        local reached = false
        for _ = 1, 400 do
            t.cheat("::tfaheal", false)
            t.ticks(1)
            if present(t, "tfa_ocellus_p3") then reached = true break end
        end
        t.ticks(4)
        t.check("p4.reached", reached, "no phase 3/4 form")
        t.ticklog.mark("phase4")
    else
        -- The way in is the hall (local 30..32, 3..5 of the arena's square,
        -- the middle at 31,41): to the middle once he is on the last
        -- quadrant (~45 ticks), then 4 out each way on his side.
        local _, me = t.world.tile()
        local mx = me.x - me.x % 64 + 31
        local mz = me.z - me.z % 64 + 41
        for _ = 1, 50 do
            t.cheat("::tfaheal", false)
            t.ticks(1)
        end
        t.cheat(string.format("::goto %d %d 1", mx, mz), false)
        t.ticks(1)
        local r, boss = t.npc.nearest("tfa_ocellus_p3", 40)
        t.check("p4.boss_seen", r == "ok", tostring(r))
        if r == "ok" then
            local sx = boss.x > mx and 1 or -1
            local sz = boss.z > mz and 1 or -1
            t.cheat(string.format("::goto %d %d 1", mx + 4 * sx, mz + 4 * sz), false)
        end
    end
    t.prayer.set("protectfrommagic", true)
    if role == 2 then
        -- On ~D+3 now: at ~D+22, with no void or explosion running, a void
        -- patch laid underfoot and stood on for 7 ticks (its hits every
        -- other tick from the next: 6 or 7).
        fight4(t, 18)
        t.cheat("::tfaocpatch", false)
        for _ = 1, 7 do
            t.ticks(1)
        end
        fight4(t, 165)
    else
        fight4(t, 190)
    end
    if role == 1 then
        t.ticklog.mark("kill")
    end
    -- His last point, held each tick (an unclaimed orb heals him back): the
    -- next shot ends him, a miss too.
    local done = function()
        if role == 1 and present(t, "tfa_ocellus_p3") then t.cheat("::tfaboss 0", false) end
        return present(t, "tfa_blade_npc_vault_entry")
    end
    t.check("ocellus.blade", fight4(t, 90, done), "no Blade to the vault")
    if role == 1 then
        t.ticks(4)
        t.ticklog.mark("end")
    end
    t.party.barrier("oc_end", 120)
    t.finish(0)
end

return {
    id = "tfa_ocellus_p4",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1",
        "::wield bow_of_faerdhinen",
        "::give black_dragonhide_body 1",
        "::wield black_dragonhide_body",
        "::give shark 20",
        "::goto 4128 5556 0",
    },
    run = run,
}
