-- yama_bonus: the demonbane bonuses against what is worn and cast (the owner's report: a
-- demonbane bonus applied with the purging staff off). Dark Demonbane on Yama's two void
-- flares, which always take the max hit from magic, so each splat IS the max:
--
--   A  staff on,  no Mark   -> the spell's max with the gear's magic damage
--   D  staff off, no Mark   -> the same without the staff's own +10%
--   B  staff off, Mark      -> D + 25%, doubled on a flare (200% demonbane vulnerability): D + 50%
--   C  staff on,  Mark      -> A + 50%, doubled on a flare: A + 100%
--
-- (wiki Mark of Darkness, Purging staff, Dark Demonbane, Void Flare.) The cheats only set
-- player alive (::boost); every hit is a real cast.
local function now(t) local _, k = t.tick() return k or -1 end

-- One flare's row (by its client slot) and the newest splat it shows.
local function flare(t, which)
    local _, _, rows = t.npc.tiles("yama_voidflare", 30)
    if type(rows) ~= "table" then return nil end
    return rows[which]
end

local function cast_on(t, which)
    local row = flare(t, which)
    if not row then return nil end
    t.player.cast("dark_demonbane", "yama_voidflare", 1, 2, { slot = row.slot, quick = true })
    return row.slot, row.hit_cycle
end

-- The splat the client shows on that flare once the cast lands (its hit_cycle moves).
local function hit_of(t, slot, before)
    local deadline = now(t) + 6
    while now(t) < deadline do
        local _, _, rows = t.npc.tiles("yama_voidflare", 30)
        for _, row in ipairs(type(rows) == "table" and rows or {}) do
            if row.slot == slot and row.hit_cycle ~= before and (row.hit_damage or -1) >= 0 then
                return row.hit_damage
            end
        end
        t.ticks(1)
    end
    return nil
end

local function run(t)
    t.ticklog.start()
    -- The real way in (the solver's): Travel, the throne's Challenge, then stand north of him.
    t.ticks(2)
    t.exec("voice.travel", t.player.talk_to, "voice_of_yama", 3)
    t.ticks(8)
    local _, at = t.world.tile()
    local bx, bz = at.x - at.x % 64, at.z - at.z % 64
    t.exec("walk.throne", t.player.walk_to, bx + 31, bz + 40, 40)
    t.player.press("yama_throne", 3, 3)
    t.player.walk_to(bx + 31, bz + 36, 10)
    -- The first flare pair (a shadow special slot of phase 1); nothing else is done.
    local deadline = now(t) + 260
    local found = false
    while now(t) < deadline do
        local _, _, rows = t.npc.tiles("yama_voidflare", 40)
        if type(rows) == "table" and #rows >= 2 then found = true break end
        -- (kept alive by cheat: this test is about the casts, not the fight)
        if now(t) % 10 == 0 then t.cheat("::boost hitpoints 255 0", false) end
        t.ticks(1)
    end
    t.check("bonus.flares", found, "two flares by tick " .. now(t))
    if not found then t.finish(1) return end
    t.ticks(1)
    local function one(label, which)
        local slot, before = cast_on(t, which)
        local v = slot and hit_of(t, slot, before) or nil
        t.check("bonus." .. label, v ~= nil, label .. " splat " .. tostring(v))
        return v
    end
    local A = one("A_staff_nomark", 1)
    t.player.equip("emberlight", { quick = true })
    local D = one("D_nostaff_nomark", 2)
    -- Mark of Darkness with the staff off (so its duration is not the staff's x5).
    local _, comp = t.player._spell_component("mark_of_darkness")
    t.ui.invoke(comp, 1)
    t.ticks(2)
    local B = one("B_nostaff_mark", 2)
    t.player.equip("purging_staff", { quick = true })
    local C = one("C_staff_mark", 1)
    if A and D and B and C then
        t.check("bonus.staff_off_mark", B == D + math.floor(D * 500 / 1000),
            string.format("staff off under Mark: %d, want %d + 50%% = %d", B, D, D + math.floor(D * 500 / 1000)))
        t.check("bonus.staff_on_mark", C == A + A,
            string.format("staff on under Mark: %d, want %d + 100%% = %d", C, A, A + A))
        t.check("bonus.staff_on_more", A > D, string.format("the staff's own damage: %d > %d", A, D))
    end
    t.finish((A and D and B and C and B == D + math.floor(D * 500 / 1000) and C == A + A) and 0 or 1)
end

local kit = { "::resetcharacter", "::clearinv" }
for _, sv in ipairs({ { "attack", 97 }, { "defence", 99 }, { "strength", 98 }, { "hitpoints", 99 },
                      { "ranged", 99 }, { "prayer", 76 }, { "magic", 99 } }) do
    kit[#kit + 1] = "::setlevel " .. sv[1] .. " " .. sv[2]
end
for _, line in ipairs({
    "::setvar varb4070_spellbook 3",
    "::setvar varb16284_chasm_voice_progress 2",
    "::setvar varp9561_yama_contract_signed 1",
    "::give viking_helmet_magic 1", "::wield viking_helmet_magic",
    "::give ma2_saradomin_cape 1", "::wield ma2_saradomin_cape",
    "::give occult_necklace 1", "::wield occult_necklace",
    "::give purging_staff 1", "::wield purging_staff",
    "::give barrows_ahrim_body_50 1", "::wield barrows_ahrim_body_50",
    "::give magictraining_bookofmagic 1", "::wield magictraining_bookofmagic",
    "::give barrows_ahrim_legs_50 1", "::wield barrows_ahrim_legs_50",
    "::give zenyte_bracelet_enchanted 1", "::wield zenyte_bracelet_enchanted",
    "::give eternal_boots 1", "::wield eternal_boots",
    "::clearinv",
    "::give firerune 30000", "::give emberlight 1", "::give soulrune 8000", "::give cosmicrune 10000",
    "::goto 1442 10080 0",
}) do kit[#kit + 1] = line end

return {
    id = "yama_bonus",
    fixture = "yama_kit.ini",
    max_frames = 200000,
    setup = kit,
    run = run,
}
