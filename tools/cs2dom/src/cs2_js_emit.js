/*
 * A decompiled CS2 script, as JavaScript.
 *
 * Input is the syntax tree `cs2 decompile --emit ast-json` produces — the same
 * structured tree the source generator prints, so this back end and the `.cs2`
 * listing describe one program. Output is a generator function per script.
 *
 * ------------------------------------------------------------------
 * Why a generator
 * ------------------------------------------------------------------
 *
 * A host operation can need something that is not loaded. The C VM answers
 * that by rolling the opcode back to a checkpoint and re-executing it after
 * the loader finishes; the whole undo-log/checkpoint apparatus in cs2vm2.c
 * exists because C cannot suspend in the middle of an expression.
 *
 * JavaScript can. A parking host call is emitted as
 *
 *     while ((t = H.cc_find(p, s)) === PARK) yield;
 *
 * which is the same contract said plainly: attempt, suspend if the answer is
 * not available, retry after the driver has serviced the load. Nothing is
 * rolled back because nothing was applied — a host method that answers PARK
 * has not mutated anything, exactly as the C host must not.
 *
 * The retry test costs one comparison, and only the 174 opcodes the C client's
 * own yield planner can park on carry it (see generated/cs2_host_park.js).
 * The other ~460 host operations are plain calls.
 *
 * ------------------------------------------------------------------
 * What is NOT emitted as a JavaScript operator
 * ------------------------------------------------------------------
 *
 * Arithmetic. CS2 ints are signed 32-bit and JS numbers are doubles, so every
 * arithmetic and string operation routes through cs2_intrinsics.js, which is
 * checked against the matching C handler. `a + b` never appears in generated
 * code; `K.add(a, b)` does. Comparisons are safe as operators because both
 * sides are already 32-bit values.
 */

import { PARK_CLASS_BY_OPCODE } from './generated/cs2_host_park.js';

export const CS2_JS_EMIT_SCHEMA = 'cs2dom-js-emit/1';
const AST_SCHEMA = 'rscache-cs2-ast/1';

/** Short-circuit operators the decompiler synthesises; they have no name. */
const OP_SS_OR = -1;
const OP_SS_AND = -2;

/**
 * Operations computed by the VM itself, and the intrinsic each becomes.
 *
 * Membership here is a claim that the operation touches no client state, so it
 * can be executed without a host at all. Anything absent is a host call — the
 * safe default, because a host method that is not implemented throws where a
 * wrongly-inlined intrinsic would return a plausible number.
 */
const INTRINSICS = new Map([
    /* Arithmetic — cs2vm2.c 4000..4036. */
    [4000, { fn: 'add', arity: 2 }],
    [4001, { fn: 'sub', arity: 2 }],
    [4002, { fn: 'multiply', arity: 2 }],
    [4003, { fn: 'div', arity: 2 }],
    [4006, { fn: 'interpolate', arity: 5 }],
    [4007, { fn: 'addpercent', arity: 2 }],
    [4008, { fn: 'setbit', arity: 2 }],
    [4009, { fn: 'clearbit', arity: 2 }],
    [4010, { fn: 'testbit', arity: 2 }],
    [4011, { fn: 'mod', arity: 2 }],
    [4012, { fn: 'pow', arity: 2 }],
    [4013, { fn: 'invpow', arity: 2 }],
    [4014, { fn: 'and', arity: 2 }],
    [4015, { fn: 'or', arity: 2 }],
    [4016, { fn: 'min', arity: 2 }],
    [4017, { fn: 'max', arity: 2 }],
    [4018, { fn: 'scale', arity: 3 }],
    [4025, { fn: 'bitcount', arity: 1 }],
    [4026, { fn: 'togglebit', arity: 2 }],
    [4029, { fn: 'getbitRange', arity: 3 }],
    [4030, { fn: 'setbitRangeValue', arity: 4 }],
    [4035, { fn: 'abs', arity: 1 }],
    /* Strings — 4100..4127. */
    [4100, { fn: 'appendNum', arity: 2 }],
    [4101, { fn: 'append', arity: 2 }],
    [4103, { fn: 'lowercase', arity: 1 }],
    [4106, { fn: 'tostring', arity: 1 }],
    [4107, { fn: 'compare', arity: 2 }],
    [4111, { fn: 'escape', arity: 1 }],
    [4112, { fn: 'appendChar', arity: 2 }],
    [4113, { fn: 'charIsprintable', arity: 1 }],
    [4114, { fn: 'charIsalphanumeric', arity: 1 }],
    [4116, { fn: 'charIsnumeric', arity: 1 }],
    [4117, { fn: 'stringLength', arity: 1 }],
    [4118, { fn: 'substring', arity: 3 }],
    [4119, { fn: 'removetags', arity: 1 }],
    [4120, { fn: 'stringIndexofChar', arity: 2 }],
    [4121, { fn: 'stringIndexofString', arity: 3 }],
    [4122, { fn: 'uppercase', arity: 1 }],
    [4036, { fn: 'stringToInt', arity: 1 }],
    /* Arrays — 8000..8024. Only the handle-local ones; anything reaching a
     * cache table (enum_getoutputs, array_split on a config) stays a host call. */
    [8003, { fn: 'arrayLength', arity: 1 }],
    [8023, { fn: 'arraySetlength', arity: 2 }],
    [8024, { fn: 'arrayAppend', arity: 3 }],
]);

