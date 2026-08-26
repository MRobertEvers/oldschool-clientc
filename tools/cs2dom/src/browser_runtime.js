/*
 * The browser entry: a session on a canvas, driven by requestAnimationFrame.
 *
 * Everything below the canvas is the same code the Node tests drive — one
 * tree, one thread, one settlement loop. What this file adds is the three
 * things only a browser has: a real 2D context, a frame clock, and DOM events
 * normalised into the queue the session already understands.
 *
 * ------------------------------------------------------------------
 * The frame loop paints only when told to
 * ------------------------------------------------------------------
 *
 * `session.frame()` answers false for an idle interface and for one still
 * waiting on a load, and in both cases the previous canvas contents stand —
 * there is nothing to do, so nothing is done. That is the whole reason a
 * gameframe clock ticking every frame costs nothing here.
 *
 * ------------------------------------------------------------------
 * Input is queued, never handled inline
 * ------------------------------------------------------------------
 *
 * A DOM handler that ran a script would re-enter the VM from an event
 * callback, against a tree that may be mid-settlement. The listeners below do
 * nothing but normalise and post; the session hit-tests them at the point in
 * the frame where the tree is settled.
 *
 * One listener per event type at the CANVAS, not per widget: there are no
 * widget elements to attach to, which is most of why this design is cheap.
 */

import { createSession } from './session.js';
import { createCanvasSurface } from './painter.js';
import { BitmapFont, SpriteStore, FontStore, ModelStore } from './assets.js';
import { createLoader } from './asset_loader.js';
import { ScriptRegistry } from './cs2_driver.js';

/* -------------------------------------------------------------------------
 * Fetching assets
 * ---------------------------------------------------------------------- */

/**
 * The dev server's asset routes, as the stores' `decode` hooks.
 *
 * Each payload is SELF-DESCRIBING — canvas size and offset alongside the
 * image — because a bare bitmap is only half a sprite. The `pack.meta` beside
 * a sprite carries where it sits inside its cell, and a browser handed the
 * pixels alone draws every trimmed icon and offset glyph in the wrong place.
 */
export function createHttpAssetSource({ base = '' } = {}) {
    return {
        async sprite(id, frame = 0) {
            const payload = await getJson(`${base}/api/sprite?id=${id | 0}&frame=${frame | 0}`);
            if( !payload ) return null;
            return {
                width: payload.width, height: payload.height,
                offsetX: payload.offsetX, offsetY: payload.offsetY,
                bitmap: await decodeImage(payload.png),
            };
        },

        async font(id) {
            const payload = await getJson(`${base}/api/font?id=${id | 0}`);
            if( !payload ) return null;
            /* Advances arrive sparse — most of 256 are zero and sending them
             * would triple the payload — so the dense array is rebuilt here,
             * where the font wants to index it directly. */
            const advances = new Array(256).fill(0);
            for( const [code, value] of Object.entries(payload.advances ?? {}) )
                advances[Number(code)] = value;

            const glyphs = new Map();
            await Promise.all(Object.entries(payload.glyphs ?? {}).map(async ([code, glyph]) => {
                glyphs.set(Number(code), {
                    id: `${payload.id}:${code}`,
                    width: glyph.width, height: glyph.height,
                    offsetX: glyph.offsetX, offsetY: glyph.offsetY,
                    bitmap: await decodeImage(glyph.png),
                });
            }));

            return new BitmapFont({
                id: payload.id, ascent: payload.ascent, advances, glyphs,
                lineHeight: payload.lineHeight,
            });
        },

        /*
         * Models are toridraw's, and a page without its worker has none.
         * Answering null records the id as wanted and draws nothing, which is
         * visibly absent — an invented placeholder would look like a decision.
         * A caller with a worker supplies a `ToridrawModelSource` instead;
         * see src/model_source.js.
         */
        async model() { return null; },
    };
}

async function getJson(url) {
    try
    {
        const response = await fetch(url);
        return response.ok ? await response.json() : null;
    }
    catch { return null; }
}

/**
 * A data URL into something `drawImage` accepts.
 *
 * `createImageBitmap` where it exists: it decodes off the main thread and
 * hands back a form the compositor can blit directly, which matters when a
 * font is two hundred images.
 */
async function decodeImage(dataUrl) {
    if( typeof createImageBitmap === 'function' )
    {
        const blob = await (await fetch(dataUrl)).blob();
        return createImageBitmap(blob);
    }
    return new Promise((resolve, reject) => {
        const image = new Image();
        image.onload = () => resolve(image);
        image.onerror = () => reject(new Error('image decode failed'));
        image.src = dataUrl;
    });
}

