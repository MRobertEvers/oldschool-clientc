import type { CSSProperties, ComponentType, ReactNode } from 'react';

export type RenderKey = string;

export interface ViewNodeSnapshot {
    readonly renderKey: RenderKey;
    readonly type: number | string;
    readonly kind?: string;
    readonly name?: string;
    readonly rendererId?: string;
    readonly children?: readonly RenderKey[];
    readonly props: Readonly<Record<string, unknown>>;
    readonly layout?: Readonly<{ x: number; y: number; width: number; height: number }>;
    readonly geometry?: Readonly<{ x: number; y: number; width: number; height: number }>;
    readonly x?: number;
    readonly y?: number;
    readonly width?: number;
    readonly height?: number;
    readonly w?: number;
    readonly h?: number;
    readonly hidden?: boolean;
    readonly effectiveHidden?: boolean;
    readonly transparency?: number;
    readonly clipChildren?: boolean;
    readonly interactive?: boolean;
    readonly ops?: readonly unknown[];
    readonly events?: readonly unknown[];
    readonly hooks?: readonly unknown[];
    readonly tabIndex?: number;
    readonly className?: string;
    readonly ariaLabel?: string;
    readonly presentation?: Readonly<{ className?: string; style?: CSSProperties; spriteUrl?: string }>;
}

export interface ViewTreeStore {
    getRoots(): readonly RenderKey[];
    getNode(renderKey: RenderKey): ViewNodeSnapshot | null;
    subscribe(listener: () => void): () => void;
    getChildren?(parentRenderKey: RenderKey): readonly RenderKey[];
    getOrderSnapshot?(parentRenderKey: RenderKey | null): readonly RenderKey[];
    subscribeNode?(renderKey: RenderKey, listener: () => void): () => void;
    subscribeOrder?(parentRenderKey: RenderKey | null, listener: () => void): () => void;
}

export interface UIAction {
    readonly type: string;
    readonly renderKey: RenderKey | null;
    readonly [field: string]: unknown;
}

export interface WidgetRendererProps {
    node: ViewNodeSnapshot;
    nodeKey: RenderKey;
    store: ViewTreeStore;
    assets: Readonly<{ sprite?: (node: ViewNodeSnapshot) => string | null }>;
    dispatch: ((action: UIAction) => void) | null;
    children: ReactNode;
}

export interface WidgetRendererRegistry {
    ids?: Readonly<Record<string, ComponentType<WidgetRendererProps>>> | ReadonlyMap<string, ComponentType<WidgetRendererProps>>;
    names?: Readonly<Record<string, ComponentType<WidgetRendererProps>>> | ReadonlyMap<string, ComponentType<WidgetRendererProps>>;
    types?: Readonly<Record<string, ComponentType<WidgetRendererProps>>> | ReadonlyMap<string | number, ComponentType<WidgetRendererProps>>;
    roles?: Readonly<Record<string, ComponentType<WidgetRendererProps>>> | ReadonlyMap<string, ComponentType<WidgetRendererProps>>;
    fallback?: ComponentType<WidgetRendererProps>;
}

export interface WidgetProps {
    store: ViewTreeStore;
    nodeKey: RenderKey;
    renderers?: WidgetRendererRegistry;
    assets?: Readonly<{ sprite?: (node: ViewNodeSnapshot) => string | null }>;
    dispatch?: ((action: UIAction) => void) | null;
}

export interface InterfacePreviewProps {
    store: ViewTreeStore;
    renderers?: WidgetRendererRegistry;
    assets?: Readonly<{ sprite?: (node: ViewNodeSnapshot) => string | null }>;
    dispatch?: ((action: UIAction) => void) | null;
    viewport?: Readonly<{ width: number; height: number }> | null;
    className?: string;
    style?: CSSProperties | null;
    ariaLabel?: string;
}

export function assertViewTreeStore(store: unknown): ViewTreeStore;
export function useUIRoots(store: ViewTreeStore): readonly RenderKey[];
export function useUINode(store: ViewTreeStore, renderKey: RenderKey): ViewNodeSnapshot | null;
export function useUIChildren(store: ViewTreeStore, parentKey: RenderKey, fallback?: readonly RenderKey[]): readonly RenderKey[];
export function widgetRole(node: ViewNodeSnapshot): string;
export function resolveWidgetRenderer(node: ViewNodeSnapshot, renderers?: WidgetRendererRegistry): ComponentType<WidgetRendererProps>;
export function widgetSurfaceProps(node: ViewNodeSnapshot, role?: string, dispatch?: ((action: UIAction) => void) | null): Record<string, unknown>;
export function interfaceInteractionProps(dispatch?: ((action: UIAction) => void) | null): Record<string, unknown>;
export const InterfacePreview: ComponentType<InterfacePreviewProps>;
export const Widget: ComponentType<WidgetProps>;
export const LayerWidget: ComponentType<WidgetRendererProps>;
export const RectangleWidget: ComponentType<WidgetRendererProps>;
export const TextWidget: ComponentType<WidgetRendererProps>;
export const GraphicWidget: ComponentType<WidgetRendererProps>;
export const ModelWidget: ComponentType<WidgetRendererProps>;
export const LineWidget: ComponentType<WidgetRendererProps>;
export const InventoryWidget: ComponentType<WidgetRendererProps>;
export const UnknownWidget: ComponentType<WidgetRendererProps>;
export const DEFAULT_WIDGET_RENDERERS: Readonly<Record<string, ComponentType<WidgetRendererProps>>>;
