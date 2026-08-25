/*
 * React-side state for the client-owned activity opcodes.
 *
 * This is deliberately independent of HostRuntime.  The C CS2VM still owns
 * opcode decoding and exposes the reflected request records; these handlers
 * only mirror rs_highlight.c and the CLIENTOP SET/DEL registry in
 * rs_clientop.c.  That makes the state usable by a React host without growing
 * a second bytecode interpreter.
 */

export const HIGHLIGHT_LIMITS = Object.freeze({
    groups: 32,
    membersPerKind: 512,
    namedMembers: 64,
    nameBytes: 32,
});

export const CLIENTOP_LIMITS = Object.freeze({
    slotsPerKind: 8,
    labelBytes: 40,
});

export const HIGHLIGHT_KINDS = Object.freeze([
    'npc', 'npctype', 'loc', 'loctype', 'obj', 'objtype', 'player', 'tile', 'opgroup',
]);

export const CLIENTOP_KINDS = Object.freeze(['npc', 'loc', 'obj', 'player', 'tile']);

const HIGHLIGHT_ACTIONS = Object.freeze(['SETUP', 'ON', 'OFF', 'GET', 'CLEAR']);
const CLIENTOP_ACTIONS = Object.freeze(['SET', 'DEL']);
const NAMED_KINDS = new Set(['player', 'opgroup']);
const HIGHLIGHT_STATE = Symbol('cs2dom.highlight.state');
const CLIENTOP_STATE = Symbol('cs2dom.clientop.state');

function immutableSet(values) {
    const target = new Set(values);
    const immutable = () => {
        throw new TypeError('request-name sets are immutable');
    };
    let proxy;
    proxy = new Proxy(target, {
        get(set, property) {
            if( property === 'add' || property === 'delete' || property === 'clear' )
                return immutable;
            if( property === 'forEach' ) return (callback, thisArg) =>
                set.forEach((value) => callback.call(thisArg, value, value, proxy));
            const value = Reflect.get(set, property, set);
            return typeof value === 'function' ? value.bind(set) : value;
        },
    });
    return Object.freeze(proxy);
}

const HIGHLIGHT_NAMES = HIGHLIGHT_KINDS.flatMap((kind) =>
    HIGHLIGHT_ACTIONS.map((action) => `HIGHLIGHT_${kind.toUpperCase()}_${action}`));
const CLIENTOP_NAMES = CLIENTOP_KINDS.flatMap((kind) =>
    CLIENTOP_ACTIONS.map((action) => `CLIENTOP_${kind.toUpperCase()}_${action}`));

/** Exact names emitted by the generated CS2VM HOST bridge. */
export const HIGHLIGHT_REQUEST_NAMES = immutableSet(HIGHLIGHT_NAMES);
export const CLIENTOP_REQUEST_NAMES = immutableSet(CLIENTOP_NAMES);
export const HOST_ACTIVITY_REQUEST_NAMES = immutableSet([...HIGHLIGHT_NAMES, ...CLIENTOP_NAMES]);

const HIGHLIGHT_FORMS = new Map();

function form(kind, action, argCount, groupSlot, keySlot = -1, coordSlot = -1,
    flagSlot = -1, named = false) {
    HIGHLIGHT_FORMS.set(`HIGHLIGHT_${kind.toUpperCase()}_${action}`, Object.freeze({
        kind, action, argCount, groupSlot, keySlot, coordSlot, flagSlot, named,
    }));
}

