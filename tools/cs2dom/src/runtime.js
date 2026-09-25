/*
 * The module a .tsx file imports: elements, hooks, and the handler vocabulary.
 *
 * All of it runs at compile time. `useVarp` does not read a variable and
 * `setEnergy(0)` does not write one — the first returns a symbolic leaf and the
 * second records an action. What a component "returns" is a tree of plain objects,
 * and the compiler turns that tree into cache records. Nothing in this file exists
 * at runtime, which is the whole point: there is no reconciler in the client.
 *
 * Rendering happens once, so the rules React needs for repeated rendering do not
 * apply. Hooks may be called anywhere, in any order, including inside a loop.
 */

import { ELEMENTS, EVENTS } from './components.js';
import { isExpr, stateRef, call, INT, STRING, Cs2domExprError } from './expr.js';

/* ---- the render pass ----------------------------------------------------- */

/**
 * State declared during the current render, and the varc pool `useState` draws
 * from. Set by the compiler around a render (see loader.js `renderModule`).
 */
let session = null;

export function beginRender(context) {
    session = { context, states: [], varcCursor: context.varcPool ? context.varcPool[0] : null };
    return session;
}

export function endRender() {
    const finished = session;
    session = null;
    return finished;
}

function requireSession(what) {
    if( !session )
        throw new Cs2domExprError(`${what} can only be called while a component is rendering`);
    return session;
}

/* ---- elements ------------------------------------------------------------ */

/*
 * The elements, as values.
 *
 * JSX treats a capitalised tag as a reference to a binding rather than as a name,
 * so `<Layer>` compiles to `__jsx(Layer, ...)` and `Layer` has to be something the
 * module exports. Each is a tag carrying its own name, which is also what makes an
 * element usable in a `.d.ts` — the editor can type `Layer` as a real import.
 */
function element(kind) {
    return { __cs2dom: 'element', kind };
}

export const Layer = element('Layer');
export const Rect = element('Rect');
export const Text = element('Text');
export const Graphic = element('Graphic');
export const Model = element('Model');
export const Line = element('Line');

/** The JSX factory. src/transform.js points TypeScript's JSX emit at this. */
export function jsx(tag, props, ...children) {
    const flat = flatten(children);

    if( typeof tag === 'function' )
        return tag({ ...(props || {}), children: flat.length ? flat : undefined });

    const kind = tag && tag.__cs2dom === 'element' ? tag.kind : tag;
    const element = ELEMENTS[kind];
    if( !element )
        throw new Cs2domExprError(
            `<${kind}> is not a component; the elements are ${Object.keys(ELEMENTS).join(', ')}`);

    const { children: propChildren, ...rest } = props || {};
    const kids = flat.length ? flat : flatten([propChildren]);

    if( !element.children && kids.some((c) => isNode(c)) )
        throw new Cs2domExprError(`<${kind}> cannot contain other components`);

    return {
        __cs2dom: 'node',
        kind,
        type: element.type,
        props: rest,
        /* A <Text> holding one value is spelling the `text` prop; anything else is
         * a child component. Keeping both shapes means the natural JSX reads right. */
        text: !element.children ? textOf(kids) : undefined,
        children: element.children ? kids.filter(isNode) : [],
    };
}

/** The fragment factory: a group with no component of its own. */
export function fragment(props, ...children) {
    return flatten([...(props?.children ? [props.children] : []), ...children]);
}

export function isNode(v) {
    return !!v && typeof v === 'object' && v.__cs2dom === 'node';
}

function flatten(children) {
    const out = [];
    for( const child of children ) {
        if( child === null || child === undefined || child === false || child === true )
            continue;
        if( Array.isArray(child) ) out.push(...flatten(child));
        else out.push(child);
    }
    return out;
}

function textOf(kids) {
    const values = kids.filter((k) => !isNode(k));
    if( values.length === 0 ) return undefined;
    if( values.length === 1 ) return values[0];
    throw new Cs2domExprError(
        'a text component takes one value; join them with a template literal instead');
}

/* ---- state --------------------------------------------------------------- */

function declare(state) {
    requireSession(`use${state.kind}`).states.push(state);
    return state;
}

