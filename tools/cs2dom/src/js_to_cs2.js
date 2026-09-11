/*
 * JavaScript back to CS2 source.
 *
 * The other direction of `cs2_js_emit.js`, and deliberately a NARROWER
 * language. Lowering CS2 to JavaScript can be total because JavaScript can
 * express everything CS2 can; the reverse cannot, and pretending otherwise is
 * how a compiler starts guessing. So this accepts a stated subset — the shape
 * the emitter produces, plus what a person editing that output would naturally
 * write — and REFUSES anything else by name.
 *
 * Refusing is the feature. An unsupported construct that compiled to something
 * approximate would produce a script that runs and misbehaves; one that fails
 * at build time costs a minute.
 *
 * ------------------------------------------------------------------
 * What is accepted
 * ------------------------------------------------------------------
 *
 *   let/assignment          `$int0 = ...`, `let $int0 = 0`
 *   if / else if / else
 *   while
 *   switch with case/default
 *   return
 *   intrinsic calls          `K.add(a, b)` -> `calc(a + b)`
 *   host calls               `H.cc_setcolour(x)` -> `cc_setcolour(x)`
 *   proc calls               `yield* cs2_123(H, a)` -> `~script123(a)`
 *   hook bindings            `K.hook(id, [args], [triggers])`
 *   comparisons              `===`, `!==`, `<`, `>`, `<=`, `>=`, `&&`, `||`
 *   literals, template strings
 *
 * Everything generated CS2 needs, and nothing that would need a semantics this
 * file cannot state. Every emitted script is handed to the REAL compiler
 * (`3rd/rscache/tools/cs2/cs2`) before anything is written, so a tree can
 * never hold source that will not bake.
 */

export class Cs2LowerError extends Error {
    constructor(message, node = null) {
        super(node && node.line ? `line ${node.line}: ${message}` : message);
        this.name = 'Cs2LowerError';
        this.node = node;
    }
}

/** Intrinsic -> the CS2 operator or command it came from. */
const INTRINSIC_INFIX = new Map(Object.entries({
    add: '+', sub: '-', multiply: '*', div: '/', mod: '%',
}));

const INTRINSIC_COMMAND = new Map(Object.entries({
    min: 'min', max: 'max', scale: 'scale', pow: 'pow', invpow: 'invpow',
    abs: 'abs', bitcount: 'bitcount', setbit: 'setbit', clearbit: 'clearbit',
    testbit: 'testbit', togglebit: 'togglebit', getbitRange: 'getbit_range',
    setbitRangeValue: 'setbit_range_toint', interpolate: 'interpolate',
    addpercent: 'addpercent', and: 'and', or: 'or',
    append: 'append', appendNum: 'append_num', appendChar: 'append_char',
    tostring: 'tostring', compare: 'compare', lowercase: 'lowercase',
    uppercase: 'uppercase', escape: 'escape', removetags: 'removetags',
    substring: 'substring', stringLength: 'string_length',
    stringIndexofChar: 'string_indexof_char',
    stringIndexofString: 'string_indexof_string',
    stringToInt: 'string_to_int',
    charIsalphanumeric: 'char_isalphanumeric', charIsnumeric: 'char_isnumeric',
    charIsprintable: 'char_isprintable',
    arrayLength: 'array_length', arraySetlength: 'array_setlength',
    arrayAppend: 'array_append', arraySortAll: 'array_sort_all',
}));

/** JavaScript comparison -> the CS2 one. */
const COMPARISONS = new Map(Object.entries({
    '===': '=', '==': '=', '!==': '!', '!=': '!',
    '<': '<', '>': '>', '<=': '<=', '>=': '>=',
}));

/**
 * Lower one generated-JavaScript script back to CS2 source.
 *
 * `ast` is an ESTree-shaped tree — whatever parser the caller has. Only the
 * node types listed above are visited; anything else throws by its own type
 * name, which is the whole point.
 */
export function lowerScriptToCs2(ast, { name, args = [], returns = [] } = {}) {
    const writer = new Cs2Writer();
    writer.writeFunction(ast, { name, args, returns });
    return writer.finish();
}

class Cs2Writer {
    constructor() {
        this.out = [];
        this.indent = 0;
        /* Locals already declared, so the first write emits `def_` and later
         * ones do not — the same rule the source generator follows. */
        this.declared = new Set();
        /* Set inside a `calc(...)`, because arithmetic is only legal there and
         * ONE calc covers a whole nested expression rather than each operator. */
        this.inCalc = false;
    }

    finish() { return `${this.out.join('\n')}\n`; }

    line(text) { this.out.push('\t'.repeat(this.indent) + text); }

