/*
 * What to draw, and in what order. Ported from src/ui/uitree_emit.c.
 *
 * The output is a FLAT command list, not a tree. A painter walks it once, in
 * order, and needs to know nothing about components — which is what lets the
 * same list feed a canvas, a test, or a comparison against the C client.
 *
 * ------------------------------------------------------------------
 * ONE interleaved pass, and a drag pass only when something is being dragged
 * ------------------------------------------------------------------
 *
 * A widget emits its own fill, sprite and text INLINE and then descends into
 * its children, in tree order — the reference's `drawNode`, and
 * `uitree_emit.c`'s single `emit_walk_pass`.
 *
 * Splitting text into a pass of its own is the tempting version and it is
 * wrong: it puts every text in the tree above every non-text, so a widget
 * group that should cover an earlier one — an open dropdown over a label —
 * draws under that label's text. The C client had exactly that bug and its own
 * comment records the fix. The stone border of interface 600 is the smallest
 * witness: its title text is the SECOND thing the reference draws, and a text
 * pass moves it past all 95 of the panel's other texts.
 *
 * The drag pass is second and real: a picked-up subtree follows the pointer
 * over content it is not part of, so it draws above everything. It is skipped
 * entirely when no drag is running — every node it reaches would take the
 * descend-only branch, and on an ordinary frame that walk was the single
 * largest traversal in the client and all of it waste.
 *
 * ------------------------------------------------------------------
 * Pruning must match the hit tests EXACTLY
 * ------------------------------------------------------------------
 *
 * Whatever this walk skips, the click, hover and drop-target tests must skip
 * too. A widget that draws but does not hit is a dead button; one that hits but
 * does not draw is a click landing on nothing. Both have happened — hover once
 * pruned only hidden *layers*, so `if_sethide` on a type-5 spell icon left its
 * mouse-repeat hook live and jewellery tooltips pierced through the spellbook.
 * That is why the prune rules live in `shouldPrune` and both walks call it.
 *
 * ------------------------------------------------------------------
 * Retention
 * ------------------------------------------------------------------
 *
 * A frame whose tree did not change reuses the previous list without walking.
 * The gate is three terms — the dirty generation, the layout resolve count and
 * the hovered id — because each can move without the others: a hover change
 * repaints nothing structural, and a layout resolve can move a clip rect while
 * no node is dirty at all.
 */

/** What a command tells the painter to draw. */
export const EMIT_KIND = Object.freeze({
    RECT: 'rect',
    TEXT: 'text',
    SPRITE: 'sprite',
    MODEL: 'model',
    LINE: 'line',
});

/** Widget types, as the cache numbers them. */
const TYPE_LAYER = 0;
const TYPE_RECTANGLE = 3;
const TYPE_TEXT = 4;
const TYPE_GRAPHIC = 5;
const TYPE_MODEL = 6;
const TYPE_LINE = 9;

/** Which pass a command belongs to. Text is drawn above everything else. */
function passOf(kind) {
    return kind === EMIT_KIND.TEXT ? 1 : 0;
}

export function createEmitter(options = {}) {
    return new Emitter(options);
}

export class Emitter {
    constructor({ tree, layout = null } = {}) {
        this.tree = tree;
        this.layout = layout;
        this.commands = [];

        /* The three terms of the retention gate; see the header. */
        this._lastDirty = -1;
        this._lastResolves = -1;
        this._lastHover = -2;

        this.stats = { walks: 0, retained: 0, visited: 0, emitted: 0 };
    }

    /**
     * Rebuild the command list if anything could have changed it.
     *
     * Returns true when it walked. A caller that paints only on true gets the
     * client's own behaviour: an idle frame costs nothing at all, including
     * when a closed interface is still ticking a model behind it.
     */
    walk({ hoveredComponentId = -1, force = false } = {}) {
        const tree = this.tree;
        const resolves = this.layout ? this.layout.stats.resolves : 0;
        if( !force
            && tree.dirtyGeneration === this._lastDirty
            && resolves === this._lastResolves
            && hoveredComponentId === this._lastHover )
        {
            this.stats.retained++;
            return false;
        }
        this._lastDirty = tree.dirtyGeneration;
        this._lastResolves = resolves;
        this._lastHover = hoveredComponentId;
        this.stats.walks++;

        this.commands = [];
        const root = this.layout
            ? this.layout.root
            : { x: 0, y: 0, width: Infinity, height: Infinity };
        const surface = { x: root.x, y: root.y, width: root.width, height: root.height };

        /*
         * One interleaved walk in tree order, then the drag pass — and the
         * drag pass only when a drag is running, because otherwise every node
         * it reaches takes the descend-only branch and the whole traversal is
         * waste. See the header for why text does NOT get a pass of its own.
         */
        const passes = tree.hasActiveDrag?.() ? [false, true] : [false];
        for( const dragPass of passes )
            for( let cursor = tree.rootIndex; cursor >= 0;
                 cursor = tree.nodes[cursor].nextSibling )
            {
                this._walkNode(cursor, {
                    clip: surface, surface, scrollX: 0, scrollY: 0,
                    dragPass, inDeferred: false, dragDx: 0, dragDy: 0,
                    hoveredComponentId,
                });
            }
        this.stats.emitted += this.commands.length;
        return true;
    }

