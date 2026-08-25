import assert from 'node:assert/strict';

import {
    CLIENTOP_REQUEST_NAMES,
    HIGHLIGHT_REQUEST_NAMES,
    createClientOpState,
    createHighlightState,
    createHostActivityState,
    handleClientOpRequest,
    handleHighlightRequest,
    handleHostActivityRequest,
    snapshotClientOpState,
    snapshotHighlightState,
    snapshotHostActivityState,
} from '../src/host_activity.js';

function highlight(state, name, args, extra = {}) {
    return handleHighlightRequest(state, name, { args, arg_count: args.length, ...extra });
}

assert.equal(HIGHLIGHT_REQUEST_NAMES.size, 45);
assert.equal(CLIENTOP_REQUEST_NAMES.size, 10);
assert(HIGHLIGHT_REQUEST_NAMES.has('HIGHLIGHT_OPGROUP_GET'));
assert(CLIENTOP_REQUEST_NAMES.has('CLIENTOP_TILE_DEL'));
assert.throws(() => HIGHLIGHT_REQUEST_NAMES.add('NOPE'), TypeError);
assert.throws(() => CLIENTOP_REQUEST_NAMES.clear(), TypeError);
HIGHLIGHT_REQUEST_NAMES.forEach((_name, _again, names) => {
    assert.throws(() => names.delete('HIGHLIGHT_NPC_GET'), TypeError);
});

const state = createHighlightState();
assert.deepEqual(state.styles.npc[0], {
    colour: -1, outlineWidth: 0, opacity: 0, flags: 0,
});
let result = highlight(state, 'HIGHLIGHT_NPC_SETUP', [3, 0x123456, 2, 90, 5]);
assert.deepEqual(result, { handled: true, value: null, changed: true, revisionChanged: true });
assert.equal(state.revision, 1);
result = highlight(state, 'HIGHLIGHT_NPC_SETUP', [3, 0x123456, 2, 90, 5]);
assert.equal(result.changed, false);
assert.equal(result.revisionChanged, true);
assert.equal(state.revision, 2, 'C increments revision for every valid SETUP');
assert.equal(highlight(state, 'HIGHLIGHT_NPC_SETUP', [32, 1, 1, 1, 1]).changed, false);
assert.equal(state.revision, 2);

result = highlight(state, 'HIGHLIGHT_LOC_ON', [91, 0x11223344, 4, 7]);
assert.equal(result.changed, true);
assert.equal(result.revisionChanged, true);
assert.equal(highlight(state, 'HIGHLIGHT_LOC_GET', [91, 0x11223344, 4, 999]).value, 1,
    'GET ignores the per-subject flags');
result = highlight(state, 'HIGHLIGHT_LOC_ON', [91, 0x11223344, 4, 8]);
assert.equal(result.changed, true);
assert.equal(result.revisionChanged, false, 'repeat ON updates flags without a C revision');
assert.equal(state.members.loc[0].flags, 8);
assert.equal(highlight(state, 'HIGHLIGHT_LOC_ON', [91, 0x11223344, 4, 8]).changed, false);
assert.equal(highlight(state, 'HIGHLIGHT_LOC_GET', [91, 0x11223344, -1, 8]).value, 0);

highlight(state, 'HIGHLIGHT_LOC_ON', [92, 0x55667788, 4, 0]);
result = highlight(state, 'HIGHLIGHT_LOC_OFF', [91, 0x11223344, 4, -123]);
assert.equal(result.changed, true);
assert.equal(state.members.loc.length, 1);
assert.equal(state.members.loc[0].key, 92, 'OFF fills the hole from the final member');
assert.equal(highlight(state, 'HIGHLIGHT_LOC_CLEAR', [4]).changed, true);
assert.equal(state.members.loc.length, 0);
assert.equal(highlight(state, 'HIGHLIGHT_LOC_CLEAR', [4]).changed, false);

assert.equal(highlight(state, 'HIGHLIGHT_TILE_ON', [1234, 6, 3]).changed, true);
assert.equal(highlight(state, 'HIGHLIGHT_TILE_GET', [1234, 6, 0]).value, 1);
assert.deepEqual(state.members.tile[0], { group: 6, key: -1, coord: 1234, flags: 3 });
assert.equal(highlight(state, 'HIGHLIGHT_NPCTYPE_ON', [77, 2]).changed, true);
assert.deepEqual(state.members.npctype[0], { group: 2, key: 77, coord: -1, flags: 0 });

assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_ON', [5], { name: 'Alice' }).changed, true);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_ON', [5], { name: 'alice' }).changed, true,
    'named keys are exact-case');
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_GET', [5], { name: 'Alice' }).value, 1);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_GET', [5], { name: 'ALICE' }).value, 0);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_ON', [5], { name: '' }).changed, false);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_ON', [5], { name: 'x'.repeat(31) }).changed, true);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_ON', [5], { name: 'x'.repeat(32) }).changed, false);
assert.equal(highlight(state, 'HIGHLIGHT_OPGROUP_ON', [5], { name: '🏴'.repeat(7) }).changed, true);
assert.equal(highlight(state, 'HIGHLIGHT_OPGROUP_ON', [5], { name: '🏴'.repeat(8) }).changed, false,
    'the name limit is UTF-8 bytes, not JS code units');
assert.equal(handleHighlightRequest(state, 'HIGHLIGHT_PLAYER_GET', {
    args: [5], arg_count: 1,
}).handled, false, 'a missing named-form string is a bridge shape error');
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_OFF', [5], { name: 'Alice' }).changed, true);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_GET', [5], { name: 'Alice' }).value, 0);
assert.equal(highlight(state, 'HIGHLIGHT_PLAYER_CLEAR', [5]).changed, true);
assert(state.named.every((member) => member.kind !== 'player' || member.group !== 5));

