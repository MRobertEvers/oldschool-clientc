import assert from 'node:assert/strict';

import {
    TREE_DELTA_SCHEMA, TREE_DIRTY, createUITreeStore, createViewTreeStore,
} from '../src/ui_tree_store.js';

function component(id, patch = {}) {
    return {
        id, parentId: null, subId: -1, fileId: id, name: `component_${id}`, type: 0,
        props: {}, ops: [], hooks: {}, runtime: {}, ...patch,
    };
}

/* Working writes are synchronous, but React's committed snapshot moves only at
 * the outer fixed-point boundary. Yield cannot expose a half-finished tree. */
{
    const store = createUITreeStore({ viewport: { width: 512, height: 334 } });
    const initialSnapshot = store.getSnapshot();
    let publications = 0;
    store.subscribe(() => publications++);
    const sourceNested = { value: 1 };
    const sourceProps = Object.freeze({ nested: sourceNested });
    const token = store.beginTransaction({ source: 'mount' });
    const root = store.upsertNode(component(100, { fileId: 'root', props: sourceProps }));
    const child = store.upsertNode(component(101, {
        parentId: 100, subId: 7, fileId: 700, name: 'dynamic_child',
    }));

    sourceNested.value = 99;
    assert.equal(root.props.nested.value, 1, 'working snapshots retained caller-owned objects');
    assert(Object.isFrozen(root) && Object.isFrozen(root.props) && Object.isFrozen(root.props.nested));
    assert.equal(store.workingNode(101), child);
    assert.equal(store.committedNode(root.renderKey), null);
    assert.equal(store.getSnapshot(), initialSnapshot);

    store.yieldTransaction(token, 'asset');
    assert.throws(() => store.patchProps(101, { text: 'too early' }), /yielded/);
    assert.equal(store.getSnapshot(), initialSnapshot);
    assert.equal(publications, 0);
    store.resumeTransaction(token);
    const delta = store.commitTransaction(token);

    assert.equal(delta.schema, TREE_DELTA_SCHEMA);
    assert.equal(delta.baseRevision, 0);
    assert.equal(delta.revision, 1);
    assert.equal(store.commitRevision, 1);
    assert.equal(publications, 1);
    assert.deepEqual(delta.upsert.map((node) => node.renderKey), [root.renderKey, child.renderKey]);
    assert.deepEqual(delta.remove, []);
    assert.deepEqual(delta.dirtyGeometryRoots, [root.renderKey]);
    assert.deepEqual(store.getRoots(), [root.renderKey]);
    assert.deepEqual(store.getChildren(root.renderKey), [child.renderKey]);
    assert.equal(store.getNode(root.renderKey), root);
    assert.equal(store.getSnapshot().roots, store.getRootsSnapshot());
    assert(Object.isFrozen(delta) && Object.isFrozen(delta.upsert) && Object.isFrozen(delta.dirty));
    assert.equal(store.consumeDelta(), delta);
    assert.equal(store.consumeDelta(), null);

    const view = createViewTreeStore();
    let viewPublications = 0;
    view.subscribe(() => viewPublications++);
    view.applyDelta(delta);
    assert.equal(viewPublications, 1);
    assert.equal(view.getSnapshot().revision, 1);
    assert.deepEqual(view.getSnapshot().viewport, { width: 512, height: 334 });
    assert.deepEqual(view.getRoots(), [root.renderKey]);
    assert.deepEqual(view.getChildren(root.renderKey), [child.renderKey]);
    assert.equal(view.getNode(child.renderKey).generation, 1);
}

