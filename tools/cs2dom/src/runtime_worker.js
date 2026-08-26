/*
 * Dedicated-worker owner for the production C CS2VM/WASM + JavaScript HOST.
 * C still calls HostRuntime synchronously, but both sides now execute away from
 * browser input, React reconciliation, model painting and developer controls.
 */

import { createHostRuntime } from './host_runtime.js';
import { clientStateForRevision } from './client_state.js';
import { createWasmCS2Runtime } from './wasm_runtime.js';
import {
    RUNTIME_WORKER_STAGE_CHUNK, RUNTIME_WORKER_STAGE_SLICE_ITEMS,
    RUNTIME_WORKER_STAGE_SLICE_MS, RUNTIME_WORKER_TREE_CHUNK,
    paintableStageBox, stageBoxKey, stageBoxesEqual, validWorkerMessage, workerMessage,
} from './runtime_worker_protocol.js';

const HOST_DATA_CACHE = new Map();
const TYPESCRIPT_ENGINE_MODULE = '/ts-vm/cs2_engine_router.js';
let typescriptEngineModulePromise = null;

export function createRuntimeWorkerEndpoint({
    send,
    schedule = (fn) => setTimeout(fn, 0),
    cancel = (id) => clearTimeout(id),
    clock = () => globalThis.performance?.now?.() ?? Date.now(),
    createHost = createHostRuntime,
    createWasm = createWasmCS2Runtime,
    prepareEngine = defaultPrepareEngine,
    createTypeScript = defaultCreateTypeScript,
    stageTaskBudgetMs = RUNTIME_WORKER_STAGE_SLICE_MS,
    stageItemsPerTask = RUNTIME_WORKER_STAGE_SLICE_ITEMS,
} = {}) {
    if( typeof send !== 'function' ) throw new TypeError('runtime worker endpoint requires send()');
    const stageBudgetMs = Math.max(0.25,
        Math.min(8, Number(stageTaskBudgetMs) || RUNTIME_WORKER_STAGE_SLICE_MS));
    const stageItemLimit = Math.max(1,
        Math.min(256, Math.floor(Number(stageItemsPerTask)) ||
            RUNTIME_WORKER_STAGE_SLICE_ITEMS));

    let current = null;
    let treeTimer = null;
    let treeJob = null;
    let dispatchTimer = null;
    let dispatchJob = null;
    let stageTimer = null;
    let stageJob = null;
    let queuedTimer = null;
    const queuedMessages = [];
    let stageTransaction = 0;

    function post(type, session, fields = {}) {
        send(workerMessage(type, session, fields));
    }

    function stopTree(stale = false) {
        if( treeTimer !== null ) cancel(treeTimer);
        treeTimer = null;
        if( stale && treeJob ) post('tree', treeJob.session.id, {
            requestId: treeJob.requestId, version: treeJob.version,
            index: treeJob.index, total: treeJob.boxes.length, boxes: [],
            stale: true, done: true,
        });
        treeJob = null;
    }

    function stopDispatch() {
        if( dispatchTimer !== null ) cancel(dispatchTimer);
        if( stageTimer !== null ) cancel(stageTimer);
        if( queuedTimer !== null ) cancel(queuedTimer);
        dispatchTimer = null;
        stageTimer = null;
        queuedTimer = null;
        dispatchJob = null;
        stageJob = null;
        queuedMessages.length = 0;
    }

    function disposeSession({ focus = false } = {}) {
        stopTree();
        stopDispatch();
        const retired = current;
        current = null;
        if( !retired ) return;
        if( focus ) {
            try { retired.host?.dispatch({ type: 'focus_lost' }); }
            catch { /* a discarded session cannot report useful hook errors */ }
        }
        try { retired.wasm?.destroy?.(); }
        catch { /* teardown must not strand the worker */ }
    }

    function disposeStaleSession(session) {
        if( !session || current === session ) return false;
        try { session.wasm?.destroy?.(); }
        catch { /* a superseded async init must not leak its WASM instance */ }
        session.wasm = null;
        session.host = null;
        return true;
    }

    function warning(session, message) {
        const text = message?.message || String(message || 'unknown runtime error');
        if( !session.warnings.includes(text) ) session.warnings.push(text);
        return text;
    }

    function failSession(session, error) {
        if( current !== session ) return;
        warning(session, error);
        try { session.wasm?.destroy?.(); } catch { /* best effort */ }
        session.wasm = null;
        session.host = null;
        session.mode = 'unavailable';
        stopTree();
        stopDispatch();
        post('failed', session.id, {
            error: serializeError(error), warnings: [...session.warnings],
        });
    }

    function beginStageUpdate(session, {
        reset = false, sequence = null, onDone = null, onError = null, treeDelta = null,
        recoveryAttempted = false,
    } = {}) {
        if( stageJob ) throw new Error('a stage projection is already active');
        const previous = session.stage;
        const dirtyKeys = !reset && previous &&
            treeDelta?.baseRevision === session.treeRevision &&
            typeof session.host.projectRenderKey === 'function'
            ? incrementalStageDirtyKeys(treeDelta) : null;
        stageJob = {
            session, reset: Boolean(reset), sequence, onDone, onError,
            treeDelta, dirtyKeys, recoveryAttempted: Boolean(recoveryAttempted),
            phase: dirtyKeys ? 'dirty' : 'layout', layout: null, layoutIndex: 0,
            dirtyIndex: 0, dirtyProjected: new Map(),
            version: dirtyKeys ? Number(session.host.version) || 0 : 0,
            viewport: dirtyKeys ? {
                width: Math.max(1, Number(session.host.viewport?.width) || 512),
                height: Math.max(1, Number(session.host.viewport?.height) || 334),
            } : null,
            previousEntries: previous?.entries || [],
            previousMap: previous?.byKey || new Map(), previousKeys: previous?.keys || [],
            nextEntries: [], nextMap: new Map(), nextKeys: [], removeIndex: 0,
            upsert: [], remove: [], orderChanged: Boolean(reset),
            chunkRemoveIndex: 0, chunkUpsertIndex: 0, chunkOrderIndex: 0,
            chunkRemove: [], chunkUpsert: [], chunkOrder: [], chunkLength: 0,
            sendIndex: 0, wireTotal: 0,
            transaction: 0, next: null, layoutMs: 0,
            createdAt: clock(), maxTaskMs: 0, maxNonLayoutTaskMs: 0,
            taskCount: 0, workMs: 0,
        };
        stageTimer = schedule(pumpStage);
    }

    function pumpStage() {
        stageTimer = null;
        const job = stageJob;
        if( !job || current !== job.session ) return;
        const startingPhase = job.phase;
        const started = clock();
        let done = false;
        let error = null;
        try { done = advanceStage(job, started); }
        catch( caught ) { error = caught; }
        const elapsed = Math.max(0, clock() - started);
        job.maxTaskMs = Math.max(job.maxTaskMs, elapsed);
        if( startingPhase !== 'layout' )
            job.maxNonLayoutTaskMs = Math.max(job.maxNonLayoutTaskMs, elapsed);
        job.workMs += elapsed;
        job.taskCount++;
        if( current !== job.session ) {
            stageJob = null;
            return;
        }
        if( error || done ) {
            stageJob = null;
            const timing = {
                maxStageTaskMs: job.maxTaskMs,
                stageTaskCount: job.taskCount,
                stageWorkMs: job.workMs,
                stageElapsedMs: Math.max(0, clock() - job.createdAt),
                stageLayoutMs: job.layoutMs,
                maxNonLayoutStageTaskMs: job.maxNonLayoutTaskMs,
            };
            try { post('timing', job.session.id, { sequence: job.sequence, timing }); }
            catch( timingError ) { if( !error ) error = timingError; }
            if( error && !job.recoveryAttempted && current === job.session && job.session.host ) {
                /* consumeTreeDelta() transfers ownership before projection starts.
                 * A projector/layout/send exception must therefore retain that
                 * exact committed revision until one bounded full reset either
                 * publishes it or reports a terminal error. A new transaction
                 * also supersedes any uncommitted chunks from the failed job. */
                warning(job.session,
                    `Stage projection failed; retrying one full rebuild: ${error?.message || error}`);
                try {
                    beginStageUpdate(job.session, {
                        reset: true,
                        sequence: job.sequence,
                        treeDelta: job.treeDelta,
                        recoveryAttempted: true,
                        onDone: job.onDone,
                        onError: job.onError,
                    });
                    return;
                } catch( retryError ) {
                    stageJob = null;
                    error = retryError;
                }
            }
            if( error ) job.onError?.(error, timing);
            else job.onDone?.(timing);
            return;
        }
        stageTimer = schedule(pumpStage);
    }

    function advanceStage(job, started) {
        let processed = 0;
        while( true ) {
            if( processed >= stageItemLimit ||
                (processed > 0 && clock() - started >= stageBudgetMs) ) return false;
            if( job.phase === 'layout' ) {
                const layoutStarted = clock();
                job.layout = job.session.host.layout() || [];
                job.layoutMs = Math.max(0, clock() - layoutStarted);
                job.version = Number(job.session.host.version) || 0;
                job.viewport = {
                    width: Math.max(1, Number(job.session.host.viewport?.width) || 512),
                    height: Math.max(1, Number(job.session.host.viewport?.height) || 334),
                };
                job.phase = 'project';
                processed++;
                continue;
            }
            if( job.phase === 'dirty' ) {
                if( job.dirtyIndex >= job.dirtyKeys.length ) {
                    if( job.upsert.length === 0 ) {
                        job.nextEntries = job.previousEntries;
                        job.nextMap = job.previousMap;
                        job.nextKeys = job.previousKeys;
                        job.phase = 'finalize';
                        continue;
                    }
                    job.phase = 'dirty-assemble';
                    job.layoutIndex = 0;
                    continue;
                }
                const key = job.dirtyKeys[job.dirtyIndex++];
                const old = job.previousMap.get(key) || null;
                const box = job.session.host.projectRenderKey(key);
                if( box && paintableStageBox(box) ) {
                    /* A formerly-unpaintable node has no retained draw-order
                     * slot. Its exact insertion point requires the full tree. */
                    if( !old ) {
                        restartFullStage(job);
                        processed++;
                        continue;
                    }
                    const entry = { key, box };
                    if( !stageBoxesEqual(old.box, box) ) {
                        job.dirtyProjected.set(key, entry);
                        job.upsert.push(entry);
                    }
                } else if( old ) {
                    /* Paintability changes alter the compact stage root order.
                     * Rebuild it from the full tree instead of inventing an
                     * incremental insertion/removal position. */
                    restartFullStage(job);
                } else if( box === null ) {
                    /* A dirty live key disappearing from HostRuntime is a
                     * generation/topology inconsistency, never a safe remove. */
                    restartFullStage(job);
                }
                processed++;
                continue;
            }
            if( job.phase === 'dirty-assemble' ) {
                if( job.layoutIndex >= job.previousEntries.length ) {
                    job.phase = 'finalize';
                    continue;
                }
                const old = job.previousEntries[job.layoutIndex++];
                const entry = job.dirtyProjected.get(old.key) || old;
                job.nextEntries.push(entry);
                if( !job.nextMap.has(entry.key) ) job.nextKeys.push(entry.key);
                job.nextMap.set(entry.key, entry);
                processed++;
                continue;
            }
            if( job.phase === 'project' ) {
                if( job.layoutIndex >= job.layout.length ) {
                    if( job.nextEntries.length !== job.previousEntries.length )
                        job.orderChanged = true;
                    job.phase = job.reset ? 'finalize' : 'remove';
                    continue;
                }
                const index = job.layoutIndex++;
                const box = job.layout[index];
                if( paintableStageBox(box) ) {
                    const entry = {
                        key: job.session.host.renderKey?.(box.ref) || stageBoxKey(box, index),
                        box,
                    };
                    const entryIndex = job.nextEntries.length;
                    if( !job.orderChanged &&
                        job.previousEntries[entryIndex]?.key !== entry.key )
                        job.orderChanged = true;
                    job.nextEntries.push(entry);
                    if( !job.nextMap.has(entry.key) ) job.nextKeys.push(entry.key);
                    job.nextMap.set(entry.key, entry);
                    const old = job.previousMap.get(entry.key);
                    if( job.reset || !old || !stageBoxesEqual(old.box, entry.box) )
                        job.upsert.push(entry);
                }
                processed++;
                continue;
            }
            if( job.phase === 'remove' ) {
                if( job.removeIndex >= job.previousKeys.length ) {
                    job.phase = 'finalize';
                    continue;
                }
                const key = job.previousKeys[job.removeIndex++];
                if( !job.nextMap.has(key) ) job.remove.push(key);
                processed++;
                continue;
            }
            if( job.phase === 'finalize' ) {
                job.next = {
                    version: job.version, viewport: job.viewport, entries: job.nextEntries,
                    byKey: job.nextMap, keys: job.nextKeys,
                };
                const shouldSend = job.reset || job.upsert.length || job.remove.length ||
                    job.orderChanged ||
                    job.session.lastViewportWidth !== job.viewport.width ||
                    job.session.lastViewportHeight !== job.viewport.height;
                if( shouldSend ) {
                    job.transaction = ++stageTransaction;
                    const operationCount = job.remove.length + job.upsert.length +
                        (job.orderChanged ? job.nextEntries.length : 0);
                    job.wireTotal = Math.max(1,
                        Math.ceil(operationCount / RUNTIME_WORKER_STAGE_CHUNK));
                    job.phase = 'wire';
                } else {
                    commitStageProjection(job);
                    job.phase = 'done';
                }
                continue;
            }
            if( job.phase === 'wire' ) {
                if( appendNextStageWire(job) ) {
                    processed++;
                    if( job.chunkLength >= RUNTIME_WORKER_STAGE_CHUNK ) {
                        postStageChunk(job);
                        processed++;
                    }
                    continue;
                }
                if( job.chunkLength || job.sendIndex === 0 ) {
                    postStageChunk(job);
                    processed++;
                }
                commitStageProjection(job);
                job.phase = 'done';
                continue;
            }
            if( job.phase === 'done' ) return true;
            throw new Error(`unknown stage phase ${job.phase}`);
        }
    }

    function appendNextStageWire(job) {
        if( job.chunkRemoveIndex < job.remove.length ) {
            job.chunkRemove.push(job.remove[job.chunkRemoveIndex++]);
            job.chunkLength++;
            return true;
        }
        if( job.chunkUpsertIndex < job.upsert.length ) {
            job.chunkUpsert.push(job.upsert[job.chunkUpsertIndex++]);
            job.chunkLength++;
            return true;
        }
        if( job.orderChanged && job.chunkOrderIndex < job.nextEntries.length ) {
            job.chunkOrder.push(job.nextEntries[job.chunkOrderIndex++].key);
            job.chunkLength++;
            return true;
        }
        return false;
    }

    function postStageChunk(job) {
        const index = job.sendIndex++;
        const chunk = {
            transaction: job.transaction,
            index,
            total: job.wireTotal,
            version: job.version,
            viewport: job.viewport,
            reset: job.reset,
            orderChanged: job.orderChanged,
            done: index === job.wireTotal - 1,
        };
        if( job.chunkRemove.length ) chunk.remove = job.chunkRemove;
        if( job.chunkUpsert.length ) chunk.upsert = job.chunkUpsert;
        if( job.chunkOrder.length ) chunk.order = job.chunkOrder;
        job.chunkRemove = [];
        job.chunkUpsert = [];
        job.chunkOrder = [];
        job.chunkLength = 0;
        post('stage', job.session.id, { chunk });
    }

    function commitStageProjection(job) {
        job.session.stage = job.next;
        job.session.treeRevision = job.treeDelta?.revision ?? job.session.host.commitRevision ??
            job.session.treeRevision;
        job.session.lastViewportWidth = job.viewport.width;
        job.session.lastViewportHeight = job.viewport.height;
    }

    function restartFullStage(job) {
        job.dirtyKeys = null;
        job.phase = 'layout';
        job.layout = null;
        job.layoutIndex = 0;
        job.nextEntries = [];
        job.nextMap = new Map();
        job.nextKeys = [];
        job.removeIndex = 0;
        job.upsert = [];
        job.remove = [];
        job.orderChanged = Boolean(job.reset);
        job.dirtyProjected.clear();
    }

    async function initialize(message) {
        /* Init is also hot reload. Retire the old interaction state in FIFO
         * order, but retain this worker's decoded HOST data and Emscripten
         * module caches across source saves. */
        disposeSession({ focus: true });
        const config = message.config || {};
        const session = {
            id: message.session,
            host: null,
            wasm: null,
            mode: 'unavailable',
            enginePlan: null,
            warnings: [],
            services: [],
            stage: null,
            treeRevision: 0,
            lastViewportWidth: -1,
            lastViewportHeight: -1,
        };
        current = session;
        try {
            const requestedEngine = workerEngineMode(config.engineMode ?? config.engine);
            /* TypeScript/auto selection decodes and audits the entire declared
             * hook closure before createHost can construct or mutate a tree.
             * The production-default WASM path deliberately skips this
             * migration preflight so incomplete TS decoding cannot regress C. */
            session.enginePlan = config.program?.available !== false &&
                Array.isArray(config.program?.scripts)
                ? requestedEngine === 'wasm'
                    ? defaultWasmEnginePlan()
                    : await prepareEngine(config.program, {
                        mode: requestedEngine,
                        hookEntryScriptIds: staticIRHookScriptIds(config.ir),
                    })
                : null;
            if( disposeStaleSession(session) ) return;
            if( session.enginePlan?.backend === 'wasm' && requestedEngine === 'auto' ) {
                const reason = session.enginePlan.reason === 'typescript-decode-failed'
                    ? session.enginePlan.decodeFailure?.message || 'TypeScript decode failed'
                    : 'the complete script closure is not reviewed for TypeScript';
                warning(session, `TypeScript CS2 auto-selection used C/WASM: ${reason}`);
            }
            const hostData = await loadHostData(config);
            if( disposeStaleSession(session) ) return;
            session.host = createHost(config.ir, {
                state: clientStateForRevision(config.program?.revision, config.state || {}),
                viewport: config.viewport,
                hostData,
                recordChanges: false,
                onService: (service) => session.services.push(service),
                invoke: (intent) => {
                    if( !session.wasm ) return 0;
                    try { return session.wasm.invokeIntent(intent); }
                    catch( error ) {
                        /* The migration backend is selected only after a
                         * fail-closed proof. A routing/execution miss means
                         * that proof was wrong; never turn it into a plausible
                         * zero result and commit a partially executed event. */
                        if( session.mode === 'typescript' ) throw error;
                        const engine = session.mode === 'typescript'
                            ? 'TypeScript CS2VM' : 'C CS2VM/WASM';
                        warning(session, `${engine}: ${error?.message || String(error)}`);
                        return 0;
                    }
                },
            });
            if( config.program?.available !== false && Array.isArray(config.program?.scripts) ) {
                const factory = session.enginePlan?.backend === 'typescript'
                    ? createTypeScript : createWasm;
                session.wasm = await factory({
                    program: config.program,
                    host: session.host,
                    plan: session.enginePlan,
                    moduleUrl: config.moduleUrl,
                    wasmUrl: config.wasmUrl,
                });
                session.mode = session.enginePlan?.backend || 'wasm';
            } else {
                for( const message of config.program?.warnings || [] ) warning(session, message);
                warning(session, 'Original CS2 bytecode is unavailable; scripts are not executed.');
                session.mode = 'static';
            }
            if( disposeStaleSession(session) ) return;
            const mounted = session.host.mount();
            const mountedTreeDelta = session.host.consumeTreeDelta?.() || null;
            beginStageUpdate(session, {
                reset: true, treeDelta: mountedTreeDelta,
                onDone: () => {
                    if( current !== session ) return;
                    post('ready', session.id, {
                        mode: session.mode,
                        engineSelection: publicEnginePlan(session.enginePlan),
                        warnings: [...session.warnings],
                        interaction: mounted.interaction,
                        services: session.services.splice(0),
                    });
                    scheduleQueuedMessages();
                },
                onError: (error) => {
                    failSession(session, error);
                },
            });
        } catch( error ) {
            if( disposeStaleSession(session) ) return;
            failSession(session, error);
        }
    }

    function dispatch(message) {
        const session = current;
        if( !session?.host || session.id !== message.session ) return;
        if( dispatchJob ) {
            queuedMessages.push(message);
            return;
        }
        session.services.length = 0;
        const repeatCount = message.input?.type === 'key'
            ? Math.max(1, Math.min(4096, Number(message.input.repeatCount) || 1)) : 1;
        const input = repeatCount === 1 ? message.input : { ...message.input };
        if( repeatCount !== 1 ) delete input.repeatCount;
        dispatchJob = {
            session, message, input, repeatCount, repeat: 0, result: undefined,
            before: session.host.version,
            warningCount: session.warnings.length,
            maxTaskMs: 0,
        };
        pumpDispatch();
    }

    function pumpDispatch() {
        dispatchTimer = null;
        const job = dispatchJob;
        if( !job || current !== job.session ) return;
        const started = clock();
        try {
            /* One expanded browser event per worker task. A compressed key run
             * represents distinct native interactions; combining even two
             * makes the task bound depend on the sum of two otherwise-bounded
             * VM/HOST calls. */
            job.result = job.session.host.dispatch(job.input);
            job.repeat++;
            job.maxTaskMs = Math.max(job.maxTaskMs, clock() - started);
            if( job.repeat < job.repeatCount ) {
                dispatchTimer = schedule(pumpDispatch);
                return;
            }
            /* Send semantic input results first. A right-click menu can appear
             * without waiting for layout projection/diffing. FIFO worker
             * messages retain service and hook ordering. */
            post('result', job.session.id, {
                sequence: job.message.sequence,
                result: job.result,
                services: job.session.services.splice(0),
                warnings: job.session.warnings.slice(job.warningCount),
                timing: { maxDispatchTaskMs: job.maxTaskMs, taskCount: job.repeat },
            });
            /* Layout/diffing is a separate worker task. Keep this logical input
             * busy until it finishes so a tree/snapshot or following input can
             * never observe the middle of a repeat-compressed transaction. */
            if( job.session.host.version !== job.before ) {
                dispatchTimer = schedule(finishDispatchStage);
                return;
            }
            finishDispatch();
        } catch( error ) {
            job.maxTaskMs = Math.max(job.maxTaskMs, clock() - started);
            post('result', job.session.id, {
                sequence: job.message.sequence,
                error: serializeError(error),
                services: job.session.services.splice(0),
                warnings: job.session.warnings.slice(job.warningCount),
                timing: { maxDispatchTaskMs: job.maxTaskMs, taskCount: job.repeat + 1 },
            });
            finishDispatch();
        }
    }

    function finishDispatchStage() {
        dispatchTimer = null;
        const job = dispatchJob;
        if( !job || current !== job.session ) return;
        try {
            const treeDelta = job.session.host.consumeTreeDelta?.() || null;
            if( treeDelta?.projection === 'none' ) {
                if( job.session.stage ) job.session.stage = {
                    ...job.session.stage, version: Number(job.session.host.version) || 0,
                };
                job.session.treeRevision = treeDelta.revision;
                finishDispatch();
                return;
            }
            beginStageUpdate(job.session, {
                sequence: job.message.sequence,
                treeDelta,
                onDone: finishDispatch,
                /* A second failure after the bounded full retry is terminal.
                 * Continuing would strand the already-consumed renderer
                 * revision behind an observably stale React stage. */
                onError: (error) => failSession(job.session, error),
            });
        } catch( error ) {
            warning(job.session, error);
            finishDispatch();
        }
    }

    function finishDispatch() {
        dispatchJob = null;
        scheduleQueuedMessages();
    }

    function scheduleQueuedMessages() {
        if( queuedMessages.length && queuedTimer === null )
            queuedTimer = schedule(pumpQueuedMessage);
    }

    function pumpQueuedMessage() {
        queuedTimer = null;
        if( dispatchJob || !queuedMessages.length ) return;
        const message = queuedMessages.shift();
        receive(message);
        if( !dispatchJob && queuedMessages.length && queuedTimer === null )
            queuedTimer = schedule(pumpQueuedMessage);
    }

    function requestTree(message) {
        const session = current;
        if( !session?.host || session.id !== message.session ) return;
        stopTree(true);
        /* layout() is cached after stageUpdate. Slice and post at most 64 boxes
         * per task so queued interactions can run between tree chunks. */
        treeJob = {
            session,
            requestId: message.requestId,
            version: session.host.version,
            boxes: session.host.layout(),
            index: 0,
        };
        post('tree', session.id, {
            requestId: treeJob.requestId, version: treeJob.version,
            index: 0, total: treeJob.boxes.length, boxes: [], begin: true, done: false,
        });
        treeTimer = schedule(pumpTree);
    }

    function pumpTree() {
        treeTimer = null;
        const job = treeJob;
        if( !job || current !== job.session ) return;
        if( job.session.host.version !== job.version ) {
            post('tree', job.session.id, {
                requestId: job.requestId, version: job.version,
                index: job.index, total: job.boxes.length, boxes: [],
                stale: true, done: true,
            });
            treeJob = null;
            return;
        }
        const index = job.index;
        const boxes = job.boxes.slice(index, index + RUNTIME_WORKER_TREE_CHUNK);
        job.index += boxes.length;
        const done = job.index >= job.boxes.length;
        post('tree', job.session.id, {
            requestId: job.requestId, version: job.version,
            index, total: job.boxes.length, boxes, done,
        });
        if( done ) treeJob = null;
        else treeTimer = schedule(pumpTree);
    }

    function snapshot(message) {
        const session = current;
        if( !session?.host || session.id !== message.session ) return;
        try {
            post('snapshot', session.id, {
                requestId: message.requestId,
                snapshot: session.host.snapshot(),
            });
        } catch( error ) {
            post('snapshot', session.id, {
                requestId: message.requestId, error: serializeError(error),
            });
        }
    }

    async function receive(value) {
        if( !validWorkerMessage(value) ) return;
        if( value.type === 'init' ) return initialize(value);
        if( !current || current.id !== value.session ) return;
        if( (dispatchJob || stageJob) && ['dispatch', 'tree', 'snapshot'].includes(value.type) ) {
            queuedMessages.push(value);
            return;
        }
        if( value.type === 'dispatch' ) return dispatch(value);
        if( value.type === 'tree' ) return requestTree(value);
        if( value.type === 'snapshot' ) return snapshot(value);
        if( value.type === 'dispose' ) disposeSession({ focus: true });
    }

    return { receive, dispose: () => disposeSession({ focus: true }) };
}