    _walkNode(index, context) {
        const tree = this.tree;
        const node = tree.at(index);
        if( !node ) return;
        this.stats.visited++;

        if( shouldPrune(node, context.hoveredComponentId) ) return;

        const box = this._box(node);

        /*
         * A collapsed clipping layer removes its whole subtree.
         *
         * Returning here also skips this node's own draw, which costs nothing:
         * the types that clip paint no content of their own.
         */
        if( clipsChildren(node) && (box.width <= 0 || box.height <= 0) ) return;

        let { dragDx, dragDy, inDeferred } = context;
        let inDrag = context.inDrag ?? false;
        if( node.props.dragActive )
        {
            /* The delta is against this node's PRE-DRAG screen position, so a
             * composite widget moves as one unit rather than only its own
             * drawn content. */
            dragDx = (node.props.dragVisualX | 0) - (box.x - context.scrollX);
            dragDy = (node.props.dragVisualY | 0) - (box.y - context.scrollY);
            inDrag = true;
            /* A picked-up drag defers its whole subtree to the drag pass; a
             * scrollbar-style drag (behaviour 1) stays in place. */
            inDeferred = (node.props.dragBehaviour | 0) !== 1;
        }

        /* A deferred subtree draws only on the drag pass, and a non-deferred
         * node on the drag pass descends without drawing — it is only there to
         * reach a deferred source deeper down. */
        const drawable = context.dragPass ? inDeferred : !inDeferred;

        if( drawable && (node.props.trans | 0) < 255 )
            this._emitSelf(node, box, context, { dragDx, dragDy, inDrag });

        /*
         * Children: clip to this node's box intersected with the enclosing
         * SURFACE, never compounded with an ancestor layer's clip — the
         * reference's `Pix2D.setClipping` OVERWRITES, clamping only to the
         * surface. The same helper decides hit-testing, so pixels and
         * hitboxes agree.
         *
         * A plain nested layer therefore does NOT become the surface for what
         * is under it. Making it one is compounding by another name, and it
         * is what put bankmain's last tab under a 40-pixel clip where the
         * reference gives it the 420 of the panel it really sits in.
         *
         * Only a SCROLL VIEWPORT establishes a surface (and, in the C client,
         * the builtin chat and sidebar, which this tree has no equivalent
         * for). That exception is not symmetry: an IF3 scroll area is a
         * viewport layer holding a content layer sized to the SCROLL EXTENT,
         * so the content's own box is taller than what may be shown, and
         * without the viewport as its surface it clips only to the screen and
         * spills out of the panel.
         */
        let clip = context.clip;
        let surface = context.surface;
        if( clipsChildren(node) )
        {
            /*
             * A clipping layer culls its subtree when its OWN BOX is
             * degenerate — `UITree_LayerCullsChildren(c, w, h)` is
             * `clips_children && (w <= 0 || h <= 0)` — and NOT when its clip
             * comes out empty against the surface.
             *
             * The two are different questions. A 600x600 layer parked at
             * y=600 on a 503-tall canvas intersects to nothing, and the
             * reference still walks it and still emits its children with that
             * empty clip; the painter is what draws none of them. Culling on
             * the intersection lost twelve of `snow_heavy`'s sixteen tiles.
             */
            if( box.width <= 0 || box.height <= 0 ) return;
            const clipX = box.x - context.scrollX + (inDrag ? dragDx : 0);
            const clipY = box.y - context.scrollY + (inDrag ? dragDy : 0);
            const own = { x: clipX, y: clipY, width: box.width, height: box.height };
            clip = intersect(own, surface);
            if( establishesSurface(node, box) ) surface = clip;
        }

        let scrollX = context.scrollX;
        let scrollY = context.scrollY;
        if( node.type === TYPE_LAYER )
        {
            const scrollWidth = node.props.scrollWidth | 0;
            const scrollHeight = node.props.scrollHeight | 0;
            if( scrollWidth > box.width ) scrollX += clampScroll(node.props.scrollX | 0,
                scrollWidth - box.width);
            if( scrollHeight > box.height ) scrollY += clampScroll(node.props.scrollY | 0,
                scrollHeight - box.height);
        }

        const childContext = {
            ...context, clip, surface, scrollX, scrollY, dragDx, dragDy, inDrag, inDeferred,
        };
        for( let child = node.firstChild; child >= 0; child = tree.nodes[child].nextSibling )
            this._walkNode(child, childContext);
    }

