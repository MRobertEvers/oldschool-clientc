import assert from 'node:assert/strict';

import {
    RUNTIME_WORKER_SCHEMA, applyStagePatch, chunkStagePatch, diffStage,
    paintableStageBox, projectStage, stageBoxesEqual,
} from '../src/runtime_worker_protocol.js';
import { createRuntimeWorkerEndpoint } from '../src/runtime_worker.js';
import { createWorkerRuntimeController } from '../src/worker_runtime_controller.js';
import { createHostRuntime } from '../src/host_runtime.js';
import { OSRS239_LOGIN_STATE } from '../src/client_state.js';

let measuredEnqueueMax = 0;
let measuredStageTaskMax = 0;
let measuredUpdateStageMax = 0;
let measuredNonLayoutStageMax = 0;
let measuredLayoutMax = 0;
let measuredUpdateStageTasks = 0;
let measuredControllerReceiveMax = 0;
let measuredSentinelMax = 0;

async function drainScheduled(scheduled, durations = null) {
    while( scheduled.length ) {
        const task = scheduled.shift();
        const started = performance.now();
        task();
        if( durations ) durations.push(performance.now() - started);
        await Promise.resolve();
    }
}

function stageWireOperations(chunk) {
    if( Array.isArray(chunk?.operations) ) return chunk.operations;
    return [
        ...(chunk?.remove || []).map((key) => [0, key]),
        ...(chunk?.upsert || []).map((entry) => [1, entry]),
        ...(chunk?.order || []).map((key) => [2, key]),
    ];
}

function box(index, patch = {}) {
    return {
        ref: { key: `k${index}`, componentId: (12 << 16) | index, subId: -1, fileId: index },
        fileId: index, name: `box_${index}`, type: 0, emitted: true,
        effectiveHidden: false, culled: false,
        x: index, y: 0, w: 1, h: 1,
        clip: { left: 0, top: 0, right: 512, bottom: 334 },
        surface: { x: 0, y: 0, w: 512, h: 334 },
        props: {}, presentation: { kind: 'layer' }, dynamic: [], ops: [], events: [], hooks: [],
        ...patch,
    };
}

{
    const layout = [
        box(0),
        box(1, { type: 5, props: { sprite: -1 } }),
        box(2, { type: 5, props: { sprite: 42 } }),
        box(3, { effectiveHidden: true }),
    ];
    assert.equal(paintableStageBox(layout[0]), true);
    assert.equal(paintableStageBox(layout[1]), false);
    assert.equal(stageBoxesEqual(box(9), box(9)), true);
    assert.equal(stageBoxesEqual(box(9), box(9, { type: 4 })), false);
    assert.equal(stageBoxesEqual(box(9), box(9, { props: { nested: { value: 1 } } })), false);
    assert.equal(stageBoxesEqual(box(9), box(9, { hooks: ['onClick'] })), false,
        'stage equality dropped live hook metadata');
    assert.equal(stageBoxesEqual(
        box(9, { props: { first: 1, second: [2, 3] } }),
        box(9, { props: { second: [2, 3], first: 1 } })), true,
    'stage equality depends on record insertion order');
    const first = projectStage(layout, { width: 512, height: 334 }, 1);
    assert.deepEqual(first.entries.map((entry) => entry.key), ['k0', 'k2']);
    const second = projectStage([
        box(0, { x: 9 }), box(2, { type: 5, props: { sprite: 42 } }), box(4),
    ], { width: 512, height: 334 }, 2);
    const patch = diffStage(first, second);
    assert.deepEqual(patch.upsert.map((entry) => entry.key), ['k0', 'k4']);
    assert.deepEqual(patch.remove, []);
    const applied = applyStagePatch(
        applyStagePatch(null, diffStage(null, first, { reset: true })), patch);
    assert.equal(applied.version, 2);
    assert.deepEqual(applied.boxes.map((entry) => entry.name), ['box_0', 'box_2', 'box_4']);

    const reordered = projectStage([
        box(4), box(2, { type: 5, props: { sprite: 42 } }), box(0, { x: 9 }),
    ], { width: 512, height: 334 }, 3);
    const reorderPatch = diffStage(second, reordered);
    assert.equal(reorderPatch.upsert.length, 0);
    assert.equal(reorderPatch.remove.length, 0);
    assert.equal(reorderPatch.orderChanged, true);
    assert.deepEqual(applyStagePatch(applied, reorderPatch).boxes.map((entry) => entry.name),
        ['box_4', 'box_2', 'box_0']);
}

