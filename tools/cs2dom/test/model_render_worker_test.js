import assert from 'node:assert/strict';

import { createModelRenderController } from '../src/model_render_controller.js';
import {
    MODEL_RENDER_WORKER_SCHEMA, createModelRenderWorkerEndpoint,
} from '../src/model_render_worker.js';

let cachedFrameBenchmark = { maximum: 0, average: 0 };

function response(bytes, { status = 200, headers = {} } = {}) {
    return {
        ok: status >= 200 && status < 300,
        status,
        headers: { get: (name) => headers[name.toLowerCase()] || null },
        arrayBuffer: async () => Uint8Array.from(bytes).buffer,
        text: async () => `HTTP ${status}`,
    };
}

function fakeRenderer() {
    const heap = new Uint8Array(2 * 1024 * 1024);
    let next = 32;
    const calls = [];
    return {
        calls,
        mod: { HEAPU8: heap },
        alloc(size) { const ptr = next; next += Math.max(1, size); return ptr; },
        release() {},
        setModel(ptr, size) { calls.push(['model', ptr, size]); return 7; },
        setModelHd(ptr, size) { calls.push(['model-hd', ptr, size]); return 7; },
        clearModelHd() { calls.push(['clear-hd']); },
        setTextures(ptr, size) { calls.push(['textures', ptr, size]); return 1; },
        setAnim(ptr, size) { calls.push(['animation', ptr, size]); return 2; },
        clearAnim() { calls.push(['clear-animation']); },
        frameCount() { calls.push(['frame-count']); return 2; },
        frameDelay(frame) { calls.push(['frame-delay', frame]); return frame + 1; },
        renderWidget(...args) {
            calls.push(['render', ...args]);
            const [width, height] = args;
            const ptr = 32768;
            heap.fill(0x7f, ptr, ptr + width * height * 4);
            return ptr;
        },
    };
}

{
    const outbound = [];
    const bitmap = { kind: 'ImageBitmap' };
    const renderer = fakeRenderer();
    let bitmapPixels = null;
    const endpoint = createModelRenderWorkerEndpoint({
        send: (message, transfer) => outbound.push({ message, transfer }),
        createRenderer: async () => renderer,
        fetchImpl: async () => response([1, 2, 3, 4]),
        makeBitmap: (pixels) => { bitmapPixels = pixels; return bitmap; },
        canMakeBitmap: () => true,
    });
    await endpoint.receive({
        schema: MODEL_RENDER_WORKER_SCHEMA,
        type: 'render', requestId: 77, owner: 'bitmap', token: 1,
        modelUrl: '/model/content/bitmap.model', startedAt: Date.now(),
        width: 2, height: 2, widgetWidth: 2, widgetHeight: 2,
        zoom: 100, preferBitmap: true,
    });
    assert.equal(outbound[0].message.bitmap, bitmap);
    assert.deepEqual(outbound[0].transfer, [bitmap]);
    assert.equal(outbound[0].message.rgba, undefined,
        'bitmap fast path also transferred a full RGBA frame');
    assert.equal(bitmapPixels.buffer, renderer.mod.HEAPU8.buffer,
        'bitmap fast path copied the WASM framebuffer');
    assert.equal(bitmapPixels.byteOffset, 32768,
        'bitmap fast path did not expose the rendered framebuffer directly');
}

{
    const outbound = [];
    const renderer = fakeRenderer();
    const endpoint = createModelRenderWorkerEndpoint({
        send: (message) => outbound.push(message),
        createRenderer: async () => renderer,
        fetchImpl: async () => response([1, 2, 3, 4]),
        canMakeBitmap: () => false,
    });
    await endpoint.receive({
        schema: MODEL_RENDER_WORKER_SCHEMA,
        type: 'render', requestId: 78, owner: 'rgba-cap', token: 1,
        modelUrl: '/model/content/fallback.model', startedAt: Date.now(),
        width: 1024, height: 768, widgetX: 256, widgetY: 128,
        widgetWidth: 512, widgetHeight: 384, zoom: 800,
        xOffset: 64, yOffset: -32, preferBitmap: true,
        fallbackMaxDimension: 512,
    });
    assert.equal(outbound[0].width, 512);
    assert.equal(outbound[0].height, 384);
    assert.equal(outbound[0].rgba.byteLength, 512 * 384 * 4,
        'RGBA fallback crossed the bounded transfer size');
    assert.notEqual(outbound[0].rgba, renderer.mod.HEAPU8.buffer,
        'RGBA fallback transferred and detached the live WASM heap');
    const render = renderer.calls.find((call) => call[0] === 'render');
    assert.deepEqual(render.slice(1, 8), [512, 384, 128, 64, 256, 192, 400],
        'fallback downscale changed the model/widget framing ratio');
}

