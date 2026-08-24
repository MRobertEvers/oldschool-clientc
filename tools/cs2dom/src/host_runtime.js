/*
 * A live, React-side host for one cs2dom interface.
 *
 * The IR remains the rendering authority: component requests mutate a private
 * copy of it and preview.layout resolves that copy into the next DOM snapshot.
 * Script execution is intentionally injected. The browser's production C
 * CS2VM/WASM receives one fully identified hook intent and calls request()/mutate()
 * back synchronously through its JavaScript HOST bridge. This module owns the
 * UITree/HOST state; it neither implements bytecode nor paints/replays a native
 * framebuffer.
 */

import { ELEMENTS, IF_TYPE } from './components.js';
import { CS2_COMMANDS } from './cs2_commands.js';
import { HOST_READS } from './host.js';
import { OPS } from './ops.js';
import { layout as resolveLayout } from './preview.js';

export const HOST_RUNTIME_SCHEMA = 'cs2dom-host/1';

export const HOST_RUNTIME_LIMITS = Object.freeze({
    components: 8192,
    dynamicComponents: 4096,
    hookInvocations: 256,
    hookArgs: 64,
    hookTriggers: 4096,
    keyTargets: 64,
    changes: 4096,
    text: 65535,
    viewport: 4096,
});

const TYPE_KIND = new Map([
    [IF_TYPE.inv, 'Object'], [IF_TYPE.rectangle, 'Rect'], [IF_TYPE.text, 'Text'],
    [IF_TYPE.graphic, 'Graphic'], [IF_TYPE.model, 'Model'], [IF_TYPE.line, 'Line'],
    [10, 'Arc'],
]);

/* `authored` is the JSX spelling, `imported` contains exact .if spellings, and
 * `canonical` is the live UITree/CS2 host spelling. Keep all three on intents:
 * collapsing them made it impossible to tell which binding was running. */
const EVENT_DEFINITIONS = Object.freeze([
    event('onOp', 'on_op', 'onop'),
    event('onClick', 'on_click', 'onclick'),
    event('onClickRepeat', 'on_click_repeat', 'onclickrepeat'),
    event('onMouseOver', 'on_mouse_over', 'onmouseover'),
    event('onMouseLeave', 'on_mouse_leave', 'onmouseleave'),
    event('onMouseRepeat', 'on_mouse_repeat', 'onmouserepeat'),
    event('onHold', 'on_hold', 'onhold'),
    event('onRelease', 'on_release', 'onrelease'),
    event('onDrag', 'on_drag', 'ondrag'),
    event('onDragComplete', 'on_drag_complete', 'ondragcomplete'),
    event('onScrollWheel', 'on_scroll_wheel', 'onscrollwheel'),
    event('onTargetEnter', 'on_target_enter', 'ontargetenter'),
    event('onTargetLeave', 'on_target_leave', 'ontargetleave'),
    event('onTimer', 'on_timer', 'ontimer'),
    event('onLoad', 'on_load', 'onload'),
    event(null, 'on_key', 'onkey'),
    event(null, 'on_key_down', 'onkeydown', 'onkey_down'),
    event(null, 'on_key_up', 'onkeyup', 'onkey_up'),
    event(null, 'on_var_transmit', 'onvartransmit', 'onvarptransmit'),
    event(null, 'on_stat_transmit', 'onstattransmit'),
    event(null, 'on_inv_transmit', 'oninvtransmit'),
    event(null, 'on_varc_transmit', 'onvarctransmit'),
    event(null, 'on_varcstr_transmit', 'onvarcstrtransmit'),
    event(null, 'on_chat_transmit', 'onchattransmit'),
    event(null, 'on_friend_transmit', 'onfriendtransmit'),
    event(null, 'on_clan_transmit', 'onclantransmit'),
    event(null, 'on_misc_transmit', 'onmisctransmit'),
    event(null, 'on_stock_transmit', 'onstocktransmit'),
    event(null, 'on_dialog_abort', 'ondialogabort'),
    event(null, 'on_sub_change', 'onsubchange'),
    event(null, 'on_resize', 'onresize'),
    event(null, 'on_clan_settings_transmit', 'onclansettingstransmit'),
    event(null, 'on_clan_channel_transmit', 'onclanchanneltransmit'),
    event(null, 'on_item_on_item', 'onitemonitem'),
    event(null, 'on_clan_settings', 'onclansettings'),
    event(null, 'on_map_post', 'onmappost'),
    event(null, 'on_submit', 'onsubmit'),
    event(null, 'on_abort', 'onabort'),
    event(null, 'on_focus_changed', 'onfocuschanged'),
    event(null, 'on_update', 'onupdate'),
]);

const EVENT_BY_NAME = new Map();
for( const definition of EVENT_DEFINITIONS ) {
    for( const name of [definition.authored, definition.canonical, ...definition.imported] )
        if( name ) EVENT_BY_NAME.set(normalizeEventName(name), definition);
}

const POINTER_EVENTS = new Set([
    'on_op', 'on_click', 'on_click_repeat', 'on_mouse_over', 'on_mouse_leave',
    'on_mouse_repeat', 'on_hold', 'on_release', 'on_drag', 'on_drag_complete',
    'on_scroll_wheel',
]);

const BOOL_OPS = new Set([
    'if_sethide', 'if_setnoclickthrough', 'if_setfill', 'if_settiling',
    'if_settextshadow', 'if_sethflip', 'if_setvflip', 'if_setmodelorthog',
    'if_setlinedirection', 'if_setmodeltransparent', 'if_setnoscrollthrough',
    'if_setpinch', 'if_setopforceleftclick',
]);

/* Type-restricted setters are silent no-ops on the wrong widget kind, matching
 * the client's HOST boundary. Geometry, visibility, alpha and interaction
 * metadata are common fields and intentionally stay unrestricted. */
const OP_TYPES = new Map([
    ['if_setscrollpos', new Set([IF_TYPE.layer])],
    ['if_setscrollsize', new Set([IF_TYPE.layer])],
    ['if_settext', new Set([IF_TYPE.text, IF_TYPE.tooltip])],
    ['if_settextfont', new Set([IF_TYPE.text, IF_TYPE.tooltip])],
    ['if_settextalign', new Set([IF_TYPE.text, IF_TYPE.tooltip])],
    ['if_settextshadow', new Set([IF_TYPE.text, IF_TYPE.tooltip])],
    ['if_setgraphic', new Set([IF_TYPE.graphic])],
    ['if_sethttpsprite', new Set([IF_TYPE.graphic])],
    ['if_setgraphic2', new Set([IF_TYPE.graphic])],
    ['if_set2dangle', new Set([IF_TYPE.graphic])],
    ['if_settiling', new Set([IF_TYPE.graphic])],
    ['if_setoutline', new Set([IF_TYPE.graphic])],
    ['if_setgraphicshadow', new Set([IF_TYPE.graphic])],
    ['if_sethflip', new Set([IF_TYPE.graphic])],
    ['if_setvflip', new Set([IF_TYPE.graphic])],
    ['if_setmodel', new Set([IF_TYPE.model])],
    ['if_setmodelsource', new Set([IF_TYPE.model])],
    ['if_setmodelanim', new Set([IF_TYPE.model])],
    ['if_setmodelorthog', new Set([IF_TYPE.model])],
    ['if_setmodeltransparent', new Set([IF_TYPE.model])],
    ['if_setmodelangle', new Set([IF_TYPE.model])],
    ['if_setlinewid', new Set([IF_TYPE.line, 10])],
    ['if_setlinedirection', new Set([IF_TYPE.line])],
    ['if_setfill', new Set([IF_TYPE.rectangle, 10])],
    ['if_setarc', new Set([10])],
]);

const REQUEST_SETTERS = Object.freeze({
    SETPOSITION: ['if_setposition', [['x'], ['y'], ['xmode', 'x_mode', 'xMode'], ['ymode', 'y_mode', 'yMode']]],
    SETSIZE: ['if_setsize', [['width'], ['height'], ['wmode', 'width_mode', 'widthMode'], ['hmode', 'height_mode', 'heightMode']]],
    SETHIDE: ['if_sethide', [['hidden']]],
    SETNOCLICKTHROUGH: ['if_setnoclickthrough', [['enabled', 'value']]],
    SETNOSCROLLTHROUGH: ['if_setnoscrollthrough', [['enabled', 'value']]],
    SETPINCH: ['if_setpinch', [['enabled', 'value']]],
    SETSCROLLPOS: ['if_setscrollpos', [['scroll_x', 'scrollX'], ['scroll_y', 'scrollY']]],
    SETSCROLLSIZE: ['if_setscrollsize', [['scroll_width', 'scrollWidth'], ['scroll_height', 'scrollHeight']]],
    SETCOLOUR: ['if_setcolour', [['colour', 'color']]],
    SETFILL: ['if_setfill', [['filled', 'fill']]],
    SETTRANS: ['if_settrans', [['trans', 'transparency']]],
    SETLINEWID: ['if_setlinewid', [['value', 'line_width', 'lineWidth']]],
    SETLINEDIRECTION: ['if_setlinedirection', [['value', 'line_direction', 'lineDirection']]],
    SETGRAPHIC: ['if_setgraphic', [['graphic_id', 'graphicId', 'sprite']]],
    SETGRAPHIC2: ['if_setgraphic2', [['graphic_id', 'graphicId', 'sprite']]],
    SET2DANGLE: ['if_set2dangle', [['value', 'angle']]],
    SETTILING: ['if_settiling', [['tiling', 'tiled']]],
    SETMODEL: ['if_setmodel', [['model_id', 'modelId', 'model']]],
    SETMODELANGLE: ['if_setmodelangle', [
        ['offset_x', 'xOffset'], ['offset_y', 'yOffset'], ['angle_x', 'xAngle'],
        ['angle_y', 'yAngle'], ['angle_z', 'zAngle'], ['zoom'],
    ]],
    SETMODELANIM: ['if_setmodelanim', [['value', 'seq']]],
    SETMODELORTHOG: ['if_setmodelorthog', [['value', 'orthographic']]],
    SETMODELTRANSPARENT: ['if_setmodeltransparent', [['value', 'transparent']]],
    SETTEXT: ['if_settext', [['text']]],
    SETTEXTFONT: ['if_settextfont', [['font_id', 'fontId', 'font']]],
    SETTEXTALIGN: ['if_settextalign', [['x_align', 'xAlign', 'halign'], ['y_align', 'yAlign', 'valign'], ['line_height', 'lineHeight']]],
    SETTEXTSHADOW: ['if_settextshadow', [['shadowed', 'shadow']]],
    SETOUTLINE: ['if_setoutline', [['outline']]],
    SETGRAPHICSHADOW: ['if_setgraphicshadow', [['shadow']]],
    SETVFLIP: ['if_setvflip', [['value', 'vflip', 'vFlip']]],
    SETHFLIP: ['if_sethflip', [['value', 'hflip', 'hFlip']]],
    SETFILLCOLOUR: ['if_setfillcolour', [['value', 'colour', 'color', 'fillColor']]],
    SETTRANSBOT: ['if_settransbot', [['value', 'trans', 'bottomTransparency']]],
    SETFILLMODE: ['if_setfillmode', [['value', 'mode', 'fillMode']]],
    SETCLICKMASK: ['if_setclickmask', [['value', 'clickMask']]],
    SETOPFORCELEFTCLICK: ['if_setopforceleftclick', [['value', 'enabled', 'forceLeftClick']]],
    SETARC: ['if_setarc', [['arc_start', 'arcStart'], ['arc_end', 'arcEnd']]],
    SETTARGETVERB: ['if_settargetverb', [['text', 'target_verb', 'targetVerb']]],
});

const REQUEST_GETTERS = Object.freeze({
    GETWIDTH: 'if_getwidth', GETHEIGHT: 'if_getheight', GETX: 'if_getx', GETY: 'if_gety',
    GETSCROLLWIDTH: 'if_getscrollwidth', GETSCROLLHEIGHT: 'if_getscrollheight',
    GETSCROLLX: 'if_getscrollx', GETSCROLLY: 'if_getscrolly', GETHIDE: 'if_gethide',
    GETTEXT: 'if_gettext', GETLAYER: 'if_getlayer', GETOP: 'if_getop',
    GETTRANS: 'if_gettrans', GETCOLOUR: 'if_getcolour', GETFILLCOLOUR: 'if_getfillcolour',
    GETINVOBJECT: 'if_getinvobject', GETINVCOUNT: 'if_getinvcount', GETID: 'if_getid',
    GETTARGETMASK: 'if_gettargetmask', GETOPBASE: 'if_getopbase',
    GETMODELZOOM: 'if_getmodelzoom', GETMODELANGLE_X: 'if_getmodelangle_x',
    GETMODELANGLE_Y: 'if_getmodelangle_y', GETMODELANGLE_Z: 'if_getmodelangle_z',
    GETMODELTRANSPARENT: 'if_getmodeltransparent', GETARCSTART: 'if_getarcstart',
    GETARCEND: 'if_getarcend', GETBLENDTRANS: 'if_getblendtrans',
});

const INPUT_SETTERS = Object.freeze({
    INPUT_SETSUBMITMODE: 'submitMode',
    INPUT_SETSELECTCOLOUR: 'selectionColor',
    INPUT_SETACCEPTMODE: 'acceptMode',
    INPUT_SETWRAPMODE: 'wrapMode',
    INPUT_SETLINEWRAPPINGWIDTH: 'lineWrappingWidth',
    INPUT_SETSELECTBGCOLOUR: 'selectionBackgroundColor',
    INPUT_SETLINECOUNTLIMIT: 'lineCountLimit',
    INPUT_SETCURSORCOLOUR: 'cursorColor',
    INPUT_SETCURSORTRANS: 'cursorTransparency',
    INPUT_SETCURSORWIDTH: 'cursorWidth',
    INPUT_SETCURSORHEIGHT: 'cursorHeight',
    INPUT_SETCURSOROFFSET: 'cursorOffset',
    INPUT_SETLINEWIDTHLIMIT: 'lineWidthLimit',
    INPUT_SETCHARFILTER: 'characterFilter',
});

const INPUT_GETTERS = Object.freeze({
    INPUT_GETCARETPOSITION: 'caretPosition',
    INPUT_GETFOCUS: 'focused',
});

const STATE_READ_REQUEST = Object.freeze({
    VARS_READ_VARP: 'varp', VARS_READ_VARBIT: 'varbit',
    VARS_READ_VARC_INT: 'varc', VARS_READ_VARC_STRING: 'varcstr',
    PUSH_VAR: 'varp', PUSH_VARBIT: 'varbit', PUSH_VARC_INT: 'varc',
    PUSH_VARC_STRING: 'varcstr', PUSH_VARC_STRING_OLD: 'varcstr',
    STAT: 'stat', STAT_BASE: 'stat',
});

const STATE_WRITE_REQUEST = Object.freeze({
    VARS_WRITE_VARP: 'varp', VARS_WRITE_VARBIT: 'varbit',
    VARS_WRITE_VARC_INT: 'varc', VARS_WRITE_VARC_STRING: 'varcstr',
    POP_VAR: 'varp', POP_VARBIT: 'varbit', POP_VARC_INT: 'varc',
    POP_VARC_STRING: 'varcstr', POP_VARC_STRING_OLD: 'varcstr',
});

const STATE_KINDS = new Set(['varp', 'varbit', 'varc', 'varcstr', 'stat', 'inv']);

const SPECIAL_REQUESTS = new Set([
    'CC_CREATE', 'CC_CREATECHILD', 'CC_CREATESIBLING', 'CC_COPY', 'CC_DELETE',
    'CC_DELETEALL', 'CC_FIND', 'IF_FIND', 'IF_CHILDREN_FIND', 'IF_CHILDREN_COLLECT',
    'CC_CHILDREN_FIND_COUNT', 'CC_CHILDREN_FINDNEXT', 'CC_CHILDREN_FINDNEXTID',
    'IF_CHILDREN_FINDNEXTID', 'CHILDREN_FINDNEXTID', 'CC_PARENTID', 'IF_GETTOP',
    'INVS_GET_NUM', 'INVS_GET_TOTAL', 'INVS_GET_SIZE', 'INV_GETOBJ', 'INV_GETNUM',
    'INV_TOTAL', 'INV_SIZE', 'CLIENTCLOCK', 'SOUND_SYNTH',
    /* Synchronous game/cache reads used while an interface constructs itself.
     * The server ships these small lookup tables beside the bytecode. */
    'CLIENTTYPE', 'MAP_MEMBERS', 'ON_MOBILE',
    'RUNENERGY_VISIBLE', 'RUNWEIGHT_VISIBLE',
    'ENUM', 'ENUM_STRING', 'ENUM_GETOUTPUTCOUNT',
    'PARAWIDTH', 'PARAHEIGHT',
    'OC_NAME', 'OC_COST', 'OC_STACKABLE', 'OC_CERT', 'OC_UNCERT', 'OC_MEMBERS',
    'OC_PLACEHOLDER', 'OC_UNPLACEHOLDER', 'OC_OP', 'OC_IOP', 'OC_PARAM',
    'STRUCT_PARAM', 'NC_PARAM', 'LC_PARAM',
]);

