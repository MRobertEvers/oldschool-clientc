import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

import { createHostRuntime } from '../src/host_runtime.js';
import { stageBoxesEqual } from '../src/runtime_worker_protocol.js';

function component(fileId, name, type, layer, staticProps = {}) {
    return {
        fileId, name, kind: type === 4 ? 'Text' : type === 5 ? 'Graphic' : 'Layer',
        type, layer,
        static: {
            x: 0, y: 0, width: 100, height: 20,
            xMode: 0, yMode: 0, widthMode: 0, heightMode: 0,
            hidden: false, transparency: 0,
            ...staticProps,
        },
        props: {}, ops: [], hooks: {}, events: {}, dynamic: [], dependencies: [],
        runtime: {},
    };
}

const ir = {
    interfaceId: 994,
    components: [
        component(0, 'root', 0, null, { width: 512, height: 334 }),
        component(1, 'label', 4, 0, { x: 12, y: 13, text: 'before', font: 1 }),
        component(2, 'icon', 5, 0, { x: 20, y: 40, sprite: 10 }),
    ],
};

const host = createHostRuntime(ir, { recordChanges: false });
host.layout();
assert.equal(host.consumeTreeDelta(), null, 'a read-only initial projection fabricated a delta');

host.mutate('if_settext', 'label', 'before');
assert.equal(host.consumeTreeDelta(), null, 'a no-op scalar setter fabricated a delta');

host.mutate('if_settext', 'label', 'after');
const paint = host.consumeTreeDelta();
assert.equal(paint.schema, 'cs2dom-tree-delta/1');
assert.equal(paint.projection, 'dirty');
assert.deepEqual(paint.dirty.paint, ['if:994:1']);
assert(Object.isFrozen(paint) && Object.isFrozen(paint.dirty) &&
    Object.isFrozen(paint.dirty.paint), 'committed Host TreeDelta is mutable');
const dirtyLabel = host.projectRenderKey('if:994:1');
const fullLabel = host.layout().find((box) => box.fileId === 1);
fullLabel.presentation = host.presentation('label');
assert(stageBoxesEqual(dirtyLabel, fullLabel),
    'single-node paint projection diverged from the full layout oracle');

host.setHook('label', 'onClick', { scriptId: 123, args: [] });
const interaction = host.consumeTreeDelta();
assert.equal(interaction.projection, 'dirty');
assert.deepEqual(interaction.dirty.interaction, ['if:994:1']);
const dirtyHook = host.projectRenderKey('if:994:1');
const fullHook = host.layout().find((box) => box.fileId === 1);
fullHook.presentation = host.presentation('label');
assert(stageBoxesEqual(dirtyHook, fullHook) && dirtyHook.hooks.includes('onClick'),
    'hook-only dirty projection lost interaction metadata');

host.mutate('if_setposition', 'label', 30, 40, 0, 0);
const geometry = host.consumeTreeDelta();
assert.equal(geometry.projection, 'full');
assert.deepEqual(geometry.dirtyGeometryRoots, ['if:994:1']);

host.writeState('varp', 1, 2, { transmit: false });
const state = host.consumeTreeDelta();
assert.equal(state.projection, 'full', 'state-driven expressions bypassed the full oracle');

const created = host.createChild('root', 4, -7);
const createdDelta = host.consumeTreeDelta();
assert.equal(createdDelta.projection, 'full');
const firstGeneration = created.generation;
const logicalKey = host.renderKey(created);
host.delete(created);
const recreated = host.createChild('root', 4, -7);
const replacedDelta = host.consumeTreeDelta();
assert.equal(replacedDelta.projection, 'full');
assert.equal(host.renderKey(recreated), logicalKey,
    'delete/recreate changed the logical React render key');
assert.notEqual(recreated.generation, firstGeneration,
    'delete/recreate failed to fence the stale VM generation');
assert.equal(host.projectRenderKey(logicalKey)?.ref.generation, recreated.generation,
    'dirty projector resolved the retired generation');

/* A failed outer hook may have changed the synchronous working tree, but it
 * must not advance or publish the renderer commit. The next successful outer
 * boundary settles all pending writes in one delta. */
let failingHost;
failingHost = createHostRuntime(ir, {
    recordChanges: false,
    invoke() {
        failingHost.mutate('if_settext', 'label', 'partial');
        throw new Error('fixture hook failed');
    },
});
failingHost.setHook('label', 'onClick', { scriptId: 321, args: [] });
failingHost.consumeTreeDelta();
const committedBeforeFailure = failingHost.commitRevision;
assert.throws(() => failingHost.trigger('label', 'onClick'), /fixture hook failed/);
assert.equal(failingHost.commitRevision, committedBeforeFailure,
    'failed outer boundary advanced the renderer revision');
