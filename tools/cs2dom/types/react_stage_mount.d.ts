import type { ReactNode } from 'react';

export interface CommittedStageSnapshot {
    readonly session: number;
    readonly revision: number;
    readonly render: unknown | null;
    readonly patch: unknown | null;
}

export interface CommittedStageStore {
    subscribeStage(listener: () => void): () => void;
    getStageSnapshot(): CommittedStageSnapshot;
}

export interface RetainedInterfaceStageProps {
    store: CommittedStageStore;
    onCommit?: (snapshot: CommittedStageSnapshot, surface: HTMLDivElement | null) => void;
    className?: string;
}

export function assertCommittedStageStore<T extends CommittedStageStore>(store: T): T;
export function RetainedInterfaceStage(props: RetainedInterfaceStageProps): ReactNode;
export function mountRetainedInterfaceStage(
    container: Element | DocumentFragment,
    initialProps: RetainedInterfaceStageProps,
): {
    update(nextProps: Partial<RetainedInterfaceStageProps>): void;
    unmount(): void;
};
