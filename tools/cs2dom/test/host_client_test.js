/*
 * Client-owned state: arrays, the interface stack, options, coords, and the
 * operations a preview records rather than performs.
 *
 * Most of these tests are about a specific NUMBER — `if_gettop` answering -1
 * and not 0, `coord` answering -1 and not 0, `coordx(-1)` answering -1 and not
 * 16383. Each of those zeroes is a real value somewhere in the game's domain,
 * so getting one wrong does not fail: it takes a branch nobody meant.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import {
    ClientState, createHostKernel, HostState, ReadyAssetSource, HOST_PARK,
} from '../src/host_kernel.js';
import { MOUNT_TYPE, createRandom, packCoord, unpackCoord } from '../src/host_client.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function harness(clientOptions = {}) {
    const tree = createUITree();
    const client = new ClientState(clientOptions);
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(), client,
    });
    return { tree, host, client };
}

/* -------------------------------------------------------------------------
 * Arrays
 * ---------------------------------------------------------------------- */

test('array_new takes (typeCode, length, capacity), in that order', () => {
    /*
     * Distinct from `define_array`, which DECLARES a local. Same storage,
     * different statement, which is why the emitter lowers them differently —
     * and a different argument list, which is why reading the type where the
     * length belongs makes every array either empty or enormous.
     *
     * Int cells are -1: `null` for every reference-typed base type.
     */
    const { host } = harness();
    assert.deepEqual(host.array_new(105, 4, 4), [-1, -1, -1, -1]);
    assert.deepEqual(host.array_new(115, 2, 2), ['', ''], 'a string array starts ""');
});

test('array helpers do what their names say', () => {
    const { host } = harness();
    assert.equal(host.array_join(['a', 'b', 'c'], '-'), 'a-b-c');
    assert.deepEqual(host.array_split('a,b,c', ','), ['a', 'b', 'c']);
    /* (handle, value, start, end, valueType); a negative end means "to the
     * end", and valueType -1 means nothing was pushed and nothing can match. */
    assert.equal(host.array_count_matches([1, 2, 1, 3, 1], 1, 0, -1, 105), 3);
    assert.equal(host.array_count_matches([1, 2, 1, 3, 1], 1, 2, 5, 105), 2,
        'the range is honoured, not decoration');
    assert.equal(host.array_count_matches([1, 1], 1, 0, -1, -1), 0,
        'type -1 is "no value": nothing can match');
});

/* -------------------------------------------------------------------------
 * The interface stack
 * ---------------------------------------------------------------------- */

test('opening a sub-interface parks until its group is baked', () => {
    const { tree, host } = harness();
    assert.equal(host.if_openwidget(0x10001, 0x0271, MOUNT_TYPE.MODAL), HOST_PARK);
    assert.deepEqual(host.pending, { kind: 'component', id: 0x0271, extra: null });

    /* Bake it and the retry completes. */
    tree.push({ componentId: (0x0271 << 16) | 0, type: WIDGET_TYPE.LAYER });
    host.if_openwidget(0x10001, 0x0271, MOUNT_TYPE.MODAL);
    assert.equal(host.if_hassub_at(0x10001), 1);
    assert.equal(host.if_getsubid(0x10001), 0x0271);
});

test('if_close asks the server; it does not unmount anything itself', () => {
    /*
     * `if_close` takes NO arguments and is not a local close: it is what every
     * interface's X runs (clientscript 29, whose entire body is this), and the
     * reference sends CLOSE_MODAL and waits for the server to unmount.
     * Unmounting here would show a panel closing that the server still
     * believes is open.
     */
    const { tree, host, client } = harness();
    tree.push({ componentId: (0x0271 << 16) | 0, type: WIDGET_TYPE.LAYER });
    host.if_openwidget(0x10001, 0x0271, MOUNT_TYPE.MODAL);

    host.if_close();
    assert.deepEqual(client.intents, [{ intent: 'closeModal' }]);
    assert.equal(host.if_hassub_at(0x10001), 1, 'still mounted until the server says');
});

test('the accepted close removes the mount but keeps the pack', () => {
    /* Re-opening must not re-bake — that is what makes a tab switch cheap,
     * and it is the reference's own behaviour. */
    const { tree, host } = harness();
    tree.push({ componentId: (0x0271 << 16) | 0, type: WIDGET_TYPE.LAYER });
    host.if_openwidget(0x10001, 0x0271, MOUNT_TYPE.MODAL);

    host.closeMount(0x10001);
    assert.equal(host.if_hassub_at(0x10001), 0);
    assert.equal(tree.hasGroup(0x0271), true, 'the pack is still in the tree');
});

test('if_gettop answers -1 with nothing open, not 0', () => {
    /*
     * Script 900 maps this to an enum id and answers -1 for a top-level
     * interface it does not know. Zero is a real interface id, so answering it
     * would resolve to interface 0 rather than to "none".
     */
    const { host } = harness();
    assert.equal(host.if_gettop(), -1);
});

test('if_gettop reports the modal, not an overlay', () => {
    const { tree, host } = harness();
    tree.push({ componentId: (0x0271 << 16) | 0 });
    tree.push({ componentId: (0x0100 << 16) | 0 });
    host.if_openwidget(0x10001, 0x0100, MOUNT_TYPE.OVERLAY);
    host.if_openwidget(0x10002, 0x0271, MOUNT_TYPE.MODAL);
    assert.equal(host.if_gettop(), 0x0271);
});

/* -------------------------------------------------------------------------
 * Options
 * ---------------------------------------------------------------------- */

test('the three option families share one id space, as the C host has them', () => {
    const { host } = harness();
    host.gameoption_set(7, 3);
    assert.equal(host.gameoption_get(7), 3);
    assert.equal(host.deviceoption_get(7), 3,
        'one map, because keeping three invites one to drift');
    assert.equal(host.clientoption_get(99), 0, 'an unset option is zero');
});

