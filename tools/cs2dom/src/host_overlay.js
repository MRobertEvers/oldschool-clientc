/*
 * React-side scripted entity overlays.
 *
 * The C CS2VM remains responsible for decoding opcodes and reflecting their
 * exact request records.  This module mirrors rs_entity_overlay.c and
 * exec_entity_overlay() without assuming that a React preview has a world,
 * camera, or UITree. Those pieces are explicit adapters; absent adapters mean
 * the same thing as the native host's absent subject/tree, never a made-up
 * entity or screen position.
 */

export const OVERLAY_LIMITS = Object.freeze({ records: 640, staticTypes: 5 });

export const OVERLAY_ANCHOR = Object.freeze({ NPC: 0, PLAYER: 1, STATIC: 2 });
export const OVERLAY_STATIC_TYPE_COORD = 4;
export const OVERLAY_BAND = Object.freeze({ MIDDLE: 0, ABOVE: 1, BELOW: 2 });
/** Current native parity: CREATE pops this final argument but does not use it. */
export const OVERLAY_SOURCE_COORD_IGNORED = true;

const REQUEST_NAMES = Object.freeze([
    'OVERLAY_CC_CREATE',
    'OVERLAY_CC_DELETEALL',
    'OVERLAY_FIND',
    'OVERLAY_CC_FIND',
    'OVERLAY_NPC_CREATE',
    'OVERLAY_LOC_CREATE',
    'OVERLAY_PLAYER_CREATE',
    'OVERLAY_COORD_CREATE',
    'OVERLAY_NPC_GET',
    'OVERLAY_LOC_GET',
    'OVERLAY_PLAYER_GET',
    'OVERLAY_COORD_GET',
    'OVERLAY_NPC_DESTROY',
    'OVERLAY_LOC_DESTROY',
    'OVERLAY_PLAYER_DESTROY',
    'OVERLAY_COORD_DESTROY',
]);

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

/** The exact 16 generated HOST request names handled here. */
export const OVERLAY_REQUEST_NAMES = immutableSet(REQUEST_NAMES);

const FORMS = new Map([
    ['OVERLAY_CC_CREATE', { action: 'cc_create', argCount: 3 }],
    ['OVERLAY_CC_DELETEALL', { action: 'cc_deleteall', argCount: 1 }],
    ['OVERLAY_FIND', { action: 'find_layer', argCount: 1 }],
    ['OVERLAY_CC_FIND', { action: 'find_child', argCount: 2 }],
    ['OVERLAY_NPC_CREATE', {
        action: 'create_entity', anchor: OVERLAY_ANCHOR.NPC, subject: 'npc', argCount: 5,
    }],
    ['OVERLAY_LOC_CREATE', { action: 'create_loc', subject: 'loc', argCount: 5 }],
    ['OVERLAY_PLAYER_CREATE', {
        action: 'create_entity', anchor: OVERLAY_ANCHOR.PLAYER, subject: 'player', argCount: 5,
    }],
    ['OVERLAY_COORD_CREATE', { action: 'create_coord', argCount: 6 }],
    ['OVERLAY_NPC_GET', {
        action: 'get_entity', anchor: OVERLAY_ANCHOR.NPC, subject: 'npc', argCount: 1,
    }],
    ['OVERLAY_LOC_GET', { action: 'get_loc', subject: 'loc', argCount: 1 }],
    ['OVERLAY_PLAYER_GET', {
        action: 'get_entity', anchor: OVERLAY_ANCHOR.PLAYER, subject: 'player', argCount: 1,
    }],
    ['OVERLAY_COORD_GET', { action: 'get_coord', argCount: 2 }],
    ['OVERLAY_NPC_DESTROY', {
        action: 'destroy_entity', anchor: OVERLAY_ANCHOR.NPC, subject: 'npc', argCount: 1,
    }],
    ['OVERLAY_LOC_DESTROY', { action: 'destroy_loc', subject: 'loc', argCount: 1 }],
    ['OVERLAY_PLAYER_DESTROY', {
        action: 'destroy_entity', anchor: OVERLAY_ANCHOR.PLAYER,
        subject: 'player', argCount: 1,
    }],
    ['OVERLAY_COORD_DESTROY', { action: 'destroy_coord', argCount: 2 }],
].map(([name, value]) => [name, Object.freeze(value)]));

