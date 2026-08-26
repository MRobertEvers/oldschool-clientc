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
import { CS2_HOST_REQUEST_NAMES } from './cs2_host_requests.js';
import { HOST_READS } from './host.js';
import {
    HOST_ACTIVITY_REQUEST_NAMES, createHostActivityState, handleHostActivityRequest,
    snapshotHostActivityState,
} from './host_activity.js';
import {
    CHAT_SOCIAL_REQUEST_NAMES, createChatSocialState, handleChatSocialRequest,
    snapshotChatSocialState,
} from './host_chat_social.js';
import { DB_REQUEST_NAMES, createDbState, handleDbRequest } from './host_db.js';
import { LOOT_REQUESTS, createLootState, handleLootRequest } from './host_loot.js';
import {
    OVERLAY_REQUEST_NAMES, createOverlayState, handleOverlayRequest,
    snapshotOverlayState,
} from './host_overlay.js';
import { SUBJECT_REQUESTS, createSubjectState, handleSubjectRequest } from './host_subject.js';
import {
    WORLDMAP_REQUESTS, createWorldMapState, cycleWorldMapState, handleWorldMapRequest,
    setWorldMapDisplayPixelSize, snapshotWorldMapState,
} from './host_worldmap.js';
import { OPS } from './ops.js';
import { resolveProps } from './eval.js';
import {
    axisFromPositionMode, dimFromParentMode,
    layout as resolveLayout, layoutBox, layoutGeometry, layoutVisibility,
} from './preview.js';
import { TREE_DELTA_SCHEMA, TREE_DIRTY } from './ui_tree_store.js';

const FAST_DB_ITERATOR_MAX = 65536;
const DB_ITERATOR_WRITES = new Set([
    'DB_FIND_WITH_COUNT', 'DB_FINDNEXT', 'DB_FINDALL_WITH_COUNT',
    'DB_FIND_FILTER_WITH_COUNT', 'DB_FIND', 'DB_FINDALL', 'DB_FIND_FILTER',
]);

/* Dirty projection is deliberately fail-closed. Only component-local paint
 * and interaction fields can be reconstructed with a single layoutBox() walk;
 * anything which can move, clip, reveal, create, delete, or expression-drive
 * another node retains the full layout projector as its correctness oracle. */
const TREE_DIRTY_CATEGORIES = Object.freeze(Object.values(TREE_DIRTY));
const TREE_GEOMETRY_OPS = new Set([
    'if_setposition', 'if_setsize', 'if_setscrollpos', 'if_setscrollsize',
]);
const TREE_VISIBILITY_OPS = new Set(['if_sethide']);
const TREE_INTERACTION_OPS = new Set([
    'if_setop', 'if_clearops', 'if_setopbase', 'if_setopsubmenu',
    'if_clearopsubmenu', 'if_settargetverb', 'if_settargetpriority',
    'if_setcomponentparam', 'if_setopkey', 'if_setopkeyrate',
    'if_setopkeyignoreheld', 'if_setdraggable', 'if_setdragdeadzone',
    'if_setdragdeadtime', 'if_setdraggablebehavior', 'if_setnoclickthrough',
    'if_setnoscrollthrough', 'if_setpinch', 'if_setclickmask',
    'if_setopforceleftclick',
]);

export const HOST_RUNTIME_SCHEMA = 'cs2dom-host/1';

export const HOST_RUNTIME_LIMITS = Object.freeze({
    components: 16384,
    dynamicComponents: 8192,
    hookInvocations: 256,
    hookArgs: 64,
    hookTriggers: 4096,
    keyTargets: 64,
    changes: 4096,
    text: 65535,
    viewport: 4096,
});

const GAME_OPTIONS = new Set([1, 7, 8, 9]);
const DEVICE_OPTIONS = new Set([2, 3, 4, 5, 6, 14, 15, 19, 22, 27]);
const OPTION_VOLUME = Object.freeze({
    SETVOLUMEMUSIC: ['game', 7], GETVOLUMEMUSIC: ['game', 7],
    SETVOLUMESOUNDS: ['game', 8], GETVOLUMESOUNDS: ['game', 8],
    SETVOLUMEAREASOUNDS: ['game', 9], GETVOLUMEAREASOUNDS: ['game', 9],
});
const OPTION_REQUESTS = new Set([
    'GETREMOVEROOFS', 'SETREMOVEROOFS',
    ...Object.keys(OPTION_VOLUME),
    'CLIENTOPTION_SET', 'CLIENTOPTION_GET', 'DEVICEOPTION_SET', 'DEVICEOPTION_GET',
    'GAMEOPTION_SET', 'GAMEOPTION_GET', 'DEVICEOPTION_GETRANGE',
]);
const VIEWPORT_REQUESTS = new Set([
    'VIEWPORT_SETFOV', 'VIEWPORT_SETZOOM', 'VIEWPORT_CLAMPFOV',
    'VIEWPORT_GETEFFECTIVESIZE', 'VIEWPORT_GETZOOM', 'VIEWPORT_GETFOV',
]);
const UIZOOM_REQUESTS = new Set(['UIZOOM_SET', 'UIZOOM_GET', 'UIZOOM_RESET', 'UIZOOM_GETDEFAULT']);
const SAFEAREA_REQUESTS = new Set([
    'SAFEAREA_GETMINX', 'SAFEAREA_GETMINY', 'SAFEAREA_GETMAXX', 'SAFEAREA_GETMAXY',
]);
const MINIMENU_REQUESTS = new Set([
    'MINIMENU_TYPE', 'MINIMENU_ENTRY', 'MINIMENU_ISOPEN',
    'MINIMENU_FINDNPC', 'MINIMENU_FINDLOC', 'MINIMENU_FINDOBJ', 'MINIMENU_FINDPLAYER',
    'MINIMENU_FINDCOMPONENT', 'MINIMENU_NUMOPS', '_7106', '_7107',
]);
const MINIMENU_SUBJECT_TYPES = Object.freeze({ npc: 2, loc: 3, obj: 4, player: 6 });
const MINIMENU_FIND_SUBJECT = Object.freeze({
    MINIMENU_FINDNPC: 'npc', MINIMENU_FINDLOC: 'loc',
    MINIMENU_FINDOBJ: 'obj', MINIMENU_FINDPLAYER: 'player',
});
const CAMERA_REQUESTS = new Set([
    'CAM_FORCEANGLE', 'CAM_GETANGLE_XA', 'CAM_GETANGLE_YA',
    'CAM_SETFOLLOWHEIGHT', 'CAM_GETFOLLOWHEIGHT',
]);
const SOUND_REQUESTS = new Set([
    'SOUND_SYNTH', 'SOUND_SONG', 'SOUND_JINGLE', 'SOUND_SONG_WITHSECONDARY',
]);
/* These are the two no-trigger transmit channels backed by the local chat and
 * social stores.  Their C HOST writers only raise a dirty flag; the interface
 * hooks are snapshotted and dispatched by the next logic-tick pump. */
const FRIEND_TRANSMIT_REQUESTS = new Set([
    'FRIEND_ADD', 'FRIEND_DEL', 'IGNORE_ADD', 'IGNORE_DEL', 'CHAT_SETFILTER',
]);
const CHAT_TRANSMIT_REQUESTS = new Set([
    'MES', 'CHAT_SETMESSAGEFILTER', 'CHAT_SETTIMESTAMPS',
]);
/* Native RS_CS2Host keeps at most 64 distinct changed ids per tick.  The next
 * change deliberately degrades that channel to a wildcard dispatch instead of
 * dropping an update that some listener may need. */
const TRANSMIT_CHANGED_ID_LIMIT = 64;
/* rs_cs2_host.h gives each script-deferred component queue sixteen slots and
 * drops the newest request on overflow. Keep these separate: the App drains
 * every resize before every trigger-op, irrespective of source issue order. */
const DEFERRED_COMPONENT_QUEUE_LIMIT = 16;
const MINIMAP_REQUESTS = new Set([
    'MINIMAP_SETZOOMABLE', 'MINIMAP_SETZOOM', 'MINIMAP_GETZOOM', 'MINIMAP_SETICONZOOMLIMIT',
]);
const VALIDATED_HOST_REQUEST_KINDS = new Set();
const NORMALIZED_HOST_REQUEST_KINDS = new Map();
const DYNAMIC_PROPS_CACHE = new Map();
/* Runtime-created components overwhelmingly leave authored metadata empty.
 * These values are never mutated in place: setters replace `dynamic`/`ops`,
 * hooks lazily allocate their own record, and the remaining fields are cache
 * input only. Sharing them avoids eight throwaway containers per CC_CREATE. */
const EMPTY_DYNAMIC_ARRAY = Object.freeze([]);
const EMPTY_AUTHORED_PROPS = new Set();
const RECYCLED_DYNAMIC_META = Symbol('cs2dom.recycledDynamicMeta');
const FAST_INT_GEOMETRY_READS = Object.freeze({
    1500: 'if_getx', 1501: 'if_gety', 1502: 'if_getwidth', 1503: 'if_getheight',
    2500: 'if_getx', 2501: 'if_gety', 2502: 'if_getwidth', 2503: 'if_getheight',
});
/* Internal C/WASM transaction format. The public HOST surface remains named
 * request records; this fixed-width view exists solely so a synchronous worker
 * can commit a large native redraw without allocating an equivalent JS object
 * graph first. The view is consumed before the C callback returns and is never
 * retained across a possible WebAssembly memory growth. */
const FAST_HOST_RECORD_WORDS = 12;
const FAST_HOST_HOOK_STRING_LENGTH = 256;
const FAST_HOST_PENDING_TOKEN_MAX = 0x7fffffff;
const FAST_HOST_TEXT_DECODER = new TextDecoder();
const FAST_HOST_KINDS = Object.freeze({
    CC_CREATE: 100,
    CC_FIND: 200,
    CC_SETPOSITION: 1000,
    CC_SETSIZE: 1001,
    CC_SETHIDE: 1003,
    CC_SETCOLOUR: 1101,
    CC_SETFILL: 1102,
    CC_SETTRANS: 1103,
    CC_SETGRAPHIC: 1105,
    CC_SETTEXT: 1112,
    CC_SETTEXTFONT: 1113,
    CC_SETTEXTALIGN: 1114,
    CC_SETTEXTSHADOW: 1115,
    CC_SETOBJECT: 1200,
    CC_SETOBJECT_NONUM: 1205,
    CC_SETOBJECT_ALWAYS_NUM: 1212,
    CC_SETOP: 1300,
    CC_SETDRAGGABLEBEHAVIOR: 1302,
    CC_SETDRAGDEADZONE: 1303,
    CC_SETDRAGDEADTIME: 1304,
    CC_SETOPBASE: 1305,
    CC_CLEAROPS: 1307,
    CC_SETONMOUSEOVER: 1403,
    CC_SETONMOUSELEAVE: 1404,
    CC_SETONDRAG: 1405,
    CC_SETONOP: 1409,
    CC_SETONDRAGCOMPLETE: 1410,
    CC_SETONMOUSEREPEAT: 1412,
    IF_SETPOSITION: 2000,
    IF_SETSIZE: 2001,
    IF_SETHIDE: 2003,
    IF_SETTRANS: 2103,
    IF_CLEAROPS: 2307,
    IF_SETONMOUSEOVER: 2403,
    IF_SETONMOUSELEAVE: 2404,
    IF_SETONOP: 2409,
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
const FAST_HOST_HOOK_DEFINITIONS = Object.freeze({
    [FAST_HOST_KINDS.CC_SETONMOUSEOVER]: EVENT_BY_NAME.get('onmouseover'),
    [FAST_HOST_KINDS.CC_SETONMOUSELEAVE]: EVENT_BY_NAME.get('onmouseleave'),
    [FAST_HOST_KINDS.CC_SETONDRAG]: EVENT_BY_NAME.get('ondrag'),
    [FAST_HOST_KINDS.CC_SETONOP]: EVENT_BY_NAME.get('onop'),
    [FAST_HOST_KINDS.CC_SETONDRAGCOMPLETE]: EVENT_BY_NAME.get('ondragcomplete'),
    [FAST_HOST_KINDS.CC_SETONMOUSEREPEAT]: EVENT_BY_NAME.get('onmouserepeat'),
    [FAST_HOST_KINDS.IF_SETONMOUSEOVER]: EVENT_BY_NAME.get('onmouseover'),
    [FAST_HOST_KINDS.IF_SETONMOUSELEAVE]: EVENT_BY_NAME.get('onmouseleave'),
    [FAST_HOST_KINDS.IF_SETONOP]: EVENT_BY_NAME.get('onop'),
});

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
    STAT: 'stat', STAT_BASE: 'stat', STAT_XP: 'statxp',
});

const STATE_WRITE_REQUEST = Object.freeze({
    VARS_WRITE_VARP: 'varp', VARS_WRITE_VARBIT: 'varbit',
    VARS_WRITE_VARC_INT: 'varc', VARS_WRITE_VARC_STRING: 'varcstr',
    POP_VAR: 'varp', POP_VARBIT: 'varbit', POP_VARC_INT: 'varc',
    POP_VARC_STRING: 'varcstr', POP_VARC_STRING_OLD: 'varcstr',
});

const STATE_KINDS = new Set(['varp', 'varbit', 'varc', 'varcstr', 'stat', 'statxp', 'inv']);

