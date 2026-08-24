/*
 * The production client's preview-tree sidecar.
 *
 * The C snapshot is deliberately a storage-oriented document: node indices and
 * sibling links are the runtime UITree's own identities. This module validates
 * its hard bounds, reconstructs link/paint order without trusting recursion,
 * and optionally decorates static ids with names from the imported IR. It does
 * not simulate layout or scripts; every box and flag comes from the C client.
 */

const SCHEMA = 1;
const MAX_BYTES = 32 * 1024 * 1024;
const MAX_NODES = 8192;
const MAX_HOOKS = 64;

/** Parse and validate one UITreeSnapshot JSON document. */
export function parseNativeTree(input, options = {}) {
    const maxBytes = boundedOption(options.maxBytes, MAX_BYTES, 'maxBytes');
    const maxNodes = boundedOption(options.maxNodes, MAX_NODES, 'maxNodes');
    let document;
    if( Buffer.isBuffer(input) || input instanceof Uint8Array ) {
        if( input.byteLength > maxBytes )
            throw new Error(`native tree snapshot exceeds ${maxBytes} bytes`);
        document = parseJson(Buffer.from(input).toString('utf8'));
    } else if( typeof input === 'string' ) {
        if( Buffer.byteLength(input) > maxBytes )
            throw new Error(`native tree snapshot exceeds ${maxBytes} bytes`);
        document = parseJson(input);
    } else if( input && typeof input === 'object' ) {
        document = input;
    } else {
        throw new Error('native tree snapshot must be JSON text, bytes, or an object');
    }

    requireObject(document, 'snapshot');
    if( document.schema !== SCHEMA )
        throw new Error(`unsupported native tree schema '${document.schema}'`);
    const viewport = requireRectSize(document.viewport, 'viewport');
    if( viewport.width < 1 || viewport.height < 1 || viewport.width > 4096 || viewport.height > 4096 )
        throw new Error(`invalid native tree viewport ${viewport.width}x${viewport.height}`);
    if( !Array.isArray(document.nodes) ) throw new Error('native tree nodes must be an array');
    if( document.nodes.length > maxNodes )
        throw new Error(`native tree snapshot exceeds ${maxNodes} nodes`);

    const nodes = document.nodes.map((node, index) => normalizeNode(node, index));
    const byNode = new Map();
    for( const node of nodes ) {
        if( byNode.has(node.node) ) throw new Error(`duplicate native tree node ${node.node}`);
        byNode.set(node.node, node);
    }
    if( !document.truncated ) validateLinks(nodes, byNode);
    const tree = linkedOrder(nodes, byNode, integer(document.root, 'root'));

    return {
        schema: SCHEMA,
        interfaceId: integer(document.interface, 'interface'),
        viewport,
        root: integer(document.root, 'root'),
        componentCount: nonnegative(document.component_count, 'component_count'),
        liveCount: nonnegative(document.live_count, 'live_count'),
        exportedCount: nonnegative(document.exported_count, 'exported_count'),
        emitCount: nonnegative(document.emit_count, 'emit_count'),
        truncated: Boolean(document.truncated),
        nodes,
        tree,
        byNode,
    };
}

/**
 * Adapt a validated snapshot to the box shape the existing inspector consumes.
 * `ir` supplies names only; geometry, hierarchy, visibility and hooks remain
 * native facts. The returned viewport must size the native stage.
 */
export function nativeTreeInspector(snapshot, ir = null) {
    const parsed = snapshot?.byNode instanceof Map ? snapshot : parseNativeTree(snapshot);
    const staticNames = new Map();
    if( ir?.components ) {
        for( const component of ir.components ) {
            if( !Number.isInteger(component.fileId) ) continue;
            staticNames.set(ir.interfaceId * 65536 + component.fileId, component.name);
        }
    }

    const names = new Map();
    const keys = new Map();
    for( const node of parsed.tree ) {
        const parentName = names.get(node.parent);
        const staticName = !node.dynamic ? staticNames.get(node.uid) : null;
        const fallback = node.uid >= 0
            ? `interface_${node.group}:${node.file}` : `${node.kind}_${node.node}`;
        const name = staticName || (node.dynamic && parentName
            ? `${parentName}[${node.childIndex}]` : fallback);
        names.set(node.node, name);
        keys.set(node.node, staticName ? node.file : `native:${node.node}`);
    }

    const viewportClip = {
        left: 0,
        top: 0,
        right: parsed.viewport.width,
        bottom: parsed.viewport.height,
    };
    const boxes = parsed.tree.map((node) => {
        const draw = node.draw;
        const drawClip = draw?.clip;
        const hookNames = node.hooks.map((hook) => hook.name);
        return {
            name: names.get(node.node),
            kind: inspectorKind(node.kind),
            type: node.widgetType,
            fileId: keys.get(node.node),
            layer: node.parent >= 0 ? keys.get(node.parent) ?? null : null,
            x: draw?.x ?? node.box.x,
            y: draw?.y ?? node.box.y,
            w: draw?.width ?? node.box.width,
            h: draw?.height ?? node.box.height,
            absX: node.box.x,
            absY: node.box.y,
            relX: node.raw.x,
            relY: node.raw.y,
            depth: node.depth,
            effectiveHidden: node.visibility.effectiveHidden,
            culled: node.visibility.culled,
            emitted: node.visibility.walked && node.visibility.displayable,
            clip: drawClip ? {
                left: drawClip.x,
                top: drawClip.y,
                right: drawClip.x + drawClip.width,
                bottom: drawClip.y + drawClip.height,
            } : { ...viewportClip },
            props: {
                x: node.raw.x,
                y: node.raw.y,
                width: node.raw.width,
                height: node.raw.height,
                xMode: node.raw.xMode,
                yMode: node.raw.yMode,
                widthMode: node.raw.widthMode,
                heightMode: node.raw.heightMode,
                hidden: node.visibility.ownHidden,
                transparency: node.transparency,
                scrollX: node.scroll.x,
                scrollY: node.scroll.y,
                scrollWidth: node.scroll.width,
                scrollHeight: node.scroll.height,
            },
            dynamic: [],
            ops: [],
            events: hookNames,
            hooks: hookNames,
            native: {
                node: node.node,
                uid: node.uid,
                dynamic: node.dynamic,
                childIndex: node.childIndex,
                clientCode: node.clientCode,
                itemId: node.itemId,
                itemCount: node.itemCount,
                drawCount: draw?.count ?? 0,
                visibility: node.visibility,
                hooks: node.hooks,
            },
        };
    });
    return { viewport: { ...parsed.viewport }, boxes, snapshot: parsed };
}