const OVERLAY_STATE = Symbol('cs2dom.overlay.state');

function int32(value) {
    return Number.isInteger(value) && value >= -0x80000000 && value <= 0x7fffffff
        ? value : null;
}

function staticTypeValid(value) {
    return value !== null && value >= 0 && value < OVERLAY_LIMITS.staticTypes;
}

function cloneRecord(record) {
    return record ? { ...record } : null;
}

function sameRecord(left, right) {
    if( left === null || right === null ) return left === right;
    return left.in_use === right.in_use && left.slot === right.slot &&
        left.anchor === right.anchor && left.uid === right.uid && left.coord === right.coord &&
        left.static_type === right.static_type && left.band === right.band &&
        left.width === right.width && left.height === right.height &&
        left.component_id === right.component_id;
}

function normalizeSeedRecord(value) {
    if( !value || typeof value !== 'object' || value.in_use === false ) return null;
    const anchor = int32(value.anchor);
    const slot = int32(value.slot);
    const band = int32(value.band);
    const width = int32(value.width);
    const height = int32(value.height);
    const componentId = int32(value.component_id ?? value.componentId ?? -1);
    if( anchor === null || anchor < OVERLAY_ANCHOR.NPC || anchor > OVERLAY_ANCHOR.STATIC ||
        slot === null || band === null || width === null || height === null ||
        componentId === null ) return null;

    if( anchor === OVERLAY_ANCHOR.STATIC ) {
        const coord = int32(value.coord);
        const staticType = int32(value.static_type ?? value.staticType);
        if( coord === null || !staticTypeValid(staticType) ) return null;
        return {
            in_use: true,
            slot,
            anchor,
            uid: -1,
            coord,
            static_type: staticType,
            band,
            width,
            height,
            component_id: componentId,
        };
    }

    const uid = int32(value.uid);
    if( uid === null ) return null;
    return {
        in_use: true,
        slot,
        anchor,
        uid,
        coord: -1,
        static_type: -1,
        band,
        width,
        height,
        component_id: componentId,
    };
}

function identity(record) {
    return record.anchor === OVERLAY_ANCHOR.STATIC
        ? `s:${record.coord}:${record.static_type}:${record.slot}`
        : `e:${record.anchor}:${record.uid}:${record.slot}`;
}

/** Construct the fixed 640-record native registry from an optional snapshot. */
export function createOverlayState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    const seeded = Array.isArray(seed.items) ? seed.items : [];
    const items = Array(OVERLAY_LIMITS.records).fill(null);
    const identities = new Set();
    for( let index = 0; index < OVERLAY_LIMITS.records; index++ ) {
        const record = normalizeSeedRecord(seeded[index]);
        if( !record || identities.has(identity(record)) ) continue;
        items[index] = record;
        identities.add(identity(record));
    }
    const state = { items };
    Object.defineProperty(state, OVERLAY_STATE, { value: true });
    return state;
}

function assertState(state) {
    if( !state || state[OVERLAY_STATE] !== true )
        throw new TypeError('overlay state is invalid');
}

function requestArgs(request, expected) {
    const source = Array.isArray(request?.args) || ArrayBuffer.isView(request?.args)
        ? Array.from(request.args) : [];
    const count = int32(request?.arg_count ?? request?.argCount ?? source.length);
    if( count !== expected || source.length < count ) return null;
    const args = source.slice(0, count).map(int32);
    return args.includes(null) ? null : args;
}

function target(componentId, dotOperand) {
    return Object.freeze({ component_id: componentId, dot_operand: dotOperand });
}

