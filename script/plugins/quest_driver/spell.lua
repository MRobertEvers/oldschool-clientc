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
    -- A cast on a GROUND OBJ or a LOC (seam23, QD.player._cast_on_world):
    -- Telekinetic Grab's own sentences (skill_magic/scripts/spells/
    -- telegrab.rs2 [label,magic_spell_telegrab]: the telegrab_disabled param,
    -- a stack that left before the grab landed -- runes and XP already
    -- paid, nothing gained -- and ~pickup_obj_check_for_space's
    -- ~inv_no_space_message, player/messages.rs2), the Charge Orb family's
    -- (skill_magic/scripts/spells/charge_orb.rs2 [label,
    -- magic_spell_charge_orb]) and the Lunar farming spells'
    -- (skill_magic/scripts/spells/farming_spells.rs2).
    "You can't cast this spell on that object.",
    "Too late - it's gone!",
    "You don't have enough inventory space.",
    "You must be holding an orb to enchant it.",
    "This spell needs to be cast on an air obelisk.",
    "This spell needs to be cast on a water obelisk.",
    "This spell needs to be cast on an earth obelisk.",
    "This spell needs to be cast on a fire obelisk.",
    "This spell needs to be cast on an obelisk.",
    "This patch is not diseased.",
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
--
-- `quick` (seam5 attack_fast_path, combat.lua QD._combat_quick): the fast
-- press instead (QD.drive._press_quick on the "select" row, re-armed before
-- its one re-press) -- one aim, one press, one re-aim, one more press, no
-- walk; a fifth return carries its account.
function QD.player._cast_press(target, label, element, arm, quick, eater)
    assert(element ~= nil, "cast press names no npc copy")
    local press_quick = function()
        target.reach_element = element
        local quick_result, quick_click, account, quick_presses =
            QD.drive._press_quick(target, "select", arm)
        target.reach_element = nil
        if quick_result ~= "ok" then
            return quick_result, label .. ": " .. tostring(quick_click) .. " ("
                .. tostring(quick_presses) .. " press(es)) -- " .. tostring(account), nil,
                quick_presses, account
        end
        assert(type(quick_click) ~= "table" or quick_click.element_id == element,
            "cast press landed on another npc copy")
        local quick_held, quick_why = QD.player._select_row_is_held(target, quick_click)
        if not quick_held then
            return "refused", label .. ": " .. tostring(quick_why) .. " -- " .. tostring(account),
                quick_click, quick_presses, account
        end
        return "ok", nil, quick_click, quick_presses, account
    end
    if quick then
        return press_quick()
    end
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
        -- b63-seam1: a re-cast inside a kill wait eats between its presses
        -- (combat.lua QD._combat_press_eat).  The spell may still be armed
        -- from the press that missed, and an Eat pressed in target mode is a
        -- cast on the food, so the selection is cancelled first and re-armed
        -- below as every re-press already is.  Still under the eat line
        -- after the eat, the rest of this press is the fast one.
        local rearm = arm
        if QD._combat_eat_due(eater) then
            local cancel_result, cancel_detail = QD.player.cancel_selection("eat between two cast presses")
            QD.note("player.cast: " .. tostring(cancel_result) .. " " .. tostring(cancel_detail))
            local under = QD._combat_press_eat(eater, "before cast press " .. tostring(presses + 1))
            -- The Eat opened the backpack, and a spell is armed only from a
            -- DISPLAYED magic tab: without this every re-arm after an eat
            -- answered "the node or an ancestor of it is display-hidden"
            -- (build/quest_gate/b63s1_eatcast1 row 3, 0 of 3 re-casts armed).
            rearm = function()
                local tab_result, tab_detail = QD.ui.tab("magic")
                if tab_result ~= "ok" then
                    return tab_result, label .. ": the magic tab after an eat -- " .. tostring(tab_detail)
                end
                local arm_result, arm_detail
                for _ = 1, 3 do
                    arm_result, arm_detail = arm()
                    if arm_result == "ok" then
                        return arm_result, arm_detail
                    end
                    local tab_tick = api_drive.tick() + 1
                    QD.await({
                        level = function() return api_drive.tick() >= tab_tick end,
                        note = "player.cast magic tab after an eat",
                    }, 3)
                end
                return arm_result, arm_detail
            end
            if under then
                eater.press_quick = eater.press_quick + 1
                local arm_result, arm_detail = rearm()
                if arm_result ~= "ok" then
                    return arm_result, arm_detail, nil, presses
                end
                local quick_result, quick_detail, quick_click, quick_presses, quick_account = press_quick()
                QD.note("player.cast: hp under the eat line (" .. tostring(eater.below)
                    .. ") before press " .. tostring(presses + 1) .. " -- the fast press: "
                    .. tostring(quick_result) .. " " .. tostring(quick_account))
                return quick_result, quick_detail, quick_click, presses + (quick_presses or 0),
                    quick_account
            end
        end
        local arm_result, arm_detail = rearm()
        if arm_result ~= "ok" then
            return arm_result, arm_detail, nil, presses
        end
    end
    if click_result ~= "ok" then
        -- The spell was re-armed before this press and nothing spent it: a
        -- player whose click missed cancels the spell before anything else
        -- (seam spell_left_selected_after_a_fight, the banner over
        -- QD.player.cancel_selection at the end of this file).  Left armed,
        -- every later press -- the eater's shark included -- meets target mode.
        local cancel_result, cancel_detail = QD.player.cancel_selection()
        return click_result, label .. ": " .. tostring(click) .. " (" .. tostring(presses)
            .. " press(es)); " .. tostring(cancel_result) .. " " .. tostring(cancel_detail), nil, presses
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
    -- seam5 attack_fast_path: `ticks` <= 2 or opts.quick -> the fast press
    -- (combat.lua QD._combat_quick; the attack verb's banner).
    local quick = QD._combat_quick(ticks, opts)
    -- t.player.attack takes opts.eat (b63-seam1); the cast verb does not, and
    -- the selector below would drop it unread -- so it is a bug here, not food.
    assert(type(opts) ~= "table" or opts.eat == nil,
        "t.player.cast takes no opts.eat: eat in the kill wait (await_dead_engaged opts.eat)")
    opts = QD._combat_selector(opts)
    ticks = ticks or 10
    attack_op = attack_op or 2
    -- SEAM self_cast_and_moving_multinpc_press (seam28): NO target -- a
    -- teleport, Charge, Bones to Bananas -- is the spell cell's own op, a
    -- plain button press (QD.player._cast_self, the last section of this
    -- file).  `t.player.cast("varrock_teleport")`, or `{kind='self'}`.
    if npc_symbol == nil or (type(npc_symbol) == "table" and npc_symbol.kind == "self") then
        return QD.player._cast_self(spell, ticks)
    end
    -- SEAM cast_on_ground_obj_and_loc (seam23): a `{kind=, id=}` world
    -- target -- a ground obj or a loc -- goes to QD.player._cast_on_world
    -- (the section after this function); a `{kind="npc", id="<symbol>"}`
    -- table is the npc path below, as if the symbol had been passed bare.
    if type(npc_symbol) == "table" then
        if npc_symbol.kind == "obj" or npc_symbol.kind == "loc" then
            return QD.player._cast_on_world(spell, npc_symbol, ticks)
        end
        -- SEAM recruitmentdrive_spawn_artifact_and_held_cast (seam27): a
        -- CARRIED item -- Superheat Item, the alchemies, the enchants --
        -- goes to QD.player._cast_on_held (the last section of this file).
        if npc_symbol.kind == "held" then
            return QD.player._cast_on_held(spell, npc_symbol, ticks)
        end
        if npc_symbol.kind == "npc" and type(npc_symbol.symbol or npc_symbol.id) == "string" then
            npc_symbol = npc_symbol.symbol or npc_symbol.id
        else
            return "unsupported", "cast " .. tostring(spell) .. ": a table target must be"
                .. " {kind='obj'|'loc', id=<symbol or id>}, {kind='held', id=<obj symbol>},"
                .. " {kind='self'} or {kind='npc', id=<symbol>}, got kind "
                .. tostring(npc_symbol.kind) .. " id " .. tostring(npc_symbol.id)
        end
    end
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
    local press_result, press_detail, click, presses, quick_account =
        QD.player._cast_press(target, label, element, arm, quick)
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
        .. tostring(presses) .. " press(es)"
        .. (quick and (" (" .. tostring(quick_account) .. ")") or "")
        .. ", pressed " .. tostring(copy_text)
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
            -- seam5: a fast-path cast's re-casts are fast presses too.
            quick = quick or nil,
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
    if quick and xp_paid then
        -- seam5: a fast cast's `ticks` (<= 2) is shorter than the flight
        -- window by design; the cast DID run (XP paid, the stamp is written),
        -- so the timeout says that rather than "never ran".
        return "timeout", detail .. " -- CAST (Magic XP paid; the stamp is written), but the "
            .. tostring(QD.player.SPELL_FLIGHT_TICKS) .. "-tick flight window outlasts ticks="
            .. tostring(ticks) .. ", so no splat was waited for"
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

function QD.npc.await_dead_engaged(ticks, attempts, opts)
    local engaged = QD._combat_last
    if not engaged or engaged.spell == nil or engaged.consumed then
        return QD.npc._await_dead_engaged_by_attack(ticks, attempts, opts)
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
    QD._combat_press_attack = function(target, label, op, element, quick, eater)
        -- b63-seam1: the wait's eater eats before the re-cast arms anything
        -- (combat.lua QD._combat_press_eat); under the eat line after it the
        -- re-cast is the fast press.  _cast_press eats between its presses.
        if QD._combat_press_eat(eater, "before the re-cast") and not quick then
            quick = true
            eater.press_quick = eater.press_quick + 1
        end
        local result, fail_detail, row_text, presses, account = QD.player._recast_press(spell,
            target, label, element, quick, eater)
        if result == "ok" then
            recast_ok = recast_ok + 1
        else
            recast_not = recast_not + 1
            last_not = tostring(result) .. " " .. tostring(fail_detail)
        end
        return result, fail_detail, row_text, presses, account
    end
    local result, detail = QD.npc._await_dead_engaged_by_attack(ticks, attempts, opts)
    QD._combat_press_attack = press_by_attack
    api_drive.player_idle = idle_by_route
    -- The fight is over (or the wait is): nothing the caller does next is a
    -- cast on this npc, so no spell may be left armed for it to meet
    -- (seam spell_left_selected_after_a_fight; QD.player.cancel_selection).
    -- A dead player's run is already ending at the death fence.
    local selection_text = ""
    if QD._death == nil then
        local cancel_result, cancel_detail, was_armed = QD.player.cancel_selection()
        if cancel_result ~= "ok" then
            selection_text = "; SELECTION NOT CLEARED: " .. tostring(cancel_result) .. " " .. tostring(cancel_detail)
        elseif was_armed then
            selection_text = "; fight over with a selection still armed -- " .. tostring(cancel_detail)
        else
            selection_text = "; fight over with nothing armed"
        end
    end
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
    return result, tostring(detail) .. tag .. selection_text .. "]"
end

-- One re-cast of `spell` on the copy whose client element is `element` --
-- the stand-in for QD._combat_press_attack while a cast fight is waited out.
-- Returns that function's four values: (result, fail_detail, row_text,
-- presses).
function QD.player._recast_press(spell, target, label, element, quick, eater)
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
    local result, fail_detail, click, presses, account = QD.player._cast_press(target,
        cast_label, element, arm, quick, eater)
    QD.player._show_backpack()
    if result ~= "ok" then
        return result, fail_detail, nil, presses
    end
    return "ok", nil, type(click) == "table" and click.row_text or "", presses, account
