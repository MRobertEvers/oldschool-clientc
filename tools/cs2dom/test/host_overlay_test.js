import assert from 'node:assert/strict';

import {
    OVERLAY_ANCHOR,
    OVERLAY_REQUEST_NAMES,
    OVERLAY_SOURCE_COORD_IGNORED,
    countOverlays,
    createOverlayState,
    destroyEntityOverlays,
    destroyOverlay,
    getOverlay,
    handleOverlayRequest,
    resolveOverlayLayout,
    snapshotOverlayState,
} from '../src/host_overlay.js';

const QUERY_REQUESTS = new Set([
    'OVERLAY_FIND', 'OVERLAY_CC_FIND',
    'OVERLAY_NPC_CREATE', 'OVERLAY_LOC_CREATE', 'OVERLAY_PLAYER_CREATE',
    'OVERLAY_COORD_CREATE', 'OVERLAY_NPC_GET', 'OVERLAY_LOC_GET',
    'OVERLAY_PLAYER_GET', 'OVERLAY_COORD_GET',
]);

function request(state, name, args, adapters = {}, extra = {}) {
    return handleOverlayRequest(state, name, {
        args,
        arg_count: args.length,
        query: QUERY_REQUESTS.has(name),
        dot_operand: 0,
        ...extra,
    }, adapters);
}

assert.equal(OVERLAY_REQUEST_NAMES.size, 16);
assert.equal(OVERLAY_SOURCE_COORD_IGNORED, true);
for( const name of [
    'OVERLAY_CC_CREATE', 'OVERLAY_CC_DELETEALL', 'OVERLAY_FIND', 'OVERLAY_CC_FIND',
    'OVERLAY_NPC_CREATE', 'OVERLAY_LOC_CREATE', 'OVERLAY_PLAYER_CREATE',
    'OVERLAY_COORD_CREATE', 'OVERLAY_NPC_GET', 'OVERLAY_LOC_GET', 'OVERLAY_PLAYER_GET',
    'OVERLAY_COORD_GET', 'OVERLAY_NPC_DESTROY', 'OVERLAY_LOC_DESTROY',
    'OVERLAY_PLAYER_DESTROY', 'OVERLAY_COORD_DESTROY',
]) assert(OVERLAY_REQUEST_NAMES.has(name), `${name} missing`);
assert.throws(() => OVERLAY_REQUEST_NAMES.add('OVERLAY_FAKE'), TypeError);
OVERLAY_REQUEST_NAMES.forEach((_name, _again, names) =>
    assert.throws(() => names.clear(), TypeError));

/* No active-scene provider means exactly no entity/loc subject. */
const headless = createOverlayState();
let result = request(headless, 'OVERLAY_NPC_CREATE', [5, 1, 60, 60, 1]);
assert.equal(result.value, -1);
assert.equal(result.changed, false);
assert.equal(request(headless, 'OVERLAY_NPC_GET', [5]).value, -1);
assert.equal(request(headless, 'OVERLAY_PLAYER_CREATE', [5, 1, 60, 60, 1]).value, -1);
assert.equal(request(headless, 'OVERLAY_LOC_CREATE', [5, 1, 60, 60, 1]).value, -1);
assert.equal(request(headless, 'OVERLAY_NPC_DESTROY', [5]).changed, false);
assert.equal(request(headless, 'OVERLAY_LOC_DESTROY', [5]).changed, false);
assert.equal(request(headless, 'OVERLAY_PLAYER_DESTROY', [5]).changed, false);

/* A bare coord needs no scene or tree. Headless native state retains the
 * record with component_id=-1, so GET still succeeds while layer FIND does not. */
result = request(headless, 'OVERLAY_COORD_CREATE', [12345, 7, 1, 60, 40, 0]);
assert.equal(result.value, 0);
assert.equal(result.changed, true);
assert.deepEqual(getOverlay(headless, 0), {
    in_use: true,
    slot: 7,
    anchor: OVERLAY_ANCHOR.STATIC,
    uid: -1,
    coord: 12345,
    static_type: 4,
    band: 1,
    width: 60,
    height: 40,
    component_id: -1,
});
assert.equal(request(headless, 'OVERLAY_COORD_GET', [12345, 7]).value, 0);
assert.equal(request(headless, 'OVERLAY_FIND', [0]).value, 0);

/* source_coord is popped/reflected but current C parity ignores it entirely:
 * it is absent from the record and cannot change identity or replacement. */
result = request(headless, 'OVERLAY_COORD_CREATE', [12345, 7, 1, 60, 40, 1]);
assert.equal(result.value, 0);
assert.equal(result.changed, false);
assert.equal(Object.hasOwn(getOverlay(headless, 0), 'source_coord'), false);

/* Replacement destroys the matching record before taking the first free one,
 * so a lower hole can move an overlay's index. */
