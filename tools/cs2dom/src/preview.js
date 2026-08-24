/*
 * Where each component lands, and what it shows.
 *
 * The geometry here is a port of the client's own IF3 layout — src/ui/ui_if3_layout.h,
 * `UITree_If3AxisFromPositionMode` and `UITree_If3DimFromParentMode`, formula for
 * formula, including the 14-bit fixed-point proportional modes and the truncating
 * division in the centring mode. A preview that laid components out its own way
 * would be a drawing of a design rather than a picture of the interface, and the
 * off-by-ones it hid would be exactly the ones worth seeing early.
 *
 * What it does not port is drawing: the browser's text is not the cache's bitmap
 * font. Drawing models and sprites happens in the dev page; this module only
 * supplies their client-accurate boxes and resolved fields.
 */

import { resolveProps } from './eval.js';
import { IF_TYPE } from './components.js';

/* C and Java use an arithmetic right shift here. Math.trunc(product / 16384)
 * disagrees for every negative product with discarded bits: -1 >> 14 is -1,
 * not zero. BigInt also keeps the multiply exact beyond Number's 53-bit range. */
const mulShift14 = (a, b) => Number((BigInt(Math.trunc(a)) * BigInt(Math.trunc(b))) >> 14n);

export function dimFromParentMode(mode, orig, parentDim) {
    switch( mode ) {
        case 0: return orig;
        case 1: return parentDim - orig;
        case 2: return mulShift14(parentDim, orig);
        default: return orig;
    }
}

export function axisFromPositionMode(mode, base, parentOrigin, parentDim, selfDim) {
    switch( mode ) {
        case 0: return parentOrigin + base;
        /* Truncating division, like the client's — not an arithmetic shift. */
        case 1: return parentOrigin + Math.trunc((parentDim - selfDim) / 2) + base;
        case 2: return parentOrigin + parentDim - base - selfDim;
        case 3: return parentOrigin + mulShift14(parentDim, base);
        case 4: return parentOrigin + Math.trunc((parentDim - selfDim) / 2) + mulShift14(parentDim, base);
        case 5: return parentOrigin + parentDim - mulShift14(parentDim, base) - selfDim;
        default: return parentOrigin + base;
    }
}

/**
 * Resolve the whole tree against a state environment.
 *
 * Returns one flat box per component with absolute geometry, in draw order, which
 * is what the page needs and what a snapshot diff against the real client's UITree
 * would compare.
 */
export function layout(ir, state, viewport = { width: 512, height: 334 }, unmodelled = null) {
    const rootBox = {
        x: 0,
        y: 0,
        w: Math.trunc(Number(viewport?.width) || 0),
        h: Math.trunc(Number(viewport?.height) || 0),
    };
    const rootClip = rectangle(rootBox.x, rootBox.y, rootBox.w, rootBox.h);
    const nodes = (ir.components || []).map((component) => ({
        component,
        props: resolveProps(component, state, unmodelled),
        parent: null,
        children: [],
        geometry: null,
    }));
    const byFileId = new Map(nodes.map((node) => [node.component.fileId, node]));

    /* The native builder allocates the whole archive before linking it. Resolving
     * parents from a completed map is what makes a child whose parent appears
     * later in a .compack behave exactly like an ordinary child. */
    for( const node of nodes ) {
        const layer = node.component.layer;
        const parent = layer === null || layer === undefined ? null : byFileId.get(layer);
        if( parent && parent !== node ) node.parent = parent;
    }
    breakParentCycles(nodes);
    for( const node of nodes )
        if( node.parent ) node.parent.children.push(node);

    const roots = nodes.filter((node) => !node.parent);
    for( const root of roots ) resolveGeometry(root, rootBox);

    /* A malformed cycle is detached above, but retain the native builder's
     * orphan tolerance: every decoded component still gets a resolved box. */
    for( const node of nodes )
        if( !node.geometry ) resolveGeometry(node, rootBox);

    const boxes = [];
    const context = {
        clip: rootClip,
        surface: rootClip,
        scrollX: 0,
        scrollY: 0,
        hidden: false,
        culled: false,
        depth: 0,
    };
    for( const root of roots ) traverse(root, context, boxes);
    for( const node of nodes )
        if( !node.traversed ) traverse(node, context, boxes);
    return boxes;
}

function resolveGeometry(node, viewport) {
    if( node.geometry ) return node.geometry;
    const parent = node.parent;
    const parentGeometry = parent ? resolveGeometry(parent, viewport) : viewport;
    const parentW = parent && componentIsLayer(parent.component) && int(parent.props.scrollWidth) > 0
        ? int(parent.props.scrollWidth) : parentGeometry.w;
    const parentH = parent && componentIsLayer(parent.component) && int(parent.props.scrollHeight) > 0
        ? int(parent.props.scrollHeight) : parentGeometry.h;
    const props = node.props;
    const widthMode = int(props.widthMode);
    const heightMode = int(props.heightMode);
    let w = dimFromParentMode(widthMode, int(props.width), parentW);
    let h = dimFromParentMode(heightMode, int(props.height), parentH);

    /* UITree_If3ComputeSize clamps only its aspect-ratio path. Ordinary minus
     * dimensions remain signed layout inputs; a non-positive clipping layer is
     * pruned later by the emit walk. */
    if( widthMode === 4 || heightMode === 4 ) {
        const aspectW = Math.max(1, int(props.aspectW) || 1);
        const aspectH = Math.max(1, int(props.aspectH) || 1);
        if( widthMode === 4 ) w = Math.trunc(aspectW * h / aspectH);
        if( heightMode === 4 ) h = Math.trunc(aspectH * w / aspectW);
        w = Math.max(0, w);
        h = Math.max(0, h);
    }
    if( !parent && w === 0 && h === 0 ) {
        w = parentW;
        h = parentH;
    }

    const x = axisFromPositionMode(int(props.xMode), int(props.x), parentGeometry.x, parentW, w);
    const y = axisFromPositionMode(int(props.yMode), int(props.y), parentGeometry.y, parentH, h);
    node.geometry = { x, y, w, h };
    return node.geometry;
}

