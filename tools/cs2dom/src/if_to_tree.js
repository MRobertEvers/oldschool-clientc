/*
 * Baking an `.if` record into the tree the runtime executes against.
 *
 * The C client does this from the packed binary; here it is done from the text
 * the content tree holds, which is the same data one decode earlier. Either
 * way the result must be the same tree, because a parity comparison that
 * started from a different tree would be comparing two things neither of which
 * is wrong.
 *
 * ------------------------------------------------------------------
 * `layer` is a component UID, not a block name
 * ------------------------------------------------------------------
 *
 * A block's parent is stored as `(interface << 16) | fileId`, so the tree is
 * rebuilt through the `.compack` rather than by reading names. A component
 * whose `layer` names a DIFFERENT interface is a mount point, not a child of
 * this one — it hangs where the mount put it, and treating it as a child here
 * would nest an interface inside itself.
 *
 * ------------------------------------------------------------------
 * Order is not incidental
 * ------------------------------------------------------------------
 *
 * Siblings draw in the order the file lists them, so they must be pushed in
 * that order. The `.compack` is an id map and its order says nothing; sorting
 * by file id would reorder every interface whose blocks were not written in id
 * order, and the difference is which widget covers which.
 */

import { parseIf, parseCompack } from './if_record.js';
import { createUITree, WIDGET_TYPE } from './uitree.js';

/*
 * `.if` field -> the tree prop it becomes.
 *
 * Taken from `cp_decode.c`'s `emit_component`, which is the writer, rather
 * than from the shape of the tree's own props. Guessing here is silent: a
 * field spelled `linehei` where the file says `lineheight` is not an error,
 * it is 175 components laying their text out on the default leading, and
 * `spriteangle` for `angle` is a sprite that never rotates.
 *
 * The `active*` family is the hover/selected variant the painter reaches for
 * through `<name>Over`; `overcolour` and `activecolour` are DIFFERENT fields
 * in the file and only the first is the mouse-over one.
 */
const FIELD_PROPS = new Map(Object.entries({
    clientcode: ['clientCode', int],
    x: ['x', int], y: ['y', int],
    width: ['width', int], height: ['height', int],
    widthmode: ['widthMode', int], heightmode: ['heightMode', int],
    xmode: ['xMode', int], ymode: ['yMode', int],
    hidden: ['hidden', flag],
    trans: ['trans', int],
    colour: ['colour', int],
    fill: ['filled', flag],
    alpha: ['alpha', flag],
    tiled: ['tiled', flag],
    outline: ['outline', int],
    graphic: ['sprite', int],
    graphicshadow: ['graphicShadow', int],
    hflip: ['flipH', flag], vflip: ['flipV', flag],
    angle: ['spriteAngle', int],
    scrollwidth: ['scrollWidth', int], scrollheight: ['scrollHeight', int],
    noclickthrough: ['noClickThrough', flag],
    clickmask: ['clickMask', int],
    linewidth: ['lineWidth', int], linedirection: ['lineDirection', flag],
    buttontype: ['buttonType', int],
    linked: ['linkedComponentId', int],
    marginx: ['marginX', int], marginy: ['marginY', int],

    text: ['text', text],
    name: ['name', text],
    targetverb: ['targetVerb', text],
    targettext: ['targetText', text],
    activetext: ['textOver', text],
    option: ['option', text],
    font: ['font', int],
    halign: ['halign', int], valign: ['valign', int],
    lineheight: ['lineHeight', int],
    shadow: ['shadowed', flag],

    model: ['model', int],
    modelzoom: ['modelZoom', int],
    modelxan: ['modelAngleX', int], modelyan: ['modelAngleY', int],
    modelzan: ['modelAngleZ', int],
    modelxof: ['modelOffsetX', int], modelyof: ['modelOffsetY', int],
    modelanim: ['modelAnim', int],
    modelortho: ['modelOrthog', flag],
    modelfixedzoom: ['modelFixedZoom', flag],
    activemodel: ['modelOver', int],
    activeanim: ['modelAnimOver', int],
    activegraphic: ['spriteOver', int],
    activecolour: ['colourActive', int],
    overcolour: ['colourOver', int],
    activeovercolour: ['colourActiveOver', int],

    dragdeadzone: ['dragDeadZone', int],
    dragdeadtime: ['dragDeadTime', int],
    dragrender: ['dragRender', flag],
}));

/*
 * `.if` hook field -> the tree's hook slot.
 *
 * These eighteen are every hook the cache can carry — `cp_decode.c` writes no
 * others — and the spellings are its own. `onvarptransmit`, not
 * `onvartransmit`: the second name is the CS2 command's, and mapping the
 * command's spelling here drops all 143 cache-authored var hooks in silence.
 */
const HOOK_SLOTS = new Map(Object.entries({
    onload: 'onLoad',
    onmouseover: 'onMouseOver',
    onmouseleave: 'onMouseLeave',
    ontargetleave: 'onTargetLeave',
    ontargetenter: 'onTargetEnter',
    onvarptransmit: 'onVarTransmit',
    oninvtransmit: 'onInvTransmit',
    onstattransmit: 'onStatTransmit',
    ontimer: 'onTimer',
    onop: 'onOp',
    onmouserepeat: 'onMouseRepeat',
    onclick: 'onClick',
    onclickrepeat: 'onClickRepeat',
    onrelease: 'onRelease',
    onhold: 'onHold',
    ondrag: 'onDrag',
    ondragcomplete: 'onDragComplete',
    onscrollwheel: 'onScrollWheel',
}));

