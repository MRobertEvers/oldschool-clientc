/*
 * The transmit pump, against the three gates that make it correct.
 *
 * Every test here names a failure the C client's gating prevents. The
 * hidden-hook case is the one worth reading: a hook that advanced its serial
 * while hidden looks up to date when the panel opens, and the panel then shows
 * whatever it was built with — a server-driven inventory that is permanently
 * blank, which is exactly how that bug presented.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import { createHostKernel, HostState, ReadyAssetSource } from '../src/host_kernel.js';
import { createDriver, ScriptRegistry } from '../src/cs2_driver.js';
import { attachTransmitPump } from '../src/transmit_pump.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function harness() {
    const tree = createUITree();
    const host = createHostKernel({
        tree, state: new HostState(), assets: new ReadyAssetSource(),
    });
    const scripts = new ScriptRegistry();
    const runs = [];
    const driver = createDriver({ host, scripts, onWarning: () => {} });
    const pump = attachTransmitPump(driver, tree);

    /* Every script records that it ran; nothing else about them matters. */
    const script = (id) => {
        scripts.add(id, function* () { runs.push(id); });
        return id;
    };
    return { tree, host, driver, pump, runs, script };
}

/** Bake a component carrying one transmit hook and return its index. */
function widget(tree, { parent = -1, slot, scriptId, triggers = [], componentId = -1 }) {
    const index = tree.push({ parentIndex: parent, componentId, type: WIDGET_TYPE.TEXT });
    tree.setHook(index, slot, { scriptId, args: [], triggers });
    return index;
}

/* -------------------------------------------------------------------------
 * The early-out
 * ---------------------------------------------------------------------- */

test('a quiet tick performs no traversal at all', async () => {
    const { tree, driver, pump, script } = harness();
    widget(tree, { slot: 'onVarTransmit', scriptId: script(1), triggers: [300] });

    /* The first pass fires: a fresh hook has seen nothing. */
    pump.noteVarChanged(300);
    await driver.settle();
    const traversals = pump.stats.traversals;

    await driver.settle();
    assert.equal(pump.pending(), false);
    assert.equal(pump.stats.traversals, traversals,
        'a settled pump must not walk the hook set again');
});

test('a fresh hook fires once even though nothing has changed yet', () => {
    /*
     * Serials start at 1 and an unseen hook is at 0, so the first pass finds
     * it out of date. Starting both at 0 would leave a newly built panel
     * showing nothing until the next unrelated change.
     */
    const { tree, pump, script } = harness();
    widget(tree, { slot: 'onVarTransmit', scriptId: script(1), triggers: [300] });
    pump.noteVarChanged(300);
    assert.equal(pump.pump(), 1);
});

/* -------------------------------------------------------------------------
 * Gate 1: the component is gone
 * ---------------------------------------------------------------------- */

test('a reclaimed component\'s hook never fires', () => {
    const { tree, pump, script, runs } = harness();
    const index = widget(tree, { slot: 'onVarTransmit', scriptId: script(1), triggers: [300] });
    tree.remove(index);
    pump.noteVarChanged(300);
    assert.equal(pump.pump(), 0);
    assert.deepEqual(runs, []);
});

/* -------------------------------------------------------------------------
 * Gate 2: hidden defers without advancing the serial
 * ---------------------------------------------------------------------- */

test('a change reaching a hidden hook fires exactly once on reveal', async () => {
    const { tree, driver, pump, script, runs } = harness();
    const panel = tree.push({ type: WIDGET_TYPE.LAYER });
    const row = widget(tree, {
        parent: panel, slot: 'onVarTransmit', scriptId: script(7), triggers: [300],
    });
    tree.setHidden(panel, true);

    pump.noteVarChanged(300);
    await driver.settle();
    assert.deepEqual(runs, [], 'a hidden hook does not run');
    assert.equal(pump.stats.deferred, 1);

    /* Reveal, with NO further var change. The deferred work must still run —
     * this is the case where advancing the serial while hidden would have made
     * the hook look up to date and left the panel showing stale content. */
    tree.setHidden(panel, false);
    pump.noteWidgetsLoaded();
    await driver.settle();
    assert.deepEqual(runs, [7]);

    /* And exactly once: revealing again with nothing changed runs nothing. */
    pump.noteWidgetsLoaded();
    await driver.settle();
    assert.deepEqual(runs, [7]);
});

