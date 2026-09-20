-- quest-driver / combat: the two verbs a quest needs to win a fight.
-- Owner: Q3 (docs/QUEST_SUITE_KIT.md).  Added to DRIVE_SCRIPT_PARTS in
-- src/plugin/torirs_plugin_drive.c; no chunk-scope local lives here (only
-- core.lua may declare one), so every helper hangs off QD.
--
-- ---------------------------------------------------------------------------
-- WHAT A CLIENT ACTUALLY KNOWS ABOUT AN NPC'S HITPOINTS: A RATIO
-- ---------------------------------------------------------------------------
--
-- Nothing in the protocol tells a client an npc's hitpoints.  What the server
-- sends is a HEADBAR block -- a healthbar type id and a FILL that is a
-- fraction of that type's own `width` denominator (src/game/rs_healthbar.h).
-- So falador_gardener's seven hitpoints are never readable as 7; at four
-- hitpoints his bar reads 17 out of 30 and that is the whole of it.  Every
-- reading below is therefore `<ratio>/<scale>`, and every row detail spells
-- it that way rather than pretending to a hitpoint count.
--
-- `row.health_ratio` / `row.health_scale` / `row.health_active` /
-- `row.hit_damage` / `row.hit_cycle` are this group's own additions to the
-- npc row (struct DriveNpcRow, torirs_plugin_drive_ui.c's
-- drive_ui_fill_npc_combat).  Two of them carry the load:
--
--   * `health_ratio` is -1 until the server sends this npc its FIRST bar,
--     which it only does once something hits it.  "No bar" is the normal
--     reading before the first landed hit and is not an error.
--   * `hit_cycle` is the start cycle of the newest LIVE hitsplat, so it rises
--     on every new splat and is the edge "a hit landed" is detected on.  A
--     splat whose window has passed is not reported at all -- an expired slot
--     read as a fresh hit forever is exactly how a combat loop stops
--     re-engaging a fight that has died.
--
-- ---------------------------------------------------------------------------
-- WHY A FIGHT NEEDS A VERB AT ALL, MEASURED 2026-09-20
-- ---------------------------------------------------------------------------
--
-- The first reading of the hunt seam was "the gardener is still alive after
-- six Attack clicks, so the click does not engage".  It does.  The probe
-- (build/quest_gate/q3probe) shows the click pressing the real row --
-- `Attack @yel@Gardener@or1@ (level-4)`, action 209 -- the player walking
-- 2999,3383 -> 2997,3381 into melee on his own, and hitpoints XP rising
-- 1154 -> 1155 while his own hp fell 10 -> 9.  Both sides were swinging.
--
-- What was actually wrong is arithmetic: a bronze dagger at attack 1 and
-- strength 1 against defence 7 misses most swings and hits for 1, and the
-- npc has seven hitpoints on a four-tick attack rate.  Thirty ticks is not a
-- fight, it is the opening of one.  So the verb's job is not to find a
-- different packet -- it is to WAIT long enough, on a signal that says
-- whether the fight is still running, and to re-engage only when it really
-- has stopped.

-- The npc row for a SERVER SLOT, not for a symbol.
--
-- await_dead locks onto the slot it was handed rather than re-resolving the
-- symbol every tick, because a symbol is not a target: falador_gardener has
-- three spawn rows in this pack (m46_52.spawn 2996,3381, m47_52.spawn
-- 3019,3370, and falador_gardener2 at 3011,3386), and the second of those is
-- inside the loaded scene from the Falador Park dig tile.  Polling by symbol
-- would find that one the moment the one we are fighting dies and the fight
-- would never be over.
function QD._combat_row_by_slot(slot)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].slot == slot then
            return "ok", rows[i]
        end
    end
    return "no_row", nil
end

