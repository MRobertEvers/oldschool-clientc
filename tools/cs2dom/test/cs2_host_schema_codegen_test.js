import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import {
    mkdtempSync, readFileSync, rmSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const REPO = resolve(CS2DOM, '../..');
const PYTHON = process.env.PYTHON || 'python3';
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const GENERATOR = join(CS2DOM, 'wasm', 'gen_host_schema.py');
const GENERATOR_TEST = join(CS2DOM, 'wasm', 'test_gen_host_schema.py');
const REQUESTS = join(REPO, 'src', 'cs2vm2', 'cs2vm2_host_request_kinds.def');
const COMMANDS = join(CS2DOM, 'src', 'cs2_commands.js');
const OPERANDS = join(
    REPO, 'src', 'osrs', 'rscache', 'dat2a', 'dat2a_cs2_opcode_decode.c');
const NATIVE_DECODER = join(
    REPO, '3rd', 'rscache', 'src', 'datatypes', 'clientscript.c');
const EXECUTABLE_REVIEWS = join(CS2DOM, 'wasm', 'cs2_host_executable_semantics.json');
const GENERATED = join(CS2DOM, 'src', 'generated', 'cs2_host.ts');
const TYPE_TEST = join(CS2DOM, 'test', 'cs2_host_types_test.ts');

const temporary = mkdtempSync(join(tmpdir(), 'cs2dom-host-schema-'));
try {
    const regenerated = join(temporary, 'cs2_host.ts');
    run(PYTHON, [
        GENERATOR, REQUESTS, regenerated, '--format', 'ts',
        '--commands', COMMANDS, '--operands', OPERANDS,
        '--native-decoder', NATIVE_DECODER,
        '--executable-semantics', EXECUTABLE_REVIEWS,
    ], 'Host TypeScript generation');
    assert.equal(
        readFileSync(regenerated, 'utf8'),
        readFileSync(GENERATED, 'utf8'),
        'checked-in typed Host catalog is stale',
    );

    run(PYTHON, ['-m', 'unittest', '-v', GENERATOR_TEST],
        'Host generator unit tests', {
            PYTHONDONTWRITEBYTECODE: '1',
        });

    run(TSC, [
        '--strict', '--target', 'ES2020', '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext', '--skipLibCheck', '--noEmit',
        GENERATED, TYPE_TEST,
    ], 'typed Host API contract');

    const compiled = join(temporary, 'compiled');
    writeFileSync(join(temporary, 'package.json'), '{"type":"module"}\n');
    run(TSC, [
        '--strict', '--target', 'ES2020', '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext', '--skipLibCheck',
        '--rootDir', join(CS2DOM, 'src'), '--outDir', compiled, GENERATED,
    ], 'typed Host catalog runtime build');
    const catalog = await import(pathToFileURL(
        join(compiled, 'generated', 'cs2_host.js')).href);
    assert.equal(catalog.CS2_HOST_REQUEST_METADATA.length, 633);
    assert.equal(new Set(catalog.CS2_HOST_REQUEST_METADATA.map(({ opcode }) => opcode)).size, 633);
    assert.equal(new Set(catalog.CS2_HOST_REQUEST_METADATA.map(({ name }) => name)).size, 633);
    assert.deepEqual(catalog.CS2_HOST_BARRIER_COUNTS, {
        none: 221, read: 216, topology: 36, geometry: 29, external: 131,
    });

    const barrierOpcodes = Object.values(catalog.CS2_HOST_BARRIER_OPCODES).flat();
    assert.equal(barrierOpcodes.length, 633);
    assert.equal(new Set(barrierOpcodes).size, 633,
        'every schema request must have exactly one barrier classification');

    const byOpcode = catalog.CS2_HOST_REQUEST_METADATA_BY_OPCODE;
    assert.equal(byOpcode[1].operand, 'int32');
    assert.equal(byOpcode[100].operand, 'int8');
    assert.equal(byOpcode[3170].operand, 'int8',
        'native >=100 signed-int8 precedence was not preserved');
    assert.equal(byOpcode[1502].barrier, 'geometry');
    assert.equal(byOpcode[200].barrier, 'topology');
    assert.equal(byOpcode[3200].barrier, 'external');
    assert.equal(byOpcode[7502].resultKind, 'db-field');
    assert.equal(byOpcode[7502].resultSource, 'wasm-adapter-special-case');
    assert.equal(byOpcode[1].resultSource, 'wasm-adapter-override');
    const reviewed = catalog.CS2_HOST_REQUEST_METADATA
        .filter(({ executableReviewed }) => executableReviewed)
        .map(({ opcode }) => opcode);
    const manifest = JSON.parse(readFileSync(EXECUTABLE_REVIEWS, 'utf8'));
    assert.deepEqual(reviewed, manifest.reviewed.map(({ opcode }) => opcode));
    assert.equal(reviewed.length, 57);
    assert.equal(catalog.cs2HostOpcodeHasReviewedExecutableSemantics(1000), true);
    assert.equal(catalog.cs2HostOpcodeHasReviewedExecutableSemantics(3200), false);
    assert.equal(catalog.cs2HostOpcodeHasReviewedExecutableSemantics(999999), false);
} finally {
    rmSync(temporary, { recursive: true, force: true });
}

console.log('typed CS2 Host schema/codegen tests passed');

function run(command, args, description, environment = {}) {
    const result = spawnSync(command, args, {
        cwd: CS2DOM,
        encoding: 'utf8',
        env: { ...process.env, ...environment },
    });
    assert.equal(result.status, 0,
        `${description} failed:\n${result.stdout}${result.stderr}`);
}
