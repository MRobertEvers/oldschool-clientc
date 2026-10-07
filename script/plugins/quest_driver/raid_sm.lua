-- quest-driver / raid_sm: THE PLAY SCRIPTS' STATE MACHINE LAYER
-- (raid seam53 play_state_machines, 2026-10-07).  A thin layer, not a rules
-- engine: about two hundred lines, no dependency beyond raid_play.lua's
-- QD.raid._play_fold.
--
-- WHY.  The owner, 2026-10-07: "You are thrashing a lot.  The script state
-- machines should be easily writable and easily written to implement the
-- states that it needs.  You need to fix that for verzik."  Before this the
-- plans' machines were hand-rolled, each in its own style -- a table
-- { state = "opening", next = "crabs", seen = {} } and a chain of string
-- comparisons, written out again per machine (the Verzik plan had four, in
-- four shapes).  Adding a state meant finding every chain that mentioned the
-- neighbouring ones.  There was no one place that said what the states were
-- or how they connected.
--
-- WHAT A MACHINE IS HERE.  Data.  A role is declared once, by name, and a
-- reader takes it in at a glance: a state holds its event handlers by event
-- name, so the declaration reads "in state RING, on tornado_step, do X and go
-- to SWING".
--
--     QD.raid.sm_declare("verzik_rotation", {
--         start = "opening",
--         states = {
--             opening = { on = {
--                 special_crabs = function(c, ev) return nil, "crabs" end,
--                 special_webs  = function(c, ev) return nil, "webs" end,
--             } },
--             crabs = {
--                 enter = function(c, ev) c.vz.owed = "webs" end,
--                 on = { auto = function(c, ev) return nil, "autos" end },
--             },
--             ...
--         },
--     })
--
-- THE CONTRACT, which is the owner's per-tick contract unchanged:
-- Events + State -> Intents, and the executor reconciles the intents per
-- channel.  This layer is the middle arrow only.  It does not send anything,
-- it does not know what a channel is, and the intents it returns keep the
-- shape raid_play.lua's executor already reconciles (walk, attack, press,
-- cast, eat, drink, gear, spec, want), folded by the executor's own
-- QD.raid._play_fold.  Nothing in raid_play.lua changed for this.
--
--   * A HANDLER is  function(ctx, ev) -> intent, go .  Both returns are
--     optional: a handler that returns nothing has handled the event and
--     stays put; one that returns only an intent stays put; one that returns
--     only a next state ( return nil, "SWING" ) transitions without an
--     intent.  TRANSITIONS ARE EXPLICIT BOTH WAYS -- the state that goes
--     forward names its target and the state it came from names the way back;
--     there is no fallthrough, no priority table, and no implicit return to a
--     start state.
--   * A STATE MAY DECLARE A PREMISE: `premise = function(ctx, ev) -> boolean`
--     with `broken = "<state>"`.  The premise is re-tested before the state is
--     offered ANY event, and when it fails the machine leaves for `broken`
--     at once (exit and enter hooks run, the trace records it as
--     `premise_broken`) and that event is then offered to the new state.  It
--     exists for the class of bug where a state's whole reason to exist has
--     been invalidated by something it does not listen for -- "I am holding
--     X" when X has been destroyed.  `broken` is the one transition target
--     this layer can check at DECLARE time, because the state names it
--     instead of a handler returning it.
--   * A STATE MAY DECLARE CHILD MACHINES: `children = { "a", "b" }`.  They
--     are live only while that state is current, stepped BY THE LAYER after
--     the parent has handled each event, in the order listed -- not by a call
--     someone has to remember at a branch.  So "which machines are running
--     now" is a property of the declaration, readable in one place.  A child
--     RESUMES rather than resets when its parent state is re-entered: a
--     parent can be left and re-entered within one logical phase (Verzik is
--     still `verzik_phase1` for the first three ticks of her transition, and
--     `v.phase or vz.phase` keeps the last phase on a tick with no boss row),
--     and a reset would throw away the child's history for nothing.  A child
--     is the SAME instance it would be at top level, so an existing machine
--     can be nested with no change to its state.  Activation work goes in the
--     PARENT's `enter` / `exit` hooks; a child has no activation hook of its
--     own, deliberately.
--
--     WHICH CHILD WINS A TICK: the `children` list is read top to bottom, and
--     a child that has taken the tick calls QD.raid.sm_claim(st) to stop its
--     later siblings for the rest of that event, rather than relying on
--     running last.  Declaration order being the answer is the point; it is
--     written down instead of being an accident of where the call sites sat.
--
--     WHERE AN INVARIANT BELONGS -- the trap nesting creates, and the reason
--     this paragraph is long.  A check written as a child of one phase CANNOT
--     FIRE while another phase is current, because the child is dormant.  So
--     a guard whose subject outlives the phase must not be a child of it.
--     Verzik's is the worked example: her shield breaking DESTROYS the
--     Dawnbringer in the holder's hand, and `verzik_dawnbringer` nested under
--     `p1` would make the premise `c.v.phase == "p1"` unfireable.  Choose:
--       - a fact about the BOUNDARY ("the sword is spent when P1 ends") is the
--         parent's `exit` hook, which runs AT the boundary rather than on the
--         first tick past it;
--       - a fact about the PHASE ("while I am in P1, I must still hold it") is
--         a `premise` on the parent state;
--       - a fact about the RAIDER ("past P1 I have a weapon I can fight with",
--         true however the hand emptied -- a destroyed sword, a failed equip, a
--         dropped swap, a death and a return) belongs to a machine that is NOT
--         nested, because no phase owns it.
--     In `_vzslow` the sword was destroyed at t211 and her form did not change
--     until t214, so for three ticks the holder was empty-handed while still
--     in p1: an exit hook cannot see that and a premise on the parent can.
--     If nesting makes it easy to write a check that cannot fire, it has made
--     things worse -- so prefer the least-nested home for a guard, not the
--     tidiest-looking one.
--   * ONE DECLARATION, MANY INSTANCES.  sm_run/sm_force/sm_at/sm_summary take
--     an optional trailing `inst` (a string or a number): the same declared
--     states running once per crab position, per pillar, per add.  Omitted,
--     the machine has one instance under its own id.  Declaring the same
--     states ten times over under ten ids is the thing this exists to avoid.
--   * ctx is the AUTHOR'S table, passed through untouched.  The layer puts
--     nothing in it and reads nothing from it.  A ported body that already
--     owns the tick's intent table may mutate ctx.intent and return nothing;
--     a new state should return an intent and let the fold decide.
--   * An event a state does not name is NOT AN ERROR: the state ignores it.
--     That is what "each state handles all events" means in practice -- the
--     ones it does not name it has decided to ignore, and the declaration
--     shows which those are by what is absent.
--   * A transition that lands mid-list is seen by the rest of the list: the
--     events after it are offered to the NEW state.  One tick can therefore
--     walk two states, which is what the rotation needs (her ball rides an
--     auto, so `auto` and `ball_air` arrive together).
--
-- EVENTS ARE DERIVED ONCE A DECIDE, IN ONE PLACE.  QD.raid.sm_events(st, v, f)
-- calls f once per tick view and caches the list on that view, so every
-- machine of one decide sees the same event set and no machine can derive its
-- own private view of the tick.  (On the view, not on the raider and not on
-- the tick number: a tick number can repeat, and see sm_events below.)  The derivation is the plan's (QD.raid._verzik_events);
-- keeping it out of the states is the point -- the states say what to do, the
-- derivation says what happened.  `tick` is raised every tick by convention
-- so a state can act without an event of its own.
--
-- THE TRACE.  Every entry and exit is recorded with its tick, for the
-- harness rows and for debugging.  m.seen keeps the plans' existing style
-- verbatim ("crabs@412/hp54") so a harness reading it does not change;
-- m.trace is the same thing in fields, and m.counts is the per-state tick
-- count the enrage rows already print (the old vz.ring_states).
--
-- GUARDRAILS.  A contract violation aborts loudly here, as in the C
-- (CLAUDE.md "Never return early because an input parameter is wrong --
-- assert"), with one assert per condition so the message names what was
-- wrong.  A TRANSITION TO AN UNDECLARED STATE IS THE ONE THAT MATTERS: a typo
-- in a state name used to be a silent no-op that showed up ten ticks later as
-- a missed prayer, and it now names the machine, the state, the event and the
-- bad target.

QD.raid.sm_decls = QD.raid.sm_decls or {}

-- the keys a state declaration may hold; anything else is a typo
local SM_STATE_KEYS = { enter = true, exit = true, on = true, note = true,
    premise = true, broken = true, children = true }

-- DECLARE: the machine, by name, with its states.  Checked here, at
-- declaration time, so a malformed machine fails when the part loads and not
-- on the tick that first reaches the bad state.
function QD.raid.sm_declare(id, decl)
    assert(type(id) == "string", "sm_declare: id must be a string")
    assert(type(decl) == "table", "sm_declare " .. id .. ": decl must be a table")
    assert(QD.raid.sm_decls[id] == nil, "sm_declare " .. id .. ": declared twice")
    assert(type(decl.states) == "table", "sm_declare " .. id .. ": decl.states must be a table")
    assert(type(decl.start) == "string", "sm_declare " .. id .. ": decl.start must be a state name")
    assert(decl.states[decl.start] ~= nil,
        "sm_declare " .. id .. ": decl.start '" .. tostring(decl.start) .. "' is not a declared state")
    local n = 0
    for name, s in pairs(decl.states) do
        assert(type(name) == "string", "sm_declare " .. id .. ": a state name must be a string")
        assert(type(s) == "table", "sm_declare " .. id .. ": state " .. name .. " must be a table")
        for k in pairs(s) do
            assert(SM_STATE_KEYS[k], "sm_declare " .. id .. ": state " .. name .. " has no such key '" .. tostring(k) .. "'")
        end
        if s.enter ~= nil then
            assert(type(s.enter) == "function", "sm_declare " .. id .. ": state " .. name .. " enter must be a function")
        end
        if s.exit ~= nil then
            assert(type(s.exit) == "function", "sm_declare " .. id .. ": state " .. name .. " exit must be a function")
        end
        if s.children ~= nil then
            assert(type(s.children) == "table",
                "sm_declare " .. id .. ": state " .. name .. " children must be a list of machine ids")
            for ci, child in ipairs(s.children) do
                assert(type(child) == "string",
                    "sm_declare " .. id .. ": state " .. name .. " child " .. ci .. " must be a machine id")
                assert(child ~= id,
                    "sm_declare " .. id .. ": state " .. name .. " names itself as a child")
            end
        end
        if s.premise ~= nil then
            assert(type(s.premise) == "function",
                "sm_declare " .. id .. ": state " .. name .. " premise must be a function")
            assert(type(s.broken) == "string",
                "sm_declare " .. id .. ": state " .. name .. " has a premise but no `broken` state to leave for")
        end
        if s.broken ~= nil then
            assert(type(s.broken) == "string",
                "sm_declare " .. id .. ": state " .. name .. " broken must be a state name")
            assert(s.premise ~= nil,
                "sm_declare " .. id .. ": state " .. name .. " has `broken` but no premise to break")
        end
        if s.on ~= nil then
            assert(type(s.on) == "table", "sm_declare " .. id .. ": state " .. name .. " on must be a table")
            for ev, h in pairs(s.on) do
                assert(type(ev) == "string", "sm_declare " .. id .. ": state " .. name .. " has a non-string event name")
                assert(type(h) == "function",
                    "sm_declare " .. id .. ": state " .. name .. " on." .. ev .. " must be a function")
            end
        end
        n = n + 1
    end
    for name, s in pairs(decl.states) do
        if s.broken ~= nil then
            assert(decl.states[s.broken] ~= nil, "sm_declare " .. id .. ": state " .. name ..
                " breaks to undeclared state '" .. s.broken .. "'")
        end
    end
    decl.id, decl.state_count = id, n
    decl.trace_max = decl.trace_max or 64
    QD.raid.sm_decls[id] = decl
    return decl
end

-- THE STATES a machine declares, sorted, for a log line or a harness row:
-- the one place that answers "what are this role's states?".
function QD.raid.sm_states(id)
    local decl = QD.raid.sm_decls[id]
    assert(decl ~= nil, "sm_states: no machine '" .. tostring(id) .. "' is declared")
    local out = {}
    for name in pairs(decl.states) do out[#out + 1] = name end
    table.sort(out)
    return out
end

-- EVENTS, once a DECIDE.  `derive` is called at most once per tick view and
-- its list is cached ON THAT VIEW, so every machine of one decide sees the
-- same set.
--
-- The cache hangs on `v` and not on the raider, and not on v.tick, because a
-- tick NUMBER can repeat: raid_play.lua's loop waits a tick only when the
-- tick has not already advanced inside the send (`if after == v.tick then
-- QD.ticks(1) end`), and a wait that does not advance leaves the next decide
-- reading the same number.  Keyed on the number, the second decide of one
-- tick would be handed the FIRST decide's list and its derivation would never
-- run -- so a derivation with any per-decide effect would silently see
-- nothing, and the machines would replay events from an already-advanced
-- state (reported by the Xarpus port, 2026-10-07, tick 45).  `v` is built
-- fresh by QD.raid._play_see once per decide, so the view itself is a key the
-- loop cannot repeat.
--
-- A derivation should still be idempotent within a tick: a repeated tick
-- number re-derives, and it must come to the same answer rather than consume
-- something twice.
function QD.raid.sm_events(st, v, derive)
    assert(st, "sm_events: st")
    assert(v, "sm_events: v")
    assert(type(derive) == "function", "sm_events: derive must be a function")
    local c = v.sm_ev
    if c ~= nil then
        assert(c.tick == v.tick, "sm_events: this view's cache is tick " ..
            tostring(c.tick) .. " but the view now reads tick " .. tostring(v.tick))
        return c.list
    end
    local list = derive(st, v)
    assert(type(list) == "table", "sm_events: derive must return a list of events")
    for i, ev in ipairs(list) do
        assert(type(ev) == "table", "sm_events: event " .. i .. " is not a table")
        assert(type(ev.name) == "string", "sm_events: event " .. i .. " has no name")
    end
    v.sm_ev = { tick = v.tick, list = list }
    return list
end

-- THE INSTANCE KEY.  One declaration can run many times over on one raider:
-- Maiden's ten crab positions are one WALKING/FROZEN/THAWED/GONE machine with
-- ten instances, not ten declarations of the same thing.  `inst` names which
-- one; omitted, the machine has a single instance under its own id.
local function sm_key(id, inst)
    if inst == nil then return id end
    assert(type(inst) == "string" or type(inst) == "number",
        "sm " .. id .. ": inst must be a string or a number")
    return id .. "#" .. tostring(inst)
end

-- the instance, created on first use.  `seen` is the plans' existing row
-- style; `counts` is the per-state tick count.
--
-- THE START STATE'S `enter` IS NOT CALLED.  Nothing transitioned into it, so
-- there is no event and no previous state to hand the hook, and calling it
-- would make the first tick of a machine different from every later entry into
-- the same state.  A plan that mirrors the machine's state onto a table of its
-- own through `enter` hooks must therefore seed that mirror with decl.start
-- itself at init (reported by the Nylocas port, 2026-10-07).
local function sm_instance(st, v, id, inst)
    st.sm = st.sm or {}
    local key = sm_key(id, inst)
    local m = st.sm[key]
    if m ~= nil then return m end
    local decl = QD.raid.sm_decls[id]
    assert(decl ~= nil, "sm: no machine '" .. tostring(id) .. "' is declared")
    m = { id = id, inst = inst, key = key, state = decl.start, since = v.tick, entered = v.tick,
        seen = {}, trace = {}, counts = {}, visits = {}, fired = {}, edges = {}, moves = 0 }
    st.sm[key] = m
    m.seen[1] = decl.start .. "@" .. tostring(v.tick) .. "/hp" .. tostring(v.hp)
    m.trace[1] = { state = decl.start, at = v.tick, hp = v.hp, by = "start" }
    m.visits[decl.start] = 1
    return m
end

-- GO: the transition, with its exit and enter hooks and its trace rows.  The
-- undeclared target is the assert that matters.
local function sm_go(st, v, m, decl, go, ctx, ev)
    local to = decl.states[go]
    assert(to ~= nil, "sm " .. m.key .. ": state '" .. m.state .. "' on " ..
        tostring(ev and ev.name) .. " transitions to undeclared state '" .. tostring(go) .. "'")
    local from = decl.states[m.state]
    if from.exit ~= nil then from.exit(ctx, ev, go) end
    local t = m.trace
    if #t > 0 then t[#t].left = v.tick end
    m.prev, m.state, m.since, m.moves = m.state, go, v.tick, m.moves + 1
    m.visits[go] = (m.visits[go] or 0) + 1
    -- EDGES TRAVERSED, counted rather than read back from the trace: a long
    -- room runs 400-600 ticks against decl.trace_max 64, so the trace is lossy
    -- at exactly the end where a late edge would show (Nylocas port).  O(1)
    -- per MOVE, not per tick, and bounded by the declaration.
    --
    -- KEYED ON THE EVENT, not just from>to, which is the whole point: the
    -- Nylocas finding is not "PILLAR_DEFENCE never reaches BOSS" -- it is
    -- "`waves_over` never moved it" while `boss_phase` moved it hundreds of
    -- times.  Without the event the row reads PILLAR_DEFENCE>BOSS 48 and looks
    -- healthy, which is the same blindness one level down.  A premise break is
    -- an edge too, under `premise_broken`, so a state only ever left by its
    -- premise does not read as having no exit taken.
    local ek = m.prev .. ">" .. go .. "/" .. tostring(ev and ev.name or "go")
    m.edges[ek] = (m.edges[ek] or 0) + 1
    m.seen[#m.seen + 1] = go .. "@" .. tostring(v.tick) .. "/hp" .. tostring(v.hp)
    if #t >= decl.trace_max then table.remove(t, 1) end
    t[#t + 1] = { state = go, at = v.tick, hp = v.hp, by = ev and ev.name or "go", from = m.prev }
    if to.enter ~= nil then to.enter(ctx, ev, m.prev) end
end

-- RUN: this tick's events through the machine.  Returns the instance and the
-- fold of whatever its handlers returned (nil when they returned no intent),
-- the fold being raid_play.lua's own, so the channels and their priorities are
-- the executor's and not a second set.
function QD.raid.sm_run(st, v, id, ctx, events, inst)
    assert(st, "sm_run: st")
    assert(v, "sm_run: v")
    local decl = QD.raid.sm_decls[id]
    assert(decl ~= nil, "sm_run: no machine '" .. tostring(id) .. "' is declared")
    assert(type(events) == "table", "sm_run " .. id .. ": events must be a list")
    local m = sm_instance(st, v, id, inst)
    local intents = nil
    for _, ev in ipairs(events) do
        local s = decl.states[m.state]
        -- THE PREMISE, re-tested on every event before the state acts on it.
        -- A state whose premise is "I am holding X" has to see the event that
        -- takes X away, and before this there was no way for it to ask: the
        -- Dawnbringer's `held` listened on the orb only, her shield breaking
        -- destroyed the sword in its hand, and it went on arming a special
        -- with an empty hand for 700 ticks (owner_verzik, 2026-10-07).
        if s.premise ~= nil and not s.premise(ctx, ev) then
            sm_go(st, v, m, decl, s.broken, ctx, { name = "premise_broken", was = ev.name })
            s = decl.states[m.state]
        end
        local h = s.on ~= nil and s.on[ev.name] or nil
        if h ~= nil then
            -- which BRANCH ran, not just which state was entered: a state can
            -- be hot while one of its handlers has never once been reached
            -- (Nylocas's PILLAR_DEFENCE is entered on 8 of 9 names and its
            -- `defence_clear` leave has never fired, because an override
            -- returns above the state tick).  A state-keyed count cannot show
            -- that; this can, and the declared `on` tables give the complete
            -- set to compare against, so "never invoked" is exact rather than
            -- inferred.
            local fk = m.state .. "/" .. ev.name
            m.fired[fk] = (m.fired[fk] or 0) + 1
            local intent, go = h(ctx, ev)
            if intent ~= nil then
                assert(type(intent) == "table",
                    "sm " .. m.key .. ": state " .. m.state .. " on " .. ev.name .. " returned a non-table intent")
                intent.by = intent.by or (m.key .. ":" .. m.state .. ":" .. ev.name)
                intents = intents or {}
                intents[#intents + 1] = intent
            end
            if go ~= nil and go ~= m.state then sm_go(st, v, m, decl, go, ctx, ev) end
        end
        -- THE CHILDREN of whatever state is current AFTER the parent handled
        -- this event, stepped in declaration order, each over this one event.
        local cs = decl.states[m.state].children
        if cs ~= nil then
            st.sm_claim = nil
            for _, child in ipairs(cs) do
                if st.sm_claim ~= nil then break end
                local cm, cfold = QD.raid.sm_run(st, v, child, ctx, { ev })
                cm.parent = m.id .. ":" .. m.state
                if cfold ~= nil then
                    intents = intents or {}
                    intents[#intents + 1] = cfold
                end
            end
            st.sm_claim = nil
        end
    end
    m.counts[m.state] = (m.counts[m.state] or 0) + 1
    m.ticks = (m.ticks or 0) + 1
    if intents == nil then return m, nil end
    return m, QD.raid._play_fold(intents)
end

-- CLAIM: "I have taken this tick."  A child calls this from a handler to
-- stop its LATER SIBLINGS being stepped for the rest of this event.  It is
-- the answer to "which child's intent wins when two write intent.attack on one
-- tick", and the answer is deliberately not "whichever ran last": the order is
-- the `children` list as declared, read top to bottom, and a child that has
-- taken the tick says so instead of relying on being last.  Verzik's enrage
-- PROTECT needs this against the rotation (owner_verzik, 2026-10-07).
--
-- It clears at the end of each event, so a claim never leaks to the next
-- event or the next tick.  A parent is never stopped by its child's claim --
-- the parent has already run by then.
function QD.raid.sm_claim(st, why)
    assert(st, "sm_claim: st")
    st.sm_claim = why or true
    return nil
end

-- FORCE: a transition nothing in the declaration owns -- the room's own form
-- change, which the plan reads straight off her npc row rather than deriving
-- as an event of its own.  Asserted the same way: a typo still aborts.
function QD.raid.sm_force(st, v, id, go, ctx, why, inst)
    assert(st, "sm_force: st")
    assert(v, "sm_force: v")
    local decl = QD.raid.sm_decls[id]
    assert(decl ~= nil, "sm_force: no machine '" .. tostring(id) .. "' is declared")
    assert(type(go) == "string", "sm_force " .. id .. ": go must be a state name")
    local m = sm_instance(st, v, id, inst)
    if m.state ~= go then sm_go(st, v, m, decl, go, ctx, { name = why or "force" }) end
    return m
end

-- the instance without running it (nil before the machine's first tick)
function QD.raid.sm_at(st, id, inst)
    assert(st, "sm_at: st")
    if st.sm == nil then return nil end
    return st.sm[sm_key(id, inst)]
end

-- COVERAGE: every machine instance this raider ran, with each DECLARED
-- state's tick count and entry count, and the declared states that never
-- fired.  One line per instance, appended to the play library's summary.
--
-- This exists because a port's proof is "identical readings", and that proof
-- is worth only as much as the states the runs actually entered.  A change
-- hung off a state nothing enters is indistinguishable from no change: the
-- Xarpus port surveyed 5 of 5 green with byte-identical ledgers for a
-- demonstration change on a state (`holding`) that is never entered in any
-- measured run (reported 2026-10-07).  A state printed here with `0` is a
-- state whose behaviour no run has checked, and it has to be accounted for --
-- unreachable in the mode we test, missed by these seeds, or wired wrong.
--
-- TWO READINGS, and the second is the stronger one.  `NEVER` lists states
-- that were never entered -- which catches a change hung off a state nothing
-- reaches (the Xarpus case).  `COLD` lists declared `<state>/<event>` handlers
-- that were never INVOKED -- which catches a change hung off a branch inside a
-- state that IS entered, and a state-keyed count cannot see that at all.
-- Nylocas is the case that forced it: PILLAR_DEFENCE is entered on 8 of 9
-- names with hundreds of ticks, and its `defence_clear` leave has never once
-- fired, because an override returns above the state tick.  Reading only the
-- state counts, that machine looks fully covered.
--
-- `edges` is the third reading: every transition TAKEN, as
-- `FROM>TO/event xN`.  It is a histogram and not a denominator -- the declared
-- edge set is not statically knowable, because a handler returns its target at
-- run time -- so an edge at zero shows up as an ABSENT key, to be read against
-- the declaration rather than announced.  And a zero does not say WHICH of
-- unreachable-by-construction, missed-by-the-seeds, or wired-wrong it is: in
-- the Nylocas case the edge was unreachable for a reason entirely outside the
-- declaration (an override returning above the state tick, so the event that
-- owns the edge is never raised).  The counter makes that question askable per
-- edge instead of per state; it does not answer it.
--
-- What NONE of the readings reach: a condition inside a handler body, an `if`
-- that never goes one way.  The layer does not see inside a handler, so that
-- is ordinary branch coverage and needs a different tool.  So `NEVER` empty
-- means "every state was entered", `COLD` empty means "every declared handler
-- ran", and neither means "every line ran".
--
-- Counts are `ticks/entries`: ticks is the number of decides spent in the
-- state, entries the number of times it was entered.  Only `ticks 0 AND
-- entries 0` means the state never fired; `ticks 0` ALONE does not, and two
-- different things produce it:
--   * a ONE-TICK state, which leaves within the tick it is entered, so the
--     end-of-tick counter never lands on it.  For such a state the entries
--     are the reading and the ticks are always 0 (`standup 0/1`,
--     `dodging 0/12`, `pre 0/1`).
--   * a START state, whose first entry is recorded when the instance is BORN,
--     before any decide is counted (reported by the Bloat port: bloat_raider
--     `outside 20/25`, where 15 of the 25 are instance births across 15 seat
--     records and the other 10 are real entries).
-- So a reader skimming for "0" will call exercised states dead.  The NEVER
-- list below is the reading to trust; it requires both counts zero.
-- the edges traversed, "FROM>TO xN", in a stable order
local function sm_edges_text(m)
    local keys = {}
    for k in pairs(m.edges) do keys[#keys + 1] = k end
    table.sort(keys)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = k .. " " .. m.edges[k] end
    return table.concat(out, " ")
end

function QD.raid.sm_coverage(st)
    assert(st, "sm_coverage: st")
    if st.sm == nil then return "" end
    local keys = {}
    for key in pairs(st.sm) do keys[#keys + 1] = key end
    table.sort(keys)
    local out = {}
    for _, key in ipairs(keys) do
        local m = st.sm[key]
        local parts, dead, cold = {}, {}, {}
        local decl = QD.raid.sm_decls[m.id]
        for _, name in ipairs(QD.raid.sm_states(m.id)) do
            local ticks, entries = m.counts[name] or 0, m.visits[name] or 0
            parts[#parts + 1] = name .. " " .. ticks .. "/" .. entries
            local never = ticks == 0 and entries == 0
            if never then dead[#dead + 1] = name end
            -- the declared handlers of this state that were never invoked.
            -- Only for a state that WAS entered: every handler of a state
            -- nothing reaches is cold trivially, and listing those buries the
            -- signal, which is "this state is hot and this branch is dead".
            local on = (not never) and decl.states[name].on or nil
            if on ~= nil then
                local evs = {}
                for e in pairs(on) do evs[#evs + 1] = e end
                table.sort(evs)
                for _, e in ipairs(evs) do
                    if m.fired[name .. "/" .. e] == nil then cold[#cold + 1] = name .. "/" .. e end
                end
            end
        end
        out[#out + 1] = key .. (m.parent ~= nil and ("<" .. m.parent) or "")
            .. " [" .. table.concat(parts, ", ") .. "] moves " .. m.moves
            .. ", ticks " .. tostring(m.ticks or 0)
            .. (#dead > 0 and (", NEVER " .. table.concat(dead, " ")) or ", all states fired")
            .. (#cold > 0 and (", COLD " .. table.concat(cold, " ")) or ", every handler ran")
            .. (next(m.edges) ~= nil and (", edges " .. sm_edges_text(m)) or "")
    end
    return "; sm coverage: " .. table.concat(out, " | ")
end

-- a one-line summary for a harness row or a log: the state now, the moves,
-- and the per-state tick counts in the order the machine declares them.
function QD.raid.sm_summary(st, id, inst)
    local m = QD.raid.sm_at(st, id, inst)
    if m == nil then return sm_key(id, inst) .. ": never ran" end
    local parts = {}
    for _, name in ipairs(QD.raid.sm_states(id)) do
        local n = m.counts[name]
        if n ~= nil then parts[#parts + 1] = name .. " " .. n end
    end
    return m.key .. ": " .. m.state .. " since t" .. tostring(m.since) ..
        ", " .. m.moves .. " moves, " .. table.concat(parts, " ")
end