for( const kind of HIGHLIGHT_KINDS ) form(kind, 'SETUP', 5, 0);
form('npc', 'ON', 3, 2, 0, 1);
form('npc', 'OFF', 3, 2, 0, 1);
form('npc', 'GET', 3, 2, 0, 1);
form('npctype', 'ON', 2, 1, 0);
form('npctype', 'OFF', 2, 1, 0);
form('npctype', 'GET', 2, 1, 0);
form('loc', 'ON', 4, 2, 0, 1, 3);
form('loc', 'OFF', 4, 2, 0, 1, 3);
form('loc', 'GET', 4, 2, 0, 1, 3);
form('loctype', 'ON', 2, 1, 0);
form('loctype', 'OFF', 2, 1, 0);
form('loctype', 'GET', 2, 1, 0);
form('obj', 'ON', 4, 2, 0, 1, 3);
form('obj', 'OFF', 4, 2, 0, 1, 3);
form('obj', 'GET', 4, 2, 0, 1, 3);
form('objtype', 'ON', 2, 1, 0);
form('objtype', 'OFF', 2, 1, 0);
form('objtype', 'GET', 2, 1, 0);
form('player', 'ON', 1, 0, -1, -1, -1, true);
form('player', 'OFF', 1, 0, -1, -1, -1, true);
form('player', 'GET', 1, 0, -1, -1, -1, true);
form('tile', 'ON', 3, 1, -1, 0, 2);
form('tile', 'OFF', 3, 1, -1, 0, 2);
form('tile', 'GET', 3, 1, -1, 0, 2);
form('opgroup', 'ON', 1, 0, -1, -1, -1, true);
form('opgroup', 'OFF', 1, 0, -1, -1, -1, true);
form('opgroup', 'GET', 1, 0, -1, -1, -1, true);
for( const kind of HIGHLIGHT_KINDS ) form(kind, 'CLEAR', 1, 0);

const CLIENTOP_FORMS = new Map(CLIENTOP_NAMES.map((name) => {
    const match = /^CLIENTOP_(NPC|LOC|OBJ|PLAYER|TILE)_(SET|DEL)$/.exec(name);
    return [name, Object.freeze({ kind: match[1].toLowerCase(), action: match[2] })];
}));

function int32(value) {
    return Number.isInteger(value) && value >= -0x80000000 && value <= 0x7fffffff
        ? value : null;
}

function nonnegativeInteger(value, fallback = 0) {
    return Number.isSafeInteger(value) && value >= 0 ? value : fallback;
}

function utf8Bytes(value) {
    let count = 0;
    for( const character of value ) {
        const point = character.codePointAt(0);
        count += point <= 0x7f ? 1 : point <= 0x7ff ? 2 : point <= 0xffff ? 3 : 4;
    }
    return count;
}

function truncateUtf8(value, maximum) {
    let result = '';
    let count = 0;
    for( const character of String(value ?? '') ) {
        const point = character.codePointAt(0);
        const bytes = point <= 0x7f ? 1 : point <= 0x7ff ? 2 : point <= 0xffff ? 3 : 4;
        if( count + bytes > maximum ) break;
        result += character;
        count += bytes;
    }
    return result;
}

function defaultStyle() {
    return { colour: -1, outlineWidth: 0, opacity: 0, flags: 0 };
}

function seededStyle(value) {
    const style = defaultStyle();
    if( !value || typeof value !== 'object' ) return style;
    const colour = int32(value.colour);
    const outlineWidth = int32(value.outlineWidth ?? value.outline_width ?? value.style);
    const opacity = int32(value.opacity);
    const flags = int32(value.flags);
    if( colour !== null ) style.colour = colour;
    if( outlineWidth !== null ) style.outlineWidth = outlineWidth;
    if( opacity !== null ) style.opacity = opacity;
    if( flags !== null ) style.flags = flags;
    return style;
}

function brand(state, symbol) {
    Object.defineProperty(state, symbol, { value: true });
    return state;
}

function assertState(state, symbol, label) {
    if( !state || state[symbol] !== true ) throw new TypeError(`${label} state is invalid`);
}

/**
 * Construct bounded highlight state, optionally restoring a prior snapshot.
 * Invalid or excess seed entries are ignored; later duplicate numeric entries
 * update flags just as a repeated C RS_HighlightOn call does.
 */
