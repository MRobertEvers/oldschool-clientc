/*
 * Lowering: a rendered tree becomes components, scripts and hooks.
 *
 * This is where the compiler decides what is a cache record and what is code. A
 * prop holding a plain value is a field in the .if and costs nothing at runtime; a
 * prop holding an expression cannot be a field, so it becomes a statement in a
 * generated script and the state it reads becomes the hook that re-runs it. The
 * split is the entire reactivity model — there is no diffing, because the compiler
 * already knows which props can move and what moves them.
 *
 * Two update paths, because the cache offers only one of them:
 *
 *   - varp, varbit, stat and inv have transmit hooks the .if grammar can author, so
 *     a component that reads them carries its own update script and its own trigger
 *     list, and the client's transmit pump re-runs it.
 *   - a varc has no authorable transmit (cachepack's interface grammar has no key
 *     for one), so local state updates travel the other way: the handler that
 *     writes the varc also carries the updates of everything that reads it. The
 *     writer knows who cared, so nothing has to notice.
 *
 * Both paths end in the same statements — see `applyGroupsFor`, used by both.
 */

import { ELEMENTS, EVENTS, TRANSMIT } from './components.js';
import { OPS } from './ops.js';
import { isExpr, walk, typeOf, INT, STRING, BOOL } from './expr.js';

export class Cs2domError extends Error {
    constructor(message, where) {
        super(where ? `${where}: ${message}` : message);
    }
}

/**
 * Lower one rendered interface.
 *
 * `interfaceId` and `scriptId(name)` come from the id ledger, because an id is a
 * property of the content tree rather than of this render — see ledger.js.
 */
export function lower({ tree, states, name, interfaceId, scriptId }) {
    const components = [];
    const byId = new Map();

    collect(tree, null, components, byId, name);

    for( const component of components )
        classifyProps(component, name);

    const scripts = [];
    planUpdates({ components, byId, name, interfaceId, scriptId, scripts });
    planHandlers({ components, byId, name, interfaceId, scriptId, scripts, states });

    return { name, interfaceId, components, scripts, states };
}

/* ---- tree -> components -------------------------------------------------- */

function collect(node, parent, components, byId, where) {
    const element = ELEMENTS[node.kind];
    const fileId = components.length;

    const blockName = uniqueName(node.props.id || defaultName(node.kind, fileId), byId, where);
    const component = {
        fileId,
        name: blockName,
        kind: node.kind,
        type: node.type,
        layer: parent ? parent.fileId : null,
        props: { ...node.props },
        static: {},
        dynamic: [],
        ops: [],
        events: {},
        hooks: {},
        triggers: {},
        dependencies: [],
    };
    delete component.props.id;

    /* <Text>value</Text> is the text prop written the way JSX wants to write it. */
    if( node.text !== undefined ) {
        if( component.props.text !== undefined )
            throw new Cs2domError(
                `<${node.kind} id="${blockName}"> has both a text prop and text children`, where);
        component.props.text = node.text;
    }

    components.push(component);
    byId.set(blockName, component);

    for( const child of node.children )
        collect(child, component, components, byId, where);

    return component;
}

function defaultName(kind, fileId) {
    return fileId === 0 ? 'root' : `${kind.toLowerCase()}${fileId}`;
}

function uniqueName(name, byId, where) {
    if( !/^[a-z_][a-z0-9_]*$/i.test(name) )
        throw new Cs2domError(`'${name}' is not a usable component name (letters, digits, _)`, where);
    if( byId.has(name) )
        throw new Cs2domError(`two components are called '${name}'`, where);
    return name;
}

/* ---- props -> static fields and dynamic bindings ------------------------- */

