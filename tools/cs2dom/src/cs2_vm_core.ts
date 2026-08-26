/*
 * Executable TypeScript foundation for the CS2 VM.
 *
 * Opcode ids, dialect coverage and the intrinsic selected for each opcode come
 * from the generated semantics declarations.  The functions below are the
 * deliberately small handwritten half: one implementation per named
 * intrinsic, audited against CS2VM2_Op_* in src/cs2vm2/cs2vm2.c.
 *
 * This module does not fall back to C after execution has begun.  Callers must
 * run analyzeCS2CoreScript (the CS2CoreVM constructor does this by default) and
 * choose a backend for the complete script before it mutates VM state.
 */

import {
    CS2_CORE_DISPATCH_DECLARATIONS,
    CS2_OPCODE_SEMANTICS,
} from './generated/cs2_opcode_semantics.js';
import type {
    CS2CoreInstruction,
    CS2CoreIntrinsicHandlers,
    CS2CoreIntrinsicName,
    CS2Dialect,
    CS2OpcodeSemantics,
} from './generated/cs2_opcode_semantics.js';

export type {
    CS2CoreInstruction,
    CS2CoreIntrinsicName,
    CS2Dialect,
    CS2OpcodeSemantics,
};

export const CS2_CORE_STACK_LIMIT = 1024;
export const CS2_CORE_LOCAL_LIMIT = 1024;
export const CS2_CORE_FRAME_LIMIT = 128;
export const CS2_CORE_ARRAY_LIMIT = 128;
export const CS2_CORE_ARRAY_CAPACITY = 5000;
export const CS2_CORE_DEFAULT_CYCLE_LIMIT = 1_000_000;

export interface CS2CoreSwitchCase {
    readonly key: number;
    /** Relative to the already-incremented program counter. */
    readonly targetPc: number;
}

export interface CS2CoreScript {
    readonly id?: number;
    readonly name?: string;
    readonly instructions: readonly CS2CoreInstruction[];
    readonly intLocalCount?: number;
    readonly stringLocalCount?: number;
    readonly intArgumentCount?: number;
    readonly stringArgumentCount?: number;
    readonly switchTables?: readonly (readonly CS2CoreSwitchCase[])[];
}

export interface CS2CoreArrayHandle {
    readonly kind: 'cs2-array-handle';
    readonly slot: number;
}

export type CS2CoreStringValue = string | CS2CoreArrayHandle;

export interface CS2CoreFrame {
    readonly script: CS2CoreScript;
    pc: number;
    readonly intLocals: Array<number | undefined>;
    readonly stringLocals: Array<CS2CoreStringValue | undefined>;
}

export type CS2CoreScriptCollection =
    | ReadonlyMap<number, CS2CoreScript>
    | readonly CS2CoreScript[];

export interface CS2CoreArray {
    isString: boolean;
    size: number;
    intCells: number[];
    stringCells: CS2CoreStringValue[];
}

export type CS2CoreErrorCode =
    | 'CYCLE_LIMIT'
    | 'CALL_STACK_OVERFLOW'
    | 'DIVIDE_BY_ZERO'
    | 'INT_STACK_OVERFLOW'
    | 'INT_STACK_UNDERFLOW'
    | 'INVALID_LOCAL'
    | 'INVALID_PC'
    | 'INVALID_STRING_VALUE'
    | 'EXTERNAL_OPCODE_FAILED'
    | 'MISSING_SCRIPT'
    | 'STRING_STACK_OVERFLOW'
    | 'STRING_STACK_UNDERFLOW'
    | 'UNSUPPORTED_OPCODE';

export interface CS2CoreExecutionError {
    readonly code: CS2CoreErrorCode;
    readonly message: string;
    readonly scriptId: number | null;
    readonly pc: number;
    readonly opcode: number;
}

export interface CS2CoreState {
    readonly intStack: number[];
    readonly stringStack: CS2CoreStringValue[];
    readonly frames: CS2CoreFrame[];
    readonly scripts: ReadonlyMap<number, CS2CoreScript>;
    readonly arrays: CS2CoreArray[];
    cycles: number;
    error: CS2CoreExecutionError | null;
    /** The instruction whose intrinsic is currently executing. */
    currentPc: number;
    currentOpcode: number;
}

export type CS2CoreStepStatus = 'ok' | 'done' | 'error';
export type CS2CoreRunStatus = 'done' | 'error';
type CS2CoreIntrinsicResult = 'ok' | 'error';
type CS2CoreIntrinsic = (
    state: CS2CoreState,
    instruction: CS2CoreInstruction,
) => CS2CoreIntrinsicResult;

export interface CS2CoreRunResult {
    readonly status: CS2CoreRunStatus;
    readonly cycles: number;
    readonly state: CS2CoreState;
    readonly error: CS2CoreExecutionError | null;
}

export interface CS2CoreStateOptions {
    readonly intLocals?: readonly number[];
    readonly stringLocals?: readonly (string | null | undefined)[];
    readonly intStack?: readonly number[];
    readonly stringStack?: readonly (string | null | undefined)[];
    /** Complete local registry used by opcode 40. */
    readonly scripts?: CS2CoreScriptCollection;
}

