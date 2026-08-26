/*
 * The interface tree, ported from src/ui/uitree.c.
 *
 * This is the single working state a CS2 script mutates. It is not a renderer
 * model and not a React tree: it is the client's own component arena, with the
 * same identity rules, the same lookup precedence and the same dirty
 * bookkeeping, because those rules are load-bearing and approximating them is
 * what produced the last runtime's class of bug.
 *
 * ------------------------------------------------------------------
 * The four things worth knowing before changing anything here
 * ------------------------------------------------------------------
 *
 * 1. STORAGE IS NOT IDENTITY. `cc_deleteall` puts an index on a free list and
 *    the next `cc_create` hands that same index to an unrelated component.
 *    Every slot therefore carries an `incarnation`, and any reference held
 *    across a mutation must be checked against it. A stale reference that
 *    resolves is worse than one that fails.
 *
 * 2. A LOOKUP HAS PRECEDENCE. `cc_find(parent, sub)` prefers the DYNAMIC child
 *    at that sub-id and falls back to the cache-baked one. Sub-ids are signed
 *    and script-created children start at 0x8000. Getting the precedence wrong
 *    finds a real component — the wrong one.
 *
 * 3. THE HOT PATHS ARE QUADRATIC WITHOUT THE INDEXES. A container rebuild
 *    creates rows one at a time and re-finds each one, so an un-indexed
 *    sibling walk is O(rows^2): the chatbox's 500 message rows cost roughly
 *    1,300 steps each before the C tree grew `child_key_index`. The indexes
 *    below are not premature optimisation; they are the difference between a
 *    chat line and a stall.
 *
 * 4. DIRTY IS DELIBERATELY CONSERVATIVE. `dirtyGeneration` over-counts freely
 *    — a write of the same value still bumps it — because over-counting costs
 *    a redundant repaint while under-counting freezes a panel. Anything that
 *    marks a node dirty must bump it in the same breath.
 */

/** What kind of thing a component draws. Mirrors enum UITreeComponentType. */
export const WIDGET_TYPE = Object.freeze({
    LAYER: 0,
    /* An item box: what a script gets from `cc_create` until `cc_setobject`
     * fills it. It draws an obj icon, not a layer, and it does NOT clip. */
    OBJ: 2,
    RECTANGLE: 3,
    TEXT: 4,
    GRAPHIC: 5,
    MODEL: 6,
    LINE: 9,
    ARC: 10,
});

/**
 * `cc_create`'s widget-type argument is NOT the cache's component type.
 *
 * They agree for 3, 4, 5, 6 and 9, which is exactly why this is easy to miss.
 * They differ at ZERO: a cache component of type 0 is a LAYER, but
 * `cc_create(parent, 0, …)` has no case at all in `UITree_CcCreate` and falls
 * to its default — "type 2 (INV) and any unknown: item box until SETOBJECT
 * fills it".
 *
 * Treating it as a layer gives the created node a layer's two powers, and the
 * second one is invisible: it CLIPS ITS CHILDREN. Bankmain's tab builder
 * (`script9582`) makes one of these and hangs the tab sprite and its icon
 * inside it, so both were clipped to a 41x40 box where the reference clips
 * them to the 40x420 panel they really sit in.
 */
export function widgetTypeFromScript(type) {
    switch( type | 0 )
    {
    case WIDGET_TYPE.RECTANGLE: return WIDGET_TYPE.RECTANGLE;
    case WIDGET_TYPE.TEXT: return WIDGET_TYPE.TEXT;
    case WIDGET_TYPE.GRAPHIC: return WIDGET_TYPE.GRAPHIC;
    case WIDGET_TYPE.MODEL: return WIDGET_TYPE.MODEL;
    case WIDGET_TYPE.LINE: return WIDGET_TYPE.LINE;
    case WIDGET_TYPE.ARC: return WIDGET_TYPE.ARC;
    default: return WIDGET_TYPE.OBJ;
    }
}

/** Sub-id sentinels, mirroring UITREE_CHILD_KEY_*. */
const CHILD_KEY_NONE = -0x80000000 + 1;
const CHILD_KEY_UNKNOWN = -0x80000000;

/** Script-created children start here; below it are the cache's own. */
export const DYNAMIC_SUB_ID_BASE = 0x8000;

