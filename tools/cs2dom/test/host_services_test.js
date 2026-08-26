/*
 * The config and widget services, against the answers the C host gives.
 *
 * Almost every test here is about a MISS. A hit is easy and a miss is where
 * these handlers earn their comments: an absent enum, a negative object id, a
 * param a struct does not carry, a component whose slot was recycled. Each of
 * those has a specific answer in `rs_cs2_host.c` and each of those answers has
 * been the cause of a bug that looked like something else.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import {
    createHostKernel, HostConfig, HostPlayerState, HostState, ReadyAssetSource,
    HOST_PARK, UnimplementedHostOp,
} from '../src/host_kernel.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const CONFIG = new HostConfig({
    enums: {
        10: { string: false, defaultInt: -1, defaultString: 'null', values: { 1: 100, 2: 200 } },
        11: { string: true, defaultInt: 0, defaultString: 'null', values: { 5: 'five' } },
    },
    params: {
        600: { string: false, defaultInt: -1, defaultString: '' },
        601: { string: true, defaultInt: 0, defaultString: 'unset' },
    },
    structs: {
        70: { params: { 600: 42, 601: 'hello' } },
        71: { params: {} },
    },
    objects: {
        995: { name: 'Coins', cost: 1, stackable: 1, members: false, params: { 600: 7 } },
    },
    inventories: { 93: 28, 94: { size: 12 } },
});

function harness({ config = CONFIG, player = new HostPlayerState() } = {}) {
    const tree = createUITree();
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(), config, player,
    });
    return { tree, host };
}

function bakeGroup(tree, groupId, subCount = 4) {
    const root = tree.push({ componentId: (groupId << 16) | 0, type: WIDGET_TYPE.LAYER });
    for( let i = 1; i <= subCount; i++ )
        tree.push({
            parentIndex: root, componentId: (groupId << 16) | i, subId: i,
            type: WIDGET_TYPE.LAYER,
        });
    return root;
}

/* -------------------------------------------------------------------------
 * Enums
 * ---------------------------------------------------------------------- */

test('an enum hit answers its value and a miss answers the default', () => {
    const { host } = harness();
    assert.equal(host.enum(105, 105, 10, 1), 100);
    assert.equal(host.enum(105, 105, 10, 999), -1, 'a missing key takes the enum default');
});

test('a string-output enum answers "null", not empty', () => {
    /*
     * "null" is what a widget will literally draw when the id is wrong, which
     * is a legible failure. An empty string reads as a layout bug.
     */
    const { host } = harness();
    assert.equal(host.enum(105, 115, 11, 5), 'five');
    assert.equal(host.enum(105, 115, 11, 999), 'null');
});

test('the opcode decides the stack, not the enum', () => {
    /* An int enum asked for a string answers the string default rather than
     * coercing a number the caller would then treat as text. */
    const { host } = harness();
    assert.equal(typeof host.enum(105, 115, 10, 1), 'string');
});

test('an absent enum parks once, then answers the miss', () => {
    const { host } = harness();
    assert.equal(host.enum(105, 105, 42, 1), HOST_PARK);
    assert.deepEqual(host.pending, { kind: 'enum', id: 42, extra: null });
    /* The retry must COMPLETE. Parking again here is the forever-loop the C
     * planner asserts against. */
    assert.equal(host.enum(105, 105, 42, 1), -1);
});

test('a negative enum id is never awaited', () => {
    /*
     * A computed enum id can legitimately be negative — script 900 maps
     * IF_GETTOP to an enum and answers -1 for a top-level interface it does
     * not know. There is no archive to wait for.
     */
    const { host } = harness();
    assert.equal(host.enum(105, 105, -1, 0), -1);
    assert.equal(host.pending, null);
});

test('enum_getoutputcount counts the entries', () => {
    const { host } = harness();
    assert.equal(host.enum_getoutputcount(10), 2);
    assert.equal(host.enum_getoutputcount(-1), 0);
});