/* Dynamic slots use the complete signed sub-id domain. Static cache children
 * may share -1, while a dynamic -1 slot remains unique and stable on recreate. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    store.upsertNode(component(110, { fileId: 'signed-slot-root' }));
    store.upsertNode(component(111, { parentId: 110, subId: -1, fileId: 111 }));
    store.upsertNode(component(112, { parentId: 110, subId: -1, fileId: 112 }));
    const first = store.upsertNode(component(113, {
        parentId: 110, subId: -1, fileId: 113, dynamic: true,
    }));
    const minimum = store.upsertNode(component(116, {
        parentId: 110, subId: -0x80000000, fileId: 116, runtime: { dynamic: true },
    }));
    const maximum = store.upsertNode(component(117, {
        parentId: 110, subId: 0x7fffffff, fileId: 117, dynamic: true,
    }));
    assert.equal(store.childAt(110, -1), first);
    assert.equal(store.childAt(110, -0x80000000), minimum);
    assert.equal(store.childAt(110, 0x7fffffff), maximum);
    assert.equal(minimum.dynamic, true, 'runtime.dynamic was not canonicalized onto the node');
    assert.throws(() => store.upsertNode(component(118, {
        parentId: 110, subId: 0x80000000, fileId: 118, dynamic: true,
    })), /signed 32-bit/);
    assert.throws(() => store.upsertNode(component(114, {
        parentId: 110, subId: -1, fileId: 114, dynamic: true,
    })), /already has child subId -1/);
    store.commitTransaction(token);

    token = store.beginTransaction();
    store.removeNode(113);
    const recreated = store.upsertNode(component(115, {
        parentId: 110, subId: -1, fileId: 115, dynamic: true,
    }));
    assert.equal(recreated.renderKey, first.renderKey);
    assert(recreated.generation > first.generation);
    store.commitTransaction(token);

    token = store.beginTransaction();
    const updatedMinimum = store.updateNode(116, { runtime: { hidden: true } });
    assert.equal(updatedMinimum.dynamic, true,
        'updating runtime state discarded signed dynamic-slot identity');
    assert.equal(store.childAt(110, -0x80000000), updatedMinimum);
    store.commitTransaction(token);
}

/* Validation failures are checkpoint-safe: neither generation fences nor
 * callbacks are consumed before the mutation has passed every validation. */
{
    const store = createUITreeStore();
    const token = store.beginTransaction();
    assert.throws(() => store.upsertNode(component(120, {
        logicalKey: 'retry-after-invalid-type', type: 'invalid',
    })), /type must be a safe integer/);
    const retried = store.upsertNode(component(120, {
        logicalKey: 'retry-after-invalid-type', type: 0,
    }));
    assert.equal(retried.generation, 1, 'rejected create consumed a generation');

    assert.throws(() => store.upsertNode(component(121, {
        logicalKey: 'retry-after-invalid-dirty',
    }), { dirty: ['not-a-category'] }), /unknown UI tree dirty category/);
    const dirtyRetried = store.upsertNode(component(121, {
        logicalKey: 'retry-after-invalid-dirty',
    }));
    assert.equal(dirtyRetried.generation, 1, 'rejected dirty category consumed a generation');
    assert.equal(store.mutationVersion, 2);
    store.commitTransaction(token);

    let callbackCalls = 0;
    assert.throws(() => store.updateNode(120, () => {
        callbackCalls++;
        return { props: { text: 'must not run' } };
    }), /active transaction/);
    assert.equal(callbackCalls, 0, 'update callback ran outside a mutable transaction');
}

