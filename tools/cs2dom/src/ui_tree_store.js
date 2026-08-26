/*
 * Transactional UI tree shared by CS2 hosts and renderers.
 *
 * The working view is synchronous VM state. The committed view is an immutable
 * renderer projection. Yielding or aborting a transaction never rolls working
 * state back; it merely leaves the previous committed revision visible until a
 * later transaction reaches a publishable fixed point.
 */

export const UI_TREE_SCHEMA = 'cs2dom-ui-tree/1';
export const TREE_DELTA_SCHEMA = 'cs2dom-tree-delta/1';

export const TREE_DIRTY = Object.freeze({
    PAINT: 'paint',
    GEOMETRY: 'geometry',
    VISIBILITY: 'visibility',
    TOPOLOGY: 'topology',
    ORDER: 'order',
    INTERACTION: 'interaction',
    VIEWPORT: 'viewport',
});

const DIRTY_CATEGORIES = Object.freeze(Object.values(TREE_DIRTY));
const DIRTY_CATEGORY_SET = new Set(DIRTY_CATEGORIES);
const TRUSTED_IMMUTABLE = new WeakSet();
const EMPTY_ARRAY = Object.freeze([]);
const EMPTY_OBJECT = Object.freeze({});
TRUSTED_IMMUTABLE.add(EMPTY_ARRAY);
TRUSTED_IMMUTABLE.add(EMPTY_OBJECT);

const GEOMETRY_PROPS = new Set([
    'x', 'y', 'width', 'height', 'xMode', 'yMode', 'widthMode', 'heightMode',
    'xmode', 'ymode', 'widthmode', 'heightmode', 'scrollX', 'scrollY',
    'scrollWidth', 'scrollHeight', 'scrollx', 'scrolly', 'scrollwidth', 'scrollheight',
]);
const VISIBILITY_PROPS = new Set(['hide', 'hidden', 'visible']);

export function createUITreeStore(options = {}) {
    return new UITreeStore(options);
}

export function createViewTreeStore(options = {}) {
    return new ViewTreeStore(options);
}

export class UITreeStore {
    constructor({ viewport = null, interaction = null, onListenerError = reportListenerError } = {}) {
        this.mutationVersion = 0;
        this.commitRevision = 0;
        this._nextTransaction = 0;
        this._transaction = null;

        this._nodesById = new Map();
        this._nodesByRenderKey = new Map();
        this._childrenByParent = new Map([[null, new Map()]]);
        this._childBySubIdByParent = new Map();
        this._idsByFileId = new Map();
        this._idsByName = new Map();
        this._renderKeyByLogicalSlot = new Map();
        this._generationByRenderKey = new Map();

        this._committedNodesById = new Map();
        this._committedNodesByRenderKey = new Map();
        this._committedOrderByParentKey = new Map([[null, EMPTY_ARRAY]]);

        this._dirty = createDirtySets();
        this._changedKeys = new Set();
        this._changedOrderParents = new Set();
        this._viewportChanged = false;
        this._interactionChanged = false;

        this._workingViewport = immutableValue(viewport);
        this._workingInteraction = immutableValue(interaction);
        this._committedViewport = this._workingViewport;
        this._committedInteraction = this._workingInteraction;
        this._lastDelta = null;

        this._listeners = new Set();
        this._nodeListeners = new Map();
        this._orderListeners = new Map();
        this._onListenerError = onListenerError;
        this._committedSnapshot = treeSnapshot(
            0, 0, EMPTY_ARRAY, this._committedViewport, this._committedInteraction);

        /* These three functions can be passed directly to useSyncExternalStore. */
        this.subscribe = (listener) => subscribeSet(this._listeners, listener);
        this.getSnapshot = () => this._committedSnapshot;
        this.getServerSnapshot = this.getSnapshot;
    }

    get transactionState() {
        return this._transaction || null;
    }

    get lastDelta() {
        return this._lastDelta;
    }

    consumeDelta() {
        const delta = this._lastDelta;
        this._lastDelta = null;
        return delta;
    }

    beginTransaction(metadata = null) {
        if( this._transaction )
            throw new Error(
                `UI tree transaction ${this._transaction.token} is already ${this._transaction.status}`);
        const token = ++this._nextTransaction;
        this._transaction = transactionState(token, 'active', metadata);
        return token;
    }

    yieldTransaction(token = this._transaction?.token, reason = null) {
        this._requireTransaction(token, 'active');
        this._transaction = transactionState(token, 'yielded', this._transaction.metadata, reason);
        return this._transaction;
    }

    resumeTransaction(token = this._transaction?.token) {
        this._requireTransaction(token, 'yielded');
        this._transaction = transactionState(token, 'active', this._transaction.metadata);
        return this._transaction;
    }

    abortTransaction(token = this._transaction?.token, reason = null) {
        this._requireTransaction(token);
        const result = Object.freeze({
            token, status: 'aborted', reason,
            mutationVersion: this.mutationVersion,
            commitRevision: this.commitRevision,
        });
        this._transaction = null;
        return result;
    }

    commitTransaction(token = this._transaction?.token) {
        this._requireTransaction(token, 'active');
        const publication = this._createPublication();
        this._transaction = null;
        if( !publication ) return null;

        const { delta, affectedKeys, affectedOrderParents } = publication;
        this._lastDelta = delta;
        notifySet(this._listeners, delta, this._onListenerError);
        for( const key of affectedKeys )
            notifySet(this._nodeListeners.get(key), delta, this._onListenerError);
        for( const key of affectedOrderParents )
            notifySet(this._orderListeners.get(key), delta, this._onListenerError);
        return delta;
    }