{
    /* A scalar Host TreeDelta must not invoke the full layout projector. The
     * same existing stage entry is replaced and published atomically through
     * the normal worker chunk protocol. */
    const scheduled = [];
    const outbound = [];
    let version = 0;
    let commitRevision = 0;
    let layoutCalls = 0;
    let pendingDelta = null;
    let boxes = [
        box(0, { type: 4, props: { text: 'before' }, hooks: ['onClick'] }),
        box(1),
    ];
    const emptyDirty = () => ({
        paint: [], geometry: [], visibility: [], topology: [], order: [],
        interaction: [], viewport: [],
    });
    const host = {
        viewport: { width: 512, height: 334 },
        get version() { return version; },
        get commitRevision() { return commitRevision; },
        renderKey: (ref) => ref?.key || null,
        layout() { layoutCalls++; return boxes; },
        mount() {
            version++;
            const dirty = emptyDirty();
            pendingDelta = Object.freeze({
                schema: 'cs2dom-tree-delta/1', baseRevision: commitRevision,
                revision: ++commitRevision, mutationVersion: version,
                upsert: [], remove: [], order: [], reorderParents: [],
                dirty, dirtyGeometryRoots: [], projection: 'full',
                fallbackReason: 'mount',
            });
            return { interaction: { menuOpen: false, menuEntries: [] } };
        },
        dispatch(input) {
            if( input.type === 'pointer_down' ) {
                boxes = [
                    box(0, { type: 4, props: { text: 'after' }, hooks: ['onClick'] }),
                    boxes[1],
                ];
            } else if( input.type === 'wheel' ) {
                boxes = [box(0, { type: 5, props: { sprite: -1 } }), boxes[1]];
            }
            if( input.type === 'pointer_down' || input.type === 'wheel' ) {
                version++;
                const dirty = emptyDirty();
                dirty.paint = ['k0'];
                pendingDelta = Object.freeze({
                    schema: 'cs2dom-tree-delta/1', baseRevision: commitRevision,
                    revision: ++commitRevision, mutationVersion: version,
                    upsert: [], remove: [], order: [], reorderParents: [],
                    dirty, dirtyGeometryRoots: [], projection: 'dirty',
                });
            }
            return { version, interaction: { menuOpen: false, menuEntries: [] } };
        },
        consumeTreeDelta() {
            const delta = pendingDelta;
            pendingDelta = null;
            return delta;
        },
        projectRenderKey(key) {
            return boxes.find((entry) => entry.ref.key === key) || null;
        },
        snapshot: () => ({ marker: 'dirty-host' }),
    };
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(structuredClone(message)),
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost: () => host,
        async createWasm() { return { destroy() {}, invokeIntent() { return 0; } }; },
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 89,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await drainScheduled(scheduled);
    assert.equal(layoutCalls, 1, 'initial reset did not use the full projector exactly once');
    assert.equal(pendingDelta, null, 'reset did not consume its committed mount delta');
    outbound.length = 0;

    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 89, sequence: 1,
        input: { type: 'pointer_down', button: 0, x: 1, y: 1 },
    });
    await drainScheduled(scheduled);
    assert.equal(layoutCalls, 1, 'scalar TreeDelta rebuilt the full Host layout');
    const operations = outbound.filter((message) => message.type === 'stage')
        .flatMap((message) => stageWireOperations(message.chunk));
    assert.deepEqual(operations.map((operation) => [operation[0], operation[1]?.key]),
        [[1, 'k0']]);
    const timing = outbound.find((message) =>
        message.type === 'timing' && message.sequence === 1)?.timing;
    assert.equal(timing?.stageLayoutMs, 0,
        'dirty stage timing charged an unexpected full-layout pass');

    outbound.length = 0;
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 89, sequence: 2,
        input: { type: 'wheel', wheel: 1, x: 1, y: 1 },
    });
    await drainScheduled(scheduled);
    assert.equal(layoutCalls, 2,
        'paintability change did not fall back to the full ordering oracle');
    const fallbackOperations = outbound.filter((message) => message.type === 'stage')
        .flatMap((message) => stageWireOperations(message.chunk));
    assert(fallbackOperations.some((operation) => operation[0] === 0 && operation[1] === 'k0'));
    assert(fallbackOperations.some((operation) => operation[0] === 2 && operation[1] === 'k1'));
    endpoint.dispose();
}

