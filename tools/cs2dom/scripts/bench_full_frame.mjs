/*
 * The whole frame, not just the host calls.
 *
 * `bench_mass_rebuild.mjs` measures the boundary the old runtime was paying
 * for. This measures what a browser actually has to do in 16 ms: execute the
 * scripts, settle, resolve layout, walk the emit list and paint it. If any of
 * those four stages had been quietly expensive, the host-call number would
 * still have looked good and the interface would still have stuttered.
 *
 *   node scripts/bench_full_frame.mjs [--rows N] [--runs N]
 */

import { createSession } from '../src/session.js';
import { createRecordingSurface } from '../src/painter.js';
import { WIDGET_TYPE, DYNAMIC_SUB_ID_BASE } from '../src/uitree.js';
import { ReadyAssetSource } from '../src/host_kernel.js';

const args = process.argv.slice(2);
const rows = Number(flag('--rows') ?? 3230);
const runs = Number(flag('--runs') ?? 9);

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

const GROUP = 0x0271;
const CONTAINER = (GROUP << 16) | 1;
const REBUILD = 5244;

/**
 * The rebuild, written the way the emitter would have lowered it.
 *
 * Hand-written rather than compiled from a tree so the benchmark has no cache
 * dependency; the shape is the one `cs2_js_emit.js` produces — direct
 * positional host calls, no request objects — which is what is being measured.
 */
function* rebuildScript(H) {
    H.cc_deleteall(CONTAINER);
    for( let i = 0; i < rows; i++ )
    {
        const sub = DYNAMIC_SUB_ID_BASE + i;
        H.cc_create(CONTAINER, WIDGET_TYPE.TEXT, sub, 0);
        H.cc_setposition(0, i * 16, 0, 0);
        H.cc_setsize(0, 16, 1, 0);
        H.cc_setcolour(0xff981f);
        H.cc_settext(`row ${i}`);
        H.cc_settextalign(0, 1, 0);
        H.cc_settrans(0);
        H.cc_setonop({ scriptId: 5245, args: [i], triggers: [] });
        H.cc_sethide(0);
    }
}

function build() {
    /* A recording surface, cleared each frame: this measures the painter's
     * decisions and its call volume, not a GPU. A real canvas adds the rasterise
     * cost, which is the browser's and not this design's to defend. */
    const surface = createRecordingSurface();
    const session = createSession({
        surface,
        assets: new ReadyAssetSource(),
        fonts: { get: () => ({ id: 495 }) },
        root: { x: 0, y: 0, width: 800, height: 600 },
        onWarning: () => {},
    });
    session.scripts.add(REBUILD, rebuildScript);

    const root = session.tree.push({
        componentId: (GROUP << 16) | 0, type: WIDGET_TYPE.LAYER,
        props: { x: 0, y: 0, width: 800, height: 600 },
    });
    session.tree.push({
        parentIndex: root, componentId: CONTAINER, subId: 1, type: WIDGET_TYPE.LAYER,
        props: { x: 0, y: 0, width: 480, height: 600, scrollHeight: rows * 16 },
    });
    return { session, surface };
}

const samples = [];
let stages = null;
let commands = 0;
let calls = 0;

for( let run = 0; run < runs; run++ )
{
    const { session, surface } = build();
    /* Warm the shapes; a cold run measures the JIT, not the design. */
    session.driver.dispatch(REBUILD);
    await session.frame(0);

    surface.calls.length = 0;
    const before = session.host.calls;

    session.driver.dispatch(REBUILD);
    const start = performance.now();
    const painted = await session.frame(16);
    const elapsed = performance.now() - start;
    if( !painted ) throw new Error('the rebuild frame painted nothing');

    samples.push(elapsed);
    calls = session.host.calls - before;
    commands = session.emitter.commands.length;
    stages = {
        hostCalls: calls,
        emitCommands: commands,
        drawCalls: surface.calls.length,
        componentsLive: session.tree.liveCount,
    };
}

samples.sort((a, b) => a - b);
const median = samples[Math.floor(samples.length / 2)];
const max = samples[samples.length - 1];

console.log(JSON.stringify({
    rows,
    runs,
    ...stages,
    medianMs: Number(median.toFixed(3)),
    minMs: Number(samples[0].toFixed(3)),
    maxMs: Number(max.toFixed(3)),
    /* The old runtime's hard gate, which it did not meet. */
    underTenMs: max < 10,
    /* And the one that actually matters for a browser. */
    withinOneFrame: max < 16.7,
}, null, 2));
