-- quest-driver / prayer: activate or deactivate a prayer by the client's own
-- prayer-book button, and read the active set back from the server's varbits.
-- Raid seam 1 (docs/RAID_ORCHESTRATOR.md section 4, prayer.set / prayer.read).
-- The namespace QD.prayer is declared in core.lua; only core.lua declares
-- chunk-scope locals, so every helper here hangs off QD.prayer.
--
--   t.prayer.set(name, on)  -> ok | refused | no_row | not_found | not_visible | timeout
--   t.prayer.read()         -> ok, detail, set      (set[name] = true/false, all 29)
--   t.prayer.points()       -> ok, reading, detail  (t.skill.read("prayer")'s reading
--                              plus .points and .text; seam22 swapped the order)
--   t.prayer.set_on_tick(name, on, tick) / switch(list, opts) / flick(name, at_tick)
--                           -> result, detail, info -- presses on a named SERVER
--                              tick; the section at the end of this file
--
-- THE PRESS IS THE PLAYER'S.  `set` opens the prayer side tab through the
-- lane's own tab map (`prayer=5`, revconfig/osrs239/osrs239_dat2_cache.ini
-- [tabs]), waits for the button to be DISPLAYED (a press on a node that has
-- not painted yet is dropped with no packet while if_click still answers ok:
-- session.lua's logout banner measured exactly that), and presses
-- `prayerbook:prayerN` as op 1 through api_drive.if_click -- DriveUi_IfClick
-- -> app_send_if_button -> IF_BUTTONX, the dispatcher a real click reaches.
-- The server answers with `[if_button,prayerbook:prayerN]` ->
-- `~prayerbook_button` -> `[proc,prayer_toggle]`
-- (OSRS-Content/osrs239-content/server/scripts/skill_prayer/scripts/
-- prayer.rs2:365-532).  There is no C prayer mask: the varbits ARE the state
-- (`~prayer_set`, prayer.rs2), all on `varp83_prayer0` (transmit=yes), so the
-- verb settles on the varbit as the server sent it (QD._var_server_varbit).
--
-- WHICH BUTTON IS WHICH PRAYER is prayer.rs2's table, copied below, and it is
-- NOT the on-screen order: `prayerbook:prayerN` is numbered by component
-- (541:8+N), the cache's introduction order, so prayer20 is Hawk Eye and
-- prayer22 is Mystic Will (prayer.rs2:427-433).  Names are the content's own
-- spelling, the suffix of `^prayer_<name>` and of the varbit
-- (`protectfrommelee`, `varb4118_prayer_protectfrommelee`); a leading
-- `prayer_` is accepted and stripped.
--
-- A set that asks for the state the varbit already holds is `ok` with a
-- detail saying no press was made -- never a press, because a press on a lit
-- prayer turns it OFF.  A press the server refuses answers `refused` with the
-- server's own line (prayer.rs2 `prayer_checks`: the level, no points, the
-- protection block).  A press that moved nothing and printed nothing in
-- SET_TICKS server ticks is `timeout`, never re-pressed: a second press that
-- lands after a slow first one toggles the prayer straight back.
--
-- THE OVERHEAD.  `read` cannot read the overhead icon from the world: the
-- server keeps it as one engine int on the player (`headicons`,
-- src/torirsserver/torirs_server.h, written by `~headicon_add`), no varp or
-- varbit carries it, and the client's player snapshot exposes no headicon to
-- the plugin API.  So the detail says the overhead was NOT read and names the
-- icon the active varbits imply (prayers.constant `^headicon_prayer_*`), as an
-- implication, labelled so.
--
-- TICKS.  Every tick in a detail is the DRIVE tick (api_drive.tick(): the
-- client world's cycle / 30), the clock t.ticks counts -- not the embedded
-- server's own tick number, which no drive reader exposes (the tick log seam's
-- business).  A clean press reads back in the SAME drive tick (+0 on all six
-- rows that print it in build/quest_gate/pr_seam_b, and that run's 58-press
-- sweep of every button took 5 drive ticks): the embedded
-- server runs a client's packets as they arrive -- ToriRSServer_EmbedPump
-- pumps every client (pump_client -> ToriRSServer_SessionPump) on every
-- poll and only then, every 30th poll, runs ToriRSServer_WorldTick, whose
-- phase_clients_in is empty -- and the varbit is flushed with it.  So a press
-- made between server ticks T-1 and T is in force for tick T's phase_npcs,
-- which is the order OSRS gives (client input -> npcs -> players,
-- ENCOUNTER_TIMING.md 1.1): an npc scanning on tick T sees the prayer.
--
-- read() and points() take no target, so they cannot go through t.exec (its
-- nil-first-argument rule writes FAIL `bad verb/target`): record them with
-- t.expect / t.check, as with t.player.alive.  set() goes through t.exec.

