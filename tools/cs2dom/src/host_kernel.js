/*
 * The HostKernel: everything a CS2 script asks the client for.
 *
 * Generated code calls these methods directly and synchronously, by the name
 * the cache calls the operation (`H.cc_setposition(...)`). There is no request
 * object, no tag, no marshalling — that boundary is the cost the old runtime
 * was paying 22,622 times per tick, and removing it is most of the point.
 *
 * ------------------------------------------------------------------
 * The active component and the dot component
 * ------------------------------------------------------------------
 *
 * The `cc_*` family takes no target: it acts on the ACTIVE component, which
 * `cc_create` and `cc_find` set as a side effect. The `dot_cc_*` family acts
 * on the DOT component, a second slot the `.` prefixed forms use so a script
 * can hold two cursors at once. The `if_*` family names its target outright.
 *
 * Both cursors are stored as incarnation-checked references, not bare indexes.
 * A script that deletes a container and then writes through a cursor it set
 * beforehand must write nowhere — with a bare index it would write to whatever
 * component the free list handed that slot to next.
 *
 * ------------------------------------------------------------------
 * Parking
 * ------------------------------------------------------------------
 *
 * A method that needs something not yet loaded records the need and returns
 * HOST_PARK. It must not have mutated anything by then: the caller will simply
 * call it again after the loader runs, and a half-applied first attempt would
 * be applied twice. The set of methods that can do this is generated from the
 * C client's own yield planner (generated/cs2_host_park.js).
 *
 * ------------------------------------------------------------------
 * Unimplemented is loud
 * ------------------------------------------------------------------
 *
 * 833 distinct operations appear in cache.osrs239 and this kernel implements
 * the ones the tree owns. Everything else throws by name. It does NOT return
 * zero: a silent zero is a widget that draws in the wrong place, an enum that
 * resolves to nothing, a panel that is subtly wrong and blames the layout.
 */

import { HOST_PARK, PARK_CLASS_BY_OPCODE } from './generated/cs2_host_park.js';
import { HOST_SURFACE } from './generated/cs2_host_surface.js';
import { HostConfig, HostPlayerState, installConfigOps } from './host_config.js';
import { installWidgetOps } from './host_widgets.js';
import {
    createDbState, createLootState, createOverlayState, createWorldMapState,
    installDbOps, installIntentOps, installLootOps, installOverlayOps,
    installTextMeasureOps, installWorldMapOps,
} from './host_bridge.js';
import { ClientState, createRandom, installClientOps } from './host_client.js';
import { ChatState, installChatOps } from './host_chat.js';
import { WorldState, installWorldOps } from './host_world.js';
import { DIRTY, DYNAMIC_SUB_ID_BASE, UITree, widgetTypeFromScript } from './uitree.js';

export { HOST_PARK, HostConfig, HostPlayerState, ClientState, ChatState, WorldState };

export class UnimplementedHostOp extends Error {
    constructor(name) {
        super(`host operation '${name}' is not implemented`);
        this.name = 'UnimplementedHostOp';
        this.op = name;
    }
}

/** Values the cache passes for the `setpos`/`setsize` mode arguments. */
export const POSITION_MODE = Object.freeze({ ABS: 0, ABS_CENTRE: 1, ABS_RIGHT: 2 });

export function createHostKernel(options = {}) {
    return new HostKernel(options);
}

