import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { RUNTIME_WORKER_SCHEMA } from '../src/runtime_worker_protocol.js';
import { createRuntimeWorkerEndpoint } from '../src/runtime_worker.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const SOURCE = join(CS2DOM, 'src', 'cs2_engine_router.ts');

const compiled = mkdtempSync(join(tmpdir(), 'cs2dom-engine-router-'));
try {
    writeFileSync(join(compiled, 'package.json'), '{"type":"module"}\n');
    const result = spawnSync(TSC, [
        '--strict',
        '--target', 'ES2020',
        '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext',
        '--skipLibCheck',
        '--rootDir', join(CS2DOM, 'src'),
        '--outDir', compiled,
        SOURCE,
    ], { cwd: CS2DOM, encoding: 'utf8' });
    assert.equal(result.status, 0,
        `TypeScript CS2 engine router did not compile:\n${result.stdout}${result.stderr}`);
    const router = await import(pathToFileURL(join(compiled, 'cs2_engine_router.js')).href);
    await runRouterTests(router);
    await runWorkerSelectionTests(router);
} finally {
    rmSync(compiled, { recursive: true, force: true });
}

console.log('TypeScript CS2 engine router tests passed');

async function runRouterTests(router) {
    const callee = encodeScript([
        op(33, 'int32', 0),
        op(33, 'int32', 1),
        op(4000, 'int8'),
        op(21, 'int8'),
    ], { intLocalCount: 2, intArgumentCount: 2 });
    const entry = encodeScript([
        op(33, 'int32', 0),
        op(33, 'int32', 1),
        op(40, 'int32', 2),
        op(21, 'int8'),
    ], { intLocalCount: 2, intArgumentCount: 2 });
    const coreProgram = program([
        { id: 1, data: entry.toString('base64') },
        { id: 2, data: callee.toString('base64') },
    ], [1]);

    const defaultPlan = router.prepareCS2EnginePlan({
        ...coreProgram,
        scripts: [{ id: 1, data: 'this is intentionally not base64' }],
    });
    assert.equal(defaultPlan.backend, 'wasm');
    assert.equal(defaultPlan.reason, 'production-default');
    assert.equal(defaultPlan.registry, null,
        'the production WASM path unexpectedly depended on the migration decoder');

    const plan = router.prepareCS2EnginePlan(coreProgram, { mode: 'typescript' });
    assert.equal(plan.backend, 'typescript');
    assert.equal(plan.reason, 'reviewed-typescript-closure');
    assert.deepEqual(plan.coverage.scriptIds, [1, 2],
        'opcode-40 dependency was not included in the preflight closure');
    assert.deepEqual(plan.coverage.unreviewedHostOpcodes, []);

    const runtime = await router.createTypeScriptCS2Runtime({ program: coreProgram, plan });
    const executed = runtime.invokeIntent({ hook: { scriptId: 1, args: [7, 8] } });
    assert.deepEqual(executed.intStack, [15]);
    assert.equal(executed.hostRequests, 0);
    assert.equal(executed.status, 'done');
    runtime.destroy();
    assert.throws(() => runtime.invokeIntent({ hook: { scriptId: 1, args: [1, 2] } }),
        (error) => error.code === 'TYPESCRIPT_RUNTIME_DESTROYED');

    const eventProgram = program([{ id: 10, data: encodeScript([
        op(33, 'int32', 0), op(21, 'int8'),
    ], { intLocalCount: 1, intArgumentCount: 1 }).toString('base64') }], [10]);
    const eventRuntime = await router.createTypeScriptCS2Runtime({ program: eventProgram });
    assert.deepEqual(eventRuntime.invokeIntent({
        hook: { scriptId: 10, args: [-2147483647] },
        locals: { eventMouseX: 321 },
    }).intStack, [321], 'hook event sentinel was not resolved like C/WASM');

    const directHostProgram = program([{ id: 20, data: encodeScript([
        op(25, 'int32', 3958), op(21, 'int8'),
    ]).toString('base64') }], [20]);
    const directHostPlan = router.prepareCS2EnginePlan(directHostProgram, {
        mode: 'typescript',
    });
    assert.equal(directHostPlan.backend, 'typescript');
    assert.deepEqual(directHostPlan.coverage.unreviewedHostOpcodes, []);
    assert.deepEqual(directHostPlan.coverage.unimplementedHostOpcodes, []);

    /* SETON's installed script id is stack data. Opcode-40/static-hook
     * traversal cannot prove that future entry closure, even when the setter
     * itself has reviewed direct Host semantics. */
    const dynamicHookProgram = program([{ id: 22, data: encodeScript([
        op(1400, 'int8'), op(21, 'int8'),
    ]).toString('base64') }], [22]);
    const dynamicHookAuto = router.prepareCS2EnginePlan(dynamicHookProgram, {
        mode: 'auto',
    });
    assert.equal(dynamicHookAuto.backend, 'wasm');
    assert.equal(dynamicHookAuto.reason, 'typescript-closure-unsupported');
    assert.deepEqual(dynamicHookAuto.coverage.unresolvedDynamicHookOpcodes, [1400]);
    assert.deepEqual(dynamicHookAuto.coverage.unresolvedDynamicHookSourceScriptIds, [22]);
    assert.throws(() => router.prepareCS2EnginePlan(dynamicHookProgram, {
        mode: 'typescript',
    }), (error) => error.code === 'TYPESCRIPT_CLOSURE_UNSUPPORTED' &&
        error.coverage.unresolvedDynamicHookOpcodes.join(',') === '1400' &&
        /unresolved runtime hook installers/.test(error.message),
    'an unresolved installed-script root entered an explicit TS session');

    /* Native CC_CREATE/CC_FIND can yield while another interface group is
     * loaded. The synchronous backend is ineligible unless its caller makes
     * the stronger all-referenced-groups-preloaded guarantee explicitly. */
    const groupLoadingProgram = program([{ id: 23, data: encodeScript([
        op(200, 'int8'), op(21, 'int8'),
    ]).toString('base64') }], [23]);
    const unresolvedGroups = router.prepareCS2EnginePlan(groupLoadingProgram, {
        mode: 'auto',
    });
    assert.equal(unresolvedGroups.backend, 'wasm');
    assert.deepEqual(unresolvedGroups.coverage.unresolvedInterfaceGroupOpcodes, [200]);
    assert.equal(unresolvedGroups.coverage.allReferencedInterfaceGroupsPreloaded, false);
    assert.throws(() => router.prepareCS2EnginePlan(groupLoadingProgram, {
        mode: 'typescript',
    }), (error) => error.code === 'TYPESCRIPT_CLOSURE_UNSUPPORTED' &&
        /unproven preloaded interface groups 200/.test(error.message));
    const preloadedGroups = router.prepareCS2EnginePlan(groupLoadingProgram, {
        mode: 'typescript', allReferencedInterfaceGroupsPreloaded: true,
    });
    assert.equal(preloadedGroups.backend, 'typescript');
    assert.deepEqual(preloadedGroups.coverage.unresolvedInterfaceGroupOpcodes, []);
    assert.equal(preloadedGroups.coverage.allReferencedInterfaceGroupsPreloaded, true);

    const directCalls = [];
    const lifecycleEvents = [];
    const directHost = {
        beginCS2DirectInvocation() { lifecycleEvents.push('begin'); },
        PUSH_VARBIT(id) { lifecycleEvents.push('host'); directCalls.push(id); return 73; },
        endCS2DirectInvocation(error) {
            lifecycleEvents.push(error === null ? 'end:ok' : 'end:error');
        },
        request() { throw new Error('tagged Host request entered the TS hot path'); },
    };
    const directRuntime = await router.createTypeScriptCS2Runtime({
        program: directHostProgram, plan: directHostPlan, host: directHost,
    });
    const directResult = directRuntime.invokeIntent({
        component: { componentId: 0x000c0001, generation: 4 },
        hook: { scriptId: 20, args: [] },
    });
    assert.deepEqual(directCalls, [3958]);
    assert.deepEqual(lifecycleEvents, ['begin', 'host', 'end:ok']);
    assert.deepEqual(directResult.intStack, [73]);
    assert.equal(directResult.hostRequests, 1);
    assert.equal(directResult.cycles, 2);
    await assert.rejects(() => router.createTypeScriptCS2Runtime({
        program: directHostProgram, plan: directHostPlan,
        host: { request() { return 73; } },
    }), (error) => error.code === 'TYPESCRIPT_HOST_SURFACE_MISSING' &&
        /PUSH_VARBIT/.test(error.message),
    'a tagged request-only Host was admitted as a positional Host surface');
    await assert.rejects(() => router.createTypeScriptCS2Runtime({
        program: directHostProgram,
        host: { PUSH_VARBIT() { return 73; } },
    }), (error) => error.code === 'TYPESCRIPT_HOST_SURFACE_MISSING' &&
        /beginCS2DirectInvocation/.test(error.message) &&
        /endCS2DirectInvocation/.test(error.message),
    'a Host-bearing closure bypassed the direct invocation transaction');
    await assert.rejects(() => router.createTypeScriptCS2Runtime({
        program: directHostProgram,
        host: { PUSH_VARBIT() { return 73; }, beginCS2DirectInvocation() {} },
    }), (error) => error.code === 'TYPESCRIPT_HOST_SURFACE_MISSING' &&
        /beginCS2DirectInvocation/.test(error.message) &&
        /endCS2DirectInvocation/.test(error.message),
    'a half-configured direct invocation lifecycle was admitted');

    const asyncLifecycle = [];
    const asyncDirectRuntime = await router.createTypeScriptCS2Runtime({
        program: directHostProgram,
        host: {
            beginCS2DirectInvocation() { asyncLifecycle.push('begin'); },
            PUSH_VARBIT() { return Promise.resolve(73); },
            endCS2DirectInvocation(error) {
                asyncLifecycle.push(error?.code || 'missing-error');
                throw new Error('finalizer must not replace original error');
            },
        },
    });
    assert.throws(() => asyncDirectRuntime.invokeIntent({
        hook: { scriptId: 20, args: [] },
    }), (error) => error.code === 'TYPESCRIPT_EXECUTION_FAILED' &&
        /non-finite integer/.test(error.message),
    'a result-bearing asynchronous Host method did not fail closed');
    assert.deepEqual(asyncLifecycle, ['begin', 'TYPESCRIPT_EXECUTION_FAILED'],
        'direct invocation finalizer did not receive the exact VM error');

    /* A core string-array handle and Host text occupy the same C stack family,
     * but the direct Host boundary must reject the handle rather than coerce it
     * to "[object Object]". */
    const arrayTextProgram = program([{ id: 21, data: encodeScript([
        op(0, 'int32', 1), op(44, 'int32', 115), op(35, 'int32', 0),
        op(1112, 'int8', 0), op(21, 'int8'),
    ], { stringLocalCount: 1 }).toString('base64') }], [21]);
    let textCalls = 0;
    const arrayTextRuntime = await router.createTypeScriptCS2Runtime({
        program: arrayTextProgram,
        host: {
            beginCS2DirectInvocation() {},
            CC_SETTEXT() { textCalls++; },
            endCS2DirectInvocation() {},
        },
    });
    assert.throws(() => arrayTextRuntime.invokeIntent({
        component: { componentId: 0x000c0002 }, hook: { scriptId: 21, args: [] },
    }), (error) => error.code === 'TYPESCRIPT_EXECUTION_FAILED' &&
        /array handle cannot be consumed as Host text/.test(error.message));
    assert.equal(textCalls, 0, 'invalid Host text reached the positional method');

    const hostProgram = program([{ id: 3, data: encodeScript([
        op(3170, 'int8', -2), op(21, 'int8'),
    ]).toString('base64') }], [3]);
    const autoHost = router.prepareCS2EnginePlan(hostProgram, { mode: 'auto' });
    assert.equal(autoHost.backend, 'wasm');
    assert.equal(autoHost.reason, 'typescript-closure-unsupported');
    assert.deepEqual(autoHost.coverage.unreviewedHostOpcodes, [3170]);
    assert.deepEqual(autoHost.coverage.unknownOpcodes, []);
    assert.throws(
        () => router.prepareCS2EnginePlan(hostProgram, { mode: 'typescript' }),
        (error) => error.code === 'TYPESCRIPT_CLOSURE_UNSUPPORTED' &&
            error.coverage.unreviewedHostOpcodes.join(',') === '3170',
        'schema-only Host metadata enabled TypeScript execution',
    );

    /* The transported entries are not authoritative: cache/IR hooks can name
     * another loaded script. That root and its whole opcode-40 closure must be
     * admitted before a TypeScript session can exist. */
    const mismatchedHookProgram = program([
        { id: 11, data: encodeScript([op(21, 'int8')]).toString('base64') },
        { id: 12, data: encodeScript([
            op(3170, 'int8', -2), op(21, 'int8'),
        ]).toString('base64') },
    ], [11]);
    const hookRootPlan = router.prepareCS2EnginePlan(mismatchedHookProgram, {
        mode: 'auto', hookEntryScriptIds: [12, 12],
    });
    assert.equal(hookRootPlan.backend, 'wasm');
    assert.deepEqual(hookRootPlan.coverage.programEntryScriptIds, [11]);
    assert.deepEqual(hookRootPlan.coverage.hookEntryScriptIds, [12]);
    assert.deepEqual(hookRootPlan.coverage.entryScriptIds, [11, 12]);
    assert.deepEqual(hookRootPlan.coverage.scriptIds, [11, 12]);
    assert.deepEqual(hookRootPlan.coverage.unreviewedHostOpcodes, [3170]);
    assert.throws(() => router.prepareCS2EnginePlan(mismatchedHookProgram, {
        mode: 'typescript', hookEntryScriptIds: [12],
    }), (error) => error.code === 'TYPESCRIPT_CLOSURE_UNSUPPORTED' &&
        error.coverage.hookEntryScriptIds.join(',') === '12');

    const extraCoreHookProgram = program([
        { id: 13, data: encodeScript([op(21, 'int8')]).toString('base64') },
        { id: 14, data: encodeScript([
            op(33, 'int32', 0), op(21, 'int8'),
        ], { intLocalCount: 1, intArgumentCount: 1 }).toString('base64') },
    ], [13]);
    const extraCoreHookPlan = router.prepareCS2EnginePlan(extraCoreHookProgram, {
        mode: 'typescript', hookEntryScriptIds: [14],
    });
    const extraCoreHookRuntime = await router.createTypeScriptCS2Runtime({
        program: extraCoreHookProgram, plan: extraCoreHookPlan,
    });
    assert.deepEqual(extraCoreHookRuntime.invokeIntent({
        hook: { scriptId: 14, args: [314] },
    }).intStack, [314], 'IR-only hook root was audited but not admitted to the TS session');
    extraCoreHookRuntime.destroy();

    const unknownProgram = program([{ id: 4, data: encodeScript([
        op(204, 'int8'), op(21, 'int8'),
    ]).toString('base64') }], [4]);
    const autoUnknown = router.prepareCS2EnginePlan(unknownProgram, { mode: 'auto' });
    assert.equal(autoUnknown.backend, 'wasm');
    assert.deepEqual(autoUnknown.coverage.unknownOpcodes, [204]);

    const missingProgram = program([{ id: 5, data: encodeScript([
        op(40, 'int32', 99), op(21, 'int8'),
    ]).toString('base64') }], [5]);
    const missing = router.prepareCS2EnginePlan(missingProgram, { mode: 'auto' });
    assert.equal(missing.backend, 'wasm');
    assert.deepEqual(missing.coverage.missingScriptIds, [99]);

    const bad = router.prepareCS2EnginePlan(program([
        { id: 6, data: 'not-base64' },
    ], [6]), { mode: 'auto' });
    assert.equal(bad.backend, 'wasm');
    assert.equal(bad.reason, 'typescript-decode-failed');
    assert.equal(bad.decodeFailure.code, 'BAD_BASE64');
    assert.throws(() => router.prepareCS2EnginePlan(program([
        { id: 6, data: 'not-base64' },
    ], [6]), { mode: 'typescript' }), (error) => error.code === 'BAD_BASE64');
}