-- {name, button, varbit, headicon or nil} in button order, prayer.rs2:467-532
-- (button) and configs/all.varbit (varbit, basevar=varp83_prayer0).  The
-- headicon column is skill_prayer/configs/prayers.constant:11-16.
QD.prayer.TABLE = {
    { "thickskin",           "prayerbook:prayer1",  "varb4104_prayer_thickskin" },
    { "burstofstrength",     "prayerbook:prayer2",  "varb4105_prayer_burstofstrength" },
    { "clarityofthought",    "prayerbook:prayer3",  "varb4106_prayer_clarityofthought" },
    { "rockskin",            "prayerbook:prayer4",  "varb4107_prayer_rockskin" },
    { "superhumanstrength",  "prayerbook:prayer5",  "varb4108_prayer_superhumanstrength" },
    { "improvedreflexes",    "prayerbook:prayer6",  "varb4109_prayer_improvedreflexes" },
    { "rapidrestore",        "prayerbook:prayer7",  "varb4110_prayer_rapidrestore" },
    { "rapidheal",           "prayerbook:prayer8",  "varb4111_prayer_rapidheal" },
    { "protectitem",         "prayerbook:prayer9",  "varb4112_prayer_protectitem" },
    { "steelskin",           "prayerbook:prayer10", "varb4113_prayer_steelskin" },
    { "ultimatestrength",    "prayerbook:prayer11", "varb4114_prayer_ultimatestrength" },
    { "incrediblereflexes",  "prayerbook:prayer12", "varb4115_prayer_incrediblereflexes" },
    { "protectfrommagic",    "prayerbook:prayer13", "varb4116_prayer_protectfrommagic", 2 },
    { "protectfrommissiles", "prayerbook:prayer14", "varb4117_prayer_protectfrommissiles", 1 },
    { "protectfrommelee",    "prayerbook:prayer15", "varb4118_prayer_protectfrommelee", 0 },
    { "retribution",         "prayerbook:prayer16", "varb4119_prayer_retribution", 3 },
    { "redemption",          "prayerbook:prayer17", "varb4120_prayer_redemption", 5 },
    { "smite",               "prayerbook:prayer18", "varb4121_prayer_smite", 4 },
    { "sharpeye",            "prayerbook:prayer19", "varb4122_prayer_sharpeye" },
    { "hawkeye",             "prayerbook:prayer20", "varb4124_prayer_hawkeye" },
    { "eagleeye",            "prayerbook:prayer21", "varb4126_prayer_eagleeye" },
    { "mysticwill",          "prayerbook:prayer22", "varb4123_prayer_mysticwill" },
    { "mysticlore",          "prayerbook:prayer23", "varb4125_prayer_mysticlore" },
    { "mysticmight",         "prayerbook:prayer24", "varb4127_prayer_mysticmight" },
    { "rigour",              "prayerbook:prayer25", "varb5464_prayer_rigour" },
    { "chivalry",            "prayerbook:prayer26", "varb4128_prayer_chivalry" },
    { "piety",               "prayerbook:prayer27", "varb4129_prayer_piety" },
    { "augury",              "prayerbook:prayer28", "varb5465_prayer_augury" },
    { "preserve",            "prayerbook:prayer29", "varb5466_prayer_preserve" },
}

-- THE EXCLUSION GROUPS (raid seam31 play_library_faults).  "Two prayers that
-- share a group cannot be up together: switching one on switches the other
-- off" (skill_prayer/configs/prayers.constant:69-70); each prayer's groups are
-- its `data=group` lines in skill_prayer/configs/prayers.dbrow, copied here
-- in the same order, and the server applies them in `[proc,prayer_toggle]` ->
-- `~prayer_deactivate_conflicting` (prayer.rs2:309-318) BEFORE it lights the
-- new one.  A press is a toggle, so a caller lighting X must never also send
-- an "off" for a prayer X shares a group with: the server has already put it
-- out, and the "off" press lights it again (raid seam30 ny30d: Protect from
-- Missiles held t67-362 while Protect from Magic was asked seven times).
-- The four with no group (rapidrestore, rapidheal, protectitem, preserve)
-- conflict with nothing.
QD.prayer.GROUPS = {
    thickskin = { "defence" }, rockskin = { "defence" }, steelskin = { "defence" },
    burstofstrength = { "strength" }, superhumanstrength = { "strength" }, ultimatestrength = { "strength" },
    clarityofthought = { "attack" }, improvedreflexes = { "attack" }, incrediblereflexes = { "attack" },
    sharpeye = { "ranged", "attack", "strength", "magic" },
    hawkeye = { "ranged", "attack", "strength", "magic" },
    eagleeye = { "ranged", "attack", "strength", "magic" },
    mysticwill = { "magic", "attack", "strength", "ranged" },
    mysticlore = { "magic", "attack", "strength", "ranged" },
    mysticmight = { "magic", "attack", "strength", "ranged" },
    protectfrommagic = { "overhead" }, protectfrommissiles = { "overhead" }, protectfrommelee = { "overhead" },
    retribution = { "overhead" }, redemption = { "overhead" }, smite = { "overhead" },
    chivalry = { "attack", "strength", "defence", "ranged", "magic" },
    piety = { "attack", "strength", "defence", "ranged", "magic" },
    rigour = { "attack", "strength", "defence", "ranged", "magic" },
    augury = { "attack", "strength", "defence", "ranged", "magic" },
    rapidrestore = {}, rapidheal = {}, protectitem = {}, preserve = {},
}

-- QD.prayer.conflicts(a, b) -> true when lighting `a` puts `b` out on the
-- server (they share an exclusion group, prayers.dbrow; a prayer never
-- conflicts with itself).  Both names in the content spelling of
-- QD.prayer.TABLE; a name with no row there is a caller's bug and raises.
function QD.prayer.conflicts(a, b)
    local ea, why_a = QD.prayer._entry(a)
    assert(ea, why_a)
    local eb, why_b = QD.prayer._entry(b)
    assert(eb, why_b)
    if ea[1] == eb[1] then
        return false
    end
    local ga = QD.prayer.GROUPS[ea[1]]
    local gb = QD.prayer.GROUPS[eb[1]]
    assert(ga, "prayer.conflicts: no group row for " .. ea[1])
    assert(gb, "prayer.conflicts: no group row for " .. eb[1])
    for i = 1, #ga do
        for j = 1, #gb do
            if ga[i] == gb[j] then
                return true
            end
        end
    end
    return false
end

-- The server's refusal sentences, prayer.rs2 `prayer_checks` (:94, :98, :103)
-- and the drain-out line (`[timer,prayer_drain]`).  Matched as plain
-- substrings against lines NEWER than the press.
QD.prayer.REFUSALS = {
    "You need a Prayer level of",
    "You have run out of Prayer points",
    "You can't use protection prayers",
}

-- Server ticks a press may take to come back as a varbit.  The IF_BUTTONX
-- packet is read at the start of the next tick and the varbit is sent the same
-- tick, so 1-2 is what a clean press costs; 5 leaves room for a busy client.
QD.prayer.SET_TICKS = 5
-- Ticks to wait for the button to be displayed after the tab press.
QD.prayer.PAINT_TICKS = 5