/**
 * A separately preflighted, synchronous extension to the generated core
 * dispatch table.  Browser Host opcodes use this seam; catalogue membership
 * alone never makes an opcode executable.
 */
export interface CS2CoreExternalOpcodeExecutor {
    readonly implementedOpcodes: ReadonlySet<number>;
    execute(
        state: CS2CoreState,
        instruction: CS2CoreInstruction,
    ): void;
}

export interface CS2CoreCoverage {
    readonly supported: boolean;
    readonly dialect: CS2Dialect;
    readonly unsupportedOpcodes: readonly number[];
    readonly missingScriptIds: readonly number[];
    readonly scriptCount: number;
}

export interface CS2CoreVMOptions extends CS2CoreStateOptions {
    readonly dialect?: CS2Dialect;
    /** Intended only for diagnostics/tests which exercise the error path. */
    readonly allowUnsupported?: boolean;
    /** Explicit reviewed implementation for non-core opcodes. */
    readonly externalOpcodeExecutor?: CS2CoreExternalOpcodeExecutor;
}

export class CS2CoreUnsupportedOpcodeError extends Error {
    readonly code = 'UNSUPPORTED_OPCODE';
    readonly dialect: CS2Dialect;
    readonly opcodes: readonly number[];
    readonly missingScriptIds: readonly number[];

    constructor(
        dialect: CS2Dialect,
        opcodes: readonly number[],
        missingScriptIds: readonly number[] = [],
    ) {
        const parts: string[] = [];
        if( opcodes.length ) parts.push(
            `${dialect} opcode${opcodes.length === 1 ? '' : 's'} ${opcodes.join(', ')}`);
        if( missingScriptIds.length ) parts.push(
            `missing script${missingScriptIds.length === 1 ? '' : 's'} ${missingScriptIds.join(', ')}`);
        super(`TypeScript CS2 core does not support closure: ${parts.join('; ')}`);
        this.name = 'CS2CoreUnsupportedOpcodeError';
        this.dialect = dialect;
        this.opcodes = Object.freeze([...opcodes]);
        this.missingScriptIds = Object.freeze([...missingScriptIds]);
    }
}

const SEMANTICS_BY_OPCODE: ReadonlyMap<number, CS2OpcodeSemantics> = new Map(
    CS2_OPCODE_SEMANTICS.map((semantic) => [semantic.opcode, semantic]),
);

function createScriptRegistry(
    entry: CS2CoreScript,
    collection?: CS2CoreScriptCollection,
): ReadonlyMap<number, CS2CoreScript> {
    const registry = new Map<number, CS2CoreScript>();
    if( Array.isArray(collection) ) {
        for( const script of collection ) {
            if( Number.isInteger(script.id) ) registry.set(Number(script.id) | 0, script);
        }
    } else if( collection ) {
        for( const [id, child] of collection as ReadonlyMap<number, CS2CoreScript> ) {
            if( Number.isInteger(id) ) registry.set(id | 0, child);
        }
    }
    if( Number.isInteger(entry.id) ) registry.set(Number(entry.id) | 0, entry);
    return registry;
}

/**
 * Determine whether a complete local call closure can run on this backend.
 * This is the pre-execution routing seam: every opcode-40 target is traversed
 * before state mutation, so unsupported closures remain wholly on C/WASM.
 */
export function analyzeCS2CoreScript(
    script: CS2CoreScript,
    dialect: CS2Dialect = 'canonical',
    scripts?: CS2CoreScriptCollection,
    externalOpcodes?: ReadonlySet<number>,
): CS2CoreCoverage {
    const registry = createScriptRegistry(script, scripts);
    const unsupported = new Set<number>();
    const missingScripts = new Set<number>();
    const pending: CS2CoreScript[] = [script];
    const visited = new Set<CS2CoreScript>();
    while( pending.length ) {
        const current = pending.pop()!;
        if( visited.has(current) ) continue;
        visited.add(current);
        for( const instruction of current.instructions ) {
            const semantic = SEMANTICS_BY_OPCODE.get(instruction.opcode);
            if( semantic ) {
                if( !semantic.dialects.includes(dialect) ) unsupported.add(instruction.opcode);
            } else if( !externalOpcodes?.has(instruction.opcode) ) {
                unsupported.add(instruction.opcode);
            }
            if( instruction.opcode !== 40 ) continue;
            const targetId = instruction.intOperand | 0;
            const target = registry.get(targetId);
            if( target ) pending.push(target);
            else missingScripts.add(targetId);
        }
    }
    const unsupportedOpcodes = Object.freeze([...unsupported].sort((left, right) => left - right));
    const missingScriptIds = Object.freeze([...missingScripts].sort((left, right) => left - right));
    return Object.freeze({
        supported: unsupportedOpcodes.length === 0 && missingScriptIds.length === 0,
        dialect,
        unsupportedOpcodes,
        missingScriptIds,
        scriptCount: visited.size,
    });
}

export function supportsCS2CoreScript(
    script: CS2CoreScript,
    dialect: CS2Dialect = 'canonical',
    scripts?: CS2CoreScriptCollection,
    externalOpcodes?: ReadonlySet<number>,
): boolean {
    return analyzeCS2CoreScript(script, dialect, scripts, externalOpcodes).supported;
}

