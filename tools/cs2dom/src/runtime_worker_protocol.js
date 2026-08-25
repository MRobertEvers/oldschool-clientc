/*
 * Structured-clone protocol shared by the browser preview and its runtime
 * worker.  Keep this file free of DOM and Worker globals: the same projection
 * and delta code is exercised in Node unit tests.
 */

export const RUNTIME_WORKER_SCHEMA = 'cs2dom-runtime-worker/1';
export const RUNTIME_WORKER_TREE_CHUNK = 64;
export const RUNTIME_WORKER_STAGE_CHUNK = 32;
export const RUNTIME_WORKER_STAGE_SLICE_MS = 3;
export const RUNTIME_WORKER_STAGE_SLICE_ITEMS = 64;

const GRAPHIC_TYPE = 5;

/**
 * The stage paints only visible/emitted widgets.  In particular, a bank can
 * contain well over a thousand GRAPHIC cells with sprite=-1; sending those on
 * every interaction turns structured-clone itself into main-thread work.
 * Wireframe diagnostics obtain those records from the separately streamed
 * tree, rather than expanding the latency-sensitive stage message.
 */
export function paintableStageBox(box) {
    if( !box || box.emitted === false || box.effectiveHidden || box.culled ) return false;
    return box.type !== GRAPHIC_TYPE || Number(box.props?.sprite) >= 0;
}

export function stageBoxKey(box, index = 0) {
    const ref = box?.ref;
    if( ref?.key ) return String(ref.key);
    const component = Number(ref?.componentId ?? box?.componentId ?? -1);
    const sub = Number(ref?.subId ?? box?.subId ?? -1);
    const file = Number(box?.fileId ?? ref?.fileId ?? index);
    return `${component}:${sub}:${file}`;
}

/** Build the compact, ordered stage projection directly from HostRuntime.layout(). */
export function projectStage(layout, viewport, version) {
    const entries = [];
    for( let index = 0; index < (layout?.length || 0); index++ ) {
        const box = layout[index];
        if( !paintableStageBox(box) ) continue;
        entries.push({ key: stageBoxKey(box, index), box });
    }
    return {
        version: Number(version) || 0,
        viewport: {
            width: Math.max(1, Number(viewport?.width) || 512),
            height: Math.max(1, Number(viewport?.height) || 334),
        },
        entries,
    };
}

/**
 * Produce a stage delta. Signatures live only in the worker and are never sent
 * across the boundary. The materialized `render` assembled by the controller
 * means callers may ignore the delta if a keyed reconciler is already cheap.
 */
export function diffStage(previous, next, { reset = false } = {}) {
    const before = new Map((previous?.entries || []).map((entry) => [entry.key, entry]));
    const after = new Map((next?.entries || []).map((entry) => [entry.key, entry]));
    const upsert = [];
    const remove = [];

    for( const entry of next?.entries || [] ) {
        const old = before.get(entry.key);
        if( reset || !old || !stageBoxesEqual(old.box, entry.box) )
            upsert.push(entry);
    }
    if( !reset ) for( const key of before.keys() ) if( !after.has(key) ) remove.push(key);

    const order = next.entries.map((entry) => entry.key);
    const previousOrder = (previous?.entries || []).map((entry) => entry.key);
    const orderChanged = Boolean(reset) || order.length !== previousOrder.length ||
        order.some((key, index) => key !== previousOrder[index]);

    return {
        version: next.version,
        viewport: next.viewport,
        reset: Boolean(reset),
        upsert,
        remove,
        order,
        orderChanged,
    };
}

/** Apply a worker delta and expose the plain render shape consumed by drawStage. */
export function applyStagePatch(previous, patch) {
    const boxes = patch.reset
        ? new Map()
        : new Map((previous?.entries || []).map((entry) => [entry.key, entry.box]));
    for( const key of patch.remove || [] ) boxes.delete(key);
    for( const entry of patch.upsert || [] ) boxes.set(entry.key, entry.box);
    const entries = [];
    const order = patch.orderChanged === false
        ? (previous?.entries || []).map((entry) => entry.key) : (patch.order || []);
    for( const key of order ) {
        const box = boxes.get(key);
        if( box ) entries.push({ key, box });
    }
    return {
        version: Number(patch.version) || 0,
        viewport: { ...patch.viewport },
        entries,
        boxes: entries.map((entry) => entry.box),
    };
}

