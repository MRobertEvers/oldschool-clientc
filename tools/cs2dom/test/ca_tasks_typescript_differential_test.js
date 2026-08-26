/*
 * Real-Dat2 differential for the ca_tasks migration slice.
 *
 * This deliberately uses one exact compiled cache program and the complete
 * static hook-root set. ca_tasks currently installs an unreviewed timer root at
 * mount, so the test proves explicit TypeScript rejection and auto-mode C/WASM
 * fallback before comparing two isolated fallback trees at every boundary. A
 * second pair of fresh sessions measures raw dispatch without monkey patches,
 * request tracing, or TypeScript compilation inside a sample.
 *
 * Run after `make browser-runtime`:
 *   node test/ca_tasks_typescript_differential_test.js
 *
 * Latency is reported by default. The still-unproven hard contract is opt-in:
 *   CS2DOM_CA_TASKS_HARD_LATENCY=1 \
 *     node test/ca_tasks_typescript_differential_test.js
 */

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';

import { compileInterfaceProgram } from '../src/bytecode.js';
import { osrs239ClientState } from '../src/client_state.js';
import { openContentInterface } from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';
import { contentHostData } from '../src/host_data.js';
import { createHostRuntime } from '../src/host_runtime.js';
import { createWasmCS2Runtime } from '../src/wasm_runtime.js';
import {
    analyzeCS2EngineClosure,
    prepareCS2EnginePlan,
} from '../web/cs2_engine_router.js';
import moduleFactory from '../web/cs2vm_wasm.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '../../..');
const CONTENT = resolve(process.env.CS2DOM_CA_TASKS_CONTENT ||
    join(REPO, 'OSRS-Content', 'osrs239-content'));
const CACHE = resolve(process.env.CS2DOM_CA_TASKS_CACHE ||
    join(REPO, 'cache.osrs239'));
const WASM = resolve(HERE, '../web/cs2vm_wasm.wasm');
const ROUTER = resolve(HERE, '../web/cs2_engine_router.js');
const THRESHOLD_MS = positiveNumber(process.env.CS2DOM_CA_TASKS_THRESHOLD_MS, 10);
const HARD_LATENCY = process.env.CS2DOM_CA_TASKS_HARD_LATENCY === '1';
const RAW_SAMPLES = positiveInteger(process.env.CS2DOM_CA_TASKS_RAW_SAMPLES, 1);
const VIEWPORT = Object.freeze({ width: 512, height: 334 });

let report = null;
try {
    report = await run();
    console.log(JSON.stringify(report, null, 2));
} catch( error ) {
    if( error?.differentialReport )
        console.error(JSON.stringify(error.differentialReport, null, 2));
    throw error;
}

