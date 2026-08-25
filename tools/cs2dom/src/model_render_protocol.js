/* Small structured-clone contract shared by the page and toridraw worker. */

export const MODEL_RENDER_WORKER_SCHEMA = 'cs2dom-model-render-worker/1';

export function validModelRenderMessage(message) {
    return Boolean(message && message.schema === MODEL_RENDER_WORKER_SCHEMA &&
        typeof message.type === 'string' && typeof message.owner === 'string' &&
        Number.isSafeInteger(message.requestId));
}