function normalizeLocalCount(value: number | undefined): number {
    if( value === undefined ) return 0;
    if( !Number.isInteger(value) || value < 0 || value > CS2_CORE_LOCAL_LIMIT )
        throw new RangeError(`CS2 local count must be between 0 and ${CS2_CORE_LOCAL_LIMIT}`);
    return value;
}

function normalizeIntValues(values: readonly number[] | undefined): number[] {
    if( !values ) return [];
    return Array.from(values, (value) => Number(value) | 0);
}

function normalizeStringValues(
    values: readonly (string | null | undefined)[] | undefined,
): string[] {
    if( !values ) return [];
    return Array.from(values, (value) => value == null ? '' : String(value));
}

function createFrame(script: CS2CoreScript): CS2CoreFrame {
    const intLocalCount = Math.max(
        normalizeLocalCount(script.intLocalCount),
        normalizeLocalCount(script.intArgumentCount),
    );
    const stringLocalCount = Math.max(
        normalizeLocalCount(script.stringLocalCount),
        normalizeLocalCount(script.stringArgumentCount),
    );
    return {
        script,
        pc: 0,
        intLocals: new Array<number | undefined>(intLocalCount),
        stringLocals: new Array<CS2CoreStringValue | undefined>(stringLocalCount),
    };
}

export function createCS2CoreState(
    script: CS2CoreScript,
    options: CS2CoreStateOptions = {},
): CS2CoreState {
    const intLocalCount = Math.max(normalizeLocalCount(script.intLocalCount),
        options.intLocals?.length ?? 0);
    const stringLocalCount = Math.max(normalizeLocalCount(script.stringLocalCount),
        options.stringLocals?.length ?? 0);
    if( intLocalCount > CS2_CORE_LOCAL_LIMIT || stringLocalCount > CS2_CORE_LOCAL_LIMIT )
        throw new RangeError(`CS2 locals exceed ${CS2_CORE_LOCAL_LIMIT}`);

    const frame = createFrame(script);
    const intLocals = frame.intLocals;
    if( intLocals.length < intLocalCount ) intLocals.length = intLocalCount;
    for( let index = 0; index < (options.intLocals?.length ?? 0); index++ )
        intLocals[index] = Number(options.intLocals![index]) | 0;
    const stringLocals = frame.stringLocals;
    if( stringLocals.length < stringLocalCount ) stringLocals.length = stringLocalCount;
    for( let index = 0; index < (options.stringLocals?.length ?? 0); index++ ) {
        const value = options.stringLocals![index];
        stringLocals[index] = value == null ? '' : String(value);
    }

    const intStack = normalizeIntValues(options.intStack);
    const stringStack = normalizeStringValues(options.stringStack);
    if( intStack.length > CS2_CORE_STACK_LIMIT || stringStack.length > CS2_CORE_STACK_LIMIT )
        throw new RangeError(`CS2 operand stacks exceed ${CS2_CORE_STACK_LIMIT}`);

    return {
        intStack,
        stringStack,
        frames: [frame],
        scripts: createScriptRegistry(script, options.scripts),
        arrays: [],
        cycles: 0,
        error: null,
        currentPc: -1,
        currentOpcode: -1,
    };
}

function currentFrame(state: CS2CoreState): CS2CoreFrame | undefined {
    return state.frames[state.frames.length - 1];
}

function fail(
    state: CS2CoreState,
    code: CS2CoreErrorCode,
    message: string,
): CS2CoreIntrinsicResult {
    if( !state.error ) {
        const frame = currentFrame(state);
        state.error = Object.freeze({
            code,
            message,
            scriptId: Number.isInteger(frame?.script.id) ? Number(frame!.script.id) : null,
            pc: state.currentPc,
            opcode: state.currentOpcode,
        });
    }
    return 'error';
}

function pushInt(state: CS2CoreState, value: number): CS2CoreIntrinsicResult {
    if( state.intStack.length >= CS2_CORE_STACK_LIMIT )
        return fail(state, 'INT_STACK_OVERFLOW', 'CS2 integer stack overflow');
    state.intStack.push(value | 0);
    return 'ok';
}

function popInt(state: CS2CoreState): number | null {
    if( state.intStack.length === 0 ) {
        fail(state, 'INT_STACK_UNDERFLOW', 'CS2 integer stack underflow');
        return null;
    }
    return state.intStack.pop()!;
}

function pushString(
    state: CS2CoreState,
    value: CS2CoreStringValue | null,
): CS2CoreIntrinsicResult {
    if( state.stringStack.length >= CS2_CORE_STACK_LIMIT )
        return fail(state, 'STRING_STACK_OVERFLOW', 'CS2 string stack overflow');
    /* JavaScript strings are immutable, so this has the value-copy semantics
     * for which the C VM duplicates constant/local strings into its pool. */
    state.stringStack.push(value ?? '');
    return 'ok';
}

function popString(state: CS2CoreState): CS2CoreStringValue | null {
    if( state.stringStack.length === 0 ) {
        fail(state, 'STRING_STACK_UNDERFLOW', 'CS2 string stack underflow');
        return null;
    }
    return state.stringStack.pop()!;
}