export class HostKernel {
    constructor({
        tree = new UITree(), state = null, assets = null, clock = null,
        config = null, player = null, world = null, db = null, fonts = null,
        client = null, chat = null, worldState = null, worldMap = null,
        loot = null, overlays = null, overlayAdapters = null,
        randomSeed = 0x2545f491,
        fakeUnimplemented = false, onUnimplemented = null,
    } = {}) {
        this.tree = tree;
        this.state = state ?? new HostState();
        this.assets = assets ?? new NullAssetSource();
        this.clock = clock ?? new HostClock();
        this.config = config ?? new HostConfig();
        this.player = player ?? new HostPlayerState();

        this.db = db ?? createDbState({});
        this.fonts = fonts;
        /*
         * What the script asked another system to do.
         *
         * A preview has no sound player, no server and no browser navigation,
         * so those operations record an intent and tell the page. Recording is
         * honest; answering as though a server accepted it is not.
         */
        this.intents = [];
        this.client = client ?? new ClientState();
        this.chat = chat ?? new ChatState();
        /*
         * ONE world object. `members`, `world` and `clientType` used to live
         * on a second one assigned a few lines above this, which this line
         * then replaced — so `map_members` read `undefined` and answered no,
         * whatever anyone passed in.
         */
        this.world = worldState ?? new WorldState(world ?? {});
        this.worldMap = worldMap ?? createWorldMapState({});
        this.loot = loot ?? createLootState({});
        this.overlays = overlays ?? createOverlayState({});
        /*
         * How the overlay service finds its SUBJECT. The CS2 side never names
         * one — the acting npc, loc or player is whatever the menu latched —
         * so with no world every create answers -1, which is the reference's
         * own answer when the subject has gone.
         */
        this.overlayAdapters = overlayAdapters ?? {};
        /* See the thrower loop at the foot of this file. */
        this.fakeUnimplemented = fakeUnimplemented;
        this.onUnimplemented = onUnimplemented;
        /** Every operation this kernel faked, by name and count. */
        this.fakedOps = new Map();
        /* Seeded: a preview that renders differently on every reload cannot be
         * compared against a reference, and no script observes the C client's
         * particular sequence. */
        if( !this.client.random ) this.client.random = createRandom(randomSeed);

        /* Incarnation-checked cursors; see the header. */
        this.active = null;
        this.dot = null;

        /**
         * The child-iteration cursor.
         *
         * `cc_children_find_count` / `if_children_find` fill it and
         * `cc_children_findnext*` walk it — state between opcodes, exactly as
         * the C VM keeps `children_iter_*` on the thread. The walkers take no
         * arguments, so without this there is nothing for them to read.
         */
        this.childIter = { parent: -1, subIds: [], index: 0 };
        /** `oc_find`'s result cursor; `oc_findnext` walks it. */
        this.objSearch = { results: [], index: 0 };
        /** The array `if_children_collect` stashed for `children_array`. */
        this.childrenCollected = null;
        /** Components whose resize hook `callonresize` asked for, drained by
         *  the driver: this is reached from inside a running script and there
         *  is no runner to nest a second one on. */
        this.pendingResize = [];
        /** A drag `dragpickup` staged for the input loop to start. */
        this.pendingDragPickup = null;

        /** What the last PARK was waiting for, for the driver to service. */
        this.pending = null;
        /** How many parks have been answered; the driver's progress witness. */
        this.awaitsConsumed = 0;
        /*
         * The load this invocation has already waited for once.
         *
         * The C VM's rule is ONE OPCODE, ONE YIELD, asserted: every operation
         * must load everything it needs in a single round trip and complete on
         * the retry. Without this record a lookup whose record never arrives —
         * an id this cache simply does not have — parks, loads nothing, and
         * parks again forever. Recording the attempt turns the second pass
         * into "complete with the miss answer", which is what the C handlers
         * do via `rs_cs2_await_spent`.
         */
        this._awaited = null;
        /*
         * Event context for the hook currently running.
         *
         * NOT named `event`: that is the accessor scripts call
         * (`H.event('mousex')`), and an instance field of the same name
         * shadows the prototype method — `H.event is not a function`, on the
         * first hook that reads a mouse coordinate.
         */
        this.eventContext = createEventContext();

        /** Counted so a test can prove a quiet tick asks the host nothing. */
        this.calls = 0;
    }

    /* --------------------------------------------------------------
     * Targets
     * ----------------------------------------------------------- */

    /** The node a `cc_*` call acts on, or null once its slot was reclaimed. */
    activeNode() { return this.tree.resolve(this.active); }
    dotNode() { return this.tree.resolve(this.dot); }

    setActive(index) { this.active = this.tree.ref(index); }
    setDot(index) { this.dot = this.tree.ref(index); }

    /**
     * Resolve the target of one call.
     *
     * `componentId` is the `if_*` form's explicit target; `undefined` selects
     * the cursor. A miss answers null and the caller no-ops, which is the C
     * behaviour for a write to a component that is not there.
     */
    _target(componentId, useDot = false) {
        if( componentId !== undefined && componentId !== null )
            return this.tree.findByComponentId(componentId);
        return useDot ? this.dotNode() : this.activeNode();
    }

    /* --------------------------------------------------------------
     * Parking
     * ----------------------------------------------------------- */

    /**
     * Record what is missing and answer PARK.
     *
     * Nothing may have been mutated before this is called. The caller retries
     * the whole operation once the driver has serviced `pending`, so a partial
     * first attempt would be applied twice.
     */
    _park(kind, id, extra = null) {
        this.pending = { kind, id, extra };
        this._awaited = `${kind}:${id}:${extra}`;
        return HOST_PARK;
    }

    /**
     * Has this exact load already been waited for and not produced the record?
     *
     * True means "you asked, it did not arrive, answer the miss" — never park
     * on the same thing twice.
     */
    _awaitSpent(kind, id, extra = null) {
        if( this._awaited !== `${kind}:${id}:${extra}` ) return false;
        /*
         * Counted, because the driver needs to tell a SPIN from a LOOP.
         *
         * A script that walks an enum key by key parks on the same enum on
         * every iteration, and that is progress; a host and a loader that
         * disagree about what "loaded" means park on the same thing having
         * consumed nothing, and that is a hang. The two are indistinguishable
         * from the key alone — this counter is what separates them.
         */
        this.awaitsConsumed++;
        /*
         * Consume it. The C host clears the record on any non-yield return,
         * which makes it mean "the park immediately before this call" — so a
         * script that legitimately asks for the same absent record again, much
         * later, gets its one load attempt again rather than being told
         * forever that it already waited.
         */
        this._awaited = null;
        return true;
    }

    /** True once the driver has satisfied whatever the last park wanted. */
    clearPending() { this.pending = null; }

    /** Called at the start of each invocation; the await record is per-call. */
    beginInvocation() { this._awaited = null; }

