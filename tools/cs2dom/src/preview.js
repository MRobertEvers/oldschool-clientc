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
import { childrenOf, isExpr } from './expr.js';

/* C and Java use an arithmetic right shift here. Math.trunc(product / 16384)
 * disagrees for every negative product with discarded bits: -1 >> 14 is -1,
 * not zero. Ordinary interface dimensions have an exactly representable Number
 * product; reserve BigInt for the extreme 32-bit values that actually need it. */
function mulShift14(a, b) {
    const left = Math.trunc(a);
    const right = Math.trunc(b);
    const product = left * right;
    if( Number.isSafeInteger(product) ) return Math.floor(product / 16384);
    return Number((BigInt(left) * BigInt(right)) >> 14n);
}

/* Layout is called after every completed HOST transaction. Component properties
 * change often, while the parent graph usually does not. Keep only that structural
 * graph: every stateful/resolved value is refreshed below, and topology mutations
 * are detected without requiring callers to publish a separate revision counter. */
const layoutPlans = new WeakMap();
const EMPTY_COMPONENTS = [];

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
export function layout(
    ir, state, viewport = { width: 512, height: 334 }, unmodelled = null,
    structureRevision = null,
) {
    const rootBox = viewportBox(viewport);
    const rootClip = rectangle(rootBox.x, rootBox.y, rootBox.w, rootBox.h);
    const components = ir.components || EMPTY_COMPONENTS;
    const plan = layoutPlan(ir, components, structureRevision);
    const { nodes, roots } = plan;
    const evaluationMemo = plan.sharedExpressions ? new WeakMap() : null;
    for( const node of nodes ) {
        node.props = resolveProps(node.component, state, unmodelled, evaluationMemo);
        node.geometryResolved = false;
        node.traversed = false;
    }

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
    for( const root of roots ) traverse(root, context, boxes, rootBox);
    /* A malformed graph is detached while the plan is built, but retain the
     * native builder's orphan tolerance: every decoded component gets a box. */
    for( const node of nodes )
        if( !node.traversed ) traverse(node, context, boxes, rootBox);
    return boxes;
}

/**
 * Resolve one component's emitted screen box without materialising its siblings.
 *
 * Unlike `layoutGeometry`, this includes every ancestor's clamped scroll offset,
 * replacement clip/surface rules, hidden state and structural culling. The result
 * has the same shape and values as the corresponding entry from `layout()`.
 */
export function layoutBox(
    ir, state, viewport = { width: 512, height: 334 }, target, unmodelled = null,
    structureRevision = null,
) {
    const components = ir.components || EMPTY_COMPONENTS;
    const plan = layoutPlan(ir, components, structureRevision);
    const node = target && typeof target === 'object'
        ? plan.byComponent.get(target) : plan.byFileId.get(target);
    if( !node ) return null;

    const chain = [];
    for( let cursor = node; cursor; cursor = cursor.parent ) chain.push(cursor);
    const evaluationMemo = plan.sharedExpressions ? new WeakMap() : null;
    for( let index = chain.length - 1; index >= 0; index-- ) {
        const cursor = chain[index];
        cursor.props = resolveProps(cursor.component, state, unmodelled, evaluationMemo);
        cursor.geometryResolved = false;
    }

    const rootBox = viewportBox(viewport);
    const rootClip = rectangle(rootBox.x, rootBox.y, rootBox.w, rootBox.h);
    let context = {
        clip: rootClip,
        surface: rootClip,
        scrollX: 0,
        scrollY: 0,
        hidden: false,
        culled: false,
        depth: 0,
    };
    for( let index = chain.length - 1; index >= 0; index-- ) {
        const cursor = chain[index];
        const box = resolveNodeBox(cursor, context, rootBox);
        if( cursor === node ) return box;
        context = childContext(cursor, context, box);
    }
    return null;
}

/**
 * Resolve one component's logical geometry without materialising the paint list.
 *
 * Synchronous CS2 geometry getters often run while a script is still rebuilding a
 * large dynamic interface. Their result depends only on the target and its parent
 * chain, including dynamic props and scroll extents; clips, siblings and children
 * cannot affect it. `target` may be the component object or its file id.
 */