function popText(state: CS2CoreState): string | null {
    const value = popString(state);
    if( value === null ) return null;
    if( typeof value !== 'string' ) {
        fail(state, 'INVALID_STRING_VALUE',
            'CS2 array handle was consumed by an opcode requiring text');
        return null;
    }
    return value;
}

/* Cache strings are byte-transparent windows-1252 in C and decoded to Unicode
 * by cs2_bytecode_decoder.ts.  Most string operations can therefore work one
 * JavaScript code unit at a time: every decoded cache byte is exactly one BMP
 * code point.  COMPARE is the exception because strcmp orders the original
 * unsigned bytes, not their Unicode code points (notably euro is byte 0x80).
 * This inverse is the same bijection as RSCache_Utf8ToCp1252. */
const CP1252_SPECIAL_TO_BYTE: ReadonlyMap<number, number> = new Map([
    [0x20ac, 0x80], [0x201a, 0x82], [0x0192, 0x83], [0x201e, 0x84],
    [0x2026, 0x85], [0x2020, 0x86], [0x2021, 0x87], [0x02c6, 0x88],
    [0x2030, 0x89], [0x0160, 0x8a], [0x2039, 0x8b], [0x0152, 0x8c],
    [0x017d, 0x8e], [0x2018, 0x91], [0x2019, 0x92], [0x201c, 0x93],
    [0x201d, 0x94], [0x2022, 0x95], [0x2013, 0x96], [0x2014, 0x97],
    [0x02dc, 0x98], [0x2122, 0x99], [0x0161, 0x9a], [0x203a, 0x9b],
    [0x0153, 0x9c], [0x017e, 0x9e], [0x0178, 0x9f],
]);

function clientByte(codeUnit: number): number {
    if( (codeUnit > 0 && codeUnit < 0x80) || (codeUnit >= 0xa0 && codeUnit <= 0xff) )
        return codeUnit;
    if( codeUnit === 0x81 || codeUnit === 0x8d || codeUnit === 0x8f ||
        codeUnit === 0x90 || codeUnit === 0x9d ) return codeUnit;
    return CP1252_SPECIAL_TO_BYTE.get(codeUnit) ?? 0x3f;
}

function cStringLength(text: string): number {
    const nul = text.indexOf('\0');
    return nul < 0 ? text.length : nul;
}

function compareClientStrings(lhs: string, rhs: string): number {
    const lhsLength = cStringLength(lhs);
    const rhsLength = cStringLength(rhs);
    const count = Math.min(lhsLength, rhsLength);
    for( let index = 0; index < count; index++ ) {
        const left = clientByte(lhs.charCodeAt(index));
        const right = clientByte(rhs.charCodeAt(index));
        if( left !== right ) return left < right ? -1 : 1;
    }
    if( lhsLength === rhsLength ) return 0;
    return lhsLength < rhsLength ? -1 : 1;
}

function lowercaseAscii(text: string): string {
    const length = cStringLength(text);
    let result = '';
    for( let index = 0; index < length; index++ ) {
        const code = text.charCodeAt(index);
        result += String.fromCharCode(code >= 0x41 && code <= 0x5a ? code + 0x20 : code);
    }
    return result;
}

function escapeClientMarkup(text: string): string {
    const length = cStringLength(text);
    let result = '';
    for( let index = 0; index < length; index++ ) {
        const code = text.charCodeAt(index);
        if( code === 0x3c ) result += '<lt>';
        else if( code === 0x3e ) result += '<gt>';
        else result += text[index];
    }
    return result;
}

function removeClientTags(text: string): string {
    const length = cStringLength(text);
    let result = '';
    let inTag = false;
    for( let index = 0; index < length; index++ ) {
        const code = text.charCodeAt(index);
        if( code === 0x3c ) inTag = true;
        else if( inTag && code === 0x3e ) inTag = false;
        else if( !inTag ) result += text[index];
    }
    return result;
}

function localIndex(
    state: CS2CoreState,
    instruction: CS2CoreInstruction,
): number | null {
    const index = instruction.intOperand;
    if( !Number.isInteger(index) || index < 0 || index >= CS2_CORE_LOCAL_LIMIT ) {
        fail(state, 'INVALID_LOCAL', `CS2 local index ${index} is outside 0..${CS2_CORE_LOCAL_LIMIT - 1}`);
        return null;
    }
    return index;
}

function jumpRelative(
    state: CS2CoreState,
    instruction: CS2CoreInstruction,
): CS2CoreIntrinsicResult {
    const frame = currentFrame(state);
    if( !frame ) return fail(state, 'INVALID_PC', 'CS2 branch has no active frame');
    /* The interpreter increments pc before dispatch. Relative offsets are
     * therefore applied to the following instruction, exactly as C does. */
    frame.pc += instruction.intOperand | 0;
    return 'ok';
}

function binaryInts(
    state: CS2CoreState,
): readonly [lhs: number, rhs: number] | null {
    /* Preserve C's observable pop order: rhs/top is removed first. */
    const rhs = popInt(state);
    if( rhs === null ) return null;
    const lhs = popInt(state);
    if( lhs === null ) return null;
    return [lhs, rhs];
}

function branchCompare(
    state: CS2CoreState,
    instruction: CS2CoreInstruction,
    compare: (lhs: number, rhs: number) => boolean,
): CS2CoreIntrinsicResult {
    const values = binaryInts(state);
    if( !values ) return 'error';
    return compare(values[0], values[1]) ? jumpRelative(state, instruction) : 'ok';
}