    /**
     * Resolve the event locals in a hook's argument list.
     *
     * `componentId` is the component the hook is bound to. The two subtle
     * ones:
     *
     *   `event_com` is the component's ADDRESS, not its runtime identity. For
     *   a DYNAMIC child that is the PARENT's packed id, with
     *   `event_comsubid` carrying the index inside it — the same
     *   (container, sub) pair the wire protocol uses, and for the same reason:
     *   a dynamic child's own id is a runtime allocation nothing outside this
     *   process has a name for. The cache's own scripts prove the convention
     *   by feeding the pair straight back into `cc_find`.
     *
     *   `event_opbase` is a STRING, so it arrives as its own literal name
     *   rather than as one of the numeric sentinels, and resolves to the
     *   component's op base.
     */
    resolveHookArgs(args, componentId) {
        if( !args || args.length === 0 ) return args ?? [];
        const node = componentId === undefined || componentId < 0
            ? null : this.tree.findByComponentId(componentId);
        const parent = node && node.dynamic && node.parent >= 0
            ? this.tree.at(node.parent) : null;
        const event = this.eventContext;

        return args.map((value) => {
            if( value === EVENT_OPBASE )
                return node ? (node.props.opBase ?? '') : '';
            switch( value )
            {
            case SCRIPT_ARG.MOUSE_X: return event.mousex | 0;
            case SCRIPT_ARG.MOUSE_Y: return event.mousey | 0;
            case SCRIPT_ARG.WIDGET_ID:
                if( parent ) return parent.componentId;
                return node ? node.componentId : (componentId ?? -1);
            case SCRIPT_ARG.OP_INDEX: return event.opindex | 0;
            case SCRIPT_ARG.WIDGET_CHILD_INDEX:
                return node && node.dynamic ? node.subId : -1;
            case SCRIPT_ARG.DRAG_TARGET_ID: return event.drop | 0;
            case SCRIPT_ARG.DRAG_TARGET_CHILD_INDEX: return event.dropsubid | 0;
            case SCRIPT_ARG.KEY_TYPED: return event.key | 0;
            case SCRIPT_ARG.KEY_PRESSED: return event.keychar | 0;
            case SCRIPT_ARG.OP_SUBINDEX: return event.opsubindex | 0;
            default: return value;
            }
        });
    }

    /* --------------------------------------------------------------
     * Variables
     * ----------------------------------------------------------- */

    varp(id) { this.calls++; return this.state.varp(id); }
    varbit(id) { this.calls++; return this.state.varbit(id); }
    varc(id) { this.calls++; return this.state.varc(id); }
    varcString(id) { this.calls++; return this.state.varcString(id); }
    varClanSetting(id) { this.calls++; return this.state.varClanSetting(id); }
    varClan(id) { this.calls++; return this.state.varClan(id); }

    /*
     * A script's own write is applied optimistically and does NOT enter the
     * changed-var set, matching the reference: a widget hook that writes a var
     * must not re-trigger itself. Only the server's updates arm the pump.
     */
    setVarp(id, value) { this.calls++; this.state.setVarp(id, value, { fromScript: true }); }
    setVarbit(id, value) { this.calls++; this.state.setVarbit(id, value, { fromScript: true }); }
    setVarc(id, value) { this.calls++; this.state.setVarc(id, value); }
    setVarcString(id, value) { this.calls++; this.state.setVarcString(id, value); }
    setVarClanSetting(id, value) { this.calls++; this.state.setVarClanSetting(id, value); }
    setVarClan(id, value) { this.calls++; this.state.setVarClan(id, value); }

    /* --------------------------------------------------------------
     * Component lifetime
     * ----------------------------------------------------------- */

    /**
     * `cc_create(parent, type, subId)`.
     *
     * Parks when the parent's interface group is not baked into the tree yet.
     * That is the C planner's rule and it must be tested against the GROUP
     * ROOT, not the exact component: once the pack is baked, a still-missing
     * child is not-found, and parking on the child instead loops forever.
     */
    cc_create(parentId, type, subId, nested = 0) {
        return this._create(parentId, type, subId, (index) => this.setActive(index));
    }

    /*
     * The dot form selects ONLY the dot. The two cursors are EXCLUSIVE —
     * `CS2VM2_SetTargetComponentId` writes one or the other, never both — and
     * that exclusivity is the whole reason there are two.
     *
     * Script 1982 is the case: it sizes a text row, then
     * `.cc_create(...)` a stripe rectangle behind it and gives the stripe
     * `cc_gety` and `cc_getheight` — the NON-dot getters, which must still be
     * reading the row. Moving the active cursor with the dot made the stripe
     * measure itself, so all 43 came out zero-height at one y.
     */
    dot_cc_create(parentId, type, subId, nested = 0) {
        return this._create(parentId, type, subId, (index) => this.setDot(index));
    }

    _create(parentId, type, subId, select) {
        this.calls++;
        const group = groupOf(parentId);
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);