    writeFunction(ast, { name, args, returns }) {
        const signature = args.length
            ? `(${args.map((a) => `${a.type} $${a.name}`).join(', ')})`
            : (returns.length ? '()' : '');
        const returnList = returns.length ? `(${returns.join(', ')})` : '';
        this.line(`${name}${signature}${returnList}`);
        for( const arg of args ) this.declared.add(arg.name);
        this.writeBody(ast);
    }

    /**
     * A body, whatever shape it arrived in.
     *
     * A function node's `body` is a BlockStatement whose own `body` is the
     * statement array, so `node.body ?? node` is ambiguous between the two —
     * it unwraps a function correctly and unwraps a block one level too far.
     * Normalising explicitly is the only way to be right for both.
     */
    writeBody(node) {
        for( const statement of statementsOf(node) ) this.writeStatement(statement);
    }

    /* --------------------------------------------------------------
     * Statements
     * ----------------------------------------------------------- */

    writeStatement(node) {
        switch( node.type )
        {
        case 'VariableDeclaration': return this.writeDeclaration(node);
        case 'ExpressionStatement': return this.writeExpressionStatement(node);
        case 'IfStatement': return this.writeIf(node);
        case 'WhileStatement': return this.writeWhile(node);
        case 'SwitchStatement': return this.writeSwitch(node);
        case 'ReturnStatement': return this.writeReturn(node);
        case 'BlockStatement': return this.writeBody(node);
        /* A `break` inside a switch case is JavaScript's requirement, not
         * CS2's — the language has no fallthrough, so the case ends by
         * itself and the break has nothing to say. */
        case 'BreakStatement': return undefined;
        case 'EmptyStatement': return undefined;
        default:
            throw new Cs2LowerError(`cannot lower a ${node.type} to CS2`, node);
        }
    }

    writeDeclaration(node) {
        for( const declarator of node.declarations )
        {
            const name = this.localName(declarator.id);
            const value = declarator.init
                ? this.expression(declarator.init)
                : this.zeroFor(name);
            /* A `let` is a declaration only the first time; the frame the
             * emitter writes declares every local up front, so most of these
             * are the initial zeroing and can be skipped where the type is
             * unknowable. */
            this.line(`def_${this.typeOf(declarator)} $${name} = ${value};`);
            this.declared.add(name);
        }
    }

    writeExpressionStatement(node) {
        const expression = node.expression;

        /* `while ((t = H.op(...)) === PARK) yield;` is the emitter's retry
         * loop. In CS2 there is no retry — the VM re-executes the opcode — so
         * the loop collapses back to the bare call it wraps. */
        const retry = this.matchRetryLoop(node);
        if( retry ) return this.line(`${retry};`);

        if( expression.type === 'AssignmentExpression' )
        {
            const target = this.assignmentTarget(expression.left);
            const value = this.expression(expression.right);
            if( expression.operator !== '=' )
                throw new Cs2LowerError(
                    `compound assignment '${expression.operator}' has no CS2 form; ` +
                    'write it out', node);
            return this.line(`${target} = ${value};`);
        }
        return this.line(`${this.expression(expression)};`);
    }

    /** `while ((t = call) === PARK) yield;` -> just the call. */
    matchRetryLoop(node) {
        if( node.type !== 'WhileStatement' ) return null;
        const test = node.test;
        if( !test || test.type !== 'BinaryExpression' ) return null;
        if( test.right?.name !== 'PARK' ) return null;
        const inner = test.left;
        if( inner?.type !== 'AssignmentExpression' ) return null;
        return this.expression(inner.right);
    }

    writeIf(node) {
        this.line(`if (${this.condition(node.test)}) {`);
        this.indent++;
        this.writeBody(node.consequent);
        this.indent--;
        let alternate = node.alternate;
        while( alternate && alternate.type === 'IfStatement' )
        {
            this.line(`} else if (${this.condition(alternate.test)}) {`);
            this.indent++;
            this.writeBody(alternate.consequent);
            this.indent--;
            alternate = alternate.alternate;
        }
        if( alternate )
        {
            this.line('} else {');
            this.indent++;
            this.writeBody(alternate);
            this.indent--;
        }
        this.line('}');
    }

    writeWhile(node) {
        const retry = this.matchRetryLoop(node);
        if( retry ) return this.line(`${retry};`);
        this.line(`while (${this.condition(node.test)}) {`);
        this.indent++;
        this.writeBody(node.body);
        this.indent--;
        this.line('}');
    }

    writeSwitch(node) {
        /* The subject's type decides how the case labels are spelled, and CS2
         * spells it in the keyword itself. Without a declared type there is
         * nothing to guess from, so `switch_int` is the honest default and a
         * caller that knows better states it. */
        this.line(`switch_int (${this.expression(node.discriminant)}) {`);
        this.indent++;
        for( const clause of node.cases )
        {
            this.line(clause.test ? `case ${this.expression(clause.test)} :` : 'case default :');
            this.indent++;
            for( const statement of clause.consequent ) this.writeStatement(statement);
            this.indent--;
        }
        this.indent--;
        this.line('}');
    }