async function runWorkerSelectionTests(router) {
    /* An explicit TS rejection must happen before createHost can allocate or
     * mutate the working tree. */
    {
        const outbound = [];
        let hosts = 0;
        let wasm = 0;
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            createHost() { hosts++; throw new Error('must not construct Host'); },
            createWasm() { wasm++; throw new Error('must not construct WASM'); },
            prepareEngine: router.prepareCS2EnginePlan,
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 1,
            config: {
                engineMode: 'typescript', ir: { components: [] },
                program: program([{ id: 3, data: encodeScript([
                    op(3170, 'int8'), op(21, 'int8'),
                ]).toString('base64') }], [3]),
            },
        });
        assert.equal(hosts, 0);
        assert.equal(wasm, 0);
        assert.equal(outbound.at(-1).type, 'failed');
        assert.equal(outbound.at(-1).error.code, 'TYPESCRIPT_CLOSURE_UNSUPPORTED');
    }

    /* Reproduction: program.entries declares a harmless script while the real
     * onLoad hook points at a loaded Host-using script. Explicit TS must reject
     * that actual hook root before Host construction. */
    const mismatchedHookProgram = program([
        { id: 8, data: encodeScript([op(21, 'int8')]).toString('base64') },
        { id: 9, data: encodeScript([
            op(3170, 'int8', -2), op(21, 'int8'),
        ]).toString('base64') },
    ], [8]);
    {
        const outbound = [];
        let hosts = 0;
        let wasm = 0;
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            createHost() { hosts++; throw new Error('must not construct Host'); },
            createWasm() { wasm++; throw new Error('must not construct WASM'); },
            prepareEngine: router.prepareCS2EnginePlan,
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 11,
            config: {
                engineMode: 'typescript',
                ir: { components: [{ hooks: {
                    onload: { script: { id: 9 }, args: [] },
                    absent: null,
                } }] },
                program: mismatchedHookProgram,
            },
        });
        assert.equal(hosts, 0);
        assert.equal(wasm, 0);
        assert.equal(outbound.at(-1).type, 'failed');
        assert.equal(outbound.at(-1).error.code, 'TYPESCRIPT_CLOSURE_UNSUPPORTED');
    }

    /* Auto sees the compact `{scriptId}` form too, commits the complete plan
     * to C/WASM, and only then allows Host construction. */
    {
        const outbound = [];
        const scheduled = [];
        let selected = false;
        let hosts = 0;
        let wasm = 0;
        let typescript = 0;
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
            cancel: () => {},
            prepareEngine(programRecord, options) {
                const plan = router.prepareCS2EnginePlan(programRecord, options);
                selected = plan.backend === 'wasm' &&
                    plan.coverage.hookEntryScriptIds.join(',') === '9';
                return plan;
            },
            createHost() {
                assert.equal(selected, true, 'Host constructed before whole-IR selection');
                hosts++;
                return {
                    version: 0, viewport: { width: 1, height: 1 }, layout: () => [],
                    mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
                    dispatch: () => ({ version: 0, interaction: {} }),
                    snapshot: () => ({}),
                };
            },
            async createWasm() {
                wasm++;
                return { destroy() {}, invokeIntent() { return { status: 'done' }; } };
            },
            async createTypeScript() { typescript++; throw new Error('TS backend crossed'); },
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 12,
            config: {
                engineMode: 'auto',
                ir: { components: [{ hooks: { onload: { scriptId: 9, args: [] } } }] },
                program: mismatchedHookProgram,
            },
        });
        await drain(scheduled);
        assert.equal(hosts, 1);
        assert.equal(wasm, 1);
        assert.equal(typescript, 0);
        const ready = outbound.find((message) => message.type === 'ready');
        assert.equal(ready.mode, 'wasm');
        assert.deepEqual(ready.engineSelection.coverage.programEntryScriptIds, [8]);
        assert.deepEqual(ready.engineSelection.coverage.hookEntryScriptIds, [9]);
        assert.deepEqual(ready.engineSelection.coverage.entryScriptIds, [8, 9]);
    }

    /* A fully reviewed core closure selects one TS session. Dispatch cannot
     * cross back into the supplied WASM factory. */
    {
        const outbound = [];
        const scheduled = [];
        let wasm = 0;
        let typescript = 0;
        let supplied;
        let version = 0;
        const coreProgram = program([{ id: 8, data: encodeScript([
            op(33, 'int32', 0), op(21, 'int8'),
        ], { intLocalCount: 1, intArgumentCount: 1 }).toString('base64') }], [8]);
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
            cancel: () => {},
            prepareEngine: router.prepareCS2EnginePlan,
            async createTypeScript(options) {
                typescript++;
                return router.createTypeScriptCS2Runtime(options);
            },
            async createWasm() { wasm++; throw new Error('WASM backend crossed'); },
            createHost(ir, options) {
                supplied = options;
                return {
                    get version() { return version; },
                    viewport: { width: 512, height: 334 },
                    layout: () => [],
                    mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
                    dispatch() {
                        const vm = supplied.invoke({ hook: { scriptId: 8, args: [44] } });
                        return {
                            version,
                            vm,
                            interaction: { menuOpen: false, menuEntries: [] },
                        };
                    },
                    snapshot: () => ({ version }),
                };
            },
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 2,
            config: { engineMode: 'auto', ir: { components: [] }, program: coreProgram },
        });
        await drain(scheduled);
        assert.equal(typescript, 1);
        assert.equal(wasm, 0);
        const ready = outbound.find((message) => message.type === 'ready');
        assert.equal(ready.mode, 'typescript');
        assert.equal(ready.engineSelection.backend, 'typescript');
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 2, sequence: 1,
            input: { type: 'key', keyTyped: 1, keyPressed: 1 },
        });
        await drain(scheduled);
        const result = outbound.find((message) => message.type === 'result');
        assert.deepEqual(result.result.vm.intStack, [44]);
        assert.equal(wasm, 0);
    }

    /* A TypeScript routing/execution failure is an invalid backend proof, not
     * a legitimate integer-zero script result. Preserve the C/WASM warning
     * compatibility path, but rethrow TS failures through dispatch. */
    {
        const outbound = [];
        const scheduled = [];
        let supplied;
        const coreProgram = program([{ id: 18, data: encodeScript([
            op(21, 'int8'),
        ]).toString('base64') }], [18]);
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
            cancel: () => {},
            prepareEngine: router.prepareCS2EnginePlan,
            async createTypeScript() {
                return {
                    destroy() {},
                    invokeIntent() {
                        const error = new Error('synthetic TypeScript routing failure');
                        error.code = 'TYPESCRIPT_SCRIPT_NOT_ROUTED';
                        throw error;
                    },
                };
            },
            async createWasm() { throw new Error('WASM backend crossed'); },
            createHost(ir, options) {
                supplied = options;
                return {
                    version: 0, viewport: { width: 1, height: 1 }, layout: () => [],
                    mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
                    dispatch() {
                        supplied.invoke({ hook: { scriptId: 18, args: [] } });
                        return { version: 0, interaction: {} };
                    },
                    snapshot: () => ({}),
                };
            },
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 21,
            config: { engineMode: 'typescript', ir: { components: [] }, program: coreProgram },
        });
        await drain(scheduled);
        outbound.length = 0;
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'dispatch', session: 21, sequence: 7,
            input: { type: 'key', keyTyped: 1, keyPressed: 1 },
        });
        await drain(scheduled);
        const result = outbound.find((message) => message.type === 'result');
        assert.equal(result.error.code, 'TYPESCRIPT_SCRIPT_NOT_ROUTED');
        assert.match(result.error.message, /synthetic TypeScript routing failure/);
        assert.deepEqual(result.warnings, [],
            'TypeScript execution failure was still converted to a warning');
    }

    /* With no mode configured, the worker neither imports nor calls the TS
     * selector. This is the production behavior. */
    {
        const outbound = [];
        const scheduled = [];
        let prepared = 0;
        let wasm = 0;
        const endpoint = createRuntimeWorkerEndpoint({
            send: (message) => outbound.push(message),
            schedule: (fn) => { scheduled.push(fn); return scheduled.length; },
            cancel: () => {},
            prepareEngine() { prepared++; throw new Error('TS selector crossed'); },
            async createWasm() {
                wasm++;
                return { destroy() {}, invokeIntent() { return { status: 'done' }; } };
            },
            createHost() {
                return {
                    version: 0, viewport: { width: 1, height: 1 }, layout: () => [],
                    mount: () => ({ interaction: { menuOpen: false, menuEntries: [] } }),
                    dispatch: () => ({ version: 0, interaction: {} }),
                    snapshot: () => ({}),
                };
            },
        });
        await endpoint.receive({
            schema: RUNTIME_WORKER_SCHEMA, type: 'init', session: 3,
            config: {
                ir: { components: [] },
                program: program([{ id: 1, data: 'not-base64' }], [1]),
            },
        });
        await drain(scheduled);
        assert.equal(prepared, 0);
        assert.equal(wasm, 1);
        assert.equal(outbound.find((message) => message.type === 'ready').mode, 'wasm');
    }
}