export function createHighlightState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    const styles = {};
    const members = {};
    for( const kind of HIGHLIGHT_KINDS ) {
        const seededStyles = Array.isArray(seed.styles?.[kind]) ? seed.styles[kind] : [];
        styles[kind] = Array.from({ length: HIGHLIGHT_LIMITS.groups }, (_, group) =>
            seededStyle(seededStyles[group]));
        members[kind] = [];
        const seededMembers = Array.isArray(seed.members?.[kind]) ? seed.members[kind] : [];
        for( const candidate of seededMembers ) {
            if( members[kind].length >= HIGHLIGHT_LIMITS.membersPerKind ) break;
            if( !candidate || typeof candidate !== 'object' ) continue;
            const group = int32(candidate.group);
            const key = int32(candidate.key);
            const coord = int32(candidate.coord);
            const flags = int32(candidate.flags);
            if( !groupOk(group) || key === null || coord === null || flags === null ) continue;
            const existing = members[kind].find((member) =>
                member.group === group && member.key === key && member.coord === coord);
            if( existing ) existing.flags = flags;
            else members[kind].push({ group, key, coord, flags });
        }
    }

    const named = [];
    const seededNamed = Array.isArray(seed.named) ? seed.named : [];
    for( const candidate of seededNamed ) {
        if( named.length >= HIGHLIGHT_LIMITS.namedMembers ) break;
        if( !candidate || typeof candidate !== 'object' || !NAMED_KINDS.has(candidate.kind) )
            continue;
        const group = int32(candidate.group);
        const name = typeof candidate.name === 'string' ? candidate.name : '';
        if( !groupOk(group) || !validName(name) ) continue;
        if( named.some((member) => member.kind === candidate.kind && member.group === group &&
            member.name === name) ) continue;
        named.push({ kind: candidate.kind, group, name });
    }

    return brand({
        revision: nonnegativeInteger(seed.revision),
        overflowed: Boolean(seed.overflowed),
        styles,
        members,
        named,
    }, HIGHLIGHT_STATE);
}

/** Construct the five-by-eight client-op registry from an optional snapshot. */
export function createClientOpState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    const slots = {};
    for( const kind of CLIENTOP_KINDS ) {
        slots[kind] = Array(CLIENTOP_LIMITS.slotsPerKind).fill(null);
        const seededSlots = Array.isArray(seed.slots?.[kind]) ? seed.slots[kind] : [];
        for( let slot = 0; slot < CLIENTOP_LIMITS.slotsPerKind; slot++ ) {
            const candidate = seededSlots[slot];
            if( !candidate || typeof candidate !== 'object' || candidate.set === false ) continue;
            slots[kind][slot] = {
                slot,
                label: truncateUtf8(candidate.label, CLIENTOP_LIMITS.labelBytes - 1),
                script_id: int32(candidate.script_id ?? candidate.scriptId) ?? 0,
            };
        }
    }
    return brand({ revision: nonnegativeInteger(seed.revision), slots }, CLIENTOP_STATE);
}

/** Construct both registries for a single host session. */
export function createHostActivityState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    return {
        highlight: createHighlightState(seed.highlight),
        clientop: createClientOpState(seed.clientop),
    };
}

function groupOk(group) {
    return group !== null && group >= 0 && group < HIGHLIGHT_LIMITS.groups;
}

function validName(name) {
    return name.length > 0 && utf8Bytes(name) < HIGHLIGHT_LIMITS.nameBytes;
}

function requestArgs(request, expected) {
    const source = Array.isArray(request?.args) || ArrayBuffer.isView(request?.args)
        ? Array.from(request.args) : [];
    const count = int32(request?.arg_count ?? request?.argCount ?? source.length);
    if( count !== expected || source.length < count ) return null;
    const args = source.slice(0, count).map(int32);
    return args.includes(null) ? null : args;
}