async function loadHostData(config) {
    if( config.hostData ) return config.hostData;
    const url = config.hostDataUrl;
    if( !url ) return null;
    let pending = HOST_DATA_CACHE.get(url);
    if( !pending ) {
        pending = fetch(url).then((response) => {
            if( !response.ok ) throw new Error(`HOST data request failed (${response.status})`);
            return response.json();
        }).catch((error) => {
            HOST_DATA_CACHE.delete(url);
            throw error;
        });
        HOST_DATA_CACHE.set(url, pending);
    }
    return pending;
}

function workerEngineMode(value) {
    const mode = value ?? 'wasm';
    if( mode === 'wasm' || mode === 'typescript' || mode === 'auto' ) return mode;
    const error = new Error(
        `unknown CS2 engine mode ${String(value)}; expected wasm, typescript, or auto`);
    error.code = 'BAD_ENGINE_MODE';
    throw error;
}

function defaultWasmEnginePlan() {
    return Object.freeze({
        requestedMode: 'wasm', backend: 'wasm', reason: 'production-default',
        registry: null, coverage: null, decodeFailure: null,
    });
}

async function loadTypeScriptEngineModule() {
    if( !typescriptEngineModulePromise )
        typescriptEngineModulePromise = import(TYPESCRIPT_ENGINE_MODULE);
    try { return await typescriptEngineModulePromise; }
    catch( error ) {
        typescriptEngineModulePromise = null;
        throw error;
    }
}

