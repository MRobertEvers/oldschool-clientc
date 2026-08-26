/*
 * What the pointer is over. Ported from src/ui/uitree_interact.c.
 *
 * There are three tests because there are three different questions, and the
 * C client learned the hard way that they cannot be one function:
 *
 *   hitTest      what did the user CLICK?      passthrough, no-click-through
 *   hoverTarget  what id is HOVERED?           hover hooks, reveal-on-hover
 *   dropTarget   what is under a DRAGGED node? visits mounts after children
 *
 * What they must share is the PRUNE RULE, and they take it from `emit.js` —
 * the same `shouldPrune` the draw walk uses. A widget that draws but cannot be
 * hit is a dead button; one that hits but does not draw is a click landing on
 * nothing. Both shipped.
 *
 * ------------------------------------------------------------------
 * Passthrough
 * ------------------------------------------------------------------
 *
 * Containers and decoration pass clicks through to whatever is behind them —
 * a layer is not a button. But a node carrying any interaction hook, or one
 * that is draggable, is ALWAYS a real target regardless of its type. That
 * exception is the whole rule: without it a text label with an `onClick` is
 * decoration, and with it a plain background layer is not a click sink.
 *
 * The walk is last-match-wins in tree order, which is what makes a later
 * sibling drawn on top also win the click.
 */

import { clipsChildren, intersect, shouldPrune } from './emit.js';

const TYPE_LAYER = 0;
const TYPE_RECTANGLE = 3;
const TYPE_TEXT = 4;
const TYPE_GRAPHIC = 5;
const TYPE_MODEL = 6;
const TYPE_LINE = 9;

/** Hook slots that make a node a click target whatever its type. */
const INTERACTIVE_HOOKS = ['onClick', 'onOp', 'onHold', 'onDrag', 'onClickRepeat', 'onRelease'];

/** Hook slots that make a node a hover target. */
const HOVER_HOOKS = ['onMouseOver', 'onMouseLeave', 'onMouseRepeat'];

export function createHitTester(options = {}) {
    return new HitTester(options);
}

export class HitTester {
    constructor({ tree, layout = null } = {}) {
        this.tree = tree;
        this.layout = layout;
    }

    /**
     * The component a click at (x, y) lands on, or null.
     *
     * Last match wins, so a sibling drawn later also takes the click.
     */
    hitTest(x, y, { hoveredComponentId = -1 } = {}) {
        return this._walk(x, y, hoveredComponentId, (node) => this._isClickTarget(node));
    }

    /** The component id the pointer is hovering, or -1. */
    hoverTarget(x, y, { hoveredComponentId = -1 } = {}) {
        const node = this._walk(x, y, hoveredComponentId, (node) => this._isHoverTarget(node));
        return node ? node.componentId : -1;
    }

    /**
     * What a dragged widget would drop onto.
     *
     * Mounted sub-interfaces are visited AFTER a container's own children, so
     * a panel mounted into a slot wins over the slot's decoration — the same
     * mount-last sweep the draw path uses, so both agree on order.
     */
    dropTarget(x, y, { exclude = -1, hoveredComponentId = -1 } = {}) {
        return this._walk(x, y, hoveredComponentId, (node) => {
            if( node.index === exclude ) return false;
            return this._isClickTarget(node) || node.type === TYPE_LAYER;
        }, { mountsLast: true });
    }

    /* --------------------------------------------------------------
     * The shared walk
     * ----------------------------------------------------------- */