function outcome({ handled = true, ok = true, value = null, changed = false,
    activeTarget = null, error = null } = {}) {
    return Object.freeze({
        handled,
        ok,
        value,
        changed,
        target: activeTarget,
        error,
    });
}

function malformed() {
    return outcome({ handled: false, ok: false, error: 'malformed overlay request' });
}

function findEntity(state, anchor, uid, slot) {
    return state.items.findIndex((record) => record && record.anchor === anchor &&
        record.uid === uid && record.slot === slot);
}

function findStatic(state, coord, staticType, slot) {
    return state.items.findIndex((record) => record &&
        record.anchor === OVERLAY_ANCHOR.STATIC && record.coord === coord &&
        record.static_type === staticType && record.slot === slot);
}

function resolveSubject(adapters, kind, requestName, request) {
    const named = kind === 'npc' ? adapters.resolveNpcSubject
        : kind === 'player' ? adapters.resolvePlayerSubject : adapters.resolveLocSubject;
    const raw = typeof named === 'function'
        ? named({ request_name: requestName, request })
        : typeof adapters.resolveSubject === 'function'
            ? adapters.resolveSubject(kind, { request_name: requestName, request }) : null;
    if( kind === 'loc' ) {
        if( !raw || typeof raw !== 'object' ) return null;
        const coord = int32(raw.coord);
        const layer = int32(raw.layer ?? raw.static_type ?? raw.staticType);
        return coord === null || layer === null ? null : { coord, layer };
    }
    const uid = int32(typeof raw === 'number' ? raw : raw?.uid);
    return uid === null ? null : { uid };
}

function componentIdFrom(value) {
    const id = int32(typeof value === 'number' ? value
        : value?.component_id ?? value?.componentId);
    return id !== null && id >= 0 ? id : -1;
}

function adapterChanged(value, fallback = false) {
    if( typeof value === 'boolean' ) return value;
    if( value && typeof value === 'object' && typeof value.changed === 'boolean' )
        return value.changed;
    return fallback;
}

function componentExists(adapters, componentId, payload) {
    if( componentId < 0 ) return false;
    const callback = adapters.hasComponent ?? adapters.layerExists;
    return typeof callback === 'function' && Boolean(callback({
        component_id: componentId,
        ...payload,
    }));
}

function activate(adapters, componentId, dotOperand, payload) {
    const activeTarget = target(componentId, dotOperand);
    if( typeof adapters.setActiveComponent === 'function' ) adapters.setActiveComponent({
        ...activeTarget,
        ...payload,
    });
    return activeTarget;
}

function takeRecord(state, record, sourceCoord, adapters) {
    const replacedIndex = record.anchor === OVERLAY_ANCHOR.STATIC
        ? findStatic(state, record.coord, record.static_type, record.slot)
        : findEntity(state, record.anchor, record.uid, record.slot);
    const replaced = replacedIndex >= 0 ? cloneRecord(state.items[replacedIndex]) : null;

    /* Exact rs_entity_overlay.c order: destroy the matching RECORD, then take
     * the first free index. The native create path does not separately delete
     * the old layer here; UITree layer creation replaces the same sub-id when
     * the reclaimed record is also the first free index. */
    if( replacedIndex >= 0 ) state.items[replacedIndex] = null;
    const index = state.items.indexOf(null);
    if( index < 0 ) return { index: -1, changed: false };
    state.items[index] = record;

    let treeChanged = false;
    if( typeof adapters.createLayer === 'function' ) {
        const attached = adapters.createLayer({
            overlay_index: index,
            width: record.width,
            height: record.height,
            record: cloneRecord(record),
            replaced,
            /* Reflected and exposed for diagnostics only. Current C parity is
             * that source_coord is consumed but neither stored nor consulted. */
            source_coord: sourceCoord,
        });
        const componentId = componentIdFrom(attached);
        if( componentId < 0 ) {
            state.items[index] = null;
            return {
                index: -1,
                changed: replaced !== null || adapterChanged(attached),
            };
        }
        record.component_id = componentId;
        treeChanged = true;
    }

    const sameIndex = replacedIndex === index;
    return {
        index,
        changed: treeChanged || !sameIndex || !sameRecord(replaced, record),
    };
}