async function run() {
    assert(existsSync(join(CACHE, 'main_file_cache.dat2')),
        `real Dat2 cache is unavailable at ${CACHE}`);
    assert(existsSync(CONTENT), `OSRS-Content fallback is unavailable at ${CONTENT}`);
    assert(existsSync(WASM), `C CS2VM WebAssembly is unavailable at ${WASM}`);
    assert(existsSync(ROUTER),
        `generated TypeScript router is unavailable; run make browser-runtime`);

    /* Preparation and all compilation happen before either timed session. */
    const project = prepareDat2Project({
        cache: CACHE,
        content: CONTENT,
        unpackedContent: CONTENT,
        revision: 'osrs239',
    });
    assert(project.dat2Content, 'Dat2 decode did not provide a content tree');
    const imported = openContentInterface(project.dat2Content, 'ca_tasks', {
        source: 'dat2',
    });
    const program = compileInterfaceProgram(project, imported);
    assert(program.available && program.scripts.length > 0,
        `ca_tasks bytecode is unavailable: ${(program.warnings || []).join('; ')}`);

    const staticHookRoots = staticIRHookScriptIds(imported.ir);
    assert.deepEqual(staticHookRoots, [4781, 4816, 4821],
        'ca_tasks static Dat2 hook roots changed; review the differential sequence');
    const plan = prepareCS2EnginePlan(program, {
        mode: 'auto',
        hookEntryScriptIds: staticHookRoots,
    });
    assert.equal(plan.backend, 'wasm',
        'ca_tasks was falsely admitted despite its unresolved dynamic timer root');
    assert.equal(plan.reason, 'typescript-closure-unsupported');
    assert.equal(plan.coverage?.supported, false);
    assert.deepEqual([...plan.coverage.hookEntryScriptIds], staticHookRoots);

    let explicitError = null;
    try {
        prepareCS2EnginePlan(program, {
            mode: 'typescript', hookEntryScriptIds: staticHookRoots,
        });
    } catch( error ) { explicitError = errorDetail(error); }
    assert.equal(explicitError?.code, 'TYPESCRIPT_CLOSURE_UNSUPPORTED',
        'explicit TypeScript ca_tasks selection did not fail closed');

    const dynamicTimerRoot = 5244;
    const dynamicCoverage = analyzeCS2EngineClosure(plan.registry, [dynamicTimerRoot]);
    assert.deepEqual(dynamicCoverage.unreviewedHostOpcodes,
        [1, 43, 1305, 2120, 2505, 2600, 2601, 3100, 4108],
        'ca_tasks dynamic timer closure changed; review it before TypeScript admission');

    const hostData = deepFreeze(contentHostData(project.dat2Content));
    const wasmUrl = `data:application/wasm;base64,${readFileSync(WASM).toString('base64')}`;
    const fixture = Object.freeze({ imported, program, plan, hostData, wasmUrl });
    const result = {
        schema: 'cs2dom-ca-tasks-typescript-differential/1',
        source: 'dat2',
        interfaceId: imported.ir.interfaceId,
        interface: 'ca_tasks',
        programFingerprint: fingerprintProgram(program),
        staticHookRoots,
        closure: {
            entryScriptIds: [...plan.coverage.entryScriptIds],
            scriptCount: plan.coverage.scriptIds.length,
        },
        selection: {
            autoBackend: plan.backend,
            autoReason: plan.reason,
            explicitError,
        },
        dynamicRoot: {
            scriptId: dynamicTimerRoot,
            scriptIds: [...dynamicCoverage.scriptIds],
            unsupportedCoreOpcodes: [...dynamicCoverage.unsupportedCoreOpcodes],
            unreviewedHostOpcodes: [...dynamicCoverage.unreviewedHostOpcodes],
            unimplementedHostOpcodes: [...dynamicCoverage.unimplementedHostOpcodes],
            unknownOpcodes: [...dynamicCoverage.unknownOpcodes],
            installedEvidence: null,
        },
        parity: [],
        raw: [],
        typescriptRaw: {
            status: 'skipped',
            reason: 'unresolved dynamic SETON root 5244 is not an admitted TypeScript closure',
        },
        thresholdMs: THRESHOLD_MS,
        hardLatency: HARD_LATENCY,
    };

    const c = await createSession('c-wasm', fixture, true);
    const selected = await createSession('auto-wasm', fixture, true);
    try {
        compareBoundaryPair(result, c, selected, mountBoundary());
        const evidence = dynamicHookEvidence(c.host, dynamicTimerRoot);
        assert(evidence.length > 0,
            'C mount did not install the expected ca_tasks timer root 5244');
        result.dynamicRoot.installedEvidence = evidence;
        compareBoundaryPair(result, c, selected, dispatchBoundary(
            'tick-redraw-1', { type: 'tick', cycle: 1 }));

        const cButton = knownTierFilterButton(c.host);
        const selectedButton = knownTierFilterButton(selected.host);
        assert.deepEqual(selectedButton, cButton,
            `tier_filter target diverged after fallback redraw: ` +
            `C=${JSON.stringify(cButton)} selected=${JSON.stringify(selectedButton)}`);
        for( const boundary of buttonBoundaries(cButton) )
            compareBoundaryPair(result, c, selected, boundary);
        compareBoundaryPair(result, c, selected, dispatchBoundary(
            'tick-after-button', { type: 'tick', cycle: 2 }));
    } catch( error ) {
        error.differentialReport = result;
        throw error;
    } finally {
        c.destroy();
        selected.destroy();
    }

    /* The parity run warms both engines. Raw samples use fresh trees and a
     * direct invoke callback; no profiler/request/method wrappers are present. */
    for( const backend of ['c-wasm', 'auto-wasm'] ) {
        for( let sample = 0; sample < RAW_SAMPLES; sample++ ) {
            const rows = await rawSequence(backend, fixture, sample + 1);
            result.raw.push(...rows);
        }
    }

    if( HARD_LATENCY ) {
        const slow = result.raw.filter((row) => row.interaction && row.ms >= THRESHOLD_MS);
        if( slow.length ) {
            const detail = slow.map((row) =>
                `${row.backend}/${row.boundary}=${row.ms.toFixed(3)}ms`).join(', ');
            const error = new Error(
                `ca_tasks raw dispatch exceeded ${THRESHOLD_MS}ms: ${detail}`);
            error.differentialReport = result;
            throw error;
        }
    }
    return result;
}