        const parent = this.tree.findByComponentId(parentId);
        if( !parent ) return undefined;
        /* Replace-in-slot, BEFORE the uid is allocated so the freed slot and
         * uid are immediately reusable and a rebuild script does not grow the
         * tree. See `reclaimDynamicChild`. */
        this.tree.reclaimDynamicChild(parent.index, subId);
        const index = this.tree.push({
            parentIndex: parent.index, type: widgetTypeFromScript(type), subId,
            dynamic: true,
            componentId: this.tree.allocateDynamicComponentId(group),
        });
        select(index);
        return undefined;
    }

    /** `cc_find(parent, subId)` — pushes whether it hit, and moves the cursor. */
    cc_find(parentId, subId) {
        this.calls++;
        const group = groupOf(parentId);
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);

        const parent = this.tree.findByComponentId(parentId);
        if( !parent ) return 0;
        const child = this.tree.findChildBySubId(parent.index, subId);
        if( !child ) return 0;
        this.setActive(child.index);
        return 1;
    }

    dot_cc_find(parentId, subId) {
        this.calls++;
        const group = groupOf(parentId);
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);
        const parent = this.tree.findByComponentId(parentId);
        if( !parent ) return 0;
        const child = this.tree.findChildBySubId(parent.index, subId);
        if( !child ) return 0;
        this.setDot(child.index);
        return 1;
    }

    dot_if_find(componentId) {
        return this._find(componentId, (index) => this.setDot(index));
    }

    if_find(componentId) {
        return this._find(componentId, (index) => this.setActive(index));
    }

    _find(componentId, select) {
        this.calls++;
        const group = groupOf(componentId);
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);
        const node = this.tree.findByComponentId(componentId);
        if( !node ) return 0;
        this.setActive(node.index);
        return 1;
    }

    /*
     * `cc_delete` addresses whatever `cc_find` selected, and a STATIC component
     * is refused: a script that has selected a component it did not create is
     * a script bug, and deleting a cache-built widget leaves a hole nothing
     * rebuilds. The cursor is cleared either way — the reference's `cc_delete`
     * ends the selection whether or not it removed anything.
     */
    cc_delete() {
        this.calls++;
        const node = this.activeNode();
        if( node && node.dynamic ) this.tree.remove(node.index);
        this.active = null;
    }

    dot_cc_delete() {
        this.calls++;
        const node = this.dotNode();
        if( node && node.dynamic ) this.tree.remove(node.index);
        this.dot = null;
    }

    cc_deleteall(componentId) {
        this.calls++;
        const node = this.tree.findByComponentId(componentId);
        if( node ) this.tree.removeChildren(node.index);
    }

    /* --------------------------------------------------------------
     * Geometry
     * ----------------------------------------------------------- */

    cc_setposition(x, y, xMode, yMode) { this._position(this.activeNode(), x, y, xMode, yMode); }
    dot_cc_setposition(x, y, xMode, yMode) { this._position(this.dotNode(), x, y, xMode, yMode); }
    if_setposition(x, y, xMode, yMode, componentId) {
        this._position(this._target(componentId), x, y, xMode, yMode);
    }

    _position(node, x, y, xMode, yMode) {
        this.calls++;
        if( !node ) return;
        const tree = this.tree;
        tree.setProp(node.index, 'x', x | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'y', y | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'xMode', xMode | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'yMode', yMode | 0, DIRTY.GEOMETRY);
    }

    cc_setsize(w, h, wMode, hMode) { this._size(this.activeNode(), w, h, wMode, hMode); }
    dot_cc_setsize(w, h, wMode, hMode) { this._size(this.dotNode(), w, h, wMode, hMode); }
    if_setsize(w, h, wMode, hMode, componentId) {
        this._size(this._target(componentId), w, h, wMode, hMode);
    }

    _size(node, w, h, wMode, hMode) {
        this.calls++;
        if( !node ) return;
        const tree = this.tree;
        tree.setProp(node.index, 'width', w | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'height', h | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'widthMode', wMode | 0, DIRTY.GEOMETRY);
        tree.setProp(node.index, 'heightMode', hMode | 0, DIRTY.GEOMETRY);
    }

    cc_setscrollpos(x, y) { this._scrollPos(this.activeNode(), x, y); }
    dot_cc_setscrollpos(x, y) { this._scrollPos(this.dotNode(), x, y); }
    if_setscrollpos(x, y, componentId) { this._scrollPos(this._target(componentId), x, y); }

    _scrollPos(node, x, y) {
        this.calls++;
        if( !node ) return;
        this.tree.setProp(node.index, 'scrollX', x | 0, DIRTY.GEOMETRY);
        this.tree.setProp(node.index, 'scrollY', y | 0, DIRTY.GEOMETRY);
    }

    if_setscrollsize(w, h, componentId) {
        this.calls++;
        const node = this._target(componentId);
        if( !node ) return;
        this.tree.setProp(node.index, 'scrollWidth', w | 0, DIRTY.GEOMETRY);
        this.tree.setProp(node.index, 'scrollHeight', h | 0, DIRTY.GEOMETRY);
    }

    cc_setscrollsize(w, h) { this._setScrollSize(this.activeNode(), w, h); }
    dot_cc_setscrollsize(w, h) { this._setScrollSize(this.dotNode(), w, h); }

    _setScrollSize(node, w, h) {
        this.calls++;
        if( !node ) return;
        /* GEOMETRY, not paint: a scroll extent is the box the children are
         * laid out against, so a write here must invalidate the layout. */
        this.tree.setProp(node.index, 'scrollWidth', w | 0, DIRTY.GEOMETRY);
        this.tree.setProp(node.index, 'scrollHeight', h | 0, DIRTY.GEOMETRY);
    }

    /*
     * Geometry getters are BARRIERS.
     *
     * A script reads `if_getwidth` immediately after `if_setsize` and must see
     * the resolved value — the dropdown scrollbar sizes its dragger exactly
     * that way. So the layout is resolved on demand here rather than at the
     * end of the frame.
     */
    if_getwidth(componentId) { return this._geometry(this._target(componentId), 'width'); }
    if_getheight(componentId) { return this._geometry(this._target(componentId), 'height'); }
    if_getx(componentId) { return this._geometry(this._target(componentId), 'x'); }
    if_gety(componentId) { return this._geometry(this._target(componentId), 'y'); }
    cc_getwidth() { return this._geometry(this.activeNode(), 'width'); }
    cc_getheight() { return this._geometry(this.activeNode(), 'height'); }

    /**
     * A geometry getter answers the LAID-OUT value, not the authored prop.
     *
     * Reading the prop makes the whole barrier pointless: `cc_setsize(0, 0,
     * ^setsize_minus, ^setsize_minus)` stores zeros, and a script asking
     * `cc_getheight` right after wants the height that mode RESOLVED to, not
     * the zero it wrote. The reference calls `UITree_GetLayoutWidth` /
     * `GetRelativeY` here, and both resolve layout first.
     *
     * x and y are RELATIVE TO THE PARENT — `abs_y - parent.abs_y` — because
     * that is the number a script feeds straight back into `cc_setposition`,
     * which is itself parent-relative. Answering the absolute value makes
     * every "put this beside that" add the parent's origin twice: the kudos
     * list's scrollbar caps landed 55 pixels below the thumb they cap.
     *
     * A node with no resolved layout falls back to its authored value, which
     * is the reference's own `!position.layout_resolved` arm.
     */
    _geometry(node, field) {
        this.calls++;
        /*
         * A MISS answers 0, not -1.
         *
         * Every one of these getters ends `if( idx < 0 ) return 0;`, and the
         * difference is load-bearing rather than cosmetic: `script7809` places
         * the collection log at `if_getx(<a component of the toplevel>)`, and
         * the toplevel is not mounted in a single-interface run. Answering -1
         * put the whole panel one pixel up and left of the reference's.
         */
        if( !node ) return 0;
        if( this.tree.layoutStale ) this.onLayoutNeeded?.();
        const box = node.layout;
        if( !box ) return this.tree.getProp(node.index, field, 0);
        if( field === 'width' || field === 'height' )
        {
            /* `pos->layout_resolved && pos->abs_w > 0 ? abs_w : (width > 0 ?
             * width : 0)` — a resolved box that came out zero or negative
             * reads back as the AUTHORED size, and never below zero. */
            const resolved = box[field] | 0;
            if( resolved > 0 ) return resolved;
            const authored = this.tree.getProp(node.index, field, 0) | 0;
            return authored > 0 ? authored : 0;
        }
        if( field === 'x' || field === 'y' )
        {
            const parent = node.parent >= 0 ? this.tree.at(node.parent) : null;
            const origin = parent && parent.layout ? parent.layout[field] | 0 : 0;
            return (box[field] | 0) - origin;
        }
        return this.tree.getProp(node.index, field, 0);
    }

    /* --------------------------------------------------------------
     * Presentation
     * ----------------------------------------------------------- */

    cc_setcolour(colour) { this._paint(this.activeNode(), 'colour', colour); }
    dot_cc_setcolour(colour) { this._paint(this.dotNode(), 'colour', colour); }
    if_setcolour(colour, componentId) { this._paint(this._target(componentId), 'colour', colour); }

    cc_settext(text) { this._paint(this.activeNode(), 'text', text); }
    dot_cc_settext(text) { this._paint(this.dotNode(), 'text', text); }
    if_settext(text, componentId) { this._paint(this._target(componentId), 'text', text); }

    cc_settrans(trans) { this._paint(this.activeNode(), 'trans', trans); }
    dot_cc_settrans(trans) { this._paint(this.dotNode(), 'trans', trans); }
    if_settrans(trans, componentId) { this._paint(this._target(componentId), 'trans', trans); }

    cc_setfill(filled) { this._paint(this.activeNode(), 'filled', filled); }
    dot_cc_setfill(filled) { this._paint(this.dotNode(), 'filled', filled); }
    if_setfill(filled, componentId) { this._paint(this._target(componentId), 'filled', filled); }

    cc_settextshadow(shadowed) { this._paint(this.activeNode(), 'shadowed', shadowed); }
    dot_cc_settextshadow(s) { this._paint(this.dotNode(), 'shadowed', s); }
    if_settextshadow(s, componentId) { this._paint(this._target(componentId), 'shadowed', s); }

    cc_settextalign(h, v, lineHeight) { this._align(this.activeNode(), h, v, lineHeight); }
    dot_cc_settextalign(h, v, l) { this._align(this.dotNode(), h, v, l); }
    if_settextalign(h, v, l, componentId) { this._align(this._target(componentId), h, v, l); }

    _align(node, h, v, lineHeight) {
        this.calls++;
        if( !node ) return;
        this.tree.setProp(node.index, 'halign', h | 0);
        this.tree.setProp(node.index, 'valign', v | 0);
        this.tree.setProp(node.index, 'lineHeight', lineHeight | 0);
    }

    _paint(node, field, value) {
        this.calls++;
        if( !node ) return;
        this.tree.setProp(node.index, field, value);
    }

    /** Fonts and sprites park until the asset is decoded. */
    cc_settextfont(fontId) { return this._asset(this.activeNode(), 'font', 'font', fontId); }
    dot_cc_settextfont(fontId) { return this._asset(this.dotNode(), 'font', 'font', fontId); }
    if_settextfont(fontId, componentId) {
        return this._asset(this._target(componentId), 'font', 'font', fontId);
    }

    cc_setgraphic(spriteId) { return this._asset(this.activeNode(), 'sprite', 'sprite', spriteId); }
    dot_cc_setgraphic(s) { return this._asset(this.dotNode(), 'sprite', 'sprite', s); }
    if_setgraphic(spriteId, componentId) {
        return this._asset(this._target(componentId), 'sprite', 'sprite', spriteId);
    }

    _asset(node, field, kind, id) {
        this.calls++;
        /*
         * The park test comes FIRST and touches nothing. A negative id is the
         * "clear it" form and needs no load; anything else must be present
         * before the write, because the retry re-runs this whole method.
         *
         * `_awaitSpent` is what makes the retry TERMINATE. Without it an asset
         * this cache does not have parks, loads nothing, and parks again —
         * forever, with no error and no frame. The C planner asserts against
         * exactly that, and the rule is the same here: one attempt, then
         * complete with whatever is true.
         */
        if( (id | 0) >= 0 && !this.assets.has(kind, id) && !this._awaitSpent(kind, id | 0) )
            return this._park(kind, id | 0);
        if( !node ) return undefined;
        this.tree.setProp(node.index, field, id | 0);
        return undefined;
    }

    cc_sethide(hidden) { this._hide(this.activeNode(), hidden); }
    dot_cc_sethide(hidden) { this._hide(this.dotNode(), hidden); }
    if_sethide(hidden, componentId) { this._hide(this._target(componentId), hidden); }

    _hide(node, hidden) {
        this.calls++;
        if( !node ) return;
        this.tree.setHidden(node.index, !!hidden);
    }

    /* --------------------------------------------------------------
     * Operations (right-click menu entries)
     * ----------------------------------------------------------- */

    cc_setop(slot, label) { this._setOp(this.activeNode(), slot, label); }
    dot_cc_setop(slot, label) { this._setOp(this.dotNode(), slot, label); }
    if_setop(slot, label, componentId) { this._setOp(this._target(componentId), slot, label); }

    _setOp(node, slot, label) {
        this.calls++;
        if( !node ) return;
        if( !node.ops ) node.ops = [];
        node.ops[(slot | 0) - 1] = label;
        this.tree.markDirty(node.index, DIRTY.INTERACTION);
    }

    cc_clearops() { this._clearOps(this.activeNode()); }
    dot_cc_clearops() { this._clearOps(this.dotNode()); }
    if_clearops(componentId) { this._clearOps(this._target(componentId)); }

    _clearOps(node) {
        this.calls++;
        if( !node ) return;
        node.ops = null;
        this.tree.markDirty(node.index, DIRTY.INTERACTION);
    }

    /* --------------------------------------------------------------
     * Hooks
     * ----------------------------------------------------------- */

    /**
     * Every `if_seton*` / `cc_seton*` lands here.
     *
     * A binding identical to the one already bound is skipped by the tree —
     * scripts re-arm wholesale on every rebuild and the great majority of
     * those calls restate what is already there.
     */
    _setHook(node, slot, binding) {
        this.calls++;
        if( !node ) return;
        this.tree.setHook(node.index, slot, binding);
    }

    /* --------------------------------------------------------------
     * Clock
     * ----------------------------------------------------------- */

    clientclock() { this.calls++; return this.clock.cycle(); }

    /* --------------------------------------------------------------
     * Event context
     * ----------------------------------------------------------- */

    /** `event_mousex` and friends, substituted where a hook argument had one. */
    event(property) {
        this.calls++;
        if( !(property in this.eventContext) )
            throw new UnimplementedHostOp(`event_${property}`);
        return this.eventContext[property];
    }
}