function arithmetic(
    state: CS2CoreState,
    operation: (lhs: number, rhs: number) => number,
): CS2CoreIntrinsicResult {
    const values = binaryInts(state);
    if( !values ) return 'error';
    return pushInt(state, operation(values[0], values[1]) | 0);
}

function popIntsTopFirst(state: CS2CoreState, count: number): number[] | null {
    const values: number[] = [];
    for( let index = 0; index < count; index++ ) {
        const value = popInt(state);
        if( value === null ) return null;
        values.push(value);
    }
    return values;
}

function arrayLocal(
    state: CS2CoreState,
    frame: CS2CoreFrame,
    slot: number,
): CS2CoreArray | undefined {
    if( !Number.isInteger(slot) || slot < 0 || slot >= CS2_CORE_LOCAL_LIMIT ) return undefined;
    const handle = frame.stringLocals[slot];
    if( typeof handle !== 'object' || handle?.kind !== 'cs2-array-handle' ) return undefined;
    return state.arrays[handle.slot];
}

function arrayFromHandle(
    state: CS2CoreState,
    handle: CS2CoreStringValue,
): CS2CoreArray | undefined {
    if( typeof handle !== 'object' || handle?.kind !== 'cs2-array-handle' ) return undefined;
    if( !Number.isInteger(handle.slot) || handle.slot < 0 || handle.slot >= state.arrays.length )
        return undefined;
    return state.arrays[handle.slot];
}

function allocateArray(state: CS2CoreState, size: number, isString: boolean): CS2CoreArrayHandle {
    const normalizedSize = Math.max(0, Math.min(CS2_CORE_ARRAY_CAPACITY, size | 0));
    const array: CS2CoreArray = {
        isString,
        size: normalizedSize,
        intCells: isString ? [] : new Array<number>(normalizedSize).fill(-1),
        stringCells: isString ? new Array<CS2CoreStringValue>(normalizedSize).fill('') : [],
    };
    let slot = state.arrays.length;
    if( slot >= CS2_CORE_ARRAY_LIMIT ) {
        /* Match the C VM's fail-soft pool behavior: every overflow reuses the
         * final slot instead of aborting an otherwise valid invocation. */
        slot = CS2_CORE_ARRAY_LIMIT - 1;
        state.arrays[slot] = array;
    } else {
        state.arrays.push(array);
    }
    return Object.freeze({ kind: 'cs2-array-handle', slot });
}

