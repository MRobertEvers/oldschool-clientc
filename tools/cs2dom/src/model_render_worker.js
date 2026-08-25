/*
 * Dedicated toridraw owner. Model fetch/decode, animation timing and the
 * synchronous soft-raster loop all stay off the browser's input thread.
 *
 * This module deliberately contains no DOM dependencies so its endpoint can
 * be driven by deterministic Node tests with a fake renderer.
 */

import {
    MODEL_RENDER_WORKER_SCHEMA, validModelRenderMessage,
} from './model_render_protocol.js';

export { MODEL_RENDER_WORKER_SCHEMA } from './model_render_protocol.js';

const MODEL_CACHE = new Map();
const ANIMATION_CACHE = new Map();
const UNPREPARED_ASSET = Symbol('unprepared model-render asset');
const NO_ANIMATION_TIMING = Object.freeze({
    delays: Object.freeze([]),
    cycle: 0,
});

export function createModelRenderWorkerEndpoint({
    send,
    fetchImpl = (...args) => fetch(...args),
    createRenderer = createToridrawRenderer,
    clock = defaultClock,
    animationClock = () => Date.now(),
    makeBitmap = makeTransferableBitmap,
    canMakeBitmap = supportsTransferableBitmap,
} = {}) {
    if( typeof send !== 'function' ) throw new TypeError('model render worker requires send()');

    const latestByOwner = new Map();
    /* These identities describe what is actually installed in this endpoint's
     * stateful WASM renderer. They must not live in the URL fetch caches: two
     * endpoints can share fetched bytes but never share toridraw state. */
    const rendererState = {
        model: UNPREPARED_ASSET,
        textures: UNPREPARED_ASSET,
        animation: UNPREPARED_ASSET,
        animationTiming: NO_ANIMATION_TIMING,
    };
    let rendererPromise = null;
    let rasterQueue = Promise.resolve();

    function current(message) {
        return latestByOwner.get(message.owner) === message.requestId;
    }

    function post(type, message, fields = {}, transfer = []) {
        send({
            schema: MODEL_RENDER_WORKER_SCHEMA,
            type,
            requestId: message.requestId,
            owner: message.owner,
            token: message.token,
            ...fields,
        }, transfer);
    }

    async function render(message) {
        latestByOwner.set(message.owner, message.requestId);
        try {
            /* Fetches for independent widgets overlap. Only the stateful WASM
             * mutation/raster section is serialized. A superseded request is
             * discarded before it ever reaches toridraw whenever possible. */
            const [model, animation] = await Promise.all([
                fetchModel(message.modelUrl, fetchImpl),
                message.animationUrl
                    ? fetchAnimation(message.animationUrl, fetchImpl) : Promise.resolve(null),
            ]);
            if( !current(message) ) return;
            if( !rendererPromise ) rendererPromise = createRenderer();
            const renderer = await rendererPromise;
            if( !current(message) ) return;

            const task = rasterQueue.then(() => {
                if( !current(message) ) return null;
                const started = clock();
                const bitmapFastPath = Boolean(message.preferBitmap && canMakeBitmap());
                let renderRequest = bitmapFastPath ? message : scaleFallbackRequest(message);
                prepareRenderer(renderer, rendererState, model, animation);
                let result = rasterize(
                    renderer, rendererState.animationTiming,
                    renderRequest, animationClock());
                if( !current(message) ) return null;
                let bitmap = bitmapFastPath
                    ? makeBitmap(framebufferView(renderer, result),
                        result.width, result.height) : null;
                /* Capability probing can still race a lost canvas context. In
                 * that exceptional case rerender at the bounded fallback size
                 * before transferring bytes; never send a 1024-square 4 MiB
                 * frame to the browser input thread. */
                if( bitmapFastPath && !bitmap ) {
                    renderRequest = scaleFallbackRequest(message);
                    if( renderRequest !== message ) result = rasterize(
                        renderer, rendererState.animationTiming,
                        renderRequest, animationClock());
                }
                const output = {
                    width: result.width,
                    height: result.height,
                    wait: result.wait,
                    renderMs: clock() - started,
                };
                if( bitmap ) {
                    output.bitmap = bitmap;
                    post('rendered', message, output, [bitmap]);
                } else {
                    /* A WASM heap must never be transferred (and detached).
                     * Allocate the standalone copy only for the RGBA fallback;
                     * the normal ImageBitmap path reads the heap view directly. */
                    const buffer = copyFramebuffer(renderer, result).buffer;
                    output.rgba = buffer;
                    post('rendered', message, output, [buffer]);
                }
                return result;
            });
            /* A failed request cannot poison the serialization tail. */
            rasterQueue = task.catch(() => null);
            await task;
        } catch( error ) {
            if( current(message) ) post('error', message, { error: serializeError(error) });
        }
    }

    function receive(message) {
        if( !validModelRenderMessage(message) ) return Promise.resolve();
        if( message.type === 'render' ) return render(message);
        if( message.type === 'cancel' &&
            latestByOwner.get(message.owner) === message.requestId )
            latestByOwner.delete(message.owner);
        if( message.type === 'clear' ) latestByOwner.clear();
        return Promise.resolve();
    }

    return { receive, dispose: () => latestByOwner.clear() };
}