/*
 * The hook-setter methods.
 *
 * Written out from the generated surface rather than by hand: there are
 * roughly fifty of them, they differ only in which slot they write, and a
 * hand-written list is a list that will be one entry short. The mapping from
 * command name to hook slot is the one place the naming has to be stated.
 */
const HOOK_SLOT_BY_COMMAND = new Map(Object.entries({
    setonclick: 'onClick',
    setonhold: 'onHold',
    setonmouseover: 'onMouseOver',
    setonmouseleave: 'onMouseLeave',
    setonmouserepeat: 'onMouseRepeat',
    setonclickrepeat: 'onClickRepeat',
    setonrelease: 'onRelease',
    setontargetenter: 'onTargetEnter',
    setontargetleave: 'onTargetLeave',
    setondrag: 'onDrag',
    setondragcomplete: 'onDragComplete',
    setonscrollwheel: 'onScrollWheel',
    setonkey: 'onKey',
    setonop: 'onOp',
    setontimer: 'onTimer',
    setonvartransmit: 'onVarTransmit',
    setoninvtransmit: 'onInvTransmit',
    setonmisctransmit: 'onMiscTransmit',
    setonfriendtransmit: 'onFriendTransmit',
    setonchattransmit: 'onChatTransmit',
    setondialogabort: 'onDialogAbort',
    setonresize: 'onResize',
    setonsubchange: 'onSubChange',
    /* The skill channel; `setonmisctransmit` above is run energy and weight. */
    setonstattransmit: 'onStatTransmit',
    /*
     * These two land on the KEY slots, and that is not a mistake being copied
     * — `rs_cs2_host.c`'s `if_hook_slot` resolves IF_SETONITEMONITEM to
     * `on_key_down` and IF_SETONCLANSETTINGS to `on_key_up`. The slots are
     * spare at this revision and the reference reuses them; putting these
     * anywhere else means a hook the reference fires and this runtime does
     * not.
     */
    setonitemonitem: 'onKeyDown',
    setonclansettings: 'onKeyUp',
}));