    writeReturn(node) {
        if( !node.argument ) return this.line('return;');
        /* A tuple returns as an array in JavaScript and as a list in CS2. */
        if( node.argument.type === 'ArrayExpression' )
        {
            const values = node.argument.elements.map((e) => this.expression(e));
            return this.line(`return(${values.join(', ')});`);
        }
        return this.line(`return(${this.expression(node.argument)});`);
    }

    /* --------------------------------------------------------------
     * Expressions
     * ----------------------------------------------------------- */

    /** A condition, where comparisons are legal and arithmetic is not. */
    condition(node) {
        if( node.type === 'LogicalExpression' )
        {
            const operator = node.operator === '&&' ? '&' : '|';
            return `${this.condition(node.left)} ${operator} ${this.condition(node.right)}`;
        }
        if( node.type === 'BinaryExpression' && COMPARISONS.has(node.operator) )
            return `${this.expression(node.left)} ${COMPARISONS.get(node.operator)} ` +
                `${this.expression(node.right)}`;
        return this.expression(node);
    }

    expression(node) {
        if( !node ) return 'null';
        switch( node.type )
        {
        case 'Literal': return this.literal(node);
        case 'Identifier': return `$${node.name}`;
        case 'TemplateLiteral': return this.template(node);
        case 'CallExpression': return this.call(node);
        case 'YieldExpression': return this.yieldExpression(node);
        case 'ArrayExpression':
            return node.elements.map((e) => this.expression(e)).join(', ');
        case 'UnaryExpression':
            if( node.operator === '-' ) return `-${this.expression(node.argument)}`;
            throw new Cs2LowerError(`unary '${node.operator}' has no CS2 form`, node);
        case 'BinaryExpression': return this.binary(node);
        case 'LogicalExpression': return this.condition(node);
        case 'MemberExpression': return this.member(node);
        default:
            throw new Cs2LowerError(`cannot lower a ${node.type} expression`, node);
        }
    }

    /**
     * Arithmetic, which is only legal inside `calc(...)`.
     *
     * One calc wraps a whole nested expression rather than each operator, and
     * an ARGUMENT LIST resets that: `calc($a - ~proc(0, $b - $c))` is inside a
     * calc where `$b - $c` is written, but that subtraction sits in an
     * argument, where bare arithmetic is illegal. Eighteen scripts in
     * cache.osrs239 decompiled to source that would not compile back for
     * exactly this reason.
     */
    binary(node) {
        /* A `| 0` is the emitter's int32 truncation, not an operation. */
        if( node.operator === '|' && node.right?.type === 'Literal' && node.right.value === 0 )
            return this.expression(node.left);

        if( COMPARISONS.has(node.operator) )
            return `${this.expression(node.left)} ${COMPARISONS.get(node.operator)} ` +
                `${this.expression(node.right)}`;

        const wasInCalc = this.inCalc;
        this.inCalc = true;
        const body = `${this.expression(node.left)} ${node.operator} ${this.expression(node.right)}`;
        this.inCalc = wasInCalc;
        return wasInCalc ? `(${body})` : `calc(${body})`;
    }

    call(node) {
        const callee = node.callee;
        if( callee.type !== 'MemberExpression' )
            throw new Cs2LowerError('only H.* and K.* calls can be lowered', node);
        const object = callee.object?.name;
        const method = callee.property?.name;

        if( object === 'K' ) return this.intrinsic(method, node);
        if( object === 'H' ) return this.hostCall(method, node);
        throw new Cs2LowerError(`unknown call target '${object}'`, node);
    }

    intrinsic(name, node) {
        if( name === 'hook' ) return this.hook(node);
        if( name === 'join' ) return this.join(node);
        if( name === 'defineArray' )
            throw new Cs2LowerError(
                'def_ arrays are declarations; write `def_int $arr(size)` directly', node);

        const infix = INTRINSIC_INFIX.get(name);
        if( infix )
        {
            const [left, right] = node.arguments;
            return this.binary({
                type: 'BinaryExpression', operator: infix, left, right, line: node.line,
            });
        }
        const command = INTRINSIC_COMMAND.get(name);
        if( !command ) throw new Cs2LowerError(`no CS2 command for K.${name}`, node);
        return `${command}(${this.argumentList(node.arguments)})`;
    }

    hostCall(name, node) {
        /* The `dot_` prefix is the emitter's spelling of the `.` form. */
        const dotted = name.startsWith('dot_');
        const command = dotted ? name.slice(4) : name;
        const args = this.argumentList(node.arguments);
        return `${dotted ? '.' : ''}${command}${args ? `(${args})` : ''}`;
    }