async function defaultPrepareEngine(program, options) {
    const module = await loadTypeScriptEngineModule();
    return module.prepareCS2EnginePlan(program, options);
}

async function defaultCreateTypeScript(options) {
    const module = await loadTypeScriptEngineModule();
    return module.createTypeScriptCS2Runtime(options);
}

function publicEnginePlan(plan) {
    if( !plan ) return null;
    const coverage = plan.coverage ? {
        supported: Boolean(plan.coverage.supported),
        dialect: plan.coverage.dialect,
        programEntryScriptIds: [...(plan.coverage.programEntryScriptIds || [])],
        hookEntryScriptIds: [...(plan.coverage.hookEntryScriptIds || [])],
        entryScriptIds: [...plan.coverage.entryScriptIds],
        scriptIds: [...plan.coverage.scriptIds],
        unsupportedCoreOpcodes: [...plan.coverage.unsupportedCoreOpcodes],
        unreviewedHostOpcodes: [...plan.coverage.unreviewedHostOpcodes],
        unimplementedHostOpcodes: [...plan.coverage.unimplementedHostOpcodes],
        unresolvedDynamicHookOpcodes:
            [...(plan.coverage.unresolvedDynamicHookOpcodes || [])],
        unresolvedDynamicHookSourceScriptIds:
            [...(plan.coverage.unresolvedDynamicHookSourceScriptIds || [])],
        unresolvedInterfaceGroupOpcodes:
            [...(plan.coverage.unresolvedInterfaceGroupOpcodes || [])],
        allReferencedInterfaceGroupsPreloaded:
            Boolean(plan.coverage.allReferencedInterfaceGroupsPreloaded),
        unknownOpcodes: [...plan.coverage.unknownOpcodes],
        missingScriptIds: [...plan.coverage.missingScriptIds],
        missingEntryPoints: Boolean(plan.coverage.missingEntryPoints),
    } : null;
    return {
        requestedMode: plan.requestedMode,
        backend: plan.backend,
        reason: plan.reason,
        coverage,
        decodeFailure: plan.decodeFailure ? { ...plan.decodeFailure } : null,
    };
}