/** The array opcodes with their own statement shapes rather than a call. */
const OP_DEFINE_ARRAY = 44;
const OP_PUSH_ARRAY_INT = 45;
const OP_POP_ARRAY_INT = 46;
const OP_JOIN_STRING = 37;
const OP_ARRAY_SORT_ALL = 8000;
const OP_PUSH_CONSTANT_NULL = 63;

/** Global variable banks, and the host accessor pair each reads and writes. */
const GLOBAL_ACCESSORS = new Map([
    ['varp', { get: 'varp', set: 'setVarp' }],
    ['varbit', { get: 'varbit', set: 'setVarbit' }],
    ['varcint', { get: 'varc', set: 'setVarc' }],
    ['varcstring', { get: 'varcString', set: 'setVarcString' }],
    ['varclansetting', { get: 'varClanSetting', set: 'setVarClanSetting' }],
    ['varclan', { get: 'varClan', set: 'setVarClan' }],
]);

/** JS reserved words a CS2 identifier could otherwise collide with. */
const RESERVED = new Set([
    'break', 'case', 'catch', 'class', 'const', 'continue', 'default', 'delete',
    'do', 'else', 'export', 'extends', 'finally', 'for', 'function', 'if',
    'import', 'in', 'instanceof', 'let', 'new', 'return', 'super', 'switch',
    'this', 'throw', 'try', 'typeof', 'var', 'void', 'while', 'with', 'yield',
    'H', 'K', 'PARK',
]);

export class Cs2EmitError extends Error {
    constructor(message, scriptId) {
        super(scriptId === undefined ? message : `script ${scriptId}: ${message}`);
        this.name = 'Cs2EmitError';
        this.scriptId = scriptId;
    }
}

/**
 * Emit one script as a JavaScript generator function.
 *
 * Returns the function source plus what the script depends on, so a packager
 * can gather a closure without re-walking the tree: `procs` are the scripts it
 * calls, `hooks` the scripts it installs, `hostOps` every host method it
 * reaches. `functionName` is what the source declares itself as.
 */
export function emitScript(ast) {
    if( !ast || ast.schema !== AST_SCHEMA )
        throw new Cs2EmitError(`expected ${AST_SCHEMA}, got ${ast && ast.schema}`);

    const emitter = new ScriptEmitter(ast);
    const body = emitter.run();
    return {
        schema: CS2_JS_EMIT_SCHEMA,
        id: ast.id,
        name: ast.name,
        functionName: emitter.functionName,
        code: body,
        procs: [...emitter.procs].sort((a, b) => a - b),
        hooks: [...emitter.hooks].sort((a, b) => a - b),
        hostOps: [...emitter.hostOps].sort(),
        parksOn: [...emitter.parkClasses].sort(),
    };
}

/** The JS function name a script id declares itself under. */
export function scriptFunctionName(id) {
    return `cs2_${id}`;
}

class ScriptEmitter {
    constructor(ast) {
        this.ast = ast;
        this.functionName = scriptFunctionName(ast.id);
        this.procs = new Set();
        this.hooks = new Set();
        this.hostOps = new Set();
        this.parkClasses = new Set();
        this.arguments = new Set();
        this.temp = 0;
        /* Statements that must be emitted before the expression being built —
         * a parking call cannot sit inside an expression, because `yield` may
         * only suspend a statement it is the whole of. */
        this.prelude = [];
        this.names = buildNameTable(ast);
    }

