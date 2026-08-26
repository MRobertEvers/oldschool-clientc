/*
 * Strict browser-safe decoder for the exact clientscript payloads transported
 * by cs2dom-bytecode/1 program records.
 *
 * The wire catalogue is generated from the same opcode source as C and mirrors
 * the read order in 3rd/rscache/src/datatypes/clientscript.c.  Decoding is
 * intentionally separate from execution: known HOST and unsupported core
 * opcodes remain representable so the caller can select one backend for the
 * complete call closure before any script mutates state.
 */

import type { CS2CoreInstruction, CS2Dialect } from './generated/cs2_opcode_semantics.js';
import { CS2_OPCODE_SEMANTICS } from './generated/cs2_opcode_semantics.js';
import { CS2_HOST_REQUEST_METADATA_BY_OPCODE } from './generated/cs2_host.js';
import {
    CS2_RS2_WIRE_OPCODE_TRANSLATIONS,
    CS2_WIRE_OPCODE_METADATA_BY_OPCODE,
} from './generated/cs2_wire_opcodes.js';
import type { CS2WireOperandKind } from './generated/cs2_wire_opcodes.js';
import type { CS2CoreScript, CS2CoreSwitchCase } from './cs2_vm_core.js';

export const CS2_BYTECODE_PROGRAM_SCHEMA = 'cs2dom-bytecode/1';
export const CS2_BYTECODE_MAX_OPS = 65_536;

export type CS2TrailerLayout = 'legacy' | 'modern';
export type CS2TrailerSelection = CS2TrailerLayout | 'auto';
export type CS2DecodedExecutionClass = 'core' | 'host' | 'unsupported';

export type CS2BytecodeDecodeErrorCode =
    | 'BAD_BASE64'
    | 'BAD_BODY_LENGTH'
    | 'BAD_OPCODE_COUNT'
    | 'BAD_PROGRAM'
    | 'BAD_SCRIPT_ID'
    | 'BAD_SIGNATURE'
    | 'BAD_TRAILER'
    | 'DUPLICATE_SCRIPT'
    | 'MISSING_ENTRY_SCRIPT'
    | 'TRUNCATED'
    | 'UNKNOWN_DIALECT'
    | 'UNKNOWN_OPCODE_METADATA'
    | 'UNKNOWN_PROGRAM_SCHEMA'
    | 'UNTERMINATED_STRING';

export interface CS2DecodeAttempt {
    readonly layout: CS2TrailerLayout;
    readonly code: CS2BytecodeDecodeErrorCode;
    readonly message: string;
}

export class CS2BytecodeDecodeError extends Error {
    readonly code: CS2BytecodeDecodeErrorCode;
    readonly scriptId: number | null;
    readonly offset: number | null;
    readonly opcode: number | null;
    readonly attempts: readonly CS2DecodeAttempt[];

    constructor(
        code: CS2BytecodeDecodeErrorCode,
        message: string,
        details: {
            readonly scriptId?: number | null;
            readonly offset?: number | null;
            readonly opcode?: number | null;
            readonly attempts?: readonly CS2DecodeAttempt[];
        } = {},
    ) {
        super(message);
        this.name = 'CS2BytecodeDecodeError';
        this.code = code;
        this.scriptId = details.scriptId ?? null;
        this.offset = details.offset ?? null;
        this.opcode = details.opcode ?? null;
        this.attempts = Object.freeze([...(details.attempts ?? [])]);
    }
}

export interface CS2DecodedInstruction extends CS2CoreInstruction {
    /** Opcode exactly as stored in the cache, before dialect translation. */
    readonly wireOpcode: number;
    /** Byte offset of the opcode's u16 inside the record. */
    readonly wireOffset: number;
    readonly opcodeName: string;
    readonly wireOperand: CS2WireOperandKind;
    readonly longOperand: bigint | null;
    readonly executionClass: CS2DecodedExecutionClass;
}

