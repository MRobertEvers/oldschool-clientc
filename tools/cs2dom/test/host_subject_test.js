import assert from 'node:assert/strict';

import {
    SUBJECT_REQUESTS, createSubjectState, handleSubjectRequest,
} from '../src/host_subject.js';

function call(state, kind, request = {}, options) {
    return handleSubjectRequest(state, { kind, ...request }, options);
}

assert.equal(SUBJECT_REQUESTS.size, 20);
assert.deepEqual([...SUBJECT_REQUESTS], [
    '_6750', '_6751', '_6752', '_6753',
    '_6800', '_6801', '_6802', 'LOC_FIND',
    '_6850', '_6851', '_6852', '_6853',
    '_6900', 'ACTIVEPLAYER_SETLOCAL', 'ACTIVEPLAYER_GETROUTELENGTH',
    'ACTIVEPLAYER_GETROUTECOORD', 'ACTIVEPLAYER_GETUID', 'LOCALPLAYER_GETUID',
    '_6950', 'COORD_INSCENE',
]);

/* A browser preview with no world uses these exact native defaults. */
const empty = createSubjectState();
assert.doesNotThrow(() => JSON.stringify(empty));
for( const kind of ['_6750', '_6800', '_6850', '_6900'] )
    assert.deepEqual(call(empty, kind), { result: '', changed: false });
for( const kind of [
    '_6751', '_6752', '_6753', '_6801', '_6802', '_6851', '_6852', '_6853',
    '_6950', 'ACTIVEPLAYER_GETROUTECOORD', 'ACTIVEPLAYER_GETUID',
    'LOCALPLAYER_GETUID',
] ) assert.deepEqual(call(empty, kind), { result: -1, changed: false });
assert.deepEqual(call(empty, 'ACTIVEPLAYER_GETROUTELENGTH'),
    { result: 0, changed: false });
assert.deepEqual(call(empty, 'ACTIVEPLAYER_SETLOCAL'),
    { result: -1, changed: false });
assert.deepEqual(call(empty, 'LOC_FIND', { coord: 123, loc_type: 10 }),
    { result: 0, changed: false });
assert.deepEqual(call(empty, 'COORD_INSCENE', { coord: 123 }),
    { result: 0, changed: false });

/* Active registers supply every context getter with the native field mapping. */
const state = createSubjectState({
    hover_coord: 777,
    active: {
        npc: { name: 'Goblin', uid: 12, coord: 1001, type: 44 },
        loc: { name: 'Door', coord: 1002, type: 55, layer: 2 },
        obj: { name: 'Coins', coord: 1003, type: 995, count: 37 },
        player: { name: 'Alice', uid: 7, coord: 1004 },
        tile: { coord: 1005 },
    },
});
const expectedContext = {
    _6750: 'Goblin', _6751: 12, _6752: 1001, _6753: 44,
    _6800: 'Door', _6801: 1002, _6802: 55,
    _6850: 'Coins', _6851: 1003, _6852: 995, _6853: 37,
    _6900: 'Alice', _6950: 1005,
};
for( const [kind, result] of Object.entries(expectedContext) )
    assert.equal(call(state, kind).result, result, kind);
state.active.tile = null;
assert.equal(call(state, '_6950').result, 777);

/* Dispatch wins only for its root script; then active, then mouseover. */
const priority = createSubjectState({
    runningScriptId: 50,
    dispatch: { kind: 'npc', scriptId: 50, name: 'Dispatch', uid: 1 },
    active: { npc: { name: 'Active', uid: 2 } },
    mouseover: { kind: 'npc', name: 'Hover', uid: 3 },
});
assert.equal(call(priority, '_6750').result, 'Dispatch');
assert.equal(call(priority, '_6751', { runningScriptId: 51 }).result, 2);
priority.active.npc = null;
assert.equal(call(priority, '_6750', {}, { runningScriptId: 51 }).result, 'Hover');
priority.dispatch.scriptId = 0;
assert.equal(call(priority, '_6750', { running_script_id: 0 }).result, 'Hover');

