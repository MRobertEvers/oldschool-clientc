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

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const SOURCE = join(CS2DOM, 'src', 'cs2_bytecode_decoder.ts');
const WASM = join(CS2DOM, 'web', 'cs2vm_wasm.wasm');

assert(existsSync(TSC), `TypeScript compiler is unavailable at ${TSC}`);

const compiled = mkdtempSync(join(tmpdir(), 'cs2dom-bytecode-decoder-'));
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
        `TypeScript clientscript decoder did not compile:\n${result.stdout}${result.stderr}`);

    const decoder = await import(pathToFileURL(join(compiled, 'cs2_bytecode_decoder.js')).href);
    const core = await import(pathToFileURL(join(compiled, 'cs2_vm_core.js')).href);
    const wire = await import(pathToFileURL(
        join(compiled, 'generated', 'cs2_wire_opcodes.js')).href);
    const host = await import(pathToFileURL(
        join(compiled, 'generated', 'cs2_host.js')).href);

    const fixtures = runDecodeTests(decoder, core, wire, host);
    await runNativeDecodeDifferential(fixtures);
} finally {
    rmSync(compiled, { recursive: true, force: true });
}

console.log('TypeScript clientscript bytecode decoder tests passed');

function runDecodeTests(decoder, core, wire, host) {
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(0).operand, 'int32');
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(3).operand, 'string');
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(61).operand, 'int64');
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(63).operand, 'int8');
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(3170).operand, 'int8');
    assert.equal(wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(4122).operand, 'int8');
    assert.equal(host.CS2_HOST_REQUEST_METADATA_BY_OPCODE[3170].operand, 'int8');
    for( const row of host.CS2_HOST_REQUEST_METADATA ) {
        const wireRow = wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(row.opcode);
        assert(wireRow, `HOST opcode ${row.opcode} is absent from the wire catalogue`);
        assert.equal(row.operand, wireRow.operand,
            `HOST opcode ${row.opcode} wire width disagrees with the native decoder`);
    }
    assert.equal(wire.CS2_RS2_WIRE_OPCODE_TRANSLATIONS[51], 60);
    assert.equal(wire.CS2_RS2_WIRE_OPCODE_TRANSLATIONS[4500], 6516);

    const calleeBytes = encodeScript(wire, [
        { opcode: 33, intOperand: 0 },
        { opcode: 33, intOperand: 1 },
        { opcode: 4000, intOperand: -128 },
        { opcode: 21, intOperand: -1 },
    ], {
        layout: 'modern', signatureBytes: Buffer.from('sum'),
        intLocalCount: 2, intArgumentCount: 2,
    });
    const callerBytes = encodeScript(wire, [
        { opcode: 0, intOperand: 7 },
        { opcode: 0, intOperand: -12 },
        { opcode: 40, intOperand: 2 },
        { opcode: 60, intOperand: 0 },
        { opcode: 0, intOperand: 0 },
        { opcode: 6, intOperand: 1 },
        { opcode: 0, intOperand: 1 },
        { opcode: 21, intOperand: 0 },
    ], {
        layout: 'modern',
        signatureBytes: Buffer.from([0x65, 0x6e, 0x74, 0x72, 0x79, 0x80]),
        switchTables: [[{ key: -5, targetPc: 2 }]],
    });
    const program = {
        schema: decoder.CS2_BYTECODE_PROGRAM_SCHEMA,
        available: true,
        dialect: 'osrs',
        revision: 'osrs239',
        entries: [1],
        scripts: [
            { id: 1, name: 'bank_entry', data: callerBytes.toString('base64') },
            { id: 2, name: 'sum_helper', data: calleeBytes.toString('base64') },
        ],
    };
    const registry = decoder.decodeCS2BytecodeProgram(program);
    assert.equal(registry.size, 2);
    assert.deepEqual(registry.entryScriptIds, [1]);
    assert.equal(registry.dialect, 'canonical');
    assert.equal(registry.revision, 239);
    assert.equal(typeof registry.set, 'undefined', 'the decoded registry is mutable');

    const caller = registry.get(1);
    const callee = registry.get(2);
    assert(caller && callee);
    assert.equal(caller.signature, 'entry€', 'cache CP-1252 was not converted exactly');
    assert.equal(caller.name, 'bank_entry', 'record identity was replaced by the signature');
    assert.equal(caller.trailerLayout, 'modern');
    assert.deepEqual(caller.switchTables, [[{ key: -5, targetPc: 2 }]]);
    assert.equal(caller.instructions[1].intOperand, -12);
    assert.equal(caller.instructions[2].executionClass, 'core',
        'GOSUB was misclassified as HOST merely because it has a request schema row');
    assert.equal(callee.instructions[2].intOperand, -128);
    assert.equal(callee.instructions[3].intOperand, -1);
    assert.deepEqual(
        [callee.intLocalCount, callee.intArgumentCount], [2, 2],
        'local/argument footer counts drifted',
    );

    const closure = core.analyzeCS2CoreScript(caller, registry.dialect, registry);
    assert.deepEqual(closure, {
        supported: true,
        dialect: 'canonical',
        unsupportedOpcodes: [],
        missingScriptIds: [],
        scriptCount: 2,
    });
    const vm = new core.CS2CoreVM(caller, { dialect: registry.dialect, scripts: registry });
    assert.equal(vm.run().status, 'done');
    assert.deepEqual(vm.state.intStack, [1], 'decoded closure did not execute as decoded');

    const hostBytes = encodeScript(wire, [
        { opcode: 3170, intOperand: -2 },
        { opcode: 21, intOperand: 0 },
    ], { layout: 'modern' });
    const hosted = decoder.decodeCS2ClientScript(hostBytes, {
        id: 3, dialect: 'canonical', revision: 239,
    });
    assert.equal(hosted.instructions[0].intOperand, -2,
        'command operand used stale int32 metadata instead of native signed int8');
    assert.equal(hosted.instructions[0].executionClass, 'host');

    const longBytes = encodeScript(wire, [
        { opcode: 61, longOperand: -0x0102030405060708n },
        { opcode: 62, intOperand: -1 },
        { opcode: 21, intOperand: 0 },
    ], {
        layout: 'modern', longLocalCount: 3, longArgumentCount: 2,
    });
    const longScript = decoder.decodeCS2ClientScript(longBytes.buffer.slice(
        longBytes.byteOffset, longBytes.byteOffset + longBytes.byteLength), {
        id: 4, dialect: 'canonical', revision: 239,
    });
    assert.equal(longScript.instructions[0].longOperand, -0x0102030405060708n);
    assert.equal(longScript.instructions[0].executionClass, 'unsupported');
    assert.deepEqual([longScript.longLocalCount, longScript.longArgumentCount], [3, 2]);

    const rs2SwitchBytes = encodeScript(wire, [
        { opcode: 0, intOperand: -9 },
        { opcode: 51, intOperand: 0 },
        { opcode: 21, intOperand: 0 },
    ], {
        layout: 'legacy', switchTables: [[{ key: -9, targetPc: 0 }]],
    });
    const rs2Switch = decoder.decodeCS2ClientScript(rs2SwitchBytes, {
        id: 5, dialect: 'rs2-dat2', revision: 634,
    });
    assert.equal(rs2Switch.trailerLayout, 'legacy');
    assert.equal(rs2Switch.instructions[1].wireOpcode, 51);
    assert.equal(rs2Switch.instructions[1].opcode, 60);
    assert.equal(rs2Switch.instructions[1].opcodeName, 'GET_VARC_LONG_OR_RS2_SWITCH');

    const rs2HostBytes = encodeScript(wire, [
        { opcode: 4500, intOperand: -3 },
        { opcode: 21, intOperand: 0 },
    ], { layout: 'legacy' });
    const rs2Host = decoder.decodeCS2ClientScript(rs2HostBytes, {
        id: 6, dialect: 'rs2-dat2', revision: 634,
    });
    assert.equal(rs2Host.instructions[0].opcode, 6516);
    assert.equal(rs2Host.instructions[0].intOperand, -3);
    assert.equal(rs2Host.instructions[0].executionClass, 'host');

    /* Match cs2w_decode_script: a metadata-preferred layout may fall back to
     * the other valid footer without accepting a partial parse. */
    const legacyFallback = decoder.decodeCS2ClientScript(rs2HostBytes, {
        id: 7, dialect: 'canonical', revision: 239,
    });
    assert.equal(legacyFallback.trailerLayout, 'legacy');

    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        new Uint8Array([1, 2, 3]), { id: 10, trailer: 'modern' }), 'TRUNCATED');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.alloc(0), Buffer.from([0, 5, 0, 0, 0, 0]), 1, 'modern'),
        { id: 11, dialect: 'canonical', revision: 239 }), 'UNKNOWN_OPCODE_METADATA');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.from([0x41]), Buffer.from([0xff, 0xff, 0x7f]), 1, 'modern', {
            terminateSignature: false,
        }), { id: 12, trailer: 'modern' }), 'BAD_SIGNATURE');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.alloc(0), Buffer.from([0, 3, 0x61, 0x62]), 1, 'modern'),
        { id: 13, trailer: 'modern' }), 'UNTERMINATED_STRING');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.alloc(0), Buffer.from([0, 0, 1, 2, 3]), 1, 'modern'),
        { id: 14, trailer: 'modern' }), 'TRUNCATED');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.alloc(0), Buffer.from([0, 21, 0]), 2, 'modern'),
        { id: 15, trailer: 'modern' }), 'TRUNCATED');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        assembleRaw(Buffer.alloc(0), Buffer.from([0, 21, 0, 0, 21, 0]), 1, 'modern'),
        { id: 16, trailer: 'modern' }), 'BAD_BODY_LENGTH');

    const zeroOps = assembleRaw(Buffer.alloc(0), Buffer.alloc(0), 0, 'modern');
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        zeroOps, { id: 17, trailer: 'modern' }), 'BAD_OPCODE_COUNT');
    const badTrailer = Buffer.from(callerBytes);
    badTrailer.writeUInt16BE(0xffff, badTrailer.length - 2);
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        badTrailer, { id: 18, trailer: 'modern' }), 'BAD_TRAILER');
    const badSwitch = Buffer.from(encodeScript(wire, [{ opcode: 21, intOperand: 0 }], {
        layout: 'modern', switchTables: [[]],
    }));
    const badSwitchTrailer = trailerOffset(badSwitch, 'modern');
    badSwitch.writeUInt16BE(0xffff, badSwitchTrailer + 17);
    assertDecodeError(decoder, () => decoder.decodeCS2ClientScript(
        badSwitch, { id: 19, trailer: 'modern' }), 'TRUNCATED');

    const baseProgram = {
        available: true, dialect: 'osrs', revision: 'osrs239',
        scripts: [{ id: 1, data: callerBytes.toString('base64') }],
    };
    assertDecodeError(decoder, () => decoder.decodeCS2BytecodeProgram({
        ...baseProgram, schema: 'cs2dom-bytecode/999',
    }), 'UNKNOWN_PROGRAM_SCHEMA');
    assertDecodeError(decoder, () => decoder.decodeCS2BytecodeProgram({
        ...baseProgram, dialect: 'imaginary-client',
    }), 'UNKNOWN_DIALECT');
    assertDecodeError(decoder, () => decoder.decodeCS2BytecodeProgram({
        ...baseProgram, scripts: [{ id: 1, data: 'not base64' }],
    }), 'BAD_BASE64');
    assertDecodeError(decoder, () => decoder.decodeCS2BytecodeProgram({
        ...baseProgram, scripts: [...baseProgram.scripts, ...baseProgram.scripts],
    }), 'DUPLICATE_SCRIPT');
    assertDecodeError(decoder, () => decoder.decodeCS2BytecodeProgram({
        ...baseProgram, entries: [999],
    }), 'MISSING_ENTRY_SCRIPT');

    return { program, callerBytes, calleeBytes };
}

