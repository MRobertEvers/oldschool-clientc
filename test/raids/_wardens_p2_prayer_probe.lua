-- _wardens_p2_prayer_probe: scratch. Phase two at once (::toawardens 2): on each shield form, ranged,
-- melee and magic in turn, and every hit_npc on the shield logged. `_mage` (Protect from Melee +
-- Missiles) takes magic only, `_range` (Protect from Melee + Magic) ranged only.
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local SHIELDS = { "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range",
    "toa_warden_tumeken_phase2_mage", "toa_warden_tumeken_phase2_range" }
local function form(t)
    for _, f in ipairs(SHIELDS) do
        local r, row = t.npc.by_symbol(f)
        if r == "ok" then return f, row.npc_id end
    end
    return nil
end
local function newest(t)
    local _, list = t.ticklog.rows({ kind = "hit_npc" })
    local s = 0
    for _, r in ipairs(list or {}) do if (r.serial or 0) > s then s = r.serial end end
    return s
end
local function hits_since(t, since, id)
    local _, list = t.ticklog.rows({ kind = "hit_npc", since = since })
    local out = {}
    for _, r in ipairs(list or {}) do
        if r.type == id then out[#out + 1] = r.damage end
    end
    return out
end
local function try_style(t, style, f, id)
    wait(t, 4) -- anything still in flight lands before the mark
    local since = newest(t)
    if style == "ranged" then
        t.player.equip("twisted_bow")
        for _ = 1, 2 do t.player.attack(f, 2, 2, { quick = true }) wait(t, 3) end
    elseif style == "melee" then
        t.player.equip("abyssal_whip")
        for _ = 1, 4 do t.player.attack(f, 2, 4) wait(t, 3) end
    else
        for _ = 1, 3 do t.player.cast("fire_blast", f, 6) wait(t, 2) end
    end
    wait(t, 3)
    return hits_since(t, since, id)
end
local function summary(h)
    local s, n = 0, 0
    for _, d in ipairs(h) do s = s + d if d > 0 then n = n + 1 end end
    return s, n
end
local function test_form(t, tag)
    local f, id = form(t)
    t.check(tag .. ".form", f and "ok" or "fail", tostring(f))
    if not f then return nil end
    local open = f:find("_mage", 1, true) and "magic" or "ranged"
    -- the off styles first: the open one fills a solo shield (140) in four hits
    local order = open == "ranged" and { "melee", "magic", "ranged" } or { "melee", "ranged", "magic" }
    for _, style in ipairs(order) do
        local h = try_style(t, style, f, id)
        local sum, landed = summary(h)
        local want_open = style == open
        local good = (#h > 0) and ((want_open and sum > 0) or (not want_open and sum == 0))
        t.check(string.format("%s.%s", tag, style), good and "ok" or "fail",
            string.format("%s on %s: %d hit(s) %s, %d dealt", style, f, #h, table.concat(h, ","), sum))
    end
    return f, open
end
return {
    id = "_wardens_p2_prayer_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 3000", "::give abyssal_whip 1",
        "::give firerune 2000", "::give airrune 2000", "::give deathrune 1000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        t.cheat("::toawardens 2", false)
        wait(t, 30)
        t.shot("p2")
        local f1, open1 = test_form(t, "first")
        if f1 then
            -- fill the shield in its open style until it kneels, then wait for the other form
            for _ = 1, 60 do
                local f = form(t)
                if f ~= f1 then break end
                if open1 == "ranged" then
                    t.player.equip("twisted_bow")
                    t.player.attack(f1, 2, 2, { quick = true })
                    wait(t, 3)
                else
                    t.player.cast("fire_blast", f1, 6)
                    wait(t, 2)
                end
            end
            for _ = 1, 30 do
                local f = form(t)
                if f and f ~= f1 then break end
                wait(t, 3)
            end
            wait(t, 5)
            test_form(t, "second")
        end
        t.finish(0)
    end,
}