    _emitSelf(node, box, context, drag) {
        const kind = emitKindOf(node);
        if( !kind ) return;
        /*
         * PRESENCE, not just appearance.
         *
         * An empty text and an absent model emit NOTHING — `uitree_emit.c`
         * returns false for both — and that is not an optimisation. The
         * chatbox builds 500 scrollback rows up front and fills the ones it
         * has messages for; emitting the empty ones put 500 zero-size text
         * commands into a list the reference gives 32. And the 254
         * special-attack bar is built entirely out of the model half: ten dark
         * cover segments over a green bar, each vanishing when its variant
         * resolves to "no model".
         *
         * The variant is resolved HERE with the same helper the painter draws
         * with, because presence and appearance disagreeing is the same class
         * of bug as the draw walk and the hit tests disagreeing about pruning.
         */
        if( kind === EMIT_KIND.TEXT )
        {
            const hovered = node.componentId >= 0
                && node.componentId === context.hoveredComponentId;
            if( !effectiveText(node.props, hovered) ) return;
        }
        if( kind === EMIT_KIND.MODEL )
        {
            const hovered = node.componentId >= 0
                && node.componentId === context.hoveredComponentId;
            if( effectiveModel(node.props, hovered) < 0 ) return;
        }
        /*
         * A graphic with nothing to show is the same rule, and it is the one
         * this walk was missing. `uitree_emit.c` ends its RS_GRAPHIC branch
         * with `if( out->scene_id <= 0 ) return false;`, and a component whose
         * cache record declares neither `graphic=` nor `activegraphic=` never
         * gets a scene at all (`torirs_component_apply_graphic_hitbox_only`
         * marks it, and the same branch returns false for that too).
         *
         * Sprite id 0 is a real sprite — the ref is built from `graphic >= 0`
         * — so the test is `< 0`, not falsy. An item overlay counts as
         * something to show whatever the sprite says: `cc_setobject` on a
         * type-5 draws the icon through the same command.
         *
         * Without this, `ge_pricechecker` drew its empty `otheritem` cell,
         * which the reference does not, and 60-odd interfaces carried a
         * placeholder graphic the C client prunes.
         */
        if( kind === EMIT_KIND.SPRITE )
        {
            /*
             * A CONTENT slot draws none of this. The world viewport, the
             * minimap and the compass are their own emit kinds in the C
             * client — `uitree_build.c` retypes a component carrying one of
             * these client codes to its builtin, and the RS_GRAPHIC arm
             * returns false for any that reached the tree some other way.
             *
             * This preview does not host those kinds and the parity oracle
             * filters them out of the reference, so emitting them as ordinary
             * sprites is a difference in both directions: the minimap and
             * compass of five toplevel interfaces drew as 35x35 and 152x152
             * placeholder art the reference has no command for at all.
             */
            if( CONTENT_CLIENT_CODES.has(node.props.clientCode | 0) ) return;
            const hovered = node.componentId >= 0
                && node.componentId === context.hoveredComponentId;
            if( effectiveSprite(node.props, hovered) < 0
                && (node.props.obj | 0) <= 0 ) return;
        }

        const x = box.x - context.scrollX + (drag.inDrag ? drag.dragDx : 0);
        const y = box.y - context.scrollY + (drag.inDrag ? drag.dragDy : 0);
        /* An empty clip is a command all the same — see the cull note above. */
        const clip = context.clip;

        this.commands.push({
            kind,
            node: node.index,
            componentId: node.componentId,
            x, y, width: box.width, height: box.height,
            clip,
            /* Presentation is passed through whole: the painter decides what a
             * `colour` or a `sprite` means, and the walk decides where. */
            props: node.props,
            /* The hovered component swaps to its `over` colour, text and
             * sprite variants; that is a draw-time choice, so it travels with
             * the command rather than being resolved into the tree. */
            hovered: node.componentId >= 0 && node.componentId === context.hoveredComponentId,
            trans: node.props.trans | 0,
        });
    }

