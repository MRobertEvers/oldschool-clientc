/*
 * Direct TypeScript execution for the reviewed CS2 Host slice.
 *
 * This is the stack/target half of the C VM's Host handlers, not another UI
 * implementation.  It pops in the same order as CS2VM2_Op_* and calls the
 * generated positional CS2Host surface.  A production in-memory tree can
 * implement those narrow methods without tagged request objects; the small
 * request adapter at the bottom exists for HostRuntime compatibility/tests.
 */

import type {
    CS2Host,
    CS2HostComponentRef,
    CS2HostRequestPayloadByKind,
    CS2HostRequestKind,
    CS2HostResultByKind,
} from './generated/cs2_host.js';

export const CS2_DIRECT_HOST_EXECUTABLE_OPCODES = Object.freeze([
    25, 42, 100, 102, 200,
    1000, 1001, 1003, 1005,
    1100, 1101, 1102, 1103, 1105, 1107, 1112, 1113, 1114, 1115,
    1300, 1301, 1302,
    1400, 1401, 1403, 1404, 1405, 1407, 1408, 1409, 1410, 1412, 1417, 1419,
    1501, 1503, 1702, 1704,
    2000, 2001, 2003, 2100, 2101, 2300, 2305,
    2407, 2408, 2409, 2417, 2502, 2503, 2604, 2704,
    3300, 3408, 3411, 6516,
] as const);

export type CS2DirectHostOpcode = (typeof CS2_DIRECT_HOST_EXECUTABLE_OPCODES)[number];

export const CS2_DIRECT_HOST_EXECUTABLE_OPCODE_SET: ReadonlySet<number> =
    new Set(CS2_DIRECT_HOST_EXECUTABLE_OPCODES);

const DIRECT_KINDS = [
    'PUSH_VARBIT', 'PUSH_VARC_INT', 'CC_CREATE', 'CC_DELETEALL', 'CC_FIND',
    'CC_SETPOSITION', 'CC_SETSIZE', 'CC_SETHIDE', 'CC_SETNOCLICKTHROUGH',
    'CC_SETSCROLLPOS', 'CC_SETCOLOUR', 'CC_SETFILL', 'CC_SETTRANS',
    'CC_SETGRAPHIC', 'CC_SETTILING', 'CC_SETTEXT', 'CC_SETTEXTFONT',
    'CC_SETTEXTALIGN', 'CC_SETTEXTSHADOW', 'CC_SETOP', 'CC_SETDRAGGABLE',
    'CC_SETDRAGGABLEBEHAVIOR', 'CC_SETONCLICK', 'CC_SETONHOLD',
    'CC_SETONMOUSEOVER', 'CC_SETONMOUSELEAVE', 'CC_SETONDRAG',
    'CC_SETONVARTRANSMIT', 'CC_SETONTIMER', 'CC_SETONOP',
    'CC_SETONDRAGCOMPLETE', 'CC_SETONMOUSEREPEAT', 'CC_SETONSCROLLWHEEL',
    'CC_SETONKEY', 'CC_GETY', 'CC_GETHEIGHT', 'CC_GETID',
    'CC_SETCOMPONENTPARAM', 'IF_SETPOSITION', 'IF_SETSIZE', 'IF_SETHIDE',
    'IF_SETSCROLLPOS', 'IF_SETCOLOUR', 'IF_SETOP', 'IF_SETOPBASE',
    'IF_SETONVARTRANSMIT', 'IF_SETONTIMER', 'IF_SETONOP',
    'IF_SETONSCROLLWHEEL', 'IF_GETWIDTH', 'IF_GETHEIGHT',
    'IF_GETSCROLLHEIGHT', 'IF_SETPARAM', 'CLIENTCLOCK', 'ENUM',
    'ENUM_GETOUTPUTCOUNT', 'STRUCT_PARAM',
] as const satisfies readonly CS2HostRequestKind[];

export type CS2DirectHostKind = (typeof DIRECT_KINDS)[number];
export type CS2DirectHost = Pick<CS2Host, CS2DirectHostKind>;

export type CS2DirectComponentTarget = CS2HostComponentRef | number | null;