-- "<ratio>/<scale>", or "no bar" before the first landed hit.  One spelling,
-- used by every detail in this file.
function QD._combat_health_text(row)
    if not row then
        return "gone"
    end
    if row.health_ratio < 0 then
        return "no bar"
    end
    return tostring(row.health_ratio) .. "/" .. tostring(row.health_scale)
        .. (row.health_active and "" or " (stale)")
end

-- Dead, as a client can see it: the row has left the pool (`no_row` from a
-- pool read that SUCCEEDED), or its bar's END fill has reached 0.  `end_fill`
-- is the value that reads 0 on the tick something dies (struct DriveNpcRow),
-- which is why the row carries that half of the pair and not `start_fill`.
--
-- Only `no_row` counts, never "the read did not answer ok": a pool read that
-- failed for its own reasons says nothing about whether anything died, and
-- claiming a kill on it is how a combat verb reports a win it never had.
function QD._combat_is_dead(result, row)
    if result == "no_row" then
        return true
    end
    if result ~= "ok" or not row then
        return false
    end
    return row.health_ratio == 0
end

-- THE FIGHT THIS DRIVER LAST STARTED, and it exists for exactly one reading.
--
-- `QD.player.attack` stamps the target it actually pressed an Attack row on:
-- the symbol it was called with, that npc's server slot, the health readings
-- either side of the settle, and the tick it read them at.  Nothing steers
-- off it -- await_dead still picks its target with a FRESH pool read, because
-- a stamp is a memory and a fight has to be tracked by something live.
--
-- It is here because of what a caller's own sequence means.  A quest file
-- that writes
--
--     t.exec("attack-gardener", t.player.attack, "falador_gardener", 2, 20)
--     t.exec("gardener-dead",   t.npc.await_dead, "falador_gardener", 60)
--
-- has handed the second verb the first one's target, and the first one read
-- that target's health bar.  When the second verb then finds no live row, the
-- stamp is the difference between "the fight we were watching finished while
-- the ledger was writing the row above" and "this name has never had a row in
-- this run at all".  A detail that can say which is worth more than one that
-- reports a bare absence.
QD._combat_last = nil

-- The stamp's own sentence, or "" when this run has never pressed an Attack
-- row on `npc_symbol` -- so a detail that names a prior fight names a REAL
-- one, never a plausible-sounding one.
function QD._combat_prior_text(npc_symbol)
    local last = QD._combat_last
    if not last or last.symbol ~= tostring(npc_symbol) then
        return ""
    end
    local text = "; an earlier player.attack in this run engaged slot "
        .. tostring(last.slot) .. " and read hp " .. tostring(last.health_before)
        .. " -> " .. tostring(last.health) .. " at tick " .. tostring(last.tick)
        .. " (" .. tostring(api_drive.tick() - last.tick) .. " tick(s) ago)"
    if not last.bar_seen then
        -- Named rather than hidden: an engagement that never saw a bar is a
        -- weaker fact than one that watched health fall, and the row says so.
        text = text .. ", though no health bar was ever sent for it"
    end
    return text
end

-- ---------------------------------------------------------------------- attack