{
    const outbound = [];
    const transferLists = [];
    const renderer = fakeRenderer();
    const endpoint = createModelRenderWorkerEndpoint({
        send(message, transfer) { outbound.push(message); transferLists.push(transfer); },
        createRenderer: async () => renderer,
        fetchImpl: async (url) => url.includes('/seq/')
            ? response([9, 8, 7])
            : response([1, 2, 3, 4], { headers: { 'x-texture-ids': '4,5' } }),
        clock: (() => { let now = 100; return () => ++now; })(),
        animationClock: () => 1045,
    });
    await endpoint.receive({
        schema: MODEL_RENDER_WORKER_SCHEMA,
        type: 'render', requestId: 1, owner: 'model:1', token: 4,
        modelUrl: '/model/content/55.model', animationUrl: '/model/seq/2.anim',
        startedAt: 1000, width: 4, height: 3,
        widgetX: 1, widgetY: 2, widgetWidth: 20, widgetHeight: 30,
        zoom: 700, xAngle: 10, yAngle: 20, zAngle: 30,
        xOffset: 4, yOffset: -5, orthographic: true, fixedZoom: true, composed: true,
    });
    assert.equal(outbound.length, 1);
    assert.equal(outbound[0].type, 'rendered');
    assert.equal(outbound[0].rgba.byteLength, 4 * 3 * 4);
    assert.deepEqual(transferLists[0], [outbound[0].rgba],
        'RGBA storage was cloned instead of transferred');
    const render = renderer.calls.find((call) => call[0] === 'render');
    assert.deepEqual(render.slice(1, 16), [
        4, 3, 1, 2, 20, 30, 700, 10, 20, 30, 4, -5, true, true, true,
    ], 'widget transform parameters changed at the worker boundary');
    assert.equal(render.at(-1), 1,
        'cross-context animation clocks left an elapsed animation on frame zero');
    assert(renderer.calls.some((call) => call[0] === 'animation'));
}

{
    const outbound = [];
    const renderer = fakeRenderer();
    let modelFetches = 0;
    let animationFetches = 0;
    const endpoint = createModelRenderWorkerEndpoint({
        send: (message) => outbound.push(message),
        createRenderer: async () => renderer,
        fetchImpl: async (url) => {
            if( url.includes('/seq/') ) {
                animationFetches++;
                return response([7, 8, 9]);
            }
            if( url.includes('/textures.bin') ) return response([5, 6]);
            modelFetches++;
            return response([1, 2, 3, 4], {
                headers: { 'x-texture-ids': 'cache-test' },
            });
        },
        makeBitmap: () => ({ kind: 'ImageBitmap' }),
        canMakeBitmap: () => true,
        animationClock: () => 2060,
    });
    const base = {
        schema: MODEL_RENDER_WORKER_SCHEMA,
        type: 'render', owner: 'cache:model', token: 1,
        modelUrl: '/model/content/cache-repeat.model',
        animationUrl: '/model/seq/cache-repeat.anim',
        startedAt: 2000, width: 4, height: 4,
        widgetWidth: 4, widgetHeight: 4, zoom: 100, preferBitmap: true,
    };
    await endpoint.receive({ ...base, requestId: 201 });
    const repeatFrames = 128;
    for( let index = 1; index <= repeatFrames; index++ )
        await endpoint.receive({
            ...base, requestId: 201 + index, yAngle: index & 2047,
        });

    const count = (name) => renderer.calls.filter((call) => call[0] === name).length;
    assert.equal(modelFetches, 1, 'repeat frames refetched the model');
    assert.equal(animationFetches, 1, 'repeat frames refetched the animation');
    assert.equal(count('model'), 1, 'repeat frames decoded/uploaded the model again');
    assert.equal(count('textures'), 1, 'repeat frames uploaded textures again');
    assert.equal(count('animation'), 1, 'repeat frames decoded/uploaded animation again');
    assert.equal(count('clear-animation'), 1, 'repeat frames reset animation state');
    assert.equal(count('frame-count'), 1, 'repeat frames rescanned animation metadata');
    assert.equal(count('frame-delay'), 2, 'repeat frames rebuilt animation delays');
    assert.equal(count('render'), repeatFrames + 1,
        'cached preparation suppressed an actual frame render');

    /* Fetch objects may be process-global, but installed renderer identities
     * cannot be. A fresh endpoint must prepare its own WASM instance. */
    const secondRenderer = fakeRenderer();
    const secondEndpoint = createModelRenderWorkerEndpoint({
        send() {},
        createRenderer: async () => secondRenderer,
        fetchImpl: async () => { throw new Error('global fetch cache missed'); },
        makeBitmap: () => ({ kind: 'ImageBitmap' }),
        canMakeBitmap: () => true,
    });
    await secondEndpoint.receive({ ...base, requestId: 401, owner: 'cache:fresh' });
    assert.equal(secondRenderer.calls.filter((call) => call[0] === 'model').length, 1,
        'shared byte identity incorrectly skipped preparation in a new renderer');
    assert.equal(secondRenderer.calls.filter((call) => call[0] === 'animation').length, 1,
        'shared animation identity incorrectly skipped preparation in a new renderer');

    const repeatRenderMs = outbound.slice(1).map((message) => message.renderMs);
    cachedFrameBenchmark = {
        maximum: Math.max(...repeatRenderMs),
        average: repeatRenderMs.reduce((sum, elapsed) => sum + elapsed, 0) /
            repeatRenderMs.length,
    };
    assert(repeatRenderMs.every((elapsed) => elapsed < 10),
        `cached animation frame exceeded 10ms: ${Math.max(...repeatRenderMs).toFixed(3)}ms`);

    const switched = {
        ...base,
        modelUrl: '/model/content/cache-switch.model',
    };
    await endpoint.receive({ ...switched, requestId: 350 });
    await endpoint.receive({ ...switched, requestId: 351, yAngle: 64 });
    assert.equal(modelFetches, 2, 'a distinct model identity reused stale fetched bytes');
    assert.equal(animationFetches, 1, 'a model switch refetched an unchanged animation');
    assert.equal(count('model'), 2, 'a model switch reused the previous decoder state');
    assert.equal(count('textures'), 2,
        'a model switch failed to reinstall model-relative textures');
    assert.equal(count('animation'), 2,
        'a model switch failed to reinstall model-relative animation state');
    assert.equal(count('frame-count'), 2,
        'a model switch retained timing from the replaced decoder state');
    assert.equal(count('frame-delay'), 4,
        'a model switch retained delay metadata from the replaced decoder state');

    await endpoint.receive({
        ...switched, requestId: 352, animationUrl: null,
    });
    await endpoint.receive({
        ...switched, requestId: 353, animationUrl: null, yAngle: 128,
    });
    assert.equal(count('model'), 2,
        'animation removal unnecessarily decoded the unchanged model');
    assert.equal(count('textures'), 2,
        'animation removal unnecessarily uploaded unchanged textures');
    assert.equal(count('animation'), 2,
        'animation removal installed nonexistent animation bytes');
    assert.equal(count('clear-animation'), 3,
        'animation-null transition did not clear exactly once');
}

