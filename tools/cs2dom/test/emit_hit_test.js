/*
 * The draw walk and the hit tests, and the rule they share.
 *
 * The most important test in this file is the last one: whatever the draw walk
 * prunes, the hit tests must prune too. A widget that draws but cannot be hit
 * is a dead button, and one that hits but does not draw is a click landing on
 * nothing — both have shipped in the C client, from exactly this divergence.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import { createLayout } from '../src/layout.js';
import { createEmitter, EMIT_KIND, intersect, shouldPrune } from '../src/emit.js';
import { createHitTester } from '../src/hit_test.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function harness(root = { x: 0, y: 0, width: 800, height: 600 }) {
    const tree = createUITree();
    const layout = createLayout({ tree, root });
    const emitter = createEmitter({ tree, layout });
    const hits = createHitTester({ tree, layout });
    const settle = () => { layout.resolve(); emitter.walk({ force: true }); };
    return { tree, layout, emitter, hits, settle };
}

/* `componentId` is node identity, not a prop — spreading it into props gives
 * the node id -1 and every hit test then answers about the wrong thing. */
function box(tree, {
    parent = -1, type, componentId = -1, subId, dynamic,
    x = 0, y = 0, width = 0, height = 0, ...props
}) {
    return tree.push({
        parentIndex: parent, type, componentId,
        ...(subId === undefined ? {} : { subId }),
        ...(dynamic === undefined ? {} : { dynamic }),
        props: { x, y, width, height, xMode: 0, yMode: 0, widthMode: 0, heightMode: 0, ...props },
    });
}

/* -------------------------------------------------------------------------
 * Pass order
 * ---------------------------------------------------------------------- */

test('one interleaved pass: text draws where the tree puts it', () => {
    /*
     * NOT a text pass. Lifting every text above every non-text is the
     * tempting version and it is the bug the C client already fixed: a widget
     * group that should cover an earlier one — an open dropdown over a label —
     * draws under that label's text.
     *
     * The reference emits a widget's own fill, sprite and text inline and then
     * descends, in tree order. Interface 600's stone border is the smallest
     * witness: its title text is the SECOND thing the client draws, and a text
     * pass moves it past all 95 of the panel's other texts.
     */
    const { tree, emitter, settle } = harness();
    const panel = box(tree, { type: WIDGET_TYPE.LAYER, width: 200, height: 100 });
    box(tree, { parent: panel, type: WIDGET_TYPE.TEXT, width: 100, height: 20, text: 'label' });
    box(tree, { parent: panel, type: WIDGET_TYPE.RECTANGLE, width: 200, height: 100 });
    settle();

    const kinds = emitter.commands.map((c) => c.kind);
    assert.deepEqual(kinds, [EMIT_KIND.TEXT, EMIT_KIND.RECT],
        'tree order, so the later sibling covers the earlier one');
});

test('the drag pass is skipped entirely when nothing is being dragged', () => {
    /*
     * Every node that pass reaches would take the descend-only branch, and
     * descend-only bypasses the collapsed-layer prune — which made it the
     * single largest traversal in the C client, all of it waste on an
     * ordinary frame.
     */
    const { tree, emitter, settle } = harness();
    const panel = box(tree, { type: WIDGET_TYPE.LAYER, width: 200, height: 100 });
    box(tree, { parent: panel, type: WIDGET_TYPE.RECTANGLE, width: 50, height: 50 });
    settle();

    const oneWalk = emitter.stats.visited;
    assert.equal(tree.hasActiveDrag(), false);

    tree.setDragActive(panel, true);
    emitter.walk({ force: true });
    assert.ok(emitter.stats.visited - oneWalk > oneWalk,
        'with a drag running the second pass is walked');
});

test('a layer draws nothing of its own', () => {
    const { tree, emitter, settle } = harness();
    box(tree, { type: WIDGET_TYPE.LAYER, width: 200, height: 100 });
    settle();
    assert.equal(emitter.commands.length, 0);
});

/* -------------------------------------------------------------------------
 * Clipping and scrolling
 * ---------------------------------------------------------------------- */

test('a child is clipped to its layer, not to an ancestor', () => {
    const { tree, emitter, settle } = harness();
    const outer = box(tree, { type: WIDGET_TYPE.LAYER, width: 400, height: 400 });
    const inner = box(tree, {
        parent: outer, type: WIDGET_TYPE.LAYER, x: 50, y: 50, width: 100, height: 100,
    });
    box(tree, { parent: inner, type: WIDGET_TYPE.RECTANGLE, width: 500, height: 500 });
    settle();

    const [command] = emitter.commands;
    assert.deepEqual(command.clip, { x: 50, y: 50, width: 100, height: 100 });
});

test('a collapsed clipping layer removes its whole subtree', () => {
    const { tree, emitter, settle } = harness();
    const layer = box(tree, { type: WIDGET_TYPE.LAYER, width: 0, height: 0 });
    box(tree, { parent: layer, type: WIDGET_TYPE.RECTANGLE, width: 50, height: 50 });
    settle();
    assert.equal(emitter.commands.length, 0);
});