    /**
     * `yield* cs2_123(H, ...)` is a proc call.
     *
     * The `yield*` is the JavaScript mechanism for propagating a park; CS2 has
     * no such thing because its VM re-executes the opcode. It carries no
     * meaning here and is dropped.
     */
    yieldExpression(node) {
        if( !node.delegate )
            throw new Cs2LowerError('a bare yield has no CS2 form', node);
        const call = node.argument;
        if( call?.type !== 'CallExpression' || call.callee?.type !== 'Identifier' )
            throw new Cs2LowerError('yield* must delegate to a generated script', node);
        const match = /^cs2_(\d+)$/.exec(call.callee.name);
        if( !match ) throw new Cs2LowerError(`'${call.callee.name}' is not a script`, node);
        /* The first argument is the host, which CS2 does not pass. */
        const args = this.argumentList(call.arguments.slice(1));
        return `~script${match[1]}${args ? `(${args})` : ''}`;
    }

    hook(node) {
        const [scriptId, args, triggers] = node.arguments;
        if( scriptId?.type === 'Literal' && scriptId.value === -1 ) return 'null';
        const id = this.expression(scriptId).replace(/^\$/, '');
        const argList = args?.elements?.length
            ? `(${args.elements.map((e) => this.expression(e)).join(', ')})` : '';
        const triggerList = triggers?.elements?.length
            ? `{${triggers.elements.map((e) => this.expression(e)).join(', ')}}` : '';
        /* The whole callback is one quoted string in the source dialect. */
        return `"script${id}${argList}${triggerList}"`;
    }

    /** String interpolation: literal parts inline, everything else bracketed. */
    join(node) {
        let out = '"';
        for( const part of node.arguments )
        {
            if( part.type === 'Literal' && typeof part.value === 'string' )
                out += escapeCacheString(part.value);
            else out += `<${this.expression(part)}>`;
        }
        return `${out}"`;
    }

    template(node) {
        let out = '"';
        node.quasis.forEach((quasi, index) => {
            out += escapeCacheString(quasi.value.cooked ?? quasi.value.raw ?? '');
            const expression = node.expressions[index];
            if( expression ) out += `<${this.expression(expression)}>`;
        });
        return `${out}"`;
    }

    member(node) {
        /* An array element read: `$arr[i]` in JavaScript, `$arr(i)` in CS2. */
        if( node.computed )
            return `${this.expression(node.object)}(${this.expression(node.property)})`;
        throw new Cs2LowerError('property access has no CS2 form', node);
    }

    /**
     * An argument list, which is a fresh calc context.
     *
     * `inCalc` must not carry into an argument: bare arithmetic is illegal
     * there and the compiler rejects it on the closing paren.
     */
    argumentList(nodes = []) {
        const wasInCalc = this.inCalc;
        this.inCalc = false;
        const out = nodes.map((node) => this.expression(node)).join(', ');
        this.inCalc = wasInCalc;
        return out;
    }

    literal(node) {
        if( typeof node.value === 'string' ) return `"${escapeCacheString(node.value)}"`;
        if( typeof node.value === 'boolean' ) return node.value ? 'true' : 'false';
        if( node.value === null ) return 'null';
        return String(node.value);
    }

    assignmentTarget(node) {
        if( node.type === 'Identifier' ) return `$${node.name}`;
        if( node.type === 'MemberExpression' && node.computed )
            return `${this.expression(node.object)}(${this.expression(node.property)})`;
        throw new Cs2LowerError('that assignment target has no CS2 form', node);
    }

    localName(node) {
        if( node.type !== 'Identifier' )
            throw new Cs2LowerError('destructuring has no CS2 form', node);
        return node.name.replace(/^\$/, '');
    }

    /**
     * The declared type of a local.
     *
     * Recovered from the name the emitter gave it, which encodes the inferred
     * type (`$component3`, `$int0`). Where it cannot be read, `int` is the
     * bank's own name and is what the source generator falls back to as well —
     * it is honest about the bank even when the type is unknown.
     */
    typeOf(declarator) {
        const name = this.localName(declarator.id);
        const match = /^([a-z_]+)\d*$/i.exec(name);
        return match ? match[1] : 'int';
    }

    zeroFor(name) {
        return /^string/i.test(name) ? '""' : '0';
    }
}

/** The statement list of a function, a block, an array, or a lone statement. */
function statementsOf(node) {
    if( Array.isArray(node) ) return node;
    if( !node ) return [];
    if( node.type === 'BlockStatement' ) return node.body;
    if( node.body ) return statementsOf(node.body);
    return [node];
}

/** Quote the two bytes that delimit a source string. */
export function escapeCacheString(text) {
    return String(text ?? '').replace(/([\\"])/g, '\\$1');
}