    run() {
        const lines = [];
        const args = this.ast.arguments.map((variable) => this.names.get(variableKey(variable)));
        for( const variable of this.ast.arguments )
            this.arguments.add(variableKey(variable));

        lines.push(`/* ${this.ast.name} — clientscript ${this.ast.id} */`);
        lines.push(`export function* ${this.functionName}(H${args.map((a) => `, ${a}`).join('')}) {`);
        /*
         * The frame is declared up front, zeroed, because that is what the VM
         * hands a script: `CS2VM2_PushCallScript` memsets the locals, so a read
         * before the first write is legal and answers 0 or "".
         *
         * No script in cache.osrs239 needs that — every local here is written
         * before it is read — so this buys nothing today. It is still the right
         * shape: declaring at first write makes the emitter's correctness
         * depend on a property of the *content*, and the one script that ever
         * reads an untouched local would get `undefined` where the client gets
         * 0, silently, on one branch.
         */
        const body = this.construct(this.ast.body, 1);
        lines.push(...this.frameDeclaration(1));
        lines.push(...body);
        lines.push('}');
        return lines.join('\n');
    }

    /** `let $a = 0, $b = "";` for every local this script uses but was not passed. */
    frameDeclaration(depth) {
        const parts = [];
        for( const [key, name] of this.names )
        {
            if( this.arguments.has(key) ) continue;
            parts.push(`${name} = ${key.startsWith('string:') ? '\'\'' : '0'}`);
        }
        return parts.length ? [pad(depth) + `let ${parts.join(', ')};`] : [];
    }

    /* --------------------------------------------------------------
     * Constructs
     * ----------------------------------------------------------- */

    construct(node, depth) {
        if( !node ) return [];
        switch( node.kind )
        {
        case 'seq':
            return [...this.seq(node, depth), ...this.construct(node.next, depth)];
        case 'if':
            return [...this.ifChain(node, depth), ...this.construct(node.next, depth)];
        case 'while':
            return [...this.whileLoop(node, depth), ...this.construct(node.next, depth)];
        case 'switch':
            return [...this.switchOn(node, depth), ...this.construct(node.next, depth)];
        default:
            throw new Cs2EmitError(`unknown construct '${node.kind}'`, this.ast.id);
        }
    }

    seq(node, depth) {
        const lines = [];
        for( const insn of node.instructions )
            lines.push(...this.instruction(insn, depth));
        return lines;
    }

    ifChain(node, depth) {
        const lines = [];
        /* Local, not a field: if-chains nest, and a shared counter would close
         * an inner chain's blocks around the outer one's body. */
        let pendingCloses = 0;
        node.branches.forEach((branch, index) => {
            /*
             * An `else if` condition may need statements of its own — a
             * parking call, a temporary. JS has nowhere to put them on an
             * `else if` line, so a condition with a prelude becomes a nested
             * `else { ... if }`. The common case stays flat.
             */
            const { code, prelude } = this.capture(() => this.expr(branch.condition));
            if( index === 0 )
            {
                lines.push(...indent(prelude, depth));
                lines.push(pad(depth) + `if (${code}) {`);
            }
            else if( prelude.length === 0 )
            {
                lines.push(pad(depth) + `} else if (${code}) {`);
            }
            else
            {
                lines.push(pad(depth) + '} else {');
                lines.push(...indent(prelude, depth + 1));
                lines.push(pad(depth + 1) + `if (${code}) {`);
                pendingCloses++;
            }
            lines.push(...this.construct(branch.body, depth + 1));
        });
        if( node.otherwise )
        {
            lines.push(pad(depth) + '} else {');
            lines.push(...this.construct(node.otherwise, depth + 1));
        }
        lines.push(pad(depth) + '}');
        /* Close any nested blocks the prelude case opened, innermost first. */
        for( let i = 0; i < pendingCloses; i++ )
            lines.push(pad(depth) + '}');
        return lines;
    }