/** Trigger lists that go with a transmit hook. */
const TRIGGER_FIELDS = new Map(Object.entries({
    varptriggers: 'onVarTransmit',
    invtriggers: 'onInvTransmit',
    stattriggers: 'onStatTransmit',
}));

function int(value) { const n = Number(value); return Number.isFinite(n) ? n : 0; }
function flag(value) { return value === 'yes' || value === 'true' || value === '1'; }
function text(value) { return value; }

/**
 * Bake one interface into a tree.
 *
 * `onLoad` bindings are returned rather than dispatched: the caller owns the
 * driver, and running them here would execute scripts against a tree that is
 * only half built when the first of them fires.
 */
export function bakeInterface({ tree = null, ifText, compackText = '', interfaceId }) {
    const target = tree ?? createUITree();
    const record = parseIf(ifText);
    const compack = parseCompack(compackText);

    /* Two passes. The first makes every node so the second can link a child to
     * a parent that appears LATER in the file — which happens, because block
     * order is draw order and has nothing to do with nesting. */
    const byFileId = new Map();
    const rows = [];

    for( const block of record.blocks )
    {
        const fileId = compack.byName.get(block.name);
        if( fileId === undefined ) continue;
        const componentId = ((interfaceId & 0xffff) << 16) | (fileId & 0xffff);
        const row = {
            block: block.name,
            fileId,
            componentId,
            type: int(record.get(block.name, 'type')),
            layer: record.get(block.name, 'layer'),
            props: readProps(record, block),
            hooks: readHooks(record, block),
        };
        rows.push(row);
        byFileId.set(fileId, row);
    }

    for( const row of rows )
    {
        const parentUid = row.layer === null ? -1 : int(row.layer);
        const parentGroup = parentUid >= 0 ? (parentUid >>> 16) & 0xffff : -1;
        const parentFileId = parentUid >= 0 ? parentUid & 0xffff : -1;
        /* A parent in another interface is a MOUNT: this bake describes one
         * interface, so such a component is a root of it. */
        const parentRow = parentGroup === (interfaceId & 0xffff)
            ? byFileId.get(parentFileId) : null;
        row.parentIndex = parentRow && parentRow !== row ? parentRow : null;
    }

    /*
     * Push parents before children, keeping FILE ORDER among siblings.
     * A child pushed before its parent has nowhere to attach; siblings pushed
     * out of order draw in the wrong sequence, and which widget covers which
     * is the whole difference.
     */
    const pushed = new Set();
    const onLoad = [];

    const push = (row) => {
        if( pushed.has(row) ) return;
        pushed.add(row);
        if( row.parentIndex ) push(row.parentIndex);

        const index = target.push({
            parentIndex: row.parentIndex ? row.parentIndex.index : -1,
            componentId: row.componentId,
            subId: row.fileId,
            type: treeType(row.type),
            props: row.props,
        });
        row.index = index;
        if( row.props.hidden ) target.setHidden(index, true);

        for( const [slot, binding] of Object.entries(row.hooks) )
        {
            if( slot === 'onLoad' ) { onLoad.push({ ...binding, componentId: row.componentId }); continue; }
            target.setHook(index, slot, binding);
        }
    };

    for( const row of rows ) push(row);

    return { tree: target, rows, onLoad, record, compack };
}

function readProps(record, block) {
    const props = {};
    for( const [field, entries] of block.fields )
    {
        const mapping = FIELD_PROPS.get(field);
        if( !mapping || entries.length === 0 ) continue;
        props[mapping[0]] = mapping[1](entries[0].value);
    }
    return props;
}

function readHooks(record, block) {
    const hooks = {};
    const triggers = new Map();

    for( const [field, entries] of block.fields )
    {
        const trigger = TRIGGER_FIELDS.get(field);
        if( !trigger || entries.length === 0 ) continue;
        const ids = entries[0].value.split(',')
            .map((value) => Number(value.trim()))
            .filter((value) => Number.isFinite(value));
        triggers.set(trigger, [...(triggers.get(trigger) ?? []), ...ids]);
    }

    for( const [field, entries] of block.fields )
    {
        const slot = HOOK_SLOTS.get(field);
        if( !slot || entries.length === 0 ) continue;
        const parts = entries[0].value.split(',').map((part) => part.trim());
        const first = /^i:(-?\d+)$/.exec(parts[0] ?? '');
        if( !first ) continue;
        const scriptId = Number(first[1]);
        if( scriptId < 0 ) continue;
        hooks[slot] = {
            scriptId,
            args: parts.slice(1).map(hookArgument),
            triggers: triggers.get(slot) ?? [],
        };
    }
    return hooks;
}

function hookArgument(part) {
    const match = /^([is]):([\s\S]*)$/.exec(part);
    if( !match ) return 0;
    return match[1] === 'i' ? Number(match[2]) : match[2];
}

/**
 * Cache type number -> the tree's widget type.
 *
 * They already agree for everything the painter draws; the mapping exists so
 * a type the tree has no equivalent for becomes a LAYER — a container that
 * draws nothing — rather than a widget that draws the wrong thing.
 */
function treeType(cacheType) {
    switch( cacheType )
    {
    case 0: return WIDGET_TYPE.LAYER;
    case 3: return WIDGET_TYPE.RECTANGLE;
    case 4: return WIDGET_TYPE.TEXT;
    case 5: return WIDGET_TYPE.GRAPHIC;
    case 6: return WIDGET_TYPE.MODEL;
    case 9: return WIDGET_TYPE.LINE;
    default: return WIDGET_TYPE.LAYER;
    }
}

export { FIELD_PROPS, HOOK_SLOTS, treeType };