export interface CS2DecodedCoreScript extends CS2CoreScript {
    readonly id: number;
    readonly name: string;
    /** Leading NUL-terminated cache string; this is not inferred from `name`. */
    readonly signature: string;
    readonly dialect: CS2Dialect;
    readonly trailerLayout: CS2TrailerLayout;
    readonly byteLength: number;
    readonly instructions: readonly CS2DecodedInstruction[];
    readonly intLocalCount: number;
    readonly stringLocalCount: number;
    readonly longLocalCount: number;
    readonly intArgumentCount: number;
    readonly stringArgumentCount: number;
    readonly longArgumentCount: number;
    readonly switchTables: readonly (readonly CS2CoreSwitchCase[])[];
}

export interface CS2BytecodeScriptRecord {
    readonly id: number;
    readonly name?: string;
    readonly data: string | Uint8Array | ArrayBuffer;
}

export interface CS2BytecodeProgramRecord {
    readonly schema?: string;
    readonly available: boolean;
    readonly dialect?: string;
    readonly revision?: number | string | null;
    readonly entries?: readonly number[];
    readonly scripts: readonly CS2BytecodeScriptRecord[];
}

export interface CS2ClientScriptDecodeOptions {
    readonly id: number;
    readonly name?: string;
    readonly dialect?: CS2Dialect;
    readonly revision?: number;
    /** `auto` matches the browser C adapter: preferred layout, then the other. */
    readonly trailer?: CS2TrailerSelection;
}

const CORE_OPCODE_BY_ID: ReadonlyMap<number, unknown> = new Map(
    CS2_OPCODE_SEMANTICS.map((row) => [row.opcode, row]),
);

/**
 * An immutable numeric script registry which can be passed directly as the
 * `ReadonlyMap` collection accepted by CS2CoreVM.
 */
export class CS2CoreScriptRegistry implements ReadonlyMap<number, CS2DecodedCoreScript> {
    readonly dialect: CS2Dialect;
    readonly revision: number;
    readonly entryScriptIds: readonly number[];
    readonly [Symbol.toStringTag] = 'CS2CoreScriptRegistry';
    readonly #scripts: Map<number, CS2DecodedCoreScript>;

    constructor(
        scripts: ReadonlyMap<number, CS2DecodedCoreScript>,
        dialect: CS2Dialect,
        revision: number,
        entries: readonly number[],
    ) {
        this.#scripts = new Map(scripts);
        this.dialect = dialect;
        this.revision = revision;
        this.entryScriptIds = Object.freeze([...entries]);
        Object.freeze(this);
    }

