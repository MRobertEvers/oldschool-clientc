/* Main-thread controller for runtime_worker.js. Input handlers enqueue only a
 * tiny structured-clone record; C/WASM execution, HOST calls and layout stay in
 * the dedicated worker. */

import {
    RUNTIME_WORKER_SCHEMA, validWorkerMessage, workerMessage,
} from './runtime_worker_protocol.js';

const COALESCE = new Set(['pointer_move', 'tick']);
const MAX_PENDING_NODES = 4096;
const MAX_AGGREGATE_EVENTS = 64;

export function createWorkerRuntimeController(options = {}) {
    return new WorkerRuntimeController(options);
}

export class WorkerRuntimeController {
    constructor({
        workerFactory = defaultWorkerFactory,
        workerUrl = '/runtime/runtime_worker.js',
        onStagePatch = null,
        onTreeChunk = null,
        onResult = null,
        onService = null,
        onWarning = null,
        onTiming = null,
        onReceiveTiming = null,
        onBudgetViolation = null,
        interactionBudgetMs = 10,
        clock = defaultClock,
    } = {}) {
        this.workerFactory = workerFactory;
        this.workerUrl = workerUrl;
        this.callbacks = { onStagePatch, onTreeChunk, onResult, onService, onWarning,
            onTiming, onReceiveTiming, onBudgetViolation };
        this.interactionBudgetMs = interactionBudgetMs;
        this.clock = clock;
        this.worker = null;
        this.session = 0;
        this.sequence = 0;
        this.requestId = 0;
        this.readyState = 'idle';
        this.mode = 'unavailable';
        this.warnings = [];
        this.interaction = null;
        this.currentRender = null;
        this.stageMap = new Map();
        this.stageIndex = new Map();
        this.stageTransaction = null;
        this.inFlight = null;
        this.pendingHead = null;
        this.pendingTail = null;
        this.pendingCount = 0;
        this.pendingEventCount = 0;
        this.completions = new Map();
        this.requests = new Map();
        this.readyPromise = null;
        this.readyResolve = null;
        this.readyReject = null;
        this.receiveMetrics = { count: 0, maxMs: 0, overBudget: 0 };
    }

    start(config) {
        this._retireWorker(new Error('runtime session was replaced'));
        return this._begin(config, true);
    }

    /** Reuse the worker so parsed HOST data and the Emscripten module survive HMR. */
    reload(config) {
        if( !this.worker ) return this.start(config);
        this._retireSession(new Error('runtime session was replaced'));
        return this._begin(config, false);
    }

    /** Recovery escape hatch for a genuinely wedged or obsolete worker module. */
    hardRestart(config) {
        this._retireWorker(new Error('runtime worker was restarted'));
        return this._begin(config, true);
    }

    _begin(config, createWorker) {
        this.session++;
        const session = this.session;
        this.readyState = 'loading';
        this.mode = 'unavailable';
        this.warnings = [];
        this.interaction = null;
        this.currentRender = null;
        this.stageMap = new Map();
        this.stageIndex = new Map();
        this.stageTransaction = null;
        if( createWorker ) {
            this.worker = this.workerFactory(this.workerUrl);
            this.worker.onmessage = (event) => this._receive(event.data);
            this.worker.onerror = (event) => {
                const error = new Error(event?.message || 'runtime worker failed');
                this.readyState = 'failed';
                this.mode = 'unavailable';
                this._retireWorker(error);
                this.callbacks.onWarning?.(error.message);
            };
        }
        this.readyPromise = new Promise((resolve, reject) => {
            this.readyResolve = resolve;
            this.readyReject = reject;
        });
        this.worker.postMessage(workerMessage('init', session, { config }));
        return this.readyPromise;
    }

    /**
     * Return before any C/JS work runs. `completion` settles in original input
     * order; consumers needing a minimenu await the pointer_down completion.
     */
    dispatch(input) {
        const started = this.clock();
        const sequence = ++this.sequence;
        let resolve;
        let reject;
        const completion = new Promise((accept, decline) => { resolve = accept; reject = decline; });
        const item = { sequence, input: { ...input }, resolve, reject };
        this.completions.set(sequence, item);
        this._enqueue(item);
        this._pump();
        const elapsed = this.clock() - started;
        if( elapsed >= this.interactionBudgetMs ) this.callbacks.onBudgetViolation?.({
            sequence, input, elapsed, budget: this.interactionBudgetMs,
            phase: 'enqueue', source: 'main-thread',
        });
        return { sequence, completion, enqueueMs: elapsed };
    }

    requestTree({ collect = true } = {}) {
        return this._request('tree', { collect });
    }

