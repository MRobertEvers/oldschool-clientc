-- wintertodt_round: one Wintertodt round for the tick log, to diff against
-- RSProx (docs/rsprox-analysis/wintertodt/README.md):
--
--   python3 tools/rsprox_tells.py --anchor 7371-7373 ~/.rsprox/binary/archive/{3254,3262,732}.txt \
--       --ours <session>/ticklog.tsv
--   python3 tools/rsprox_wintertodt.py --ticklog <session>/ticklog.tsv
--
-- Through the doors, the round started by ::wintphase, all four braziers lit
-- (every lane's fires), then the south-east lane worked the way RSProx 3262
-- was: chop, fletch, feed, fix and relight, help the pyromancer. The energy is
-- dropped to 40% so the late round's brazier and pyromancer attacks come, then
-- to nothing for the settlement.
--
--   ./src/build_wint_opt_opt/torirsserver --scriptrun test/bosses/wintertodt_round.lua --bots 1 \
--       --name wint0 --ticks 4000 --session /tmp/wint --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end
local function count(t, name) local _, n = t.inv.count(name) return tonumber(n) or 0 end
-- The server's Warmth (tenths); drink a dose under half, the way a player would.
local warned = false
local function warmth(t)
    local r, v, src = t.var.server("varb11434_wint_warmth")
    if not warned then
        warned = true
        t.check("warmth.read", r == "ok", string.format("%s %s %s", tostring(r), tostring(v), tostring(src)))
    end
    return r == "ok" and tonumber(v) or 1000
end
local function keep_warm(t)
    if warmth(t) >= 700 then return end
    for _, dose in ipairs({ "wint_potion1", "wint_potion2", "wint_potion3", "wint_potion4" }) do
        if count(t, dose) > 0 then
            t.player.inv_op(dose, 1)
            t.ticks(2)
            return
        end
    end
end

-- The prison floor is a bridge deck: its locs are on cache level 1, which is
-- where the client's selector finds them (the server walks them at level 0).
local SE_BRAZIER = { 1638, 3997, 1 }
local SE_ROOTS = { 1639, 3988, 1 }
local BRAZIERS = { { 1620, 3997, 1 }, { 1620, 4015, 1 }, { 1638, 4015, 1 }, SE_BRAZIER }

-- Whatever the brazier is, do its op: fix, light or feed. Returns the verb's result.
local function tend(t, at)
    local r = t.player.click_loc("wint_brazier_broken", 1, { at = at })
    if r == "ok" then t.ticks(3) return "fix" end
    r = t.player.click_loc("wint_brazier", 1, { at = at })
    if r == "ok" then t.ticks(4) return "light" end
    if count(t, "wint_bruma_root") + count(t, "wint_bruma_kindling") > 0 then
        r = t.player.click_loc("wint_brazier_lit", 1, { at = at })
        if r == "ok" then t.ticks(12) return "feed" end
    end
    return "none"
end

local function help(t)
    local _, row = t.npc.nearest("wint_wizard_down", 6)
    if row then
        t.player.talk_to("wint_wizard_down", 1)
        t.ticks(2)
    end
end

return {
    id = "wintertodt_round",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel firemaking 99", "::setlevel woodcutting 99", "::setlevel fletching 99",
        "::setlevel herblore 99", "::setlevel construction 99", "::setlevel hitpoints 99",
        "::setvar varp6476_wint_ignisia 1",
        "::give dragon_axe", "::give knife", "::give tinderbox", "::give hammer",
        "::give wint_potion4 16",
        -- The pyromancer outfit: four warm items, as every recorded player wore.
        "::give pyromancer_hood", "::wield pyromancer_hood",
        "::give pyromancer_top", "::wield pyromancer_top",
        "::give pyromancer_bottom", "::wield pyromancer_bottom",
        "::give pyromancer_boots", "::wield pyromancer_boots",
        "::goto 1630 3962 0",
    },
    run = function(t)
        t.ticklog.start()
        t.ticklog.mark("doors")
        t.exec("enter", t.player.click_loc, "wint_door", 1)
        t.ticks(6)
        t.ticklog.mark("start")
        t.cheat("::wintphase 1", false)
        t.ticks(2)
        for _, at in ipairs(BRAZIERS) do
            t.player.click_loc("wint_brazier", 1, { at = at })
            t.ticks(4)
        end
        t.ticklog.mark("lit")
        t.cheat("::wintenergy 1400", false)
        local stop = now(t) + 2400
        while now(t) < stop do
            keep_warm(t)
            help(t)
            tend(t, SE_BRAZIER)
            keep_warm(t)
            t.player.click_loc("wint_roots", 1, { at = SE_ROOTS })
            t.ticks(14)
            keep_warm(t)
            if count(t, "wint_bruma_root") > 0 then
                t.player.use_item_on_item("knife", "wint_bruma_root")
                t.ticks(12)
            end
            keep_warm(t)
            help(t)
            tend(t, SE_BRAZIER)
        end
        t.ticklog.mark("end")
        t.cheat("::wintenergy 0", false)
        t.ticks(120)
        t.expect("wint.settled", t.msg.expect("Your subdued Wintertodt count is"))
        -- The deterministic contract (wintertodt_selftest.rs2): damage in
        -- tenths, the Warmth bar's fill, settlement, reward curves.
        t.cheat("::wintrun", false)
        t.ticks(2)
        t.expect("wint.contract", t.msg.expect("wintrun OK"))
        t.expect("wint.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