    whileLoop(node, depth) {
        /*
         * A loop condition is re-evaluated every iteration, so a prelude
         * belongs inside the loop, not before it. `while (true) { prelude; if
         * (!cond) break; body }` is the shape that keeps that true; a
         * prelude-free condition keeps the plain `while`.
         */
        const { code, prelude } = this.capture(() => this.expr(node.condition));
        if( prelude.length === 0 )
        {
            return [
                pad(depth) + `while (${code}) {`,
                ...this.construct(node.body, depth + 1),
                pad(depth) + '}',
            ];
        }
        return [
            pad(depth) + 'while (true) {',
            ...indent(prelude, depth + 1),
            pad(depth + 1) + `if (!(${code})) break;`,
            ...this.construct(node.body, depth + 1),
            pad(depth) + '}',
        ];
    }

    switchOn(node, depth) {
        const { code, prelude } = this.capture(() => this.expr(node.expression));
        const lines = [...indent(prelude, depth), pad(depth) + `switch (${code}) {`];
        for( const group of node.cases )
        {
            group.keys.forEach((key, index) => {
                const label = group.literals && group.literals[index];
                const comment = label && label !== String(key) ? ` /* ${label} */` : '';
                lines.push(pad(depth + 1) + `case ${key}:${comment}`);
            });
            lines.push(...this.construct(group.body, depth + 2));
            /*
             * Every CS2 case body ends by leaving the switch — the language has
             * no fallthrough — so a `break` closes each group. A body ending in
             * `return` gets one too; it is unreachable and harmless, and
             * proving otherwise would mean tracking termination here.
             */
            lines.push(pad(depth + 2) + 'break;');
        }
        if( node.default )
        {
            lines.push(pad(depth + 1) + 'default:');
            lines.push(...this.construct(node.default, depth + 2));
            lines.push(pad(depth + 2) + 'break;');
        }
        lines.push(pad(depth) + '}');
        return lines;
    }

    /* --------------------------------------------------------------
     * Instructions
     * ----------------------------------------------------------- */

    instruction(insn, depth) {
        switch( insn.kind )
        {
        case 'assignment':
            return this.assignment(insn, depth);
        case 'return':
            return this.returnStatement(insn, depth);
        default:
            throw new Cs2EmitError(`unknown instruction '${insn.kind}'`, this.ast.id);
        }
    }

    assignment(insn, depth) {
        const defs = insn.definitions || [];

        /*
         * `define_array` declares its target in its FIRST ARGUMENT, not in the
         * instruction's definitions — the source spells it `def_int $arr(n)`,
         * and the tree keeps that shape. Emitting it as a bare statement would
         * build the array and throw the handle away, leaving every later
         * element access reading an undefined slot.
         */
        if( defs.length === 0 && insn.expression && insn.expression.opcode === OP_DEFINE_ARRAY )
        {
            const [target, size] = insn.expression.arguments;
            const stackType = target.variable ? target.variable.stackType : 'int';
            const { code, prelude } = this.capture(() => this.expr(size));
            const lines = indent(prelude, depth);
            lines.push(pad(depth) +
                this.storeOne(target, `K.defineArray(${code}, ${JSON.stringify(stackType)})`));
            return lines;
        }

        /* A bare statement: the expression is evaluated for its effect. */
        if( defs.length === 0 )
        {
            const { code, prelude } = this.capture(() => this.expr(insn.expression));
            const lines = indent(prelude, depth);
            /* A parking call is already a complete statement in the prelude
             * and leaves only its temporary behind; `t0;` would say nothing. */
            if( !(prelude.length > 0 && /^t\d+$/.test(code)) )
                lines.push(pad(depth) + `${code};`);
            return lines;
        }

        /* Writing through an array element or a global is a call, not a name. */
        if( defs.length === 1 )
        {
            const { code, prelude } = this.capture(() => this.expr(insn.expression));
            const lines = indent(prelude, depth);
            lines.push(pad(depth) + this.storeOne(defs[0], code));
            return lines;
        }

        /*
         * Several slots at once — a proc returning a tuple. The values arrive
         * as one array, destructured into whatever the slots are, which may
         * mix fresh locals with globals, so a plain destructuring pattern will
         * not always do: the tuple lands in a temporary and each slot is
         * stored from it.
         */
        const { code, prelude } = this.capture(() => this.expr(insn.expression));
        const lines = indent(prelude, depth);
        const tuple = this.newTemp();
        lines.push(pad(depth) + `const ${tuple} = ${code};`);
        defs.forEach((def, index) => {
            lines.push(pad(depth) + this.storeOne(def, `${tuple}[${index}]`));
        });
        return lines;
    }