function classifyProps(component, where) {
    const element = ELEMENTS[component.kind];
    const at = `<${component.kind} id="${component.name}">`;

    for( const [prop, value] of Object.entries(component.props) ) {
        if( value === undefined ) continue;

        if( prop === 'ops' ) {
            component.ops = normalizeOps(value, `${where}: ${at}`);
            continue;
        }
        if( prop in EVENTS ) {
            if( typeof value !== 'function' )
                throw new Cs2domError(`${at} ${prop} must be a function`, where);
            component.events[prop] = value;
            continue;
        }
        if( prop === 'children' ) continue;

        const schema = element.props[prop];
        if( !schema )
            throw new Cs2domError(
                `${at} has no prop '${prop}'; it takes ${Object.keys(element.props).join(', ')}`,
                where);

        if( isExpr(value) ) {
            if( !schema.op )
                throw new Cs2domError(
                    `${at} '${prop}' can only be given a fixed value — the cache has no ` +
                    `command that changes it at runtime`, where);
            if( typeOf(value) !== schema.type )
                throw new Cs2domError(
                    `${at} '${prop}' is ${schema.type}, but the expression is ${typeOf(value)}`,
                    where);
            component.dynamic.push({ prop, expr: value, schema });
            /* The static side keeps the default so a grouped command has something
             * to send for the props that did not move. */
            component.static[prop] = schema.default;
            continue;
        }

        component.static[prop] = coerce(value, schema, at, prop, where);
    }

    component.dependencies = dependenciesOf(component.dynamic.map((d) => d.expr));
}

function coerce(value, schema, at, prop, where) {
    if( schema.enum && typeof value === 'string' ) {
        if( !(value in schema.enum) )
            throw new Cs2domError(
                `${at} '${prop}' does not take "${value}"; it takes ` +
                `${Object.keys(schema.enum).join(', ')}`, where);
        return schema.enum[value];
    }
    const actual = typeof value === 'number' ? INT
        : typeof value === 'string' ? STRING
        : typeof value === 'boolean' ? BOOL : typeof value;
    if( actual !== schema.type )
        throw new Cs2domError(`${at} '${prop}' is ${schema.type}, got ${actual}`, where);
    if( schema.type === INT && !Number.isInteger(value) )
        throw new Cs2domError(`${at} '${prop}' must be a whole number`, where);
    return value;
}

function normalizeOps(value, at) {
    if( !Array.isArray(value) )
        throw new Cs2domError(`${at} 'ops' must be an array of [index, "text"] pairs`);
    return value.map((entry) => {
        const [index, text] = Array.isArray(entry) ? entry : [null, entry];
        if( typeof text !== 'string' )
            throw new Cs2domError(`${at} an op is a string`);
        if( index !== null && (!Number.isInteger(index) || index < 1 || index > 10) )
            throw new Cs2domError(`${at} op index ${index} is outside 1..10`);
        return { index, text };
    }).map((op, i) => ({ index: op.index ?? i + 1, text: op.text }));
}

/** Every distinct state leaf an expression set reads, in a stable order. */
export function dependenciesOf(expressions) {
    const seen = new Map();
    for( const expression of expressions ) {
        walk(expression, (node) => {
            if( node.kind === 'state' ) {
                const key = `${node.source.kind}:${node.source.id}`;
                if( !seen.has(key) ) seen.set(key, node.source);
            }
        });
    }
    return [...seen.values()];
}

/* ---- statements ---------------------------------------------------------- */

/**
 * The commands that put a component's current prop values on screen.
 *
 * Grouped by command, because several props share one: a component whose `y` is
 * bound to state still sends x, both modes and y together, since if_setposition has
 * no single-field form. `only` limits the work to the props that can have changed —
 * a handler that wrote one varc has no reason to re-send a font.
 */
export function applyGroupsFor(component, only = null) {
    const groups = new Map();

    for( const binding of component.dynamic ) {
        if( only && !only.has(binding.prop) ) continue;
        const op = binding.schema.op;
        if( !groups.has(op) ) groups.set(op, new Map());
        groups.get(op).set(binding.prop, binding.expr);
    }

    const out = [];
    for( const [op, changed] of groups ) {
        const signature = OPS[op];
        if( !signature || !signature.args )
            throw new Cs2domError(`no argument order recorded for ${op}`);
        const args = signature.args.map((prop) => {
            if( changed.has(prop) ) return changed.get(prop);
            const value = component.static[prop];
            return value === undefined ? defaultFor(component, prop) : value;
        });
        out.push({ op, args, target: component });
    }
    return out;
}

function defaultFor(component, prop) {
    const schema = ELEMENTS[component.kind].props[prop];
    return schema ? schema.default : 0;
}

/* ---- update scripts ------------------------------------------------------ */

