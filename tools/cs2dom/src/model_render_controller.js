/* Main-thread, constant-work handoff for model_render_worker.js. */

import {
    MODEL_RENDER_WORKER_SCHEMA, validModelRenderMessage,
} from './model_render_protocol.js';

export function createModelRenderController(options = {}) {
    return new ModelRenderController(options);
}

export class ModelRenderController {
    constructor({
        workerFactory = defaultWorkerFactory,
        workerUrl = '/runtime/model_render_worker.js',
        interactionBudgetMs = 10,
        clock = defaultClock,
        epochClock = () => Date.now(),
        onBudgetViolation = null,
    } = {}) {
        this.worker = workerFactory(workerUrl);
        this.interactionBudgetMs = interactionBudgetMs;
        this.clock = clock;
        this.epochClock = epochClock;
        this.onBudgetViolation = onBudgetViolation;
        this.nextRequestId = 0;
        this.latestByOwner = new Map();
        this.completions = new Map();
        this.disposed = false;
        this.worker.onmessage = (event) => this._receive(event.data);
        this.worker.onerror = (event) => this._failAll(
            new Error(event?.message || 'model render worker failed'));
    }

    render(request) {
        const started = this.clock();
        if( this.disposed ) throw new Error('model render worker is disposed');
        const requestId = ++this.nextRequestId;
        const owner = String(request.owner);
        const previous = this.latestByOwner.get(owner);
        if( previous ) this._retire(previous, requestId);

        let resolve;
        let reject;
        const completion = new Promise((accept, decline) => { resolve = accept; reject = decline; });
        const item = { requestId, owner, token: request.token, resolve, reject };
        this.latestByOwner.set(owner, item);
        this.completions.set(requestId, item);
        this.worker.postMessage({
            schema: MODEL_RENDER_WORKER_SCHEMA,
            type: 'render',
            requestId,
            owner,
            token: request.token,
            modelUrl: String(request.modelUrl),
            animationUrl: request.animationUrl ? String(request.animationUrl) : null,
            startedAt: Number(request.startedAt) || this.epochClock(),
            width: request.width | 0,
            height: request.height | 0,
            widgetX: request.widgetX | 0,
            widgetY: request.widgetY | 0,
            widgetWidth: request.widgetWidth | 0,
            widgetHeight: request.widgetHeight | 0,
            zoom: request.zoom | 0,
            xAngle: request.xAngle | 0,
            yAngle: request.yAngle | 0,
            zAngle: request.zAngle | 0,
            xOffset: request.xOffset | 0,
            yOffset: request.yOffset | 0,
            orthographic: Boolean(request.orthographic),
            fixedZoom: Boolean(request.fixedZoom),
            composed: Boolean(request.composed),
            preferBitmap: Boolean(request.preferBitmap),
            fallbackMaxDimension: Math.max(64, Math.min(512,
                request.fallbackMaxDimension | 0 || 512)),
        });
        const enqueueMs = this.clock() - started;
        if( enqueueMs >= this.interactionBudgetMs ) this.onBudgetViolation?.({
            owner, requestId, elapsed: enqueueMs, budget: this.interactionBudgetMs,
        });
        return { requestId, completion, enqueueMs };
    }

    cancel(owner) {
        owner = String(owner);
        const item = this.latestByOwner.get(owner);
        if( !item ) return;
        this.latestByOwner.delete(owner);
        this.completions.delete(item.requestId);
        item.resolve({ requestId: item.requestId, owner, stale: true });
        this.worker.postMessage({
            schema: MODEL_RENDER_WORKER_SCHEMA,
            type: 'cancel', requestId: item.requestId, owner, token: item.token,
        });
    }

    clear() {
        for( const owner of [...this.latestByOwner.keys()] ) this.cancel(owner);
    }

    dispose() {
        if( this.disposed ) return;
        this.disposed = true;
        this._failAll(new Error('model render worker was disposed'));
        this.worker.terminate?.();
    }

    _retire(item, replacement) {
        this.completions.delete(item.requestId);
        item.resolve({
            requestId: item.requestId, owner: item.owner,
            stale: true, replacedBy: replacement,
        });
    }

    _receive(message) {
        if( !validModelRenderMessage(message) ) return;
        const item = this.completions.get(message.requestId);
        if( !item || this.latestByOwner.get(item.owner) !== item ) {
            message.bitmap?.close?.();
            return;
        }
        this.completions.delete(item.requestId);
        this.latestByOwner.delete(item.owner);
        if( message.type === 'error' ) item.reject(remoteError(message.error));
        else if( message.type === 'rendered' ) item.resolve({
            requestId: item.requestId,
            owner: item.owner,
            token: message.token,
            width: message.width,
            height: message.height,
            wait: message.wait,
            renderMs: message.renderMs,
            rgba: message.rgba,
            bitmap: message.bitmap,
        });
    }

    _failAll(error) {
        for( const item of this.completions.values() ) item.reject(error);
        this.completions.clear();
        this.latestByOwner.clear();
    }
}

function defaultWorkerFactory(url) {
    return new Worker(url, { type: 'module', name: 'cs2dom-model-render' });
}

function defaultClock() {
    return typeof performance !== 'undefined' ? performance.now() : Date.now();
}

function remoteError(value) {
    const error = new Error(value?.message || 'model render failed');
    error.name = value?.name || 'Error';
    if( value?.stack ) error.stack = value.stack;
    return error;
}