/** The categories a mutation can dirty. Mirrors the C tree's dirty flags. */
export const DIRTY = Object.freeze({
    PAINT: 'paint',
    GEOMETRY: 'geometry',
    VISIBILITY: 'visibility',
    TOPOLOGY: 'topology',
    ORDER: 'order',
    INTERACTION: 'interaction',
});

/**
 * The hook slots a component can carry, in the order uitree_hook.h declares
 * them. A component with no hooks carries no block at all — most do not, and
 * inlining the slots is what made a C component 13,200 bytes at a 9% fill.
 */
export const HOOK_SLOTS = Object.freeze([
    'onClick', 'onHold', 'onMouseOver', 'onMouseLeave', 'onMouseRepeat',
    'onClickRepeat', 'onRelease', 'onTargetEnter', 'onTargetLeave',
    'onDrag', 'onDragComplete', 'onScrollWheel', 'onKey', 'onKeyDown',
    'onKeyUp', 'onOp', 'onTimer', 'onVarTransmit', 'onInvTransmit',
    'onMiscTransmit', 'onFriendTransmit', 'onChatTransmit', 'onDialogAbort',
    'onResize', 'onSubChange',
    /*
     * The skill channel. The C tree has no slot for it — `rs_cs2_host.c` keeps
     * stat hooks in a host-side table beside the tree — but they are per
     * component either way, and splitting them out here would mean a second
     * place to look for a hook and a second lifetime to get right. Note that
     * `onMiscTransmit` above is NOT this: at this revision "misc" is run
     * energy and run weight, the two transmits with no registry of their own.
     */
    'onStatTransmit',
]);

const HOOK_SLOT_SET = new Set(HOOK_SLOTS);

export class UITreeError extends Error {
    constructor(message) {
        super(message);
        this.name = 'UITreeError';
    }
}

/**
 * One component.
 *
 * A plain object rather than a typed-array row: at the ~7,000 nodes a loaded
 * gameframe holds, the C tree's cache-line packing is answering a DRAM
 * question JavaScript cannot ask, and object property access is already a
 * shape lookup. If a profile ever names field access, the hot fields can move
 * behind these same accessors without the callers noticing.
 */
class UINode {
    constructor(index) {
        this.index = index;
        this.incarnation = 0;
        this.freed = true;

        this.type = WIDGET_TYPE.LAYER;
        this.componentId = -1;
        this.subId = CHILD_KEY_NONE;
        this.dynamic = false;

        this.parent = -1;
        this.firstChild = -1;
        this.nextSibling = -1;
        /* A hint at the tail of the sibling list, never trusted blindly: any
         * mutation may leave it stale, and it is only used when it still looks
         * like this node's last child. Without it, filling a container one
         * child at a time is quadratic. */
        this.lastChildHint = -1;
        this.childKeyMax = CHILD_KEY_NONE;
        /* sub-id -> child index, built lazily and in two halves so a lookup
         * can honour the dynamic-wins precedence without walking. */
        this.childIndex = null;
        /* Set when children carry duplicate sub-ids: only the sibling walk can
         * reproduce their order, so the map must not answer. */
        this.childIndexAmbiguous = false;

        this.hidden = false;
        this.props = {};
        this.ops = null;
        this.hooks = null;
        this.params = null;
        this.freeNext = -1;
    }
}

export function createUITree(options = {}) {
    return new UITree(options);
}

export class UITree {
    constructor({ capacityHint = 256 } = {}) {
        this.nodes = [];
        this.rootIndex = -1;
        this.lastRootIndex = -1;
        this.freeHead = -1;
        this.nextIncarnation = 1;
        /* Cursor for `allocateDynamicComponentId`; see it for the id space. */
        this.nextDynamicUid = 0x8000;
        /** Nodes with `props.dragActive`; see `hasActiveDrag`. */
        this.dragActiveNodes = new Set();
        this.liveCount = 0;
        /*
         * Sibling-walk steps taken since construction.
         *
         * Every linear scan over a child list increments this, so a test can
         * assert that a container rebuild stays linear instead of hoping a
         * wall-clock number stays under a threshold. Quadratic behaviour here
         * is not a slow path, it is the chatbox stall, and it is invisible at
         * the sizes a unit test would otherwise use.
         */
        this.walkSteps = 0;

        /* Bumped by topology changes only. An id index depends on ids alone,
         * so churn in the tree's shape must not invalidate it — hence the
         * separate counters, exactly as the C tree keeps them. */
        this.generation = 0;
        this.idGeneration = 0;
        this.dirtyGeneration = 0;
        this.layoutStale = true;

        this._idIndex = new Map();
        this._idIndexGeneration = -1;
        this._groups = new Map();
        this._dirty = new Map(Object.values(DIRTY).map((name) => [name, new Set()]));

        /* Live-set indexes, so a per-tick pass visits the dozen nodes that
         * carry a timer rather than scanning every node in the tree. */
        this._nodesWithHook = new Map(HOOK_SLOTS.map((slot) => [slot, new Set()]));

        for( let i = 0; i < capacityHint; i++ ) this._grow();
    }