    upsertNode(input, { dirty = null } = {}) {
        this._requireMutable();
        if( !input || typeof input !== 'object' ) throw new TypeError('UI tree node must be an object');
        const id = nodeId(input.id);
        const previous = this._nodesById.get(id) || null;
        if( previous && input.renderKey !== undefined && input.renderKey !== previous.renderKey )
            throw new Error(`renderKey is immutable for live node ${id}`);

        const parentId = input.parentId === undefined
            ? (previous?.parentId ?? null) : nullableNodeId(input.parentId);
        if( parentId === id ) throw new Error(`UI tree node ${id} cannot parent itself`);
        const parent = parentId === null ? null : this._nodesById.get(parentId);
        if( parentId !== null && !parent ) throw new Error(`UI tree parent ${parentId} does not exist`);
        if( previous && parentId !== previous.parentId ) this._assertNoCycle(id, parentId);

        const subId = signedInteger(input.subId ?? previous?.subId ?? -1, 'subId');
        const source = previous ? { ...previous, ...input, parentId, subId } : {
            fileId: id, name: '', type: 0, props: EMPTY_OBJECT, ops: EMPTY_ARRAY,
            hooks: EMPTY_OBJECT, runtime: EMPTY_OBJECT,
            ...input, id, parentId, subId,
        };
        const renderKey = previous?.renderKey || this._renderKey(source, parent);
        const slotted = isSlottedNode(source);
        const previouslySlotted = previous ? isSlottedNode(previous) : false;
        this._assertSlotAvailable(parentId, subId, id, slotted);
        const owner = this._nodesByRenderKey.get(renderKey);
        if( owner && owner.id !== id )
            throw new Error(`renderKey ${renderKey} is already owned by node ${owner.id}`);

        const generation = previous?.generation || this._generationCandidate(renderKey);
        const next = nodeSnapshot(source, { id, parentId, subId, renderKey, generation }, previous);
        if( previous && nodeFieldsEqual(previous, next) ) return previous;
        const categories = dirty === null ? inferDirty(previous, next) : normalizeDirty(dirty);

        if( !previous ) this._generationByRenderKey.set(renderKey, generation);
        if( previous ) this._unindex(previous);
        if( !previous ) {
            this._insertIntoParent(parentId, id, subId, slotted);
            this._markOrderParent(parentId);
            if( parent ) this._mark(parent.renderKey, [TREE_DIRTY.TOPOLOGY, TREE_DIRTY.ORDER]);
        } else if( previous.parentId !== parentId ) {
            const oldParent = this._nodesById.get(previous.parentId);
            this._removeFromParent(
                previous.parentId, id, previous.subId, previouslySlotted);
            this._insertIntoParent(parentId, id, subId, slotted);
            this._markOrderParent(previous.parentId, previous);
            this._markOrderParent(parentId);
            if( oldParent ) this._mark(oldParent.renderKey, [
                TREE_DIRTY.TOPOLOGY, TREE_DIRTY.ORDER, TREE_DIRTY.GEOMETRY,
            ]);
            if( parent ) this._mark(parent.renderKey, [
                TREE_DIRTY.TOPOLOGY, TREE_DIRTY.ORDER, TREE_DIRTY.GEOMETRY,
            ]);
        } else if( previous.subId !== subId || previouslySlotted !== slotted ) {
            this._removeSubId(previous.parentId, previous.subId, id, previouslySlotted);
            this._insertSubId(parentId, subId, id, slotted);
        }
        this._nodesById.set(id, next);
        this._nodesByRenderKey.set(renderKey, next);
        this._index(next);
        this._changedKeys.add(renderKey);

        this._mark(renderKey, categories);
        if( !previous ) this._markOrderParent(parentId);
        this.mutationVersion++;
        return next;
    }

    updateNode(id, patch, options = {}) {
        this._requireMutable();
        const current = this.requireWorkingNode(id);
        const value = typeof patch === 'function' ? patch(current) : patch;
        if( !value || typeof value !== 'object' ) throw new TypeError('node patch must be an object');
        return this.upsertNode({ ...value, id: current.id }, options);
    }

    patchProps(id, patch, { dirty = null } = {}) {
        this._requireMutable();
        const current = this.requireWorkingNode(id);
        const value = typeof patch === 'function' ? patch(current.props) : patch;
        if( !value || typeof value !== 'object' || Array.isArray(value) )
            throw new TypeError('props patch must be an object');
        let changed = false;
        for( const [key, next] of Object.entries(value) ) {
            if( !Object.is(current.props[key], next) ) { changed = true; break; }
        }
        if( !changed ) return current;
        const props = { ...current.props, ...value };
        const categories = dirty === null ? inferPropDirty(Object.keys(value)) : dirty;
        return this.upsertNode({ id: current.id, props }, { dirty: categories });
    }

    setGeometry(id, geometry) {
        return this.upsertNode({ id: nodeId(id), geometry }, { dirty: [TREE_DIRTY.GEOMETRY] });
    }

    removeNode(id, { recursive = true } = {}) {
        this._requireMutable();
        id = nodeId(id);
        const node = this._nodesById.get(id);
        if( !node ) return false;
        const firstChildren = this._childrenByParent.get(id);
        if( firstChildren?.size && !recursive )
            throw new Error(`UI tree node ${id} still has ${firstChildren.size} children`);
        const pending = [node];
        const preorder = [];
        while( pending.length ) {
            const current = pending.pop();
            preorder.push(current);
            const children = this._childrenByParent.get(current.id);
            if( !children ) continue;
            for( const childId of children.keys() ) pending.push(this._nodesById.get(childId));
        }
        const doomedIds = new Set(preorder.map((current) => current.id));
        for( let index = preorder.length - 1; index >= 0; index-- )
            this._removeOneNode(preorder[index], doomedIds);
        this.mutationVersion++;
        return true;
    }

