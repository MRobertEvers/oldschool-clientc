import assert from 'node:assert/strict';

import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';

import {
    GraphicWidget,
    InterfacePreview,
    TextWidget,
    assertViewTreeStore,
    interfaceInteractionProps,
    resolveWidgetRenderer,
    widgetRole,
    widgetSurfaceProps,
} from '../src/react_tree_renderer.js';
import {
    RetainedInterfaceStage,
    assertCommittedStageStore,
} from '../src/react_stage_mount.js';
import { createUITreeStore, createViewTreeStore } from '../src/ui_tree_store.js';

function immutableNode(renderKey, type, fields = {}) {
    return Object.freeze({
        renderKey,
        type,
        children: Object.freeze(fields.children || []),
        props: Object.freeze(fields.props || {}),
        layout: Object.freeze(fields.layout || { x: 0, y: 0, width: 10, height: 10 }),
        ...fields,
    });
}

const roots = Object.freeze(['bank']);
const nodes = new Map([
    ['bank', immutableNode('bank', 0, {
        name: 'bankmain',
        children: ['backdrop', 'title', 'sprite', 'player', 'divider'],
        layout: { x: 0, y: 0, width: 512, height: 334 },
        props: { scrollWidth: 512, scrollHeight: 334 },
    })],
    ['backdrop', immutableNode('backdrop', 3, {
        name: 'backdrop',
        layout: { x: 1, y: 2, width: 510, height: 332 },
        props: { fill: true, color: 0x40382f },
    })],
    ['title', immutableNode('title', 4, {
        name: 'title',
        layout: { x: 20, y: 8, width: 200, height: 20 },
        props: { text: 'The Bank of Gielinor', color: 0xff981f, halign: 1, shadow: true },
    })],
    ['sprite', immutableNode('sprite', 5, {
        name: 'close',
        interactive: true,
        layout: { x: 470, y: 8, width: 24, height: 24 },
        props: { sprite: 7, tiled: false },
    })],
    ['player', immutableNode('player', 6, {
        name: 'player_model',
        layout: { x: 250, y: 50, width: 80, height: 120 },
        props: { model: -1, zoom: 560 },
    })],
    ['divider', immutableNode('divider', 9, {
        name: 'divider',
        layout: { x: 5, y: 32, width: 500, height: 0 },
        props: { color: 0xc0a060, lineWidth: 2 },
    })],
]);

let subscriptions = 0;
const store = {
    getRoots: () => roots,
    getNode: (renderKey) => nodes.get(renderKey) || null,
    getOrderSnapshot: (parentKey) => nodes.get(parentKey)?.children || Object.freeze([]),
    subscribe(listener) {
        assert.equal(typeof listener, 'function');
        subscriptions++;
        return () => { subscriptions--; };
    },
};

assert.equal(assertViewTreeStore(store), store);
assert.throws(() => assertViewTreeStore({}), /getRoots/);
assert.equal(widgetRole(nodes.get('bank')), 'layer');
assert.equal(widgetRole({ type: 'Rectangle' }), 'rect');
assert.equal(widgetRole({ kind: 'INV' }), 'inventory');
assert.equal(widgetRole({ type: 12345 }), 'unknown');

const html = renderToStaticMarkup(createElement(InterfacePreview, {
    store,
    viewport: { width: 512, height: 334 },
    assets: { sprite: (node) => node.props.sprite >= 0 ? `/sprite/${node.props.sprite}.png` : null },
}));

