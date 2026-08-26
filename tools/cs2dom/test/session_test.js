/*
 * The whole loop: tick, settle, interact, settle, paint.
 *
 * The order is the C client's frame contract, and the tests below are about
 * the consequences of getting it wrong — painting a tree a parked script had
 * not finished writing, handling a click against that same tree, or repainting
 * an interface where nothing moved.
 */

import assert from 'node:assert/strict';

import { createSession, TICK_MS } from '../src/session.js';
import { createRecordingSurface } from '../src/painter.js';
import { HOST_PARK } from '../src/host_kernel.js';
import { WIDGET_TYPE } from '../src/uitree.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function harness({ loader = null } = {}) {
    const surface = createRecordingSurface();
    const session = createSession({
        surface, loader, onWarning: () => {},
        root: { x: 0, y: 0, width: 400, height: 300 },
    });
    const runs = [];
    const script = (id, body) => {
        session.scripts.add(id, body ?? function* () { runs.push(id); });
        return id;
    };
    return { session, surface, runs, script };
}

/** A component with an id, a box, and optionally a hook. */
function widget(session, { parent = -1, componentId, type = WIDGET_TYPE.TEXT,
                          x = 0, y = 0, width = 100, height = 50, slot, scriptId } = {}) {
    const index = session.tree.push({
        parentIndex: parent, componentId, type,
        props: { x, y, width, height, xMode: 0, yMode: 0, widthMode: 0, heightMode: 0,
                 colour: 0x112233, filled: 1 },
    });
    if( slot ) session.tree.setHook(index, slot, { scriptId, args: [], triggers: [] });
    return index;
}

/* -------------------------------------------------------------------------
 * The idle case
 * ---------------------------------------------------------------------- */

test('an idle interface paints nothing after the first frame', async () => {
    const { session, surface } = harness();
    widget(session, { componentId: 0x10001, type: WIDGET_TYPE.RECTANGLE });

    assert.equal(await session.frame(0), true, 'the first frame builds the list');
    const painted = surface.calls.length;

    assert.equal(await session.frame(16), false);
    assert.equal(await session.frame(32), false);
    assert.equal(surface.calls.length, painted, 'and draws nothing further');
    assert.equal(session.stats.painted, 1);
});

test('an idle interface with a timer hook still settles quietly', async () => {
    /*
     * A timer fires every tick by definition, so the pump dispatches it — but
     * a script that changes nothing must not produce a repaint. This is where
     * the emit retention gate earns its keep: the gameframe clock varc ticks
     * every frame on a real cache.
     */
    const { session, script } = harness();
    const index = widget(session, { componentId: 0x20001, type: WIDGET_TYPE.RECTANGLE });
    session.tree.setHook(index, 'onTimer', { scriptId: script(1), args: [], triggers: [] });

    await session.frame(0);
    await session.frame(TICK_MS * 2);
    const painted = session.stats.painted;

    await session.frame(TICK_MS * 4);
    assert.ok(session.stats.ticks >= 2, 'ticks did run');
    assert.equal(session.stats.painted, painted, 'and painted nothing');
});

/* -------------------------------------------------------------------------
 * Parking
 * ---------------------------------------------------------------------- */

test('a parked script paints nothing and does not accept input', async () => {
    /*
     * The tree is validly intermediate while a script is parked: earlier
     * operations stayed applied and the script has not finished deciding what
     * the frame looks like. Painting it shows a half-built panel; hit-testing
     * it clicks a button that is about to move.
     */
    let release;
    const gate = new Promise((resolve) => { release = resolve; });
    const { session, surface } = harness({
        loader: { loadSync: () => false, load: () => gate },
    });
    widget(session, { componentId: 0x30001, type: WIDGET_TYPE.RECTANGLE,
                      slot: 'onClick', scriptId: 1 });

    session.scripts.add(1, function* (H) {
        H.cc_settext('half-built');
        /* Park on a sprite the loader will not answer yet. */
        while( H.if_setgraphic(99, 0x30001) === HOST_PARK ) yield;
    });

    session.driver.dispatch(1);
    const before = surface.calls.length;
    assert.equal(await session.frame(0), false, 'a parked frame paints nothing');
    assert.equal(surface.calls.length, before);

    session.post({ type: 'down', x: 10, y: 10 });
    assert.equal(await session.frame(16), false, 'still parked');
    assert.equal(session.input.length, 1, 'and the click is queued, not lost');

    release();
});

/* -------------------------------------------------------------------------
 * Interaction
 * ---------------------------------------------------------------------- */

