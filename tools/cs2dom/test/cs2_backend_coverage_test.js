import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

import { compileInterfaceProgram } from '../src/bytecode.js';
import { contentInterfaceCatalog, openContentInterface } from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const REPO = resolve(CS2DOM, '../..');
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const SOURCE = join(CS2DOM, 'src', 'cs2_backend_coverage.ts');
const temporary = mkdtempSync(join(tmpdir(), 'cs2dom-backend-coverage-'));

try {
    writeFileSync(join(temporary, 'package.json'), '{"type":"module"}\n');
    const compiled = join(temporary, 'compiled');
    const result = spawnSync(TSC, [
        '--strict', '--target', 'ES2020', '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext', '--skipLibCheck',
        '--rootDir', join(CS2DOM, 'src'), '--outDir', compiled, SOURCE,
    ], { cwd: CS2DOM, encoding: 'utf8' });
    assert.equal(result.status, 0,
        `TypeScript backend coverage audit did not compile:\n${result.stdout}${result.stderr}`);

    const decoder = await import(pathToFileURL(
        join(compiled, 'cs2_bytecode_decoder.js')).href);
    const coverage = await import(pathToFileURL(
        join(compiled, 'cs2_backend_coverage.js')).href);
    const wire = await import(pathToFileURL(
        join(compiled, 'generated', 'cs2_wire_opcodes.js')).href);

    const records = [
        {
            id: 1,
            name: 'mixed_entry',
            instructions: [
                { opcode: 0, intOperand: 7 },
                { opcode: 40, intOperand: 2 },
                { opcode: 1000, intOperand: 0 },
                { opcode: 61, longOperand: 9n },
                { opcode: 21, intOperand: 0 },
            ],
        },
        {
            id: 2,
            name: 'pure_helper',
            instructions: [
                { opcode: 0, intOperand: 3 },
                { opcode: 21, intOperand: 0 },
            ],
        },
        {
            id: 3,
            name: 'missing_helper_entry',
            instructions: [
                { opcode: 40, intOperand: 99 },
                { opcode: 21, intOperand: 0 },
            ],
        },
        {
            id: 4,
            name: 'pure_entry',
            instructions: [
                { opcode: 0, intOperand: 11 },
                { opcode: 21, intOperand: 0 },
            ],
        },
    ];
    const program = {
        schema: decoder.CS2_BYTECODE_PROGRAM_SCHEMA,
        available: true,
        dialect: 'osrs',
        revision: 'osrs239',
        entries: [1, 3, 4],
        scripts: records.map((record) => ({
            id: record.id,
            name: record.name,
            data: encodeScript(wire, record.instructions).toString('base64'),
        })),
    };
    const registry = decoder.decodeCS2BytecodeProgram(program);
    const audit = coverage.auditCS2BackendRegistry(registry);

    assert.equal(audit.decodeKnown, true);
    assert.equal(audit.registryScriptCount, 4);
    assert.equal(audit.registryInstructionCount, 11);
    assert.equal(audit.reachableScriptCount, 4);
    assert.equal(audit.reachableInstructionCount, 11);
    assert.equal(audit.entryClosureInstructionCount, 11);
    assert.equal(audit.completeClosureCount, 2);
    assert.equal(audit.tsEligibleClosureCount, 1);
    assert.equal(audit.interfaceTsEligible, false);
    assert.deepEqual(audit.missingGosubTargets, [99]);

    const mixed = audit.entryClosures[0];
    assert.deepEqual(mixed.scriptIds, [1, 2]);
    assert.equal(mixed.completeGosubClosure, true);
    assert.equal(mixed.tsEligible, false);
    assert.equal(mixed.instructionCount, 7);
    assert.deepEqual(mixed.unsupportedCoreOpcodes.map(({ opcode }) => opcode), [61]);
    assert.deepEqual(mixed.schemaOnlyHostOpcodes, []);
    assert.deepEqual(mixed.unimplementedHostOpcodes, []);
    const setPosition = audit.opcodeFrequencies.find(({ opcode }) => opcode === 1000);
    assert.equal(setPosition.decodeKnown, true);
    assert.equal(setPosition.reviewClass, 'host-reviewed');
    assert.equal(setPosition.executableReviewed, true);
    assert.equal(mixed.tsEligible, false,
        'an unsupported core opcode was incorrectly ignored after Host integration');
    assert.deepEqual(audit.entryClosures[1].missingGosubTargets, [99]);
    assert.equal(audit.entryClosures[2].tsEligible, true);

    const gosub = audit.opcodeFrequencies.find(({ opcode }) => opcode === 40);
    assert.deepEqual(gosub && {
        count: gosub.count,
        reviewClass: gosub.reviewClass,
        executableReviewed: gosub.executableReviewed,
    }, { count: 2, reviewClass: 'core-reviewed', executableReviewed: true });

    const aggregate = coverage.aggregateCS2BackendCoverage([audit, audit]);
    assert.equal(aggregate.registryInstructionCount, 22);
    assert.equal(aggregate.reachableInstructionCount, 22);
    assert.equal(aggregate.entryClosureCount, 6);
    assert.equal(aggregate.tsEligibleClosureCount, 2);
    assert.equal(aggregate.opcodeFrequencies.find(({ opcode }) => opcode === 21).count, 8);
    assert(Object.isFrozen(audit) && Object.isFrozen(audit.entryClosures));

    const dynamicHookProgram = {
        schema: decoder.CS2_BYTECODE_PROGRAM_SCHEMA,
        available: true,
        dialect: 'osrs',
        revision: 'osrs239',
        entries: [5],
        scripts: [{
            id: 5,
            name: 'dynamic_hook_entry',
            data: encodeScript(wire, [
                { opcode: 1400 }, { opcode: 21 },
            ]).toString('base64'),
        }],
    };
    const dynamicHookAudit = coverage.auditCS2BackendRegistry(
        decoder.decodeCS2BytecodeProgram(dynamicHookProgram));
    assert.equal(dynamicHookAudit.interfaceTsEligible, false);
    assert.deepEqual(dynamicHookAudit.unresolvedDynamicHookOpcodes.map(
        ({ opcode }) => opcode), [1400]);
    assert.deepEqual(dynamicHookAudit.unresolvedDynamicHookSourceScriptIds, [5]);
    assert.equal(dynamicHookAudit.entryClosures[0].tsEligible, false);

    const unloadedGroupProgram = {
        ...dynamicHookProgram,
        entries: [6],
        scripts: [{
            id: 6,
            name: 'group_loading_entry',
            data: encodeScript(wire, [
                { opcode: 100 }, { opcode: 21 },
            ]).toString('base64'),
        }],
    };
    const unloadedGroupAudit = coverage.auditCS2BackendRegistry(
        decoder.decodeCS2BytecodeProgram(unloadedGroupProgram));
    assert.equal(unloadedGroupAudit.interfaceTsEligible, false);
    assert.deepEqual(unloadedGroupAudit.unresolvedInterfaceGroupOpcodes.map(
        ({ opcode }) => opcode), [100]);
    assert.equal(unloadedGroupAudit.entryClosures[0].tsEligible, false);

    /* Exercise the same real Dat2 input used by audit:ts-backend. ca_tasks was
     * the regression where onLoad installed a script absent from the admitted
     * opcode-40/static-hook closure, then the first tick failed at runtime. */
    const cache = join(REPO, 'cache.osrs239');
    if( existsSync(join(cache, 'main_file_cache.dat2')) ) {
        const project = prepareDat2Project({
            cache,
            content: join(REPO, 'OSRS-Content', 'osrs239-content'),
            unpackedContent: join(REPO, 'OSRS-Content', 'osrs239-content'),
            revision: 'osrs239',
        }, { log: () => {} });
        const record = contentInterfaceCatalog(project.dat2Content, { source: 'dat2' })
            .find(({ name }) => name === 'ca_tasks');
        assert(record, 'real Dat2 catalog is missing ca_tasks');
        const opened = openContentInterface(project.dat2Content, record.name, {
            source: 'dat2',
        });
        const caProgram = compileInterfaceProgram(project, opened);
        const caAudit = coverage.auditCS2BackendRegistry(
            decoder.decodeCS2BytecodeProgram(caProgram));
        assert.equal(caAudit.interfaceTsEligible, false,
            'real ca_tasks still claims complete TypeScript eligibility');
        assert(caAudit.unresolvedDynamicHookOpcodes.length > 0,
            'real ca_tasks rejection lacks dynamic-hook diagnostics');
        assert(caAudit.unresolvedDynamicHookSourceScriptIds.length > 0,
            'real ca_tasks rejection lacks source-script diagnostics');
    }
} finally {
    rmSync(temporary, { recursive: true, force: true });
}