function prepareRenderer(renderer, state, model, animation) {
    if( state.model !== model ) {
        const faces = pushWasm(renderer, model.bytes, (ptr, size) =>
            model.hd ? renderer.setModelHd(ptr, size) : renderer.setModel(ptr, size));
        if( !faces ) throw new Error('model decode failed');
        if( !model.hd ) renderer.clearModelHd();
        state.model = model;
        /* Both resources may be interpreted relative to the decoded model.
         * Force their reinstall after a model switch even when their fetched
         * byte identities happen to be shared. */
        state.textures = UNPREPARED_ASSET;
        state.animation = UNPREPARED_ASSET;
        state.animationTiming = NO_ANIMATION_TIMING;
    }
    if( state.textures !== model.textures ) {
        pushWasm(renderer, model.textures,
            (ptr, size) => renderer.setTextures(ptr, size));
        state.textures = model.textures;
    }
    if( state.animation !== animation ) {
        renderer.clearAnim();
        const animated = animation &&
            pushWasm(renderer, animation,
                (ptr, size) => renderer.setAnim(ptr, size)) > 0;
        state.animationTiming = animated
            ? readAnimationTiming(renderer) : NO_ANIMATION_TIMING;
        state.animation = animation;
    }
}

function rasterize(renderer, animationTiming, request, now) {
    const timing = currentAnimationFrame(
        animationTiming, request.startedAt, now);
    const width = clampDimension(request.width);
    const height = clampDimension(request.height);
    const ptr = renderer.renderWidget(
        width, height,
        integer(request.widgetX), integer(request.widgetY),
        integer(request.widgetWidth), integer(request.widgetHeight),
        Math.max(1, integer(request.zoom)), integer(request.xAngle),
        integer(request.yAngle), integer(request.zAngle),
        integer(request.xOffset), integer(request.yOffset),
        Boolean(request.orthographic), Boolean(request.fixedZoom),
        Boolean(request.composed), timing.frame);
    if( !ptr ) throw new Error('model widget render failed');
    return { ptr, width, height, wait: timing.wait };
}

function readAnimationTiming(renderer) {
    const count = Math.max(0, renderer.frameCount() | 0);
    if( count === 0 ) return NO_ANIMATION_TIMING;
    const delays = new Array(count);
    let cycle = 0;
    for( let index = 0; index < count; index++ ) {
        const delay = Math.max(1, renderer.frameDelay(index) | 0);
        delays[index] = delay;
        cycle += delay;
    }
    return { delays, cycle };
}

function currentAnimationFrame(timing, startedAt, now) {
    if( timing.cycle === 0 ) return { frame: -1, wait: 0 };
    const { delays, cycle } = timing;
    let tick = Math.floor(Math.max(0, now - (Number(startedAt) || now)) / 20) % cycle;
    for( let frame = 0; frame < delays.length; frame++ ) {
        if( tick < delays[frame] )
            return { frame, wait: Math.max(1, (delays[frame] - tick) * 20) };
        tick -= delays[frame];
    }
    return { frame: 0, wait: 20 };
}

function framebufferView(renderer, result) {
    const heap = renderer.mod.HEAPU8;
    return new Uint8ClampedArray(
        heap.buffer, heap.byteOffset + result.ptr, result.width * result.height * 4);
}

function copyFramebuffer(renderer, result) {
    const source = framebufferView(renderer, result);
    const copy = new Uint8ClampedArray(source.length);
    copy.set(source);
    return copy;
}

function pushWasm(renderer, bytes, fn) {
    const ptr = renderer.alloc(bytes.length || 1);
    if( !ptr ) return 0;
    if( bytes.length ) renderer.mod.HEAPU8.set(bytes, ptr);
    try { return fn(ptr, bytes.length); }
    finally { renderer.release(ptr); }
}

async function fetchModel(url, fetchImpl) {
    let pending = MODEL_CACHE.get(url);
    if( pending ) return pending;
    pending = (async () => {
        const response = await fetchImpl(url);
        if( !response.ok ) throw new Error((await response.text()) || 'model not found');
        const bytes = new Uint8Array(await response.arrayBuffer());
        const textureIds = response.headers?.get?.('X-Texture-Ids');
        let textures = new Uint8Array(0);
        if( textureIds ) {
            const textureResponse = await fetchImpl(
                '/model/textures.bin?ids=' + encodeURIComponent(textureIds));
            if( textureResponse.ok ) textures = new Uint8Array(await textureResponse.arrayBuffer());
        }
        return {
            bytes,
            textures,
            hd: bytes.length >= 4 && bytes[0] === 0x45 && bytes[1] === 0x56 &&
                bytes[2] === 0x48 && bytes[3] === 0x31,
        };
    })().catch((error) => {
        MODEL_CACHE.delete(url);
        throw error;
    });
    MODEL_CACHE.set(url, pending);
    return pending;
}