/** Handwritten executable behavior selected by the generated dispatch rows. */
export const CS2_CORE_INTRINSICS = {
    pushIntConstant(state, instruction) {
        return pushInt(state, instruction.intOperand);
    },
    pushStringConstant(state, instruction) {
        return pushString(state, instruction.stringOperand);
    },
    branch(state, instruction) {
        return jumpRelative(state, instruction);
    },
    branchIntNotEquals(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs !== rhs);
    },
    branchIntEquals(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs === rhs);
    },
    branchIntLessThan(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs < rhs);
    },
    branchIntGreaterThan(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs > rhs);
    },
    returnFrame(state) {
        if( !currentFrame(state) )
            return fail(state, 'INVALID_PC', 'CS2 return has no active frame');
        state.frames.pop();
        return 'ok';
    },
    branchIntLessThanOrEquals(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs <= rhs);
    },
    branchIntGreaterThanOrEquals(state, instruction) {
        return branchCompare(state, instruction, (lhs, rhs) => lhs >= rhs);
    },
    pushIntLocal(state, instruction) {
        const index = localIndex(state, instruction);
        const frame = currentFrame(state);
        if( index === null || !frame ) return 'error';
        return pushInt(state, frame.intLocals[index] ?? 0);
    },
    popIntLocal(state, instruction) {
        const index = localIndex(state, instruction);
        const frame = currentFrame(state);
        if( index === null || !frame ) return 'error';
        const value = popInt(state);
        if( value === null ) return 'error';
        frame.intLocals[index] = value;
        return 'ok';
    },
    pushStringLocal(state, instruction) {
        const index = localIndex(state, instruction);
        const frame = currentFrame(state);
        if( index === null || !frame ) return 'error';
        return pushString(state, frame.stringLocals[index] ?? '');
    },
    popStringLocal(state, instruction) {
        const index = localIndex(state, instruction);
        const frame = currentFrame(state);
        if( index === null || !frame ) return 'error';
        const value = popString(state);
        if( value === null ) return 'error';
        frame.stringLocals[index] = value;
        return 'ok';
    },
    joinStrings(state, instruction) {
        const count = instruction.intOperand | 0;
        if( count <= 0 ) return pushString(state, '');
        const parts = new Array<string>(count);
        for( let index = count - 1; index >= 0; index-- ) {
            const value = popText(state);
            if( value === null ) return 'error';
            parts[index] = value;
        }
        return pushString(state, parts.join(''));
    },
    discardInt(state) {
        return popInt(state) === null ? 'error' : 'ok';
    },
    discardString(state) {
        return popString(state) === null ? 'error' : 'ok';
    },
    callScriptWithParams(state, instruction) {
        const targetId = instruction.intOperand | 0;
        const target = state.scripts.get(targetId);
        if( !target ) return fail(state, 'MISSING_SCRIPT',
            `CS2 gosub target ${targetId} is absent from the local registry`);
        if( state.frames.length >= CS2_CORE_FRAME_LIMIT ) return fail(
            state,
            'CALL_STACK_OVERFLOW',
            `CS2 call depth ${CS2_CORE_FRAME_LIMIT} exhausted calling script ${targetId}`,
        );

        const callee = createFrame(target);
        state.frames.push(callee);
        const stringArguments = normalizeLocalCount(target.stringArgumentCount);
        const intArguments = normalizeLocalCount(target.intArgumentCount);
        for( let index = stringArguments - 1; index >= 0; index-- ) {
            const value = popString(state);
            if( value === null ) return 'error';
            callee.stringLocals[index] = value;
        }
        for( let index = intArguments - 1; index >= 0; index-- ) {
            const value = popInt(state);
            if( value === null ) return 'error';
            callee.intLocals[index] = value;
        }
        return 'ok';
    },
    defineArray(state, instruction) {
        const size = popInt(state);
        if( size === null ) return 'error';
        const frame = currentFrame(state);
        if( !frame ) return fail(state, 'INVALID_PC', 'CS2 array definition has no active frame');
        const slot = (instruction.intOperand | 0) >> 16;
        if( slot < 0 || slot >= CS2_CORE_LOCAL_LIMIT ) return 'ok';
        const isString = ((instruction.intOperand | 0) & 0xffff) === 115;
        frame.stringLocals[slot] = allocateArray(state, size, isString);
        return 'ok';
    },
    pushArrayElement(state, instruction) {
        const index = popInt(state);
        if( index === null ) return 'error';
        const frame = currentFrame(state);
        if( !frame ) return fail(state, 'INVALID_PC', 'CS2 array read has no active frame');
        const array = arrayLocal(state, frame, instruction.intOperand | 0);
        const inRange = !!array && index >= 0 && index < array.size;
        if( array?.isString ) return pushString(
            state,
            inRange ? array.stringCells[index] ?? '' : '',
        );
        return pushInt(state, inRange ? array!.intCells[index] : 0);
    },
    popArrayElement(state, instruction) {
        const frame = currentFrame(state);
        if( !frame ) return fail(state, 'INVALID_PC', 'CS2 array write has no active frame');
        const array = arrayLocal(state, frame, instruction.intOperand | 0);
        if( array?.isString ) {
            const value = popString(state);
            if( value === null ) return 'error';
            const index = popInt(state);
            if( index === null ) return 'error';
            if( index >= 0 && index < array.size ) array.stringCells[index] = value;
            return 'ok';
        }
        const value = popInt(state);
        if( value === null ) return 'error';
        const index = popInt(state);
        if( index === null ) return 'error';
        if( array && index >= 0 && index < array.size ) array.intCells[index] = value;
        return 'ok';
    },
    switchBranch(state, instruction) {
        const key = popInt(state);
        if( key === null ) return 'error';
        const frame = currentFrame(state);
        if( !frame ) return fail(state, 'INVALID_PC', 'CS2 switch has no active frame');
        const table = frame.script.switchTables?.[instruction.intOperand | 0];
        if( !table ) return 'ok';
        for( const entry of table ) {
            if( (entry.key | 0) === key ) {
                frame.pc += entry.targetPc | 0;
                break;
            }
        }
        return 'ok';
    },
    moveCoord(state) {
        const values = popIntsTopFirst(state, 4);
        if( !values ) return 'error';
        const [z, plane, x, packed] = values;
        const offset = (plane << 28) | (x << 14) | z;
        return pushInt(state, (packed + offset) | 0);
    },
    intAdd(state) {
        return arithmetic(state, (lhs, rhs) => lhs + rhs);
    },
    intSubtract(state) {
        return arithmetic(state, (lhs, rhs) => lhs - rhs);
    },
    intMultiply(state) {
        /* Math.imul is the exact low signed i32 product. A Number multiply
         * loses low bits before |0 for sufficiently large operands. */
        return arithmetic(state, Math.imul);
    },
    intDivide(state) {
        const values = binaryInts(state);
        if( !values ) return 'error';
        if( values[1] === 0 )
            return fail(state, 'DIVIDE_BY_ZERO', 'CS2 integer division by zero');
        return pushInt(state, Math.trunc(values[0] / values[1]));
    },
    intModulo(state) {
        const values = binaryInts(state);
        if( !values ) return 'error';
        if( values[1] === 0 )
            return fail(state, 'DIVIDE_BY_ZERO', 'CS2 integer modulo by zero');
        return pushInt(state, values[0] % values[1]);
    },
    intPower(state) {
        const values = binaryInts(state);
        if( !values ) return 'error';
        const result = Math.pow(values[0], values[1]);
        /* Production browser C/WASM uses the reference client's saturating
         * double-to-int conversion for pow: NaN -> 0 and infinities/out-of-
         * range values clamp to signed i32. */
        if( Number.isNaN(result) ) return pushInt(state, 0);
        if( result >= 0x7fffffff ) return pushInt(state, 0x7fffffff);
        if( result <= -0x80000000 ) return pushInt(state, -0x80000000);
        return pushInt(state, Math.trunc(result));
    },
    intInterpolate(state) {
        const values = popIntsTopFirst(state, 5);
        if( !values ) return 'error';
        const [e, d, c, b, a] = values;
        const denominator = (d - c) | 0;
        if( denominator === 0 ) return pushInt(state, a);
        const product = Math.imul((b - a) | 0, (e - c) | 0);
        return pushInt(state, (a + Math.trunc(product / denominator)) | 0);
    },
    intSetBit(state) {
        const values = binaryInts(state);
        if( !values ) return 'error';
        return pushInt(state, values[0] | (1 << values[1]));
    },
    intTestBit(state) {
        const values = binaryInts(state);
        if( !values ) return 'error';
        return pushInt(state, (values[0] & (1 << values[1])) !== 0 ? 1 : 0);
    },
    intMinimum(state) {
        return arithmetic(state, (lhs, rhs) => lhs < rhs ? lhs : rhs);
    },
    intMaximum(state) {
        return arithmetic(state, (lhs, rhs) => lhs > rhs ? lhs : rhs);
    },
    intScale(state) {
        const values = popIntsTopFirst(state, 3);
        if( !values ) return 'error';
        const [c, b, a] = values;
        if( b === 0 ) return pushInt(state, 0);
        const quotient = (BigInt(c) * BigInt(a)) / BigInt(b);
        return pushInt(state, Number(BigInt.asIntN(32, quotient)));
    },
    intGetBitRange(state) {
        const values = popIntsTopFirst(state, 3);
        if( !values ) return 'error';
        const [high, low, value] = values;
        const mask = ((1 << ((high - low + 1) | 0)) - 1) | 0;
        return pushInt(state, (value >> low) & mask);
    },
    appendStrings(state) {
        const src = popText(state);
        if( src === null ) return 'error';
        const dest = popText(state);
        if( dest === null ) return 'error';
        return pushString(state,
            dest.slice(0, cStringLength(dest)) + src.slice(0, cStringLength(src)));
    },
    lowercaseAscii(state) {
        const text = popText(state);
        if( text === null ) return 'error';
        return pushString(state, lowercaseAscii(text));
    },
    intToString(state) {
        const value = popInt(state);
        if( value === null ) return 'error';
        return pushString(state, String(value | 0));
    },
    compareClientStrings(state) {
        const rhs = popText(state);
        if( rhs === null ) return 'error';
        const lhs = popText(state);
        if( lhs === null ) return 'error';
        return pushInt(state, compareClientStrings(lhs, rhs));
    },
    escapeMarkup(state) {
        const text = popText(state);
        if( text === null ) return 'error';
        return pushString(state, escapeClientMarkup(text));
    },
    stringLength(state) {
        const text = popText(state);
        if( text === null ) return 'error';
        return pushInt(state, cStringLength(text));
    },
    substring(state) {
        const end = popInt(state);
        if( end === null ) return 'error';
        const startValue = popInt(state);
        if( startValue === null ) return 'error';
        const text = popText(state);
        if( text === null ) return 'error';
        const length = cStringLength(text);
        const boundedEnd = end > length ? length : end;
        const start = Math.max(startValue, 0) > boundedEnd
            ? boundedEnd : Math.max(startValue, 0);
        /* The C handler makes start == end for a negative end. Its resulting
         * zero-byte duplicate is an empty string, despite the pointer itself
         * being before the source buffer. Preserve the observable result. */
        if( start === boundedEnd ) return pushString(state, '');
        return pushString(state, text.slice(start, boundedEnd));
    },
    removeTags(state) {
        const text = popText(state);
        if( text === null ) return 'error';
        return pushString(state, removeClientTags(text));
    },
    stringIndexOfString(state) {
        const startValue = popInt(state);
        if( startValue === null ) return 'error';
        const needle = popText(state);
        if( needle === null ) return 'error';
        const haystack = popText(state);
        if( haystack === null ) return 'error';
        const haystackLength = cStringLength(haystack);
        const needleLength = cStringLength(needle);
        if( needleLength === 0 ) return pushInt(state, -1);
        const start = Math.max(startValue, 0);
        if( start > haystackLength ) return pushInt(state, -1);
        return pushInt(state, haystack.slice(0, haystackLength).indexOf(
            needle.slice(0, needleLength), start));
    },
    onMobile(state) {
        return pushInt(state, 0);
    },
    clientType(state) {
        return pushInt(state, 10);
    },
    arrayLength(state) {
        const handle = popString(state);
        if( handle === null ) return 'error';
        return pushInt(state, arrayFromHandle(state, handle)?.size ?? 0);
    },
} satisfies CS2CoreIntrinsicHandlers<CS2CoreState, CS2CoreIntrinsicResult>;