test('a clip rect never exceeds the surface', () => {
    /*
     * A scissor rect can be authored larger than the canvas; an unclamped
     * overflow wraps rows in the painter, so the clamp belongs here and the
     * painter never sees one.
     */
    const clipped = intersect(
        { x: -10, y: -10, width: 2000, height: 2000 },
        { x: 0, y: 0, width: 800, height: 600 });
    assert.deepEqual(clipped, { x: 0, y: 0, width: 800, height: 600 });
});

test('scroll offsets move children and the clip stays put', () => {
    const { tree, emitter, settle } = harness();
    const list = box(tree, {
        type: WIDGET_TYPE.LAYER, x: 10, y: 20, width: 100, height: 50,
        scrollHeight: 500, scrollY: 40,
    });
    box(tree, { parent: list, type: WIDGET_TYPE.RECTANGLE, y: 100, width: 100, height: 16 });
    settle();

    const [command] = emitter.commands;
    assert.equal(command.y, 80, 'row at content-y 100, scrolled down 40, layer at y 20');
    assert.deepEqual(command.clip, { x: 10, y: 20, width: 100, height: 50 },
        'the window does not scroll with its content');
});

/* -------------------------------------------------------------------------
 * Retention
 * ---------------------------------------------------------------------- */

test('an idle frame does not walk', () => {
    const { tree, emitter, layout } = harness();
    box(tree, { type: WIDGET_TYPE.RECTANGLE, width: 10, height: 10 });
    layout.resolve();
    assert.equal(emitter.walk({}), true);

    assert.equal(emitter.walk({}), false, 'nothing changed');
    assert.equal(emitter.stats.retained, 1);
});

test('a hover change alone rebuilds the list', () => {
    /*
     * The hovered component swaps its colour, text and sprite variants, so a
     * hover is a repaint even though nothing in the tree is dirty. This is the
     * second of the gate's three terms.
     */
    const { tree, emitter, layout } = harness();
    box(tree, { type: WIDGET_TYPE.RECTANGLE, width: 10, height: 10, });
    layout.resolve();
    emitter.walk({ hoveredComponentId: -1 });
    assert.equal(emitter.walk({ hoveredComponentId: 5 }), true);
});

test('a layout resolve alone rebuilds the list', () => {
    /*
     * The third term. A resolve can move a clip rect while no node's own dirty
     * flag was raised — measured once at emit #5 of a 2,000-frame run, where a
     * clip width moved 765 -> 807 under a gate that called the frame quiet.
     */
    const { tree, emitter, layout } = harness();
    box(tree, { type: WIDGET_TYPE.RECTANGLE, width: 0, height: 0, widthMode: 1, heightMode: 1 });
    layout.resolve();
    emitter.walk({});

    layout.setRoot({ width: 1024, height: 768 });
    layout.resolve();
    assert.equal(emitter.walk({}), true);
});

/* -------------------------------------------------------------------------
 * Hit testing
 * ---------------------------------------------------------------------- */

test('a layer passes clicks through; a hooked node does not', () => {
    const { tree, hits, settle } = harness();
    const panel = box(tree, {
        type: WIDGET_TYPE.LAYER, componentId: 0x10001, width: 200, height: 200,
    });
    const button = tree.push({
        parentIndex: panel, componentId: 0x10002, type: WIDGET_TYPE.TEXT,
        props: { x: 10, y: 10, width: 50, height: 20 },
    });
    tree.setHook(button, 'onClick', { scriptId: 1, args: [], triggers: [] });
    settle();

    assert.equal(hits.hitTest(20, 15).componentId, 0x10002);
    assert.equal(hits.hitTest(150, 150), null, 'the bare layer is not a click sink');
});

test('a decorative node with an op is a target; without one it is not', () => {
    const { tree, hits, settle } = harness();
    const plain = box(tree, {
        type: WIDGET_TYPE.GRAPHIC, componentId: 0x20001, width: 40, height: 40,
    });
    const withOp = box(tree, {
        type: WIDGET_TYPE.GRAPHIC, componentId: 0x20002, x: 100, width: 40, height: 40,
    });
    tree.at(withOp).ops = ['Use'];
    settle();

    assert.equal(hits.hitTest(10, 10), null);
    assert.equal(hits.hitTest(110, 10).componentId, 0x20002);
});

test('last match wins, so a later sibling takes the click', () => {
    const { tree, hits, settle } = harness();
    const under = box(tree, {
        type: WIDGET_TYPE.TEXT, componentId: 0x30001, width: 100, height: 100,
    });
    const over = box(tree, {
        type: WIDGET_TYPE.TEXT, componentId: 0x30002, width: 100, height: 100,
    });
    tree.setHook(under, 'onClick', { scriptId: 1, args: [], triggers: [] });
    tree.setHook(over, 'onClick', { scriptId: 2, args: [], triggers: [] });
    settle();
    assert.equal(hits.hitTest(50, 50).componentId, 0x30002);
});