assert.equal(request(headless, 'OVERLAY_COORD_CREATE', [20000, 0, 0, 1, 1, 0]).value, 1);
assert.equal(request(headless, 'OVERLAY_COORD_CREATE', [30000, 0, 0, 1, 1, 0]).value, 2);
assert.equal(request(headless, 'OVERLAY_COORD_DESTROY', [20000, 0]).changed, true);
assert.equal(request(headless, 'OVERLAY_COORD_CREATE', [30000, 0, 2, 5, 6, 1]).value, 1);
assert.equal(getOverlay(headless, 2), null);
assert.equal(getOverlay(headless, 1).coord, 30000);
assert.equal(request(headless, 'OVERLAY_COORD_DESTROY', [99999, 0]).changed, false);

/* Entity kinds and loc layers are disjoint parts of identity. */
const subjects = {
    npc: { uid: 44 },
    player: { uid: 44 },
    loc: { coord: 0x12345678, layer: 0 },
};
const subjectAdapters = {
    resolveNpcSubject: () => subjects.npc,
    resolvePlayerSubject: () => subjects.player,
    resolveLocSubject: () => subjects.loc,
};
const identityState = createOverlayState();
const npcIndex = request(identityState, 'OVERLAY_NPC_CREATE', [2, 1, 10, 11, 0],
    subjectAdapters).value;
const playerIndex = request(identityState, 'OVERLAY_PLAYER_CREATE', [2, 1, 10, 11, 0],
    subjectAdapters).value;
const wallIndex = request(identityState, 'OVERLAY_LOC_CREATE', [2, 1, 10, 11, 0],
    subjectAdapters).value;
subjects.loc = { coord: 0x12345678, layer: 3 };
const decorIndex = request(identityState, 'OVERLAY_LOC_CREATE', [2, 1, 10, 11, 0],
    subjectAdapters).value;
assert.equal(new Set([npcIndex, playerIndex, wallIndex, decorIndex]).size, 4);
assert.equal(request(identityState, 'OVERLAY_NPC_GET', [2], subjectAdapters).value, npcIndex);
assert.equal(request(identityState, 'OVERLAY_PLAYER_GET', [2], subjectAdapters).value,
    playerIndex);
assert.equal(request(identityState, 'OVERLAY_LOC_GET', [2], subjectAdapters).value, decorIndex);
subjects.loc.layer = 5;
assert.equal(request(identityState, 'OVERLAY_LOC_CREATE', [9, 0, 1, 1, 0],
    subjectAdapters).value, -1, 'native IsTypeValid rejects loc layers outside 0..4');

/* Full means refuse, never evict; replacing an existing identity still works
 * because its own record is freed before the first-free scan. */
const full = createOverlayState();
for( let index = 0; index < 640; index++ )
    assert.equal(request(full, 'OVERLAY_COORD_CREATE', [index, 0, 0, 1, 1, 0]).value, index);
assert.equal(countOverlays(full), 640);
assert.equal(request(full, 'OVERLAY_COORD_CREATE', [700, 0, 0, 1, 1, 0]).value, -1);
assert.equal(countOverlays(full), 640);
assert.equal(request(full, 'OVERLAY_COORD_CREATE', [25, 0, 2, 8, 9, 1]).value, 25);
assert.equal(getOverlay(full, 25).band, 2);
assert.equal(countOverlays(full), 640);

/* UITree adapters receive exact reflected fields and own all dynamic nodes. */
let nextComponentId = 1000;
const components = new Set();
const layerByOverlay = new Map();
const children = new Map();
const calls = [];
const treeAdapters = {
    ...subjectAdapters,
    createLayer(payload) {
        calls.push(['createLayer', payload]);
        const old = layerByOverlay.get(payload.overlay_index);
        if( old !== undefined ) components.delete(old);
        const component_id = nextComponentId++;
        layerByOverlay.set(payload.overlay_index, component_id);
        components.add(component_id);
        return { component_id };
    },
    deleteLayer(payload) {
        calls.push(['deleteLayer', payload]);
        components.delete(payload.component_id);
        layerByOverlay.delete(payload.overlay_index);
    },
    hasComponent: ({ component_id }) => components.has(component_id),
    createChild(payload) {
        calls.push(['createChild', payload]);
        const component_id = nextComponentId++;
        components.add(component_id);
        children.set(`${payload.parent_component_id}:${payload.child_index}`, component_id);
        return { component_id, changed: true };
    },
    findChild(payload) {
        const component_id = children.get(
            `${payload.parent_component_id}:${payload.child_index}`) ?? -1;
        return { component_id };
    },
    deleteAllChildren(payload) {
        let changed = false;
        for( const [key, componentId] of [...children] ) {
            if( key.startsWith(`${payload.parent_component_id}:`) ) {
                children.delete(key);
                components.delete(componentId);
                changed = true;
            }
        }
        calls.push(['deleteAllChildren', payload]);
        return changed;
    },
    setActiveComponent(payload) {
        calls.push(['setActiveComponent', payload]);
    },
};