const DISPATCH_BY_OPCODE: readonly (CS2CoreIntrinsic | undefined)[] = (() => {
    let maxOpcode = 0;
    for( const declaration of CS2_CORE_DISPATCH_DECLARATIONS )
        maxOpcode = Math.max(maxOpcode, declaration.opcode);
    const dispatch = new Array<CS2CoreIntrinsic | undefined>(maxOpcode + 1);
    for( const declaration of CS2_CORE_DISPATCH_DECLARATIONS ) {
        if( dispatch[declaration.opcode] )
            throw new Error(`duplicate generated CS2 core opcode ${declaration.opcode}`);
        const intrinsic = CS2_CORE_INTRINSICS[declaration.intrinsic] as CS2CoreIntrinsic | undefined;
        if( !intrinsic ) throw new Error(
            `generated CS2 intrinsic ${declaration.intrinsic satisfies CS2CoreIntrinsicName} is missing`);
        dispatch[declaration.opcode] = intrinsic;
    }
    return Object.freeze(dispatch);
})();

export const CS2_CORE_IMPLEMENTED_OPCODES: readonly number[] = Object.freeze(
    CS2_CORE_DISPATCH_DECLARATIONS.map((declaration) => declaration.opcode),
);

export function stepCS2Core(
    state: CS2CoreState,
    externalOpcodeExecutor?: CS2CoreExternalOpcodeExecutor,
): CS2CoreStepStatus {
    if( state.error ) return 'error';
    const frame = currentFrame(state);
    if( !frame ) return 'done';
    if( frame.pc >= frame.script.instructions.length ) return 'done';
    if( frame.pc < 0 || !Number.isInteger(frame.pc) ) {
        state.currentPc = frame.pc;
        state.currentOpcode = -1;
        fail(state, 'INVALID_PC', `CS2 program counter ${frame.pc} is invalid`);
        return 'error';
    }

    const pc = frame.pc;
    const instruction = frame.script.instructions[pc];
    state.currentPc = pc;
    state.currentOpcode = instruction.opcode;
    frame.pc = pc + 1;
    state.cycles++;

    const intrinsic = DISPATCH_BY_OPCODE[instruction.opcode];
    if( intrinsic ) return intrinsic(state, instruction) === 'ok' ? 'ok' : 'error';
    if( !externalOpcodeExecutor?.implementedOpcodes.has(instruction.opcode) ) {
        fail(state, 'UNSUPPORTED_OPCODE', `TypeScript CS2 core has no opcode ${instruction.opcode}`);
        return 'error';
    }
    try {
        const result = externalOpcodeExecutor.execute(state, instruction) as unknown;
        /* A Promise or yield token cannot outlive the synchronous HostRuntime
         * boundary. External implementations must finish before returning. */
        if( result !== undefined ) return fail(
            state,
            'EXTERNAL_OPCODE_FAILED',
            `TypeScript CS2 external opcode ${instruction.opcode} did not complete synchronously`,
        );
        return 'ok';
    } catch( error ) {
        const detail = error instanceof Error ? error.message : String(error);
        fail(state, 'EXTERNAL_OPCODE_FAILED',
            `TypeScript CS2 external opcode ${instruction.opcode} failed: ${detail}`);
        return 'error';
    }
}