async function createSession(backend, fixture, instrumentCounts) {
    const metrics = { invocations: 0, hostRequests: 0 };
    const session = { backend, host: null, runtime: null, metrics };
    const invoke = instrumentCounts
        ? (intent) => {
            metrics.invocations++;
            const value = session.runtime.invokeIntent(intent);
            metrics.hostRequests += Number(value?.hostRequests) || 0;
            return value;
        }
        : (intent) => session.runtime.invokeIntent(intent);
    session.host = createHostRuntime(fixture.imported.ir, {
        viewport: VIEWPORT,
        hostData: fixture.hostData,
        state: osrs239ClientState(),
        recordChanges: false,
        invoke,
    });
    try {
        assert(backend === 'c-wasm' || backend === 'auto-wasm');
        if( backend === 'auto-wasm' ) assert.equal(fixture.plan.backend, 'wasm');
        session.runtime = await createWasmCS2Runtime({
            program: fixture.program,
            host: session.host,
            moduleFactory,
            wasmUrl: fixture.wasmUrl,
            fastHost: true,
            preloadHostData: true,
        });
    } catch( error ) {
        const wrapped = new Error(
            `${backend} ca_tasks runtime creation failed: ${error?.message || String(error)}`,
            { cause: error });
        wrapped.code = error?.code;
        throw wrapped;
    }
    session.destroy = () => session.runtime?.destroy();
    return session;
}

function compareBoundaryPair(result, c, selected, boundary) {
    const cRow = observedBoundary(c, boundary);
    const selectedRow = observedBoundary(selected, boundary);
    const row = {
        boundary: boundary.label,
        event: boundary.event || null,
        c: publicObservation(cRow),
        selected: publicObservation(selectedRow),
    };
    result.parity.push(row);

    assert.deepEqual(selectedRow.error, cRow.error,
        `${boundary.label} backend errors diverged`);
    assert.equal(cRow.error, null,
        `${boundary.label} unexpectedly failed: ${JSON.stringify(cRow.error)}`);
    assert.equal(selectedRow.invocations, cRow.invocations,
        `${boundary.label} hook invocation count diverged`);
    assert.equal(selectedRow.hostRequests, cRow.hostRequests,
        `${boundary.label} Host-call count diverged`);
    if( selectedRow.treeFingerprint !== cRow.treeFingerprint ||
        selectedRow.snapshotFingerprint !== cRow.snapshotFingerprint ) {
        const difference = firstDifference(cRow.snapshot, selectedRow.snapshot);
        assert.fail(`${boundary.label} Host tree diverged at ${difference.path}: ` +
            `C=${preview(difference.left)} selected=${preview(difference.right)}; ` +
            `tree ${cRow.treeFingerprint}/${selectedRow.treeFingerprint}; ` +
            `snapshot ${cRow.snapshotFingerprint}/${selectedRow.snapshotFingerprint}`);
    }
}

function observedBoundary(session, boundary) {
    const beforeInvocations = session.metrics.invocations;
    const beforeHostRequests = session.metrics.hostRequests;
    let error = null;
    try { boundary.run(session.host); }
    catch( caught ) { error = errorDetail(caught); }
    const snapshot = session.host.snapshot();
    return {
        error,
        invocations: session.metrics.invocations - beforeInvocations,
        hostRequests: session.metrics.hostRequests - beforeHostRequests,
        treeFingerprint: fingerprint(snapshot.boxes),
        snapshotFingerprint: fingerprint(snapshot),
        snapshot,
    };
}