{
    class ManualWorker {
        constructor() { this.sent = []; this.terminated = false; }
        postMessage(message) { this.sent.push(message); }
        terminate() { this.terminated = true; }
        emit(message) { this.onmessage?.({ data: message }); }
    }
    const worker = new ManualWorker();
    let now = 0;
    const controller = createModelRenderController({
        workerFactory: () => worker,
        clock: () => (now += 0.2),
    });
    const base = {
        owner: 'interface:figure', token: 1,
        modelUrl: '/model/content/55.model', startedAt: 1,
        width: 2, height: 2, widgetWidth: 2, widgetHeight: 2, zoom: 100,
    };
    const obsolete = controller.render(base);
    const latest = controller.render({ ...base, token: 2, yAngle: 128 });
    assert(obsolete.enqueueMs < 10 && latest.enqueueMs < 10);
    assert.equal((await obsolete.completion).stale, true,
        'superseded model work remained live');

    let staleBitmapClosed = false;
    worker.emit({
        schema: MODEL_RENDER_WORKER_SCHEMA, type: 'rendered',
        requestId: obsolete.requestId, owner: base.owner, token: 1,
        width: 2, height: 2, wait: 0,
        bitmap: { close() { staleBitmapClosed = true; } },
    });
    let latestSettled = false;
    latest.completion.then(() => { latestSettled = true; });
    await Promise.resolve();
    assert.equal(latestSettled, false, 'a stale RGBA result resolved the current canvas paint');
    assert.equal(staleBitmapClosed, true, 'a stale transferred bitmap leaked');

    const latestMessage = worker.sent.find((message) => message.requestId === latest.requestId);
    worker.emit({
        schema: MODEL_RENDER_WORKER_SCHEMA, type: 'rendered',
        requestId: latest.requestId, owner: base.owner, token: latestMessage.token,
        width: 2, height: 2, wait: 20, renderMs: 3, rgba: new ArrayBuffer(16),
    });
    const painted = await latest.completion;
    assert.equal(painted.token, 2);
    assert.equal(painted.rgba.byteLength, 16);
    controller.dispose();
    assert.equal(worker.terminated, true);
}

console.log('model render worker tests passed ' +
    `(128 cached frames: avg ${cachedFrameBenchmark.average.toFixed(3)}ms, ` +
    `max ${cachedFrameBenchmark.maximum.toFixed(3)}ms; zero-copy bitmap verified)`);