/*
 * Registrations the reference PARSES AND DISCARDS.
 *
 * `RS_CS2_UNMODELED_EVENT_CASE`: the argument list is decoded exactly and then
 * dropped, because the tree exposes no such event source. Accepting them here
 * the same way is not a stub — refusing would abort a script the reference
 * runs to completion, and storing them would arm a hook nothing can ever fire.
 *
 * `cc_setonmisctransmit` is on this list for a different reason: there is no
 * CC_ misc-transmit request kind at this revision at all, and opcode 1422 is
 * parsed into the discard group. The IF_ form is real and is a slot above.
 */
const DISCARDED_HOOK_COMMANDS = new Set([
    'setonclantransmit', 'setonclanchanneltransmit', 'setonclansettingstransmit',
    'setonstocktransmit', 'setonmappost',
]);

/** The method names one `seton*` command installs: the dot form too, for cc_. */
function row_hook_methods(name, prefix) {
    return prefix === 'cc' ? [name, `dot_${name}`] : [name];
}

for( const [name] of HOST_SURFACE )
{
    const match = /^(cc|if)_(seton[a-z]+)$/.exec(name);
    if( !match ) continue;
    if( DISCARDED_HOOK_COMMANDS.has(match[2])
        || (match[1] === 'cc' && match[2] === 'setonmisctransmit') )
    {
        for( const method of row_hook_methods(name, match[1]) )
            HostKernel.prototype[method] = function () { this.calls++; };
        continue;
    }
    const slot = HOOK_SLOT_BY_COMMAND.get(match[2]);
    if( !slot ) continue;

    if( match[1] === 'cc' )
    {
        HostKernel.prototype[name] = function (binding) {
            this._setHook(this.activeNode(), slot, binding);
        };
        HostKernel.prototype[`dot_${name}`] = function (binding) {
            this._setHook(this.dotNode(), slot, binding);
        };
    }
    else
    {
        HostKernel.prototype[name] = function (binding, componentId) {
            this._setHook(this._target(componentId), slot, binding);
        };
    }
}