function publicObservation(row) {
    return {
        error: row.error,
        invocations: row.invocations,
        hostRequests: row.hostRequests,
        treeFingerprint: row.treeFingerprint,
        snapshotFingerprint: row.snapshotFingerprint,
    };
}

async function rawSequence(backend, fixture, sample) {
    const session = await createSession(backend, fixture, false);
    const rows = [];
    try {
        rows.push(rawBoundary(session, mountBoundary(), sample));
        rows.push(rawBoundary(session, dispatchBoundary(
            'tick-redraw-1', { type: 'tick', cycle: 1 }), sample));
        const button = knownTierFilterButton(session.host);
        for( const boundary of buttonBoundaries(button) )
            rows.push(rawBoundary(session, boundary, sample));
        rows.push(rawBoundary(session, dispatchBoundary(
            'tick-after-button', { type: 'tick', cycle: 2 }), sample));
        return rows;
    } finally { session.destroy(); }
}

function rawBoundary(session, boundary, sample) {
    const before = performance.now();
    let error = null;
    try { boundary.run(session.host); }
    catch( caught ) { error = errorDetail(caught); }
    const ms = performance.now() - before;
    if( error ) assert.fail(
        `${session.backend} raw ${boundary.label} failed: ${JSON.stringify(error)}`);
    /* Snapshot work is deliberately outside the raw dispatch interval. It also
     * reproduces the browser's render boundary before the next interaction. */
    const snapshot = session.host.snapshot();
    return {
        backend: session.backend,
        sample,
        boundary: boundary.label,
        interaction: boundary.interaction,
        ms,
        treeFingerprint: fingerprint(snapshot.boxes),
        snapshotFingerprint: fingerprint(snapshot),
    };
}

function mountBoundary() {
    return {
        label: 'mount', interaction: false, event: null,
        run: (host) => host.mount(),
    };
}

function dispatchBoundary(label, event) {
    return {
        label, event, interaction: true,
        run: (host) => host.dispatch(event),
    };
}

function buttonBoundaries(button) {
    const point = { x: button.x, y: button.y };
    return [
        dispatchBoundary('tier-filter-pointer-move', { type: 'pointer_move', ...point }),
        dispatchBoundary('tier-filter-pointer-down', {
            type: 'pointer_down', ...point, button: 0,
        }),
        dispatchBoundary('tier-filter-pointer-up', {
            type: 'pointer_up', ...point, button: 0,
        }),
    ];
}

function knownTierFilterButton(host) {
    const root = host._component(host.ref('tier_filter'), false);
    assert(root, 'ca_tasks tier_filter component is missing');
    const candidates = [];
    for( const box of host.layout() ) {
        const component = host._component(box.ref, false);
        if( !component || !descendsFrom(host, component, root) ) continue;
        const hooks = Object.keys(component.hooks || {})
            .map(normalizeHookName).sort();
        if( !hooks.some((name) => name === 'onclick' || name === 'onop') ) continue;
        const point = boxPoint(host, box);
        if( !point ) continue;
        const hit = host._hit(point.x, point.y);
        if( !hit || !descendsFrom(host, hit, root) ) continue;
        const meta = host.meta.get(component);
        candidates.push({
            x: point.x,
            y: point.y,
            componentId: meta.componentId,
            subId: meta.subId,
            hooks,
            area: box.w * box.h,
        });
    }
    candidates.sort((left, right) => right.area - left.area || left.subId - right.subId);
    assert(candidates.length > 0,
        'ca_tasks tier_filter has no visible click hook after the redraw tick');
    const { area, ...button } = candidates[0];
    return button;
}

function dynamicHookEvidence(host, scriptId) {
    const evidence = [];
    for( const component of host.ir.components ) {
        for( const [hook, binding] of Object.entries(component.hooks || {})) {
            const installed = Number(binding?.script?.id ?? binding?.scriptId ??
                binding?.script_id ?? binding?.script);
            if( installed !== scriptId ) continue;
            const meta = host.meta.get(component);
            evidence.push({
                component: component.name || `file_${component.fileId}`,
                componentId: meta?.componentId ?? null,
                fileId: component.fileId,
                hook,
                scriptId: installed,
                signature: binding?.signature ?? null,
            });
        }
    }
    return evidence.sort((left, right) =>
        left.componentId - right.componentId || left.hook.localeCompare(right.hook));
}