function createEntityRecord(anchor, uid, args) {
    const record = {
        in_use: true,
        slot: args[0],
        anchor,
        uid,
        coord: -1,
        static_type: -1,
        band: args[1],
        width: args[2],
        height: args[3],
        component_id: -1,
    };
    return record;
}

function createStaticRecord(coord, staticType, args, offset) {
    return {
        in_use: true,
        slot: args[offset],
        anchor: OVERLAY_ANCHOR.STATIC,
        uid: -1,
        coord,
        static_type: staticType,
        band: args[offset + 1],
        width: args[offset + 2],
        height: args[offset + 3],
        component_id: -1,
    };
}

function deleteRecord(state, index, adapters) {
    if( index < 0 || index >= OVERLAY_LIMITS.records ) return false;
    const record = state.items[index];
    if( !record ) return false;
    if( record.component_id >= 0 && typeof adapters.deleteLayer === 'function' )
        adapters.deleteLayer({
            overlay_index: index,
            component_id: record.component_id,
            record: cloneRecord(record),
        });
    state.items[index] = null;
    return true;
}

/** Native RS_OverlayDestroy plus host-owned layer deletion. */
export function destroyOverlay(state, index, adapters = {}) {
    assertState(state);
    const normalized = int32(index);
    return normalized === null ? false : deleteRecord(state, normalized, adapters);
}

/** Native RS_OverlayDestroyEntity, useful when a scene provider despawns one. */
export function destroyEntityOverlays(state, anchor, uid, adapters = {}) {
    assertState(state);
    anchor = int32(anchor);
    uid = int32(uid);
    if( (anchor !== OVERLAY_ANCHOR.NPC && anchor !== OVERLAY_ANCHOR.PLAYER) || uid === null )
        return 0;
    let count = 0;
    for( let index = 0; index < OVERLAY_LIMITS.records; index++ ) {
        const record = state.items[index];
        if( record && record.anchor === anchor && record.uid === uid &&
            deleteRecord(state, index, adapters) ) count++;
    }
    return count;
}

/** Return a detached record copy, or null for an out-of-range/free index. */
export function getOverlay(state, index) {
    assertState(state);
    index = int32(index);
    return index !== null && index >= 0 && index < OVERLAY_LIMITS.records
        ? cloneRecord(state.items[index]) : null;
}

export function countOverlays(state) {
    assertState(state);
    return state.items.reduce((count, record) => count + (record ? 1 : 0), 0);
}

/**
 * Apply one of the 16 reflected OVERLAY_* requests.
 *
 * `value` is the exact int result (-1/index for create/get, 0/1 for find).
 * `target` describes the active/dot component update for an integrating host.
 * `ok=false` represents native CS2VM_EXECNO_ERROR (currently only rejected
 * dynamic layers or a failed dynamic-child creation).
 */