-- (entry) or (nil, detail) for a prayer name.
function QD.prayer._entry(name)
    if type(name) ~= "string" then
        return nil, "prayer: the name must be a string, got " .. type(name)
    end
    local key = string.gsub(name, "^prayer_", "")
    for i = 1, #QD.prayer.TABLE do
        if QD.prayer.TABLE[i][1] == key then
            return QD.prayer.TABLE[i]
        end
    end
    return nil, "prayer: no prayer called " .. name
        .. " (content spelling, e.g. protectfrommelee; see QD.prayer.TABLE)"
end

-- (result, value, source) for one entry's varbit as the server sent it.
function QD.prayer._varbit(entry)
    local symbol_result, varbit_id = api_drive.symbol("varbit", entry[3])
    if symbol_result ~= "ok" then
        return "no_row", "prayer: varbit " .. entry[3] .. " does not resolve (" .. tostring(symbol_result) .. ")"
    end
    return QD._var_server_varbit(varbit_id)
end

-- The newest line after `since` that is one of the server's refusals, or nil.
function QD.prayer._refusal_since(since)
    local result, list = api_drive.messages()
    if result ~= "ok" then
        return nil
    end
    for i = 1, #list do
        if list[i].serial > since then
            for j = 1, #QD.prayer.REFUSALS do
                if string.find(list[i].text, QD.prayer.REFUSALS[j], 1, true) then
                    return list[i].text
                end
            end
        end
    end
    return nil
end