function descendsFrom(host, component, root) {
    for( let cursor = component; cursor; cursor = host._parentOf(cursor) )
        if( cursor === root ) return true;
    return false;
}

function boxPoint(host, box) {
    if( box.effectiveHidden || box.culled || box.w <= 0 || box.h <= 0 ) return null;
    const left = Math.max(0, box.x, Number(box.clip?.left ?? box.x));
    const top = Math.max(0, box.y, Number(box.clip?.top ?? box.y));
    const right = Math.min(host.viewport.width, box.x + box.w,
        Number(box.clip?.right ?? box.x + box.w));
    const bottom = Math.min(host.viewport.height, box.y + box.h,
        Number(box.clip?.bottom ?? box.y + box.h));
    if( right <= left || bottom <= top ) return null;
    return {
        x: Math.floor(left + (right - left - 1) / 2),
        y: Math.floor(top + (bottom - top - 1) / 2),
    };
}

function staticIRHookScriptIds(ir) {
    const result = new Set();
    for( const component of ir?.components || [] ) {
        for( const bindings of [component?.hooks, component?.events] ) {
            if( !bindings || typeof bindings !== 'object' ) continue;
            for( const binding of Object.values(bindings) ) {
                if( !binding || typeof binding !== 'object' ) continue;
                const raw = binding.script?.id ?? binding.scriptId ?? binding.script_id;
                if( raw === undefined || raw === null ) continue;
                const id = Number(raw);
                if( Number.isFinite(id) && id <= 0 ) continue;
                assert(Number.isSafeInteger(id) && id <= 0x7fffffff,
                    `invalid static hook clientscript id ${String(raw)}`);
                result.add(id);
            }
        }
    }
    return [...result].sort((left, right) => left - right);
}

function fingerprintProgram(program) {
    return fingerprint({
        schema: program.schema,
        dialect: program.dialect,
        revision: program.revision,
        entries: program.entries,
        scripts: program.scripts.map(({ id, name, data }) => ({ id, name, data })),
    });
}

function fingerprint(value) {
    return createHash('sha256').update(JSON.stringify(value)).digest('hex');
}

function errorDetail(error) {
    if( !error ) return null;
    return {
        name: error.name || 'Error',
        code: error.code ?? null,
        message: error.message || String(error),
        scriptId: error.scriptId ?? null,
        pc: error.pc ?? null,
        opcode: error.opcode ?? null,
        vmError: error.vmError ?? null,
    };
}

function firstDifference(left, right, path = '$', seen = new WeakMap()) {
    if( Object.is(left, right) ) return null;
    if( typeof left !== 'object' || left === null ||
        typeof right !== 'object' || right === null ) return { path, left, right };
    if( seen.get(left) === right ) return null;
    seen.set(left, right);
    if( Array.isArray(left) !== Array.isArray(right) ) return { path, left, right };
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    if( leftKeys.join('\0') !== rightKeys.join('\0') )
        return { path: `${path}.[keys]`, left: leftKeys, right: rightKeys };
    for( const key of leftKeys ) {
        const result = firstDifference(left[key], right[key],
            Array.isArray(left) ? `${path}[${key}]` : `${path}.${key}`, seen);
        if( result ) return result;
    }
    return { path, left, right };
}

function preview(value) {
    const text = JSON.stringify(value);
    return text?.length > 300 ? `${text.slice(0, 300)}...` : text;
}

function normalizeHookName(value) {
    return String(value).replace(/[^a-z0-9]/gi, '').toLowerCase();
}

function deepFreeze(value, seen = new WeakSet()) {
    if( !value || typeof value !== 'object' || seen.has(value) ) return value;
    seen.add(value);
    if( value instanceof Map || value instanceof Set )
        for( const item of value.values() ) deepFreeze(item, seen);
    else for( const item of Object.values(value) ) deepFreeze(item, seen);
    return Object.freeze(value);
}

function positiveNumber(value, fallback) {
    const result = Number(value);
    return Number.isFinite(result) && result > 0 ? result : fallback;
}

function positiveInteger(value, fallback) {
    const result = Math.trunc(Number(value));
    return Number.isFinite(result) && result > 0 ? result : fallback;
}
