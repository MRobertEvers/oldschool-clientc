import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import {
    existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

import { createWasmCS2Runtime } from '../src/wasm_runtime.js';
import moduleFactory from '../web/cs2vm_wasm.js';

const CP1252_SPECIAL = new Map([
    [0x20ac, 0x80], [0x201a, 0x82], [0x0192, 0x83], [0x201e, 0x84],
    [0x2026, 0x85], [0x2020, 0x86], [0x2021, 0x87], [0x02c6, 0x88],
    [0x2030, 0x89], [0x0160, 0x8a], [0x2039, 0x8b], [0x0152, 0x8c],
    [0x017d, 0x8e], [0x2018, 0x91], [0x2019, 0x92], [0x201c, 0x93],
    [0x201d, 0x94], [0x2022, 0x95], [0x2013, 0x96], [0x2014, 0x97],
    [0x02dc, 0x98], [0x2122, 0x99], [0x0161, 0x9a], [0x203a, 0x9b],
    [0x0153, 0x9c], [0x017e, 0x9e], [0x0178, 0x9f],
]);

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const REPO = resolve(CS2DOM, '../..');
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const SOURCE = join(CS2DOM, 'src', 'cs2_vm_core.ts');
const WASM = join(CS2DOM, 'web', 'cs2vm_wasm.wasm');

assert(existsSync(TSC), `TypeScript compiler is unavailable at ${TSC}`);

/* Compile the real TypeScript module, including its generated semantics
 * dependency. The temporary package boundary makes Node execute the emitted
 * JavaScript as ESM without adding checked-in build products. */
const compiled = mkdtempSync(join(tmpdir(), 'cs2dom-vm-core-'));
let core;
let generated;
try {
    writeFileSync(join(compiled, 'package.json'), '{"type":"module"}\n');
    const result = spawnSync(TSC, [
        '--strict',
        '--target', 'ES2020',
        '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext',
        '--skipLibCheck',
        '--rootDir', join(CS2DOM, 'src'),
        '--outDir', compiled,
        SOURCE,
    ], { cwd: CS2DOM, encoding: 'utf8' });
    assert.equal(result.status, 0,
        `TypeScript CS2 core did not compile:\n${result.stdout}${result.stderr}`);
    core = await import(pathToFileURL(join(compiled, 'cs2_vm_core.js')).href);
    generated = await import(pathToFileURL(
        join(compiled, 'generated', 'cs2_opcode_semantics.js')).href);

    runSemanticTests(core, generated);
    await runNativeDifferentialTests(core, generated);
} finally {
    rmSync(compiled, { recursive: true, force: true });
}

console.log('TypeScript CS2 VM core tests passed');

function op(opcode, intOperand = 0, stringOperand = null) {
    return Object.freeze({ opcode, intOperand, stringOperand });
}

function script(instructions, fields = {}) {
    return Object.freeze({
        id: fields.id ?? 1,
        name: fields.name ?? 'fixture',
        instructions: Object.freeze(instructions),
        intLocalCount: fields.intLocalCount ?? 0,
        stringLocalCount: fields.stringLocalCount ?? 0,
        intArgumentCount: fields.intArgumentCount ?? 0,
        stringArgumentCount: fields.stringArgumentCount ?? 0,
        switchTables: fields.switchTables ?? [],
    });
}

function execute(coreModule, instructions, fields = {}, options = {}, cycleLimit) {
    const vm = new coreModule.CS2CoreVM(script(instructions, fields), options);
    const result = vm.run(cycleLimit);
    return { vm, result };
}

function intProgram(lhs, rhs, opcode) {
    return [op(0, lhs), op(0, rhs), op(opcode), op(21)];
}

function predicateProgram(opcode, lhs, rhs) {
    /* C increments pc before adding a branch operand. True jumps from the
     * following pc (3) to PUSH 1 (5); false pushes 0 then skips PUSH 1. */
    return [
        op(0, lhs), op(0, rhs), op(opcode, 2),
        op(0, 0), op(6, 1), op(0, 1), op(21),
    ];
}

function runSemanticTests(coreModule, generatedModule) {
    assert.deepEqual(
        coreModule.CS2_CORE_IMPLEMENTED_OPCODES,
        generatedModule.CS2_CORE_DISPATCH_DECLARATIONS.map(({ opcode }) => opcode),
        'executable dispatch drifted from the generated declarations',
    );
    assert.deepEqual(
        Object.keys(coreModule.CS2_CORE_INTRINSICS).sort(),
        [...new Set(generatedModule.CS2_CORE_DISPATCH_DECLARATIONS
            .map(({ intrinsic }) => intrinsic))].sort(),
        'a generated intrinsic is missing its handwritten TypeScript implementation',
    );

    const unsupportedScript = script([op(0, 9), op(9999), op(7777), op(9999), op(21)]);
    assert.deepEqual(coreModule.analyzeCS2CoreScript(unsupportedScript), {
        supported: false,
        dialect: 'canonical',
        unsupportedOpcodes: [7777, 9999],
        missingScriptIds: [],
        scriptCount: 1,
    });
    assert.throws(() => new coreModule.CS2CoreVM(unsupportedScript), (error) =>
        error instanceof coreModule.CS2CoreUnsupportedOpcodeError &&
        error.opcodes.join(',') === '7777,9999');

    const externalCalls = [];
    const externalExecutor = {
        implementedOpcodes: new Set([9000]),
        execute(state, instruction) {
            externalCalls.push({ pc: state.currentPc, opcode: instruction.opcode });
            state.intStack.push((state.intStack.pop() ?? 0) + 5);
        },
    };
    const externalScript = script([
        op(0, 7), op(9000), op(21),
    ], { id: 900 });
    assert.deepEqual(coreModule.analyzeCS2CoreScript(
        externalScript, 'canonical', undefined, externalExecutor.implementedOpcodes), {
        supported: true,
        dialect: 'canonical',
        unsupportedOpcodes: [],
        missingScriptIds: [],
        scriptCount: 1,
    }, 'only an explicitly injected external implementation admitted the opcode');
    assert.equal(coreModule.analyzeCS2CoreScript(externalScript).supported, false,
        'external catalogue knowledge leaked into core preflight');
    const externalVm = new coreModule.CS2CoreVM(externalScript, {
        externalOpcodeExecutor: externalExecutor,
    });
    const externalResult = externalVm.run();
    assert.equal(externalResult.status, 'done');
    assert.equal(externalResult.cycles, 3,
        'external dispatch did not share exact core instruction accounting');
    assert.deepEqual(externalVm.state.intStack, [12]);
    assert.deepEqual(externalCalls, [{ pc: 1, opcode: 9000 }]);
    assert.throws(() => new coreModule.CS2CoreVM(externalScript), (error) =>
        error instanceof coreModule.CS2CoreUnsupportedOpcodeError &&
        error.opcodes.join(',') === '9000',
    'a missing executor did not fail before execution');

    const externalCallee = script([op(9000), op(21)], { id: 902 });
    const externalCaller = script([op(0, 1), op(40, 902), op(21)], { id: 901 });
    const externalClosure = new coreModule.CS2CoreVM(externalCaller, {
        scripts: [externalCallee], externalOpcodeExecutor: externalExecutor,
    });
    assert.equal(externalClosure.run().status, 'done');
    assert.deepEqual(externalClosure.state.intStack, [6],
        'opcode-40 child did not retain the session-wide external executor');

    for( const [name, execute, message] of [
        ['throw', () => { throw new Error('host exploded'); }, 'host exploded'],
        ['yield', () => 'yielded', 'did not complete synchronously'],
        ['async', () => Promise.resolve(), 'did not complete synchronously'],
    ] ) {
        const vm = new coreModule.CS2CoreVM(externalScript, {
            externalOpcodeExecutor: {
                implementedOpcodes: new Set([9000]), execute,
            },
        });
        const result = vm.run();
        assert.equal(result.status, 'error', `${name} external opcode did not fail closed`);
        assert.equal(result.error.code, 'EXTERNAL_OPCODE_FAILED');
        assert.equal(result.error.pc, 1);
        assert.equal(result.error.opcode, 9000);
        assert.match(result.error.message, new RegExp(message));
        assert.equal(result.cycles, 2,
            `${name} external opcode was counted more than once`);
    }
    assert.deepEqual(coreModule.analyzeCS2CoreScript(script([op(63), op(21)]))
        .unsupportedOpcodes, [63],
    'PUSH_CONSTANT_NULL must remain fail-closed until executable null semantics are proven');

    const arithmeticCases = [
        ['ADD wraps i32', intProgram(0x7fffffff, 1, 4000), -0x80000000],
        ['SUB preserves operand order', intProgram(7, 12, 4001), -5],
        ['MULTIPLY uses low i32 product', intProgram(0x40000000, 4, 4002), 0],
        ['DIV truncates toward zero', intProgram(-7, 2, 4003), -3],
        ['MOD keeps the dividend sign', intProgram(-7, 3, 4011), -1],
    ];
    for( const [name, instructions, expected] of arithmeticCases ) {
        const { vm, result } = execute(coreModule, instructions);
        assert.equal(result.status, 'done', `${name} did not finish`);
        assert.deepEqual(vm.state.intStack, [expected], name);
        assert.equal(result.cycles, instructions.length, `${name} cycle count drifted`);
    }

    const integerLocal = execute(coreModule, [
        op(0, -123456789), op(34, 7), op(33, 7), op(21),
    ]);
    assert.equal(integerLocal.result.status, 'done');
    assert.deepEqual(integerLocal.vm.state.intStack, [-123456789]);

    const emptyIntegerLocal = execute(coreModule, [op(33, 900), op(21)]);
    assert.deepEqual(emptyIntegerLocal.vm.state.intStack, [0],
        'an unwritten C frame integer local is not zero');

    const stringLocal = execute(coreModule, [
        op(3, 0, 'Bank of Gielinor'), op(36, 11), op(35, 11), op(21),
    ]);
    assert.equal(stringLocal.result.status, 'done');
    assert.deepEqual(stringLocal.vm.state.stringStack, ['Bank of Gielinor']);

    const emptyStringLocal = execute(coreModule, [op(35, 777), op(21)]);
    assert.deepEqual(emptyStringLocal.vm.state.stringStack, [''],
        'an unwritten C frame string local is not empty');

    const predicates = [
        [7, 4, 5, 1, 'not equals true'],
        [7, 4, 4, 0, 'not equals false'],
        [8, 4, 4, 1, 'equals true'],
        [8, 4, 5, 0, 'equals false'],
        [9, -2, 1, 1, 'less-than true'],
        [9, 2, 1, 0, 'less-than false'],
        [10, 2, 1, 1, 'greater-than true'],
        [10, -2, 1, 0, 'greater-than false'],
        [31, 2, 2, 1, 'less-than-or-equal true'],
        [31, 3, 2, 0, 'less-than-or-equal false'],
        [32, 2, 2, 1, 'greater-than-or-equal true'],
        [32, 1, 2, 0, 'greater-than-or-equal false'],
    ];
    for( const [opcode, lhs, rhs, expected, name] of predicates ) {
        const { vm, result } = execute(coreModule, predicateProgram(opcode, lhs, rhs));
        assert.equal(result.status, 'done', name);
        assert.deepEqual(vm.state.intStack, [expected], name);
    }

    const unconditional = execute(coreModule, [
        op(6, 1), op(0, 99), op(0, 7), op(21),
    ]);
    assert.deepEqual(unconditional.vm.state.intStack, [7]);

    const divideByZero = execute(coreModule, intProgram(7, 0, 4003));
    assert.equal(divideByZero.result.status, 'error');
    assert.equal(divideByZero.result.error.code, 'DIVIDE_BY_ZERO');
    assert.equal(divideByZero.result.error.pc, 2);
    assert.equal(divideByZero.result.error.opcode, 4003);
    assert.deepEqual(divideByZero.vm.state.intStack, [],
        'malformed arithmetic no longer preserves C pop order');

    const underflow = execute(coreModule, [op(4000), op(21)]);
    assert.equal(underflow.result.error.code, 'INT_STACK_UNDERFLOW');

    const negativePc = execute(coreModule, [op(6, -2), op(21)]);
    assert.equal(negativePc.result.error.code, 'INVALID_PC');

    const loop = execute(coreModule, [op(6, -1)], {}, {}, 8);
    assert.equal(loop.result.status, 'error');
    assert.equal(loop.result.error.code, 'CYCLE_LIMIT');
    assert.equal(loop.result.cycles, 8);

    const joined = execute(coreModule, [
        op(3, 0, 'The '), op(3, 0, 'Bank '), op(3, 0, 'of Gielinor'), op(37, 3), op(21),
    ]);
    assert.deepEqual(joined.vm.state.stringStack, ['The Bank of Gielinor']);
    const emptyJoin = execute(coreModule, [op(37, 0), op(21)]);
    assert.deepEqual(emptyJoin.vm.state.stringStack, ['']);
    const discards = execute(coreModule, [
        op(0, 99), op(38), op(3, 0, 'unused'), op(39), op(0, 7), op(21),
    ]);
    assert.deepEqual(discards.vm.state.intStack, [7]);
    assert.deepEqual(discards.vm.state.stringStack, []);

    const callee = script([
        op(35, 0), op(35, 1), op(37, 2), op(39),
        op(33, 0), op(33, 1), op(4000), op(21),
    ], {
        id: 2,
        name: 'callee',
        intLocalCount: 2,
        stringLocalCount: 2,
        intArgumentCount: 2,
        stringArgumentCount: 2,
    });
    const caller = script([
        op(0, 7), op(0, 9), op(3, 0, 'left'), op(3, 0, 'right'), op(40, 2), op(21),
    ], { id: 1, name: 'caller' });
    const closure = coreModule.analyzeCS2CoreScript(caller, 'canonical', [callee]);
    assert.deepEqual(closure, {
        supported: true,
        dialect: 'canonical',
        unsupportedOpcodes: [],
        missingScriptIds: [],
        scriptCount: 2,
    });
    const called = new coreModule.CS2CoreVM(caller, { scripts: [callee] });
    assert.equal(called.run().status, 'done');
    assert.deepEqual(called.state.intStack, [16]);
    assert.deepEqual(called.state.stringStack, []);
    assert.equal(called.state.frames.length, 0);
    const missingClosure = coreModule.analyzeCS2CoreScript(caller);
    assert.deepEqual(missingClosure.missingScriptIds, [2]);
    assert.throws(() => new coreModule.CS2CoreVM(caller), (error) =>
        error instanceof coreModule.CS2CoreUnsupportedOpcodeError &&
        error.missingScriptIds.join(',') === '2');
    const unsupportedCallee = script([op(9998), op(21)], { id: 2 });
    const rejectedClosure = coreModule.analyzeCS2CoreScript(
        caller, 'canonical', [unsupportedCallee]);
    assert.deepEqual(rejectedClosure.unsupportedOpcodes, [9998]);
    assert.equal(rejectedClosure.scriptCount, 2);
    assert.throws(() => new coreModule.CS2CoreVM(caller, { scripts: [unsupportedCallee] }),
        (error) => error instanceof coreModule.CS2CoreUnsupportedOpcodeError &&
            error.opcodes.join(',') === '9998');

    const intArray = execute(coreModule, [
        op(0, 3), op(44, (2 << 16) | 105),
        op(0, 1), op(0, 77), op(46, 2),
        op(0, 0), op(45, 2), op(0, 1), op(45, 2), op(21),
    ], { stringLocalCount: 3 });
    assert.deepEqual(intArray.vm.state.intStack, [-1, 77]);
    const stringArray = execute(coreModule, [
        op(0, 2), op(44, (1 << 16) | 115),
        op(0, 1), op(3, 0, 'Rune'), op(46, 1),
        op(0, 0), op(45, 1), op(0, 1), op(45, 1), op(21),
    ], { stringLocalCount: 2 });
    assert.deepEqual(stringArray.vm.state.stringStack, ['', 'Rune']);
    const clampedArray = execute(coreModule, [
        op(0, 99_999), op(44, 105), op(0, 5_000), op(45, 0), op(21),
    ], { stringLocalCount: 1 });
    assert.deepEqual(clampedArray.vm.state.intStack, [0]);

    const switchFields = { switchTables: [[{ key: 7, targetPc: 2 }]] };
    const switchHit = execute(coreModule, [
        op(0, 7), op(60, 0), op(0, 0), op(6, 1), op(0, 1), op(21),
    ], switchFields);
    assert.deepEqual(switchHit.vm.state.intStack, [1]);
    const switchMiss = execute(coreModule, [
        op(0, 8), op(60, 0), op(0, 0), op(6, 1), op(0, 1), op(21),
    ], switchFields);
    assert.deepEqual(switchMiss.vm.state.intStack, [0]);

    const pureMath = [
        ['INTERPOLATE', [op(0, 10), op(0, 30), op(0, 0), op(0, 100), op(0, 25), op(4006)], 15],
        ['SETBIT', [op(0, 8), op(0, 1), op(4008)], 10],
        ['TESTBIT', [op(0, 8), op(0, 3), op(4010)], 1],
        ['MIN', [op(0, -4), op(0, 7), op(4016)], -4],
        ['MAX', [op(0, -4), op(0, 7), op(4017)], 7],
        ['SCALE/int64', [op(0, 2_000_000_000), op(0, 2_000_000_000), op(0, 2), op(4018)], 2],
        ['GETBIT_RANGE', [op(0, 0b110110), op(0, 1), op(0, 3), op(4029)], 0b011],
        ['MOVECOORD/wrap', [
            op(0, 0x3fff), op(0, 1), op(0, 2), op(0, 3), op(3325),
        ], 0x20008002],
        ['POW', [op(0, -3), op(0, 5), op(4012)], -243],
        ['POW/fraction truncates', [op(0, 2), op(0, -3), op(4012)], 0],
        ['POW/positive overflow saturates', [op(0, 46341), op(0, 2), op(4012)], 0x7fffffff],
        ['ON_MOBILE/desktop', [op(6518)], 0],
        ['CLIENTTYPE/desktop', [op(6519)], 10],
    ];
    for( const [name, prefix, expected] of pureMath ) {
        const value = execute(coreModule, [...prefix, op(21)]);
        assert.equal(value.result.status, 'done', name);
        assert.deepEqual(value.vm.state.intStack, [expected], name);
    }

    const pureStrings = [
        ['APPEND/order', [op(3, 0, 'Bank '), op(3, 0, 'of Gielinor'), op(4101)],
            'Bank of Gielinor'],
        ['LOWERCASE/ascii-only', [op(3, 0, 'AbC €Ÿ'), op(4103)], 'abc €Ÿ'],
        ['TOSTRING/i32', [op(0, -2147483648), op(4106)], '-2147483648'],
        ['ESCAPE/client tags', [op(3, 0, '<col=ff00ff>A>B</col>'), op(4111)],
            '<lt>col=ff00ff<gt>A<gt>B<lt>/col<gt>'],
        ['SUBSTRING/clamps', [op(3, 0, 'Gielinor'), op(0, -4), op(0, 4), op(4118)],
            'Giel'],
        ['REMOVETAGS/state machine', [op(3, 0, 'a<x<y>z>q'), op(4119)], 'az>q'],
    ];
    for( const [name, prefix, expected] of pureStrings ) {
        const value = execute(coreModule, [...prefix, op(21)]);
        assert.equal(value.result.status, 'done', name);
        assert.deepEqual(value.vm.state.stringStack, [expected], name);
    }

    const compareCp1252 = execute(coreModule, [
        op(3, 0, '€'), op(3, 0, '‚'), op(4107), op(21),
    ]);
    assert.deepEqual(compareCp1252.vm.state.intStack, [-1],
        'COMPARE orders decoded strings by original unsigned windows-1252 bytes');
    const stringLength = execute(coreModule, [op(3, 0, 'A€Ÿ'), op(4117), op(21)]);
    assert.deepEqual(stringLength.vm.state.intStack, [3]);
    const indexOf = execute(coreModule, [
        op(3, 0, 'bank bank'), op(3, 0, 'bank'), op(0, 1), op(4121), op(21),
    ]);
    assert.deepEqual(indexOf.vm.state.intStack, [5]);
    const emptyNeedle = execute(coreModule, [
        op(3, 0, 'bank'), op(3, 0, ''), op(0, 0), op(4121), op(21),
    ]);
    assert.deepEqual(emptyNeedle.vm.state.intStack, [-1]);
    const negativeEndSubstring = execute(coreModule, [
        op(3, 0, 'bank'), op(0, 0), op(0, -1), op(4118), op(21),
    ]);
    assert.equal(negativeEndSubstring.result.status, 'done');
    assert.deepEqual(negativeEndSubstring.vm.state.stringStack, ['']);

    const arrayLength = execute(coreModule, [
        op(0, 7), op(44, 105), op(35, 0), op(8003), op(21),
    ], { stringLocalCount: 1 });
    assert.deepEqual(arrayLength.vm.state.intStack, [7]);
    const nonArrayLength = execute(coreModule, [
        op(3, 0, 'not-an-array'), op(8003), op(21),
    ]);
    assert.deepEqual(nonArrayLength.vm.state.intStack, [0]);
}

async function runNativeDifferentialTests(coreModule, generatedModule) {
    if( !existsSync(WASM) ) {
        console.log(`TypeScript CS2 VM native differential skipped (missing ${WASM})`);
        return;
    }

    const cases = [
        { name: 'add/wrap', instructions: intProgram(0x7fffffff, 1, 4000), kind: 'int' },
        { name: 'subtract/order', instructions: intProgram(7, 12, 4001), kind: 'int' },
        { name: 'multiply/wrap', instructions: intProgram(0x40000000, 4, 4002), kind: 'int' },
        { name: 'negative divide', instructions: intProgram(-7, 2, 4003), kind: 'int' },
        { name: 'negative modulo', instructions: intProgram(-7, 3, 4011), kind: 'int' },
        { name: 'int local', instructions: [
            op(0, -123456789), op(34, 7), op(33, 7), op(21),
        ], kind: 'int', intLocalCount: 8 },
        { name: 'unwritten int local', instructions: [op(33, 900), op(21)], kind: 'int' },
        { name: 'string local', instructions: [
            op(3, 0, 'Bank of Gielinor'), op(36, 11), op(35, 11), op(21),
        ], kind: 'string', stringLocalCount: 12 },
        { name: 'unwritten string local', instructions: [op(35, 777), op(21)], kind: 'string' },
        { name: 'relative branch', instructions: [
            op(6, 1), op(0, 99), op(0, 7), op(21),
        ], kind: 'int' },
        { name: 'join string', instructions: [
            op(3, 0, 'Bank '), op(3, 0, 'of '), op(3, 0, 'Gielinor'), op(37, 3), op(21),
        ], kind: 'string' },
        { name: 'discard int', instructions: [
            op(0, 99), op(38), op(0, 7), op(21),
        ], kind: 'int' },
        { name: 'discard string', instructions: [
            op(3, 0, 'unused'), op(39), op(3, 0, 'kept'), op(21),
        ], kind: 'string' },
        { name: 'switch hit', instructions: [
            op(0, 7), op(60, 0), op(0, 0), op(6, 1), op(0, 1), op(21),
        ], kind: 'int', switchTables: [[{ key: 7, targetPc: 2 }]] },
        { name: 'int array', instructions: [
            op(0, 3), op(44, 105), op(0, 1), op(0, 77), op(46, 0),
            op(0, 1), op(45, 0), op(21),
        ], kind: 'int', stringLocalCount: 1 },
        { name: 'string array', instructions: [
            op(0, 2), op(44, 115), op(0, 1), op(3, 0, 'Rune'), op(46, 0),
            op(0, 1), op(45, 0), op(21),
        ], kind: 'string', stringLocalCount: 1 },
        { name: 'interpolate', instructions: [
            op(0, 10), op(0, 30), op(0, 0), op(0, 100), op(0, 25), op(4006), op(21),
        ], kind: 'int' },
        { name: 'set bit', instructions: [op(0, 8), op(0, 1), op(4008), op(21)], kind: 'int' },
        { name: 'test bit', instructions: [op(0, 8), op(0, 3), op(4010), op(21)], kind: 'int' },
        { name: 'minimum', instructions: [op(0, -4), op(0, 7), op(4016), op(21)], kind: 'int' },
        { name: 'maximum', instructions: [op(0, -4), op(0, 7), op(4017), op(21)], kind: 'int' },
        { name: 'scale/int64', instructions: [
            op(0, 2_000_000_000), op(0, 2_000_000_000), op(0, 2), op(4018), op(21),
        ], kind: 'int' },
        { name: 'get bit range', instructions: [
            op(0, 0b110110), op(0, 1), op(0, 3), op(4029), op(21),
        ], kind: 'int' },
        { name: 'move coord', instructions: [
            op(0, 0x3fff), op(0, 1), op(0, 2), op(0, 3), op(3325), op(21),
        ], kind: 'int' },
        { name: 'power', instructions: [op(0, -3), op(0, 5), op(4012), op(21)], kind: 'int' },
        { name: 'power saturation', instructions: [
            op(0, 46341), op(0, 2), op(4012), op(21),
        ], kind: 'int' },
        { name: 'append strings', instructions: [
            op(3, 0, 'Bank '), op(3, 0, 'of Gielinor'), op(4101), op(21),
        ], kind: 'string' },
        { name: 'lowercase', instructions: [
            op(3, 0, 'AbC XYZ'), op(4103), op(21),
        ], kind: 'string' },
        { name: 'to string', instructions: [op(0, -2147483648), op(4106), op(21)], kind: 'string' },
        { name: 'windows-1252 compare', instructions: [
            op(3, 0, '€'), op(3, 0, '‚'), op(4107), op(21),
        ], kind: 'int' },
        { name: 'escape markup', instructions: [
            op(3, 0, 'a<b>c'), op(4111), op(21),
        ], kind: 'string' },
        { name: 'string length', instructions: [
            op(3, 0, 'Gielinor'), op(4117), op(21),
        ], kind: 'int' },
        { name: 'substring', instructions: [
            op(3, 0, 'Gielinor'), op(0, 1), op(0, 5), op(4118), op(21),
        ], kind: 'string' },
        { name: 'substring/negative end', instructions: [
            op(3, 0, 'bank'), op(0, 0), op(0, -1), op(4118), op(21),
        ], kind: 'string' },
        { name: 'remove tags', instructions: [
            op(3, 0, '<col=ff0000>bank</col>'), op(4119), op(21),
        ], kind: 'string' },
        { name: 'string index of string', instructions: [
            op(3, 0, 'bank bank'), op(3, 0, 'bank'), op(0, 1), op(4121), op(21),
        ], kind: 'int' },
        { name: 'on mobile', instructions: [op(6518), op(21)], kind: 'int' },
        { name: 'client type', instructions: [op(6519), op(21)], kind: 'int' },
        { name: 'array length', instructions: [
            op(0, 7), op(44, 105), op(35, 0), op(8003), op(21),
        ], kind: 'int', stringLocalCount: 1 },
    ];
    for( const [opcode, lhs, rhs, _expected, name] of [
        [7, 4, 5, 1, 'not equals'],
        [7, 4, 4, 0, 'not equals/fallthrough'],
        [8, 4, 4, 1, 'equals'],
        [9, -2, 1, 1, 'less than'],
        [10, 2, 1, 1, 'greater than'],
        [31, 2, 2, 1, 'less than or equals'],
        [32, 2, 2, 1, 'greater than or equals'],
    ]) cases.push({ name, instructions: predicateProgram(opcode, lhs, rhs), kind: 'int' });

    const gosubTargetId = 980_001;
    cases.push({
        name: 'gosub/frame parameters',
        instructions: [
            op(0, 7), op(0, 9), op(3, 0, 'left'), op(3, 0, 'right'),
            op(40, gosubTargetId), op(21),
        ],
        kind: 'int',
        dependencies: [{
            id: gosubTargetId,
            name: 'native differential callee',
            instructions: [
                op(35, 0), op(35, 1), op(37, 2), op(39),
                op(33, 0), op(33, 1), op(4000), op(21),
            ],
            intLocalCount: 2,
            stringLocalCount: 2,
            intArgumentCount: 2,
            stringArgumentCount: 2,
        }],
    });
    const arrayGosubTargetId = 980_002;
    cases.push({
        name: 'array handle through gosub',
        instructions: [
            op(0, 2), op(44, 105), op(0, 1), op(0, 91), op(46, 0),
            op(35, 0), op(40, arrayGosubTargetId), op(21),
        ],
        kind: 'int',
        stringLocalCount: 1,
        dependencies: [{
            id: arrayGosubTargetId,
            name: 'array handle callee',
            instructions: [op(0, 1), op(45, 0), op(21)],
            stringLocalCount: 1,
            stringArgumentCount: 1,
        }],
    });

    const captures = new Map();
    const scripts = [];
    const expected = new Map();
    for( let index = 0; index < cases.length; index++ ) {
        const fixture = cases[index];
        const id = 900_000 + index;
        const captureId = 12_000 + index;
        const ts = execute(coreModule, fixture.instructions, {
            id,
            name: fixture.name,
            intLocalCount: fixture.intLocalCount,
            stringLocalCount: fixture.stringLocalCount,
            intArgumentCount: fixture.intArgumentCount,
            stringArgumentCount: fixture.stringArgumentCount,
            switchTables: fixture.switchTables,
        }, { scripts: fixture.dependencies });
        assert.equal(ts.result.status, 'done', `${fixture.name} TypeScript execution failed`);
        const value = fixture.kind === 'string'
            ? ts.vm.state.stringStack.at(-1) : ts.vm.state.intStack.at(-1);
        expected.set(captureId, value);

        assert.equal(fixture.instructions.at(-1).opcode, 21,
            `${fixture.name} must end in RETURN for native capture`);
        const captureOpcode = fixture.kind === 'string' ? 50 : 2;
        const instrumented = [
            ...fixture.instructions.slice(0, -1),
            op(captureOpcode, captureId),
            fixture.instructions.at(-1),
        ];
        scripts.push({
            id,
            data: encodeNativeScript(instrumented, generatedModule, {
                intLocalCount: fixture.intLocalCount,
                stringLocalCount: fixture.stringLocalCount,
                intArgumentCount: fixture.intArgumentCount,
                stringArgumentCount: fixture.stringArgumentCount,
                switchTables: fixture.switchTables,
            }).toString('base64'),
        });
        for( const dependency of fixture.dependencies || [] ) scripts.push({
            id: dependency.id,
            data: encodeNativeScript(dependency.instructions, generatedModule, dependency)
                .toString('base64'),
        });
    }
    const nativeErrorId = 990_000;
    const divideByZeroInstructions = intProgram(7, 0, 4003);
    const tsDivideByZero = execute(coreModule, divideByZeroInstructions, {
        id: nativeErrorId,
        name: 'divide by zero',
    });
    assert.equal(tsDivideByZero.result.error?.opcode, 4003);
    assert.equal(tsDivideByZero.result.error?.pc, 2);
    scripts.push({
        id: nativeErrorId,
        data: encodeNativeScript(divideByZeroInstructions, generatedModule).toString('base64'),
    });

    const host = {
        viewport: { width: 512, height: 334 },
        ref(value) { return value; },
        setActive() {},
        read() { return ''; },
        request(request) {
            if( request.kind === 'POP_VAR' ) {
                captures.set(request.varp_id, request.value | 0);
                return undefined;
            }
            if( request.kind === 'POP_VARC_STRING' ) {
                captures.set(request.varc_id, request.value ?? '');
                return undefined;
            }
            throw new Error(`unexpected native differential HOST request ${request.kind}`);
        },
    };
    const wasmUrl = `data:application/wasm;base64,${readFileSync(WASM).toString('base64')}`;
    const runtime = await createWasmCS2Runtime({
        program: {
            available: true,
            dialect: 'osrs',
            revision: 'osrs239',
            scripts,
        },
        host,
        moduleFactory,
        wasmUrl,
        fastHost: false,
        preloadHostData: false,
    });
    try {
        for( let index = 0; index < cases.length; index++ ) {
            runtime.invokeIntent({
                component: { componentId: 1, subId: -1 },
                hook: { scriptId: 900_000 + index, args: [] },
                locals: {},
            });
            const captureId = 12_000 + index;
            assert.deepEqual(captures.get(captureId), expected.get(captureId),
                `${cases[index].name} differs between TypeScript and production C/WASM`);
        }
        assert.throws(() => runtime.invokeIntent({
            component: { componentId: 1, subId: -1 },
            hook: { scriptId: nativeErrorId, args: [] },
            locals: {},
        }), (error) => error?.opcode === tsDivideByZero.result.error.opcode &&
            error?.pc === tsDivideByZero.result.error.pc,
        'divide-by-zero diagnostics differ between TypeScript and production C/WASM');
    } finally {
        runtime.destroy();
    }
}

function encodeCp1252(text) {
    const bytes = [];
    for( const char of text ) {
        const codepoint = char.codePointAt(0);
        if( (codepoint > 0 && codepoint < 0x80) ||
            (codepoint >= 0xa0 && codepoint <= 0xff) ||
            [0x81, 0x8d, 0x8f, 0x90, 0x9d].includes(codepoint) ) {
            bytes.push(codepoint);
        } else {
            bytes.push(CP1252_SPECIAL.get(codepoint) ?? 0x3f);
        }
    }
    return Buffer.from(bytes);
}

function encodeNativeScript(instructions, generatedModule, {
    intLocalCount = 0,
    stringLocalCount = 0,
    intArgumentCount = 0,
    stringArgumentCount = 0,
    switchTables = [],
} = {}) {
    const semantics = new Map(generatedModule.CS2_OPCODE_SEMANTICS
        .map((semantic) => [semantic.opcode, semantic]));
    /* POP_VAR and POP_VARC_STRING are test-only sinks outside the generated
     * core. Their cache operands are ordinary int32 ids. */
    const operandKind = (opcode) => semantics.get(opcode)?.operand ||
        ([2, 50].includes(opcode) ? 'int32' : null);
    const chunks = [Buffer.from([0])]; // empty script signature
    for( const instruction of instructions ) {
        const opcode = Buffer.alloc(2);
        opcode.writeUInt16BE(instruction.opcode, 0);
        chunks.push(opcode);
        const kind = operandKind(instruction.opcode);
        if( kind === 'string' ) {
            chunks.push(encodeCp1252(instruction.stringOperand ?? ''), Buffer.from([0]));
        } else if( kind === 'int32' ) {
            const operand = Buffer.alloc(4);
            operand.writeInt32BE(instruction.intOperand | 0, 0);
            chunks.push(operand);
        } else if( kind === 'int8' ) {
            const operand = Buffer.alloc(1);
            operand.writeInt8((instruction.intOperand << 24) >> 24, 0);
            chunks.push(operand);
        } else if( kind !== 'none' ) {
            throw new Error(`no cache operand kind for opcode ${instruction.opcode}`);
        }
    }

    /* Modern footer: fixed counts, then switch tables, then their byte length
     * plus the switch-count byte itself. */
    const counts = Buffer.alloc(17);
    counts.writeInt32BE(instructions.length, 0);
    counts.writeUInt16BE(intLocalCount, 4);
    counts.writeUInt16BE(stringLocalCount, 6);
    counts.writeUInt16BE(0, 8);  // local longs
    counts.writeUInt16BE(intArgumentCount, 10);
    counts.writeUInt16BE(stringArgumentCount, 12);
    counts.writeUInt16BE(0, 14); // long arguments
    counts.writeUInt8(switchTables.length, 16);
    chunks.push(counts);

    let switchBytes = 0;
    for( const table of switchTables ) {
        const encoded = Buffer.alloc(2 + table.length * 8);
        encoded.writeUInt16BE(table.length, 0);
        for( let index = 0; index < table.length; index++ ) {
            encoded.writeInt32BE(table[index].key | 0, 2 + index * 8);
            encoded.writeInt32BE(table[index].targetPc | 0, 6 + index * 8);
        }
        switchBytes += encoded.length;
        chunks.push(encoded);
    }
    const trailerLength = Buffer.alloc(2);
    trailerLength.writeUInt16BE(switchBytes + 1, 0);
    chunks.push(trailerLength);
    return Buffer.concat(chunks);
}