{
    /* consumeTreeDelta() is destructive, while dirty projection is deferred.
     * A one-shot projector failure must retain that committed revision, publish
     * a complete reset, and acknowledge it so the following delta can return
     * to the incremental path instead of leaving React permanently stale. */
    const scheduled = [];
    const outbound = [];
    let version = 0;
    let commitRevision = 0;
    let layoutCalls = 0;
    let projectorCalls = 0;
    let dispatches = 0;
    let failProjector = true;
    let pendingDelta = null;
    let boxes = [box(0, { type: 4, props: { text: 'before' } })];
    const dirtyRecord = () => ({
        paint: ['k0'], geometry: [], visibility: [], topology: [], order: [],
        interaction: [], viewport: [],
    });
    const host = {
        viewport: { width: 512, height: 334 },
        get version() { return version; },
        get commitRevision() { return commitRevision; },
        renderKey: (ref) => ref?.key || null,
        layout() { layoutCalls++; return boxes; },
        mount() {
            version++;
            pendingDelta = {
                schema: 'cs2dom-tree-delta/1', baseRevision: commitRevision,
                revision: ++commitRevision, mutationVersion: version,
                dirty: { ...dirtyRecord(), paint: [] }, projection: 'full',
            };
            return { interaction: { menuOpen: false, menuEntries: [] } };
        },
        dispatch() {
            dispatches++;
            boxes = [box(0, { type: 4, props: { text: `after-${dispatches}` } })];
            version++;
            pendingDelta = {
                schema: 'cs2dom-tree-delta/1', baseRevision: commitRevision,
                revision: ++commitRevision, mutationVersion: version,
                dirty: dirtyRecord(), projection: 'dirty',
            };
            return { version, interaction: { menuOpen: false, menuEntries: [] } };
        },
        consumeTreeDelta() {
            const result = pendingDelta;
            pendingDelta = null;
            return result;
        },
        projectRenderKey(key) {
            projectorCalls++;
            if( failProjector ) {
                failProjector = false;
                throw new Error('one-shot dirty projector failure');
            }
            return boxes.find((entry) => entry.ref.key === key) || null;
        },
        snapshot: () => ({ version }),
    };
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(structuredClone(message)),
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost: () => host,
        async createWasm() { return { destroy() {}, invokeIntent() { return 0; } }; },
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 90,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await drainScheduled(scheduled);
    assert.equal(layoutCalls, 1);
    outbound.length = 0;

    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 90, sequence: 1,
        input: { type: 'pointer_down', button: 0, x: 1, y: 1 },
    });
    await drainScheduled(scheduled);
    assert.equal(projectorCalls, 1);
    assert.equal(layoutCalls, 2, 'failed dirty projection did not retry through full layout');
    assert.equal(outbound.filter((message) =>
        message.type === 'timing' && message.sequence === 1).length, 2,
        'dirty failure exceeded its single bounded full-rebuild retry');
    assert.equal(pendingDelta, null, 'failed projection unexpectedly re-consumed a newer delta');
    const recovered = outbound.filter((message) => message.type === 'stage');
    assert(recovered.length > 0 && recovered.every((message) => message.chunk.reset),
        'one-shot recovery did not publish an atomic reset transaction');
    assert(recovered.flatMap((message) => stageWireOperations(message.chunk))
        .some((operation) => operation[0] === 1 &&
            operation[1]?.box?.props?.text === 'after-1'),
        'full retry published the stale pre-dispatch stage');

    outbound.length = 0;
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 90, sequence: 2,
        input: { type: 'pointer_down', button: 0, x: 1, y: 1 },
    });
    await drainScheduled(scheduled);
    assert.equal(projectorCalls, 2,
        'successful retry did not acknowledge its consumed TreeDelta revision');
    assert.equal(layoutCalls, 2,
        'following scalar delta missed the acknowledged revision and rebuilt again');
    const nextStage = outbound.filter((message) => message.type === 'stage');
    assert(nextStage.length > 0 && nextStage.every((message) => !message.chunk.reset));
    endpoint.dispose();
}

function mockRuntime() {
    const state = {
        version: 0,
        viewport: { width: 512, height: 334 },
        boxes: Array.from({ length: 150 }, (_, index) => box(index, index >= 80
            ? { type: 5, props: { sprite: -1 } } : {})),
    };
    let options;
    const inputs = [];
    const host = {
        get version() { return state.version; },
        viewport: state.viewport,
        layout: () => state.boxes,
        renderKey: (ref) => `render:${ref.fileId}`,
        mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
        dispatch(input) {
            inputs.push({ ...input });
            if( input.type === 'pointer_down' ) {
                options.onService({ kind: 'if_button', componentId: 12 << 16 });
                state.version++;
                state.boxes[0] = box(0, { x: state.version });
            }
            return {
                epoch: state.version, version: state.version,
                interaction: { menuOpen: input.button === 2, menuEntries: [] },
                ...(input.button === 2 ? { menu: [{ text: 'Use', opIndex: 1 }] } : {}),
            };
        },
        snapshot: () => ({ version: state.version, marker: 'explicit-only' }),
    };
    return {
        host,
        inputs,
        options: () => options,
        createHost(ir, supplied) { options = supplied; return host; },
        async createWasm() { return { destroy() {}, invokeIntent() { return 0; } }; },
    };
}