    /* --------------------------------------------------------------
     * Slots
     * ----------------------------------------------------------- */

    _grow() {
        const node = new UINode(this.nodes.length);
        node.freeNext = this.freeHead;
        this.freeHead = node.index;
        this.nodes.push(node);
        return node;
    }

    /** The live node at `index`, or null. Never returns a reclaimed slot. */
    at(index) {
        if( index < 0 || index >= this.nodes.length ) return null;
        const node = this.nodes[index];
        return node.freed ? null : node;
    }

    /**
     * A reference that survives reclamation: index plus the incarnation that
     * occupied it. `resolve` answers null once the slot has been reused, which
     * is the whole point — an index alone would resolve to a stranger.
     */
    ref(index) {
        const node = this.at(index);
        return node ? { index, incarnation: node.incarnation } : null;
    }

    resolve(reference) {
        if( !reference ) return null;
        const node = this.at(reference.index);
        return node && node.incarnation === reference.incarnation ? node : null;
    }

    /* --------------------------------------------------------------
     * Creation and destruction
     * ----------------------------------------------------------- */

    /**
     * Attach a new component.
     *
     * `parentIndex < 0` makes a root. `subId` is the key `cc_find` will look
     * it up by; `dynamic` marks a script-created child, which wins over a
     * cache-baked sibling sharing its key.
     */
    push({ parentIndex = -1, type = WIDGET_TYPE.LAYER, componentId = -1,
           subId = CHILD_KEY_NONE, dynamic = false, props = {} } = {}) {
        if( this.freeHead < 0 ) this._grow();
        const index = this.freeHead;
        const node = this.nodes[index];
        this.freeHead = node.freeNext;

        node.freed = false;
        node.incarnation = this.nextIncarnation++;
        node.type = type;
        node.componentId = componentId;
        node.subId = subId;
        node.dynamic = dynamic;
        node.parent = -1;
        node.firstChild = -1;
        node.nextSibling = -1;
        node.lastChildHint = -1;
        node.childKeyMax = CHILD_KEY_NONE;
        node.childIndex = null;
        node.childIndexAmbiguous = false;
        node.hidden = false;
        node.props = { ...props };
        /*
         * if3 is INHERITED by a script-created child.
         *
         * `UITree_CcCreate` copies the parent's flag onto the new component
         * because the flag decides whether a graphic is stretched to the box
         * the script just gave it or drawn at the sprite's own size -- an
         * if3=0 cell ignores its own cc_setsize. A dynamic child built under
         * an if3 parent with the flag dropped drew every icon unscaled.
         */
        if( node.props.if3 === undefined && parentIndex >= 0 )
        {
            const parent = this.nodes[parentIndex];
            if( parent && !parent.freed && parent.props.if3 ) node.props.if3 = true;
        }
        node.ops = null;
        node.hooks = null;
        node.params = null;
        node.freeNext = -1;
        this.liveCount++;

        this._link(node, parentIndex);
        if( componentId >= 0 ) this._indexId(node);
        this.generation++;
        this.layoutStale = true;
        this.markDirty(index, DIRTY.TOPOLOGY);
        return index;
    }

