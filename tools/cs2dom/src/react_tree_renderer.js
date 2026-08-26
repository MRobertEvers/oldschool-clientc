/*
 * React presentation for a committed UI-tree mirror.
 *
 * The renderer deliberately knows nothing about CS2, HOST requests, worker
 * transactions, or the mutable WorkingTree. Its input is the tiny
 * ViewTreeStore contract below. A store must keep the roots array and each node
 * snapshot referentially stable until that value changes. useSyncExternalStore
 * can then discard a store-wide notification for every unchanged widget.
 *
 * ViewTreeStore:
 *   getRoots(): readonly RenderKey[]
 *   getNode(renderKey): immutable NodeSnapshot | null
 *   subscribe(listener): () => void
 *
 * A NodeSnapshot contains a stable `renderKey`, `type`, and immutable `props`.
 * Child order may be a separate store snapshot or an immutable `children`
 * fallback. Geometry is parent-relative and may either be supplied as
 * `layout: { x, y, width, height }` or as x/y/w/h fields. The optional renderer
 * registry is the extension point for bitmap fonts, cache sprites, inventory
 * grids, and toridraw model surfaces.
 */

import {
    createElement,
    memo,
    useCallback,
    useSyncExternalStore,
} from 'react';

const EMPTY_KEYS = Object.freeze([]);
const EMPTY_PROPS = Object.freeze({});
const EMPTY_RENDERERS = Object.freeze({});
const EMPTY_ASSETS = Object.freeze({});

const TYPE_ROLES = Object.freeze({
    0: 'layer',
    2: 'inventory',
    3: 'rect',
    4: 'text',
    5: 'graphic',
    6: 'model',
    8: 'text',
    9: 'line',
});

const KIND_ROLES = Object.freeze({
    layer: 'layer',
    container: 'layer',
    inv: 'inventory',
    inventory: 'inventory',
    rect: 'rect',
    rectangle: 'rect',
    text: 'text',
    tooltip: 'text',
    graphic: 'graphic',
    sprite: 'graphic',
    model: 'model',
    line: 'line',
});

/** Fail at the integration boundary rather than from inside a React commit. */
export function assertViewTreeStore(store) {
    if( !store || typeof store.getRoots !== 'function' ||
        typeof store.getNode !== 'function' || typeof store.subscribe !== 'function' ) {
        throw new TypeError('ViewTreeStore must implement getRoots(), getNode(), and subscribe()');
    }
    return store;
}

/** Subscribe only to the immutable root-key vector. */
export function useUIRoots(store) {
    assertViewTreeStore(store);
    const subscribe = useCallback((listener) => typeof store.subscribeOrder === 'function'
        ? store.subscribeOrder(null, listener) : store.subscribe(listener), [store]);
    const snapshot = useCallback(() => store.getRoots(), [store]);
    return useSyncExternalStore(subscribe, snapshot, snapshot);
}

/**
 * Subscribe to one immutable node snapshot. Store notifications are allowed to
 * be coarse: useSyncExternalStore compares this selected object with Object.is.
 */
export function useUINode(store, renderKey) {
    assertViewTreeStore(store);
    const subscribe = useCallback((listener) => typeof store.subscribeNode === 'function'
        ? store.subscribeNode(renderKey, listener) : store.subscribe(listener), [store, renderKey]);
    const snapshot = useCallback(() => store.getNode(renderKey) ?? null, [store, renderKey]);
    return useSyncExternalStore(subscribe, snapshot, snapshot);
}

/**
 * Child order may be stored separately from node fields. This prevents a pure
 * reorder from changing the parent's node snapshot and is the preferred store
 * shape; `node.children` remains the minimal-contract fallback.
 */
export function useUIChildren(store, parentKey, fallback = EMPTY_KEYS) {
    assertViewTreeStore(store);
    const subscribe = useCallback((listener) => typeof store.subscribeOrder === 'function'
        ? store.subscribeOrder(parentKey, listener) : store.subscribe(listener), [store, parentKey]);
    const snapshot = useCallback(() => {
        const value = typeof store.getChildren === 'function'
            ? store.getChildren(parentKey)
            : typeof store.getOrderSnapshot === 'function'
                ? store.getOrderSnapshot(parentKey) : fallback;
        return value || EMPTY_KEYS;
    }, [store, parentKey, fallback]);
    return useSyncExternalStore(subscribe, snapshot, snapshot);
}

/** Resolve the IF widget role without coupling the view to a concrete store. */
export function widgetRole(node) {
    const numeric = Number(node?.type);
    if( Number.isInteger(numeric) && TYPE_ROLES[numeric] ) return TYPE_ROLES[numeric];
    const kind = String(node?.kind ?? node?.type ?? '').toLowerCase();
    return KIND_ROLES[kind] || 'unknown';
}