function traverse(node, context, boxes) {
    if( node.traversed ) return;
    node.traversed = true;
    const { component, props, geometry } = node;
    const x = geometry.x - context.scrollX;
    const y = geometry.y - context.scrollY;
    const clipsChildren = componentClipsChildren(component);
    const scrollLayer = componentIsLayer(component);
    const ownCull = clipsChildren && (geometry.w <= 0 || geometry.h <= 0);
    const effectiveHidden = context.hidden || Boolean(props.hidden);
    const culled = context.culled || ownCull;
    const scrollWidth = int(props.scrollWidth);
    const scrollHeight = int(props.scrollHeight);
    const maxScrollX = scrollLayer
        ? Math.max(0, scrollWidth - geometry.w) : 0;
    const maxScrollY = scrollLayer
        ? Math.max(0, scrollHeight - geometry.h) : 0;
    const ownScrollX = clamp(int(props.scrollX), 0, maxScrollX);
    const ownScrollY = clamp(int(props.scrollY), 0, maxScrollY);

    const box = {
        name: component.name,
        kind: component.kind,
        type: component.type,
        fileId: component.fileId,
        layer: component.layer,
        /* x/y are screen coordinates, as emitted by UITree. absX/absY preserve
         * the logical layout box for inspectors and scroll diagnostics. */
        x, y, w: geometry.w, h: geometry.h,
        absX: geometry.x, absY: geometry.y,
        relX: node.parent ? geometry.x - node.parent.geometry.x : geometry.x,
        relY: node.parent ? geometry.y - node.parent.geometry.y : geometry.y,
        depth: context.depth,
        effectiveHidden,
        culled,
        emitted: !effectiveHidden && !culled,
        clip: { ...context.clip },
        surface: { ...context.surface },
        scrollX: ownScrollX,
        scrollY: ownScrollY,
        props,
        dynamic: (component.dynamic || []).map((d) => d.prop),
        ops: component.ops || [],
        events: Object.keys(component.events || {}),
        hooks: Object.keys(component.hooks || {}),
    };
    boxes.push(box);

    let childClip = context.clip;
    let childSurface = context.surface;
    if( clipsChildren && !ownCull ) {
        /* Pix2D.setClipping replaces an ordinary ancestor layer clip. A layer's
         * child clip is its screen box intersected with the enclosing surface,
         * not with context.clip. A genuinely scrollable layer then becomes the
         * new surface so its content cannot escape the viewport. */
        childClip = intersect(context.surface, rectangle(x, y, geometry.w, geometry.h));
        if( scrollLayer && (maxScrollX > 0 || maxScrollY > 0) )
            childSurface = childClip;
    }

    const childContext = {
        clip: childClip,
        surface: childSurface,
        scrollX: context.scrollX + (maxScrollX > 0 ? ownScrollX : 0),
        scrollY: context.scrollY + (maxScrollY > 0 ? ownScrollY : 0),
        hidden: effectiveHidden,
        culled,
        depth: context.depth + 1,
    };
    for( const child of node.children ) traverse(child, childContext, boxes);
}

/* `cc_create`'s widget type is not the UITree element type. In particular,
 * widget types 0 and 2 (and every unknown value) create UIELEM_CC_OBJ, not an
 * RS_LAYER/RS_INV. HostRuntime deliberately keeps the original widget type on
 * the React component, so use the dynamic Object identity before interpreting
 * those numeric values as cache component types. A CC object neither clips nor
 * culls its children; they inherit the enclosing layer's clip. */
function componentClipsChildren(component) {
    if( isDynamicObject(component) ) return false;
    return componentIsLayer(component) || component.type === IF_TYPE.inv;
}

function componentIsLayer(component) {
    return component.type === IF_TYPE.layer && !isDynamicObject(component);
}

function isDynamicObject(component) {
    return component.runtimeDynamic && component.kind === 'Object';
}

function breakParentCycles(nodes) {
    for( const node of nodes ) {
        const seen = new Set([node]);
        for( let cursor = node.parent; cursor; cursor = cursor.parent ) {
            if( !seen.has(cursor) ) {
                seen.add(cursor);
                continue;
            }
            node.parent = null;
            break;
        }
    }
}

function int(value) {
    return Number(value) | 0;
}

function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
}

function rectangle(x, y, w, h) {
    return {
        left: x,
        top: y,
        right: x + Math.max(0, w),
        bottom: y + Math.max(0, h),
    };
}

function intersect(left, right) {
    const x = Math.max(left.left, right.left);
    const y = Math.max(left.top, right.top);
    return {
        left: x,
        top: y,
        right: Math.max(x, Math.min(left.right, right.right)),
        bottom: Math.max(y, Math.min(left.bottom, right.bottom)),
    };
}

/** What the page draws a box as; the type decides, the way the client's emit does. */
export function boxRole(type) {
    switch( type ) {
        case IF_TYPE.layer: return 'layer';
        case IF_TYPE.rectangle: return 'rect';
        case IF_TYPE.text: return 'text';
        case IF_TYPE.graphic: return 'graphic';
        case IF_TYPE.model: return 'model';
        case IF_TYPE.tooltip: return 'text';
        case IF_TYPE.line: return 'line';
        default: return 'unknown';
    }
}