    _removeOneNode(node, doomedIds) {
        const id = node.id;
        const parentSurvives = node.parentId === null || !doomedIds.has(node.parentId);
        if( parentSurvives ) {
            this._removeFromParent(node.parentId, id, node.subId, isSlottedNode(node));
            this._markOrderParent(node.parentId, node);
        }
        this._childrenByParent.delete(id);
        this._childBySubIdByParent.delete(id);
        this._unindex(node);
        this._nodesById.delete(id);
        this._nodesByRenderKey.delete(node.renderKey);
        this._changedKeys.add(node.renderKey);
        const parent = parentSurvives && node.parentId !== null
            ? this._nodesById.get(node.parentId) : null;
        if( parent ) this._mark(parent.renderKey, [
            TREE_DIRTY.TOPOLOGY, TREE_DIRTY.ORDER, TREE_DIRTY.GEOMETRY,
        ]);
    }

    setChildOrder(parentId, orderedChildIds) {
        this._requireMutable();
        parentId = nullableNodeId(parentId);
        if( parentId !== null && !this._nodesById.has(parentId) )
            throw new Error(`UI tree parent ${parentId} does not exist`);
        if( !Array.isArray(orderedChildIds) ) throw new TypeError('child order must be an array');
        const currentMap = this._childrenByParent.get(parentId);
        const current = currentMap ? [...currentMap.keys()] : [];
        const next = orderedChildIds.map(nodeId);
        if( next.length !== current.length || new Set(next).size !== next.length ||
            next.some((id) => this._nodesById.get(id)?.parentId !== parentId) )
            throw new Error('child order must contain every direct child exactly once');
        if( arraysEqual(current, next) ) return false;
        this._childrenByParent.set(parentId, new Map(next.map((id) => [id, true])));
        this._markOrderParent(parentId);
        if( parentId !== null ) this._mark(this._nodesById.get(parentId).renderKey, [
            TREE_DIRTY.ORDER, TREE_DIRTY.GEOMETRY,
        ]);
        else for( const id of next )
            this._mark(this._nodesById.get(id).renderKey, [TREE_DIRTY.GEOMETRY]);
        this.mutationVersion++;
        return true;
    }

    markDirty(idOrRenderKey, categories) {
        this._requireMutable();
        const node = this._resolveWorking(idOrRenderKey);
        if( !node ) throw new Error(`unknown UI tree node ${String(idOrRenderKey)}`);
        this._mark(node.renderKey, normalizeDirty(categories));
        this.mutationVersion++;
        return node;
    }

    advanceMutationVersion(count = 1) {
        this._requireMutable();
        count = integer(count, 'mutation count');
        if( count < 0 ) throw new RangeError('mutation count cannot be negative');
        this.mutationVersion += count;
        return this.mutationVersion;
    }

    setViewport(viewport) {
        this._requireMutable();
        const next = immutableValue(viewport);
        if( shallowValueEqual(this._workingViewport, next) ) return false;
        this._workingViewport = next;
        this._viewportChanged = true;
        this.mutationVersion++;
        return true;
    }

    setInteraction(interaction) {
        this._requireMutable();
        const next = immutableValue(interaction);
        if( shallowValueEqual(this._workingInteraction, next) ) return false;
        this._workingInteraction = next;
        this._interactionChanged = true;
        this.mutationVersion++;
        return true;
    }

    workingNode(idOrRenderKey) {
        return this._resolveWorking(idOrRenderKey);
    }

    requireWorkingNode(idOrRenderKey) {
        const node = this._resolveWorking(idOrRenderKey);
        if( !node ) throw new Error(`unknown UI tree node ${String(idOrRenderKey)}`);
        return node;
    }

    committedNode(idOrRenderKey) {
        return typeof idOrRenderKey === 'number'
            ? this._committedNodesById.get(idOrRenderKey) || null
            : this._committedNodesByRenderKey.get(String(idOrRenderKey)) || null;
    }

    getNodeSnapshot(renderKey) {
        return this._committedNodesByRenderKey.get(String(renderKey)) || null;
    }

    getRootsSnapshot() {
        return this._committedOrderByParentKey.get(null) || EMPTY_ARRAY;
    }

    getRoots() {
        return this.getRootsSnapshot();
    }

    getNode(renderKey) {
        return this.getNodeSnapshot(renderKey);
    }

    getOrderSnapshot(parentRenderKey = null) {
        return this._committedOrderByParentKey.get(parentRenderKey) || EMPTY_ARRAY;
    }

    getChildren(parentRenderKey = null) {
        return this.getOrderSnapshot(parentRenderKey);
    }

    workingChildren(parentId = null) {
        parentId = nullableNodeId(parentId);
        const children = this._childrenByParent.get(parentId);
        return Object.freeze([...(children?.keys() || EMPTY_ARRAY)]
            .map((id) => this._nodesById.get(id)).filter(Boolean));
    }

    childAt(parentId, subId) {
        parentId = nullableNodeId(parentId);
        subId = signedInteger(subId, 'subId');
        const id = this._childBySubIdByParent.get(parentId)?.get(subId);
        return id === undefined ? null : this._nodesById.get(id) || null;
    }

    findByFileId(fileId) {
        return this._nodesForIndex(this._idsByFileId, indexKey(fileId));
    }

    findByName(name) {
        return this._nodesForIndex(this._idsByName, String(name));
    }

    ref(idOrRenderKey) {
        const node = this._resolveWorking(idOrRenderKey);
        return node ? Object.freeze({
            id: node.id, renderKey: node.renderKey, generation: node.generation,
        }) : null;
    }

    resolveRef(ref, { committed = false } = {}) {
        if( !ref || typeof ref !== 'object' ) return null;
        const node = committed ? this.committedNode(ref.renderKey ?? ref.id)
            : this._resolveWorking(ref.renderKey ?? ref.id);
        return node && node.id === ref.id && node.generation === ref.generation ? node : null;
    }

