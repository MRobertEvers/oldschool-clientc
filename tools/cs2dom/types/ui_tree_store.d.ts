export type NodeId = number;
export type RenderKey = string;
export type DirtyCategory =
    | 'paint' | 'geometry' | 'visibility' | 'topology'
    | 'order' | 'interaction' | 'viewport';

export interface UITreeNodeSnapshot {
    readonly id: NodeId;
    readonly renderKey: RenderKey;
    readonly generation: number;
    readonly parentId: NodeId | null;
    readonly subId: number;
    readonly fileId: number | string;
    readonly name: string;
    readonly type: number;
    readonly dynamic?: boolean;
    readonly props: Readonly<Record<string, unknown>>;
    readonly ops: readonly unknown[];
    readonly hooks: Readonly<Record<string, unknown>>;
    readonly runtime: Readonly<Record<string, unknown>>;
    readonly geometry?: Readonly<Record<string, unknown>> | null;
    readonly [field: string]: unknown;
}

export interface UITreeNodeInput {
    readonly id: NodeId;
    readonly renderKey?: RenderKey;
    readonly logicalKey?: string;
    readonly parentId?: NodeId | null;
    readonly subId?: number;
    readonly fileId?: number | string;
    readonly name?: string;
    readonly type?: number;
    /** Marks every signed subId, including -1, as a stable dynamic slot. */
    readonly dynamic?: boolean;
    readonly props?: Readonly<Record<string, unknown>>;
    readonly ops?: readonly unknown[];
    readonly hooks?: Readonly<Record<string, unknown>>;
    readonly runtime?: Readonly<Record<string, unknown>>;
    readonly geometry?: Readonly<Record<string, unknown>> | null;
    readonly [field: string]: unknown;
}

export interface TreeOrderDelta {
    readonly parentRenderKey: RenderKey | null;
    readonly children: readonly RenderKey[];
}

export interface TreeDelta {
    readonly schema: 'cs2dom-tree-delta/1';
    readonly baseRevision: number;
    readonly revision: number;
    readonly mutationVersion: number;
    readonly upsert: readonly UITreeNodeSnapshot[];
    readonly remove: readonly RenderKey[];
    readonly order: readonly TreeOrderDelta[];
    readonly reorderParents: readonly (RenderKey | null)[];
    readonly dirty: Readonly<Record<DirtyCategory, readonly RenderKey[]>>;
    readonly dirtyGeometryRoots: readonly RenderKey[];
    /** HostRuntime extension: whether a renderer may project only dirty keys. */
    readonly projection?: 'dirty' | 'full' | 'none';
    readonly fallbackReason?: string;
    readonly viewport?: unknown;
    readonly interaction?: unknown;
}

export interface UITreeSnapshot {
    readonly schema: 'cs2dom-ui-tree/1';
    readonly revision: number;
    readonly commitRevision: number;
    readonly mutationVersion: number;
    readonly roots: readonly RenderKey[];
    readonly viewport: unknown;
    readonly interaction: unknown;
}

export interface UITreeRef {
    readonly id: NodeId;
    readonly renderKey: RenderKey;
    readonly generation: number;
}

export interface TreeTransactionState {
    readonly token: number;
    readonly status: 'active' | 'yielded';
    readonly metadata: unknown;
    readonly reason: unknown;
}

export interface TreeStoreOptions {
    viewport?: unknown;
    interaction?: unknown;
    onListenerError?: (error: unknown) => void;
}

export class UITreeStore {
    constructor(options?: TreeStoreOptions);
    readonly mutationVersion: number;
    readonly commitRevision: number;
    readonly transactionState: TreeTransactionState | null;
    readonly lastDelta: TreeDelta | null;

    beginTransaction(metadata?: unknown): number;
    yieldTransaction(token?: number, reason?: unknown): TreeTransactionState;
    resumeTransaction(token?: number): TreeTransactionState;
    abortTransaction(token?: number, reason?: unknown): Readonly<{
        token: number;
        status: 'aborted';
        reason: unknown;
        mutationVersion: number;
        commitRevision: number;
    }>;
    commitTransaction(token?: number): TreeDelta | null;
    consumeDelta(): TreeDelta | null;

