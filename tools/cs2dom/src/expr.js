/*
 * Symbolic expressions.
 *
 * A component's props are ordinary JavaScript values right up until one of them
 * touches game state. `useVarp(173)` does not return a number — it returns a node
 * from this module, and every operator applied to it builds more nodes instead of
 * computing anything. What the compiler finally holds is the expression the author
 * wrote, in a form that can be printed as CS2 and whose leaves name the state that
 * has to re-run it.
 *
 * The operators are real ones. `energy / 100` is written as division in the .tsx
 * because src/transform.js rewrites every binary operator in a component into a
 * call to `__op` below, which computes when both sides are plain values and builds
 * a node when either is symbolic. Nothing here is reached for constant arithmetic:
 * `2 + 2` is 4 before the compiler ever sees it.
 *
 * Types are the three CS2 has room for in a component prop: int, string, boolean.
 * A boolean is not an int here — CS2 spells conditions in `if (...)` rather than
 * arithmetic, so mixing the two is a mistake worth catching in the compiler rather
 * than in the client.
 */

export const INT = 'int';
export const STRING = 'string';
export const BOOL = 'boolean';

let uid = 0;

/** Every symbolic node carries `__cs2dom` so a plain value is telling apart cheaply. */
export function isExpr(v) {
    return !!v && typeof v === 'object' && v.__cs2dom === 'expr';
}

function node(kind, type, fields) {
    return { __cs2dom: 'expr', id: ++uid, kind, type, ...fields };
}

/* ---- leaves -------------------------------------------------------------- */

export function constant(value) {
    if( typeof value === 'number' ) {
        if( !Number.isInteger(value) )
            throw new Cs2domExprError(`CS2 has no fractional numbers; got ${value}`);
        return node('const', INT, { value });
    }
    if( typeof value === 'string' )
        return node('const', STRING, { value });
    if( typeof value === 'boolean' )
        return node('const', BOOL, { value });
    throw new Cs2domExprError(`cannot use ${typeof value} in a component prop`);
}

/**
 * A state source: the thing whose change has to re-run the script that reads it.
 * `source` is what the emitted hook binds to — see src/ir.js `dependenciesOf`.
 */
export function stateRef(source, type) {
    return node('state', type, { source });
}

/** A local the generated script declares (a hook parameter, a hoisted select). */
export function local(name, type) {
    return node('local', type, { name });
}

/* ---- combinators --------------------------------------------------------- */

const ARITH = new Set(['+', '-', '*', '/', '%']);
const COMPARE = new Set(['<', '>', '<=', '>=', '==', '===', '!=', '!==']);

export class Cs2domExprError extends Error {}

export function lift(v) {
    return isExpr(v) ? v : constant(v);
}

/**
 * The hook src/transform.js rewrites `a <op> b` into.
 *
 * Plain values on both sides means the author wrote arithmetic the compiler can
 * finish now, so it does — that keeps constant folding out of the emitter and
 * keeps ordinary TypeScript in a component behaving like TypeScript.
 */
export function __op(op, a, b) {
    if( !isExpr(a) && !isExpr(b) )
        return applyConcrete(op, a, b);

    if( op === '+' && (typeOf(a) === STRING || typeOf(b) === STRING) )
        return template(['', '', ''], [a, b]);

    const left = lift(a);
    const right = lift(b);

    if( ARITH.has(op) ) {
        requireType(left, INT, op);
        requireType(right, INT, op);
        return node('arith', INT, { op, left, right });
    }
    if( COMPARE.has(op) ) {
        const cs2 = { '==': '=', '===': '=', '!=': '!', '!==': '!', '<': '<', '>': '>', '<=': '<=', '>=': '>=' }[op];
        if( left.type !== right.type )
            throw new Cs2domExprError(`cannot compare ${left.type} with ${right.type}`);
        return node('compare', BOOL, { op: cs2, left, right });
    }
    throw new Cs2domExprError(
        `operator '${op}' has no CS2 spelling; use arithmetic, comparison, && , || or a ternary`);
}