    subscribeNode(renderKey, listener) {
        return subscribeMap(this._nodeListeners, String(renderKey), listener);
    }

    subscribeOrder(parentRenderKey, listener) {
        const key = parentRenderKey === null ? null : String(parentRenderKey);
        return subscribeMap(this._orderListeners, key, listener);
    }

    _requireTransaction(token, status = null) {
        if( !this._transaction ) throw new Error('no UI tree transaction is active');
        if( token !== this._transaction.token ) throw new Error(`stale UI tree transaction token ${token}`);
        if( status && this._transaction.status !== status )
            throw new Error(`UI tree transaction ${token} is ${this._transaction.status}, not ${status}`);
    }

    _requireMutable() {
        if( this._transaction?.status !== 'active' )
            throw new Error(this._transaction
                ? `UI tree transaction ${this._transaction.token} is ${this._transaction.status}`
                : 'UI tree mutation requires an active transaction');
    }

    _renderKey(source, parent) {
        const explicit = source.renderKey ?? source.logicalKey;
        if( explicit !== undefined && explicit !== null && String(explicit) !== '' )
            return String(explicit);
        let logical;
        if( parent && isSlottedNode(source) )
            logical = `slot|${atom(parent.renderKey)}|${source.subId}`;
        else if( source.fileId !== undefined && source.fileId !== null )
            logical = `file|${atom(source.fileId)}`;
        else if( parent )
            logical = `child|${atom(parent.renderKey)}|${atom(source.name || source.id)}`;
        else logical = `root|${atom(source.name || source.id)}`;
        let key = this._renderKeyByLogicalSlot.get(logical);
        if( !key ) {
            key = `ui:${logical}`;
            this._renderKeyByLogicalSlot.set(logical, key);
        }
        return key;
    }

    _generationCandidate(renderKey) {
        return (this._generationByRenderKey.get(renderKey) || 0) + 1;
    }

    _assertNoCycle(id, parentId) {
        for( let cursor = parentId; cursor !== null; ) {
            if( cursor === id ) throw new Error(`reparenting node ${id} would create a cycle`);
            cursor = this._nodesById.get(cursor)?.parentId ?? null;
        }
    }

    _assertSlotAvailable(parentId, subId, id, slotted) {
        if( !slotted ) return;
        const childId = this._childBySubIdByParent.get(parentId)?.get(subId);
        if( childId !== undefined && childId !== id )
            throw new Error(`parent ${parentId ?? '<root>'} already has child subId ${subId}`);
    }

    _insertIntoParent(parentId, id, subId, slotted) {
        let children = this._childrenByParent.get(parentId);
        if( !children ) this._childrenByParent.set(parentId, children = new Map());
        children.set(id, true);
        this._insertSubId(parentId, subId, id, slotted);
    }

    _removeFromParent(parentId, id, subId, slotted) {
        const children = this._childrenByParent.get(parentId);
        children?.delete(id);
        this._removeSubId(parentId, subId, id, slotted);
    }

    _insertSubId(parentId, subId, id, slotted) {
        if( !slotted ) return;
        let slots = this._childBySubIdByParent.get(parentId);
        if( !slots ) this._childBySubIdByParent.set(parentId, slots = new Map());
        slots.set(subId, id);
    }

    _removeSubId(parentId, subId, id, slotted) {
        if( !slotted ) return;
        const slots = this._childBySubIdByParent.get(parentId);
        if( slots?.get(subId) === id ) slots.delete(subId);
        if( slots?.size === 0 ) this._childBySubIdByParent.delete(parentId);
    }

    _markOrderParent(parentId, removedParent = null) {
        const key = parentId === null ? null
            : (this._nodesById.get(parentId)?.renderKey ||
                (removedParent?.id === parentId ? removedParent.renderKey : null));
        if( parentId === null || key !== null ) this._changedOrderParents.add(key);
    }

    _index(node) {
        addIndex(this._idsByFileId, indexKey(node.fileId), node.id);
        if( node.name ) addIndex(this._idsByName, node.name, node.id);
    }

    _unindex(node) {
        removeIndex(this._idsByFileId, indexKey(node.fileId), node.id);
        if( node.name ) removeIndex(this._idsByName, node.name, node.id);
    }

    _nodesForIndex(index, key) {
        return Object.freeze([...(index.get(key) || EMPTY_ARRAY)]
            .map((id) => this._nodesById.get(id)).filter(Boolean));
    }

    _resolveWorking(value) {
        return typeof value === 'number' ? this._nodesById.get(value) || null
            : this._nodesByRenderKey.get(String(value)) || null;
    }

    _mark(renderKey, categories) {
        for( const category of categories ) this._dirty[category].add(renderKey);
    }

