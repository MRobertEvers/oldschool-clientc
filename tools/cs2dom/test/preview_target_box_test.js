import assert from 'node:assert/strict';

import { IF_TYPE } from '../src/components.js';
import { stateRef, INT } from '../src/expr.js';
import { layout, layoutBox } from '../src/preview.js';

function component(name, fileId, layer, type, props = {}, extra = {}) {
    const fixed = {
        x: 0, y: 0, xMode: 0, yMode: 0,
        width: 10, height: 10, widthMode: 0, heightMode: 0,
        scrollWidth: 0, scrollHeight: 0, scrollX: 0, scrollY: 0,
        hidden: false,
        ...props,
    };
    return {
        name,
        kind: type === IF_TYPE.layer ? 'Layer' : 'Rect',
        type,
        fileId,
        layer,
        static: fixed,
        dynamic: [],
        dependencies: [],
        ops: [],
        events: {},
        hooks: {},
        ...extra,
    };
}

const hidden = stateRef({ kind: 'varp', id: 300, initial: 0 }, INT);
const root = component('root', 0, null, IF_TYPE.layer, {
    x: 10, y: 20, width: 100, height: 80,
    scrollWidth: 200, scrollHeight: 160, scrollX: 30, scrollY: 40,
}, { dynamic: [{ prop: 'hidden', expr: hidden }] });
const inner = component('inner', 1, 0, IF_TYPE.layer, {
    x: 80, y: 50, width: 100, height: 60,
});
const leaf = component('leaf', 2, 1, IF_TYPE.rectangle, {
    x: 70, y: 5, width: 20, height: 20,
});
const collapsed = component('collapsed', 3, 0, IF_TYPE.layer, {
    x: 5, y: 5, width: 0, height: 20,
});
const culledLeaf = component('culled-leaf', 4, 3, IF_TYPE.rectangle);
const inventory = component('inventory', 5, 0, IF_TYPE.inv, {
    x: 20, y: 20, width: 25, height: 25,
});
const inventoryLeaf = component('inventory-leaf', 6, 5, IF_TYPE.rectangle, {
    x: 20, y: 20, width: 20, height: 20,
});
const objectProps = {
    x: 60, y: 20, width: 20, height: 20,
    scrollWidth: 200, scrollHeight: 200, scrollX: 100, scrollY: 100,
};
const object = component('object', 7, 0, IF_TYPE.layer, objectProps, {
    kind: 'Object', runtimeDynamic: true,
});
object.props = object.static;
const objectLeaf = component('object-leaf', 8, 7, IF_TYPE.rectangle, {
    x: 15, y: 15, width: 10, height: 10,
});
const orphan = component('orphan', 9, 0x7fffffff, IF_TYPE.rectangle, {
    x: 3, y: 4, width: 5, height: 6,
});

/* Deliberately put children before parents: the cached structural plan must use
 * the completed file-id map, just like the full native-oriented traversal. */
const ir = {
    components: [leaf, culledLeaf, inventoryLeaf, objectLeaf, orphan,
        inner, collapsed, inventory, object, root],
};
const viewport = { width: 200, height: 100 };

function compare(state) {
    const boxes = layout(ir, state, viewport);
    for( const target of ir.components ) {
        const expected = boxes.find((box) => box.fileId === target.fileId);
        assert.deepEqual(layoutBox(ir, state, viewport, target), expected,
            `target box diverged for ${target.name}`);
        assert.deepEqual(layoutBox(ir, state, viewport, target.fileId), expected,
            `file-id target box diverged for ${target.name}`);
    }
}

compare({ 'varp:300': 0 });
compare({ 'varp:300': 1 });

const leafBox = layoutBox(ir, { 'varp:300': 0 }, viewport, leaf);
assert.deepEqual({ x: leafBox.x, y: leafBox.y }, { x: 130, y: 35 });
assert.deepEqual(leafBox.clip, { left: 60, top: 30, right: 110, bottom: 90 });
assert.equal(layoutBox(ir, { 'varp:300': 0 }, viewport, culledLeaf).culled, true);
assert.equal(layoutBox(ir, { 'varp:300': 1 }, viewport, leaf).effectiveHidden, true);

/* A dynamic CC object is not a cache layer: its authored-looking scroll fields
 * neither move nor clip its child. */
const objectLeafBox = layoutBox(ir, {}, viewport, objectLeaf);
assert.deepEqual({ x: objectLeafBox.x, y: objectLeafBox.y }, { x: 55, y: 15 });
assert.deepEqual(objectLeafBox.clip, { left: 10, top: 20, right: 110, bottom: 100 });

/* In-place topology changes must invalidate the structural plan even when no
 * full layout pass runs first. */
inner.layer = null;
const reparented = layoutBox(ir, {}, viewport, leaf);
const reparentedFull = layout(ir, {}, viewport).find((box) => box.fileId === leaf.fileId);
assert.deepEqual(reparented, reparentedFull);
assert.equal(layoutBox(ir, {}, viewport, 0x7ffffffe), null);

console.log('preview target box test: ok');