function normalizeNode(value, index) {
    requireObject(value, `nodes[${index}]`);
    const node = integer(value.node, `nodes[${index}].node`);
    const hooks = value.hooks;
    if( !Array.isArray(hooks) || hooks.length > MAX_HOOKS )
        throw new Error(`nodes[${index}].hooks must contain at most ${MAX_HOOKS} entries`);
    return {
        node,
        uid: integer(value.uid, `nodes[${index}].uid`),
        group: integer(value.group, `nodes[${index}].group`),
        file: integer(value.file, `nodes[${index}].file`),
        parent: integer(value.parent, `nodes[${index}].parent`),
        firstChild: integer(value.first_child, `nodes[${index}].first_child`),
        nextSibling: integer(value.next_sibling, `nodes[${index}].next_sibling`),
        depth: nonnegative(value.depth, `nodes[${index}].depth`),
        dynamic: Boolean(value.dynamic),
        childIndex: integer(value.child_index, `nodes[${index}].child_index`),
        kind: requireString(value.kind, `nodes[${index}].kind`),
        type: integer(value.type, `nodes[${index}].type`),
        widgetType: integer(value.widget_type, `nodes[${index}].widget_type`),
        if3: Boolean(value.if3),
        transparency: integer(value.transparency, `nodes[${index}].transparency`),
        clientCode: integer(value.client_code, `nodes[${index}].client_code`),
        itemId: integer(value.item_id, `nodes[${index}].item_id`),
        itemCount: integer(value.item_count, `nodes[${index}].item_count`),
        raw: normalizeRaw(value.raw, `nodes[${index}].raw`),
        box: normalizeBox(value.box, `nodes[${index}].box`),
        scroll: normalizeScroll(value.scroll, `nodes[${index}].scroll`),
        visibility: normalizeVisibility(value.visibility, `nodes[${index}].visibility`),
        hooks: hooks.map((hook, hookIndex) => normalizeHook(hook, `nodes[${index}].hooks[${hookIndex}]`)),
        draw: value.draw === null ? null : normalizeDraw(value.draw, `nodes[${index}].draw`),
    };
}

function normalizeRaw(value, where) {
    requireObject(value, where);
    return {
        x: integer(value.x, `${where}.x`),
        y: integer(value.y, `${where}.y`),
        width: integer(value.width, `${where}.width`),
        height: integer(value.height, `${where}.height`),
        xMode: integer(value.x_mode, `${where}.x_mode`),
        yMode: integer(value.y_mode, `${where}.y_mode`),
        widthMode: integer(value.width_mode, `${where}.width_mode`),
        heightMode: integer(value.height_mode, `${where}.height_mode`),
    };
}

function normalizeBox(value, where) {
    requireObject(value, where);
    return {
        x: integer(value.x, `${where}.x`),
        y: integer(value.y, `${where}.y`),
        width: integer(value.width, `${where}.width`),
        height: integer(value.height, `${where}.height`),
        resolved: Boolean(value.resolved),
    };
}

function normalizeScroll(value, where) {
    requireObject(value, where);
    return {
        x: integer(value.x, `${where}.x`),
        y: integer(value.y, `${where}.y`),
        width: integer(value.width, `${where}.width`),
        height: integer(value.height, `${where}.height`),
    };
}

