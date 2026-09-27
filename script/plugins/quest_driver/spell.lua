-- quest-driver / spell: t.player.cast -- a spellbook spell cast on an npc,
-- the way a player casts one.  Seam cast_spell_on_npc (seam19, 2026-09-27):
-- Family Crest's Chronozon and every later magic fight in tier 2 need the
-- four elemental blasts to LAND on an npc, and nothing in this driver could
-- press a spell at all.
--
-- This file declares nothing at chunk scope (core.lua's rule) and wraps
-- nothing: it only adds QD.player.cast and its private helpers.  It comes
-- after combat.lua in DRIVE_SCRIPT_PARTS because it reads combat.lua's slot
-- helpers (QD._combat_row_by_slot, QD._combat_health_text) and writes the
-- record combat.lua's npc.await_dead_engaged holds (QD._combat_last).
--
-- ---------------------------------------------------------------------------
-- WHAT A CAST IS, CLIENT AND SERVER
--
--   1. The magic tab.  A spell's component is only pressable while it is
--      DISPLAYED (app_minimenu_ui_pick_live refuses a display-hidden node),
--      so the tab is opened first, exactly as the backpack verbs open theirs.
--   2. The spell's own "Cast <spell>" row -- REVCONFIG_MINIMENU_TGT_BUTTON,
--      which the client turns into app->targetsel (app_minimenu.c "Arm target
--      mode from a spell/prayer button").  api_drive.spell_arm
--      (torirs_plugin_drive_pointer.c drive_pointer_spell_arm) runs that one
--      row through the dispatcher a real click reaches, and refuses whenever
--      the real menu would not have offered it: no target verb, an effective
--      target mask of 0 (the server's IF_SETEVENTS bits), the tab not shown.
--      api_drive.if_click could not do this: it builds IF_BUTTON for every
--      IF3 component, and a rev-239 spellbook component is IF3.
--   3. The npc's collapsed "Cast <spell> -> <npc>" row (add_world_select_row,
--      TGT_NPC), pressed through click_minimenu's "select" wildcard -- the
--      same press, framing, re-aim and far-side logic player.attack and
--      use_on's second phase use.  The client sends OPNPCT with the spell's
--      component; the server runs `[apnpct,magic_spellbook:<spell>]`
--      (skill_combat/scripts/player/player_magic.rs2) -> ~pvm_default_spell.
--   4. The server's answer, which is what this verb settles on:
--        - a REFUSAL sentence in the chat ring.  "You do not have enough
--          <Rune> Runes to cast this spell." is its own word, `no_runes`
--          (skill_magic/scripts/magic.rs2 [proc,check_spell_requirements]);
--          the level/members/immune/frozen sentences and the engine's
--          single-way pair (QD.ATTACK_REFUSAL_LINES) are `refused`.
--        - the CAST itself: ~pvm_spell_cast deletes the runes and pays the
--          spell's base Magic experience (~give_spell_xp) on the tick the
--          server runs the script, landed or splashed.  Magic XP rising is
--          therefore the proof the cast happened, and it is what `ok` means.
--        - what the cast DID, which the client can only half see.  A landed
--          hit queues damage on the npc (~pvm_spell_success -> npc_queue(2))
--          and pays 2 more Magic XP per point of damage; a splash
--          (~pvm_spell_fail) plays the failedspell spotanim, which no driver
--          read carries.  And a hitsplat on the npc after the cast is not
--          proof of a landed spell: the npc swings back, the player's
--          auto-retaliate answers in MELEE, and a melee miss is a 0 splat on
--          the same npc (measured 2026-09-27 on Chronozon,
--          build/quest_gate/s19cast_chron_b: four "0" splats, hp 30/30 all the
--          way, the log showing [apnpc2]/[opnpc2] swings between the casts
--          and no "Chronozon weakens..." line).  So the detail REPORTS the
--          Magic XP delta and the splats and health it saw, and never claims
--          "landed".  A quest that needs the spell to land (Chronozon's
--          ~chronozon_spell runs only on a landed hit) waits on the
--          content's own sentence -- t.msg.await("Chronozon weakens") -- and
--          casts again when it does not come.
--
-- A manual cast does not chain (~pvm_combat_spell_checks opens with
-- p_stopaction, and only autocast continues), so one call is one cast.
--
-- THE STAMP.  A cast that reached the npc stamps QD._combat_last with the
-- same fields player.attack writes, plus `spell`, so npc.await_dead_engaged
-- can hold the slot.  Its re-engagement presses an ATTACK row (op 2 unless
-- the caller passed one) -- it does not re-cast; a pure magic fight loops
-- this verb instead.  No stamp is written for a refused or rune-less cast.
-- ---------------------------------------------------------------------------

-- The rune refusal, as a pattern over the trimmed line: the rune's name is
-- oc_name with its last five characters removed (" rune"), so "Air" for
-- air_rune -- see magic.rs2's four mes() lines.
QD.player.SPELL_NO_RUNES_PATTERN = "^You do not have enough (.-) Runes to cast this spell%.$"

-- Every other sentence content prints when it declines a cast on an npc,
-- word for word: magic.rs2 [proc,check_spell_requirements] (members, level)
-- and player_magic.rs2 (~pvm_combat_spell_checks immunity,
-- [proc,pvm_freeze_allowed]).  The engine's own single-way refusals are
-- QD.ATTACK_REFUSAL_LINES (combat.lua) and its unreachable-target line is
-- QD.player.CLICK_REFUSAL_LINES (pointer.lua); both are checked beside these.
QD.player.SPELL_REFUSAL_LINES = {
    "You need to be on a members' server to use this spell.",
    "Your Magic level is not high enough for this spell.",
    "Your spells do not seem to affect it.",
    "That target is already frozen.",
}

-- Ticks after the Magic XP lands that the verb keeps watching the npc: the
-- damage of a landed spell is queued `duration / 30 + 1` ticks out
-- (player_magic.rs2 ~pvm_spell_success), and a strike's projectile flies for
-- well under four ticks at spell range, so a splat the spell made shows
-- inside this window.
QD.player.SPELL_FLIGHT_TICKS = 6

-- The magic tab paints the spellbook in its own onload; the same budget the
-- emote tab waits for its grid (ui.lua EMOTE_PAINT_TICKS).
QD.player.SPELL_PAINT_TICKS = 12

-- "Wind Strike" / "wind-strike" / "wind_strike" -> "wind_strike".
function QD.player._spell_symbol(spell)
    local folded = string.lower(tostring(spell))
    folded = string.gsub(folded, "[%s%-]+", "_")
    return folded
end

-- The refusal a cast provoked, or nil: the OLDEST matching line newer than
-- `since` (api_drive.messages answers newest-first, so the last match walking
-- forward is the oldest -- QD._combat_refusal_since's rule).  Returns
-- (word, line) with word `no_runes` or `refused`.
function QD.player._spell_refusal_since(since)
    if type(since) ~= "number" then
        return nil, nil
    end
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return nil, nil
    end
    local word, found = nil, nil
    for i = 1, #rows do
        if rows[i].serial > since and type(rows[i].text) == "string" then
            local trimmed = string.match(rows[i].text, "^%s*(.-)%s*$") or rows[i].text
            if string.match(trimmed, QD.player.SPELL_NO_RUNES_PATTERN) then
                word, found = "no_runes", trimmed
            else
                -- The engine's own "the click did not land" sentences
                -- (QD.player.CLICK_REFUSAL_LINES, pointer.lua) first: a goblin
                -- behind a wall answers "I can't reach that!" and nothing
                -- else, which read as `timeout` "the cast never ran" until the
                -- seam19 closer measured it (build/quest_gate/s19close_probe3).
                local engine = QD.player._refusal_line(trimmed) or QD._combat_refusal_line(trimmed)
                if engine then
                    word, found = "refused", engine
                else
                    for j = 1, #QD.player.SPELL_REFUSAL_LINES do
                        if trimmed == QD.player.SPELL_REFUSAL_LINES[j] then
                            word, found = "refused", trimmed
                        end
                    end
                end
            end
        end
    end
    return word, found
end

-- Magic experience now, or nil when the stat read did not answer.
function QD.player._magic_xp()
    local result, reading = QD.skill.read("magic")
    if result ~= "ok" or type(reading) ~= "table" then
        return nil
    end
    return reading.experience
end

-- Open the magic tab and wait until the spell's component is DISPLAYED.
-- Returns (ok, component_id, tab_detail) or (result, detail).
function QD.player._spell_component(symbol)
    local component_symbol = "magic_spellbook:" .. symbol
    local tab_result, tab_detail = QD.ui.tab("magic")
    if tab_result ~= "ok" then
        return tab_result, "cast " .. symbol .. ": the magic tab would not open -- "
            .. tostring(tab_detail)
    end
    local component_id = nil
    local last = "not read"
    local shown = QD.await({
        level = function()
            local result, id = api_drive.component(component_symbol, -1)
            if result == "no_row" then
                last = "no_row"
                return true
            end
            if result ~= "ok" then
                last = result
                return false
            end
            component_id = id
            local presented_result, presented = api_drive.widget_presented(id)
            last = "presented " .. tostring(presented_result) .. " " .. tostring(presented)
            return presented_result == "ok" and presented == true
        end,
        note = "player.cast: " .. component_symbol .. " displayed",
    }, QD.player.SPELL_PAINT_TICKS)
    if last == "no_row" then
        return "no_row", "cast " .. symbol .. ": no component " .. component_symbol
            .. " in this pack (spell names are the spellbook's component names, e.g."
            .. " wind_strike, fire_blast)"
    end
    if shown ~= "ok" or component_id == nil then
        return "not_visible", "cast " .. symbol .. ": " .. component_symbol
            .. " never displayed inside " .. tostring(QD.player.SPELL_PAINT_TICKS)
            .. " ticks after the magic tab press (" .. last .. ")"
    end
    return "ok", component_id, tab_detail
end

-- t.player.cast(spell, npc_symbol, ticks, attack_op) -> `ok` `timeout`
-- `refused` `no_runes` / `no_row` `not_visible` `unsupported` /
-- click_minimenu's own results.
--
-- `spell` is the spellbook component's name ("wind_strike"; "Wind Strike"
-- folds to it).  `ticks` (default 10) is the settle deadline after the press.
-- `attack_op` (default 2) only goes into the stamp, for the Attack row
-- npc.await_dead_engaged re-engages with.  `ok` = the cast was paid (Magic XP
-- rose) and the flight window passed, or the npc left the pool.  The detail
-- names the press row, the Magic XP delta, the npc's health before and after
-- and the newest splat inside the window -- never "landed" (banner above).
function QD.player.cast(spell, npc_symbol, ticks, attack_op)
    ticks = ticks or 10
    attack_op = attack_op or 2
    local symbol = QD.player._spell_symbol(spell)
    local label = "cast " .. symbol .. " on " .. tostring(npc_symbol)

    if QD.player._death_fence("t.player.cast " .. symbol .. " " .. tostring(npc_symbol)) then
        return "refused", QD.player._death_text(QD._death)
    end
    -- The shared binary is rebuilt only by a closer; this file is read live
    -- by every client that starts (docs/QUEST_SUITE_KIT.md working rules).
    if api_drive.spell_arm == nil then
        return "unsupported", label .. ": this binary predates api_drive.spell_arm"
            .. " (torirs_plugin_drive_pointer.c) -- rebuild it; no cast was attempted"
    end

    local target, target_result = QD.player.by_symbol("npc", npc_symbol)
    if not target then
        return target_result, label .. ": " .. tostring(target_result)
    end
    local before_result, before = QD.npc.nearest(npc_symbol, 0)
    if before_result ~= "ok" then
        return before_result, label .. ": no npc row to cast on (" .. tostring(before_result) .. ")"
    end
    local slot = before.slot
    local before_health = QD._combat_health_text(before)
    local before_hit = before.hit_cycle

    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        return component_result, component_id
    end

    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" or type(since) ~= "number" then
        since = nil
    end
    local xp_before = QD.player._magic_xp()

    local arm = function()
        local arm_result, arm_detail = api_drive.spell_arm(component_id)
        if arm_result ~= "ok" then
            return arm_result, label .. ": arming the spell -- " .. tostring(arm_detail)
        end
        return "ok", arm_detail
    end
    local arm_result, arm_detail = arm()
    if arm_result ~= "ok" then
        return arm_result, arm_detail
    end

    -- Phase 2: the npc's one "Cast ... ->" row.  `arm` is handed in as the
    -- retry hook for the reason use_on hands in its own: a covered press
    -- leaves its menu up and the next press cancels it, which clears a live
    -- selection (app_selection_clear), so every retry re-takes the arming --
    -- free when it survived ("already armed, nothing sent").
    local click_result, click = QD.drive.click_minimenu(target, "select", nil, arm)
    if click_result ~= "ok" then
        return click_result, label .. ": " .. tostring(click)
    end
    local held_ok, held_why = QD.player._select_row_is_held(target, click)
    local row_text = type(click) == "table" and tostring(click.row_text) or ""
    if not held_ok then
        return "refused", label .. ": " .. tostring(held_why)
    end

    local refusal_word, refusal_line = nil, nil
    local xp_tick = nil
    local gone = false
    local settle_result = QD.await({
        level = function()
            refusal_word, refusal_line = QD.player._spell_refusal_since(since)
            if refusal_word then
                return true
            end
            local result = QD._combat_row_by_slot(slot)
            if result == "no_row" then
                gone = true
                return true
            end
            if xp_tick == nil and xp_before ~= nil then
                local xp_now = QD.player._magic_xp()
                if xp_now ~= nil and xp_now > xp_before then
                    xp_tick = api_drive.tick()
                end
            end
            return xp_tick ~= nil and api_drive.tick() >= xp_tick + QD.player.SPELL_FLIGHT_TICKS
        end,
        note = "player.cast " .. symbol .. " " .. tostring(npc_symbol),
    }, ticks)

    local after_result, after = QD._combat_row_by_slot(slot)
    local xp_after = QD.player._magic_xp()
    local detail = label .. " [" .. row_text .. "] (" .. tostring(arm_detail) .. "): hp " .. before_health .. " -> "
        .. QD._combat_health_text(after) .. ", magic xp " .. tostring(xp_before)
        .. " -> " .. tostring(xp_after)
    if after_result == "ok" and after and after.hit_damage >= 0 and after.hit_cycle > before_hit then
        detail = detail .. ", newest splat " .. tostring(after.hit_damage)
            .. " (the spell's or a melee auto-retaliation's -- the client cannot tell)"
    elseif after_result == "ok" then
        detail = detail .. ", no splat"
    end

    -- Put the backpack back: every held-item verb presses backpack cells, and
    -- a use_on issued with the sidebar on another tab refuses on the ARM (the
    -- emote verb's rule, ui.lua).
    QD.player._show_backpack()

    if not refusal_word then
        refusal_word, refusal_line = QD.player._spell_refusal_since(since)
    end
    if refusal_word then
        -- No stamp: nothing was cast, so there is no fight for
        -- npc.await_dead_engaged to hold (player.attack's rule).
        return refusal_word, detail .. " -- the SERVER refused the cast: '"
            .. tostring(refusal_line) .. "'"
    end

    local xp_paid = xp_before ~= nil and xp_after ~= nil and xp_after > xp_before
    if gone or xp_paid then
        QD._combat_last = {
            symbol = tostring(npc_symbol),
            slot = slot,
            op = attack_op,
            spell = symbol,
            npc_id = before.npc_id,
            name = before.name,
            health_before = before_health,
            health = QD._combat_health_text(after),
            bar_seen = (before_health ~= "no bar" and before_health ~= "gone")
                or (after_result == "ok" and after ~= nil and after.health_ratio >= 0),
            tick = api_drive.tick(),
        }
    end

    if QD.player._death_fence("t.player.cast " .. symbol .. ", after the settle") then
        return "refused", QD.player._death_text(QD._death)
    end

    if gone then
        return "ok", detail .. " -- the npc left the pool inside the settle"
    end
    if xp_paid and settle_result == "ok" then
        return "ok", detail .. " -- CAST (Magic XP paid; watched "
            .. tostring(QD.player.SPELL_FLIGHT_TICKS) .. " ticks of flight)"
    end
    return "timeout", detail .. " -- no Magic XP, no refusal and the npc still there after "
        .. tostring(ticks) .. " ticks: the cast never ran"
end