const SPECIAL_COMPONENT_SUFFIXES = new Set([
    'SETOP', 'SETOBJECT', 'SETOBJECT_NONUM', 'SETOBJECT_ALWAYS_NUM',
    'SETNPCHEAD', 'SETPLAYERHEAD_SELF', 'SETPLAYERMODEL_SELF',
    'SETMODEL_PLAYERCHATHEAD', 'SETLOCMODEL', 'SETNPCMODEL',
    'SETDRAGGABLE', 'SETDRAGGABLEBEHAVIOR',
    'SETDRAGDEADZONE', 'SETDRAGDEADTIME', 'SETOPBASE', 'CLEAROPS',
    'SETOPSUBMENU', 'CLEAROPSUBMENU', 'SETTARGETPRIORITY', 'SETCOMPONENTPARAM',
    'SETPARAM', 'GETCOMPONENTPARAM', 'SETOPKEY', 'SETOPTKEY', 'SETOPKEYRATE',
    'SETOPTKEYRATE', 'SETOPKEYIGNOREHELD', 'SETOPTKEYIGNOREHELD',
    'TRIGGEROP', 'TRIGGEROPLOCAL', 'CALLONRESIZE',
    /* Complete generated CC_/IF_ surface. Some are service requests rather
     * than component mutations, but they are still deliberately handled by
     * this HOST instead of falling through as plausible silent failures. */
    'ASSERT', 'CRMVIEW_DISMISS', 'DRAGPICKUP', 'FIND_PARAM', 'GETPARAM',
    'OP1309', 'OP2309', 'RESUME_PAUSEBUTTON', 'SETHTTPSPRITE',
    'CLOSE', 'HASCHILD_OVERLAY', 'HASSUB',
]);

const COMMAND_NAMES = new Set([...CS2_COMMANDS.values()]
    .map((command) => command.name.toUpperCase()));

/** Describe whether a named CS2 command belongs to this UITree/HOST. */
export function hostRequestCapability(rawKind) {
    const kind = normalizeRequestKind(rawKind);
    const supported = supportsHostRequest(kind);
    const known = COMMAND_NAMES.has(kind);
    return Object.freeze({
        kind, known, supported,
        reason: supported ? null : known
            ? 'command is outside the bounded UITree/HOST implementation'
            : 'unknown command name',
    });
}

const coverageEntries = [...COMMAND_NAMES].sort().map((kind) => hostRequestCapability(kind));
export const HOST_REQUEST_COVERAGE = Object.freeze({
    total: coverageEntries.length,
    supported: coverageEntries.filter((entry) => entry.supported).length,
    unsupported: coverageEntries.filter((entry) => !entry.supported).length,
    uiTotal: coverageEntries.filter((entry) => /^(?:CC|IF)_/.test(entry.kind)).length,
    uiSupported: coverageEntries.filter((entry) => /^(?:CC|IF)_/.test(entry.kind) && entry.supported).length,
    entries: Object.freeze(coverageEntries),
});

export class HostRuntimeError extends Error {
    constructor(message, code = 'HOST_RUNTIME') {
        super(message);
        this.name = 'HostRuntimeError';
        this.code = code;
    }
}

/** Create an isolated, mutable UITree/HOST. Call mount() after `invoke` is ready. */
export function createHostRuntime(ir, options = {}) {
    return new HostRuntime(ir, options);
}

export class HostRuntime {
    constructor(ir, options = {}) {
        if( !ir || !Array.isArray(ir.components) )
            throw new HostRuntimeError('host runtime requires an interface IR', 'BAD_IR');
        if( ir.components.length > HOST_RUNTIME_LIMITS.components )
            throw new HostRuntimeError(
                `interface has ${ir.components.length} components; limit is ${HOST_RUNTIME_LIMITS.components}`,
                'LIMIT');

        this.viewport = viewport(options.viewport);
        this.state = cloneState(options.state || {});
        this.invoke = typeof options.invoke === 'function' ? options.invoke : () => undefined;
        this.paramDefault = typeof options.paramDefault === 'function'
            ? options.paramDefault : () => 0;
        this.hostData = normalizeHostData(options.hostData);
        this.clientType = integer(options.clientType ?? this.hostData.clientType, 10);
        this.mapMembers = Boolean(options.mapMembers ?? this.hostData.mapMembers ?? true);
        this.interfaceParents = normalizeInterfaceParents(
            options.interfaceParents ?? this.state.interfaceParents ?? this.hostData.interfaceParents);
        this.services = {
            closeModalRequested: false,
            resumePauseButton: null,
            crmViewDismissals: 0,
            soundSynthCount: 0,
            lastSoundSynth: null,
        };
        this.limits = limits(options.limits);
        this.ir = cloneInterface(ir);
        this.interfaceId = integer(ir.interfaceId, 0);
        this.version = 0;
        this.epoch = 0;
        this.cycle = 0;
        this.mounted = false;
        this.dynamicCount = 0;
        this.nextDynamic = 0;
        this.nextDynamicUid = 0x8000;
        this.nextGeneration = 1;
        this.sequence = 0;
        this.operationDepth = 0;
        this.dispatchDepth = 0;
        this.invocations = 0;
        this.changeLog = [];
        this.meta = new WeakMap();
        this.byKey = new Map();
        this.byName = new Map();
        this.byFileId = new Map();
        this.byUid = new Map();
        this.active = null;
        this.dotActive = null;
        this.childIteration = { parent: null, refs: [], index: 0 };
        this.layoutVersion = -1;
        this.layoutCache = [];
        this.boxByComponent = new WeakMap();
        this.interaction = {
            x: 0, y: 0, hover: null, pressed: null, button: null,
            pressX: 0, pressY: 0, pressCycle: 0, clickFired: false,
            dragging: false, dragPickupX: 0, dragPickupY: 0, heldKeys: new Set(),
        };

        const pendingDynamic = [];
        for( const component of this.ir.components ) {
            if( component.runtimeDynamic ) pendingDynamic.push(component);
            else this._indexStatic(component);
        }
        /* Source-hook previewing may already have materialised CC children.
         * Preserve their dynamic parent/sub-id identity instead of turning the
         * synthetic `dN` archive key into a static widget id. */
        while( pendingDynamic.length ) {
            const before = pendingDynamic.length;
            for( let index = pendingDynamic.length - 1; index >= 0; index-- ) {
                const component = pendingDynamic[index];
                const parent = this.byFileId.get(component.layer);
                if( !parent ) continue;
                this._indexDynamic(component, parent, boundedInteger(
                    'dynamic child index', component.subId ?? 0, 0, 65535));
                this.dynamicCount++;
                pendingDynamic.splice(index, 1);
            }
            if( pendingDynamic.length === before )
                throw new HostRuntimeError('dynamic component has no live parent', 'BAD_IR');
        }
    }

    /** Run cache/React onLoad hooks exactly once after the script runner is installed. */
    mount() {
        return this._boundary(() => {
            if( this.mounted ) return this._result([]);
            this.mounted = true;
            const intents = [];
            for( const component of [...this.ir.components] ) {
                const resolved = this._resolveHook(component, definition('on_load'));
                if( resolved ) this._emit(component, resolved, baseEvent('mount'), {}, intents);
            }
            return this._result(intents);
        });
    }

    /** Current paint-order boxes. Each box carries the stable component ref. */
    layout() {
        if( this.layoutVersion === this.version ) return this.layoutCache;
        const raw = resolveLayout(this.ir, this.state, this.viewport);
        const byFileId = new Map(this.ir.components.map((component) => [component.fileId, component]));
        this.boxByComponent = new WeakMap();
        this.layoutCache = raw.map((box) => {
            const component = byFileId.get(box.fileId);
            const value = {
                ...box,
                ref: component ? this.ref(component) : null,
                presentation: component ? this._presentation(component) : null,
            };
            if( component ) this.boxByComponent.set(component, value);
            return value;
        });
        this.layoutVersion = this.version;
        return this.layoutCache;
    }

    /** Serializable React renderer/state-tree snapshot. */
    snapshot() {
        this._retireInvisibleInteraction();
        return {
            schema: HOST_RUNTIME_SCHEMA,
            interfaceId: this.interfaceId,
            epoch: this.epoch,
            version: this.version,
            cycle: this.cycle,
            viewport: { ...this.viewport },
            state: cloneState(this.state),
            services: cloneValue(this.services),
            boxes: this.layout().map(cloneBox),
            interaction: this._interactionView(),
        };
    }

    /** Stable identity. Generation fences deletion/recreation of a dynamic slot. */
    ref(value) {
        const component = this._component(value, false);
        if( !component ) return null;
        const meta = this.meta.get(component);
        return Object.freeze({
            key: meta.key,
            componentId: meta.componentId,
            fileId: meta.publicFileId,
            subId: meta.subId,
            dynamic: meta.dynamic,
            generation: meta.generation,
            name: component.name,
        });
    }

    component(value) {
        const component = this._component(value);
        const meta = this.meta.get(component);
        const parent = component.layer === null || component.layer === undefined
            ? null : this.byFileId.get(component.layer) || null;
        return {
            ref: this.ref(component),
            fileId: meta.publicFileId,
            subId: meta.subId,
            name: component.name,
            kind: component.kind,
            type: component.type,
            parent: parent ? this.ref(parent) : null,
            props: cloneRecord(component.static || {}),
            ops: (component.ops || []).map((op) => ({ ...op })),
            hooks: Object.keys(component.hooks || {}),
            draggable: meta.draggable,
            dragParent: meta.dragParent,
            dragDeadZone: meta.dragDeadZone,
            dragDeadTime: meta.dragDeadTime,
            dragBehavior: meta.dragBehavior,
            runtime: runtimeView(component),
            presentation: this._presentation(component),
        };
    }

    /** Resolve any accepted target into a read-only view, or null if stale. */
    resolve(value) {
        const component = this._component(value, false);
        return component ? this.component(component) : null;
    }

    setActive(value, { dot = false } = {}) {
        const ref = value === null ? null : this.ref(this._component(value));
        if( dot ) this.dotActive = ref;
        else this.active = ref;
        return ref;
    }

    activeRef({ dot = false } = {}) {
        return dot ? this.dotActive : this.active;
    }

    /** Change the React stage dimensions and invalidate every resolved box. */
    setViewport(value) {
        return this._boundary(() => {
            const next = viewport(value);
            if( next.width === this.viewport.width && next.height === this.viewport.height )
                return { ...this.viewport };
            this.viewport = next;
            this._record({ kind: 'viewport', viewport: { ...next } });
            this._retireInvisibleInteraction();
            return { ...next };
        });
    }

    /** Component getter surface used by the C HOST bridge and React controls. */
    read(op, value = null, index = null) {
        const name = String(op || '').toLowerCase().replace(/^cc_/, 'if_');
        const component = this._component(value ?? this.active);
        const box = this._box(component);
        switch( name ) {
            case 'if_getwidth': return box?.w ?? 0;
            case 'if_getheight': return box?.h ?? 0;
            case 'if_getx': return box?.relX ?? 0;
            case 'if_gety': return box?.relY ?? 0;
            case 'if_getscrollwidth': return component.type === IF_TYPE.layer
                ? component.static.scrollWidth ?? 0 : 0;
            case 'if_getscrollheight': return component.type === IF_TYPE.layer
                ? component.static.scrollHeight ?? 0 : 0;
            case 'if_getscrollx': return box?.scrollX ?? component.static.scrollX ?? 0;
            case 'if_getscrolly': return box?.scrollY ?? component.static.scrollY ?? 0;
            case 'if_gethide': return Boolean(component.static.hidden);
            case 'if_gettext': return component.static.text ?? '';
            case 'if_getlayer': return component.layer === null ? -1
                : this.ref(this.byFileId.get(component.layer));
            case 'if_getop': return component.ops?.find((entry) => entry.index === Number(index))?.text || '';
            case 'if_getopbase': return component.runtime.opBase;
            case 'if_gettrans': return component.static.transparency ?? 0;
            case 'if_getcolour': return component.static.color ?? 0;
            case 'if_getfillcolour': return component.static.fillColor ?? 0;
            case 'if_getinvobject': return component.static.objectId ?? 0;
            case 'if_getinvcount': return component.static.objectCount ?? 0;
            case 'if_getid': return this.meta.get(component).subId;
            case 'if_gettargetmask': return component.static.targetMask ??
                finiteOptional(component.rawFields?.targetmask, 0);
            case 'if_getmodelzoom': return component.static.zoom ?? 0;
            case 'if_getmodelangle_x': return component.static.xAngle ?? 0;
            case 'if_getmodelangle_y': return component.static.yAngle ?? 0;
            case 'if_getmodelangle_z': return component.static.zAngle ?? 0;
            case 'if_getmodeltransparent': return Boolean(component.static.modelTransparent);
            case 'if_getarcstart': return component.static.arcStart ?? 0;
            case 'if_getarcend': return component.static.arcEnd ?? 0;
            case 'if_getblendtrans': return component.static.bottomTransparency ?? 0;
            case 'if_input_getfocus': return Boolean(component.runtime.input?.focused);
            case 'if_input_getcaretposition': return component.runtime.input?.caretPosition ?? 0;
            default: throw new HostRuntimeError(`unsupported component read ${op}`, 'UNSUPPORTED');
        }
    }

    /** Declarative asset/paint description consumed by a React renderer. */
    presentation(value) {
        return cloneValue(this._presentation(this._component(value)));
    }

    /** Typed editable-widget metadata and browser-owned focus/caret state. */
    inputState(value) {
        const component = this._component(value);
        return cloneInputState(component.runtime.input || {});
    }

    setInputState(value, patch) {
        return this._boundary(() => {
            if( !patch || typeof patch !== 'object' || Array.isArray(patch) )
                throw new HostRuntimeError('input state patch must be an object', 'BAD_REQUEST');
            const component = this._component(value);
            const input = component.runtime.input ||= cloneInputState({ configured: true });
            const changed = {};
            const previousFocus = input.focused;
            for( const key of Object.keys(patch) ) {
                if( key === 'focused' ) {
                    const next = Boolean(patch.focused);
                    if( input.focused !== next ) changed.focused = input.focused = next;
                } else if( key === 'caretPosition' ) {
                    const next = boundedInteger('caret position', patch.caretPosition,
                        0, HOST_RUNTIME_LIMITS.text);
                    if( input.caretPosition !== next )
                        changed.caretPosition = input.caretPosition = next;
                } else {
                    throw new HostRuntimeError(`unsupported input state field ${key}`, 'BAD_REQUEST');
                }
            }
            input.configured = true;
            const intents = [];
            if( Object.keys(changed).length ) {
                this._record({ kind: 'input', ref: this.ref(component), state: changed });
                if( previousFocus !== input.focused && this._visible(component) ) {
                    const resolved = this._resolveHook(component, definition('on_focus_changed'));
                    if( resolved ) this._emit(component, resolved,
                        baseEvent('input_focus', { focused: input.focused }), {}, intents);
                }
            }
            return this._result(intents, { input: cloneInputState(input) });
        });
    }

