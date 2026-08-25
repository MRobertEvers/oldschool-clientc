/*
 * Browser-side mirror of the subject/session handlers in rs_cs2_host.c:
 * client-op context reads, scene subject lookup, and the active-player route.
 *
 * State is deliberately plain JSON data. Scene-owned answers remain optional
 * synchronous callbacks, matching the native host's callback boundary.
 */

const SUBJECT_KINDS = Object.freeze(['npc', 'loc', 'obj', 'player', 'tile']);

const SUBJECT_REQUEST_NAME_LIST = Object.freeze([
    '_6750', '_6751', '_6752', '_6753',
    '_6800', '_6801', '_6802', 'LOC_FIND',
    '_6850', '_6851', '_6852', '_6853',
    '_6900', 'ACTIVEPLAYER_SETLOCAL', 'ACTIVEPLAYER_GETROUTELENGTH',
    'ACTIVEPLAYER_GETROUTECOORD', 'ACTIVEPLAYER_GETUID', 'LOCALPLAYER_GETUID',
    '_6950', 'COORD_INSCENE',
]);

const CONTEXT_GETTERS = Object.freeze({
    _6750: ['npc', 'name'],
    _6751: ['npc', 'uid'],
    _6752: ['npc', 'coord'],
    _6753: ['npc', 'type'],
    _6800: ['loc', 'name'],
    _6801: ['loc', 'coord'],
    _6802: ['loc', 'type'],
    _6850: ['obj', 'name'],
    _6851: ['obj', 'coord'],
    _6852: ['obj', 'type'],
    _6853: ['obj', 'count'],
    _6900: ['player', 'name'],
    _6950: ['tile', 'coord'],
});

/** The exact 20 generated HOST request names handled by this module. */
export const SUBJECT_REQUESTS = new Set(SUBJECT_REQUEST_NAME_LIST);

/** Construct an isolated, JSON-serializable subject/session state. */
export function createSubjectState(seed = {}) {
    const input = record(seed);
    const activeSeed = input.active;
    const active = {};
    for( let index = 0; index < SUBJECT_KINDS.length; index++ ) {
        const kind = SUBJECT_KINDS[index];
        const value = Array.isArray(activeSeed) ? activeSeed[index] : activeSeed?.[kind];
        active[kind] = normalizeContext(value, kind);
    }

    return {
        localPlayerUid: integer(input.localPlayerUid ?? input.localPid ?? input.local_pid, -1),
        localCoord: integer(input.localCoord ?? input.local_coord, 0),
        hoverCoord: integer(input.hoverCoord ?? input.hover_coord, -1),
        runningScriptId: integer(
            input.runningScriptId ?? input.running_script_id ?? input.rootScriptId, -1),
        dispatch: normalizeContext(input.dispatch ?? input.ctx),
        active,
        mouseover: normalizeContext(input.mouseover),
        locations: array(input.locations).map(normalizeLocation),
        sceneCoords: uniqueInts(input.sceneCoords ?? input.scene_coords),
        routes: normalizeRoutes(input.routes),
    };
}

/**
 * Execute one reflected subject/session HOST request.
 *
 * Optional synchronous providers mirror `RS_CS2Host` callbacks:
 *
 * - `locAtCoord(coord, locType)` returns null/false or `{ layer, name }`.
 * - `coordInScene(coord)` returns truthy when the packed coord is loaded.
 * - `playerRoute(uid, index)` returns `{ length, coord }`, a coord array, or
 *   just a numeric route length.
 * - `runningScriptId` supplies the root CS2 frame id used to gate dispatch.
 */
export function handleSubjectRequest(state, request, options = {}) {
    assertState(state);
    const kind = String(request?.kind || '').toUpperCase();
    if( !SUBJECT_REQUESTS.has(kind) )
        throw new Error(`unsupported subject HOST request ${kind || '<empty>'}`);

    const runningScriptId = integer(
        request.running_script_id ?? request.runningScriptId ??
        options.runningScriptId ?? state.runningScriptId,
        -1);
    let result;
    let changed = false;

    if( CONTEXT_GETTERS[kind] ) {
        const [subjectKind, field] = CONTEXT_GETTERS[kind];
        const subject = selectSubject(state, subjectKind, runningScriptId);
        result = field === 'name' ? subject?.name ?? '' : subject?.[field] ?? -1;
        /* Native _6950 falls back even when a selected tile has coord -1. */
        if( kind === '_6950' && result < 0 ) result = state.hoverCoord;
        return { result, changed };
    }

    switch( kind ) {
    case 'LOC_FIND': {
        const coord = integer(request.coord, 0);
        const locType = integer(request.loc_type ?? request.locType, -1);
        const found = findLocation(state, coord, locType, options);
        const next = found ? {
            kind: 'loc', scriptId: 0, uid: -1, type: locType, count: 0,
            layer: integer(found.layer, -1), coord, name: subjectName(found.name),
        } : null;
        changed = !sameContext(state.active.loc, next);
        state.active.loc = next;
        result = found ? 1 : 0;
        break;
    }

    case 'COORD_INSCENE': {
        const coord = integer(request.coord, 0);
        if( typeof options.coordInScene === 'function' )
            result = options.coordInScene(coord) ? 1 : 0;
        else result = state.sceneCoords.includes(coord) ? 1 : 0;
        break;
    }

    case 'ACTIVEPLAYER_SETLOCAL':
        if( state.localPlayerUid < 0 ) {
            /* Native returns -1 and deliberately leaves an old register alone. */
            result = -1;
            break;
        }
        {
            const next = {
                kind: 'player', scriptId: -1, uid: state.localPlayerUid,
                type: -1, count: 0, layer: -1, coord: state.localCoord, name: '',
            };
            changed = !sameContext(state.active.player, next);
            state.active.player = next;
            result = 1;
        }
        break;

    case 'ACTIVEPLAYER_GETUID':
        result = selectSubject(state, 'player', runningScriptId)?.uid ?? -1;
        break;

    case 'LOCALPLAYER_GETUID':
        result = state.localPlayerUid;
        break;

    case 'ACTIVEPLAYER_GETROUTELENGTH':
    case 'ACTIVEPLAYER_GETROUTECOORD': {
        const subject = selectSubject(state, 'player', runningScriptId);
        const uid = subject?.uid ?? -1;
        const index = kind === 'ACTIVEPLAYER_GETROUTECOORD'
            ? integer(request.index, -1) : -1;
        const route = uid >= 0 ? resolveRoute(state, uid, index, options) : null;
        const length = route && route.length >= 0 ? route.length : 0;
        result = kind === 'ACTIVEPLAYER_GETROUTELENGTH'
            ? length : route?.coord ?? -1;
        break;
    }
    }

    return { result, changed };
}

