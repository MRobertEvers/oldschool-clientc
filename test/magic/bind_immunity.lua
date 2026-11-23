-- Binding spells: a target is immune for 5 ticks after a freeze ends, and a cast at it then is
-- refused with Jagex's line, "Your target is currently immune to that spell." (RSProx 4626 [770]:
-- a Snare recast on Damis as his bind from [751] ran out; 235 [3379]). The engine's window is
-- TORIRSSERVER_FREEZE_IMMUNITY_TICKS (5); content asks npc_freeze_immune in ~pvm_freeze_allowed.
-- ::freeze_nearest freezes the goblin for a known 2 ticks so the window can be timed.
return {
    id = "bind_immunity",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv", "::~maxmage", "::setlevel magic 99", "::setlevel hitpoints 99", "::runes",
        "::setvar varb4070_spellbook 0", "::tele 0_50_50_6_49",
    },
    run = function(t)
        local IMMUNE = "Your target is currently immune to that spell."
        -- the Lumbridge goblin north of the castle (3206,3252)
        local GOB = "goblin_unarmed_melee_3"
        t.ticks(3)
        t.cheat("::freeze_nearest " .. GOB .. " 2")
        -- frozen 2 ticks, then immune 5: a cast pressed 4 ticks on lands inside the immunity
        local function lastmsgs(n)
            local _, ls = t.msg.last(n or 6)
            local out = {}
            for _, l in ipairs(ls or {}) do out[#out + 1] = tostring(l.text) end
            return table.concat(out, " | ")
        end
        t.note("after freeze: " .. lastmsgs())
        t.ticks(4)
        local cr, cd = t.player.cast("bind", GOB, 3)
        t.check("immune.cast_issued", true, "cast: " .. tostring(cr) .. " " .. tostring(cd) .. " msgs: " .. lastmsgs())
        t.exec("immune.refused", t.msg.expect, IMMUNE)
        -- well past the window: the cast goes ahead (no refusal line)
        t.chat.drain()
        t.ticks(10)
        local _, before = t.msg.last(30)
        local n_before = 0
        for _, l in ipairs(before or {}) do if tostring(l.text) == IMMUNE then n_before = n_before + 1 end end
        t.player.cast("bind", GOB, 3)
        t.ticks(2)
        local _, after = t.msg.last(30)
        local n_after = 0
        for _, l in ipairs(after or {}) do if tostring(l.text) == IMMUNE then n_after = n_after + 1 end end
        t.check("free.cast", n_after == n_before, "no refusal after the window (" .. n_before .. " -> " .. n_after .. ")")
    end,
}
