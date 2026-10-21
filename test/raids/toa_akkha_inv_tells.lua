-- toa_akkha_inv_tells: Akkha with Double Trouble, Keep Back and Feeling Special? (no Stay
-- Vigilant), solo, raid level 0 (docs/rsprox-analysis/toa/invocations.md, "Akkha"). The raider
-- stands beside him for a while, then steps back and forth through the specials. Checks:
-- seven swings between specials (revisions 234-241); every Double Trouble cast shows Memory
-- Blast's animation, with trailing orbs alongside; Keep Back's melee lands the tick after a
-- ranged or magic swing on a raider beside him; Feeling Special?'s second orb a step on.
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function origin(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(20)) do
        local x, z = l:match("^toastate.-entry=(%-?%d+),(%-?%d+)")
        if x then return tonumber(x) - 31, tonumber(z) - 56 end
    end
    return nil
end
local AKKHA = { [11790] = true, [11791] = true, [11792] = true }
-- Every row of a kind, a page at a time.
local function all_rows(t, kind)
    local out, since = {}, 0
    while true do
        local _, list = t.ticklog.rows({ kind = kind, since = since })
        if not list or #list == 0 then break end
        for _, row in ipairs(list) do
            out[#out + 1] = row
            if (row.serial or 0) > since then since = row.serial end
        end
    end
    return out
end

return {
    id = "toa_akkha_inv_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = { "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 7", false)
        t.ticks(1)
        for _, inv in ipairs({ 30, 31, 33 }) do
            t.cheat("::toainv " .. inv, false)
            t.ticks(1)
        end
        t.expect("akkha.invocations", said(t, "Invocation 33 on.") and "ok" or "fail")
        t.ticks(4)
        t.expect("akkha.crystal", t.player.click_loc("akkha_teleport", 2))
        -- Beside him through his first styles (he walks to his target), then stepping east and
        -- west a tile every tick so the trailing orbs come.
        for _ = 1, 30 do t.ticks(4) keep_alive(t) end
        local px, pz
        for _, row in ipairs(all_rows(t, "player_tile")) do px, pz = row.x, row.z end
        t.expect("akkha.tile", px and "ok" or "fail")
        local west = true
        for _ = 1, 150 do
            if px then
                if west then t.player.walk_to(px - 1, pz, 0) else t.player.walk_to(px + 1, pz, 0) end
            end
            west = not west
            t.ticks(1)
            keep_alive(t)
        end
        -- One sequence block per npc per tick goes out: keep each tick's last row.
        local anims, last = {}, {}
        for _, row in ipairs(all_rows(t, "npc_anim")) do
            if AKKHA[row.type] then
                local key = row.tick .. ":" .. row.slot
                if last[key] then anims[last[key]] = row else anims[#anims + 1] = row last[key] = #anims end
            end
        end
        local swings, casts, ranged = 0, {}, {}
        local runs = {}
        for _, row in ipairs(anims) do
            if AKKHA[row.type] then
                if row.seq == 9770 or row.seq == 9772 or row.seq == 9774 then
                    swings = swings + 1
                    if row.seq ~= 9770 then ranged[row.tick] = true end
                elseif row.seq == 9776 or row.seq == 9777 or row.seq == 9778 then
                    runs[#runs + 1] = swings
                    swings = 0
                    casts[#casts + 1] = row
                end
            end
        end
        local sevens = 0
        for _, r in ipairs(runs) do if r == 7 then sevens = sevens + 1 end end
        t.expect("akkha.special_every_eighth", (#runs >= 2 and sevens == #runs) and "ok" or "fail",
            "swings between specials " .. table.concat(runs, " "))
        local mem = 0
        for _, c in ipairs(casts) do if c.seq == 9777 then mem = mem + 1 end end
        t.expect("akkha.double_trouble_anim", (#casts >= 2 and mem == #casts) and "ok" or "fail",
            string.format("%d of %d casts show 9777", mem, #casts))
        -- Keep Back: a hit on swing + 1 after some ranged or magic swing.
        local hits = {}
        for _, row in ipairs(all_rows(t, "hit_player")) do hits[row.tick] = true end
        local kb = 0
        for tick, _ in pairs(ranged) do if hits[tick + 1] then kb = kb + 1 end end
        t.expect("akkha.keep_back", kb >= 1 and "ok" or "fail", string.format("%d ranged/magic swings followed by a hit at +1", kb))
        -- Feeling Special?: ticks that made two trailing orbs.
        local per = {}
        for _, row in ipairs(all_rows(t, "npc_spawn")) do
            if row.type >= 11800 and row.type <= 11803 then per[row.tick] = (per[row.tick] or 0) + 1 end
        end
        local one, two = 0, 0
        for _, n in pairs(per) do if n >= 2 then two = two + 1 else one = one + 1 end end
        t.expect("akkha.feeling_special_orbs", two >= 3 and "ok" or "fail", string.format("%d ticks with two orbs, %d with one", two, one))
    end,
}