    _createPublication() {
        const upsert = [];
        const remove = [];
        for( const key of this._changedKeys ) {
            const working = this._nodesByRenderKey.get(key) || null;
            const committed = this._committedNodesByRenderKey.get(key) || null;
            if( working && working !== committed ) upsert.push(working);
            else if( !working && committed ) remove.push(key);
        }
        /* Set insertion order is the VM's deterministic mutation order. The
         * mirror validates the complete id set before applying it, so it does
         * not require a costly parent-before-child sort here. */

        const order = [];
        for( const parentRenderKey of this._changedOrderParents ) {
            if( parentRenderKey !== null && !this._nodesByRenderKey.has(parentRenderKey) ) continue;
            const children = this._workingOrderForKey(parentRenderKey);
            const previous = this._committedOrderByParentKey.get(parentRenderKey) || EMPTY_ARRAY;
            if( !arraysEqual(children, previous) ) order.push(Object.freeze({
                parentRenderKey,
                children: Object.freeze([...children]),
            }));
        }
        order.sort((left, right) => parentKeyRank(left.parentRenderKey, right.parentRenderKey));

        const dirty = {};
        let hasDirty = false;
        for( const category of DIRTY_CATEGORIES ) {
            const values = [...this._dirty[category]]
                .filter((key) => this._nodesByRenderKey.has(key));
            dirty[category] = Object.freeze(values);
            hasDirty ||= values.length > 0;
        }
        const dirtyGeometryRoots = Object.freeze(this._geometryRoots(dirty.geometry));
        const treeChanged = upsert.length || remove.length || order.length || hasDirty;
        const viewportChanged = (this._viewportChanged &&
            !shallowValueEqual(this._workingViewport, this._committedViewport)) ||
            (this.commitRevision === 0 && Boolean(treeChanged) && this._workingViewport !== null);
        const interactionChanged = (this._interactionChanged &&
            !shallowValueEqual(this._workingInteraction, this._committedInteraction)) ||
            (this.commitRevision === 0 && Boolean(treeChanged) && this._workingInteraction !== null);
        const changed = treeChanged ||
            viewportChanged || interactionChanged;
        if( !changed ) {
            this._clearPending();
            return null;
        }

        const baseRevision = this.commitRevision;
        const revision = baseRevision + 1;
        const delta = Object.freeze({
            schema: TREE_DELTA_SCHEMA,
            baseRevision,
            revision,
            mutationVersion: this.mutationVersion,
            upsert: Object.freeze(upsert),
            remove: Object.freeze(remove),
            order: Object.freeze(order),
            reorderParents: Object.freeze(order.map((entry) => entry.parentRenderKey)),
            dirty: Object.freeze(dirty),
            dirtyGeometryRoots,
            ...(viewportChanged ? { viewport: this._workingViewport } : {}),
            ...(interactionChanged ? { interaction: this._workingInteraction } : {}),
        });

        for( const key of remove ) {
            const old = this._committedNodesByRenderKey.get(key);
            if( old ) this._committedNodesById.delete(old.id);
            this._committedNodesByRenderKey.delete(key);
            this._committedOrderByParentKey.delete(key);
        }
        for( const node of upsert ) {
            const old = this._committedNodesByRenderKey.get(node.renderKey);
            if( old ) this._committedNodesById.delete(old.id);
        }
        for( const node of upsert ) {
            this._committedNodesByRenderKey.set(node.renderKey, node);
            this._committedNodesById.set(node.id, node);
        }
        for( const entry of order )
            this._committedOrderByParentKey.set(entry.parentRenderKey, entry.children);
        if( viewportChanged ) this._committedViewport = this._workingViewport;
        if( interactionChanged ) this._committedInteraction = this._workingInteraction;
        this.commitRevision = revision;
        const roots = this._committedOrderByParentKey.get(null) || EMPTY_ARRAY;
        this._committedSnapshot = treeSnapshot(revision, this.mutationVersion, roots,
            this._committedViewport, this._committedInteraction);

        const affectedKeys = new Set([
            ...upsert.map((node) => node.renderKey), ...remove,
            ...DIRTY_CATEGORIES.flatMap((category) => dirty[category]),
        ]);
        const affectedOrderParents = new Set(order.map((entry) => entry.parentRenderKey));
        this._clearPending();
        return { delta, affectedKeys, affectedOrderParents };
    }

    _workingOrderForKey(parentRenderKey) {
        const parentId = parentRenderKey === null ? null
            : this._nodesByRenderKey.get(parentRenderKey)?.id;
        if( parentId === undefined ) return EMPTY_ARRAY;
        const children = this._childrenByParent.get(parentId);
        return [...(children?.keys() || EMPTY_ARRAY)]
            .map((id) => this._nodesById.get(id)?.renderKey).filter(Boolean);
    }

    _geometryRoots(keys) {
        const set = new Set(keys);
        const roots = [];
        for( const key of keys ) {
            let node = this._nodesByRenderKey.get(key);
            let covered = false;
            while( node?.parentId !== null ) {
                node = this._nodesById.get(node.parentId);
                if( node && set.has(node.renderKey) ) { covered = true; break; }
            }
            if( !covered ) roots.push(key);
        }
        return roots.sort((left, right) => {
            const leftNode = this._nodesByRenderKey.get(left);
            const rightNode = this._nodesByRenderKey.get(right);
            return this._nodeDepth(leftNode) - this._nodeDepth(rightNode) || left.localeCompare(right);
        });
    }

    _nodeDepth(node) {
        let depth = 0;
        while( node?.parentId !== null ) {
            node = this._nodesById.get(node.parentId);
            depth++;
        }
        return depth;
    }

    _clearPending() {
        this._changedKeys.clear();
        this._changedOrderParents.clear();
        for( const category of DIRTY_CATEGORIES ) this._dirty[category].clear();
        this._viewportChanged = false;
        this._interactionChanged = false;
    }
}

/* Main-thread mirror. applyDelta validates the complete change set before it
 * mutates any map, installs it synchronously, then emits exactly one revision. */
export class ViewTreeStore {
    constructor({ viewport = null, interaction = null, onListenerError = reportListenerError } = {}) {
        this.revision = 0;
        this.mutationVersion = 0;
        this._nodesById = new Map();
        this._nodesByRenderKey = new Map();
        this._orderByParentKey = new Map([[null, EMPTY_ARRAY]]);
        this._viewport = immutableValue(viewport);
        this._interaction = immutableValue(interaction);
        this._snapshot = treeSnapshot(0, 0, EMPTY_ARRAY, this._viewport, this._interaction);
        this._listeners = new Set();
        this._nodeListeners = new Map();
        this._orderListeners = new Map();
        this._onListenerError = onListenerError;
        this._lastDelta = null;
        this.subscribe = (listener) => subscribeSet(this._listeners, listener);
        this.getSnapshot = () => this._snapshot;
        this.getServerSnapshot = this.getSnapshot;
    }