{
    const outbound = [];
    const scheduled = [];
    const mock = mockRuntime();
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(message),
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost: mock.createHost,
        createWasm: mock.createWasm,
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 7,
        config: {
            ir: { components: [] },
            program: { revision: 'osrs239', scripts: [] },
            state: { 'varc:1339': 777 },
        },
    });
    await drainScheduled(scheduled);
    assert.deepEqual(mock.options().state, {
        ...OSRS239_LOGIN_STATE,
        'varc:1339': 777,
    }, 'live worker did not merge revision globals beneath explicit Host state');
    const initialStage = outbound.filter((message) => message.type === 'stage');
    assert(initialStage.length > 1, 'large stage reset crossed the worker boundary in one task');
    assert(initialStage.every((message) => !('operations' in message.chunk)),
        'worker retained tuple-heavy legacy stage operations');
    assert(initialStage.every((message) => stageWireOperations(message.chunk).length <= 32));
    assert.equal(initialStage.flatMap((message) => stageWireOperations(message.chunk))
        .filter((operation) => operation[0] === 1).length, 80,
        'blank bank inventory cells leaked into the main-thread stage payload');
    assert(initialStage.flatMap((message) => stageWireOperations(message.chunk))
        .some((operation) => operation[0] === 1 && operation[1].key === 'render:0'),
        'worker projection ignored the HOST logical render identity');
    assert.equal(outbound.at(-1).type, 'ready');

    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 7, sequence: 1,
        input: { type: 'pointer_down', button: 2, x: 2, y: 2 },
    });
    await drainScheduled(scheduled);
    const resultIndex = outbound.findIndex((message) => message.type === 'result');
    const readyIndex = outbound.findIndex((message) => message.type === 'ready');
    const stageIndex = outbound.findIndex((message, index) =>
        index > readyIndex && message.type === 'stage');
    assert(resultIndex >= 0 && resultIndex < stageIndex,
        'semantic menu result must precede layout projection');
    assert.equal(outbound[resultIndex].services[0].kind, 'if_button');
    assert.equal(outbound.slice(stageIndex).filter((message) => message.type === 'stage')
        .flatMap((message) => stageWireOperations(message.chunk))
        .filter((operation) => operation[0] === 1).length, 1);
    assert.equal(outbound.slice(stageIndex).filter((message) => message.type === 'stage')
        .flatMap((message) => stageWireOperations(message.chunk))
        .filter((operation) => operation[0] === 2).length, 0,
        'unchanged 80-widget order was retransmitted for a one-widget paint update');

    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'tree', session: 7, requestId: 9,
    });
    while( scheduled.length ) scheduled.shift()();
    const tree = outbound.filter((message) => message.type === 'tree');
    assert.deepEqual(tree.slice(1).map((message) => message.boxes.length), [64, 64, 22]);
    assert.equal(tree.at(-1).done, true);
}

{
    /* The controller compresses identical key events into runs of up to 64.
     * Expanding that run in one callback used to multiply otherwise-fast C/JS
     * dispatches into a long worker task. A deterministic 3 ms HOST models the
     * expensive bank path and proves the worker yields below 10 ms without
     * exposing partial state to an explicit snapshot request. */
    const outbound = [];
    const scheduled = [];
    const taskDurations = [];
    let now = 0;
    let version = 0;
    let dispatches = 0;
    let supplied;
    const host = {
        viewport: { width: 512, height: 334 },
        get version() { return version; },
        layout: () => [box(0, { x: version })],
        mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
        dispatch(input) {
            assert.equal(input.repeatCount, undefined);
            now += 3;
            dispatches++;
            version++;
            supplied.onService({ kind: 'repeat', index: dispatches });
            return { version, interaction: { menuOpen: false, menuEntries: [] } };
        },
        snapshot: () => ({ version, dispatches }),
    };
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(message),
        clock: () => now,
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost(ir, options) { supplied = options; return host; },
        async createWasm() { return { destroy() {}, invokeIntent() { return 0; } }; },
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 31,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await drainScheduled(scheduled);
    outbound.length = 0;
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 31, sequence: 77,
        input: { type: 'key', keyTyped: 48, keyPressed: 65, repeatCount: 5 },
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'snapshot', session: 31, requestId: 8,
    });
    assert.equal(dispatches, 1);
    assert.equal(outbound.some((message) => message.type === 'result'), false);
    assert.equal(outbound.some((message) => message.type === 'snapshot'), false,
        'snapshot observed a partially expanded key transaction');
    while( scheduled.length ) {
        const started = now;
        scheduled.shift()();
        taskDurations.push(now - started);
    }
    assert.equal(dispatches, 5);
    assert(Math.max(...taskDurations) < 10,
        `repeat expansion monopolized a ${Math.max(...taskDurations)}ms worker task`);
    const resultAt = outbound.findIndex((message) => message.type === 'result');
    const stageAt = outbound.findIndex((message) => message.type === 'stage');
    const snapshotAt = outbound.findIndex((message) => message.type === 'snapshot');
    assert(resultAt >= 0 && stageAt > resultAt && snapshotAt > stageAt);
    assert.equal(outbound[resultAt].services.length, 5);
    assert.deepEqual(outbound[resultAt].timing, { maxDispatchTaskMs: 3, taskCount: 5 });
    assert.deepEqual(outbound[snapshotAt].snapshot, { version: 5, dispatches: 5 });
    endpoint.dispose();
}


{
    const outbound = [];
    const pendingWasm = [];
    const destroyed = [];
    const scheduled = [];
    const mock = mockRuntime();
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(message),
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost: mock.createHost,
        createWasm: () => new Promise((resolve) => pendingWasm.push(resolve)),
    });
    const first = endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 21,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await Promise.resolve();
    const second = endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 22,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await Promise.resolve();
    assert.equal(pendingWasm.length, 2);
    pendingWasm[0]({ destroy() { destroyed.push(21); }, invokeIntent() { return 0; } });
    await first;
    assert.deepEqual(destroyed, [21], 'superseded async init leaked its WASM instance');
    pendingWasm[1]({ destroy() { destroyed.push(22); }, invokeIntent() { return 0; } });
    await second;
    await drainScheduled(scheduled);
    assert.equal(outbound.at(-1).type, 'ready');
    endpoint.dispose();
    assert.deepEqual(destroyed, [21, 22]);
}

