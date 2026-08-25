import assert from 'node:assert/strict';
import {
    FONT_RUNTIME_METRICS,
    __fontRuntimeTest,
    layoutBitmapText,
    paintCacheText,
    stripMarkup,
} from '../src/font_runtime.js';

const font = {
    id: 7,
    lineHeight: 8,
    advances: { 32: 3, 60: 4, 62: 4, 65: 5, 66: 6, 63: 4 },
    glyphs: {
        60: { width: 3, height: 7, x: 0, y: 0, url: '/glyph/60' },
        62: { width: 3, height: 7, x: 0, y: 0, url: '/glyph/62' },
        65: { width: 3, height: 7, x: 0, y: 0, url: '/glyph/65' },
        66: { width: 3, height: 7, x: 0, y: 0, url: '/glyph/66' },
        63: { width: 3, height: 7, x: 0, y: 0, url: '/glyph/63' },
    },
};

class FakeContext {
    constructor(owner) {
        this.owner = owner;
        this.operations = [];
        this.fillStyle = '#000000';
        this.globalCompositeOperation = 'source-over';
        this.imageSmoothingEnabled = true;
    }
    drawImage(source, ...args) {
        this.operations.push({ kind: 'drawImage', source, args });
    }
    fillRect(...args) {
        this.operations.push({ kind: 'fillRect', color: this.fillStyle, args });
        if( this.owner.kind === 'tint' ) this.owner.tintColor = this.fillStyle;
    }
}

class FakeCanvas {
    constructor(width = 0, height = 0, kind = 'destination') {
        this.width = width;
        this.height = height;
        this.kind = kind;
        this.attributes = new Map();
        this.context = new FakeContext(this);
    }
    getContext(kind) { return kind === '2d' ? this.context : null; }
    setAttribute(name, value) { this.attributes.set(name, String(value)); }
    getAttribute(name) { return this.attributes.get(name) ?? null; }
}

class FakeImage {
    constructor() { this.width = 3; this.height = 7; }
    set src(value) {
        this.url = value;
        queueMicrotask(() => this.onload?.());
    }
}

globalThis.Image = FakeImage;
globalThis.fetch = async () => ({ ok: true, json: async () => font });
globalThis.document = {
    createElement(name) {
        assert.equal(name, 'canvas');
        return new FakeCanvas();
    },
};

const callbacks = [];
let fakeTime = 0;
let scheduledSlices = 0;
let tintCreations = 0;

__fontRuntimeTest.reset();
__fontRuntimeTest.configure({
    budgetMs: 3,
    clock: () => (fakeTime += 0.1),
    schedule: (callback) => { scheduledSlices++; callbacks.push(callback); },
    tintEntries: 2,
    tintPixels: 1024,
    canvasFactory: (width, height) => {
        tintCreations++;
        return new FakeCanvas(width, height, 'tint');
    },
});

async function drain(promise) {
    let complete = false;
    let value;
    let failure;
    promise.then((result) => { complete = true; value = result; },
        (error) => { complete = true; failure = error; });
    for( let guard = 0; !complete && guard < 100000; guard++ ) {
        await Promise.resolve();
        const callback = callbacks.shift();
        if( callback ) callback();
        else await new Promise((resolve) => setImmediate(resolve));
    }
    if( !complete ) throw new Error('font work did not settle');
    if( failure ) throw failure;
    return value;
}

function element() {
    return {
        isConnected: true,
        textContent: 'CSS fallback',
        classes: new Set(),
        classList: { add(name) { this.owner.classes.add(name); }, owner: null },
        appendChild(child) { this.child = child; },
    };
}

const richCases = [
    ['A B A', 8, 30],
    ['A|B  A', 8, 30],
    ['<col=ff0000>A</col> B', 8, 30],
    ['<col=ff0000></col>', 8, 30],
    ['A<br />B\\nA\r\nB', 100, 30],
    ['@red@A@gre@B<lt><gt>', 100, 5],
];
for( const [text, width, height] of richCases ) {
    const prepared = await drain(__fontRuntimeTest.prepare(font, text, width, height));
    assert.deepEqual(prepared.lines, layoutBitmapText(font, text, width, height),
        `incremental layout diverged for ${JSON.stringify(text)}`);
    assert.equal(prepared.ariaLabel, stripMarkup(text),
        `incremental aria text diverged for ${JSON.stringify(text)}`);
}

/* Thousands of glyph operations must require many separately scheduled queue
 * slices, while one tint surface is reused for the repeated glyph. */
const longElement = element();
longElement.classList.owner = longElement;
const longText = 'A'.repeat(1200);
const coldPaintStepStart = FONT_RUNTIME_METRICS.steps;
assert.equal(await drain(paintCacheText(longElement, {
    w: 7000, h: 8,
    props: { font: 7, text: longText, color: 0x123456 },
}, { spriteSource: 'test' })), true);
assert.ok(scheduledSlices > 100,
    `expected macrotask yielding, only scheduled ${scheduledSlices} slices`);
