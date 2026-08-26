/*
 * Layout, against the arithmetic the client actually does.
 *
 * The modes are easy to implement plausibly and hard to implement exactly, and
 * a plausible one is right until the window is a different size. The cases
 * below are the ones where "obvious" and "correct" diverge: the 64-bit
 * fractional product, truncation toward zero on a negative overhang, a
 * scrolling layer's children laid out against the CONTENT rather than the
 * window, and the barrier that makes a getter see a size the same script just
 * set.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import { createHostKernel, HostState, ReadyAssetSource } from '../src/host_kernel.js';
import {
    attachLayout, axisFromPositionMode, computeSize, createLayout,
    dimFromParentMode, mulShift14,
} from '../src/layout.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/* -------------------------------------------------------------------------
 * The primitives
 * ---------------------------------------------------------------------- */

test('the fractional product is 64-bit, not a double', () => {
    /*
     * `(a * b) >> 14` where a is a parent dimension and b a 14-bit fraction.
     * At realistic sizes the product passes 2^31, where `>>` on a JS number
     * has already coerced to int32 and lost the high bits. The client does the
     * multiply at 64 bits and shifts after.
     */
    assert.equal(mulShift14(16384, 16384), 16384);
    assert.equal(mulShift14(765, 8192), 382, 'half of 765, truncated');
    /* The case a 32-bit intermediate gets wrong: */
    const wide = mulShift14(1000000, 16384);
    assert.equal(wide, 1000000);
    assert.notEqual((1000000 * 16384) >> 14, wide,
        'the int32 route disagrees, which is why this goes through BigInt');
});

test('dimension modes read the parent the way the cache means them', () => {
    assert.equal(dimFromParentMode(0, 40, 500), 40, 'literal');
    assert.equal(dimFromParentMode(1, 40, 500), 460, 'parent minus base');
    assert.equal(dimFromParentMode(2, 8192, 500), 250, 'half the parent');
});

test('centring truncates toward zero, including for a negative overhang', () => {
    /*
     * -27 / 2 is -13 in Java and C, and -14 under an arithmetic shift. The
     * difference shows only when the component is WIDER than its parent, which
     * is a real case — a fixed-width panel inside a narrowed container.
     */
    assert.equal(axisFromPositionMode(1, 0, 0, 100, 127), -13);
    assert.notEqual(axisFromPositionMode(1, 0, 0, 100, 127), (100 - 127) >> 1);
});

test('every position mode anchors where it says', () => {
    const parentOrigin = 10;
    const parentDim = 200;
    const selfDim = 40;
    assert.equal(axisFromPositionMode(0, 5, parentOrigin, parentDim, selfDim), 15);
    assert.equal(axisFromPositionMode(1, 0, parentOrigin, parentDim, selfDim), 90);
    assert.equal(axisFromPositionMode(2, 5, parentOrigin, parentDim, selfDim), 165);
    assert.equal(axisFromPositionMode(3, 8192, parentOrigin, parentDim, selfDim), 110);
    assert.equal(axisFromPositionMode(5, 8192, parentOrigin, parentDim, selfDim), 70);
});

test('a size goes negative, and only the aspect path floors it', () => {
    /*
     * `setsize_minus` with a base larger than the parent really does produce a
     * negative, and the reference keeps it: `uitree_layout.c` takes
     * `dim_from_parent_mode` straight, and only `UITree_If3ComputeSize` — which
     * it calls solely when an axis is in aspect mode — clamps at zero.
     *
     * It is observable, not academic: a centred axis divides `parent - self`
     * by two, so flooring the size moves the box as well as resizing it. Twelve
     * interfaces drew a 36x-18 sprite nine pixels below where they should.
     */
    const { width } = computeSize({ widthMode: 1, width: 900, heightMode: 0, height: 10 },
        500, 500);
    assert.equal(width, -400);

    const aspect = computeSize(
        { widthMode: 4, width: 0, heightMode: 1, height: 900, aspectWidth: 1, aspectHeight: 1 },
        500, 500);
    assert.equal(aspect.height, 0);
    assert.equal(aspect.width, 0);
});

test('an aspect mode derives one axis from the other', () => {
    const size = computeSize(
        { widthMode: 4, width: 0, heightMode: 0, height: 60, aspectWidth: 4, aspectHeight: 3 },
        500, 500);
    assert.equal(size.height, 60);
    assert.equal(size.width, 80);
});

/* -------------------------------------------------------------------------
 * Resolution
 * ---------------------------------------------------------------------- */