{
    const worker = { postMessage() {}, terminate() {} };
    let published = 0;
    const controller = createWorkerRuntimeController({
        workerFactory: () => worker,
        onStagePatch: () => published++,
    });
    let storePublished = 0;
    const unsubscribeStage = controller.subscribeStage(() => storePublished++);
    let rootOrderPublished = 0;
    let firstNodePublished = 0;
    let unaffectedNodePublished = 0;
    const unsubscribeOrder = controller.subscribeOrder(null, () => rootOrderPublished++);
    const unsubscribeNode = controller.subscribeNode('k0', () => firstNodePublished++);
    const unsubscribeUnaffected = controller.subscribeNode(
        'k100', () => unaffectedNodePublished++);
    const emptyStageSnapshot = controller.getStageSnapshot();
    const emptyRoots = controller.getRoots();
    const projection = projectStage(
        Array.from({ length: 4096 }, (_, index) => box(index)),
        { width: 765, height: 503 }, 9);
    const chunks = chunkStagePatch(diffStage(null, projection, { reset: true }), 44);
    assert(chunks.length > 100);
    assert(chunks.every((chunk) => chunk.operations.length <= 64));
    for( let index = 0; index < chunks.length; index++ ) {
        controller._receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'stage', session: 0, chunk: chunks[index],
        });
        if( index < chunks.length - 1 ) assert.equal(published, 0,
            'controller published a partially materialized stage');
        if( index < chunks.length - 1 ) {
            assert.strictEqual(controller.getStageSnapshot(), emptyStageSnapshot,
                'external store observed a partially materialized first stage');
            assert.equal(storePublished, 0);
            assert.strictEqual(controller.getRoots(), emptyRoots);
            assert.equal(controller.getNode('k0'), null);
        }
    }
    assert.equal(published, 1);
    assert.equal(storePublished, 1);
    assert.strictEqual(controller.getStageSnapshot().render, controller.currentRender);
    assert.equal(controller.getRoots().length, 4096);
    assert.equal(controller.getNode('k0').geometry.x, 0);
    assert.equal(rootOrderPublished, 1);
    assert.equal(firstNodePublished, 1);
    assert.equal(unaffectedNodePublished, 1);
    const unaffectedNode = controller.getNode('k100');
    const committedRoots = controller.getRoots();
    assert.equal(controller.currentRender.boxes.length, 4096);
    assert.equal(controller.currentRender.boxes[4095].name, 'box_4095');

    const committedMap = controller.stageMap;
    const update = projectStage(
        Array.from({ length: 4096 }, (_, index) =>
            box(index, index < 65 ? { x: 10_000 + index } : {})),
        { width: 765, height: 503 }, 10);
    const updateChunks = chunkStagePatch(diffStage(projection, update), 45, 32);
    assert(updateChunks.length > 1);
    for( let index = 0; index < updateChunks.length; index++ ) {
        controller._receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'stage', session: 0,
            chunk: updateChunks[index],
        });
        if( index < updateChunks.length - 1 ) {
            assert.strictEqual(controller.stageMap, committedMap,
                'an unfinished stage transaction replaced the published map');
            assert.equal(controller.stageMap.get('k0').box.x, 0,
                'an unfinished stage transaction mutated the published map');
            assert.equal(controller.currentRender.boxes[0].x, 0,
                'an unfinished stage transaction mutated the published render');
            assert.equal(published, 1,
                'controller published an unfinished update transaction');
            assert.equal(storePublished, 1,
                'external store published an unfinished update transaction');
            assert.strictEqual(controller.getRoots(), committedRoots,
                'unfinished update replaced the React root-order snapshot');
            assert.equal(controller.getNode('k0').geometry.x, 0,
                'unfinished update leaked into the React node snapshot');
        }
    }
    assert.notStrictEqual(controller.stageMap, committedMap);
    assert.equal(controller.currentRender.boxes[0].x, 10_000);
    assert.equal(published, 2);
    assert.equal(storePublished, 2);
    assert.equal(controller.getStageSnapshot().patch.transaction, 45);
    assert.strictEqual(controller.getRoots(), committedRoots,
        'paint-only update invalidated stable React root order');
    assert.equal(controller.getNode('k0').geometry.x, 10_000);
    assert.equal(rootOrderPublished, 1);
    assert.equal(firstNodePublished, 2);
    assert.equal(unaffectedNodePublished, 1,
        'paint update notified an unrelated node selector');
    assert.strictEqual(controller.getNode('k100'), unaffectedNode,
        'paint update invalidated an unrelated immutable node snapshot');

    /* Starting a replacement session atomically empties every committed
     * selector. A node-only subscriber must not retain a stale snapshot just
     * because the root-order subscriber will eventually unmount its widget. */
    const reloading = controller.reload({ ir: { components: [] }, program: { scripts: [] } });
    reloading.catch(() => {});
    assert.equal(controller.getStageSnapshot().render, null);
    assert.deepEqual(controller.getRoots(), []);
    assert.equal(controller.getNode('k0'), null);
    assert.equal(storePublished, 3);
    assert.equal(rootOrderPublished, 2);
    assert.equal(firstNodePublished, 3,
        'session reset did not invalidate a committed node selector');
    assert.equal(unaffectedNodePublished, 2,
        'session reset did not invalidate every retired node selector');
    unsubscribeStage();
    unsubscribeOrder();
    unsubscribeNode();
    unsubscribeUnaffected();
    controller.dispose();
    await assert.rejects(reloading, /disposed/);
}