end

-- ------------------------------------ a cast on a GROUND OBJ or a LOC
--
-- SEAM cast_on_ground_obj_and_loc (seam23, 2026-09-28).  t.player.cast
-- only ever targeted npcs, so a guide step whose whole point is a spell on
-- something that is not an npc could only be hand-driven another way:
-- Spirits of the Elid's `telegrabKey` ("You should Telekinetic Grab the
-- ancestral key from the table", elid_house.rs2:117) was a click_obj Take,
-- and the sampler reverted the quest for it (sonnet-b27).  The same verb now
-- takes the `{kind=, id=}` world target use_on takes:
--
--   t.player.cast("telegrab", { kind = "obj", id = "elid_key" })
--   t.player.cast("charge_water_orb", { kind = "loc", id = "obelisk_water" })
--
-- `id` is the content symbol (resolved through player.by_symbol, so a loc
-- gets its live-id rule) or the numeric id a t.world.obj_near/loc_near/
-- player.by_symbol table already carries.
--
-- THE PRESS is the npc cast's, for the other two target kinds the client's
-- target mode knows: the magic tab, the spell's own "Cast" row
-- (api_drive.spell_arm, TGT_BUTTON), then the obj's or loc's collapsed
-- "<Cast> <spell> -> <name>" row -- add_world_select_row with mask bit 0x1
-- (obj, REVCONFIG_MINIMENU_TGT_OBJ) or 0x4 (loc, TGT_LOC)
-- (src/game/rs_minimenu_world.c) -- through click_minimenu's "select"
-- wildcard, re-armed before every re-press, and the pressed row checked to
-- be the held one.  A spell whose target mask excludes the kind gets NO row
-- from the client ("mode active but this kind is not a valid target"), so
-- that press answers click_minimenu's own no-row word and nothing is sent.
-- The client sends OPOBJT / OPLOCT (app_minimenu.c); the server runs
-- `[opobjt,magic_spellbook:<spell>]` / `[oploct,...]` or their ap twins.
--
-- NO WALK BEFORE THE PRESS.  The point of a spell on a ground obj is that
-- the player does not walk onto it (the ancestral key sits on a table), so
-- the cast is pressed from where the caller stands -- the server paths into
-- spell range itself for the ap trigger.  Only a press that answered
-- `covered` walks, once, to QD.player._click_obj_standoff tiles (obj) or the
-- loc standoff (loc), exactly as the npc cast's press loop does.
--
-- WHAT `ok` MEANS -- the server's EFFECT, never the press:
--   obj  the obj ARRIVED in the backpack (Telekinetic Grab's
--        obj_takeitem(inv), telegrab.rs2) -- the stack's count in the
--        backpack rose.  The detail names the Magic XP delta and the
--        backpack diff (the runes that left) beside it.
--   loc  Magic XP was paid (every loc spell in the pack pays ~give_spell_xp
--        on the cast: charge_orb.rs2, farming_spells.rs2), plus the
--        backpack diff and the chat lines the cast printed, which are the
--        loc's effect as far as the client can see it.
-- `refused` / `no_runes` = the SERVER declined it in words
-- (QD.player._spell_refusal_since: SPELL_REFUSAL_LINES, the rune pattern,
-- the engine's click refusals such as "Nothing interesting happens." for a
-- spell with no trigger on that target).  A chat line the cast printed
-- that is none of those, with nothing paid and nothing arriving, is also
-- `refused` naming the line: the server answered and did not cast.
-- `timeout` = none of the above inside `ticks` (default 10).
--
-- No combat stamp: nothing here is a fight for npc.await_dead_engaged.
-- ---------------------------------------------------------------------------

