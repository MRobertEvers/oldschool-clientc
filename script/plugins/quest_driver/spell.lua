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
-- same fields player.attack writes (slot, element, op), plus `spell`, so
-- npc.await_dead_engaged holds the slot -- and, since seam22, RE-CASTS that
-- spell on the slot's own copy when the fight stalls, instead of pressing an
-- Attack row (the wrap at the end of this file).  No stamp is written for a
-- refused or rune-less cast.
--
-- ONE COPY -- SEAM cast_picks_one_copy (seam22).  The seam21 rule
-- t.player.attack keeps (combat.lua, QD._combat_pick_copy) holds here too:
-- the cast fights ONE copy -- the nearest by default, or `opts` `{slot=n}` /
-- `{at={x,z[,level]}}` exactly as talk_to and attack take it -- and the press
-- is aimed by that copy's client element, re-aimed as itself, and asserted to
-- have landed on it; the settle, the stamp and the re-casts all watch its
-- slot.  Before this seam the settle watched npc.nearest's slot while the
-- bare-id press landed on whichever copy App_NpcScreenPosition ranked nearest
-- the VIEWPORT CENTRE, and a selector was not an argument at all: measured
-- build/quest_gate/s22cast_before1 (a Lumbridge goblin pair, Magic 99), row
-- cast.named_slot -- `{slot=72}` ignored, the verb read slot 125 "hp 30/30 ->
-- 24/30" and slot 72 was never hit; row cast.at_empty -- `{at={1,1}}` pressed
-- a goblin anyway instead of answering no_row.  Zogre Flesh Eaters' Crumble
-- Undead on Slash Bash (three spawned copies, parity2c) and every tier 2 magic
-- fight among several copies were blocked on it.
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
-- The last three are Crumble Undead's own (skill_combat/scripts/player/
-- spells/crumble_undead.rs2 [proc,pvm_crumble_undead]); the "only affects"
-- line read as `timeout` "the cast never ran" on Slash Bash, four casts in a
-- row, until seam22 read the chat pane in the -FAIL shot
-- (build/quest_gate/s22zogre1 row bash.magic.cast).
QD.player.SPELL_REFUSAL_LINES = {
    "You need to be on a members' server to use this spell.",
    "Your Magic level is not high enough for this spell.",
    "Your spells do not seem to affect it.",
    "That target is already frozen.",
    "You can't attack this npc.",
    "Crumble Undead has no effect on the Draugen.",
    "This spell only affects skeletons, zombies, ghosts and shades.",
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


-- The press half of a cast on ONE copy: the npc's "Cast <spell> -> <npc>"
-- row, aimed by `element` (the copy's client element, seam13's
-- reach_element), up to three presses a tick apart with one walk toward a
-- copy that answered `covered` -- QD._combat_press_attack's loop and its
-- reasons (combat.lua), for the select row instead of an op row.  `arm` is
-- the spell's arming: click_minimenu takes it as the hook before each of ITS
-- retry presses, and it is re-taken here before each of this loop's own
-- re-presses, because a covered press leaves a menu whose dismissal (and a
-- walk's ground click) clears the selection.  Free when it survived
-- ("already armed, nothing sent").
--
-- Returns (result, fail_detail, click, presses): on `ok`, fail_detail is nil
-- and `click` is _press_row's answer (row_text, row_action, element_id).
function QD.player._cast_press(target, label, element, arm)
    assert(element ~= nil, "cast press names no npc copy")
    local click_result, click
    local presses = 0
    local walked = false
    while true do
        presses = presses + 1
        target.reach_element = element
        click_result, click = QD.drive.click_minimenu(target, "select", nil, arm)
        if click_result == "covered" and not walked then
            walked = true
            local walk_result, walk_detail = QD.player.walk_near(target, nil, 1)
            QD.note("player.cast: the copy's press answered covered; walk_near it -> "
                .. tostring(walk_result) .. " " .. tostring(walk_detail))
        end
        target.reach_element = nil
        if click_result == "ok" or presses >= 3 then
            break
        end
        local next_tick = api_drive.tick() + 1
        QD.await({
            level = function() return api_drive.tick() >= next_tick end,
            note = "player.cast re-press",
        }, 3)
        local arm_result, arm_detail = arm()
        if arm_result ~= "ok" then
            return arm_result, arm_detail, nil, presses
        end
    end
    if click_result ~= "ok" then
        return click_result, label .. ": " .. tostring(click) .. " (" .. tostring(presses)
            .. " press(es))", nil, presses
    end
    -- _press_row matched its row on the named element, so an `ok` for any
    -- other copy is this file's bug, not the world's (the attack press's
    -- own check, combat.lua).
    assert(type(click) ~= "table" or click.element_id == element,
        "cast press landed on another npc copy")
    local held_ok, held_why = QD.player._select_row_is_held(target, click)
    if not held_ok then
        return "refused", label .. ": " .. tostring(held_why), click, presses
    end
    return "ok", nil, click, presses
end

-- t.player.cast(spell, npc_symbol, ticks, attack_op, opts) -> `ok` `timeout`
-- `refused` `no_runes` / `no_row` `not_visible` `unsupported` /
-- click_minimenu's own results.
--
-- `spell` is the spellbook component's name ("wind_strike"; "Wind Strike"
-- folds to it).  `ticks` (default 10) is the settle deadline after the press.
-- `attack_op` (default 2) only goes into the stamp, as the op a melee
-- re-engagement would use; since seam22 npc.await_dead_engaged re-CASTS a
-- stamp that carries a spell, so it is kept for the record only.  `opts` is
-- the one-copy selector (banner above): nil = the nearest copy, `{slot=n}`,
-- `{at={x,z[,level]}}`; a selector that matches no live copy is `no_row`
-- naming every live copy, and nothing is pressed.  `ok` = the cast was paid
-- (Magic XP rose) and the flight window passed, or the npc left the pool.
-- The detail names the press row, `pressed slot N (element E) at x,z,
-- watching slot N`, the Magic XP delta, the copy's health before and after
-- and the newest splat inside the window -- never "landed" (banner above).
function QD.player.cast(spell, npc_symbol, ticks, attack_op, opts)
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
    -- The ONE copy (seam22): attack's picker, so the two verbs choose a copy
    -- by the same rule and a selector means the same thing to both.
    local before_result, before, copy_text = QD._combat_pick_copy(target, npc_symbol, opts)
    if before_result ~= "ok" then
        return before_result, label .. ": " .. tostring(before)
    end
    local slot = before.slot
    local element = before.element_id
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

    -- Phase 2: the named copy's one "Cast ... ->" row.
    local press_result, press_detail, click, presses = QD.player._cast_press(target, label,
        element, arm)
    if press_result ~= "ok" then
        QD.player._show_backpack()
        return press_result, tostring(press_detail) .. " -- the copy named " .. tostring(copy_text)
    end
    local row_text = type(click) == "table" and tostring(click.row_text) or ""

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
    local detail = label .. " [" .. row_text .. "] (" .. tostring(arm_detail) .. ") in "
        .. tostring(presses) .. " press(es), pressed " .. tostring(copy_text)
        .. ", watching slot " .. tostring(slot) .. ": hp " .. before_health .. " -> "
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
            -- The copy's client element the press was aimed by (seam22), as
            -- player.attack stamps it.
            element = element,
            op = attack_op,
            -- What npc.await_dead_engaged re-casts (the wrap below).
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

-- ------------------------------------------- await_dead_engaged re-CASTS
--
-- SEAM cast_picks_one_copy (seam22): a fight a CAST opened is re-engaged by
-- casting that spell again on the slot's own copy, never by pressing Attack.
-- A melee press on Slash Bash is the 25% weapon (Zogre Flesh Eaters, the
-- weakness parity2c drove), and on Chronozon it is no damage at all, so the
-- old re-engagement turned a magic fight into the wrong fight the moment it
-- stalled.
--
-- The wrap lives HERE, the way combat.lua wraps pointer.lua's settle
-- (QD.player._settle_after_click): spell.lua is after combat.lua in
-- DRIVE_SCRIPT_PARTS, and combat.lua is not this seam's file.  For a stamp
-- that carries a spell the wrap swaps QD._combat_press_attack -- the one press
-- npc.await_dead_engaged's re-engagement calls, with the slot's CURRENT id and
-- element -- for a re-cast with the same four returns, for exactly the length
-- of the call.  Everything else the verb does (the kill verdict, the
-- three-signal stall rule, the attempts cap, the death fence) is unchanged,
-- and a stamp with no spell goes straight through.  Each re-cast re-opens the
-- magic tab, re-arms, presses the copy's select row, checks the row is the
-- held one and puts the backpack back; the row's detail gains
-- `[re-engagements re-CAST <spell>: N press(es) ok, M not]` and the first
-- server refusal a re-cast provoked (out of runes reads here).
QD.npc._await_dead_engaged_by_attack = QD.npc.await_dead_engaged

function QD.npc.await_dead_engaged(ticks, attempts)
    local engaged = QD._combat_last
    if not engaged or engaged.spell == nil or engaged.consumed then
        return QD.npc._await_dead_engaged_by_attack(ticks, attempts)
    end
    local spell = engaged.spell
    local since_result, since = api_drive.message_serial()
    if since_result ~= "ok" or type(since) ~= "number" then
        since = nil
    end
    local recast_ok, recast_not = 0, 0
    local last_not = nil
    local press_by_attack = QD._combat_press_attack
    -- The stall rule's "player idle" half, for a CAST fight.  api_drive.
    -- player_idle is movement idleness -- route_length 0 AND no minimap flag
    -- -- and a cast pressed on a copy several tiles off leaves the server's map
    -- flag up with no route to clear it: the player is in spell range, never
    -- walks, and the flag stays on the copy's old tile for the whole fight.
    -- Measured build/quest_gate/s22cast_after4 row cast.dead_named: Wind
    -- Strike on a goblin 7.8 tiles away, 150 ticks, hp 24/30 stale, ZERO
    -- re-engagements, the red flag on the minimap in its -FAIL shot; the
    -- adjacent copy (row cast.dead_default) was re-cast 8 times.  So while a
    -- cast fight is waited out, "not idle" from that read is taken as idle
    -- when the player's TILE has not changed since the previous read (the
    -- stall rule reads it at most once per five still ticks, so that is five
    -- ticks of standing on one tile: a walking player changes tile every
    -- tick).  Swapped for exactly the length of the call, like the press.
    local idle_by_route = api_drive.player_idle
    local idle_moving, idle_stale_flag = 0, 0
    local tile_result, last_tile = QD.world.tile()
    if tile_result ~= "ok" then
        last_tile = nil
    end
    api_drive.player_idle = function()
        local result, idle = idle_by_route()
        local now_result, now = QD.world.tile()
        local was = last_tile
        last_tile = now_result == "ok" and now or nil
        if result ~= "ok" or idle then
            return result, idle
        end
        if was ~= nil and last_tile ~= nil and was.x == last_tile.x and was.z == last_tile.z
            and was.level == last_tile.level then
            idle_stale_flag = idle_stale_flag + 1
            return "ok", true
        end
        idle_moving = idle_moving + 1
        return result, idle
    end
    QD._combat_press_attack = function(target, label, op, element)
        local result, fail_detail, row_text, presses = QD.player._recast_press(spell, target,
            label, element)
        if result == "ok" then
            recast_ok = recast_ok + 1
        else
            recast_not = recast_not + 1
            last_not = tostring(result) .. " " .. tostring(fail_detail)
        end
        return result, fail_detail, row_text, presses
    end
    local result, detail = QD.npc._await_dead_engaged_by_attack(ticks, attempts)
    QD._combat_press_attack = press_by_attack
    api_drive.player_idle = idle_by_route
    local tag = " [re-engagements re-CAST " .. spell .. ": " .. tostring(recast_ok)
        .. " press(es) ok, " .. tostring(recast_not) .. " not"
    if idle_stale_flag > 0 or idle_moving > 0 then
        tag = tag .. "; idle read " .. tostring(idle_stale_flag)
            .. "x standing still under a map flag, " .. tostring(idle_moving) .. "x moving"
    end
    if last_not then
        tag = tag .. "; last: " .. last_not
    end
    local refusal_word, refusal_line = QD.player._spell_refusal_since(since)
    if refusal_word then
        tag = tag .. "; the server refused a re-cast: '" .. tostring(refusal_line) .. "'"
    end
    return result, tostring(detail) .. tag .. "]"