function outcome(handled, value = null, changed = false, revisionChanged = false) {
    return Object.freeze({ handled, value, changed, revisionChanged });
}

function memberIndex(state, kind, group, key, coord) {
    return state.members[kind].findIndex((member) =>
        member.group === group && member.key === key && member.coord === coord);
}

function namedIndex(state, kind, group, name) {
    return state.named.findIndex((member) =>
        member.kind === kind && member.group === group && member.name === name);
}

/**
 * Apply one reflected HIGHLIGHT_* request.
 *
 * `value` is the exact integer GET result (otherwise null). `changed` reports
 * an observable snapshot change. `revisionChanged` separately mirrors the C
 * revision: notably, SETUP increments it even when identical, while a repeat
 * ON may update flags without incrementing it.
 */
export function handleHighlightRequest(state, requestName, request = {}) {
    assertState(state, HIGHLIGHT_STATE, 'highlight');
    const form = HIGHLIGHT_FORMS.get(requestName);
    if( !form ) return outcome(false);
    const args = requestArgs(request, form.argCount);
    if( !args ) return outcome(false);
    const group = args[form.groupSlot];

    if( form.named ) {
        if( typeof request.name !== 'string' ) return outcome(false);
        const index = namedIndex(state, form.kind, group, request.name);
        if( form.action === 'GET' )
            return outcome(true, groupOk(group) && index >= 0 ? 1 : 0);
        if( !groupOk(group) ) return outcome(true);
        if( form.action === 'OFF' ) {
            if( index < 0 ) return outcome(true);
            state.named[index] = state.named[state.named.length - 1];
            state.named.pop();
            state.revision++;
            return outcome(true, null, true, true);
        }
        if( !validName(request.name) || index >= 0 ) return outcome(true);
        if( state.named.length >= HIGHLIGHT_LIMITS.namedMembers ) {
            const changed = !state.overflowed;
            state.overflowed = true;
            return outcome(true, null, changed);
        }
        state.named.push({ kind: form.kind, group, name: request.name });
        state.revision++;
        return outcome(true, null, true, true);
    }

    if( form.action === 'SETUP' ) {
        if( !groupOk(group) ) return outcome(true);
        const previous = state.styles[form.kind][group];
        const next = {
            colour: args[1], outlineWidth: args[2], opacity: args[3], flags: args[4],
        };
        const changed = previous.colour !== next.colour ||
            previous.outlineWidth !== next.outlineWidth || previous.opacity !== next.opacity ||
            previous.flags !== next.flags;
        state.styles[form.kind][group] = next;
        state.revision++;
        return outcome(true, null, changed, true);
    }

    if( form.action === 'CLEAR' ) {
        if( !groupOk(group) ) return outcome(true);
        if( NAMED_KINDS.has(form.kind) ) {
            const kept = state.named.filter((member) =>
                member.kind !== form.kind || member.group !== group);
            if( kept.length === state.named.length ) return outcome(true);
            state.named = kept;
        } else {
            const kept = state.members[form.kind].filter((member) => member.group !== group);
            if( kept.length === state.members[form.kind].length ) return outcome(true);
            state.members[form.kind] = kept;
        }
        state.revision++;
        return outcome(true, null, true, true);
    }

    const key = form.keySlot >= 0 ? args[form.keySlot] : -1;
    const coord = form.coordSlot >= 0 ? args[form.coordSlot] : -1;
    const flags = form.flagSlot >= 0 ? args[form.flagSlot] : 0;
    const index = memberIndex(state, form.kind, group, key, coord);
    if( form.action === 'GET' )
        return outcome(true, groupOk(group) && index >= 0 ? 1 : 0);
    if( !groupOk(group) ) return outcome(true);
    if( form.action === 'OFF' ) {
        if( index < 0 ) return outcome(true);
        const members = state.members[form.kind];
        members[index] = members[members.length - 1];
        members.pop();
        state.revision++;
        return outcome(true, null, true, true);
    }
    if( index >= 0 ) {
        const changed = state.members[form.kind][index].flags !== flags;
        state.members[form.kind][index].flags = flags;
        return outcome(true, null, changed);
    }
    if( state.members[form.kind].length >= HIGHLIGHT_LIMITS.membersPerKind ) {
        const changed = !state.overflowed;
        state.overflowed = true;
        return outcome(true, null, changed);
    }
    state.members[form.kind].push({ group, key, coord, flags });
    state.revision++;
    return outcome(true, null, true, true);
}