/**
 * Select an authored renderer. Precedence is explicit renderer id, component
 * name, numeric/string widget type, semantic role, then the built-in renderer.
 */
export function resolveWidgetRenderer(node, renderers = EMPTY_RENDERERS) {
    const role = widgetRole(node);
    const rendererId = node?.rendererId ?? node?.props?.rendererId;
    return lookupRenderer(renderers.ids, rendererId) ||
        lookupRenderer(renderers.names, node?.name) ||
        lookupRenderer(renderers.types, node?.type) ||
        lookupRenderer(renderers.roles, role) ||
        (role === 'unknown' ? null : DEFAULT_WIDGET_RENDERERS[role]) ||
        renderers.fallback || UnknownWidget;
}

function lookupRenderer(table, key) {
    if( key === undefined || key === null || !table ) return null;
    if( table instanceof Map ) return table.get(key) || table.get(String(key)) || null;
    return table[key] || table[String(key)] || null;
}

/**
 * Render all committed roots. Developer chrome should live in a separate React
 * root so replacing this preview cannot reset picker or editor state.
 */
export function InterfacePreview({
    store,
    renderers = EMPTY_RENDERERS,
    assets = EMPTY_ASSETS,
    dispatch = null,
    viewport = null,
    className = '',
    style = null,
    ariaLabel = 'Interface preview',
}) {
    const roots = useUIRoots(store);
    const rootStyle = {
        position: 'relative',
        overflow: 'hidden',
        ...(viewport ? {
            width: pixel(viewport.width),
            height: pixel(viewport.height),
        } : null),
        ...style,
    };
    const children = roots.map((renderKey) => createElement(Widget, {
        /* The stable render key is deliberately both the React key and lookup. */
        key: renderKey,
        nodeKey: renderKey,
        store,
        renderers,
        assets,
        dispatch,
    }));
    return createElement('div', {
        className: joinClasses('cs2dom-interface-preview', className),
        style: rootStyle,
        role: 'application',
        'aria-label': ariaLabel,
        tabIndex: typeof dispatch === 'function' ? 0 : undefined,
        ...interfaceInteractionProps(dispatch),
    }, children);
}

function WidgetImpl({
    store,
    nodeKey,
    renderers = EMPTY_RENDERERS,
    assets = EMPTY_ASSETS,
    dispatch = null,
}) {
    const node = useUINode(store, nodeKey);
    const childKeys = useUIChildren(store, nodeKey,
        Array.isArray(node?.children) ? node.children : EMPTY_KEYS);
    if( !node ) return null;
    const children = childKeys.map((renderKey) => createElement(Widget, {
        key: renderKey,
        nodeKey: renderKey,
        store,
        renderers,
        assets,
        dispatch,
    }));
    const Renderer = resolveWidgetRenderer(node, renderers);
    return createElement(Renderer, {
        node,
        nodeKey,
        store,
        assets,
        dispatch,
        children,
    });
}

/** One independently subscribed and memoized committed widget. */
export const Widget = memo(WidgetImpl);
Widget.displayName = 'CS2Widget';

/** Shared DOM props for built-in and authored widget renderers. */
export function widgetSurfaceProps(node, role = widgetRole(node), dispatch = null) {
    const props = node?.props || EMPTY_PROPS;
    const layout = node?.layout || node?.geometry || EMPTY_PROPS;
    const x = numberOr(layout.x, node?.x, 0);
    const y = numberOr(layout.y, node?.y, 0);
    const width = numberOr(layout.width, node?.width, node?.w, props.width, 0);
    const height = numberOr(layout.height, node?.height, node?.h, props.height, 0);
    const hidden = Boolean(node?.hidden ?? node?.effectiveHidden ?? props.hidden);
    const transparency = clamp(numberOr(props.transparency, node?.transparency, 0), 0, 255);
    const presentationStyle = node?.presentation?.style || EMPTY_PROPS;
    const interactive = Boolean(node?.interactive || node?.ops?.length ||
        node?.events?.length || node?.hooks?.length);
    return {
        className: joinClasses(
            'cs2dom-widget',
            `cs2dom-widget-${role}`,
            node?.className,
            node?.presentation?.className,
        ),
        style: {
            position: 'absolute',
            boxSizing: 'border-box',
            left: pixel(x),
            top: pixel(y),
            width: pixel(width),
            height: pixel(height),
            display: hidden ? 'none' : undefined,
            opacity: transparency ? (255 - transparency) / 255 : undefined,
            overflow: node?.clipChildren ? 'hidden' : undefined,
            ...presentationStyle,
        },
        'data-render-key': String(node?.renderKey ?? node?.key ?? ''),
        'data-widget-role': role,
        'data-widget-type': String(node?.type ?? ''),
        'aria-label': node?.ariaLabel || (node?.name
            ? `${node.name}, ${role}` : undefined),
        tabIndex: props.tabIndex ?? node?.tabIndex ??
            (typeof dispatch === 'function' && interactive ? 0 : undefined),
    };
}