export function mountInterface(options) {
    return new BrowserRuntime(options);
}

export class BrowserRuntime {
    constructor({
        canvas, cache, scripts = null, state = null, config = null, player = null,
        models = null, onFrame = null, onWarning = null, devicePixelRatio = null,
    } = {}) {
        if( !canvas ) throw new TypeError('a browser runtime needs a canvas');
        this.canvas = canvas;
        this.context = canvas.getContext('2d', { alpha: true });
        /* Nearest-neighbour: the cache's art is pixel art, and smoothing it
         * makes every sprite edge soft in a way no amount of scaling fixes. */
        this.context.imageSmoothingEnabled = false;

        const source = cache ?? createHttpAssetSource({});
        this.sprites = new SpriteStore({ decode: (id) => source.sprite(id) });
        this.fonts = new FontStore({ decode: (id) => source.font(id) });
        /* A toridraw source if one was supplied, otherwise the asset source's
         * own answer — which is "no models", honestly. */
        this.modelSource = models;
        this.models = new ModelStore({
            render: (id, pose) => (models ? models.render(id, pose) : source.model(id, pose)),
        });
        this.loader = createLoader({
            sprites: this.sprites, fonts: this.fonts, models: this.models,
            interfaces: source.interfaces ?? null,
            configs: source.configs ?? null,
            scripts: scripts ?? null,
            onWarning,
        });

        this.session = createSession({
            surface: createCanvasSurface(this.context),
            sprites: this.sprites, fonts: this.fonts, models: this.models,
            loader: this.loader, scripts: scripts ?? new ScriptRegistry(),
            state, config, player, onWarning,
            root: { x: 0, y: 0, width: canvas.width, height: canvas.height },
        });

        this.onFrame = onFrame;
        this.ratio = devicePixelRatio ?? (globalThis.devicePixelRatio || 1);
        this.running = false;
        this._raf = 0;
        this._listeners = [];
        this._attach();
    }

    /* --------------------------------------------------------------
     * The loop
     * ----------------------------------------------------------- */

    start() {
        if( this.running ) return;
        this.running = true;
        const tick = async (now) => {
            if( !this.running ) return;
            /*
             * `await` inside the frame is safe because `session.frame` never
             * blocks on I/O — a parked script returns false and the next frame
             * resumes it. The await is only for the microtask the settlement
             * loop yields on.
             */
            const painted = await this.session.frame(now);
            this.onFrame?.(painted, this.session);
            this._raf = requestAnimationFrame(tick);
        };
        this._raf = requestAnimationFrame(tick);
    }

    stop() {
        this.running = false;
        if( this._raf ) cancelAnimationFrame(this._raf);
        this._raf = 0;
    }

    /** Detach every listener. A session swap must not leave the old one live. */
    dispose() {
        this.stop();
        for( const [target, type, handler] of this._listeners )
            target.removeEventListener(type, handler);
        this._listeners.length = 0;
        /* The model worker outlives a session swap unless it is told not to. */
        this.modelSource?.dispose?.();
    }

    /* --------------------------------------------------------------
     * Sizing
     * ----------------------------------------------------------- */

    /**
     * Resize to a CSS size, accounting for device pixels.
     *
     * The interface is laid out in CSS pixels — the cache's coordinates are
     * what the scripts compute with — and the backing store is scaled up so
     * the pixel art is crisp on a retina display. Laying out in device pixels
     * instead would make every authored coordinate wrong by the ratio.
     */
    resize(cssWidth, cssHeight) {
        const width = Math.max(1, Math.floor(cssWidth));
        const height = Math.max(1, Math.floor(cssHeight));
        this.canvas.width = Math.floor(width * this.ratio);
        this.canvas.height = Math.floor(height * this.ratio);
        this.canvas.style.width = `${width}px`;
        this.canvas.style.height = `${height}px`;
        this.context.imageSmoothingEnabled = false;
        this.context.setTransform(this.ratio, 0, 0, this.ratio, 0, 0);
        return this.session.resize(width, height);
    }

    /* --------------------------------------------------------------
     * Input
     * ----------------------------------------------------------- */