/** A server variable. Its transmit re-runs whatever reads it. */
export function useVarp(id) {
    const source = { kind: 'varp', id, trigger: id };
    declare(source);
    return stateRef(source, INT);
}

/**
 * A packed field of a server variable.
 *
 * The transmit that re-runs the reader is the *containing varp's*, so the varp has
 * to be known. cache.gen.ts carries the mapping for every varbit the cache defines;
 * pass `{ varp }` for one it does not.
 */
export function useVarbit(id, options) {
    const varp = options?.varp ?? session?.context.varbitVarp?.[id];
    if( varp === undefined )
        throw new Cs2domExprError(
            `varbit ${id} is not in cache.gen.ts, so its varp is unknown — ` +
            `pass useVarbit(${id}, { varp: <id> })`);
    const source = { kind: 'varbit', id, trigger: varp };
    declare(source);
    return stateRef(source, INT);
}

/** A skill level. `stat` is an id; cache.gen.ts names them. */
export function useStat(id) {
    const source = { kind: 'stat', id, trigger: id };
    declare(source);
    return stateRef(source, INT);
}

/** How many of one item an inventory holds. */
export function useInvCount(inv, obj) {
    const source = { kind: 'inv', id: inv, trigger: inv };
    declare(source);
    return call('inv_getnum', [inv, obj], INT);
}

/**
 * Client-side state, backed by a varc.
 *
 * Returns the value and a setter. The setter is not a function that runs later —
 * calling it inside a handler records a write, and the compiler follows that write
 * to every component whose props read the same varc, appending their updates to the
 * same script. That is why local state needs no transmit hook: nothing has to
 * notice the change, because the writer already knows who cared.
 */
export function useState(initial, options) {
    const active = requireSession('useState');
    const type = typeof initial === 'string' ? STRING : INT;
    const id = options?.varc ?? allocateVarc(active, type);
    const source = { kind: type === STRING ? 'varcstr' : 'varc', id, trigger: null, initial };
    declare(source);
    const value = stateRef(source, type);
    const set = (next) => ({ __cs2dom: 'action', action: 'setState', source, value: next });
    return [value, set];
}

function allocateVarc(active, type) {
    const pool = active.context.varcPool;
    if( !pool )
        throw new Cs2domExprError(
            'useState needs a varc pool; set "varcPool": [first, last] in cs2dom.json, ' +
            'or pass useState(value, { varc: <id> })');
    const id = active.varcCursor++;
    if( id > pool[1] )
        throw new Cs2domExprError(`the varc pool ${pool[0]}..${pool[1]} is exhausted`);
    return id;
}

/* ---- handler actions ----------------------------------------------------- */

/**
 * What a handler may do. Each returns a recorded action rather than performing
 * one; a handler's return value is the action or the list of them.
 */
export const actions = {
    /** Show or hide a component. `ref` is another element's `ref` prop value. */
    hide: (ref, hidden = true) => ({ __cs2dom: 'action', action: 'apply', ref, prop: 'hidden', value: hidden }),
    show: (ref) => ({ __cs2dom: 'action', action: 'apply', ref, prop: 'hidden', value: false }),
    /** Set any prop the element type supports at runtime. */
    set: (ref, prop, value) => ({ __cs2dom: 'action', action: 'apply', ref, prop, value }),
    /** Send the click to the server, the way an ordinary interface button does. */
    button: (op = 1) => ({ __cs2dom: 'action', action: 'button', op }),
    /** Call a script that already exists in the cache, by id. */
    runScript: (id, args = []) => ({ __cs2dom: 'action', action: 'runScript', id, args }),
};

/* ---- the CS2 command surface exposed to expressions ---------------------- */

export const cs2 = {
    toString: (value) => call('tostring', [value], STRING),
    min: (a, b) => call('min', [a, b], INT),
    max: (a, b) => call('max', [a, b], INT),
    /** Multiply then divide in one step, the way CS2's own scaling does. */
    scale: (value, numerator, denominator) => call('scale', [value, numerator, denominator], INT),
    /** enum(<int|string> key, <int|string> value, id, key) */
    enumLookup: (keyType, valueType, id, key, ret = INT) =>
        call('enum', [keyType, valueType, id, key], ret),
    clientClock: () => call('clientclock', [], INT),
};

export const events = Object.keys(EVENTS);

export { isExpr };