export function LayerWidget({ node, dispatch, children }) {
    const elementProps = widgetSurfaceProps(node, 'layer', dispatch);
    if( node?.props?.scrollWidth > 0 || node?.props?.scrollHeight > 0 )
        elementProps.style.overflow = 'hidden';
    return createElement('div', elementProps, children);
}

export function RectangleWidget({ node, dispatch, children }) {
    const props = node.props || EMPTY_PROPS;
    const elementProps = widgetSurfaceProps(node, 'rect', dispatch);
    if( props.fill ) elementProps.style.backgroundColor = cssColour(props.color);
    else elementProps.style.border = `1px solid ${cssColour(props.color)}`;
    return createElement('div', elementProps, children);
}

export function TextWidget({ node, dispatch, children }) {
    const props = node.props || EMPTY_PROPS;
    const elementProps = widgetSurfaceProps(node, 'text', dispatch);
    elementProps.style.display = elementProps.style.display || 'flex';
    elementProps.style.color = cssColour(props.color);
    elementProps.style.justifyContent = horizontalAlign(props.halign);
    elementProps.style.alignItems = verticalAlign(props.valign);
    elementProps.style.whiteSpace = 'pre-wrap';
    elementProps.style.lineHeight = props.lineHeight > 0 ? pixel(props.lineHeight) : undefined;
    elementProps.style.textShadow = props.shadow ? '1px 1px 0 #000' : undefined;
    return createElement('div', elementProps,
        createElement('span', { className: 'cs2dom-text-content' }, String(props.text ?? '')),
        children);
}

export function GraphicWidget({ node, assets = EMPTY_ASSETS, dispatch, children }) {
    const props = node.props || EMPTY_PROPS;
    const elementProps = widgetSurfaceProps(node, 'graphic', dispatch);
    const url = resolveAsset(assets.sprite, node, props.spriteUrl ?? node?.presentation?.spriteUrl);
    if( url ) {
        elementProps.style.backgroundImage = `url(${JSON.stringify(String(url))})`;
        elementProps.style.backgroundRepeat = props.tiled ? 'repeat' : 'no-repeat';
        elementProps.style.backgroundPosition = 'center';
        elementProps.style.imageRendering = 'pixelated';
    }
    if( props.hFlip || props.vFlip ) {
        const x = props.hFlip ? -1 : 1;
        const y = props.vFlip ? -1 : 1;
        elementProps.style.transform = `scale(${x}, ${y})`;
    }
    return createElement('div', elementProps, children);
}

export function ModelWidget({ node, dispatch, children }) {
    const elementProps = widgetSurfaceProps(node, 'model', dispatch);
    const props = node.props || EMPTY_PROPS;
    const layout = node.layout || node.geometry || EMPTY_PROPS;
    const width = Math.max(0, numberOr(layout.width, node.width, node.w, props.width, 0));
    const height = Math.max(0, numberOr(layout.height, node.height, node.h, props.height, 0));
    return createElement('div', elementProps,
        createElement('canvas', {
            className: 'cs2dom-model-surface',
            width,
            height,
            'data-model': String(props.model ?? -1),
            'aria-hidden': true,
        }),
        children);
}

export function LineWidget({ node, dispatch }) {
    const props = node.props || EMPTY_PROPS;
    const elementProps = widgetSurfaceProps(node, 'line', dispatch);
    const layout = node.layout || node.geometry || EMPTY_PROPS;
    const width = Math.max(0, numberOr(layout.width, node.width, node.w, props.width, 0));
    const height = Math.max(0, numberOr(layout.height, node.height, node.h, props.height, 0));
    const stroke = Math.max(1, Number(props.lineWidth) || 1);
    elementProps.style.overflow = 'visible';
    elementProps.viewBox = `0 0 ${Math.max(1, width)} ${Math.max(1, height)}`;
    elementProps.preserveAspectRatio = 'none';
    return createElement('svg', elementProps,
        createElement('line', {
            x1: 0,
            y1: props.lineDirection ? height : 0,
            x2: width,
            y2: props.lineDirection ? 0 : height,
            stroke: cssColour(props.color),
            strokeWidth: stroke,
            strokeLinecap: 'square',
            shapeRendering: 'crispEdges',
        }));
}

export function InventoryWidget({ node, dispatch, children }) {
    return createElement('div', widgetSurfaceProps(node, 'inventory', dispatch), children);
}

export function UnknownWidget({ node, dispatch, children }) {
    return createElement('div', widgetSurfaceProps(node, 'unknown', dispatch), children);
}