async function runNativeDecodeDifferential({ program }) {
    if( !existsSync(WASM) ) {
        console.log(`TypeScript decoder native differential skipped (missing ${WASM})`);
        return;
    }
    const runtime = await createWasmCS2Runtime({
        program,
        moduleFactory,
        wasmUrl: `data:application/wasm;base64,${readFileSync(WASM).toString('base64')}`,
        fastHost: false,
        preloadHostData: false,
        host: {
            viewport: { width: 512, height: 334 },
            ref(value) { return value; },
            setActive() {},
            request(request) { throw new Error(`unexpected HOST request ${request.kind}`); },
        },
    });
    try {
        const result = runtime.invokeIntent({
            component: { componentId: 1, subId: -1 },
            hook: { scriptId: 1, args: [] },
            locals: {},
        });
        assert.equal(result.status, 'done',
            'the production C/rscache decoder rejected the TypeScript fixture');
        /* CS2VM2 resolves GOSUB through its internal request seam even though
         * the adapter satisfies it from the sealed native registry without a
         * JavaScript Host call. */
        assert.equal(result.hostRequests, 1);
    } finally {
        runtime.destroy();
    }
}

function assertDecodeError(decoder, callback, code) {
    assert.throws(callback, (error) =>
        error instanceof decoder.CS2BytecodeDecodeError && error.code === code,
    `expected decoder error ${code}`);
}