    get lastDelta() { return this._lastDelta; }

    getNodeSnapshot(renderKey) {
        return this._nodesByRenderKey.get(String(renderKey)) || null;
    }

    getRootsSnapshot() {
        return this._orderByParentKey.get(null) || EMPTY_ARRAY;
    }

    getRoots() {
        return this.getRootsSnapshot();
    }

    getNode(renderKey) {
        return this.getNodeSnapshot(renderKey);
    }

    getOrderSnapshot(parentRenderKey = null) {
        return this._orderByParentKey.get(parentRenderKey) || EMPTY_ARRAY;
    }

    getChildren(parentRenderKey = null) {
        return this.getOrderSnapshot(parentRenderKey);
    }

    subscribeNode(renderKey, listener) {
        return subscribeMap(this._nodeListeners, String(renderKey), listener);
    }

    subscribeOrder(parentRenderKey, listener) {
        const key = parentRenderKey === null ? null : String(parentRenderKey);
        return subscribeMap(this._orderListeners, key, listener);
    }

    applyDelta(delta) {
        validateDelta(delta, this.revision, this.mutationVersion);
        const removals = new Set(delta.remove);
        const adopted = delta.upsert.map((node) => nodeSnapshot(node, {
            id: nodeId(node.id), parentId: nullableNodeId(node.parentId),
            subId: signedInteger(node.subId, 'subId'), renderKey: String(node.renderKey),
            generation: positiveInteger(node.generation, 'generation'),
        }, null));
        const adoptedByKey = new Map(adopted.map((node) => [node.renderKey, node]));
        const futureNode = (key) => adoptedByKey.get(key) ||
            (!removals.has(key) ? this._nodesByRenderKey.get(key) : null);
        const hasFutureKey = (key) => adoptedByKey.has(key) ||
            (!removals.has(key) && this._nodesByRenderKey.has(key));
        const retiredIds = new Set();
        for( const key of removals ) {
            const old = this._nodesByRenderKey.get(key);
            if( old ) retiredIds.add(old.id);
        }
        for( const node of adopted ) {
            const old = this._nodesByRenderKey.get(node.renderKey);
            if( old ) retiredIds.add(old.id);
        }
        const adoptedById = new Map();
        for( const node of adopted ) {
            const adoptedOwner = adoptedById.get(node.id);
            if( adoptedOwner && adoptedOwner.renderKey !== node.renderKey )
                throw new Error(`tree delta node id ${node.id} has two render keys`);
            const currentOwner = this._nodesById.get(node.id);
            if( currentOwner && !retiredIds.has(node.id) &&
                currentOwner.renderKey !== node.renderKey )
                throw new Error(`tree delta node id ${node.id} has two render keys`);
            adoptedById.set(node.id, node);
        }
        const futureNodeById = (id) => adoptedById.get(id) ||
            (!retiredIds.has(id) ? this._nodesById.get(id) : null);
        for( const node of adopted ) if( node.parentId !== null && !futureNodeById(node.parentId) )
            throw new Error(`tree delta node ${node.renderKey} has missing parent id ${node.parentId}`);
        const verifiedAncestors = new Set();
        const traversalMarks = new Map();
        let traversal = 0;
        for( const node of adopted ) {
            traversal++;
            const path = [];
            for( let current = node; current && !verifiedAncestors.has(current.id); ) {
                if( traversalMarks.get(current.id) === traversal )
                    throw new Error(`tree delta node ${node.renderKey} creates a parent cycle`);
                traversalMarks.set(current.id, traversal);
                path.push(current.id);
                if( current.parentId === null ) break;
                const parent = futureNodeById(current.parentId);
                if( !parent ) throw new Error(
                    `tree delta node ${node.renderKey} has missing ancestor ${current.parentId}`);
                current = parent;
            }
            for( const id of path ) verifiedAncestors.add(id);
        }
        for( const key of removals ) {
            for( const childKey of this._orderByParentKey.get(key) || EMPTY_ARRAY ) {
                const child = futureNode(childKey);
                if( child && child.parentId !== null && !futureNodeById(child.parentId) )
                    throw new Error(`tree delta leaves ${childKey} below removed parent ${key}`);
            }
        }
        for( const entry of delta.order ) {
            if( entry.parentRenderKey !== null && !hasFutureKey(entry.parentRenderKey) )
                throw new Error(`tree delta orders missing parent ${entry.parentRenderKey}`);
            if( !Array.isArray(entry.children) || new Set(entry.children).size !== entry.children.length ||
                entry.children.some((key) => !hasFutureKey(key)) )
                throw new Error('tree delta child order contains missing or duplicate keys');
            const parentId = entry.parentRenderKey === null ? null : futureNode(entry.parentRenderKey)?.id;
            if( entry.children.some((key) => futureNode(key)?.parentId !== parentId) )
                throw new Error('tree delta child order contains a node from another parent');
        }

        for( const key of removals ) {
            const old = this._nodesByRenderKey.get(key);
            if( old ) this._nodesById.delete(old.id);
            this._nodesByRenderKey.delete(key);
            this._orderByParentKey.delete(key);
        }
        for( const node of adopted ) {
            const old = this._nodesByRenderKey.get(node.renderKey);
            if( old ) this._nodesById.delete(old.id);
        }
        for( const node of adopted ) {
            this._nodesByRenderKey.set(node.renderKey, node);
            this._nodesById.set(node.id, node);
        }
        for( const entry of delta.order ) this._orderByParentKey.set(
            entry.parentRenderKey, Object.freeze([...entry.children]));
        if( Object.hasOwn(delta, 'viewport') ) this._viewport = immutableValue(delta.viewport);
        if( Object.hasOwn(delta, 'interaction') ) this._interaction = immutableValue(delta.interaction);
        this.revision = delta.revision;
        this.mutationVersion = delta.mutationVersion;
        this._lastDelta = delta;
        this._snapshot = treeSnapshot(this.revision, this.mutationVersion,
            this._orderByParentKey.get(null) || EMPTY_ARRAY, this._viewport, this._interaction);

        const affectedKeys = new Set([
            ...delta.remove, ...adopted.map((node) => node.renderKey),
            ...DIRTY_CATEGORIES.flatMap((category) => delta.dirty?.[category] || EMPTY_ARRAY),
        ]);
        notifySet(this._listeners, delta, this._onListenerError);
        for( const key of affectedKeys )
            notifySet(this._nodeListeners.get(key), delta, this._onListenerError);
        for( const entry of delta.order )
            notifySet(this._orderListeners.get(entry.parentRenderKey), delta, this._onListenerError);
        return this._snapshot;
    }
}