const numericFull = createHighlightState();
for( let key = 0; key < 512; key++ )
    assert.equal(highlight(numericFull, 'HIGHLIGHT_OBJTYPE_ON', [key, 1]).changed, true);
const numericOverflow = highlight(numericFull, 'HIGHLIGHT_OBJTYPE_ON', [512, 1]);
assert.equal(numericFull.members.objtype.length, 512);
assert.equal(numericOverflow.changed, true, 'the first refusal records the C overflow flag');
assert.equal(numericOverflow.revisionChanged, false);
assert.equal(highlight(numericFull, 'HIGHLIGHT_OBJTYPE_ON', [513, 1]).changed, false);

const namedFull = createHighlightState();
for( let index = 0; index < 64; index++ ) {
    const kind = index % 2 ? 'PLAYER' : 'OPGROUP';
    assert.equal(highlight(namedFull, `HIGHLIGHT_${kind}_ON`, [index % 32], {
        name: `name-${index}`,
    }).changed, true);
}
assert.equal(namedFull.named.length, 64, 'player/opgroup share one 64-entry table');
assert.equal(highlight(namedFull, 'HIGHLIGHT_PLAYER_ON', [0], { name: 'overflow' }).changed, true);
assert.equal(namedFull.overflowed, true);

const malformed = handleHighlightRequest(state, 'HIGHLIGHT_NPC_ON', {
    args: [1, 2, 3], arg_count: 2,
});
assert.equal(malformed.handled, false);
assert.equal(handleHighlightRequest(state, 'HIGHLIGHT_NOT_REAL', {}).handled, false);

const clientops = createClientOpState();
result = handleClientOpRequest(clientops, 'CLIENTOP_TILE_SET', {
    slot: 7, label: 'Mark tile', script_id: 4762,
});
assert.deepEqual(result, { handled: true, value: null, changed: true, revisionChanged: true });
assert.deepEqual(clientops.slots.tile[7], { slot: 7, label: 'Mark tile', script_id: 4762 });
assert.equal(handleClientOpRequest(clientops, 'CLIENTOP_TILE_SET', {
    slot: 7, label: 'Mark tile', script_id: 4762,
}).changed, false);
assert.equal(handleClientOpRequest(clientops, 'CLIENTOP_TILE_SET', {
    slot: 7, label: 'Mark this tile', script_id: 4763,
}).changed, true, 'SET overwrites the whole slot');
assert.equal(handleClientOpRequest(clientops, 'CLIENTOP_TILE_SET', {
    slot: 8, label: 'invalid', script_id: 1,
}).changed, false);
assert.equal(handleClientOpRequest(clientops, 'CLIENTOP_TILE_DEL', { slot: 7 }).changed, true);
assert.equal(clientops.slots.tile[7], null);
assert.equal(handleClientOpRequest(clientops, 'CLIENTOP_TILE_DEL', { slot: 7 }).changed, false);

handleClientOpRequest(clientops, 'CLIENTOP_NPC_SET', {
    slot: 0, label: '🏴'.repeat(10), script_id: -1,
});
assert.equal(new TextEncoder().encode(clientops.slots.npc[0].label).length, 36,
    'labels are safely truncated below the C buffer\'s 40-byte size');
assert.equal(clientops.slots.npc[0].slot, 0);
assert.equal(clientops.slots.npc[0].script_id, -1);

const highlightSnapshot = snapshotHighlightState(state);
const highlightJSON = JSON.stringify(highlightSnapshot);
assert.equal(JSON.stringify(snapshotHighlightState(createHighlightState(highlightSnapshot))), highlightJSON);
highlightSnapshot.styles.npc[0].colour = 999;
assert.notEqual(state.styles.npc[0].colour, 999, 'snapshots do not alias mutable runtime state');
const oversizedHighlightSeed = createHighlightState({
    members: { npc: Array.from({ length: 600 }, (_, key) => ({
        group: 0, key, coord: key, flags: 0,
    })) },
    named: Array.from({ length: 100 }, (_, index) => ({
        kind: index % 2 ? 'player' : 'opgroup', group: 0, name: `seed-${index}`,
    })),
});
assert.equal(snapshotHighlightState(oversizedHighlightSeed).members.npc.length, 512);
assert.equal(snapshotHighlightState(oversizedHighlightSeed).named.length, 64);

const clientopSnapshot = snapshotClientOpState(clientops);
const clientopJSON = JSON.stringify(clientopSnapshot);
assert.equal(JSON.stringify(snapshotClientOpState(createClientOpState(clientopSnapshot))), clientopJSON);
clientopSnapshot.slots.npc[0].label = 'mutated';
assert.notEqual(clientops.slots.npc[0].label, 'mutated');
assert(Object.values(snapshotClientOpState(clientops).slots)
    .every((slots) => slots.length === 8), 'client-op snapshots are exactly five-by-eight');

const activity = createHostActivityState();
assert.equal(handleHostActivityRequest(activity, 'HIGHLIGHT_NPC_GET', {
    args: [1, 2, 0], arg_count: 3,
}).value, 0);
assert.equal(handleHostActivityRequest(activity, 'CLIENTOP_NPC_SET', {
    slot: 1, label: 'Tag', script_id: 6688,
}).changed, true);
assert.equal(handleHostActivityRequest(activity, 'NOT_AN_ACTIVITY', {}).handled, false);
assert.doesNotThrow(() => JSON.stringify(snapshotHostActivityState(activity)));

process.stdout.write('host_activity_test: ok\n');