test('a click dispatches, and only after press and release on one component', async () => {
    const { session, runs, script } = harness();
    widget(session, { componentId: 0x40001, x: 0, y: 0, width: 100, height: 50,
                      slot: 'onClick', scriptId: script(1) });
    widget(session, { componentId: 0x40002, x: 200, y: 0, width: 100, height: 50,
                      slot: 'onClick', scriptId: script(2) });
    await session.frame(0);

    session.post({ type: 'down', x: 10, y: 10 });
    session.post({ type: 'up', x: 10, y: 10 });
    await session.frame(16);
    assert.deepEqual(runs, [1]);

    /* Pressed on one, released on another: a cancelled click, not a click on
     * whichever component the pointer happened to land on. */
    session.post({ type: 'down', x: 10, y: 10 });
    session.post({ type: 'up', x: 210, y: 10 });
    await session.frame(32);
    assert.deepEqual(runs, [1]);
});

test('the hook sees the event that caused it, not the pointer since', async () => {
    const { session } = harness();
    const seen = [];
    session.scripts.add(1, function* (H) { seen.push(H.event('mousex')); });
    widget(session, { componentId: 0x50001, width: 400, height: 300,
                      slot: 'onClick', scriptId: 1 });
    await session.frame(0);

    session.post({ type: 'down', x: 11, y: 5 });
    session.post({ type: 'up', x: 11, y: 5 });
    session.post({ type: 'move', x: 333, y: 200 });
    await session.frame(16);

    assert.deepEqual(seen, [11], 'the click coordinate, not the later move');
});

test('hover fires leave before enter', async () => {
    const { session, runs, script } = harness();
    const first = widget(session, {
        componentId: 0x60001, x: 0, y: 0, width: 50, height: 50,
        slot: 'onMouseOver', scriptId: script(1),
    });
    session.tree.setHook(first, 'onMouseLeave', { scriptId: script(2), args: [], triggers: [] });
    widget(session, { componentId: 0x60002, x: 100, y: 0, width: 50, height: 50,
                      slot: 'onMouseOver', scriptId: script(3) });
    await session.frame(0);

    session.post({ type: 'move', x: 10, y: 10 });
    await session.frame(16);
    assert.deepEqual(runs, [1]);

    session.post({ type: 'move', x: 110, y: 10 });
    await session.frame(32);
    assert.deepEqual(runs, [1, 2, 3], 'leave the first, then enter the second');
});

test('pointer motion coalesces', () => {
    const { session } = harness();
    for( let i = 0; i < 50; i++ ) session.post({ type: 'move', x: i, y: 0 });
    assert.equal(session.input.length, 1, 'only the latest position matters');
    assert.equal(session.input[0].x, 49);
});

/* -------------------------------------------------------------------------
 * Server updates
 * ---------------------------------------------------------------------- */

test('a server update is one transaction and arms the pump', async () => {
    const { session, runs, script } = harness();
    const index = widget(session, { componentId: 0x70001, type: WIDGET_TYPE.RECTANGLE });
    session.tree.setHook(index, 'onVarTransmit',
        { scriptId: script(9), args: [], triggers: [300] });
    await session.frame(0);
    runs.length = 0;

    session.applyServerUpdate({ varps: { 300: 5, 301: 9 } });
    await session.frame(16);
    assert.deepEqual(runs, [9], 'the hook watching 300 ran once for the whole batch');
    assert.equal(session.host.state.varp(301), 9);
});

/* -------------------------------------------------------------------------
 * Resize
 * ---------------------------------------------------------------------- */

test('a resize re-lays out and repaints; the same size does neither', async () => {
    const { session } = harness();
    session.tree.push({
        componentId: 0x80001, type: WIDGET_TYPE.RECTANGLE,
        props: { x: 0, y: 0, width: 0, height: 0, widthMode: 1, heightMode: 1,
                 colour: 1, filled: 1 },
    });
    await session.frame(0);
    assert.equal(session.emitter.commands[0].width, 400);

    assert.equal(session.resize(800, 600), true);
    assert.equal(await session.frame(16), true);
    assert.equal(session.emitter.commands[0].width, 800);

    assert.equal(session.resize(800, 600), false, 'the same size is not a resize');
});

/* -------------------------------------------------------------------------
 * Tick pacing
 * ---------------------------------------------------------------------- */

test('a stall does not cause a tick avalanche', async () => {
    const { session } = harness();
    await session.frame(0);
    const before = session.stats.ticks;
    /* Ten seconds of debt: five hundred ticks' worth. */
    await session.frame(10_000);
    assert.ok(session.stats.ticks - before <= 5,
        `catch-up must be bounded, ran ${session.stats.ticks - before}`);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { await fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
