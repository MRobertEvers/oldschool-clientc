/*
 * The intrinsic elements, and what each prop is in the cache.
 *
 * One table, three consumers: the .if writer needs the field name a static prop
 * settles into, the CS2 writer needs the command a dynamic one has to be applied
 * with at runtime, and the .d.ts is written against the same set. Keeping them in
 * one place is what stops a prop from being writable at build time and dead at
 * runtime, or the other way round.
 *
 * `field` is the key cachepack's interface reader accepts (tools/cachepack/cp_decode.c,
 * `read_component_line`). `op` is the CS2 command a change is applied with — null
 * means the prop can only be static, and binding state to it is a compile error
 * rather than a value that silently never updates.
 *
 * Several props share one command: if_setposition takes x, y and both modes at
 * once, so they name the same `op` and the emitter groups them (see emit_cs2.js
 * `groupApplications`). A grouped command re-sends the sibling props it was given
 * along with the changed one, which is why their static values stay on the node.
 */

import { INT, STRING, BOOL } from './expr.js';

/** IF3 component types, from RuneStar's iftype-names.tsv plus the container at 0. */
export const IF_TYPE = {
    layer: 0,
    inv: 2,
    rectangle: 3,
    text: 4,
    graphic: 5,
    model: 6,
    line: 9,
};

export const ALIGN_H = { left: 0, centre: 1, center: 1, right: 2 };
export const ALIGN_V = { top: 0, centre: 1, center: 1, bottom: 2 };
export const POS_MODE_H = { abs: 0, abs_left: 0, abs_centre: 1, abs_center: 1, abs_right: 2, proportional: 3 };
export const POS_MODE_V = { abs: 0, abs_top: 0, abs_centre: 1, abs_center: 1, abs_bottom: 2, proportional: 3 };
export const SIZE_MODE = { abs: 0, minus: 1, proportional: 2 };

/**
 * Props every component has. Geometry first, because it is the one group whose
 * runtime command bundles four fields.
 */
const COMMON = {
    x: { field: 'x', type: INT, op: 'if_setposition', default: 0 },
    y: { field: 'y', type: INT, op: 'if_setposition', default: 0 },
    xMode: { field: 'xmode', type: INT, op: 'if_setposition', default: 0, enum: POS_MODE_H },
    yMode: { field: 'ymode', type: INT, op: 'if_setposition', default: 0, enum: POS_MODE_V },
    width: { field: 'width', type: INT, op: 'if_setsize', default: 0 },
    height: { field: 'height', type: INT, op: 'if_setsize', default: 0 },
    widthMode: { field: 'widthmode', type: INT, op: 'if_setsize', default: 0, enum: SIZE_MODE },
    heightMode: { field: 'heightmode', type: INT, op: 'if_setsize', default: 0, enum: SIZE_MODE },

    hidden: { field: 'hidden', type: BOOL, op: 'if_sethide', default: false },
    transparency: { field: 'trans', type: INT, op: 'if_settrans', default: 0 },
    noClickThrough: { field: 'noclickthrough', type: BOOL, op: 'if_setnoclickthrough', default: false },
    clickMask: { field: 'clickmask', type: INT, op: null, default: 0 },
    scrollWidth: { field: 'scrollwidth', type: INT, op: 'if_setscrollsize', default: 0 },
    scrollHeight: { field: 'scrollheight', type: INT, op: 'if_setscrollsize', default: 0 },
    /*
     * `name` and `targetVerb` are written because the encoder wants them, not
     * because they arrive: this client drops cache-authored name/target text on
     * the way to the runtime (see the IF3 opBase note in the repo's memory).
     * Authoring them is still right — the cache is correct, and the client's gap
     * is a client bug — but a prop bound to state through them would never move.
     */
    name: { field: 'name', type: STRING, op: null, default: '' },
    targetVerb: { field: 'targetverb', type: STRING, op: 'if_settargetverb', default: '' },
};

const TEXT_PROPS = {
    text: { field: 'text', type: STRING, op: 'if_settext', default: '' },
    font: { field: 'font', type: INT, op: 'if_settextfont', default: 0 },
    color: { field: 'colour', type: INT, op: 'if_setcolour', default: 0 },
    shadow: { field: 'shadow', type: BOOL, op: 'if_settextshadow', default: false },
    halign: { field: 'halign', type: INT, op: 'if_settextalign', default: 0, enum: ALIGN_H },
    valign: { field: 'valign', type: INT, op: 'if_settextalign', default: 0, enum: ALIGN_V },
    lineHeight: { field: 'lineheight', type: INT, op: 'if_settextalign', default: 0 },
};