    /** Full HOST/session state is deliberately explicit and never piggybacks on input. */
    requestSnapshot() {
        return this._request('snapshot');
    }

    dispose() {
        if( this.worker ) {
            try { this.worker.postMessage(workerMessage('dispose', this.session)); }
            catch { /* termination below is authoritative */ }
        }
        this._retireWorker(new Error('runtime session was disposed'));
        this.readyState = 'disposed';
    }

    _enqueue(item) {
        const tail = this.pendingTail;
        if( COALESCE.has(item.input.type) ) {
            if( tail?.input?.type === item.input.type ) {
                this.completions.delete(tail.sequence);
                tail.resolve({ sequence: tail.sequence, coalesced: true, replacedBy: item.sequence });
                tail.sequence = item.sequence;
                tail.input = item.input;
                tail.resolve = item.resolve;
                tail.reject = item.reject;
                return;
            }
        }
        if( this.pendingEventCount >= MAX_PENDING_NODES ) {
            const error = new Error('runtime input backlog exceeded 4096 ordered events');
            this.completions.delete(item.sequence);
            item.reject(error);
            this.callbacks.onWarning?.(error.message);
            return;
        }
        /* Repeated key events remain semantically exact: the worker expands
         * repeatCount inside the ordered transaction, while one queue node and
         * one structured-clone message represent the run. */
        if( item.input.type === 'key' && tail?.input?.type === 'key' &&
            tail.input.keyTyped === item.input.keyTyped &&
            tail.input.keyPressed === item.input.keyPressed &&
            1 + (tail.followers?.length || 0) < MAX_AGGREGATE_EVENTS ) {
            tail.input.repeatCount = (tail.input.repeatCount || 1) + 1;
            (tail.followers ||= []).push(item);
            this.pendingEventCount++;
            return;
        }
        if( this.pendingCount >= MAX_PENDING_NODES ) {
            const error = new Error('runtime input backlog exceeded 4096 ordered events');
            this.completions.delete(item.sequence);
            item.reject(error);
            this.callbacks.onWarning?.(error.message);
            return;
        }
        item.next = null;
        if( this.pendingTail ) this.pendingTail.next = item;
        else this.pendingHead = item;
        this.pendingTail = item;
        this.pendingCount++;
        this.pendingEventCount++;
    }

    _pump() {
        if( !this.worker || this.readyState !== 'ready' || this.inFlight || !this.pendingHead ) return;
        const item = this.pendingHead;
        this.pendingHead = item.next;
        if( !this.pendingHead ) this.pendingTail = null;
        item.next = null;
        this.pendingCount--;
        this.inFlight = item;
        try {
            this.worker.postMessage(workerMessage('dispatch', this.session, {
                sequence: item.sequence, input: item.input,
            }));
        } catch( error ) {
            this.inFlight = null;
            const participants = [item, ...(item.followers || [])];
            this.pendingEventCount -= participants.length;
            for( const participant of participants ) {
                this.completions.delete(participant.sequence);
                participant.reject(error);
            }
            this._pump();
        }
    }

    _request(type, { collect = true } = {}) {
        if( !this.worker || this.readyState !== 'ready' )
            return Promise.reject(new Error('runtime worker is not ready'));
        const requestId = ++this.requestId;
        const promise = new Promise((resolve, reject) => {
            this.requests.set(requestId, {
                type, resolve, reject, collect: Boolean(collect),
                chunks: collect ? [] : null,
            });
        });
        this.worker.postMessage(workerMessage(type, this.session, { requestId }));
        return promise;
    }

    _receive(message) {
        const started = this.clock();
        try { return this._routeMessage(message); }
        finally {
            const elapsed = Math.max(0, this.clock() - started);
            this.receiveMetrics.count++;
            this.receiveMetrics.maxMs = Math.max(this.receiveMetrics.maxMs, elapsed);
            const timing = { type: message?.type || 'invalid', elapsed };
            this.callbacks.onReceiveTiming?.(timing);
            if( elapsed >= this.interactionBudgetMs ) {
                this.receiveMetrics.overBudget++;
                this.callbacks.onBudgetViolation?.({
                    sequence: message?.sequence ?? null,
                    elapsed, budget: this.interactionBudgetMs,
                    phase: 'controller-receive', source: 'main-thread',
                    messageType: timing.type,
                });
            }
        }
    }