export function handleOverlayRequest(state, requestName, request = {}, adapters = {}) {
    assertState(state);
    const form = FORMS.get(requestName);
    if( !form ) return outcome({ handled: false, ok: false, error: 'unknown overlay request' });
    const args = requestArgs(request, form.argCount);
    if( !args ) return malformed();
    const rawDotOperand = request.dot_operand ?? request.dotOperand ?? 0;
    const dotOperand = typeof rawDotOperand === 'boolean'
        ? Number(rawDotOperand) : int32(rawDotOperand);
    if( dotOperand === null ) return malformed();

    if( form.action === 'create_entity' ) {
        const subject = resolveSubject(adapters, form.subject, requestName, request);
        if( !subject ) return outcome({ value: -1 });
        const record = createEntityRecord(form.anchor, subject.uid, args);
        const created = takeRecord(state, record, args[4], adapters);
        return outcome({ value: created.index, changed: created.changed });
    }
    if( form.action === 'create_loc' ) {
        const subject = resolveSubject(adapters, 'loc', requestName, request);
        if( !subject || !staticTypeValid(subject.layer) ) return outcome({ value: -1 });
        const record = createStaticRecord(subject.coord, subject.layer, args, 0);
        const created = takeRecord(state, record, args[4], adapters);
        return outcome({ value: created.index, changed: created.changed });
    }
    if( form.action === 'create_coord' ) {
        const record = createStaticRecord(args[0], OVERLAY_STATIC_TYPE_COORD, args, 1);
        const created = takeRecord(state, record, args[5], adapters);
        return outcome({ value: created.index, changed: created.changed });
    }
    if( form.action === 'get_entity' ) {
        const subject = resolveSubject(adapters, form.subject, requestName, request);
        return outcome({ value: subject ? findEntity(state, form.anchor, subject.uid, args[0]) : -1 });
    }
    if( form.action === 'get_loc' ) {
        const subject = resolveSubject(adapters, 'loc', requestName, request);
        return outcome({
            value: subject ? findStatic(state, subject.coord, subject.layer, args[0]) : -1,
        });
    }
    if( form.action === 'get_coord' ) return outcome({
        value: findStatic(state, args[0], OVERLAY_STATIC_TYPE_COORD, args[1]),
    });

    if( form.action === 'destroy_entity' ) {
        const subject = resolveSubject(adapters, form.subject, requestName, request);
        const index = subject ? findEntity(state, form.anchor, subject.uid, args[0]) : -1;
        return outcome({ changed: deleteRecord(state, index, adapters) });
    }
    if( form.action === 'destroy_loc' ) {
        const subject = resolveSubject(adapters, 'loc', requestName, request);
        const index = subject ? findStatic(state, subject.coord, subject.layer, args[0]) : -1;
        return outcome({ changed: deleteRecord(state, index, adapters) });
    }
    if( form.action === 'destroy_coord' ) return outcome({
        changed: deleteRecord(state,
            findStatic(state, args[0], OVERLAY_STATIC_TYPE_COORD, args[1]), adapters),
    });

    const overlayIndex = args[0];
    const record = overlayIndex >= 0 && overlayIndex < OVERLAY_LIMITS.records
        ? state.items[overlayIndex] : null;
    const componentId = record?.component_id ?? -1;
    const layerPayload = { overlay_index: overlayIndex, record: cloneRecord(record) };
    const layerFound = record !== null && componentExists(
        adapters, componentId, layerPayload);

    if( form.action === 'find_layer' ) {
        if( !layerFound ) return outcome({ value: 0 });
        return outcome({
            value: 1,
            activeTarget: activate(adapters, componentId, dotOperand, layerPayload),
        });
    }
    if( form.action === 'find_child' ) {
        if( !layerFound || typeof adapters.findChild !== 'function' )
            return outcome({ value: 0 });
        const found = adapters.findChild({
            ...layerPayload,
            parent_component_id: componentId,
            child_index: args[1],
        });
        const childId = componentIdFrom(found);
        if( childId < 0 ) return outcome({ value: 0 });
        return outcome({
            value: 1,
            activeTarget: activate(adapters, childId, dotOperand, {
                ...layerPayload,
                parent_component_id: componentId,
                child_index: args[1],
            }),
        });
    }
    if( form.action === 'cc_create' ) {
        if( args[1] === 0 ) return outcome({
            ok: false,
            error: 'dynamic layers are not allowed inside entity overlays',
        });
        if( !layerFound || typeof adapters.createChild !== 'function' ) return outcome();
        const created = adapters.createChild({
            ...layerPayload,
            parent_component_id: componentId,
            component_type: args[1],
            child_index: args[2],
            dot_operand: dotOperand,
        });
        const childId = componentIdFrom(created);
        if( childId < 0 ) return outcome({
            ok: false,
            error: 'dynamic overlay child creation failed',
        });
        return outcome({
            changed: adapterChanged(created, true),
            activeTarget: activate(adapters, childId, dotOperand, {
                ...layerPayload,
                parent_component_id: componentId,
                child_index: args[2],
            }),
        });
    }
    if( form.action === 'cc_deleteall' ) {
        if( !layerFound || typeof adapters.deleteAllChildren !== 'function' ) return outcome();
        const deleted = adapters.deleteAllChildren({
            ...layerPayload,
            parent_component_id: componentId,
        });
        return outcome({ changed: adapterChanged(deleted) });
    }
    return malformed();
}