/**
 * Collect every cache/static hook which HostRuntime can invoke immediately
 * after construction. Both imported `{script:{id}}` records and compact
 * `{scriptId}` bindings occur in real IR. Null/sentinel hooks are inert; a
 * malformed positive id is rejected before Host construction rather than
 * silently escaping the whole-closure selector.
 */
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
                if( !Number.isSafeInteger(id) || id > 0x7fffffff ) {
                    const error = new Error(`invalid static hook clientscript id ${String(raw)}`);
                    error.code = 'BAD_ENTRY_SCRIPT';
                    throw error;
                }
                result.add(id);
            }
        }
    }
    return [...result].sort((left, right) => left - right);
}

function incrementalStageDirtyKeys(delta) {
    if( delta?.schema !== 'cs2dom-tree-delta/1' || delta.projection !== 'dirty' ||
        !delta.dirty || typeof delta.dirty !== 'object' ) return null;
    const allowed = new Set(['paint', 'interaction']);
    for( const [category, values] of Object.entries(delta.dirty) ) {
        if( !Array.isArray(values) ) return null;
        if( !allowed.has(category) && values.length ) return null;
    }
    const keys = [];
    const seen = new Set();
    for( const category of allowed ) for( const rawKey of delta.dirty[category] || [] ) {
        const key = String(rawKey);
        if( !key || seen.has(key) ) continue;
        seen.add(key);
        keys.push(key);
    }
    return keys;
}

function serializeError(error) {
    return {
        name: error?.name || 'Error',
        message: error?.message || String(error),
        code: error?.code || null,
        stack: error?.stack || null,
    };
}

/* Do not install the endpoint merely because this module was imported by a
 * browser page or a Node test. DedicatedWorkerGlobalScope is the positive gate. */
if( typeof DedicatedWorkerGlobalScope !== 'undefined' &&
    globalThis instanceof DedicatedWorkerGlobalScope ) {
    const endpoint = createRuntimeWorkerEndpoint({ send: (message) => globalThis.postMessage(message) });
    globalThis.onmessage = (event) => endpoint.receive(event.data);
}