    _link(node, parentIndex) {
        if( parentIndex < 0 )
        {
            if( this.rootIndex < 0 )
            {
                this.rootIndex = node.index;
            }
            else
            {
                /* The tail hint can be stale — `_unlink` drops it when the last
                 * root is the node it removes, which `reparent` does for every
                 * non-root component of a freshly baked pack. Walking is the
                 * fallback, not the path. */
                let tail = this.lastRootIndex;
                if( tail < 0 || this.nodes[tail].nextSibling !== -1
                    || this.nodes[tail].freed )
                {
                    tail = this.rootIndex;
                    while( this.nodes[tail].nextSibling >= 0 )
                        tail = this.nodes[tail].nextSibling;
                }
                this.nodes[tail].nextSibling = node.index;
            }
            this.lastRootIndex = node.index;
            return;
        }
        const parent = this.at(parentIndex);
        if( !parent ) throw new UITreeError(`parent ${parentIndex} is not live`);
        node.parent = parentIndex;

        /* O(1) append via the tail hint, verified before it is trusted. */
        const hint = parent.lastChildHint;
        const hinted = hint >= 0 ? this.at(hint) : null;
        if( hinted && hinted.parent === parentIndex && hinted.nextSibling === -1 )
        {
            hinted.nextSibling = node.index;
        }
        else if( parent.firstChild < 0 )
        {
            parent.firstChild = node.index;
        }
        else
        {
            let cursor = this.nodes[parent.firstChild];
            while( cursor.nextSibling >= 0 )
            {
                this.walkSteps++;
                cursor = this.nodes[cursor.nextSibling];
            }
            cursor.nextSibling = node.index;
        }
        parent.lastChildHint = node.index;

        this._noteChildKey(parent, node);
    }

    _noteChildKey(parent, child) {
        if( child.subId === CHILD_KEY_NONE ) return;
        /*
         * An UNKNOWN ceiling stays unknown. One insert cannot tighten it: the
         * real maximum is at least whatever the surviving siblings already
         * carry, and only a walk can say what that is.
         *
         * Setting it to the new child's sub-id instead makes the ceiling
         * REJECT children that are there. A rebuild script is where it shows:
         * remove-then-add row 0 pins the ceiling at 0, so the lookup for row 1
         * answers "no such child" above the ceiling without walking, the
         * replace-in-slot never happens, and the container doubles. Interface
         * 600's kudos list went from 43 stripes to 85 that way — 85 and not
         * 86, because row 0 alone was replaced.
         */
        if( parent.childKeyMax === CHILD_KEY_UNKNOWN ) return;
        if( parent.childKeyMax === CHILD_KEY_NONE || child.subId > parent.childKeyMax )
            parent.childKeyMax = child.subId;

        /*
         * INSERT into the key index; do not discard it.
         *
         * Discarding is the obvious thing and it is quadratic: a container is
         * filled one child at a time and each row is looked up right after it
         * is made, so throwing the map away per insert means rebuilding it by
         * a full sibling walk per lookup — n(n+1)/2 steps to build a list of
         * n. Measured at 500,500 steps for 1,000 rows before this. The C tree
         * inserts incrementally for the same reason.
         */
        if( parent.childIndexAmbiguous ) return;
        if( !parent.childIndex ) return; /* not built yet; the lazy build covers it */
        const bucket = child.dynamic ? parent.childIndex.dynamic : parent.childIndex.static;
        if( bucket.has(child.subId) )
        {
            /* Two children now share a key, and only sibling order can say
             * which one a lookup means. The map must stop answering. */
            parent.childIndex = null;
            parent.childIndexAmbiguous = true;
            return;
        }
        bucket.set(child.subId, child.index);
    }

    /**
     * Reclaim an existing DYNAMIC child with this sub-id, if there is one.
     *
     * Replace-in-slot: `cc_create` on a sub-id that is already taken replaces
     * that child rather than adding a second one with the same key. Without
     * it a rebuild script GROWS the tree every time it runs — interface 600's
     * kudos list went from 43 stripes to 86 the first time its transmit hook
     * fired, and every hook after that would have added 43 more.
     *
     * Static children are left alone: a cache-built widget is not something a
     * script created, and reclaiming one would leave a hole nothing fills.
     */
    reclaimDynamicChild(parentIndex, subId) {
        const existing = this.findChildBySubId(parentIndex, subId);
        if( !existing || !existing.dynamic ) return false;
        this._unlink(existing);
        this._reclaimSubtree(existing.index);
        const parent = this.at(parentIndex);
        if( parent )
        {
            parent.lastChildHint = -1;
            parent.childKeyMax = CHILD_KEY_UNKNOWN;
            parent.childIndex = null;
            parent.childIndexAmbiguous = false;
        }
        this.generation++;
        this.layoutStale = true;
        return true;
    }

