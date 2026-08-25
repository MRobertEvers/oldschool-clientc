/*
 * Browser-side mirror of src/game/rs_loot_store.c and exec_loot().
 *
 * The state intentionally contains arrays and plain objects only: callers may
 * persist it with JSON and seed a later preview session with that value.  The
 * handler mutates the supplied state in place and reports whether its visible
 * contents changed.
 */

const INT_MIN = -0x80000000;
const AUX_KIND_COUNT = 5;

const LOOT_REQUEST_NAME_LIST = Object.freeze([
    'LOOT_AUX_UPSERT2', 'LOOT_AUX_UPSERT', 'LOOT_AUX_REMOVE', 'LOOT_AUX_GET',
    'LOOT_AUX_COUNT', 'LOOT_AUX_LOOKUP', 'LOOT_AUX_CLEAR',
    'LOOT_SOURCE_COUNT', 'LOOT_SOURCE_NAME', 'LOOT_SOURCE_ITEMCOUNT',
    'LOOT_SOURCE_TOTALVAL', 'LOOT_BEGIN_QUERY', 'LOOT_QUERY_ID',
    'LOOT_AUX_COUNT_TOTAL', 'LOOT_ROW_COUNT_BYNAME', 'LOOT_ROW_COUNT_BYID',
    'LOOT_ROW_BYNAME', 'LOOT_ROW_BYID', 'LOOT_CLEAR_ALL', 'LOOT_CLEAR_SOURCE',
    'LOOT_REMOVE_BYID', 'LOOT_IGNORE_ADD', 'LOOT_IGNORE_REMOVE',
    'LOOT_GROUND_COUNT', 'LOOT_GROUND_NAME', 'LOOT_IGNORE_CLEAR',
    'LOOT_SOURCE_IGNORE_ADD', 'LOOT_SOURCE_IGNORE_REMOVE',
    'LOOT_SRCLIST_COUNT', 'LOOT_SRCLIST_NAME', 'LOOT_ADD', 'LOOT_SOURCE_NAME2',
]);

/** The exact 32 generated HOST request names handled by this module. */
export const LOOT_REQUESTS = new Set(LOOT_REQUEST_NAME_LIST);

/**
 * Construct an isolated, JSON-serializable loot store.
 *
 * Both camelCase keys emitted by this module and native-style snake_case seed
 * keys are accepted.  Arrays are cloned, so mutating the returned state never
 * mutates the seed object.
 */
export function createLootState(seed = {}) {
    const input = record(seed);
    const sources = list(input.sources).map((source) => normalizeSource(source));
    const highestId = sources.reduce((highest, source) => Math.max(highest, source.id), 0);
    const seededNextId = intSeed(input.nextSourceId ?? input.next_source_id, highestId + 1);
    const auxSeed = input.aux;

    return {
        sources,
        nextSourceId: Math.max(highestId + 1, seededNextId),
        nextEventId: intSeed(input.nextEventId ?? input.next_event_id, 1),
        itemIgnored: uniqueStrings(input.itemIgnored ?? input.item_ignored),
        sourceIgnored: uniqueStrings(input.sourceIgnored ?? input.source_ignored),
        aux: Array.from({ length: AUX_KIND_COUNT }, (_, kind) =>
            uniqueStrings(Array.isArray(auxSeed) ? auxSeed[kind] : auxSeed?.[kind])),
        queryIds: list(input.queryIds ?? input.query_ids).map((value) => intSeed(value, 0)),
    };
}

/**
 * Execute one reflected C HOST request against `state`.
 *
 * `options.objectCost(id)` is the cache lookup used by LOOT_ADD.  Returning
 * null/undefined means that the object is absent and selects the native
 * fallback cost of 1.  A numeric zero is a real cached cost and is preserved.
 */