    /** One assignment target, as a complete statement. */
    storeOne(def, valueCode) {
        if( def.kind === 'access' || def.kind === 'pointer' )
        {
            const variable = def.variable;
            if( !variable.local )
            {
                const accessor = GLOBAL_ACCESSORS.get(variable.kind);
                if( !accessor )
                    throw new Cs2EmitError(`no accessor for global '${variable.kind}'`, this.ast.id);
                this.hostOps.add(accessor.set);
                return `H.${accessor.set}(${variable.id}, ${valueCode});`;
            }
            return `${this.names.get(variableKey(variable))} = ${valueCode};`;
        }
        throw new Cs2EmitError(`cannot assign to '${def.kind}'`, this.ast.id);
    }

    returnStatement(insn, depth) {
        const values = insn.values || [];
        if( values.length === 0 ) return [pad(depth) + 'return;'];

        const parts = [];
        const prelude = [];
        for( const value of values )
        {
            const captured = this.capture(() => this.expr(value));
            prelude.push(...captured.prelude);
            parts.push(captured.code);
        }
        const lines = indent(prelude, depth);
        /* One value returns bare; several return the tuple the caller
         * destructures, matching how the stack delivered them. */
        lines.push(pad(depth) + (parts.length === 1
            ? `return ${parts[0]};`
            : `return [${parts.join(', ')}];`));
        return lines;
    }

    /* --------------------------------------------------------------
     * Expressions
     * ----------------------------------------------------------- */

    expr(node) {
        if( node === null || node === undefined ) return 'null';
        switch( node.kind )
        {
        case 'constant':
            return constantCode(node);
        case 'access':
            return this.read(node.variable);
        case 'pointer':
            return this.read(node.variable);
        case 'event':
            this.hostOps.add('event');
            return `H.event(${JSON.stringify(node.property)})`;
        case 'compound':
            /* A compound in value position is a tuple of stack slots. */
            return `[${node.children.map((child) => this.expr(child)).join(', ')}]`;
        case 'operation':
            return this.operation(node);
        case 'proc':
            return this.procCall(node);
        case 'clientscript':
            return this.hookCall(node);
        default:
            throw new Cs2EmitError(`unknown expression '${node.kind}'`, this.ast.id);
        }
    }

    read(variable) {
        if( variable.local ) return this.names.get(variableKey(variable));
        const accessor = GLOBAL_ACCESSORS.get(variable.kind);
        if( !accessor )
            throw new Cs2EmitError(`no accessor for global '${variable.kind}'`, this.ast.id);
        this.hostOps.add(accessor.get);
        return `H.${accessor.get}(${variable.id})`;
    }

    operation(node) {
        const args = node.arguments || [];

        /* Short-circuit operators: JS has them, with the same semantics. */
        if( node.opcode === OP_SS_AND || node.opcode === OP_SS_OR )
        {
            const operator = node.opcode === OP_SS_AND ? '&&' : '||';
            return `(${args.map((a) => this.expr(a)).join(` ${operator} `)})`;
        }

        /* Comparisons. Both sides are already 32-bit, so the JS operator is
         * exact — unlike arithmetic, which never becomes an operator here. */
        if( node.branchInfix && args.length === 2 )
        {
            const operator = BRANCH_OPERATORS.get(node.branchInfix);
            if( !operator )
                throw new Cs2EmitError(`unknown comparison '${node.branchInfix}'`, this.ast.id);
            return `(${this.expr(args[0])} ${operator} ${this.expr(args[1])})`;
        }

        switch( node.opcode )
        {
        case OP_PUSH_CONSTANT_NULL:
            return 'null';

        case OP_JOIN_STRING:
            return `K.join(${this.argList(args)})`;

        case OP_DEFINE_ARRAY: {
            /* `def_int $arr(size)` — the first argument names the handle, the
             * second sizes it. This is a definition, so it is a store. */
            const [target, size] = args;
            const stackType = target.variable ? target.variable.stackType : 'int';
            return `K.defineArray(${this.expr(size)}, ${JSON.stringify(stackType)})`;
        }

        case OP_PUSH_ARRAY_INT:
            return `K.arrayGet(${this.expr(args[0])}, ${this.expr(args[1])})`;

        case OP_POP_ARRAY_INT:
            return `K.arraySet(${this.expr(args[0])}, ${this.expr(args[1])}, ${this.expr(args[2])})`;

        case OP_ARRAY_SORT_ALL:
            return `K.arraySortAll(${this.expr(args[0])}, ${this.expr(args[1])})`;

        default:
            break;
        }

        const intrinsic = INTRINSICS.get(node.opcode);
        if( intrinsic )
        {
            const slots = args.reduce((total, arg) => total + stackSlotCount(arg), 0);
            if( slots !== intrinsic.arity )
                throw new Cs2EmitError(
                    `${node.name} takes ${intrinsic.arity} values, tree supplies ${slots}`,
                    this.ast.id);
            return `K.${intrinsic.fn}(${this.argList(args)})`;
        }

        return this.hostCall(node, args);
    }

