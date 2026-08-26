/*
 * The rest of the widget surface: getters, the remaining setters, and the
 * runtime param table.
 *
 * Split from host_kernel.js so that file stays about identity, targeting and
 * parking — the three things the rest of the kernel depends on. Everything
 * here is one more `rs_cs2_host.c` handler, and the ones with a comment are
 * the ones where the obvious implementation is wrong.
 */

import { DIRTY, WIDGET_TYPE, widgetTypeFromScript } from './uitree.js';
import { HOST_PARK } from './generated/cs2_host_park.js';

export function installWidgetOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* --------------------------------------------------------------
     * Identity
     * ----------------------------------------------------------- */

    proto.cc_getid = function () { return idOf(this, this.activeNode()); };
    proto.dot_cc_getid = function () { return idOf(this, this.dotNode()); };
    proto.if_getid = function (componentId) { return idOf(this, this._target(componentId)); };

    function idOf(host, node) {
        host.calls++;
        if( !node ) return -1;
        /*
         * The SUB-ID of a dynamic child, and -1 for anything else:
         *
         *     return CS2VM2_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);
         *
         * A dynamic child does have a uid — the tree allocates one — but that
         * is storage, not the handle a script passes around: every reader of
         * `cc_getid` feeds it straight back to `cc_find(parent, id)`. Returning
         * the uid made every such round trip miss. `steelborder` hands its
         * title's id back to its caller, so `clan_info_draw` could never find
         * the title it was meant to write, in eleven interfaces.
         */
        return node.dynamic ? node.subId : -1;
    }

    /**
     * `if_getlayer(component)` — the component's DECLARED layer.
     *
     * It stops at the component's own interface. The cache stores `layer` per
     * component and a pack's root carries none, so the reference answers -1
     * there; our tree bakes a mounted pack under its owner, so a raw parent
     * walk goes straight out of the group.
     *
     * `~script5774` is what makes this load-bearing: it is the generic
     * dropdown's "where is this button in the dropdown's coordinates"
     * recursion, and without the boundary it summed the gameframe's offsets
     * too and put the music list at x=1158 on an 807-pixel canvas — built
     * correctly, mounted correctly, entirely off-screen.
     */
    proto.if_getlayer = function (componentId) {
        this.calls++;
        const node = this._target(componentId);
        if( !node || node.parent < 0 ) return -1;
        const parent = this.tree.at(node.parent);
        if( !parent || parent.componentId < 0 ) return -1;
        if( (parent.componentId >>> 16) !== (node.componentId >>> 16) ) return -1;
        return parent.componentId;
    };

    proto.cc_getlayer = function () {
        const node = this.activeNode();
        return node ? this.if_getlayer(node.componentId) : -1;
    };

    proto.cc_parentid = function () {
        this.calls++;
        const node = this.activeNode();
        if( !node || node.parent < 0 ) return -1;
        const parent = this.tree.at(node.parent);
        return parent ? parent.componentId : -1;
    };

    /** Is this interface group mounted at all? */
    proto.if_hassub = function (groupId) {
        this.calls++;
        return this.tree.hasGroup(groupId) ? 1 : 0;
    };

    /* --------------------------------------------------------------
     * Geometry and visibility getters
     * ----------------------------------------------------------- */

    proto.cc_getx = function () { return this._geometry(this.activeNode(), 'x'); };
    proto.cc_gety = function () { return this._geometry(this.activeNode(), 'y'); };
    proto.dot_cc_getx = function () { return this._geometry(this.dotNode(), 'x'); };
    proto.dot_cc_gety = function () { return this._geometry(this.dotNode(), 'y'); };
    proto.dot_cc_getwidth = function () { return this._geometry(this.dotNode(), 'width'); };
    proto.dot_cc_getheight = function () { return this._geometry(this.dotNode(), 'height'); };

    proto.if_getscrollx = function (id) { return this._geometry(this._target(id), 'scrollX'); };
    proto.if_getscrolly = function (id) { return this._geometry(this._target(id), 'scrollY'); };
    proto.cc_getscrollx = function () { return this._geometry(this.activeNode(), 'scrollX'); };
    proto.cc_getscrolly = function () { return this._geometry(this.activeNode(), 'scrollY'); };

    proto.if_gethide = function (componentId) {
        this.calls++;
        const node = this._target(componentId);
        return node && node.hidden ? 1 : 0;
    };
    proto.cc_gethide = function () {
        this.calls++;
        const node = this.activeNode();
        return node && node.hidden ? 1 : 0;
    };

    proto.if_getcolour = function (id) { return this._read(this._target(id), 'colour', 0); };
    proto.cc_getcolour = function () { return this._read(this.activeNode(), 'colour', 0); };
    proto.if_gettext = function (id) { return this._read(this._target(id), 'text', ''); };
    proto.cc_gettext = function () { return this._read(this.activeNode(), 'text', ''); };
    proto.if_gettrans = function (id) { return this._read(this._target(id), 'trans', 0); };
    proto.cc_gettrans = function () { return this._read(this.activeNode(), 'trans', 0); };
    proto.if_getgraphic = function (id) { return this._read(this._target(id), 'sprite', -1); };
    proto.cc_getgraphic = function () { return this._read(this.activeNode(), 'sprite', -1); };

    /*
     * The remaining `dot_` twins.
     *
     * A `.` prefix on a command targets the DOT cursor instead of the active
     * one, and the two cursors are what let a script build a row and a
     * sibling at the same time — `cc_create` sets both, `.cc_create` moves
     * only the dot, and the whole chatbox row builder alternates between
     * them. A missing twin does not throw at the call site the script cares
     * about; it throws several statements later, if at all.
     */
    proto.dot_cc_getscrollx = function () { return this._geometry(this.dotNode(), 'scrollX'); };
    proto.dot_cc_getscrolly = function () { return this._geometry(this.dotNode(), 'scrollY'); };
    proto.dot_cc_getscrollwidth = function () {
        return this._read(this.dotNode(), 'scrollWidth', 0);
    };
    proto.dot_cc_getscrollheight = function () {
        return this._read(this.dotNode(), 'scrollHeight', 0);
    };
    proto.dot_cc_getcolour = function () { return this._read(this.dotNode(), 'colour', 0); };
    proto.dot_cc_gettext = function () { return this._read(this.dotNode(), 'text', ''); };
    proto.dot_cc_gettrans = function () { return this._read(this.dotNode(), 'trans', 0); };
    proto.dot_cc_gethide = function () {
        this.calls++;
        const node = this.dotNode();
        return node && node.hidden ? 1 : 0;
    };
    proto.dot_cc_getlayer = function () {
        const node = this.dotNode();
        return node ? this.if_getlayer(node.componentId) : -1;
    };

    proto.dot_cc_setcomponentparam = function (paramId, kind, value) {
        this.calls++;
        const node = this.dotNode();
        if( !node ) return;
        if( !node.params ) node.params = new Map();
        node.params.set(paramId | 0, value);
        this.tree.markDirty(node.index, DIRTY.INTERACTION);
    };

    proto.dot_cc_getcomponentparam = function (paramId) {
        this.calls++;
        const node = this.dotNode();
        const stored = node && node.params ? node.params.get(paramId | 0) : undefined;
        if( stored !== undefined ) return typeof stored === 'string' ? 0 : stored | 0;
        const param = this.config.get('params', paramId);
        if( !param )
        {
            if( paramId >= 0 && !this._awaitSpent('struct', -1, paramId) )
                return this._park('struct', -1, paramId);
            return 0;
        }
        return param.string ? 0 : (param.defaultInt ?? 0);
    };

    /* The two cursors are EXCLUSIVE; see `_create` in host_kernel.js. */
    proto.dot_cc_copy = function (parentId, srcSubId, dstSubId) {
        return this._copy(parentId, srcSubId, dstSubId, (index) => this.setDot(index));
    };

    proto._read = function (node, field, fallback) {
        this.calls++;
        if( !node ) return fallback;
        return this.tree.getProp(node.index, field, fallback);
    };

    /* --------------------------------------------------------------
     * Remaining presentation setters
     * ----------------------------------------------------------- */

    /*
     * Scalar writes with nothing to say beyond which field they set. Declared
     * as a table because writing fifty near-identical methods by hand is how
     * one of them ends up setting the wrong field, and that mistake draws.
     */
    const SCALAR_SETTERS = {
        cc_setoutline: 'outline',
        cc_setgraphicshadow: 'graphicShadow',
        cc_settiling: 'tiled',
        cc_setopbase: 'opBase',
        cc_setvflip: 'flipV',
        cc_sethflip: 'flipH',
        cc_set2dangle: 'spriteAngle',
        cc_setfillcolour: 'fillColour',
        cc_settransbot: 'transBot',
        cc_setlinewid: 'lineWidth',
        cc_setlinedirection: 'lineDirection',
        cc_setfillmode: 'fillMode',
        cc_setnoclickthrough: 'noClickThrough',
        cc_setnoscrollthrough: 'noScrollThrough',
        cc_setdragdeadzone: 'dragDeadZone',
        cc_setdragdeadtime: 'dragDeadTime',
        cc_setdraggablebehavior: 'dragBehaviour',
        cc_setmodeltransparent: 'modelTransparent',
        cc_setmodelorthog: 'modelOrthog',
        cc_setclickmask: 'clickMask',
        cc_setopforceleftclick: 'forceLeftClick',
        cc_settargetverb: 'targetVerb',
        cc_settargetpriority: 'targetPriority',
        cc_setpinch: 'pinch',
    };

    for( const [ccName, field] of Object.entries(SCALAR_SETTERS) )
    {
        const ifName = `if_${ccName.slice(3)}`;
        proto[ccName] = function (value) { this._paint(this.activeNode(), field, value); };
        proto[`dot_${ccName}`] = function (value) { this._paint(this.dotNode(), field, value); };
        proto[ifName] = function (value, componentId) {
            this._paint(this._target(componentId), field, value);
        };
    }

    /**
     * `cc_setdraggable(uid, childIndex)` — the widget's drag RENDER AREA.
     *
     * Not a boolean. It names the node whose box clamps the dragged copy and
     * supplies its coordinate space, and -1 means the widget drags freely.
     * Setting it also makes the widget draggable — `UITree_ComponentIsDraggable`
     * answers yes on `drag_render_area_uid >= 0` before it looks at the click
     * mask — so the flag the hit test reads is set from the same write rather
     * than from a second one that could disagree.
     */
    function setDragRenderArea(node, uid, childIndex) {
        if( !node ) return;
        node.props.dragRenderAreaUid = uid | 0;
        node.props.dragRenderAreaChild = childIndex === undefined ? -1 : childIndex | 0;
        node.props.draggable = (uid | 0) >= 0;
    }

    proto.cc_setdraggable = function (uid, childIndex) {
        this.calls++;
        setDragRenderArea(this.activeNode(), uid, childIndex);
    };
    proto.dot_cc_setdraggable = function (uid, childIndex) {
        this.calls++;
        setDragRenderArea(this.dotNode(), uid, childIndex);
    };
    proto.if_setdraggable = function (uid, childIndex, componentId) {
        this.calls++;
        setDragRenderArea(this._target(componentId), uid, childIndex);
    };

    /* Models park on the model archive, like sprites and fonts on theirs. */
    proto.cc_setmodel = function (modelId) {
        return this._asset(this.activeNode(), 'model', 'model', modelId);
    };
    proto.dot_cc_setmodel = function (modelId) {
        return this._asset(this.dotNode(), 'model', 'model', modelId);
    };
    proto.if_setmodel = function (modelId, componentId) {
        return this._asset(this._target(componentId), 'model', 'model', modelId);
    };

    proto.cc_setmodelangle = function (offsetX, offsetY, angleX, angleY, angleZ, zoom) {
        this._modelAngle(this.activeNode(), offsetX, offsetY, angleX, angleY, angleZ, zoom);
    };
    proto.dot_cc_setmodelangle = function (ox, oy, ax, ay, az, zoom) {
        this._modelAngle(this.dotNode(), ox, oy, ax, ay, az, zoom);
    };
    proto.if_setmodelangle = function (ox, oy, ax, ay, az, zoom, componentId) {
        this._modelAngle(this._target(componentId), ox, oy, ax, ay, az, zoom);
    };

    proto._modelAngle = function (node, offsetX, offsetY, angleX, angleY, angleZ, zoom) {
        this.calls++;
        if( !node ) return;
        const tree = this.tree;
        tree.setProp(node.index, 'modelOffsetX', offsetX | 0);
        tree.setProp(node.index, 'modelOffsetY', offsetY | 0);
        tree.setProp(node.index, 'modelAngleX', angleX | 0);
        tree.setProp(node.index, 'modelAngleY', angleY | 0);
        tree.setProp(node.index, 'modelAngleZ', angleZ | 0);
        tree.setProp(node.index, 'modelZoom', zoom | 0);
    };

    proto.cc_setmodelanim = function (seqId) { this._paint(this.activeNode(), 'modelAnim', seqId); };
    proto.dot_cc_setmodelanim = function (s) { this._paint(this.dotNode(), 'modelAnim', s); };
    proto.if_setmodelanim = function (s, id) { this._paint(this._target(id), 'modelAnim', s); };

    /*
     * `cc_setobject` and its two siblings: an item icon in a cell.
     *
     * The variants differ only in the count-text rule — 0 stackable-only,
     * 1 always, 2 never — and `count === -1` means "icon only, never a
     * number", which must be kept RAW rather than clamped to 0: the readers
     * clamp at use, and clamping here loses the distinction between "no
     * number" and "the number zero".
     */
    const OBJECT_SETTERS = {
        cc_setobject: 0,
        cc_setobject_always_num: 1,
        cc_setobject_nonum: 2,
    };
    for( const [ccName, numMode] of Object.entries(OBJECT_SETTERS) )
    {
        const ifName = `if_${ccName.slice(3)}`;
        proto[ccName] = function (objId, count) {
            return this._setObject(this.activeNode(), objId, count, numMode);
        };
        proto[`dot_${ccName}`] = function (objId, count) {
            return this._setObject(this.dotNode(), objId, count, numMode);
        };
        proto[ifName] = function (objId, count, componentId) {
            return this._setObject(this._target(componentId), objId, count, numMode);
        };
    }

    proto._setObject = function (node, objId, count, numMode) {
        this.calls++;
        /* obj <= 0 clears the cell and needs no load. The park test precedes
         * every write, because the retry re-runs this whole method. */
        if( (objId | 0) > 0 && !this.assets.has('obj', objId) )
            return this._park('obj', objId | 0);
        if( !node ) return undefined;
        const tree = this.tree;
        tree.setProp(node.index, 'obj', objId | 0);
        tree.setProp(node.index, 'objCount', count | 0);
        tree.setProp(node.index, 'objNumMode', numMode);
        /*
         * An item UN-HIDES its cell.
         *
         * `UITree_ApplyObject` ends its obj_id > 0 arm with
         * `if( c->behavior.hide ) { c->behavior.hide = 0; ... }`, and scripts
         * lean on it: `itemsets_side_draw` hides an empty slot and then calls
         * `cc_setobject(obj_6512, 1)` on the same node in the same breath, so
         * all 28 cells of four side panels are visible in the reference and
         * were invisible here.
         */
        if( (objId | 0) > 0 && node.hidden ) tree.setHidden(node.index, false);
        /*
         * A type-6 cell draws the obj in 3D, not as its 32x32 icon.
         *
         * `IfType.getModel` prefers `objectId` over `modelId`, so the
         * reference builds the objtype's inventory model here and stamps the
         * objtype's own 2D composition over the CC_CREATE defaults —
         * `xan2d`/`yan2d`/`zoom2d`/`yof2d`, because zoom 100 is a camera
         * roughly twenty times too close and a model at its own orientation is
         * not the shape a player recognises.
         *
         * Without it a MODEL node with an object set has no model at all, and
         * the emit walk's "an absent model draws nothing" rule is right to
         * skip it: `membership_benefits_prompt` has thirteen of these and drew
         * an empty box.
         *
         * `xof2d` is deliberately not carried: the widget transform has no X
         * translation, so it would tilt rather than shift. The stack-variant
         * model a big count selects is not modelled either — this is the base
         * inventory model, as `EnsureObjModel` takes no count.
         */
        if( (objId | 0) > 0 && node.type === WIDGET_TYPE.MODEL )
        {
            const objtype = this.config?.get('objects', objId | 0);
            if( objtype && (objtype.model | 0) >= 0 )
            {
                tree.setProp(node.index, 'model', objtype.model | 0);
                tree.setProp(node.index, 'modelAngleX', objtype.xan2d | 0);
                tree.setProp(node.index, 'modelAngleY', objtype.yan2d | 0);
                tree.setProp(node.index, 'modelZoom',
                    (objtype.zoom2d | 0) > 0 ? objtype.zoom2d | 0 : 2000);
                tree.setProp(node.index, 'modelOffsetY', objtype.yof2d | 0);
            }
        }
        return undefined;
    };

    /* --------------------------------------------------------------
     * Text-input widgets
     * ----------------------------------------------------------- */

    /*
     * The reference ACCEPTS these and throws them away — `rs_cs2_host.c` puts
     * the whole family behind `RS_CS2_UNMODELED_INPUT_CASE` with the comment
     * "Input widget fields are not represented by UITree yet".
     *
     * They are stored here instead, in a block of their own on the node. That
     * is not a divergence: nothing reads it yet, so no draw list can differ,
     * and what a script SAID about its input field is the one thing a renderer
     * for it will need. Discarding it would mean building that renderer twice.
     */
    const INPUT_FIELDS = {
        cc_input_setsubmitmode: 'submitMode',
        cc_input_setselectcolour: 'selectColour',
        cc_input_setacceptmode: 'acceptMode',
        cc_input_setwrapmode: 'wrapMode',
        cc_input_setlinewrappingwidth: 'lineWrappingWidth',
        cc_input_setselectbgcolour: 'selectBackground',
        cc_input_setlinecountlimit: 'lineCountLimit',
        cc_input_setcursorcolour: 'cursorColour',
        cc_input_setcursortrans: 'cursorTrans',
        cc_input_setcursorheight: 'cursorHeight',
        cc_input_setcursoroffset: 'cursorOffset',
        cc_input_setlinewidthlimit: 'lineWidthLimit',
        cc_input_setcharfilter: 'charFilter',
    };

    function inputState(host, node) {
        if( !node ) return null;
        if( !node.input ) node.input = {};
        host.tree.markDirty(node.index, DIRTY.PAINT);
        return node.input;
    }

    for( const [ccName, field] of Object.entries(INPUT_FIELDS) )
    {
        const ifName = `if_${ccName.slice(3)}`;
        proto[ccName] = function (value) {
            this.calls++;
            const state = inputState(this, this.activeNode());
            if( state ) state[field] = value;
        };
        proto[`dot_${ccName}`] = function (value) {
            this.calls++;
            const state = inputState(this, this.dotNode());
            if( state ) state[field] = value;
        };
        proto[ifName] = function (value, componentId) {
            this.calls++;
            const state = inputState(this, this._target(componentId));
            if( state ) state[field] = value;
        };
    }

    /*
     * The CC form takes TWO values where every other member of the family
     * takes one; the IF form takes one and its component, like the rest. That
     * asymmetry is the decompiler's table, and it is worth stating because the
     * C VM's request struct carries a single `value` for both — so an
     * implementation copied from the struct would swallow the component id as
     * a weight and write the field on whatever was selected.
     */
    proto.cc_input_setcursorwidth = function (width, weight) {
        this.calls++;
        const state = inputState(this, this.activeNode());
        if( state ) { state.cursorWidth = width | 0; state.cursorWeight = weight | 0; }
    };
    proto.dot_cc_input_setcursorwidth = function (width, weight) {
        this.calls++;
        const state = inputState(this, this.dotNode());
        if( state ) { state.cursorWidth = width | 0; state.cursorWeight = weight | 0; }
    };
    proto.if_input_setcursorwidth = function (width, componentId) {
        this.calls++;
        const state = inputState(this, this._target(componentId));
        if( state ) state.cursorWidth = width | 0;
    };

    /* The input hooks are `clientscript` bindings like any other, so they take
     * a binding and go in a slot of their own rather than into the shared
     * tree hook table — nothing dispatches them yet, and putting them in a
     * real slot would arm a hook nothing can fire. */
    for( const ccName of ['cc_input_setonsubmit', 'cc_input_setonabort',
        'cc_input_setonfocuschanged', 'cc_input_setonupdate'] )
    {
        const slot = ccName.slice('cc_input_seton'.length);
        const ifName = `if_${ccName.slice(3)}`;
        proto[ccName] = function (binding) {
            this.calls++;
            const state = inputState(this, this.activeNode());
            if( state ) state[`on_${slot}`] = binding ?? null;
        };
        proto[`dot_${ccName}`] = function (binding) {
            this.calls++;
            const state = inputState(this, this.dotNode());
            if( state ) state[`on_${slot}`] = binding ?? null;
        };
        proto[ifName] = function (binding, componentId) {
            this.calls++;
            const state = inputState(this, this._target(componentId));
            if( state ) state[`on_${slot}`] = binding ?? null;
        };
    }

    /*
     * A preview has no focused input and no caret in one. -1 and 0 are the
     * "nothing focused" answers rather than placeholders: a script comparing
     * the focus against a component id can tell -1 from a real one.
     */
    proto.cc_input_getfocus = function () { this.calls++; return -1; };
    proto.dot_cc_input_getfocus = proto.cc_input_getfocus;
    proto.cc_input_getcaretposition = function () { this.calls++; return 0; };
    proto.dot_cc_input_getcaretposition = proto.cc_input_getcaretposition;

    /* --------------------------------------------------------------
     * Deferred calls, drags, and mount questions
     * ----------------------------------------------------------- */

    /*
     * `callonresize` is QUEUED, not run. It is reached from inside a running
     * script and there is no runner to nest a second one on; the driver drains
     * the queue on its next settlement pass, which is what the C host does
     * with `rs_cs2_call_on_resize_push`.
     */
    proto.cc_callonresize = function () {
        this.calls++;
        const node = this.activeNode();
        if( node ) this.pendingResize.push(node.componentId);
    };
    proto.dot_cc_callonresize = function () {
        this.calls++;
        const node = this.dotNode();
        if( node ) this.pendingResize.push(node.componentId);
    };
    proto.if_callonresize = function (componentId) {
        this.calls++;
        this.pendingResize.push(componentId | 0);
    };
    proto.dot_if_callonresize = proto.if_callonresize;

    /*
     * `dragpickup` starts a drag the INPUT loop owns — the C host stages it
     * and `InteractFrame` turns it into a live drag source, because the CS2
     * host cannot reach the input state. Staging it the same way here keeps
     * the one place that knows about pointers in charge of drags.
     */
    proto.cc_dragpickup = function (x, y) {
        this.calls++;
        const node = this.activeNode();
        if( node ) this.pendingDragPickup = { componentId: node.componentId, x: x | 0, y: y | 0 };
    };
    proto.dot_cc_dragpickup = function (x, y) {
        this.calls++;
        const node = this.dotNode();
        if( node ) this.pendingDragPickup = { componentId: node.componentId, x: x | 0, y: y | 0 };
    };
    /* `if_dragpickup(x, y, component)` — the component is LAST, as it is on
     * every `if_` form: `CS2VM2_Op_DragPickup` pops component, then y, then x,
     * so push order puts it at the end. */
    proto.if_dragpickup = function (x, y, componentId) {
        this.calls++;
        this.pendingDragPickup = { componentId: componentId | 0, x: x | 0, y: y | 0 };
    };

    /** `if_haschild_overlay(component, group)` — is THAT group mounted here? */
    proto.if_haschild_overlay = function (componentId, groupId) {
        this.calls++;
        return this.client.mounts.get(componentId)?.group === (groupId | 0) ? 1 : 0;
    };

    /**
     * `if_children_collect(unused, uid, startIndex)` — fill the cursor AND
     * stash an int array of the sub-ids for `children_array` to hand back.
     *
     * The leading argument is unused; the reference pops it and drops it. The
     * COUNT is what this pushes, not the array — `children_array` is a
     * separate opcode, and a script that calls it without a collect first must
     * still get a usable empty array rather than a missing handle.
     */
    proto.if_children_collect = function (unused, componentId, startIndex) {
        this.calls++;
        const group = componentId >= 0 ? (componentId >>> 16) & 0xffff : -1;
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);
        const count = this.cc_children_find_count_for(componentId, startIndex);
        this.childrenCollected = [...this.childIter.subIds];
        return count;
    };

    proto.children_array = function () {
        this.calls++;
        /* A length-0 array, never a missing handle: `array_length` and indexing
         * must stay safe for a script that never collected. */
        return this.childrenCollected ?? [];
    };

    /* --------------------------------------------------------------
     * Menu options: keys, rates, submenus
     * ----------------------------------------------------------- */

    /*
     * `op_index` is ONE-BASED throughout this block, the same as `cc_setop`
     * and `cc_getop`. The typed (`opt`) forms address the TYPED slot instead
     * of an option — that is the whole difference between the two halves of
     * every pair here, and it is why they cannot share a signature.
     */
    const OPKEY_TYPED_SLOT = 0;

    function opKeyState(host, node) {
        if( !node ) return null;
        if( !node.opKeys ) node.opKeys = new Map();
        host.tree.markDirty(node.index, DIRTY.INTERACTION);
        return node.opKeys;
    }

    /**
     * `cc_setopkey(op, char0, code0, ... char4, code4)` — up to five bindings.
     *
     * The pairs STOP at the first negative character: the bytecode always
     * pushes five and the script fills the ones it wants, so counting them by
     * length would arm four dead bindings on every call.
     */
    function setOpKey(host, node, opIndex, pairs) {
        const state = opKeyState(host, node);
        if( !state ) return;
        const keys = [];
        for( let i = 0; i + 1 < pairs.length; i += 2 )
        {
            if( (pairs[i] | 0) < 0 ) break;
            keys.push({ char: pairs[i] | 0, code: pairs[i + 1] | 0 });
        }
        const entry = state.get(opIndex | 0) ?? {};
        state.set(opIndex | 0, { ...entry, keys });
    }

    proto.cc_setopkey = function (opIndex, ...pairs) {
        this.calls++;
        setOpKey(this, this.activeNode(), opIndex, pairs);
    };
    proto.dot_cc_setopkey = function (opIndex, ...pairs) {
        this.calls++;
        setOpKey(this, this.dotNode(), opIndex, pairs);
    };
    proto.if_setopkey = function (opIndex, ...rest) {
        this.calls++;
        /* The component is LAST in call order, after the five pairs. */
        const componentId = rest[rest.length - 1];
        setOpKey(this, this._target(componentId), opIndex, rest.slice(0, -1));
    };

    proto.cc_setoptkey = function (keyChar, keyCode) {
        this.calls++;
        setOpKey(this, this.activeNode(), OPKEY_TYPED_SLOT, [keyChar, keyCode]);
    };
    proto.dot_cc_setoptkey = function (keyChar, keyCode) {
        this.calls++;
        setOpKey(this, this.dotNode(), OPKEY_TYPED_SLOT, [keyChar, keyCode]);
    };
    proto.if_setoptkey = function (keyChar, keyCode, componentId) {
        this.calls++;
        setOpKey(this, this._target(componentId), OPKEY_TYPED_SLOT, [keyChar, keyCode]);
    };

    function setOpKeyRate(host, node, opIndex, rate, enabled) {
        const state = opKeyState(host, node);
        if( !state ) return;
        const entry = state.get(opIndex | 0) ?? {};
        state.set(opIndex | 0, { ...entry, rate: rate | 0, repeat: !!enabled });
    }

    proto.cc_setopkeyrate = function (opIndex, rate, enabled) {
        this.calls++;
        setOpKeyRate(this, this.activeNode(), opIndex, rate, enabled);
    };
    proto.dot_cc_setopkeyrate = function (opIndex, rate, enabled) {
        this.calls++;
        setOpKeyRate(this, this.dotNode(), opIndex, rate, enabled);
    };
    proto.if_setopkeyrate = function (opIndex, rate, enabled, componentId) {
        this.calls++;
        setOpKeyRate(this, this._target(componentId), opIndex, rate, enabled);
    };

    proto.cc_setoptkeyrate = function (rate, enabled) {
        this.calls++;
        setOpKeyRate(this, this.activeNode(), OPKEY_TYPED_SLOT, rate, enabled);
    };
    proto.dot_cc_setoptkeyrate = function (rate, enabled) {
        this.calls++;
        setOpKeyRate(this, this.dotNode(), OPKEY_TYPED_SLOT, rate, enabled);
    };
    proto.if_setoptkeyrate = function (rate, enabled, componentId) {
        this.calls++;
        setOpKeyRate(this, this._target(componentId), OPKEY_TYPED_SLOT, rate, enabled);
    };

    /* `ignoreheld` and `rate` are exclusive in the C handler: it applies one or
     * the other, never both, so setting the flag clears the rate. */
    function setOpKeyIgnoreHeld(host, node, opIndex) {
        const state = opKeyState(host, node);
        if( !state ) return;
        const entry = state.get(opIndex | 0) ?? {};
        state.set(opIndex | 0, { ...entry, ignoreHeld: true, rate: 0, repeat: false });
    }

    proto.cc_setopkeyignoreheld = function (opIndex) {
        this.calls++;
        setOpKeyIgnoreHeld(this, this.activeNode(), opIndex);
    };
    proto.dot_cc_setopkeyignoreheld = function (opIndex) {
        this.calls++;
        setOpKeyIgnoreHeld(this, this.dotNode(), opIndex);
    };
    proto.if_setopkeyignoreheld = function (opIndex, componentId) {
        this.calls++;
        setOpKeyIgnoreHeld(this, this._target(componentId), opIndex);
    };
    proto.cc_setoptkeyignoreheld = function () {
        this.calls++;
        setOpKeyIgnoreHeld(this, this.activeNode(), OPKEY_TYPED_SLOT);
    };
    proto.dot_cc_setoptkeyignoreheld = function () {
        this.calls++;
        setOpKeyIgnoreHeld(this, this.dotNode(), OPKEY_TYPED_SLOT);
    };
    proto.if_setoptkeyignoreheld = function (componentId) {
        this.calls++;
        setOpKeyIgnoreHeld(this, this._target(componentId), OPKEY_TYPED_SLOT);
    };

    /* A submenu hangs off one option: `op -> [sub0, sub1, ...]`. */
    function opSubmenu(host, node) {
        if( !node ) return null;
        if( !node.opSubmenus ) node.opSubmenus = new Map();
        host.tree.markDirty(node.index, DIRTY.INTERACTION);
        return node.opSubmenus;
    }

    proto.cc_setopsubmenu = function (opIndex, subIndex, text) {
        this.calls++;
        const state = opSubmenu(this, this.activeNode());
        if( !state ) return;
        const rows = state.get(opIndex | 0) ?? [];
        rows[subIndex | 0] = String(text ?? '');
        state.set(opIndex | 0, rows);
    };
    proto.dot_cc_setopsubmenu = function (opIndex, subIndex, text) {
        this.calls++;
        const state = opSubmenu(this, this.dotNode());
        if( !state ) return;
        const rows = state.get(opIndex | 0) ?? [];
        rows[subIndex | 0] = String(text ?? '');
        state.set(opIndex | 0, rows);
    };
    proto.if_setopsubmenu = function (opIndex, subIndex, text, componentId) {
        this.calls++;
        const state = opSubmenu(this, this._target(componentId));
        if( !state ) return;
        const rows = state.get(opIndex | 0) ?? [];
        rows[subIndex | 0] = String(text ?? '');
        state.set(opIndex | 0, rows);
    };

    proto.cc_clearopsubmenu = function (opIndex) {
        this.calls++;
        opSubmenu(this, this.activeNode())?.delete(opIndex | 0);
    };
    proto.dot_cc_clearopsubmenu = function (opIndex) {
        this.calls++;
        opSubmenu(this, this.dotNode())?.delete(opIndex | 0);
    };

    /* --------------------------------------------------------------
     * Arcs, sprite variants, and the odd ones out
     * ----------------------------------------------------------- */

    proto.cc_setarc = function (start, end) {
        this.calls++;
        const node = this.activeNode();
        if( !node ) return;
        this.tree.setProp(node.index, 'arcStart', start | 0);
        this.tree.setProp(node.index, 'arcEnd', end | 0);
    };
    proto.dot_cc_setarc = function (start, end) {
        this.calls++;
        const node = this.dotNode();
        if( !node ) return;
        this.tree.setProp(node.index, 'arcStart', start | 0);
        this.tree.setProp(node.index, 'arcEnd', end | 0);
    };
    proto.if_setarc = function (start, end, componentId) {
        this.calls++;
        const node = this._target(componentId);
        if( !node ) return;
        this.tree.setProp(node.index, 'arcStart', start | 0);
        this.tree.setProp(node.index, 'arcEnd', end | 0);
    };

    /* The hover sprite, which the painter reaches through `spriteOver`. It
     * parks on the sprite archive like `cc_setgraphic` does: a variant that
     * is not decoded when the hover happens would pop in a frame late. */
    proto.cc_setgraphic2 = function (spriteId) {
        return this._asset(this.activeNode(), 'sprite', 'spriteOver', spriteId);
    };
    proto.dot_cc_setgraphic2 = function (spriteId) {
        return this._asset(this.dotNode(), 'sprite', 'spriteOver', spriteId);
    };
    proto.if_setgraphic2 = function (spriteId, componentId) {
        return this._asset(this._target(componentId), 'sprite', 'spriteOver', spriteId);
    };

    /*
     * `cc_sethttpsprite` fetches an image over the network in the real client
     * (news banners, the launcher's promo art). A preview has no such fetch
     * and inventing a placeholder would put a box where a picture goes, so the
     * URL is RECORDED and the page decides.
     */
    proto.cc_sethttpsprite = function (url) {
        this.calls++;
        this.intents.push({ intent: 'httpSprite', url: String(url ?? '') });
        this.onIntent?.('httpSprite', { url: String(url ?? '') });
    };
    proto.dot_cc_sethttpsprite = proto.cc_sethttpsprite;

    /* --------------------------------------------------------------
     * Model sources that are not a plain model id
     * ----------------------------------------------------------- */

    /*
     * An npc head, a player's own head or body, a chathead, a loc's model.
     * Each is a model the reference COMPOSES rather than loads: an npc head
     * from the npc's head models, a player from the appearance the server
     * sent. A preview has neither an appearance nor a composer.
     *
     * So the SOURCE is recorded on the node — kind and id — and the page's
     * model layer resolves whatever it can. That is not the same as writing a
     * model id the composer would have produced, and it is deliberately not:
     * a widget showing the wrong head is harder to notice than one showing
     * nothing, and `modelSource` is a thing a test and a renderer can both
     * read.
     */
    const MODEL_SOURCES = {
        cc_setnpchead: ['npcHead', 1],
        cc_setnpcmodel: ['npc', 1],
        cc_setlocmodel: ['loc', 1],
        cc_setplayerhead_self: ['playerHeadSelf', 0],
        cc_setplayermodel_self: ['playerSelf', 1],
        cc_setmodel_playerchathead: ['playerChatHead', 1],
    };
    for( const [ccName, [kind, arity]] of Object.entries(MODEL_SOURCES) )
    {
        proto[ccName] = function (id) {
            this.calls++;
            const node = this.activeNode();
            if( node ) this.tree.setProp(node.index, 'modelSource',
                arity ? { kind, id: id | 0 } : { kind });
        };
        proto[`dot_${ccName}`] = function (id) {
            this.calls++;
            const node = this.dotNode();
            if( node ) this.tree.setProp(node.index, 'modelSource',
                arity ? { kind, id: id | 0 } : { kind });
        };
        const ifName = `if_${ccName.slice(3)}`;
        proto[ifName] = function (...args) {
            this.calls++;
            const componentId = args[args.length - 1];
            const node = this._target(componentId);
            if( node ) this.tree.setProp(node.index, 'modelSource',
                arity ? { kind, id: args[0] | 0 } : { kind });
        };
    }

    /* --------------------------------------------------------------
     * The rest of the getters
     * ----------------------------------------------------------- */

    /*
     * Every one of these mirrors a setter that already exists, and each
     * answers the reference's own MISS value for a component that is not
     * there. That value is never uniformly zero: `cc_getop` answers the empty
     * string, `cc_getid` answers -1 for a STATIC component (only a dynamic one
     * has a child index), and a colour answers 0 because 0 is black and the
     * reference stores it that way.
     *
     * Declared as a table because writing sixty near-identical methods by hand
     * is how one of them ends up reading the wrong field, and that mistake
     * does not throw — it draws.
     */
    const SCALAR_GETTERS = {
        cc_getmodelzoom: ['modelZoom', 0],
        cc_getmodelangle_x: ['modelAngleX', 0],
        cc_getmodelangle_y: ['modelAngleY', 0],
        cc_getmodelangle_z: ['modelAngleZ', 0],
        /* `trans_bot` in the C, the second stop of a vertical fade. */
        cc_getblendtrans: ['transBot', 0],
        cc_getfillcolour: ['fillColour', 0],
        cc_getmodeltransparent: ['modelTransparent', 0],
        cc_getarcstart: ['arcStart', 0],
        cc_getarcend: ['arcEnd', 0],
        /* What CC_SETOBJECT put on the widget, not what an inventory holds. */
        cc_getinvobject: ['obj', 0],
        cc_getinvcount: ['objCount', 0],
        cc_gettargetmask: ['targetMask', 0],
    };

    for( const [ccName, [field, absent]] of Object.entries(SCALAR_GETTERS) )
    {
        const ifName = `if_${ccName.slice(3)}`;
        proto[ccName] = function () { return this._read(this.activeNode(), field, absent); };
        proto[`dot_${ccName}`] = function () { return this._read(this.dotNode(), field, absent); };
        proto[ifName] = function (componentId) {
            return this._read(this._target(componentId), field, absent);
        };
    }

    /**
     * `cc_getop(index)` — the menu option text, ONE-BASED.
     *
     * Index 1 is `ops[0]`. Answering `ops[index]` instead shifts every option
     * by one, which reads as the right-click menu naming the action below the
     * one it runs.
     */
    function opText(node, oneBasedIndex) {
        const index = (oneBasedIndex | 0) - 1;
        if( !node || !node.ops || index < 0 || index >= node.ops.length ) return '';
        return node.ops[index] ?? '';
    }

    proto.cc_getop = function (index) {
        this.calls++;
        return opText(this.activeNode(), index);
    };
    proto.dot_cc_getop = function (index) {
        this.calls++;
        return opText(this.dotNode(), index);
    };
    /*
     * `if_getop(index, component)`, in that order.
     *
     * `CS2VM2_Op_IF_GetOp` pops the component first and the one-based index
     * second, so push order is index-then-component — the same shape as every
     * other `if_` form, whose component is its last argument. Taking them the
     * other way round asked the OP-INDEX-th component for its component-id-th
     * option, which answers "" for every call: `raids_storage_side` and
     * `sailing_boat_cargohold_side` label their dismiss button with one of
     * these and drew a blank.
     */
    proto.if_getop = function (index, componentId) {
        this.calls++;
        return opText(this._target(componentId), index);
    };

    proto.cc_getopbase = function () { return this._read(this.activeNode(), 'opBase', ''); };
    proto.dot_cc_getopbase = function () { return this._read(this.dotNode(), 'opBase', ''); };
    proto.if_getopbase = function (componentId) {
        return this._read(this._target(componentId), 'opBase', '');
    };

    /**
     * `cc_getparam(param)` — a STRUCT param, with struct -1.
     *
     * Not a component param. The opcode pops only the param id and the C
     * lowering hard-codes `struct_id = -1`, so this is `struct_param(-1, p)`:
     * an enum lookup that missed falls through to the ParamType's default
     * rather than being awaited.
     */
    proto.cc_getparam = function (paramId) {
        return this.struct_param(-1, paramId);
    };
    proto.dot_cc_getparam = proto.cc_getparam;

    /* --------------------------------------------------------------
     * The runtime param table
     * ----------------------------------------------------------- */

    /*
     * CC_SETCOMPONENTPARAM / CC_GETCOMPONENTPARAM: a per-component key-value
     * table that exists only at runtime.
     *
     * An OldSchool IF3 file has no param section at all, so the table starts
     * empty and only the setter fills it. The gameframe scripts use it to
     * label the widgets they build — "this row is kind 600, index 4" — and
     * read the labels back after a `cc_find` to decide what a click meant.
     *
     * The GETTER never answers a string. Its opcode arity is int-out, so a
     * string-typed param answers 0 rather than unbalancing the caller's stack.
     */
    proto.cc_setcomponentparam = function (paramId, kind, value) {
        this.calls++;
        const node = this.activeNode();
        if( !node ) return;
        if( !node.params ) node.params = new Map();
        node.params.set(paramId | 0, value);
        this.tree.markDirty(node.index, DIRTY.INTERACTION);
    };

    /**
     * `if_setparam(param, value, uid, childIndex, type)` — the same table,
     * addressed by name rather than by the active cursor.
     *
     * The argument order is the VM's pop order reversed: type last. `type` 2 /
     * 115 / the string kind means the value came off the string stack, and it
     * is stored as it arrived — a param written as a string and read back as
     * an int answers 0, which is the getter's own rule and not a loss here.
     *
     * `childIndex` of -1 means the component itself, which is what every call
     * site passes. A non-(-1) child would name a dynamic child under `uid`;
     * the C lowering leaves it at the parent rather than inventing a lookup,
     * and doing anything else here would disagree with it.
     */
    proto.if_setparam = function (paramId, value, componentId, childIndex, type) {
        this.calls++;
        const node = this._target(componentId);
        if( !node ) return;
        if( !node.params ) node.params = new Map();
        node.params.set(paramId | 0, value);
        this.tree.markDirty(node.index, DIRTY.INTERACTION);
    };

    proto.cc_getcomponentparam = function (paramId) {
        this.calls++;
        const node = this.activeNode();
        const stored = node && node.params ? node.params.get(paramId | 0) : undefined;
        if( stored !== undefined ) return typeof stored === 'string' ? 0 : stored | 0;
        const param = this.config.get('params', paramId);
        if( !param )
        {
            if( paramId >= 0 && !this._awaitSpent('struct', -1, paramId) )
                return this._park('struct', -1, paramId);
            return 0;
        }
        return param.string ? 0 : (param.defaultInt ?? 0);
    };

    /**
     * `if_getcomponentparam(param, component, fallback)`.
     *
     * The THIRD argument is the caller's own answer for a miss, and it is the
     * whole point of the opcode: unlike the `cc_` form this never consults the
     * ParamType's default and so never parks. Dropping it answers 0 where the
     * script asked for -1, and -1 is how half of them spell "absent".
     */
    proto.if_getcomponentparam = function (paramId, componentId, fallback) {
        this.calls++;
        const node = this._target(componentId);
        const stored = node && node.params ? node.params.get(paramId | 0) : undefined;
        if( stored === undefined ) return fallback | 0;
        return typeof stored === 'string' ? (fallback | 0) : stored | 0;
    };

    /* --------------------------------------------------------------
     * The remaining creation forms
     * ----------------------------------------------------------- */

    /*
     * `cc_createchild` differs from `cc_create` only in that the parent is the
     * active component rather than a named one — and the DOT form still reads
     * its parent from the active cursor while selecting only the dot. Both
     * halves matter: the parent comes from one cursor, the selection goes to
     * the other, and collapsing them is what makes a script that builds two
     * things at once build one thing twice.
     */
    proto.dot_cc_createchild = function (type, subId, nested = 0) {
        return this._createChild(type, subId, (index) => this.setDot(index));
    };

    proto.dot_cc_createsibling = function (type, subId, nested = 0) {
        return this._createSibling(type, subId, (index) => this.setDot(index));
    };

    proto.cc_createchild = function (type, subId, nested = 0) {
        return this._createChild(type, subId, (index) => this.setActive(index));
    };

    proto._createChild = function (type, subId, select) {
        this.calls++;
        const parent = this.activeNode();
        if( !parent ) return undefined;
        this.tree.reclaimDynamicChild(parent.index, subId);
        const index = this.tree.push({
            parentIndex: parent.index, type: widgetTypeFromScript(type), subId,
            dynamic: true,
            componentId: this.tree.allocateDynamicComponentId(parent.componentId >>> 16),
        });
        select(index);
        return undefined;
    };

    /* `cc_createsibling` attaches beside the active component instead. */
    proto.cc_createsibling = function (type, subId, nested = 0) {
        return this._createSibling(type, subId, (index) => this.setActive(index));
    };

    proto._createSibling = function (type, subId, select) {
        this.calls++;
        const node = this.activeNode();
        if( !node ) return undefined;
        this.tree.reclaimDynamicChild(node.parent, subId);
        const index = this.tree.push({
            parentIndex: node.parent, type: widgetTypeFromScript(type), subId,
            dynamic: true,
            componentId: this.tree.allocateDynamicComponentId(node.componentId >>> 16),
        });
        select(index);
        return undefined;
    };

    /**
     * `cc_copy(parent, srcSub, dstSub)` — clone a dynamic child.
     *
     * The bank's tab strip builds tab 0 and copies it into slots 1..9, and an
     * unimplemented copy is not a no-op: it leaves its arguments on the stack
     * and every following `cc_setposition` retargets the one widget that was
     * made, collapsing the whole strip onto the last iteration's x.
     */
    proto.cc_copy = function (parentId, srcSubId, dstSubId) {
        return this._copy(parentId, srcSubId, dstSubId, (index) => this.setActive(index));
    };

    proto._copy = function (parentId, srcSubId, dstSubId, select) {
        this.calls++;
        const parent = this.tree.findByComponentId(parentId);
        if( !parent ) return undefined;
        const source = this.tree.findChildBySubId(parent.index, srcSubId);
        if( !source ) return undefined;
        this.tree.reclaimDynamicChild(parent.index, dstSubId);
        const index = this.tree.push({
            parentIndex: parent.index, type: source.type, subId: dstSubId,
            dynamic: true, props: source.props,
            /* Its OWN id: a copy that shared the source's would make every
             * later `if_find` and every hook registration address the tab the
             * strip was cloned from. */
            componentId: this.tree.allocateDynamicComponentId(parentId >>> 16),
        });
        const copy = this.tree.at(index);
        copy.hidden = source.hidden;
        if( source.ops ) copy.ops = [...source.ops];
        if( source.hooks ) copy.hooks = { ...source.hooks };
        select(index);
        return undefined;
    };

    /* --------------------------------------------------------------
     * Child iteration
     * ----------------------------------------------------------- */

    /*
     * A CURSOR, not four independent lookups.
     *
     * `cc_children_find_count` / `if_children_find` FILL an iterator with the
     * parent's dynamic child sub-ids; `cc_children_findnextid` and
     * `cc_children_findnext` then walk it. Only the fill takes a parent, and
     * only the fill takes a start index — the C VM keeps
     * `children_iter_parent/indices/index/count` between them, and the walkers
     * take no stack arguments at all.
     *
     * Reading a parent off the stack in the walkers, as this file did, means
     * the script's ONE argument lands in the parent slot and the start index
     * is undefined: script 9179 walks the Overview tab's children and rebinds
     * their ops, and it silently rebound nothing.
     *
     * DYNAMIC children only, and the bound is INCLUSIVE. A script passes
     * start=1 to walk sub-ids 1..N, and `> start` drops the first real child
     * whenever the allocator's first slot is 1 — which is exactly the child
     * 9179 needs.
     */
    function fillChildIterator(host, parentId, startIndex) {
        host.childIter = { parent: parentId, subIds: [], index: 0 };
        const parent = host.tree.findByComponentId(parentId);
        if( !parent ) return 0;
        for( const index of host.tree.children(parent.index) )
        {
            const child = host.tree.at(index);
            if( !child || !child.dynamic ) continue;
            if( child.subId < (startIndex | 0) ) continue;
            host.childIter.subIds.push(child.subId);
        }
        /* Sub-id order, not sibling order: the reference sorts the collected
         * indices, and a script indexing rows by number expects that. */
        host.childIter.subIds.sort((a, b) => a - b);
        return host.childIter.subIds.length;
    }

    /** Fill the cursor from a NAMED parent, and say how many. */
    proto.cc_children_find_count_for = function (componentId, startIndex) {
        return fillChildIterator(this, componentId, startIndex);
    };

    /** `cc_children_find_count(startIndex)` — the parent is the CURSOR's. */
    proto.cc_children_find_count = function (startIndex) {
        this.calls++;
        const parent = this.activeNode();
        if( !parent ) { this.childIter = { parent: -1, subIds: [], index: 0 }; return 0; }
        return fillChildIterator(this, parent.componentId, startIndex);
    };
    proto.dot_cc_children_find_count = function (startIndex) {
        this.calls++;
        const parent = this.dotNode();
        if( !parent ) { this.childIter = { parent: -1, subIds: [], index: 0 }; return 0; }
        return fillChildIterator(this, parent.componentId, startIndex);
    };

    /** `if_children_find(startIndex, uid)` — fill from a NAMED component. */
    proto.if_children_find = function (startIndex, componentId) {
        this.calls++;
        const group = componentId >= 0 ? (componentId >>> 16) & 0xffff : -1;
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);
        fillChildIterator(this, componentId, startIndex);
        /* The fill also SELECTS the parent, like `cc_find` — the reference's
         * `set_target_dot` arm — so the walkers below have a cursor even when
         * the script never found the parent itself. */
        const parent = this.tree.findByComponentId(componentId);
        if( parent ) this.setActive(parent.index);
        return undefined;
    };
    proto.dot_if_children_find = function (startIndex, componentId) {
        this.calls++;
        const group = componentId >= 0 ? (componentId >>> 16) & 0xffff : -1;
        if( group >= 0 && !this.tree.hasGroup(group) ) return this._park('component', group);
        fillChildIterator(this, componentId, startIndex);
        const parent = this.tree.findByComponentId(componentId);
        if( parent ) this.setDot(parent.index);
        return undefined;
    };

    /** The next sub-id, or -1. Does NOT move the component cursor. */
    proto.cc_children_findnextid = function () {
        this.calls++;
        const iter = this.childIter;
        if( !iter || iter.index >= iter.subIds.length ) return -1;
        return iter.subIds[iter.index++];
    };
    proto.dot_cc_children_findnextid = proto.cc_children_findnextid;
    proto.if_children_findnextid = proto.cc_children_findnextid;
    proto.dot_if_children_findnextid = proto.cc_children_findnextid;

    /**
     * `cc_children_findnext()` — advance AND select, pushing 1 or 0.
     *
     * Distinct from `findnextid`, which only reads. Script 9179 compares this
     * against 1 and then runs `.cc_setop`/`.cc_setonop` on the child it
     * selected; aliasing the two broke the Overview and Quest XP tabs.
     */
    function advanceChildIterator(host, select) {
        host.calls++;
        const iter = host.childIter;
        if( !iter || iter.parent < 0 || iter.index >= iter.subIds.length ) return 0;
        const parent = host.tree.findByComponentId(iter.parent);
        if( !parent ) return 0;
        const child = host.tree.findChildBySubId(parent.index, iter.subIds[iter.index]);
        if( !child ) return 0;
        iter.index++;
        select(child.index);
        return 1;
    }

    proto.cc_children_findnext = function () {
        return advanceChildIterator(this, (index) => this.setActive(index));
    };
    proto.dot_cc_children_findnext = function () {
        return advanceChildIterator(this, (index) => this.setDot(index));
    };
}