-- Ticks a cast on an obj/loc keeps watching after its effect first shows:
-- telegrab pays its XP and takes the obj on the same tick, then
-- p_delay(1)s; charge orb pays and swaps, then p_delay(2)s.  Two ticks
-- lets the XP (or the swap) that trails the first edge reach the client.
QD.player.SPELL_WORLD_TRAIL_TICKS = 2

-- The chat lines newer than `since`, oldest first, trimmed.
function QD.player._spell_lines_since(since)
    local lines = {}
    if type(since) ~= "number" then
        return lines
    end
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return lines
    end
    for i = #rows, 1, -1 do
        if rows[i].serial > since and type(rows[i].text) == "string" then
            local trimmed = string.match(rows[i].text, "^%s*(.-)%s*$") or rows[i].text
            if trimmed ~= "" then
                lines[#lines + 1] = trimmed
            end
        end
    end
    return lines
end

-- The world target a cast names, resolved: (target) or (nil, result, detail).
function QD.player._cast_world_target(spell_symbol, target)
    local kind = target.kind
    if type(target.id) == "string" then
        local resolved, sym_result = QD.player.by_symbol(kind, target.id)
        if not resolved then
            return nil, sym_result, "cast " .. spell_symbol .. ": no " .. kind .. " symbol "
                .. target.id .. " (" .. tostring(sym_result) .. ")"
        end
        return resolved
    end
    if type(target.id) ~= "number" then
        return nil, "unsupported", "cast " .. spell_symbol .. ": " .. kind
            .. " target carries no id (a symbol string or a numeric id)"
    end
    if target.symbol == nil then
        local name_result, name = api_drive.symbol_name(kind, target.id)
        if name_result == "ok" then
            target.symbol = name
        end
    end
    return target
end

-- The one walk a covered cast press takes.  A loc: walk_near at the loc
-- standoff (click_loc's).  A ground obj, which walk_near does not take:
-- click_obj's rule -- within QD.player._click_obj_standoff and no closer --
-- plus one step OFF the stack when the player stands on it, since a
-- square at the player's feet is under his own model.
function QD.player._cast_approach(world, tile_x, tile_z)
    if world.kind == "loc" then
        return QD.player.walk_near(world, nil, QD.player._standoff_for_kind("loc") or 1)
    end
    local here_result, here = api_drive.player_tile()
    if here_result ~= "ok" or type(here) ~= "table" then
        return here_result, "player_tile"
    end
    local distance = QD.player._tile_distance(here.x, here.z, tile_x, tile_z)
    if distance < 1 then
        return QD.player._step_off_tile(world, tile_x, tile_z, 1)
    end
    if distance <= QD.player._click_obj_standoff then
        return "ok", "stack " .. tostring(distance) .. " tile(s) off already; no walk"
    end
    local step_x = QD.player._step_off(tile_x, here.x, QD.player._click_obj_standoff)
    local step_z = QD.player._step_off(tile_z, here.z, QD.player._click_obj_standoff)
    local walk_result, walk_detail = QD.player.walk_to(step_x, step_z)
    return walk_result, "walk_to " .. tostring(step_x) .. "," .. tostring(step_z)
        .. (walk_detail and (" " .. tostring(walk_detail)) or "")
end

-- True when a covered press's detail lists a menu of Cancel alone
-- (_press_row's "menu rows: <text|kind|id|act>..." list).
function QD.player._cast_menu_only_cancel(click)
    local text = tostring(click)
    local rows = string.match(text, "menu rows: (.*)$")
    if rows == nil then
        return false
    end
    local count = 0
    for _ in string.gmatch(rows, "<[^>]*>") do
        count = count + 1
    end
    return count == 1 and string.find(rows, "<Cancel|", 1, true) ~= nil
end

-- The obj's count in the backpack, or nil when it could not be read.
function QD.player._cast_obj_held(obj_id)
    local container_result, container_id = QD._inv_container()
    if container_result ~= "ok" then
        return nil
    end
    local count_result, total = api_drive.inv_count(container_id, obj_id)
    if count_result ~= "ok" then
        return nil
    end
    return total
end

-- t.player.cast(spell, {kind="obj"|"loc", id=...}, ticks) -- the banner
-- above.  Called by QD.player.cast for a table target of those two kinds.
function QD.player._cast_on_world(spell, target, ticks)
    ticks = ticks or 10
    local symbol = QD.player._spell_symbol(spell)
    local kind = target.kind
    local named = tostring(target.symbol or target.id)
    local label = "cast " .. symbol .. " on " .. kind .. " " .. named

    if QD.player._death_fence("t.player.cast " .. symbol .. " " .. kind .. " " .. named) then
        return "refused", QD.player._death_text(QD._death)
    end
    if api_drive.spell_arm == nil then
        return "unsupported", label .. ": this binary predates api_drive.spell_arm"
            .. " (torirs_plugin_drive_pointer.c) -- rebuild it; no cast was attempted"
    end
    local world, resolve_result, resolve_detail = QD.player._cast_world_target(symbol, target)
    if not world then
        return resolve_result, resolve_detail
    end
    named = tostring(world.symbol or world.id)
    label = "cast " .. symbol .. " on " .. kind .. " " .. named
    if kind == "loc" and world.symbol then
        local note = QD.player._loc_match_note(world.symbol, world.id, world.match)
        if note then
            QD.note("player.cast " .. note)
        end
    end
    -- Where the target stands, for the detail and the covered walk: an obj
    -- or loc absent from the pool is `not_found` before anything is armed.
    local tile_result, tile_x, tile_z = QD.drive._target_tile(world)
    if tile_result ~= "ok" then
        return tile_result, label .. ": no " .. kind .. " " .. named .. " in the scene ("
            .. tostring(tile_result) .. ") -- nothing was cast"
    end
    local here_result, here = api_drive.player_tile()
    local where = kind .. " at " .. tostring(tile_x) .. "," .. tostring(tile_z)
    if here_result == "ok" and type(here) == "table" then
        where = where .. ", cast from " .. tostring(here.x) .. "," .. tostring(here.z)
            .. " (" .. tostring(QD.player._tile_distance(here.x, here.z, tile_x, tile_z))
            .. " tile(s))"
    end

    local held_before = nil
    if kind == "obj" then
        held_before = QD.player._cast_obj_held(world.id)
        if held_before == nil then
            return "unsupported", label .. ": the backpack count of " .. named
                .. " could not be read, so the grab could not be graded -- nothing was cast"
        end
    end
    local inv_result, inv_before = QD.player._inv_contents()
    if inv_result ~= "ok" then
        inv_before = nil
    end

    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        QD.player._show_backpack()
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
        QD.player._show_backpack()
        return arm_result, arm_detail
    end

    -- The press: up to three, a tick apart, re-armed before each re-press
    -- (a covered press leaves a menu open and dismissing it clears the
    -- arming); one walk toward a target that answered covered.
    local click_result, click
    local presses = 0
    local walked = nil
    local only_cancel = 0
    while true do
        presses = presses + 1
        click_result, click = QD.drive.click_minimenu(world, "select", nil, arm)
        if click_result == "covered" and QD.player._cast_menu_only_cancel(click) then
            only_cancel = only_cancel + 1
        end
        if click_result == "covered" and walked == nil then
            local walk_result, walk_detail = QD.player._cast_approach(world, tile_x, tile_z)
            walked = tostring(walk_result) .. " " .. tostring(walk_detail)
            QD.note("player.cast: the " .. kind .. " press answered covered; walked -> "
                .. walked)
        end
        if click_result == "ok" or presses >= 3 then
            break
        end
        local next_tick = api_drive.tick() + 1
        QD.await({
            level = function() return api_drive.tick() >= next_tick end,
            note = "player.cast re-press",
        }, 3)
        local rearm_result, rearm_detail = arm()
        if rearm_result ~= "ok" then
            QD.player._show_backpack()
            return rearm_result, rearm_detail
        end
    end
    if click_result ~= "ok" then
        QD.player._show_backpack()
        local hint = ""
        if only_cancel == presses then
            -- With a spell armed the client offers ONLY rows whose kind the
            -- spell's target mask accepts (add_world_select_row), so a menu
            -- of Cancel alone on every press is what a mask without this
            -- kind's bit looks like -- or a pixel on something else the
            -- spell does not target.  Said, not decided: the mask is not a
            -- driver read.
            hint = " [every press's menu held only Cancel: nothing under the pixel takes "
                .. symbol .. " -- the spell's target mask may exclude a " .. kind
                .. " (Wind Strike's is npc/player)]"
        end
        return click_result, label .. ": " .. tostring(click) .. " (" .. tostring(presses)
            .. " press(es)" .. (walked and ("; walked after a covered press: " .. walked) or "")
            .. ") -- " .. where .. hint
    end
    local held_ok, held_why = QD.player._select_row_is_held(world, click)
    if not held_ok then
        QD.player._show_backpack()
        return "refused", label .. ": " .. tostring(held_why) .. " -- " .. where
    end
    local row_text = type(click) == "table" and tostring(click.row_text) or ""

    -- The settle: the server's refusal, or the effect (banner above).
    local refusal_word, refusal_line = nil, nil
    local effect_tick = nil
    local answered_tick = nil
    local settle_result = QD.await({
        level = function()
            refusal_word, refusal_line = QD.player._spell_refusal_since(since)
            if refusal_word then
                return true
            end
            local now = api_drive.tick()
            if effect_tick == nil then
                if kind == "obj" then
                    local held = QD.player._cast_obj_held(world.id)
                    if held ~= nil and held > held_before then
                        effect_tick = now
                    end
                else
                    local xp_now = QD.player._magic_xp()
                    if xp_now ~= nil and xp_before ~= nil and xp_now > xp_before then
                        effect_tick = now
                    end
                end
            end
            if effect_tick ~= nil then
                return now >= effect_tick + QD.player.SPELL_WORLD_TRAIL_TICKS
            end
            -- A LOC cast's line that is no refusal and no effect: give the
            -- effect the trail window to follow it (a script can mes before
            -- it pays), then stop -- the caller reads it as the server's
            -- answer.  An OBJ cast waits for the obj or a refusal only: a
            -- grab is silent on success, and an unrelated line landing
            -- while the server walks into spell range must not cut the
            -- wait short.
            if kind == "loc" and answered_tick == nil
                and #QD.player._spell_lines_since(since) > 0 then
                answered_tick = now
            end
            return answered_tick ~= nil
                and now >= answered_tick + QD.player.SPELL_WORLD_TRAIL_TICKS + 1
        end,
        note = "player.cast " .. symbol .. " " .. kind .. " " .. named,
    }, ticks)

    local xp_after = QD.player._magic_xp()
    local held_after = kind == "obj" and QD.player._cast_obj_held(world.id) or nil
    local diff = ""
    if inv_before ~= nil then
        local after_result, inv_after = QD.player._inv_contents()
        if after_result == "ok" then
            diff = QD.player._inv_contents_diff(inv_before, inv_after)
        end
    end
    local lines = QD.player._spell_lines_since(since)
    QD.player._show_backpack()

    local detail = label .. " [" .. row_text .. "] (" .. tostring(arm_detail) .. ") in "
        .. tostring(presses) .. " press(es), " .. where
        .. (walked and ("; walked after a covered press: " .. walked) or "")
        .. ": magic xp " .. tostring(xp_before) .. " -> " .. tostring(xp_after)
    if kind == "obj" then
        detail = detail .. ", " .. named .. " held " .. tostring(held_before) .. " -> "
            .. tostring(held_after)
    end
    detail = detail .. ", backpack " .. (diff ~= "" and diff or "unchanged")
    if #lines > 0 then
        detail = detail .. ", chat '" .. table.concat(lines, "' '") .. "'"
    end

    if QD.player._death_fence("t.player.cast " .. symbol .. ", after the settle") then
        return "refused", QD.player._death_text(QD._death)
    end
    if not refusal_word then
        refusal_word, refusal_line = QD.player._spell_refusal_since(since)
    end
    if refusal_word then
        return refusal_word, detail .. " -- the SERVER refused the cast: '"
            .. tostring(refusal_line) .. "'"
    end
    local xp_paid = xp_before ~= nil and xp_after ~= nil and xp_after > xp_before
    if kind == "obj" and held_after ~= nil and held_after > held_before then
        return "ok", detail .. " -- GRABBED (" .. named .. " arrived in the backpack"
            .. (xp_paid and "; Magic XP paid" or "; no Magic XP seen") .. ")"
    end
    if kind == "loc" and xp_paid then
        return "ok", detail .. " -- CAST (Magic XP paid)"
    end
    if #lines > 0 then
        return "refused", detail .. " -- the server answered '" .. lines[#lines]
            .. "' and " .. (kind == "obj" and ("no " .. named .. " arrived")
            or "paid no Magic XP") .. ": it did not cast"
    end
    if kind == "obj" and xp_paid then
        return "timeout", detail .. " -- Magic XP paid but " .. named
            .. " never arrived in the backpack inside " .. tostring(ticks) .. " ticks"
    end
    return "timeout", detail .. " -- " .. (settle_result == "ok" and "" or "no refusal, ")
        .. "no effect inside " .. tostring(ticks) .. " ticks: the cast never ran"
end

-- ---------------------------------------------- a cast on a CARRIED item
--
-- SEAM recruitmentdrive_spawn_artifact_and_held_cast (seam27, 2026-09-28).
-- No verb could cast a spell on an item in the backpack, so a guide step
-- that is exactly that -- Family Crest's "Superheat the perfect gold ore"
-- (seam26 ported LostCity's perfect_gold_ore -> perfect_gold_bar row into
-- superheat.rs2), every alchemy and enchant -- could only be skipped or
-- hand-driven another way.  The verb now takes a held target:
--
--   t.player.cast("superheat", { kind = "held", id = "perfect_gold_ore" })
--   t.player.cast("low_alchemy", { kind = "held", id = "bronze_dagger" })
--
-- `spell` is the spellbook component's name (magic_spellbook:superheat,
-- magic_spellbook:low_alchemy -- NOT the display name "Superheat Item");
-- `id` is the obj symbol of a stack in the backpack.
--
-- THE PRESS is the two clicks a player makes.  (1) The magic tab and the
-- spell's own "Cast" row (api_drive.spell_arm, TGT_BUTTON).  The spellbook's
-- target-enter clientscript then hands the sidebar to the backpack for any
-- spell with the held-item target bit (alchemy.rs2's banner: script2617 ->
-- ~script916), so the verb WAITS for inventory:items to be displayed rather
-- than pressing the tab.  (2) The cell's "<spell> -> <item>" row
-- (rs_minimenu_build.c add_inv_slot_select_row, REVCONFIG_MINIMENU_TGT_HELD),
-- run through the real dispatcher by api_drive.inv_cast
-- (torirs_plugin_drive_pointer.c drive_pointer_inv_cast), which refuses and
-- sends nothing wherever the real cell would offer no such row (no spell
-- armed, a target mask without the held bit, the cell not displayed).  The
-- client sends OPHELDT and the server runs `[opheldt,magic_spellbook:<spell>]`.
--
-- WHAT `ok` MEANS -- the server's effect: Magic XP was paid AND the item's
-- stack in the backpack went down (superheat deletes the ore, an alchemy the
-- item; ~give_spell_xp runs in the same script).  The detail names the Magic
-- XP delta, the item's count before and after, the whole backpack diff (the
-- bar or the coins that arrived, the runes that left) and the chat lines.
-- `refused` / `no_runes` = the server declined it in words
-- (QD.player._spell_refusal_since), or printed any other line and paid
-- nothing ("You need to cast superheat item on ore.", a smelting level
-- failure, an alchemy refusal).  `timeout` = nothing inside `ticks`
-- (default 10).  No combat stamp.
-- ---------------------------------------------------------------------------

-- Ticks the verb waits for the backpack to take the sidebar after the arming
-- (the client's own target-enter hook does it; one or two frames in practice).
QD.player.SPELL_HELD_TAB_TICKS = 6

-- Ticks a held cast keeps watching after its first effect: superheat pays,
-- swaps and p_delay(1)s, an alchemy p_delay(3)s after its swap, so the XP or
-- the swap that trails the first edge reaches the client inside this.
QD.player.SPELL_HELD_TRAIL_TICKS = 2

-- t.player.cast(spell, {kind="held", id=<obj symbol>}, ticks) -- the banner
-- above.  Called by QD.player.cast for a `held` table target.
function QD.player._cast_on_held(spell, target, ticks)
    ticks = ticks or 10
    local symbol = QD.player._spell_symbol(spell)
    local item = target.id
    local label = "cast " .. symbol .. " on held " .. tostring(item)
    if type(item) ~= "string" then
        return "unsupported", label .. ": a held target names its obj SYMBOL"
            .. " ({kind='held', id='gold_ore'})"
    end
    if QD.player._death_fence("t.player.cast " .. symbol .. " held " .. item) then
        return "refused", QD.player._death_text(QD._death)
    end
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, label .. ": " .. tostring(cell) .. " -- nothing was cast"
    end
    local held_before = cell.count
    local counted = QD.player._cast_obj_held(cell.obj_id)
    if counted ~= nil then
        held_before = counted
    end
    local inv_result, inv_before = QD.player._inv_contents()
    if inv_result ~= "ok" then
        inv_before = nil
    end

    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        QD.player._show_backpack()
        return component_result, component_id
    end
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" or type(since) ~= "number" then
        since = nil
    end
    local xp_before = QD.player._magic_xp()

    local arm_result, arm_detail = api_drive.spell_arm(component_id)
    if arm_result ~= "ok" then
        QD.player._show_backpack()
        return arm_result, label .. ": arming the spell -- " .. tostring(arm_detail)
    end

    -- The sidebar: the client's target-enter hook shows the backpack.  Asked,
    -- not pressed -- a tab press is a click, and the one left over after an
    -- arming is the cast's own.
    local items_result, items_id = api_drive.component("inventory:items", -1)
    if items_result ~= "ok" then
        return items_result, label .. ": no inventory:items component"
    end
    local tab_shown = QD.await({
        level = function()
            local result, presented = api_drive.widget_presented(items_id)
            return result == "ok" and presented == true
        end,
        note = "player.cast " .. symbol .. ": the backpack takes the sidebar",
    }, QD.player.SPELL_HELD_TAB_TICKS)
    -- Not shown is NOT answered here: the press below still goes to
    -- api_drive.inv_cast, whose refusal is the authoritative one (a spell
    -- with no held-item bit -- the client never hands it the backpack -- is
    -- refused on its mask) and which CLEARS the arming on every refusal, so
    -- no live spell is left for the next verb's click to spend.
    local tab_note = tab_shown == "ok" and "" or ("; the backpack never took the sidebar inside "
        .. tostring(QD.player.SPELL_HELD_TAB_TICKS) .. " ticks")
    -- The cell again, now that it is the displayed one: a stack can move
    -- between reads (nothing here moves it, but the press names the cell).
    cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, label .. ": " .. tostring(cell) .. " -- nothing was cast"
    end
    local cast_result, cast_detail = api_drive.inv_cast(cell.component_id, cell.slot,
        cell.obj_id, cell.count)
    if cast_result ~= "ok" then
        QD.player._show_backpack()
        return cast_result, label .. " slot " .. tostring(cell.slot) .. ": the cast row -- "
            .. tostring(cast_detail) .. " (armed: " .. tostring(arm_detail) .. tab_note .. ")"
    end

    local refusal_word, refusal_line = nil, nil
    local effect_tick = nil
    local answered_tick = nil
    local settle_result = QD.await({
        level = function()
            refusal_word, refusal_line = QD.player._spell_refusal_since(since)
            if refusal_word then
                return true
            end
            local now = api_drive.tick()
            if effect_tick == nil then
                local xp_now = QD.player._magic_xp()
                local held_now = QD.player._cast_obj_held(cell.obj_id)
                if (xp_now ~= nil and xp_before ~= nil and xp_now > xp_before)
                    or (held_now ~= nil and held_now < held_before) then
                    effect_tick = now
                end
            end
            if effect_tick ~= nil then
                return now >= effect_tick + QD.player.SPELL_HELD_TRAIL_TICKS
            end
            -- A line that is no refusal and no effect: the trail window for
            -- an effect to follow it, then the caller reads it as the answer.
            if answered_tick == nil and #QD.player._spell_lines_since(since) > 0 then
                answered_tick = now
            end
            return answered_tick ~= nil
                and now >= answered_tick + QD.player.SPELL_HELD_TRAIL_TICKS + 1
        end,
        note = "player.cast " .. symbol .. " held " .. item,
    }, ticks)

    local xp_after = QD.player._magic_xp()
    local held_after = QD.player._cast_obj_held(cell.obj_id)
    local diff = ""
    if inv_before ~= nil then
        local after_result, inv_after = QD.player._inv_contents()
        if after_result == "ok" then
            diff = QD.player._inv_contents_diff(inv_before, inv_after)
        end
    end
    local lines = QD.player._spell_lines_since(since)
    QD.player._show_backpack()

    local detail = label .. " (" .. tostring(arm_detail) .. "; OPHELDT on slot "
        .. tostring(cell.slot) .. "): magic xp " .. tostring(xp_before) .. " -> "
        .. tostring(xp_after) .. ", " .. item .. " held " .. tostring(held_before) .. " -> "
        .. tostring(held_after) .. ", backpack " .. (diff ~= "" and diff or "unchanged")
    if #lines > 0 then
        detail = detail .. ", chat '" .. table.concat(lines, "' '") .. "'"
    end

    if QD.player._death_fence("t.player.cast " .. symbol .. ", after the settle") then
        return "refused", QD.player._death_text(QD._death)
    end
    if not refusal_word then
        refusal_word, refusal_line = QD.player._spell_refusal_since(since)
    end
    if refusal_word then
        return refusal_word, detail .. " -- the SERVER refused the cast: '"
            .. tostring(refusal_line) .. "'"
    end
    local xp_paid = xp_before ~= nil and xp_after ~= nil and xp_after > xp_before
    local spent = held_after ~= nil and held_after < held_before
    if xp_paid and spent then
        return "ok", detail .. " -- CAST (Magic XP paid; " .. item .. " consumed)"
    end
    if #lines > 0 and not xp_paid then
        return "refused", detail .. " -- the server answered '" .. lines[#lines]
            .. "' and paid no Magic XP: it did not cast"
    end
    if xp_paid then
        return "timeout", detail .. " -- Magic XP paid but " .. item
            .. " never left the backpack inside " .. tostring(ticks) .. " ticks"
    end
    return "timeout", detail .. " -- " .. (settle_result == "ok" and "" or "no refusal, ")
        .. "no effect inside " .. tostring(ticks) .. " ticks: the cast never ran"
end

-- ------------------------------------------------------------ a SELF-CAST
--
-- SEAM self_cast_and_moving_multinpc_press (seam28, 2026-09-29).  Plague
-- City's reward is the Ardougne Teleport gate (skill_magic/scripts/spells/
-- teleport.rs2:16-23, from LostCity teleport.rs2): before %elenaquest reaches
-- ^elena_complete the spell answers "You must have completed Plague City to
-- use this spell.", after it and before the scroll is read "You havn't learnt
-- how to cast this spell yet.", and only then ~magic_teleport.  Every later
-- tier has teleport-spell steps.  No verb could cast a spell with NO target:
-- `t.player.cast("varrock_teleport")` died inside by_symbol("npc", nil)
-- (build/quest_gate/s28sc_before row 2, `bad argument #2 to 'symbol'`).
--
-- WHAT IS PRESSED.  A teleport is not a target spell.  Its spellbook cell
-- offers a plain "Cast" op, and a left click on it sends IF_BUTTON1, which
-- the server runs as `[if_button,magic_spellbook:<spell>]` -- teleport.rs2's
-- triggers.  So the press is the cell's own op 1 through api_drive.if_click
-- -> app_plugin_click_node, the dispatcher a real click reaches (the emote
-- verb's press, ui.lua), never api_drive.spell_arm (which arms target mode
-- and would leave a live selection behind for the next click).  The magic
-- tab is opened and the cell awaited DISPLAYED first (_spell_component).
--
-- WHAT IT SETTLES ON.  A teleport pays its runes and XP on the press tick
-- (~delete_spell_runes, ~give_spell_xp) and lands two ticks later
-- (~player_teleport_normal: anim, p_delay(1), p_delay(0), p_telejump).  Every
-- refusal on the way is a mes() and moves nothing: the quest gates above,
-- magic.rs2's level and rune lines, ~magic_teleport_gate's wilderness line,
-- the Gauntlet and Mage Training Arena blocks.  So:
--   ok       = the player LEFT the tile: more than SELF_CAST_MOVED tiles, or
--              another level.  The detail names the tile reached.  Also ok:
--              Magic XP paid with no move (a self-cast that is not a
--              teleport -- Charge, Bones to Bananas), and it says "no
--              teleport" so a teleport test cannot mistake it for one.
--   no_runes = magic.rs2's rune sentence.
--   refused  = any other line, with no move and no XP, left standing
--              SELF_CAST_LINE_GRACE ticks -- the server's sentence quoted.
--   timeout  = nothing inside `ticks` (default 10).  A TARGET spell (Wind
--              Strike) pressed here lands in this word: its cell's left click
--              arms target mode in the real client, and the server has no
--              [if_button] for it -- give it a target.
-- The backpack is put back afterwards (the emote verb's rule).  No combat
-- stamp.
--
-- A PRESS WHILE THE PLAYER IS DELAYED IS DROPPED, SILENTLY (seam33
-- spellbook_cast_never_runs).  The server refuses an interface click that
-- would start a script while another script of the player's sits in its
-- p_delay -- LostCity's IfButtonHandler runs [if_button] with protected
-- access and Player.runScript returns -1 while `delayed`
-- (torirs_server_world.c if_button_refused_while_delayed, verbose line
-- `IF_BUTTONN ... refused: player is delayed (p_delay)`).  Nothing reaches
-- the screen.  Lost City's teleportAway was pressed on the tick the Dramen
-- tree's chop (leprechaun_tree.rs2 [oploc1,dramentree]: inv_add, p_delay(1))
-- still held the player, and read "the cast never ran" (build/quest_gate/
-- zanaris row 35; reproduced build/quest_gate/s33cast_repro2 row 4).  Neither
-- p_finduid nor the dbrow: the script never started.  So, as a person clicks
-- again when nothing happened (the equip verb's rule, pointer.lua): a press
-- that leaves NO trace -- no line, no move, no Magic XP -- for
-- SELF_CAST_REPRESS_TICKS is pressed again, up to SELF_CAST_PRESSES times;
-- the last press gets the whole `ticks`.  Safe: a press the server ran pays
-- its XP (or prints its refusal) on the press tick, and the teleport's own
-- p_delays refuse a second press while it lands.  The detail says
-- "(press N)" when it took more than one.
-- ---------------------------------------------------------------------------

-- Presses of the spell's cell before the verb stops pressing, and the ticks of
-- silence (no line, no move, no XP) after a press before the next one.
QD.player.SELF_CAST_PRESSES = 3
QD.player.SELF_CAST_REPRESS_TICKS = 3

-- More than this many tiles (Chebyshev), or a level change, is a teleport
-- and not a step: nothing the press can start walks the player at all, and
-- the nearest teleport destination is dozens of tiles from any start.
QD.player.SELF_CAST_MOVED = 2

-- Ticks a line with no move and no XP is left standing before it is the
-- answer: the teleport's own p_delays are two ticks, so a line that a
-- landing follows (a content hook printing on cast) is not read as a refusal.
QD.player.SELF_CAST_LINE_GRACE = 3

-- Ticks after the Magic XP edge the verb still waits for the landing
-- (p_delay(1) + p_delay(0) + a tick of client latency, with one to spare).
QD.player.SELF_CAST_LAND_TICKS = 5

function QD.player._self_cast_tile()
    local result, tile = api_drive.player_tile()
    if result ~= "ok" or type(tile) ~= "table" then
        return nil
    end
    return tile
end

function QD.player._self_cast_tile_text(tile)
    if tile == nil then
        return "?"
    end
    return tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level)
end

function QD.player._self_cast_moved(from, to)
    if from == nil or to == nil then
        return false
    end
    if from.level ~= to.level then
        return true
    end
    local dx = math.abs(to.x - from.x)
    local dz = math.abs(to.z - from.z)
    return math.max(dx, dz) > QD.player.SELF_CAST_MOVED
end

-- t.player.cast(spell[, nil or {kind="self"}, ticks]) -- the banner above.
function QD.player._cast_self(spell, ticks)
    ticks = ticks or 10
    local symbol = QD.player._spell_symbol(spell)
    local label = "cast " .. symbol .. " (self, magic_spellbook:" .. symbol .. " op 1)"
    if QD.player._death_fence("t.player.cast " .. symbol .. " self") then
        return "refused", QD.player._death_text(QD._death)
    end
    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        QD.player._show_backpack()
        return component_result, component_id
    end
    local from = QD.player._self_cast_tile()
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" or type(since) ~= "number" then
        since = nil
    end
    local xp_before = QD.player._magic_xp()
    local start_tick = api_drive.tick()

    local moved_tick, xp_tick, line_tick = nil, nil, nil
    local refusal_word, refusal_line = nil, nil
    local settle_result = "timeout"
    local presses = 0
    local press_tick = start_tick
    while presses < QD.player.SELF_CAST_PRESSES do
        if presses > 0 then
            -- The cell again before a re-press: the tab is still the magic
            -- one (nothing ran to close it), but ask rather than assume.
            component_result, component_id = QD.player._spell_component(symbol)
            if component_result ~= "ok" then
                QD.player._show_backpack()
                return component_result, tostring(component_id) .. " (before press "
                    .. tostring(presses + 1) .. ")"
            end
        end
        local click_result, click_why = api_drive.if_click(component_id, 1)
        if click_result ~= "ok" then
            QD.player._show_backpack()
            return click_result, label .. ": if_click(" .. tostring(component_id) .. ", 1) -> "
                .. tostring(click_result) .. " " .. tostring(click_why) .. " (press "
                .. tostring(presses + 1) .. ") -- nothing was cast"
        end
        presses = presses + 1
        press_tick = api_drive.tick()
        local last_press = presses >= QD.player.SELF_CAST_PRESSES
        settle_result = QD.await({
            level = function()
                local now = api_drive.tick()
                if moved_tick == nil and QD.player._self_cast_moved(from, QD.player._self_cast_tile()) then
                    moved_tick = now
                end
                if xp_tick == nil and xp_before ~= nil then
                    local xp_now = QD.player._magic_xp()
                    if xp_now ~= nil and xp_now > xp_before then
                        xp_tick = now
                    end
                end
                if moved_tick ~= nil then
                    -- Landed.  One tick more when the XP has not been read yet,
                    -- so the detail carries it.
                    return xp_tick ~= nil or xp_before == nil or now >= moved_tick + 1
                end
                refusal_word, refusal_line = QD.player._spell_refusal_since(since)
                if refusal_word then
                    return true
                end
                if xp_tick ~= nil then
                    return now >= xp_tick + QD.player.SELF_CAST_LAND_TICKS
                end
                if line_tick == nil and #QD.player._spell_lines_since(since) > 0 then
                    line_tick = now
                end
                return line_tick ~= nil and now >= line_tick + QD.player.SELF_CAST_LINE_GRACE
            end,
            note = "player.cast " .. symbol .. " self (press " .. tostring(presses) .. ")",
        }, last_press and ticks or QD.player.SELF_CAST_REPRESS_TICKS)
        -- Any trace at all is the server's answer to a press that ran: stop.
        -- Only total silence is a dropped press.
        if settle_result == "ok" or moved_tick ~= nil or xp_tick ~= nil or line_tick ~= nil
            or refusal_word ~= nil or #QD.player._spell_lines_since(since) > 0 then
            break
        end
        local xp_now = QD.player._magic_xp()
        if xp_now ~= nil and xp_before ~= nil and xp_now > xp_before then
            break
        end
    end

    local to = QD.player._self_cast_tile()
    local xp_after = QD.player._magic_xp()
    local lines = QD.player._spell_lines_since(since)
    local back_result = QD.player._show_backpack()
    local detail = label .. ": at " .. QD.player._self_cast_tile_text(from) .. " -> "
        .. QD.player._self_cast_tile_text(to) .. ", magic xp " .. tostring(xp_before)
        .. " -> " .. tostring(xp_after)
    if moved_tick ~= nil then
        detail = detail .. ", landed " .. tostring(moved_tick - press_tick) .. " tick(s) after the press"
    end
    if presses > 1 then
        -- The earlier presses left no trace: the server refused them while a
        -- p_delay held the player (the banner's seam33 paragraph).
        detail = detail .. " (press " .. tostring(presses) .. ": " .. tostring(presses - 1)
            .. " earlier press(es) left no line, no move and no XP in "
            .. tostring(QD.player.SELF_CAST_REPRESS_TICKS) .. " ticks, first press "
            .. tostring(press_tick - start_tick) .. " tick(s) before the last)"
    end
    if #lines > 0 then
        detail = detail .. ", chat '" .. table.concat(lines, "' '") .. "'"
    end
    detail = detail .. " (sidebar back to inventory: " .. tostring(back_result) .. ")"

    if QD.player._death_fence("t.player.cast " .. symbol .. " self, after the settle") then
        return "refused", QD.player._death_text(QD._death)
    end
    local xp_paid = xp_before ~= nil and xp_after ~= nil and xp_after > xp_before
    if moved_tick ~= nil or QD.player._self_cast_moved(from, to) then
        return "ok", detail .. " -- TELEPORTED to " .. QD.player._self_cast_tile_text(to)
    end
    if not refusal_word then
        refusal_word, refusal_line = QD.player._spell_refusal_since(since)
    end
    if refusal_word then
        return refusal_word, detail .. " -- the SERVER refused the cast: '"
            .. tostring(refusal_line) .. "'"
    end
    if xp_paid then
        return "ok", detail .. " -- CAST (Magic XP paid) and no teleport: the player stayed within "
            .. tostring(QD.player.SELF_CAST_MOVED) .. " tile(s)"
    end
    if #lines > 0 then
        return "refused", detail .. " -- the server answered '" .. lines[#lines]
            .. "', paid no Magic XP and moved nothing: it did not cast"
    end
    return "timeout", detail .. " -- " .. (settle_result == "ok" and "" or "no line, ")
        .. "no move and no Magic XP inside " .. tostring(ticks) .. " ticks after "
        .. tostring(presses) .. " press(es): the cast never ran"
        .. " (a target spell's cell arms target mode instead -- give it a target)"
end

-- ------------------------------------------- a live selection, cancelled
--
-- SEAM spell_left_selected_after_a_fight (seam pass matthew-mbp-m4-b60-seam1).
-- A spell armed and never spent stays armed: the client drops app->targetsel
-- (and a held item's app->objsel) only at the doAction tail of a menu row or
-- a left click off anything targetable (app_minimenu_use_option,
-- app_frame.c's Cancel-only branches) -- never on a menu that closes because
-- the pointer left it, a teleport, an if_click, or a tick going by.  A
-- re-cast whose three presses all answer `covered` (the npc dying, or
-- re-added under another element) left Fire Blast armed after Family
-- Crest's Chronozon, and every later world press read `covered ... menu
-- rows: <Cancel>` (crest run 1 rows 138 and 147, build/orchestrator/fix_b60/
-- crest.progress.md; the kill row's tag "re-CAST fire_blast: 2 press(es) ok,
-- 1 not; last: covered").  The test had to spend a quest item use to retire
-- it.
--
-- t.player.cancel_selection(why) -> (ok, detail, was_armed) `refused`
-- `not_visible` `timeout`
--
--   t.exec("dropFireBlast", t.player.cancel_selection, "the kill's last re-cast")
--
-- `why` (text, optional) is only echoed at the head of the detail: it is what
-- lets the call go through t.exec, which grades a nil first argument `bad
-- verb/target`.  Called directly, it may be left out.
--
-- What a player does: right-click the world and pick Cancel.  The READING
-- comes first and is the client's own: rs_minimenu_world.c offers "Walk
-- here" on every world right-click EXCEPT while a use or target mode is
-- armed (the reference gates it on useMode==0 && targetMode==0), so a menu
-- with no Walk here row IS an armed selection.  The pixel is the player's
-- own, or 40px off it, the first one the client's world gate takes.  Armed:
-- Cancel is pressed (its doAction tail clears both modes), and the menu is
-- read again at the same pixel -- `ok` only when Walk here is back.  Nothing
-- armed: the menu is closed with Cancel and the answer is `ok` too, saying
-- so (`was_armed` false).  `refused` = still no Walk here after Cancel;
-- `not_visible` = no candidate pixel is on the world; `timeout` = the menu
-- never opened.  Nothing walks: Cancel is the only row ever pressed.
QD.player.SELECTION_MENU_TICKS = 3
QD.player.SELECTION_PIXEL_OFFSETS = { { 0, 0 }, { 0, 40 }, { 0, -40 }, { 40, 0 }, { -40, 0 } }

-- The menu rows' plain text as one line: "<Cancel> <Walk here>".
function QD.player._selection_rows_text(rows)
    local parts = {}
    for i = 1, #rows do
        parts[#parts + 1] = "<" .. QD.player._plain_line(tostring(rows[i].text or "")) .. ">"
    end
    return table.concat(parts, " ")
end

-- The first row whose plain text starts with `prefix`, or nil.
function QD.player._selection_row(rows, prefix)
    for i = 1, #rows do
        local plain = QD.player._plain_line(tostring(rows[i].text or ""))
        if string.sub(plain, 1, #prefix) == prefix then
            return rows[i]
        end
    end
    return nil
end

-- A world pixel to read the menu at: the player's own, or the first offset
-- the client's world gate takes.  Answers {x=, y=} or nil, why.
function QD.player._selection_pixel()
    local result, pos = api_drive.screen_position("player", -1)
    if result ~= "ok" or type(pos) ~= "table" then
        return nil, "the player has no screen position (" .. tostring(result) .. ")"
    end
    -- An open menu owns the whole canvas and the gate refuses every pixel
    -- under it (QD.drive._dismiss_menu's banner): close it first.
    QD.drive._dismiss_menu({ x = pos.x, y = pos.y })
    local refused = {}
    for i = 1, #QD.player.SELECTION_PIXEL_OFFSETS do
        local offset = QD.player.SELECTION_PIXEL_OFFSETS[i]
        local at = { x = pos.x + offset[1], y = pos.y + offset[2] }
        local on_world, why = QD.drive._world_gate(at.x, at.y)
        if on_world ~= false then
            return at
        end
        refused[#refused + 1] = at.x .. "," .. at.y .. " " .. tostring(why)
    end
    return nil, "no pixel around the player is on the world: " .. table.concat(refused, "; ")
end

-- Right-click `at` and read the menu that opens: (ok, rows) or
-- (timeout, why).
function QD.player._selection_menu(at)
    QD.drive._dismiss_menu(at)
    api_drive.mouse_move(at.x, at.y)
    QD.drive._pick_settled(at.x, at.y, 2)
    api_drive.mouse_button("right", 1, at.x, at.y)
    api_drive.mouse_button("right", 0, at.x, at.y)
    local open = QD.await({
        level = function()
            local r, visible = api_drive.menu_visible()
            return r == "ok" and visible
        end,
        note = "cancel_selection: menu at " .. at.x .. "," .. at.y,
    }, QD.player.SELECTION_MENU_TICKS)
    if open ~= "ok" then
        return "timeout", "the right-click at " .. at.x .. "," .. at.y .. " opened no menu"
    end
    local rows_result, rows = api_drive.menu_rows()
    if rows_result ~= "ok" or type(rows) ~= "table" then
        QD.drive._dismiss_menu(at)
        return "timeout", "the menu at " .. at.x .. "," .. at.y .. " gave no rows (" .. tostring(rows_result) .. ")"
    end
    return "ok", rows
end

-- Left-press one open-menu row and wait for the menu to close.
function QD.player._selection_press_row(row)
    api_drive.mouse_button("left", 1, row.centre_x, row.centre_y)
    api_drive.mouse_button("left", 0, row.centre_x, row.centre_y)
    return QD.await({
        level = function()
            local r, visible = api_drive.menu_visible()
            return r == "ok" and not visible
        end,
        note = "cancel_selection: menu closed",
    }, QD.player.SELECTION_MENU_TICKS)
end

function QD.player.cancel_selection(why)
    assert(why == nil or type(why) == "string", "cancel_selection(why): why must be text")
    local result, detail, was_armed = QD.player._cancel_selection()
    if why ~= nil then
        detail = "(" .. why .. ") " .. tostring(detail)
    end
    return result, detail, was_armed
end

function QD.player._cancel_selection()
    local at, why = QD.player._selection_pixel()
    if at == nil then
        return "not_visible", "cancel_selection: " .. tostring(why), nil
    end
    local read_result, rows = QD.player._selection_menu(at)
    if read_result ~= "ok" then
        return read_result, "cancel_selection: " .. tostring(rows), nil
    end
    local first_text = QD.player._selection_rows_text(rows)
    local cancel = QD.player._selection_row(rows, "Cancel")
    if cancel == nil then
        QD.drive._dismiss_menu(at)
        return "refused", "cancel_selection: the menu at " .. at.x .. "," .. at.y .. " has no Cancel row: "
            .. first_text, nil
    end
    if QD.player._selection_row(rows, "Walk here") ~= nil then
        QD.player._selection_press_row(cancel)
        return "ok", "cancel_selection: nothing was armed -- the menu at " .. at.x .. "," .. at.y
            .. " offered " .. first_text .. " (Walk here is offered only with no selection); closed it with Cancel",
            false
    end
    QD.player._selection_press_row(cancel)
    local again_result, again = QD.player._selection_menu(at)
    if again_result ~= "ok" then
        return again_result, "cancel_selection: a selection was armed (menu at " .. at.x .. "," .. at.y
            .. ": " .. first_text .. ", no Walk here); pressed Cancel; the re-read: " .. tostring(again), true
    end
    local again_text = QD.player._selection_rows_text(again)
    local again_cancel = QD.player._selection_row(again, "Cancel")
    local walk_back = QD.player._selection_row(again, "Walk here") ~= nil
    if again_cancel ~= nil then
        QD.player._selection_press_row(again_cancel)
    else
        QD.drive._dismiss_menu(at)
    end
    if not walk_back then
        return "refused", "cancel_selection: a selection was armed (menu at " .. at.x .. "," .. at.y .. ": "
            .. first_text .. ", no Walk here); pressed Cancel and the menu there STILL offers no Walk here: "
            .. again_text, true
    end
    return "ok", "cancel_selection: a selection WAS armed -- the menu at " .. at.x .. "," .. at.y .. " offered "
        .. first_text .. " and no Walk here; pressed Cancel (the doAction tail clears it); the menu there now"
        .. " offers " .. again_text, true
end

-- Arm `spell` and leave it armed -- the state a re-cast whose presses all
-- answered `covered` used to leave behind.  For the conformance rows that
-- prove cancel_selection and the fight's end clear it; no quest step arms a
-- spell without casting it.  Answers (ok, detail) with spell_arm's own word.
function QD.player._arm_spell(spell)
    local symbol = QD.player._spell_symbol(spell)
    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        return component_result, tostring(component_id)
    end
    local arm_result, arm_detail = api_drive.spell_arm(component_id)
    QD.player._show_backpack()
    return arm_result, "spell_arm " .. symbol .. " (component " .. tostring(component_id) .. ") -> "
        .. tostring(arm_result) .. " " .. tostring(arm_detail)
end

-- The press half of a cast with `element` as the copy to aim at and the
-- spell re-armed before every press: for the conformance row that proves a
-- missed press leaves nothing armed (an element no npc carries is `covered`
-- on every press).  Answers _cast_press's first two values.
function QD.player._cast_press_on_element(spell, npc_symbol, element)
    local symbol = QD.player._spell_symbol(spell)
    local target, target_result = QD.player.by_symbol("npc", npc_symbol)
    if not target then
        return target_result, "no target " .. tostring(npc_symbol)
    end
    local component_result, component_id = QD.player._spell_component(symbol)
    if component_result ~= "ok" then
        return component_result, tostring(component_id)
    end
    local arm = function()
        return api_drive.spell_arm(component_id)
    end
    local arm_result, arm_detail = arm()
    if arm_result ~= "ok" then
        QD.player._show_backpack()
        return arm_result, tostring(arm_detail)
    end
    local result, detail = QD.player._cast_press(target, "cast " .. symbol .. " on element " .. tostring(element),
        element, arm)
    QD.player._show_backpack()
    return result, detail
end