function createDirtySets() {
    return Object.fromEntries(DIRTY_CATEGORIES.map((category) => [category, new Set()]));
}

function transactionState(token, status, metadata, reason = null) {
    return Object.freeze({ token, status, metadata, reason });
}

function treeSnapshot(revision, mutationVersion, roots, viewport, interaction) {
    return Object.freeze({
        schema: UI_TREE_SCHEMA, revision, commitRevision: revision, mutationVersion,
        roots, viewport, interaction,
    });
}

function nodeSnapshot(source, identity, previous) {
    const output = {};
    for( const [key, value] of Object.entries(source) ) {
        if( key === 'logicalKey' || key === 'generation' || key === 'renderKey' || key === 'id' ||
            key === 'parentId' || key === 'subId' ) continue;
        output[key] = previous && value === previous[key] ? value : immutableValue(value);
    }
    output.id = identity.id;
    output.renderKey = identity.renderKey;
    output.generation = identity.generation;
    output.parentId = identity.parentId;
    output.subId = identity.subId;
    output.fileId ??= identity.id;
    output.name = String(output.name ?? '');
    output.type = integer(output.type ?? 0, 'type');
    output.props ??= EMPTY_OBJECT;
    output.ops ??= EMPTY_ARRAY;
    output.hooks ??= EMPTY_OBJECT;
    output.runtime ??= EMPTY_OBJECT;
    if( !plainRecord(output.props) ) throw new TypeError('node props must be a plain object');
    if( !Array.isArray(output.ops) ) throw new TypeError('node ops must be an array');
    if( !plainRecord(output.hooks) ) throw new TypeError('node hooks must be a plain object');
    if( !plainRecord(output.runtime) ) throw new TypeError('node runtime must be a plain object');
    if( output.dynamic === undefined && output.runtime.dynamic === true ) output.dynamic = true;
    if( output.dynamic !== undefined && typeof output.dynamic !== 'boolean' )
        throw new TypeError('node dynamic must be boolean');
    if( (typeof output.fileId !== 'number' || !Number.isSafeInteger(output.fileId)) &&
        typeof output.fileId !== 'string' )
        throw new TypeError('node fileId must be a number or string');
    return Object.freeze(output);
}