function applyConcrete(op, a, b) {
    switch( op ) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return Math.trunc(a / b);
        case '%': return a % b;
        case '<': return a < b;
        case '>': return a > b;
        case '<=': return a <= b;
        case '>=': return a >= b;
        case '==': case '===': return a === b;
        case '!=': case '!==': return a !== b;
        default:
            throw new Cs2domExprError(`operator '${op}' is not supported in a component`);
    }
}

/** `a && b` / `a || b`, rewritten by the transformer so both sides stay unevaluated. */
export function __logic(op, a, thunkB) {
    if( !isExpr(a) ) {
        /* Plain JavaScript short-circuit: the author is branching on build-time data. */
        if( op === '&&' ) return a ? thunkB() : a;
        return a ? a : thunkB();
    }
    const b = thunkB();
    const left = lift(a);
    const right = lift(b);
    requireType(left, BOOL, op);
    requireType(right, BOOL, op);
    return node('logic', BOOL, { op: op === '&&' ? '&' : '|', left, right });
}

/** `!a`. */
export function __not(a) {
    if( !isExpr(a) )
        return !a;
    requireType(a, BOOL, '!');
    return node('not', BOOL, { value: a });
}

/**
 * `test ? a : b`, rewritten so the branches stay unevaluated.
 *
 * A symbolic test cannot pick a branch here, so both are evaluated symbolically and
 * the choice becomes a node the emitter lowers to an `if`/`else` around an
 * assignment. A concrete test is ordinary JavaScript and picks a branch now.
 */
export function __cond(test, thunkA, thunkB) {
    if( !isExpr(test) )
        return test ? thunkA() : thunkB();
    requireType(test, BOOL, '?:');
    const a = lift(thunkA());
    const b = lift(thunkB());
    if( a.type !== b.type )
        throw new Cs2domExprError(
            `the branches of a ternary must have the same type; got ${a.type} and ${b.type}`);
    return node('select', a.type, { test, whenTrue: a, whenFalse: b });
}

/** A template literal, rewritten by the transformer. Becomes CS2 string interpolation. */
export function template(strings, values) {
    if( !values.some(isExpr) )
        return strings.reduce((acc, s, i) => acc + s + (i < values.length ? String(values[i]) : ''), '');
    return node('template', STRING, { strings: [...strings], values: values.map(lift) });
}

/** A call into the CS2 command set. `ret` is the type it leaves on the stack. */
export function call(command, args, ret) {
    return node('call', ret, { command, args: args.map(lift) });
}

/* ---- helpers ------------------------------------------------------------- */

export function typeOf(v) {
    if( isExpr(v) ) return v.type;
    if( typeof v === 'number' ) return INT;
    if( typeof v === 'string' ) return STRING;
    if( typeof v === 'boolean' ) return BOOL;
    return typeof v;
}

function requireType(e, type, what) {
    if( typeOf(e) !== type )
        throw new Cs2domExprError(`'${what}' needs ${type}, got ${typeOf(e)}`);
}

/** Walk every node of an expression, parents before children. */
export function walk(e, visit) {
    if( !isExpr(e) ) return;
    visit(e);
    for( const child of childrenOf(e) )
        walk(child, visit);
}

export function childrenOf(e) {
    switch( e.kind ) {
        case 'arith': case 'compare': case 'logic': return [e.left, e.right];
        case 'not': return [e.value];
        case 'select': return [e.test, e.whenTrue, e.whenFalse];
        case 'template': return e.values;
        case 'call': return e.args;
        default: return [];
    }
}

/** True when the expression can be written as a single CS2 term with no statements. */
export function isPure(e) {
    let pure = true;
    walk(e, (n) => { if( n.kind === 'select' ) pure = false; });
    return pure;
}