    _box(node) {
        return node.layout ?? {
            x: node.props.x | 0, y: node.props.y | 0,
            width: node.props.width | 0, height: node.props.height | 0,
        };
    }
}

/**
 * The variant a widget actually shows.
 *
 * Shared with the painter: the emit walk uses these to decide whether a
 * command exists at all, and if the two resolved differently a widget would
 * be listed with one value and drawn with another.
 *
 * An `over` variant counts only when it is really declared — `undefined`,
 * `null` and -1 all mean "this widget does not react", and -1 is how the cache
 * spells an absent sprite or model.
 */
export function variantOf(props, hovered, base, over = `${base}Over`) {
    if( hovered )
    {
        const value = props[over];
        if( value !== undefined && value !== null && value !== -1 && value !== '' )
            return value;
    }
    return props[base];
}

/** The text a widget shows, as a string; '' means it draws nothing. */
export function effectiveText(props, hovered) {
    const text = variantOf(props, hovered, 'text');
    return text === undefined || text === null ? '' : String(text);
}

/** The model a widget shows, or -1 for "none". */
export function effectiveModel(props, hovered) {
    const model = variantOf(props, hovered, 'model');
    return model === undefined || model === null ? -1 : model | 0;
}

/** The sprite a widget shows, or -1 for "none". */
export function effectiveSprite(props, hovered) {
    const sprite = variantOf(props, hovered, 'sprite');
    return sprite === undefined || sprite === null ? -1 : sprite | 0;
}

/**
 * The prune rules, shared by the emit walk and every hit test.
 *
 * They MUST be one function. A hover test that pruned only hidden layers, and
 * an emit walk that pruned all hidden nodes, is a real bug this client had:
 * hidden spell icons kept their mouse-repeat hooks live and their tooltips
 * pierced through to the spellbook underneath.
 */
export function shouldPrune(node, hoveredComponentId = -1) {
    if( !node ) return true;
    if( node.props.frameHidden ) return true;
    /* A hide-gated node stays invisible unless its own id is the hovered one —
     * the reveal-on-hover form the cache uses for tooltips. */
    if( node.hidden && !(node.props.hoverReveal
        && node.componentId >= 0 && node.componentId === hoveredComponentId) )
        return true;
    return false;
}

/**
 * Does this node establish a SURFACE for everything beneath it?
 *
 * Only a scroll viewport: a layer whose content is bigger than its box on
 * either axis. Everything else clips its own children and leaves the surface
 * where it was — see the walk for why that distinction is the whole rule.
 */
export function establishesSurface(node, box) {
    if( node.type !== TYPE_LAYER ) return false;
    return (node.props.scrollWidth | 0) > box.width
        || (node.props.scrollHeight | 0) > box.height;
}

/** Types that clip their children to their own box. */
export function clipsChildren(node) {
    return node.type === TYPE_LAYER;
}

/** `enum UITreeClientCode`'s content slots: world 1337, minimap 1338,
 *  compass 1339. Each is a builtin draw this preview does not host. */
const CONTENT_CLIENT_CODES = new Set([1337, 1338, 1339]);

function emitKindOf(node) {
    switch( node.type )
    {
    case TYPE_RECTANGLE: return EMIT_KIND.RECT;
    case TYPE_TEXT: return EMIT_KIND.TEXT;
    case TYPE_GRAPHIC: return EMIT_KIND.SPRITE;
    case TYPE_MODEL: return EMIT_KIND.MODEL;
    case TYPE_LINE: return EMIT_KIND.LINE;
    /*
     * A layer draws nothing of its own; it exists to clip and to group. An
     * OBJ box and an ARC draw nothing HERE for a different reason: the C
     * client emits them as its own `CC_OBJ` and `ARC` kinds, which this
     * preview does not host, and the parity oracle filters both out of the
     * reference for exactly that reason.
     */
    default: return null;
    }
}

/**
 * Intersect two rectangles.
 *
 * A clip rect can exceed the canvas — the scissor is authored, not derived —
 * and an unclamped overflow wraps rows in the painter. Clamping here means the
 * painter never sees one.
 */
export function intersect(a, b) {
    const x = Math.max(a.x, b.x);
    const y = Math.max(a.y, b.y);
    const right = Math.min(a.x + a.width, b.x + b.width);
    const bottom = Math.min(a.y + a.height, b.y + b.height);
    return { x, y, width: Math.max(0, right - x), height: Math.max(0, bottom - y) };
}

function clampScroll(value, max) {
    if( max <= 0 ) return 0;
    return Math.max(0, Math.min(max, value));
}