export function runCS2Core(
    state: CS2CoreState,
    cycleLimit = CS2_CORE_DEFAULT_CYCLE_LIMIT,
    externalOpcodeExecutor?: CS2CoreExternalOpcodeExecutor,
): CS2CoreRunResult {
    if( !Number.isInteger(cycleLimit) || cycleLimit <= 0 )
        throw new RangeError('CS2 cycle limit must be a positive integer');
    const beginCycles = state.cycles;
    while( state.cycles - beginCycles < cycleLimit ) {
        const status = stepCS2Core(state, externalOpcodeExecutor);
        if( status === 'ok' ) continue;
        return Object.freeze({
            status: status === 'done' ? 'done' : 'error',
            cycles: state.cycles - beginCycles,
            state,
            error: state.error,
        });
    }

    const frame = currentFrame(state);
    state.currentPc = frame?.pc ?? -1;
    state.currentOpcode = -1;
    fail(state, 'CYCLE_LIMIT', `CS2 script exceeded ${cycleLimit} cycles`);
    return Object.freeze({
        status: 'error',
        cycles: state.cycles - beginCycles,
        state,
        error: state.error,
    });
}

export class CS2CoreVM {
    readonly dialect: CS2Dialect;
    readonly coverage: CS2CoreCoverage;
    readonly state: CS2CoreState;
    readonly externalOpcodeExecutor: CS2CoreExternalOpcodeExecutor | undefined;

    constructor(script: CS2CoreScript, options: CS2CoreVMOptions = {}) {
        this.dialect = options.dialect ?? 'canonical';
        this.externalOpcodeExecutor = options.externalOpcodeExecutor;
        this.coverage = analyzeCS2CoreScript(
            script, this.dialect, options.scripts,
            this.externalOpcodeExecutor?.implementedOpcodes,
        );
        if( !this.coverage.supported && !options.allowUnsupported )
            throw new CS2CoreUnsupportedOpcodeError(
                this.coverage.dialect,
                this.coverage.unsupportedOpcodes,
                this.coverage.missingScriptIds,
            );
        this.state = createCS2CoreState(script, options);
    }

    step(): CS2CoreStepStatus {
        return stepCS2Core(this.state, this.externalOpcodeExecutor);
    }

    run(cycleLimit = CS2_CORE_DEFAULT_CYCLE_LIMIT): CS2CoreRunResult {
        return runCS2Core(this.state, cycleLimit, this.externalOpcodeExecutor);
    }
}