    get size(): number { return this.#scripts.size; }
    get(id: number): CS2DecodedCoreScript | undefined { return this.#scripts.get(id); }
    has(id: number): boolean { return this.#scripts.has(id); }
    entries(): MapIterator<[number, CS2DecodedCoreScript]> { return this.#scripts.entries(); }
    keys(): MapIterator<number> { return this.#scripts.keys(); }
    values(): MapIterator<CS2DecodedCoreScript> { return this.#scripts.values(); }
    [Symbol.iterator](): MapIterator<[number, CS2DecodedCoreScript]> {
        return this.#scripts[Symbol.iterator]();
    }
    forEach(
        callback: (
            value: CS2DecodedCoreScript,
            key: number,
            map: ReadonlyMap<number, CS2DecodedCoreScript>,
        ) => void,
        thisArg?: unknown,
    ): void {
        for( const [key, value] of this.#scripts ) callback.call(thisArg, value, key, this);
    }
}

/** Decode every exact payload atomically; no partially filled registry escapes. */
export function decodeCS2BytecodeProgram(
    program: CS2BytecodeProgramRecord,
): CS2CoreScriptRegistry {
    if( !program || program.available !== true || !Array.isArray(program.scripts) )
        fail('BAD_PROGRAM', 'original clientscript bytecode is unavailable');
    if( program.schema !== undefined && program.schema !== CS2_BYTECODE_PROGRAM_SCHEMA )
        fail('UNKNOWN_PROGRAM_SCHEMA', `unsupported clientscript program schema ${program.schema}`);

    const dialect = normalizeCS2Dialect(program.dialect);
    const revision = normalizeCS2Revision(program.revision);
    const decoded = new Map<number, CS2DecodedCoreScript>();
    for( const record of program.scripts ) {
        const id = checkedScriptId(record?.id);
        if( decoded.has(id) )
            fail('DUPLICATE_SCRIPT', `duplicate clientscript id ${id}`, { scriptId: id });
        const bytes = decodeProgramBytes(record?.data, id);
        decoded.set(id, decodeCS2ClientScript(bytes, {
            id,
            name: record?.name,
            dialect,
            revision,
            trailer: 'auto',
        }));
    }

    const entries: number[] = [];
    for( const value of program.entries ?? [] ) {
        const id = checkedScriptId(value);
        if( !decoded.has(id) )
            fail('MISSING_ENTRY_SCRIPT', `entry clientscript ${id} is not loaded`, { scriptId: id });
        if( !entries.includes(id) ) entries.push(id);
    }
    return new CS2CoreScriptRegistry(decoded, dialect, revision, entries);
}

export function decodeCS2ClientScript(
    input: Uint8Array | ArrayBuffer,
    options: CS2ClientScriptDecodeOptions,
): CS2DecodedCoreScript {
    const id = checkedScriptId(options?.id);
    const bytes = input instanceof Uint8Array
        ? input : input instanceof ArrayBuffer ? new Uint8Array(input) : null;
    if( !bytes ) fail('BAD_PROGRAM', `clientscript ${id} bytes are not a byte array`, { scriptId: id });
    const dialect = options.dialect ?? 'canonical';
    if( dialect !== 'canonical' && dialect !== 'rs2-dat2' )
        fail('UNKNOWN_DIALECT', `unsupported CS2 dialect ${String(dialect)}`, { scriptId: id });
    const revision = normalizeCS2Revision(options.revision);
    const preferred: CS2TrailerLayout = dialect === 'rs2-dat2' || revision < 237
        ? 'legacy' : 'modern';
    const selected = options.trailer ?? 'auto';
    const layouts: readonly CS2TrailerLayout[] = selected === 'auto'
        ? [preferred, preferred === 'legacy' ? 'modern' : 'legacy']
        : [selected];
    const failures: CS2BytecodeDecodeError[] = [];
    for( const layout of layouts ) {
        try {
            return decodeWithLayout(bytes, id, options.name, dialect, layout);
        } catch( error ) {
            if( !(error instanceof CS2BytecodeDecodeError) ) throw error;
            failures.push(error);
        }
    }
    const useful = failures.find((error) => error.code === 'UNKNOWN_OPCODE_METADATA') ??
        failures[0] ?? new CS2BytecodeDecodeError(
            'BAD_PROGRAM', `clientscript ${id} could not be decoded`, { scriptId: id });
    throw new CS2BytecodeDecodeError(useful.code, useful.message, {
        scriptId: useful.scriptId,
        offset: useful.offset,
        opcode: useful.opcode,
        attempts: failures.map((error, index) => Object.freeze({
            layout: layouts[index], code: error.code, message: error.message,
        })),
    });
}

function decodeWithLayout(
    bytes: Uint8Array,
    scriptId: number,
    recordName: string | undefined,
    dialect: CS2Dialect,
    layout: CS2TrailerLayout,
): CS2DecodedCoreScript {
    const footerSize = layout === 'legacy' ? 14 : 18;
    if( bytes.length < footerSize + 1 )
        fail('TRUNCATED', `clientscript ${scriptId} is too short for its ${layout} footer`, {
            scriptId, offset: bytes.length,
        });
    const trailerLength = readU16(bytes, bytes.length - 2, bytes.length, scriptId);
    const trailerOffset = bytes.length - footerSize - trailerLength;
    if( trailerOffset < 1 || trailerOffset >= bytes.length - 2 )
        fail('BAD_TRAILER', `clientscript ${scriptId} has an invalid ${layout} trailer offset`, {
            scriptId, offset: Math.max(0, trailerOffset),
        });

    const trailerLimit = bytes.length - 2;
    let trailer = trailerOffset;
    const opCount = readI32(bytes, trailer, trailerLimit, scriptId);
    trailer += 4;
    if( opCount <= 0 || opCount > CS2_BYTECODE_MAX_OPS )
        fail('BAD_OPCODE_COUNT', `clientscript ${scriptId} declares ${opCount} opcodes`, {
            scriptId, offset: trailerOffset,
        });
    const intLocalCount = readU16(bytes, trailer, trailerLimit, scriptId); trailer += 2;
    const stringLocalCount = readU16(bytes, trailer, trailerLimit, scriptId); trailer += 2;
    const longLocalCount = layout === 'modern'
        ? readU16(bytes, trailer, trailerLimit, scriptId) : 0;
    if( layout === 'modern' ) trailer += 2;
    const intArgumentCount = readU16(bytes, trailer, trailerLimit, scriptId); trailer += 2;
    const stringArgumentCount = readU16(bytes, trailer, trailerLimit, scriptId); trailer += 2;
    const longArgumentCount = layout === 'modern'
        ? readU16(bytes, trailer, trailerLimit, scriptId) : 0;
    if( layout === 'modern' ) trailer += 2;
    const switchCount = readU8(bytes, trailer, trailerLimit, scriptId); trailer++;

    const switchTables: Array<readonly CS2CoreSwitchCase[]> = [];
    for( let tableIndex = 0; tableIndex < switchCount; tableIndex++ ) {
        const caseCount = readU16(bytes, trailer, trailerLimit, scriptId); trailer += 2;
        if( caseCount > Math.floor((trailerLimit - trailer) / 8) )
            fail('TRUNCATED',
                `clientscript ${scriptId} switch ${tableIndex} declares ${caseCount} cases`, {
                    scriptId, offset: trailer,
                });
        const cases: CS2CoreSwitchCase[] = [];
        for( let caseIndex = 0; caseIndex < caseCount; caseIndex++ ) {
            const key = readI32(bytes, trailer, trailerLimit, scriptId); trailer += 4;
            const targetPc = readI32(bytes, trailer, trailerLimit, scriptId); trailer += 4;
            cases.push(Object.freeze({ key, targetPc }));
        }
        switchTables.push(Object.freeze(cases));
    }
    /* The native footer formula leaves precisely the final u16 length after
     * the switch records. Reject hidden trailer bytes instead of letting a
     * corrupt record select an accidental layout. */
    if( trailer !== trailerLimit )
        fail('BAD_TRAILER',
            `clientscript ${scriptId} ${layout} trailer leaves ${trailerLimit - trailer} bytes`, {
                scriptId, offset: trailer,
            });

    const signatureEnd = indexOfZero(bytes, 0, trailerOffset);
    if( signatureEnd < 0 )
        fail('BAD_SIGNATURE', `clientscript ${scriptId} signature is not NUL-terminated`, {
            scriptId, offset: 0,
        });
    const signature = decodeCp1252(bytes.subarray(0, signatureEnd));
    let body = signatureEnd + 1;
    const instructions: CS2DecodedInstruction[] = [];
    for( let opIndex = 0; opIndex < opCount; opIndex++ ) {
        const wireOffset = body;
        const wireOpcode = readU16(bytes, body, trailerOffset, scriptId); body += 2;
        const metadata = CS2_WIRE_OPCODE_METADATA_BY_OPCODE.get(wireOpcode);
        if( !metadata )
            fail('UNKNOWN_OPCODE_METADATA',
                `clientscript ${scriptId} opcode ${wireOpcode} has no generated wire metadata`, {
                    scriptId, offset: wireOffset, opcode: wireOpcode,
                });

        let intOperand = 0;
        let stringOperand: string | null = null;
        let longOperand: bigint | null = null;
        switch( metadata.operand ) {
        case 'int8':
            intOperand = readI8(bytes, body, trailerOffset, scriptId); body++;
            break;
        case 'int32':
            intOperand = readI32(bytes, body, trailerOffset, scriptId); body += 4;
            break;
        case 'int64':
            longOperand = readI64(bytes, body, trailerOffset, scriptId); body += 8;
            break;
        case 'string': {
            const end = indexOfZero(bytes, body, trailerOffset);
            if( end < 0 )
                fail('UNTERMINATED_STRING',
                    `clientscript ${scriptId} opcode ${wireOpcode} string is not NUL-terminated`, {
                        scriptId, offset: body, opcode: wireOpcode,
                    });
            stringOperand = decodeCp1252(bytes.subarray(body, end));
            body = end + 1;
            break;
        }
        }
        const opcode = dialect === 'rs2-dat2'
            ? CS2_RS2_WIRE_OPCODE_TRANSLATIONS[wireOpcode] ?? wireOpcode
            : wireOpcode;
        const core = CORE_OPCODE_BY_ID.has(opcode);
        const host = CS2_HOST_REQUEST_METADATA_BY_OPCODE[opcode] !== undefined;
        instructions.push(Object.freeze({
            opcode,
            wireOpcode,
            wireOffset,
            opcodeName: metadata.name,
            wireOperand: metadata.operand,
            intOperand,
            stringOperand,
            longOperand,
            executionClass: core ? 'core' : host ? 'host' : 'unsupported',
        }));
    }
    if( body !== trailerOffset )
        fail('BAD_BODY_LENGTH',
            `clientscript ${scriptId} body ends at ${body}, trailer begins at ${trailerOffset}`, {
                scriptId, offset: body,
            });

    const name = typeof recordName === 'string' && recordName.length > 0
        ? recordName : signature || `script_${scriptId}`;
    return Object.freeze({
        id: scriptId,
        name,
        signature,
        dialect,
        trailerLayout: layout,
        byteLength: bytes.length,
        instructions: Object.freeze(instructions),
        intLocalCount,
        stringLocalCount,
        longLocalCount,
        intArgumentCount,
        stringArgumentCount,
        longArgumentCount,
        switchTables: Object.freeze(switchTables),
    });
}

export function normalizeCS2Dialect(value: string | undefined): CS2Dialect {
    const dialect = String(value ?? 'canonical').toLowerCase().replace(/_/g, '-');
    if( dialect === 'canonical' || dialect === 'osrs' || dialect === 'oldschool' )
        return 'canonical';
    if( dialect === 'rs2' || dialect === 'rs2-dat2' || dialect === '634' )
        return 'rs2-dat2';
    fail('UNKNOWN_DIALECT', `unsupported CS2 dialect ${String(value)}`);
}

export function normalizeCS2Revision(value: number | string | null | undefined): number {
    if( Number.isInteger(value) && Number(value) >= 0 ) return Number(value);
    const match = /(\d+)/.exec(String(value ?? ''));
    if( !match ) return 0;
    const revision = Number(match[1]);
    return Number.isSafeInteger(revision) ? revision : 0;
}

function checkedScriptId(value: unknown): number {
    const id = Number(value);
    if( !Number.isSafeInteger(id) || id < 0 || id > 0x7fffffff )
        fail('BAD_SCRIPT_ID', `invalid clientscript id ${String(value)}`);
    return id;
}

function decodeProgramBytes(
    value: string | Uint8Array | ArrayBuffer | undefined,
    scriptId: number,
): Uint8Array {
    if( value instanceof Uint8Array ) return value;
    if( value instanceof ArrayBuffer ) return new Uint8Array(value);
    if( typeof value !== 'string' )
        fail('BAD_PROGRAM', `clientscript ${scriptId} has no byte payload`, { scriptId });
    if( value.length === 0 || value.length % 4 !== 0 ||
        !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value) )
        fail('BAD_BASE64', `clientscript ${scriptId} payload is not canonical base64`, { scriptId });
    const padding = value.endsWith('==') ? 2 : value.endsWith('=') ? 1 : 0;
    const bytes = new Uint8Array(value.length / 4 * 3 - padding);
    let output = 0;
    for( let offset = 0; offset < value.length; offset += 4 ) {
        const a = base64Digit(value.charCodeAt(offset));
        const b = base64Digit(value.charCodeAt(offset + 1));
        const c = value[offset + 2] === '=' ? 0 : base64Digit(value.charCodeAt(offset + 2));
        const d = value[offset + 3] === '=' ? 0 : base64Digit(value.charCodeAt(offset + 3));
        const packed = (a << 18) | (b << 12) | (c << 6) | d;
        if( output < bytes.length ) bytes[output++] = packed >>> 16;
        if( output < bytes.length ) bytes[output++] = packed >>> 8;
        if( output < bytes.length ) bytes[output++] = packed;
    }
    return bytes;
}

function base64Digit(code: number): number {
    if( code >= 65 && code <= 90 ) return code - 65;
    if( code >= 97 && code <= 122 ) return code - 71;
    if( code >= 48 && code <= 57 ) return code + 4;
    if( code === 43 ) return 62;
    if( code === 47 ) return 63;
    return 0;
}

function readU8(bytes: Uint8Array, offset: number, limit: number, scriptId: number): number {
    requireBytes(offset, 1, limit, scriptId);
    return bytes[offset];
}

function readI8(bytes: Uint8Array, offset: number, limit: number, scriptId: number): number {
    const value = readU8(bytes, offset, limit, scriptId);
    return value < 0x80 ? value : value - 0x100;
}

function readU16(bytes: Uint8Array, offset: number, limit: number, scriptId: number): number {
    requireBytes(offset, 2, limit, scriptId);
    return bytes[offset] * 0x100 + bytes[offset + 1];
}

function readI32(bytes: Uint8Array, offset: number, limit: number, scriptId: number): number {
    requireBytes(offset, 4, limit, scriptId);
    return (bytes[offset] << 24) |
        (bytes[offset + 1] << 16) |
        (bytes[offset + 2] << 8) |
        bytes[offset + 3];
}

function readI64(bytes: Uint8Array, offset: number, limit: number, scriptId: number): bigint {
    requireBytes(offset, 8, limit, scriptId);
    const high = readI32(bytes, offset, limit, scriptId);
    const low = readI32(bytes, offset + 4, limit, scriptId);
    return BigInt.asIntN(64, (BigInt(high >>> 0) << 32n) | BigInt(low >>> 0));
}

function requireBytes(offset: number, count: number, limit: number, scriptId: number): void {
    if( !Number.isSafeInteger(offset) || offset < 0 || offset + count > limit )
        fail('TRUNCATED', `clientscript ${scriptId} is truncated at byte ${offset}`, {
            scriptId, offset,
        });
}

function indexOfZero(bytes: Uint8Array, start: number, limit: number): number {
    for( let offset = start; offset < limit; offset++ ) if( bytes[offset] === 0 ) return offset;
    return -1;
}

/* RSCache keeps raw CP-1252 bytes in C strings; JavaScript strings need the
 * corresponding Unicode code points. Undefined C1 slots deliberately retain
 * their numeric control values, matching RSCache_Cp1252ToUtf8. */
const CP1252_C1 = Object.freeze([
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
]);

function decodeCp1252(bytes: Uint8Array): string {
    let result = '';
    const chunk: number[] = [];
    for( const byte of bytes ) {
        chunk.push(byte >= 0x80 && byte < 0xa0 ? CP1252_C1[byte - 0x80] : byte);
        if( chunk.length === 2048 ) {
            result += String.fromCodePoint(...chunk);
            chunk.length = 0;
        }
    }
    return result + String.fromCodePoint(...chunk);
}

function fail(
    code: CS2BytecodeDecodeErrorCode,
    message: string,
    details: ConstructorParameters<typeof CS2BytecodeDecodeError>[2] = {},
): never {
    throw new CS2BytecodeDecodeError(code, message, details);
}