/* -------------------------------------------------------------------------
 * Struct and object params
 * ---------------------------------------------------------------------- */

test('struct_param answers the value, the type default, or zero', () => {
    const { host } = harness();
    assert.equal(host.struct_param(70, 600), 42, 'the struct carries it');
    assert.equal(host.struct_param(71, 600), -1, 'the ParamType default');
    assert.equal(host.struct_param(70, 601), 'hello');
    assert.equal(host.struct_param(71, 601), 'unset', 'a string default stays a string');
});

test('struct -1 is a valid input, not a load', () => {
    /*
     * An enum lookup that misses pushes -1, and scripts feed that straight
     * into struct_param. Awaiting it would assert in the C planner.
     */
    const { host } = harness();
    assert.equal(host.struct_param(-1, 600), -1);
    assert.equal(host.pending, null);
});

test('a param the ParamType declares as a string always answers a string', () => {
    const { host } = harness();
    assert.equal(typeof host.struct_param(-1, 601), 'string');
});

test('oc_name answers "null" for the empty slot without loading', () => {
    /* Bank empty slots are obj -1 and scripts ask about them freely. */
    const { host } = harness();
    assert.equal(host.oc_name(-1), 'null');
    assert.equal(host.pending, null);
    assert.equal(host.oc_name(995), 'Coins');
});

test('oc_param falls through to the ParamType default', () => {
    const { host } = harness();
    assert.equal(host.oc_param(995, 600), 7);
    assert.equal(host.oc_param(-1, 600), -1);
});

/* -------------------------------------------------------------------------
 * Inventories and stats
 * ---------------------------------------------------------------------- */

test('inv_size reads the inventory TYPE, not the live container', () => {
    /*
     * The bug this prevents: answering with the used prefix made every slot
     * past it invisible, and a 0 from a container that did not exist yet
     * teleported every login into the Gauntlet.
     */
    const player = new HostPlayerState({
        inventories: new Map([[93, [{ obj: 995, count: 5 }]]]),
    });
    const { host } = harness({ player });
    assert.equal(host.inv_size(93), 28, 'one occupied slot, capacity 28');
    assert.equal(host.inv_size(94), 12, 'the object form carries a size too');
});

test('inv reads answer for an empty slot without inventing one', () => {
    const player = new HostPlayerState({
        inventories: new Map([[93, [{ obj: 995, count: 5 }, null, { obj: 995, count: 2 }]]]),
    });
    const { host } = harness({ player });
    assert.equal(host.inv_getobj(93, 0), 995);
    assert.equal(host.inv_getobj(93, 1), -1);
    assert.equal(host.inv_getnum(93, 1), 0);
    assert.equal(host.inv_total(93, 995), 7, 'summed across slots');
    assert.equal(host.inv_total(93, 1), 0);
});

test('stats answer their three separate values', () => {
    const player = new HostPlayerState({
        stats: new Map([[6, { level: 70, base: 75, xp: 1210421 }]]),
    });
    const { host } = harness({ player });
    assert.equal(host.stat(6), 70);
    assert.equal(host.stat_base(6), 75);
    assert.equal(host.stat_xp(6), 1210421);
    assert.equal(host.stat(20), 0, 'an unset stat is zero, not undefined');
});

/* -------------------------------------------------------------------------
 * Widget identity
 * ---------------------------------------------------------------------- */

test('if_getlayer stops at the component\'s own interface', () => {
    /*
     * The cache stores `layer` per component and a pack root carries none, so
     * the reference answers -1 there. Our tree bakes a mounted pack under its
     * owner, so a raw parent walk goes straight out of the group — which is
     * what put the music list at x=1158 on an 807-pixel canvas.
     */
    const { tree, host } = harness();
    const outerRoot = tree.push({ componentId: (0x0161 << 16) | 0 });
    const innerRoot = tree.push({ parentIndex: outerRoot, componentId: (0x00ef << 16) | 0 });
    const child = tree.push({ parentIndex: innerRoot, componentId: (0x00ef << 16) | 3 });

    assert.equal(host.if_getlayer((0x00ef << 16) | 3), (0x00ef << 16) | 0,
        'a child reports its own interface\'s parent');
    assert.equal(host.if_getlayer((0x00ef << 16) | 0), -1,
        'a pack root reports no layer, even though the tree gives it a parent');
});

