import assert from 'node:assert/strict';

import {
    LOOT_REQUESTS, createLootState, handleLootRequest,
} from '../src/host_loot.js';

function call(state, kind, request = {}, options) {
    return handleLootRequest(state, { kind, ...request }, options);
}

assert.equal(LOOT_REQUESTS.size, 32);
assert.deepEqual([...LOOT_REQUESTS], [
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

const empty = createLootState();
assert.doesNotThrow(() => JSON.stringify(empty));
assert.deepEqual(empty, {
    sources: [], nextSourceId: 1, nextEventId: 1,
    itemIgnored: [], sourceIgnored: [], aux: [[], [], [], [], []], queryIds: [],
});
assert.deepEqual(call(empty, 'LOOT_SOURCE_COUNT'), { result: 0, changed: false });
assert.deepEqual(call(empty, 'LOOT_SOURCE_NAME', { int_args: [99] }),
    { result: '', changed: false });
assert.deepEqual(call(empty, 'LOOT_SOURCE_NAME2', { int_args: [99] }),
    { result: '', changed: false });
assert.deepEqual(call(empty, 'LOOT_ROW_BYNAME', { name: 'missing', int_args: [1] }),
    { result: [0, 0], changed: false });
assert.deepEqual(call(empty, 'LOOT_ROW_BYID', { int_args: [99, 1] }),
    { result: [0, 0], changed: false });

/* Aux lists are 0-based, bounded to kinds 0..4, unique, and remove by swap. */
assert.equal(call(empty, 'LOOT_AUX_UPSERT2', { name: 'first', int_args: [2] }).changed, true);
assert.equal(call(empty, 'LOOT_AUX_UPSERT', { name: 'second', int_args: [2, 77] }).changed, true);
assert.equal(call(empty, 'LOOT_AUX_UPSERT', { name: 'first', int_args: [2, -1] }).changed, false);
assert.equal(call(empty, 'LOOT_AUX_UPSERT2', { name: 'invalid', int_args: [5] }).changed, false);
assert.equal(call(empty, 'LOOT_AUX_COUNT', { int_args: [2] }).result, 2);
assert.equal(call(empty, 'LOOT_AUX_COUNT', { int_args: [-1] }).result, 0);
assert.equal(call(empty, 'LOOT_AUX_GET', { int_args: [2, 0] }).result, 'first');
assert.equal(call(empty, 'LOOT_AUX_GET', { int_args: [2, 2] }).result, '');
assert.equal(call(empty, 'LOOT_AUX_LOOKUP', {
    name: 'second', int_args: [2, 123, 456],
}).result, 1);
assert.equal(call(empty, 'LOOT_AUX_REMOVE', {
    name: 'first', int_args: [2, 999],
}).changed, true);
assert.deepEqual(empty.aux[2], ['second']);
assert.equal(call(empty, 'LOOT_AUX_COUNT_TOTAL').result, 1);
assert.equal(call(empty, 'LOOT_AUX_CLEAR', { int_args: [2] }).changed, true);
assert.equal(call(empty, 'LOOT_AUX_CLEAR', { int_args: [2] }).changed, false);

/* Persistent item/source ignore getters are 1-based; removal also swaps. */
call(empty, 'LOOT_IGNORE_ADD', { name: 'Bones' });
call(empty, 'LOOT_IGNORE_ADD', { name: 'Coins' });
assert.equal(call(empty, 'LOOT_IGNORE_ADD', { name: 'Bones' }).changed, false);
assert.equal(call(empty, 'LOOT_GROUND_COUNT').result, 2);
assert.equal(call(empty, 'LOOT_GROUND_NAME', { int_args: [0] }).result, '');
assert.equal(call(empty, 'LOOT_GROUND_NAME', { int_args: [1] }).result, 'Bones');
assert.equal(call(empty, 'LOOT_IGNORE_REMOVE', { name: 'Bones' }).changed, true);
assert.deepEqual(empty.itemIgnored, ['Coins']);
call(empty, 'LOOT_SOURCE_IGNORE_ADD', { name: 'Goblin' });
call(empty, 'LOOT_SOURCE_IGNORE_ADD', { name: 'Dragon' });
assert.equal(call(empty, 'LOOT_SRCLIST_COUNT').result, 2);
assert.equal(call(empty, 'LOOT_SRCLIST_NAME', { int_args: [2] }).result, 'Dragon');
assert.equal(call(empty, 'LOOT_SOURCE_IGNORE_REMOVE', { name: 'Goblin' }).changed, true);
assert.deepEqual(empty.sourceIgnored, ['Dragon']);

/* LOOT_ADD's reflected int array is [event id, quantity, object id]. */
const costLookups = [];
const objectCost = (id) => {
    costLookups.push(id);
    if( id === 100 ) return 50;
    if( id === 101 ) return 0;
    return undefined;
};
call(empty, 'LOOT_ADD', {
    name: 'Goblin', int_args: [7, 2, 100],
}, { objectCost });
call(empty, 'LOOT_ADD', {
    name: 'Goblin', int_args: [7, 4, 101],
}, { objectCost });
call(empty, 'LOOT_ADD', {
    name: 'Goblin', int_args: [8, 3, 100],
}, { objectCost });
call(empty, 'LOOT_ADD', {
    name: 'Dragon', int_args: [9, 6, 102],
}, { objectCost });
assert.deepEqual(costLookups, [100, 101, 100, 102]);
assert.deepEqual(empty.sources, [
    {
        id: 1, name: 'Goblin', killCount: 2, lastEventId: 8,
        rows: [{ objId: 100, qty: 5, value: 250 }, { objId: 101, qty: 4, value: 0 }],
    },
    {
        id: 2, name: 'Dragon', killCount: 1, lastEventId: 9,
        rows: [{ objId: 102, qty: 6, value: 6 }],
    },
]);
assert.equal(call(empty, 'LOOT_SOURCE_ITEMCOUNT', { name: 'Goblin' }).result, 2);
assert.equal(call(empty, 'LOOT_SOURCE_TOTALVAL', { name: 'Goblin' }).result, 2);
assert.equal(call(empty, 'LOOT_ROW_COUNT_BYNAME', { name: 'Goblin' }).result, 2);
assert.equal(call(empty, 'LOOT_ROW_COUNT_BYID', { int_args: [1] }).result, 2);
assert.deepEqual(call(empty, 'LOOT_ROW_BYNAME', {
    name: 'Goblin', int_args: [1],
}).result, [100, 5]);
assert.deepEqual(call(empty, 'LOOT_ROW_BYID', { int_args: [1, 2] }).result, [101, 4]);
assert.deepEqual(call(empty, 'LOOT_ROW_BYID', { int_args: [1, 0] }).result, [0, 0]);

assert.deepEqual(call(empty, 'LOOT_BEGIN_QUERY', { int_args: [-5, 1, 1] }),
    { result: 1, changed: true });
assert.equal(call(empty, 'LOOT_QUERY_ID', { int_args: [0] }).result, 1);
assert.equal(call(empty, 'LOOT_QUERY_ID', { int_args: [1] }).result, -1);
assert.deepEqual(call(empty, 'LOOT_BEGIN_QUERY', { int_args: [1, 1, 2] }),
    { result: 1, changed: true });
assert.deepEqual(empty.queryIds, [2]);
assert.equal(call(empty, 'LOOT_BEGIN_QUERY', { int_args: [0, 10, 3] }).result, 2);
assert.deepEqual(empty.queryIds, [1, 2]);

/* Removing index zero replaces it with the last source. Native leaves the
 * current query untouched until another begin-query or clear-all. */
assert.equal(call(empty, 'LOOT_CLEAR_SOURCE', { name: 'Goblin' }).changed, true);
assert.deepEqual(empty.sources.map((source) => source.name), ['Dragon']);
assert.deepEqual(empty.queryIds, [1, 2]);
call(empty, 'LOOT_ADD', { name: 'Imp', int_args: [10, 1, 100] }, { objectCost });
assert.equal(call(empty, 'LOOT_SOURCE_NAME2', { int_args: [3] }).result, 'Imp');
assert.equal(call(empty, 'LOOT_REMOVE_BYID', { int_args: [2] }).changed, true);
assert.deepEqual(empty.sources.map((source) => source.name), ['Imp']);
assert.deepEqual(call(empty, 'LOOT_BEGIN_QUERY', { int_args: [0, -1, 1] }),
    { result: 0, changed: true });
assert.deepEqual(call(empty, 'LOOT_BEGIN_QUERY', { int_args: [0, 10, 0] }),
    { result: 0, changed: false });

/* Clear-all preserves both ignore registries, aux lists, and the next id. */
call(empty, 'LOOT_AUX_UPSERT2', { name: 'kept', int_args: [4] });
const nextSourceId = empty.nextSourceId;
assert.equal(call(empty, 'LOOT_CLEAR_ALL').changed, true);
assert.equal(empty.nextSourceId, nextSourceId);
assert.deepEqual(empty.itemIgnored, ['Coins']);
assert.deepEqual(empty.sourceIgnored, ['Dragon']);
assert.deepEqual(empty.aux[4], ['kept']);
assert.equal(call(empty, 'LOOT_IGNORE_CLEAR').changed, true);
assert.equal(call(empty, 'LOOT_IGNORE_CLEAR').changed, false);

/* Seed construction clones state, accepts native key spellings, and remains JSON data. */
const seed = {
    sources: [{
        id: 9, name: 'Seed', kill_count: 4, last_event_id: 12,
        rows: [{ obj_id: 5, qty: 6, value: 7 }],
    }],
    item_ignored: ['A', 'A', 'B'], source_ignored: ['C'],
    aux: { 0: ['x', 'x'], 4: ['y'] }, query_ids: [9], next_event_id: 44,
};
const seeded = createLootState(seed);
assert.equal(seeded.nextSourceId, 10);
assert.equal(seeded.nextEventId, 44);
assert.deepEqual(seeded.itemIgnored, ['A', 'B']);
assert.deepEqual(seeded.aux, [['x'], [], [], [], ['y']]);
assert.deepEqual(call(seeded, 'LOOT_ROW_BYID', { int_args: [9, 1] }).result, [5, 6]);
seed.sources[0].rows[0].qty = 999;
assert.equal(seeded.sources[0].rows[0].qty, 6);
assert.deepEqual(createLootState(JSON.parse(JSON.stringify(seeded))), seeded);

assert.throws(() => call(empty, 'NOT_LOOT'), /unsupported loot HOST request/);

console.log('host_loot_test: ok (32 request kinds)');
