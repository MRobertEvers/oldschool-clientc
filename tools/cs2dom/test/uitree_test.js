/*
 * UITreeJS against the rules it was ported to keep.
 *
 * Each test names a behaviour the C tree has for a reason recorded in
 * src/ui/uitree.h — slot recycling, find precedence, the child-key ceiling,
 * re-arm skipping. A port that gets the shape right and these wrong looks
 * correct and misbehaves only under the workloads that matter.
 */

import assert from 'node:assert/strict';

import {
    createUITree, DIRTY, DYNAMIC_SUB_ID_BASE, WIDGET_TYPE, UITreeError,
} from '../src/uitree.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

test('a reclaimed slot is reused, and the old reference stops resolving', () => {
    const tree = createUITree();
    const root = tree.push({});
    const child = tree.push({ parentIndex: root, componentId: 0x10001 });
    const reference = tree.ref(child);
    assert.ok(tree.resolve(reference));

    tree.remove(child);
    const replacement = tree.push({ parentIndex: root, componentId: 0x10002 });

    assert.equal(replacement, child, 'the free list should hand back the same slot');
    assert.equal(tree.resolve(reference), null,
        'a stale reference must not resolve to the slot\'s new occupant');
});

test('cc_find prefers the dynamic child over the cache-baked one', () => {
    const tree = createUITree();
    const parent = tree.push({});
    const baked = tree.push({ parentIndex: parent, subId: 4, dynamic: false });
    const dynamic = tree.push({ parentIndex: parent, subId: 4, dynamic: true });

    assert.equal(tree.findChildBySubId(parent, 4).index, dynamic,
        'a script must find the row it just created, not the template under it');
    tree.remove(dynamic);
    assert.equal(tree.findChildBySubId(parent, 4).index, baked);
});

test('a miss above the child-key ceiling answers without walking', () => {
    const tree = createUITree();
    const parent = tree.push({});
    for( let i = 0; i < 50; i++ ) tree.push({ parentIndex: parent, subId: i });
    assert.equal(tree.findChildBySubId(parent, 500), null);
    assert.equal(tree.findChildBySubId(parent, 49).subId, 49);
});

test('removing a child leaves the ceiling recomputable, never too low', () => {
    /*
     * A stale-high ceiling costs a scan; a stale-low one reports "no such
     * child" for a child that is there. Removal cannot know the new maximum
     * without walking, so it must mark the ceiling unknown rather than guess.
     */
    const tree = createUITree();
    const parent = tree.push({});
    const low = tree.push({ parentIndex: parent, subId: 1 });
    const high = tree.push({ parentIndex: parent, subId: 9 });
    tree.remove(high);
    assert.equal(tree.findChildBySubId(parent, 1).index, low);
    assert.equal(tree.findChildBySubId(parent, 9), null);
});

test('duplicate sub-ids fall back to sibling order rather than answering wrongly', () => {
    const tree = createUITree();
    const parent = tree.push({});
    const first = tree.push({ parentIndex: parent, subId: 2 });
    tree.push({ parentIndex: parent, subId: 2 });
    assert.equal(tree.findChildBySubId(parent, 2).index, first,
        'with ambiguous keys only the walk is authoritative');
});

test('filling a container is linear, not quadratic', () => {
    /*
     * The shape that matters: create a row, then find it back, 2,000 times.
     * Without the tail hint and the key index this is the chatbox stall.
     */
    const build = (rows) => {
        const tree = createUITree();
        const parent = tree.push({});
        for( let i = 0; i < rows; i++ )
        {
            const sub = DYNAMIC_SUB_ID_BASE + i;
            tree.push({ parentIndex: parent, subId: sub, dynamic: true });
            assert.equal(tree.findChildBySubId(parent, sub).subId, sub);
        }
        assert.equal(tree.children(parent).length, rows);
        return tree.walkSteps;
    };

    /*
     * Doubling the rows must not quadruple the work. A wall-clock assertion
     * would pass on a quadratic tree at these sizes — 2,000 rows is only four
     * million steps — so the sibling-walk counter is what actually decides it.
     */
    const small = build(1000);
    const large = build(2000);
    assert.ok(large <= small * 2 + 64,
        `walk steps went ${small} -> ${large}; doubling the rows should not more than double them`);
});

test('an identical hook re-arm is skipped', () => {
    const tree = createUITree();
    const node = tree.push({});
    const binding = { scriptId: 42, args: [1, 'x'], triggers: [300] };
    assert.equal(tree.setHook(node, 'onClick', binding), true);
    assert.equal(tree.setHook(node, 'onClick', { scriptId: 42, args: [1, 'x'], triggers: [300] }),
        false, 'a restated binding must not be rebuilt');
    assert.equal(tree.setHook(node, 'onClick', { scriptId: 42, args: [1, 'y'], triggers: [300] }),
        true, 'a different argument is a different hook');
});

