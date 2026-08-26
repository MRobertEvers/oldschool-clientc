/*
 * Phase 6's gate.
 *
 * The plan makes the optional AOT/superinstruction phase conditional: "add
 * only if profiles still identify interpreter dispatch as material". This
 * measures that share, so the decision is made on a number rather than on an
 * assumption — including the assumption that more optimisation is always worth
 * having.
 *
 * The question is precise. Of the time a real transaction spends, how much is
 * the SCRIPT's own control flow and arithmetic, versus the work the host and
 * the tree do on its behalf? Phase 6 would attack the first. If the first is
 * small, Phase 6 is effort spent where the time is not.
 *
 *   node scripts/bench_dispatch_share.mjs [--rows N] [--runs N]
 */

import { createUITree, WIDGET_TYPE, DYNAMIC_SUB_ID_BASE } from '../src/uitree.js';
import { createHostKernel, HostState, ReadyAssetSource } from '../src/host_kernel.js';
import * as K from '../src/cs2_intrinsics.js';

const args = process.argv.slice(2);
const rows = Number(flag('--rows') ?? 3230);
const runs = Number(flag('--runs') ?? 9);

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

const GROUP = 0x0271;
const CONTAINER = (GROUP << 16) | 1;

function build() {
    const tree = createUITree({ capacityHint: rows + 64 });
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(),
    });
    const root = tree.push({ componentId: (GROUP << 16) | 0, type: WIDGET_TYPE.LAYER });
    tree.push({ parentIndex: root, componentId: CONTAINER, subId: 1, type: WIDGET_TYPE.LAYER });
    return { tree, host };
}

/**
 * The real transaction: a container rebuild, exactly as the emitter lowers one.
 *
 * Generator, host calls, intrinsics — the shape `cs2_js_emit.js` produces.
 */
function* rebuild(H) {
    H.cc_deleteall(CONTAINER);
    for( let i = 0; i < rows; i++ )
    {
        const sub = K.add(DYNAMIC_SUB_ID_BASE, i);
        H.cc_create(CONTAINER, WIDGET_TYPE.TEXT, sub, 0);
        H.cc_setposition(0, K.multiply(i, 16), 0, 0);
        H.cc_setsize(0, 16, 1, 0);
        H.cc_setcolour(0xff981f);
        H.cc_settext(K.join('row ', K.tostring(i)));
        H.cc_settrans(0);
        H.cc_sethide(0);
    }
}

/**
 * The same control flow and arithmetic with the host calls removed.
 *
 * What is left is what Phase 6 would optimise: the generator's own stepping,
 * the loop, the intrinsics. Measuring it directly is the only honest way to
 * ask whether it is where the time goes.
 */
function* dispatchOnly() {
    let sink = 0;
    for( let i = 0; i < rows; i++ )
    {
        const sub = K.add(DYNAMIC_SUB_ID_BASE, i);
        sink = K.add(sink, sub);
        sink = K.add(sink, K.multiply(i, 16));
        sink = K.add(sink, K.stringLength(K.join('row ', K.tostring(i))));
    }
    return sink;
}

function time(fn) {
    const start = performance.now();
    fn();
    return performance.now() - start;
}

const full = [];
const bare = [];

for( let run = 0; run < runs; run++ )
{
    const { tree, host } = build();
    /* Warm both shapes; a cold run measures the JIT. */
    for( const step of rebuild(host) ) void step;
    for( const step of dispatchOnly() ) void step;

    const fresh = build();
    full.push(time(() => { for( const step of rebuild(fresh.host) ) void step; }));
    bare.push(time(() => { for( const step of dispatchOnly() ) void step; }));
}

const median = (values) => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
const fullMs = median(full);
const bareMs = median(bare);
const share = bareMs / fullMs;

console.log(JSON.stringify({
    rows,
    runs,
    fullTransactionMs: Number(fullMs.toFixed(3)),
    scriptDispatchOnlyMs: Number(bareMs.toFixed(3)),
    dispatchSharePercent: Number((share * 100).toFixed(1)),
    /*
     * The plan's own words: Phase 6 is worth doing only if interpreter
     * dispatch is still MATERIAL after the boundary and tree costs are gone.
     * A tenth of the transaction is not material — halving it would buy less
     * than the measurement error on the rest.
     */
    phase6Justified: share > 0.25,
    verdict: share > 0.25
        ? 'dispatch is material; Phase 6 has something to attack'
        : 'dispatch is not where the time goes; Phase 6 is not justified',
}, null, 2));