    /**
     * Move a live node under a new parent, appended last.
     *
     * The bake needs it: a component's parent may sit LATER in the pack than
     * the component does, so the reference allocates every component in pack
     * order unlinked and only then runs a second pass of `UITree_Reparent`.
     * Hoisting a parent to the position of its first child instead — the
     * obvious one-pass version — reorders siblings, and sibling order IS draw
     * order. `notification_display` is the case: its `title` layer is file 6
     * and its text is file 4, so the one-pass bake drew the title panel before
     * the background it is supposed to sit on.
     *
     * Appended, never inserted: that is what the second pass does, and it is
     * what makes pack order come out as child order.
     */
    reparent(index, parentIndex) {
        const node = this.at(index);
        if( !node ) throw new UITreeError(`node ${index} is not live`);
        if( node.parent === parentIndex ) return;
        this._unlink(node);
        node.nextSibling = -1;
        node.parent = -1;
        this._link(node, parentIndex);
        this.generation++;
        this.layoutStale = true;
        this.markDirty(index, DIRTY.TOPOLOGY);
    }

    /**
     * Is a drag actually running?
     *
     * The emit walk's second pass exists only to lift a picked-up subtree
     * above everything else, and when nothing is being dragged every node it
     * reaches takes the descend-only branch. That walk was the single largest
     * traversal in the C client — more visits than the draw pass, because
     * descend-only bypasses the collapsed-layer prune — and on an ordinary
     * frame all of it was waste. So the answer is a maintained COUNT, not a
     * scan: asking by walking would cost what the pass costs.
     */
    hasActiveDrag() { return this.dragActiveNodes.size > 0; }

    /** Write `props.dragActive` through this so the count cannot drift. */
    setDragActive(index, active) {
        const node = this.at(index);
        if( !node ) return;
        node.props.dragActive = !!active;
        if( active ) this.dragActiveNodes.add(index);
        else this.dragActiveNodes.delete(index);
        this.markDirty(index, DIRTY.ORDER);
    }

    /**
     * A component id for a script-created child.
     *
     * A dynamic child is NOT id-less. The reference allocates it
     * `(group << 16) | child`, with `child` cycling 0x8000..0xffff and
     * skipping whatever is live — the high bit is what separates a created
     * component from a cache-authored one, and the id is real: `if_find`
     * addresses it, hooks are registered against it, and the emit walk reports
     * it. Leaving these at -1 is what made 79 of the spellbook's draw commands
     * anonymous while the reference named every one.
     *
     * The cursor is kept so the common case is one probe, and the wrap is
     * bounded: 0x8000 ids, then a second sweep from the bottom, and only then
     * the last id — which is the reference's own resolution, and is a
     * collision rather than a hang.
     */
    allocateDynamicComponentId(groupId) {
        const group = groupId & 0xffff;
        let next = this.nextDynamicUid < 0x8000 ? 0x8000 : this.nextDynamicUid;
        for( let i = 0; i < 0x8000; i++ )
        {
            const child = next;
            const uid = (group << 16) | child;
            next = (child + 1) & 0xffff;
            if( next < 0x8000 ) next = 0x8000;
            if( !this.findByComponentId(uid) )
            {
                this.nextDynamicUid = next;
                return uid;
            }
        }
        for( let child = 0x8000; child <= 0xffff; child++ )
        {
            const uid = (group << 16) | child;
            if( !this.findByComponentId(uid) ) return uid;
        }
        return (group << 16) | 0xffff;
    }

    /**
     * Detach and reclaim a component and everything under it.
     *
     * The slots go on the free list, so every index into the removed subtree
     * is now a live reference to whatever is created next. That is why
     * `incarnation` exists and why nothing may hold a bare index across this.
     */
    remove(index) {
        const node = this.at(index);
        if( !node ) return 0;
        this._unlink(node);
        const removed = this._reclaimSubtree(index);
        this.generation++;
        this.layoutStale = true;
        this.dirtyGeneration++;
        return removed;
    }