/**
 * Apply one reflected CLIENTOP_*_SET/DEL request. Slots retain their reflected
 * number, label and script_id. Labels use a UTF-8-safe equivalent of the C
 * 40-byte snprintf buffer (at most 39 payload bytes).
 */
export function handleClientOpRequest(state, requestName, request = {}) {
    assertState(state, CLIENTOP_STATE, 'clientop');
    const form = CLIENTOP_FORMS.get(requestName);
    if( !form ) return outcome(false);
    const slot = int32(request.slot);
    if( slot === null || slot < 0 || slot >= CLIENTOP_LIMITS.slotsPerKind )
        return outcome(true);
    const previous = state.slots[form.kind][slot];
    if( form.action === 'DEL' ) {
        if( previous === null ) return outcome(true);
        state.slots[form.kind][slot] = null;
        state.revision++;
        return outcome(true, null, true, true);
    }
    const next = {
        slot,
        label: truncateUtf8(request.label, CLIENTOP_LIMITS.labelBytes - 1),
        script_id: int32(request.script_id ?? request.scriptId) ?? 0,
    };
    const changed = previous === null || previous.label !== next.label ||
        previous.script_id !== next.script_id;
    state.slots[form.kind][slot] = next;
    if( changed ) state.revision++;
    return outcome(true, null, changed, changed);
}

/** Route either family against an aggregate activity state. */
export function handleHostActivityRequest(state, requestName, request = {}) {
    if( HIGHLIGHT_REQUEST_NAMES.has(requestName) )
        return handleHighlightRequest(state.highlight, requestName, request);
    if( CLIENTOP_REQUEST_NAMES.has(requestName) )
        return handleClientOpRequest(state.clientop, requestName, request);
    return outcome(false);
}

/** Return a bounded, deterministic, JSON-serializable highlight snapshot. */
export function snapshotHighlightState(state) {
    assertState(state, HIGHLIGHT_STATE, 'highlight');
    const styles = {};
    const members = {};
    for( const kind of HIGHLIGHT_KINDS ) {
        styles[kind] = Array.from({ length: HIGHLIGHT_LIMITS.groups }, (_, group) =>
            ({ ...state.styles[kind][group] }));
        members[kind] = state.members[kind]
            .slice(0, HIGHLIGHT_LIMITS.membersPerKind)
            .map((member) => ({ ...member }));
    }
    return {
        revision: state.revision,
        overflowed: state.overflowed,
        styles,
        members,
        named: state.named.slice(0, HIGHLIGHT_LIMITS.namedMembers)
            .map((member) => ({ ...member })),
    };
}

/** Return a bounded, deterministic, JSON-serializable client-op snapshot. */
export function snapshotClientOpState(state) {
    assertState(state, CLIENTOP_STATE, 'clientop');
    const slots = {};
    for( const kind of CLIENTOP_KINDS ) slots[kind] = Array.from(
        { length: CLIENTOP_LIMITS.slotsPerKind }, (_, index) => {
            const slot = state.slots[kind][index];
            return slot === null || slot === undefined ? null : { ...slot };
        });
    return { revision: state.revision, slots };
}

export function snapshotHostActivityState(state) {
    return {
        highlight: snapshotHighlightState(state.highlight),
        clientop: snapshotClientOpState(state.clientop),
    };
}