test('cc_getid reports the sub-id for a component the cache never named', () => {
    const { tree, host } = harness();
    const parent = tree.push({ componentId: (0x0500 << 16) | 0 });
    const dynamic = tree.push({ parentIndex: parent, subId: 0x8003, dynamic: true });
    host.setActive(dynamic);
    assert.equal(host.cc_getid(), 0x8003);
});

test('if_hassub answers whether the group is mounted', () => {
    const { tree, host } = harness();
    bakeGroup(tree, 0x0271);
    assert.equal(host.if_hassub(0x0271), 1);
    assert.equal(host.if_hassub(0x0999), 0);
});

/* -------------------------------------------------------------------------
 * Runtime params and copying
 * ---------------------------------------------------------------------- */

test('the component param table starts empty and only the setter fills it', () => {
    /* An OldSchool IF3 file has no param section at all, so a read before any
     * write must answer the ParamType default — which is what the scripts'
     * `= -1` guards are testing for. */
    const { tree, host } = harness();
    const node = tree.push({ componentId: (0x0600 << 16) | 1 });
    host.setActive(node);
    assert.equal(host.cc_getcomponentparam(600), -1, 'the ParamType default');
    host.cc_setcomponentparam(600, 0, 4);
    assert.equal(host.cc_getcomponentparam(600), 4);
});

test('the param getter never answers a string', () => {
    /* Its opcode arity is int-out; answering a string would unbalance the
     * caller's stack. */
    const { tree, host } = harness();
    const node = tree.push({ componentId: (0x0600 << 16) | 2 });
    host.setActive(node);
    host.cc_setcomponentparam(601, 2, 'a label');
    assert.equal(host.cc_getcomponentparam(601), 0);
});

test('cc_copy clones the child rather than no-oping', () => {
    /*
     * The bank's tab strip builds tab 0 and copies it into slots 1..9. An
     * unimplemented copy leaves its arguments on the stack and every following
     * cc_setposition retargets the one widget that was made — the whole strip
     * collapses onto the last iteration's x.
     */
    const { tree, host } = harness();
    const parent = tree.push({ componentId: (0x0800 << 16) | 1 });
    const source = tree.push({
        parentIndex: parent, subId: 0, dynamic: true, type: WIDGET_TYPE.GRAPHIC,
        props: { colour: 0x00ff00, width: 30 },
    });
    tree.at(source).ops = ['Select'];

    host.cc_copy((0x0800 << 16) | 1, 0, 1);
    const copy = tree.findChildBySubId(parent, 1);
    assert.ok(copy, 'the copy exists');
    assert.equal(copy.type, WIDGET_TYPE.GRAPHIC);
    assert.equal(copy.props.colour, 0x00ff00);
    assert.deepEqual(copy.ops, ['Select']);
    assert.notEqual(copy.index, source, 'and it is a different component');
});

test('cc_createchild attaches under the active component', () => {
    const { tree, host } = harness();
    const parent = tree.push({ componentId: (0x0900 << 16) | 1 });
    host.setActive(parent);
    host.cc_createchild(WIDGET_TYPE.TEXT, 0x8000);
    assert.equal(tree.children(parent).length, 1);
    assert.equal(host.activeNode().type, WIDGET_TYPE.TEXT,
        'and becomes the new active component');
});

/* -------------------------------------------------------------------------
 * Coverage is still honest
 * ---------------------------------------------------------------------- */

test('an operation with no implementation still throws by name', () => {
    const { host } = harness();
    assert.throws(() => host.stockmarket_getofferprice(0),
        (error) => error instanceof UnimplementedHostOp);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