/* Per-node snapshots retain identity across unrelated commits, which makes
 * subscribe/getSnapshot pairs suitable for useSyncExternalStore selectors. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(1, { fileId: 'stable-root' }));
    const child = store.upsertNode(component(2, {
        parentId: 1, subId: 4, props: { text: 'old', color: 1 },
    }));
    const initial = store.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(initial);
    const committedRoot = store.getNodeSnapshot(root.renderKey);
    const committedChild = store.getNodeSnapshot(child.renderKey);
    const mirroredRoot = view.getNode(root.renderKey);
    const mirroredChild = view.getNode(child.renderKey);
    let rootNotifications = 0;
    let childNotifications = 0;
    store.subscribeNode(root.renderKey, () => rootNotifications++);
    store.subscribeNode(child.renderKey, () => childNotifications++);

    token = store.beginTransaction();
    store.patchProps(2, { text: 'new' });
    const update = store.commitTransaction(token);
    assert.equal(store.getNodeSnapshot(root.renderKey), committedRoot);
    assert.notEqual(store.getNodeSnapshot(child.renderKey), committedChild);
    assert.equal(rootNotifications, 0);
    assert.equal(childNotifications, 1);
    assert.deepEqual(update.dirty.paint, [child.renderKey]);
    assert.deepEqual(update.dirty.geometry, []);
    view.applyDelta(update);
    assert.equal(view.getNode(root.renderKey), mirroredRoot);
    assert.notEqual(view.getNode(child.renderKey), mirroredChild);

    const snapshot = store.getSnapshot();
    const version = store.mutationVersion;
    token = store.beginTransaction();
    assert.equal(store.patchProps(2, { text: 'new' }), store.workingNode(2));
    assert.equal(store.commitTransaction(token), null);
    assert.equal(store.getSnapshot(), snapshot);
    assert.equal(store.mutationVersion, version);
    assert.equal(childNotifications, 1);

    const dirtyNode = store.getNode(child.renderKey);
    const dirtyMirror = view.getNode(child.renderKey);
    token = store.beginTransaction();
    store.markDirty(child.renderKey, TREE_DIRTY.PAINT);
    const dirtyOnly = store.commitTransaction(token);
    assert.deepEqual(dirtyOnly.upsert, []);
    assert.equal(store.getNode(child.renderKey), dirtyNode,
        'dirty-only publication replaced an unchanged node snapshot');
    assert.equal(childNotifications, 2);
    view.applyDelta(dirtyOnly);
    assert.equal(view.getNode(child.renderKey), dirtyMirror,
        'dirty-only mirror application replaced an unchanged node snapshot');
}

/* Recursive removal is one semantic mutation, removes every nested index and
 * order record, and leaves the old committed projection visible until commit. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(200, { fileId: 'nested-root' }));
    const parent = store.upsertNode(component(201, {
        parentId: 200, subId: -5, fileId: 201, name: 'nested_parent', dynamic: true,
    }));
    const child = store.upsertNode(component(202, {
        parentId: 201, subId: -0x80000000, fileId: 202,
        name: 'nested_child', dynamic: true,
    }));
    const grandchild = store.upsertNode(component(203, {
        parentId: 202, subId: 9, fileId: 203, name: 'nested_grandchild', dynamic: true,
    }));
    const initial = store.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(initial);
    const mirroredRoot = view.getNode(root.renderKey);
    const parentRef = store.ref(parent.id);
    const beforeVersion = store.mutationVersion;

    token = store.beginTransaction();
    store.removeNode(parent.id);
    assert.equal(store.mutationVersion, beforeVersion + 1);
    assert.equal(store.workingNode(parent.id), null);
    assert.equal(store.workingNode(child.id), null);
    assert.equal(store.workingNode(grandchild.id), null);
    assert.equal(store.resolveRef(parentRef), null);
    assert.equal(store.committedNode(parent.renderKey), parent,
        'nested removal leaked into the committed projection before commit');
    assert.deepEqual(store.findByName('nested_parent'), []);
    assert.deepEqual(store.findByName('nested_child'), []);
    const removal = store.commitTransaction(token);
    assert.deepEqual(removal.remove, [
        grandchild.renderKey, child.renderKey, parent.renderKey,
    ]);
    assert.deepEqual(removal.order, [{ parentRenderKey: root.renderKey, children: [] }]);
    assert.deepEqual(removal.dirtyGeometryRoots, [root.renderKey]);
    view.applyDelta(removal);
    assert.equal(view.getNode(root.renderKey), mirroredRoot);
    assert.equal(view.getNode(parent.renderKey), null);
    assert.equal(view.getNode(child.renderKey), null);
    assert.equal(view.getNode(grandchild.renderKey), null);
    assert.deepEqual(view.getChildren(root.renderKey), []);
    assert.deepEqual(view.getChildren(parent.renderKey), []);
}

/* The component ceiling permits pathologically deep trees. Removal is
 * iterative, and a create-then-delete transaction coalesces to no publication. */
{
    const depth = 12000;
    const store = createUITreeStore();
    const snapshot = store.getSnapshot();
    const token = store.beginTransaction();
    store.upsertNode(component(10000), { dirty: [] });
    for( let index = 1; index < depth; index++ ) store.upsertNode(component(10000 + index, {
        parentId: 9999 + index, subId: -1,
    }), { dirty: [] });
    const beforeRemovalVersion = store.mutationVersion;
    assert.equal(store.removeNode(10000), true);
    assert.equal(store.mutationVersion, beforeRemovalVersion + 1);
    assert.equal(store.commitTransaction(token), null);
    assert.equal(store.commitRevision, 0);
    assert.equal(store.getSnapshot(), snapshot);
    assert.deepEqual(store.workingChildren(), []);
}