-- Every line newer than `since`, joined, for a timeout's detail.
function QD.prayer._lines_since(since)
    local result, list = api_drive.messages()
    if result ~= "ok" then
        return "(chat ring unreadable: " .. tostring(result) .. ")"
    end
    local parts = {}
    for i = #list, 1, -1 do
        if list[i].serial > since then
            parts[#parts + 1] = list[i].text
        end
    end
    if #parts == 0 then
        return "no new chat line"
    end
    return "new chat: " .. table.concat(parts, " | ")
end

-- ("ok", component) or (result, detail): the prayer tab opened and `entry`'s
-- button DISPLAYED, ready for a press (file banner: a press on a node that has
-- not painted is dropped).  `who` prefixes the detail.  A button already
-- displayed answers on the spot, without a yield: the tick-exact verbs below
-- call this before they wait for their tick, and again right before the
-- press, so that second call must cost nothing.
function QD.prayer._prepare(entry, who)
    local resolved_result, resolved = api_drive.component(entry[2])
    if resolved_result == "ok" then
        local presented_result, presented = api_drive.widget_presented(resolved)
        if presented_result == "ok" and presented == true then
            return "ok", resolved
        end
    end
    local tab_result, tab_detail = QD.ui.tab("prayer")
    if tab_result ~= "ok" then
        return tab_result, who .. " " .. entry[1] .. ": the prayer tab did not open: " .. tostring(tab_detail)
    end
    QD.prayer._component = nil
    local shown = await({
        level = function()
            local result, id = api_drive.component(entry[2])
            if result ~= "ok" then
                return false
            end
            QD.prayer._component = id
            local presented_result, presented = api_drive.widget_presented(id)
            return presented_result == "ok" and presented == true
        end,
        note = who .. " " .. entry[2] .. " displayed",
    }, QD.prayer.PAINT_TICKS)
    local component = QD.prayer._component
    if shown ~= "ok" then
        if component == nil then
            return "not_found", who .. " " .. entry[1] .. ": " .. entry[2]
                .. " never resolved after the prayer tab opened"
        end
        return "not_visible", string.format(
            "%s %s: %s (component %d) was never displayed in %d tick(s) after the prayer tab opened",
            who, entry[1], entry[2], component, QD.prayer.PAINT_TICKS)
    end
    return "ok", component
end

-- t.prayer.set(name, on): `on` is true (light it) or false (put it out).
function QD.prayer.set(name, on)
    if on ~= true and on ~= false then
        error("prayer.set(" .. tostring(name) .. ", on): on must be true or false, got " .. tostring(on))
    end
    local entry, why = QD.prayer._entry(name)
    if not entry then
        return "no_row", why
    end
    local want = on and 1 or 0
    local word = on and "on" or "off"
    -- "Already so?" is asked of the SERVER's own value (waves seam pass 2):
    -- the client's record of varp83 can trail it by a few frames after a
    -- press (build/quest_gate/pf_d1: t.var.server read 1 right after
    -- set_on_tick saw the server's 0), and a press decided from a stale 1
    -- would put a lit prayer out.  The settle below still waits for the
    -- client's record, so a t.var.server read after set() agrees with it.
    local before_result, before, before_source = QD.prayer._server_varbit(entry)
    if before_result ~= "ok" then
        return before_result, "prayer.set " .. entry[1] .. ": the varbit before the press read "
            .. tostring(before_result) .. " " .. tostring(before)
    end
    -- SEAM-TOGETHER (raid seam27): inside t.together, press and do not wait
    -- (pointer.lua, the several_inputs_one_tick banner).
    if QD._together ~= nil then
        return QD._together_prayer(entry, want, word, before)
    end
    if before == want then
        return "ok", string.format(
            "%s already %s: %s = %d (%s) on drive tick %d -- no press made (a press would toggle it %s)",
            entry[1], word, entry[3], before, tostring(before_source), api_drive.tick(),
            on and "off" or "on")
    end

    local prepare_result, component = QD.prayer._prepare(entry, "prayer.set")
    if prepare_result ~= "ok" then
        return prepare_result, component
    end

    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, "prayer.set " .. entry[1] .. ": the chat serial is unreadable: " .. tostring(since)
    end
    local press_tick = api_drive.tick()
    local click_result, click_detail = api_drive.if_click(component, 1)
    if click_result ~= "ok" then
        return click_result, string.format("prayer.set %s: the press on %s (component %d) answered %s %s",
            entry[1], entry[2], component, tostring(click_result), tostring(click_detail))
    end

    QD.prayer._last = nil
    QD.prayer._last_source = nil
    QD.prayer._refused = nil
    local settled = await({
        level = function()
            local result, value, source = QD.prayer._varbit(entry)
            if result == "ok" then
                QD.prayer._last = value
                QD.prayer._last_source = source
                if value == want then
                    return true
                end
            end
            local refusal = QD.prayer._refusal_since(since)
            if refusal then
                QD.prayer._refused = refusal
                return true
            end
            return false
        end,
        note = "prayer.set " .. entry[3] .. " == " .. want,
    }, QD.prayer.SET_TICKS)
    local read_tick = api_drive.tick()
    local after = QD.prayer._last
    local where = string.format("pressed %s (component %d) on drive tick %d", entry[2], component, press_tick)
    if QD.prayer._refused and after ~= want then
        return "refused", string.format("%s %s refused: %s -- %s; %s = %s on drive tick %d",
            entry[1], word, QD.prayer._refused, where, entry[3], tostring(after), read_tick)
    end
    if settled ~= "ok" or after ~= want then
        return "timeout", string.format(
            "%s %s: %s; %s still %s after %d tick(s) (%s) -- not re-pressed",
            entry[1], word, where, entry[3], tostring(after), read_tick - press_tick,
            QD.prayer._lines_since(since))
    end
    return "ok", string.format("%s %s: %s; %s %d -> %d (%s) read on drive tick %d (+%d)",
        entry[1], word, where, entry[3], before, after, tostring(QD.prayer._last_source),
        read_tick, read_tick - press_tick)
end

-- t.prayer.read() -> "ok", detail, set
-- set[name] is a boolean for every one of the 29 prayers, read from the
-- server's varbits on one tick.  The detail names the lit set, the tick, and
-- the overhead the lit set implies -- implied, not read (file banner).
function QD.prayer.read()
    local set = {}
    local lit = {}
    local implied = {}
    local tick = api_drive.tick()
    for i = 1, #QD.prayer.TABLE do
        local entry = QD.prayer.TABLE[i]
        local result, value = QD.prayer._varbit(entry)
        if result ~= "ok" then
            return result, "prayer.read: " .. entry[3] .. " read " .. tostring(result) .. " " .. tostring(value)
        end
        set[entry[1]] = (value == 1)
        if value == 1 then
            lit[#lit + 1] = entry[1]
            if entry[4] ~= nil then
                implied[#implied + 1] = entry[1] .. "=headicon " .. tostring(entry[4])
            end
        end
    end
    local overhead = (#implied > 0) and table.concat(implied, ", ") or "none"
    return "ok", string.format(
        "on drive tick %d: %d of %d lit [%s]; overhead NOT read (headicons is an engine int, no var or "
            .. "client reader carries it) -- implied by the varbits: %s",
        tick, #lit, #QD.prayer.TABLE, table.concat(lit, ", "), overhead), set
end

-- t.prayer.points() -> "ok", reading, detail
-- The prayer stat, in t.skill.read("prayer")'s own shape: the SECOND value is
-- the reading (level = points left, base_level = the prayer level,
-- experience, stated), plus `points` (= level, the number a room test wants)
-- and `text`, the detail a ledger row can carry, which is also the third value.
--
-- Raid seam22 (party_death_and_member_readers): it used to answer
-- ("ok", detail, reading), so every room author who wrote it the way they
-- write `local _, pp = t.skill.read("prayer")` got the detail STRING in `pp`
-- and called it unusable (the Normal Sotetseg review; four of six Normal
-- authors fell back to t.skill.read). Same numbers, same read, now the order
-- every other reader has. Record it with t.check(name, r, reading.text).
function QD.prayer.points()
    local result, reading = QD.skill.read("prayer")
    if result ~= "ok" or type(reading) ~= "table" then
        local why = "prayer.points: skill.read(prayer) answered " .. tostring(result) .. " " .. tostring(reading)
        return result ~= "ok" and result or "refused", why, why
    end
    reading.points = reading.level
    reading.text = string.format("prayer points %d/%d (xp %d) on drive tick %d",
        reading.level, reading.base_level, reading.experience, api_drive.tick())
    return "ok", reading, reading.text
end

-- ------------------------------------------------------------------------
-- Presses on a named SERVER tick: set_on_tick, switch, flick
-- (waves seam pass 2, prayer_flick; docs/WAVES_ORCHESTRATOR.md section 5 row
-- "prayer flick", docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-03b.md)
-- ------------------------------------------------------------------------
--
--   t.prayer.set_on_tick(name, on, tick)  -> ok | timeout | refused | no_row
--                                            | not_found | not_visible
--                                            | unsupported, detail, info
--   t.prayer.switch(list, opts)           -> the same set, detail, info
--   t.prayer.flick(name, at_tick)         -> the same set, detail, info
--
-- THE CLOCK IS THE SERVER'S: srv->tick, the tick every t.ticklog row carries
-- (t.tick()), never api_drive.tick().  A press "on tick T" is a press issued
-- while t.tick() reads T, i.e. after tick T ran and before T+1 runs.
--
-- WHY THAT PRESS IS IN FORCE FOR T+1 AND NOT FOR T.  ToriRSServer_WorldTick
-- does `srv->tick++` first and then phase_clients_in (empty), phase_npc_events,
-- phase_npcs, phase_players (src/torirsserver/torirs_server_world.c, the
-- WorldTick body); a client's packets are handled BEFORE that, in
-- ToriRSServer_EmbedPump -> pump_client -> SessionPump -> step_online, and the
-- pump runs the tick only after every client's input (torirs_server_embed.c,
-- "Every client's input first, then *one* tick").  So the `[if_button]` ->
-- `~prayer_toggle` of a press issued on T has run, and the varbit is set,
-- before tick T+1's npc phase: an npc that rolls on T+1 reads the prayer
-- (`~check_protect_prayer`, skill_combat/combat_stats.rs2:163, read at the
-- roll: playerhit_n_melee_apply :924, inferno_hit_player inferno_ai.rs2:35),
-- and one that rolled on T did not see it.  That is the T-1 rule of
-- docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md section 1 written in our
-- server's numbers: to have a prayer up for an attack rolled on tick A, press
-- it on A-1 -- set_on_tick(name, true, A - 1), or flick(name, A).
--
-- WHAT EACH ANSWER NAMES.  Every detail carries the tick the press was ISSUED
-- on, the server tick the server's own varbit was first SEEN changed on
-- (api_drive.varbit_content: ToriRSServer_VarbitGet, the value content reads,
-- not the client's copy), and the tick it is IN FORCE from (issued + 1).
-- `info` (third return) carries the same numbers for a technique row:
-- {issued, seen, in_force, before, after, ...}.  `ok` is answered only when
-- the varbit was seen changed while the server tick still read the issue
-- tick; a change first seen on a later tick is `timeout` ("taken in late"),
-- because then the driver cannot say which npc phase saw it.
--
-- NEVER A LATE PRESS.  The tab is opened and the button waited for BEFORE the
-- wait for the tick; when the driver wakes on any tick but the asked one, or
-- is called after it, nothing is pressed and the answer is `timeout`.
--
-- TWO PRESSES IN ONE TICK (switch).  The presses of one switch are issued in
-- one Lua resume, with no yield between them, so one EmbedPump takes them all
-- in, in order, before the next tick.  Two protection prayers pressed in one
-- tick: the LATER press wins -- `[proc,prayer_toggle]` lights a prayer with
-- `~prayer_deactivate_conflicting($prayer, $data)` first
-- (skill_prayer/scripts/prayer.rs2), and the three protections share
-- `data=group,^prayer_group_overhead` (configs/prayers.dbrow).  switch answers
-- the earlier one as "displaced by" the later.
--
-- WHAT A FLICK COSTS on this server: prayer.rs2's `[proc,prayer_switched]`
-- arms `settimer(prayer_drain, 1)`, `[timer,prayer_drain]` adds the drain
-- effect (12 for each protection) to %varp6296_prayer_drain_counter every
-- tick a prayer is up and takes a point per max(60, 60 + 2 * prayer bonus),
-- and the last prayer going off clears the timer AND zeroes the counter.  A
-- timer armed on T first fires on T+1 (`srv->tick >= clock + interval`,
-- torirs_server_scripts.c timer loop), in the player phase, after the npcs.
-- flick reports the points and the counter before and after.

-- The server's tick, or nil on a binary / run without one.
function QD.prayer._server_tick()
    if api_drive.server_tick == nil then
        return nil
    end
    local result, tick = api_drive.server_tick()
    if result ~= "ok" then
        return nil
    end
    return tick
end

-- (result, value, source) for one entry's varbit as the SERVER holds it now:
-- ToriRSServer_VarbitGet through api_drive.varbit_content.  A binary without
-- that reader falls back to the client's record and says so in `source`.
function QD.prayer._server_varbit(entry)
    local symbol_result, varbit_id = api_drive.symbol("varbit", entry[3])
    if symbol_result ~= "ok" then
        return "no_row", "prayer: varbit " .. entry[3] .. " does not resolve (" .. tostring(symbol_result) .. ")"
    end
    if type(api_drive.varbit_content) ~= "function" then
        local result, value = api_drive.varbit_server(varbit_id)
        return result, value, "client record (no api_drive.varbit_content in this binary)"
    end
    local result, value = api_drive.varbit_content(varbit_id)
    if result == "unsupported" then
        -- A PARTY MEMBER hosts no embedded server (the leader does), so the
        -- server-side reader answers unsupported there: the client's record
        -- is the member's truth, as on a binary without the reader (raid
        -- seam42: after the v3 merge every member fought prayerless).
        local client_result, client_value = api_drive.varbit_server(varbit_id)
        return client_result, client_value, "client record (no embedded server in this process)"
    end
    if result ~= "ok" then
        return result, "prayer: varbit_content(" .. entry[3] .. ") answered " .. tostring(result)
    end
    return "ok", value, "server"
end

-- Waits for server tick `tick` and answers ("ok", tick) on it, or
-- ("timeout", detail) when it has passed or the driver woke after it.
-- Nothing here presses: the caller presses only on "ok".
function QD.prayer._await_server_tick(tick, who)
    local now = QD.prayer._server_tick()
    if now == nil then
        return "unsupported", who .. ": this run has no server tick (api_drive.server_tick)"
    end
    if now > tick then
        return "timeout", string.format("%s: tick %d has passed (server tick now %d) -- not pressed",
            who, tick, now)
    end
    if now < tick then
        await({
            level = function()
                local at = QD.prayer._server_tick()
                return at ~= nil and at >= tick
            end,
            note = who .. " waits for server tick " .. tick,
        }, (tick - now) + 3)
        now = QD.prayer._server_tick()
    end
    if now ~= tick then
        return "timeout", string.format(
            "%s: woke on server tick %s, not %d -- not pressed (never a late press)", who, tostring(now), tick)
    end
    return "ok", tick
end

-- One press list, issued NOW in one resume.  `presses` is a list of
-- {entry=, want=, component=}.  Returns issued_tick, end_tick, since (the
-- chat serial before the first press), or nil, detail, <the failing call's
-- result word> when the serial or a click fails.
function QD.prayer._press_all(presses, who)
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return nil, who .. ": the chat serial is unreadable: " .. tostring(since), serial_result
    end
    local issued = QD.prayer._server_tick()
    for i = 1, #presses do
        local press = presses[i]
        local click_result, click_detail = api_drive.if_click(press.component, 1)
        if click_result ~= "ok" then
            return nil, string.format("%s: press %d of %d on %s (component %d) answered %s %s",
                who, i, #presses, press.entry[2], press.component, tostring(click_result), tostring(click_detail)),
                click_result
        end
    end
    return issued, QD.prayer._server_tick(), since
end

-- After a press list: wait until the LAST press shows on the server's varbit
-- (or a refusal line arrives), recording the server tick it was first seen
-- on.  Packets are taken in in order by one pump, so the last one showing
-- means every earlier one has been handled.  Returns settled ("ok" or
-- "timeout"), seen_tick, refusal.
function QD.prayer._await_seen(presses, since, who)
    local last = presses[#presses]
    QD.prayer._seen_tick = nil
    QD.prayer._seen_refusal = nil
    local settled = await({
        level = function()
            local result, value = QD.prayer._server_varbit(last.entry)
            if result == "ok" and value == last.want then
                QD.prayer._seen_tick = QD.prayer._server_tick()
                return true
            end
            local refusal = QD.prayer._refusal_since(since)
            if refusal then
                QD.prayer._seen_refusal = refusal
                QD.prayer._seen_tick = QD.prayer._server_tick()
                return true
            end
            return false
        end,
        note = who .. " " .. last.entry[3] .. " == " .. last.want,
    }, QD.prayer.SET_TICKS)
    return settled, QD.prayer._seen_tick, QD.prayer._seen_refusal
end

-- t.prayer.set_on_tick(name, on, tick) -> result, detail, info
-- Press `name` to `on` while the server tick reads `tick`; in force from
-- tick+1's npc phase.  info = {name, want, asked, issued, seen, in_force,
-- before, after}.  A prayer already in the asked state on `tick` is `ok`
-- with "no press made" and info.issued = nil.
function QD.prayer.set_on_tick(name, on, tick)
    if on ~= true and on ~= false then
        error("prayer.set_on_tick(" .. tostring(name) .. ", on, tick): on must be true or false, got " .. tostring(on))
    end
    if type(tick) ~= "number" then
        error("prayer.set_on_tick(" .. tostring(name) .. ", on, tick): tick must be a server tick number, got "
            .. tostring(tick))
    end
    local who = "prayer.set_on_tick"
    local entry, why = QD.prayer._entry(name)
    if not entry then
        return "no_row", why
    end
    local want = on and 1 or 0
    local word = on and "on" or "off"
    local info = { name = entry[1], want = want, asked = tick }
    local now = QD.prayer._server_tick()
    if now == nil then
        return "unsupported", who .. ": this run has no server tick (api_drive.server_tick)", info
    end
    if now > tick then
        return "timeout", string.format("%s %s %s: tick %d has passed (server tick now %d) -- not pressed",
            who, entry[1], word, tick, now), info
    end
    -- The tab and the button first, while there is time.
    local prepare_result, component = QD.prayer._prepare(entry, who)
    if prepare_result ~= "ok" then
        return prepare_result, component, info
    end
    local wait_result, wait_detail = QD.prayer._await_server_tick(tick, who .. " " .. entry[1] .. " " .. word)
    if wait_result ~= "ok" then
        return wait_result, wait_detail, info
    end
    -- The tab can have been changed by something else while waiting: re-check
    -- (free when the button is still displayed).
    prepare_result, component = QD.prayer._prepare(entry, who)
    if prepare_result ~= "ok" then
        return prepare_result, component, info
    end
    if QD.prayer._server_tick() ~= tick then
        return "timeout", string.format("%s %s %s: the prayer tab re-opened past tick %d -- not pressed",
            who, entry[1], word, tick), info
    end
    local before_result, before = QD.prayer._server_varbit(entry)
    if before_result ~= "ok" then
        return before_result, who .. " " .. entry[1] .. ": the varbit before the press read "
            .. tostring(before_result) .. " " .. tostring(before), info
    end
    info.before = before
    if before == want then
        info.after = before
        return "ok", string.format(
            "%s already %s on server tick %d: %s = %d (server) -- no press made (a press would toggle it %s)",
            entry[1], word, tick, entry[3], before, on and "off" or "on"), info
    end
    local presses = { { entry = entry, want = want, component = component } }
    local issued, issued_end, since = QD.prayer._press_all(presses, who .. " " .. entry[1])
    if issued == nil then
        -- (nil, detail, the failing call's own result word)
        return since, issued_end, info
    end
    info.issued = issued
    local settled, seen, refusal = QD.prayer._await_seen(presses, since, who)
    local after_result, after = QD.prayer._server_varbit(entry)
    info.after = after_result == "ok" and after or nil
    info.seen = seen
    local where = string.format("pressed %s (component %d) on server tick %d (asked %d)",
        entry[2], component, issued, tick)
    if refusal and info.after ~= want then
        return "refused", string.format("%s %s refused: %s -- %s; %s = %s on server tick %s",
            entry[1], word, refusal, where, entry[3], tostring(info.after), tostring(seen)), info
    end
    if settled ~= "ok" or info.after ~= want then
        return "timeout", string.format("%s %s: %s; server %s still %s after %d tick(s) (%s) -- not re-pressed",
            entry[1], word, where, entry[3], tostring(info.after),
            (QD.prayer._server_tick() or issued) - issued, QD.prayer._lines_since(since)), info
    end
    if seen ~= issued then
        info.in_force = seen + 1
        return "timeout", string.format(
            "%s %s: %s; server %s %d -> %d first seen on server tick %d -- taken in LATE (not before tick %d's "
                .. "npc phase for certain; in force from %d at the latest)",
            entry[1], word, where, entry[3], before, want, seen, issued + 1, seen + 1), info
    end
    info.in_force = issued + 1
    return "ok", string.format(
        "%s %s: %s; server %s %d -> %d seen on server tick %d; in force from tick %d's npc phase "
            .. "(an npc rolling on %d sees it %s, one that rolled on %d did not)",
        entry[1], word, where, entry[3], before, want, seen, issued + 1, issued + 1, word, issued), info
end

-- t.prayer.switch(list, opts) -> result, detail, info
-- Two or more presses inside ONE server tick.  `list` entries are a name
-- (press it ON) or {name, on}; they are pressed in list order, with no yield
-- between them.  opts.tick: wait for that server tick first (as
-- set_on_tick); without it the presses go on the tick the call is made on.
-- A press asking for the state its prayer is already in is not made (it would
-- toggle it back), judged against the server's varbit on the press tick and
-- then against this list's own earlier presses of the same name.
-- `ok`: every press issued on one tick, seen on that tick, and every prayer
-- ends as the last press naming it asked -- or ends off because a LATER press
-- in this switch lit something (the server's conflict rule; "displaced by").
-- info = {issued, seen, in_force, presses = {{name, want}...},
--         final = {name = value}, displaced = {name = by}}.
function QD.prayer.switch(list, opts)
    if type(list) ~= "table" or #list < 1 then
        error("prayer.switch(list, opts): list must be a non-empty list of names or {name, on}, got " .. tostring(list))
    end
    opts = opts or {}
    local who = "prayer.switch"
    local asks = {}
    for i = 1, #list do
        local item = list[i]
        local name, on = item, true
        if type(item) == "table" then
            name, on = item[1], item[2]
        end
        if on ~= true and on ~= false then
            error("prayer.switch: entry " .. i .. " (" .. tostring(name) .. "): on must be true or false, got "
                .. tostring(on))
        end
        local entry, why = QD.prayer._entry(name)
        if not entry then
            return "no_row", who .. ": entry " .. i .. ": " .. why
        end
        asks[#asks + 1] = { entry = entry, want = on and 1 or 0 }
    end
    local info = { asked = opts.tick, presses = {}, final = {}, displaced = {} }
    local now = QD.prayer._server_tick()
    if now == nil then
        return "unsupported", who .. ": this run has no server tick (api_drive.server_tick)", info
    end
    if opts.tick ~= nil and now > opts.tick then
        return "timeout", string.format("%s: tick %d has passed (server tick now %d) -- not pressed",
            who, opts.tick, now), info
    end
    -- Every button displayed first (one tab holds them all).
    for i = 1, #asks do
        local prepare_result, component = QD.prayer._prepare(asks[i].entry, who)
        if prepare_result ~= "ok" then
            return prepare_result, component, info
        end
        asks[i].component = component
    end
    if opts.tick ~= nil then
        local wait_result, wait_detail = QD.prayer._await_server_tick(opts.tick, who)
        if wait_result ~= "ok" then
            return wait_result, wait_detail, info
        end
        for i = 1, #asks do
            local prepare_result, component = QD.prayer._prepare(asks[i].entry, who)
            if prepare_result ~= "ok" then
                return prepare_result, component, info
            end
            asks[i].component = component
        end
        if QD.prayer._server_tick() ~= opts.tick then
            return "timeout", string.format("%s: the prayer tab re-opened past tick %d -- not pressed",
                who, opts.tick), info
        end
    end
    -- Which presses to make: the server's state now, then this list's own.
    local state = {}
    local before = {}
    local presses = {}
    local skipped = {}
    for i = 1, #asks do
        local ask = asks[i]
        local key = ask.entry[1]
        if state[key] == nil then
            local read_result, value = QD.prayer._server_varbit(ask.entry)
            if read_result ~= "ok" then
                return read_result, who .. " " .. key .. ": the varbit before the press read "
                    .. tostring(read_result) .. " " .. tostring(value), info
            end
            state[key] = value
            before[key] = value
        end
        if state[key] == ask.want then
            skipped[#skipped + 1] = key .. "=" .. ask.want
        else
            presses[#presses + 1] = ask
            state[key] = ask.want
            info.presses[#info.presses + 1] = { name = key, want = ask.want }
        end
    end
    if #presses == 0 then
        return "ok", string.format("%s: nothing to press on server tick %d -- every prayer already as asked (%s)",
            who, QD.prayer._server_tick(), table.concat(skipped, ", ")), info
    end
    local issued, issued_end, since = QD.prayer._press_all(presses, who)
    if issued == nil then
        return since, issued_end, info
    end
    info.issued = issued
    local settled, seen, refusal = QD.prayer._await_seen(presses, since, who)
    info.seen = seen
    -- Final values, and whether each one is what its last press asked.
    local last_index = {}
    for i = 1, #presses do
        last_index[presses[i].entry[1]] = i
    end
    local parts = {}
    local wrong = {}
    for i = 1, #presses do
        local key = presses[i].entry[1]
        if last_index[key] == i then
            local read_result, value = QD.prayer._server_varbit(presses[i].entry)
            info.final[key] = read_result == "ok" and value or nil
            local text = string.format("%s %s->%s", key, tostring(before[key]), tostring(info.final[key]))
            if info.final[key] ~= presses[i].want then
                local by = nil
                if info.final[key] == 0 then
                    for j = i + 1, #presses do
                        if by == nil and presses[j].want == 1 then
                            by = presses[j].entry[1]
                        end
                    end
                end
                if by then
                    info.displaced[key] = by
                    text = text .. " (asked " .. presses[i].want .. "; displaced by " .. by .. ", a later press this tick)"
                else
                    wrong[#wrong + 1] = key
                    text = text .. " (asked " .. presses[i].want .. ")"
                end
            end
            parts[#parts + 1] = text
        end
    end
    local order = {}
    for i = 1, #presses do
        order[#order + 1] = presses[i].entry[1] .. (presses[i].want == 1 and "(on)" or "(off)")
    end
    local where = string.format("%d press(es) %s issued on server tick %d%s%s",
        #presses, table.concat(order, ", "), issued,
        (issued_end == issued) and " (all inside that one tick)" or (" -- the last on tick " .. tostring(issued_end)),
        (#skipped > 0) and ("; not pressed, already so: " .. table.concat(skipped, ", ")) or "")
    if refusal and #wrong > 0 then
        return "refused", string.format("%s: %s; refused: %s; server: %s", who, where, refusal,
            table.concat(parts, ", ")), info
    end
    if settled ~= "ok" or #wrong > 0 or issued_end ~= issued then
        return "timeout", string.format("%s: %s; server: %s; not as asked: %s (%s)", who, where,
            table.concat(parts, ", "), (#wrong > 0) and table.concat(wrong, ", ") or "none",
            QD.prayer._lines_since(since)), info
    end
    if seen ~= issued then
        info.in_force = seen + 1
        return "timeout", string.format(
            "%s: %s; server: %s; first seen on server tick %d -- taken in LATE (in force from %d at the latest)",
            who, where, table.concat(parts, ", "), seen, seen + 1), info
    end
    info.in_force = issued + 1
    return "ok", string.format("%s: %s; server: %s, seen on server tick %d; in force from tick %d's npc phase",
        who, where, table.concat(parts, ", "), seen, issued + 1), info
end

-- (points, counter) now: the prayer stat's current level as the client holds
-- it, and the server's drain counter (%varp6296_prayer_drain_counter, a
-- server-only varp, read through t.var.server's content path).  nil when
-- unreadable.
function QD.prayer._drain_reading()
    local points = nil
    local skill_result, reading = QD.skill.read("prayer")
    if skill_result == "ok" then
        points = reading.level
    end
    local counter_result, counter = QD.var.server("varp6296_prayer_drain_counter")
    if counter_result ~= "ok" then
        counter = nil
    end
    return points, counter
end

-- t.prayer.flick(name, at_tick) -> result, detail, info
-- The one-tick flick: `name` up for tick `at_tick`'s npc phase and for no
-- other -- ON pressed on server tick at_tick-1, OFF pressed on at_tick (file
-- section banner: a press on T is in force from T+1).  Back-to-back flicks
-- (flick(n, A), flick(n, A+1), ...) put the off and the next on inside the
-- same tick, which is the player's off-on double click.  The prayer must be
-- off before the flick: one already up is `refused` (a flick of a held prayer
-- is a hold).  info = {in_force = at_tick, on = <set_on_tick info>,
-- off = <set_on_tick info>, points_before, points_after, counter_before,
-- counter_after}.  The points are read before the ON press and after the
-- OFF press is seen; the drain timer of a tick runs in its player phase,
-- after the npcs, so the cost of tick at_tick is in points_after.
function QD.prayer.flick(name, at_tick)
    if type(at_tick) ~= "number" then
        error("prayer.flick(" .. tostring(name) .. ", at_tick): at_tick must be a server tick number, got "
            .. tostring(at_tick))
    end
    local who = "prayer.flick"
    local entry, why = QD.prayer._entry(name)
    if not entry then
        return "no_row", why
    end
    local info = { name = entry[1], in_force = at_tick }
    local now = QD.prayer._server_tick()
    if now == nil then
        return "unsupported", who .. ": this run has no server tick (api_drive.server_tick)", info
    end
    if now > at_tick - 1 then
        return "timeout", string.format(
            "%s %s for tick %d: its ON press belongs on tick %d, which has passed (server tick now %d) -- not pressed",
            who, entry[1], at_tick, at_tick - 1, now), info
    end
    local up_result, up = QD.prayer._server_varbit(entry)
    if up_result ~= "ok" then
        return up_result, who .. " " .. entry[1] .. ": the varbit read " .. tostring(up_result) .. " " .. tostring(up), info
    end
    if up == 1 then
        return "refused", string.format("%s %s for tick %d: %s is already up on server tick %d -- a flick starts "
            .. "from off (put it out first, or flick the tick after)", who, entry[1], at_tick, entry[1], now), info
    end
    info.points_before, info.counter_before = QD.prayer._drain_reading()
    local on_result, on_detail, on_info = QD.prayer.set_on_tick(entry[1], true, at_tick - 1)
    info.on = on_info
    if on_result ~= "ok" then
        return on_result, who .. " " .. entry[1] .. " for tick " .. at_tick .. ": the ON press: " .. tostring(on_detail), info
    end
    if on_info.issued == nil then
        return "refused", who .. " " .. entry[1] .. " for tick " .. at_tick
            .. ": the prayer came up by itself before the ON press: " .. tostring(on_detail), info
    end
    -- The counter on tick at_tick, after its timer ran and before the OFF
    -- press zeroes it: what this one tick added.
    if QD.prayer._await_server_tick(at_tick, who .. " " .. entry[1]) == "ok" then
        local _, counter_up = QD.prayer._drain_reading()
        info.counter_up = counter_up
    end
    local off_result, off_detail, off_info = QD.prayer.set_on_tick(entry[1], false, at_tick)
    info.off = off_info
    info.points_after, info.counter_after = QD.prayer._drain_reading()
    if off_result ~= "ok" then
        return off_result, string.format("%s %s for tick %d: ON pressed on %d (seen %s), then the OFF press: %s",
            who, entry[1], at_tick, on_info.issued, tostring(on_info.seen), tostring(off_detail)), info
    end
    if off_info.issued == nil then
        return "timeout", string.format("%s %s for tick %d: ON pressed on %d, but the prayer was already off on "
            .. "tick %d with no OFF press (drained out or turned off elsewhere): %s",
            who, entry[1], at_tick, on_info.issued, at_tick, tostring(off_detail)), info
    end
    return "ok", string.format(
        "%s %s up for tick %d's npc phase only: ON pressed on server tick %d (seen %d), OFF pressed on %d "
            .. "(seen %d); prayer points %s -> %s; drain counter %s before, %s on tick %d while up, %s after the OFF",
        who, entry[1], at_tick, on_info.issued, on_info.seen, off_info.issued, off_info.seen,
        tostring(info.points_before), tostring(info.points_after),
        tostring(info.counter_before), tostring(info.counter_up), at_tick, tostring(info.counter_after)), info
end