export const DEFAULT_WIDGET_RENDERERS = Object.freeze({
    layer: LayerWidget,
    inventory: InventoryWidget,
    rect: RectangleWidget,
    text: TextWidget,
    graphic: GraphicWidget,
    model: ModelWidget,
    line: LineWidget,
    unknown: UnknownWidget,
});

/**
 * One delegated handler set for the entire interface. Materializing handlers
 * per widget makes a 4,000-cell interface allocate tens of thousands of
 * closures and lets one bubbled event dispatch once per ancestor.
 */
export function interfaceInteractionProps(dispatch) {
    if( typeof dispatch !== 'function' ) return EMPTY_PROPS;
    const send = (type, project, preventDefault = false) => (event) => {
        if( preventDefault ) event.preventDefault();
        event.stopPropagation?.();
        dispatch({ type, renderKey: eventRenderKey(event), ...project(event) });
    };
    const boundary = (type) => (event) => {
        const renderKey = eventRenderKey(event);
        const relatedKey = elementRenderKey(event.relatedTarget, event.currentTarget);
        if( renderKey === relatedKey ) return;
        event.stopPropagation?.();
        dispatch({ type, renderKey, relatedKey, ...pointerAction(event) });
    };
    return {
        onPointerDown: send('pointer-down', pointerAction),
        onPointerUp: send('pointer-up', pointerAction),
        onPointerMove: send('pointer-move', pointerAction),
        onPointerOver: boundary('pointer-enter'),
        onPointerOut: boundary('pointer-leave'),
        onPointerCancel: send('pointer-cancel', pointerAction),
        onClick: send('click', pointerAction),
        onDoubleClick: send('double-click', pointerAction),
        onAuxClick: send('aux-click', pointerAction),
        onContextMenu: send('context-menu', pointerAction, true),
        onWheel: send('wheel', wheelAction, true),
        onKeyDown: send('key-down', keyAction),
        onKeyUp: send('key-up', keyAction),
        onFocus: send('focus', modifierAction),
        onBlur: send('blur', modifierAction),
    };
}

function eventRenderKey(event) {
    return elementRenderKey(event.target, event.currentTarget);
}

function elementRenderKey(start, boundary) {
    for( let element = start; element && element !== boundary;
        element = element.parentElement || element.parentNode ) {
        const key = element.dataset?.renderKey ?? element.getAttribute?.('data-render-key');
        if( key !== undefined && key !== null && key !== '' ) return String(key);
    }
    return null;
}

function pointerAction(event) {
    return {
        pointerId: numberOr(event.pointerId, 0),
        pointerType: event.pointerType || 'mouse',
        button: numberOr(event.button, 0),
        buttons: numberOr(event.buttons, 0),
        clientX: numberOr(event.clientX, 0),
        clientY: numberOr(event.clientY, 0),
        offsetX: numberOr(event.nativeEvent?.offsetX, 0),
        offsetY: numberOr(event.nativeEvent?.offsetY, 0),
        pressure: numberOr(event.pressure, 0),
        ...modifierAction(event),
    };
}

function wheelAction(event) {
    return {
        deltaX: numberOr(event.deltaX, 0),
        deltaY: numberOr(event.deltaY, 0),
        deltaZ: numberOr(event.deltaZ, 0),
        deltaMode: numberOr(event.deltaMode, 0),
        clientX: numberOr(event.clientX, 0),
        clientY: numberOr(event.clientY, 0),
        ...modifierAction(event),
    };
}

function keyAction(event) {
    return {
        key: String(event.key ?? ''),
        code: String(event.code ?? ''),
        repeat: Boolean(event.repeat),
        location: numberOr(event.location, 0),
        ...modifierAction(event),
    };
}

function modifierAction(event) {
    return {
        altKey: Boolean(event.altKey),
        ctrlKey: Boolean(event.ctrlKey),
        metaKey: Boolean(event.metaKey),
        shiftKey: Boolean(event.shiftKey),
    };
}

function resolveAsset(resolver, node, fallback) {
    if( typeof resolver === 'function' ) return resolver(node);
    return fallback || null;
}

function horizontalAlign(value) {
    return ['flex-start', 'center', 'flex-end'][Number(value) | 0] || 'flex-start';
}

function verticalAlign(value) {
    return ['flex-start', 'center', 'flex-end'][Number(value) | 0] || 'flex-start';
}

function cssColour(value) {
    const rgb = (Number(value) | 0) & 0xffffff;
    return `#${rgb.toString(16).padStart(6, '0')}`;
}

function numberOr(...values) {
    for( const value of values ) {
        const number = Number(value);
        if( Number.isFinite(number) ) return number;
    }
    return 0;
}

function pixel(value) {
    return `${numberOr(value, 0)}px`;
}

function clamp(value, lower, upper) {
    return Math.max(lower, Math.min(upper, value));
}

function joinClasses(...values) {
    return values.filter(Boolean).join(' ');
}