assert.ok(FONT_RUNTIME_METRICS.yieldedSlices > 100);
assert.equal(FONT_RUNTIME_METRICS.overBudgetSlices, 0);
assert.ok(FONT_RUNTIME_METRICS.maxSliceMs < 4,
    `deterministic slice exceeded 4ms: ${FONT_RUNTIME_METRICS.maxSliceMs}`);
assert.equal(longElement.child.context.operations
    .filter((operation) => operation.kind === 'drawImage').length, longText.length);
assert.equal(tintCreations, 1, 'repeated glyphs must not allocate scratch canvases');
assert.equal(FONT_RUNTIME_METRICS.tintMisses, 1);
assert.equal(FONT_RUNTIME_METRICS.tintHits, longText.length - 1);
const coldPaintSteps = FONT_RUNTIME_METRICS.steps - coldPaintStepStart;

const repeatedLongElement = element();
repeatedLongElement.classList.owner = repeatedLongElement;
const warmPaintStepStart = FONT_RUNTIME_METRICS.steps;
const preparedHitsBeforeLongRepeat = FONT_RUNTIME_METRICS.preparedHits;
assert.equal(await drain(paintCacheText(repeatedLongElement, {
    w: 7000, h: 8,
    props: { font: 7, text: longText, color: 0x123456 },
}, { spriteSource: 'test' })), true);
const warmPaintSteps = FONT_RUNTIME_METRICS.steps - warmPaintStepStart;
assert.equal(FONT_RUNTIME_METRICS.preparedHits, preparedHitsBeforeLongRepeat + 1);
assert.ok(warmPaintSteps < coldPaintSteps / 2,
    `repeat paint retained parser work (${coldPaintSteps} -> ${warmPaintSteps} steps)`);
assert.equal(repeatedLongElement.child.context.operations
    .filter((operation) => operation.kind === 'drawImage').length, longText.length);
assert.equal(tintCreations, 1, 'repeat paint recreated cached glyph surfaces');

/* Markup, shadow ordering, pen movement, underline placement and aria text
 * retain the pre-sliced painter's observable semantics. */
const markedElement = element();
markedElement.classList.owner = markedElement;
const markedText = '<col=ff0000>A</col><u=00ff00>B</u>A';
assert.equal(await drain(paintCacheText(markedElement, {
    w: 40, h: 8,
    props: { font: 7, text: markedText, color: 0x123456, shadow: true },
}, { spriteSource: 'test' })), true);
assert.equal(markedElement.child.getAttribute('aria-label'), 'ABA');
const markedDraws = markedElement.child.context.operations
    .filter((operation) => operation.kind === 'drawImage');
assert.deepEqual(markedDraws.map((operation) => operation.args), [
    [1, 1], [6, 1], [12, 1],
    [0, 0], [5, 0], [11, 0],
]);
assert.deepEqual(markedDraws.map((operation) => operation.source.tintColor), [
    '#000000', '#000000', '#000000',
    '#ff0000', '#123456', '#123456',
]);
assert.deepEqual(markedElement.child.context.operations
    .filter((operation) => operation.kind === 'fillRect')
    .map(({ color, args }) => ({ color, args })), [
        { color: '#00ff00', args: [5, 9, 6, 1] },
    ]);

assert.equal(FONT_RUNTIME_METRICS.tintEntries, 2);
assert.ok(FONT_RUNTIME_METRICS.tintEvictions > 0,
    'bounded tint LRU should evict colors beyond its configured capacity');
assert.ok(__fontRuntimeTest.cacheKeys().every((key) => typeof key === 'string'));
assert.ok(FONT_RUNTIME_METRICS.metricHits > 0,
    'vertical metrics should be reused instead of rescanning every glyph');

/* Prepared markup is independent of the component's default color. A cache
 * hit must still resolve default/reset colors from the new paint request. */
const recoloredElement = element();
recoloredElement.classList.owner = recoloredElement;
const preparedHitsBeforeRecolor = FONT_RUNTIME_METRICS.preparedHits;
assert.equal(await drain(paintCacheText(recoloredElement, {
    w: 40, h: 8,
    props: { font: 7, text: markedText, color: 0x654321 },
}, { spriteSource: 'test' })), true);
assert.equal(FONT_RUNTIME_METRICS.preparedHits, preparedHitsBeforeRecolor + 1,
    'identical text/layout missed the prepared cache');
assert.deepEqual(recoloredElement.child.context.operations
    .filter((operation) => operation.kind === 'drawImage')
    .map((operation) => operation.source.tintColor), [
        '#ff0000', '#654321', '#654321',
    ], 'cached tokens captured the previous component default color');

/* Real-clock preparation benchmark: the warm path must perform no generator
 * steps, while the cold path remains cooperatively sliced. */