function selectSubject(state, kind, runningScriptId) {
    const dispatch = state.dispatch;
    if( dispatch?.kind === kind && dispatch.scriptId > 0 &&
        dispatch.scriptId === runningScriptId ) return dispatch;
    if( state.active[kind]?.kind === kind ) return state.active[kind];
    if( state.mouseover?.kind === kind ) return state.mouseover;
    return null;
}

function findLocation(state, coord, locType, options) {
    let found;
    if( typeof options.locAtCoord === 'function' )
        found = options.locAtCoord(coord, locType);
    else found = state.locations.find((loc) => loc.coord === coord && loc.type === locType);
    if( !found || (typeof found === 'object' && found.hit === false) ) return null;
    return found === true ? {} : record(found);
}

function resolveRoute(state, uid, index, options) {
    let answer;
    if( typeof options.playerRoute === 'function' ) answer = options.playerRoute(uid, index);
    else answer = state.routes[String(uid)];

    if( Array.isArray(answer) ) return {
        length: answer.length | 0,
        coord: index >= 0 && index < answer.length ? integer(answer[index], -1) : -1,
    };
    if( answer && typeof answer === 'object' ) {
        const coords = Array.isArray(answer.coords) ? answer.coords : null;
        return {
            length: integer(answer.length, coords ? coords.length : -1),
            coord: answer.coord !== undefined ? integer(answer.coord, -1)
                : coords && index >= 0 && index < coords.length ? integer(coords[index], -1) : -1,
        };
    }
    if( answer !== undefined && answer !== null ) return {
        length: integer(answer, -1), coord: -1,
    };
    return { length: -1, coord: -1 };
}

function normalizeContext(value, forcedKind) {
    if( !value || typeof value !== 'object' || Array.isArray(value) ) return null;
    const input = value;
    const kind = normalizeKind(forcedKind ?? input.kind);
    if( !kind ) return null;
    return {
        kind,
        scriptId: integer(input.scriptId ?? input.script_id, -1),
        uid: integer(input.uid, -1),
        type: integer(input.type, -1),
        count: integer(input.count, -1),
        layer: integer(input.layer, -1),
        coord: integer(input.coord, -1),
        name: subjectName(input.name),
    };
}

function normalizeKind(value) {
    if( Number.isInteger(value) ) return SUBJECT_KINDS[value] ?? null;
    const kind = String(value ?? '').toLowerCase();
    return SUBJECT_KINDS.includes(kind) ? kind : null;
}

function normalizeLocation(value) {
    const input = record(value);
    return {
        coord: integer(input.coord, -1),
        type: integer(input.type ?? input.locType ?? input.loc_type, -1),
        layer: integer(input.layer, -1),
        name: subjectName(input.name),
    };
}

function normalizeRoutes(value) {
    const input = record(value);
    const result = {};
    for( const [rawUid, rawRoute] of Object.entries(input) ) {
        const uid = integer(rawUid, -1);
        if( uid < 0 ) continue;
        const coords = Array.isArray(rawRoute) ? rawRoute
            : Array.isArray(rawRoute?.coords) ? rawRoute.coords : [];
        result[String(uid)] = coords.map((coord) => integer(coord, -1));
    }
    return result;
}

function subjectName(value) {
    /* RS_CLIENTOP_NAME_MAX is 64 bytes. Cache subject names are ASCII. */
    return String(value ?? '').slice(0, 63);
}

function sameContext(left, right) {
    if( left === right ) return true;
    if( !left || !right ) return false;
    return left.kind === right.kind && left.scriptId === right.scriptId &&
        left.uid === right.uid && left.type === right.type && left.count === right.count &&
        left.layer === right.layer && left.coord === right.coord && left.name === right.name;
}

function uniqueInts(value) {
    const result = [];
    for( const entry of array(value) ) {
        const coord = integer(entry, -1);
        if( !result.includes(coord) ) result.push(coord);
    }
    return result;
}

function assertState(state) {
    if( !state || !state.active || !Array.isArray(state.locations) ||
        !Array.isArray(state.sceneCoords) || !state.routes ||
        SUBJECT_KINDS.some((kind) => !(kind in state.active)) )
        throw new Error('subject state must come from createSubjectState()');
}

function integer(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) | 0 : fallback | 0;
}

function record(value) {
    return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
}

function array(value) {
    return Array.isArray(value) ? value : [];
}