    _attach() {
        const canvas = this.canvas;
        const point = (event) => {
            const box = canvas.getBoundingClientRect();
            return {
                x: Math.floor(event.clientX - box.left),
                y: Math.floor(event.clientY - box.top),
            };
        };

        this._on(canvas, 'pointermove', (event) =>
            this.session.post({ type: 'move', ...point(event) }));
        this._on(canvas, 'pointerdown', (event) => {
            /* Capture, so a drag that leaves the canvas still reports its
             * release — otherwise a widget stays pressed forever. */
            canvas.setPointerCapture?.(event.pointerId);
            this.session.post({ type: 'down', ...point(event), op: event.button === 2 ? 2 : 1 });
        });
        this._on(canvas, 'pointerup', (event) =>
            this.session.post({ type: 'up', ...point(event), op: event.button === 2 ? 2 : 1 }));
        this._on(canvas, 'wheel', (event) => {
            event.preventDefault();
            this.session.post({ type: 'wheel', ...point(event), delta: Math.sign(event.deltaY) });
        }, { passive: false });
        /* The context menu is the cache's, drawn by scripts into the tree; the
         * browser's would appear over it. */
        this._on(canvas, 'contextmenu', (event) => event.preventDefault());

        this._on(globalThis, 'keydown', (event) => {
            if( !this.running ) return;
            this.session.post({
                type: 'key', x: 0, y: 0,
                key: event.keyCode ?? -1,
                char: event.key?.length === 1 ? event.key.charCodeAt(0) : -1,
            });
        });
    }

    _on(target, type, handler, options) {
        target.addEventListener(type, handler, options);
        this._listeners.push([target, type, handler]);
    }
}

/* -------------------------------------------------------------------------
 * The compiled-script cache
 * ---------------------------------------------------------------------- */

const DB_NAME = 'cs2dom-aot';
const STORE = 'scripts';
const DB_VERSION = 1;

/**
 * Compiled scripts, remembered across page loads.
 *
 * Lowering a closure is fast but not free, and a cache open should not repeat
 * it on every reload. The key includes the CACHE identity and the generator
 * version: a different cache is a different program under the same script id,
 * and a changed emitter must invalidate everything it produced. Getting either
 * wrong serves yesterday's code for today's cache, which is the least
 * debuggable kind of stale.
 */
export class AotCache {
    constructor({ cacheKey, generatorVersion, storage = null } = {}) {
        if( !cacheKey ) throw new TypeError('an AOT cache needs a cache identity');
        this.prefix = `${cacheKey}:${generatorVersion ?? '0'}`;
        this.storage = storage;
        this.memory = new Map();
        this.stats = { hits: 0, misses: 0, writes: 0 };
    }

    /** Open IndexedDB, or fall back to memory only. Never throws. */
    static async open({ cacheKey, generatorVersion }) {
        const cache = new AotCache({ cacheKey, generatorVersion });
        try { cache.storage = await openDatabase(); }
        catch { cache.storage = null; }
        return cache;
    }

    async get(scriptId) {
        const key = `${this.prefix}:${scriptId}`;
        if( this.memory.has(key) ) { this.stats.hits++; return this.memory.get(key); }
        if( !this.storage ) { this.stats.misses++; return null; }
        try
        {
            const source = await request(
                this.storage.transaction(STORE, 'readonly').objectStore(STORE).get(key));
            if( typeof source === 'string' )
            {
                this.memory.set(key, source);
                this.stats.hits++;
                return source;
            }
        }
        catch { /* a broken store is a cold cache, not an error */ }
        this.stats.misses++;
        return null;
    }

    async put(scriptId, source) {
        const key = `${this.prefix}:${scriptId}`;
        this.memory.set(key, source);
        this.stats.writes++;
        if( !this.storage ) return;
        try
        {
            await request(
                this.storage.transaction(STORE, 'readwrite').objectStore(STORE).put(source, key));
        }
        catch { /* the cache is an optimisation; failing to write is not fatal */ }
    }
}

function openDatabase() {
    return new Promise((resolve, reject) => {
        const open = indexedDB.open(DB_NAME, DB_VERSION);
        open.onupgradeneeded = () => {
            if( !open.result.objectStoreNames.contains(STORE) )
                open.result.createObjectStore(STORE);
        };
        open.onsuccess = () => resolve(open.result);
        open.onerror = () => reject(open.error);
    });
}

function request(operation) {
    return new Promise((resolve, reject) => {
        operation.onsuccess = () => resolve(operation.result);
        operation.onerror = () => reject(operation.error);
    });
}

/**
 * Turn generated module source into callable generators.
 *
 * `new Function` rather than a blob import so the whole closure is installed
 * synchronously: a script registry half-populated when a hook fires is a
 * missing-script warning that looks like a content bug.
 */
export function compileModule(source, { intrinsics, park }) {
    const names = [...source.matchAll(/function\* (cs2_\d+)/g)].map((match) => match[1]);
    const body = source.replace(/^export function\*/gm, 'function*');
    // eslint-disable-next-line no-new-func
    const factory = new Function('K', 'PARK', `${body}\nreturn { ${names.join(', ')} };`);
    return factory(intrinsics, park);
}