export interface CS2DirectHostState {
    readonly intStack: number[];
    readonly stringStack: Array<string | null>;
    activeComponent: CS2DirectComponentTarget;
    dotComponent: CS2DirectComponentTarget;
    /** CC_CREATE has three arguments in the RS2 Dat2 dialect, four in OSRS. */
    dialect?: 'canonical' | 'rs2-dat2';
}

export interface CS2DirectHostInstruction {
    readonly opcode: number;
    readonly intOperand?: number;
}

export class CS2DirectHostError extends Error {
    readonly code: 'INT_STACK_UNDERFLOW' | 'STRING_STACK_UNDERFLOW' |
        'BAD_STRING_VALUE' | 'BAD_HOST_RESULT' | 'ASYNC_HOST_RESULT' |
        'UNSUPPORTED_OPCODE';
    readonly opcode: number;

    constructor(
        code: CS2DirectHostError['code'],
        opcode: number,
        message: string,
    ) {
        super(message);
        this.name = 'CS2DirectHostError';
        this.code = code;
        this.opcode = opcode;
    }
}

/** Execute one reviewed Host opcode. The complete closure must be gated first. */
export function executeCS2DirectHostInstruction(
    state: CS2DirectHostState,
    instruction: CS2DirectHostInstruction,
    host: CS2DirectHost,
): void {
    const opcode = instruction.opcode | 0;
    const operand = instruction.intOperand === undefined ? 0 : instruction.intOperand | 0;
    switch( opcode ) {
    case 25:
        pushInt(state, opcode, host.PUSH_VARBIT(operand));
        return;
    case 42:
        pushInt(state, opcode, host.PUSH_VARC_INT(operand));
        return;
    case 100:
    {
        const isNested = state.dialect === 'rs2-dat2' ? 0 : popInt(state, opcode);
        const childIndex = popInt(state, opcode);
        const componentType = popInt(state, opcode);
        const parentId = popInt(state, opcode);
        const result = host.CC_CREATE(
            parentId, componentType, childIndex, isNested, operand, 0);
        if( result ) setTarget(state, operand, result);
        return;
    }
    case 102:
        requireSynchronousVoid(opcode, host.CC_DELETEALL(popInt(state, opcode)));
        return;
    case 200:
    {
        const subId = popInt(state, opcode);
        const parentId = popInt(state, opcode);
        const result = host.CC_FIND(parentId, subId, operand);
        if( result ) setTarget(state, operand, result);
        pushInt(state, opcode, result ? 1 : 0);
        return;
    }
    case 1000:
    {
        const ymode = popInt(state, opcode);
        const xmode = popInt(state, opcode);
        const y = popInt(state, opcode);
        const x = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETPOSITION(targetId(state, operand), x, y, xmode, ymode));
        return;
    }
    case 1001:
    {
        const hmode = popInt(state, opcode);
        const wmode = popInt(state, opcode);
        const height = popInt(state, opcode);
        const width = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETSIZE(targetId(state, operand), width, height, wmode, hmode));
        return;
    }
    case 1003:
        requireSynchronousVoid(opcode,
            host.CC_SETHIDE(targetId(state, operand), popInt(state, opcode) !== 0));
        return;
    case 1005:
        requireSynchronousVoid(opcode,
            host.CC_SETNOCLICKTHROUGH(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1100:
    {
        const scrollY = popInt(state, opcode);
        const scrollX = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETSCROLLPOS(targetId(state, operand), scrollX, scrollY));
        return;
    }
    case 1101:
        requireSynchronousVoid(opcode,
            host.CC_SETCOLOUR(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1102:
        requireSynchronousVoid(opcode,
            host.CC_SETFILL(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1103:
        requireSynchronousVoid(opcode,
            host.CC_SETTRANS(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1105:
        requireSynchronousVoid(opcode,
            host.CC_SETGRAPHIC(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1107:
        requireSynchronousVoid(opcode,
            host.CC_SETTILING(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1112:
        requireSynchronousVoid(opcode,
            host.CC_SETTEXT(targetId(state, operand), popString(state, opcode)));
        return;
    case 1113:
        requireSynchronousVoid(opcode,
            host.CC_SETTEXTFONT(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1114:
    {
        const lineHeight = popInt(state, opcode);
        const yAlign = popInt(state, opcode);
        const xAlign = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETTEXTALIGN(targetId(state, operand), xAlign, yAlign, lineHeight));
        return;
    }
    case 1115:
        requireSynchronousVoid(opcode,
            host.CC_SETTEXTSHADOW(targetId(state, operand), popInt(state, opcode)));
        return;
    case 1300:
    {
        const text = popString(state, opcode);
        const index = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETOP(targetId(state, operand), index, text));
        return;
    }
    case 1301:
    {
        const childIndex = popInt(state, opcode);
        const parentUid = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.CC_SETDRAGGABLE(targetId(state, operand), parentUid, childIndex));
        return;
    }
    case 1302:
        requireSynchronousVoid(opcode, host.CC_SETDRAGGABLEBEHAVIOR(
            targetId(state, operand), popInt(state, opcode)));
        return;
    case 1400: runSetOn(state, opcode, operand, host, host.CC_SETONCLICK); return;
    case 1401: runSetOn(state, opcode, operand, host, host.CC_SETONHOLD); return;
    case 1403: runSetOn(state, opcode, operand, host, host.CC_SETONMOUSEOVER); return;
    case 1404: runSetOn(state, opcode, operand, host, host.CC_SETONMOUSELEAVE); return;
    case 1405: runSetOn(state, opcode, operand, host, host.CC_SETONDRAG); return;
    case 1407: runSetOn(state, opcode, operand, host, host.CC_SETONVARTRANSMIT); return;
    case 1408: runSetOn(state, opcode, operand, host, host.CC_SETONTIMER); return;
    case 1409: runSetOn(state, opcode, operand, host, host.CC_SETONOP); return;
    case 1410: runSetOn(state, opcode, operand, host, host.CC_SETONDRAGCOMPLETE); return;
    case 1412: runSetOn(state, opcode, operand, host, host.CC_SETONMOUSEREPEAT); return;
    case 1417: runSetOn(state, opcode, operand, host, host.CC_SETONSCROLLWHEEL); return;
    case 1419: runSetOn(state, opcode, operand, host, host.CC_SETONKEY); return;
    case 1501:
        pushInt(state, opcode, host.CC_GETY(targetId(state, operand)));
        return;
    case 1503:
        pushInt(state, opcode, host.CC_GETHEIGHT(targetId(state, operand)));
        return;
    case 1702:
        pushInt(state, opcode, host.CC_GETID(targetId(state, operand)));
        return;
    case 1704:
    {
        const valueKind = popInt(state, opcode);
        let value = 0;
        let stringValue: string | null = null;
        if( valueKind === 2 ) stringValue = popString(state, opcode);
        else value = popInt(state, opcode);
        const paramId = popInt(state, opcode);
        requireSynchronousVoid(opcode, host.CC_SETCOMPONENTPARAM(
            targetId(state, operand), paramId, value, stringValue, valueKind));
        return;
    }
    case 2000:
    {
        const componentId = popInt(state, opcode);
        const ymode = popInt(state, opcode);
        const xmode = popInt(state, opcode);
        const y = popInt(state, opcode);
        const x = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.IF_SETPOSITION(componentId, x, y, xmode, ymode));
        return;
    }
    case 2001:
    {
        const componentId = popInt(state, opcode);
        const hmode = popInt(state, opcode);
        const wmode = popInt(state, opcode);
        const height = popInt(state, opcode);
        const width = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.IF_SETSIZE(componentId, width, height, wmode, hmode));
        return;
    }
    case 2003:
    {
        const componentId = popInt(state, opcode);
        const hidden = popInt(state, opcode) !== 0;
        requireSynchronousVoid(opcode, host.IF_SETHIDE(componentId, hidden));
        return;
    }
    case 2100:
    {
        const componentId = popInt(state, opcode);
        const scrollY = popInt(state, opcode);
        const scrollX = popInt(state, opcode);
        requireSynchronousVoid(opcode,
            host.IF_SETSCROLLPOS(componentId, scrollX, scrollY));
        return;
    }
    case 2101:
    {
        const componentId = popInt(state, opcode);
        const colour = popInt(state, opcode);
        requireSynchronousVoid(opcode, host.IF_SETCOLOUR(componentId, colour));
        return;
    }
    case 2300:
    {
        const componentId = popInt(state, opcode);
        const index = popInt(state, opcode);
        const text = popString(state, opcode);
        requireSynchronousVoid(opcode, host.IF_SETOP(componentId, index, text));
        return;
    }
    case 2305:
    {
        const componentId = popInt(state, opcode);
        const text = popString(state, opcode);
        requireSynchronousVoid(opcode, host.IF_SETOPBASE(componentId, text));
        return;
    }
    case 2407: runIfSetOn(state, opcode, host, host.IF_SETONVARTRANSMIT); return;
    case 2408: runIfSetOn(state, opcode, host, host.IF_SETONTIMER); return;
    case 2409: runIfSetOn(state, opcode, host, host.IF_SETONOP); return;
    case 2417: runIfSetOn(state, opcode, host, host.IF_SETONSCROLLWHEEL); return;
    case 2502:
        pushInt(state, opcode, host.IF_GETWIDTH(popInt(state, opcode)));
        return;
    case 2503:
        pushInt(state, opcode, host.IF_GETHEIGHT(popInt(state, opcode)));
        return;
    case 2604:
        pushInt(state, opcode, host.IF_GETSCROLLHEIGHT(popInt(state, opcode)));
        return;
    case 2704:
    {
        let valueKind = popInt(state, opcode);
        /* C accepts both the descriptor char and the component-param tag. */
        const childIndex = popInt(state, opcode);
        const componentId = popInt(state, opcode);
        let value = 0;
        let stringValue: string | null = null;
        if( valueKind === 2 || valueKind === 115 ) {
            stringValue = popString(state, opcode);
            valueKind = 2;
        } else value = popInt(state, opcode);
        const paramId = popInt(state, opcode);
        /* Native currently consumes but does not resolve a non--1 child. */
        void childIndex;
        requireSynchronousVoid(opcode,
            host.IF_SETPARAM(componentId, paramId, value, stringValue, valueKind));
        return;
    }
    case 3300:
        pushInt(state, opcode, host.CLIENTCLOCK(0));
        return;
    case 3408:
    {
        const key = popInt(state, opcode);
        const enumId = popInt(state, opcode);
        const outputType = popInt(state, opcode);
        const inputType = popInt(state, opcode);
        const result = host.ENUM(inputType, outputType, enumId, key);
        /* Unlike STRUCT_PARAM's genuinely polymorphic result, ENUM's output
         * bank is selected by the requested descriptor. This mirrors
         * specialResultPattern() in the WASM bridge even if a bad Host returns
         * the opposite JavaScript primitive. */
        if( outputType === 115 ) pushStringResult(state, result);
        else pushInt(state, opcode, result);
        return;
    }
    case 3411:
        pushInt(state, opcode, host.ENUM_GETOUTPUTCOUNT(popInt(state, opcode)));
        return;
    case 6516:
    {
        const paramId = popInt(state, opcode);
        const structId = popInt(state, opcode);
        pushDynamic(state, opcode, host.STRUCT_PARAM(structId, paramId));
        return;
    }
    default:
        throw new CS2DirectHostError(
            'UNSUPPORTED_OPCODE', opcode,
            `TypeScript direct Host does not implement opcode ${opcode}`);
    }
}

type SetOnMethod = (
    componentId: number,
    scriptId: number,
    signature: string | null,
    triggerIds: readonly number[],
    triggerCount: number,
    intArgs: readonly number[],
    intArgCount: number,
    stringMask: readonly [low: number, high: number],
    stringArgCount: number,
    stringArgs: readonly string[],
) => void;

interface SetOnPayload {
    readonly scriptId: number;
    readonly signature: string | null;
    readonly triggerIds: readonly number[];
    readonly triggerCount: number;
    readonly intArgs: readonly number[];
    readonly intArgCount: number;
    readonly stringMask: readonly [low: number, high: number];
    readonly stringArgCount: number;
    readonly stringArgs: readonly string[];
}

function requireSynchronousVoid(opcode: number, result: unknown): void {
    if( (typeof result === 'object' && result !== null) || typeof result === 'function' ) {
        let then: unknown;
        try { then = (result as { readonly then?: unknown }).then; }
        catch {
            throw new CS2DirectHostError(
                'ASYNC_HOST_RESULT', opcode,
                `TypeScript direct Host opcode ${opcode} returned an unreadable thenable`);
        }
        if( typeof then === 'function' ) throw new CS2DirectHostError(
            'ASYNC_HOST_RESULT', opcode,
            `TypeScript direct Host opcode ${opcode} returned a Promise/thenable`);
    }
}

function runSetOn(
    state: CS2DirectHostState,
    opcode: number,
    operand: number,
    receiver: CS2DirectHost,
    method: SetOnMethod,
): void {
    const payload = parseSetOn(state, opcode);
    const result = method.call(
        receiver,
        targetId(state, operand), payload.scriptId, payload.signature,
        payload.triggerIds, payload.triggerCount, payload.intArgs,
        payload.intArgCount, payload.stringMask, payload.stringArgCount,
        payload.stringArgs,
    );
    requireSynchronousVoid(opcode, result);
}

function runIfSetOn(
    state: CS2DirectHostState,
    opcode: number,
    receiver: CS2DirectHost,
    method: SetOnMethod,
): void {
    /* IF_SETON* pops the explicit UID before the shared signature parser. */
    const componentId = popInt(state, opcode);
    const payload = parseSetOn(state, opcode);
    const result = method.call(
        receiver,
        componentId, payload.scriptId, payload.signature, payload.triggerIds,
        payload.triggerCount, payload.intArgs, payload.intArgCount,
        payload.stringMask, payload.stringArgCount, payload.stringArgs,
    );
    requireSynchronousVoid(opcode, result);
}

function parseSetOn(state: CS2DirectHostState, opcode: number): SetOnPayload {
    const signature = popString(state, opcode);
    const length = signature?.length ?? 0;
    const hasTriggers = length > 0 && signature![length - 1] === 'Y';
    let triggerCount = 0;
    let triggerIds: number[] = [];
    if( hasTriggers ) {
        triggerCount = popInt(state, opcode);
        if( triggerCount > 0 ) {
            triggerIds = new Array<number>(triggerCount);
            for( let index = triggerCount - 1; index >= 0; index-- )
                triggerIds[index] = popInt(state, opcode);
        }
    }

    const parseLength = hasTriggers ? length - 1 : length;
    const intArgs = new Array<number>(Math.min(parseLength, 64)).fill(0);
    const stringsByPosition = new Array<string | null>(Math.min(parseLength, 64));
    let intArgCount = 0;
    let lowMask = 0;
    let highMask = 0;
    for( let index = parseLength - 1; index >= 0; index-- ) {
        const character = signature![index];
        const string = character === 's' || character === 'W' || character === 'X';
        if( string ) {
            const value = popString(state, opcode);
            if( index < 64 ) {
                stringsByPosition[index] = value;
                if( index < 32 ) lowMask = (lowMask | 1 << index) | 0;
                else highMask = (highMask | 1 << (index - 32)) | 0;
                if( index + 1 > intArgCount ) intArgCount = index + 1;
            }
        } else {
            const value = popInt(state, opcode);
            if( index < 64 ) {
                intArgs[index] = value;
                if( index + 1 > intArgCount ) intArgCount = index + 1;
            }
        }
    }
    const scriptId = popInt(state, opcode);
    intArgs.length = intArgCount;
    const stringArgs: string[] = [];
    for( let index = 0; index < intArgCount && stringArgs.length < 16; index++ ) {
        const string = index < 32
            ? Boolean((lowMask >>> index) & 1)
            : Boolean((highMask >>> (index - 32)) & 1);
        if( string ) stringArgs.push(truncateHookString(stringsByPosition[index] ?? ''));
    }
    return {
        scriptId,
        signature,
        triggerIds,
        triggerCount,
        intArgs,
        intArgCount,
        stringMask: [lowMask, highMask],
        stringArgCount: stringArgs.length,
        stringArgs,
    };
}

const UTF8_ENCODER = new TextEncoder();
const UTF8_DECODER = new TextDecoder();

function truncateHookString(value: string): string {
    const encoded = UTF8_ENCODER.encode(value);
    return encoded.length <= 255 ? value : UTF8_DECODER.decode(encoded.subarray(0, 255));
}

function popInt(state: CS2DirectHostState, opcode: number): number {
    if( state.intStack.length === 0 ) throw new CS2DirectHostError(
        'INT_STACK_UNDERFLOW', opcode, `integer stack underflow at Host opcode ${opcode}`);
    return state.intStack.pop()! | 0;
}

function popString(state: CS2DirectHostState, opcode: number): string | null {
    if( state.stringStack.length === 0 ) throw new CS2DirectHostError(
        'STRING_STACK_UNDERFLOW', opcode, `string stack underflow at Host opcode ${opcode}`);
    const value: unknown = state.stringStack.pop();
    if( value === null || value === undefined ) return null;
    if( typeof value !== 'string' ) throw new CS2DirectHostError(
        'BAD_STRING_VALUE', opcode,
        `Host opcode ${opcode} cannot consume a CS2 array handle as a string`);
    return value;
}

function pushInt(state: CS2DirectHostState, opcode: number, value: unknown): void {
    const number = Number(value ?? 0);
    if( !Number.isFinite(number) ) throw new CS2DirectHostError(
        'BAD_HOST_RESULT', opcode, `Host opcode ${opcode} returned a non-finite integer`);
    state.intStack.push(Math.trunc(number) | 0);
}

function pushDynamic(state: CS2DirectHostState, opcode: number, value: unknown): void {
    if( typeof value === 'string' ) state.stringStack.push(value);
    else pushInt(state, opcode, value);
}

function pushStringResult(state: CS2DirectHostState, value: unknown): void {
    state.stringStack.push(String(value ?? ''));
}

function targetId(state: CS2DirectHostState, operand: number): number {
    const target = operand === 1 ? state.dotComponent : state.activeComponent;
    if( typeof target === 'number' ) return target | 0;
    if( target && Number.isInteger(target.componentId) ) return target.componentId | 0;
    return -1;
}

function setTarget(
    state: CS2DirectHostState,
    operand: number,
    target: CS2HostComponentRef,
): void {
    if( operand === 1 ) state.dotComponent = target;
    else state.activeComponent = target;
}

export type CS2FlatHostRequest<K extends CS2DirectHostKind = CS2DirectHostKind> = {
    readonly [P in K]: Readonly<{ kind: P }> & CS2HostRequestPayloadByKind[P];
}[K];

/** Tagged reflected HostRuntime shape, intentionally outside the hot API. */
export interface CS2RequestHost {
    request<K extends CS2DirectHostKind>(
        request: CS2FlatHostRequest<K>,
    ): CS2HostResultByKind[K];
}

const REQUEST_FIELDS = Object.freeze({
    PUSH_VARBIT: ['varbit_id'],
    PUSH_VARC_INT: ['varc_id'],
    CC_CREATE: ['parent_id', 'component_type', 'child_index', 'is_nested', 'dot_operand',
        'parent_is_sibling'],
    CC_DELETEALL: ['component_id'],
    CC_FIND: ['parent_id', 'sub_id', 'dot_operand'],
    CC_SETPOSITION: ['component_id', 'x', 'y', 'xmode', 'ymode'],
    CC_SETSIZE: ['component_id', 'width', 'height', 'wmode', 'hmode'],
    CC_SETHIDE: ['component_id', 'hidden'],
    CC_SETNOCLICKTHROUGH: ['component_id', 'enabled'],
    CC_SETSCROLLPOS: ['component_id', 'scroll_x', 'scroll_y'],
    CC_SETCOLOUR: ['component_id', 'colour'],
    CC_SETFILL: ['component_id', 'filled'],
    CC_SETTRANS: ['component_id', 'trans'],
    CC_SETGRAPHIC: ['component_id', 'graphic_id'],
    CC_SETTILING: ['component_id', 'tiling'],
    CC_SETTEXT: ['component_id', 'text'],
    CC_SETTEXTFONT: ['component_id', 'font_id'],
    CC_SETTEXTALIGN: ['component_id', 'x_align', 'y_align', 'line_height'],
    CC_SETTEXTSHADOW: ['component_id', 'shadowed'],
    CC_SETOP: ['component_id', 'index', 'text'],
    CC_SETDRAGGABLE: ['component_id', 'parent_uid', 'child_index'],
    CC_SETDRAGGABLEBEHAVIOR: ['component_id', 'behavior'],
    CC_SETONCLICK: setOnFields(),
    CC_SETONHOLD: setOnFields(),
    CC_SETONMOUSEOVER: setOnFields(),
    CC_SETONMOUSELEAVE: setOnFields(),
    CC_SETONDRAG: setOnFields(),
    CC_SETONVARTRANSMIT: setOnFields(),
    CC_SETONTIMER: setOnFields(),
    CC_SETONOP: setOnFields(),
    CC_SETONDRAGCOMPLETE: setOnFields(),
    CC_SETONMOUSEREPEAT: setOnFields(),
    CC_SETONSCROLLWHEEL: setOnFields(),
    CC_SETONKEY: setOnFields(),
    CC_GETY: ['component_id'],
    CC_GETHEIGHT: ['component_id'],
    CC_GETID: ['component_id'],
    CC_SETCOMPONENTPARAM: ['component_id', 'param_id', 'value', 'str_value', 'value_kind'],
    IF_SETPOSITION: ['component_id', 'x', 'y', 'xmode', 'ymode'],
    IF_SETSIZE: ['component_id', 'width', 'height', 'wmode', 'hmode'],
    IF_SETHIDE: ['component_id', 'hidden'],
    IF_SETSCROLLPOS: ['component_id', 'scroll_x', 'scroll_y'],
    IF_SETCOLOUR: ['component_id', 'colour'],
    IF_SETOP: ['component_id', 'index', 'text'],
    IF_SETOPBASE: ['component_id', 'text'],
    IF_SETONVARTRANSMIT: setOnFields(),
    IF_SETONTIMER: setOnFields(),
    IF_SETONOP: setOnFields(),
    IF_SETONSCROLLWHEEL: setOnFields(),
    IF_GETWIDTH: ['component_id'],
    IF_GETHEIGHT: ['component_id'],
    IF_GETSCROLLHEIGHT: ['component_id'],
    IF_SETPARAM: ['component_id', 'param_id', 'value', 'str_value', 'value_kind'],
    CLIENTCLOCK: ['_unused'],
    ENUM: ['input_type', 'output_type', 'enum_id', 'key'],
    ENUM_GETOUTPUTCOUNT: ['enum_id'],
    STRUCT_PARAM: ['struct_id', 'param_id'],
} satisfies Record<CS2DirectHostKind, readonly string[]>);

function setOnFields(): readonly string[] {
    return [
        'component_id', 'script_id', 'signature', 'trigger_ids', 'trigger_count',
        'int_args', 'int_arg_count', 'str_arg_mask', 'str_arg_count', 'str_args',
    ];
}

/**
 * Adapt the existing tagged request surface. Production TS execution should
 * eventually have HostRuntime implement CS2DirectHost itself; this compatibility
 * layer intentionally keeps object allocation visible rather than hiding it in
 * the VM and claiming it is free.
 */
export function createCS2RequestHostAdapter(target: CS2RequestHost): CS2DirectHost {
    const direct = {} as Record<CS2DirectHostKind, (...args: unknown[]) => unknown>;
    for( const kind of DIRECT_KINDS ) {
        const fields = REQUEST_FIELDS[kind];
        direct[kind] = (...args: unknown[]): unknown => {
            const request: Record<string, unknown> = { kind };
            for( let index = 0; index < fields.length; index++ )
                request[fields[index]] = args[index];
            return target.request(
                request as unknown as CS2FlatHostRequest<CS2DirectHostKind>);
        };
    }
    return direct as unknown as CS2DirectHost;
}
