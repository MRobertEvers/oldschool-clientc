/*
 * Where every component ends up, ported from src/ui/ui_if3_layout.h.
 *
 * An IF3 box is not stored, it is COMPUTED: a component carries a base
 * position and size plus a mode per axis saying how to read them against its
 * parent. Mode 0 is literal, mode 1 is "the parent minus this", mode 2 is a
 * fraction in 1/16384ths, and the position modes add centring and
 * right/bottom anchoring on top. Getting one of them wrong does not produce a
 * visibly broken layout — it produces a panel that is right until the window
 * is a different size.
 *
 * Two arithmetic details are load-bearing and are the reason this is a port
 * rather than a re-derivation:
 *
 *   - the fractional modes are `(a * b) >> 14` over a 64-BIT product. In
 *     JavaScript that has to go through BigInt or an explicit split, because a
 *     parent dimension times a 14-bit fraction passes 2^31 and a double
 *     silently rounds where the client truncates.
 *
 *   - centring divides by two with JAVA truncation toward zero, which differs
 *     from an arithmetic shift for odd negative overhangs: -27 / 2 is -13, not
 *     -14. A component wider than its parent is exactly when that happens.
 *
 * Resolution is invalidation-driven. `tree.layoutStale` is set by every write
 * to a layout input and cleared here; a frame that touched no geometry does
 * not walk. That is not an optimisation bolted on — a geometry getter is a
 * BARRIER, so the walk has to be cheap enough to run mid-script.
 */

import { DIRTY } from './uitree.js';

/** The canvas the root box is resolved against, unless a caller says otherwise. */
export const DEFAULT_ROOT = Object.freeze({ x: 0, y: 0, width: 765, height: 503 });

/** `(a * b) >> 14` at 64-bit width, which is what the client computes. */
export function mulShift14(a, b) {
    return Number((BigInt(a | 0) * BigInt(b | 0)) >> 14n) | 0;
}

/** Java integer division: truncate toward zero, not floor. */
function divTruncate(a, b) {
    return (a / b) | 0;
}

/**
 * One dimension, from its mode.
 *
 *   0  the base value
 *   1  the parent's, less the base ("fill, leaving this much")
 *   2  a fraction of the parent, in 1/16384ths
 *   4  handled by the caller: derived from the other axis and the aspect ratio
 */
export function dimFromParentMode(mode, base, parentDim) {
    switch( mode )
    {
    case 0: return base;
    case 1: return (parentDim - base) | 0;
    case 2: return mulShift14(parentDim, base);
    default: return base;
    }
}

/**
 * One axis, from its position mode.
 *
 *   0  parent origin + base
 *   1  centred, plus base
 *   2  anchored to the far edge
 *   3  a fraction of the parent
 *   4  centred, offset by a fraction
 *   5  anchored to the far edge by a fraction
 */
export function axisFromPositionMode(mode, base, parentOrigin, parentDim, selfDim) {
    switch( mode )
    {
    case 0: return (parentOrigin + base) | 0;
    case 1: return (parentOrigin + divTruncate(parentDim - selfDim, 2) + base) | 0;
    case 2: return (parentOrigin + parentDim - base - selfDim) | 0;
    case 3: return (parentOrigin + mulShift14(parentDim, base)) | 0;
    case 4: return (parentOrigin + divTruncate(parentDim - selfDim, 2)
                    + mulShift14(parentDim, base)) | 0;
    case 5: return (parentOrigin + parentDim - mulShift14(parentDim, base) - selfDim) | 0;
    default: return (parentOrigin + base) | 0;
    }
}

/**
 * Size, including the aspect-ratio modes.
 *
 * Mode 4 on an axis means "whatever keeps the aspect", so the two axes cannot
 * both use it — the second would read a dimension the first had not settled.
 * The client resolves width first and derives height from it, and the reverse
 * only when height alone asks; that order is preserved here.
 */
export function computeSize(props, parentWidth, parentHeight) {
    const widthMode = props.widthMode | 0;
    const heightMode = props.heightMode | 0;
    const aspectW = (props.aspectWidth | 0) > 0 ? props.aspectWidth | 0 : 1;
    const aspectH = (props.aspectHeight | 0) > 0 ? props.aspectHeight | 0 : 1;

    let width = dimFromParentMode(widthMode, props.width | 0, parentWidth);
    let height = dimFromParentMode(heightMode, props.height | 0, parentHeight);

    if( widthMode === 4 ) width = divTruncate(aspectW * height, aspectH);
    if( heightMode === 4 ) height = divTruncate(aspectH * width, aspectW);

    return { width: Math.max(0, width), height: Math.max(0, height) };
}