{
    /* A premature `done` marker must never expose the declared prefix of a
     * multi-chunk transaction. Reject the whole malformed transaction rather
     * than accepting a later suffix against an already published prefix. */
    let publications = 0;
    const controller = createWorkerRuntimeController({
        workerFactory: () => ({ postMessage() {}, terminate() {} }),
        onStagePatch: () => publications++,
    });
    const malformed = chunkStagePatch(diffStage(null,
        projectStage([box(0), box(1)], { width: 10, height: 10 }, 1),
        { reset: true }), 99, 1);
    assert.equal(malformed.length, 4);
    controller._receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'stage', session: 0,
        chunk: { ...malformed[0], done: true },
    });
    for( const chunk of malformed.slice(1) ) controller._receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'stage', session: 0, chunk,
    });
    assert.equal(publications, 0);
    assert.equal(controller.currentRender, null);
    assert.equal(controller.getNode('k0'), null);
    controller.dispose();
}

{
    /* Worker timings are distinct from the tiny main-thread enqueue timing.
     * Both expensive VM dispatches and expensive stage slices must reach the
     * same controller budget channel with an explicit phase. */
    const posted = [];
    const timings = [];
    const violations = [];
    const worker = {
        postMessage(message) { posted.push(message); },
        terminate() {},
    };
    const controller = createWorkerRuntimeController({
        workerFactory: () => worker,
        onTiming: (timing) => timings.push(timing),
        onBudgetViolation: (violation) => violations.push(violation),
    });
    const ready = controller.start({ ir: { components: [] }, program: { scripts: [] } });
    controller._receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'ready', session: controller.session,
        mode: 'wasm', warnings: [], services: [], interaction: {},
    });
    await ready;
    const ticket = controller.dispatch({ type: 'pointer_down', button: 1, x: 1, y: 1 });
    controller._receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'result', session: controller.session,
        sequence: ticket.sequence, result: { interaction: {} }, services: [], warnings: [],
        timing: { maxDispatchTaskMs: 12, taskCount: 1 },
    });
    await ticket.completion;
    controller._receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'timing', session: controller.session,
        sequence: ticket.sequence,
        timing: { maxStageTaskMs: 11, stageTaskCount: 3, stageWorkMs: 15, stageElapsedMs: 18 },
    });
    assert.deepEqual(timings.map((timing) => timing.phase), ['dispatch', 'stage']);
    assert.deepEqual(violations.map((violation) => violation.phase), ['dispatch', 'stage']);
    assert(violations.every((violation) => violation.source === 'runtime-worker'));
    controller.dispose();
}

