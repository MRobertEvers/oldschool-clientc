/*
 * Where each component lands, and what it shows.
 *
 * The geometry here is a port of the client's own IF3 layout — src/ui/ui_if3_layout.h,
 * `UITree_If3AxisFromPositionMode` and `UITree_If3DimFromParentMode`, formula for
 * formula, including the 14-bit fixed-point proportional modes and the truncating
 * division in the centring mode. A preview that laid components out its own way
 * would be a drawing of a design rather than a picture of the interface, and the
 * off-by-ones it hid would be exactly the ones worth seeing early.
 *
 * What it does not port is drawing: the browser's text is not the cache's bitmap
 * font, and a model is a box with a label. Sprites are real, because the content
 * tree already holds them as bitmaps.
 */

import { resolveProps } from './eval.js';
import { IF_TYPE } from './components.js';

const mulShift14 = (a, b) => Math.trunc((a * b) / 16384);

export function dimFromParentMode(mode, orig, parentDim) {
    switch( mode ) {
        case 0: return orig;
        case 1: return parentDim - orig;
        case 2: return mulShift14(parentDim, orig);
        default: return orig;
    }
}

export function axisFromPositionMode(mode, base, parentOrigin, parentDim, selfDim) {
    switch( mode ) {
        case 0: return parentOrigin + base;
        /* Truncating division, like the client's — not an arithmetic shift. */
        case 1: return parentOrigin + Math.trunc((parentDim - selfDim) / 2) + base;
        case 2: return parentOrigin + parentDim - base - selfDim;
        case 3: return parentOrigin + mulShift14(parentDim, base);
        case 4: return parentOrigin + Math.trunc((parentDim - selfDim) / 2) + mulShift14(parentDim, base);
        case 5: return parentOrigin + parentDim - mulShift14(parentDim, base) - selfDim;
        default: return parentOrigin + base;
    }
}

/**
 * Resolve the whole tree against a state environment.
 *
 * Returns one flat box per component with absolute geometry, in draw order, which
 * is what the page needs and what a snapshot diff against the real client's UITree
 * would compare.
 */
export function layout(ir, state, viewport = { width: 512, height: 334 }, unmodelled = null) {
    const boxes = [];
    const byFileId = new Map();

    for( const component of ir.components ) {
        const props = resolveProps(component, state, unmodelled);
        const parent = component.layer === null ? null : byFileId.get(component.layer);

        const parentBox = parent
            ? { x: parent.x, y: parent.y, w: parent.w, h: parent.h }
            : { x: 0, y: 0, w: viewport.width, h: viewport.height };

        const w = Math.max(0, dimFromParentMode(props.widthMode | 0, props.width | 0, parentBox.w));
        const h = Math.max(0, dimFromParentMode(props.heightMode | 0, props.height | 0, parentBox.h));
        const x = axisFromPositionMode(props.xMode | 0, props.x | 0, parentBox.x, parentBox.w, w);
        const y = axisFromPositionMode(props.yMode | 0, props.y | 0, parentBox.y, parentBox.h, h);

        const box = {
            name: component.name,
            kind: component.kind,
            type: component.type,
            fileId: component.fileId,
            layer: component.layer,
            x, y, w, h,
            props,
            dynamic: component.dynamic.map((d) => d.prop),
            ops: component.ops,
            events: Object.keys(component.events),
            hooks: Object.keys(component.hooks),
        };
        boxes.push(box);
        byFileId.set(component.fileId, box);
    }

    return boxes;
}

/** What the page draws a box as; the type decides, the way the client's emit does. */
export function boxRole(type) {
    switch( type ) {
        case IF_TYPE.layer: return 'layer';
        case IF_TYPE.rectangle: return 'rect';
        case IF_TYPE.text: return 'text';
        case IF_TYPE.graphic: return 'graphic';
        case IF_TYPE.model: return 'model';
        case IF_TYPE.line: return 'line';
        default: return 'unknown';
    }
}