function program(scripts, entries) {
    return {
        schema: 'cs2dom-bytecode/1', available: true,
        dialect: 'osrs', revision: 'osrs239', entries, scripts,
    };
}

function op(opcode, operand, intOperand = 0) {
    return { opcode, operand, intOperand };
}

function encodeScript(instructions, options = {}) {
    const chunks = [];
    for( const instruction of instructions ) {
        const opcode = Buffer.alloc(2);
        opcode.writeUInt16BE(instruction.opcode, 0);
        chunks.push(opcode);
        if( instruction.operand === 'int8' ) {
            const operand = Buffer.alloc(1);
            operand.writeInt8(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else if( instruction.operand === 'int32' ) {
            const operand = Buffer.alloc(4);
            operand.writeInt32BE(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else {
            throw new Error(`unsupported fixture operand ${instruction.operand}`);
        }
    }
    const fixed = Buffer.alloc(17);
    fixed.writeInt32BE(instructions.length, 0);
    fixed.writeUInt16BE(options.intLocalCount || 0, 4);
    fixed.writeUInt16BE(options.stringLocalCount || 0, 6);
    fixed.writeUInt16BE(0, 8);
    fixed.writeUInt16BE(options.intArgumentCount || 0, 10);
    fixed.writeUInt16BE(options.stringArgumentCount || 0, 12);
    fixed.writeUInt16BE(0, 14);
    fixed.writeUInt8(0, 16);
    const trailerLength = Buffer.alloc(2);
    trailerLength.writeUInt16BE(1, 0);
    return Buffer.concat([Buffer.from([0]), Buffer.concat(chunks), fixed, trailerLength]);
}

async function drain(scheduled) {
    while( scheduled.length ) {
        scheduled.shift()();
        await Promise.resolve();
    }
}