    _presentation(component) {
        const props = component.static || {};
        if( component.type === IF_TYPE.graphic ) return {
            kind: 'sprite', sprite: props.sprite ?? -1, activeSprite: props.activeSprite ?? -1,
            httpSprite: props.httpSprite ?? null,
            angle: props.angle ?? 0, tiled: Boolean(props.tiled), hFlip: Boolean(props.hFlip),
            vFlip: Boolean(props.vFlip), outline: props.outline ?? 0, shadow: props.shadow ?? 0,
            color: props.color ?? 0, transparency: props.transparency ?? 0,
        };
        if( component.type === IF_TYPE.model ) return {
            kind: 'model', source: modelSource(props), sequence: props.seq ?? -1,
            transparent: Boolean(props.modelTransparent), orthographic: Boolean(props.orthographic),
            fixedZoom: Boolean(props.fixedZoom),
            transform: {
                xOffset: props.xOffset ?? 0, yOffset: props.yOffset ?? 0,
                xAngle: props.xAngle ?? 0, yAngle: props.yAngle ?? 0,
                zAngle: props.zAngle ?? 0, zoom: props.zoom ?? 0,
            },
        };
        if( component.type === IF_TYPE.text || component.type === IF_TYPE.tooltip ) return {
            kind: 'text', text: props.text ?? '', font: props.font ?? -1,
            color: props.color ?? 0, shadow: Boolean(props.shadow),
            halign: props.halign ?? 0, valign: props.valign ?? 0, lineHeight: props.lineHeight ?? 0,
            input: component.runtime.input ? cloneInputState(component.runtime.input) : null,
        };
        if( component.type === IF_TYPE.inv || component.kind === 'Object' ) return {
            kind: 'object', objectId: props.objectId ?? 0, count: props.objectCount ?? 0,
            numberMode: props.objectNumMode ?? 0,
        };
        if( component.type === 10 ) return {
            kind: 'arc', color: props.color ?? 0, fillColor: props.fillColor ?? 0,
            fill: Boolean(props.fill), lineWidth: props.lineWidth ?? 1,
            start: props.arcStart ?? 0, end: props.arcEnd ?? 0,
            transparency: props.transparency ?? 0,
        };
        if( component.type === IF_TYPE.rectangle ) return {
            kind: 'rect', color: props.color ?? 0, fillColor: props.fillColor ?? 0,
            fill: Boolean(props.fill), fillMode: props.fillMode ?? 0,
            transparency: props.transparency ?? 0,
            bottomTransparency: props.bottomTransparency ?? 0,
        };
        if( component.type === IF_TYPE.line ) return {
            kind: 'line', color: props.color ?? 0, lineWidth: props.lineWidth ?? 1,
            direction: Boolean(props.lineDirection), transparency: props.transparency ?? 0,
        };
        return { kind: 'layer', transparency: props.transparency ?? 0 };
    }

    /** Apply one cs2dom/CS2 component operation to the owned IR. */
    mutate(op, value, ...rawValues) {
        return this._boundary(() => this._mutate(op, value, unpack(rawValues)));
    }