-- t.player.attack(npc_symbol, op, ticks) -> `ok` / click_minimenu's own
-- results / `timeout` / `refused`.
--
-- ONE Attack click, then a settle on the first thing that proves the swing
-- reached the server: a new hitsplat on the npc, its health bar appearing or
-- moving, or the npc leaving the pool outright (a one-shot kill).
--
-- `op` defaults to 2 because Attack is op 2 on the npc records this pack
-- ships (`op1=Talk-to`, `op2=Attack` on falador_gardener in
-- OSRS-Content/osrs239-content/configs/all.npc), and is an argument because
-- it is not universal -- a pure monster with no Talk-to carries Attack on
-- op 1.  Whatever the number, the row that gets PRESSED is checked: the
-- minimenu row text has to start with "Attack", and a click that landed on
-- something else answers `refused` naming the row it pressed rather than
-- reporting a fight that never started.  This is the one verb where pressing
-- the wrong row is silent -- Talk-to opens a dialogue the next verb then
-- blames.
--
-- `ticks` is the settle deadline and defaults to the kit's ten.  A `timeout`
-- is NOT "the click failed": it means the deadline passed with no hit
-- landing, which is an ordinary opening to a fight -- a level-3 player misses
-- most swings (the banner above), and a click issued while ANOTHER action is
-- still in flight buys nothing for its first ten ticks.  Measured
-- 2026-09-20 (build/quest_gate/q3proof, the run before the one on disk): an
-- Attack click one tick after the spade dig that provoked the gardener
-- pressed its row first time and still saw no splat at all inside ten ticks,
-- while npc.await_dead -- starting immediately after it -- killed the same
-- npc in eleven ticks with no re-engagement at all.  At ticks=20 the same
-- row reads `hp no bar -> 17/30, hitsplat 3`.  So pass a bigger `ticks`
-- after another click, and read npc.await_dead's row rather than this one
-- for whether the fight was won.
function QD.player.attack(npc_symbol, op, ticks)
    op = op or 2
    ticks = ticks or 10

    local target, target_result = QD.player.by_symbol("npc", npc_symbol)
    if not target then
        return target_result, "attack " .. tostring(npc_symbol) .. ": " .. tostring(target_result)
    end

    local before_result, before = QD.npc.nearest(npc_symbol, 0)
    if before_result ~= "ok" then
        return before_result, "attack " .. tostring(npc_symbol)
            .. ": no npc row to fight (" .. tostring(before_result) .. ")"
    end
    local slot = before.slot
    local before_health = QD._combat_health_text(before)
    local before_hit = before.hit_cycle

    -- THREE presses, a tick apart, and the reason is what this verb is for.
    --
    -- Every other click verb in this driver aims at something standing
    -- still.  The first thing an Attack target does is CLOSE ON THE PLAYER
    -- (dig.rs2's [label,pirate_irate_gardener_attack] is one
    -- `npc_setmode(opplayer2)`), so the pixel _ensure_visible projected is a
    -- tile behind the npc by the time the right-press lands and the menu
    -- that opens carries no row for it: `covered`, from every camera pose,
    -- on a target in plain view.  Measured 2026-09-20 -- build/quest_gate/
    -- q3proof row `proof.attack`, "element 1073768063 at 350,53: pickset
    -- held=false, menu has no row for it", while the identical click one
    -- re-engagement later (the npc now standing in melee) pressed
    -- `Attack @yel@Gardener@gre@ (level-4)` first time.
    --
    -- A tick between presses is the point: it is a fresh projection of where
    -- the npc now IS, not the same failed pixel again.  (The q3proof run on
    -- disk now reads `in 1 press(es)` -- the retry is for the case that run
    -- hit once and no longer reproduces every time, not for every click.)
    local click_result, click
    local presses = 0
    while true do
        presses = presses + 1
        click_result, click = QD.drive.click_minimenu(target, op)
        if click_result == "ok" or presses >= 3 then
            break
        end
        local next_tick = api_drive.tick() + 1
        QD.await({
            level = function() return api_drive.tick() >= next_tick end,
            note = "player.attack re-press",
        }, 3)
    end
    if click_result ~= "ok" then
        return click_result, "attack " .. tostring(npc_symbol) .. " op" .. tostring(op)
            .. ": " .. tostring(click) .. " (" .. tostring(presses) .. " press(es))"
    end

    -- The row that was pressed, checked rather than assumed.
    local row_text = type(click) == "table" and click.row_text or ""
    if string.find(row_text, "Attack", 1, true) ~= 1 then
        return "refused", "attack " .. tostring(npc_symbol) .. " op" .. tostring(op)
            .. ": pressed '" .. tostring(row_text) .. "', which is not an Attack row"
    end

    local settle_result = QD.await({
        level = function()
            local result, row = QD._combat_row_by_slot(slot)
            if result ~= "ok" or not row then
                -- `no_row` is a one-shot kill; any other non-ok is a failed
                -- read, and neither is a hit, so neither ends this settle
                -- early except the kill.
                return result == "no_row"
            end
            return row.hit_cycle > before_hit or QD._combat_health_text(row) ~= before_health
        end,
        note = "player.attack " .. tostring(npc_symbol),
    }, ticks)

    local after_result, after = QD._combat_row_by_slot(slot)
    local detail = "attack " .. tostring(npc_symbol) .. " op" .. tostring(op)
        .. " [" .. tostring(row_text) .. "] in " .. tostring(presses) .. " press(es)"
        .. ": hp " .. before_health .. " -> " .. QD._combat_health_text(after)
    if after_result == "ok" and after and after.hit_damage >= 0 and after.hit_cycle > before_hit then
        detail = detail .. ", hitsplat " .. tostring(after.hit_damage)
    end

    -- Stamped on the timeout path too: "an Attack row was pressed on this
    -- slot and the deadline brought no splat" is still a fight that started
    -- here, and it is the opening of most of them (this verb's banner).  The
    -- stamp is written only past the row check above, so a click that pressed
    -- Talk-to never leaves one.
    QD._combat_last = {
        symbol = tostring(npc_symbol),
        slot = slot,
        health_before = before_health,
        health = QD._combat_health_text(after),
        bar_seen = (before_health ~= "no bar" and before_health ~= "gone")
            or (after_result == "ok" and after ~= nil and after.health_ratio >= 0),
        tick = api_drive.tick(),
    }

    if settle_result ~= "ok" then
        return "timeout", detail
            .. " -- no hit landed inside " .. tostring(ticks) .. " ticks"
    end
    return "ok", detail
end

-- ------------------------------------------------------------------ await_dead

-- t.npc.await_dead(npc_symbol, ticks, radius, attempts) -> `ok` `timeout`
-- `not_found`.
--
-- Resolves when the npc we started fighting has left the pool or its bar
-- reads 0 -- including when both were already true before the first tick of
-- the wait (the `no_row` arm below, `ok` with a detail that names it) --
-- and RE-ISSUES Attack when the fight has stopped: the npc's health
-- reading has not moved for five server ticks, no new hitsplat has landed in
-- that window, and the player is idle.  All three, because any one alone is
-- normal mid-fight -- a miss streak moves no health, and a player in melee is
-- "idle" the whole time (he is not walking).
--
-- `radius` (default 10, the same ten tiles dig.rs2's own `npc_find(coord,
-- falador_gardener, 10, 0)` uses) only picks WHICH npc; from then on the
-- fight is tracked by that npc's server slot and the radius is never applied
-- again, so a fight that drifts out of it still finishes.  `attempts`
-- (default 6) caps the re-engagements.
--
-- The detail always carries the re-engagement count and the last health
-- reading -- a kill that needed four re-engagements and one that needed none
-- are different facts about this driver and the ledger has to keep both.
function QD.npc.await_dead(npc_symbol, ticks, radius, attempts)
    ticks = ticks or 60
    radius = radius or 10
    attempts = attempts or 6

    local start_result, start_row = QD.npc.nearest(npc_symbol, radius)
    if start_result == "no_row" then
        -- ALREADY GONE IS NOT NEVER THERE, and until 2026-09-20 this verb
        -- could not tell them apart: both left by the same `return
        -- start_result` and both read `nothing to wait on within 10`.
        --
        -- `QD.npc.nearest` answers `not_found` when the SYMBOL does not
        -- resolve in the compack -- a typo in the quest file, a name that is
        -- not an npc -- and `no_row` when it resolves and the pool holds no
        -- live row for that id.  The second is what a WON fight looks like
        -- from here: the corpse's row leaves the pool a few ticks after the
        -- kill, so a quest whose press-to-press pace outran the ledger by
        -- those few ticks asks this verb to watch a death that has already
        -- happened.  That is Pirate's Treasure's `gardener-dead`, red at
        -- 73a4251d0 with `hunt.gardener_gone` -- the very next row, which
        -- asks the same pool the same question -- passing.  There is nothing
        -- left to wait for and nothing failed, so the answer is `ok` and the
        -- detail says WHY it is ok rather than claiming a wait it never ran.
        --
        -- What this reading cannot see is unchanged and still true of it: the
        -- pool read is the nearest 64 npcs (DRIVE_UI_POOL_CAP) within
        -- `radius`, so a live target ranked past the 64th, or standing
        -- outside the radius the caller named, reads `no_row` here exactly as
        -- a dead one does -- which is why the detail carries the pool read's
        -- own count and the radius it searched.
        return "ok", "await_dead " .. tostring(npc_symbol)
            .. ": already gone before the wait (no live row at call time"
            .. (type(start_row) == "string" and (", " .. start_row) or "") .. ")"
            .. QD._combat_prior_text(npc_symbol)
    end
    if start_result ~= "ok" then
        return start_result, "await_dead " .. tostring(npc_symbol)
            .. ": nothing to wait on within " .. tostring(radius) .. " (" .. tostring(start_result) .. ")"
    end

    local slot = start_row.slot
    local health = QD._combat_health_text(start_row)
    local hit = start_row.hit_cycle
    local still = 0
    local reengaged = 0
    local last = health
    local started = api_drive.tick()
    local elapsed = 0

    while elapsed < ticks do
        -- One SERVER tick per turn of the loop, counted the way QD.ticks
        -- counts (api_drive.tick() is the server tick, never a client cycle).
        local next_tick = api_drive.tick() + 1
        QD.await({
            level = function() return api_drive.tick() >= next_tick end,
            note = "await_dead tick",
        }, 3)
        elapsed = api_drive.tick() - started

        local result, row = QD._combat_row_by_slot(slot)
        if QD._combat_is_dead(result, row) then
            return "ok", "await_dead " .. tostring(npc_symbol) .. ": dead after "
                .. tostring(elapsed) .. " tick(s), " .. tostring(reengaged)
                .. " re-engagement(s), last hp " .. last
        end

        if result ~= "ok" or not row then
            -- A pool read that answered neither `ok` nor `no_row`: nothing
            -- was learned this tick, so nothing is judged on it.  Nor is
            -- `row.hit_cycle` read below -- indexing a nil row RAISES, and a
            -- raise in this sandbox ends the whole run (trap 5), turning a
            -- transient read into twenty unreached rows.
            QD.note("await_dead: npc pool read answered " .. tostring(result))
        else
            last = QD._combat_health_text(row)
            if last ~= health or row.hit_cycle > hit then
                health = last
                hit = row.hit_cycle
                still = 0
            else
                still = still + 1
            end

            -- The fight has stopped: five server ticks with the health
            -- reading unmoved AND no new hitsplat AND the player standing
            -- still.  All three, because each alone is ordinary mid-fight --
            -- a miss streak moves no health, and a player in melee is "idle"
            -- the whole time (idle means not walking, not out of combat).
            if still >= 5 then
                still = 0
                local idle_result, idle = api_drive.player_idle()
                if idle_result == "ok" and idle and reengaged < attempts then
                    reengaged = reengaged + 1
                    -- The re-engagement is the verb itself, so a re-click
                    -- that lands on the wrong row is caught here too; its
                    -- own answer is folded into this row's detail rather
                    -- than a row of its own, which is what QD.note is for.
                    local attack_result, attack_detail = QD.player.attack(npc_symbol)
                    QD.note("await_dead re-engage " .. tostring(reengaged) .. ": "
                        .. tostring(attack_result) .. " " .. tostring(attack_detail))
                end
            end
        end
    end

    return "timeout", "await_dead " .. tostring(npc_symbol) .. ": still alive after "
        .. tostring(ticks) .. " tick(s), " .. tostring(reengaged)
        .. " re-engagement(s), hp " .. last
end