    /**
     * A host operation.
     *
     * `dot` is the `.cc_*` form, which targets the dot component rather than
     * the active one; it is a distinct method so the host is not passed a flag
     * it would have to branch on at every call.
     */
    hostCall(node, args) {
        const method = hostMethodName(node);
        this.hostOps.add(method);
        const call = `H.${method}(${this.argList(args)})`;

        const parkClass = PARK_CLASS_BY_OPCODE.get(node.opcode);
        if( !parkClass ) return call;

        this.parkClasses.add(parkClass);
        /*
         * The retry loop, hoisted into a statement.
         *
         * `yield` must be the whole of a statement to suspend cleanly, so a
         * parking call cannot stay inside the expression that wanted it. The
         * value lands in a temporary and the expression reads that instead —
         * which also fixes the evaluation order, since the loop runs where the
         * call appeared rather than wherever the surrounding expression got to.
         */
        const temp = this.newTemp();
        this.prelude.push(`let ${temp};`);
        this.prelude.push(`while ((${temp} = ${call}) === PARK) yield;`);
        return temp;
    }

    procCall(node) {
        this.procs.add(node.scriptId);
        const args = this.argList(node.arguments || []);
        /*
         * `yield*` rather than a call: a proc is a generator too, so its own
         * parking suspends this frame as well. That is the C behaviour — a
         * yield unwinds to the task, not to the caller's frame — expressed
         * without a stack of our own.
         */
        return `(yield* ${scriptFunctionName(node.scriptId)}(H${args ? `, ${args}` : ''}))`;
    }

    hookCall(node) {
        const method = hostMethodName(node);
        this.hostOps.add(method);
        if( node.scriptId !== -1 ) this.hooks.add(node.scriptId);

        const args = this.argList(node.arguments || []);
        const triggers = this.argList(node.triggers || []);
        const binding = `K.hook(${node.scriptId}, [${args}], [${triggers}])`;
        const target = node.component ? `, ${this.expr(node.component)}` : '';
        return `H.${method}(${binding}${target})`;
    }

    /* --------------------------------------------------------------
     * Helpers
     * ----------------------------------------------------------- */

    /**
     * An argument list, where one expression may fill several slots.
     *
     * `scale(~script5787, 32)` is a real call in the cache: the proc returns
     * two ints, so two argument *nodes* fill scale's three argument *slots*.
     * The tree counts nodes and the callee counts slots, and the two are only
     * the same number when nothing multi-valued appears. A multi-valued
     * expression returns an array here, so spreading it is both the correct
     * lowering and the one that keeps evaluation order.
     */
    argList(args) {
        return args
            .map((arg) => (stackSlotCount(arg) === 1 ? this.expr(arg) : `...(${this.expr(arg)})`))
            .join(', ');
    }

    /** Run `build`, collecting any statements it needed alongside its code. */
    capture(build) {
        const saved = this.prelude;
        this.prelude = [];
        const code = build();
        const prelude = this.prelude;
        this.prelude = saved;
        return { code, prelude };
    }

    newTemp() {
        return `t${this.temp++}`;
    }
}

const BRANCH_OPERATORS = new Map([
    ['=', '==='],
    ['!', '!=='],
    ['<', '<'],
    ['>', '>'],
    ['<=', '<='],
    ['>=', '>='],
]);