const treeState = createOverlayState();
result = request(treeState, 'OVERLAY_NPC_CREATE', [3, 1, 72, 64, 1], treeAdapters);
assert.equal(result.value, 0);
assert.equal(result.changed, true);
assert.equal(getOverlay(treeState, 0).component_id, 1000);
assert.equal(calls[0][1].source_coord, 1, 'ignored source_coord remains diagnostic metadata');
result = request(treeState, 'OVERLAY_FIND', [0], treeAdapters, { dot_operand: 1 });
assert.equal(result.value, 1);
assert.deepEqual(result.target, { component_id: 1000, dot_operand: 1 });

result = request(treeState, 'OVERLAY_CC_CREATE', [0, 5, 0], treeAdapters, {
    dot_operand: 1,
});
assert.equal(result.ok, true);
assert.equal(result.changed, true);
assert.deepEqual(result.target, { component_id: 1001, dot_operand: 1 });
result = request(treeState, 'OVERLAY_CC_FIND', [0, 0], treeAdapters);
assert.equal(result.value, 1);
assert.equal(result.target.component_id, 1001);
assert.equal(request(treeState, 'OVERLAY_CC_FIND', [0, 7], treeAdapters).value, 0);
assert.equal(request(treeState, 'OVERLAY_CC_DELETEALL', [0], treeAdapters).changed, true);
assert.equal(request(treeState, 'OVERLAY_CC_DELETEALL', [0], treeAdapters).changed, false);
result = request(treeState, 'OVERLAY_CC_CREATE', [0, 0, 0], treeAdapters);
assert.equal(result.ok, false, 'native rejects a dynamic layer inside an overlay');
assert.equal(request(treeState, 'OVERLAY_NPC_DESTROY', [3], treeAdapters).changed, true);
assert(calls.some(([kind]) => kind === 'deleteLayer'));

const failedTree = createOverlayState();
const failedAdapters = {
    resolveNpcSubject: () => ({ uid: 2 }),
    createLayer: () => -1,
};
result = request(failedTree, 'OVERLAY_NPC_CREATE', [0, 0, 1, 1, 0], failedAdapters);
assert.equal(result.value, -1);
assert.equal(result.changed, false);
assert.equal(countOverlays(failedTree), 0, 'a layer attach failure releases its record');

/* Scene projection is explicit. Missing provider returns no target/geometry;
 * live-but-off-camera stays distinct from a subject that has gone. */
const layoutState = createOverlayState();
const layoutIndex = request(layoutState, 'OVERLAY_COORD_CREATE',
    [123, 1, 1, 60, 40, 0]).value;
assert.deepEqual(resolveOverlayLayout(layoutState, layoutIndex), {
    found: true,
    subject_live: false,
    ok: false,
    x: null,
    y: null,
    width: 60,
    height: 40,
    band: 1,
});
assert.equal(resolveOverlayLayout(layoutState, layoutIndex, {
    projectCoordAnchor: () => ({ subject_live: true, ok: false }),
}).subject_live, true);
assert.equal(resolveOverlayLayout(layoutState, layoutIndex, {
    projectCoordAnchor: () => ({
        subject_live: true,
        ok: true,
        top_x: 200,
        top_y: 100,
        mid_x: 200,
        mid_y: 140,
        foot_x: 200,
        foot_y: 180,
        origin_x: 20,
        origin_y: 30,
    }),
}).x, 150);
assert.equal(resolveOverlayLayout(layoutState, layoutIndex, {
    projectCoordAnchor: () => ({
        subject_live: true,
        ok: true,
        top_x: 200,
        top_y: 100,
        mid_x: 200,
        mid_y: 140,
        foot_x: 200,
        foot_y: 180,
        origin_x: 20,
        origin_y: 30,
    }),
}).y, 30, 'above uses top_y-height, then subtracts the world origin');
assert.equal(resolveOverlayLayout(layoutState, -1).found, false);

/* External reap/despawn helpers preserve host-owned layer lifetime. */
const reapState = createOverlayState();
request(reapState, 'OVERLAY_NPC_CREATE', [0, 0, 1, 1, 0], treeAdapters);
request(reapState, 'OVERLAY_NPC_CREATE', [1, 0, 1, 1, 0], treeAdapters);
assert.equal(destroyEntityOverlays(reapState, OVERLAY_ANCHOR.NPC, 44, treeAdapters), 2);
assert.equal(countOverlays(reapState), 0);
assert.equal(destroyOverlay(reapState, -1, treeAdapters), false);

const snapshot = snapshotOverlayState(identityState);
assert.equal(snapshot.items.length, 640);
const serialized = JSON.stringify(snapshot);
assert.equal(JSON.stringify(snapshotOverlayState(createOverlayState(snapshot))), serialized);
const firstLive = snapshot.items.find((record) => record);
firstLive.width = 999;
assert.notEqual(getOverlay(identityState, npcIndex).width, 999);

assert.equal(handleOverlayRequest(identityState, 'OVERLAY_COORD_GET', {
    args: [1, 2], arg_count: 1,
}).handled, false);
assert.equal(handleOverlayRequest(identityState, 'OVERLAY_NOT_REAL', {}).handled, false);

process.stdout.write('host_overlay_test: ok\n');