async function fetchAnimation(url, fetchImpl) {
    let pending = ANIMATION_CACHE.get(url);
    if( pending ) return pending;
    pending = fetchImpl(url).then(async (response) => {
        if( response.status === 404 ) return null;
        if( !response.ok ) throw new Error((await response.text()) || 'animation not found');
        return new Uint8Array(await response.arrayBuffer());
    }).catch((error) => {
        ANIMATION_CACHE.delete(url);
        throw error;
    });
    ANIMATION_CACHE.set(url, pending);
    return pending;
}

async function createToridrawRenderer() {
    /* The dev server exposes the generated Emscripten file with one appended
     * ESM export. It is imported only here, never parsed or initialized by the
     * main document. */
    const { EVModule } = await import('/toridraw/ev_wasm_module.js');
    const mod = await EVModule({ locateFile: () => '/toridraw/ev_wasm.wasm' });
    const wrap = (name, result, args) => mod.cwrap(name, result, args);
    const renderer = {
        mod,
        init: wrap('ev_w_init', null, []),
        alloc: wrap('ev_w_alloc', 'number', ['number']),
        release: wrap('ev_w_release', null, ['number']),
        setModel: wrap('ev_w_set_model', 'number', ['number', 'number']),
        setModelHd: wrap('ev_w_set_model_hd', 'number', ['number', 'number']),
        clearModelHd: wrap('ev_w_clear_model_hd', null, []),
        setTextures: wrap('ev_w_set_textures', 'number', ['number', 'number']),
        setAnim: wrap('ev_w_set_anim', 'number', ['number', 'number']),
        clearAnim: wrap('ev_w_clear_anim', null, []),
        frameCount: wrap('ev_w_frame_count', 'number', []),
        frameDelay: wrap('ev_w_frame_delay', 'number', ['number']),
        renderWidget: wrap('ev_w_render_widget', 'number', Array(16).fill('number')),
    };
    renderer.init();
    return renderer;
}

function makeTransferableBitmap(rgba, width, height) {
    if( !supportsTransferableBitmap() ) return null;
    const canvas = new OffscreenCanvas(width, height);
    const context = canvas.getContext('2d');
    if( !context || typeof canvas.transferToImageBitmap !== 'function' ) return null;
    context.putImageData(new ImageData(rgba, width, height), 0, 0);
    return canvas.transferToImageBitmap();
}

function supportsTransferableBitmap() {
    return typeof OffscreenCanvas === 'function' && typeof ImageData === 'function' &&
        typeof OffscreenCanvas.prototype?.transferToImageBitmap === 'function';
}

function scaleFallbackRequest(request) {
    const maximum = Math.max(64, Math.min(512,
        integer(request.fallbackMaxDimension) || 512));
    const width = clampDimension(request.width);
    const height = clampDimension(request.height);
    const scale = Math.min(1, maximum / Math.max(width, height));
    if( scale === 1 ) return request;
    const scaled = (value) => Math.round(integer(value) * scale);
    return {
        ...request,
        width: Math.max(1, Math.round(width * scale)),
        height: Math.max(1, Math.round(height * scale)),
        widgetX: scaled(request.widgetX),
        widgetY: scaled(request.widgetY),
        widgetWidth: Math.max(1, scaled(request.widgetWidth)),
        widgetHeight: Math.max(1, scaled(request.widgetHeight)),
        zoom: Math.max(1, scaled(request.zoom)),
        xOffset: scaled(request.xOffset),
        yOffset: scaled(request.yOffset),
    };
}

function clampDimension(value) {
    return Math.max(1, Math.min(1024, integer(value)));
}

function integer(value) {
    return Number(value) | 0;
}

function defaultClock() {
    return typeof performance !== 'undefined' ? performance.now() : Date.now();
}

function serializeError(error) {
    return {
        name: error?.name || 'Error',
        message: error?.message || String(error),
        stack: error?.stack || null,
    };
}

if( typeof DedicatedWorkerGlobalScope !== 'undefined' &&
    globalThis instanceof DedicatedWorkerGlobalScope ) {
    const endpoint = createModelRenderWorkerEndpoint({
        send: (message, transfer) => globalThis.postMessage(message, transfer),
    });
    globalThis.onmessage = (event) => endpoint.receive(event.data);
}