    /**
     * Reclaim the DYNAMIC children, keeping the node and its static ones.
     * This is `cc_deleteall`.
     *
     * Dynamic-only is the whole rule, not a refinement of it. A cache-built
     * widget is not something a script created and not something it can
     * rebuild: deleting one leaves a hole nothing fills. The spellbook is the
     * case that shows it — its onload calls deleteall on the layer holding all
     * 199 spell icons, and clearing the statics with them left an interface of
     * eleven empty layers that drew nothing at all.
     *
     * The survivors keep their sub-ids, which is what a list deleting one row
     * expects, so this splices rather than clearing and re-adding.
     */
    removeChildren(index) {
        const node = this.at(index);
        if( !node ) return 0;
        let removed = 0;
        let cursor = node.firstChild;
        let previous = -1;
        while( cursor >= 0 )
        {
            const next = this.nodes[cursor].nextSibling;
            if( this.nodes[cursor].dynamic )
            {
                if( previous < 0 ) node.firstChild = next;
                else this.nodes[previous].nextSibling = next;
                this.nodes[cursor].parent = -1;
                this.nodes[cursor].nextSibling = -1;
                removed += this._reclaimSubtree(cursor);
            }
            else
            {
                previous = cursor;
            }
            cursor = next;
        }
        /*
         * A deleteall that found nothing dynamic CHANGED NOTHING — the child
         * list still points where it did and the key ceiling is still right
         * for the statics. Invalidating them anyway is not merely wasted work:
         * the reference client rebuilds a list by clearing it and re-adding
         * rows, so the steady-state call is a deleteall on an already-empty
         * parent, and the bump made the whole tree look modified every frame.
         */
        if( !removed ) return 0;
        node.lastChildHint = -1;
        /*
         * UNKNOWN, not NONE. NONE means "this parent has no keyed children"
         * and answers every by-sub-id lookup with a miss — but the STATIC
         * children are still here, and their keys are still valid. The removal
         * cannot know the new maximum without walking, so it marks the ceiling
         * unknown and lets the next lookup recompute it.
         */
        node.childKeyMax = CHILD_KEY_UNKNOWN;
        node.childIndex = null;
        node.childIndexAmbiguous = false;
        this.generation++;
        this.layoutStale = true;
        this.markDirty(index, DIRTY.TOPOLOGY);
        return removed;
    }

    _unlink(node) {
        if( node.parent < 0 )
        {
            if( this.rootIndex === node.index )
            {
                this.rootIndex = node.nextSibling;
            }
            else
            {
                let cursor = this.rootIndex;
                while( cursor >= 0 && this.nodes[cursor].nextSibling !== node.index )
                    cursor = this.nodes[cursor].nextSibling;
                if( cursor >= 0 ) this.nodes[cursor].nextSibling = node.nextSibling;
            }
            if( this.lastRootIndex === node.index ) this.lastRootIndex = -1;
            return;
        }
        const parent = this.nodes[node.parent];
        if( parent.firstChild === node.index )
        {
            parent.firstChild = node.nextSibling;
        }
        else
        {
            let cursor = parent.firstChild;
            while( cursor >= 0 && this.nodes[cursor].nextSibling !== node.index )
                cursor = this.nodes[cursor].nextSibling;
            if( cursor >= 0 ) this.nodes[cursor].nextSibling = node.nextSibling;
        }
        if( parent.lastChildHint === node.index ) parent.lastChildHint = -1;
        /* The removed child may have been the ceiling, and recomputing it now
         * would mean a walk; UNKNOWN says "recompute on demand". Only ever too
         * high, never too low — a stale-high value costs a scan, it cannot
         * miss a child. */
        parent.childKeyMax = CHILD_KEY_UNKNOWN;
        parent.childIndex = null;
        parent.childIndexAmbiguous = false;
        this.markDirty(parent.index, DIRTY.TOPOLOGY);
    }

    _reclaimSubtree(index) {
        let removed = 0;
        const stack = [index];
        while( stack.length )
        {
            const at = stack.pop();
            const node = this.nodes[at];
            if( node.freed ) continue;
            for( let child = node.firstChild; child >= 0; child = this.nodes[child].nextSibling )
                stack.push(child);
            this._forgetNode(node);
            node.freed = true;
            node.incarnation = 0;
            node.freeNext = this.freeHead;
            this.freeHead = at;
            this.liveCount--;
            removed++;
        }
        return removed;
    }

    _forgetNode(node) {
        if( node.componentId >= 0 )
        {
            this._idIndex.delete(node.componentId);
            const group = this._groups.get(node.componentId >>> 16);
            if( group ) group.delete(node.index);
            this.idGeneration++;
        }
        if( node.hooks )
            for( const slot of Object.keys(node.hooks) )
                this._nodesWithHook.get(slot)?.delete(node.index);
        for( const set of this._dirty.values() ) set.delete(node.index);
        /* A reclaimed slot must leave the drag count too, or the emit walk
         * keeps paying for a drag pass over a component that no longer
         * exists. Reclaim is the only place a node can vanish. */
        this.dragActiveNodes.delete(node.index);
    }