    _mutate(rawOp, value, values) {
        const op = String(rawOp || '').toLowerCase().replace(/^cc_/, 'if_');
        const component = this._component(value ?? this.active);
        const meta = this.meta.get(component);
        if( !this._supports(component, op) ) return this.ref(component);
        const inputField = INPUT_SETTERS[op.slice('if_'.length).toUpperCase()];
        if( inputField ) return this._setInputField(component, inputField, values[0]);
        if( op === 'if_setlocmodel' )
            return this._mutate('if_setmodelsource', component, ['locModel', values[0]]);
        if( op === 'if_setnpcmodel' )
            return this._mutate('if_setmodelsource', component, ['npcModel', values[0]]);
        if( op === 'if_setscrollpos' ) {
            if( values.length < 2 )
                throw new HostRuntimeError(`${op} needs 2 values, got ${values.length}`, 'BAD_REQUEST');
            const box = this._box(component);
            const maxX = Math.max(0, finiteOptional(component.static.scrollWidth, 0) - (box?.w || 0));
            const maxY = Math.max(0, finiteOptional(component.static.scrollHeight, 0) - (box?.h || 0));
            return this._setProps(component, op, ['scrollX', 'scrollY'], [
                clampInteger(values[0], 0, maxX), clampInteger(values[1], 0, maxY),
            ]);
        }
        if( op === 'if_setscrollsize' ) {
            const changed = this._setProps(component, op, ['scrollWidth', 'scrollHeight'], values);
            const box = this._box(component);
            const maxX = Math.max(0, finiteOptional(component.static.scrollWidth, 0) - (box?.w || 0));
            const maxY = Math.max(0, finiteOptional(component.static.scrollHeight, 0) - (box?.h || 0));
            const x = clampInteger(component.static.scrollX ?? 0, 0, maxX);
            const y = clampInteger(component.static.scrollY ?? 0, 0, maxY);
            if( x !== component.static.scrollX || y !== component.static.scrollY )
                this._setProps(component, 'if_setscrollpos', ['scrollX', 'scrollY'], [x, y]);
            return changed;
        }
        if( op === 'if_setop' ) {
            /* The client exposes ten menu slots. Cache scripts sometimes write
             * slot 11 (notably bank item "Examine"); rs_cs2_apply_op silently
             * ignores it, so the browser host must not abort the script. */
            const suppliedIndex = Number(values[0]);
            if( Number.isInteger(suppliedIndex) && (suppliedIndex < 1 || suppliedIndex > 10) )
                return this.ref(component);
            const index = boundedInteger('operation index', values[0], 1, 10);
            const text = boundedText('operation text', values[1] ?? '');
            component.ops ||= [];
            const previous = component.ops.find((entry) => entry.index === index)?.text || '';
            if( previous === text ) return this.ref(component);
            component.ops = component.ops.filter((entry) => entry.index !== index);
            if( text ) component.ops.push({ index, text });
            component.ops.sort((a, b) => a.index - b.index);
            return this._changed('component', component, { op, index, text });
        }
        if( op === 'if_setobject' ) {
            const objectId = finiteOptional(values[0], 0);
            const count = finiteOptional(values[1], 0);
            const numberMode = finiteOptional(values[2], 0);
            const renderObjectId = objectId > 0
                ? resolveCountObject(this.hostData.objects, objectId, count) : -1;
            const props = ['objectId', 'objectCount', 'objectNumMode', 'modelKind', 'modelSourceId'];
            const next = [objectId, count, numberMode,
                objectId > 0 ? 'object' : 'none', renderObjectId];

            /* The native HOST applies an ObjType's interface-model camera as
             * part of CC/IF_SETOBJECT. A newly-created MODEL starts at zoom 100;
             * merely swapping its model id therefore renders a close-up of one
             * face. Use the count-resolved ObjType, just like
             * ObjModelLoad_RenderObjId + exec_set_object in the C client. */
            if( component.type === IF_TYPE.model && objectId > 0 ) {
                const object = this.hostData.objects[String(renderObjectId)] ||
                    this.hostData.objects[String(objectId)] || null;
                if( object ) {
                    props.push('xAngle', 'yAngle', 'zoom', 'xOffset', 'yOffset');
                    next.push(
                        finiteOptional(object.xan2d, 0), finiteOptional(object.yan2d, 0),
                        finiteOptional(object.zoom2d, 2000) > 0
                            ? finiteOptional(object.zoom2d, 2000) : 2000,
                        0, finiteOptional(object.offsetY2d ?? object.offset_y2d, 0));
                }
            }
            return this._setProps(component, op, props, next);
        }
        if( op === 'if_setmodel' ) {
            const result = this._setProps(component, op, ['model'], values);
            component.static.modelKind = 'model';
            component.static.modelSourceId = finiteOptional(values[0], -1);
            return result;
        }
        if( op === 'if_setmodelsource' ) {
            if( values.length < 2 )
                throw new HostRuntimeError(`${op} needs kind and id`, 'BAD_REQUEST');
            const kind = modelKind(values[0]);
            const id = finiteValue('model source id', values[1]);
            return this._setProps(component, op, ['modelKind', 'modelSourceId'], [kind, id]);
        }
        if( op === 'if_setmodelangle' ) {
            if( values.length < 6 )
                throw new HostRuntimeError(`${op} needs 6 values, got ${values.length}`, 'BAD_REQUEST');
            const props = ['xOffset', 'yOffset', 'xAngle', 'yAngle', 'zAngle'];
            const next = values.slice(0, 5);
            if( finiteValue('zoom', values[5]) > 0 ) {
                props.push('zoom');
                next.push(values[5]);
            }
            return this._setProps(component, op, props, next);
        }
        if( op === 'if_setgraphic2' )
            return this._setProps(component, op, ['activeSprite'], values);
        if( op === 'if_sethttpsprite' ) {
            const url = boundedText('HTTPS sprite URL', values[0] ?? '');
            if( url && !/^https:\/\//i.test(url) )
                throw new HostRuntimeError('HTTPS sprite URL must use https://', 'BAD_REQUEST');
            if( component.static.httpSprite === url ) return this.ref(component);
            component.static.httpSprite = url || null;
            if( component.props ) component.props.httpSprite = url || null;
            return this._changed('component', component, { op, url });
        }
        if( op === 'if_setfillcolour' )
            return this._setProps(component, op, ['fillColor'], values);
        if( op === 'if_settransbot' )
            return this._setProps(component, op, ['bottomTransparency'], values);
        if( op === 'if_setfillmode' )
            return this._setProps(component, op, ['fillMode'], values);
        if( op === 'if_setmodeltransparent' )
            return this._setProps(component, op, ['modelTransparent'], values);
        if( op === 'if_setnoscrollthrough' )
            return this._setProps(component, op, ['noScrollThrough'], values);
        if( op === 'if_setpinch' )
            return this._setProps(component, op, ['pinch'], values);
        if( op === 'if_setclickmask' )
            return this._setProps(component, op, ['clickMask'], values);
        if( op === 'if_setopforceleftclick' )
            return this._setProps(component, op, ['forceLeftClick'], values);
        if( op === 'if_setarc' )
            return this._setProps(component, op, ['arcStart', 'arcEnd'], values);
        if( op === 'if_setopbase' ) {
            const text = boundedText('operation base', values[0] ?? '');
            if( component.runtime.opBase === text ) return this.ref(component);
            component.runtime.opBase = text;
            return this._changed('component', component, { op, text });
        }
        if( op === 'if_clearops' ) {
            if( !component.ops?.length ) return this.ref(component);
            component.ops = [];
            return this._changed('component', component, { op });
        }
        if( op === 'if_setopsubmenu' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const subIndex = boundedInteger('submenu index', values[1], 0, 31);
            const text = boundedText('submenu text', values[2] ?? '');
            component.runtime.submenus[opIndex] ||= {};
            if( text ) component.runtime.submenus[opIndex][subIndex] = text;
            else delete component.runtime.submenus[opIndex][subIndex];
            return this._changed('component', component, { op, opIndex, subIndex, text });
        }
        if( op === 'if_clearopsubmenu' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            if( !component.runtime.submenus[opIndex] ) return this.ref(component);
            delete component.runtime.submenus[opIndex];
            return this._changed('component', component, { op, opIndex });
        }
        if( op === 'if_settargetpriority' ) {
            const priority = finiteValue('target priority', values[0]);
            if( component.runtime.targetPriority === priority ) return this.ref(component);
            component.runtime.targetPriority = priority;
            return this._changed('component', component, { op, priority });
        }
        if( op === 'if_setcomponentparam' ) {
            const paramId = stateId(values[0]);
            const entry = typeof values[1] === 'string'
                ? { string: boundedText('component parameter', values[1]) }
                : { value: finiteValue('component parameter', values[1]) };
            component.runtime.params[paramId] = entry;
            return this._changed('component', component, { op, paramId, entry });
        }
        if( op === 'if_setopkey' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const chars = boundedKeyList('operation key characters', values[1]);
            const codes = boundedKeyList('operation key codes', values[2]);
            if( chars.length !== codes.length )
                throw new HostRuntimeError('operation key arrays must have equal length', 'BAD_REQUEST');
            component.runtime.opKeys[opIndex] = {
                pairs: chars.map((character, index) => ({ character, code: codes[index] })),
                rate: 0, enabled: true, ignoreHeld: false,
            };
            return this._changed('component', component, {
                op, opIndex, pairs: component.runtime.opKeys[opIndex].pairs,
            });
        }
        if( op === 'if_setopkeyrate' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const key = component.runtime.opKeys[opIndex] ||= {
                pairs: [], rate: 0, enabled: true, ignoreHeld: false,
            };
            key.rate = finiteValue('operation key rate', values[1]);
            key.enabled = Boolean(values[2]);
            return this._changed('component', component, {
                op, opIndex, rate: key.rate, enabled: key.enabled,
            });
        }
        if( op === 'if_setopkeyignoreheld' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const key = component.runtime.opKeys[opIndex] ||= {
                pairs: [], rate: 0, enabled: true, ignoreHeld: false,
            };
            key.ignoreHeld = true;
            return this._changed('component', component, { op, opIndex });
        }
        if( op === 'if_setdraggable' ) {
            meta.draggable = Boolean(values[0] ?? true);
            meta.dragParent = values[1] ? this.ref(this._component(values[1])) : null;
            return this._changed('component', component, { op, draggable: meta.draggable });
        }
        if( op === 'if_setdragdeadzone' ) {
            /* UITree stores both fields as uint8_t; CS2 passes an unrestricted
             * int and the client truncates it (INT_MAX intentionally becomes
             * 255 to make a bank tab effectively non-draggable). */
            meta.dragDeadZone = finiteValue('drag dead zone', values[0]) & 0xff;
            return this._changed('component', component, { op, value: meta.dragDeadZone });
        }
        if( op === 'if_setdragdeadtime' ) {
            meta.dragDeadTime = finiteValue('drag dead time', values[0]) & 0xff;
            return this._changed('component', component, { op, value: meta.dragDeadTime });
        }
        if( op === 'if_setdraggablebehavior' ) {
            meta.dragBehavior = finiteValue('drag behavior', values[0]);
            return this._changed('component', component, { op, value: meta.dragBehavior });
        }
        const signature = OPS[op];
        if( !signature?.args )
            throw new HostRuntimeError(`unsupported component mutation ${rawOp}`, 'UNSUPPORTED');
        return this._setProps(component, op, signature.args, values);
    }

    _setProps(component, op, props, values) {
        if( values.length < props.length )
            throw new HostRuntimeError(`${op} needs ${props.length} values, got ${values.length}`, 'BAD_REQUEST');
        const changed = {};
        for( let index = 0; index < props.length; index++ ) {
            const prop = props[index];
            let value = values[index];
            if( BOOL_OPS.has(op) ) value = Boolean(value);
            else if( prop === 'text' || prop === 'targetVerb' ) value = boundedText(prop, value ?? '');
            else if( prop === 'modelKind' ) value = modelKind(value);
            else value = finiteValue(prop, value);
            if( component.static[prop] === value ) continue;
            component.static[prop] = value;
            if( component.props ) component.props[prop] = value;
            /* Runtime component fields are authoritative until a later script
             * writes them. Do not let the declarative expression re-evaluate
             * over an imperative HOST setter in preview.layout. */
            component.dynamic = (component.dynamic || []).filter((binding) => binding.prop !== prop);
            changed[prop] = value;
        }
        if( Object.keys(changed).length === 0 ) return this.ref(component);
        return this._changed('component', component, { op, props: changed });
    }

    _supports(component, op) {
        const supported = OP_TYPES.get(op);
        /* The number supplied to cc_create is a widget type, while the C tree
         * maps unsupported values (including 0, 2 and 8) to UIELEM_CC_OBJ.
         * Keeping the wire value on component.type is useful to React callers,
         * but it must not grant a CC object layer/text-only setter semantics. */
        if( component.runtimeDynamic && component.kind === 'Object' ) return !supported;
        return !supported || supported.has(component.type);
    }

    _setInputField(component, field, value) {
        const existed = Boolean(component.runtime.input);
        const input = component.runtime.input ||= cloneInputState({ configured: true });
        const next = finiteValue(`input ${field}`, value);
        if( existed && input[field] === next ) return this.ref(component);
        input.configured = true;
        input[field] = next;
        return this._changed('component', component, {
            op: `if_input_${field}`, input: { [field]: next },
        });
    }

    setHook(value, eventName, binding) {
        return this._boundary(() => this._setHook(value, eventName, binding));
    }

    _setHook(value, eventName, binding) {
        const component = this._component(value ?? this.active);
        const descriptor = definition(eventName);
        const exact = exactHookKey(eventName, descriptor);
        component.hooks ||= {};
        for( const alias of hookAliases(descriptor) ) delete component.hooks[alias];
        if( binding && scriptId(binding) > 0 ) component.hooks[exact] = normalizeBinding(binding, this);
        return this._changed('hook', component, {
            hook: exact, canonical: descriptor.canonical, scriptId: scriptId(binding),
        });
    }

    /** Dynamic CC state. Returned refs remain valid until explicit deletion. */
    createChild(parentValue, type, subId, options = {}) {
        return this._boundary(() => this._createChild(parentValue, type, subId, options));
    }

    _createChild(parentValue, rawType, rawSubId, { dot = false } = {}) {
        const parent = this._component(parentValue);
        const type = boundedInteger('component type', rawType, 0, 255);
        const kind = TYPE_KIND.get(type) || 'Object';
        const subId = boundedInteger('child index', rawSubId, 0, 65535);
        const existing = this.findChild(parent, subId, false);
        if( existing ) this._delete(existing);
        if( this.dynamicCount >= this.limits.dynamicComponents )
            throw new HostRuntimeError('dynamic component limit reached', 'LIMIT');
        if( this.ir.components.length >= this.limits.components )
            throw new HostRuntimeError('component limit reached', 'LIMIT');
        const staticProps = dynamicProps(type, kind);
        const component = {
            fileId: `@host:${this.nextDynamic++}`,
            name: `${parent.name}[${subId}]`,
            kind, type, layer: parent.fileId, subId,
            props: staticProps, static: staticProps, authoredProps: new Set(), dynamic: [],
            ops: [], events: {}, hooks: {}, triggers: {}, dependencies: [],
            scriptBindings: [], rawFields: {}, runtimeDynamic: true,
            runtime: emptyRuntimeState(),
        };
        this.ir.components.push(component);
        this._indexDynamic(component, parent, subId);
        this.dynamicCount++;
        const ref = this._changed('create', component, { parent: this.ref(parent), type, subId });
        this.setActive(component, { dot });
        return ref;
    }

    _copyChild(parentValue, sourceSubId, destinationSubId, { dot = false } = {}) {
        const parent = this._component(parentValue);
        const sourceRef = this.findChild(parent, sourceSubId, false);
        if( !sourceRef ) throw new HostRuntimeError('dynamic source child was not found', 'BAD_REQUEST');
        const source = this._component(sourceRef);
        const created = this._createChild(parent, source.type, destinationSubId, { dot });
        const target = this._component(created);
        target.static = cloneRecord(source.static);
        target.props = target.static;
        target.dynamic = (source.dynamic || []).map((binding) => ({ ...binding }));
        target.ops = (source.ops || []).map((op) => ({ ...op }));
        target.events = { ...(source.events || {}) };
        target.hooks = Object.fromEntries(Object.entries(source.hooks || {}).map(([key, binding]) =>
            [key, binding && typeof binding === 'object'
                ? { ...binding, args: [...(binding.args || [])] } : binding]));
        target.triggers = Object.fromEntries(Object.entries(source.triggers || {}).map(([key, ids]) =>
            [key, Array.isArray(ids) ? [...ids] : ids]));
        target.runtime = cloneRuntimeState(source.runtime);
        const sourceMeta = this.meta.get(source);
        const targetMeta = this.meta.get(target);
        targetMeta.draggable = sourceMeta.draggable;
        targetMeta.dragParent = sourceMeta.dragParent;
        targetMeta.dragDeadZone = sourceMeta.dragDeadZone;
        targetMeta.dragDeadTime = sourceMeta.dragDeadTime;
        targetMeta.dragBehavior = sourceMeta.dragBehavior;
        this._record({ kind: 'copy', ref: created, source: sourceRef });
        return created;
    }

    findChild(parentValue, rawSubId, updateActive = true, { dot = false } = {}) {
        const parent = this._component(parentValue);
        const subId = boundedInteger('child index', rawSubId, 0, 65535);
        const found = this.ir.components.find((component) => component.layer === parent.fileId &&
            this.meta.get(component)?.dynamic && this.meta.get(component).subId === subId) || null;
        if( updateActive ) this.setActive(found, { dot });
        return found ? this.ref(found) : null;
    }

    /** Dynamic children in canonical ascending sub-id order. */
    children(parentValue, { startIndex = 0 } = {}) {
        const parent = this._component(parentValue);
        const start = boundedInteger('child start index', startIndex, -1, 65535);
        return this.ir.components
            .filter((component) => component.layer === parent.fileId &&
                this.meta.get(component)?.dynamic && this.meta.get(component).subId >= start)
            .sort((left, right) => this.meta.get(left).subId - this.meta.get(right).subId)
            .map((component) => this.ref(component));
    }

    delete(value) {
        return this._boundary(() => this._delete(value));
    }

    _delete(value) {
        const component = this._component(value);
        if( !this.meta.get(component).dynamic )
            throw new HostRuntimeError('only dynamic components can be deleted', 'BAD_REQUEST');
        return this._deleteSet(new Set([component]));
    }

    deleteAll(parentValue) {
        return this._boundary(() => {
            const parent = this._component(parentValue);
            const doomed = new Set(this.ir.components.filter((component) =>
                component.layer === parent.fileId && this.meta.get(component)?.dynamic));
            return this._deleteSet(doomed);
        });
    }

    _deleteSet(initial) {
        if( initial.size === 0 ) return [];
        const doomed = new Set(initial);
        let grew = true;
        while( grew ) {
            grew = false;
            for( const component of this.ir.components ) {
                const parent = this.byFileId.get(component.layer);
                if( parent && doomed.has(parent) && !doomed.has(component) ) {
                    doomed.add(component); grew = true;
                }
            }
        }
        const refs = [...doomed].map((component) => this.ref(component));
        this.ir.components = this.ir.components.filter((component) => !doomed.has(component));
        for( const component of doomed ) {
            const meta = this.meta.get(component);
            this.byKey.delete(meta.key);
            this.byName.delete(component.name);
            this.byFileId.delete(component.fileId);
            if( meta.componentId !== null && this.byUid.get(meta.componentId) === component )
                this.byUid.delete(meta.componentId);
            this.dynamicCount -= Number(meta.dynamic);
            this.meta.delete(component);
        }
        if( this.active && refs.some((ref) => sameRef(ref, this.active)) ) this.active = null;
        if( this.dotActive && refs.some((ref) => sameRef(ref, this.dotActive)) ) this.dotActive = null;
        this._record({ kind: 'delete', refs });
        this._retireDeletedInteraction(refs);
        return refs;
    }

    readState(kind, rawId) {
        if( !STATE_KINDS.has(kind) ) throw new HostRuntimeError(`unsupported state kind ${kind}`, 'BAD_REQUEST');
        const id = stateId(rawId);
        const key = kind === 'inv' ? `invobj:${id}` : `${kind}:${id}`;
        if( key in this.state ) return cloneValue(this.state[key]);
        /* VarCManager_GetInt is deliberately -1 for an unset slot. A zero here
         * is observable control flow, not a harmless placeholder: bank tags,
         * chat tabs, and other cache scripts use -1 as their absent sentinel. */
        return kind === 'inv' ? {} : kind === 'varcstr' ? '' : kind === 'varc' ? -1
            : kind === 'stat' ? 1 : 0;
    }

    /** Write state and synchronously fan out its matching visible transmit hook. */
    writeState(kind, rawId, value, options = {}) {
        return this._boundary(() => {
            if( !STATE_KINDS.has(kind) )
                throw new HostRuntimeError(`unsupported state kind ${kind}`, 'BAD_REQUEST');
            const id = stateId(rawId);
            const key = kind === 'inv' ? `invobj:${id}` : `${kind}:${id}`;
            const next = kind === 'inv' ? inventoryState(value)
                : kind === 'varcstr' ? boundedText(key, value ?? '') : finiteValue(key, value);
            this.state[key] = next;
            this._record({ kind: 'state', key, value: cloneValue(next) });
            const intents = [];
            if( options.transmit !== false ) {
                const descriptor = transmitDefinition(kind);
                if( descriptor ) {
                    const trigger = stateId(options.trigger ?? id);
                    const targets = this._hookTargets(descriptor, this.limits.keyTargets);
                    for( const ref of targets ) {
                        const component = this._component(ref, false);
                        if( !component ) continue;
                        const resolved = this._resolveHook(component, descriptor);
                        if( resolved && transmitMatches(component, kind, trigger, resolved.binding) )
                            this._emit(component, resolved,
                                baseEvent('transmit', { kind, id, trigger }), {}, intents);
                    }
                }
            }
            return this._result(intents, { key, value: next });
        });
    }

    /**
     * Execute one named HOST operation. Requests are ordinary JS records and
     * results are ordinary JS values; component refs preserve dynamic identity.
     */
    request(kindOrRequest, payload = {}) {
        const supplied = typeof kindOrRequest === 'object' && kindOrRequest
            ? kindOrRequest : { ...payload, kind: kindOrRequest };
        if( Array.isArray(supplied) )
            throw new HostRuntimeError('host request must be an object', 'BAD_REQUEST');
        if( supplied.fields !== undefined )
            throw new HostRuntimeError('host request fields must be top-level', 'BAD_REQUEST');
        const request = { ...supplied };
        const kind = normalizeRequestKind(request.kind);
        if( !supportsHostRequest(kind) ) throw new HostRuntimeError(
            COMMAND_NAMES.has(kind)
                ? `${kind} is explicitly unsupported by the UITree/HOST runtime`
                : `unknown host request ${kind}`,
            'UNSUPPORTED');
        return this._boundary(() => this._request(kind, request));
    }

    _request(kind, request) {
        request._kind = kind;
        if( STATE_READ_REQUEST[kind] )
            return this.readState(STATE_READ_REQUEST[kind], requestField(request,
                'id', 'varp', 'varbit', 'varc', 'stat', 'varp_id', 'varbit_id', 'varc_id',
                'varpId', 'varbitId', 'varcId'));
        if( STATE_WRITE_REQUEST[kind] )
            return this.writeState(STATE_WRITE_REQUEST[kind],
                requestField(request, 'id', 'varp', 'varbit', 'varc', 'varp_id', 'varbit_id', 'varc_id',
                    'varpId', 'varbitId', 'varcId'),
                requestField(request, 'value', 'text'),
                { transmit: request.transmit !== false });
        if( kind === 'INVS_GET_NUM' || kind === 'INV_TOTAL' )
            return HOST_READS.inv_getnum.evaluate(request.args || [
                requestField(request, 'inv_id', 'inventory_id'),
                requestField(request, 'item_id', 'obj_id'),
            ], this.state);
        if( kind === 'INVS_GET_TOTAL' )
            return HOST_READS.inv_total.evaluate(request.args || [
                requestField(request, 'inv_id', 'inventory_id'),
            ], this.state);
        if( kind === 'INVS_GET_SIZE' || kind === 'INV_SIZE' )
            return HOST_READS.inv_size.evaluate(request.args || [
                requestField(request, 'inv_id', 'inventory_id'),
            ], this.state);
        if( kind === 'CLIENTCLOCK' ) return HOST_READS.clientclock.evaluate([], this.state);
        if( kind === 'SOUND_SYNTH' ) {
            const sound = soundSynthIntent(request);
            this.services.soundSynthCount++;
            this.services.lastSoundSynth = sound;
            this._record({ kind: 'service', service: 'sound_synth', sound });
            return null;
        }
        if( kind === 'CLIENTTYPE' ) return this.clientType;
        if( kind === 'MAP_MEMBERS' ) return this.mapMembers ? 1 : 0;
        if( kind === 'ON_MOBILE' ) return 0;
        if( kind === 'RUNENERGY_VISIBLE' ) return finiteOptional(this.state.runenergy, 0);
        if( kind === 'RUNWEIGHT_VISIBLE' ) return finiteOptional(this.state.runweight, 0);
        if( kind === 'ENUM' || kind === 'ENUM_STRING' ) return this._enumLookup(kind, request);
        if( kind === 'ENUM_GETOUTPUTCOUNT' ) return this._enumOutputCount(request);
        if( kind === 'PARAWIDTH' || kind === 'PARAHEIGHT' )
            return this._paragraphMeasure(kind, request);
        if( kind === 'STRUCT_PARAM' ) return this._structParam(request);
        if( kind === 'OC_PARAM' ) return this._entityParam('objects', request,
            request.itemId ?? request.item_id ?? request.id ?? request.args?.[0]);
        if( kind === 'NC_PARAM' || kind === 'LC_PARAM' ) return this._entityParam(
            kind === 'NC_PARAM' ? 'npcs' : 'locs', request,
            request.typeId ?? request.type_id ?? request.id ?? request.args?.[0]);
        if( kind.startsWith('OC_') ) return this._objectRead(kind, request);
        if( kind === 'INV_GETOBJ' || kind === 'INV_GETNUM' ) {
            const invId = stateId(requestField(request, 'inv_id', 'inventory_id'));
            const slot = boundedInteger('inventory slot', requestField(request, 'slot'), 0, 65535);
            const entry = this.state[`invslots:${invId}`]?.[slot];
            if( kind === 'INV_GETOBJ' ) return Number(entry?.id ?? entry?.objectId ?? -1);
            return Number(entry?.count ?? 0);
        }

        if( kind === 'CC_CREATE' || kind === 'CC_CREATECHILD' || kind === 'CC_CREATESIBLING' ) {
            let parent = this._component(targetOf(request, this, 'parent_id'));
            if( kind === 'CC_CREATESIBLING' || request.parentIsSibling || request.parent_is_sibling ) {
                parent = this.byFileId.get(parent.layer) || null;
                if( !parent ) throw new HostRuntimeError('sibling has no parent', 'BAD_REQUEST');
            }
            return this._createChild(parent,
                requestField(request, 'component_type', 'componentType', 'type'),
                requestField(request, 'child_index', 'childIndex', 'sub_id', 'subId'),
                { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        }
        if( kind === 'CC_FIND' ) return this.findChild(targetOf(request, this, 'parent_id'),
            requestField(request, 'sub_id', 'subId', 'child_index', 'childIndex'), true,
            { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        if( kind === 'IF_FIND' ) {
            const found = this._component(targetOf(request, this), false);
            this.setActive(found, { dot: Boolean(request.dot_operand ?? request.dotOperand) });
            return found ? this.ref(found) : null;
        }
        if( kind === 'CC_COPY' ) return this._copyChild(
            targetOf(request, this, 'parent_id'),
            requestField(request, 'src_sub_id', 'srcSubId'),
            requestField(request, 'dst_sub_id', 'dstSubId'),
            { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        if( kind === 'CC_DELETE' ) return this._delete(targetOf(request, this));
        if( kind === 'CC_DELETEALL' ) return this.deleteAll(targetOf(request, this));
        if( kind === 'IF_CHILDREN_FIND' || kind === 'IF_CHILDREN_COLLECT' ||
            kind === 'CC_CHILDREN_FIND_COUNT' ) {
            const parent = targetOf(request, this, kind.startsWith('IF_') ? 'uid' : 'parent_id');
            const refs = this.children(parent, {
                startIndex: request.startIndex ?? request.start_index ?? 0,
            });
            this.childIteration = { parent: this.ref(parent), refs, index: 0 };
            if( kind.startsWith('IF_') )
                this.setActive(parent, { dot: Boolean(request.dotOperand ?? request.dot_operand) });
            return kind === 'CC_CHILDREN_FIND_COUNT' ? refs.length : refs;
        }
        if( kind === 'CC_CHILDREN_FINDNEXT' ) {
            if( request.subId !== undefined || request.sub_id !== undefined )
                return this.findChild(targetOf(request, this, 'parent_id'),
                    request.subId ?? request.sub_id, true,
                    { dot: Boolean(request.dotOperand ?? request.dot_operand) });
            const ref = this.childIteration.refs[this.childIteration.index++] || null;
            if( ref ) this.setActive(ref, { dot: Boolean(request.dotOperand ?? request.dot_operand) });
            return ref;
        }
        if( kind === 'CHILDREN_FINDNEXTID' || kind === 'CC_CHILDREN_FINDNEXTID' ||
            kind === 'IF_CHILDREN_FINDNEXTID' ) {
            const ref = this.childIteration.refs[this.childIteration.index++] || null;
            return ref ? ref.subId : -1;
        }
        if( kind === 'CC_PARENTID' ) {
            const component = this._component(targetOf(request, this));
            const parent = this.byFileId.get(component.layer) || null;
            return parent ? this.ref(parent) : null;
        }
        if( kind === 'IF_GETTOP' ) return this.interfaceId;

        const prefix = /^(CC|IF)_(.+)$/.exec(kind);
        if( !prefix ) throw new HostRuntimeError(`unsupported host request ${kind}`, 'UNSUPPORTED');
        const suffix = prefix[2];
        const target = targetOf(request, this);
        if( suffix === 'ASSERT' || suffix === 'OP1309' || suffix === 'OP2309' )
            return null;
        if( suffix === 'CRMVIEW_DISMISS' ) {
            this.services.crmViewDismissals++;
            this._record({ kind: 'service', service: 'crm_view_dismiss' });
            /* Desktop C host has no CRM service and the inherited VM signature
             * answers zero. Keep that deterministic result while exposing the
             * request in preview state. */
            return 0;
        }
        if( suffix === 'CLOSE' ) {
            if( !this.services.closeModalRequested ) {
                this.services.closeModalRequested = true;
                this._record({ kind: 'service', service: 'close_modal' });
            }
            return null;
        }
        if( suffix === 'RESUME_PAUSEBUTTON' ) {
            const component = this._component(target, false);
            const value = component ? this.ref(component) : cloneValue(target);
            if( !sameServiceTarget(this.services.resumePauseButton, value) ) {
                this.services.resumePauseButton = value;
                this._record({ kind: 'service', service: 'resume_pausebutton', component: value });
            }
            return null;
        }
        if( suffix === 'HASSUB' ) return this._interfaceParentGroup(target) === null ? 0 : 1;
        if( suffix === 'HASCHILD_OVERLAY' ) {
            const groupId = finiteValue('interface child group',
                request.groupId ?? request.group_id ?? request.values?.[0] ?? request.args?.[1]);
            return this._interfaceParentGroup(target) === groupId ? 1 : 0;
        }
        if( suffix === 'DRAGPICKUP' ) return this._dragPickup(target,
            request.pickupX ?? request.pickup_x ?? request.values?.[0] ?? request.args?.[0],
            request.pickupY ?? request.pickup_y ?? request.values?.[1] ?? request.args?.[1]);
        if( suffix === 'FIND_PARAM' ) return this._findParam(request);
        if( suffix === 'GETPARAM' ) return this._structParam(request);
        if( suffix === 'SETHTTPSPRITE' ) return this._mutate('if_sethttpsprite', target,
            [request.url ?? request.text ?? request.values?.[0] ?? request.args?.[0] ?? '']);
        if( INPUT_GETTERS[suffix] ) {
            const input = this._component(target).runtime.input || cloneInputState({});
            return INPUT_GETTERS[suffix] === 'focused'
                ? Boolean(input.focused) : input[INPUT_GETTERS[suffix]];
        }
        if( INPUT_SETTERS[suffix] ) return this._setInputField(
            this._component(target), INPUT_SETTERS[suffix], requestField(request, 'value'));
        if( suffix === 'GETCOMPONENTPARAM' ) {
            const component = this._component(target);
            const paramId = stateId(requestField(request, 'paramId', 'param_id'));
            const entry = component.runtime.params[paramId];
            if( entry && Object.hasOwn(entry, 'value') ) return entry.value;
            if( prefix[1] === 'IF' ) return finiteValue('component parameter default',
                request.value ?? request.defaultValue ?? 0);
            return finiteValue('component parameter default', this.paramDefault(paramId, this) ?? 0);
        }
        if( REQUEST_GETTERS[suffix] )
            return this.read(REQUEST_GETTERS[suffix], target,
                suffix === 'GETOP' ? requestField(request, 'index', 'op_index', 'opIndex') : null);
        if( suffix === 'SETOP' ) return this._mutate('if_setop', target,
            [requestField(request, 'index', 'op_index'), requestField(request, 'text')]);
        if( suffix === 'SETOBJECT' || suffix === 'SETOBJECT_NONUM' || suffix === 'SETOBJECT_ALWAYS_NUM' )
            return this._mutate('if_setobject', target, [
                request.obj_id ?? request.objectId,
                request.count ?? 0,
                request.num_mode ?? request.numberMode ??
                    (suffix === 'SETOBJECT_ALWAYS_NUM' ? 1 : suffix === 'SETOBJECT_NONUM' ? 2 : 0),
            ]);
        if( suffix === 'SETNPCHEAD' ) return this._mutate('if_setmodelsource', target,
            ['npcHead', requestField(request, 'modelId', 'model_id', 'npcId', 'npc_id')]);
        if( suffix === 'SETPLAYERHEAD_SELF' ) return this._mutate('if_setmodelsource', target,
            ['playerHead', -1]);
        if( suffix === 'SETPLAYERMODEL_SELF' ) return this._mutate('if_setmodelsource', target,
            ['playerSelf', -1]);
        if( suffix === 'SETMODEL_PLAYERCHATHEAD' ) return this._mutate('if_setmodelsource', target,
            ['playerChatHead', -1]);
        if( suffix === 'SETLOCMODEL' ) return this._mutate('if_setmodelsource', target,
            ['locModel', requestField(request, 'locId', 'loc_id', 'modelId', 'model_id', 'id', 'value')]);
        if( suffix === 'SETNPCMODEL' ) return this._mutate('if_setmodelsource', target,
            ['npcModel', requestField(request, 'npcId', 'npc_id', 'modelId', 'model_id', 'id', 'value')]);
        if( suffix === 'SETDRAGGABLE' ) {
            let dragParent = request.dragParent ?? request.parent ?? request.ref_parent ??
                request.parent_ref ?? request.parentId ?? request.parent_uid ?? null;
            const rawChildIndex = request.childIndex ?? request.child_index;
            const childIndex = rawChildIndex === undefined
                ? -1 : finiteValue('drag render-area child index', rawChildIndex);
            /* The C HOST eagerly resolves parent.children[child_index] only
             * for a non-negative, representable dynamic slot.  -1 is the
             * canonical sentinel meaning that parent_uid already names the
             * render area; an unavailable slot likewise falls back to the
             * parent instead of rejecting the request. */
            if( typeof dragParent === 'number' && dragParent < 0 ) dragParent = null;
            if( dragParent !== null && dragParent !== undefined &&
                childIndex >= 0 && childIndex <= 0xffff )
                dragParent = this.findChild(dragParent, childIndex, false) || dragParent;
            return this._mutate('if_setdraggable', target, [true, dragParent]);
        }
        if( suffix === 'SETDRAGDEADZONE' ) return this._mutate('if_setdragdeadzone', target,
            [requestField(request, 'zone', 'value')]);
        if( suffix === 'SETDRAGDEADTIME' ) return this._mutate('if_setdragdeadtime', target,
            [requestField(request, 'time', 'value')]);
        if( suffix === 'SETDRAGGABLEBEHAVIOR' ) return this._mutate('if_setdraggablebehavior', target,
            [request.behavior ?? request.value ?? request.values?.[0] ?? request.args?.[0] ?? 0]);
        if( suffix === 'SETOPBASE' ) return this._mutate('if_setopbase', target,
            [request.text ?? request.values?.[0] ?? request.args?.[0] ?? '']);
        if( suffix === 'CLEAROPS' ) return this._mutate('if_clearops', target, []);
        if( suffix === 'SETOPSUBMENU' ) return this._mutate('if_setopsubmenu', target, [
            requestField(request, 'opIndex', 'op_index'),
            requestField(request, 'subIndex', 'sub_index'),
            requestField(request, 'text'),
        ]);
        if( suffix === 'CLEAROPSUBMENU' ) return this._mutate('if_clearopsubmenu', target,
            [requestField(request, 'opIndex', 'op_index')]);
        if( suffix === 'SETTARGETPRIORITY' ) return this._mutate('if_settargetpriority', target,
            [requestField(request, 'priority', 'value')]);
        if( suffix === 'SETCOMPONENTPARAM' || suffix === 'SETPARAM' )
            return this._mutate('if_setcomponentparam', target, [
                requestField(request, 'paramId', 'param_id'),
                request.strValue ?? request.str_value ?? request.value,
            ]);
        if( suffix === 'SETOPKEY' || suffix === 'SETOPTKEY' ) {
            const opIndex = suffix === 'SETOPTKEY' ? 10
                : requestField(request, 'opIndex', 'op_index', 'index');
            return this._mutate('if_setopkey', target, [
                opIndex, request.keyChars ?? request.key_chars ?? [],
                request.keyCodes ?? request.key_codes ?? [],
            ]);
        }
        if( suffix === 'SETOPKEYRATE' || suffix === 'SETOPTKEYRATE' )
            return this._mutate('if_setopkeyrate', target, [
                suffix === 'SETOPTKEYRATE' ? 10 : requestField(request, 'opIndex', 'op_index', 'index'),
                requestField(request, 'rate'), request.enabled ?? true,
            ]);
        if( suffix === 'SETOPKEYIGNOREHELD' || suffix === 'SETOPTKEYIGNOREHELD' )
            return this._mutate('if_setopkeyignoreheld', target, [
                suffix === 'SETOPTKEYIGNOREHELD' ? 10
                    : requestField(request, 'opIndex', 'op_index', 'index'),
            ]);
        if( suffix === 'TRIGGEROP' || suffix === 'TRIGGEROPLOCAL' )
            return this.trigger(target, 'on_op', {
                opIndex: requestField(request, 'opIndex', 'op_index', 'sub'),
            });
        if( suffix === 'CALLONRESIZE' ) return this.trigger(target, 'on_resize');
        if( suffix.startsWith('INPUT_SETON') ) {
            const descriptor = definition(`on_${suffix.slice('INPUT_SETON'.length).toLowerCase()}`);
            return this._setHook(target, descriptor.canonical, hookFromRequest(request));
        }
        if( suffix.startsWith('SETON') ) {
            const descriptor = definition(setOnEvent(suffix));
            return this._setHook(target, descriptor.canonical, hookFromRequest(request));
        }
        const setter = REQUEST_SETTERS[suffix];
        if( !setter ) throw new HostRuntimeError(`unsupported host request ${kind}`, 'UNSUPPORTED');
        const values = requestValues(request, setter[1]);
        return this._mutate(setter[0], target, values);
    }

    _enumLookup(kind, request) {
        const args = request.args || request.values || [];
        const enumId = finiteValue('enum id', request.enumId ?? request.enum_id ??
            (kind === 'ENUM' ? args[2] : args[0]));
        const key = finiteValue('enum key', request.key ??
            (kind === 'ENUM' ? args[3] : args[1]));
        const outputType = kind === 'ENUM_STRING' ? 115
            : finiteValue('enum output type', request.outputType ?? request.output_type ?? args[1]);
        const entry = this.hostData.enums[String(enumId)] || null;

        /* An unavailable enum is distinct from an available empty enum. The C
         * client answers the former with "null"/-1; the latter uses the record's
         * own default (zero/"null" when no explicit default was encoded). */
        if( !entry ) return outputType === 115 ? 'null' : -1;
        const stringResult = outputType === 115 || Boolean(entry.string);
        const values = entry.values || {};
        if( Object.prototype.hasOwnProperty.call(values, String(key)) )
            return stringResult ? String(values[String(key)])
                : finiteValue('enum value', values[String(key)]);
        return stringResult ? String(entry.defaultString ?? 'null')
            : finiteValue('enum default', entry.defaultInt ?? 0);
    }

    _structParam(request) {
        const args = request.args || request.values || [];
        const structId = finiteValue('struct id',
            request.structId ?? request.struct_id ?? args[0] ?? -1);
        const paramId = finiteValue('parameter id',
            request.paramId ?? request.param_id ?? args[1] ?? -1);
        const param = this.hostData.params[String(paramId)] || null;
        const struct = this.hostData.structs[String(structId)] || null;
        const values = struct?.params || struct?.values || struct || {};
        const present = Object.prototype.hasOwnProperty.call(values, String(paramId));
        const entry = present ? values[String(paramId)] : undefined;
        const stringParam = Boolean(param?.string ?? param?.isString ?? param?.is_string);
        if( present ) {
            if( entry && typeof entry === 'object' && !Array.isArray(entry) ) {
                if( Object.hasOwn(entry, 'string') ) return String(entry.string ?? '');
                if( Object.hasOwn(entry, 'value') )
                    return stringParam
                        ? typeof entry.value === 'string'
                            ? String(entry.value)
                            : String(param?.defaultString ?? param?.default_string ?? '')
                        : typeof entry.value === 'string'
                            ? String(entry.value)
                            : finiteValue('struct parameter', entry.value);
            }
            if( typeof entry === 'string' ) return entry;
            if( stringParam )
                return String(param?.defaultString ?? param?.default_string ?? '');
            return finiteValue('struct parameter', entry);
        }
        if( stringParam ) return String(param?.defaultString ?? param?.default_string ?? '');
        const fallback = param?.defaultInt ?? param?.default_int ?? this.paramDefault(paramId, this) ?? 0;
        return finiteValue('struct parameter default', fallback);
    }

    _entityParam(collection, request, rawEntityId) {
        const args = request.args || request.values || [];
        const paramId = finiteValue('parameter id',
            request.paramId ?? request.param_id ?? args[1] ?? -1);
        const entityId = finiteValue(`${collection} id`, rawEntityId ?? -1);
        const param = this.hostData.params[String(paramId)] || null;
        const entity = entityId >= 0 ? this.hostData[collection][String(entityId)] || null : null;
        const values = entity?.params || entity?.parameters || {};
        const present = Object.prototype.hasOwnProperty.call(values, String(paramId));
        const entry = present ? values[String(paramId)] : undefined;
        const stringParam = Boolean(param?.string ?? param?.isString ?? param?.is_string);
        if( present ) {
            const stringValue = entry && typeof entry === 'object' && !Array.isArray(entry)
                ? entry.string ?? (typeof entry.value === 'string' ? entry.value : undefined)
                : typeof entry === 'string' ? entry : undefined;
            const intValue = entry && typeof entry === 'object' && !Array.isArray(entry)
                ? entry.value ?? entry.int ?? entry.intValue ?? entry.int_value : entry;
            if( stringParam && stringValue !== undefined ) return String(stringValue ?? '');
            if( !stringParam && typeof intValue !== 'string' && intValue !== undefined )
                return finiteValue('entity parameter', intValue);
        }
        if( stringParam ) return String(param?.defaultString ?? param?.default_string ?? '');
        return finiteValue('entity parameter default', param?.defaultInt ?? param?.default_int ?? 0);
    }

    _findParam(request) {
        const raw = request.args || request.values || [];
        let rootValue = request.root ?? request.root_id ?? request.componentRoot;
        let criteria = request.criteria;
        if( !Array.isArray(criteria) ) {
            if( raw.length < 5 ) return null;
            const firstType = finiteValue('first parameter selector', raw[raw.length - 2]);
            const secondType = finiteValue('second parameter selector', raw[raw.length - 1]);
            let at = 0;
            rootValue ??= raw[at++];
            criteria = [];
            const firstParam = raw[at++];
            if( firstType !== -1 ) criteria.push({
                paramId: firstParam, type: firstType, value: raw[at++],
            });
            else criteria.push(null);
            const secondParam = raw[at++];
            if( secondType !== -1 ) criteria.push({
                paramId: secondParam, type: secondType, value: raw[at++],
            });
            else criteria.push(null);
        }
        const root = this._component(rootValue, false);
        if( !root ) {
            this.setActive(null, { dot: Boolean(request.dot_operand ?? request.dotOperand) });
            return null;
        }
        const matches = (component) => criteria.filter(Boolean).every((criterion) => {
            const paramId = finiteValue('component parameter id',
                criterion.paramId ?? criterion.param_id);
            const entry = component.runtime?.params?.[paramId];
            if( !entry ) return false;
            const type = finiteValue('component parameter type', criterion.type ?? 0);
            if( type === 2 || type === 115 )
                return String(entry.string ?? entry.value ?? '') === String(criterion.value ?? '');
            return finiteOptional(entry.value, 0) === finiteValue('component parameter value', criterion.value);
        });
        const pending = [root];
        let found = null;
        while( pending.length && !found ) {
            const component = pending.shift();
            if( matches(component) ) { found = component; break; }
            for( const child of this.ir.components )
                if( child.layer === component.fileId ) pending.push(child);
        }
        this.setActive(found, { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        return found ? this.ref(found) : null;
    }

    _interfaceParentGroup(target) {
        const component = this._component(target, false);
        const candidates = [];
        if( component ) {
            const ref = this.ref(component);
            candidates.push(ref.key, ref.componentId, ref.fileId, ref.name);
        } else candidates.push(target);
        for( const candidate of candidates ) {
            const key = String(candidate);
            if( !this.interfaceParents.has(key) ) continue;
            const entry = this.interfaceParents.get(key);
            return finiteValue('interface parent group', entry?.groupId ?? entry?.group_id ?? entry);
        }
        return null;
    }

    _dragPickup(target, rawX, rawY) {
        const component = this._component(target, false);
        if( !component || this.interaction.dragging ) return this._result([]);
        const meta = this.meta.get(component);
        const dragDepth = finiteOptional(component.static?.clickMask, 0) >>> 17 & 7;
        if( !meta.draggable && !meta.dragParent && dragDepth === 0 ) return this._result([]);
        const pickupX = finiteValue('drag pickup x', rawX ?? 0);
        const pickupY = finiteValue('drag pickup y', rawY ?? 0);
        const ref = this.ref(component);
        this.interaction.pressed = ref;
        this.interaction.button = 0;
        this.interaction.pressX = this.interaction.x;
        this.interaction.pressY = this.interaction.y;
        this.interaction.pressCycle = this.cycle;
        this.interaction.clickFired = true;
        this.interaction.dragging = true;
        this.interaction.dragPickupX = pickupX;
        this.interaction.dragPickupY = pickupY;
        const intents = [];
        if( this._visible(component) ) this._emitNamed(component, 'on_drag',
            baseEvent('drag_pickup', { x: this.interaction.x, y: this.interaction.y }), intents,
            { dragTarget: this._hit(this.interaction.x, this.interaction.y) });
        return this._result(intents, { pickup: ref });
    }

    _enumOutputCount(request) {
        const args = request.args || request.values || [];
        const enumId = finiteValue('enum id', request.enumId ?? request.enum_id ?? args[0]);
        const entry = this.hostData.enums[String(enumId)] || null;
        return entry ? Object.keys(entry.values || {}).length : 0;
    }

    _paragraphMeasure(kind, request) {
        const args = request.args || request.values || [];
        const text = boundedText('paragraph text', request.text ?? args[0] ?? '');
        const maxWidth = Math.max(0, finiteValue('paragraph width',
            request.maxWidth ?? request.max_width ?? args[1] ?? 0));
        const fontId = finiteValue('font id', request.fontId ?? request.font_id ?? args[2] ?? 0);
        const font = this.hostData.fonts[String(fontId)] || null;
        if( !text || !font ) return 0;
        const measured = measureParagraph(font, text, maxWidth);
        return kind === 'PARAWIDTH' ? measured.width : measured.lines;
    }

    _objectRead(kind, request) {
        const args = request.args || request.values || [];
        const itemId = finiteValue('object id',
            request.itemId ?? request.item_id ?? request.id ?? args[0] ?? -1);
        if( kind === 'OC_PLACEHOLDER' || kind === 'OC_UNPLACEHOLDER' ) {
            if( itemId < 0 ) return itemId;
            const object = this.hostData.objects[String(itemId)] || null;
            if( !object ) return itemId;
            const link = finiteOptional(object.placeholderLink ?? object.placeholder_link, -1);
            if( link > 0 ) {
                const template = finiteOptional(
                    object.placeholderTemplate ?? object.placeholder_template, -1);
                const isPlaceholder = template >= 0;
                const wantPlaceholder = kind === 'OC_PLACEHOLDER';
                if( isPlaceholder !== wantPlaceholder ) return link;
            }
            return itemId;
        }
        const object = itemId >= 0 ? this.hostData.objects[String(itemId)] || null : null;
        if( kind === 'OC_OP' || kind === 'OC_IOP' ) {
            const opIndex = finiteValue('object operation index',
                request.opIndex ?? request.op_index ?? args[1] ?? -1);
            if( !object || opIndex < 0 || opIndex >= 5 ) return '';
            const actions = kind === 'OC_IOP'
                ? object.invActions ?? object.inv_actions ?? object.inventoryActions ??
                    object.inventory_actions ?? object.inventoryOps ?? object.inventory_ops ?? []
                : object.groundActions ?? object.ground_actions ?? object.groundOps ??
                    object.ground_ops ?? [];
            return String(actions?.[opIndex] ?? actions?.[String(opIndex)] ?? '');
        }
        if( kind === 'OC_NAME' ) return object?.name ? String(object.name) : 'null';
        if( kind === 'OC_COST' ) return finiteOptional(object?.cost, 0);
        if( kind === 'OC_STACKABLE' ) return finiteOptional(object?.stackable, 0);
        if( kind === 'OC_MEMBERS' ) return object?.members ? 1 : 0;
        /* The present C host models OC_CERT/OC_UNCERT as its identity field. */
        if( kind === 'OC_CERT' || kind === 'OC_UNCERT' ) return itemId < 0 ? 0 : itemId;
        throw new HostRuntimeError(`unsupported object request ${kind}`, 'UNSUPPORTED');
    }

    /** Synchronously invoke a registered component hook by semantic name. */
    trigger(value, eventName, locals = {}) {
        return this._boundary(() => {
            const component = this._component(value);
            const descriptor = definition(eventName);
            const intents = [];
            if( this._visible(component) ) {
                const resolved = this._resolveHook(component, descriptor);
                if( resolved ) this._emit(component, resolved,
                    baseEvent('trigger', {
                        opIndex: locals.opIndex,
                        keyTyped: locals.keyTyped,
                        keyPressed: locals.keyPressed,
                    }), locals, intents);
            }
            return this._result(intents);
        });
    }

    /** Dispatch one browser-normalized event. Browser focus is intentionally absent. */
    dispatch(rawEvent) {
        return this._boundary(() => {
            if( this.dispatchDepth ) throw new HostRuntimeError('nested input dispatch is not allowed', 'REENTRANT');
            const input = validateInput(rawEvent, this.viewport);
            this.dispatchDepth++;
            this.epoch++;
            const intents = [];
            let extra = {};
            try {
                switch( input.type ) {
                    case 'pointer_move': this._pointerMove(input, intents); break;
                    case 'pointer_down': extra = this._pointerDown(input, intents); break;
                    case 'pointer_up': this._pointerUp(input, intents); break;
                    case 'wheel': this._wheel(input, intents); break;
                    case 'key': this._key(input, definition('on_key'), intents); break;
                    case 'key_down':
                        this.interaction.heldKeys.add(input.keyTyped);
                        this._key(input, definition('on_key_down'), intents); break;
                    case 'key_up':
                        this.interaction.heldKeys.delete(input.keyTyped);
                        this._key(input, definition('on_key_up'), intents); break;
                    case 'op': this._op(input, intents); break;
                    case 'tick': this._tick(input, intents); break;
                    case 'focus_lost': this._focusLost(); break;
                    default: throw new HostRuntimeError(`unsupported input ${input.type}`, 'BAD_INPUT');
                }
            } finally { this.dispatchDepth--; }
            return this._result(intents, extra);
        });
    }

    _pointerMove(input, intents) {
        this._setPointer(input);
        this._hover(intents, input);
        const pressed = this._component(this.interaction.pressed, false);
        if( !pressed || this.interaction.button !== 0 ) return;
        const meta = this.meta.get(pressed);
        const moved = Math.max(
            Math.abs(input.x - this.interaction.pressX), Math.abs(input.y - this.interaction.pressY));
        const elapsed = this.cycle - this.interaction.pressCycle;
        if( !this.interaction.dragging && meta.draggable && moved > meta.dragDeadZone && elapsed >= meta.dragDeadTime )
            this.interaction.dragging = true;
        if( this.interaction.dragging ) this._emitNamed(pressed, 'on_drag', input, intents,
            { dragTarget: this._hit(input.x, input.y) });
    }

    _pointerDown(input, intents) {
        this._setPointer(input);
        this._hover(intents, input);
        if( input.button === 2 ) return { menu: this.menuAt(input.x, input.y) };
        if( input.button !== 0 ) return {};
        const hit = this._hit(input.x, input.y);
        this.interaction.pressed = hit ? this.ref(hit) : null;
        this.interaction.button = 0;
        this.interaction.pressX = input.x;
        this.interaction.pressY = input.y;
        this.interaction.pressCycle = this.cycle;
        this.interaction.clickFired = false;
        this.interaction.dragging = false;
        if( hit && !this.meta.get(hit).draggable ) {
            this._click(hit, input, intents);
            this.interaction.clickFired = true;
        }
        return { hit: hit ? this.ref(hit) : null };
    }

    _pointerUp(input, intents) {
        this._setPointer(input);
        this._hover(intents, input);
        if( input.button !== 0 ) return;
        const pressedRef = this.interaction.pressed;
        const pressed = this._component(pressedRef, false);
        const wasDragging = this.interaction.dragging;
        const hit = this._hit(input.x, input.y);
        if( pressed ) {
            if( wasDragging ) this._emitNamed(pressed, 'on_drag_complete', input, intents,
                { dragTarget: hit ? this.ref(hit) : null });
            this._emitNamed(pressed, 'on_release', input, intents);
            if( !wasDragging && !this.interaction.clickFired && hit && sameRef(this.ref(hit), pressedRef) )
                this._click(hit, input, intents);
        }
        this.interaction.pressed = null;
        this.interaction.button = null;
        this.interaction.clickFired = false;
        this.interaction.dragging = false;
        this.interaction.dragPickupX = 0;
        this.interaction.dragPickupY = 0;
    }

    _wheel(input, intents) {
        this._setPointer(input);
        this._hover(intents, input);
        const leaf = this._geometricHit(input.x, input.y);
        const resolved = leaf ? this._resolveAncestorHook(leaf, definition('on_scroll_wheel')) : null;
        if( resolved ) this._emit(resolved.component, resolved.hook, input,
            { wheel: input.wheel }, intents);
    }

    _key(input, descriptor, intents) {
        /* Snapshot targets, then fence every dispatch by generation. Earlier
         * key hooks may synchronously delete/recreate a later target. */
        const targets = this._hookTargets(descriptor, this.limits.keyTargets);
        for( const ref of targets ) {
            const component = this._component(ref, false);
            if( !component || !this._visible(component) ) continue;
            const resolved = this._resolveHook(component, descriptor);
            if( resolved ) this._emit(component, resolved, input, {
                keyTyped: input.keyTyped, keyPressed: input.keyPressed,
            }, intents);
        }
    }

    _op(input, intents) {
        const component = this._component(input.target);
        if( !this._visible(component) ) return;
        const resolved = this._resolveHook(component, definition('on_op'));
        if( resolved ) this._emit(component, resolved, input, { opIndex: input.opIndex }, intents);
    }

    _tick(input, intents) {
        this.cycle = input.cycle ?? this.cycle + 1;
        const hover = this._component(this.interaction.hover, false);
        if( hover && this._visible(hover) ) this._emitNamed(hover, 'on_mouse_repeat', input, intents);
        const pressed = this._component(this.interaction.pressed, false);
        if( pressed && this.interaction.button === 0 ) {
            const synthetic = { ...input, x: this.interaction.x, y: this.interaction.y };
            const meta = this.meta.get(pressed);
            const moved = Math.max(Math.abs(this.interaction.x - this.interaction.pressX),
                Math.abs(this.interaction.y - this.interaction.pressY));
            if( !this.interaction.dragging && meta.draggable && moved > meta.dragDeadZone &&
                this.cycle - this.interaction.pressCycle >= meta.dragDeadTime )
                this.interaction.dragging = true;
            if( this.interaction.dragging ) this._emitNamed(pressed, 'on_drag', synthetic, intents,
                { dragTarget: this._hit(this.interaction.x, this.interaction.y) });
            else {
                this._emitNamed(pressed, 'on_hold', synthetic, intents);
                this._emitNamed(pressed, 'on_click_repeat', synthetic, intents);
            }
        }
        for( const ref of this._hookTargets(definition('on_timer'), this.limits.keyTargets) ) {
            const component = this._component(ref, false);
            if( component && this._visible(component) ) this._emitNamed(component, 'on_timer', input, intents);
        }
    }

    _focusLost() {
        this.interaction.pressed = null;
        this.interaction.button = null;
        this.interaction.clickFired = false;
        this.interaction.dragging = false;
        this.interaction.dragPickupX = 0;
        this.interaction.dragPickupY = 0;
        this.interaction.heldKeys.clear();
    }

    _hover(intents, input) {
        const hit = this._hit(input.x, input.y);
        const next = hit ? this.ref(hit) : null;
        const previous = this.interaction.hover;
        if( sameRef(previous, next) ) return;
        const oldComponent = this._component(previous, false);
        if( oldComponent && this._visible(oldComponent) )
            this._emitNamed(oldComponent, 'on_mouse_leave', input, intents);
        this.interaction.hover = next;
        const newComponent = this._component(next, false);
        if( newComponent && this._visible(newComponent) )
            this._emitNamed(newComponent, 'on_mouse_over', input, intents);
    }

    _click(leaf, input, intents) {
        const resolved = this._resolveAncestorHook(leaf, definition('on_op'), definition('on_click'));
        if( resolved ) this._emit(resolved.component, resolved.hook, input, { opIndex: 1 }, intents);
    }

    _emitNamed(component, name, input, intents, locals = {}) {
        const resolved = this._resolveHook(component, definition(name));
        if( resolved ) this._emit(component, resolved, input, locals, intents);
    }

    _emit(component, resolved, input, overrides, intents) {
        if( this.invocations >= this.limits.hookInvocations )
            throw new HostRuntimeError('hook invocation limit reached', 'LIMIT');
        const live = this._component(this.ref(component), false);
        if( !live ) return;
        this.invocations++;
        const box = this._box(live);
        const eventMouseX = integer((input.x ?? this.interaction.x) - (box?.x || 0), 0);
        const relativeY = integer((input.y ?? this.interaction.y) - (box?.y || 0), 0);
        const wheel = overrides.wheel ?? input.wheel ?? 0;
        const locals = {
            mouseX: eventMouseX,
            mouseY: relativeY,
            eventMouseX,
            eventMouseY: input.type === 'wheel' ? wheel : relativeY,
            opIndex: overrides.opIndex ?? input.opIndex ?? 1,
            keyTyped: overrides.keyTyped ?? input.keyTyped ?? -1,
            keyPressed: overrides.keyPressed ?? input.keyPressed ?? -1,
            wheel,
        };
        const ref = this.ref(live);
        const hook = hookView(resolved, ref, this);
        const intent = {
            sequence: ++this.sequence,
            component: ref,
            hook,
            event: eventView(input),
            locals,
        };
        if( overrides.dragTarget ) intent.dragTarget = this.ref(overrides.dragTarget) || overrides.dragTarget;
        intents.push(intent);
        const result = this.invoke(intent, this);
        if( result && typeof result.then === 'function' )
            throw new HostRuntimeError('hook invoke must be synchronous', 'ASYNC_INVOKE');
    }

    _resolveHook(component, descriptor) {
        if( !component || !descriptor ) return null;
        const hooks = component.hooks || {};
        for( const key of hookAliases(descriptor) ) {
            const binding = hooks[key];
            if( binding && scriptId(binding) > 0 ) return { key, binding, descriptor };
        }
        if( descriptor.authored && component.events?.[descriptor.authored] ) {
            const binding = component.events[descriptor.authored];
            return { key: descriptor.authored, binding, descriptor };
        }
        return null;
    }

    _resolveAncestorHook(leaf, ...descriptors) {
        for( let component = leaf; component; component = this.byFileId.get(component.layer) || null ) {
            for( const descriptor of descriptors ) {
                const hook = this._resolveHook(component, descriptor);
                if( hook ) return { component, hook };
            }
        }
        return null;
    }

    _hookTargets(descriptor, cap) {
        const result = [];
        for( const component of this.ir.components ) {
            if( result.length >= cap ) break;
            if( !this._visible(component) || !this._resolveHook(component, descriptor) ) continue;
            result.push(this.ref(component));
        }
        return result;
    }

    _visible(component) {
        const box = this._box(component);
        return Boolean(box && !box.effectiveHidden);
    }

    _hit(x, y) {
        const boxes = this.layout();
        for( let index = boxes.length - 1; index >= 0; index-- ) {
            const box = boxes[index];
            if( !hitBox(box, x, y) ) continue;
            const component = this._component(box.ref, false);
            if( !component ) continue;
            if( this._pointerInteractive(component) ) return component;
            if( box.props?.noClickThrough ) return null;
        }
        return null;
    }

    _geometricHit(x, y) {
        const boxes = this.layout();
        for( let index = boxes.length - 1; index >= 0; index-- ) {
            const box = boxes[index];
            if( hitBox(box, x, y) ) return this._component(box.ref, false);
        }
        return null;
    }

    _pointerInteractive(component) {
        if( component.ops?.length || component.static?.clickMask || component.static?.noClickThrough ) return true;
        return EVENT_DEFINITIONS.some((descriptor) => POINTER_EVENTS.has(descriptor.canonical) &&
            this._resolveHook(component, descriptor));
    }

    menuAt(x, y) {
        const leaf = this._hit(x, y);
        if( !leaf ) return [];
        const result = [];
        for( let component = leaf; component; component = this.byFileId.get(component.layer) || null ) {
            for( const op of component.ops || [] ) result.push({
                component: this.ref(component), opIndex: op.index, text: op.text,
            });
        }
        return result.slice(0, 10);
    }

    changes(afterVersion = 0) {
        const first = this.changeLog[0]?.version ?? this.version + 1;
        return {
            from: afterVersion,
            to: this.version,
            truncated: afterVersion < first - 1,
            changes: this.changeLog.filter((change) => change.version > afterVersion)
                .map((change) => cloneValue(change)),
        };
    }

    _changed(kind, component, detail) {
        this._record({ kind, ref: this.ref(component), ...detail });
        this._retireInvisibleInteraction();
        return this.ref(component);
    }

    _record(change) {
        this.version++;
        this.layoutVersion = -1;
        this.changeLog.push({ version: this.version, ...change });
        if( this.changeLog.length > this.limits.changes ) this.changeLog.shift();
    }

    _box(component) {
        this.layout();
        return this.boxByComponent.get(component) || null;
    }

    _indexStatic(component) {
        const fileId = component.fileId;
        if( this.byFileId.has(fileId) )
            throw new HostRuntimeError(`duplicate component file id ${fileId}`, 'BAD_IR');
        const uid = Number.isInteger(fileId) ? this.interfaceId * 65536 + fileId : null;
        const meta = {
            key: `if:${this.interfaceId}:${String(fileId)}`,
            componentId: uid,
            publicFileId: fileId,
            subId: -1,
            dynamic: false,
            generation: this.nextGeneration++,
            draggable: Boolean(component.static?.draggable),
            dragParent: null,
            dragDeadZone: Math.max(0, integer(component.static?.dragDeadZone, 5)),
            dragDeadTime: Math.max(0, integer(component.static?.dragDeadTime, 0)),
            dragBehavior: integer(component.static?.dragBehavior, 0),
        };
        this._index(component, meta);
        if( uid !== null ) this.byUid.set(uid, component);
    }

    _indexDynamic(component, parent, subId) {
        const parentMeta = this.meta.get(parent);
        const componentId = this._allocateDynamicComponentId(parentMeta.componentId);
        this._index(component, {
            key: `dyn:${this.interfaceId}:${this.nextGeneration}`,
            /* Match UITree_CcCreate's 0x8000..0xffff UID band. A dynamic
             * component is addressed by this transient packed id inside C;
             * its stable React identity remains key+generation and its public
             * child slot remains subId. Sharing the parent's UID made two
             * active children indistinguishable across the WASM boundary. */
            componentId,
            publicFileId: parentMeta.publicFileId,
            subId,
            dynamic: true,
            generation: this.nextGeneration++,
            draggable: false,
            dragParent: null,
            dragDeadZone: 5,
            dragDeadTime: 0,
            dragBehavior: 0,
        });
        this.byUid.set(componentId, component);
    }

    _allocateDynamicComponentId(parentId) {
        const group = Number.isInteger(parentId) ? parentId >>> 16 & 0xffff : this.interfaceId & 0xffff;
        let next = this.nextDynamicUid;
        for( let attempt = 0; attempt < 0x8000; attempt++ ) {
            const uid = group * 65536 + next;
            next++;
            if( next > 0xffff ) next = 0x8000;
            if( this.byUid.has(uid) ) continue;
            this.nextDynamicUid = next;
            return uid;
        }
        throw new HostRuntimeError('dynamic component UID space exhausted', 'LIMIT');
    }

    _index(component, meta) {
        if( this.byName.has(component.name) )
            throw new HostRuntimeError(`duplicate component name ${component.name}`, 'BAD_IR');
        this.meta.set(component, meta);
        this.byKey.set(meta.key, component);
        this.byName.set(component.name, component);
        this.byFileId.set(component.fileId, component);
    }

    _component(value, required = true) {
        let component = null;
        if( value && typeof value === 'object' ) {
            if( this.meta.has(value) ) component = value;
            else if( typeof value.key === 'string' ) {
                const candidate = this.byKey.get(value.key) || null;
                const meta = candidate && this.meta.get(candidate);
                if( candidate && (value.generation === undefined || value.generation === meta.generation) )
                    component = candidate;
            } else if( value.ref ) return this._component(value.ref, required);
        } else if( typeof value === 'string' ) {
            component = this.byKey.get(value) || this.byName.get(value) || null;
            const named = /^interface_(\d+):(\d+)$/.exec(value);
            if( !component && named && Number(named[1]) === this.interfaceId )
                component = this.byUid.get(Number(named[1]) * 65536 + Number(named[2])) || null;
        } else if( Number.isInteger(value) ) {
            component = this.byUid.get(value) || this.byFileId.get(value) || null;
        }
        if( required && !component ) throw new HostRuntimeError('component reference is missing or stale', 'STALE_REF');
        return component;
    }

    _setPointer(input) {
        this.interaction.x = input.x;
        this.interaction.y = input.y;
    }

    _retireInvisibleInteraction() {
        for( const field of ['hover', 'pressed'] ) {
            const component = this._component(this.interaction[field], false);
            if( this.interaction[field] && (!component || !this._visible(component))) {
                this.interaction[field] = null;
                if( field === 'pressed' ) {
                    this.interaction.button = null;
                    this.interaction.dragging = false;
                    this.interaction.clickFired = false;
                }
            }
        }
    }

    _retireDeletedInteraction(refs) {
        for( const field of ['hover', 'pressed'] )
            if( refs.some((ref) => sameRef(ref, this.interaction[field])) ) this.interaction[field] = null;
        if( !this.interaction.pressed ) {
            this.interaction.button = null;
            this.interaction.dragging = false;
            this.interaction.clickFired = false;
            this.interaction.dragPickupX = 0;
            this.interaction.dragPickupY = 0;
        }
    }

    _interactionView() {
        return {
            x: this.interaction.x,
            y: this.interaction.y,
            hover: this.interaction.hover,
            pressed: this.interaction.pressed,
            button: this.interaction.button,
            dragging: this.interaction.dragging,
            dragPickupX: this.interaction.dragPickupX,
            dragPickupY: this.interaction.dragPickupY,
            heldKeys: [...this.interaction.heldKeys].sort((a, b) => a - b),
        };
    }

    _result(intents, extra = {}) {
        return {
            epoch: this.epoch,
            version: this.version,
            intents,
            interaction: this._interactionView(),
            ...extra,
        };
    }

    _boundary(fn) {
        const outer = this.operationDepth === 0;
        if( outer ) this.invocations = 0;
        this.operationDepth++;
        try { return fn(); }
        finally { this.operationDepth--; }
    }
}

function event(authored, canonical, ...imported) {
    return Object.freeze({ authored, canonical, imported: Object.freeze(imported) });
}

function definition(name) {
    const result = EVENT_BY_NAME.get(normalizeEventName(name));
    if( !result ) throw new HostRuntimeError(`unknown hook ${name}`, 'BAD_HOOK');
    return result;
}

function normalizeEventName(name) {
    return String(name || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
}

function hookAliases(descriptor) {
    return [descriptor.canonical, ...descriptor.imported, descriptor.authored].filter(Boolean);
}

function exactHookKey(name, descriptor) {
    const exact = String(name || '');
    return hookAliases(descriptor).includes(exact) ? exact : descriptor.canonical;
}

function scriptId(binding) {
    return Number(binding?.script?.id ?? binding?.scriptId ?? binding?.script_id ??
        (typeof binding === 'function' ? 1 : -1));
}

function normalizeBinding(binding, host) {
    if( binding?.script ) return {
        ...binding,
        args: (binding.args || []).slice(0, host.limits.hookArgs).map((arg) => cloneHookValue(arg, host)),
    };
    return {
        script: { id: scriptId(binding) },
        args: (binding.args || []).slice(0, host.limits.hookArgs).map((arg) => cloneHookValue(arg, host)),
        signature: binding.signature || '',
        triggerIds: (binding.triggerIds || binding.trigger_ids || []).slice(0, host.limits.hookTriggers),
    };
}

function hookView(resolved, ref, host) {
    const binding = resolved.binding;
    return {
        id: `${ref.key}@${ref.generation}:${resolved.key}:${scriptId(binding)}`,
        name: resolved.key,
        canonical: resolved.descriptor.canonical,
        authoredEvent: resolved.descriptor.authored,
        scriptId: scriptId(binding),
        args: (binding?.args || []).slice(0, host.limits.hookArgs)
            .map((arg) => cloneHookValue(arg, host)),
        signature: binding?.signature || null,
    };
}

function cloneHookValue(value, host) {
    if( value && typeof value === 'object' ) {
        /* Imported .if hooks carry typed script arguments as {type, value}.
         * Their numeric payload can resemble a packed component id, but the
         * wrapper itself is not a component reference and must never enter
         * HostRuntime.ref()'s stale-incarnation checks. */
        if( value.type && 'value' in value )
            return { type: String(value.type), value: cloneValue(value.value) };
        const ref = host.ref(value);
        if( ref ) return ref;
    }
    return cloneValue(value);
}

function hookFromRequest(request) {
    if( request.binding ) return request.binding;
    const signature = String(request.signature || '');
    const direct = request.args !== undefined ? request.args
        : request.values !== undefined ? request.values : null;
    const triggerIds = requestList(request.trigger_ids ?? request.triggerIds);
    const triggerCount = finiteOptional(request.trigger_count ?? request.triggerCount,
        triggerIds.length);
    return {
        script_id: requestField(request, 'script_id', 'scriptId'),
        signature,
        /* The C ABI keeps integer hook arguments at descriptor positions and
         * packs strings separately in set-bit order.  A JS bridge may provide
         * a convenient `args`/`values` array, but accepting the ABI fields here
         * keeps HostRuntime independent of one particular WASM marshaller. */
        args: direct === null ? unpackSetOnArgs(signature, request)
            : requestList(direct),
        triggerIds: triggerIds.slice(0, Math.max(0, triggerCount)),
    };
}

function unpackSetOnArgs(signature, request) {
    const parseLength = Math.min(signature.endsWith('Y') ? signature.length - 1
        : signature.length, HOST_RUNTIME_LIMITS.hookArgs);
    const integers = requestList(request.int_args ?? request.intArgs);
    const strings = requestList(request.str_args ?? request.strArgs);
    const args = new Array(parseLength);
    let stringIndex = 0;
    for( let index = 0; index < parseLength; index++ ) {
        const type = signature[index];
        if( type === 's' || type === 'W' || type === 'X' )
            args[index] = String(strings[stringIndex++] ?? '');
        else args[index] = finiteOptional(integers[index], 0);
    }
    return args;
}

function requestList(value) {
    if( Array.isArray(value) ) return value;
    if( typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView(value) )
        return Array.from(value);
    return [];
}

function setOnEvent(suffix) {
    const name = suffix.slice('SETON'.length).toLowerCase();
    const aliases = {
        vartransmit: 'on_var_transmit', stattransmit: 'on_stat_transmit',
        invtransmit: 'on_inv_transmit', key: 'on_key', keydown: 'on_key_down', keyup: 'on_key_up',
    };
    return aliases[name] || `on_${name.replace(/([a-z])([A-Z])/g, '$1_$2')}`;
}

function transmitDefinition(kind) {
    if( kind === 'varp' || kind === 'varbit' ) return definition('on_var_transmit');
    if( kind === 'stat' ) return definition('on_stat_transmit');
    if( kind === 'inv' ) return definition('on_inv_transmit');
    if( kind === 'varc' ) return definition('on_varc_transmit');
    if( kind === 'varcstr' ) return definition('on_varcstr_transmit');
    return null;
}

function transmitMatches(component, kind, trigger, binding = null) {
    const hookTriggers = binding?.triggerIds || binding?.trigger_ids;
    if( Array.isArray(hookTriggers) )
        return hookTriggers.length === 0 || hookTriggers.includes(trigger);
    const field = kind === 'stat' ? 'stattriggers' : kind === 'inv' ? 'invtriggers' : 'varptriggers';
    const triggers = component.triggers?.[field];
    return !Array.isArray(triggers) || triggers.length === 0 || triggers.includes(trigger);
}

function cloneInterface(ir) {
    return {
        ...ir,
        components: ir.components.map((component) => {
            const staticProps = cloneRecord(component.static || {});
            return {
                ...component,
                props: cloneRecord(component.props || staticProps),
                static: staticProps,
                authoredProps: new Set(component.authoredProps || []),
                dynamic: (component.dynamic || []).map((binding) => ({ ...binding })),
                ops: (component.ops || []).map((op) => ({ ...op })),
                events: { ...(component.events || {}) },
                hooks: Object.fromEntries(Object.entries(component.hooks || {}).map(([key, binding]) =>
                    [key, binding && typeof binding === 'object' ? { ...binding, args: [...(binding.args || [])] } : binding])),
                triggers: Object.fromEntries(Object.entries(component.triggers || {}).map(([key, ids]) =>
                    [key, Array.isArray(ids) ? [...ids] : ids])),
                dependencies: (component.dependencies || []).map((source) => ({ ...source })),
                scriptBindings: (component.scriptBindings || []).map((binding) => ({ ...binding })),
                rawFields: { ...(component.rawFields || {}) },
                runtime: cloneRuntimeState(component.runtime),
            };
        }),
    };
}

function emptyRuntimeState() {
    return { opBase: '', targetPriority: 0, submenus: {}, params: {}, opKeys: {}, input: null };
}

function cloneRuntimeState(value) {
    const source = value || {};
    return {
        opBase: String(source.opBase || ''),
        targetPriority: finiteOptional(source.targetPriority, 0),
        submenus: cloneRecord(source.submenus || {}),
        params: cloneRecord(source.params || {}),
        opKeys: cloneRecord(source.opKeys || {}),
        input: source.input ? cloneInputState(source.input) : null,
    };
}

function cloneInputState(value = {}) {
    return {
        configured: Boolean(value.configured),
        submitMode: finiteOptional(value.submitMode, 0),
        selectionColor: finiteOptional(value.selectionColor, 0),
        acceptMode: finiteOptional(value.acceptMode, 0),
        wrapMode: finiteOptional(value.wrapMode, 0),
        lineWrappingWidth: finiteOptional(value.lineWrappingWidth, 0),
        selectionBackgroundColor: finiteOptional(value.selectionBackgroundColor, 0),
        lineCountLimit: finiteOptional(value.lineCountLimit, 0),
        cursorColor: finiteOptional(value.cursorColor, 0),
        cursorTransparency: finiteOptional(value.cursorTransparency, 0),
        cursorWidth: finiteOptional(value.cursorWidth, 0),
        cursorHeight: finiteOptional(value.cursorHeight, 0),
        cursorOffset: finiteOptional(value.cursorOffset, 0),
        lineWidthLimit: finiteOptional(value.lineWidthLimit, 0),
        characterFilter: finiteOptional(value.characterFilter, 0),
        focused: Boolean(value.focused),
        caretPosition: Math.max(0, finiteOptional(value.caretPosition, 0)),
    };
}

function runtimeView(component) {
    return cloneRuntimeState(component.runtime);
}

function cloneState(state) {
    const result = {};
    for( const [key, value] of Object.entries(state || {}) ) result[key] = cloneValue(value);
    return result;
}

function cloneRecord(value) {
    const result = {};
    for( const [key, item] of Object.entries(value || {}) ) result[key] = cloneValue(item);
    return result;
}

function cloneValue(value, seen = new WeakSet()) {
    if( value === null || typeof value !== 'object' ) return value;
    if( seen.has(value) ) return '[circular]';
    seen.add(value);
    if( Array.isArray(value) ) return value.map((item) => cloneValue(item, seen));
    if( value instanceof Set ) return [...value].map((item) => cloneValue(item, seen));
    const result = {};
    for( const [key, item] of Object.entries(value) ) {
        if( typeof item === 'function' ) continue;
        result[key] = cloneValue(item, seen);
    }
    return result;
}

function cloneBox(box) {
    return {
        ...box,
        ref: box.ref,
        clip: { ...box.clip },
        surface: { ...box.surface },
        props: cloneRecord(box.props),
        dynamic: [...box.dynamic],
        ops: box.ops.map((op) => ({ ...op })),
        events: [...box.events],
        hooks: [...box.hooks],
        presentation: cloneValue(box.presentation),
    };
}

function dynamicProps(type, kind) {
    const definition = ELEMENTS[kind];
    const common = definition || ELEMENTS.Layer;
    const result = Object.fromEntries(Object.entries(common.props)
        .map(([key, schema]) => [key, schema.default]));
    /* UITree_Push gives every cc_create child the unset (-1) alignment modes;
     * position/size setters replace them with the script's explicit modes. The
     * -1 formulas intentionally resolve like absolute mode 0, but retaining the
     * sentinel keeps the React tree and native inspector state identical. */
    result.xMode = -1;
    result.yMode = -1;
    result.widthMode = -1;
    result.heightMode = -1;
    if( type === IF_TYPE.model ) {
        result.zoom = 100;
        result.model = -1;
        result.modelKind = 'model';
        result.modelSourceId = -1;
        result.modelTransparent = false;
    }
    if( type === IF_TYPE.graphic ) result.activeSprite = -1;
    if( type === IF_TYPE.inv || kind === 'Object' ) {
        result.objectId = 0;
        result.objectCount = 0;
        result.objectNumMode = 0;
        result.modelKind = 'none';
        result.modelSourceId = -1;
    }
    if( type === 10 ) Object.assign(result, {
        color: 0, fillColor: 0, fill: false, lineWidth: 1,
        arcStart: 0, arcEnd: 0,
    });
    return result;
}

function modelKind(value) {
    const aliases = {
        plain: 'model', model: 'model', npc: 'npcHead', npchead: 'npcHead',
        playerhead: 'playerHead', playerself: 'playerSelf',
        playerchathead: 'playerChatHead', loc: 'locModel', locmodel: 'locModel',
        npcmodel: 'npcModel', object: 'object', none: 'none',
    };
    const normalized = String(value || '').replace(/[^a-z]/gi, '').toLowerCase();
    const result = aliases[normalized];
    if( !result ) throw new HostRuntimeError(`unsupported model source ${value}`, 'BAD_REQUEST');
    return result;
}

function modelSource(props) {
    const kind = modelKind(props.modelKind || (props.objectId > 0 ? 'object' : 'model'));
    const id = kind === 'object' ? props.modelSourceId ?? props.objectId ?? 0
        : props.modelSourceId ?? props.model ?? -1;
    const result = { kind, id };
    if( kind === 'object' ) {
        result.baseId = props.objectId ?? id;
        result.count = props.objectCount ?? 0;
        result.numberMode = props.objectNumMode ?? 0;
        result.composed = true;
    }
    return result;
}

function resolveCountObject(objects, objectId, count) {
    const object = objects?.[String(objectId)] || null;
    let resolved = objectId;
    if( !object || count <= 1 || !Array.isArray(object.countVariants) ) return resolved;
    for( const variant of object.countVariants ) {
        if( !variant ) continue;
        const threshold = finiteOptional(variant.count, 0);
        const id = finiteOptional(variant.id, -1);
        if( threshold !== 0 && count >= threshold && id > 0 ) resolved = id;
    }
    return resolved;
}

function viewport(value = {}) {
    const width = boundedInteger('viewport width', value.width ?? 512, 1, HOST_RUNTIME_LIMITS.viewport);
    const height = boundedInteger('viewport height', value.height ?? 334, 1, HOST_RUNTIME_LIMITS.viewport);
    return { width, height };
}

function limits(overrides = {}) {
    const result = {};
    for( const [key, fallback] of Object.entries(HOST_RUNTIME_LIMITS) )
        result[key] = boundedInteger(`${key} limit`, overrides[key] ?? fallback, 1, fallback);
    return Object.freeze(result);
}

function validateInput(raw, viewportValue) {
    if( !raw || typeof raw !== 'object' || Array.isArray(raw) )
        throw new HostRuntimeError('input event must be an object', 'BAD_INPUT');
    const aliases = {
        mousemove: 'pointer_move', mouse_move: 'pointer_move', pointermove: 'pointer_move',
        mousedown: 'pointer_down', mouse_down: 'pointer_down', pointerdown: 'pointer_down',
        mouseup: 'pointer_up', mouse_up: 'pointer_up', pointerup: 'pointer_up',
        keydown: 'key_down', keyup: 'key_up', focuslost: 'focus_lost', blur: 'focus_lost',
    };
    const name = String(raw.type || '').toLowerCase();
    const type = aliases[name.replace(/[^a-z_]/g, '')] || name;
    const result = { type };
    if( ['pointer_move', 'pointer_down', 'pointer_up', 'wheel'].includes(type) ) {
        result.x = boundedInteger('pointer x', raw.x, -viewportValue.width, viewportValue.width * 2);
        result.y = boundedInteger('pointer y', raw.y, -viewportValue.height, viewportValue.height * 2);
    }
    if( type === 'pointer_down' || type === 'pointer_up' ) result.button = button(raw.button);
    if( type === 'wheel' ) {
        const delta = finiteValue('wheel delta', raw.wheel ?? raw.deltaY);
        if( delta === 0 ) throw new HostRuntimeError('wheel delta cannot be zero', 'BAD_INPUT');
        result.wheel = delta > 0 ? 1 : -1;
    }
    if( type === 'key' ) {
        result.keyTyped = boundedInteger('keyTyped', raw.keyTyped ?? -1, -1, 65535);
        result.keyPressed = boundedInteger('keyPressed', raw.keyPressed ?? -1, -1, 0x10ffff);
        if( result.keyTyped < 0 && result.keyPressed < 0 )
            throw new HostRuntimeError('key needs keyTyped or keyPressed', 'BAD_INPUT');
    }
    if( type === 'key_down' || type === 'key_up' ) {
        result.keyTyped = boundedInteger('keyTyped', raw.keyTyped ?? raw.code, 0, 255);
        result.keyPressed = boundedInteger('keyPressed', raw.keyPressed ?? 0, 0, 0x10ffff);
    }
    if( type === 'op' ) {
        result.target = raw.target ?? raw.ref ?? raw.component;
        result.opIndex = boundedInteger('operation index', raw.opIndex, 1, 10);
    }
    if( type === 'tick' && raw.cycle !== undefined )
        result.cycle = boundedInteger('cycle', raw.cycle, 0, Number.MAX_SAFE_INTEGER);
    const known = new Set(['pointer_move', 'pointer_down', 'pointer_up', 'wheel', 'key',
        'key_down', 'key_up', 'op', 'tick', 'focus_lost']);
    if( !known.has(type) ) throw new HostRuntimeError(`unsupported input event ${raw.type}`, 'BAD_INPUT');
    return result;
}

function baseEvent(type, values = {}) {
    return { type, ...values };
}

function eventView(input) {
    const result = { type: input.type };
    for( const key of ['x', 'y', 'button', 'wheel', 'keyTyped', 'keyPressed', 'opIndex', 'cycle', 'kind', 'id', 'trigger'] )
        if( input[key] !== undefined ) result[key] = input[key];
    return result;
}

function hitBox(box, x, y) {
    if( box.effectiveHidden || box.culled || box.w <= 0 || box.h <= 0 ) return false;
    if( x < box.x || y < box.y || x >= box.x + box.w || y >= box.y + box.h ) return false;
    return x >= box.clip.left && y >= box.clip.top && x < box.clip.right && y < box.clip.bottom;
}

function button(value) {
    if( value === 'left' || value === undefined ) return 0;
    if( value === 'middle' ) return 1;
    if( value === 'right' ) return 2;
    return boundedInteger('pointer button', value, 0, 2);
}

function requestValues(request, names) {
    if( Array.isArray(request.values) ) return request.values;
    return names.map((aliases) => requestField(request, ...aliases));
}

function targetOf(request, host, preferred = 'component_id') {
    const explicit = request.ref ?? request.component ?? request.component_ref ?? request.target;
    if( explicit !== undefined ) return explicit;
    if( preferred === 'parent_id' ) {
        if( request.parent !== undefined ) return request.parent;
        if( request.parent_id !== undefined ) return request.parent_id;
        if( request.parentId !== undefined ) return request.parentId;
    }
    if( preferred === 'uid' && request.uid !== undefined ) return request.uid;
    const componentId = request.component_id ?? request.componentId;
    if( componentId !== undefined && request._kind?.startsWith('CC_') ) {
        const active = (request.dot_operand ?? request.dotOperand) ? host.dotActive : host.active;
        if( active?.dynamic && active.componentId === componentId ) return active;
    }
    if( componentId !== undefined ) return componentId;
    return (request.dot_operand ?? request.dotOperand) ? host.dotActive : host.active;
}

function requestField(request, ...keys) {
    for( const key of keys ) if( key && request[key] !== undefined ) return request[key];
    throw new HostRuntimeError(`host request is missing ${keys.filter(Boolean).join('/')}`, 'BAD_REQUEST');
}

function normalizeRequestKind(value) {
    if( typeof value !== 'string' )
        throw new HostRuntimeError('host request kind must be a name', 'BAD_REQUEST');
    const raw = value.toUpperCase();
    const kind = raw === 'CLIENT_CLOCK' ? 'CLIENTCLOCK' : raw;
    if( !/^[A-Z0-9_]+$/.test(kind) ) throw new HostRuntimeError('host request kind is invalid', 'BAD_REQUEST');
    return kind;
}

function soundSynthIntent(request) {
    return {
        id: finiteOptional(request.id ?? request.soundId ?? request.sound_id ?? request.args?.[0], -1),
        secondaryId: finiteOptional(request.secondary_id ?? request.secondaryId, 0),
        loops: finiteOptional(request.loops ?? request.args?.[1], 0),
        delay: finiteOptional(request.delay ?? request.args?.[2], 0),
        fadeOutDelay: finiteOptional(request.fade_out_delay ?? request.fadeOutDelay, 0),
        fadeOutSpeed: finiteOptional(request.fade_out_speed ?? request.fadeOutSpeed, 0),
        fadeInDelay: finiteOptional(request.fade_in_delay ?? request.fadeInDelay, 0),
        fadeInSpeed: finiteOptional(request.fade_in_speed ?? request.fadeInSpeed, 0),
    };
}

function supportsHostRequest(kind) {
    if( STATE_READ_REQUEST[kind] || STATE_WRITE_REQUEST[kind] || SPECIAL_REQUESTS.has(kind) )
        return true;
    const match = /^(?:CC|IF)_(.+)$/.exec(kind);
    if( !match ) return false;
    const suffix = match[1];
    if( REQUEST_GETTERS[suffix] || REQUEST_SETTERS[suffix] || INPUT_GETTERS[suffix] ||
        INPUT_SETTERS[suffix] ||
        SPECIAL_COMPONENT_SUFFIXES.has(suffix) ) return true;
    if( suffix.startsWith('SETON') || suffix.startsWith('INPUT_SETON') ) {
        try {
            const eventName = suffix.startsWith('INPUT_SETON')
                ? `on_${suffix.slice('INPUT_SETON'.length).toLowerCase()}`
                : setOnEvent(suffix);
            definition(eventName);
            return true;
        } catch { return false; }
    }
    return false;
}

function unpack(values) {
    return values.length === 1 && Array.isArray(values[0]) ? values[0] : values;
}

function stateId(value) {
    return boundedInteger('state id', value, 0, 0x7fffffff);
}

function inventoryState(value) {
    if( !value || typeof value !== 'object' || Array.isArray(value) )
        throw new HostRuntimeError('inventory state must be an object of object-id counts', 'BAD_REQUEST');
    const entries = Object.entries(value);
    if( entries.length > HOST_RUNTIME_LIMITS.components )
        throw new HostRuntimeError('inventory state has too many entries', 'LIMIT');
    const result = {};
    for( const [rawObjectId, rawCount] of entries ) {
        const objectId = boundedInteger('inventory object id', Number(rawObjectId), 0, 0x7fffffff);
        result[objectId] = boundedInteger('inventory object count', rawCount, 0, 0x7fffffff);
    }
    return result;
}

function boundedKeyList(name, value) {
    if( !Array.isArray(value) ) throw new HostRuntimeError(`${name} must be an array`, 'BAD_REQUEST');
    if( value.length > 10 ) throw new HostRuntimeError(`${name} has too many entries`, 'LIMIT');
    return value.map((entry) => boundedInteger(name, entry, -1, 0x10ffff));
}

function finiteValue(name, value) {
    if( typeof value === 'boolean' ) return value ? 1 : 0;
    const number = Number(value);
    if( !Number.isFinite(number) || !Number.isSafeInteger(number) )
        throw new HostRuntimeError(`${name} must be a safe integer`, 'BAD_REQUEST');
    return number;
}

function finiteOptional(value, fallback) {
    const number = Number(value);
    return Number.isSafeInteger(number) ? number : fallback;
}

function clampInteger(value, low, high) {
    return Math.max(low, Math.min(high, finiteValue('integer value', value)));
}

function integer(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) : fallback;
}

function boundedInteger(name, value, low, high) {
    const number = Number(value);
    if( !Number.isSafeInteger(number) || number < low || number > high )
        throw new HostRuntimeError(`${name} must be an integer in ${low}..${high}`, 'BAD_REQUEST');
    return number;
}

function boundedText(name, value) {
    const text = String(value);
    if( text.length > HOST_RUNTIME_LIMITS.text )
        throw new HostRuntimeError(`${name} exceeds ${HOST_RUNTIME_LIMITS.text} characters`, 'LIMIT');
    return text;
}

function normalizeHostData(value) {
    const source = value && typeof value === 'object' && !Array.isArray(value) ? value : {};
    const record = (entry) => entry && typeof entry === 'object' && !Array.isArray(entry) ? entry : {};
    return {
        clientType: source.clientType,
        mapMembers: source.mapMembers,
        enums: record(source.enums),
        fonts: record(source.fonts),
        objects: record(source.objects),
        npcs: record(source.npcs),
        locs: record(source.locs),
        params: record(source.params),
        structs: record(source.structs),
        interfaceParents: source.interfaceParents,
    };
}

function normalizeInterfaceParents(value) {
    if( value instanceof Map ) return new Map([...value.entries()].map(([key, entry]) =>
        [String(key), cloneValue(entry)]));
    if( Array.isArray(value) ) return new Map(value.map((entry) => {
        if( !Array.isArray(entry) || entry.length < 2 )
            throw new HostRuntimeError('interface parent entries must be [component, group]', 'BAD_REQUEST');
        return [String(entry[0]), cloneValue(entry[1])];
    }));
    if( value && typeof value === 'object' )
        return new Map(Object.entries(value).map(([key, entry]) => [String(key), cloneValue(entry)]));
    return new Map();
}

/* The synchronous half of ToriDraw2D's paragraph layout. CS2's PARAHEIGHT
 * and PARAWIDTH wrap between words only, preserve explicit blank lines, skip
 * draw markup, and clamp an over-wide single word to maxWidth. */
function measureParagraph(font, text, maxWidth) {
    let lines = 0;
    let best = 0;
    const space = glyphAdvance(font, 32);
    for( const segment of explicitParagraphLines(text) ) {
        lines++;
        let width = 0;
        for( const word of segment.split(' ') ) {
            if( !word ) continue;
            const wordWidth = measureFontSpan(font, word);
            const candidate = width === 0 ? wordWidth : width + space + wordWidth;
            if( width > 0 && maxWidth > 0 && candidate > maxWidth ) {
                best = Math.max(best, width);
                lines++;
                width = wordWidth;
            } else width = candidate;
        }
        best = Math.max(best, width);
    }
    return { lines: lines || 1, width: maxWidth > 0 ? Math.min(best, maxWidth) : best };
}

function explicitParagraphLines(text) {
    return String(text).split(/\\n|\r\n|[\r\n]|<br>/i);
}

function measureFontSpan(font, text) {
    let width = 0;
    for( let index = 0; index < text.length; ) {
        const token = fontMarkupToken(text, index);
        if( token ) {
            index += token.length;
            if( token.emit ) width += glyphAdvance(font, token.emit);
            continue;
        }
        const point = text.codePointAt(index);
        width += glyphAdvance(font, cp1252Byte(point));
        index += point > 0xffff ? 2 : 1;
    }
    return width;
}

function fontMarkupToken(text, index) {
    const rest = text.slice(index);
    if( /^@...@/.test(rest) ) return { length: 5, emit: 0 };
    if( /^<gt>/i.test(rest) ) return { length: 4, emit: 62 };
    if( /^<lt>/i.test(rest) ) return { length: 4, emit: 60 };
    for( const literal of ['</col>', '</u>', '</str>'] )
        if( rest.startsWith(literal) ) return { length: literal.length, emit: 0 };
    for( const pattern of [
        /^<col=(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})>/,
        /^<u(?:=(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/,
        /^<str(?:=(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/,
    ] ) {
        const match = pattern.exec(rest);
        if( match ) return { length: match[0].length, emit: 0 };
    }
    return null;
}

const CP1252_BYTES = new Map([
    [0x20ac, 0x80], [0x201a, 0x82], [0x0192, 0x83], [0x201e, 0x84],
    [0x2026, 0x85], [0x2020, 0x86], [0x2021, 0x87], [0x02c6, 0x88],
    [0x2030, 0x89], [0x0160, 0x8a], [0x2039, 0x8b], [0x0152, 0x8c],
    [0x017d, 0x8e], [0x2018, 0x91], [0x2019, 0x92], [0x201c, 0x93],
    [0x201d, 0x94], [0x2022, 0x95], [0x2013, 0x96], [0x2014, 0x97],
    [0x02dc, 0x98], [0x2122, 0x99], [0x0161, 0x9a], [0x203a, 0x9b],
    [0x0153, 0x9c], [0x017e, 0x9e], [0x0178, 0x9f],
]);

function cp1252Byte(point) {
    return point >= 0 && point <= 0x7f || point >= 0xa0 && point <= 0xff
        ? point : CP1252_BYTES.get(point) ?? 63;
}

function glyphAdvance(font, code) {
    const value = Number(font.advances?.[code]);
    return Number.isSafeInteger(value) && value > 0 ? value : 4;
}

function sameRef(left, right) {
    if( left === null || left === undefined || right === null || right === undefined )
        return !left && !right;
    return left.key === right.key && left.generation === right.generation;
}

function sameServiceTarget(left, right) {
    if( left && right && typeof left === 'object' && typeof right === 'object' &&
        left.key !== undefined && right.key !== undefined ) return sameRef(left, right);
    return Object.is(left, right);
}
