import assert from 'node:assert/strict';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { page } from '../src/dev_page.js';
import { contentInterfaceCatalog, openContentInterface } from '../src/content.js';
import { layout } from '../src/preview.js';
import { createWorkerRuntimeController } from '../src/worker_runtime_controller.js';
import { RUNTIME_WORKER_SCHEMA } from '../src/runtime_worker_protocol.js';

class FakeClassList {
    constructor(element) { this.element = element; }
    _values() { return new Set(this.element.className.split(/\s+/).filter(Boolean)); }
    add(...names) {
        const values = this._values();
        for( const name of names ) values.add(name);
        this.element.className = [...values].join(' ');
    }
    remove(...names) {
        const values = this._values();
        for( const name of names ) values.delete(name);
        this.element.className = [...values].join(' ');
    }
    toggle(name, force) {
        const present = this._values().has(name);
        const next = force === undefined ? !present : Boolean(force);
        if( next ) this.add(name); else this.remove(name);
        return next;
    }
}

class FakeElement {
    constructor(tagName = 'div') {
        this.tagName = tagName.toUpperCase();
        this.children = [];
        this.parentElement = null;
        this.style = {};
        this.dataset = {};
        this.attributes = new Map();
        this.className = '';
        this.classList = new FakeClassList(this);
        this._innerHTML = '';
        this.textContent = '';
        this.checked = false;
    }
    get firstElementChild() { return this.children[0] || null; }
    get lastElementChild() { return this.children.at(-1) || null; }
    get nextElementSibling() {
        if( !this.parentElement ) return null;
        const siblings = this.parentElement.children;
        return siblings[siblings.indexOf(this) + 1] || null;
    }
    get innerHTML() { return this._innerHTML; }
    set innerHTML(value) {
        for( const child of this.children ) child.parentElement = null;
        this.children = [];
        this._innerHTML = String(value);
    }
    setAttribute(name, value) { this.attributes.set(name, String(value)); }
    removeAttribute(name) { this.attributes.delete(name); }
    appendChild(child) { return this.insertBefore(child, null); }
    append(...children) { for( const child of children ) this.appendChild(child); }
    prepend(child) { return this.insertBefore(child, this.firstElementChild); }
    insertBefore(child, before) {
        let index = before === null ? this.children.length : this.children.indexOf(before);
        assert(index >= 0, 'insertBefore anchor is not a child');
        if( child.parentElement ) {
            const siblings = child.parentElement.children;
            const oldIndex = siblings.indexOf(child);
            siblings.splice(oldIndex, 1);
            if( child.parentElement === this && oldIndex < index ) index--;
        }
        this.children.splice(index, 0, child);
        child.parentElement = this;
        return child;
    }
    remove() {
        if( !this.parentElement ) return;
        const siblings = this.parentElement.children;
        siblings.splice(siblings.indexOf(this), 1);
        this.parentElement = null;
    }
    querySelector(selector) {
        if( selector.startsWith('.') ) {
            const className = selector.slice(1);
            const pending = [...this.children];
            while( pending.length ) {
                const child = pending.shift();
                if( child.className.split(/\s+/).includes(className) ) return child;
                pending.push(...child.children);
            }
        }
        return null;
    }
    getBoundingClientRect() {
        return { left: 0, top: 0, width: 765, height: 503, right: 765, bottom: 503 };
    }
    contains(target) {
        for( let node = target; node; node = node.parentElement ) if( node === this ) return true;
        return false;
    }
    scrollIntoView() {}
}

const elements = new Map([
    ['stage', new FakeElement()],
    ['tree', new FakeElement()],
    ['wire', new FakeElement('input')],
    ['dims', new FakeElement('span')],
    ['pick', new FakeElement('input')],
    ['pickmenu', new FakeElement()],
    ['records', new FakeElement()],
    ['controls', new FakeElement()],
    ['stateactions', new FakeElement()],
    ['save-state', new FakeElement('button')],
    ['revert-state', new FakeElement('button')],
    ['state-note', new FakeElement('span')],
]);
const document = {
    getElementById: (id) => elements.get(id) || null,
    createElement: (name) => new FakeElement(name),
    createElementNS: (_namespace, name) => new FakeElement(name),
};