    /* --------------------------------------------------------------
     * Lookup
     * ----------------------------------------------------------- */

    _indexId(node) {
        this._idIndex.set(node.componentId, node.index);
        const group = node.componentId >>> 16;
        if( !this._groups.has(group) ) this._groups.set(group, new Set());
        this._groups.get(group).add(node.index);
        this.idGeneration++;
    }

    /** The live node carrying `componentId`, or null. */
    findByComponentId(componentId) {
        const index = this._idIndex.get(componentId);
        return index === undefined ? null : this.at(index);
    }

    /** Live node indexes whose component id belongs to `groupId`. */
    nodesInGroup(groupId) {
        const group = this._groups.get(groupId);
        if( !group ) return [];
        return [...group].filter((index) => this.at(index) !== null);
    }

    /** Is this interface group present in the tree at all? */
    hasGroup(groupId) {
        const group = this._groups.get(groupId);
        if( !group ) return false;
        for( const index of group ) if( this.at(index) ) return true;
        return false;
    }

    /**
     * `cc_find(parent, sub)`.
     *
     * A DYNAMIC child wins over a cache-baked one sharing the key: a script
     * that rebuilds a row expects to find the row it just made, not the
     * template underneath it. The ceiling answers a miss without walking,
     * which is what stops a container rebuild being quadratic.
     */
    findChildBySubId(parentIndex, subId) {
        const parent = this.at(parentIndex);
        if( !parent ) return null;

        if( parent.childKeyMax === CHILD_KEY_NONE ) return null;
        if( parent.childKeyMax !== CHILD_KEY_UNKNOWN && subId > parent.childKeyMax ) return null;

        if( !parent.childIndex && !parent.childIndexAmbiguous ) this._buildChildIndex(parent);
        if( parent.childIndex )
        {
            const dynamic = parent.childIndex.dynamic.get(subId);
            if( dynamic !== undefined ) return this.at(dynamic);
            const stat = parent.childIndex.static.get(subId);
            return stat === undefined ? null : this.at(stat);
        }

        /* Ambiguous keys: only the sibling order is authoritative. */
        let fallback = null;
        for( let cursor = parent.firstChild; cursor >= 0; cursor = this.nodes[cursor].nextSibling )
        {
            this.walkSteps++;
            const child = this.nodes[cursor];
            if( child.subId !== subId ) continue;
            if( child.dynamic ) return child;
            if( !fallback ) fallback = child;
        }
        return fallback;
    }

    _buildChildIndex(parent) {
        const dynamic = new Map();
        const stat = new Map();
        let ambiguous = false;
        let max = CHILD_KEY_NONE;
        for( let cursor = parent.firstChild; cursor >= 0; cursor = this.nodes[cursor].nextSibling )
        {
            this.walkSteps++;
            const child = this.nodes[cursor];
            if( child.subId === CHILD_KEY_NONE ) continue;
            if( max === CHILD_KEY_NONE || child.subId > max ) max = child.subId;
            const bucket = child.dynamic ? dynamic : stat;
            if( bucket.has(child.subId) ) { ambiguous = true; break; }
            bucket.set(child.subId, cursor);
        }
        parent.childKeyMax = max;
        if( ambiguous )
        {
            parent.childIndex = null;
            parent.childIndexAmbiguous = true;
            return;
        }
        parent.childIndex = { dynamic, static: stat };
        parent.childIndexAmbiguous = false;
    }

    /** Child indexes in sibling order. */
    children(index) {
        const node = this.at(index);
        if( !node ) return [];
        const out = [];
        for( let cursor = node.firstChild; cursor >= 0; cursor = this.nodes[cursor].nextSibling )
            out.push(cursor);
        return out;
    }

    /* --------------------------------------------------------------
     * Properties
     * ----------------------------------------------------------- */

    /**
     * Write one presentation field.
     *
     * Returns whether the value actually changed. The dirty bump happens
     * either way — see the note at the top about over-counting — but a caller
     * that wants to skip downstream work can ask.
     */
    setProp(index, name, value, category = DIRTY.PAINT) {
        const node = this.at(index);
        if( !node ) return false;
        const changed = node.props[name] !== value;
        node.props[name] = value;
        if( category === DIRTY.GEOMETRY ) this.layoutStale = true;
        this.markDirty(index, category);
        return changed;
    }