/* Replacing a complete nested dynamic subtree in the same slots emits only
 * generation-changing upserts. Child-first removal history must still install
 * atomically in the mirror without spurious order or remove records. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(210, { fileId: 'replace-root' }));
    const oldParent = store.upsertNode(component(211, {
        parentId: 210, subId: -2, fileId: 211, dynamic: true,
    }));
    const oldChild = store.upsertNode(component(212, {
        parentId: 211, subId: -3, fileId: 212, dynamic: true,
    }));
    const initial = store.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(initial);

    token = store.beginTransaction();
    store.removeNode(oldParent.id);
    const newParent = store.upsertNode(component(213, {
        parentId: 210, subId: -2, fileId: 213, dynamic: true,
    }));
    const newChild = store.upsertNode(component(214, {
        parentId: 213, subId: -3, fileId: 214, dynamic: true,
    }));
    const replacement = store.commitTransaction(token);
    assert.equal(newParent.renderKey, oldParent.renderKey);
    assert.equal(newChild.renderKey, oldChild.renderKey);
    assert(newParent.generation > oldParent.generation);
    assert(newChild.generation > oldChild.generation);
    assert.deepEqual(replacement.remove, []);
    assert.deepEqual(replacement.order, []);
    assert.deepEqual(new Set(replacement.upsert.map((node) => node.renderKey)),
        new Set([oldParent.renderKey, oldChild.renderKey]));
    view.applyDelta(replacement);
    assert.equal(view.getNode(oldParent.renderKey).id, newParent.id);
    assert.equal(view.getNode(oldChild.renderKey).id, newChild.id);
    assert.deepEqual(view.getChildren(root.renderKey), [newParent.renderKey]);
    assert.deepEqual(view.getChildren(newParent.renderKey), [newChild.renderKey]);
}

/* Abort is publication abort, not state rollback. Its writes remain visible to
 * the VM and are folded into the next successful fixed-point publication. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(10, { fileId: 'abort-root', props: { text: 'old' } }));
    store.commitTransaction(token);
    const committed = store.getNode(root.renderKey);
    const snapshot = store.getSnapshot();

    token = store.beginTransaction();
    store.patchProps(10, { text: 'working' });
    store.abortTransaction(token, 'script error');
    assert.equal(store.workingNode(10).props.text, 'working');
    assert.equal(store.getNode(root.renderKey), committed);
    assert.equal(store.getSnapshot(), snapshot);

    token = store.beginTransaction();
    const delta = store.commitTransaction(token);
    assert.equal(delta.revision, 2);
    assert.equal(store.getNode(root.renderKey).props.text, 'working');
}

/* A logical parent/sub-id slot is the React identity. Recreating that slot
 * changes the VM generation and id while preserving the render key. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(20, { fileId: 'generation-root' }));
    const first = store.upsertNode(component(21, { parentId: 20, subId: 3, fileId: 900 }));
    const firstDelta = store.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(firstDelta);
    const stale = store.ref(21);

    token = store.beginTransaction();
    store.removeNode(21);
    const second = store.upsertNode(component(22, { parentId: 20, subId: 3, fileId: 901 }));
    assert.equal(second.renderKey, first.renderKey);
    assert(second.generation > first.generation);
    assert.equal(store.resolveRef(stale), null);
    const replacement = store.commitTransaction(token);
    assert.deepEqual(replacement.remove, []);
    assert.deepEqual(replacement.upsert.map((node) => node.renderKey), [first.renderKey]);
    assert.deepEqual(replacement.order, [], 'stable slot identity caused false parent reorder');
    view.applyDelta(replacement);
    assert.equal(view.getNode(first.renderKey).id, 22);
    assert.equal(view.getNode(first.renderKey).generation, second.generation);

    token = store.beginTransaction();
    const beforeRemovalVersion = store.mutationVersion;
    store.removeNode(22);
    assert.equal(store.mutationVersion, beforeRemovalVersion + 1,
        'one HOST delete must advance the semantic mutation version once');
    const removal = store.commitTransaction(token);
    assert.deepEqual(removal.remove, [first.renderKey]);
    assert.deepEqual(removal.order, [{ parentRenderKey: root.renderKey, children: [] }]);
    view.applyDelta(removal);
    assert.equal(view.getNode(first.renderKey), null);
    assert.deepEqual(view.getChildren(root.renderKey), []);
}

/* Two slots may exchange recycled VM ids in one settled transaction. Both the
 * authoritative committed id index and mirror id index update in two phases. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(300, { fileId: 'id-swap-root' }));
    const oldLeft = store.upsertNode(component(301, {
        parentId: 300, subId: 1, fileId: 301, dynamic: true,
    }));
    const oldRight = store.upsertNode(component(302, {
        parentId: 300, subId: 2, fileId: 302, dynamic: true,
    }));
    const initial = store.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(initial);

    token = store.beginTransaction();
    store.removeNode(oldLeft.id);
    store.removeNode(oldRight.id);
    const newLeft = store.upsertNode(component(302, {
        parentId: 300, subId: 1, fileId: 303, dynamic: true,
    }));
    const newRight = store.upsertNode(component(301, {
        parentId: 300, subId: 2, fileId: 304, dynamic: true,
    }));
    const swapped = store.commitTransaction(token);
    assert.equal(store.committedNode(302), newLeft);
    assert.equal(store.committedNode(301), newRight);
    assert.equal(newLeft.renderKey, oldLeft.renderKey);
    assert.equal(newRight.renderKey, oldRight.renderKey);
    view.applyDelta(swapped);
    assert.equal(view.getNode(oldLeft.renderKey).id, 302);
    assert.equal(view.getNode(oldRight.renderKey).id, 301);

    token = store.beginTransaction();
    const nested = store.upsertNode(component(303, {
        parentId: newLeft.id, subId: 5, fileId: 305, dynamic: true,
    }));
    const nestedDelta = store.commitTransaction(token);
    view.applyDelta(nestedDelta);
    assert.deepEqual(view.getChildren(newLeft.renderKey), [nested.renderKey]);
    assert.deepEqual(view.getChildren(root.renderKey), [newLeft.renderKey, newRight.renderKey]);
}

/* Ordering and geometry invalidation are independent from node snapshot
 * replacement; a renderer can subscribe to only the parent order. */
{
    const store = createUITreeStore();
    let token = store.beginTransaction();
    const root = store.upsertNode(component(30, { fileId: 'order-root' }));
    const left = store.upsertNode(component(31, { parentId: 30, subId: 1 }));
    const right = store.upsertNode(component(32, { parentId: 30, subId: 2 }));
    store.commitTransaction(token);
    const rootSnapshot = store.getNode(root.renderKey);
    let orderNotifications = 0;
    store.subscribeOrder(root.renderKey, () => orderNotifications++);

    token = store.beginTransaction();
    store.setChildOrder(30, [32, 31]);
    const delta = store.commitTransaction(token);
    assert.deepEqual(delta.upsert, []);
    assert.deepEqual(delta.order, [{
        parentRenderKey: root.renderKey,
        children: [right.renderKey, left.renderKey],
    }]);
    assert.deepEqual(delta.dirtyGeometryRoots, [root.renderKey]);
    assert.equal(store.getNode(root.renderKey), rootSnapshot);
    assert.equal(orderNotifications, 1);
    assert.deepEqual(store.getChildren(root.renderKey), [right.renderKey, left.renderKey]);
}