const html = page();
const scripts = [...html.matchAll(/<script(?: [^>]*)?>([\s\S]*?)<\/script>/g)];
const inline = scripts.find((match) => match[1].trim())?.[1];
assert(inline, 'dev page has no inline script');
const browserScript = inline.replace(/^import .*;$/gm, '');
const declarations = browserScript.slice(0, browserScript.indexOf('const treeView ='));
const idleCallbacks = [];
const animationCallbacks = [];
const cooperativeCallbacks = [];
let fakeNow = 0;
let fakeStep = 0.25;
let inputPending = false;
const fakePerformance = { now: () => (fakeNow += fakeStep) };
const fakeGlobal = {
    __cs2domPostTask(callback) { cooperativeCallbacks.push(callback); },
    __cs2domPostTimer(callback) { cooperativeCallbacks.push(callback); },
    navigator: { scheduling: { isInputPending: () => inputPending } },
};
const loadRenderer = new Function(
    'document', 'Image', 'paintCacheText', 'createModelRenderController', 'requestAnimationFrame',
    'requestIdleCallback', 'performance', 'globalThis',
    declarations + `\nreturn {
      drawStage, drawTree, renderPicker, treeSliceMetrics, stageSliceMetrics,
      pickerSliceMetrics, drawRecords, recordSliceMetrics,
      cooperativeTaskMetrics, drawControls, controlSliceMetrics,
      scheduleCooperativeTask, budgetedInputHandler, inputHandlerMetrics,
      setCatalog(next) { catalog = next; indexCatalog(); },
      setRecordData(next, warnings = []) { data = next; runtimeWarnings = warnings; },
      setChosen(next) { chosen = next; },
      stagePending() { return Boolean(stageDrawJob); },
      pickerResults() { return pickerMatches; },
    };`);
const {
    drawStage, drawTree, renderPicker, treeSliceMetrics, stageSliceMetrics,
    pickerSliceMetrics, drawRecords, recordSliceMetrics, setCatalog, setRecordData,
    pickerResults, cooperativeTaskMetrics, drawControls, controlSliceMetrics,
    scheduleCooperativeTask, budgetedInputHandler, inputHandlerMetrics, setChosen,
    stagePending,
} = loadRenderer(
    document, class FakeImage {}, async () => false, () => null,
    (callback) => { animationCallbacks.push(callback); return animationCallbacks.length; },
    (callback) => { idleCallbacks.push(callback); return idleCallbacks.length; },
    fakePerformance, fakeGlobal);

function flushAnimation() {
    let calls = 0;
    while( animationCallbacks.length || cooperativeCallbacks.length ) {
        assert(++calls < 100000, 'time-sliced work did not settle (' +
            JSON.stringify(cooperativeTaskMetrics) + ', coop=' + cooperativeCallbacks.length +
            ', raf=' + animationCallbacks.length + ')');
        if( cooperativeCallbacks.length ) cooperativeCallbacks.shift()();
        else animationCallbacks.shift()(fakeNow);
    }
}

function flushTree() {
    let calls = 0;
    while( idleCallbacks.length || cooperativeCallbacks.length ) {
        assert(++calls < 10000, 'time-sliced tree reconciliation did not settle');
        if( cooperativeCallbacks.length ) cooperativeCallbacks.shift()();
        else idleCallbacks.shift()({ didTimeout: false, timeRemaining: () => 10 });
    }
}

function box(overrides = {}) {
    return {
        fileId: 0,
        name: 'root',
        kind: 'Rectangle',
        type: 3,
        layer: null,
        depth: 0,
        x: 0,
        y: 0,
        w: 765,
        h: 503,
        clip: { left: 0, top: 0, right: 765, bottom: 503 },
        props: { fill: true, color: 0x202428, transparency: 0 },
        presentation: null,
        emitted: true,
        effectiveHidden: false,
        culled: false,
        dynamic: [],
        events: [],
        native: null,
        ...overrides,
    };
}

const boxes = [box()];
for( let index = 0; index < 1410; index++ ) boxes.push(box({
    fileId: `@host:${index}`,
    name: `bank_slots[${index}]`,
    kind: 'Graphic',
    type: 5,
    layer: 0,
    depth: 1,
    x: index % 40 * 18,
    y: Math.floor(index / 40) * 14,
    w: 18,
    h: 14,
    props: { sprite: -1, transparency: 0 },
    native: { dynamic: true, childIndex: index },
}));
const iface = {
    interfaceId: 12,
    viewport: { width: 765, height: 503 },
    spriteSource: 'content',
    modelSource: 'content',
    boxes,
};