/**
 * The box a child is laid out inside.
 *
 * For a LAYER carrying a scroll extent, that is the SCROLL size, not the
 * layer's own — the content is bigger than the window and the children are
 * placed against the content. Every write to a scroll extent must therefore
 * invalidate the layout, which is why `if_setscrollsize` dirties geometry.
 */
function parentBox(tree, index, root) {
    if( index < 0 ) return { x: root.x, y: root.y, width: root.width, height: root.height };
    const node = tree.at(index);
    if( !node ) return { x: root.x, y: root.y, width: root.width, height: root.height };
    const box = node.layout ?? { x: 0, y: 0, width: 0, height: 0 };
    const scrollWidth = node.props.scrollWidth | 0;
    const scrollHeight = node.props.scrollHeight | 0;
    return {
        x: box.x,
        y: box.y,
        width: scrollWidth > 0 ? scrollWidth : box.width,
        height: scrollHeight > 0 ? scrollHeight : box.height,
    };
}

export function createLayout(options = {}) {
    return new Layout(options);
}

export class Layout {
    constructor({ tree, root = DEFAULT_ROOT } = {}) {
        this.tree = tree;
        this.root = { ...root };
        this.stats = { resolves: 0, nodesVisited: 0, skipped: 0 };
    }

    /** Resize the canvas. A different root box invalidates every root-level box. */
    setRoot(root) {
        /* Compare the MERGED box, not the argument: a caller passing only
         * `{ width, height }` leaves x and y undefined, and comparing those
         * against the current origin reports a change every time. */
        const next = { ...this.root, ...root };
        const changed = next.x !== this.root.x || next.y !== this.root.y
            || next.width !== this.root.width || next.height !== this.root.height;
        this.root = next;
        if( changed ) this.tree.layoutStale = true;
        return changed;
    }

    /**
     * Resolve every box whose inputs may have moved.
     *
     * Returns false without walking when nothing invalidated since the last
     * pass — the case an idle frame is in, and the reason a geometry getter
     * can afford to call this.
     */
    resolve({ force = false } = {}) {
        if( !force && !this.tree.layoutStale )
        {
            this.stats.skipped++;
            return false;
        }
        this.stats.resolves++;

        const tree = this.tree;
        /* Parents before children, in sibling order — a child's box is a
         * function of its parent's, so any other order reads a stale one. */
        const stack = [];
        for( let cursor = tree.rootIndex; cursor >= 0; cursor = tree.nodes[cursor].nextSibling )
            stack.push(cursor);
        stack.reverse();

        while( stack.length )
        {
            const index = stack.pop();
            const node = tree.at(index);
            if( !node ) continue;
            this.stats.nodesVisited++;

            const parent = parentBox(tree, node.parent, this.root);
            const { width, height } = computeSize(node.props, parent.width, parent.height);
            const x = axisFromPositionMode(
                node.props.xMode | 0, node.props.x | 0, parent.x, parent.width, width);
            const y = axisFromPositionMode(
                node.props.yMode | 0, node.props.y | 0, parent.y, parent.height, height);

            node.layout = { x, y, width, height };

            const children = [];
            for( let c = node.firstChild; c >= 0; c = tree.nodes[c].nextSibling ) children.push(c);
            for( let i = children.length - 1; i >= 0; i-- ) stack.push(children[i]);
        }

        tree.layoutStale = false;
        return true;
    }

    /** The resolved box, or null when the node has never been laid out. */
    boxOf(index) {
        const node = this.tree.at(index);
        return node?.layout ?? null;
    }
}

/**
 * Give a kernel a layout, and make its geometry getters read resolved values.
 *
 * Installing a layout is ALL that is needed. The kernel's `_geometry` already
 * calls `onLayoutNeeded` before reading — that is the barrier — and already
 * knows that x and y are answered relative to the parent while width and
 * height are answered absolute.
 *
 * This function used to install a second `_geometry` on the instance, which
 * shadowed the kernel's and returned the ABSOLUTE x and y. Two copies of one
 * rule is how they came to disagree: the kudos list's scrollbar caps were
 * positioned from a `.cc_gety` that answered 71 where the reference answers
 * 16, and landed 55 pixels below the thumb they cap.
 */
export function attachLayout(host, { root = DEFAULT_ROOT } = {}) {
    const layout = createLayout({ tree: host.tree, root });
    host.layout = layout;
    host.onLayoutNeeded = () => layout.resolve();
    return layout;
}

export { DIRTY };