const SPECIAL_REQUESTS = new Set([
    'CC_CREATE', 'CC_CREATECHILD', 'CC_CREATESIBLING', 'CC_COPY', 'CC_DELETE',
    'CC_DELETEALL', 'CC_FIND', 'IF_FIND', 'IF_CHILDREN_FIND', 'IF_CHILDREN_COLLECT',
    'CC_CHILDREN_FIND_COUNT', 'CC_CHILDREN_FINDNEXT', 'CC_CHILDREN_FINDNEXTID',
    'IF_CHILDREN_FINDNEXTID', 'CHILDREN_FINDNEXTID', 'CC_PARENTID', 'IF_GETTOP',
    'INVS_GET_NUM', 'INVS_GET_TOTAL', 'INVS_GET_SIZE', 'INV_GETOBJ', 'INV_GETNUM',
    'INV_TOTAL', 'INV_SIZE', 'CLIENTCLOCK', 'SOUND_SYNTH',
    'MOUSE_GETX', 'MOUSE_GETY',
    'KEYHELD', 'KEYPRESSED', 'STAT_XP', 'COORD', 'STAFFMODLEVEL', 'MAP_WORLD', '_3330',
    'MES', 'RESUME_COUNTDIALOG',
    'LOCAL_NOTIFICATION', 'LOCAL_NOTIFICATION_CANCEL',
    'LOCAL_NOTIFICATION_CANCELALL', 'LOCAL_NOTIFICATION_SUPPORTED',
    'SETANTIDRAG', 'LOGOUT', ...MINIMAP_REQUESTS,
    'SOUND_SONG', 'SOUND_JINGLE', 'SOUND_SONG_WITHSECONDARY',
    'GETREMOVEROOFS', 'SETREMOVEROOFS',
    'SETVOLUMEMUSIC', 'GETVOLUMEMUSIC', 'SETVOLUMESOUNDS', 'GETVOLUMESOUNDS',
    'SETVOLUMEAREASOUNDS', 'GETVOLUMEAREASOUNDS',
    'CLIENTOPTION_SET', 'CLIENTOPTION_GET', 'DEVICEOPTION_SET', 'DEVICEOPTION_GET',
    'GAMEOPTION_SET', 'GAMEOPTION_GET', 'DEVICEOPTION_GETRANGE',
    'SETWINDOWMODE', 'SETDEFAULTWINDOWMODE',
    'CAM_FORCEANGLE', 'CAM_GETANGLE_XA', 'CAM_GETANGLE_YA',
    'CAM_SETFOLLOWHEIGHT', 'CAM_GETFOLLOWHEIGHT',
    'VIEWPORT_SETFOV', 'VIEWPORT_SETZOOM', 'VIEWPORT_CLAMPFOV',
    'VIEWPORT_GETEFFECTIVESIZE', 'VIEWPORT_GETZOOM', 'VIEWPORT_GETFOV',
    'UIZOOM_SET', 'UIZOOM_GET', 'UIZOOM_RESET', 'UIZOOM_GETDEFAULT',
    'SAFEAREA_GETMINX', 'SAFEAREA_GETMINY', 'SAFEAREA_GETMAXX', 'SAFEAREA_GETMAXY',
    'OC_FIND', 'OC_FINDNEXT', 'OC_FINDRESET', 'OC_SHIFTCLICKIOP',
    'OC_WEARPOS', 'OC_WEARPOS2', 'OC_WEARPOS3', 'OC_WEIGHT', 'OC_EXAMINE', 'OC_ISUBOP',
    'NC_NAME', 'MEC_TEXT', 'MEC_TEXTSIZE', 'MEC_CATEGORY', 'MEC_SPRITE',
    ...MINIMENU_REQUESTS,
    ...HOST_ACTIVITY_REQUEST_NAMES,
    ...CHAT_SOCIAL_REQUEST_NAMES,
    ...DB_REQUEST_NAMES,
    ...LOOT_REQUESTS,
    ...OVERLAY_REQUEST_NAMES,
    ...SUBJECT_REQUESTS,
    ...WORLDMAP_REQUESTS,
    'HISCORES_STATUS', 'HISCORES_ERROR',
    /* Synchronous game/cache reads used while an interface constructs itself.
     * The server ships these small lookup tables beside the bytecode. */
    'CLIENTTYPE', 'MAP_MEMBERS', 'ON_MOBILE',
    'RUNENERGY_VISIBLE', 'RUNWEIGHT_VISIBLE',
    'ENUM', 'ENUM_STRING', 'ENUM_GETOUTPUTCOUNT',
    'FROMDATE', 'PARAWIDTH', 'PARAHEIGHT',
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

/* Native IF/CC setters address the whole mounted UITree. A cache script may
 * legitimately name a component from a companion group (bank search drives
 * meslayer/chatbox 162 and 163) which is not part of cs2dom's selected React
 * root. Once native's lazy group load has been attempted, an absent node makes
 * these operations a no-op; it does not abort the running script. Keep this
 * list at the HOST-request seam so the public mutate() API remains strict about
 * stale React refs. */
const MISSING_COMPONENT_NOOP_SUFFIXES = new Set([
    'SETHTTPSPRITE', 'SETOP', 'SETOBJECT', 'SETOBJECT_NONUM',
    'SETOBJECT_ALWAYS_NUM', 'SETNPCHEAD', 'SETPLAYERHEAD_SELF',
    'SETPLAYERMODEL_SELF', 'SETMODEL_PLAYERCHATHEAD', 'SETLOCMODEL',
    'SETNPCMODEL', 'SETDRAGGABLE', 'SETDRAGGABLEBEHAVIOR',
    'SETDRAGDEADZONE', 'SETDRAGDEADTIME', 'SETOPBASE', 'CLEAROPS',
    'SETOPSUBMENU', 'CLEAROPSUBMENU', 'SETTARGETPRIORITY',
    'SETCOMPONENTPARAM', 'SETPARAM', 'SETOPKEY', 'SETOPTKEY',
    'SETOPKEYRATE', 'SETOPTKEYRATE', 'SETOPKEYIGNOREHELD',
    'SETOPTKEYIGNOREHELD',
]);

/* This is the C ABI surface, not the larger opcode vocabulary. Arithmetic,
 * branching, string operations, and other VM-internal commands never cross
 * the HOST seam and must not dilute its coverage numbers. */
const COMMAND_NAMES = new Set(CS2_HOST_REQUEST_NAMES);

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
        /* Cache definitions are immutable input for one preview generation.
         * Retain the caller's object identity so the shared WASM module can
         * reuse exact enum/struct answers across fresh interaction sessions
         * without conflating different cache/content sources. */
        this.hostDataIdentity = options.hostData && typeof options.hostData === 'object'
            ? options.hostData : this.hostData;
        this.clientType = integer(options.clientType ?? this.hostData.clientType, 10);
        this.mapMembers = Boolean(options.mapMembers ?? this.hostData.mapMembers ?? true);
        this.interfaceParents = normalizeInterfaceParents(
            options.interfaceParents ?? this.state.interfaceParents ?? this.hostData.interfaceParents);
        this.session = sessionState(options, this.state, options.hostData, this.viewport);
        this.activity = createHostActivityState(
            options.activity ?? options.session?.activity ?? this.state.activity ??
            options.hostData?.activity);
        this.chatSocial = createChatSocialState(
            options.chatSocial ?? options.session?.chatSocial ?? this.state.chatSocial ??
            options.hostData?.chatSocial);
        const clockSeed = options.clientClock ?? this.state.clientClock ?? this.state.clock ??
            options.session?.clientClock ?? this.chatSocial.chat.clientClock;
        this.clientClock = integer(clockSeed, 100) | 0;
        this.chatSocial.chat.clientClock = this.clientClock;
        /* Keep the established source-preview `clock` state spelling live as
         * well as the explicit runtime spelling, so either snapshot form can
         * restore the native clock without falling back to its initial 100. */
        this.state.clock = this.clientClock;
        this.state.clientClock = this.clientClock;
        const pendingTransmits = options.pendingTransmits ??
            options.session?.pendingTransmits ?? this.state.pendingTransmits;
        this.pendingTransmits = pendingTransmitState(pendingTransmits);
        const pendingDeferred = options.pendingDeferred ??
            options.session?.pendingDeferred ?? this.state.pendingDeferred;
        this.pendingDeferred = pendingDeferredState(pendingDeferred);
        const dbOverride = options.db ?? options.session?.db ?? this.state.db;
        const dbSeed = dbOverride ?? options.hostData?.db;
        const dbData = dbSeed?.data ?? (dbSeed?.dbTables || dbSeed?.tables
            ? dbSeed : this.hostData);
        this.db = createDbState({ data: dbData, iterator: dbSeed?.iterator });
        this.fastDbIteratorRevision = 1;
        /* Only data carried by the HostData identity may enter its shared WASM
         * namespace. A restored/explicit DB state can differ while reusing the
         * same HostData object, so give that shape an isolated namespace and
         * leave its DB calls on the exact generic path. */
        this.fastScalarDataIdentity = dbOverride === undefined
            ? this.hostDataIdentity : this;
        this.fastScalarDbData = dbOverride === undefined ? this.db.data : null;
        this.loot = createLootState(
            options.loot ?? options.session?.loot ?? this.state.loot ?? options.hostData?.loot);
        this.overlay = createOverlayState(
            options.overlay ?? options.session?.overlay ?? this.state.overlay ??
            options.hostData?.overlay);
        this.overlayProviders = options.overlayAdapters && typeof options.overlayAdapters === 'object'
            ? options.overlayAdapters : {};
        this.overlayTreeEnabled = options.overlayTree !== false;
        this.overlayMount = null;
        this.subject = createSubjectState({
            localCoord: this.session.localCoord,
            ...(options.subjectState ?? options.session?.subject ?? this.state.subject ??
                options.hostData?.subject),
        });
        this.subjectProviders = options.subjectProviders && typeof options.subjectProviders === 'object'
            ? options.subjectProviders : {};
        const worldMapSeed = options.worldMap ?? options.session?.worldMap ?? this.state.worldMap;
        const worldMapSource = worldMapSeed?.areas
            ? { worldMap: worldMapSeed, mapElements: this.hostData.mapElements }
            : this.hostData;
        this.worldMap = createWorldMapState(worldMapSource,
            worldMapSeed?.areas ? null : worldMapSeed);
        this.onService = typeof options.onService === 'function' ? options.onService : null;
        this.services = {
            closeModalRequested: false,
            logoutRequested: false,
            resumePauseButton: null,
            crmViewDismissals: 0,
            soundSynthCount: 0,
            lastSoundSynth: null,
            soundSongCount: 0,
            lastSoundSong: null,
            soundJingleCount: 0,
            lastSoundJingle: null,
            soundSongWithSecondaryCount: 0,
            lastSoundSongWithSecondary: null,
            sounds: [],
            messages: [],
            countDialogResponses: [],
            outbound: [],
            deferredDrops: [],
        };
        this.limits = limits(options.limits);
        this.ir = cloneInterface(ir);
        this.interfaceId = integer(ir.interfaceId, 0);
        /* `version` remains the native-compatible per-mutation clock used by
         * read/layout caches and retained change history. `commitRevision` is
         * the renderer clock: it advances once after an outer HOST operation
         * and all of its deferred component work reach a fixed point. */
        this.version = 0;
        this.commitRevision = 0;
        this.committedMutationVersion = 0;
        this.epoch = 0;
        this.cycle = 0;
        this.mounted = false;
        this.dynamicCount = 0;
        this.nextDynamic = 0;
        this.nextDynamicUid = 0x8000;
        this.nextGeneration = 1;
        this.sequence = 0;
        this.operationDepth = 0;
        this.directInvocationDepth = 0;
        this.directInvocationOwnsBoundary = false;
        this.directInvocationOwnsFastTouches = false;
        this.directInvocationOwnsFastDeletes = false;
        this.directInvocationError = null;
        this.dispatchDepth = 0;
        this.invocations = 0;
        this.recordChanges = options.recordChanges !== false;
        this.fastTouchCount = null;
        this.fastDeletedComponents = null;
        this.fastHookPayloadScratch = {
            triggerCount: 0, intCount: 0, stringCount: 0, signatureLength: 0,
            signatureOffset: 0, triggerOffset: 0, intOffset: 0, stringOffset: 0,
        };
        this.dynamicComponentPools = new Map();
        this.changeLog = [];
        this.changeLogHead = 0;
        this.structureRevision = 0;
        this.treeDeltaRevision = 0;
        this.treeDeltaFull = false;
        this.treeDeltaFallbackReason = null;
        this.treeViewportDirty = false;
        this.treeDirty = createTreeDirtySets();
        this.meta = new WeakMap();
        this.byKey = new Map();
        this.byRenderKey = new Map();
        this.byName = new Map();
        this.byFileId = new Map();
        /* Packed redraws create thousands of short-lived rows before React can
         * observe them. Their component records and native identities are
         * complete immediately, but publishing two public lookup entries per
         * row is wasted work when a later packed record replaces/deletes the
         * row in the same transaction. Keep a dense creation-order queue and
         * flush it only at a lookup/layout observation boundary. */
        this.pendingPublicIndexes = [];
        this.byUid = new Map();
        /* CC_FIND is one of the hottest bank redraw operations. Keep the
         * native parent/sub-id identity indexed instead of rediscovering it by
         * scanning the complete mounted interface for every lookup. */
        /* Kept iterable so the C bridge can snapshot every parent/sub-id edge
         * once per invocation instead of issuing one JS query per CC_FIND
         * parent. Delete paths remove every retired parent explicitly. */
        this.dynamicChildren = new Map();
        this.active = null;
        this.dotActive = null;
        this.childIteration = { parent: null, refs: [], index: 0 };
        this.layoutVersion = -1;
        this.layoutCache = [];
        this.boxByComponent = new WeakMap();
        /* Target-only layout helpers walk the complete ancestor chain. CS2
         * frequently asks the same component width/height dozens of times in
         * one unmodified hook, so retain those pure answers for this HOST
         * version. `_touch` advances `version` for every state/viewport/tree
         * mutation; replacing each WeakMap lazily keeps invalidation O(1). */
        this.targetGeometryVersion = -1;
        this.targetGeometryCache = new WeakMap();
        this.targetBoxVersion = -1;
        this.targetBoxCache = new WeakMap();
        this.visibilityVersion = -1;
        this.visibilityCache = new WeakMap();
        /* A CS2 hook commonly performs thousands of component writes before
         * returning (bankmain_draw rebuilds every bank slot). Revalidating the
         * hovered/pressed component after each write forces a full layout for
         * every mutation and turns that bounded native transaction into an
         * O(n^2) browser stall. Native UITree interaction is reconciled at the
         * script/task boundary, so coalesce that work at the outer boundary. */
        this.interactionVisibilityDirty = false;
        this.interaction = {
            /* RS_CS2Host initializes the live canvas pointer to (-1,-1).
             * MOUSE_GETX/Y are global pointer reads, distinct from the
             * component-relative eventMouseX/Y locals latched for a hook. */
            x: -1, y: -1, hover: null, pressed: null, button: null,
            pressX: 0, pressY: 0, pressCycle: 0, clickFired: false,
            dragging: false, dragPickupX: 0, dragPickupY: 0,
            heldKeys: new Set(), pressedKeys: new Set(), menuOpen: false, menuEntries: [],
            antiDrag: false,
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
                    'dynamic child index', component.subId ?? 0,
                    -0x80000000, 0x7fffffff));
                this.dynamicCount++;
                pendingDynamic.splice(index, 1);
            }
            if( pendingDynamic.length === before )
                throw new HostRuntimeError('dynamic component has no live parent', 'BAD_IR');
        }
        this._syncWorldMapDisplaySize();
    }

    /** Run cache/React onLoad hooks exactly once after the script runner is installed. */
    mount() {
        return this._boundary(() => {
            if( this.mounted ) return this._result([], null, this.operationDepth === 1);
            this.mounted = true;
            const intents = [];
            for( const component of [...this.ir.components] ) {
                const resolved = this._resolveHook(component, definition('on_load'));
                if( resolved ) this._emit(component, resolved, baseEvent('mount'), {}, intents);
            }
            return this._result(intents, null, this.operationDepth === 1);
        });
    }

    /** Current paint-order boxes. Each box carries the stable component ref. */
    layout() {
        this._publishPendingPublicIndexes();
        if( this.layoutVersion === this.version ) return this.layoutCache;
        const raw = resolveLayout(
            this.ir, this.state, this.viewport, null, this.structureRevision);
        this.boxByComponent = new WeakMap();
        this.layoutCache = raw.map((box) => {
            /* The live index is already updated by every create/delete. Avoid
             * rebuilding the same component map after every bank mutation. */
            const component = this.byFileId.get(box.fileId);
            box.ref = component ? this.ref(component) : null;
            box.presentation = component ? this._presentation(component) : null;
            if( component ) this.boxByComponent.set(component, box);
            return box;
        });
        this.layoutVersion = this.version;
        return this.layoutCache;
    }

    /** Alias used by transactional tree/VM adapters without changing legacy version semantics. */
    get mutationVersion() {
        return this.version;
    }

    /** Serializable React renderer/state-tree snapshot. */
    snapshot() {
        this._publishPendingPublicIndexes();
        this._retireInvisibleInteraction();
        return {
            schema: HOST_RUNTIME_SCHEMA,
            interfaceId: this.interfaceId,
            epoch: this.epoch,
            version: this.version,
            cycle: this.cycle,
            clientClock: this.clientClock,
            viewport: { ...this.viewport },
            state: cloneState(this.state),
            session: cloneValue(this.session),
            activity: snapshotHostActivityState(this.activity),
            chatSocial: snapshotChatSocialState(this.chatSocial),
            pendingTransmits: snapshotPendingTransmits(this.pendingTransmits),
            pendingDeferred: snapshotPendingDeferred(this.pendingDeferred),
            /* DB records are immutable cache input and already live in
             * hostData. Persist only the cursor, otherwise every React
             * snapshot would clone tens of thousands of rows. */
            db: {
                iterator: {
                    rows: [...this.db.iterator.rows],
                    cursor: this.db.iterator.cursor,
                },
            },
            loot: cloneValue(this.loot),
            overlay: snapshotOverlayState(this.overlay),
            subject: cloneValue(this.subject),
            worldMap: snapshotWorldMapState(this.worldMap),
            services: cloneValue(this.services),
            boxes: this.layout().map(cloneBox),
            interaction: this._interactionView(),
        };
    }

    /**
     * Data needed to repaint the browser preview. The default is detached for
     * direct callers. A worker that immediately hands the payload to
     * postMessage may request the read-only layout view and let structured
     * clone perform the one necessary copy instead of cloning it twice.
     */
    renderSnapshot({ detached = true } = {}) {
        this._publishPendingPublicIndexes();
        this._retireInvisibleInteraction();
        return {
            version: this.version,
            viewport: { ...this.viewport },
            boxes: detached ? this.layout().map(cloneBox) : this.layout(),
        };
    }

    /**
     * Consume the renderer mutations committed since the previous call.
     *
     * `upsert` remains empty here because HostRuntime owns mutable VM records,
     * while immutable paint boxes are projected on demand by projectRenderKey.
     * The record otherwise follows the shared TreeDelta schema and is sealed
     * only after a successful outer HOST boundary. A `full` projection is an
     * explicit instruction to use layout(); callers must never reinterpret it
     * as a dirty-only update.
     */
    consumeTreeDelta() {
        if( this.commitRevision <= this.treeDeltaRevision ) return null;
        const dirty = {};
        let dirtyCount = 0;
        for( const category of TREE_DIRTY_CATEGORIES ) {
            const values = Object.freeze([...this.treeDirty[category]]);
            dirty[category] = values;
            dirtyCount += values.length;
        }
        const baseRevision = this.treeDeltaRevision;
        const projection = this.treeDeltaFull ? 'full' : dirtyCount ? 'dirty' : 'none';
        const delta = Object.freeze({
            schema: TREE_DELTA_SCHEMA,
            baseRevision,
            revision: this.commitRevision,
            mutationVersion: this.committedMutationVersion,
            upsert: Object.freeze([]),
            remove: Object.freeze([]),
            order: Object.freeze([]),
            reorderParents: Object.freeze([]),
            dirty: Object.freeze(dirty),
            dirtyGeometryRoots: dirty[TREE_DIRTY.GEOMETRY],
            projection,
            ...(this.treeDeltaFallbackReason
                ? { fallbackReason: this.treeDeltaFallbackReason } : {}),
            ...(this.treeViewportDirty ? { viewport: Object.freeze({ ...this.viewport }) } : {}),
        });
        this.treeDeltaRevision = this.commitRevision;
        this.treeDeltaFull = false;
        this.treeDeltaFallbackReason = null;
        this.treeViewportDirty = false;
        for( const category of TREE_DIRTY_CATEGORIES ) this.treeDirty[category].clear();
        return delta;
    }

    /** Exact single-node projector used only for proven dirty categories. */
    projectRenderKey(renderKey) {
        this._publishPendingPublicIndexes();
        const component = this.byRenderKey.get(String(renderKey)) || null;
        if( !component || !this.meta.has(component) ) return null;
        const box = layoutBox(
            this.ir, this.state, this.viewport, component, null, this.structureRevision);
        if( !box ) return null;
        box.ref = this.ref(component);
        box.presentation = this._presentation(component);
        return box;
    }

    /** Stable identity. Generation fences deletion/recreation of a dynamic slot. */
    ref(value) {
        const component = this._component(value, false);
        if( !component ) return null;
        return this._materializeRef(component);
    }

    /**
     * Renderer identity is the logical UI slot, not the transient VM handle.
     * A stale ref deliberately resolves to null even when its former slot has
     * since been recreated: generation fencing remains entirely authoritative
     * for script-side component access.
     */
    renderKey(value) {
        const component = this._component(value, false);
        return component ? this._materializeRenderKey(component) : null;
    }

    component(value) {
        this._publishPendingPublicIndexes();
        const component = this._component(value);
        const meta = this.meta.get(component);
        const parent = this._parentOf(component);
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
        const active = dot ? this.dotActive : this.active;
        return active ? this.ref(active) : null;
    }

    /** Change the React stage dimensions and invalidate every resolved box. */
    setViewport(value) {
        return this._boundary(() => {
            const next = viewport(value);
            if( next.width === this.viewport.width && next.height === this.viewport.height )
                return { ...this.viewport };
            this.viewport = next;
            this._record({ kind: 'viewport', viewport: { ...next } });
            this._syncWorldMapDisplaySize();
            this._retireInvisibleInteraction();
            return { ...next };
        });
    }

    /** Component getter surface used by the C HOST bridge and React controls. */
    read(op, value = null, index = null) {
        const name = String(op || '').toLowerCase().replace(/^cc_/, 'if_');
        const component = this._component(value ?? this.active);
        switch( name ) {
            case 'if_getwidth': return this._geometry(component)?.w ?? 0;
            case 'if_getheight': return this._geometry(component)?.h ?? 0;
            case 'if_getx': return this._geometry(component)?.relX ?? 0;
            case 'if_gety': return this._geometry(component)?.relY ?? 0;
            case 'if_getscrollwidth': return component.type === IF_TYPE.layer
                ? component.static.scrollWidth ?? 0 : 0;
            case 'if_getscrollheight': return component.type === IF_TYPE.layer
                ? component.static.scrollHeight ?? 0 : 0;
            case 'if_getscrollx': return this._geometry(component)?.scrollX ??
                component.static.scrollX ?? 0;
            case 'if_getscrolly': return this._geometry(component)?.scrollY ??
                component.static.scrollY ?? 0;
            case 'if_gethide': return Boolean(component.static.hidden);
            case 'if_gettext': return component.static.text ?? '';
            case 'if_getlayer': return component.layer === null ? -1
                : this.ref(this._parentOf(component));
            case 'if_getop': return component.ops?.find((entry) => entry.index === Number(index))?.text || '';
            case 'if_getopbase': return component.runtime.opBase;
            case 'if_gettrans': return component.static.transparency ?? 0;
            case 'if_getcolour': return component.static.color ?? 0;
            case 'if_getfillcolour': return component.static.fillColor ?? 0;
            case 'if_getinvobject': return component.static.objectId ?? 0;
            case 'if_getinvcount': return component.static.objectCount ?? 0;
            case 'if_getid': return this.meta.get(component).subId;
            case 'if_gettargetmask': {
                const clickMask = finiteOptional(component.static.clickMask, 0);
                /* Native decode normalises this once onto
                 * UITreeBehavior.target_mask. IF3's value is bits 11..16 of
                 * its events word; IF1's exported value is already the mask. */
                return component.if3 === false ? Math.max(0, clickMask)
                    : (clickMask >>> 11) & 0x3f;
            }
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
            return this._result(intents, { input: cloneInputState(input) },
                this.operationDepth === 1);
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
            const geometry = this._geometry(component);
            const maxX = Math.max(0,
                finiteOptional(component.static.scrollWidth, 0) - (geometry?.w || 0));
            const maxY = Math.max(0,
                finiteOptional(component.static.scrollHeight, 0) - (geometry?.h || 0));
            return this._setProps(component, op, ['scrollX', 'scrollY'], [
                clampInteger(values[0], 0, maxX), clampInteger(values[1], 0, maxY),
            ]);
        }
        if( op === 'if_setscrollsize' ) {
            const changed = this._setProps(component, op, ['scrollWidth', 'scrollHeight'], values);
            const geometry = this._geometry(component);
            const maxX = Math.max(0,
                finiteOptional(component.static.scrollWidth, 0) - (geometry?.w || 0));
            const maxY = Math.max(0,
                finiteOptional(component.static.scrollHeight, 0) - (geometry?.h || 0));
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
            const submenus = component.runtime.submenus ||= {};
            submenus[opIndex] ||= {};
            if( text ) submenus[opIndex][subIndex] = text;
            else delete submenus[opIndex][subIndex];
            return this._changed('component', component, { op, opIndex, subIndex, text });
        }
        if( op === 'if_clearopsubmenu' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            if( !component.runtime.submenus?.[opIndex] ) return this.ref(component);
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
            (component.runtime.params ||= {})[paramId] = entry;
            return this._changed('component', component, { op, paramId, entry });
        }
        if( op === 'if_setopkey' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const chars = boundedKeyList('operation key characters', values[1]);
            const codes = boundedKeyList('operation key codes', values[2]);
            if( chars.length !== codes.length )
                throw new HostRuntimeError('operation key arrays must have equal length', 'BAD_REQUEST');
            const opKeys = component.runtime.opKeys ||= {};
            opKeys[opIndex] = {
                pairs: chars.map((character, index) => ({ character, code: codes[index] })),
                rate: 0, enabled: true, ignoreHeld: false,
            };
            return this._changed('component', component, {
                op, opIndex, pairs: opKeys[opIndex].pairs,
            });
        }
        if( op === 'if_setopkeyrate' ) {
            const opIndex = boundedInteger('operation index', values[0], 1, 10);
            const opKeys = component.runtime.opKeys ||= {};
            const key = opKeys[opIndex] ||= {
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
            const opKeys = component.runtime.opKeys ||= {};
            const key = opKeys[opIndex] ||= {
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
        const wasHidden = op === 'if_sethide' && Boolean(component.static.hidden);
        const changed = this.recordChanges ? {} : null;
        const changedProps = [];
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
            changedProps.push(prop);
            if( changed ) changed[prop] = value;
        }
        if( changedProps.length === 0 ) return this.ref(component);
        /* Runtime component fields are authoritative until a later script
         * writes them. Remove overwritten expressions once per operation;
         * object setters can change ten fields and used to allocate ten
         * successively-filtered arrays for one logical native write. */
        if( component.dynamic?.length ) {
            const overwritten = new Set(changedProps);
            component.dynamic = component.dynamic.filter((binding) => !overwritten.has(binding.prop));
        }
        if( wasHidden && component.static.hidden === false )
            this.pendingTransmits.widgetsLoaded = true;
        return this._fastChangedComponent(component, op, changed);
    }

    _fastSetPosition(component, x, y, xMode, yMode) {
        const props = component.static;
        const cx = props.x !== x;
        const cy = props.y !== y;
        const cxMode = props.xMode !== xMode;
        const cyMode = props.yMode !== yMode;
        if( !cx && !cy && !cxMode && !cyMode ) return;
        const changed = this.recordChanges ? {} : null;
        if( cx ) this._fastAssignProp(component, 'x', x, changed);
        if( cy ) this._fastAssignProp(component, 'y', y, changed);
        if( cxMode ) this._fastAssignProp(component, 'xMode', xMode, changed);
        if( cyMode ) this._fastAssignProp(component, 'yMode', yMode, changed);
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter((binding) =>
            !(cx && binding.prop === 'x') && !(cy && binding.prop === 'y') &&
            !(cxMode && binding.prop === 'xMode') && !(cyMode && binding.prop === 'yMode'));
        this._fastChangedComponent(component, 'if_setposition', changed, false);
    }

    _fastSetSize(component, width, height, widthMode, heightMode) {
        const props = component.static;
        const cw = props.width !== width;
        const ch = props.height !== height;
        const cwMode = props.widthMode !== widthMode;
        const chMode = props.heightMode !== heightMode;
        if( !cw && !ch && !cwMode && !chMode ) return;
        const changed = this.recordChanges ? {} : null;
        if( cw ) this._fastAssignProp(component, 'width', width, changed);
        if( ch ) this._fastAssignProp(component, 'height', height, changed);
        if( cwMode ) this._fastAssignProp(component, 'widthMode', widthMode, changed);
        if( chMode ) this._fastAssignProp(component, 'heightMode', heightMode, changed);
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter((binding) =>
            !(cw && binding.prop === 'width') && !(ch && binding.prop === 'height') &&
            !(cwMode && binding.prop === 'widthMode') &&
            !(chMode && binding.prop === 'heightMode'));
        this._fastChangedComponent(component, 'if_setsize', changed, false);
    }

    _fastSetHidden(component, hidden) {
        const before = Boolean(component.static.hidden);
        if( before === hidden ) return;
        const changed = this.recordChanges ? { hidden } : null;
        component.static.hidden = hidden;
        if( component.props ) component.props.hidden = hidden;
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter(
            (binding) => binding.prop !== 'hidden');
        if( before && !hidden ) this.pendingTransmits.widgetsLoaded = true;
        this._fastChangedComponent(component, 'if_sethide', changed, false);
    }

    _fastSetTransparency(component, transparency) {
        if( component.static.transparency === transparency ) return;
        const changed = this.recordChanges ? { transparency } : null;
        component.static.transparency = transparency;
        if( component.props ) component.props.transparency = transparency;
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter(
            (binding) => binding.prop !== 'transparency');
        this._fastChangedComponent(component, 'if_settrans', changed, false);
    }

    _fastSetSimpleProp(component, op, name, value, booleanValue = false) {
        if( !this._supports(component, op) ) return;
        if( booleanValue ) value = Boolean(value);
        else if( name === 'text' || name === 'targetVerb' )
            value = boundedText(name, value ?? '');
        if( component.static[name] === value ) return;
        const changed = this.recordChanges ? { [name]: value } : null;
        this._fastAssignProp(component, name, value, null);
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter(
            (binding) => binding.prop !== name);
        this._fastChangedComponent(component, op, changed, false);
    }

    _fastSetTextAlign(component, halign, valign, lineHeight) {
        if( !this._supports(component, 'if_settextalign') ) return;
        const props = component.static;
        const ch = props.halign !== halign;
        const cv = props.valign !== valign;
        const cl = props.lineHeight !== lineHeight;
        if( !ch && !cv && !cl ) return;
        const changed = this.recordChanges ? {} : null;
        if( ch ) this._fastAssignProp(component, 'halign', halign, changed);
        if( cv ) this._fastAssignProp(component, 'valign', valign, changed);
        if( cl ) this._fastAssignProp(component, 'lineHeight', lineHeight, changed);
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter((binding) =>
            !(ch && binding.prop === 'halign') && !(cv && binding.prop === 'valign') &&
            !(cl && binding.prop === 'lineHeight'));
        this._fastChangedComponent(component, 'if_settextalign', changed, false);
    }

    _fastSetOp(component, index, rawText) {
        if( index < 1 || index > 10 ) return;
        const text = boundedText('operation text', rawText ?? '');
        component.ops ||= [];
        const previous = component.ops.find((entry) => entry.index === index)?.text || '';
        if( previous === text ) return;
        if( component.ops.length === 0 && text ) component.ops = [{ index, text }];
        else {
            component.ops = component.ops.filter((entry) => entry.index !== index);
            if( text ) component.ops.push({ index, text });
            component.ops.sort((left, right) => left.index - right.index);
        }
        if( this.recordChanges ) this._record({
            kind: 'component', ref: this.ref(component),
            op: 'if_setop', index, text,
        });
        else {
            this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
            this._touch(true, true);
        }
    }

    _fastSetOpBase(component, rawText) {
        const text = boundedText('operation base', rawText ?? '');
        if( component.runtime.opBase === text ) return;
        component.runtime.opBase = text;
        if( this.recordChanges ) this._record({
            kind: 'component', ref: this.ref(component),
            op: 'if_setopbase', text,
        });
        else {
            this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
            this._touch(true, true);
        }
    }

    _fastSetObject(component, objectId, count, numberMode) {
        const renderObjectId = objectId > 0
            ? resolveCountObject(this.hostData.objects, objectId, count) : -1;
        const modelKind = objectId > 0 ? 'object' : 'none';
        let xAngle;
        let yAngle;
        let zoom;
        let xOffset;
        let yOffset;
        let camera = false;
        if( component.type === IF_TYPE.model && objectId > 0 ) {
            const object = this.hostData.objects[String(renderObjectId)] ||
                this.hostData.objects[String(objectId)] || null;
            if( object ) {
                camera = true;
                xAngle = finiteOptional(object.xan2d, 0);
                yAngle = finiteOptional(object.yan2d, 0);
                zoom = finiteOptional(object.zoom2d, 2000);
                if( zoom <= 0 ) zoom = 2000;
                xOffset = 0;
                yOffset = finiteOptional(object.offsetY2d ?? object.offset_y2d, 0);
            }
        }
        const props = component.static;
        if( props.objectId === objectId && props.objectCount === count &&
            props.objectNumMode === numberMode && props.modelKind === modelKind &&
            props.modelSourceId === renderObjectId && (!camera ||
                (props.xAngle === xAngle && props.yAngle === yAngle && props.zoom === zoom &&
                 props.xOffset === xOffset && props.yOffset === yOffset)) )
            return;

        const changed = this.recordChanges ? {} : null;
        let changedMask = 0;
        if( props.objectId !== objectId ) {
            this._fastAssignProp(component, 'objectId', objectId, changed); changedMask |= 1 << 0;
        }
        if( props.objectCount !== count ) {
            this._fastAssignProp(component, 'objectCount', count, changed); changedMask |= 1 << 1;
        }
        if( props.objectNumMode !== numberMode ) {
            this._fastAssignProp(component, 'objectNumMode', numberMode, changed); changedMask |= 1 << 2;
        }
        if( props.modelKind !== modelKind ) {
            this._fastAssignProp(component, 'modelKind', modelKind, changed); changedMask |= 1 << 3;
        }
        if( props.modelSourceId !== renderObjectId ) {
            this._fastAssignProp(component, 'modelSourceId', renderObjectId, changed);
            changedMask |= 1 << 4;
        }
        if( camera ) {
            if( props.xAngle !== xAngle ) {
                this._fastAssignProp(component, 'xAngle', xAngle, changed); changedMask |= 1 << 5;
            }
            if( props.yAngle !== yAngle ) {
                this._fastAssignProp(component, 'yAngle', yAngle, changed); changedMask |= 1 << 6;
            }
            if( props.zoom !== zoom ) {
                this._fastAssignProp(component, 'zoom', zoom, changed); changedMask |= 1 << 7;
            }
            if( props.xOffset !== xOffset ) {
                this._fastAssignProp(component, 'xOffset', xOffset, changed); changedMask |= 1 << 8;
            }
            if( props.yOffset !== yOffset ) {
                this._fastAssignProp(component, 'yOffset', yOffset, changed); changedMask |= 1 << 9;
            }
        }
        if( component.dynamic?.length ) component.dynamic = component.dynamic.filter((binding) =>
            !fastObjectPropChanged(changedMask, binding.prop));
        this._fastChangedComponent(component, 'if_setobject', changed, false);
    }

    _fastAssignProp(component, prop, value, changed) {
        component.static[prop] = value;
        if( component.props ) component.props[prop] = value;
        if( changed ) changed[prop] = value;
    }

    _fastChangedComponent(component, op, props, returnRef = true) {
        const ref = this.recordChanges || returnRef ? this.ref(component) : undefined;
        if( this.recordChanges ) {
            this._record({ kind: 'component', ref, op, props });
            return returnRef ? ref : undefined;
        }
        this._markTreeComponent(component, treeDirtyForOperation(op));
        this._touch(true, true);
        return returnRef ? ref : undefined;
    }

    _fastSetPackedHook(component, descriptor, records, base, arena, arenaView) {
        component.hooks ||= {};
        const exact = descriptor.canonical;
        const aliases = hookAliases(descriptor);
        let presentCount = 0;
        let exactPresent = false;
        for( const alias of aliases ) {
            if( !Object.prototype.hasOwnProperty.call(component.hooks, alias) ) continue;
            presentCount++;
            exactPresent ||= alias === exact;
        }
        const installs = records[base + 2] > 0;
        if( !installs && presentCount === 0 ) return;
        const payload = installs ? fastHookPayload(
            records, base, arena, arenaView, this.fastHookPayloadScratch) : null;
        if( installs && presentCount === 1 && exactPresent && fastHookMatches(
            component.hooks[exact], records, base, arena, arenaView, payload) )
            return;

        const binding = installs
            ? fastHookBinding(records, base, arena, arenaView, payload) : null;
        for( const alias of aliases ) delete component.hooks[alias];
        if( binding ) component.hooks[exact] = binding;
        if( this.recordChanges ) this._record({
            kind: 'hook', ref: this.ref(component),
            hook: exact, canonical: descriptor.canonical, scriptId: records[base + 2],
        });
        else {
            this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
            this._touch(true, true);
        }
    }

    /* CC_CREATE is followed by setters targeting its batch-local token in the
     * cache's dynamic-list builders. The ordinary packed loop must resolve the
     * token and re-check authored bindings for every record. A brand-new
     * component has no authored/runtime overrides, so commit that contiguous
     * run directly while retaining each operation's no-op and version rules.
     * Stop at the first observer, different target, nested create, or setter
     * outside this proven vocabulary; the outer loop handles it unchanged. */
    _fastApplyFreshPackedRun(component, token, records, start, recordCount, arena, arenaView) {
        const props = component.static;
        let touches = 0;
        let index = start;
        try {
            for( ; index < recordCount; index++ ) {
                const base = index * FAST_HOST_RECORD_WORDS;
                if( !records[base + 11] || records[base + 1] !== token ) break;
                const kind = records[base];
                if( kind === FAST_HOST_KINDS.CC_SETPOSITION ) {
                    const x = records[base + 2];
                    const y = records[base + 3];
                    const xMode = records[base + 4];
                    const yMode = records[base + 5];
                    if( props.x !== x || props.y !== y ||
                        props.xMode !== xMode || props.yMode !== yMode ) {
                        props.x = x; props.y = y;
                        props.xMode = xMode; props.yMode = yMode;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETSIZE ) {
                    const width = records[base + 2];
                    const height = records[base + 3];
                    const widthMode = records[base + 4];
                    const heightMode = records[base + 5];
                    if( props.width !== width || props.height !== height ||
                        props.widthMode !== widthMode || props.heightMode !== heightMode ) {
                        props.width = width; props.height = height;
                        props.widthMode = widthMode; props.heightMode = heightMode;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETHIDE ) {
                    const hidden = Boolean(records[base + 2]);
                    const before = Boolean(props.hidden);
                    if( before !== hidden ) {
                        props.hidden = hidden;
                        if( before && !hidden ) this.pendingTransmits.widgetsLoaded = true;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETTRANS ) {
                    const transparency = records[base + 2];
                    if( props.transparency !== transparency ) {
                        props.transparency = transparency;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETCOLOUR ) {
                    const color = records[base + 2];
                    if( props.color !== color ) {
                        props.color = color;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETFILL ) {
                    if( component.type !== IF_TYPE.rectangle && component.type !== 10 ) continue;
                    const fill = Boolean(records[base + 2]);
                    if( props.fill !== fill ) {
                        props.fill = fill;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETGRAPHIC ) {
                    if( component.type !== IF_TYPE.graphic ) continue;
                    const sprite = records[base + 2];
                    if( props.sprite !== sprite ) {
                        props.sprite = sprite;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETTEXT ) {
                    /* Decode first: the ordinary packed call evaluates its
                     * string argument before discovering an unsupported
                     * dynamic type. A corrupt arena therefore still fails at
                     * the identical record boundary. */
                    const rawText = fastRecordString(records, base, arena);
                    if( component.type !== IF_TYPE.text ) continue;
                    const text = boundedText('text', rawText);
                    if( props.text !== text ) {
                        props.text = text;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETTEXTFONT ) {
                    if( component.type !== IF_TYPE.text ) continue;
                    const font = records[base + 2];
                    if( props.font !== font ) {
                        props.font = font;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETTEXTALIGN ) {
                    if( component.type !== IF_TYPE.text ) continue;
                    const halign = records[base + 2];
                    const valign = records[base + 3];
                    const lineHeight = records[base + 4];
                    if( props.halign !== halign || props.valign !== valign ||
                        props.lineHeight !== lineHeight ) {
                        props.halign = halign; props.valign = valign;
                        props.lineHeight = lineHeight;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETTEXTSHADOW ) {
                    if( component.type !== IF_TYPE.text ) continue;
                    const shadow = Boolean(records[base + 2]);
                    if( props.shadow !== shadow ) {
                        props.shadow = shadow;
                        touches++;
                    }
                } else if( FAST_HOST_HOOK_DEFINITIONS[kind] ) {
                    const descriptor = FAST_HOST_HOOK_DEFINITIONS[kind];
                    let aliasPresent = false;
                    if( component.hooks ) {
                        for( const alias of hookAliases(descriptor) ) {
                            if( !Object.prototype.hasOwnProperty.call(component.hooks, alias) )
                                continue;
                            aliasPresent = true;
                            break;
                        }
                    }
                    if( aliasPresent ) {
                        this._fastSetPackedHook(
                            component, descriptor, records, base, arena, arenaView);
                    } else if( records[base + 2] > 0 ) {
                        component.hooks ||= {};
                        const payload = fastHookPayload(
                            records, base, arena, arenaView, this.fastHookPayloadScratch);
                        component.hooks[descriptor.canonical] = fastHookBinding(
                            records, base, arena, arenaView, payload);
                        touches++;
                    } else component.hooks ||= {};
                } else if( kind === FAST_HOST_KINDS.CC_SETOPBASE ) {
                    const text = boundedText(
                        'operation base', fastRecordString(records, base, arena));
                    if( component.runtime.opBase !== text ) {
                        component.runtime.opBase = text;
                        touches++;
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETOP ) {
                    const opIndex = records[base + 2];
                    const rawText = fastRecordString(records, base, arena);
                    if( opIndex < 1 || opIndex > 10 ) continue;
                    const text = boundedText('operation text', rawText);
                    if( !component.ops?.length ) {
                        if( text ) {
                            component.ops = [{ index: opIndex, text }];
                            touches++;
                        }
                    } else this._fastSetOp(component, opIndex, text);
                } else if( kind === FAST_HOST_KINDS.CC_CLEAROPS ) {
                    if( component.ops?.length ) {
                        component.ops = [];
                        touches++;
                    }
                } else break;
            }
        } finally {
            /* requestFastPackedBatch establishes the deferred counter before
             * entering this path. Keep partial writes/versioning observable if
             * a malformed later string or hook throws, exactly like replaying
             * the preceding records one by one. */
            this.fastTouchCount += touches;
        }
        return index - 1;
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
        const descriptor = typeof eventName === 'object' && eventName
            ? eventName : definition(eventName);
        const exact = typeof eventName === 'string'
            ? exactHookKey(eventName, descriptor) : descriptor.canonical;
        component.hooks ||= {};
        const aliases = hookAliases(descriptor);
        let presentCount = 0;
        let exactPresent = false;
        for( const alias of aliases ) {
            if( !Object.prototype.hasOwnProperty.call(component.hooks, alias) ) continue;
            presentCount++;
            exactPresent ||= alias === exact;
        }
        const installs = Boolean(binding && scriptId(binding) > 0);
        /* bankmain_draw rebinds the same drag listener for every dirty pass.
         * Once the exact canonical slot already owns an equivalent binding,
         * there is no tree mutation to version, retain, or repaint. Do still
         * canonicalise imported aliases and remove duplicate aliases. */
        if( installs && presentCount === 1 && exactPresent &&
            hookBindingMatchesInput(component.hooks[exact], binding, this) )
            return this.ref(component);
        if( !installs && presentCount === 0 ) return this.ref(component);
        const normalized = installs ? normalizeBinding(binding, this) : null;
        /* Component-object hook args normalize to stable refs. The cheap raw
         * comparison above intentionally falls through for that uncommon
         * shape; compare once more after normalization before recording. */
        if( normalized && presentCount === 1 && exactPresent &&
            hookBindingsEqual(component.hooks[exact], normalized) ) return this.ref(component);
        for( const alias of aliases ) delete component.hooks[alias];
        if( normalized ) component.hooks[exact] = normalized;
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
        /* UITree stores the script-provided dynamic child sub-id as a signed
         * int. Values such as -1 are used by shipped interfaces; the separate
         * transient packed component UID remains in its 0x8000..0xffff band. */
        const subId = boundedInteger(
            'child index', rawSubId, -0x80000000, 0x7fffffff);
        const existing = this.findChild(parent, subId, false);
        if( existing ) {
            const live = this._component(existing);
            if( !this.recordChanges && this.fastDeletedComponents )
                this._deleteForFastReplace(live);
            else this._delete(live);
        }
        if( this.dynamicCount >= this.limits.dynamicComponents )
            throw new HostRuntimeError('dynamic component limit reached', 'LIMIT');
        const liveComponentCount = this.ir.components.length -
            (this.fastDeletedComponents?.size || 0);
        if( liveComponentCount >= this.limits.components )
            throw new HostRuntimeError('component limit reached', 'LIMIT');
        const pool = !this.recordChanges ? this.dynamicComponentPools.get(type) : null;
        let component = pool?.pop() || null;
        if( component ) {
            const staticProps = resetDynamicProps(component.static, type, kind);
            component.fileId = `@host:${this.nextDynamic++}`;
            component.name = `${parent.name}[${subId}]`;
            component.kind = kind;
            component.type = type;
            component.layer = parent.fileId;
            component.subId = subId;
            component.props = staticProps;
            component.static = staticProps;
            component.authoredProps = EMPTY_AUTHORED_PROPS;
            component.dynamic = EMPTY_DYNAMIC_ARRAY;
            component.ops = EMPTY_DYNAMIC_ARRAY;
            component.events = null;
            component.hooks = null;
            component.triggers = null;
            component.dependencies = EMPTY_DYNAMIC_ARRAY;
            component.scriptBindings = EMPTY_DYNAMIC_ARRAY;
            component.rawFields = null;
            component.runtimeDynamic = true;
            resetRuntimeState(component.runtime);
        } else {
            const staticProps = dynamicProps(type, kind);
            component = {
                fileId: `@host:${this.nextDynamic++}`,
                name: `${parent.name}[${subId}]`,
                kind, type, layer: parent.fileId, subId,
                props: staticProps, static: staticProps, authoredProps: EMPTY_AUTHORED_PROPS,
                dynamic: EMPTY_DYNAMIC_ARRAY, ops: EMPTY_DYNAMIC_ARRAY,
                events: null, hooks: null, triggers: null,
                dependencies: EMPTY_DYNAMIC_ARRAY, scriptBindings: EMPTY_DYNAMIC_ARRAY,
                rawFields: null, runtimeDynamic: true,
                runtime: emptyRuntimeState(),
            };
        }
        this.ir.components.push(component);
        this.structureRevision++;
        this._indexDynamic(
            component, parent, subId, component[RECYCLED_DYNAMIC_META] || null);
        this.dynamicCount++;
        const ref = this.meta.get(component).ref;
        if( this.recordChanges ) this._record({
            kind: 'create', ref, parent: this.meta.get(parent).ref, type, subId,
        });
        else {
            this._markTreeFull('topology-create');
            this._touch(true, true);
        }
        this.setActive(component, { dot });
        return ref;
    }

    /* The packed C bridge has already resolved the parent and transports every
     * numeric field through an Int32Array. Avoid resolving that same parent
     * three more times through the public reference machinery for each of the
     * thousands of CC_CREATEs in a redraw. This is deliberately production
     * only: public/change-recording callers retain the fully defensive path
     * above, while malformed packed types still fall back to its validation. */
    _fastCreatePackedChild(parent, type, subId, dot) {
        if( this.recordChanges || !Number.isInteger(type) || type < 0 || type > 255 ) {
            const ref = this._createChild(parent, type, subId, { dot });
            return this._component(ref);
        }
        const kind = TYPE_KIND.get(type) || 'Object';
        const existing = this.dynamicChildren.get(parent)?.get(subId) || null;
        if( existing ) this._deleteForFastReplace(existing);
        if( this.dynamicCount >= this.limits.dynamicComponents )
            throw new HostRuntimeError('dynamic component limit reached', 'LIMIT');
        const liveComponentCount = this.ir.components.length -
            (this.fastDeletedComponents?.size || 0);
        if( liveComponentCount >= this.limits.components )
            throw new HostRuntimeError('component limit reached', 'LIMIT');

        const pool = this.dynamicComponentPools.get(type);
        let component = pool?.pop() || null;
        if( component ) {
            const staticProps = resetDynamicProps(component.static, type, kind);
            component.fileId = `@host:${this.nextDynamic++}`;
            component.name = `${parent.name}[${subId}]`;
            component.kind = kind;
            component.type = type;
            component.layer = parent.fileId;
            component.subId = subId;
            component.props = staticProps;
            component.static = staticProps;
            component.authoredProps = EMPTY_AUTHORED_PROPS;
            component.dynamic = EMPTY_DYNAMIC_ARRAY;
            component.ops = EMPTY_DYNAMIC_ARRAY;
            component.events = null;
            component.hooks = null;
            component.triggers = null;
            component.dependencies = EMPTY_DYNAMIC_ARRAY;
            component.scriptBindings = EMPTY_DYNAMIC_ARRAY;
            component.rawFields = null;
            component.runtimeDynamic = true;
            resetRuntimeState(component.runtime);
        } else {
            const staticProps = dynamicProps(type, kind);
            component = {
                fileId: `@host:${this.nextDynamic++}`,
                name: `${parent.name}[${subId}]`,
                kind, type, layer: parent.fileId, subId,
                props: staticProps, static: staticProps,
                authoredProps: EMPTY_AUTHORED_PROPS,
                dynamic: EMPTY_DYNAMIC_ARRAY, ops: EMPTY_DYNAMIC_ARRAY,
                events: null, hooks: null, triggers: null,
                dependencies: EMPTY_DYNAMIC_ARRAY,
                scriptBindings: EMPTY_DYNAMIC_ARRAY,
                rawFields: null, runtimeDynamic: true,
                runtime: emptyRuntimeState(),
            };
        }
        this.ir.components.push(component);
        this.structureRevision++;
        const componentMeta = this._fastIndexDynamic(
            component, parent, subId, component[RECYCLED_DYNAMIC_META] || null);
        this.dynamicCount++;
        this._markTreeFull('topology-create');
        this.fastTouchCount++;
        /* Packed C code consumes the fresh UID, not the public JS ref. Keep the
         * active target as the live component until a public observer asks for
         * a ref, avoiding a frozen object and transient key for every row that
         * is immediately superseded by the next CC_CREATE. */
        if( dot ) this.dotActive = component;
        else this.active = component;
        component[RECYCLED_DYNAMIC_META] = componentMeta;
        return component;
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
        /* The C client treats every signed-int sub-id as a lookup and reports
         * not-found for values outside the dynamic child band.  Do not turn a
         * cache script's miss into a JS exception at the HOST boundary. */
        const subId = finiteValue('child index', rawSubId);
        const found = this.dynamicChildren.get(parent)?.get(subId) || null;
        const ref = found ? this.ref(found) : null;
        /* exec_cc_find only writes the implicit target on success. A miss
         * pushes false and leaves the prior CC/dot target intact. */
        if( updateActive && ref ) {
            if( dot ) this.dotActive = ref;
            else this.active = ref;
        }
        return ref;
    }

    /** Dynamic children in canonical ascending sub-id order. */
    children(parentValue, { startIndex = 0 } = {}) {
        const parent = this._component(parentValue);
        const start = boundedInteger(
            'child start index', startIndex, -0x80000000, 0x7fffffff);
        return [...(this.dynamicChildren.get(parent)?.entries() || [])]
            .filter(([subId]) => subId >= start)
            .sort(([left], [right]) => left - right)
            .map(([, component]) => this.ref(component));
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
            const doomed = new Set(this.dynamicChildren.get(parent)?.values() || []);
            return this._deleteSet(doomed);
        });
    }

    _deleteSet(initial) {
        if( initial.size === 0 ) return [];
        const doomed = new Set(initial);
        const pending = [...initial];
        while( pending.length ) {
            const component = pending.pop();
            for( const child of this.dynamicChildren.get(component)?.values() || [] ) {
                if( doomed.has(child) ) continue;
                doomed.add(child);
                pending.push(child);
            }
        }
        const refs = [...doomed].map((component) => this.ref(component));
        if( this.fastDeletedComponents && this.recordChanges ) {
            for( const component of doomed ) this.fastDeletedComponents.add(component);
        } else this.ir.components = this.ir.components.filter((component) => !doomed.has(component));
        this.structureRevision++;
        /* Resolve every parent before removing any file-id entries: a doomed
         * dynamic parent may precede its doomed descendants in this set. */
        for( const component of doomed ) {
            const meta = this.meta.get(component);
            if( meta?.dynamic ) {
                const parent = this._parentOf(component);
                const siblings = parent && this.dynamicChildren.get(parent);
                if( siblings?.get(meta.subId) === component ) siblings.delete(meta.subId);
                if( siblings?.size === 0 ) this.dynamicChildren.delete(parent);
            }
            this.dynamicChildren.delete(component);
        }
        for( const component of doomed ) {
            const meta = this.meta.get(component);
            this._removePendingPublicIndex(component, meta);
            this.byKey.delete(meta.key);
            if( meta.renderKey && this.byRenderKey.get(meta.renderKey) === component )
                this.byRenderKey.delete(meta.renderKey);
            if( this.byName.get(component.name) === component )
                this.byName.delete(component.name);
            if( this.byFileId.get(component.fileId) === component )
                this.byFileId.delete(component.fileId);
            if( meta.componentId !== null && this.byUid.get(meta.componentId) === component )
                this.byUid.delete(meta.componentId);
            this.dynamicCount -= Number(meta.dynamic);
            this.meta.delete(component);
            if( !this.recordChanges && meta.dynamic ) {
                component[RECYCLED_DYNAMIC_META] = meta;
                let pool = this.dynamicComponentPools.get(component.type);
                if( !pool ) {
                    pool = [];
                    this.dynamicComponentPools.set(component.type, pool);
                }
                pool.push(component);
            }
        }
        if( this.active && (doomed.has(this.active) ||
            refs.some((ref) => sameRef(ref, this.active))) ) this.active = null;
        if( this.dotActive && (doomed.has(this.dotActive) ||
            refs.some((ref) => sameRef(ref, this.dotActive))) ) this.dotActive = null;
        if( this.recordChanges ) this._record({ kind: 'delete', refs });
        else {
            this._markTreeFull('topology-delete');
            this._touch(true, true);
        }
        this._retireDeletedInteraction(refs);
        return refs;
    }

    /* Production redraws replace thousands of dynamic slots inside one packed
     * transaction. The public delete path materialises a Set, traversal array,
     * refs array and several callback closures for each slot. Here the target
     * is already proven dynamic and change recording is disabled, so walk its
     * tree once, deindex directly, and let the batch's single final filter drop
     * those exact component objects. Paint order remains unchanged because the
     * replacement itself is still appended like native UITree_CcCreate. */
    _deleteForFastReplace(component) {
        const parent = this._parentOf(component);
        const descendants = this.dynamicChildren.get(component);
        let doomed = null;
        if( descendants?.size ) {
            doomed = [component, ...descendants.values()];
            for( let index = 1; index < doomed.length; index++ ) {
                const children = this.dynamicChildren.get(doomed[index]);
                if( children?.size ) doomed.push(...children.values());
            }
        }
        const count = doomed ? doomed.length : 1;
        for( let index = 0; index < count; index++ ) {
            const target = doomed ? doomed[index] : component;
            const meta = this.meta.get(target);
            const ref = meta.ref;
            this._removePendingPublicIndex(target, meta);
            this.fastDeletedComponents.add(target);
            this.dynamicChildren.delete(target);
            this.byKey.delete(meta.key);
            if( meta.renderKey && this.byRenderKey.get(meta.renderKey) === target )
                this.byRenderKey.delete(meta.renderKey);
            if( this.byName.get(target.name) === target ) this.byName.delete(target.name);
            if( this.byFileId.get(target.fileId) === target )
                this.byFileId.delete(target.fileId);
            if( this.byUid.get(meta.componentId) === target ) this.byUid.delete(meta.componentId);
            this.dynamicCount--;
            this.meta.delete(target);
            if( this.active === target || (ref && sameRef(ref, this.active)) ) this.active = null;
            if( this.dotActive === target || (ref && sameRef(ref, this.dotActive)) )
                this.dotActive = null;
            if( ref && sameRef(ref, this.interaction.hover) ) this.interaction.hover = null;
            if( ref && sameRef(ref, this.interaction.pressed) ) this.interaction.pressed = null;
        }
        const siblings = parent && this.dynamicChildren.get(parent);
        /* meta was removed above; the root's public sub-id is also the slot in
         * the parent's map, so identity removal avoids retaining it without
         * needing a second ref allocation. */
        if( siblings ) {
            for( const [slot, child] of siblings ) {
                if( child !== component ) continue;
                siblings.delete(slot);
                break;
            }
            if( siblings.size === 0 ) this.dynamicChildren.delete(parent);
        }
        if( !this.interaction.pressed ) {
            this.interaction.button = null;
            this.interaction.dragging = false;
            this.interaction.clickFired = false;
            this.interaction.dragPickupX = 0;
            this.interaction.dragPickupY = 0;
        }
        this.structureRevision++;
        this._markTreeFull('topology-replace');
        this._touch(true, true);
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
            : kind === 'stat' ? (id === 3 ? 10 : 1)
                : kind === 'statxp' ? (id === 3 ? 1154 : 0) : 0;
    }

    /** Write state and defer its matching transmit hooks to the next logic tick. */
    writeState(kind, rawId, value, options = {}) {
        return this._boundary(() => {
            if( !STATE_KINDS.has(kind) )
                throw new HostRuntimeError(`unsupported state kind ${kind}`, 'BAD_REQUEST');
            const id = stateId(rawId);
            const key = kind === 'inv' ? `invobj:${id}` : `${kind}:${id}`;
            const next = kind === 'inv' ? inventoryState(value)
                : kind === 'varcstr' ? boundedText(key, value ?? '') : finiteValue(key, value);
            const before = this.readState(kind, id);
            if( stateValuesEqual(before, next) )
                return this._result([], { key, value: cloneValue(before), changed: false },
                    this.operationDepth === 1);
            this.state[key] = next;
            this._record({ kind: 'state', key, value: cloneValue(next) });
            if( options.transmit !== false ) this._queueStateTransmit(kind, id, options);
            return this._result([], { key, value: cloneValue(next), changed: true },
                this.operationDepth === 1);
        });
    }

    /**
     * Execute one named HOST operation. Requests are ordinary JS records and
     * results are ordinary JS values; component refs preserve dynamic identity.
     */
    fastHostInventorySnapshot(rawId) {
        const numericId = finiteValue('inventory id', rawId);
        /* rs_cs2_inv_get_obj/num return their empty defaults for negative
         * inventory ids instead of aborting the script.  An empty snapshot
         * gives the compact C lookup exactly the same result. */
        if( numericId < 0 || numericId > 0x7fffffff ) return new Int32Array(0);
        const id = stateId(numericId);
        const source = this.state[`invslots:${id}`] || {};
        const rows = [];
        for( const [rawSlot, entry] of Object.entries(source) ) {
            const slot = Number(rawSlot);
            if( !Number.isInteger(slot) || slot < 0 || slot > 0x7fffffff ) continue;
            const objectId = Number(entry?.id ?? entry?.objectId ?? -1) | 0;
            rows.push(slot | 0, objectId > 0 ? objectId : -1,
                objectId > 0 ? Number(entry?.count ?? 0) | 0 : 0);
        }
        if( rows.length > 3 ) {
            const records = [];
            for( let index = 0; index < rows.length; index += 3 )
                records.push(rows.slice(index, index + 3));
            records.sort((left, right) => left[0] - right[0]);
            return Int32Array.from(records.flat());
        }
        return Int32Array.from(rows);
    }

    fastHostChildrenSnapshot(parentId) {
        const parent = this._component(parentId, false);
        if( !parent ) return null;
        const rows = [...(this.dynamicChildren.get(parent)?.entries() || [])]
            .sort(([left], [right]) => left - right)
            .flatMap(([subId, component]) => [subId | 0,
                this.meta.get(component).componentId | 0]);
        return Int32Array.from(rows);
    }

    /** One borrowed snapshot for every dynamic edge in the current tree.
     * CC_FIND-heavy scripts otherwise cross C -> JS once per distinct parent
     * even though all those queries observe the same tree revision. */
    fastHostAllChildrenSnapshot() {
        let count = 0;
        for( const children of this.dynamicChildren.values() ) count += children.size;
        if( count > this.limits.dynamicComponents ) throw new HostRuntimeError(
            'dynamic child snapshot exceeds the configured limit', 'LIMIT');
        const rows = new Int32Array(count * 3);
        let index = 0;
        for( const [parent, children] of this.dynamicChildren ) {
            const parentId = this.meta.get(parent)?.componentId;
            if( !Number.isInteger(parentId) ) continue;
            for( const [subId, child] of children ) {
                const childId = this.meta.get(child)?.componentId;
                if( !Number.isInteger(childId) ) continue;
                rows[index++] = parentId | 0;
                rows[index++] = subId | 0;
                rows[index++] = childId | 0;
            }
        }
        return index === rows.length ? rows : rows.slice(0, index);
    }

    /**
     * Write the global CC_FIND table directly into a borrowed WASM heap view.
     * The C bridge may provide a provisional capacity and retry with the
     * returned authoritative count; each call fills only that bounded prefix.
     * No view is retained after this synchronous call.
     */
    fastHostAllChildrenWrite(target, offset, capacity) {
        if( !(target instanceof Int32Array) || !Number.isInteger(offset) || offset < 0 ||
            !Number.isInteger(capacity) || capacity < 0 || capacity > 65536 ||
            offset + capacity * 3 > target.length )
            throw new HostRuntimeError('malformed borrowed child table', 'BAD_REQUEST');
        const count = this.dynamicCount;
        if( count > this.limits.dynamicComponents ) throw new HostRuntimeError(
            'dynamic child snapshot exceeds the configured limit', 'LIMIT');
        if( capacity === 0 || count === 0 ) return count;
        const limit = Math.min(count, capacity);
        let written = 0;
        let failure = null;
        this.dynamicChildren.forEach((children, parent) => {
            if( written >= limit || failure ) return;
            const parentId = this.meta.get(parent)?.componentId;
            if( !Number.isInteger(parentId) ) {
                failure = new HostRuntimeError(
                    'dynamic child table has a stale parent', 'STALE_REF');
                return;
            }
            children.forEach((child, subId) => {
                if( written >= limit || failure ) return;
                const childId = this.meta.get(child)?.componentId;
                if( !Number.isInteger(childId) ) {
                    failure = new HostRuntimeError(
                        'dynamic child table has a stale child', 'STALE_REF');
                    return;
                }
                const base = offset + written * 3;
                target[base] = parentId | 0;
                target[base + 1] = subId | 0;
                target[base + 2] = childId | 0;
                written++;
            });
        });
        if( failure ) throw failure;
        if( limit === count && written !== count ) throw new HostRuntimeError(
            'dynamic child count diverged from its index', 'STALE_REF');
        return count;
    }

    fastHostValue(queryKind, key) {
        let value;
        if( queryKind === 3 ) value = this.readState('varp', key);
        else if( queryKind === 4 ) value = this.readState('varbit', key);
        else if( queryKind === 5 ) value = this.readState('varc', key);
        else if( queryKind === 6 ) value = this.clientClock;
        else throw new HostRuntimeError('unknown fast scalar snapshot query', 'BAD_REQUEST');
        return Number(value) | 0;
    }

    fastHostValueSnapshot(queryKind, key) {
        return Int32Array.of(this.fastHostValue(queryKind, key));
    }

    /** Exact scalar companion to the generic reflected HOST surface. Values
     * stay JavaScript-owned while the bridge avoids constructing thousands of
     * transient request records. */
    fastHostScalarDataIdentity() {
        return this.fastScalarDataIdentity;
    }

    fastHostDbDataSnapshot() {
        return this.fastScalarDbData;
    }

    /** Copy the current DB query iterator into one short-lived C invocation.
     * Explicit/restored DB stores deliberately stay on the generic path: only
     * the immutable HostData namespace has the identity guarantee needed by
     * the native fast DB handlers. */
    fastHostDbIteratorSnapshot() {
        if( !this.fastScalarDbData ) return null;
        const rows = this.db.iterator.rows;
        if( !Array.isArray(rows) || rows.length > FAST_DB_ITERATOR_MAX ) return null;
        return {
            rows: Int32Array.from(rows, (row) => Number(row) | 0),
            cursor: Math.max(0, Math.min(rows.length, Number(this.db.iterator.cursor) | 0)),
            revision: this.fastDbIteratorRevision | 0,
        };
    }

    /** Publish native DB_FINDNEXT progress back to the React-owned iterator.
     * The revision check prevents an outer invocation from overwriting an
     * iterator replaced by a nested hook. */
    fastHostDbIteratorCommit(revision, cursor) {
        if( !this.fastScalarDbData || (revision | 0) !== (this.fastDbIteratorRevision | 0) )
            return false;
        const rows = this.db.iterator.rows;
        if( !Array.isArray(rows) || !Number.isInteger(cursor) ||
            cursor < 0 || cursor > rows.length ) return false;
        this.db.iterator.cursor = cursor;
        return true;
    }

    fastHostScalarDataValue(requestKind, a, b, c) {
        let value;
        if( requestKind === 6516 ) value = this._structParamValue(a, b);
        else if( requestKind === 3400 || requestKind === 3408 )
            value = this._enumValue(a, b, requestKind === 3400 ? 115 : c);
        else if( requestKind === 3411 ) value = this._enumOutputCountValue(a);
        else {
            const read = FAST_INT_GEOMETRY_READS[requestKind];
            if( !read ) throw new HostRuntimeError(
                'unknown fast integer data query', 'BAD_REQUEST');
            const component = this._component(a, false);
            value = component ? this.read(read, component) : 0;
        }
        return typeof value === 'string' ? value : Number(value) | 0;
    }

    /** Whether a scalar cache-data answer is immutable for this HostRuntime.
     * Geometry is versioned elsewhere and must never enter the WASM session
     * cache. A STRUCT_PARAM that reaches the user-supplied paramDefault hook is
     * deliberately excluded because that callback may be stateful. */
    fastHostScalarDataCacheable(requestKind, a, b, c) {
        if( requestKind === 3400 || requestKind === 3408 || requestKind === 3411 ) return true;
        if( requestKind !== 6516 ) return false;
        const param = this.hostData.params[String(b)] || null;
        const struct = this.hostData.structs[String(a)] || null;
        const values = struct?.params || struct?.values || struct || {};
        if( Object.prototype.hasOwnProperty.call(values, String(b)) ) return true;
        if( Boolean(param?.string ?? param?.isString ?? param?.is_string) ) return true;
        return Object.prototype.hasOwnProperty.call(param || {}, 'defaultInt') ||
            Object.prototype.hasOwnProperty.call(param || {}, 'default_int');
    }

    requestFastBatch(requests) {
        if( !Array.isArray(requests) )
            throw new HostRuntimeError('fast host batch must be an array', 'BAD_REQUEST');
        const apply = () => {
            const createdByToken = new Map();
            for( const request of requests ) {
                const kind = request.kind;
                if( kind === 'CC_CREATE' ) {
                    const previous = request._fast_previous_temporary
                        ? createdByToken.get(request._fast_previous_id)
                        : { id: request._fast_previous_id,
                            component: this._component(request._fast_previous_id, false) };
                    if( request._fast_previous_temporary && !previous )
                        throw new HostRuntimeError(
                            'fast CC_CREATE references an unknown temporary target', 'BAD_REQUEST');
                    const parent = this._component(request.parent_id, false);
                    const result = parent ? this._request(kind, request) : null;
                    const entry = result
                        ? { id: result.componentId | 0, component: this._component(result) }
                        : previous;
                    /* Internal fallback handshake for C: unlike ordinary
                     * named requests, the compact create record must return
                     * its freshly allocated signed component id in-place. */
                    request.result_component_id = entry.id | 0;
                    createdByToken.set(request._fast_token, entry);
                    continue;
                }
                if( kind === 'CC_FIND' ) {
                    const parent = this._component(request.parent_id, false);
                    /* Native leaves the active target untouched on every miss,
                     * including an existing parent with no matching child. */
                    const result = parent
                        ? this.findChild(parent, request.sub_id, true,
                            { dot: Boolean(request.dot_operand) })
                        : null;
                    const actual = result?.componentId ?? -1;
                    if( request.expected_component_id !== undefined &&
                        (actual | 0) !== (request.expected_component_id | 0) )
                        throw new HostRuntimeError(
                            'fast CC_FIND snapshot became stale before commit', 'STALE_FAST_SNAPSHOT');
                    continue;
                }
                const component = request._fast_temporary_component
                    ? createdByToken.get(request.component_id)?.component || null
                    : this._component(request.component_id, false);
                if( !component ) continue;
                if( kind === 'CC_SETPOSITION' || kind === 'IF_SETPOSITION' )
                    this._mutate('if_setposition', component,
                    [request.x, request.y, request.xmode, request.ymode]);
                else if( kind === 'CC_SETSIZE' || kind === 'IF_SETSIZE' )
                    this._mutate('if_setsize', component,
                    [request.width, request.height, request.wmode, request.hmode]);
                else if( kind === 'CC_SETHIDE' || kind === 'IF_SETHIDE' )
                    this._mutate('if_sethide', component, [request.hidden]);
                else if( kind === 'CC_SETTRANS' || kind === 'IF_SETTRANS' )
                    this._mutate('if_settrans', component, [request.trans]);
                else if( kind === 'CC_SETOBJECT' || kind === 'CC_SETOBJECT_NONUM' ||
                    kind === 'CC_SETOBJECT_ALWAYS_NUM' ) this._mutate(
                    'if_setobject', component, [request.obj_id, request.count, request.num_mode]);
                else if( kind === 'CC_CLEAROPS' || kind === 'IF_CLEAROPS' )
                    this._mutate('if_clearops', component, []);
                else if( kind === 'CC_SETONDRAG' || kind === 'CC_SETONDRAGCOMPLETE' )
                    this._setHook(component,
                        definition(kind === 'CC_SETONDRAG' ? 'on_drag' : 'on_drag_complete'),
                        hookFromRequest(request));
                else if( kind === 'CC_SETONMOUSEOVER' || kind === 'CC_SETONMOUSELEAVE' ||
                    kind === 'CC_SETONMOUSEREPEAT' || kind === 'CC_SETONOP' ) this._setHook(component,
                    definition(kind === 'CC_SETONMOUSEOVER' ? 'on_mouse_over'
                        : kind === 'CC_SETONMOUSELEAVE' ? 'on_mouse_leave'
                            : kind === 'CC_SETONMOUSEREPEAT' ? 'on_mouse_repeat' : 'on_op'),
                    hookFromRequest(request));
                else if( kind === 'IF_SETONMOUSEOVER' || kind === 'IF_SETONMOUSELEAVE' ||
                    kind === 'IF_SETONOP' ) this._setHook(component,
                    definition(kind === 'IF_SETONMOUSEOVER' ? 'on_mouse_over'
                        : kind === 'IF_SETONMOUSELEAVE' ? 'on_mouse_leave' : 'on_op'),
                    hookFromRequest(request));
                else if( kind === 'CC_SETCOLOUR' ) this._mutate(
                    'if_setcolour', component, [request.colour]);
                else if( kind === 'CC_SETFILL' ) this._mutate(
                    'if_setfill', component, [request.filled]);
                else if( kind === 'CC_SETGRAPHIC' ) this._mutate(
                    'if_setgraphic', component, [request.graphic_id]);
                else if( kind === 'CC_SETTEXT' ) this._mutate(
                    'if_settext', component, [request.text]);
                else if( kind === 'CC_SETTEXTFONT' ) this._mutate(
                    'if_settextfont', component, [request.font_id]);
                else if( kind === 'CC_SETTEXTALIGN' ) this._mutate(
                    'if_settextalign', component,
                    [request.x_align, request.y_align, request.line_height]);
                else if( kind === 'CC_SETTEXTSHADOW' ) this._mutate(
                    'if_settextshadow', component, [request.shadowed]);
                else if( kind === 'CC_SETOP' ) this._mutate(
                    'if_setop', component, [request.index, request.text]);
                else if( kind === 'CC_SETOPBASE' ) this._mutate(
                    'if_setopbase', component, [request.text]);
                else if( kind === 'CC_SETDRAGGABLEBEHAVIOR' ) this._mutate(
                    'if_setdraggablebehavior', component, [request.behavior]);
                else if( kind === 'CC_SETDRAGDEADZONE' ) this._mutate(
                    'if_setdragdeadzone', component, [request.zone]);
                else if( kind === 'CC_SETDRAGDEADTIME' ) this._mutate(
                    'if_setdragdeadtime', component, [request.time]);
                else throw new HostRuntimeError(
                    `unsupported fast host request ${kind}`, 'UNSUPPORTED');
            }
            return null;
        };
        return this.operationDepth > 0 ? apply() : this._boundary(apply);
    }

    /**
     * Commit the compact C/WASM transaction directly from its live heap view.
     * `records` and `arena` are borrowed for this synchronous call only. Named
     * request replay above remains the portable/public fallback and defines the
     * observable ordering, versioning and missing-component semantics.
     */
    requestFastPackedBatch(records, recordCount, arena) {
        if( !(records instanceof Int32Array) || !(arena instanceof Uint8Array) ||
            !Number.isInteger(recordCount) || recordCount < 0 || recordCount > 65536 ||
            records.length < recordCount * FAST_HOST_RECORD_WORDS )
            throw new HostRuntimeError('malformed packed fast host batch', 'BAD_REQUEST');
        const arenaView = new DataView(arena.buffer, arena.byteOffset, arena.byteLength);
        const apply = () => {
            const deferTouches = !this.recordChanges && this.fastTouchCount === null;
            if( deferTouches ) this.fastTouchCount = 0;
            const ownsFastDeletes = this.fastDeletedComponents === null;
            if( ownsFastDeletes ) this.fastDeletedComponents = new Set();
            /* A bank cell emits CC_FIND followed by several setters for that
             * exact child. Packed transactions cannot create/delete nodes, so
             * retain those resolutions for the lifetime of this borrowed view. */
            const byUid = this.byUid;
            const dynamicChildren = this.dynamicChildren;
            const meta = this.meta;
            /* C assigns compact-create tokens as INT_MAX - serial, where the
             * serial starts at one and cannot exceed this chunk's record
             * count. A dense array avoids allocating one Map node for every
             * one of ca_tasks' thousands of transient rows. `undefined` is an
             * unknown token; `null` is a known create whose parent was absent. */
            const createdBySerial = [];
            let missingCreatedIds = null;
            let cachedComponentId = NaN;
            let cachedComponentTemporary = false;
            let cachedComponent = null;
            let cachedParentId = NaN;
            let cachedParent = null;
            let cachedParentChildren = null;
            try { for( let index = 0; index < recordCount; index++ ) {
                const base = index * FAST_HOST_RECORD_WORDS;
                const kind = records[base];
                if( kind === FAST_HOST_KINDS.CC_FIND ) {
                    const rawParentId = records[base + 1];
                    let parent;
                    if( rawParentId === cachedParentId ) parent = cachedParent;
                    else {
                        parent = byUid.get(rawParentId) ||
                            (rawParentId < -1 ? byUid.get(rawParentId >>> 0) : null) || null;
                        cachedParentId = rawParentId;
                        cachedParent = parent;
                        cachedParentChildren = parent ? dynamicChildren.get(parent) || null : null;
                    }
                    let actual = -1;
                    if( parent ) {
                        const child = cachedParentChildren?.get(records[base + 2]) || null;
                        const childMeta = child ? meta.get(child) : null;
                        if( childMeta ) {
                            if( records[base + 3] ) this.dotActive = child;
                            else this.active = child;
                            actual = childMeta.componentId | 0;
                            cachedComponentId = actual;
                            cachedComponent = child;
                        }
                    }
                    if( actual !== records[base + 4] ) throw new HostRuntimeError(
                        'fast CC_FIND snapshot became stale before commit',
                        'STALE_FAST_SNAPSHOT');
                    continue;
                }

                if( kind === FAST_HOST_KINDS.CC_CREATE ) {
                    const rawParentId = records[base + 1];
                    let parent;
                    if( rawParentId === cachedParentId ) parent = cachedParent;
                    else {
                        parent = byUid.get(rawParentId) ||
                            (rawParentId < -1 ? byUid.get(rawParentId >>> 0) : null) || null;
                        cachedParentId = rawParentId;
                        cachedParent = parent;
                        cachedParentChildren = parent
                            ? dynamicChildren.get(parent) || null : null;
                    }
                    const previousId = records[base + 8];
                    const previousIsToken = Boolean(records[base + 9]);
                    const previousSerial = FAST_HOST_PENDING_TOKEN_MAX - previousId;
                    if( previousIsToken && (previousSerial <= 0 ||
                        previousSerial > recordCount ||
                        createdBySerial[previousSerial] === undefined) ) throw new HostRuntimeError(
                        'packed CC_CREATE references an unknown temporary target', 'BAD_REQUEST');
                    const previousComponent = previousIsToken
                        ? createdBySerial[previousSerial] || null
                        : byUid.get(previousId) ||
                        (previousId < -1 ? byUid.get(previousId >>> 0) : null) || null;
                    const previousActualId = previousIsToken
                        ? previousComponent
                            ? meta.get(previousComponent).componentId
                            : missingCreatedIds?.[previousSerial] ?? -1
                        : previousId;
                    const token = records[base + 7];
                    const tokenSerial = FAST_HOST_PENDING_TOKEN_MAX - token;
                    if( tokenSerial <= 0 || tokenSerial > recordCount )
                        throw new HostRuntimeError(
                            'packed CC_CREATE carries a malformed temporary target', 'BAD_REQUEST');
                    if( !parent ) {
                        records[base + 6] = previousActualId | 0;
                        createdBySerial[tokenSerial] = previousComponent;
                        if( !previousComponent ) {
                            missingCreatedIds ||= [];
                            missingCreatedIds[tokenSerial] = previousActualId;
                        }
                        continue;
                    }
                    const child = this._fastCreatePackedChild(
                        parent, records[base + 2], records[base + 3],
                        Boolean(records[base + 5]));
                    const actual = meta.get(child).componentId | 0;
                    records[base + 6] = actual;
                    createdBySerial[tokenSerial] = child;
                    cachedComponentId = actual;
                    cachedComponentTemporary = false;
                    cachedComponent = child;
                    cachedParentId = rawParentId;
                    cachedParent = parent;
                    cachedParentChildren = dynamicChildren.get(parent) || null;
                    if( !this.recordChanges ) index = this._fastApplyFreshPackedRun(
                        child, records[base + 7], records, index + 1,
                        recordCount, arena, arenaView);
                    continue;
                }

                const rawComponentId = records[base + 1];
                const temporaryComponent = Boolean(records[base + 11]);
                let component;
                if( rawComponentId === cachedComponentId &&
                    temporaryComponent === cachedComponentTemporary ) component = cachedComponent;
                else if( temporaryComponent ) {
                    const tokenSerial = FAST_HOST_PENDING_TOKEN_MAX - rawComponentId;
                    if( tokenSerial <= 0 || tokenSerial > recordCount ||
                        createdBySerial[tokenSerial] === undefined ) throw new HostRuntimeError(
                        'packed setter references an unknown temporary target', 'BAD_REQUEST');
                    component = createdBySerial[tokenSerial] || null;
                    cachedComponentId = rawComponentId;
                    cachedComponentTemporary = true;
                    cachedComponent = component;
                }
                else {
                    component = byUid.get(rawComponentId) ||
                        (rawComponentId < -1 ? byUid.get(rawComponentId >>> 0) : null) || null;
                    cachedComponentId = rawComponentId;
                    cachedComponentTemporary = false;
                    cachedComponent = component;
                }
                if( !component ) continue;
                if( kind === FAST_HOST_KINDS.CC_SETPOSITION ||
                    kind === FAST_HOST_KINDS.IF_SETPOSITION ) this._fastSetPosition(
                    component, records[base + 2], records[base + 3],
                    records[base + 4], records[base + 5]);
                else if( kind === FAST_HOST_KINDS.CC_SETSIZE ||
                    kind === FAST_HOST_KINDS.IF_SETSIZE ) this._fastSetSize(
                    component, records[base + 2], records[base + 3],
                    records[base + 4], records[base + 5]);
                else if( kind === FAST_HOST_KINDS.CC_SETHIDE ||
                    kind === FAST_HOST_KINDS.IF_SETHIDE ) this._fastSetHidden(
                    component, Boolean(records[base + 2]));
                else if( kind === FAST_HOST_KINDS.CC_SETTRANS ||
                    kind === FAST_HOST_KINDS.IF_SETTRANS ) this._fastSetTransparency(
                    component, records[base + 2]);
                else if( kind === FAST_HOST_KINDS.CC_SETCOLOUR )
                    this._fastSetSimpleProp(component, 'if_setcolour',
                        'color', records[base + 2]);
                else if( kind === FAST_HOST_KINDS.CC_SETFILL )
                    this._fastSetSimpleProp(component, 'if_setfill',
                        'fill', records[base + 2], true);
                else if( kind === FAST_HOST_KINDS.CC_SETGRAPHIC )
                    this._fastSetSimpleProp(component, 'if_setgraphic',
                        'sprite', records[base + 2]);
                else if( kind === FAST_HOST_KINDS.CC_SETTEXT )
                    this._fastSetSimpleProp(component, 'if_settext', 'text',
                        fastRecordString(records, base, arena));
                else if( kind === FAST_HOST_KINDS.CC_SETTEXTFONT )
                    this._fastSetSimpleProp(component, 'if_settextfont',
                        'font', records[base + 2]);
                else if( kind === FAST_HOST_KINDS.CC_SETTEXTALIGN )
                    this._fastSetTextAlign(component, records[base + 2],
                        records[base + 3], records[base + 4]);
                else if( kind === FAST_HOST_KINDS.CC_SETTEXTSHADOW )
                    this._fastSetSimpleProp(component, 'if_settextshadow',
                        'shadow', records[base + 2], true);
                else if( kind === FAST_HOST_KINDS.CC_SETOBJECT ||
                    kind === FAST_HOST_KINDS.CC_SETOBJECT_NONUM ||
                    kind === FAST_HOST_KINDS.CC_SETOBJECT_ALWAYS_NUM ) this._fastSetObject(
                    component, records[base + 2], records[base + 3], records[base + 4]);
                else if( kind === FAST_HOST_KINDS.CC_CLEAROPS ||
                    kind === FAST_HOST_KINDS.IF_CLEAROPS ) {
                    if( component.ops?.length ) {
                        component.ops = [];
                        if( this.recordChanges ) this._record({
                            kind: 'component', ref: meta.get(component).ref, op: 'if_clearops',
                        });
                        else {
                            this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
                            this._touch(true, true);
                        }
                    }
                } else if( kind === FAST_HOST_KINDS.CC_SETONDRAG ||
                    kind === FAST_HOST_KINDS.CC_SETONDRAGCOMPLETE ||
                    kind === FAST_HOST_KINDS.CC_SETONMOUSEOVER ||
                    kind === FAST_HOST_KINDS.CC_SETONMOUSELEAVE ||
                    kind === FAST_HOST_KINDS.CC_SETONOP ||
                    kind === FAST_HOST_KINDS.CC_SETONMOUSEREPEAT ||
                    kind === FAST_HOST_KINDS.IF_SETONMOUSEOVER ||
                    kind === FAST_HOST_KINDS.IF_SETONMOUSELEAVE ||
                    kind === FAST_HOST_KINDS.IF_SETONOP ) {
                    this._fastSetPackedHook(component, FAST_HOST_HOOK_DEFINITIONS[kind],
                        records, base, arena, arenaView);
                } else if( kind === FAST_HOST_KINDS.CC_SETOP ) this._fastSetOp(
                    component, records[base + 2], fastRecordString(records, base, arena));
                else if( kind === FAST_HOST_KINDS.CC_SETOPBASE ) this._fastSetOpBase(
                    component, fastRecordString(records, base, arena));
                else if( kind === FAST_HOST_KINDS.CC_SETDRAGGABLEBEHAVIOR ) this._mutate(
                    'if_setdraggablebehavior', component, [records[base + 2]]);
                else if( kind === FAST_HOST_KINDS.CC_SETDRAGDEADZONE ) this._mutate(
                    'if_setdragdeadzone', component, [records[base + 2]]);
                else if( kind === FAST_HOST_KINDS.CC_SETDRAGDEADTIME ) this._mutate(
                    'if_setdragdeadtime', component, [records[base + 2]]);
                else throw new HostRuntimeError(
                    `unsupported packed fast host record ${kind}`, 'UNSUPPORTED');
            } } finally {
                if( ownsFastDeletes ) {
                    const deleted = this.fastDeletedComponents;
                    this.fastDeletedComponents = null;
                    if( deleted.size ) this.ir.components = this.ir.components.filter(
                        (component) => !deleted.has(component));
                }
                if( deferTouches ) {
                    const touches = this.fastTouchCount;
                    this.fastTouchCount = null;
                    if( touches ) {
                        this.version += touches;
                        this.layoutVersion = -1;
                        this.interactionVisibilityDirty = true;
                    }
                }
            }
            return null;
        };
        return this.operationDepth > 0 ? apply() : this._boundary(apply);
    }

    /**
     * Own one allocation-free TypeScript VM -> Host transaction. The router
     * may call this while dispatch() already owns the outer Host boundary; in
     * that case direct execution only owns its fast mutation batch and leaves
     * deferred work and renderer publication to the existing boundary.
     */
    beginCS2DirectInvocation() {
        if( this.directInvocationDepth++ > 0 ) return;
        this.directInvocationOwnsBoundary = this.operationDepth === 0;
        this.directInvocationError = null;
        if( this.directInvocationOwnsBoundary ) {
            this.invocations = 0;
            this.operationDepth++;
        }
        this.directInvocationOwnsFastTouches =
            !this.recordChanges && this.fastTouchCount === null;
        if( this.directInvocationOwnsFastTouches ) this.fastTouchCount = 0;
        this.directInvocationOwnsFastDeletes = this.fastDeletedComponents === null;
        if( this.directInvocationOwnsFastDeletes ) this.fastDeletedComponents = new Set();
    }

    endCS2DirectInvocation(error = null) {
        if( this.directInvocationDepth <= 0 ) throw new HostRuntimeError(
            'direct CS2 invocation is not active', 'BAD_STATE');
        if( error !== null && error !== undefined && this.directInvocationError === null )
            this.directInvocationError = error;
        if( --this.directInvocationDepth > 0 ) return;

        const ownsBoundary = this.directInvocationOwnsBoundary;
        const ownsFastTouches = this.directInvocationOwnsFastTouches;
        const ownsFastDeletes = this.directInvocationOwnsFastDeletes;
        const failed = this.directInvocationError !== null;
        this.directInvocationOwnsBoundary = false;
        this.directInvocationOwnsFastTouches = false;
        this.directInvocationOwnsFastDeletes = false;
        this.directInvocationError = null;
        let completed = false;
        try {
            if( ownsFastDeletes ) {
                const deleted = this.fastDeletedComponents;
                this.fastDeletedComponents = null;
                if( deleted.size ) this.ir.components = this.ir.components.filter(
                    (component) => !deleted.has(component));
            }
            if( ownsFastTouches ) {
                const touches = this.fastTouchCount;
                this.fastTouchCount = null;
                if( touches ) {
                    this.version += touches;
                    this.layoutVersion = -1;
                    this.interactionVisibilityDirty = true;
                }
            }
            completed = !failed;
        } finally {
            if( ownsBoundary ) {
                try {
                    if( completed ) this._settleSuccessfulBoundary();
                } finally { this.operationDepth--; }
            }
        }
    }

    _directComponent(componentId) {
        return this._component(componentId, false);
    }

    _directMutate(componentId, operation, values) {
        const component = this._directComponent(componentId);
        if( component ) this._mutate(operation, component, values);
    }

    _directSimple(componentId, operation, property, value, booleanValue = false) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetSimpleProp(
            component, operation, property, value, booleanValue);
    }

    _directSetOn(componentId, eventName, scriptIdValue, signatureValue,
        triggerIds, triggerCount, intArgs, intArgCount, stringMask,
        stringArgCount, stringArgs) {
        const component = this._directComponent(componentId);
        if( !component ) return;
        const descriptor = definition(eventName);
        const binding = directHookBinding(
            scriptIdValue, signatureValue, triggerIds, triggerCount,
            intArgs, intArgCount, stringMask, stringArgCount, stringArgs);
        if( this.recordChanges ) {
            this._setHook(component, descriptor, binding);
            return;
        }
        component.hooks ||= {};
        const exact = descriptor.canonical;
        const aliases = hookAliases(descriptor);
        let presentCount = 0;
        let exactPresent = false;
        for( const alias of aliases ) {
            if( !Object.prototype.hasOwnProperty.call(component.hooks, alias) ) continue;
            presentCount++;
            exactPresent ||= alias === exact;
        }
        const installs = scriptId(binding) > 0;
        if( installs && presentCount === 1 && exactPresent &&
            hookBindingMatchesInput(component.hooks[exact], binding, this) ) return;
        if( !installs && presentCount === 0 ) return;
        const normalized = installs ? normalizeBinding(binding, this) : null;
        if( normalized && presentCount === 1 && exactPresent &&
            hookBindingsEqual(component.hooks[exact], normalized) ) return;
        for( const alias of aliases ) delete component.hooks[alias];
        if( normalized ) component.hooks[exact] = normalized;
        this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
        this._touch(true, true);
    }

    PUSH_VARBIT(varbitId) {
        return Number(this.readState('varbit', varbitId)) | 0;
    }

    PUSH_VARC_INT(varcId) {
        return Number(this.readState('varc', varcId)) | 0;
    }

    CC_CREATE(parentId, componentType, childIndex, isNested, dotOperand,
        parentIsSibling) {
        let parent = this._directComponent(parentId);
        /* Native attempts a synchronous group load before this Host handler.
         * HostRuntime owns one already-mounted React tree and has no safe
         * synchronous loader to invoke here. The native terminal behavior for
         * an unavailable parent is success-without-target-change, represented
         * by null; yielding or inventing an async retry would split the VM
         * transaction and violate whole-closure execution. */
        if( !parent ) return null;
        if( parentIsSibling ) {
            parent = this._parentOf(parent);
            if( !parent ) throw new HostRuntimeError('sibling has no parent', 'BAD_REQUEST');
        }
        void isNested;
        const component = !this.recordChanges && this.fastDeletedComponents
            ? this._fastCreatePackedChild(
                parent, componentType, childIndex, Boolean(dotOperand))
            : this._component(this._createChild(
                parent, componentType, childIndex, { dot: Boolean(dotOperand) }));
        return this._directComponentRef(component);
    }

    CC_DELETEALL(componentId) {
        const parent = this._directComponent(componentId);
        if( parent ) this._deleteSet(new Set(this.dynamicChildren.get(parent)?.values() || []));
    }

    CC_FIND(parentId, subId, dotOperand) {
        const parent = this._directComponent(parentId);
        /* As with CC_CREATE, an absent companion group is the native terminal
         * miss after synchronous loading has been attempted: push false and
         * retain the previous implicit target. HostRuntime cannot safely yield
         * the synchronous TypeScript VM to invent a later group-load retry. */
        if( !parent ) return null;
        const childIndex = finiteValue('child index', subId);
        const found = this.dynamicChildren.get(parent)?.get(childIndex) || null;
        if( !found ) return null;
        /* Keep the Host target live and private just like packed C/WASM. The
         * returned direct ref is generation-fenced but deliberately carries no
         * public key; React/public observers materialize that only on demand. */
        if( dotOperand ) this.dotActive = found;
        else this.active = found;
        return this._directComponentRef(found);
    }

    CC_SETPOSITION(componentId, x, y, xMode, yMode) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetPosition(component, x, y, xMode, yMode);
    }

    CC_SETSIZE(componentId, width, height, widthMode, heightMode) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetSize(component, width, height, widthMode, heightMode);
    }

    CC_SETHIDE(componentId, hidden) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetHidden(component, Boolean(hidden));
    }

    CC_SETNOCLICKTHROUGH(componentId, enabled) {
        this._directSimple(componentId, 'if_setnoclickthrough',
            'noClickThrough', enabled, true);
    }

    CC_SETSCROLLPOS(componentId, scrollX, scrollY) {
        this._directMutate(componentId, 'if_setscrollpos', [scrollX, scrollY]);
    }

    CC_SETCOLOUR(componentId, colour) {
        this._directSimple(componentId, 'if_setcolour', 'color', colour);
    }

    CC_SETFILL(componentId, filled) {
        this._directSimple(componentId, 'if_setfill', 'fill', filled, true);
    }

    CC_SETTRANS(componentId, transparency) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetTransparency(component, transparency);
    }

    CC_SETGRAPHIC(componentId, graphicId) {
        this._directSimple(componentId, 'if_setgraphic', 'sprite', graphicId);
    }

    CC_SETTILING(componentId, tiling) {
        this._directSimple(componentId, 'if_settiling', 'tiled', tiling, true);
    }

    CC_SETTEXT(componentId, value) {
        this._directSimple(componentId, 'if_settext', 'text', value);
    }

    CC_SETTEXTFONT(componentId, fontId) {
        this._directSimple(componentId, 'if_settextfont', 'font', fontId);
    }

    CC_SETTEXTALIGN(componentId, horizontal, vertical, lineHeight) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetTextAlign(
            component, horizontal, vertical, lineHeight);
    }

    CC_SETTEXTSHADOW(componentId, shadowed) {
        this._directSimple(componentId, 'if_settextshadow', 'shadow', shadowed, true);
    }

    CC_SETOP(componentId, index, text) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetOp(component, index, text);
    }

    CC_SETDRAGGABLE(componentId, parentUid, childIndex) {
        const component = this._directComponent(componentId);
        if( !component ) return;
        let dragParent = parentUid < 0 ? null : parentUid;
        if( dragParent !== null && childIndex >= 0 && childIndex <= 0xffff )
            dragParent = this.findChild(dragParent, childIndex, false) || dragParent;
        this._mutate('if_setdraggable', component, [true, dragParent]);
    }

    CC_SETDRAGGABLEBEHAVIOR(componentId, behavior) {
        this._directMutate(componentId, 'if_setdraggablebehavior', [behavior]);
    }

    CC_SETONCLICK(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_click', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONHOLD(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_hold', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONMOUSEOVER(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_mouse_over', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONMOUSELEAVE(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_mouse_leave', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONDRAG(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_drag', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONVARTRANSMIT(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_var_transmit', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONTIMER(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_timer', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONOP(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_op', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONDRAGCOMPLETE(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_drag_complete', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONMOUSEREPEAT(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_mouse_repeat', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONSCROLLWHEEL(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_scroll_wheel', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_SETONKEY(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_key', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    CC_GETY(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.read('if_gety', component) : 0;
    }

    CC_GETHEIGHT(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.read('if_getheight', component) : 0;
    }

    CC_GETID(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.meta.get(component).subId : -1;
    }

    CC_SETCOMPONENTPARAM(componentId, paramId, value, stringValue, valueKind) {
        void valueKind;
        this._directMutate(componentId, 'if_setcomponentparam',
            [paramId, stringValue ?? value]);
    }

    IF_SETPOSITION(componentId, x, y, xMode, yMode) {
        this.CC_SETPOSITION(componentId, x, y, xMode, yMode);
    }

    IF_SETSIZE(componentId, width, height, widthMode, heightMode) {
        this.CC_SETSIZE(componentId, width, height, widthMode, heightMode);
    }

    IF_SETHIDE(componentId, hidden) {
        this.CC_SETHIDE(componentId, hidden);
    }

    IF_SETSCROLLPOS(componentId, scrollX, scrollY) {
        this.CC_SETSCROLLPOS(componentId, scrollX, scrollY);
    }

    IF_SETCOLOUR(componentId, colour) {
        this.CC_SETCOLOUR(componentId, colour);
    }

    IF_SETOP(componentId, index, text) {
        this.CC_SETOP(componentId, index, text);
    }

    IF_SETOPBASE(componentId, text) {
        const component = this._directComponent(componentId);
        if( component ) this._fastSetOpBase(component, text);
    }

    IF_SETONVARTRANSMIT(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_var_transmit', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    IF_SETONTIMER(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_timer', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    IF_SETONOP(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_op', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    IF_SETONSCROLLWHEEL(componentId, scriptIdValue, signature, triggerIds, triggerCount,
        intArgs, intArgCount, stringMask, stringArgCount, stringArgs) {
        this._directSetOn(componentId, 'on_scroll_wheel', scriptIdValue, signature,
            triggerIds, triggerCount, intArgs, intArgCount, stringMask,
            stringArgCount, stringArgs);
    }

    IF_GETWIDTH(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.read('if_getwidth', component) : 0;
    }

    IF_GETHEIGHT(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.read('if_getheight', component) : 0;
    }

    IF_GETSCROLLHEIGHT(componentId) {
        const component = this._directComponent(componentId);
        return component ? this.read('if_getscrollheight', component) : 0;
    }

    IF_SETPARAM(componentId, paramId, value, stringValue, valueKind) {
        void valueKind;
        this._directMutate(componentId, 'if_setcomponentparam',
            [paramId, stringValue ?? value]);
    }

    CLIENTCLOCK(_unused) {
        return this.clientClock | 0;
    }

    ENUM(inputType, outputType, enumId, key) {
        void inputType;
        return this._enumValue(enumId, key, outputType);
    }

    ENUM_GETOUTPUTCOUNT(enumId) {
        return this._enumOutputCountValue(enumId);
    }

    STRUCT_PARAM(structId, paramId) {
        return this._structParamValue(structId, paramId);
    }

    request(kindOrRequest, payload = {}) {
        const supplied = typeof kindOrRequest === 'object' && kindOrRequest
            ? kindOrRequest : { ...payload, kind: kindOrRequest };
        if( Array.isArray(supplied) )
            throw new HostRuntimeError('host request must be an object', 'BAD_REQUEST');
        if( supplied.fields !== undefined )
            throw new HostRuntimeError('host request fields must be top-level', 'BAD_REQUEST');
        /* HOST request handlers are read-only. Avoid cloning every reflected
         * WASM record: bank redraws cross this seam more than 20,000 times in
         * one synchronous tick. */
        const request = supplied;
        const kind = normalizeRequestKind(request.kind);
        if( !VALIDATED_HOST_REQUEST_KINDS.has(kind) ) {
            if( !supportsHostRequest(kind) ) throw new HostRuntimeError(
                COMMAND_NAMES.has(kind)
                    ? `${kind} is explicitly unsupported by the UITree/HOST runtime`
                    : `unknown host request ${kind}`,
                'UNSUPPORTED');
            VALIDATED_HOST_REQUEST_KINDS.add(kind);
        }
        /* Reflected HOST calls run inside the enclosing hook/dispatch boundary.
         * Another closure and nested try/finally for each of bankmain's 20K+
         * calls cannot drain or reconcile anything, because only the outermost
         * boundary owns that work. */
        if( this.operationDepth > 0 ) return this._request(kind, request);
        return this._boundary(() => this._request(kind, request));
    }

    _request(kind, request) {
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
        if( HOST_ACTIVITY_REQUEST_NAMES.has(kind) ) {
            const outcome = handleHostActivityRequest(this.activity, kind, request);
            if( !outcome.handled ) throw new HostRuntimeError(
                `${kind} has malformed reflected fields`, 'BAD_REQUEST');
            if( outcome.changed || outcome.revisionChanged ) this._record({
                kind: 'activity', request: kind,
                changed: outcome.changed, revisionChanged: outcome.revisionChanged,
            });
            return outcome.value;
        }
        if( CHAT_SOCIAL_REQUEST_NAMES.has(kind) ) {
            /* MES is stamped by the HOST clock, not by a caller-supplied
             * approximation.  The pure store accepts an explicit clock so it
             * remains independently testable. */
            const exactRequest = kind === 'MES'
                ? { ...request, clock: this.clientClock } : request;
            const outcome = handleChatSocialRequest(this.chatSocial, kind, exactRequest);
            if( outcome.changed ) {
                if( FRIEND_TRANSMIT_REQUESTS.has(kind) ) this.pendingTransmits.friend = true;
                if( CHAT_TRANSMIT_REQUESTS.has(kind) ) this.pendingTransmits.chat = true;
            }
            if( kind === 'MES' ) {
                const latest = this.chatSocial.chat.messages[0];
                const message = {
                    type: 'game', name: latest?.name ?? '', clan: '', text: latest?.text ?? '',
                };
                this.services.messages.unshift(message);
                if( this.services.messages.length > 100 ) this.services.messages.length = 100;
            }
            if( outcome.service ) {
                const service = cloneValue(outcome.service);
                this.services.outbound.push(service);
                if( this.services.outbound.length > 100 ) this.services.outbound.shift();
                this.onService?.(cloneValue(service));
            }
            if( outcome.changed || outcome.service ) this._record({
                kind: 'chat-social', request: kind,
                changed: outcome.changed, service: cloneValue(outcome.service),
            });
            return outcome.result;
        }
        if( DB_REQUEST_NAMES.has(kind) ) {
            const result = handleDbRequest(this.db, kind, request);
            if( DB_ITERATOR_WRITES.has(kind) ) {
                this.fastDbIteratorRevision = this.fastDbIteratorRevision === 0x7fffffff
                    ? 1 : this.fastDbIteratorRevision + 1;
            }
            return result;
        }
        if( LOOT_REQUESTS.has(kind) ) {
            const outcome = handleLootRequest(this.loot, request, {
                objectCost: (id) => this.hostData.objects[String(id)]?.cost,
            });
            if( outcome.changed ) this._record({ kind: 'loot', request: kind });
            return outcome.result;
        }
        if( OVERLAY_REQUEST_NAMES.has(kind) ) {
            const outcome = handleOverlayRequest(
                this.overlay, kind, request, this._overlayAdapterSurface());
            if( !outcome.handled ) throw new HostRuntimeError(
                `${kind} has malformed reflected fields`, 'BAD_REQUEST');
            if( !outcome.ok ) throw new HostRuntimeError(
                outcome.error || `${kind} failed`, 'HOST_ERROR');
            if( outcome.changed ) this._record({ kind: 'overlay', request: kind });
            if( outcome.target ) {
                const component = this._component(outcome.target.component_id, false);
                this.setActive(component, { dot: Boolean(outcome.target.dot_operand) });
                return component ? this.ref(component) : null;
            }
            return outcome.value;
        }
        if( SUBJECT_REQUESTS.has(kind) ) {
            const outcome = handleSubjectRequest(this.subject, request, this.subjectProviders);
            if( outcome.changed ) this._record({ kind: 'subject', request: kind });
            return outcome.result;
        }
        if( WORLDMAP_REQUESTS.has(kind) ) {
            this._syncWorldMapDisplaySize();
            const outcome = handleWorldMapRequest(this.worldMap, request);
            if( outcome.changed ) this._record({ kind: 'worldmap', request: kind });
            return outcome.result;
        }
        /* The desktop client deliberately selects the content-authored error
         * branch until a real hiscores transport is connected. */
        if( kind === 'HISCORES_STATUS' ) return 3;
        if( kind === 'HISCORES_ERROR' ) return '';
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
            return this._inventorySize(request);
        if( kind === 'CLIENTCLOCK' ) return this.clientClock;
        if( kind === 'MOUSE_GETX' ) return this.interaction.x;
        if( kind === 'MOUSE_GETY' ) return this.interaction.y;
        if( kind === 'KEYHELD' || kind === 'KEYPRESSED' ) {
            const key = finiteOptional(request.key_code ?? request.keyCode, -1);
            const keys = kind === 'KEYHELD' ? this.interaction.heldKeys : this.interaction.pressedKeys;
            return key >= 0 && key < 256 && keys.has(key) ? 1 : 0;
        }
        if( SOUND_REQUESTS.has(kind) ) return this._sound(kind, request);
        if( MINIMAP_REQUESTS.has(kind) ) return this._minimap(kind, request);
        if( OPTION_REQUESTS.has(kind) ) return this._clientOption(kind, request);
        if( kind === 'SETWINDOWMODE' || kind === 'SETDEFAULTWINDOWMODE' )
            return this._setWindowMode(kind, request);
        if( CAMERA_REQUESTS.has(kind) ) return this._camera(kind, request);
        if( VIEWPORT_REQUESTS.has(kind) ) return this._viewportRequest(kind, request);
        if( UIZOOM_REQUESTS.has(kind) ) return this._uiZoom(kind, request);
        if( SAFEAREA_REQUESTS.has(kind) ) return this._safeArea(kind);
        if( MINIMENU_REQUESTS.has(kind) ) return this._minimenu(kind);
        if( kind.startsWith('LOCAL_NOTIFICATION') )
            return kind === 'LOCAL_NOTIFICATION' || kind === 'LOCAL_NOTIFICATION_SUPPORTED' ? 0 : null;
        if( kind === 'MES' ) return this._message(request);
        if( kind === 'RESUME_COUNTDIALOG' ) return this._countDialog(request);
        if( kind === 'SETANTIDRAG' ) {
            this.interaction.antiDrag = Boolean(request.value ?? request.args?.[0]);
            return null;
        }
        if( kind === 'LOGOUT' ) {
            if( !this.services.logoutRequested ) {
                this.services.logoutRequested = true;
                this._record({ kind: 'service', service: 'logout' });
            }
            return null;
        }
        if( kind === 'COORD' ) return this.session.localCoord;
        if( kind === '_3330' ) return this.session.destinationCoord;
        if( kind === 'MAP_WORLD' ) return this.session.mapWorld;
        if( kind === 'STAFFMODLEVEL' ) return this.session.staffModLevel;
        if( kind === 'CLIENTTYPE' ) return this.clientType;
        if( kind === 'MAP_MEMBERS' ) return this.mapMembers ? 1 : 0;
        if( kind === 'ON_MOBILE' ) return 0;
        if( kind === 'RUNENERGY_VISIBLE' ) return finiteOptional(this.state.runenergy, 100);
        if( kind === 'RUNWEIGHT_VISIBLE' ) return finiteOptional(this.state.runweight, 0);
        if( kind === 'FROMDATE' ) return formatClientDate(request.day);
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
        if( kind === 'NC_NAME' ) {
            const npcId = finiteValue('npc id',
                request.npcId ?? request.npc_id ?? request.id ?? request.args?.[0] ?? -1);
            const npc = npcId >= 0 ? this.hostData.npcs[String(npcId)] || null : null;
            return npc?.name ? String(npc.name) : 'null';
        }
        if( kind.startsWith('MEC_') ) return this._mapElementRead(kind, request);
        if( kind.startsWith('OC_') ) return this._objectRead(kind, request);
        if( kind === 'INV_GETOBJ' || kind === 'INV_GETNUM' ) {
            const rawInvId = finiteValue('inventory id',
                requestField(request, 'inv_id', 'inventory_id'));
            const slot = finiteValue('inventory slot', requestField(request, 'slot'));
            /* InvManager_GetObj/GetNum accept signed ints. Negative inputs and
             * values outside that C domain are ordinary empty reads. Positive
             * out-of-container slots likewise miss instead of trapping. */
            if( rawInvId < 0 || rawInvId > 0x7fffffff ||
                slot < 0 || slot > 0x7fffffff ) return kind === 'INV_GETOBJ' ? -1 : 0;
            const invId = stateId(rawInvId);
            const entry = this.state[`invslots:${invId}`]?.[slot];
            const objectId = Number(entry?.id ?? entry?.objectId ?? -1);
            if( kind === 'INV_GETOBJ' ) return objectId > 0 ? objectId : -1;
            return objectId > 0 ? Number(entry?.count ?? 0) : 0;
        }

        if( kind === 'CC_CREATE' || kind === 'CC_CREATECHILD' || kind === 'CC_CREATESIBLING' ) {
            let parent = this._component(targetOf(request, this, kind, 'parent_id'), false);
            /* exec_cc_create returns OK and leaves the implicit CC target alone
             * when a directly named parent remains absent after group loading. */
            if( !parent && kind !== 'CC_CREATESIBLING' ) return null;
            if( !parent ) throw new HostRuntimeError(
                'component reference is missing or stale', 'STALE_REF');
            if( kind === 'CC_CREATESIBLING' || request.parentIsSibling || request.parent_is_sibling ) {
                parent = this._parentOf(parent);
                if( !parent ) throw new HostRuntimeError('sibling has no parent', 'BAD_REQUEST');
            }
            return this._createChild(parent,
                requestField(request, 'component_type', 'componentType', 'type'),
                requestField(request, 'child_index', 'childIndex', 'sub_id', 'subId'),
                { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        }
        if( kind === 'CC_FIND' ) {
            const parent = this._component(targetOf(request, this, kind, 'parent_id'), false);
            if( !parent ) return null;
            return this.findChild(parent,
                requestField(request, 'sub_id', 'subId', 'child_index', 'childIndex'), true,
                { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        }
        if( kind === 'IF_FIND' ) {
            const found = this._component(targetOf(request, this, kind), false);
            this.setActive(found, { dot: Boolean(request.dot_operand ?? request.dotOperand) });
            return found ? this.ref(found) : null;
        }
        if( kind === 'CC_COPY' ) return this._copyChild(
            targetOf(request, this, kind, 'parent_id'),
            requestField(request, 'src_sub_id', 'srcSubId'),
            requestField(request, 'dst_sub_id', 'dstSubId'),
            { dot: Boolean(request.dot_operand ?? request.dotOperand) });
        if( kind === 'CC_DELETE' ) {
            const component = this._component(targetOf(request, this, kind), false);
            return component ? this._delete(component) : [];
        }
        if( kind === 'CC_DELETEALL' ) {
            const parent = this._component(targetOf(request, this, kind), false);
            return parent ? this.deleteAll(parent) : [];
        }
        if( kind === 'IF_CHILDREN_FIND' || kind === 'IF_CHILDREN_COLLECT' ||
            kind === 'CC_CHILDREN_FIND_COUNT' ) {
            const parent = targetOf(request, this, kind,
                kind.startsWith('IF_') ? 'uid' : 'parent_id');
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
                return this.findChild(targetOf(request, this, kind, 'parent_id'),
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
            const component = this._component(targetOf(request, this, kind));
            const parent = this._parentOf(component);
            return parent ? this.ref(parent) : null;
        }
        if( kind === 'IF_GETTOP' ) return this.interfaceId;

        const prefix = /^(CC|IF)_(.+)$/.exec(kind);
        if( !prefix ) throw new HostRuntimeError(`unsupported host request ${kind}`, 'UNSUPPORTED');
        const suffix = prefix[2];
        const target = targetOf(request, this, kind);
        const targetComponent = this._component(target, false);
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
        if( !targetComponent && missingComponentNoop(suffix) ) return null;
        if( suffix === 'SETHTTPSPRITE' ) return this._mutate('if_sethttpsprite', targetComponent,
            [request.url ?? request.text ?? request.values?.[0] ?? request.args?.[0] ?? '']);
        if( INPUT_GETTERS[suffix] ) {
            if( !targetComponent ) return INPUT_GETTERS[suffix] === 'focused' ? false : 0;
            const input = targetComponent.runtime.input || cloneInputState({});
            return INPUT_GETTERS[suffix] === 'focused'
                ? Boolean(input.focused) : input[INPUT_GETTERS[suffix]];
        }
        if( INPUT_SETTERS[suffix] ) return this._setInputField(
            targetComponent, INPUT_SETTERS[suffix], requestField(request, 'value'));
        if( suffix === 'GETCOMPONENTPARAM' ) {
            const paramId = stateId(requestField(request, 'paramId', 'param_id'));
            const entry = targetComponent?.runtime.params?.[paramId];
            if( entry && Object.hasOwn(entry, 'value') ) return entry.value;
            if( prefix[1] === 'IF' ) return finiteValue('component parameter default',
                request.value ?? request.defaultValue ?? 0);
            return finiteValue('component parameter default', this.paramDefault(paramId, this) ?? 0);
        }
        if( REQUEST_GETTERS[suffix] ) {
            if( !targetComponent ) return missingComponentGetter(suffix);
            return this.read(REQUEST_GETTERS[suffix], targetComponent,
                suffix === 'GETOP' ? requestField(request, 'index', 'op_index', 'opIndex') : null);
        }
        if( suffix === 'SETOP' ) return this._mutate('if_setop', targetComponent,
            [requestField(request, 'index', 'op_index'), requestField(request, 'text')]);
        if( suffix === 'SETOBJECT' || suffix === 'SETOBJECT_NONUM' || suffix === 'SETOBJECT_ALWAYS_NUM' )
            return this._mutate('if_setobject', targetComponent, [
                request.obj_id ?? request.objectId,
                request.count ?? 0,
                request.num_mode ?? request.numberMode ??
                    (suffix === 'SETOBJECT_ALWAYS_NUM' ? 1 : suffix === 'SETOBJECT_NONUM' ? 2 : 0),
            ]);
        if( suffix === 'SETNPCHEAD' ) return this._mutate('if_setmodelsource', targetComponent,
            ['npcHead', requestField(request, 'modelId', 'model_id', 'npcId', 'npc_id')]);
        if( suffix === 'SETPLAYERHEAD_SELF' ) return this._mutate('if_setmodelsource', targetComponent,
            ['playerHead', -1]);
        if( suffix === 'SETPLAYERMODEL_SELF' ) return this._mutate('if_setmodelsource', targetComponent,
            ['playerSelf', -1]);
        if( suffix === 'SETMODEL_PLAYERCHATHEAD' ) return this._mutate('if_setmodelsource', targetComponent,
            ['playerChatHead', -1]);
        if( suffix === 'SETLOCMODEL' ) return this._mutate('if_setmodelsource', targetComponent,
            ['locModel', requestField(request, 'locId', 'loc_id', 'modelId', 'model_id', 'id', 'value')]);
        if( suffix === 'SETNPCMODEL' ) return this._mutate('if_setmodelsource', targetComponent,
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
            return this._mutate('if_setdraggable', targetComponent, [true, dragParent]);
        }
        if( suffix === 'SETDRAGDEADZONE' ) return this._mutate('if_setdragdeadzone', targetComponent,
            [requestField(request, 'zone', 'value')]);
        if( suffix === 'SETDRAGDEADTIME' ) return this._mutate('if_setdragdeadtime', targetComponent,
            [requestField(request, 'time', 'value')]);
        if( suffix === 'SETDRAGGABLEBEHAVIOR' ) return this._mutate(
            'if_setdraggablebehavior', targetComponent,
            [request.behavior ?? request.value ?? request.values?.[0] ?? request.args?.[0] ?? 0]);
        if( suffix === 'SETOPBASE' ) return this._mutate('if_setopbase', targetComponent,
            [request.text ?? request.values?.[0] ?? request.args?.[0] ?? '']);
        if( suffix === 'CLEAROPS' ) return this._mutate('if_clearops', targetComponent, []);
        if( suffix === 'SETOPSUBMENU' ) return this._mutate('if_setopsubmenu', targetComponent, [
            requestField(request, 'opIndex', 'op_index'),
            requestField(request, 'subIndex', 'sub_index'),
            requestField(request, 'text'),
        ]);
        if( suffix === 'CLEAROPSUBMENU' ) return this._mutate('if_clearopsubmenu', targetComponent,
            [requestField(request, 'opIndex', 'op_index')]);
        if( suffix === 'SETTARGETPRIORITY' ) return this._mutate(
            'if_settargetpriority', targetComponent,
            [requestField(request, 'priority', 'value')]);
        if( suffix === 'SETCOMPONENTPARAM' || suffix === 'SETPARAM' )
            return this._mutate('if_setcomponentparam', targetComponent, [
                requestField(request, 'paramId', 'param_id'),
                request.strValue ?? request.str_value ?? request.value,
            ]);
        if( suffix === 'SETOPKEY' || suffix === 'SETOPTKEY' ) {
            const opIndex = suffix === 'SETOPTKEY' ? 10
                : requestField(request, 'opIndex', 'op_index', 'index');
            return this._mutate('if_setopkey', targetComponent, [
                opIndex, request.keyChars ?? request.key_chars ?? [],
                request.keyCodes ?? request.key_codes ?? [],
            ]);
        }
        if( suffix === 'SETOPKEYRATE' || suffix === 'SETOPTKEYRATE' )
            return this._mutate('if_setopkeyrate', targetComponent, [
                suffix === 'SETOPTKEYRATE' ? 10 : requestField(request, 'opIndex', 'op_index', 'index'),
                requestField(request, 'rate'), request.enabled ?? true,
            ]);
        if( suffix === 'SETOPKEYIGNOREHELD' || suffix === 'SETOPTKEYIGNOREHELD' )
            return this._mutate('if_setopkeyignoreheld', targetComponent, [
                suffix === 'SETOPTKEYIGNOREHELD' ? 10
                    : requestField(request, 'opIndex', 'op_index', 'index'),
            ]);
        if( suffix === 'TRIGGEROP' ) return this._queueDeferredComponent('triggerOp', {
            /* IF_TRIGGEROPLOCAL may deliberately name a companion-group UID
             * which is not mounted in the selected React tree. Preserve that
             * exact wire identity; resolving first would collapse it to -1. */
            componentId: this._deferredComponentId(target),
            opIndex: finiteValue('operation index',
                requestField(request, 'opIndex', 'op_index', 'index')),
        });
        if( suffix === 'TRIGGEROPLOCAL' ) return this._queueDeferredComponent('triggerOpLocal', {
            componentId: this._deferredComponentId(target),
            sub: finiteValue('component sub id', requestField(request, 'sub', 'subId', 'sub_id')),
        });
        if( suffix === 'CALLONRESIZE' ) return this._queueDeferredComponent('callOnResize', {
            componentId: this._deferredComponentId(target),
        });
        if( suffix.startsWith('INPUT_SETON') ) {
            const descriptor = definition(`on_${suffix.slice('INPUT_SETON'.length).toLowerCase()}`);
            return this._setHook(targetComponent, descriptor, hookFromRequest(request));
        }
        if( suffix.startsWith('SETON') ) {
            const descriptor = definition(setOnEvent(suffix));
            return this._setHook(targetComponent, descriptor, hookFromRequest(request));
        }
        const setter = REQUEST_SETTERS[suffix];
        if( !setter ) throw new HostRuntimeError(`unsupported host request ${kind}`, 'UNSUPPORTED');
        const values = requestValues(request, setter[1]);
        return this._mutate(setter[0], targetComponent, values);
    }

    _overlayAdapterSurface() {
        const defaults = {
            resolveSubject: (kind, context) => this._overlaySubject(kind, context?.request),
            createLayer: (payload) => this.overlayTreeEnabled
                ? this._attachOverlayLayer(payload) : -1,
            deleteLayer: ({ component_id: componentId }) => {
                const component = this._component(componentId, false);
                if( !component ) return false;
                this._delete(component);
                return true;
            },
            hasComponent: ({ component_id: componentId }) =>
                Boolean(this._component(componentId, false)),
            createChild: ({ parent_component_id: parentId, component_type: type,
                child_index: childIndex, dot_operand: dot }) =>
                this._createChild(parentId, type, childIndex, { dot: Boolean(dot) }),
            findChild: ({ parent_component_id: parentId, child_index: childIndex }) =>
                this.findChild(parentId, childIndex, false),
            deleteAllChildren: ({ parent_component_id: parentId }) =>
                this.deleteAll(parentId).length > 0,
            setActiveComponent: ({ component_id: componentId, dot_operand: dot }) =>
                this.setActive(this._component(componentId, false), { dot: Boolean(dot) }),
        };
        return { ...defaults, ...this.overlayProviders };
    }

    _overlaySubject(kind, request = {}) {
        const runningScriptId = finiteOptional(
            request.running_script_id ?? request.runningScriptId ?? this.subject.runningScriptId,
            -1);
        const dispatch = this.subject.dispatch;
        const selected = dispatch?.kind === kind && dispatch.scriptId > 0 &&
            dispatch.scriptId === runningScriptId
            ? dispatch : this.subject.active?.[kind]?.kind === kind
                ? this.subject.active[kind] : this.subject.mouseover?.kind === kind
                    ? this.subject.mouseover : null;
        if( !selected ) return null;
        if( kind === 'loc' ) return { coord: selected.coord, layer: selected.layer };
        return { uid: selected.uid };
    }

    _ensureOverlayMount() {
        const existing = this._component(this.overlayMount, false);
        if( existing ) return existing;
        if( this.ir.components.length >= this.limits.components )
            throw new HostRuntimeError('component limit reached', 'LIMIT');

        const staticProps = dynamicProps(IF_TYPE.layer, 'Layer');
        Object.assign(staticProps, {
            x: 0, y: 0, xMode: 0, yMode: 0,
            width: this.viewport.width, height: this.viewport.height,
            widthMode: 0, heightMode: 0,
            /* Native entity-overlay children are positioned by the scene pass.
             * The interface preview has no camera, so retain/decorate them in
             * the React tree but do not invent a screen-space projection. */
            hidden: true,
        });
        const component = {
            fileId: '@host:entity-overlay',
            name: '$entity_overlay',
            kind: 'Layer', type: IF_TYPE.layer, layer: null, subId: -1,
            props: staticProps, static: staticProps, authoredProps: new Set(), dynamic: [],
            ops: [], events: {}, hooks: {}, triggers: {}, dependencies: [],
            scriptBindings: [], rawFields: { builtin: 'entity_overlay' },
            runtime: emptyRuntimeState(),
        };
        this.ir.components.push(component);
        this.structureRevision++;
        this._indexStatic(component);
        this.overlayMount = this.ref(component);
        this._record({ kind: 'overlay-mount', ref: this.overlayMount });
        return component;
    }

    _attachOverlayLayer(payload) {
        const index = boundedInteger('overlay index', payload.overlay_index, 0, 639);
        const width = finiteValue('overlay width', payload.width);
        const height = finiteValue('overlay height', payload.height);
        const active = this.active;
        const dotActive = this.dotActive;
        try {
            const layer = this._createChild(
                this._ensureOverlayMount(), IF_TYPE.layer, index, { dot: false });
            const component = this._component(layer);
            component.static.width = width;
            component.static.height = height;
            component.static.hidden = false;
            return layer;
        } catch( error ) {
            if( !(error instanceof HostRuntimeError) ) throw error;
            return -1;
        } finally {
            /* OVERLAY_*_CREATE returns an index but does not retarget CC. */
            this.active = active;
            this.dotActive = dotActive;
        }
    }

    _sound(kind, request) {
        const sound = { kind: kind.slice('SOUND_'.length).toLowerCase(), ...soundSynthIntent(request) };
        const service = kind.toLowerCase();
        const suffix = kind === 'SOUND_SYNTH' ? 'Synth'
            : kind === 'SOUND_SONG' ? 'Song'
                : kind === 'SOUND_JINGLE' ? 'Jingle' : 'SongWithSecondary';
        this.services[`sound${suffix}Count`]++;
        this.services[`lastSound${suffix}`] = sound;
        this.services.sounds.push(sound);
        if( this.services.sounds.length > 64 ) this.services.sounds.shift();
        this._record({ kind: 'service', service, sound });
        return null;
    }

    _minimap(kind, request) {
        if( kind === 'MINIMAP_GETZOOM' ) return this.session.minimapZoom;
        if( kind === 'MINIMAP_SETZOOM' ) {
            const value = finiteOptional(request.value ?? request.args?.[0], 0);
            if( this.session.minimapZoom !== value ) {
                this.session.minimapZoom = value;
                this._record({ kind: 'session', field: 'minimapZoom', value });
            }
        }
        /* SETZOOMABLE/SETICONZOOMLIMIT are accepted no-ops in the C client:
         * its minimap renderer does not yet expose either backing field. */
        return null;
    }

    _inventorySize(request) {
        const args = request.args || request.values || [];
        const invId = finiteOptional(
            request.inv_id ?? request.invId ?? request.inventory_id ??
            request.inventoryId ?? args[0], -1);
        if( invId < 0 ) return 0;
        const entry = this.hostData.inventoryTypes[String(invId)];
        return Math.max(0, finiteOptional(
            entry && typeof entry === 'object' ? entry.size : entry, 0));
    }

    _message(request) {
        const text = boundedText('message text', request.text ?? request.args?.[0] ?? '');
        const message = { type: 'game', name: '', clan: '', text: text.slice(0, 199) };
        this.services.messages.unshift(message);
        if( this.services.messages.length > 100 ) this.services.messages.length = 100;
        this._record({ kind: 'service', service: 'message', message });
        return null;
    }

    _countDialog(request) {
        const text = boundedText('count dialog response', request.text ?? request.args?.[0] ?? '');
        if( !text ) return null;
        const response = text.slice(0, 199);
        if( this.services.countDialogResponses.length < 8 ) {
            this.services.countDialogResponses.push(response);
            this._record({ kind: 'service', service: 'resume_countdialog', text: response });
        }
        return null;
    }

    _setWindowMode(kind, request) {
        const mode = finiteOptional(request.mode ?? request.value ?? request.args?.[0], 0);
        if( mode !== 1 && mode !== 2 ) return null;
        const field = kind === 'SETDEFAULTWINDOWMODE' ? 'defaultWindowMode' : 'windowMode';
        if( this.session[field] !== mode ) {
            this.session[field] = mode;
            this._record({ kind: 'session', field, value: mode });
        }
        return null;
    }

    _camera(kind, request) {
        const camera = this.session.camera;
        if( kind === 'CAM_GETANGLE_XA' ) return camera.angleX;
        if( kind === 'CAM_GETANGLE_YA' ) return camera.angleY;
        if( kind === 'CAM_GETFOLLOWHEIGHT' ) return camera.followHeight;
        if( kind === 'CAM_FORCEANGLE' ) {
            const angleX = clampInteger(
                request.angle_x ?? request.angleX ?? request.args?.[0] ?? 0, 128, 383);
            const angleY = finiteValue('camera y angle',
                request.angle_y ?? request.angleY ?? request.args?.[1] ?? 0) & 0x7ff;
            if( camera.angleX !== angleX || camera.angleY !== angleY || !camera.forced ) {
                Object.assign(camera, { angleX, angleY, yaw: angleY, forced: true });
                this._record({ kind: 'session', field: 'camera', value: cloneValue(camera) });
            }
            return null;
        }
        const followHeight = finiteValue('camera follow height',
            request.height ?? request.value ?? request.args?.[0] ?? 0);
        if( camera.followHeight !== followHeight ) {
            camera.followHeight = followHeight;
            this._record({ kind: 'session', field: 'camera.followHeight', value: followHeight });
        }
        return null;
    }

    _clientOption(kind, request) {
        let table;
        let optionId;
        if( OPTION_VOLUME[kind] ) [table, optionId] = OPTION_VOLUME[kind];
        else if( kind === 'GETREMOVEROOFS' || kind === 'SETREMOVEROOFS' ) {
            table = 'game'; optionId = 1;
        } else {
            optionId = finiteOptional(request.option_id ?? request.optionId ?? request.args?.[0], -1);
            if( kind.startsWith('GAMEOPTION_') ) table = 'game';
            else if( kind.startsWith('DEVICEOPTION_') ) table = 'device';
            else table = DEVICE_OPTIONS.has(optionId) ? 'device'
                : GAME_OPTIONS.has(optionId) ? 'game' : null;
        }
        if( kind === 'DEVICEOPTION_GETRANGE' ) return [0, optionId === 19 ? 100 : 255];
        const setter = kind.startsWith('SET') || kind.endsWith('_SET');
        if( !setter ) {
            const value = this._getOption(table, optionId);
            return kind === 'GETREMOVEROOFS' ? (value ? 1 : 0) : value;
        }
        if( !table || optionId < 0 || optionId >= 64 ) return null;
        let value = finiteOptional(request.value ?? request.args?.at(-1), 0);
        if( kind === 'SETREMOVEROOFS' ) value = value ? 1 : 0;
        this._setOption(table, optionId, value);
        return null;
    }

    _getOption(table, optionId) {
        if( !table || optionId < 0 || optionId >= 64 ) return 0;
        return this.session[`${table}Options`][optionId];
    }

    _setOption(table, optionId, rawValue) {
        let value = finiteOptional(rawValue, 0);
        const volume = (table === 'game' && [7, 8, 9].includes(optionId)) ||
            (table === 'device' && optionId === 19);
        if( volume ) value = Math.max(0, Math.min(100, value));
        if( table === 'device' && optionId === 27 ) value = Math.max(100, Math.min(400, value));
        if( table === 'device' && optionId === 15 ) value = Math.max(0, Math.min(2, value));
        const options = this.session[`${table}Options`];
        if( options[optionId] === value ) return;
        options[optionId] = value;
        this._record({ kind: 'session', field: `${table}Options.${optionId}`, value });
    }

    _viewportRequest(kind, request) {
        const settings = this.session.viewport;
        const args = request.args || request.values || [];
        if( kind === 'VIEWPORT_SETFOV' ) {
            const near = viewportZoomDecode(finiteOptional(args[0], 0));
            const far = viewportZoomDecode(finiteOptional(args[1], 0));
            if( settings.zoomNear !== near || settings.zoomFar !== far ) {
                settings.zoomNear = near; settings.zoomFar = far;
                this._record({ kind: 'session', field: 'viewport.fov', value: [near, far] });
            }
            return null;
        }
        if( kind === 'VIEWPORT_GETFOV' )
            return [viewportZoomEncode(settings.zoomNear), viewportZoomEncode(settings.zoomFar)];
        if( kind === 'VIEWPORT_SETZOOM' ) {
            const zoom = finiteOptional(args[0], 0) > 0 ? finiteOptional(args[0], 0) : 256;
            const zoomMax = finiteOptional(args[1], 0) > 0 ? finiteOptional(args[1], 0) : 320;
            if( settings.zoom !== zoom || settings.zoomMax !== zoomMax ) {
                settings.zoom = zoom; settings.zoomMax = zoomMax;
                this._record({ kind: 'session', field: 'viewport.zoom', value: [zoom, zoomMax] });
            }
            return null;
        }
        if( kind === 'VIEWPORT_GETZOOM' ) return [settings.zoom, settings.zoomMax];
        if( kind === 'VIEWPORT_CLAMPFOV' ) {
            const fovMin = positiveOr(args[0], 1);
            const fovMax = Math.max(fovMin, positiveOr(args[1], 32767));
            const aspectMin = positiveOr(args[2], 1);
            const aspectMax = Math.max(aspectMin, positiveOr(args[3], 32767));
            Object.assign(settings, { fovMin, fovMax, aspectMin, aspectMax });
            this._record({ kind: 'session', field: 'viewport.clamp',
                value: [fovMin, fovMax, aspectMin, aspectMax] });
            return null;
        }
        const world = this.ir.components.find((component) =>
            finiteOptional(component.static?.clientCode ?? component.rawFields?.clientcode, -1) === 1337);
        if( !world ) return [-1, -1];
        const box = this._box(world);
        return box ? effectiveViewportSize(settings, box.w, box.h) : [-1, -1];
    }

    _uiZoom(kind, request) {
        if( kind === 'UIZOOM_GETDEFAULT' ) return 100;
        if( kind === 'UIZOOM_GET' ) return this._getOption('device', 27);
        const value = kind === 'UIZOOM_RESET' ? 100
            : finiteOptional(request.value ?? request.args?.[0], 100);
        this._setOption('device', 27, value);
        return null;
    }

    _safeArea(kind) {
        if( kind === 'SAFEAREA_GETMINX' || kind === 'SAFEAREA_GETMINY' ) return 0;
        return kind === 'SAFEAREA_GETMAXX' ? this.viewport.width : this.viewport.height;
    }

    _minimenu(kind) {
        const entries = this.interaction.menuEntries;
        const acting = entries[0] || null;
        const hovered = this._component(acting?.component ?? this.interaction.hover, false);
        const worldSubject = acting ? null : this.subject.mouseover;
        const worldType = worldSubject ? minimenuSubjectType(worldSubject.kind) : 0;
        if( kind === 'MINIMENU_TYPE' ) return hovered ? 7 : worldType;
        if( kind === 'MINIMENU_ENTRY' ) return acting ? [acting.text, acting.target] : ['', ''];
        if( kind === 'MINIMENU_ISOPEN' ) return this.interaction.menuOpen ? 1 : 0;
        if( kind === 'MINIMENU_NUMOPS' ) return entries.length;
        if( kind === '_7106' ) return finiteOptional(
            this.subject.mouseover?.coord, -1) >= 0
            ? finiteOptional(this.subject.mouseover.coord, -1) : this.subject.hoverCoord;
        if( kind === '_7107' ) return this.subject.mouseover?.kind === 'obj'
            ? finiteOptional(this.subject.mouseover.type, 0) : 0;
        const subjectKind = MINIMENU_FIND_SUBJECT[kind];
        if( subjectKind ) {
            const next = worldSubject?.kind === subjectKind ? cloneValue(worldSubject) : null;
            const changed = !sameSubjectContext(this.subject.active[subjectKind], next);
            this.subject.active[subjectKind] = next;
            if( changed ) this._record({
                kind: 'subject', request: kind, active: subjectKind,
                found: Boolean(next),
            });
            return next ? 1 : 0;
        }
        if( kind !== 'MINIMENU_FINDCOMPONENT' ) return 0;
        const component = hovered;
        if( !component ) return 0;
        this.setActive(component);
        this.setActive(component, { dot: true });
        return 1;
    }

    _mapElementRead(kind, request) {
        const id = finiteValue('map element id',
            request.mecId ?? request.mec_id ?? request.id ?? request.args?.[0] ?? -1);
        const element = id >= 0 ? this.hostData.mapElements[String(id)] || null : null;
        if( kind === 'MEC_TEXT' ) return element ? String(element.name ?? '') : '';
        if( kind === 'MEC_TEXTSIZE' ) return finiteOptional(element?.textSize ?? element?.text_size, 0);
        if( kind === 'MEC_CATEGORY' ) return finiteOptional(element?.category, -1);
        return finiteOptional(element?.sprite ?? element?.spriteId ?? element?.sprite_id, -1);
    }

    _enumLookup(kind, request) {
        const args = request.args || request.values || [];
        const enumId = finiteValue('enum id', request.enumId ?? request.enum_id ??
            (kind === 'ENUM' ? args[2] : args[0]));
        const key = finiteValue('enum key', request.key ??
            (kind === 'ENUM' ? args[3] : args[1]));
        const outputType = kind === 'ENUM_STRING' ? 115
            : finiteValue('enum output type', request.outputType ?? request.output_type ?? args[1]);
        return this._enumValue(enumId, key, outputType);
    }

    _enumValue(enumId, key, outputType) {
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
        return this._structParamValue(structId, paramId);
    }

    _structParamValue(structId, paramId) {
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
        if( !component || this.interaction.antiDrag || this.interaction.dragging ) return this._result([]);
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
        return this._enumOutputCountValue(enumId);
    }

    _enumOutputCountValue(enumId) {
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
        if( kind === 'OC_FINDRESET' ) {
            this.session.objectSearch = { ids: [], index: 0 };
            return null;
        }
        if( kind === 'OC_FINDNEXT' )
            return this.session.objectSearch.ids[this.session.objectSearch.index++] ?? -1;
        if( kind === 'OC_FIND' ) {
            const query = boundedText('object search query',
                request.query ?? request.text ?? args[0] ?? '').slice(0, 255).toLowerCase();
            const ids = query ? Object.entries(this.hostData.objects)
                .filter(([, object]) => String(object?.name ?? '').toLowerCase().includes(query))
                .map(([id]) => Number(id))
                .filter(Number.isSafeInteger)
                .sort((left, right) => left - right) : [];
            this.session.objectSearch = { ids, index: 0 };
            return ids.length;
        }
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
        if( kind === 'OC_MEMBERS' ) return 0;
        if( kind === 'OC_EXAMINE' ) return object ? String(object.examine ?? '') : '';
        if( kind === 'OC_SHIFTCLICKIOP' ) {
            if( !object ) return -1;
            const actions = object.invActions ?? object.inv_actions ?? object.inventoryActions ??
                object.inventory_actions ?? object.inventoryOps ?? object.inventory_ops ?? [];
            let index = finiteOptional(
                object.shiftClickDropIndex ?? object.shift_click_drop_index ??
                    object.shiftclickdrop, -2);
            if( index >= 0 ) {
                if( index >= 5 || !actions?.[index] ) index = -1;
            } else if( index < -1 ) {
                index = String(actions?.[4] ?? '').toLowerCase() === 'drop' ? 4 : -1;
            }
            return index < 0 ? -1 : index + 1;
        }
        /* These are explicit current C-client answers: Objtype does not yet
         * expose wearable slots, weight, or inventory submenus there. */
        if( kind === 'OC_WEARPOS' || kind === 'OC_WEARPOS2' || kind === 'OC_WEARPOS3' ) return -1;
        if( kind === 'OC_WEIGHT' ) return 0;
        if( kind === 'OC_ISUBOP' ) return '';
        /* The present C host maps both note-family opcodes to Objtype.id: an
         * available object answers its own id and an unavailable one answers 0. */
        if( kind === 'OC_CERT' || kind === 'OC_UNCERT' ) return object ? itemId : 0;
        throw new HostRuntimeError(`unsupported object request ${kind}`, 'UNSUPPORTED');
    }

    _deferredComponentId(value) {
        const direct = value && typeof value === 'object'
            ? value.componentId ?? value.component_id : value;
        if( Number.isSafeInteger(direct) ) return direct;
        const component = this._component(value, false);
        return component ? this.meta.get(component).componentId : -1;
    }

    _queueDeferredComponent(queueName, entry) {
        const queue = this.pendingDeferred[queueName];
        if( queue.length < DEFERRED_COMPONENT_QUEUE_LIMIT ) {
            queue.push(entry);
            return null;
        }
        /* Native reports the overflow and retains the first sixteen requests.
         * A browser has no stderr panel, so retain the same bounded diagnostic
         * in the serializable service view without turning a dropped request
         * into a fatal VM error. */
        const dropped = { queue: queueName, ...cloneValue(entry) };
        this.services.deferredDrops.push(dropped);
        if( this.services.deferredDrops.length > DEFERRED_COMPONENT_QUEUE_LIMIT )
            this.services.deferredDrops.shift();
        this._record({ kind: 'service', service: 'deferred_queue_full', dropped });
        return null;
    }

    _deferredHookJob(component, eventName, input, locals = {}) {
        const resolved = this._resolveHook(component, definition(eventName));
        if( !resolved ) return null;
        /* RS_CS2_DispatchHook copies the listener into its FIFO task. A script
         * earlier in the same batch may rewrite the live hook, but must not
         * rewrite work which the App already queued. */
        return {
            component,
            resolved: { ...resolved, binding: normalizeBinding(resolved.binding, this) },
            input,
            locals,
        };
    }

    _buttonTarget(component) {
        const meta = this.meta.get(component);
        if( !meta?.dynamic ) return { componentId: meta?.componentId ?? -1, subId: -1 };
        const parent = this._parentOf(component);
        const parentMeta = parent && this.meta.get(parent);
        return {
            componentId: parentMeta?.componentId ?? meta.componentId,
            subId: meta.subId,
        };
    }

    _publishButtonService(service) {
        const outbound = cloneValue(service);
        this.services.outbound.push(outbound);
        if( this.services.outbound.length > 100 ) this.services.outbound.shift();
        this.onService?.(cloneValue(outbound));
        /* Publishing an outbound packet changes snapshot/service state, not a
         * widget field. Keep the already-resolved hit/event geometry valid for
         * the local hook which native dispatches immediately afterwards. */
        this._record({ kind: 'service', service: 'if_button', outbound }, { layout: false });
    }

    _publishArmedButton(component, opIndex, source) {
        if( !component || opIndex < 1 || opIndex > 10 ) return false;
        const events = finiteOptional(component.static?.clickMask, 0) >>> 0;
        if( !(events & (1 << opIndex)) ) return false;
        const target = this._buttonTarget(component);
        const service = {
            kind: 'if_button', source, opIndex,
            componentId: target.componentId, subId: target.subId,
        };
        /* Script-created item cells use the same IF_BUTTON<n> family, but
         * rev-239's collapsed packet also carries the object id. Plain widget
         * buttons retain the no-object sentinel by omitting this field. */
        const objectId = finiteOptional(component.static?.objectId, 0);
        if( objectId > 0 ) service.objectId = objectId;
        this._publishButtonService(service);
        return true;
    }

    _drainDeferredComponents(intents) {
        let passes = 0;
        while( this.pendingDeferred.callOnResize.length ||
               this.pendingDeferred.triggerOp.length ) {
            if( ++passes > this.limits.hookInvocations * 4 )
                throw new HostRuntimeError('deferred component queue did not settle', 'LIMIT');

            /* Snapshot a native App pump before invoking any task. Requests a
             * listener raises therefore join the following pass, behind every
             * task this pass had already queued. */
            const resizes = this.pendingDeferred.callOnResize.splice(0);
            const triggers = this.pendingDeferred.triggerOp.splice(0);
            const resizeJobs = [];
            const triggerJobs = [];

            for( const pending of resizes ) {
                const component = this._component(pending.componentId, false);
                if( !component ) continue;
                const job = this._deferredHookJob(component, 'on_resize',
                    baseEvent('trigger', { kind: 'call_on_resize' }));
                if( job ) resizeJobs.push(job);
            }

            for( const pending of triggers ) {
                const component = this._component(pending.componentId, false);
                if( !component ) continue;
                const opIndex = pending.opIndex;
                const job = this._deferredHookJob(component, 'on_op',
                    baseEvent('trigger', { kind: 'cc_triggerop', opIndex }), { opIndex });
                triggerJobs.push({ component, opIndex, job });
            }

            for( const job of resizeJobs )
                this._emit(job.component, job.resolved, job.input, job.locals, intents);
            for( const trigger of triggerJobs ) {
                /* Native CC_TRIGGEROP dispatches its local on_op before it
                 * synthesizes IF_BUTTON<n>; direct minimenu clicks use the
                 * opposite ordering. Keep that distinction observable to a
                 * JavaScript service adapter. */
                const job = trigger.job;
                if( job ) this._emit(job.component, job.resolved, job.input, job.locals, intents);
                this._publishArmedButton(
                    trigger.component, trigger.opIndex, 'cc_triggerop');
            }
        }
    }

    _drainTriggerOpLocals() {
        /* Unlike component follow-ups, app.c consumes this outbound queue in
         * app_tick. It must survive the script boundary that produced it and
         * must never participate in the resize/trigger-op fixed point. */
        while( this.pendingDeferred.triggerOpLocal.length ) {
            const pending = this.pendingDeferred.triggerOpLocal.shift();
            this._publishButtonService({
                kind: 'if_button', source: 'if_triggeroplocal', opIndex: 1,
                componentId: pending.componentId, subId: pending.sub,
            });
        }
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
            return this._result(intents, null, this.operationDepth === 1);
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
            let extra = null;
            try {
                switch( input.type ) {
                    case 'pointer_move': this._pointerMove(input, intents); break;
                    case 'pointer_down': extra = this._pointerDown(input, intents); break;
                    case 'pointer_up': this._pointerUp(input, intents); break;
                    case 'wheel': this._wheel(input, intents); break;
                    case 'key': this._key(input, definition('on_key'), intents); break;
                    case 'key_down':
                        this.interaction.heldKeys.add(input.keyTyped);
                        this.interaction.pressedKeys.add(input.keyTyped);
                        this._key(input, definition('on_key_down'), intents); break;
                    case 'key_up':
                        this.interaction.heldKeys.delete(input.keyTyped);
                        this._key(input, definition('on_key_up'), intents); break;
                    case 'op': this._op(input, intents); break;
                    case 'menu_close':
                        this.interaction.menuOpen = false;
                        this.interaction.menuEntries = [];
                        break;
                    case 'tick':
                        this._tick(input, intents);
                        this.interaction.pressedKeys.clear();
                        break;
                    case 'focus_lost': this._focusLost(); break;
                    default: throw new HostRuntimeError(`unsupported input ${input.type}`, 'BAD_INPUT');
                }
            } finally { this.dispatchDepth--; }
            return this._result(intents, extra, this.operationDepth === 1);
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
        if( !this.interaction.antiDrag && !this.interaction.dragging && meta.draggable &&
            moved > meta.dragDeadZone && elapsed >= meta.dragDeadTime )
            this.interaction.dragging = true;
        if( this.interaction.dragging ) this._emitNamed(pressed, 'on_drag', input, intents,
            { dragTarget: this._hit(input.x, input.y) });
    }

    _pointerDown(input, intents) {
        this._setPointer(input);
        if( input.button !== 2 ) this.interaction.menuOpen = false;
        this._hover(intents, input);
        if( input.button === 2 ) {
            this.interaction.menuEntries = this.menuAt(input.x, input.y);
            this.interaction.menuOpen = true;
            return { menu: cloneValue(this.interaction.menuEntries) };
        }
        if( input.button !== 0 ) return null;
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
        this.interaction.menuOpen = false;
        const component = this._component(input.target);
        if( !this._visible(component) ) return;
        /* app.c sends an armed numbered IF3 operation before it dispatches
         * that component's local on_op. A local-only button still runs its
         * listener; a server-armed button does both in this order. */
        this._publishArmedButton(component, input.opIndex, 'minimenu');
        const resolved = this._resolveHook(component, definition('on_op'));
        if( resolved ) this._emit(component, resolved, input, { opIndex: input.opIndex }, intents);
    }

    _tick(input, intents) {
        this.cycle = input.cycle ?? this.cycle + 1;
        /* RS_CS2Host_Tick advances independently of any caller-provided cycle
         * number: one browser tick is one native client-clock increment. */
        this.clientClock = (this.clientClock + 1) | 0;
        this.chatSocial.chat.clientClock = this.clientClock;
        this.state.clock = this.clientClock;
        this.state.clientClock = this.clientClock;
        this._syncWorldMapDisplaySize();
        if( cycleWorldMapState(this.worldMap) )
            this._record({ kind: 'worldmap-cycle', cycle: this.cycle });
        this._pumpTransmits(intents);
        /* app.c runs and settles processWidgetTimers before
         * UITree_InteractFrame. This ordering is observable in bankmain: its
         * timer clears the mouseover container and onMouseRepeat rebuilds the
         * tooltip for the current pointer. Reversing them clears the freshly
         * rebuilt tooltip again and causes needless dynamic-tree churn. */
        for( const ref of this._hookTargets(definition('on_timer'), this.limits.keyTargets) ) {
            const component = this._component(ref, false);
            if( component && this._visible(component) ) this._emitNamed(component, 'on_timer', input, intents);
        }
        /* Within UITree_InteractFrame, held-press hooks precede hover repeat. */
        const pressed = this._component(this.interaction.pressed, false);
        if( pressed && this.interaction.button === 0 ) {
            const synthetic = { ...input, x: this.interaction.x, y: this.interaction.y };
            const meta = this.meta.get(pressed);
            const moved = Math.max(Math.abs(this.interaction.x - this.interaction.pressX),
                Math.abs(this.interaction.y - this.interaction.pressY));
            if( !this.interaction.antiDrag && !this.interaction.dragging && meta.draggable &&
                moved > meta.dragDeadZone &&
                this.cycle - this.interaction.pressCycle >= meta.dragDeadTime )
                this.interaction.dragging = true;
            if( this.interaction.dragging ) this._emitNamed(pressed, 'on_drag', synthetic, intents,
                { dragTarget: this._hit(this.interaction.x, this.interaction.y) });
            else {
                this._emitNamed(pressed, 'on_hold', synthetic, intents);
                this._emitNamed(pressed, 'on_click_repeat', synthetic, intents);
            }
        }
        const hover = this._component(this.interaction.hover, false);
        if( hover && this._visible(hover) ) this._emitNamed(hover, 'on_mouse_repeat', input, intents);
        this._drainTriggerOpLocals();
    }

    _queueStateTransmit(kind, id, options) {
        /* Rev-239 has widget transmit registries for varps, inventories and
         * stats only.  POP_VARC_* changes the VarCManager, but intentionally
         * raises no widget dirty flag (see rs_cs2_host.c:8771). */
        let channel = null;
        let trigger = id;
        if( kind === 'varp' ) channel = 'var';
        else if( kind === 'varbit' ) {
            channel = 'var';
            const explicit = options.trigger;
            const mapped = this.hostData.varbitVarp[String(id)];
            trigger = transmitId(explicit !== undefined ? explicit : mapped, -1);
        } else if( kind === 'inv' ) channel = 'inv';
        else if( kind === 'stat' || kind === 'statxp' ) channel = 'stat';
        if( channel ) queueTransmitId(this.pendingTransmits[channel], trigger);
    }

    _pumpTransmits(intents) {
        /* RS_CS2_PumpTransmits queues every task before the task runner invokes
         * any hook. Snapshot every selected binding first so an earlier listener
         * cannot rewrite a later listener in the same tick. */
        const batches = [];
        const inv = this.pendingTransmits.inv;
        const vars = this.pendingTransmits.var;
        const stats = this.pendingTransmits.stat;
        if( transmitBucketDirty(inv) ) batches.push(this._stateTransmitBatch(
            'inv', 'inv', definition('on_inv_transmit'), inv,
            inv.all || inv.ids.length !== 1));
        if( this.pendingTransmits.widgetsLoaded ) batches.push(this._unhideTransmitBatch(
            'inv', 'inv', definition('on_inv_transmit')));
        if( transmitBucketDirty(vars) ) batches.push(this._stateTransmitBatch(
            'var', 'varp', definition('on_var_transmit'), vars, vars.all));
        if( this.pendingTransmits.widgetsLoaded ) batches.push(this._unhideTransmitBatch(
            'var', 'varp', definition('on_var_transmit')));
        if( transmitBucketDirty(stats) ) batches.push(this._stateTransmitBatch(
            'stat', 'stat', definition('on_stat_transmit'), stats, stats.all));
        if( this.pendingTransmits.widgetsLoaded ) batches.push(this._unhideTransmitBatch(
            'stat', 'stat', definition('on_stat_transmit')));

        const wantsFriend = this.pendingTransmits.friend;
        const wantsChat = this.pendingTransmits.chat;
        if( wantsFriend ) batches.push({
            kind: 'friend',
            hooks: this._snapshotHookTargets(
                definition('on_friend_transmit'), this.limits.hookInvocations),
        });
        if( wantsChat ) batches.push({
            kind: 'chat',
            hooks: this._snapshotHookTargets(
                definition('on_chat_transmit'), this.limits.hookInvocations),
        });

        if( batches.length === 0 ) return;

        /* Clear before invoking.  A listener that mutates chat/social state
         * or writes host state raises fresh work for the following tick instead
         * of re-entering this pump or being swallowed by this clear-down. */
        clearTransmitBucket(inv);
        clearTransmitBucket(vars);
        clearTransmitBucket(stats);
        this.pendingTransmits.widgetsLoaded = false;
        this.pendingTransmits.friend = false;
        this.pendingTransmits.chat = false;

        for( const batch of batches ) {
            const input = baseEvent('transmit', {
                kind: batch.kind,
                ...(batch.ids ? {
                    triggers: [...batch.ids],
                    all: Boolean(batch.all),
                    ...(batch.ids.length === 1 ? { trigger: batch.ids[0] } : {}),
                } : {}),
            });
            for( const target of batch.hooks ) {
                const component = this._component(target.ref, false);
                if( component ) this._emit(component, target.resolved, input, {}, intents);
            }
        }
    }

    _stateTransmitBatch(channel, kind, descriptor, bucket, wildcard) {
        const ids = [...bucket.ids];
        const hooks = [];
        for( const component of this.ir.components ) {
            if( hooks.length >= this.limits.hookInvocations ) break;
            const resolved = this._resolveHook(component, descriptor);
            if( !resolved ) continue;
            if( !wildcard && !ids.some((id) =>
                transmitMatches(component, kind, id, resolved.binding)) ) continue;
            if( !this._visible(component) ) {
                queuePendingUnhide(this.pendingTransmits.unhide[channel], this.ref(component));
                continue;
            }
            removePendingUnhide(this.pendingTransmits.unhide[channel], this.ref(component));
            hooks.push({
                ref: this.ref(component),
                resolved: {
                    ...resolved,
                    binding: normalizeBinding(resolved.binding, this),
                },
            });
        }
        return { kind, ids, all: wildcard, hooks };
    }

    _unhideTransmitBatch(channel, kind, descriptor) {
        const pending = this.pendingTransmits.unhide[channel];
        const hooks = [];
        for( let index = 0; index < pending.length; ) {
            const ref = pending[index];
            const component = this._component(ref, false);
            const resolved = component && this._resolveHook(component, descriptor);
            if( !component || !resolved ) {
                pending.splice(index, 1);
                continue;
            }
            if( !this._visible(component) ) {
                index++;
                continue;
            }
            if( hooks.length >= this.limits.hookInvocations ) break;
            pending.splice(index, 1);
            hooks.push({
                ref: this.ref(component),
                resolved: {
                    ...resolved,
                    binding: normalizeBinding(resolved.binding, this),
                },
            });
        }
        return { kind, ids: [], all: true, hooks };
    }

    _snapshotHookTargets(descriptor, cap) {
        const result = [];
        for( const component of this.ir.components ) {
            if( result.length >= cap ) break;
            if( !this._visible(component) ) continue;
            const resolved = this._resolveHook(component, descriptor);
            if( !resolved ) continue;
            result.push({
                ref: this.ref(component),
                resolved: {
                    ...resolved,
                    binding: normalizeBinding(resolved.binding, this),
                },
            });
        }
        return result;
    }

    _focusLost() {
        this.interaction.pressed = null;
        this.interaction.button = null;
        this.interaction.clickFired = false;
        this.interaction.dragging = false;
        this.interaction.dragPickupX = 0;
        this.interaction.dragPickupY = 0;
        this.interaction.heldKeys.clear();
        this.interaction.pressedKeys.clear();
        this.interaction.menuOpen = false;
        this.interaction.menuEntries = [];
    }

    _hover(intents, input) {
        const hit = this._hit(input.x, input.y);
        const next = hit ? this.ref(hit) : null;
        if( !this.interaction.menuOpen ) this.interaction.menuEntries = this.menuAt(input.x, input.y);
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
        const component = this._defaultButtonComponent(leaf, resolved?.component);
        /* A pointer may land on decorative content nested inside the actual
         * button. Native minimenu rows name the ancestor which owns the op;
         * script-created object cells are the exception and keep their own
         * object id while _buttonTarget maps them to parent/sub on the wire. */
        this._publishArmedButton(component, 1, 'pointer');
        if( resolved ) this._emit(resolved.component, resolved.hook, input, { opIndex: 1 }, intents);
    }

    _defaultButtonComponent(leaf, resolvedComponent = null) {
        for( let component = leaf; component;
             component = this._parentOf(component) )
            if( finiteOptional(component.static?.objectId, 0) > 0 ) return component;
        if( resolvedComponent ) return resolvedComponent;
        for( let component = leaf; component;
             component = this._parentOf(component) )
            if( component.ops?.some((op) => op.index === 1) ) return component;
        return null;
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
        for( let component = leaf; component; component = this._parentOf(component) ) {
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
            /* Hook registries are sparse. Resolve the cheap listener map
             * before consulting layout visibility; timer scans otherwise do a
             * cached box lookup for every one of bankmain's 1,700+ item cells. */
            if( !this._resolveHook(component, descriptor) || !this._visible(component) ) continue;
            result.push(this.ref(component));
        }
        return result;
    }

    _visible(component) {
        if( this.visibilityVersion !== this.version ) {
            this.visibilityVersion = this.version;
            this.visibilityCache = new WeakMap();
        } else if( this.visibilityCache.has(component) )
            return this.visibilityCache.get(component);
        const visible = layoutVisibility(
            this.ir, this.state, component, null, this.structureRevision);
        this.visibilityCache.set(component, visible);
        return visible;
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
        for( let component = leaf; component; component = this._parentOf(component) ) {
            for( const op of component.ops || [] ) result.push({
                component: this.ref(component), opIndex: op.index, text: op.text,
                target: component.runtime?.opBase || component.static?.name || component.name || '',
            });
        }
        return result.slice(0, 10);
    }

    changes(afterVersion = 0) {
        const length = this.changeLog.length;
        const first = length
            ? this.changeLog[this.changeLogHead]?.version : this.version + 1;
        const changes = [];
        for( let offset = 0; offset < length; offset++ ) {
            const change = this.changeLog[(this.changeLogHead + offset) % length];
            if( change.version > afterVersion ) changes.push(cloneValue(change));
        }
        return {
            from: afterVersion,
            to: this.version,
            truncated: afterVersion < first - 1,
            changes,
        };
    }

    _changed(kind, component, detail) {
        this._record({ kind, ref: this.ref(component), ...detail });
        return this.ref(component);
    }

    _record(change, { layout = true } = {}) {
        this._trackTreeChange(change, layout);
        this._touch(layout, true);
        if( !this.recordChanges ) return;
        const entry = { version: this.version, ...change };
        if( this.changeLog.length < this.limits.changes ) this.changeLog.push(entry);
        else {
            /* A bank redraw emits hundreds of thousands of changes. Shifting a
             * full 4K array for each one made retention O(writes * capacity).
             * Overwrite the oldest slot and advance the chronological head. */
            this.changeLog[this.changeLogHead] = entry;
            this.changeLogHead = (this.changeLogHead + 1) % this.changeLog.length;
        }
    }

    _touch(layout = true, dirtyClassified = false) {
        /* Any new direct caller is unsafe for incremental projection until it
         * states what it changed. This makes missing instrumentation fall back
         * to the established full-layout oracle instead of dropping pixels. */
        if( layout && !dirtyClassified ) this._markTreeFull('unclassified-host-write');
        if( this.fastTouchCount !== null ) {
            this.fastTouchCount++;
            return;
        }
        const cachedLayout = this.layoutVersion === this.version;
        this.version++;
        if( layout ) {
            this.layoutVersion = -1;
            this.interactionVisibilityDirty = true;
        } else if( cachedLayout ) this.layoutVersion = this.version;
    }

    _trackTreeChange(change, layout) {
        if( !layout ) return;
        if( change?.kind === 'component' ) {
            const component = this._component(change.ref, false);
            if( !component ) return this._markTreeFull('stale-component-write');
            return this._markTreeComponent(component, treeDirtyForOperation(change.op));
        }
        if( change?.kind === 'hook' ) {
            const component = this._component(change.ref, false);
            if( !component ) return this._markTreeFull('stale-hook-write');
            return this._markTreeComponent(component, TREE_DIRTY.INTERACTION);
        }
        if( change?.kind === 'input' ) {
            const component = this._component(change.ref, false);
            if( !component ) return this._markTreeFull('stale-input-write');
            return this._markTreeComponent(component, [
                TREE_DIRTY.PAINT, TREE_DIRTY.INTERACTION,
            ]);
        }
        if( change?.kind === 'viewport' ) this.treeViewportDirty = true;
        this._markTreeFull(`${change?.kind || 'unknown'}-write`);
    }

    _markTreeComponent(component, categories) {
        /* Once any write requires the full oracle, per-node keys cannot make
         * that commit cheaper. Avoid materialising thousands of transient
         * dynamic render identities during bank/list rebuilds. */
        if( this.treeDeltaFull ) return;
        const key = this._materializeRenderKey(component);
        for( const category of Array.isArray(categories) ? categories : [categories] ) {
            this.treeDirty[category].add(key);
            if( category !== TREE_DIRTY.PAINT && category !== TREE_DIRTY.INTERACTION )
                this._markTreeFull(`${category}-write`);
        }
    }

    _markTreeFull(reason) {
        this.treeDeltaFull = true;
        this.treeDeltaFallbackReason ||= String(reason || 'full-projector-required');
    }

    _box(component) {
        if( this.layoutVersion === this.version )
            return this.boxByComponent.get(component) || null;
        /* Event-local coordinates need the target's true screen box, including
         * ancestor scroll and clips, but not a rebuilt paint list for every
         * hook inside one synchronous bank transaction. */
        if( this.targetBoxVersion !== this.version ) {
            this.targetBoxVersion = this.version;
            this.targetBoxCache = new WeakMap();
        } else if( this.targetBoxCache.has(component) )
            return this.targetBoxCache.get(component);
        const box = layoutBox(
            this.ir, this.state, this.viewport, component, null, this.structureRevision);
        this.targetBoxCache.set(component, box);
        return box;
    }

    _geometry(component) {
        if( this.targetGeometryVersion !== this.version ) {
            this.targetGeometryVersion = this.version;
            this.targetGeometryCache = new WeakMap();
        } else if( this.targetGeometryCache.has(component) )
            return this.targetGeometryCache.get(component);
        /* The generic preview helper first builds a structural node for every
         * live component. That is ideal for painting, but a CS2 width/height
         * read depends only on the target's ancestor chain. Large dynamic
         * grids (ca_tasks creates 3,230 cells) were rebuilding megabytes of
         * unrelated nodes at each ordered HOST barrier. Walk the already
         * indexed HostRuntime parent chain directly; retain the cycle-tolerant
         * generic path only for malformed authored graphs. */
        const geometry = this._fastGeometry(component) || layoutGeometry(
            this.ir, this.state, this.viewport, component, null, this.structureRevision);
        this.targetGeometryCache.set(component, geometry);
        return geometry;
    }

    _fastGeometry(component) {
        const chain = [];
        for( let cursor = component; cursor; cursor = this._parentOf(cursor) ) {
            chain.push(cursor);
            if( chain.length > this.ir.components.length ) return null;
        }
        let parentX = 0;
        let parentY = 0;
        let parentW = Number(this.viewport.width) | 0;
        let parentH = Number(this.viewport.height) | 0;
        let parentProps = null;
        let parent = null;
        let relX = 0;
        let relY = 0;
        let props = null;
        let width = 0;
        let height = 0;
        let x = 0;
        let y = 0;

        for( let index = chain.length - 1; index >= 0; index-- ) {
            const current = chain[index];
            props = resolveProps(current, this.state);
            const parentIsLayer = parent?.type === IF_TYPE.layer &&
                !(parent.runtimeDynamic && parent.kind === 'Object');
            const scrollWidth = parentIsLayer ? Number(parentProps.scrollWidth) | 0 : 0;
            const scrollHeight = parentIsLayer ? Number(parentProps.scrollHeight) | 0 : 0;
            const availableW = scrollWidth > 0 ? scrollWidth : parentW;
            const availableH = scrollHeight > 0 ? scrollHeight : parentH;
            const widthMode = Number(props.widthMode) | 0;
            const heightMode = Number(props.heightMode) | 0;
            width = dimFromParentMode(widthMode, Number(props.width) | 0, availableW);
            height = dimFromParentMode(heightMode, Number(props.height) | 0, availableH);
            if( widthMode === 4 || heightMode === 4 ) {
                const aspectW = Math.max(1, Number(props.aspectW) | 0 || 1);
                const aspectH = Math.max(1, Number(props.aspectH) | 0 || 1);
                if( widthMode === 4 ) width = Math.trunc(aspectW * height / aspectH);
                if( heightMode === 4 ) height = Math.trunc(aspectH * width / aspectW);
                width = Math.max(0, width);
                height = Math.max(0, height);
            }
            if( !parent && width === 0 && height === 0 ) {
                width = availableW;
                height = availableH;
            }
            x = axisFromPositionMode(
                Number(props.xMode) | 0, Number(props.x) | 0,
                parentX, availableW, width);
            y = axisFromPositionMode(
                Number(props.yMode) | 0, Number(props.y) | 0,
                parentY, availableH, height);
            relX = x - parentX;
            relY = y - parentY;
            parent = current;
            parentProps = props;
            parentX = x;
            parentY = y;
            parentW = width;
            parentH = height;
        }

        const scrollLayer = component.type === IF_TYPE.layer &&
            !(component.runtimeDynamic && component.kind === 'Object');
        const maxScrollX = scrollLayer
            ? Math.max(0, (Number(props.scrollWidth) | 0) - width) : 0;
        const maxScrollY = scrollLayer
            ? Math.max(0, (Number(props.scrollHeight) | 0) - height) : 0;
        return {
            x, y, w: width, h: height, relX, relY,
            scrollX: Math.max(0, Math.min(maxScrollX, Number(props.scrollX) | 0)),
            scrollY: Math.max(0, Math.min(maxScrollY, Number(props.scrollY) | 0)),
        };
    }

    _syncWorldMapDisplaySize() {
        /* Native updates RS_WorldMap from the clientCode-1400 map surface's
         * resolved box, not from the outer canvas. World-map chrome and side
         * panels can make those dimensions materially different. The renderer
         * does not issue that update for an absent, hidden, culled, or empty
         * surface, so retain the last dimensions (or the 512x334 default). */
        const surface = this.ir.components.find((component) =>
            finiteOptional(component.static?.clientCode ?? component.rawFields?.clientcode, -1) === 1400);
        if( !surface ) return false;
        const box = this._box(surface);
        if( !box || box.effectiveHidden || box.culled || box.w <= 0 || box.h <= 0 ) return false;
        return setWorldMapDisplayPixelSize(this.worldMap, box.w, box.h);
    }

    _indexStatic(component) {
        this._publishPendingPublicIndexes();
        const fileId = component.fileId;
        if( this.byFileId.has(fileId) )
            throw new HostRuntimeError(`duplicate component file id ${fileId}`, 'BAD_IR');
        const uid = Number.isInteger(fileId) ? this.interfaceId * 65536 + fileId : null;
        const meta = {
            key: `if:${this.interfaceId}:${String(fileId)}`,
            renderKey: `if:${this.interfaceId}:${String(fileId)}`,
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

    _indexDynamic(component, parent, subId, recycledMeta = null) {
        const parentMeta = this.meta.get(parent);
        const componentId = this._allocateDynamicComponentId(parentMeta.componentId);
        const meta = recycledMeta || {};
        /* Direct writes retain the exact stable field order on both new and
         * recycled metadata without allocating a 12-field Object.assign source
         * for every transient row. */
        meta.key = `dyn:${this.interfaceId}:${this.nextGeneration}`;
        /* Dynamic VM handles are deliberately fresh on every incarnation, but
         * React owns the logical parent/sub-id slot. Deriving this recursively
         * also preserves nested renderer identity when an ancestor slot is
         * deleted and rebuilt during one interface redraw. */
        meta.renderKey = `${this._materializeRenderKey(parent)}/cc:${subId}`;
        /* Match UITree_CcCreate's 0x8000..0xffff UID band. A dynamic component
         * is addressed by this transient packed id inside C; its public child
         * slot remains subId, while renderKey is deliberately renderer-only.
         * Sharing the parent's UID made two active children
         * indistinguishable across the WASM boundary. */
        meta.componentId = componentId;
        meta.publicFileId = parentMeta.publicFileId;
        meta.subId = subId;
        meta.dynamic = true;
        meta.generation = this.nextGeneration++;
        meta.draggable = false;
        meta.dragParent = null;
        meta.dragDeadZone = 5;
        meta.dragDeadTime = 0;
        meta.dragBehavior = 0;
        meta.parent = parent;
        this._index(component, meta);
        this.byUid.set(componentId, component);
        let children = this.dynamicChildren.get(parent);
        if( !children ) {
            children = new Map();
            this.dynamicChildren.set(parent, children);
        }
        children.set(subId, component);
    }

    /* Packed redraw nodes are initially private to C and are usually replaced
     * thousands at a time. Allocate the native identity/indexes now, but defer
     * the JS-facing key, render key and frozen ref until one crosses a public
     * API or the renderer projects it. Generation is still advanced here, so
     * materialising later cannot make an old VM handle live again. */
    _fastIndexDynamic(component, parent, subId, recycledMeta = null) {
        const parentMeta = this.meta.get(parent);
        const componentId = this._allocateDynamicComponentId(parentMeta.componentId);
        const meta = recycledMeta || {};
        meta.key = null;
        meta.renderKey = null;
        meta.parent = parent;
        meta.componentId = componentId;
        meta.publicFileId = parentMeta.publicFileId;
        meta.subId = subId;
        meta.dynamic = true;
        meta.generation = this.nextGeneration++;
        meta.draggable = false;
        meta.dragParent = null;
        meta.dragDeadZone = 5;
        meta.dragDeadTime = 0;
        meta.dragBehavior = 0;
        meta.ref = null;
        meta.directRef = null;
        meta.publicIndexed = false;
        if( this.byName.has(component.name) )
            throw new HostRuntimeError(`duplicate component name ${component.name}`, 'BAD_IR');
        this.meta.set(component, meta);
        meta.pendingPublicIndex = this.pendingPublicIndexes.length;
        this.pendingPublicIndexes.push(component);
        this.byUid.set(componentId, component);
        let children = this.dynamicChildren.get(parent);
        if( !children ) {
            children = new Map();
            this.dynamicChildren.set(parent, children);
        }
        children.set(subId, component);
        return meta;
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
        this._publishPendingPublicIndexes();
        if( this.byName.has(component.name) )
            throw new HostRuntimeError(`duplicate component name ${component.name}`, 'BAD_IR');
        const renderOwner = meta.renderKey && this.byRenderKey.get(meta.renderKey);
        if( renderOwner && renderOwner !== component ) throw new HostRuntimeError(
            `duplicate render key ${meta.renderKey}`, 'BAD_IR');
        meta.ref = Object.freeze({
            key: meta.key,
            componentId: meta.componentId,
            fileId: meta.publicFileId,
            subId: meta.subId,
            dynamic: meta.dynamic,
            generation: meta.generation,
            name: component.name,
        });
        this.meta.set(component, meta);
        this.byKey.set(meta.key, component);
        if( meta.renderKey ) this.byRenderKey.set(meta.renderKey, component);
        this.byName.set(component.name, component);
        this.byFileId.set(component.fileId, component);
        meta.publicIndexed = true;
        meta.pendingPublicIndex = -1;
    }

    /** Remove an unpublished row in O(1) without retaining pooled objects. */
    _removePendingPublicIndex(component, meta) {
        if( !meta || meta.publicIndexed || !Number.isInteger(meta.pendingPublicIndex) ||
            meta.pendingPublicIndex < 0 ) return;
        const pending = this.pendingPublicIndexes;
        const index = meta.pendingPublicIndex;
        if( index >= pending.length || pending[index] !== component ) {
            meta.pendingPublicIndex = -1;
            return;
        }
        const lastIndex = pending.length - 1;
        const last = pending[lastIndex];
        pending.pop();
        if( index !== lastIndex ) {
            pending[index] = last;
            const lastMeta = this.meta.get(last);
            if( lastMeta && !lastMeta.publicIndexed ) lastMeta.pendingPublicIndex = index;
        }
        meta.pendingPublicIndex = -1;
    }

    /**
     * Publish packed-created dynamic components into the public name/file-id
     * maps. Validation is deliberately a separate pass: a duplicate name must
     * not leave half of the pending batch observable. Components deleted
     * before publication are removed in O(1), while the liveness checks below
     * keep publication fail-safe if future instrumentation mutates the queue.
     */
    _publishPendingPublicIndexes() {
        const pending = this.pendingPublicIndexes;
        if( pending.length === 0 ) return;
        const live = [];
        const seenNames = new Set();
        for( let index = 0; index < pending.length; index++ ) {
            const component = pending[index];
            const meta = this.meta.get(component);
            if( !meta || meta.publicIndexed ) continue;
            const owner = this.byName.get(component.name);
            if( (owner && owner !== component) || seenNames.has(component.name) )
                throw new HostRuntimeError(
                    `duplicate component name ${component.name}`, 'BAD_IR');
            seenNames.add(component.name);
            live.push(component);
        }
        for( const component of live ) {
            const meta = this.meta.get(component);
            /* No user code can run during this method, but retain the liveness
             * guard so future observer hooks cannot publish a retired pooled
             * object if this routine gains instrumentation. */
            if( !meta || meta.publicIndexed ) continue;
            this.byName.set(component.name, component);
            this.byFileId.set(component.fileId, component);
            meta.publicIndexed = true;
            meta.pendingPublicIndex = -1;
        }
        pending.length = 0;
    }

    _materializeRef(component) {
        const meta = this.meta.get(component);
        if( !meta ) return null;
        if( meta.ref ) return meta.ref;
        const key = meta.key || `dyn:${this.interfaceId}:${meta.generation}`;
        const owner = this.byKey.get(key);
        if( owner && owner !== component ) throw new HostRuntimeError(
            `duplicate component key ${key}`, 'BAD_IR');
        meta.key = key;
        meta.ref = Object.freeze({
            key,
            componentId: meta.componentId,
            fileId: meta.publicFileId,
            subId: meta.subId,
            dynamic: meta.dynamic,
            generation: meta.generation,
            name: component.name,
        });
        this.byKey.set(key, component);
        return meta.ref;
    }

    /** Minimal stable target returned to the direct TypeScript VM. It carries
     * the native identity and generation fence required by CS2HostComponentRef
     * without forcing a public key string, frozen presentation ref, or byKey
     * entry for every transient list row. */
    _directComponentRef(component) {
        const meta = this.meta.get(component);
        if( !meta ) return null;
        if( meta.ref ) return meta.ref;
        if( meta.directRef ) return meta.directRef;
        meta.directRef = Object.freeze({
            componentId: meta.componentId,
            subId: meta.subId,
            generation: meta.generation,
        });
        return meta.directRef;
    }

    _parentOf(component) {
        if( !component || component.layer === null || component.layer === undefined ) return null;
        const meta = this.meta.get(component);
        return meta?.parent || this.byFileId.get(component.layer) || null;
    }

    _materializeRenderKey(component) {
        const meta = this.meta.get(component);
        if( !meta ) return null;
        if( meta.renderKey ) return meta.renderKey;
        const parent = this._parentOf(component);
        const parentKey = parent ? this._materializeRenderKey(parent) : null;
        if( !parentKey ) throw new HostRuntimeError(
            'dynamic component has no renderer parent', 'STALE_REF');
        meta.renderKey = `${parentKey}/cc:${meta.subId}`;
        const owner = this.byRenderKey.get(meta.renderKey);
        if( owner && owner !== component ) throw new HostRuntimeError(
            `duplicate render key ${meta.renderKey}`, 'BAD_IR');
        this.byRenderKey.set(meta.renderKey, component);
        return meta.renderKey;
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
            else if( Number.isInteger(value.componentId) ) {
                const id = value.componentId;
                const candidate = this.byUid.get(id) ||
                    (id < -1 ? this.byUid.get(id >>> 0) : null) || null;
                const meta = candidate && this.meta.get(candidate);
                if( candidate && (value.generation === undefined ||
                    value.generation === meta.generation) ) component = candidate;
            }
        } else if( typeof value === 'string' ) {
            component = this.byKey.get(value) || this.byName.get(value) || null;
            if( !component && this.pendingPublicIndexes.length ) {
                this._publishPendingPublicIndexes();
                component = this.byKey.get(value) || this.byName.get(value) || null;
            }
            const named = /^interface_(\d+):(\d+)$/.exec(value);
            if( !component && named && Number(named[1]) === this.interfaceId )
                component = this.byUid.get(Number(named[1]) * 65536 + Number(named[2])) || null;
        } else if( Number.isInteger(value) ) {
            /* C/WASM transports packed interface ids as signed i32 values,
             * while the React tree indexes their canonical uint32 identity.
             * Preserve -1 as the missing sentinel and normalize every other
             * negative wire id before falling back to a local file id. */
            component = this.byUid.get(value) ||
                (value < -1 ? this.byUid.get(value >>> 0) : null) ||
                this.byFileId.get(value) || null;
            if( !component && this.pendingPublicIndexes.length ) {
                this._publishPendingPublicIndexes();
                component = this.byUid.get(value) ||
                    (value < -1 ? this.byUid.get(value >>> 0) : null) ||
                    this.byFileId.get(value) || null;
            }
        }
        if( required && !component ) throw new HostRuntimeError('component reference is missing or stale', 'STALE_REF');
        return component;
    }

    _setPointer(input) {
        /* The native input bridge collapses any off-canvas position to the
         * pair (-1,-1); it never exposes a half-valid coordinate. */
        const inside = input.x >= 0 && input.y >= 0 &&
            input.x < this.viewport.width && input.y < this.viewport.height;
        this.interaction.x = inside ? input.x : -1;
        this.interaction.y = inside ? input.y : -1;
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
        this.interactionVisibilityDirty = false;
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
            pressedKeys: [...this.interaction.pressedKeys].sort((a, b) => a - b),
            menuOpen: this.interaction.menuOpen,
            menuEntries: cloneValue(this.interaction.menuEntries),
            antiDrag: this.interaction.antiDrag,
        };
    }

    _result(intents, extra = null, deferInteraction = false) {
        return {
            epoch: this.epoch,
            version: this.version,
            intents,
            /* The outer boundary always refreshes this field after draining
             * component follow-ups and retiring newly-hidden interaction
             * targets. Avoid materialising the guaranteed-to-be-discarded
             * first view. Nested React-authored Host calls are observable
             * before that boundary returns and therefore never defer. */
            interaction: deferInteraction ? null : this._interactionView(),
            ...extra,
        };
    }

    _boundary(fn) {
        const outer = this.operationDepth === 0;
        if( outer ) this.invocations = 0;
        this.operationDepth++;
        let result;
        let completed = false;
        try {
            result = fn();
            completed = true;
            return result;
        } finally {
            try {
                /* HOST requests made by this.invoke() are nested boundaries.
                 * Drain only after their outer hook has completely returned,
                 * while keeping operationDepth raised so deferred listeners
                 * cannot recursively start a second drain on the C VM stack. */
                if( outer && completed ) this._settleSuccessfulBoundary(result);
            } finally { this.operationDepth--; }
        }
    }

    _settleSuccessfulBoundary(result = null) {
        const intents = Array.isArray(result?.intents) ? result.intents : [];
        this._drainDeferredComponents(intents);
        if( this.interactionVisibilityDirty ) this._retireInvisibleInteraction();
        if( result && Array.isArray(result.intents) ) {
            result.version = this.version;
            result.epoch = this.epoch;
            result.interaction = this._interactionView();
        }
        /* A failed operation or deferred/interaction settlement may leave
         * valid synchronous working mutations behind. They remain private
         * until a later successful outer boundary publishes the complete
         * accumulated transaction exactly once. */
        if( this.version !== this.committedMutationVersion ) {
            this.commitRevision++;
            this.committedMutationVersion = this.version;
        }
    }
}

function event(authored, canonical, ...imported) {
    const frozenImported = Object.freeze(imported);
    return Object.freeze({
        authored, canonical, imported: frozenImported,
        aliases: Object.freeze([canonical, ...frozenImported, authored].filter(Boolean)),
    });
}

function createTreeDirtySets() {
    return Object.fromEntries(TREE_DIRTY_CATEGORIES.map((category) => [category, new Set()]));
}

function treeDirtyForOperation(rawOperation) {
    const operation = String(rawOperation || '').toLowerCase().replace(/^cc_/, 'if_');
    if( TREE_GEOMETRY_OPS.has(operation) ) return TREE_DIRTY.GEOMETRY;
    if( TREE_VISIBILITY_OPS.has(operation) ) return TREE_DIRTY.VISIBILITY;
    if( TREE_INTERACTION_OPS.has(operation) ) return TREE_DIRTY.INTERACTION;
    return TREE_DIRTY.PAINT;
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
    return descriptor.aliases;
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

function hookBindingsEqual(left, right) {
    if( scriptId(left) !== scriptId(right) ||
        String(left?.signature || '') !== String(right?.signature || '') ) return false;
    const leftTriggers = left?.triggerIds || left?.trigger_ids || [];
    const rightTriggers = right?.triggerIds || right?.trigger_ids || [];
    return hookValuesEqual(left?.args || [], right?.args || []) &&
        hookValuesEqual(leftTriggers, rightTriggers);
}

function hookBindingMatchesInput(existing, binding, host) {
    if( scriptId(existing) !== scriptId(binding) ||
        String(existing?.signature || '') !== String(binding?.signature || '') ||
        !Array.isArray(binding?.args || []) ) return false;
    const args = (binding.args || []).slice(0, host.limits.hookArgs);
    const suppliedTriggers = binding.triggerIds || binding.trigger_ids || [];
    if( !Array.isArray(suppliedTriggers) ) return false;
    const triggers = binding.script
        ? suppliedTriggers : suppliedTriggers.slice(0, host.limits.hookTriggers);
    const existingTriggers = existing?.triggerIds || existing?.trigger_ids || [];
    return hookValuesEqual(existing?.args || [], args) &&
        hookValuesEqual(existingTriggers, triggers);
}

function hookValuesEqual(left, right) {
    if( Object.is(left, right) ) return true;
    if( !left || !right || typeof left !== 'object' || typeof right !== 'object' ) return false;
    if( Array.isArray(left) !== Array.isArray(right) ) return false;
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    if( leftKeys.length !== rightKeys.length ) return false;
    for( const key of leftKeys ) {
        if( !Object.prototype.hasOwnProperty.call(right, key) ||
            !hookValuesEqual(left[key], right[key]) ) return false;
    }
    return true;
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

/* Positional companion to hookFromRequest(). The TypeScript VM has already
 * separated the listener ABI into fixed arrays and masks, so reconstruct only
 * the retained binding; never manufacture a reflected/tagged Host request. */
function directHookBinding(scriptIdValue, signatureValue, triggerIdsValue,
    triggerCountValue, intArgsValue, intArgCountValue, stringMaskValue,
    stringArgCountValue, stringArgsValue) {
    const signature = String(signatureValue || '');
    const parseLength = Math.min(signature.endsWith('Y')
        ? signature.length - 1 : signature.length, HOST_RUNTIME_LIMITS.hookArgs);
    const integers = requestList(intArgsValue);
    const strings = requestList(stringArgsValue);
    const masks = requestList(stringMaskValue);
    const intCount = Math.max(0, Math.min(parseLength,
        finiteOptional(intArgCountValue, parseLength), integers.length));
    const stringCount = Math.max(0, Math.min(HOST_RUNTIME_LIMITS.hookArgs,
        finiteOptional(stringArgCountValue, strings.length), strings.length));
    const lowMask = finiteOptional(masks[0], 0) >>> 0;
    const highMask = finiteOptional(masks[1], 0) >>> 0;
    const args = new Array(intCount);
    let stringAt = 0;
    for( let index = 0; index < intCount; index++ ) {
        const string = index < 32
            ? Boolean((lowMask >>> index) & 1)
            : Boolean((highMask >>> (index - 32)) & 1);
        args[index] = string && stringAt < stringCount
            ? String(strings[stringAt++] ?? '') : finiteOptional(integers[index], 0);
    }
    const triggers = requestList(triggerIdsValue);
    const triggerCount = Math.max(0, Math.min(HOST_RUNTIME_LIMITS.hookTriggers,
        finiteOptional(triggerCountValue, triggers.length), triggers.length));
    return {
        script: { id: finiteOptional(scriptIdValue, -1) },
        args,
        signature,
        triggerIds: triggers.slice(0, triggerCount).map((value) => finiteOptional(value, 0)),
    };
}

function fastObjectPropChanged(mask, prop) {
    switch( prop ) {
        case 'objectId': return Boolean(mask & 1 << 0);
        case 'objectCount': return Boolean(mask & 1 << 1);
        case 'objectNumMode': return Boolean(mask & 1 << 2);
        case 'modelKind': return Boolean(mask & 1 << 3);
        case 'modelSourceId': return Boolean(mask & 1 << 4);
        case 'xAngle': return Boolean(mask & 1 << 5);
        case 'yAngle': return Boolean(mask & 1 << 6);
        case 'zoom': return Boolean(mask & 1 << 7);
        case 'xOffset': return Boolean(mask & 1 << 8);
        case 'yOffset': return Boolean(mask & 1 << 9);
        default: return false;
    }
}

function fastRecordString(records, base, arena) {
    const offset = records[base + 3];
    const length = records[base + 4];
    if( offset < 0 || length < 0 || offset > arena.length || length > arena.length - offset )
        throw new HostRuntimeError('packed fast host string overflow', 'BAD_REQUEST');
    return FAST_HOST_TEXT_DECODER.decode(arena.subarray(offset, offset + length));
}

function fastHookPayload(records, base, arena, arenaView, payload) {
    const triggerCount = records[base + 3];
    const intCount = records[base + 4];
    const stringCount = records[base + 7];
    const signatureLength = records[base + 8];
    const payloadOffset = records[base + 9];
    const payloadLength = records[base + 10];
    if( triggerCount < 0 || triggerCount > HOST_RUNTIME_LIMITS.hookTriggers ||
        intCount < 0 || intCount > HOST_RUNTIME_LIMITS.hookArgs ||
        stringCount < 0 || stringCount > 16 ||
        signatureLength < 0 || signatureLength > HOST_RUNTIME_LIMITS.hookArgs + 1 ||
        payloadOffset < 0 || payloadLength < 4 ||
        payloadOffset > arena.length - payloadLength || payloadOffset % 4 !== 0 )
        throw new HostRuntimeError('malformed packed fast host hook', 'BAD_REQUEST');
    const storedSignatureLength = arenaView.getInt32(payloadOffset, true);
    if( storedSignatureLength !== signatureLength ) throw new HostRuntimeError(
        'packed fast host hook signature length mismatch', 'BAD_REQUEST');
    const signatureOffset = payloadOffset + 4;
    const triggerOffset = signatureOffset + ((signatureLength + 3) & ~3);
    const intOffset = triggerOffset + triggerCount * 4;
    const stringOffset = intOffset + intCount * 4;
    const end = stringOffset + stringCount * FAST_HOST_HOOK_STRING_LENGTH;
    if( end > payloadOffset + payloadLength ) throw new HostRuntimeError(
        'packed fast host hook payload overflow', 'BAD_REQUEST');
    payload.triggerCount = triggerCount;
    payload.intCount = intCount;
    payload.stringCount = stringCount;
    payload.signatureLength = signatureLength;
    payload.signatureOffset = signatureOffset;
    payload.triggerOffset = triggerOffset;
    payload.intOffset = intOffset;
    payload.stringOffset = stringOffset;
    return payload;
}

function fastHookMatches(existing, records, base, arena, arenaView, payload) {
    if( scriptId(existing) !== records[base + 2] ) return false;
    const signature = String(existing?.signature || '');
    if( signature.length !== payload.signatureLength ) return false;
    for( let index = 0; index < signature.length; index++ )
        if( signature.charCodeAt(index) !== arena[payload.signatureOffset + index] ) return false;

    const triggers = existing?.triggerIds || existing?.trigger_ids || [];
    if( triggers.length !== payload.triggerCount ) return false;
    for( let index = 0; index < payload.triggerCount; index++ )
        if( triggers[index] !== arenaView.getInt32(payload.triggerOffset + index * 4, true) )
            return false;

    const args = existing?.args || [];
    if( args.length !== payload.intCount ) return false;
    let stringAt = 0;
    const lowMask = records[base + 5] >>> 0;
    const highMask = records[base + 6] >>> 0;
    for( let index = 0; index < payload.intCount; index++ ) {
        const string = index < 32
            ? Boolean((lowMask >>> index) & 1)
            : Boolean((highMask >>> (index - 32)) & 1);
        if( string ) {
            if( stringAt >= payload.stringCount || String(args[index] ?? '') !==
                fastHookString(arena, payload.stringOffset + stringAt * FAST_HOST_HOOK_STRING_LENGTH) )
                return false;
            stringAt++;
        } else if( args[index] !== arenaView.getInt32(payload.intOffset + index * 4, true) )
            return false;
    }
    return stringAt === payload.stringCount;
}

function fastHookBinding(records, base, arena, arenaView, payload) {
    const signature = fastHookSignature(
        arena, payload.signatureOffset, payload.signatureLength);
    const triggerIds = new Array(payload.triggerCount);
    for( let index = 0; index < triggerIds.length; index++ )
        triggerIds[index] = arenaView.getInt32(payload.triggerOffset + index * 4, true);
    const args = new Array(payload.intCount);
    let stringAt = 0;
    const lowMask = records[base + 5] >>> 0;
    const highMask = records[base + 6] >>> 0;
    for( let index = 0; index < args.length; index++ ) {
        const string = index < 32
            ? Boolean((lowMask >>> index) & 1)
            : Boolean((highMask >>> (index - 32)) & 1);
        args[index] = string
            ? fastHookString(arena,
                payload.stringOffset + stringAt++ * FAST_HOST_HOOK_STRING_LENGTH)
            : arenaView.getInt32(payload.intOffset + index * 4, true);
    }
    if( stringAt !== payload.stringCount ) throw new HostRuntimeError(
        'packed fast host hook string mask mismatch', 'BAD_REQUEST');
    return {
        script: { id: records[base + 2] }, args, signature, triggerIds,
    };
}

function fastHookSignature(arena, offset, length) {
    /* C emits the listener ABI vocabulary as single-byte ASCII (`i`, `s`, and
     * the optional trigger marker). Decode those bytes directly so every hook
     * install does not allocate a short-lived Uint8Array subview. */
    let result = '';
    let index = 0;
    for( ; index + 4 <= length; index += 4 ) result += String.fromCharCode(
        arena[offset + index], arena[offset + index + 1],
        arena[offset + index + 2], arena[offset + index + 3]);
    for( ; index < length; index++ )
        result += String.fromCharCode(arena[offset + index]);
    return result;
}

function fastHookString(arena, offset) {
    const limit = offset + FAST_HOST_HOOK_STRING_LENGTH;
    let end = offset;
    while( end < limit && arena[end] !== 0 ) end++;
    return FAST_HOST_TEXT_DECODER.decode(arena.subarray(offset, end));
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

function pendingTransmitState(seed) {
    const value = seed && typeof seed === 'object' ? seed : {};
    return {
        friend: Boolean(value.friend),
        chat: Boolean(value.chat),
        var: pendingTransmitBucket(value.var),
        inv: pendingTransmitBucket(value.inv),
        stat: pendingTransmitBucket(value.stat),
        widgetsLoaded: Boolean(value.widgetsLoaded),
        unhide: {
            var: pendingUnhideRefs(value.unhide?.var),
            inv: pendingUnhideRefs(value.unhide?.inv),
            stat: pendingUnhideRefs(value.unhide?.stat),
        },
    };
}

function pendingTransmitBucket(seed) {
    const value = seed && typeof seed === 'object' ? seed : {};
    const ids = [];
    for( const rawId of Array.isArray(value.ids) ? value.ids : [] ) {
        const id = transmitId(rawId, -1);
        if( id < 0 || ids.includes(id) ) continue;
        if( ids.length >= TRANSMIT_CHANGED_ID_LIMIT ) return { all: true, ids: [] };
        ids.push(id);
    }
    ids.sort((left, right) => left - right);
    return { all: Boolean(value.all), ids: value.all ? [] : ids };
}

function snapshotPendingTransmits(pending) {
    const result = { friend: Boolean(pending.friend), chat: Boolean(pending.chat) };
    for( const channel of ['var', 'inv', 'stat'] ) {
        const bucket = pending[channel];
        if( transmitBucketDirty(bucket) ) result[channel] = {
            all: Boolean(bucket.all), ids: [...bucket.ids],
        };
    }
    const unhide = {};
    for( const channel of ['var', 'inv', 'stat'] )
        if( pending.unhide[channel].length )
            unhide[channel] = pending.unhide[channel].map((ref) => cloneValue(ref));
    if( Object.keys(unhide).length ) result.unhide = unhide;
    if( pending.widgetsLoaded ) result.widgetsLoaded = true;
    return result;
}

function pendingDeferredState(seed) {
    const value = seed && typeof seed === 'object' && !Array.isArray(seed) ? seed : {};
    const componentIds = (entries) => requestList(entries)
        .map((entry) => finiteOptional(entry?.componentId ?? entry?.component_id ?? entry, NaN))
        .filter(Number.isSafeInteger)
        .slice(0, DEFERRED_COMPONENT_QUEUE_LIMIT)
        .map((componentId) => ({ componentId }));
    const pairs = (entries, valueName, ...aliases) => requestList(entries)
        .map((entry) => {
            if( !entry || typeof entry !== 'object' || Array.isArray(entry) ) return null;
            const componentId = finiteOptional(entry.componentId ?? entry.component_id, NaN);
            const valueKey = aliases.find((key) => entry[key] !== undefined);
            const item = finiteOptional(valueKey ? entry[valueKey] : entry[valueName], NaN);
            return Number.isSafeInteger(componentId) && Number.isSafeInteger(item)
                ? { componentId, [valueName]: item } : null;
        })
        .filter(Boolean)
        .slice(0, DEFERRED_COMPONENT_QUEUE_LIMIT);
    return {
        callOnResize: componentIds(value.callOnResize ?? value.call_on_resize),
        triggerOp: pairs(value.triggerOp ?? value.trigger_op, 'opIndex', 'op_index', 'index'),
        triggerOpLocal: pairs(
            value.triggerOpLocal ?? value.trigger_op_local ?? value.triggeroplocal,
            'sub', 'subId', 'sub_id'),
    };
}

function snapshotPendingDeferred(pending) {
    const result = {};
    for( const name of ['callOnResize', 'triggerOp', 'triggerOpLocal'] )
        if( pending[name].length ) result[name] = pending[name].map((entry) => ({ ...entry }));
    return result;
}

function pendingUnhideRefs(value) {
    if( !Array.isArray(value) ) return [];
    const result = [];
    for( const ref of value ) {
        if( !ref || typeof ref !== 'object' || typeof ref.key !== 'string' ) continue;
        if( result.some((existing) => sameRef(existing, ref)) ) continue;
        result.push(cloneValue(ref));
    }
    return result;
}

function queuePendingUnhide(pending, ref) {
    if( ref && !pending.some((existing) => sameRef(existing, ref)) ) pending.push(ref);
}

function removePendingUnhide(pending, ref) {
    const index = pending.findIndex((existing) => sameRef(existing, ref));
    if( index >= 0 ) pending.splice(index, 1);
}

function transmitBucketDirty(bucket) {
    return Boolean(bucket?.all || bucket?.ids?.length);
}

function clearTransmitBucket(bucket) {
    bucket.all = false;
    bucket.ids.length = 0;
}

function queueTransmitId(bucket, rawId) {
    if( bucket.all ) return;
    const id = transmitId(rawId, -1);
    /* Match RS_CS2Host_Notify* exactly: once the 64-entry set is full, even a
     * later duplicate promotes the dispatch to wildcard before deduplication. */
    if( id < 0 || bucket.ids.length >= TRANSMIT_CHANGED_ID_LIMIT ) {
        bucket.all = true;
        bucket.ids.length = 0;
        return;
    }
    if( !bucket.ids.includes(id) ) bucket.ids.push(id);
}

function transmitId(value, fallback) {
    const number = Number(value);
    return Number.isSafeInteger(number) && number >= 0 && number <= 0x7fffffff
        ? number : fallback;
}

function transmitMatches(component, kind, trigger, binding = null) {
    const hookTriggers = binding?.triggerIds || binding?.trigger_ids;
    if( Array.isArray(hookTriggers) )
        return hookTriggers.length === 0 || hookTriggers.includes(trigger);
    const field = kind === 'stat' ? 'stattriggers' : kind === 'inv' ? 'invtriggers' : 'varptriggers';
    const triggers = component.triggers?.[field];
    return !Array.isArray(triggers) || triggers.length === 0 || triggers.includes(trigger);
}

function stateValuesEqual(left, right) {
    if( left === right ) return true;
    if( !left || !right || typeof left !== 'object' || typeof right !== 'object' ) return false;
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    if( leftKeys.length !== rightKeys.length ) return false;
    return leftKeys.every((key) => Object.prototype.hasOwnProperty.call(right, key) &&
        left[key] === right[key]);
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
    return { opBase: '', targetPriority: 0, submenus: null, params: null, opKeys: null, input: null };
}

function resetRuntimeState(runtime) {
    runtime.opBase = '';
    runtime.targetPriority = 0;
    runtime.submenus = null;
    runtime.params = null;
    runtime.opKeys = null;
    runtime.input = null;
    return runtime;
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

function dynamicPropsTemplate(type, kind) {
    const cacheKey = `${type}:${kind}`;
    const cached = DYNAMIC_PROPS_CACHE.get(cacheKey);
    if( cached ) return cached;
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
    DYNAMIC_PROPS_CACHE.set(cacheKey, Object.freeze(result));
    return result;
}

function dynamicProps(type, kind) {
    return { ...dynamicPropsTemplate(type, kind) };
}

function resetDynamicProps(target, type, kind) {
    const template = dynamicPropsTemplate(type, kind);
    /* Fast setters may add fields that are not part of the authored type's
     * default schema. A pooled component must be indistinguishable from a new
     * CC_CREATE, so drop every such field before copying the canonical
     * defaults back onto the stable-shape object. */
    for( const key of Object.keys(target) ) {
        if( !Object.prototype.hasOwnProperty.call(template, key) ) delete target[key];
    }
    return Object.assign(target, template);
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
    const modelId = finiteOptional(props.modelSourceId ?? props.model, -1);
    const clientCode = finiteOptional(props.clientCode, -1);
    /* The native UITree builder assigns both local-player model client-code
     * variants before paint. Imported cache widgets carry no model id, so
     * leaving them as an unavailable plain model bypasses ToriDraw's player
     * appearance path entirely. An explicit CS2 model kind still wins. */
    if( props.modelKind === undefined && (clientCode === 327 || clientCode === 328) &&
        modelId < 0 && finiteOptional(props.objectId, -1) < 0 )
        return { kind: 'playerSelf', id: -1 };
    const kind = modelKind(props.modelKind || (props.objectId > 0 ? 'object' : 'model'));
    const id = kind === 'object' ? props.modelSourceId ?? props.objectId ?? 0
        : modelId;
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

function sessionState(options, state, rawHostData, canvasViewport) {
    const hostSession = rawHostData && typeof rawHostData.session === 'object'
        ? rawHostData.session : {};
    const stateSession = state && typeof state.session === 'object' ? state.session : {};
    const supplied = options.session && typeof options.session === 'object' ? options.session : {};
    const camera = { ...(hostSession.camera || {}), ...(stateSession.camera || {}),
        ...(supplied.camera || {}), ...(options.camera || {}) };
    const viewportSettings = { ...(hostSession.viewport || {}), ...(stateSession.viewport || {}),
        ...(supplied.viewport || {}), ...(options.viewportSettings || {}) };
    const pick = (name, fallback) => options[name] ?? supplied[name] ?? stateSession[name] ??
        hostSession[name] ?? state?.[name] ?? fallback;
    return {
        windowMode: windowMode(pick('windowMode', 2)),
        defaultWindowMode: windowMode(pick('defaultWindowMode', 2)),
        localCoord: finiteOptional(pick('localCoord', 0), 0),
        destinationCoord: finiteOptional(pick('destinationCoord', -1), -1),
        mapWorld: finiteOptional(pick('mapWorld', 0), 0),
        staffModLevel: finiteOptional(pick('staffModLevel', 0), 0),
        minimapZoom: finiteOptional(pick('minimapZoom', 2), 2),
        gameOptions: optionState('game', [
            hostSession.gameOptions, stateSession.gameOptions, supplied.gameOptions, options.gameOptions,
        ], state),
        deviceOptions: optionState('device', [
            hostSession.deviceOptions, stateSession.deviceOptions, supplied.deviceOptions,
            options.deviceOptions,
        ], state),
        camera: {
            angleX: clampSeed(camera.angleX ?? camera.angle_x, 128, 383, 128),
            angleY: finiteOptional(camera.angleY ?? camera.angle_y, 0) & 0x7ff,
            yaw: finiteOptional(camera.yaw, camera.angleY ?? camera.angle_y ?? 0) & 0x7ff,
            followHeight: finiteOptional(camera.followHeight ?? camera.follow_height, 0),
            forced: Boolean(camera.forced),
        },
        viewport: {
            zoom: positiveOr(viewportSettings.zoom, 256),
            zoomMax: positiveOr(viewportSettings.zoomMax ?? viewportSettings.zoom_max, 320),
            zoomNear: positiveOr(viewportSettings.zoomNear ?? viewportSettings.zoom_near, 256),
            zoomFar: positiveOr(viewportSettings.zoomFar ?? viewportSettings.zoom_far, 256),
            fovMin: positiveOr(viewportSettings.fovMin ?? viewportSettings.fov_min, 1),
            fovMax: positiveOr(viewportSettings.fovMax ?? viewportSettings.fov_max, 32767),
            aspectMin: positiveOr(viewportSettings.aspectMin ?? viewportSettings.aspect_min, 1),
            aspectMax: positiveOr(viewportSettings.aspectMax ?? viewportSettings.aspect_max, 32767),
            canvasWidth: canvasViewport.width,
            canvasHeight: canvasViewport.height,
        },
        objectSearch: { ids: [], index: 0 },
    };
}

function optionState(kind, sources, state) {
    const result = Array.from({ length: 64 }, (_, id) => optionDefault(kind, id));
    for( const source of sources ) {
        if( !source || typeof source !== 'object' ) continue;
        for( const [rawId, rawValue] of Object.entries(source) ) {
            const id = Number(rawId);
            if( Number.isInteger(id) && id >= 0 && id < result.length )
                result[id] = normalizeOptionValue(kind, id, rawValue);
        }
    }
    const prefix = `${kind}option:`;
    for( const [key, rawValue] of Object.entries(state || {}) ) {
        if( !key.startsWith(prefix) ) continue;
        const id = Number(key.slice(prefix.length));
        if( Number.isInteger(id) && id >= 0 && id < result.length )
            result[id] = normalizeOptionValue(kind, id, rawValue);
    }
    return result;
}

function optionDefault(kind, id) {
    if( kind === 'device' && id === 19 ) return 0;
    if( kind === 'device' && id === 27 ) return 100;
    if( kind === 'device' && id === 15 ) return 2;
    return kind === 'game' && [7, 8, 9].includes(id) ? 100 : 0;
}

function normalizeOptionValue(kind, id, rawValue) {
    let value = finiteOptional(rawValue, optionDefault(kind, id));
    if( (kind === 'game' && [7, 8, 9].includes(id)) || (kind === 'device' && id === 19) )
        value = Math.max(0, Math.min(100, value));
    if( kind === 'device' && id === 27 ) value = Math.max(100, Math.min(400, value));
    if( kind === 'device' && id === 15 ) value = Math.max(0, Math.min(2, value));
    return value;
}

function windowMode(value) {
    const mode = finiteOptional(value, 2);
    return mode === 1 || mode === 2 ? mode : 2;
}

function clampSeed(value, low, high, fallback) {
    const integer = finiteOptional(value, fallback);
    return Math.max(low, Math.min(high, integer));
}

function positiveOr(value, fallback) {
    const integer = finiteOptional(value, fallback);
    return integer > 0 ? integer : fallback;
}

function viewportZoomDecode(value) {
    const zoom = Math.trunc(2 ** (finiteOptional(value, 0) / 256 + 7));
    return Number.isSafeInteger(zoom) && zoom > 0 ? zoom : 256;
}

function viewportZoomEncode(value) {
    const zoom = finiteOptional(value, 0);
    return zoom > 0 ? Math.trunc((Math.log2(zoom) - 7) * 256) : 0;
}

function effectiveViewportSize(settings, rawWidth, rawHeight) {
    let width = Math.max(1, finiteOptional(rawWidth, 1));
    let height = Math.max(1, finiteOptional(rawHeight, 1));
    const band = height - 334;
    let fov = band < 0 ? settings.zoomNear : band >= 100 ? settings.zoomFar
        : Math.trunc((settings.zoomFar - settings.zoomNear) * band / 100) + settings.zoomNear;
    const aspect = height * fov * 512 / (width * 334);
    if( aspect < settings.aspectMin ) {
        const floorAspect = settings.aspectMin;
        fov = width * floorAspect * 334 / (height * 512);
        if( fov > settings.fovMax ) {
            fov = settings.fovMax;
            const visible = height * fov * 512 / (floorAspect * 334);
            const cut = Math.trunc((width - visible) / 2);
            width -= cut * 2;
        }
    } else if( aspect > settings.aspectMax ) {
        const ceilAspect = settings.aspectMax;
        fov = width * ceilAspect * 334 / (height * 512);
        if( fov < settings.fovMin ) {
            fov = settings.fovMin;
            const visible = width * ceilAspect * 334 / (fov * 512);
            const cut = Math.trunc((height - visible) / 2);
            height -= cut * 2;
        }
    }
    return [width, height];
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
        'key_down', 'key_up', 'op', 'menu_close', 'tick', 'focus_lost']);
    if( !known.has(type) ) throw new HostRuntimeError(`unsupported input event ${raw.type}`, 'BAD_INPUT');
    return result;
}

function baseEvent(type, values = {}) {
    return { type, ...values };
}

function eventView(input) {
    const result = { type: input.type };
    for( const key of ['x', 'y', 'button', 'wheel', 'keyTyped', 'keyPressed', 'opIndex',
        'cycle', 'kind', 'id', 'trigger', 'triggers', 'all'] )
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

function targetOf(request, host, kind, preferred = 'component_id') {
    const explicit = request.ref ?? request.component ?? request.component_ref ?? request.target;
    if( explicit !== undefined ) return explicit;
    if( preferred === 'parent_id' ) {
        if( request.parent !== undefined ) return request.parent;
        if( request.parent_id !== undefined ) return request.parent_id;
        if( request.parentId !== undefined ) return request.parentId;
    }
    if( preferred === 'uid' && request.uid !== undefined ) return request.uid;
    const componentId = request.component_id ?? request.componentId;
    if( componentId !== undefined && kind.startsWith('CC_') ) {
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
    const cached = NORMALIZED_HOST_REQUEST_KINDS.get(value);
    if( cached ) return cached;
    const raw = value.toUpperCase();
    const kind = raw === 'CLIENT_CLOCK' ? 'CLIENTCLOCK' : raw;
    if( !/^[A-Z0-9_]+$/.test(kind) ) throw new HostRuntimeError('host request kind is invalid', 'BAD_REQUEST');
    NORMALIZED_HOST_REQUEST_KINDS.set(value, kind);
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

function missingComponentNoop(suffix) {
    return Boolean(REQUEST_SETTERS[suffix] || INPUT_SETTERS[suffix] ||
        MISSING_COMPONENT_NOOP_SUFFIXES.has(suffix) || suffix.startsWith('SETON') ||
        suffix.startsWith('INPUT_SETON'));
}

function missingComponentGetter(suffix) {
    if( suffix === 'GETTEXT' || suffix === 'GETOP' || suffix === 'GETOPBASE' ) return '';
    if( suffix === 'GETLAYER' || suffix === 'GETID' ) return -1;
    if( suffix === 'GETHIDE' || suffix === 'GETMODELTRANSPARENT' ) return false;
    return 0;
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

const CLIENT_MONTH_NAMES = Object.freeze([
    'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
    'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec',
]);

/* Native FROMDATE anchors CS2 day zero 11,745 UTC days after Unix epoch.
 * Date handles the entire realistic game range. The integer civil-date
 * fallback keeps the HOST deterministic for the remaining signed-i32 inputs
 * that exceed ECMAScript Date's TimeClip range. */
function formatClientDate(rawDay) {
    const day = boundedInteger('date day', rawDay, -0x80000000, 0x7fffffff);
    const unixDay = day + 11745;
    const date = new Date(unixDay * 86400000);
    if( Number.isFinite(date.getTime()) ) return `${date.getUTCDate()}-${
        CLIENT_MONTH_NAMES[date.getUTCMonth()]}-${date.getUTCFullYear()}`;

    let z = BigInt(unixDay) + 719468n;
    const era = (z >= 0n ? z : z - 146096n) / 146097n;
    const dayOfEra = z - era * 146097n;
    const yearOfEra = (dayOfEra - dayOfEra / 1460n + dayOfEra / 36524n -
        dayOfEra / 146096n) / 365n;
    let year = yearOfEra + era * 400n;
    const dayOfYear = dayOfEra -
        (365n * yearOfEra + yearOfEra / 4n - yearOfEra / 100n);
    const monthPrime = (5n * dayOfYear + 2n) / 153n;
    const monthDay = dayOfYear - (153n * monthPrime + 2n) / 5n + 1n;
    const month = monthPrime + (monthPrime < 10n ? 3n : -9n);
    if( month <= 2n ) year++;
    return `${monthDay}-${CLIENT_MONTH_NAMES[Number(month - 1n)]}-${year}`;
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
        inventoryTypes: record(source.inventoryTypes ?? source.invTypes),
        mapElements: record(source.mapElements),
        params: record(source.params),
        structs: record(source.structs),
        dbTables: record(source.dbTables),
        dbRows: record(source.dbRows),
        worldMap: record(source.worldMap ?? source.worldmap),
        varbitVarp: record(source.varbitVarp ?? source.varbit_varp),
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

function minimenuSubjectType(kind) {
    return MINIMENU_SUBJECT_TYPES[String(kind ?? '').toLowerCase()] ?? 0;
}

function sameSubjectContext(left, right) {
    if( left === right ) return true;
    if( !left || !right ) return false;
    return left.kind === right.kind && left.scriptId === right.scriptId &&
        left.uid === right.uid && left.type === right.type && left.count === right.count &&
        left.layer === right.layer && left.coord === right.coord && left.name === right.name;
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