drawStage(iface);
flushAnimation();
assert.equal(elements.get('stage').children.length, 1,
    'normal preview materialized sprite-less bank cells');
const rootElement = elements.get('stage').firstElementChild;
drawStage(iface);
flushAnimation();
assert.equal(elements.get('stage').firstElementChild, rootElement,
    'unchanged stage component was replaced');

elements.get('wire').checked = true;
drawStage(iface);
flushAnimation();
assert.equal(elements.get('stage').children.length, boxes.length,
    'wire view omitted diagnostic boxes');
assert.equal(elements.get('stage').firstElementChild, rootElement,
    'wire toggle replaced an unchanged visible component');

elements.get('wire').checked = false;
drawStage(iface);
flushAnimation();
assert.equal(elements.get('stage').children.length, 1,
    'leaving wire view retained sprite-less bank cells');
assert.equal(elements.get('stage').firstElementChild, rootElement,
    'leaving wire view replaced an unchanged visible component');

boxes[0].props = { ...boxes[0].props, color: 0x334455 };
drawStage(iface);
flushAnimation();
assert.notEqual(elements.get('stage').firstElementChild, rootElement,
    'a changed visible component retained stale paint');

const partialBoxes = [0, 1, 2].map((index) => box({
    fileId: 900 + index,
    name: 'partial_' + index,
    x: index * 40,
    w: 36,
    props: { fill: true, color: 0x101010 + index, transparency: 0 },
}));
const partialIface = { ...iface, boxes: partialBoxes };
drawStage(partialIface);
flushAnimation();
const partialElements = [...elements.get('stage').children];
const changedPartial = {
    ...partialBoxes[1],
    props: { ...partialBoxes[1].props, color: 0xff00ff },
};
drawStage(partialIface, { upsert: [{ key: 'worker-key-1', box: changedPartial }] });
flushAnimation();
assert.equal(elements.get('stage').children[0], partialElements[0]);
assert.notEqual(elements.get('stage').children[1], partialElements[1]);
assert.equal(elements.get('stage').children[2], partialElements[2]);
assert.equal(elements.get('stage').children[1].style.background, '#ff00ff',
    'one-widget stage patch retained stale paint');

const firstCoalesced = {
    ...partialBoxes[2],
    props: { ...partialBoxes[2].props, color: 0x123456 },
};
const latestCoalesced = {
    ...partialBoxes[2],
    props: { ...partialBoxes[2].props, color: 0x654321 },
};
drawStage(partialIface, {
    upsertBatches: [
        [{ key: 'worker-key-2', box: firstCoalesced }],
        [{ key: 'worker-key-2', box: latestCoalesced }],
    ],
    dedupePartial: true,
});
flushAnimation();
assert.equal(elements.get('stage').children.length, 3,
    'coalesced partial revisions duplicated a stage widget');
assert.equal(elements.get('stage').children[2].style.background, '#654321',
    'coalesced partial revisions did not retain the newest widget');

const currentRoot = elements.get('stage').firstElementChild;
const visibleBoxes = Array.from({ length: 1500 }, (_, index) => box({
    fileId: index,
    name: 'visible_' + index,
    x: index % 50 * 12,
    y: Math.floor(index / 50) * 12,
    w: 12,
    h: 12,
    props: { fill: true, color: index, transparency: 0 },
}));
drawStage({ ...iface, boxes: visibleBoxes });
assert.equal(elements.get('stage').firstElementChild, currentRoot,
    'a large stage update blanked the old interactive preview before yielding');
flushAnimation();
assert.equal(elements.get('stage').children.length, 1500);
assert(stageSliceMetrics.count > 20, '1,500 visible widgets were reconciled in one task');
assert.equal(stageSliceMetrics.overBudget, 0,
    'a deterministic stage slice exceeded the 10ms hard budget');
assert(stageSliceMetrics.maxMs < 4,
    'stage slice instrumentation exceeded the input budget');

/* A newer projection retires queued slices and converges from any partial DOM
   work left by the obsolete projection. */