assert.match(html, /class="cs2dom-interface-preview"/);
assert.match(html, /width:512px;height:334px/);
assert.match(html, /data-render-key="bank"/);
assert.match(html, /data-render-key="backdrop"/);
assert.match(html, /background-color:#40382f/);
assert.match(html, /The Bank of Gielinor/);
assert.match(html, /color:#ff981f/);
assert.match(html, /\/sprite\/7\.png/);
assert.match(html, /<canvas[^>]+width="80"[^>]+height="120"/);
assert.match(html, /<svg[^>]+data-render-key="divider"/);
assert.match(html, /stroke="#c0a060"/);
assert.equal(subscriptions, 0, 'server snapshots unexpectedly installed live subscriptions');

const committedStageSnapshot = Object.freeze({
    session: 2, revision: 7, render: null, patch: null,
});
const committedStageStore = {
    subscribeStage: () => () => {},
    getStageSnapshot: () => committedStageSnapshot,
};
assert.equal(assertCommittedStageStore(committedStageStore), committedStageStore);
assert.throws(() => assertCommittedStageStore({}), /subscribeStage/);
const retainedHtml = renderToStaticMarkup(createElement(RetainedInterfaceStage, {
    store: committedStageStore,
}));
assert.match(retainedHtml, /class="cs2dom-retained-stage"/);
assert.match(retainedHtml, /data-react-stage-revision="7"/);

function NamedRenderer({ node }) {
    return createElement('output', { 'data-authored': node.renderKey }, node.props.text);
}
function IdRenderer({ node }) {
    return createElement('mark', { 'data-authored-id': node.renderKey }, node.props.text);
}
const overrideNode = immutableNode('authored', 4, {
    name: 'title',
    rendererId: 'cache-font',
    props: { text: 'authored' },
});
assert.equal(resolveWidgetRenderer(overrideNode, {
    ids: { 'cache-font': IdRenderer },
    names: { title: NamedRenderer },
    types: { 4: GraphicWidget },
}), IdRenderer, 'explicit renderer id did not take precedence');
assert.equal(resolveWidgetRenderer({ ...overrideNode, rendererId: null }, {
    names: new Map([['title', NamedRenderer]]),
    types: { 4: GraphicWidget },
}), NamedRenderer, 'component-name renderer did not take precedence');
assert.equal(resolveWidgetRenderer(overrideNode, {}), TextWidget);
assert.equal(resolveWidgetRenderer({ ...overrideNode, type: 12345 }, {
    fallback: NamedRenderer,
}), NamedRenderer, 'unknown-widget fallback was ignored');

const authoredStore = {
    getRoots: () => Object.freeze(['authored']),
    getNode: () => overrideNode,
    subscribe: () => () => {},
};
const authored = renderToStaticMarkup(createElement(InterfacePreview, {
    store: authoredStore,
    renderers: { ids: { 'cache-font': IdRenderer } },
}));
assert.match(authored, /<mark data-authored-id="authored">authored<\/mark>/);

/* The production store keeps child order outside node snapshots. Verify the
 * optional optimized contract, rather than testing only the children fallback. */
const workingTree = createUITreeStore();
const transaction = workingTree.beginTransaction('react renderer integration');
workingTree.upsertNode({
    id: 1, renderKey: 'store-root', type: 0, parentId: null, subId: -1,
    layout: { x: 0, y: 0, width: 64, height: 32 }, props: {},
});
workingTree.upsertNode({
    id: 2, renderKey: 'store-label', type: 4, parentId: 1, subId: 0,
    geometry: { x: 2, y: 3, width: 60, height: 12 },
    props: { text: 'separate order', color: 0xffffff },
});
const delta = workingTree.commitTransaction(transaction);
const viewTree = createViewTreeStore();
viewTree.applyDelta(delta);
assert.equal(viewTree.getNode('store-root').children, undefined);
assert.deepEqual(viewTree.getChildren('store-root'), ['store-label']);
const storeHtml = renderToStaticMarkup(createElement(InterfacePreview, { store: viewTree }));
assert.match(storeHtml, /data-render-key="store-label"/);
assert.match(storeHtml, /left:2px;top:3px;width:60px;height:12px/);
assert.match(storeHtml, />separate order<\/span>/);

const actions = [];
const surface = widgetSurfaceProps(nodes.get('sprite'), 'graphic', (action) => actions.push(action));
assert.equal(surface.tabIndex, 0, 'interactive widget is not keyboard focusable');
assert.equal(surface.onClick, undefined, 'renderer allocated a per-widget event handler');
const interaction = interfaceInteractionProps((action) => actions.push(action));
const interfaceRoot = { dataset: {} };
const widgetElement = { dataset: { renderKey: 'sprite' }, parentElement: interfaceRoot };
const contentElement = { dataset: {}, parentElement: widgetElement };
let prevented = 0;
let stopped = 0;
interaction.onContextMenu({
    preventDefault: () => { prevented++; },
    stopPropagation: () => { stopped++; },
    target: contentElement,
    currentTarget: interfaceRoot,
    pointerId: 4,
    pointerType: 'mouse',
    button: 2,
    buttons: 2,
    clientX: 123,
    clientY: 45,
    nativeEvent: { offsetX: 7, offsetY: 9 },
    shiftKey: true,
});
interaction.onKeyDown({
    stopPropagation: () => { stopped++; },
    target: widgetElement,
    currentTarget: interfaceRoot,
    key: 'Enter', code: 'Enter', repeat: false, location: 0,
    ctrlKey: true,
});
interaction.onWheel({
    preventDefault: () => { prevented++; },
    stopPropagation: () => { stopped++; },
    target: widgetElement,
    currentTarget: interfaceRoot,
    deltaX: 0, deltaY: -120, deltaZ: 0, deltaMode: 0,
    clientX: 10, clientY: 11,
});
assert.equal(prevented, 2);
assert.equal(stopped, 3);
assert.deepEqual(actions[0], {
    type: 'context-menu', renderKey: 'sprite',
    pointerId: 4, pointerType: 'mouse', button: 2, buttons: 2,
    clientX: 123, clientY: 45, offsetX: 7, offsetY: 9, pressure: 0,
    altKey: false, ctrlKey: false, metaKey: false, shiftKey: true,
});
assert.deepEqual(actions[1], {
    type: 'key-down', renderKey: 'sprite', key: 'Enter', code: 'Enter',
    repeat: false, location: 0,
    altKey: false, ctrlKey: true, metaKey: false, shiftKey: false,
});
assert.equal(actions[2].type, 'wheel');
assert.equal(actions[2].deltaY, -120);

console.log('react tree renderer tests passed');