/* LOC_FIND writes or clears only the active-loc register. */
const found = call(state, 'LOC_FIND', { coord: 0x12345678, loc_type: 10 }, {
    locAtCoord(coord, type) {
        assert.equal(coord, 0x12345678);
        assert.equal(type, 10);
        return { layer: 3, name: 'Treasure chest' };
    },
});
assert.deepEqual(found, { result: 1, changed: true });
assert.deepEqual(state.active.loc, {
    kind: 'loc', scriptId: 0, uid: -1, type: 10, count: 0,
    layer: 3, coord: 0x12345678, name: 'Treasure chest',
});
assert.equal(call(state, '_6800').result, 'Treasure chest');
assert.deepEqual(call(state, 'LOC_FIND', { coord: 2, loc_type: 10 }, {
    locAtCoord: () => null,
}), { result: 0, changed: true });
assert.equal(state.active.loc, null);
assert.deepEqual(call(state, 'LOC_FIND', { coord: 2, loc_type: 10 }),
    { result: 0, changed: false });

/* Seeded scene records are an optional JSON equivalent of the C callbacks. */
const scene = createSubjectState({
    locations: [{ coord: 500, loc_type: 22, layer: 1, name: 'Gate' }],
    scene_coords: [500, 600],
});
assert.equal(call(scene, 'LOC_FIND', { coord: 500, loc_type: 22 }).result, 1);
assert.equal(call(scene, 'COORD_INSCENE', { coord: 500 }).result, 1);
assert.equal(call(scene, 'COORD_INSCENE', { coord: 700 }).result, 0);
assert.equal(call(scene, 'COORD_INSCENE', { coord: 700 }, {
    coordInScene: (coord) => coord === 700,
}).result, 1);

/* SETLOCAL writes the active player; route index zero is the newest tile. */
const player = createSubjectState({
    local_pid: 4, local_coord: 900, routes: { 4: [903, 902, 901] },
});
assert.deepEqual(call(player, 'ACTIVEPLAYER_SETLOCAL'), { result: 1, changed: true });
assert.deepEqual(call(player, 'ACTIVEPLAYER_SETLOCAL'), { result: 1, changed: false });
assert.equal(call(player, 'ACTIVEPLAYER_GETUID').result, 4);
assert.equal(call(player, 'LOCALPLAYER_GETUID').result, 4);
assert.equal(call(player, 'ACTIVEPLAYER_GETROUTELENGTH').result, 3);
assert.equal(call(player, 'ACTIVEPLAYER_GETROUTECOORD', { index: 0 }).result, 903);
assert.equal(call(player, 'ACTIVEPLAYER_GETROUTECOORD', { index: 3 }).result, -1);
assert.equal(call(player, 'ACTIVEPLAYER_GETROUTELENGTH', {}, {
    playerRoute(uid, index) {
        assert.equal(uid, 4);
        assert.equal(index, -1);
        return { length: 8, coord: -1 };
    },
}).result, 8);
assert.equal(call(player, 'ACTIVEPLAYER_GETROUTECOORD', { index: 2 }, {
    playerRoute: (uid, index) => ({ length: 8, coord: uid * 100 + index }),
}).result, 402);

/* A missing local player neither returns false nor clears an old active one. */
player.localPlayerUid = -1;
assert.deepEqual(call(player, 'ACTIVEPLAYER_SETLOCAL'), { result: -1, changed: false });
assert.equal(call(player, 'ACTIVEPLAYER_GETUID').result, 4);

/* Constructor cloning and aliases remain round-trippable JSON data. */
const seed = {
    localPlayerUid: 8,
    active: { obj: { obj_id: 123, type: 123, count: 2, name: 'Seed object' } },
    routes: { 8: { coords: [11, 12] } },
};
const cloned = createSubjectState(seed);
seed.routes[8].coords[0] = 99;
assert.deepEqual(cloned.routes['8'], [11, 12]);
assert.deepEqual(createSubjectState(JSON.parse(JSON.stringify(cloned))), cloned);

assert.throws(() => call(empty, 'NOT_A_SUBJECT_REQUEST'),
    /unsupported subject HOST request/);

console.log('host_subject_test: ok (20 request kinds)');