drawStage({ ...iface, boxes: visibleBoxes.map((entry) => ({ ...entry })) });
drawStage(iface);
flushAnimation();
assert(stageSliceMetrics.stale > 0, 'an obsolete stage job continued painting');
assert.equal(elements.get('stage').children.length, 1,
    'the replacement stage did not converge after cancelling stale work');

const searchCatalog = Array.from({ length: 5000 }, (_, index) => ({
    key: 'content:' + index,
    name: (index % 3 === 0 ? 'target_' : 'interface_') + index,
    source: ['authored', 'dat2', 'content'][index % 3],
    interfaceId: index,
}));
setCatalog(searchCatalog);
const beforePickerEnqueue = fakePerformance.now();
renderPicker('target');
const pickerEnqueueMs = fakePerformance.now() - beforePickerEnqueue;
assert(pickerEnqueueMs < 10, 'picker input synchronously filtered the full catalog');
flushAnimation();
assert.equal(pickerResults().length, 120);
assert(elements.get('pickmenu').children.length <= 124,
    'picker materialized more than the bounded result window');
assert(pickerSliceMetrics.count > 20, '5,000-entry picker search ran as one task');
assert.equal(pickerSliceMetrics.overBudget, 0);
assert(pickerSliceMetrics.maxMs < 10);

drawTree(iface);
assert.notEqual(elements.get('tree').style.visibility, 'hidden',
    'tree reconciliation hid the old inspector and deferred one large reveal');
assert.equal(elements.get('tree').attributes.get('aria-busy'), 'true');
flushTree();
const firstRows = [...elements.get('tree').children];
drawTree(iface);
flushTree();
assert.deepEqual(elements.get('tree').children, firstRows,
    'unchanged tree rows were rebuilt');
boxes[1].fileId = '@host:next';
drawTree(iface);
flushTree();
assert.equal(elements.get('tree').children[1], firstRows[1],
    'changed tree row was replaced instead of updated in place');
assert.match(elements.get('tree').children[1].innerHTML, /@host:next/,
    'reused tree row retained stale inspector text');
assert(treeSliceMetrics.count > 3, 'large tree was not split across bounded tasks');
assert.equal(treeSliceMetrics.overBudget, 0, 'a deterministic tree slice exceeded 10ms');
assert(treeSliceMetrics.maxMs < 10, 'tree slice instrumentation exceeded the input budget');
assert.equal(elements.get('tree').attributes.has('aria-busy'), false,
    'completed tree remained marked busy');

const recordIface = {
    name: 'record_stress', reactSource: 'r'.repeat(200000),
    interfaceText: '[root]\ntype=0', compackText: 'root=0',
    scripts: Array.from({ length: 1000 }, (_, index) => ({
        name: 'script_' + index, source: 'body_' + index,
    })),
};
setRecordData({ warnings: ['static warning'] }, ['runtime warning']);
drawRecords(recordIface);
flushAnimation();
const recordRoot = elements.get('records').firstElementChild;
assert.equal(recordRoot.children.length, 2 + 3 + recordIface.scripts.length,
    'sliced record pane dropped warnings or source shells');
const openRecord = recordRoot.children[2];
assert(openRecord.lastElementChild.children.length > 1,
    'large visible source was assigned as one blocking text node');
assert(recordSliceMetrics.count > 20,
    '1,000 cache records were synchronously materialized');
assert.equal(recordSliceMetrics.overBudget, 0);
assert(recordSliceMetrics.maxMs < 10);

/* Deterministic browser-projector coverage. Exercise every checked-in static
   content tree, then exceed the real cache maxima with the protocol/Host
   component limit. This proves the cooperative loops contain their own work;
   real wall-clock VM/worker/browser interaction latency is tested separately. */
fakeStep = 0.02;
const content = resolve(dirname(fileURLToPath(import.meta.url)),
    '../../../OSRS-Content/osrs239-content');