function encodeScript(wire, instructions, options = {}) {
    const chunks = [];
    for( const instruction of instructions ) {
        const metadata = wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(instruction.opcode);
        const kind = instruction.operandKind || metadata?.operand;
        assert(kind, `fixture has no operand kind for opcode ${instruction.opcode}`);
        const opcode = Buffer.alloc(2);
        opcode.writeUInt16BE(instruction.opcode, 0);
        chunks.push(opcode);
        if( kind === 'int8' ) {
            const operand = Buffer.alloc(1);
            operand.writeInt8(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else if( kind === 'int32' ) {
            const operand = Buffer.alloc(4);
            operand.writeInt32BE(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else if( kind === 'int64' ) {
            const operand = Buffer.alloc(8);
            operand.writeBigInt64BE(instruction.longOperand || 0n, 0);
            chunks.push(operand);
        } else if( kind === 'string' ) {
            chunks.push(instruction.stringBytes || Buffer.from(instruction.stringOperand || '', 'latin1'));
            chunks.push(Buffer.from([0]));
        }
    }
    return assembleRaw(
        options.signatureBytes || Buffer.from(''), Buffer.concat(chunks), instructions.length,
        options.layout || 'modern', options,
    );
}

function assembleRaw(signatureBytes, body, opCount, layout, options = {}) {
    const signature = options.terminateSignature === false
        ? Buffer.from(signatureBytes) : Buffer.concat([Buffer.from(signatureBytes), Buffer.from([0])]);
    const modern = layout === 'modern';
    const fixed = Buffer.alloc(modern ? 17 : 13);
    fixed.writeInt32BE(opCount, 0);
    fixed.writeUInt16BE(options.intLocalCount || 0, 4);
    fixed.writeUInt16BE(options.stringLocalCount || 0, 6);
    let offset = 8;
    if( modern ) {
        fixed.writeUInt16BE(options.longLocalCount || 0, offset);
        offset += 2;
    }
    fixed.writeUInt16BE(options.intArgumentCount || 0, offset); offset += 2;
    fixed.writeUInt16BE(options.stringArgumentCount || 0, offset); offset += 2;
    if( modern ) {
        fixed.writeUInt16BE(options.longArgumentCount || 0, offset);
        offset += 2;
    }
    const tables = options.switchTables || [];
    fixed.writeUInt8(tables.length, offset);
    const switchChunks = [];
    let switchBytes = 0;
    for( const table of tables ) {
        const encoded = Buffer.alloc(2 + table.length * 8);
        encoded.writeUInt16BE(table.length, 0);
        for( let index = 0; index < table.length; index++ ) {
            encoded.writeInt32BE(table[index].key, 2 + index * 8);
            encoded.writeInt32BE(table[index].targetPc, 6 + index * 8);
        }
        switchChunks.push(encoded);
        switchBytes += encoded.length;
    }
    const length = Buffer.alloc(2);
    length.writeUInt16BE(switchBytes + 1, 0);
    return Buffer.concat([signature, body, fixed, ...switchChunks, length]);
}

function trailerOffset(bytes, layout) {
    const footerSize = layout === 'modern' ? 18 : 14;
    return bytes.length - footerSize - bytes.readUInt16BE(bytes.length - 2);
}
