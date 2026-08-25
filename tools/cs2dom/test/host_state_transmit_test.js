import assert from 'node:assert/strict';

import { createHostRuntime } from '../src/host_runtime.js';

function binding(scriptId) {
    return { script: { id: scriptId }, args: [] };
}

function layer(fileId, name, hooks = {}, triggers = {}, hidden = false) {
    return {
        fileId,
        name,
        kind: 'Layer',
        type: 0,
        layer: fileId === 0 ? null : 0,
        static: {
            x: 0,
            y: 0,
            width: 100,
            height: 100,
            widthMode: 0,
            heightMode: 0,
            hidden,
        },
        hooks,
        events: {},
        ops: [],
        triggers,
        dynamic: [],
        dependencies: [],
        rawFields: {},
    };
}

function fixtureIr({ hiddenListener = false } = {}) {
    const components = [
        layer(0, 'root'),
        layer(1, 'var5', { onvarptransmit: binding(11) }, { varptriggers: [5] }),
        layer(2, 'var6', { on_var_transmit: binding(12) }, { varptriggers: [6] }),
        layer(3, 'var115', { onvarptransmit: binding(15) }, { varptriggers: [115] }),
        layer(4, 'var_any', { onvarptransmit: binding(13) }),
        layer(5, 'inv10', { oninvtransmit: binding(21) }, { invtriggers: [10] }),
        layer(6, 'inv20', { on_inv_transmit: binding(22) }, { invtriggers: [20] }),
        layer(7, 'stat3', { onstattransmit: binding(31) }, { stattriggers: [3] }),
        /* Rev-239 has no VarC widget-transmit registry. Keeping this hook in
         * the fixture proves a POP_VARC does not invent one in JavaScript. */
        layer(9, 'varc', { on_varc_transmit: binding(41) }),
    ];
    if( hiddenListener ) components.push(
        layer(8, 'hidden', { onvarptransmit: binding(99) }, { varptriggers: [5] }, true));
    return {
        interfaceId: 708,
        components,
    };
}

const seen = [];
let rewrite = false;
let host;
host = createHostRuntime(fixtureIr(), {
    viewport: { width: 100, height: 100 },
    hostData: { varbitVarp: { 3958: 115 } },
    invoke: (intent, runtime) => {
        seen.push(intent);
        if( rewrite && intent.hook.scriptId === 11 ) {
            rewrite = false;
            runtime.setHook('var6', 'on_var_transmit', binding(112));
            runtime.writeState('varp', 7, 1);
        }
    },
});

/* Genuine writes are retained immediately, while listeners are deferred and
 * duplicate ids coalesce to one pass at the next logic tick. */
let write = host.writeState('varp', 5, 42);
assert.equal(write.changed, true);
assert.equal(write.intents.length, 0);
assert.equal(host.readState('varp', 5), 42);
assert.deepEqual(host.snapshot().pendingTransmits.var, { all: false, ids: [5] });

write = host.writeState('varp', 5, 42);
assert.equal(write.changed, false, 'an equal varp write raised a native dirty flag');
host.writeState('varp', 5, 43);
host.writeState('varp', 6, 1);
assert.deepEqual(host.snapshot().pendingTransmits.var, { all: false, ids: [5, 6] });

let tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [11, 12, 13]);
assert.deepEqual(tick.intents[0].event, {
    type: 'transmit', kind: 'varp', triggers: [5, 6], all: false,
});
assert.deepEqual(host.snapshot().pendingTransmits, { friend: false, chat: false });
assert.equal(host.dispatch({ type: 'tick' }).intents.length, 0,
    'a cleared var dirty flag fired forever');

/* A relevant change reaching a hidden hook is remembered. The exact hook runs
 * once, on the tick after its component (or an ancestor) is unhidden. */
const hiddenSeen = [];
const hiddenHost = createHostRuntime(fixtureIr({ hiddenListener: true }), {
    viewport: { width: 100, height: 100 },
    invoke: (intent) => hiddenSeen.push(intent),
});
hiddenHost.writeState('varp', 5, 1);
tick = hiddenHost.dispatch({ type: 'tick' });
assert(!tick.intents.some((intent) => intent.hook.scriptId === 99));
assert.equal(hiddenHost.snapshot().pendingTransmits.unhide.var.length, 1);
hiddenHost.mutate('if_sethide', 'hidden', false);
assert.equal(hiddenSeen.some((intent) => intent.hook.scriptId === 99), false,
    'unhide synchronously dispatched a pending transmit hook');
tick = hiddenHost.dispatch({ type: 'tick' });
assert.equal(tick.intents.filter((intent) => intent.hook.scriptId === 99).length, 1);
assert.equal(hiddenHost.snapshot().pendingTransmits.unhide, undefined);

/* Every batch is captured before invocation. Rebinding a later listener from
 * the first callback cannot alter this tick, and a write from that callback is
 * retained for the following tick. */
seen.length = 0;
rewrite = true;
host.writeState('varp', 5, 44);
host.writeState('varp', 6, 2);
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [11, 12, 13]);
assert.deepEqual(host.snapshot().pendingTransmits.var, { all: false, ids: [7] });
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [13]);

/* One inventory id filters listeners. Two ids in one tick use the native
 * wildcard path rather than incorrectly testing just the first container. */
host.writeState('inv', 10, { 100: 2 });
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [21]);
assert.deepEqual(tick.intents[0].event, {
    type: 'transmit', kind: 'inv', triggers: [10], all: false, trigger: 10,
});
host.writeState('inv', 10, { 100: 3 });
host.writeState('inv', 20, { 200: 1 });
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [21, 22]);
assert.equal(tick.intents[0].event.all, true);

/* A varbit announces its backing varp, never the numerically-equal varbit id. */
host.request({ kind: 'POP_VARBIT', varbit_id: 3958, value: 1 });
assert.deepEqual(host.snapshot().pendingTransmits.var, { all: false, ids: [115] });
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [15, 13]);

/* VarCs are deliberately local state only at rev 239; stats use the same later
 * tick and changed-id filtering as the native stat dispatch. */
host.request({ kind: 'POP_VARC_INT', varc_id: 8, value: 9 });
assert.equal(host.dispatch({ type: 'tick' }).intents.length, 0);
host.writeState('stat', 3, 2);
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => intent.hook.scriptId), [31]);

/* Pending work survives a React snapshot/restore without eager dispatch. */
host.writeState('inv', 10, { 100: 4 });
const saved = host.snapshot();
const restoredSeen = [];
const restored = createHostRuntime(fixtureIr(), {
    ...saved,
    hostData: { varbitVarp: { 3958: 115 } },
    invoke: (intent) => restoredSeen.push(intent),
});
assert.deepEqual(restored.snapshot().pendingTransmits.inv, { all: false, ids: [10] });
assert.equal(restoredSeen.length, 0);
restored.dispatch({ type: 'tick' });
assert.deepEqual(restoredSeen.map((intent) => intent.hook.scriptId), [21]);

/* The native 64-id buffer fails open: the 65th real change becomes one bounded
 * wildcard dispatch, never an unbounded snapshot or a dropped listener. */
const capped = createHostRuntime(fixtureIr(), { viewport: { width: 100, height: 100 } });
for( let id = 0; id <= 64; id++ ) capped.writeState('varp', id, id + 1);
assert.deepEqual(capped.snapshot().pendingTransmits.var, { all: true, ids: [] });
assert.deepEqual(capped.dispatch({ type: 'tick' }).intents.map((intent) => intent.hook.scriptId),
    [11, 12, 15, 13]);

console.log('host state deferred transmit tests passed');