const allInterfaces = contentInterfaceCatalog(content, { source: 'content' });
assert(allInterfaces.length >= 900, 'all-interface latency corpus is missing');
let sawBank = false;
let sawPirate = false;
let largestReal = { name: '', boxes: 0 };
for( const entry of allInterfaces ) {
    const imported = openContentInterface(content, entry.name, { source: 'content' });
    const realBoxes = layout(imported.ir, {}, { width: 512, height: 334 });
    if( realBoxes.length > largestReal.boxes )
        largestReal = { name: entry.name, boxes: realBoxes.length };
    sawBank ||= entry.name === 'bankmain';
    sawPirate ||= entry.name === 'pirate_combilock';
    const realIface = {
        interfaceId: imported.interfaceId,
        viewport: { width: 512, height: 334 },
        spriteSource: 'content', modelSource: 'content', boxes: realBoxes,
    };
    drawStage(realIface);
    drawTree(realIface);
    flushAnimation();
    flushTree();
}
assert(sawBank && sawPirate,
    'bankmain or pirate_combilock was absent from static projector coverage');

const maximumBoxes = Array.from({ length: 4096 }, (_, index) => box({
    fileId: 20000 + index,
    name: 'maximum_' + index,
    x: index % 64 * 8,
    y: Math.floor(index / 64) * 5,
    w: 8,
    h: 5,
    props: { fill: true, color: index & 0xffffff, transparency: 0 },
}));
const maximumIface = { ...iface, interfaceId: 65535, boxes: maximumBoxes };
const completedBeforeFairness = stageSliceMetrics.completed;
let fairSentinelSawPending = false;
drawStage(maximumIface);
scheduleCooperativeTask(() => {
    fairSentinelSawPending = stagePending() &&
        stageSliceMetrics.completed === completedBeforeFairness;
});
let fairnessTurns = 0;
while( !fairSentinelSawPending && cooperativeCallbacks.length ) {
    assert(++fairnessTurns < 10, 'cooperative FIFO starved a peer task');
    cooperativeCallbacks.shift()();
}
assert(fairSentinelSawPending,
    'a large stage monopolized the cooperative queue until completion');
flushAnimation();
const partialSliceStart = stageSliceMetrics.count;
const maximumChanged = maximumBoxes.map((entry, index) => ({
    key: 'worker-maximum-' + index,
    box: { ...entry, props: { ...entry.props, color: (entry.props.color + 1) & 0xffffff } },
}));
maximumIface.runtimeVersion = 1;
drawStage(maximumIface, { upsert: maximumChanged });
assert(stagePending(),
    'a 4,096-widget runtime patch completed before yielding to browser input');
flushAnimation();
assert(stageSliceMetrics.count - partialSliceStart > 20,
    'runtime patch coalescing/reconciliation escaped the cooperative stage budget');
assert.equal(elements.get('stage').children.length, 4096,
    'large partial runtime patch duplicated or removed stage widgets');
const dedupeSliceStart = stageSliceMetrics.count;
const earlierRevision = maximumChanged.map((entry) => ({
    key: entry.key,
    box: {
        ...entry.box,
        props: { ...entry.box.props, color: (entry.box.props.color + 1) & 0xffffff },
    },
}));
const latestRevision = maximumChanged.map((entry) => ({
    key: entry.key,
    box: {
        ...entry.box,
        props: { ...entry.box.props, color: (entry.box.props.color + 2) & 0xffffff },
    },
}));
maximumIface.runtimeVersion = 2;
drawStage(maximumIface, {
    upsertBatches: [earlierRevision, latestRevision],
    dedupePartial: true,
});
assert(stagePending(),
    'multi-revision patch coalescing completed before yielding to browser input');
flushAnimation();
assert(stageSliceMetrics.count - dedupeSliceStart > 20,
    'multi-revision dedupe escaped the cooperative stage budget');
assert.equal(elements.get('stage').children.length, 4096,
    'multi-revision dedupe duplicated stage widgets');
assert.equal(elements.get('stage').firstElementChild.style.background,
    '#' + latestRevision[0].box.props.color.toString(16).padStart(6, '0'),
    'multi-revision dedupe painted an obsolete widget revision');
drawTree(maximumIface);
flushTree();
assert.equal(elements.get('stage').children.length, 4096);
assert.equal(elements.get('tree').children.length, 4096);

const maximumCatalog = Array.from({ length: 16384 }, (_, index) => ({
    key: 'synthetic:' + index,
    name: (index % 4 ? 'interface_' : 'needle_') + index,
    source: ['authored', 'dat2', 'content'][index % 3],
    interfaceId: index,
}));
setCatalog(maximumCatalog);
const pickerHandler = budgetedInputHandler('cert-picker-input', () => renderPicker('needle'));
pickerHandler({});
flushAnimation();
assert.equal(pickerResults().length, 120);