function normalizeVisibility(value, where) {
    requireObject(value, where);
    return {
        ownHidden: Boolean(value.own_hidden),
        frameHidden: Boolean(value.frame_hidden),
        replacementHidden: Boolean(value.replacement_hidden),
        effectiveHidden: Boolean(value.effective_hidden),
        culled: Boolean(value.culled),
        walked: Boolean(value.walked),
        displayable: Boolean(value.displayable),
    };
}

function normalizeHook(value, where) {
    requireObject(value, where);
    const name = requireString(value.name, `${where}.name`);
    if( !/^[a-z][a-z0-9_]*$/.test(name) ) throw new Error(`invalid hook name '${name}'`);
    return {
        name,
        script: integer(value.script, `${where}.script`),
        argc: nonnegative(value.argc, `${where}.argc`),
        stringArgc: nonnegative(value.string_argc, `${where}.string_argc`),
    };
}

function normalizeDraw(value, where) {
    requireObject(value, where);
    const clip = requireRectSize(value.clip, `${where}.clip`);
    return {
        count: nonnegative(value.count, `${where}.count`),
        kind: integer(value.kind, `${where}.kind`),
        x: integer(value.x, `${where}.x`),
        y: integer(value.y, `${where}.y`),
        width: integer(value.width, `${where}.width`),
        height: integer(value.height, `${where}.height`),
        clip: {
            x: integer(value.clip.x, `${where}.clip.x`),
            y: integer(value.clip.y, `${where}.clip.y`),
            ...clip,
        },
        scrollX: integer(value.scroll_x, `${where}.scroll_x`),
        scrollY: integer(value.scroll_y, `${where}.scroll_y`),
    };
}

function validateLinks(nodes, byNode) {
    for( const node of nodes ) {
        for( const [name, target] of [
            ['parent', node.parent], ['first_child', node.firstChild], ['next_sibling', node.nextSibling],
        ]) {
            if( target >= 0 && !byNode.has(target) )
                throw new Error(`native tree node ${node.node} has missing ${name} ${target}`);
        }
        if( node.parent === node.node || node.firstChild === node.node || node.nextSibling === node.node )
            throw new Error(`native tree node ${node.node} links to itself`);
    }
}

function linkedOrder(nodes, byNode, root) {
    const ordered = [];
    const visited = new Set();
    const visit = (start, depth) => {
        const stack = [{ node: start, depth }];
        while( stack.length ) {
            const item = stack.pop();
            const node = byNode.get(item.node);
            if( !node || visited.has(node.node) ) continue;
            visited.add(node.node);
            ordered.push({ ...node, depth: item.depth });

            const children = [];
            const siblings = new Set();
            for( let child = node.firstChild; child >= 0; ) {
                if( siblings.has(child) ) break;
                siblings.add(child);
                const candidate = byNode.get(child);
                if( !candidate ) break;
                children.push(candidate.node);
                child = candidate.nextSibling;
            }
            for( let i = children.length - 1; i >= 0; i-- )
                stack.push({ node: children[i], depth: item.depth + 1 });
        }
    };

    const roots = [];
    const rootSiblings = new Set();
    for( let current = root; current >= 0; ) {
        if( rootSiblings.has(current) ) break;
        rootSiblings.add(current);
        const candidate = byNode.get(current);
        if( !candidate ) break;
        roots.push(current);
        current = candidate.nextSibling;
    }
    for( const node of nodes )
        if( node.parent < 0 && !roots.includes(node.node) ) roots.push(node.node);
    for( const node of roots ) visit(node, 0);
    for( const node of nodes )
        if( !visited.has(node.node) ) visit(node.node, Math.max(0, node.depth));
    return ordered;
}

function inspectorKind(kind) {
    return ({
        layer: 'Layer', inventory: 'Inv', rectangle: 'Rect', text: 'Text',
        graphic: 'Graphic', model: 'Model', inventory_text: 'Text', line: 'Line',
    })[kind] || 'Unknown';
}

function requireRectSize(value, where) {
    requireObject(value, where);
    return {
        width: integer(value.width, `${where}.width`),
        height: integer(value.height, `${where}.height`),
    };
}

function requireObject(value, where) {
    if( !value || typeof value !== 'object' || Array.isArray(value) )
        throw new Error(`${where} must be an object`);
}

function requireString(value, where) {
    if( typeof value !== 'string' || value.length > 80 ) throw new Error(`${where} must be a short string`);
    return value;
}

function integer(value, where) {
    if( !Number.isSafeInteger(value) ) throw new Error(`${where} must be an integer`);
    return value;
}

function nonnegative(value, where) {
    value = integer(value, where);
    if( value < 0 ) throw new Error(`${where} must be non-negative`);
    return value;
}

function boundedOption(value, fallback, name) {
    if( value === undefined ) return fallback;
    if( !Number.isSafeInteger(value) || value < 1 || value > fallback )
        throw new Error(`${name} must be between 1 and ${fallback}`);
    return value;
}

function parseJson(text) {
    try { return JSON.parse(text); }
    catch( error ) { throw new Error(`invalid native tree JSON: ${error.message}`); }
}