function projectionCallback(record, adapters) {
    if( record.anchor === OVERLAY_ANCHOR.NPC ) return adapters.projectNpcAnchor;
    if( record.anchor === OVERLAY_ANCHOR.PLAYER ) return adapters.projectPlayerAnchor;
    if( record.static_type === OVERLAY_STATIC_TYPE_COORD ) return adapters.projectCoordAnchor;
    return adapters.projectLocAnchor;
}

function projectionResult(found, record, raw) {
    const subjectLive = Boolean(raw?.subject_live ?? raw?.subjectLive);
    const base = {
        found,
        subject_live: subjectLive,
        ok: false,
        x: null,
        y: null,
        width: record?.width ?? 0,
        height: record?.height ?? 0,
        band: record?.band ?? 0,
    };
    if( !found || !subjectLive || !raw || (raw.ok === false || raw.projected === false) )
        return Object.freeze(base);

    const topX = int32(raw.top_x ?? raw.top?.x);
    const topY = int32(raw.top_y ?? raw.top?.y);
    const midX = int32(raw.mid_x ?? raw.middle?.x ?? raw.mid?.x);
    const midY = int32(raw.mid_y ?? raw.middle?.y ?? raw.mid?.y);
    const footX = int32(raw.foot_x ?? raw.foot?.x);
    const footY = int32(raw.foot_y ?? raw.foot?.y);
    if( [topX, topY, midX, midY, footX, footY].includes(null) )
        return Object.freeze(base);

    const originX = int32(raw.origin_x ?? raw.origin?.x ?? 0);
    const originY = int32(raw.origin_y ?? raw.origin?.y ?? 0);
    if( originX === null || originY === null ) return Object.freeze(base);
    let x = midX - Math.trunc(record.width / 2);
    let y = midY - Math.trunc(record.height / 2);
    if( record.band === OVERLAY_BAND.ABOVE ) {
        x = topX - Math.trunc(record.width / 2);
        y = topY - record.height;
    } else if( record.band === OVERLAY_BAND.BELOW ) {
        x = footX - Math.trunc(record.width / 2);
        y = footY;
    }
    return Object.freeze({ ...base, ok: true, x: x - originX, y: y - originY });
}

/**
 * Resolve one record's parent-relative box through an optional scene adapter.
 * No provider (or a provider returning null) is the native no-world answer:
 * subject_live=false, ok=false, and no fabricated x/y. A live but off-camera
 * provider should return `{subject_live:true, ok:false}` so callers hide rather
 * than reap it, matching app_overlay_anchor().
 */
export function resolveOverlayLayout(state, index, adapters = {}) {
    assertState(state);
    index = int32(index);
    const record = index !== null && index >= 0 && index < OVERLAY_LIMITS.records
        ? state.items[index] : null;
    if( !record ) return projectionResult(false, null, null);
    const callback = projectionCallback(record, adapters) ?? adapters.projectAnchor;
    if( typeof callback !== 'function' ) return projectionResult(true, record, null);
    const raw = callback({ overlay_index: index, record: cloneRecord(record) });
    return projectionResult(true, record, raw);
}

/** Fixed-width, deterministic, JSON-serializable registry snapshot. */
export function snapshotOverlayState(state) {
    assertState(state);
    return {
        items: Array.from({ length: OVERLAY_LIMITS.records }, (_, index) =>
            cloneRecord(state.items[index])),
    };
}