export function handleLootRequest(state, request, options = {}) {
    assertState(state);
    const kind = String(request?.kind || '').toUpperCase();
    if( !LOOT_REQUESTS.has(kind) )
        throw new Error(`unsupported loot HOST request ${kind || '<empty>'}`);

    const args = Array.isArray(request.int_args)
        ? request.int_args.map((value) => intSeed(value, 0))
        : Array.isArray(request.intArgs)
            ? request.intArgs.map((value) => intSeed(value, 0))
            : [];
    const arg = (index) => args[index] ?? 0;
    const name = request.name == null ? '' : String(request.name);
    let result;
    let changed = false;

    switch( kind ) {
    case 'LOOT_AUX_UPSERT2':
    case 'LOOT_AUX_UPSERT':
        changed = auxUpsert(state, arg(0), name);
        break;
    case 'LOOT_AUX_REMOVE':
        changed = auxRemove(state, arg(0), name);
        break;
    case 'LOOT_AUX_GET':
        result = auxGet(state, arg(0), arg(1));
        break;
    case 'LOOT_AUX_COUNT':
        result = validAuxKind(arg(0)) ? state.aux[arg(0)].length : 0;
        break;
    case 'LOOT_AUX_LOOKUP':
        /* int_args[1] and [2] are deliberately ignored by the C store. */
        result = validAuxKind(arg(0)) && state.aux[arg(0)].includes(name) ? 1 : 0;
        break;
    case 'LOOT_AUX_CLEAR':
        if( validAuxKind(arg(0)) && state.aux[arg(0)].length ) {
            state.aux[arg(0)] = [];
            changed = true;
        }
        break;

    case 'LOOT_SOURCE_COUNT':
        result = state.sources.length;
        break;
    case 'LOOT_SOURCE_NAME':
    case 'LOOT_SOURCE_NAME2':
        result = sourceById(state, arg(0))?.name ?? '';
        break;
    case 'LOOT_SOURCE_ITEMCOUNT':
    case 'LOOT_ROW_COUNT_BYNAME':
        result = sourceByName(state, name)?.rows.length ?? 0;
        break;
    case 'LOOT_SOURCE_TOTALVAL':
        /* Despite the historical opcode name, native returns kill_count. */
        result = sourceByName(state, name)?.killCount ?? 0;
        break;
    case 'LOOT_BEGIN_QUERY': {
        const ids = beginQuery(state, arg(0), arg(1), arg(2));
        changed = !sameArray(state.queryIds, ids);
        state.queryIds = ids;
        result = ids.length;
        break;
    }
    case 'LOOT_QUERY_ID':
        result = arg(0) >= 0 && arg(0) < state.queryIds.length
            ? state.queryIds[arg(0)] : -1;
        break;
    case 'LOOT_AUX_COUNT_TOTAL':
        result = state.aux.reduce((total, entries) => total + entries.length, 0);
        break;
    case 'LOOT_ROW_COUNT_BYID':
        result = sourceById(state, arg(0))?.rows.length ?? 0;
        break;
    case 'LOOT_ROW_BYNAME':
        result = rowResult(sourceByName(state, name), arg(0));
        break;
    case 'LOOT_ROW_BYID':
        result = rowResult(sourceById(state, arg(0)), arg(1));
        break;
    case 'LOOT_CLEAR_ALL':
        changed = state.sources.length !== 0 || state.queryIds.length !== 0;
        state.sources = [];
        state.queryIds = [];
        break;
    case 'LOOT_CLEAR_SOURCE':
        changed = removeSwap(state.sources, (source) => source.name === name);
        break;
    case 'LOOT_REMOVE_BYID':
        changed = removeSwap(state.sources, (source) => source.id === arg(0));
        break;

    case 'LOOT_IGNORE_ADD':
        changed = stringAdd(state.itemIgnored, name);
        break;
    case 'LOOT_IGNORE_REMOVE':
        changed = removeSwap(state.itemIgnored, (entry) => entry === name);
        break;
    case 'LOOT_GROUND_COUNT':
        result = state.itemIgnored.length;
        break;
    case 'LOOT_GROUND_NAME':
        result = oneBasedString(state.itemIgnored, arg(0));
        break;
    case 'LOOT_IGNORE_CLEAR':
        changed = state.itemIgnored.length !== 0;
        state.itemIgnored = [];
        break;
    case 'LOOT_SOURCE_IGNORE_ADD':
        changed = stringAdd(state.sourceIgnored, name);
        break;
    case 'LOOT_SOURCE_IGNORE_REMOVE':
        changed = removeSwap(state.sourceIgnored, (entry) => entry === name);
        break;
    case 'LOOT_SRCLIST_COUNT':
        result = state.sourceIgnored.length;
        break;
    case 'LOOT_SRCLIST_NAME':
        result = oneBasedString(state.sourceIgnored, arg(0));
        break;

    case 'LOOT_ADD': {
        /* Reflected int_args retain native pop order: event, quantity, object. */
        const objectCost = typeof options === 'function' ? options : options.objectCost;
        const lookedUp = typeof objectCost === 'function' ? objectCost(arg(2)) : undefined;
        const cost = lookedUp == null ? 1 : intSeed(lookedUp, 1);
        changed = addKillLoot(state, name, arg(2), arg(1), cost, arg(0));
        break;
    }
    }

    return { result, changed };
}