setChosen('synthetic:controls');
const maximumInputs = Array.from({ length: 4096 }, (_, index) => ({
    key: 'varp:' + index, id: index, label: 'varp', readBy: ['maximum'],
    control: { kind: 'slider', min: 0, max: 100, step: 1, initial: 0 },
}));
drawControls({
    interfaceId: 65535, source: 'content', inputs: maximumInputs, unmodelled: [],
});
flushAnimation();
assert.equal(elements.get('controls').firstElementChild.children.length, 4096,
    'maximum host controls did not materialize');

let deferredForInput = false;
inputPending = true;
scheduleCooperativeTask(() => { deferredForInput = true; });
assert(cooperativeCallbacks.length > 0);
cooperativeCallbacks.shift()();
assert.equal(deferredForInput, false,
    'cooperative UI work ran while navigator reported pending input');
inputPending = false;
flushAnimation();
assert.equal(deferredForInput, true);

class ManualRuntimeWorker {
    constructor() { this.sent = []; }
    postMessage(message) { this.sent.push(message); }
    emit(message) { this.onmessage?.({ data: message }); }
    terminate() {}
}
let manualWorker;
let controllerNow = 0;
let controllerViolations = 0;
const controller = createWorkerRuntimeController({
    workerFactory: () => (manualWorker = new ManualRuntimeWorker()),
    clock: () => (controllerNow += 0.02),
    onBudgetViolation: () => { controllerViolations++; },
});
const ready = controller.start({ ir: { components: [] }, program: { scripts: [] } });
manualWorker.emit({
    schema: RUNTIME_WORKER_SCHEMA, type: 'ready', session: controller.session,
    mode: 'wasm', warnings: [], interaction: { menuOpen: false }, services: [],
});
await ready;
let maxControllerEnqueue = 0;
const pendingTickets = [];
for( let index = 0; index < 4096; index++ ) {
    const ticket = controller.dispatch({
        type: 'key_down', keyTyped: index & 127, keyPressed: index & 255,
    });
    maxControllerEnqueue = Math.max(maxControllerEnqueue, ticket.enqueueMs);
    ticket.completion.catch(() => {});
    pendingTickets.push(ticket.completion);
}
assert.equal(controller.pendingEventCount, 4096);
assert(maxControllerEnqueue < 10 && controllerViolations === 0,
    'maximum runtime-controller backlog exceeded the input enqueue budget');
controller.dispose();
await Promise.allSettled(pendingTickets);

for( const [name, metric] of Object.entries({
    stage: stageSliceMetrics,
    tree: treeSliceMetrics,
    picker: pickerSliceMetrics,
    records: recordSliceMetrics,
    controls: controlSliceMetrics,
}) ) {
    assert.equal(metric.overBudget, 0, name + ' produced a >=10ms task');
    assert(metric.maxMs < 10, name + ' max task was ' + metric.maxMs + 'ms');
}
assert(stageSliceMetrics.maxMs < 4,
    'stage reconciliation did not retain sub-4ms headroom');
assert.equal(cooperativeTaskMetrics.overBudget, 0,
    'cooperative scheduler observed a >=10ms task');
assert(cooperativeTaskMetrics.maxTaskMs < 10);
assert(cooperativeTaskMetrics.timerFences > 0,
    'large jobs never crossed the two-slice fairness fence');
assert(inputHandlerMetrics['cert-picker-input'].maxMs < 10);

process.stdout.write('dev_page_render_test: ok (1410 empty bank cells elided; ' +
    stageSliceMetrics.count + ' bounded stage slices, max ' + stageSliceMetrics.maxMs +
    'ms; picker enqueue ' + pickerEnqueueMs + 'ms / max slice ' +
    pickerSliceMetrics.maxMs + 'ms; ' + treeSliceMetrics.count +
    ' bounded tree slices, max ' + treeSliceMetrics.maxMs + 'ms; ' +
    recordSliceMetrics.count + ' bounded record slices; projected ' +
    allInterfaces.length + ' static content interfaces incl. bankmain/pirate; largest ' +
    largestReal.name + '=' + largestReal.boxes + '; synthetic 4096; controller enqueue ' +
    maxControllerEnqueue.toFixed(3) + 'ms)\n');