test('noClickThrough blocks the click without becoming the answer', () => {
    const { tree, hits, settle } = harness();
    const behind = box(tree, {
        type: WIDGET_TYPE.TEXT, componentId: 0x40001, width: 800, height: 600,
    });
    tree.setHook(behind, 'onClick', { scriptId: 1, args: [], triggers: [] });
    box(tree, {
        type: WIDGET_TYPE.LAYER, componentId: 0x40002, x: 100, y: 100,
        width: 200, height: 200, noClickThrough: 1,
    });
    settle();

    assert.equal(hits.hitTest(50, 50).componentId, 0x40001, 'outside the modal');
    assert.equal(hits.hitTest(150, 150).componentId, 0x40002,
        'inside it, the click stops at the modal rather than reaching the world');
});

test('a click outside a scrolling layer\'s window misses its rows', () => {
    const { tree, hits, settle } = harness();
    const list = box(tree, {
        type: WIDGET_TYPE.LAYER, componentId: 0x50001, width: 100, height: 50,
        scrollHeight: 500,
    });
    const row = tree.push({
        parentIndex: list, componentId: 0x50002, type: WIDGET_TYPE.TEXT,
        props: { x: 0, y: 200, width: 100, height: 16 },
    });
    tree.setHook(row, 'onClick', { scriptId: 1, args: [], triggers: [] });
    settle();

    assert.equal(hits.hitTest(50, 205), null,
        'the row is inside the content but outside the 50-pixel window');
});

/* -------------------------------------------------------------------------
 * The rule they share
 * ---------------------------------------------------------------------- */

test('whatever the draw walk prunes, the hit tests prune too', () => {
    /*
     * THE test in this file. Hover once pruned only hidden LAYERS while emit
     * pruned every hidden node, so `if_sethide` on a type-5 spell icon left
     * its mouse-repeat hook live and jewellery-enchant tooltips pierced
     * through to the spellbook underneath.
     *
     * Every widget type, hidden, checked against all three answers.
     */
    for( const type of Object.values(WIDGET_TYPE) )
    {
        const { tree, hits, emitter, settle } = harness();
        /*
         * Content appropriate to the type, because presence is not decided by
         * the type alone: an EMPTY text and an ABSENT model emit nothing even
         * when visible, which is the reference's own rule. Without this the
         * "and it was reachable before hiding" guard below fires, and the test
         * would be proving nothing for two of the six types.
         */
        const node = box(tree, {
            type, componentId: 0x60001, width: 100, height: 100,
            text: 'x', model: 7,
        });
        tree.setHook(node, 'onClick', { scriptId: 1, args: [], triggers: [] });
        tree.setHook(node, 'onMouseRepeat', { scriptId: 2, args: [], triggers: [] });

        settle();
        const drewWhenVisible = emitter.commands.length;

        tree.setHidden(node, true);
        settle();

        assert.equal(emitter.commands.length, 0, `type ${type} still drew while hidden`);
        assert.equal(hits.hitTest(50, 50), null, `type ${type} still clicked while hidden`);
        assert.equal(hits.hoverTarget(50, 50), -1, `type ${type} still hovered while hidden`);
        assert.equal(hits.dropTarget(50, 50), null, `type ${type} still dropped while hidden`);
        assert.equal(shouldPrune(tree.at(node)), true);
        /*
         * And it was reachable before hiding, or the test proves nothing —
         * for the types that draw at all. A LAYER draws nothing of its own,
         * and an OBJ box and an ARC draw nothing HERE: the C client emits
         * them as its own `CC_OBJ` and `ARC` kinds, which this preview does
         * not host. They are still walked, still hidden and still hit-tested,
         * which is what this test is about.
         */
        const drawsNothing = [WIDGET_TYPE.LAYER, WIDGET_TYPE.OBJ, WIDGET_TYPE.ARC];
        if( !drawsNothing.includes(type) )
            assert.ok(drewWhenVisible > 0, `type ${type} never drew at all`);
    }
});

test('a hidden ancestor prunes its subtree from all three walks', () => {
    const { tree, hits, emitter, settle } = harness();
    const panel = box(tree, { type: WIDGET_TYPE.LAYER, width: 200, height: 200 });
    const child = tree.push({
        parentIndex: panel, componentId: 0x70001, type: WIDGET_TYPE.TEXT,
        props: { x: 0, y: 0, width: 100, height: 100 },
    });
    tree.setHook(child, 'onClick', { scriptId: 1, args: [], triggers: [] });

    tree.setHidden(panel, true);
    settle();
    assert.equal(emitter.commands.length, 0);
    assert.equal(hits.hitTest(10, 10), null);
    assert.equal(hits.hoverTarget(10, 10), -1);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