    _routeMessage(message) {
        if( !validWorkerMessage(message) || message.session !== this.session ) return;
        if( message.type === 'stage' ) return this._stage(message.chunk);
        if( message.type === 'ready' ) {
            this.readyState = 'ready';
            this.mode = message.mode;
            this.interaction = message.interaction || null;
            this._appendWarnings(message.warnings);
            this._publishServices(message.services);
            const value = {
                mode: this.mode, warnings: [...this.warnings],
                render: this.currentRender, interaction: this.interaction,
            };
            this.readyResolve?.(value);
            this.readyResolve = this.readyReject = null;
            this._pump();
            return;
        }
        if( message.type === 'failed' ) {
            this._appendWarnings(message.warnings);
            this.readyState = 'failed';
            this._failReady(remoteError(message.error));
            return;
        }
        if( message.type === 'result' ) return this._result(message);
        if( message.type === 'timing' ) return this._timing(message);
        if( message.type === 'tree' ) return this._tree(message);
        if( message.type === 'snapshot' ) return this._snapshot(message);
    }

    _result(message) {
        const item = this.inFlight?.sequence === message.sequence ? this.inFlight
            : this.completions.get(message.sequence);
        if( !item ) return;
        this._publishTiming(message.sequence, 'dispatch', message.timing, item.input);
        const participants = [item, ...(item.followers || [])];
        this.pendingEventCount -= participants.length;
        for( const participant of participants ) this.completions.delete(participant.sequence);
        if( this.inFlight === item ) this.inFlight = null;
        this._appendWarnings(message.warnings);
        this._publishServices(message.services);
        if( message.result?.interaction ) this.interaction = message.result.interaction;
        if( message.error ) {
            const error = remoteError(message.error);
            for( const participant of participants ) participant.reject(error);
        }
        else {
            let value;
            for( const participant of participants ) {
                value = {
                    sequence: participant.sequence, result: message.result,
                    services: message.services || [], warnings: message.warnings || [],
                    timing: message.timing || null,
                };
                participant.resolve(value);
            }
            this.callbacks.onResult?.(value);
        }
        this._pump();
    }

    _timing(message) {
        this._publishTiming(message.sequence, 'stage', message.timing, null);
    }

    _publishTiming(sequence, phase, timing, input) {
        if( !timing || typeof timing !== 'object' ) return;
        const record = { sequence, phase, timing };
        this.callbacks.onTiming?.(record);
        const elapsed = Number(phase === 'stage'
            ? timing.maxStageTaskMs : timing.maxDispatchTaskMs);
        if( Number.isFinite(elapsed) && elapsed >= this.interactionBudgetMs )
            this.callbacks.onBudgetViolation?.({
                sequence, input, elapsed, budget: this.interactionBudgetMs,
                phase, source: 'runtime-worker', timing,
            });
    }