/**
 * The host method a command becomes.
 *
 * The command's dialect name is the identity — it is what the cache, the
 * decompiler and this tree's own vocabulary all call the operation — so the
 * host surface is keyed by it rather than by an opcode number nobody reads.
 * An opcode the name table has nothing for keeps its number, which is what the
 * decompiler already prints (`_8026`).
 */
function hostMethodName(node) {
    const base = node.name && !node.name.startsWith('_') ? node.name : `op${node.opcode}`;
    return node.dot ? `dot_${base}` : base;
}

function constantCode(node) {
    if( node.stackType === 'string' ) return JSON.stringify(node.value);
    /* The source spelling rides along as a comment where it says something the
     * number does not — an enum member, a component id, a colour. */
    const literal = node.literal;
    const comment = literal && literal !== String(node.value) ? ` /* ${literal} */` : '';
    return `${node.value}${comment}`;
}

/**
 * A stable JavaScript name for every local the script uses.
 *
 * The source spelling (`$component3`) is the readable one and is used wherever
 * it is unambiguous. It is not always: the inferred identifier belongs to the
 * typing, and an int slot and a string slot can carry the same one — the quest
 * list has `$length1` as both an array handle and an int. So the table is
 * built first, over every variable in the tree, and only the names that
 * actually collide get their bank appended.
 */
function buildNameTable(ast) {
    const variables = new Map();
    collectVariables(ast, variables);

    const byPreferred = new Map();
    for( const [key, variable] of variables )
    {
        const preferred = preferredName(variable);
        if( !byPreferred.has(preferred) ) byPreferred.set(preferred, []);
        byPreferred.get(preferred).push(key);
    }

    const names = new Map();
    for( const [preferred, keys] of byPreferred )
    {
        if( keys.length === 1 && !RESERVED.has(preferred) )
        {
            names.set(keys[0], preferred);
            continue;
        }
        for( const key of keys )
        {
            const variable = variables.get(key);
            names.set(key, `${preferred}_${bankSuffix(variable.kind)}`);
        }
    }
    return names;
}

function preferredName(variable) {
    const identifier = variable.identifier || variable.kind;
    const safe = identifier.replace(/[^A-Za-z0-9_]/g, '_');
    return `$${safe}${variable.id}`;
}

function bankSuffix(kind) {
    return localBank(kind) === 'string' ? 's' : 'i';
}

/**
 * Which local bank a variable lives in.
 *
 * An ARRAY and the STRING of the same index are ONE SLOT, not two. A rev-239
 * array is a handle held in a string local, so `~quicksort_questlist($string1)`
 * and `$lengtharray1($i)` in the same script address the same storage — the
 * decompiler prints both spellings because the bytecode uses both, one for the
 * handle and one for its elements. Keying them apart would declare the array
 * under one name and pass an undeclared variable under the other, which is
 * silent at emit time and a ReferenceError on the first sort.
 */
function localBank(kind) {
    return kind === 'string' || kind === 'array' ? 'string' : 'int';
}

function variableKey(variable) {
    return `${localBank(variable.kind)}:${variable.id}`;
}

function collectVariables(node, out) {
    if( Array.isArray(node) )
    {
        for( const item of node ) collectVariables(item, out);
        return;
    }
    if( !node || typeof node !== 'object' ) return;
    if( node.kind && node.id !== undefined && node.local !== undefined )
    {
        if( node.local ) out.set(variableKey(node), node);
        return;
    }
    for( const value of Object.values(node) ) collectVariables(value, out);
}

/**
 * How many stack slots an expression leaves behind.
 *
 * One for anything that names a single value, and whatever the callee's
 * signature says for a call — a proc declared to return two ints occupies two
 * argument positions at every call site. A compound is the sum of its parts.
 * This is the number the CS2 stack machine counted; the tree's node count is
 * a different number that usually happens to agree.
 */
function stackSlotCount(node) {
    if( !node ) return 0;
    switch( node.kind )
    {
    case 'operation':
    case 'proc':
        return (node.stackTypes || []).length;
    case 'compound':
        return (node.children || []).reduce((total, child) => total + stackSlotCount(child), 0);
    case 'clientscript':
        /* A hook registration is a statement; it leaves nothing. */
        return 0;
    default:
        return 1;
    }
}

function pad(depth) {
    return '    '.repeat(depth);
}

function indent(lines, depth) {
    return lines.map((line) => pad(depth) + line);
}