/* A malformed main-thread delta is rejected before any renderer-visible state
 * is modified or notified. */
{
    const source = createUITreeStore();
    const token = source.beginTransaction();
    const root = source.upsertNode(component(40, { fileId: 'mirror-root' }));
    const child = source.upsertNode(component(41, { parentId: 40, subId: 1 }));
    const initial = source.commitTransaction(token);
    const view = createViewTreeStore();
    view.applyDelta(initial);
    const snapshot = view.getSnapshot();
    let notifications = 0;
    view.subscribe(() => notifications++);
    const bad = {
        schema: TREE_DELTA_SCHEMA,
        baseRevision: 1,
        revision: 2,
        mutationVersion: initial.mutationVersion,
        upsert: [], remove: [],
        order: [{ parentRenderKey: null, children: ['missing'] }],
        dirty: Object.fromEntries(Object.values(TREE_DIRTY).map((key) => [key, []])),
        dirtyGeometryRoots: [],
    };
    assert.throws(() => view.applyDelta(bad), /missing or duplicate keys/);
    assert.equal(view.getSnapshot(), snapshot);
    assert.equal(view.revision, 1);
    assert.equal(notifications, 0);

    const emptyDirty = () => Object.fromEntries(
        Object.values(TREE_DIRTY).map((key) => [key, []]));
    const duplicateOrder = {
        ...bad,
        order: [
            { parentRenderKey: null, children: [root.renderKey] },
            { parentRenderKey: null, children: [root.renderKey] },
        ],
        dirty: emptyDirty(),
    };
    assert.throws(() => view.applyDelta(duplicateOrder), /duplicate parent orders/);
    assert.equal(view.getSnapshot(), snapshot);

    const regressedMutation = {
        ...bad,
        mutationVersion: initial.mutationVersion - 1,
        order: [],
        dirty: emptyDirty(),
    };
    assert.throws(() => view.applyDelta(regressedMutation), /mutation version regressed/);
    assert.equal(view.getSnapshot(), snapshot);

    const cycle = {
        ...bad,
        upsert: [
            { ...view.getNode(root.renderKey), parentId: child.id },
            { ...view.getNode(child.renderKey), parentId: root.id },
        ],
        order: [],
        dirty: emptyDirty(),
    };
    assert.throws(() => view.applyDelta(cycle), /parent cycle/);
    assert.equal(view.getSnapshot(), snapshot);
    assert.equal(view.revision, 1);
    assert.equal(notifications, 0);
}

/* Working indexes and viewport/interaction publication cover the remaining
 * foundation APIs without exposing mutable collections. */
{
    const store = createUITreeStore({ viewport: { width: 1, height: 1 } });
    let token = store.beginTransaction();
    const first = store.upsertNode(component(50, { fileId: 'shared', name: 'named' }));
    const second = store.upsertNode(component(51, { fileId: 'other', name: 'named' }));
    assert.deepEqual(store.findByFileId('shared').map((node) => node.id), [50]);
    assert.deepEqual(store.findByName('named').map((node) => node.id), [50, 51]);
    store.commitTransaction(token);

    token = store.beginTransaction();
    assert.equal(store.setViewport({ width: 1, height: 1 }), false);
    assert.equal(store.setViewport({ width: 2, height: 1 }), true);
    assert.equal(store.setInteraction({ hover: first.renderKey, pressed: second.renderKey }), true);
    const delta = store.commitTransaction(token);
    assert.deepEqual(delta.viewport, { width: 2, height: 1 });
    assert.deepEqual(delta.interaction, { hover: first.renderKey, pressed: second.renderKey });
    assert.equal(store.getSnapshot().viewport.width, 2);
}

console.log('ui tree store tests passed');
