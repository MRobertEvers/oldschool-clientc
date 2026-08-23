/*
 * Evaluating the IR against made-up state.
 *
 * The preview needs the value a prop *would* have when the run energy is 43 or the
 * varbit is 1, which means running the same expressions the emitter prints. This is
 * the second reader of the expression tree, and keeping it beside the emitter is
 * deliberate: the two agree about what an expression means or the preview is
 * lying, and a disagreement is visible as a preview that does not match the client.
 *
 * Integer division truncates, the way CS2's does. That is the one arithmetic
 * difference from JavaScript that would otherwise show up as a preview off by one.
 */

import { isExpr, INT, STRING, BOOL } from './expr.js';
import { SLICES, HOST_READS, UNMODELLED } from './host.js';

/** `state` is `{ 'varp:300': 4300, 'varc:1400': 1, 'stat:3': 55 }`. */
export function evaluate(expression, state, unmodelled = null) {
    if( !isExpr(expression) ) return expression;

    switch( expression.kind ) {
        case 'const':
            return expression.value;
        case 'local':
            /* A handler parameter has no value outside a click; zero reads as
             * "nothing happened yet", which is what the first paint shows. */
            return expression.type === STRING ? '' : expression.type === BOOL ? false : 0;
        case 'state':
            return stateValue(expression.source, state);
        case 'arith': {
            const a = evaluate(expression.left, state, unmodelled);
            const b = evaluate(expression.right, state, unmodelled);
            switch( expression.op ) {
                case '+': return a + b;
                case '-': return a - b;
                case '*': return a * b;
                case '/': return b === 0 ? 0 : Math.trunc(a / b);
                case '%': return b === 0 ? 0 : a % b;
                default: return 0;
            }
        }
        case 'compare': {
            const a = evaluate(expression.left, state, unmodelled);
            const b = evaluate(expression.right, state, unmodelled);
            switch( expression.op ) {
                case '=': return a === b;
                case '!': return a !== b;
                case '<': return a < b;
                case '>': return a > b;
                case '<=': return a <= b;
                case '>=': return a >= b;
                default: return false;
            }
        }
        case 'logic': {
            const a = evaluate(expression.left, state, unmodelled);
            return expression.op === '&'
                ? a && evaluate(expression.right, state, unmodelled)
                : a || evaluate(expression.right, state, unmodelled);
        }
        case 'not':
            return !evaluate(expression.value, state, unmodelled);
        case 'select':
            return evaluate(expression.test, state, unmodelled)
                ? evaluate(expression.whenTrue, state, unmodelled)
                : evaluate(expression.whenFalse, state, unmodelled);
        case 'template': {
            let out = '';
            for( let i = 0; i < expression.strings.length; i++ ) {
                out += expression.strings[i];
                if( i < expression.values.length ) out += String(evaluate(expression.values[i], state, unmodelled));
            }
            return out;
        }
        case 'call':
            return evaluateCall(expression, state, unmodelled);
        default:
            return 0;
    }
}

function stateValue(source, state) {
    const slice = SLICES[source.kind];
    if( slice ) return slice.read(state, source);
    const key = `${source.kind}:${source.id}`;
    return key in state ? state[key] : (source.initial ?? 0);
}

function evaluateCall(expression, state, unmodelled) {
    const args = expression.args.map((a) => evaluate(a, state, unmodelled));

    /* Pure arithmetic and string commands: the preview can answer these itself. */
    switch( expression.command ) {
        case 'tostring': return String(args[0]);
        case 'min': return Math.min(args[0], args[1]);
        case 'max': return Math.max(args[0], args[1]);
        case 'scale': return Math.trunc((args[0] * args[1]) / (args[2] || 1));
        default: break;
    }

    /* Host reads: answered out of the state the page owns. */
    const host = HOST_READS[expression.command];
    if( host ) return host.evaluate(args, state);

    /* Everything else is a read with no model. Recorded, not invented. */
    if( unmodelled )
        unmodelled.add(`${expression.command}: ${UNMODELLED[expression.command] || 'not modelled by the preview'}`);
    return 0;
}

/** Every prop of a component, resolved against `state`. */
export function resolveProps(component, state, unmodelled = null) {
    const props = { ...component.static };
    for( const binding of component.dynamic )
        props[binding.prop] = evaluate(binding.expr, state, unmodelled);
    return props;
}

/**
 * The host state an interface reads, as a list the page can offer controls for.
 *
 * Each entry carries its slice's control shape (src/host.js), because "what kind of
 * thing is this and how do I move it" is a property of the slice rather than of the
 * component that happened to read it.
 */
export function stateInputs(ir) {
    const seen = new Map();

    const note = (source, componentName) => {
        const key = `${source.kind}:${source.id}`;
        if( !seen.has(key) ) {
            const slice = SLICES[source.kind];
            seen.set(key, {
                key,
                kind: source.kind,
                id: source.id,
                label: slice ? slice.label : source.kind,
                request: slice ? slice.request : null,
                control: slice ? slice.control : { min: 0, max: 100, step: 1 },
                initial: source.initial ?? (slice?.control?.initial ?? 0),
                readBy: [],
            });
        }
        if( !seen.get(key).readBy.includes(componentName) )
            seen.get(key).readBy.push(componentName);
    };

    for( const component of ir.components )
        for( const source of component.dependencies )
            note(source, component.name);

    return [...seen.values()];
}