    upsertNode(input: UITreeNodeInput, options?: {
        dirty?: DirtyCategory | readonly DirtyCategory[] | null;
    }): UITreeNodeSnapshot;
    updateNode(
        id: NodeId,
        patch: Partial<UITreeNodeInput> |
            ((node: UITreeNodeSnapshot) => Partial<UITreeNodeInput>),
        options?: { dirty?: DirtyCategory | readonly DirtyCategory[] | null },
    ): UITreeNodeSnapshot;
    patchProps(
        id: NodeId,
        patch: Readonly<Record<string, unknown>> |
            ((props: Readonly<Record<string, unknown>>) => Readonly<Record<string, unknown>>),
        options?: { dirty?: DirtyCategory | readonly DirtyCategory[] | null },
    ): UITreeNodeSnapshot;
    setGeometry(id: NodeId, geometry: Readonly<Record<string, unknown>> | null): UITreeNodeSnapshot;
    removeNode(id: NodeId, options?: { recursive?: boolean }): boolean;
    setChildOrder(parentId: NodeId | null, orderedChildIds: readonly NodeId[]): boolean;
    markDirty(
        idOrRenderKey: NodeId | RenderKey,
        categories: DirtyCategory | readonly DirtyCategory[],
    ): UITreeNodeSnapshot;
    advanceMutationVersion(count?: number): number;
    setViewport(viewport: unknown): boolean;
    setInteraction(interaction: unknown): boolean;

    workingNode(idOrRenderKey: NodeId | RenderKey): UITreeNodeSnapshot | null;
    requireWorkingNode(idOrRenderKey: NodeId | RenderKey): UITreeNodeSnapshot;
    committedNode(idOrRenderKey: NodeId | RenderKey): UITreeNodeSnapshot | null;
    workingChildren(parentId?: NodeId | null): readonly UITreeNodeSnapshot[];
    childAt(parentId: NodeId | null, subId: number): UITreeNodeSnapshot | null;
    findByFileId(fileId: number | string): readonly UITreeNodeSnapshot[];
    findByName(name: string): readonly UITreeNodeSnapshot[];
    ref(idOrRenderKey: NodeId | RenderKey): UITreeRef | null;
    resolveRef(ref: UITreeRef, options?: { committed?: boolean }): UITreeNodeSnapshot | null;

    getSnapshot(): UITreeSnapshot;
    getServerSnapshot(): UITreeSnapshot;
    getNodeSnapshot(renderKey: RenderKey): UITreeNodeSnapshot | null;
    getNode(renderKey: RenderKey): UITreeNodeSnapshot | null;
    getRootsSnapshot(): readonly RenderKey[];
    getRoots(): readonly RenderKey[];
    getOrderSnapshot(parentRenderKey?: RenderKey | null): readonly RenderKey[];
    getChildren(parentRenderKey?: RenderKey | null): readonly RenderKey[];
    subscribe(listener: (delta: TreeDelta) => void): () => boolean;
    subscribeNode(renderKey: RenderKey, listener: (delta: TreeDelta) => void): () => boolean;
    subscribeOrder(parentRenderKey: RenderKey | null, listener: (delta: TreeDelta) => void): () => boolean;
}

export class ViewTreeStore {
    constructor(options?: TreeStoreOptions);
    readonly revision: number;
    readonly mutationVersion: number;
    readonly lastDelta: TreeDelta | null;
    applyDelta(delta: TreeDelta): UITreeSnapshot;
    getSnapshot(): UITreeSnapshot;
    getServerSnapshot(): UITreeSnapshot;
    getNodeSnapshot(renderKey: RenderKey): UITreeNodeSnapshot | null;
    getNode(renderKey: RenderKey): UITreeNodeSnapshot | null;
    getRootsSnapshot(): readonly RenderKey[];
    getRoots(): readonly RenderKey[];
    getOrderSnapshot(parentRenderKey?: RenderKey | null): readonly RenderKey[];
    getChildren(parentRenderKey?: RenderKey | null): readonly RenderKey[];
    subscribe(listener: (delta: TreeDelta) => void): () => boolean;
    subscribeNode(renderKey: RenderKey, listener: (delta: TreeDelta) => void): () => boolean;
    subscribeOrder(parentRenderKey: RenderKey | null, listener: (delta: TreeDelta) => void): () => boolean;
}

export const UI_TREE_SCHEMA: 'cs2dom-ui-tree/1';
export const TREE_DELTA_SCHEMA: 'cs2dom-tree-delta/1';
export const TREE_DIRTY: Readonly<{
    PAINT: 'paint';
    GEOMETRY: 'geometry';
    VISIBILITY: 'visibility';
    TOPOLOGY: 'topology';
    ORDER: 'order';
    INTERACTION: 'interaction';
    VIEWPORT: 'viewport';
}>;

export function createUITreeStore(options?: TreeStoreOptions): UITreeStore;
export function createViewTreeStore(options?: TreeStoreOptions): ViewTreeStore;