/**
 * Split every clone-heavy part of a stage delta. The order vector is chunked
 * too: sending 4,096 small keys as one record still creates an unbounded
 * browser message task even when only one widget changed.
 */
export function chunkStagePatch(patch, transaction, chunkSize = RUNTIME_WORKER_STAGE_CHUNK) {
    const operations = [];
    for( const key of patch.remove || [] ) operations.push([0, key]);
    for( const entry of patch.upsert || [] ) operations.push([1, entry]);
    if( patch.orderChanged ) for( const key of patch.order || [] ) operations.push([2, key]);
    const size = Math.max(1, Math.min(256, Number(chunkSize) || RUNTIME_WORKER_STAGE_CHUNK));
    const total = Math.max(1, Math.ceil(operations.length / size));
    return Array.from({ length: total }, (_, index) => ({
        transaction,
        index,
        total,
        version: patch.version,
        viewport: patch.viewport,
        reset: Boolean(patch.reset),
        orderChanged: Boolean(patch.orderChanged),
        operations: operations.slice(index * size, (index + 1) * size),
        done: index === total - 1,
    }));
}

export function workerMessage(type, session, fields = {}) {
    return { schema: RUNTIME_WORKER_SCHEMA, type, session, ...fields };
}

export function validWorkerMessage(value) {
    return Boolean(value && value.schema === RUNTIME_WORKER_SCHEMA &&
        typeof value.type === 'string' && Number.isSafeInteger(value.session));
}

export function stageBoxSignature(box) {
    /* Layout boxes are ordinary records. JSON is intentionally paid in the
     * worker's bounded stage slices, after blank graphics have been elided. */
    return JSON.stringify([
        box.x, box.y, box.w, box.h, box.clip, box.props, box.presentation,
        box.emitted, box.effectiveHidden, box.culled, box.depth, box.layer,
        box.dynamic, box.ops, box.events,
    ]);
}

/**
 * Compare exactly the fields which can change React paint. Geometry and clip
 * reject first, avoiding JSON allocation for the common resize/position path;
 * the remaining cache records are small ordinary arrays/objects.
 */
export function stageBoxesEqual(left, right) {
    if( left === right ) return true;
    if( !left || !right || left.type !== right.type ||
        left.x !== right.x || left.y !== right.y ||
        left.w !== right.w || left.h !== right.h || left.emitted !== right.emitted ||
        left.effectiveHidden !== right.effectiveHidden || left.culled !== right.culled ||
        left.depth !== right.depth || left.layer !== right.layer ) return false;
    return stageValueEqual(left.clip, right.clip) &&
        stageValueEqual(left.props, right.props) &&
        stageValueEqual(left.presentation, right.presentation) &&
        stageValueEqual(left.dynamic, right.dynamic) &&
        stageValueEqual(left.ops, right.ops) &&
        stageValueEqual(left.events, right.events);
}

function stageValueEqual(left, right) {
    if( left === right ) return true;
    if( Number.isNaN(left) && Number.isNaN(right) ) return true;
    if( !left || !right || typeof left !== 'object' || typeof right !== 'object' ) return false;
    const leftArray = Array.isArray(left);
    if( leftArray !== Array.isArray(right) ) return false;
    if( leftArray ) {
        if( left.length !== right.length ) return false;
        for( let index = 0; index < left.length; index++ )
            if( !stageValueEqual(left[index], right[index]) ) return false;
        return true;
    }
    const keys = Object.keys(left);
    if( keys.length !== Object.keys(right).length ) return false;
    for( const key of keys )
        if( !Object.prototype.hasOwnProperty.call(right, key) ||
            !stageValueEqual(left[key], right[key]) ) return false;
    return true;
}