    _walk(x, y, hoveredComponentId, accept, { mountsLast = false } = {}) {
        const root = this.layout
            ? this.layout.root
            : { x: 0, y: 0, width: Infinity, height: Infinity };
        const surface = { x: root.x, y: root.y, width: root.width, height: root.height };
        let found = null;

        const visit = (index, clip, scrollX, scrollY) => {
            const node = this.tree.at(index);
            if( !node ) return;
            /* The one rule shared with the draw walk. */
            if( shouldPrune(node, hoveredComponentId) ) return;

            const box = this._box(node);
            const screenX = box.x - scrollX;
            const screenY = box.y - scrollY;

            let childClip = clip;
            if( clipsChildren(node) )
            {
                childClip = intersect(
                    { x: screenX, y: screenY, width: box.width, height: box.height }, clip);
                /* Nothing inside a collapsed clip can be reached, and neither
                 * can the layer itself — it draws nothing. */
                if( childClip.width <= 0 || childClip.height <= 0 ) return;
            }

            const inside = contains(clip, x, y)
                && x >= screenX && x < screenX + box.width
                && y >= screenY && y < screenY + box.height;

            /*
             * Remember what had matched BEFORE this subtree.
             *
             * `noClickThrough` has to distinguish "nothing here matched, so I
             * block what is behind me" from "my own child matched, so it
             * wins". Both look like a non-null `found` at the end; only the
             * before-and-after comparison tells them apart.
             */
            const foundBefore = found;
            if( inside && accept(node) ) found = node;

            let childScrollX = scrollX;
            let childScrollY = scrollY;
            if( node.type === TYPE_LAYER )
            {
                const scrollWidth = node.props.scrollWidth | 0;
                const scrollHeight = node.props.scrollHeight | 0;
                if( scrollWidth > box.width )
                    childScrollX += clamp(node.props.scrollX | 0, scrollWidth - box.width);
                if( scrollHeight > box.height )
                    childScrollY += clamp(node.props.scrollY | 0, scrollHeight - box.height);
            }

            const children = [];
            for( let c = node.firstChild; c >= 0; c = this.tree.nodes[c].nextSibling )
                children.push(c);
            if( mountsLast )
            {
                /* Two sweeps: the container's own children, then the mounts. */
                for( const child of children )
                    if( !isMount(this.tree, node, child) )
                        visit(child, childClip, childScrollX, childScrollY);
                for( const child of children )
                    if( isMount(this.tree, node, child) )
                        visit(child, childClip, childScrollX, childScrollY);
            }
            else
            {
                for( const child of children )
                    visit(child, childClip, childScrollX, childScrollY);
            }

            /*
             * A modal stops the click here. It overrides an EARLIER sibling's
             * match — that is what "does not pass through" means — but never
             * a match from inside itself, which is a real button on the panel.
             */
            if( inside && node.props.noClickThrough && found === foundBefore ) found = node;
        };

        for( let cursor = this.tree.rootIndex; cursor >= 0;
             cursor = this.tree.nodes[cursor].nextSibling )
            visit(cursor, surface, 0, 0);
        return found;
    }

    /**
     * Is this a click target?
     *
     * A node carrying an interaction hook or a drag flag always is. Otherwise
     * only the decorative leaf types are, and containers pass through.
     */
    _isClickTarget(node) {
        if( node.hooks )
            for( const slot of INTERACTIVE_HOOKS ) if( node.hooks[slot] ) return true;
        if( node.props.draggable ) return true;
        if( node.ops && node.ops.some((op) => op) ) return true;
        return false;
    }

    _isHoverTarget(node) {
        if( node.componentId < 0 ) return false;
        if( node.hooks )
        {
            for( const slot of HOVER_HOOKS ) if( node.hooks[slot] ) return true;
            for( const slot of INTERACTIVE_HOOKS ) if( node.hooks[slot] ) return true;
        }
        if( node.ops && node.ops.some((op) => op) ) return true;
        return false;
    }

    _box(node) {
        return node.layout ?? {
            x: node.props.x | 0, y: node.props.y | 0,
            width: node.props.width | 0, height: node.props.height | 0,
        };
    }
}

/** A child whose interface group differs from its parent's is a mount. */
function isMount(tree, parent, childIndex) {
    const child = tree.at(childIndex);
    if( !child || child.componentId < 0 || parent.componentId < 0 ) return false;
    return (child.componentId >>> 16) !== (parent.componentId >>> 16);
}

function contains(rect, x, y) {
    return x >= rect.x && x < rect.x + rect.width
        && y >= rect.y && y < rect.y + rect.height;
}

function clamp(value, max) {
    if( max <= 0 ) return 0;
    return Math.max(0, Math.min(max, value));
}

export { TYPE_LAYER, TYPE_RECTANGLE, TYPE_TEXT, TYPE_GRAPHIC, TYPE_MODEL, TYPE_LINE };