function plainRecord(value) {
    if( value === null || typeof value !== 'object' || Array.isArray(value) ) return false;
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function nodeFieldsEqual(left, right) {
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    if( leftKeys.length !== rightKeys.length ) return false;
    return leftKeys.every((key) => Object.hasOwn(right, key) && Object.is(left[key], right[key]));
}

function immutableValue(value, seen = null) {
    if( value === null || typeof value !== 'object' || TRUSTED_IMMUTABLE.has(value) ) return value;
    if( !Array.isArray(value) && Object.getPrototypeOf(value) !== Object.prototype &&
        Object.getPrototypeOf(value) !== null ) return value;
    seen ||= new WeakMap();
    if( seen.has(value) ) return seen.get(value);
    const copy = Array.isArray(value) ? [] : Object.create(Object.getPrototypeOf(value));
    seen.set(value, copy);
    if( Array.isArray(value) ) for( const entry of value ) copy.push(immutableValue(entry, seen));
    else for( const [key, entry] of Object.entries(value) ) copy[key] = immutableValue(entry, seen);
    Object.freeze(copy);
    TRUSTED_IMMUTABLE.add(copy);
    return copy;
}

function isSlottedNode(node) {
    return node.subId >= 0 || node.dynamic === true || node.runtime?.dynamic === true;
}

function inferDirty(previous, next) {
    if( !previous ) return [
        TREE_DIRTY.PAINT, TREE_DIRTY.GEOMETRY, TREE_DIRTY.VISIBILITY,
        TREE_DIRTY.TOPOLOGY,
    ];
    const result = new Set([TREE_DIRTY.PAINT]);
    if( previous.parentId !== next.parentId || previous.subId !== next.subId ) {
        result.add(TREE_DIRTY.TOPOLOGY);
        result.add(TREE_DIRTY.ORDER);
        result.add(TREE_DIRTY.GEOMETRY);
    }
    if( previous.geometry !== next.geometry ) result.add(TREE_DIRTY.GEOMETRY);
    if( previous.props !== next.props ) for( const category of inferPropDirty(
        changedObjectKeys(previous.props, next.props))) result.add(category);
    if( previous.runtime !== next.runtime ) {
        result.add(TREE_DIRTY.INTERACTION);
        if( previous.runtime?.hidden !== next.runtime?.hidden ) result.add(TREE_DIRTY.VISIBILITY);
    }
    return [...result];
}

function inferPropDirty(keys) {
    const result = new Set([TREE_DIRTY.PAINT]);
    for( const key of keys ) {
        if( GEOMETRY_PROPS.has(key) ) result.add(TREE_DIRTY.GEOMETRY);
        if( VISIBILITY_PROPS.has(key) ) {
            result.add(TREE_DIRTY.VISIBILITY);
            result.add(TREE_DIRTY.GEOMETRY);
        }
    }
    return [...result];
}

function changedObjectKeys(left = EMPTY_OBJECT, right = EMPTY_OBJECT) {
    const keys = new Set([...Object.keys(left), ...Object.keys(right)]);
    return [...keys].filter((key) => !Object.is(left[key], right[key]));
}

function normalizeDirty(value) {
    const values = typeof value === 'string' ? [value] : [...(value || EMPTY_ARRAY)];
    for( const category of values ) if( !DIRTY_CATEGORY_SET.has(category) )
        throw new Error(`unknown UI tree dirty category ${category}`);
    return values;
}

function validateDelta(delta, revision, mutationVersion) {
    if( !delta || delta.schema !== TREE_DELTA_SCHEMA ) throw new Error('invalid UI tree delta schema');
    if( delta.baseRevision !== revision )
        throw new Error(`tree delta base revision ${delta.baseRevision} does not match ${revision}`);
    if( delta.revision !== revision + 1 ) throw new Error('tree delta revision must advance exactly once');
    if( !Array.isArray(delta.upsert) || !Array.isArray(delta.remove) || !Array.isArray(delta.order) )
        throw new Error('tree delta collections are malformed');
    if( !Number.isSafeInteger(delta.mutationVersion) || delta.mutationVersion < mutationVersion )
        throw new Error('tree delta mutation version regressed or is invalid');
    if( delta.remove.some((key) => typeof key !== 'string') )
        throw new Error('tree delta removal keys must be strings');
    if( new Set(delta.remove).size !== delta.remove.length )
        throw new Error('tree delta contains duplicate removals');
    if( delta.upsert.some((node) => !node || typeof node.renderKey !== 'string' || !node.renderKey) )
        throw new Error('tree delta upserts require nonempty string render keys');
    const upsertKeys = delta.upsert.map((node) => String(node.renderKey));
    if( new Set(upsertKeys).size !== upsertKeys.length )
        throw new Error('tree delta contains duplicate upserts');
    const orderParents = delta.order.map((entry) => entry?.parentRenderKey);
    if( orderParents.some((key) => key !== null && typeof key !== 'string') )
        throw new Error('tree delta order parent keys are malformed');
    if( new Set(orderParents).size !== orderParents.length )
        throw new Error('tree delta contains duplicate parent orders');
    if( !delta.dirty || DIRTY_CATEGORIES.some((category) =>
        !Array.isArray(delta.dirty[category]) ||
        delta.dirty[category].some((key) => typeof key !== 'string')) )
        throw new Error('tree delta dirty categories are malformed');
    if( !Array.isArray(delta.dirtyGeometryRoots) ||
        delta.dirtyGeometryRoots.some((key) => typeof key !== 'string') )
        throw new Error('tree delta geometry roots are malformed');
}

function nodeId(value) {
    return integer(value, 'node id');
}

function nullableNodeId(value) {
    return value === null || value === undefined ? null : nodeId(value);
}

function integer(value, label) {
    if( !Number.isSafeInteger(value) ) throw new TypeError(`${label} must be a safe integer`);
    return value;
}

function signedInteger(value, label) {
    value = integer(value, label);
    if( value < -0x80000000 || value > 0x7fffffff )
        throw new RangeError(`${label} must fit a signed 32-bit integer`);
    return value;
}

function positiveInteger(value, label) {
    value = integer(value, label);
    if( value < 1 ) throw new TypeError(`${label} must be positive`);
    return value;
}

function atom(value) {
    const text = String(value);
    return `${typeof value}:${text.length}:${text}`;
}

function indexKey(value) {
    return `${typeof value}:${String(value)}`;
}

function addIndex(index, key, id) {
    let values = index.get(key);
    if( !values ) index.set(key, values = new Set());
    values.add(id);
}

function removeIndex(index, key, id) {
    const values = index.get(key);
    if( !values ) return;
    values.delete(id);
    if( values.size === 0 ) index.delete(key);
}

function arraysEqual(left, right) {
    return left === right || left.length === right.length &&
        left.every((value, index) => value === right[index]);
}

function shallowValueEqual(left, right) {
    if( Object.is(left, right) ) return true;
    if( !left || !right || typeof left !== 'object' || typeof right !== 'object' ||
        Array.isArray(left) !== Array.isArray(right) ) return false;
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    return leftKeys.length === rightKeys.length &&
        leftKeys.every((key) => Object.hasOwn(right, key) && Object.is(left[key], right[key]));
}

function parentKeyRank(left, right) {
    if( left === null ) return right === null ? 0 : -1;
    if( right === null ) return 1;
    return left.localeCompare(right);
}

function subscribeSet(set, listener) {
    if( typeof listener !== 'function' ) throw new TypeError('tree listener must be a function');
    set.add(listener);
    return () => set.delete(listener);
}

function subscribeMap(map, key, listener) {
    let listeners = map.get(key);
    if( !listeners ) map.set(key, listeners = new Set());
    const unsubscribe = subscribeSet(listeners, listener);
    return () => {
        const removed = unsubscribe();
        if( listeners.size === 0 ) map.delete(key);
        return removed;
    };
}

function notifySet(set, delta, onError) {
    if( !set ) return;
    for( const listener of [...set] ) {
        try { listener(delta); }
        catch( error ) { onError(error); }
    }
}

function reportListenerError(error) {
    queueMicrotask(() => { throw error; });
}