    getProp(index, name, fallback = 0) {
        const node = this.at(index);
        if( !node ) return fallback;
        const value = node.props[name];
        return value === undefined ? fallback : value;
    }

    setHidden(index, hidden) {
        const node = this.at(index);
        if( !node ) return false;
        const changed = node.hidden !== hidden;
        node.hidden = hidden;
        /* Visibility always advances retained-output identity: a node the last
         * paint pruned was never visited, so revealing it necessarily changes
         * the next one — a plain paint bump would not say that. */
        this.markDirty(index, DIRTY.VISIBILITY);
        this.layoutStale = true;
        return changed;
    }

    /** True when this node or any ancestor is hidden. */
    hiddenByAncestor(index) {
        for( let cursor = index; cursor >= 0; )
        {
            const node = this.at(cursor);
            if( !node ) return true;
            if( node.hidden ) return true;
            cursor = node.parent;
        }
        return false;
    }

    /* --------------------------------------------------------------
     * Hooks
     * ----------------------------------------------------------- */

    /**
     * Bind (or clear) one event hook.
     *
     * Returns false when the binding is identical to the one already there.
     * That answer matters: scripts re-arm every `if_seton*` wholesale on each
     * rebuild, and the great majority of those calls restate what is already
     * bound. Re-setting would free and rebuild the argument list to arrive
     * where it started.
     */
    setHook(index, slot, binding) {
        if( !HOOK_SLOT_SET.has(slot) ) throw new UITreeError(`unknown hook slot '${slot}'`);
        const node = this.at(index);
        if( !node ) return false;

        const existing = node.hooks ? node.hooks[slot] : undefined;
        if( hookEquals(existing, binding) ) return false;

        if( binding === null || binding === undefined )
        {
            if( node.hooks )
            {
                delete node.hooks[slot];
                if( Object.keys(node.hooks).length === 0 ) node.hooks = null;
            }
            this._nodesWithHook.get(slot).delete(index);
        }
        else
        {
            if( !node.hooks ) node.hooks = {};
            node.hooks[slot] = binding;
            this._nodesWithHook.get(slot).add(index);
        }
        this.markDirty(index, DIRTY.INTERACTION);
        return true;
    }

    getHook(index, slot) {
        const node = this.at(index);
        return node && node.hooks ? (node.hooks[slot] ?? null) : null;
    }

    /** Live node indexes carrying a hook in `slot`, for a per-tick pass. */
    nodesWithHook(slot) {
        const set = this._nodesWithHook.get(slot);
        if( !set ) throw new UITreeError(`unknown hook slot '${slot}'`);
        return [...set].filter((index) => this.at(index) !== null);
    }

    /* --------------------------------------------------------------
     * Dirt
     * ----------------------------------------------------------- */

    markDirty(index, category) {
        const set = this._dirty.get(category);
        if( !set ) throw new UITreeError(`unknown dirty category '${category}'`);
        set.add(index);
        this.dirtyGeneration++;
    }

    dirtyNodes(category) {
        const set = this._dirty.get(category);
        if( !set ) throw new UITreeError(`unknown dirty category '${category}'`);
        return [...set];
    }

    clearDirty() {
        for( const set of this._dirty.values() ) set.clear();
    }

    /** A cheap "has anything happened" signal for a retention gate. */
    revision() {
        return `${this.generation}:${this.dirtyGeneration}:${this.idGeneration}`;
    }
}

/**
 * Would setting this binding leave the slot exactly as it is?
 *
 * "Identical", not "similar": the arguments are positional and a difference
 * anywhere in them is a different hook. The comparison is worth its cost
 * because it is run against a re-arm that usually matches.
 */
function hookEquals(a, b) {
    if( !a && !b ) return true;
    if( !a || !b ) return false;
    if( a.scriptId !== b.scriptId ) return false;
    return sameValues(a.args, b.args) && sameValues(a.triggers, b.triggers);
}

function sameValues(a = [], b = []) {
    if( a.length !== b.length ) return false;
    for( let i = 0; i < a.length; i++ ) if( a[i] !== b[i] ) return false;
    return true;
}