console.log('TypeScript backend coverage audit tests passed');

function encodeScript(wire, instructions) {
    const chunks = [Buffer.from([0])];
    for( const instruction of instructions ) {
        const metadata = wire.CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(instruction.opcode);
        assert(metadata, `fixture opcode ${instruction.opcode} is absent from wire metadata`);
        const opcode = Buffer.alloc(2);
        opcode.writeUInt16BE(instruction.opcode, 0);
        chunks.push(opcode);
        if( metadata.operand === 'int8' ) {
            const operand = Buffer.alloc(1);
            operand.writeInt8(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else if( metadata.operand === 'int32' ) {
            const operand = Buffer.alloc(4);
            operand.writeInt32BE(instruction.intOperand || 0, 0);
            chunks.push(operand);
        } else if( metadata.operand === 'int64' ) {
            const operand = Buffer.alloc(8);
            operand.writeBigInt64BE(instruction.longOperand || 0n, 0);
            chunks.push(operand);
        } else if( metadata.operand === 'string' ) {
            chunks.push(Buffer.from(instruction.stringOperand || '', 'latin1'), Buffer.from([0]));
        }
    }

    const footer = Buffer.alloc(17);
    footer.writeInt32BE(instructions.length, 0);
    const trailerLength = Buffer.alloc(2);
    trailerLength.writeUInt16BE(1, 0);
    return Buffer.concat([...chunks, footer, trailerLength]);
}