export function layoutGeometry(
    ir, state, viewport = { width: 512, height: 334 }, target, unmodelled = null,
    structureRevision = null,
) {
    const components = ir.components || EMPTY_COMPONENTS;
    const plan = layoutPlan(ir, components, structureRevision);
    const node = target && typeof target === 'object'
        ? plan.byComponent.get(target) : plan.byFileId.get(target);
    if( !node ) return null;

    const chain = [];
    for( let cursor = node; cursor; cursor = cursor.parent ) chain.push(cursor);
    const evaluationMemo = plan.sharedExpressions ? new WeakMap() : null;
    for( let index = chain.length - 1; index >= 0; index-- ) {
        const cursor = chain[index];
        cursor.props = resolveProps(cursor.component, state, unmodelled, evaluationMemo);
        cursor.geometryResolved = false;
    }

    const geometry = resolveGeometry(node, viewportBox(viewport));
    const parentGeometry = node.parent?.geometry;
    const scrollLayer = componentIsLayer(node.component);
    const maxScrollX = scrollLayer
        ? Math.max(0, int(node.props.scrollWidth) - geometry.w) : 0;
    const maxScrollY = scrollLayer
        ? Math.max(0, int(node.props.scrollHeight) - geometry.h) : 0;
    return {
        x: geometry.x,
        y: geometry.y,
        w: geometry.w,
        h: geometry.h,
        relX: parentGeometry ? geometry.x - parentGeometry.x : geometry.x,
        relY: parentGeometry ? geometry.y - parentGeometry.y : geometry.y,
        scrollX: clamp(int(node.props.scrollX), 0, maxScrollX),
        scrollY: clamp(int(node.props.scrollY), 0, maxScrollY),
    };
}

/** Resolve native effective-hidden state without materialising the paint list. */
export function layoutVisibility(
    ir, state, target, unmodelled = null, structureRevision = null,
) {
    const components = ir.components || EMPTY_COMPONENTS;
    const plan = layoutPlan(ir, components, structureRevision);
    const node = target && typeof target === 'object'
        ? plan.byComponent.get(target) : plan.byFileId.get(target);
    if( !node ) return false;
    const evaluationMemo = plan.sharedExpressions ? new WeakMap() : null;
    for( let cursor = node; cursor; cursor = cursor.parent ) {
        const props = resolveProps(cursor.component, state, unmodelled, evaluationMemo);
        if( Boolean(props.hidden) ) return false;
    }
    return true;
}

function viewportBox(viewport) {
    return {
        x: 0,
        y: 0,
        w: Math.trunc(Number(viewport?.width) || 0),
        h: Math.trunc(Number(viewport?.height) || 0),
    };
}

function layoutPlan(ir, components, structureRevision) {
    let plan = layoutPlans.get(ir);
    if( plan && plan.components === components && plan.nodes.length === components.length ) {
        /* HostRuntime owns its cloned tree and supplies a monotonic revision on
         * create/delete. That turns hot synchronous geometry reads into O(depth)
         * work. Standalone callers omit it and retain mutation auto-detection. */
        if( structureRevision !== null && plan.structureRevision === structureRevision )
            return plan;
        let unchanged = true;
        for( let index = 0; index < components.length; index++ ) {
            const node = plan.nodes[index];
            const component = components[index];
            if( node.component !== component || node.fileId !== component.fileId ||
                node.layer !== component.layer ) {
                unchanged = false;
                break;
            }
        }
        if( unchanged ) {
            plan.structureRevision = structureRevision;
            return plan;
        }
    }

    const nodes = new Array(components.length);
    const byFileId = new Map();
    const byComponent = new WeakMap();
    for( let index = 0; index < components.length; index++ ) {
        const component = components[index];
        const node = nodes[index] = {
            component,
            fileId: component.fileId,
            layer: component.layer,
            props: null,
            parent: null,
            children: [],
            geometry: { x: 0, y: 0, w: 0, h: 0 },
            geometryResolved: false,
            traversed: false,
            cycleMark: 0,
        };
        byFileId.set(component.fileId, node);
        byComponent.set(component, node);
    }

    /* The native builder allocates the whole archive before linking it. Resolving
     * parents from a completed map is what makes a child whose parent appears
     * later in a .compack behave exactly like an ordinary child. */
    for( const node of nodes ) {
        const layer = node.layer;
        const parent = layer === null || layer === undefined ? null : byFileId.get(layer);
        if( parent && parent !== node ) node.parent = parent;
    }
    breakParentCycles(nodes);

    const roots = [];
    for( const node of nodes ) {
        if( node.parent ) node.parent.children.push(node);
        else roots.push(node);
    }
    plan = {
        components, nodes, roots, byFileId, byComponent,
        structureRevision,
        sharedExpressions: expressionsShareNodes(components),
    };
    layoutPlans.set(ir, plan);
    return plan;
}