/* The rest of the surface, installed from its own modules. Order matters only
 * in that both must precede the thrower loop below, which fills whatever is
 * still absent. */
installWidgetOps(HostKernel);
installConfigOps(HostKernel);
installDbOps(HostKernel);
installWorldMapOps(HostKernel);
installLootOps(HostKernel);
installOverlayOps(HostKernel);
installTextMeasureOps(HostKernel);
installIntentOps(HostKernel);
installClientOps(HostKernel);
installChatOps(HostKernel);
installWorldOps(HostKernel);

/*
 * Everything else is UNIMPLEMENTED, and there are two defensible things to do
 * about it.
 *
 * By default it throws, by name, so an unimplemented operation fails at the
 * call that needs it and says which one. (An absent method is a TypeError
 * naming `undefined`; a bare zero is worse than either, because it draws.)
 *
 * `fakeUnimplemented` switches to what the REFERENCE does. `cs2vm2.c`'s
 * `CS2VM2_Op_StackMetaStub` pops the declared arguments, pushes zeros and
 * empty strings for the declared results, and announces the opcode once on
 * stderr — its own comment calls the outcome "a plausible wrong answer rather
 * than a crash, and that is the harder failure to find", which is exactly why
 * it announces. A comparison against the reference has to reproduce that or it
 * is comparing a full draw list against an aborted script; a page rendering an
 * interface that touches the clan ops has the same argument.
 *
 * The faked call is still recorded, in `host.fakedOps`, and reported through
 * `onUnimplemented` the first time each name is reached. Faking silently is
 * the one option that is not on the table.
 */
for( const [name, row] of HOST_SURFACE )
{
    const kinds = row.resultKinds ?? 'i'.repeat(row.results.length);
    /* Built once per row: a zero for an int result, an empty string for a
     * string one. Returned as an array only where the caller expects several,
     * because the emitter spreads a multi-result call and would otherwise
     * spread a single value into its own argument list. */
    const faked = [...kinds].map((kind) => (kind === 's' ? '' : 0));
    const answer = faked.length === 0 ? undefined : (faked.length === 1 ? faked[0] : faked);

    for( const method of row.dotCapable ? [name, `dot_${name}`] : [name] )
    {
        if( method in HostKernel.prototype ) continue;
        HostKernel.prototype[method] = function () {
            if( !this.fakeUnimplemented ) throw new UnimplementedHostOp(method);
            this.calls++;
            const seen = this.fakedOps.get(method) ?? 0;
            this.fakedOps.set(method, seen + 1);
            if( seen === 0 ) this.onUnimplemented?.(method, row);
            /* A fresh copy: a script that mutates a returned array must not
             * edit the answer every later call gets. */
            return Array.isArray(answer) ? [...answer] : answer;
        };
    }
}

/** How many of the declared surface this kernel actually answers. */
export function hostCoverage() {
    let implemented = 0;
    let total = 0;
    const missing = [];
    for( const [name, row] of HOST_SURFACE )
    {
        for( const method of row.dotCapable ? [name, `dot_${name}`] : [name] )
        {
            total++;
            const fn = HostKernel.prototype[method];
            /* A generated thrower is not an implementation. */
            if( fn && !/UnimplementedHostOp/.test(String(fn)) ) implemented++;
            else missing.push(method);
        }
    }
    return { implemented, total, missing };
}

