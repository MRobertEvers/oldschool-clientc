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
    local before_result, before, before_source = QD.prayer._varbit(entry)
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

    local tab_result, tab_detail = QD.ui.tab("prayer")
    if tab_result ~= "ok" then
        return tab_result, "prayer.set " .. entry[1] .. ": the prayer tab did not open: " .. tostring(tab_detail)
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
        note = "prayer.set " .. entry[2] .. " displayed",
    }, QD.prayer.PAINT_TICKS)
    local component = QD.prayer._component
    if shown ~= "ok" then
        if component == nil then
            return "not_found", "prayer.set " .. entry[1] .. ": " .. entry[2]
                .. " never resolved after the prayer tab opened"
        end
        return "not_visible", string.format(
            "prayer.set %s: %s (component %d) was never displayed in %d tick(s) after the prayer tab opened",
            entry[1], entry[2], component, QD.prayer.PAINT_TICKS)
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