function planUpdates({ components, name, interfaceId, scriptId, scripts }) {
    for( const component of components ) {
        if( component.dynamic.length === 0 ) continue;

        const scriptName = `cs2dom_${name}_${component.name}`;
        const script = {
            name: scriptName,
            id: scriptId(scriptName),
            kind: 'update',
            component,
            params: [],
            statements: applyGroupsFor(component),
        };
        scripts.push(script);

        /* First paint. Without this the component shows its authored fields until
         * something happens to move it, which for most bindings is never. */
        component.hooks.onload = { script, args: [] };

        for( const source of component.dependencies ) {
            const transmit = TRANSMIT[source.kind];
            if( !transmit ) continue;   /* varc: updated by its writer, not by a hook */
            component.hooks[transmit.hook] = { script, args: [] };
            const triggers = component.triggers[transmit.triggers] || [];
            if( !triggers.includes(source.trigger) ) triggers.push(source.trigger);
            component.triggers[transmit.triggers] = triggers;
        }
    }
}

/* ---- handlers ------------------------------------------------------------ */

function planHandlers({ components, byId, name, interfaceId, scriptId, scripts }) {
    for( const component of components ) {
        for( const [event, handler] of Object.entries(component.events) ) {
            const definition = EVENTS[event];
            const params = definition.params.map((p, i) => ({ ...p, local: `$${p.type}${i}` }));

            const produced = handler(...params.map((p) => paramExpression(p)));
            const actions = flattenActions(produced, `${name}: ${component.name}.${event}`);

            const statements = [];
            for( const action of actions )
                statements.push(...lowerAction(action, byId, components, `${name}: ${component.name}.${event}`));

            const scriptName = `cs2dom_${name}_${component.name}_${event.toLowerCase()}`;
            const script = {
                name: scriptName,
                id: scriptId(scriptName),
                kind: 'handler',
                component,
                params,
                statements,
            };
            scripts.push(script);

            const hookKey = definition.field;
            if( component.hooks[hookKey] ) {
                /* An authored onLoad and a generated update want the same hook.
                 * The update runs first — the handler is about what the author wants
                 * *after* the component is showing its state. */
                component.hooks[hookKey].script.statements.push(...statements);
            } else {
                component.hooks[hookKey] = { script, args: params.map((p) => p.sentinel) };
            }
        }
    }
}

function paramExpression(param) {
    return { __cs2dom: 'expr', id: -1, kind: 'local', type: param.type, name: param.local };
}

function flattenActions(produced, where) {
    if( produced === undefined || produced === null ) return [];
    const list = Array.isArray(produced) ? produced.flat(Infinity) : [produced];
    for( const action of list ) {
        if( !action || action.__cs2dom !== 'action' )
            throw new Cs2domError(
                `a handler returns actions from the 'actions' module (hide, set, button, ` +
                `runScript) or a state setter — got ${describe(action)}`, where);
    }
    return list;
}

function describe(value) {
    if( value === undefined ) return 'undefined';
    if( value && value.__cs2dom === 'node' ) return 'a component';
    return typeof value;
}

/**
 * One recorded action becomes statements.
 *
 * A state write is the one that fans out: after the varc is set, every component
 * whose props read it needs its update statements appended, because a varc has no
 * transmit hook to do it (see the file header).
 */
function lowerAction(action, byId, components, where) {
    switch( action.action ) {
        case 'setState': {
            const statements = [{ op: 'assign', source: action.source, value: action.value }];
            for( const component of components ) {
                const reads = component.dependencies.some(
                    (d) => d.kind === action.source.kind && d.id === action.source.id);
                if( reads ) statements.push(...applyGroupsFor(component));
            }
            return statements;
        }
        case 'apply': {
            const target = byId.get(action.ref);
            if( !target )
                throw new Cs2domError(`no component with id "${action.ref}"`, where);
            const schema = ELEMENTS[target.kind].props[action.prop];
            if( !schema )
                throw new Cs2domError(
                    `<${target.kind} id="${target.name}"> has no prop '${action.prop}'`, where);
            if( !schema.op )
                throw new Cs2domError(
                    `'${action.prop}' cannot be changed at runtime`, where);
            const signature = OPS[schema.op];
            const args = signature.args.map((prop) =>
                prop === action.prop ? action.value
                    : (target.static[prop] !== undefined ? target.static[prop] : defaultFor(target, prop)));
            return [{ op: schema.op, args, target }];
        }
        case 'button':
            return [{ op: 'if_button', args: [action.op], target: null }];
        case 'runScript':
            return [{ op: 'runscript', args: action.args, scriptId: action.id, target: null }];
        default:
            throw new Cs2domError(`unknown action '${action.action}'`, where);
    }
}