/* Expression trees are usually independent and evaluate faster without memo
 * lookups. Source components can deliberately share state/DAG nodes, though;
 * detect that once with the structural plan and only then enable per-layout
 * memoization. A stale answer is impossible because the memo itself never crosses
 * a layout call. */
function expressionsShareNodes(components) {
    const seen = new WeakSet();
    const pending = [];
    for( const component of components )
        for( const binding of component.dynamic || [] ) pending.push(binding.expr);

    while( pending.length ) {
        const expression = pending.pop();
        if( !isExpr(expression) ) continue;
        if( seen.has(expression) ) return true;
        seen.add(expression);
        for( const child of childrenOf(expression) ) pending.push(child);
    }
    return false;
}

function resolveGeometry(node, viewport) {
    if( node.geometryResolved ) return node.geometry;
    const parent = node.parent;
    const parentGeometry = parent ? resolveGeometry(parent, viewport) : viewport;
    const parentIsLayer = parent && componentIsLayer(parent.component);
    const parentScrollWidth = parentIsLayer ? int(parent.props.scrollWidth) : 0;
    const parentScrollHeight = parentIsLayer ? int(parent.props.scrollHeight) : 0;
    const parentW = parentScrollWidth > 0 ? parentScrollWidth : parentGeometry.w;
    const parentH = parentScrollHeight > 0 ? parentScrollHeight : parentGeometry.h;
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
    const geometry = node.geometry;
    geometry.x = x;
    geometry.y = y;
    geometry.w = w;
    geometry.h = h;
    node.geometryResolved = true;
    return geometry;
}

function traverse(node, context, boxes, viewport) {
    if( node.traversed ) return;
    node.traversed = true;
    const box = resolveNodeBox(node, context, viewport);
    boxes.push(box);

    /* Most large dynamic interfaces are grids of leaf components. They do not
     * need a child clip intersection or a short-lived traversal context. */
    if( node.children.length === 0 ) return;

    const nextContext = childContext(node, context, box);
    for( const child of node.children ) traverse(child, nextContext, boxes, viewport);
}

function resolveNodeBox(node, context, viewport) {
    const { component, props } = node;
    const geometry = resolveGeometry(node, viewport);
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

    return {
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
        dynamic: component.dynamic?.length
            ? component.dynamic.map((d) => d.prop) : EMPTY_COMPONENTS,
        ops: component.ops || [],
        events: component.events ? Object.keys(component.events) : EMPTY_COMPONENTS,
        hooks: component.hooks ? Object.keys(component.hooks) : EMPTY_COMPONENTS,
    };
}

function childContext(node, context, box) {
    const { component, props } = node;
    const clipsChildren = componentClipsChildren(component);
    const scrollLayer = componentIsLayer(component);
    const ownCull = clipsChildren && (box.w <= 0 || box.h <= 0);
    const maxScrollX = scrollLayer
        ? Math.max(0, int(props.scrollWidth) - box.w) : 0;
    const maxScrollY = scrollLayer
        ? Math.max(0, int(props.scrollHeight) - box.h) : 0;
    let childClip = context.clip;
    let childSurface = context.surface;
    if( clipsChildren && !ownCull ) {
        /* Pix2D.setClipping replaces an ordinary ancestor layer clip. A layer's
         * child clip is its screen box intersected with the enclosing surface,
         * not with context.clip. A genuinely scrollable layer then becomes the
         * new surface so its content cannot escape the viewport. */
        childClip = intersect(context.surface, rectangle(box.x, box.y, box.w, box.h));
        if( scrollLayer && (maxScrollX > 0 || maxScrollY > 0) )
            childSurface = childClip;
    }

    return {
        clip: childClip,
        surface: childSurface,
        scrollX: context.scrollX + (maxScrollX > 0 ? box.scrollX : 0),
        scrollY: context.scrollY + (maxScrollY > 0 ? box.scrollY : 0),
        hidden: box.effectiveHidden,
        culled: box.culled,
        depth: context.depth + 1,
    };
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
    let cycleMark = 0;
    for( const node of nodes ) {
        cycleMark++;
        node.cycleMark = cycleMark;
        for( let cursor = node.parent; cursor; cursor = cursor.parent ) {
            if( cursor.cycleMark !== cycleMark ) {
                cursor.cycleMark = cycleMark;
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
