/*
 * The transaction the old runtime could not fit in a frame.
 *
 * A `ca_tasks` filter click was measured at 35,595 host calls creating ~3,230
 * components in one logical tick, and cost 10.7-12.1 ms through the C/WASM
 * bridge: roughly half in the VM and its record bridge, half replaying 22,622
 * packed mutations into a JavaScript tree. The hard gate is 10 ms and it was
 * not being met.
 *
 * This measures the same shape with the bridge gone — generated JavaScript
 * calling the HostKernel directly, mutating UITreeJS in place, no wire, no
 * replay. It is not a claim about `ca_tasks` itself (that needs the real
 * closure and the real cache), but it is the same work at the same scale, and
 * it is the number the design rests on.
 *
 *   node scripts/bench_mass_rebuild.mjs [--rows N] [--runs N]
 */

import { createUITree, WIDGET_TYPE, DYNAMIC_SUB_ID_BASE } from '../src/uitree.js';
import { createHostKernel, HostState, ReadyAssetSource } from '../src/host_kernel.js';

const args = process.argv.slice(2);
const rows = Number(flag('--rows') ?? 3230);
const runs = Number(flag('--runs') ?? 9);

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

const GROUP = 0x0271;
const CONTAINER = (GROUP << 16) | 1;

/**
 * One rebuild, in the shape the cache's own container builders use: clear the
 * container, then per row create a widget and set position, size, colour,
 * text, transparency and two hooks. That is eleven host calls a row, which is
 * where the 35,595 comes from at this row count.
 */
function rebuild(host, tree) {
    host.cc_deleteall(CONTAINER);
    for( let i = 0; i < rows; i++ )
    {
        const sub = DYNAMIC_SUB_ID_BASE + i;
        host.cc_create(CONTAINER, WIDGET_TYPE.LAYER, sub, 0);
        host.cc_setposition(0, i * 16, 0, 0);
        host.cc_setsize(480, 16, 0, 0);
        host.cc_setcolour(0xff981f);
        host.cc_settext(`row ${i}`);
        host.cc_settrans(0);
        host.cc_setonop({ scriptId: 5244, args: [i], triggers: [] });
        host.cc_setonmouserepeat({ scriptId: 5245, args: [i], triggers: [] });
        /* And the read-back a real builder does, which is the barrier that
         * stops any of this being batched away. */
        host.cc_find(CONTAINER, sub);
        host.cc_setsize(480, 16, 0, 0);
        host.cc_sethide(0);
    }
}

function build() {
    const tree = createUITree({ capacityHint: rows + 64 });
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(),
    });
    const root = tree.push({ componentId: (GROUP << 16) | 0, type: WIDGET_TYPE.LAYER });
    tree.push({
        parentIndex: root, componentId: CONTAINER, subId: 1, type: WIDGET_TYPE.LAYER,
    });
    return { tree, host };
}

const samples = [];
let calls = 0;
let created = 0;

for( let run = 0; run < runs; run++ )
{
    const { tree, host } = build();
    /* Warm the shapes on a small pass first; a cold run measures the JIT. */
    if( run === 0 ) rebuild(host, tree);

    const before = host.calls;
    const start = performance.now();
    rebuild(host, tree);
    const elapsed = performance.now() - start;

    samples.push(elapsed);
    calls = host.calls - before;
    created = tree.liveCount;
}

samples.sort((a, b) => a - b);
const median = samples[Math.floor(samples.length / 2)];

console.log(JSON.stringify({
    rows,
    runs,
    hostCallsPerRebuild: calls,
    componentsLive: created,
    medianMs: Number(median.toFixed(3)),
    minMs: Number(samples[0].toFixed(3)),
    maxMs: Number(samples[samples.length - 1].toFixed(3)),
    /* The gate the old runtime kept missing. */
    underTenMs: samples[samples.length - 1] < 10,
}, null, 2));