end

-- One re-cast of `spell` on the copy whose client element is `element` --
-- the stand-in for QD._combat_press_attack while a cast fight is waited out.
-- Returns that function's four values: (result, fail_detail, row_text,
-- presses).
function QD.player._recast_press(spell, target, label, element)
    local cast_label = "re-cast " .. spell .. " on " .. tostring(label)
    local component_result, component_id = QD.player._spell_component(spell)
    if component_result ~= "ok" then
        QD.player._show_backpack()
        return component_result, cast_label .. ": " .. tostring(component_id), nil, 0
    end
    local arm = function()
        local arm_result, arm_detail = api_drive.spell_arm(component_id)
        if arm_result ~= "ok" then
            return arm_result, cast_label .. ": arming the spell -- " .. tostring(arm_detail)
        end
        return "ok", arm_detail
    end
    local arm_result, arm_detail = arm()
    if arm_result ~= "ok" then
        QD.player._show_backpack()
        return arm_result, arm_detail, nil, 0
    end
    local result, fail_detail, click, presses = QD.player._cast_press(target, cast_label,
        element, arm)
    QD.player._show_backpack()
    if result ~= "ok" then
        return result, fail_detail, nil, presses
    end
    return "ok", nil, type(click) == "table" and click.row_text or "", presses
end