{
    /* Exercise the real projection/signature/diff/chunk/send code with the
     * maximum supported 4,096 live widgets. The structuredClone in send()
     * charges each worker slice for the same serialization work postMessage
     * performs, while the controller receives every real wire chunk. */
    const components = Array.from({ length: 4096 }, (_, index) => ({
        fileId: index, name: `box_${index}`, kind: 'Layer', type: 0, layer: null,
        static: {
            x: index % 512, y: (index >> 9) % 334, width: 1, height: 1,
            xMode: 0, yMode: 0, widthMode: 0, heightMode: 0, hidden: false,
        },
        hooks: {}, events: {}, ops: [], dynamic: [], dependencies: [], rawFields: {},
    }));
    const runtimeHost = createHostRuntime({ interfaceId: 991, components }, {
        viewport: { width: 512, height: 334 }, recordChanges: false,
    });
    const inputs = [];
    const outbound = [];
    const scheduled = [];
    const workerTaskDurations = [];
    const controllerTaskDurations = [];
    const host = {
        get viewport() { return runtimeHost.viewport; },
        get version() { return runtimeHost.version; },
        layout: () => runtimeHost.layout(),
        mount: () => runtimeHost.mount(),
        dispatch(input) {
            inputs.push({ ...input });
            if( input.type === 'pointer_down' )
                runtimeHost.setViewport({ width: 513, height: 334 });
            return {
                version: runtimeHost.version,
                interaction: { menuOpen: false, menuEntries: [] },
            };
        },
        snapshot: () => runtimeHost.snapshot(),
    };
    const endpoint = createRuntimeWorkerEndpoint({
        send: (message) => outbound.push(structuredClone(message)),
        schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
        cancel: () => {},
        createHost: () => host,
        async createWasm() { return { destroy() {}, invokeIntent() { return 0; } }; },
    });
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 0,
        config: { ir: { components: [] }, program: { scripts: [] } },
    });
    await drainScheduled(scheduled, workerTaskDurations);

    let published = 0;
    const controller = createWorkerRuntimeController({
        workerFactory: () => ({ postMessage() {}, terminate() {} }),
        onStagePatch: () => published++,
    });
    for( const message of outbound ) {
        const started = performance.now();
        controller._receive(message);
        controllerTaskDurations.push(performance.now() - started);
    }
    assert.equal(controller.currentRender.boxes.length, 4096);
    assert.equal(published, 1);
    const coldStageTiming = outbound.find((message) =>
        message.type === 'timing' && message.sequence === null)?.timing;
    assert(coldStageTiming && coldStageTiming.maxStageTaskMs < 10,
        `real HostRuntime cold layout/stage took ${coldStageTiming?.maxStageTaskMs}ms`);
    assert(coldStageTiming.stageLayoutMs > 0 &&
        coldStageTiming.maxNonLayoutStageTaskMs < 4,
    'cold stage timing did not separate layout from cooperative work');
    measuredStageTaskMax = Math.max(measuredStageTaskMax, coldStageTiming.maxStageTaskMs);
    measuredLayoutMax = Math.max(measuredLayoutMax, coldStageTiming.stageLayoutMs);
    measuredNonLayoutStageMax = Math.max(
        measuredNonLayoutStageMax, coldStageTiming.maxNonLayoutStageTaskMs);
    outbound.length = 0;

    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 0, sequence: 1,
        input: { type: 'pointer_down', button: 1, x: 2, y: 2 },
    });
    /* finishDispatchStage starts the revision-fenced stage job; run one real
     * projection slice, then prove a newly delivered input message can enter
     * the worker between slices instead of waiting behind one monolithic task. */
    for( let index = 0; index < 2; index++ ) {
        const started = performance.now();
        scheduled.shift()();
        workerTaskDurations.push(performance.now() - started);
    }
    const sentinelStarted = performance.now();
    await endpoint.receive({
        schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 0, sequence: 2,
        input: { type: 'pointer_move', x: 3, y: 3 },
    });
    const sentinelMs = performance.now() - sentinelStarted;
    await drainScheduled(scheduled, workerTaskDurations);

    const resultOne = outbound.findIndex((message) =>
        message.type === 'result' && message.sequence === 1);
    const firstStage = outbound.findIndex((message) => message.type === 'stage');
    const lastStage = outbound.findLastIndex((message) => message.type === 'stage');
    const resultTwo = outbound.findIndex((message) =>
        message.type === 'result' && message.sequence === 2);
    assert(resultOne >= 0 && resultOne < firstStage,
        'semantic result no longer precedes its stage revision');
    assert(lastStage < resultTwo,
        'a following input overtook the preceding stage revision');
    assert(sentinelMs < 10, `a stage slice blocked worker input delivery for ${sentinelMs}ms`);
    measuredSentinelMax = Math.max(measuredSentinelMax, sentinelMs);

    for( const message of outbound ) {
        const started = performance.now();
        controller._receive(message);
        controllerTaskDurations.push(performance.now() - started);
    }
    assert.equal(controller.currentRender.version, 1);
    assert.equal(controller.currentRender.boxes.length, 4096);
    assert(controller.currentRender.boxes.every((entry) => entry.clip.right === 513),
        'cooperative stage delta changed the materialized render semantics');
    assert.equal(published, 2, 'the controller published a partial stage transaction');
    assert.equal(outbound.filter((message) => message.type === 'stage')
        .flatMap((message) => stageWireOperations(message.chunk))
        .filter((operation) => operation[0] === 1).length, 4096,
        'the real viewport/layout revision lost changed boxes');
    const stageTiming = outbound.find((message) =>
        message.type === 'timing' && message.sequence === 1)?.timing;
    assert(stageTiming && stageTiming.stageTaskCount > 1,
        '4,096-box stage work did not cross cooperative task boundaries');
    assert(stageTiming.maxStageTaskMs < 10,
        `worker reported a ${stageTiming.maxStageTaskMs}ms stage slice`);
    assert(stageTiming.maxNonLayoutStageTaskMs < 4,
        'avoidable 4,096-box update work took ' +
        `${stageTiming.maxNonLayoutStageTaskMs}ms`);
    measuredUpdateStageMax = Math.max(measuredUpdateStageMax, stageTiming.maxStageTaskMs);
    measuredNonLayoutStageMax = Math.max(
        measuredNonLayoutStageMax, stageTiming.maxNonLayoutStageTaskMs);
    measuredLayoutMax = Math.max(measuredLayoutMax, stageTiming.stageLayoutMs);
    measuredUpdateStageTasks = Math.max(measuredUpdateStageTasks, stageTiming.stageTaskCount);
    measuredStageTaskMax = Math.max(measuredStageTaskMax, stageTiming.maxStageTaskMs);
    const maxWorkerTask = Math.max(...workerTaskDurations);
    const maxControllerTask = Math.max(...controllerTaskDurations);
    assert(maxWorkerTask < 10, `4,096-box worker stage task took ${maxWorkerTask}ms`);
    assert(maxControllerTask < 4, `4,096-box controller stage task took ${maxControllerTask}ms`);
    assert(controller.receiveMetrics.maxMs < 4,
        `instrumented controller receive took ${controller.receiveMetrics.maxMs}ms`);
    assert(sentinelMs < 4, `worker input sentinel took ${sentinelMs}ms`);
    measuredControllerReceiveMax = Math.max(
        measuredControllerReceiveMax, maxControllerTask, controller.receiveMetrics.maxMs);
    assert.deepEqual(inputs.map((input) => input.type), ['pointer_down', 'pointer_move']);
    controller.dispose();
    endpoint.dispose();
}