assert.equal(failingHost.consumeTreeDelta(), null,
    'failed outer boundary published its working mutation');
/* The recovering boundary is intentionally a read. Its own start/end
 * version is equal, so only comparison with committedMutationVersion can
 * publish the dirty working tree left by the failed hook. */
assert.equal(failingHost.request({ kind: 'IF_GETTEXT', component: 'label' }), 'partial');
const settled = failingHost.consumeTreeDelta();
assert.equal(settled.revision, committedBeforeFailure + 1);
assert.deepEqual(settled.dirty.paint, ['if:994:1']);
const settledBox = failingHost.projectRenderKey('if:994:1');
assert.equal(settledBox.props.text, 'partial');

/* Completion of the main HOST call is not enough to commit: deferred work and
 * interaction retirement are part of the same native transaction. A failure
 * in either settlement phase keeps every working mutation private until a
 * later successful (even no-op) outer boundary. */
const drainFailureHost = createHostRuntime(ir, { recordChanges: false });
const originalDrain = drainFailureHost._drainDeferredComponents.bind(drainFailureHost);
let failDrain = true;
drainFailureHost._drainDeferredComponents = (intents) => {
    if( failDrain ) {
        failDrain = false;
        drainFailureHost.mutate('if_settext', 'label', 'from-drain');
        throw new Error('fixture deferred drain failed');
    }
    return originalDrain(intents);
};
const drainCommitted = drainFailureHost.commitRevision;
assert.throws(() => drainFailureHost.mutate('if_setcolour', 'label', 0x123456),
    /fixture deferred drain failed/);
assert.equal(drainFailureHost.commitRevision, drainCommitted,
    'failed deferred drain advanced the renderer revision');
assert.equal(drainFailureHost.consumeTreeDelta(), null,
    'failed deferred drain published a partial renderer transaction');
assert.equal(drainFailureHost.request(
    { kind: 'IF_GETTEXT', component: 'label' }), 'from-drain');
const drainSettled = drainFailureHost.consumeTreeDelta();
assert.equal(drainSettled.revision, drainCommitted + 1);
assert.deepEqual(drainSettled.dirty.paint, ['if:994:1']);
const drainBox = drainFailureHost.projectRenderKey('if:994:1');
assert.equal(drainBox.props.text, 'from-drain');
assert.equal(drainBox.props.color, 0x123456);

const interactionFailureHost = createHostRuntime(ir, { recordChanges: false });
const originalRetire = interactionFailureHost._retireInvisibleInteraction
    .bind(interactionFailureHost);
let failRetire = true;
interactionFailureHost._retireInvisibleInteraction = () => {
    if( failRetire ) {
        failRetire = false;
        throw new Error('fixture interaction settlement failed');
    }
    return originalRetire();
};
interactionFailureHost.interactionVisibilityDirty = true;
const interactionCommitted = interactionFailureHost.commitRevision;
assert.throws(() => interactionFailureHost.mutate('if_settext', 'label', 'retired'),
    /fixture interaction settlement failed/);
assert.equal(interactionFailureHost.commitRevision, interactionCommitted,
    'failed interaction settlement advanced the renderer revision');
assert.equal(interactionFailureHost.consumeTreeDelta(), null,
    'failed interaction settlement published a partial renderer transaction');
assert.equal(interactionFailureHost.request(
    { kind: 'IF_GETTEXT', component: 'label' }), 'retired');
const interactionSettled = interactionFailureHost.consumeTreeDelta();
assert.equal(interactionSettled.revision, interactionCommitted + 1);
assert.equal(interactionFailureHost.projectRenderKey('if:994:1').props.text, 'retired');

/* A large retained tree must not be rescanned merely to publish one scalar.
 * layoutVersion staying invalid proves projectRenderKey used the target walk;
 * the wall-clock assertion guards accidental O(tree-size) bookkeeping. */
const large = createHostRuntime({
    interfaceId: 995,
    components: Array.from({ length: 4096 }, (_, index) => component(
        index, `n${index}`, index === 4095 ? 4 : 0, null,
        { x: index & 511, y: index >> 9, width: 1, height: 1,
            ...(index === 4095 ? { text: 'a', font: 1 } : {}) },
    )),
}, { recordChanges: false });
large.layout();
const started = performance.now();
large.mutate('if_settext', 'n4095', 'b');
const one = large.consumeTreeDelta();
const projected = large.projectRenderKey(one.dirty.paint[0]);
const elapsed = performance.now() - started;
assert.equal(projected.props.text, 'b');
assert.notEqual(large.layoutVersion, large.version,
    'dirty projection silently rebuilt the complete layout');
assert(elapsed < 10, `one-node TreeDelta/project took ${elapsed.toFixed(3)}ms`);

console.log(`host TreeDelta tests passed (${elapsed.toFixed(3)}ms one-node projection)`);