export const ELEMENTS = {
    Layer: {
        type: IF_TYPE.layer,
        props: { ...COMMON },
        children: true,
    },
    Rect: {
        type: IF_TYPE.rectangle,
        props: {
            ...COMMON,
            color: { field: 'colour', type: INT, op: 'if_setcolour', default: 0 },
            fill: { field: 'fill', type: BOOL, op: 'if_setfill', default: false },
            alpha: { field: 'alpha', type: BOOL, op: null, default: false },
        },
    },
    Text: {
        type: IF_TYPE.text,
        props: { ...COMMON, ...TEXT_PROPS },
    },
    Graphic: {
        type: IF_TYPE.graphic,
        props: {
            ...COMMON,
            sprite: { field: 'graphic', type: INT, op: 'if_setgraphic', default: -1 },
            angle: { field: 'angle', type: INT, op: 'if_set2dangle', default: 0 },
            tiled: { field: 'tiled', type: BOOL, op: 'if_settiling', default: false },
            alpha: { field: 'alpha', type: BOOL, op: null, default: false },
            outline: { field: 'outline', type: INT, op: 'if_setoutline', default: 0 },
            shadow: { field: 'graphicshadow', type: INT, op: 'if_setgraphicshadow', default: 0 },
            color: { field: 'colour', type: INT, op: 'if_setcolour', default: 0 },
            hFlip: { field: 'hflip', type: BOOL, op: 'if_sethflip', default: false },
            vFlip: { field: 'vflip', type: BOOL, op: 'if_setvflip', default: false },
        },
    },
    Model: {
        type: IF_TYPE.model,
        props: {
            ...COMMON,
            model: { field: 'model', type: INT, op: 'if_setmodel', default: -1 },
            zoom: { field: 'modelzoom', type: INT, op: 'if_setmodelangle', default: 0 },
            xAngle: { field: 'modelxan', type: INT, op: 'if_setmodelangle', default: 0 },
            yAngle: { field: 'modelyan', type: INT, op: 'if_setmodelangle', default: 0 },
            zAngle: { field: 'modelzan', type: INT, op: 'if_setmodelangle', default: 0 },
            xOffset: { field: 'modelxof', type: INT, op: 'if_setmodelangle', default: 0 },
            yOffset: { field: 'modelyof', type: INT, op: 'if_setmodelangle', default: 0 },
            seq: { field: 'modelanim', type: INT, op: 'if_setmodelanim', default: -1 },
            orthographic: { field: 'modelortho', type: BOOL, op: 'if_setmodelorthog', default: false },
        },
    },
    Line: {
        type: IF_TYPE.line,
        props: {
            ...COMMON,
            color: { field: 'colour', type: INT, op: 'if_setcolour', default: 0 },
            lineWidth: { field: 'linewidth', type: INT, op: 'if_setlinewid', default: 1 },
            lineDirection: { field: 'linedirection', type: BOOL, op: 'if_setlinedirection', default: false },
        },
    },
};

/*
 * No <Inv>. The inventory component's contents are not a component field in IF3 —
 * they are set with if_setobject/cc_setobject from a script — so an element with an
 * `inv` prop would be authoring a field the format does not have. It belongs with
 * the dynamic-child work, not here.
 */

/**
 * Event props, and the cache hook each settles into.
 *
 * `field` is the .if key; `args` is what the hook passes the generated script, using
 * the sentinels in src/cs2vm2/cs2vm2.h. A handler that takes no parameters is bound
 * with no arguments at all, which is the shape that never meets the compiler's
 * hook-argument inference gap (EXCEPTIONS.md G3).
 */
export const EVENTS = {
    onOp: { field: 'onop', params: [{ name: 'op', type: INT, sentinel: -2147483644 }] },
    onClick: { field: 'onclick', params: [] },
    onClickRepeat: { field: 'onclickrepeat', params: [] },
    onMouseOver: { field: 'onmouseover', params: [] },
    onMouseLeave: { field: 'onmouseleave', params: [] },
    onMouseRepeat: { field: 'onmouserepeat', params: [] },
    onHold: { field: 'onhold', params: [] },
    onRelease: { field: 'onrelease', params: [] },
    onDrag: { field: 'ondrag', params: [] },
    onDragComplete: { field: 'ondragcomplete', params: [] },
    onScrollWheel: { field: 'onscrollwheel', params: [] },
    onTargetEnter: { field: 'ontargetenter', params: [] },
    onTargetLeave: { field: 'ontargetleave', params: [] },
    onTimer: { field: 'ontimer', params: [] },
    onLoad: { field: 'onload', params: [] },
};

/**
 * The hook a state kind re-runs a script through, and the trigger list it joins.
 *
 * Only these four. The component struct has varc and varcstr transmit hooks, but
 * cachepack's .if grammar has no key for them (cp_decode.c writes and reads varp,
 * inv and stat triggers and nothing else), so a varc cannot be made to re-run a
 * script by authoring alone. That is why local state takes the other route: a
 * handler that writes a varc also carries the updates that depend on it, which
 * needs no transmit at all. See ir.js `planUpdates`.
 *
 * A varbit's trigger is its *varp*, not the varbit id — the transmit is sent for
 * the containing variable, and the script re-reads the bits.
 */
export const TRANSMIT = {
    varp: { hook: 'onvarptransmit', triggers: 'varptriggers' },
    varbit: { hook: 'onvarptransmit', triggers: 'varptriggers' },
    stat: { hook: 'onstattransmit', triggers: 'stattriggers' },
    inv: { hook: 'oninvtransmit', triggers: 'invtriggers' },
};