__fontRuntimeTest.reset();
__fontRuntimeTest.configure({
    budgetMs: 3.5,
    clock: () => performance.now(),
    schedule: queueMicrotask,
    preparedEntries: 8,
    preparedUnits: 2 * 1024 * 1024,
});
const benchmarkText = '<col=ff0000>AB CD</col> '.repeat(2000);
let beforeSteps = FONT_RUNTIME_METRICS.steps;
let started = performance.now();
const coldPrepared = await __fontRuntimeTest.prepare(font, benchmarkText, 320, 4000, 8);
const coldMs = performance.now() - started;
const coldSteps = FONT_RUNTIME_METRICS.steps - beforeSteps;
beforeSteps = FONT_RUNTIME_METRICS.steps;
started = performance.now();
const warmPrepared = await __fontRuntimeTest.prepare(font, benchmarkText, 320, 4000, 8);
const warmMs = performance.now() - started;
const warmSteps = FONT_RUNTIME_METRICS.steps - beforeSteps;
const benchmarkMaxSliceMs = FONT_RUNTIME_METRICS.maxSliceMs;
const benchmarkOverBudgetSlices = FONT_RUNTIME_METRICS.overBudgetSlices;
assert.equal(warmPrepared, coldPrepared,
    'repeat preparation did not reuse its immutable prepared value');
assert.ok(coldSteps > 50000, `cold preparation unexpectedly did only ${coldSteps} steps`);
assert.equal(warmSteps, 0, 'prepared cache hit re-entered the cooperative parser');
assert.ok(warmMs < coldMs,
    `prepared cache was not materially faster (${coldMs.toFixed(3)} vs ${warmMs.toFixed(3)}ms)`);

/* Cancellation cannot publish a partial entry. The bounded LRU retains the
 * recently touched entry, evicts the true oldest, and refuses oversize work. */
__fontRuntimeTest.reset();
callbacks.length = 0;
fakeTime = 0;
scheduledSlices = 0;
__fontRuntimeTest.configure({
    budgetMs: 3,
    clock: () => (fakeTime += 0.1),
    schedule: (callback) => { scheduledSlices++; callbacks.push(callback); },
    preparedEntries: 2,
    preparedUnits: 1024,
});
let active = true;
const cancelled = __fontRuntimeTest.prepare(
    font, 'AB '.repeat(100), 100, 100, 8, () => active);
await Promise.resolve();
active = false;
assert.equal(await drain(cancelled), null, 'cancelled preparation produced a value');
assert.equal(FONT_RUNTIME_METRICS.cancelledJobs, 1);
assert.equal(FONT_RUNTIME_METRICS.preparedEntries, 0,
    'cancelled preparation polluted the prepared cache');

const cachedA = await drain(__fontRuntimeTest.prepare(font, 'A', 20, 8, 8));
await drain(__fontRuntimeTest.prepare(font, 'B', 20, 8, 8));
const hitsBeforeA = FONT_RUNTIME_METRICS.preparedHits;
assert.equal(await drain(__fontRuntimeTest.prepare(font, 'A', 20, 8, 8)), cachedA);
assert.equal(FONT_RUNTIME_METRICS.preparedHits, hitsBeforeA + 1);
await drain(__fontRuntimeTest.prepare(font, 'AB', 20, 8, 8));
assert.equal(FONT_RUNTIME_METRICS.preparedEntries, 2);
const missesBeforeB = FONT_RUNTIME_METRICS.preparedMisses;
await drain(__fontRuntimeTest.prepare(font, 'B', 20, 8, 8));
assert.equal(FONT_RUNTIME_METRICS.preparedMisses, missesBeforeB + 1,
    'bounded LRU retained its oldest entry instead of the recently used one');
assert.ok(FONT_RUNTIME_METRICS.preparedEvictions >= 2,
    'prepared entry limit did not evict old layouts');

__fontRuntimeTest.configure({ preparedUnits: 1 });
assert.equal(FONT_RUNTIME_METRICS.preparedEntries, 0,
    'lowering the memory limit did not trim retained layouts');
const skipsBefore = FONT_RUNTIME_METRICS.preparedSkips;
await drain(__fontRuntimeTest.prepare(font, 'AB', 20, 8, 8));
assert.equal(FONT_RUNTIME_METRICS.preparedEntries, 0);
assert.equal(FONT_RUNTIME_METRICS.preparedSkips, skipsBefore + 1,
    'oversize prepared layout bypassed its memory limit');
assert.equal(FONT_RUNTIME_METRICS.overBudgetSlices, 0);
assert.ok(FONT_RUNTIME_METRICS.maxSliceMs < 4);

console.log('font runtime cooperative queue tests passed ' +
    `(48k prepared text: cold ${coldMs.toFixed(3)}ms/${coldSteps} steps, ` +
    `warm ${warmMs.toFixed(3)}ms/${warmSteps} steps; ` +
    `max slice ${benchmarkMaxSliceMs.toFixed(3)}ms/` +
    `${benchmarkOverBudgetSlices} over budget; ` +
    `1200-glyph paint ${coldPaintSteps}->${warmPaintSteps} steps)`);