test('hook slots are indexed so a per-tick pass does not scan the tree', () => {
    const tree = createUITree();
    const root = tree.push({});
    for( let i = 0; i < 100; i++ ) tree.push({ parentIndex: root });
    const timed = tree.push({ parentIndex: root });
    tree.setHook(timed, 'onTimer', { scriptId: 7, args: [], triggers: [] });

    assert.deepEqual(tree.nodesWithHook('onTimer'), [timed]);
    tree.setHook(timed, 'onTimer', null);
    assert.deepEqual(tree.nodesWithHook('onTimer'), []);
});

test('a reclaimed node leaves no entry behind in any index', () => {
    const tree = createUITree();
    const root = tree.push({});
    const node = tree.push({ parentIndex: root, componentId: 0x20003 });
    tree.setHook(node, 'onTimer', { scriptId: 1, args: [], triggers: [] });
    assert.ok(tree.findByComponentId(0x20003));
    assert.equal(tree.hasGroup(2), true);

    tree.remove(node);
    assert.equal(tree.findByComponentId(0x20003), null);
    assert.deepEqual(tree.nodesWithHook('onTimer'), []);
    assert.equal(tree.hasGroup(2), false);
});

test('cc_deleteall clears the DYNAMIC children and keeps the node', () => {
    const tree = createUITree();
    const parent = tree.push({});
    for( let i = 0; i < 5; i++ )
        tree.push({ parentIndex: parent, subId: i, dynamic: true });
    assert.equal(tree.removeChildren(parent), 5);
    assert.ok(tree.at(parent));
    assert.deepEqual(tree.children(parent), []);
    assert.equal(tree.findChildBySubId(parent, 2), null);
});

test('creating a sub-id that is taken REPLACES it', () => {
    /*
     * Replace-in-slot. `cc_create` on a sub-id an existing DYNAMIC child
     * already has reclaims that child first, so a rebuild script does not grow
     * the tree — the reference reclaims before it allocates the new uid, so
     * the freed slot and uid are immediately reusable.
     *
     * Interface 600's kudos list is the witness: its transmit hook rebuilds
     * the stripe rows, and without this the list went from 43 rectangles to
     * 86 the first time the hook fired.
     */
    const tree = createUITree();
    const parent = tree.push({ componentId: 0x10000 });
    const first = tree.push({ parentIndex: parent, subId: 4, dynamic: true });
    /* Through a REF, because storage is not identity: the reclaimed slot is
     * the first thing the free list hands back, so `at(first)` resolves to the
     * replacement and would prove nothing. */
    const reference = tree.ref(first);

    assert.equal(tree.reclaimDynamicChild(parent, 4), true);
    const second = tree.push({ parentIndex: parent, subId: 4, dynamic: true });

    assert.equal(tree.resolve(reference), null, 'the old row is reclaimed');
    assert.equal(tree.children(parent).length, 1, 'and not left beside the new one');
    assert.equal(tree.findChildBySubId(parent, 4).index, second);
});

test('a static child in the slot is left alone', () => {
    const tree = createUITree();
    const parent = tree.push({ componentId: 0x10000 });
    const baked = tree.push({ parentIndex: parent, subId: 4 });
    assert.equal(tree.reclaimDynamicChild(parent, 4), false);
    assert.ok(tree.at(baked), 'a cache-built widget is not a script\'s to replace');
});

test('an UNKNOWN child-key ceiling stays unknown across an insert', () => {
    /*
     * One insert cannot tighten a ceiling that is unknown: the real maximum is
     * at least whatever the surviving siblings carry, and only a walk can say
     * what that is.
     *
     * Setting it to the new child's sub-id instead makes the ceiling REJECT
     * children that are there — the lookup answers "no such child" ABOVE the
     * ceiling without walking. A rebuild is where it shows: remove-then-add
     * row 0 pins the ceiling at 0, every later row is then unfindable, the
     * replace-in-slot above never happens and the container doubles.
     */
    const tree = createUITree();
    const parent = tree.push({ componentId: 0x10000 });
    for( let subId = 0; subId < 8; subId++ )
        tree.push({ parentIndex: parent, subId, dynamic: true });

    /* Reclaiming row 0 drops the ceiling to unknown; re-adding row 0 must not
     * pin it there. */
    tree.reclaimDynamicChild(parent, 0);
    tree.push({ parentIndex: parent, subId: 0, dynamic: true });

    for( let subId = 1; subId < 8; subId++ )
        assert.ok(tree.findChildBySubId(parent, subId),
            `row ${subId} became unfindable above a pinned ceiling`);
});