function harness(root) {
    const tree = createUITree();
    const layout = createLayout({ tree, ...(root ? { root } : {}) });
    return { tree, layout };
}

test('children resolve against their parent, parents first', () => {
    const { tree, layout } = harness({ x: 0, y: 0, width: 800, height: 600 });
    const panel = tree.push({
        type: WIDGET_TYPE.LAYER,
        props: { x: 100, y: 50, width: 200, height: 300, xMode: 0, yMode: 0 },
    });
    const child = tree.push({
        parentIndex: panel, type: WIDGET_TYPE.TEXT,
        props: { x: 0, y: 0, width: 0, height: 20, xMode: 1, yMode: 0, widthMode: 1 },
    });

    layout.resolve();
    assert.deepEqual(layout.boxOf(panel), { x: 100, y: 50, width: 200, height: 300 });
    assert.deepEqual(layout.boxOf(child), { x: 100, y: 50, width: 200, height: 20 },
        'centred in a 200-wide parent at full width');
});

test('a scrolling layer lays its children out against the CONTENT', () => {
    /*
     * The children of a scroll layer are placed inside the scroll extent, not
     * the visible window — that is what makes row 400 of a list exist at all.
     * Every write to a scroll extent therefore has to invalidate the layout.
     */
    const { tree, layout } = harness({ x: 0, y: 0, width: 800, height: 600 });
    const list = tree.push({
        type: WIDGET_TYPE.LAYER,
        props: { x: 0, y: 0, width: 200, height: 100, scrollHeight: 4000 },
    });
    const row = tree.push({
        parentIndex: list, type: WIDGET_TYPE.TEXT,
        props: { x: 0, y: 0, width: 0, height: 0, heightMode: 1 },
    });

    layout.resolve();
    assert.equal(layout.boxOf(row).height, 4000,
        'a full-height child fills the content, not the 100-pixel window');
});

test('an idle frame does not walk', () => {
    const { tree, layout } = harness();
    tree.push({ props: { width: 10, height: 10 } });
    layout.resolve();
    const visited = layout.stats.nodesVisited;

    assert.equal(layout.resolve(), false, 'nothing invalidated');
    assert.equal(layout.stats.nodesVisited, visited);
    assert.equal(layout.stats.skipped, 1);
});

test('a geometry write invalidates and a paint write does not', () => {
    const { tree, layout } = harness();
    const node = tree.push({ props: { width: 10, height: 10 } });
    layout.resolve();

    tree.setProp(node, 'colour', 0xff0000);
    assert.equal(layout.resolve(), false, 'a colour cannot move a box');

    tree.setProp(node, 'width', 20, 'geometry');
    assert.equal(layout.resolve(), true);
    assert.equal(layout.boxOf(node).width, 20);
});

test('resizing the canvas re-resolves', () => {
    const { tree, layout } = harness({ x: 0, y: 0, width: 800, height: 600 });
    const full = tree.push({ props: { width: 0, height: 0, widthMode: 1, heightMode: 1 } });
    layout.resolve();
    assert.equal(layout.boxOf(full).width, 800);

    assert.equal(layout.setRoot({ width: 1024, height: 768 }), true);
    layout.resolve();
    assert.equal(layout.boxOf(full).width, 1024);

    assert.equal(layout.setRoot({ width: 1024, height: 768 }), false,
        'the same box is not a change');
});

/* -------------------------------------------------------------------------
 * The barrier
 * ---------------------------------------------------------------------- */

test('a getter sees the size the same script just set', () => {
    /*
     * The dropdown sizes its scrollbar dragger by reading `if_getwidth`
     * immediately after `if_setsize`. If the read answered the authored value
     * it would get the mode operand (-0, 18) rather than pixels; if it waited
     * for end of frame it would get the previous size.
     */
    const tree = createUITree();
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(),
    });
    attachLayout(host, { root: { x: 0, y: 0, width: 500, height: 400 } });

    const panel = tree.push({
        componentId: (0x0100 << 16) | 1,
        props: { x: 0, y: 0, width: 500, height: 400 },
    });
    const bar = tree.push({
        parentIndex: panel, componentId: (0x0100 << 16) | 2,
        props: { x: 0, y: 0, width: 500, height: 400 },
    });
    host.onLayoutNeeded();

    /* "Full width less 18 pixels", written as the cache writes it. */
    host.if_setsize(18, 0, 1, 1, (0x0100 << 16) | 2);
    assert.equal(host.if_getwidth((0x0100 << 16) | 2), 482,
        'the resolved pixels, mid-script, without waiting for the frame');
    assert.equal(host.if_getheight((0x0100 << 16) | 2), 400);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
