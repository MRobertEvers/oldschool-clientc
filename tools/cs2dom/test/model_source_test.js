/*
 * The toridraw seam.
 *
 * Three properties, and each one is about a frame that must not stall or lie:
 * a still model rasterises once however long it is on screen; a spinning one
 * supersedes its own previous frame instead of queueing behind it; and a
 * render that has not finished draws nothing rather than a stale angle.
 */

import assert from 'node:assert/strict';

import { createModelSource, poseOwner } from '../src/model_source.js';
import { ModelStore } from '../src/assets.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/**
 * A controller that records what it was asked for and lets a test settle each
 * request by hand. Standing in for the worker, because what is being tested is
 * the handoff and not the rasteriser.
 */
function fakeController() {
    const requests = [];
    const byOwner = new Map();
    return {
        requests,
        render(request) {
            /* Supersede, as the real controller does: a widget's previous
             * request is retired the moment a new one for it arrives. */
            const previous = byOwner.get(request.owner);
            if( previous ) previous.settle({ stale: true });

            let settle;
            const completion = new Promise((resolve) => { settle = resolve; });
            const item = { request, settle, completion };
            byOwner.set(request.owner, item);
            requests.push(item);
            return { requestId: requests.length, completion };
        },
        finish(index, result) { requests[index].settle(result); },
        dispose() {},
    };
}

const POSE = { width: 100, height: 50, zoom: 500, angleX: 150, angleY: 0, anim: -1 };

/* -------------------------------------------------------------------------
 * Supersession
 * ---------------------------------------------------------------------- */

test('the owner is the widget, so a spin supersedes rather than queues', () => {
    /*
     * Keying the owner by POSE would make every angle a separate owner and the
     * worker's queue would grow for as long as the model spun — by the time
     * the tenth frame rendered, nine of them would be wrong.
     */
    assert.equal(poseOwner(7, POSE), poseOwner(7, { ...POSE, angleY: 512 }));
    assert.notEqual(poseOwner(7, POSE), poseOwner(8, POSE));
    assert.notEqual(poseOwner(7, POSE), poseOwner(7, { ...POSE, width: 32 }),
        'a different box is a different widget');
});

test('a superseded render answers null, not a stale angle', async () => {
    const controller = fakeController();
    const source = createModelSource({ controller });

    const first = source.render(7, POSE);
    /* Let the first request register before the second supersedes it. */
    await Promise.resolve();
    const second = source.render(7, { ...POSE, angleY: 512 });

    controller.finish(1, { bitmap: 'spun', width: 100, height: 50 });
    assert.equal(await first, null, 'the stale angle is not drawn');
    assert.ok(await second);
    assert.equal(source.stats.superseded, 1);
});

/* -------------------------------------------------------------------------
 * Not asking twice
 * ---------------------------------------------------------------------- */

test('the same pose in flight is asked for once', async () => {
    /*
     * A widget asks every frame until the image lands. Without this the worker
     * gets sixty identical requests a second for one still model.
     */
    const controller = fakeController();
    const source = createModelSource({ controller });

    const a = source.render(7, POSE);
    const b = source.render(7, POSE);
    const c = source.render(7, POSE);
    assert.equal(controller.requests.length, 1);

    controller.finish(0, { bitmap: 'done', width: 100, height: 50 });
    assert.deepEqual([await a, await b, await c].map((r) => r?.bitmap),
        ['done', 'done', 'done']);
    assert.equal(source.stats.requested, 1);
});

test('a still model rasterises once however long it is on screen', async () => {
    const controller = fakeController();
    const source = createModelSource({ controller });
    const store = new ModelStore({ render: (id, pose) => source.render(id, pose) });

    /* Frame one: the store misses and the caller services it. */
    assert.equal(store.get(7, POSE), null);
    const pending = store.load(7, POSE);
    controller.finish(0, { bitmap: 'still', width: 100, height: 50 });
    await pending;

    /* Every frame after: a cache hit, and no further request. */
    for( let frame = 0; frame < 120; frame++ ) assert.ok(store.get(7, POSE));
    assert.equal(controller.requests.length, 1);
    assert.equal(store.stats.rendered, 1);
});

/* -------------------------------------------------------------------------
 * Failure
 * ---------------------------------------------------------------------- */

test('a worker that fails is reported and answers null', async () => {
    /* Throwing would abandon the frame over a widget that draws nothing. */
    const warnings = [];
    const source = createModelSource({
        controller: { render() { throw new Error('worker gone'); }, dispose() {} },
        onWarning: (message) => warnings.push(message),
    });
    assert.equal(await source.render(7, POSE), null);
    assert.match(warnings[0], /model 7: worker gone/);
    assert.equal(source.stats.failed, 1);
});

test('a render with no image answers null rather than an empty box', async () => {
    const controller = fakeController();
    const source = createModelSource({ controller });
    const pending = source.render(7, POSE);
    controller.finish(0, {});
    assert.equal(await pending, null);
    assert.equal(source.stats.failed, 1);
});

test('with no controller at all, a model simply does not draw', () => {
    /* A preview opened without a worker is a preview without models, not a
     * preview that fails to open. */
    const source = createModelSource({});
    return source.render(7, POSE).then((result) => assert.equal(result, null));
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { await fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
