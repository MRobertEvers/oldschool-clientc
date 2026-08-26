/*
 * Model widgets, rendered by toridraw.
 *
 * The 3D rasteriser stays where it is: in WASM, in a worker. It is a raster
 * kernel and not a VM, and reimplementing it in JavaScript would buy nothing
 * this design cares about — the whole argument of the redesign was that the
 * BRIDGE was the cost, and a model render crosses that bridge once per pose
 * rather than once per host call.
 *
 * ------------------------------------------------------------------
 * A pose is rendered once, not once per frame
 * ------------------------------------------------------------------
 *
 * A widget showing a still model at a fixed angle asks for the same image
 * every frame for as long as it is open. `ModelStore` keys on the pose, so the
 * second ask is a cache hit; this adapter only ever sees the first.
 *
 * ------------------------------------------------------------------
 * Superseding, not queueing
 * ------------------------------------------------------------------
 *
 * A spinning model asks for a new pose every frame, and by the time the third
 * request is served the first two are already stale. The controller retires a
 * widget's previous request when a new one arrives, so the worker is never
 * more than one frame behind per widget — which is why `owner` is the widget
 * rather than the request.
 *
 * ------------------------------------------------------------------
 * A render that has not finished draws nothing
 * ------------------------------------------------------------------
 *
 * Never a placeholder and never a stale pose from a different angle. The
 * painter records the id as wanted and leaves the box empty, which is visibly
 * absent; a stale frame is a model that looks like it is lagging rather than
 * loading, and a placeholder looks deliberate.
 */

import { poseKey } from './assets.js';

export class ModelSourceError extends Error {
    constructor(message) {
        super(message);
        this.name = 'ModelSourceError';
    }
}

export function createModelSource(options = {}) {
    return new ToridrawModelSource(options);
}

export class ToridrawModelSource {
    constructor({
        controller = null,
        /** id -> the URL the worker fetches the model record from. */
        modelUrl = (id) => `/api/model?id=${id | 0}`,
        animationUrl = (seq) => (seq >= 0 ? `/api/seq?id=${seq | 0}` : null),
        onWarning = null,
    } = {}) {
        this.controller = controller;
        this.modelUrl = modelUrl;
        this.animationUrl = animationUrl;
        this.onWarning = onWarning;
        /*
         * Poses already asked for, keyed by the POSE.
         *
         * Two different keys for two different jobs, and conflating them is a
         * real bug: keyed by owner, a spinning model's next angle would
         * coalesce into the request for its previous one and the widget would
         * freeze at whatever pose it first asked for. The owner supersedes
         * across poses; this deduplicates within one.
         */
        this.inFlight = new Map();
        this.stats = { requested: 0, rendered: 0, superseded: 0, failed: 0 };
    }

    /**
     * Ask for one pose. Resolves to an image, or null.
     *
     * Null is a real answer — the worker was superseded, or the model is not
     * in this cache — and the caller draws nothing rather than waiting.
     */
    async render(id, pose = {}) {
        if( !this.controller ) return null;
        const key = poseKey(id, pose);
        if( this.inFlight.has(key) ) return this.inFlight.get(key);

        const work = this._render(id, pose, poseOwner(id, pose))
            .finally(() => this.inFlight.delete(key));
        this.inFlight.set(key, work);
        return work;
    }

    async _render(id, pose, owner) {
        this.stats.requested++;
        try
        {
            const { completion } = this.controller.render({
                /*
                 * The owner is the WIDGET, not the request: a spinning model
                 * must supersede its own previous frame rather than queue
                 * behind it. Keying by pose here would make every angle a
                 * separate owner and the queue would grow without bound.
                 */
                owner: `${owner}`,
                token: `${id}:${pose.anim ?? -1}`,
                modelUrl: this.modelUrl(id),
                animationUrl: this.animationUrl(pose.anim ?? -1),
                width: pose.width | 0, height: pose.height | 0,
                widgetX: 0, widgetY: 0,
                widgetWidth: pose.width | 0, widgetHeight: pose.height | 0,
                zoom: pose.zoom | 0,
                xAngle: pose.angleX | 0, yAngle: pose.angleY | 0, zAngle: pose.angleZ | 0,
                xOffset: pose.offsetX | 0, yOffset: pose.offsetY | 0,
                orthographic: Boolean(pose.orthographic),
                fixedZoom: Boolean(pose.fixedZoom),
                preferBitmap: true,
            });

            const result = await completion;
            /* A superseded render is not a failure: a newer pose is already on
             * its way and this one would draw the wrong angle. */
            if( result?.stale ) { this.stats.superseded++; return null; }
            if( !result?.bitmap && !result?.image )
            {
                this.stats.failed++;
                return null;
            }
            this.stats.rendered++;
            const bitmap = result.bitmap ?? result.image;
            return {
                width: result.width ?? pose.width | 0,
                height: result.height ?? pose.height | 0,
                offsetX: 0, offsetY: 0,
                bitmap,
            };
        }
        catch( error )
        {
            /* A worker that failed is reported and answered with null. Throwing
             * would abandon the frame over a widget that draws nothing. */
            this.stats.failed++;
            this.onWarning?.(`model ${id}: ${error.message}`);
            return null;
        }
    }

    dispose() { this.controller?.dispose?.(); }
}

/**
 * The owner key: the widget, identified by everything that is not the pose.
 *
 * Deliberately NOT the full pose. The store keys images by pose, which is
 * about caching; this key is about supersession, and a spinning model has to
 * be one owner across every angle it passes through.
 */
function poseOwner(id, pose) {
    return `${id | 0}:${pose.width | 0}x${pose.height | 0}`;
}

export { poseOwner };