test('cc_deleteall leaves the cache-built children alone', () => {
    /*
     * The rule, not a refinement of it. A static child is not something a
     * script created and not something it can rebuild, so deleting one leaves
     * a hole nothing fills — the spellbook's onload calls deleteall on the
     * layer holding all 199 of its spell icons, and clearing those with the
     * dynamic ones left an interface that drew nothing at all.
     */
    const tree = createUITree();
    const parent = tree.push({});
    const statics = [0, 1, 2].map((subId) => tree.push({ parentIndex: parent, subId }));
    const dynamics = [3, 4].map((subId) =>
        tree.push({ parentIndex: parent, subId, dynamic: true }));

    assert.equal(tree.removeChildren(parent), 2, 'only the two dynamic ones');
    assert.deepEqual(tree.children(parent), statics, 'in their original order');
    for( const index of dynamics ) assert.equal(tree.at(index), null);
    assert.equal(tree.findChildBySubId(parent, 1).index, statics[1],
        'and the survivors keep their sub-ids');
});

test('a deleteall with nothing dynamic to remove changes nothing at all', () => {
    /*
     * The reference rebuilds a list by clearing it and re-adding rows, so the
     * steady-state call is a deleteall on an already-empty parent. Bumping the
     * generation there made the whole tree look modified every frame and held
     * the emit retention gate at zero hits for a whole run.
     */
    const tree = createUITree();
    const parent = tree.push({});
    tree.push({ parentIndex: parent, subId: 0 });
    tree.layoutStale = false;
    const generation = tree.generation;
    const dirty = tree.dirtyGeneration;

    assert.equal(tree.removeChildren(parent), 0);
    assert.equal(tree.generation, generation, 'no topology change');
    assert.equal(tree.dirtyGeneration, dirty, 'and nothing marked dirty');
    assert.equal(tree.layoutStale, false, 'and no relayout asked for');
});

test('deleting a subtree reclaims every descendant', () => {
    const tree = createUITree();
    const root = tree.push({});
    const branch = tree.push({ parentIndex: root });
    const leafA = tree.push({ parentIndex: branch });
    const leafB = tree.push({ parentIndex: leafA });
    assert.equal(tree.remove(branch), 3);
    assert.equal(tree.at(leafB), null);
    assert.equal(tree.children(root).length, 0);
});

test('geometry writes stale the layout; paint writes do not', () => {
    const tree = createUITree();
    const node = tree.push({ props: { x: 0, width: 10 } });
    tree.layoutStale = false;
    tree.setProp(node, 'colour', 0xff0000, DIRTY.PAINT);
    assert.equal(tree.layoutStale, false);
    tree.setProp(node, 'width', 20, DIRTY.GEOMETRY);
    assert.equal(tree.layoutStale, true);
});

test('visibility is its own dirt, not paint', () => {
    /*
     * A node the last paint pruned was never visited, so revealing it changes
     * the next output in a way a paint bump does not describe.
     */
    const tree = createUITree();
    const node = tree.push({});
    tree.clearDirty();
    tree.setHidden(node, true);
    assert.deepEqual(tree.dirtyNodes(DIRTY.VISIBILITY), [node]);
    assert.deepEqual(tree.dirtyNodes(DIRTY.PAINT), []);
});

test('hidden propagates down for the prune test', () => {
    const tree = createUITree();
    const root = tree.push({});
    const mid = tree.push({ parentIndex: root });
    const leaf = tree.push({ parentIndex: mid });
    tree.setHidden(mid, true);
    assert.equal(tree.hiddenByAncestor(leaf), true);
    assert.equal(tree.hiddenByAncestor(root), false);
});

test('topology and dirt advance separate counters', () => {
    /*
     * An id index depends on ids alone, so a `cc_settext` must not invalidate
     * it and a reparent must not invalidate a repaint gate.
     */
    const tree = createUITree();
    const node = tree.push({ componentId: 0x30001 });
    const generation = tree.generation;
    const idGeneration = tree.idGeneration;
    tree.setProp(node, 'text', 'hello');
    assert.equal(tree.generation, generation, 'a text write is not a topology change');
    assert.equal(tree.idGeneration, idGeneration, 'a text write does not touch ids');
    assert.notEqual(tree.revision(), `${generation}:0:${idGeneration}`);
});

test('an unknown hook slot is refused rather than silently dropped', () => {
    const tree = createUITree();
    const node = tree.push({});
    assert.throws(() => tree.setHook(node, 'onWhatever', { scriptId: 1 }),
        (error) => error instanceof UITreeError);
});

test('roots append in order', () => {
    const tree = createUITree();
    const a = tree.push({ type: WIDGET_TYPE.LAYER });
    const b = tree.push({ type: WIDGET_TYPE.LAYER });
    const c = tree.push({ type: WIDGET_TYPE.LAYER });
    const order = [];
    for( let cursor = tree.rootIndex; cursor >= 0; cursor = tree.nodes[cursor].nextSibling )
        order.push(cursor);
    assert.deepEqual(order, [a, b, c]);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