test('an ancestor being hidden defers the hook, not just the node itself', () => {
    const { tree, pump, script } = harness();
    const outer = tree.push({ type: WIDGET_TYPE.LAYER });
    const inner = tree.push({ parentIndex: outer, type: WIDGET_TYPE.LAYER });
    widget(tree, { parent: inner, slot: 'onVarTransmit', scriptId: script(3), triggers: [1] });
    tree.setHidden(outer, true);

    pump.noteVarChanged(1);
    assert.equal(pump.pump(), 0);
    assert.equal(pump.stats.deferred, 1);
});

/* -------------------------------------------------------------------------
 * Gate 3: the per-hook serial
 * ---------------------------------------------------------------------- */

test('a hook that is up to date does not re-run', async () => {
    const { tree, driver, pump, script, runs } = harness();
    widget(tree, { slot: 'onVarTransmit', scriptId: script(1), triggers: [300] });

    pump.noteVarChanged(300);
    await driver.settle();
    assert.deepEqual(runs, [1]);

    /* No new change: a second settle finds it current. */
    await driver.settle();
    assert.deepEqual(runs, [1]);

    pump.noteVarChanged(300);
    await driver.settle();
    assert.deepEqual(runs, [1, 1]);
});

/* -------------------------------------------------------------------------
 * Trigger filtering
 * ---------------------------------------------------------------------- */

test('only hooks that watch the changed var re-run', () => {
    const { tree, pump, script, runs } = harness();
    widget(tree, { slot: 'onVarTransmit', scriptId: script(1), triggers: [300] });
    widget(tree, { slot: 'onVarTransmit', scriptId: script(2), triggers: [400] });

    pump.noteVarChanged(400);
    pump.pump();
    assert.deepEqual(runs, [], 'dispatch only queues; nothing has settled yet');
    assert.equal(pump.stats.dispatched, 1, 'and only one hook was queued');
});

test('a container change with no id re-runs every inv hook', () => {
    /* `UPDATE_INV_FULL` for an unknown container: the reference treats "all
     * changed" as matching every hook rather than none. */
    const { tree, pump, script } = harness();
    widget(tree, { slot: 'onInvTransmit', scriptId: script(1), triggers: [93] });
    widget(tree, { slot: 'onInvTransmit', scriptId: script(2), triggers: [94] });
    pump.noteInvChanged(null);
    assert.equal(pump.pump(), 2);
});

test('the unfiltered families re-run on their own stamp', () => {
    /*
     * Chat, friend and misc carry no trigger list — there is nowhere in the
     * wire format to put one — so every registered hook re-runs. The chatbox's
     * own hook is what redraws its 500 line components; the client writes none
     * of them itself.
     */
    const { tree, pump, script } = harness();
    widget(tree, { slot: 'onChatTransmit', scriptId: script(1) });
    widget(tree, { slot: 'onChatTransmit', scriptId: script(2) });
    pump.noteChanged('chat');
    assert.equal(pump.pump(), 2);
});

/* -------------------------------------------------------------------------
 * Timers
 * ---------------------------------------------------------------------- */

test('timer hooks fire every tick, but not while hidden', () => {
    const { tree, pump, script } = harness();
    const panel = tree.push({ type: WIDGET_TYPE.LAYER });
    widget(tree, { parent: panel, slot: 'onTimer', scriptId: script(1) });
    widget(tree, { slot: 'onTimer', scriptId: script(2) });

    assert.equal(pump.tick(), 2);
    assert.equal(pump.tick(), 2, 'a timer has no serial; it fires every tick');

    tree.setHidden(panel, true);
    assert.equal(pump.tick(), 1, 'a closed panel must not tick its clock');
});

/* -------------------------------------------------------------------------
 * Settlement
 * ---------------------------------------------------------------------- */

test('a hook that queues more work still reaches a fixed point', async () => {
    /*
     * The pump is the driver's follow-up source, so a transmit hook that
     * writes a var another hook watches must settle rather than pass the
     * frame back and forth forever.
     */
    const { tree, driver, pump, host, runs } = harness();
    const first = tree.push({ type: WIDGET_TYPE.TEXT });
    const second = tree.push({ type: WIDGET_TYPE.TEXT });
    tree.setHook(first, 'onVarTransmit', { scriptId: 10, args: [], triggers: [300] });
    tree.setHook(second, 'onVarTransmit', { scriptId: 11, args: [], triggers: [400] });

    driver.scripts.add(10, function* () {
        runs.push(10);
        /* A SERVER-style write, which is what arms the pump; a script's own
         * write deliberately does not. */
        host.state.setVarp(400, 1);
        pump.noteVarChanged(400);
    });
    driver.scripts.add(11, function* () { runs.push(11); });

    pump.noteVarChanged(300);
    assert.equal(await driver.settle(), true);
    assert.deepEqual(runs, [10, 11]);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { await fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