/** The interface group a component id belongs to, or -1 for "not one". */
function groupOf(componentId) {
    return componentId >= 0 ? (componentId >>> 16) & 0xffff : -1;
}

/*
 * The event locals a hook's ARGUMENT LIST can carry.
 *
 * A cache hook is written `onload=i:703,i:-2147483645,s:Kudos List,i:0`: the
 * script id, then its arguments, and `-2147483645` is not a number the script
 * wants — it is `event_com`, which the client substitutes at dispatch. Passing
 * it through unsubstituted hands `cc_create` a parent id of -2147483645, which
 * resolves to nothing, and the whole stone border of every framed interface
 * silently fails to exist.
 *
 * Values from `cs2vm2.h`; the substitution is `task_cs2_set_int_local`.
 */
export const SCRIPT_ARG = Object.freeze({
    MOUSE_X: -2147483647,
    MOUSE_Y: -2147483646,
    WIDGET_ID: -2147483645,
    OP_INDEX: -2147483644,
    WIDGET_CHILD_INDEX: -2147483643,
    DRAG_TARGET_ID: -2147483642,
    DRAG_TARGET_CHILD_INDEX: -2147483641,
    KEY_TYPED: -2147483640,
    KEY_PRESSED: -2147483639,
    OP_SUBINDEX: -2147483638,
});

/** The one event local that travels as a STRING, by its own literal name. */
export const EVENT_OPBASE = 'event_opbase';

function createEventContext() {
    return {
        opbase: '', mousex: 0, mousey: 0, com: -1, opindex: 1,
        comsubid: -1, drop: -1, dropsubid: -1, key: -1, keychar: -1,
        opsubindex: 0,
    };
}

/* -------------------------------------------------------------------------
 * The state a script reads that is not a component
 * ---------------------------------------------------------------------- */

/**
 * Variables, and the change serials the transmit pump runs off.
 *
 * A serial per var rather than a dirty flag, because the pump's gating rule
 * needs to know whether a particular hook has seen a particular change: a hook
 * on a hidden component is skipped WITHOUT advancing its serial, so it fires
 * exactly once when the component is revealed.
 */
export class HostState {
    constructor({ varps = new Map(), varbits = new Map(), varcs = new Map() } = {}) {
        this.varps = new Map(varps);
        this.varbits = new Map(varbits);
        this.varcs = new Map(varcs);
        this.varcStrings = new Map();
        this.varClanSettings = new Map();
        this.varClans = new Map();
        this.serial = 0;
        /** var id -> the serial at which it last changed from the server. */
        this.changed = new Map();
    }

    varp(id) { return this.varps.get(id) ?? 0; }
    varbit(id) { return this.varbits.get(id) ?? 0; }
    varc(id) { return this.varcs.get(id) ?? -1; }
    varcString(id) { return this.varcStrings.get(id) ?? ''; }
    varClanSetting(id) { return this.varClanSettings.get(id) ?? 0; }
    varClan(id) { return this.varClans.get(id) ?? 0; }

    setVarp(id, value, { fromScript = false } = {}) {
        this.varps.set(id, value | 0);
        if( !fromScript ) this._change(id);
    }

    setVarbit(id, value, { fromScript = false } = {}) {
        this.varbits.set(id, value | 0);
        if( !fromScript ) this._change(id);
    }

    setVarc(id, value) { this.varcs.set(id, value | 0); }
    setVarcString(id, value) { this.varcStrings.set(id, String(value ?? '')); }
    setVarClanSetting(id, value) { this.varClanSettings.set(id, value | 0); }
    setVarClan(id, value) { this.varClans.set(id, value | 0); }

    _change(id) {
        this.serial++;
        this.changed.set(id, this.serial);
    }

    /** The serial at which `id` last changed, or 0 if it never has. */
    changedAt(id) { return this.changed.get(id) ?? 0; }
}

/** An asset source that has nothing, so every asset op parks once. */
export class NullAssetSource {
    has() { return false; }
}

/**
 * An asset source backed by the stores the painter draws from.
 *
 * These MUST be the same stores. A kernel asking one thing whether an asset is
 * loaded, while the loader satisfies another, is an infinite park: the loader
 * reports success, the kernel re-checks its own empty source, and the retry
 * loop spins with no error and no frame. That is not hypothetical — it is what
 * happened the first time the session wired `assets` and `sprites` separately.
 */
export class StoreAssetSource {
    constructor({ sprites = null, fonts = null, models = null, config = null } = {}) {
        this.sprites = sprites;
        this.fonts = fonts;
        this.models = models;
        this.config = config;
    }

    has(kind, id) {
        switch( kind )
        {
        case 'sprite': return !!this.sprites?.has(id);
        case 'font': return !!this.fonts?.has(id);
        /* A model park is about the RECORD existing, not about a pose having
         * been rasterised — the pose is read at paint time. */
        case 'model': return !!this.models?.hasRecord?.(id);
        case 'obj': return !!this.config?.has('objects', id);
        default: return false;
        }
    }
}

/** An asset source that has everything, for tests that are not about loading. */
export class ReadyAssetSource {
    has() { return true; }
}

/** The 50 Hz cycle counter `clientclock` reports. */
export class HostClock {
    constructor(start = 0) { this.cycles = start; }
    cycle() { return this.cycles; }
    advance(cycles = 1) { this.cycles += cycles; }
}