    _stage(chunk) {
        if( !chunk || !Number.isSafeInteger(chunk.transaction) ||
            !Number.isSafeInteger(chunk.index) ) return;
        const legacy = Array.isArray(chunk.operations);
        if( !legacy && ![chunk.remove, chunk.upsert, chunk.order]
            .every((values) => values === undefined || Array.isArray(values)) ) return;
        if( chunk.index === 0 ) {
            this.stageTransaction = {
                id: chunk.transaction,
                next: 0,
                map: chunk.reset ? new Map() : this.stageMap,
                index: chunk.orderChanged ? new Map() : this.stageIndex,
                entries: chunk.orderChanged ? [] : (this.currentRender?.entries || []).slice(),
                boxes: chunk.orderChanged ? [] : (this.currentRender?.boxes || []).slice(),
                upsertBatches: [],
                upsertCount: 0,
                remove: [],
                version: chunk.version,
                viewport: { ...chunk.viewport },
                reset: Boolean(chunk.reset),
                orderChanged: Boolean(chunk.orderChanged),
            };
        }
        const transaction = this.stageTransaction;
        if( !transaction || transaction.id !== chunk.transaction ||
            transaction.next !== chunk.index ) return;
        transaction.next++;
        if( legacy ) {
            let upsertBatch = null;
            for( const operation of chunk.operations ) {
                if( operation?.[0] === 0 ) {
                    transaction.map.delete(operation[1]);
                    transaction.remove.push(operation[1]);
                } else if( operation?.[0] === 1 ) {
                    transaction.map.set(operation[1].key, operation[1]);
                    (upsertBatch ||= []).push(operation[1]);
                    transaction.upsertCount++;
                    if( !transaction.orderChanged ) {
                        const index = transaction.index.get(operation[1].key);
                        if( index !== undefined ) {
                            transaction.entries[index] = operation[1];
                            transaction.boxes[index] = operation[1].box;
                        }
                    }
                } else if( operation?.[0] === 2 ) {
                    const entry = transaction.map.get(operation[1]);
                    if( entry ) {
                        transaction.index.set(operation[1], transaction.entries.length);
                        transaction.entries.push(entry);
                        transaction.boxes.push(entry.box);
                    }
                }
            }
            if( upsertBatch ) transaction.upsertBatches.push(upsertBatch);
        } else {
            for( const key of chunk.remove || [] ) {
                transaction.map.delete(key);
                transaction.remove.push(key);
            }
            const upsertBatch = chunk.upsert || [];
            for( const entry of upsertBatch ) {
                transaction.map.set(entry.key, entry);
                transaction.upsertCount++;
                if( !transaction.orderChanged ) {
                    const index = transaction.index.get(entry.key);
                    if( index !== undefined ) {
                        transaction.entries[index] = entry;
                        transaction.boxes[index] = entry.box;
                    }
                }
            }
            if( upsertBatch.length ) transaction.upsertBatches.push(upsertBatch);
            for( const key of chunk.order || [] ) {
                const entry = transaction.map.get(key);
                if( entry ) {
                    transaction.index.set(key, transaction.entries.length);
                    transaction.entries.push(entry);
                    transaction.boxes.push(entry.box);
                }
            }
        }
        if( !chunk.done ) return;
        this.stageMap = transaction.map;
        this.stageIndex = transaction.index;
        this.currentRender = {
            version: Number(transaction.version) || 0,
            viewport: transaction.viewport,
            entries: transaction.entries,
            boxes: transaction.boxes,
        };
        this.stageTransaction = null;
        const upsertBatches = transaction.upsertBatches;
        let flattenedUpsert = null;
        const patch = {
            version: this.currentRender.version,
            viewport: this.currentRender.viewport,
            reset: transaction.reset,
            transaction: transaction.id,
            orderChanged: transaction.orderChanged,
            upsertBatches,
            upsertCount: transaction.upsertCount,
            remove: transaction.remove,
            removeCount: transaction.remove.length,
            get upsert() {
                if( flattenedUpsert ) return flattenedUpsert;
                if( upsertBatches.length === 0 ) return [];
                if( upsertBatches.length === 1 ) return upsertBatches[0];
                flattenedUpsert = upsertBatches.flat();
                return flattenedUpsert;
            },
        };
        this.callbacks.onStagePatch?.({
            render: this.currentRender,
            patch,
        });
    }

    _tree(message) {
        const request = this.requests.get(message.requestId);
        if( !request || request.type !== 'tree' ) return;
        if( request.collect && message.begin ) request.chunks = [];
        if( request.collect && message.boxes?.length ) request.chunks.push(...message.boxes);
        this.callbacks.onTreeChunk?.(message);
        if( !message.done ) return;
        this.requests.delete(message.requestId);
        if( message.stale ) request.reject(new Error('tree changed while it was streaming'));
        else request.resolve({
            version: message.version,
            boxes: request.collect ? request.chunks : null,
        });
    }

    _snapshot(message) {
        const request = this.requests.get(message.requestId);
        if( !request || request.type !== 'snapshot' ) return;
        this.requests.delete(message.requestId);
        if( message.error ) request.reject(remoteError(message.error));
        else request.resolve(message.snapshot);
    }

    _appendWarnings(warnings) {
        for( const warning of warnings || [] ) if( warning && !this.warnings.includes(warning) ) {
            this.warnings.push(warning);
            this.callbacks.onWarning?.(warning);
        }
    }

    _publishServices(services) {
        for( const service of services || [] ) this.callbacks.onService?.(service);
    }

    _failReady(error) {
        this.readyReject?.(error);
        this.readyResolve = this.readyReject = null;
    }

    _retireWorker(reason) {
        try { this.worker?.terminate?.(); } catch { /* best effort */ }
        this.worker = null;
        this._retireSession(reason);
    }

    _retireSession(reason) {
        this.inFlight = null;
        this.pendingHead = this.pendingTail = null;
        this.pendingCount = 0;
        this.pendingEventCount = 0;
        for( const item of this.completions.values() ) item.reject(reason);
        this.completions.clear();
        for( const request of this.requests.values() ) request.reject(reason);
        this.requests.clear();
        this._failReady(reason);
    }
}

function defaultWorkerFactory(url) {
    return new Worker(url, { type: 'module', name: 'cs2dom-runtime' });
}

function defaultClock() {
    return typeof performance !== 'undefined' ? performance.now() : Date.now();
}

function remoteError(value) {
    const error = new Error(value?.message || 'runtime worker failed');
    error.name = value?.name || 'Error';
    if( value?.code ) error.code = value.code;
    if( value?.stack ) error.stack = value.stack;
    return error;
}

export { RUNTIME_WORKER_SCHEMA };
