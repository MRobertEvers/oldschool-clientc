-- tfa_vault: the Fractured Archive's vault, solo, for the tick log and the
-- wire log, to diff against RSProx index 4301 and 4308 (docs/rsprox-analysis/
-- guthixian_archive/vault_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to the vault
--   the chests, the braziers at A+9; the raider's own chest (1): Open, the
--   loot interface, Take-all
--   the small Rift five ticks after the chest opens; Leave to the lobby
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_vault.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local function used_slots(t)
    local n = 0
    for i = 0, 27 do
        local r, sl = t.inv.slot(i)
        if r == "ok" and type(sl) == "table" and sl.name ~= "" then n = n + 1 end
    end
    return n
end

return {
    id = "tfa_vault",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
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
        -- Straight to the vault (all six beaten).
        t.ticklog.mark("vault")
        t.cheat("::tfaroom 7 6", false)
        ok = t.await({ level = function() return present(t, "tfa_rewards_chest_vault_1_closed") end, note = "chests" }, 30)
        t.check("vault.chests", ok == "ok", "no chests")
        -- The arrival tile is the square's (31,14): chest 1 at (28,33), chest 2
        -- at (34,33).
        local _, a = t.world.tile()
        local c1 = { a.x - 3, a.z + 19 }
        local c2 = { a.x + 3, a.z + 19 }
        t.ticks(10)
        local _, v = t.var.varp("varp5922_tfa_loot_temp_1")
        t.check("vault.own_chest", v ~= nil and v % 16 == 1, "own chest " .. tostring(v))
        -- Someone else's chest does nothing.
        t.player.talk_to("tfa_rewards_chest_vault_2_closed", 1, { at = c2 })
        t.ticks(3)
        t.check("vault.not_mine", present(t, "tfa_rewards_chest_vault_1_closed") and not present(t, "tfa_rewards_chest_vault_2_open"), "a chest opened")
        t.ticklog.mark("open")
        local r, d = t.player.talk_to("tfa_rewards_chest_vault_1_closed", 1, { at = c1 })
        t.check("vault.open", r == "ok", tostring(r) .. " " .. tostring(d))
        t.check("vault.interface", t.ui.await_open("tfa_chests", 10))
        ok = t.await({ level = function() return present(t, "tfa_rewards_chest_vault_1_open") end, note = "chest open" }, 5)
        t.check("vault.chest_open", ok == "ok", "chest 1 still closed")
        local before = used_slots(t)
        local _, take = t.ui.widget("tfa_chests:inventory")
        t.ui.invoke(take, 1)
        t.ticks(2)
        t.check("vault.loot", used_slots(t) > before, "nothing taken")
        t.ui.close_modal()
        ok = t.await({ level = function() return present(t, "tfa_vault_exit_small") end, note = "the rift" }, 10)
        t.check("vault.rift", ok == "ok", "no small Rift")
        t.ticks(2)
        t.ticklog.mark("leave")
        t.player.talk_to("tfa_vault_exit_small", 1)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby" }, 20)
        t.check("vault.lobby", ok == "ok", "not in the lobby")
        t.ticks(4)
        t.ticklog.mark("end")
        t.expect("tfa_vault.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