function addKillLoot(state, name, objId, qty, cost, eventId) {
    let source = sourceByName(state, name);
    let changed = false;
    if( !source ) {
        source = {
            id: state.nextSourceId | 0,
            name,
            rows: [],
            killCount: 0,
            lastEventId: INT_MIN,
        };
        state.nextSourceId = (state.nextSourceId + 1) | 0;
        state.sources.push(source);
        changed = true;
    }

    if( eventId !== source.lastEventId ) {
        source.killCount = (source.killCount + 1) | 0;
        source.lastEventId = eventId;
        changed = true;
    }

    const row = source.rows.find((entry) => entry.objId === objId);
    const deltaValue = Math.imul(cost, qty);
    if( row ) {
        const nextQty = (row.qty + qty) | 0;
        const nextValue = (row.value + deltaValue) | 0;
        changed ||= nextQty !== row.qty || nextValue !== row.value;
        row.qty = nextQty;
        row.value = nextValue;
    } else {
        source.rows.push({ objId, qty, value: deltaValue });
        changed = true;
    }
    return changed;
}

function beginQuery(state, rawStart, rawLimit, kind) {
    if( kind !== 1 && kind !== 2 && kind !== 3 ) return [];
    const total = state.sources.length;
    const start = Math.max(0, rawStart);
    if( start >= total ) return [];
    /* The WASM build performs this native `int` addition as i32.add. */
    const end = Math.min(total, (start + rawLimit) | 0);
    const count = end - start;
    if( count <= 0 ) return [];
    return state.sources.slice(start, end).map((source) => source.id);
}

function auxUpsert(state, kind, name) {
    if( !validAuxKind(kind) ) return false;
    return stringAdd(state.aux[kind], name);
}

function auxRemove(state, kind, name) {
    if( !validAuxKind(kind) ) return false;
    return removeSwap(state.aux[kind], (entry) => entry === name);
}

function auxGet(state, kind, index) {
    if( !validAuxKind(kind) || index < 0 || index >= state.aux[kind].length ) return '';
    return state.aux[kind][index] ?? '';
}

function validAuxKind(kind) {
    return Number.isInteger(kind) && kind >= 0 && kind < AUX_KIND_COUNT;
}

function sourceByName(state, name) {
    return state.sources.find((source) => source.name === name);
}

function sourceById(state, id) {
    return state.sources.find((source) => source.id === id);
}

function rowResult(source, index1Based) {
    const index = index1Based - 1;
    const row = source && index >= 0 && index < source.rows.length ? source.rows[index] : null;
    return row ? [row.objId, row.qty] : [0, 0];
}

function oneBasedString(entries, index) {
    return index >= 1 && index <= entries.length ? entries[index - 1] ?? '' : '';
}

function stringAdd(entries, name) {
    if( entries.includes(name) ) return false;
    entries.push(name);
    return true;
}

/* Native removals fill the hole with the final element; they do not splice. */
function removeSwap(entries, predicate) {
    const index = entries.findIndex(predicate);
    if( index < 0 ) return false;
    const last = entries.pop();
    if( index < entries.length ) entries[index] = last;
    return true;
}

function sameArray(left, right) {
    return left.length === right.length && left.every((value, index) => value === right[index]);
}

function normalizeSource(value) {
    const source = record(value);
    return {
        id: intSeed(source.id, 0),
        name: String(source.name ?? ''),
        rows: list(source.rows).map((row) => {
            const input = record(row);
            return {
                objId: intSeed(input.objId ?? input.obj_id, 0),
                qty: intSeed(input.qty, 0),
                value: intSeed(input.value, 0),
            };
        }),
        killCount: intSeed(source.killCount ?? source.kill_count, 0),
        lastEventId: intSeed(source.lastEventId ?? source.last_event_id, INT_MIN),
    };
}

function uniqueStrings(value) {
    const result = [];
    for( const entry of list(value) ) {
        const string = String(entry ?? '');
        if( !result.includes(string) ) result.push(string);
    }
    return result;
}

function assertState(state) {
    if( !state || !Array.isArray(state.sources) || !Array.isArray(state.itemIgnored) ||
        !Array.isArray(state.sourceIgnored) || !Array.isArray(state.queryIds) ||
        !Array.isArray(state.aux) || state.aux.length !== AUX_KIND_COUNT ||
        state.aux.some((entries) => !Array.isArray(entries)) )
        throw new Error('loot state must come from createLootState()');
}

function record(value) {
    return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
}

function list(value) {
    return Array.isArray(value) ? value : [];
}

function intSeed(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) | 0 : fallback | 0;
}
