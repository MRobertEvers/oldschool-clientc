import type { InterfacePreviewProps } from './react_tree_renderer.js';

export interface MountedInterfacePreview {
    update(props: Partial<InterfacePreviewProps>): void;
    unmount(): void;
}

export function mountInterfacePreview(
    container: Element,
    props: InterfacePreviewProps,
): MountedInterfacePreview;