test('window mode round-trips and has a separate default', () => {
    const { host } = harness({ windowMode: 1, defaultWindowMode: 1 });
    host.setwindowmode(2);
    assert.equal(host.getwindowmode(), 2);
    assert.equal(host.getdefaultwindowmode(), 1, 'the default is not the current');
});

/* -------------------------------------------------------------------------
 * Coordinates
 * ---------------------------------------------------------------------- */

test('coord answers -1 with no local player, not 0', () => {
    /*
     * Zero is a real tile in the corner of the map, and a script comparing
     * against a box cannot tell it from a position — "the player is in the
     * north-west corner" becomes a branch nobody meant to take.
     */
    const { host } = harness();
    assert.equal(host.coord(), -1);
});

test('a packed coord unpacks to its three parts', () => {
    const packed = packCoord(1, 3200, 3400);
    assert.deepEqual(unpackCoord(packed), { level: 1, x: 3200, y: 3400 });

    const { host } = harness({ coord: packed });
    assert.equal(host.coord(), packed);
    /* RuneScript names the axes x/y/z with Y AS THE LEVEL, so `coordy` is the
     * plane and `coordz` is the second tile axis — `CS2VM2_Op_CoordY` returns
     * `(packed >> 28) & 0x3`. Scripts also pack a SIZE into a coord and read
     * it back with this pair, which is where the swap showed. */
    assert.equal(host.coordx(packed), 3200);
    assert.equal(host.coordy(packed), 1);
    assert.equal(host.coordz(packed), 3400);
});

test('the parts of "no coord" are -1, not the bits of -1', () => {
    /* `coordx(-1)` answering 16383 would put the player at the map's edge —
     * a real place, and a plausible one. */
    assert.deepEqual(unpackCoord(-1), { level: -1, x: -1, y: -1 });
    const { host } = harness();
    assert.equal(host.coordx(-1), -1);
});

/* -------------------------------------------------------------------------
 * Randomness
 * ---------------------------------------------------------------------- */

test('randomness is seeded, so two runs render the same', () => {
    /* A preview that differs on every reload cannot be compared against
     * anything, and no script observes the C client's particular sequence. */
    const a = harness().host;
    const b = harness().host;
    const first = Array.from({ length: 8 }, () => a.random(100));
    const second = Array.from({ length: 8 }, () => b.random(100));
    assert.deepEqual(first, second);
    assert.ok(first.some((value) => value !== first[0]), 'and it does vary');
});

test('random respects its bound, and randominc is inclusive', () => {
    const { host } = harness();
    for( let i = 0; i < 200; i++ )
    {
        const value = host.random(10);
        assert.ok(value >= 0 && value < 10, `random(10) gave ${value}`);
    }
    assert.equal(host.random(0), 0, 'a zero bound has one answer');
    const seen = new Set(Array.from({ length: 400 }, () => host.randominc(3)));
    assert.ok(seen.has(3), 'randominc(3) can answer 3');
});

/* -------------------------------------------------------------------------
 * Recorded, not performed
 * ---------------------------------------------------------------------- */

test('an operation reaching a system the preview lacks is recorded', () => {
    /*
     * Not faked. `if_triggeroplocal` clicks a component in the real client and
     * `resume_countdialog` answers a paused server script; answering as though
     * either happened makes a panel look right and behave wrongly.
     */
    const { host, client } = harness();
    host.if_triggeroplocal(0x10002, 4);
    host.resume_countdialog(42);
    assert.deepEqual(client.intents, [
        { intent: 'triggerOpLocal', component: 0x10002, subId: 4 },
        { intent: 'resumeCount', value: 42 },
    ]);
});

test('a highlight is keyed on the whole SUBJECT, not just the id', () => {
    /*
     * `highlight_obj_on(obj, coord, ...)` names one pile on one tile, and
     * `highlight_npc_on(npc, coord, slot)` one npc among several of a kind.
     * Keying by the id alone would switch off a highlight the script never
     * mentioned — the wrong goblin stops glowing and nothing reports it.
     *
     * The trailing argument is the SLOT, the `_setup` id whose colour and
     * thickness this subject uses, so one family carries several styles at
     * once and `_clear(slot)` drops only that style's subjects.
     */
    const { host } = harness();
    host.highlight_npc_on(1234, 5000, 0);
    host.highlight_npc_on(1234, 6000, 0);
    assert.equal(host.highlight_npc_get(1234, 5000, 0), 1);
    assert.equal(host.highlight_npc_get(1234, 6000, 0), 1);

    host.highlight_npc_off(1234, 5000, 0);
    assert.equal(host.highlight_npc_get(1234, 5000, 0), 0);
    assert.equal(host.highlight_npc_get(1234, 6000, 0), 1,
        'the other tile is a different subject');
});

test('_clear takes the slot, and drops only that slot\'s subjects', () => {
    const { host } = harness();
    host.highlight_loctype_on(50, 0);
    host.highlight_loctype_on(51, 1);
    host.highlight_loctype_clear(0);
    assert.equal(host.highlight_loctype_get(50, 0), 0);
    assert.equal(host.highlight_loctype_get(51, 1), 1);

    host.highlight_clear();
    assert.equal(host.highlight_loctype_get(51, 1), 0, 'and clear drops everything');
});

test('a world question with no world answers the reference\'s "nothing"', () => {
    /* A preview with no entities genuinely has no npc under the cursor; -1 is
     * the answer, not a placeholder. */
    const { host } = harness();
    assert.equal(host.npc_uid(), -1);
    assert.equal(host.npc_creationcycle(), -1);
    assert.equal(host.player_uid(), -1);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
