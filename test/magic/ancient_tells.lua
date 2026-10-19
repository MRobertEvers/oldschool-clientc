-- ancient_tells: what each of the sixteen Ancient Magicks combat spells sends,
-- for the tick log. Read it with tools/rsprox/ticklog_tells.py and compare with
-- the recorded casts (docs/rsprox-analysis/ancients/README.md,
-- tools/rsprox_ancients.py): per cast, the caster's zaros_casting_walkmerge
-- (rush/blitz) or zaros_vertical_casting_walkmerge (burst/barrage), the
-- element's cast synth to the caster, the flight (start 51, angle 16, progress
-- 64, 46 + 10 * tiles), and on the landing cycle the impact graphic and the
-- spell's impact area sound (range 10) at the target -- or, for a splash,
-- failedspell_impact at 124 and spellfail.
--
-- One fresh npc per cast, in a multiway zone (Lumbridge is single-way: the
-- second npc refuses with "I'm already under attack") so the burst and barrage
-- 3x3 reaches the npcs beside the target. Ice goes last, on npc types nothing
-- has struck yet: Ice Burst's splash freezes its neighbours, and a frozen npc
-- refuses the next ice cast ("That target is already frozen").
local TARGETS = { "black_demon", "hellhound", "greater_demon", "lesser_demon" }
local ICE_TARGETS = { "goblin", "imp", "woman", "man" }
local TIERS = { "rush", "burst", "blitz", "barrage" }
local ELEMENTS = { "smoke", "shadow", "blood", "ice" }

return {
    id = "ancient_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::~maxmage", "::setlevel magic 99", "::setlevel hitpoints 99",
        "::give ancient_sceptre 1", "::wield ancient_sceptre", "::runes", "::setvar varb4070_spellbook 1", "::goto 2388 3108 0",
    },
    run = function(t)
        t.ticklog.start()
        local results = {}
        for _, element in ipairs(ELEMENTS) do
            for i, tier in ipairs(TIERS) do
                local spell = element .. "_" .. tier
                local npc = (element == "ice") and ICE_TARGETS[i] or TARGETS[i]
                t.cheat("::spawn " .. npc, false)
                t.ticks(2)
                t.ticklog.mark(npc)
                local result, detail = t.player.cast(spell, npc, 8)
                results[#results + 1] = spell .. "=" .. tostring(result)
                t.expect("ancient." .. spell, result, detail)
                t.cheat("::boost hitpoints 255 0", false)
                t.ticks(4)
            end
        end
        t.expect("ancient.run", "ok", table.concat(results, " "))
    end,
}