{
    class LoopbackWorker {
        constructor() {
            const mock = mockRuntime();
            this.inputs = mock.inputs;
            this.endpoint = createRuntimeWorkerEndpoint({
                createHost: mock.createHost,
                createWasm: mock.createWasm,
                send: (message) => queueMicrotask(() => this.onmessage?.({ data: message })),
            });
            this.terminated = false;
        }
        postMessage(message) {
            if( this.terminated ) throw new Error('terminated');
            queueMicrotask(() => this.endpoint.receive(message));
        }
        terminate() { this.terminated = true; this.endpoint.dispose(); }
    }

    const services = [];
    const stages = [];
    const controller = createWorkerRuntimeController({
        workerFactory: () => new LoopbackWorker(),
        onService: (service) => services.push(service),
        onStagePatch: (stage) => stages.push(stage),
    });
    const ready = await controller.start({
        ir: { components: [] }, program: { scripts: [] }, state: {},
    });
    assert.equal(ready.mode, 'wasm');
    assert.equal(controller.currentRender.boxes.length, 80);

    const first = controller.dispatch({ type: 'pointer_down', button: 2, x: 1, y: 1 });
    const obsolete = controller.dispatch({ type: 'pointer_move', x: 2, y: 2 });
    const latest = controller.dispatch({ type: 'pointer_move', x: 3, y: 3 });
    const wheelA = controller.dispatch({ type: 'wheel', wheel: -2, x: 4, y: 4 });
    const wheelB = controller.dispatch({ type: 'wheel', wheel: -3, x: 5, y: 5 });
    const keys = Array.from({ length: 130 }, () =>
        controller.dispatch({ type: 'key', keyTyped: 48, keyPressed: 65 }));
    measuredEnqueueMax = Math.max(first.enqueueMs, obsolete.enqueueMs, latest.enqueueMs);
    assert(first.enqueueMs < 10 && obsolete.enqueueMs < 10 && latest.enqueueMs < 10);
    assert.equal((await obsolete.completion).coalesced, true);
    const selected = await first.completion;
    await latest.completion;
    await Promise.all([
        wheelA.completion, wheelB.completion,
        ...keys.map((ticket) => ticket.completion),
    ]);
    assert.equal(selected.result.menu[0].text, 'Use');
    assert.equal(controller.interaction.menuOpen, false,
        'latest pointer result must become the public interaction state');
    assert.equal(services.length, 1);
    assert(stages.length >= 2);
    assert.equal(stages.at(-1).patch.orderChanged, false);
    assert.deepEqual(stages.at(-1).patch.upsert.map((entry) => entry.key), ['render:0']);
    assert.deepEqual(controller.worker.inputs.filter((input) => input.type === 'wheel')
        .map((input) => input.wheel), [-2, -3],
        'wheel aggregation lost native onScrollWheel count/order');
    assert.equal(controller.worker.inputs.filter((input) => input.type === 'key').length, 130,
        'key repeat run-length encoding lost native key transactions');
    assert.equal(controller.pendingCount, 0);
    assert.equal(controller.pendingEventCount, 0);

    const tree = await controller.requestTree();
    assert.equal(tree.boxes.length, 150);
    const streamedTree = await controller.requestTree({ collect: false });
    assert.equal(streamedTree.boxes, null,
        'stream-only tree request retained a duplicate full inspector snapshot');
    const snapshot = await controller.requestSnapshot();
    assert.equal(snapshot.marker, 'explicit-only');

    const oldWorker = controller.worker;
    await controller.reload({ ir: { components: [] }, program: { scripts: [] } });
    assert.equal(controller.worker, oldWorker, 'reload discarded the worker module/data caches');
    assert.equal(oldWorker.terminated, false);
    controller.dispose();
}

console.log('runtime worker tests passed (max input enqueue ' +
    `${measuredEnqueueMax.toFixed(3)}ms; max 4096-box stage task ` +
    `${measuredStageTaskMax.toFixed(3)}ms; update ${measuredUpdateStageMax.toFixed(3)}ms; ` +
    `layout ${measuredLayoutMax.toFixed(3)}ms; non-layout ` +
    `${measuredNonLayoutStageMax.toFixed(3)}ms/${measuredUpdateStageTasks} tasks; ` +
    `controller ${measuredControllerReceiveMax.toFixed(3)}ms; ` +
    `sentinel ${measuredSentinelMax.toFixed(3)}ms)`);
