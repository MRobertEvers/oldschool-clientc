/* Browser adapter for the focused C CS2VM2 WebAssembly module.
 *
 * C owns bytecode decoding/execution. JavaScript owns component identity and
 * the React-style IR: each reflected HOST request is synchronously forwarded
 * to HostRuntime, then its typed result is copied back into the C thread.
 */

import { CS2_COMMANDS } from './cs2_commands.js';

const ABI = Object.freeze({
    dialectCanonical: 0,
    dialectRs2Dat2: 1,
    runDone: 0,
    runYielded: 1,
    runError: 2,
    hostOk: 0,
    hostError: -1,
    fieldI32: 1,
    fieldBool: 2,
    fieldU8: 3,
    fieldString: 4,
    fieldI32Array: 5,
    fieldI32Pointer: 6,
    fieldU64: 7,
    fieldStringArray: 8,
});

const EVENT_I32 = Object.freeze({
    mouseX: 0,
    mouseY: 1,
    componentId: 2,
    componentSubId: 3,
    opIndex: 4,
    dragTargetId: 5,
    dragTargetSubId: 6,
    keyTyped: 7,
    keyPressed: 8,
    opSubIndex: 9,
    windowMode: 10,
    defaultWindowMode: 11,
});

const EVENT_STRING_OPBASE = 0;
const MAX_REFLECTED_FIELDS = 256;
const MAX_REFLECTED_VALUES = 4096;
const MAX_CHILD_ITERATOR = 256;
const FAST_RECORD_WORDS = 12;
const FAST_QUERY_INVENTORY = 1;
const FAST_QUERY_CHILDREN = 2;
const FAST_QUERY_SCALAR_MIN = 3;
const FAST_QUERY_SCALAR_MAX = 6;
const FAST_QUERY_MISSING = -2;
const FAST_HOOK_STRING_LENGTH = 256;
const UTF8_ENCODER = new TextEncoder();
const UTF8_DECODER = new TextDecoder();
const DIRECT_REFLECTION_CACHE = new WeakMap();
const HEAP_VIEW_CACHE = new WeakMap();
const TARGET_COMMANDS = new Set([
    'CC_CREATE', 'CC_COPY', 'CC_CREATECHILD', 'CC_CREATESIBLING',
    'CC_FIND', 'IF_FIND', 'OVERLAY_CC_CREATE', 'OVERLAY_FIND', 'OVERLAY_CC_FIND',
    'CC_CHILDREN_FINDNEXT', 'IF_CHILDREN_FIND', 'MINIMENU_FINDCOMPONENT',
]);
const CHILD_ITERATOR_COMMANDS = new Set([
    'IF_CHILDREN_FIND', 'IF_CHILDREN_COLLECT', 'CC_CHILDREN_FIND_COUNT',
]);
const POLYMORPHIC_RESULTS = new Set([
    'CC_GETPARAM', 'NC_PARAM', 'LC_PARAM', 'OC_PARAM', 'STRUCT_PARAM',
]);
const BASE_RESULTS = Object.freeze({
    PUSH_VAR: 'i',
    PUSH_VARBIT: 'i',
    PUSH_VARC_INT: 'i',
    PUSH_VARC_STRING: 's',
    PUSH_VARC_STRING_OLD: 's',
    PUSH_VARCLANSETTING: 'i',
    PUSH_VARCLAN: 'i',
});
const DB_REQUESTS = new Set([
    'DB_FIND_WITH_COUNT', 'DB_FINDNEXT', 'DB_GETFIELD', 'DB_GETFIELDCOUNT',
    'DB_FINDALL_WITH_COUNT', 'DB_GETROWTABLE', 'DB_GETROW',
    'DB_FIND_FILTER_WITH_COUNT', 'DB_FIND', 'DB_FINDALL', 'DB_FIND_FILTER',
]);
const DB_FIND_REQUESTS = new Set([
    'DB_FIND_WITH_COUNT', 'DB_FIND_FILTER_WITH_COUNT', 'DB_FIND', 'DB_FIND_FILTER',
]);
/* cachepack's legacy command type ids overstate these result arities. The
 * current C opcode stack/native handlers push one integer for each; trusting
 * the legacy table leaves extra zeroes on the C VM stack after an otherwise
 * correct JavaScript HOST call. */
const LEGACY_SCALAR_RESULT_REQUESTS = new Set([
    'WORLDMAP_GETSOURCECOORD', 'WORLDMAP_GETNEARESTICON', 'MEC_SPRITE',
]);

let defaultModulePromise = null;

export class WasmCS2RuntimeError extends Error {
    constructor(message, code = 'WASM_CS2VM', detail = {}) {
        super(message);
        this.name = 'WasmCS2RuntimeError';
        this.code = code;
        Object.assign(this, detail);
    }
}

/** Load a program into an isolated C session. The returned hook API is sync. */
export async function createWasmCS2Runtime({
    program,
    host,
    moduleFactory = null,
    moduleUrl = '/cs2vm-wasm/cs2vm_wasm.js',
    wasmUrl = '/cs2vm-wasm/cs2vm_wasm.wasm',
    fastHost = true,
} = {}) {
    validateProgram(program);
    if( !host || typeof host.request !== 'function' )
        throw new WasmCS2RuntimeError('C CS2VM/WASM requires a synchronous HostRuntime', 'BAD_HOST');

    const bundle = moduleFactory
        ? await instantiateModule(moduleFactory, wasmUrl)
        : await defaultModule(moduleUrl, wasmUrl);
    const runtime = new WasmSession(bundle, program, host, fastHost);
    try { runtime.load(); }
    catch( error ) {
        runtime.destroy();
        throw error;
    }
    return runtime;
}

class WasmSession {
    constructor(bundle, program, host, fastHost) {
        this.bundle = bundle;
        this.api = bundle.api;
        this.program = program;
        this.host = host;
        this.session = 0;
        this.destroyed = false;
        this.hostErrors = new Map();
        this.fastHost = Boolean(fastHost && typeof this.api._cs2w_invocation_set_fast_host === 'function' &&
            typeof host.fastHostInventorySnapshot === 'function' &&
            typeof host.fastHostChildrenSnapshot === 'function' &&
            typeof host.fastHostValueSnapshot === 'function' &&
            typeof host.fastHostScalarDataValue === 'function' &&
            typeof host.fastHostScalarDataCacheable === 'function' &&
            typeof host.requestFastBatch === 'function');
    }

    load() {
        const dialect = normalizeDialect(this.program.dialect);
        const revision = normalizeRevision(this.program.revision);
        this.session = this.api._cs2w_session_create(dialect, revision) | 0;
        if( !this.session ) throw new WasmCS2RuntimeError(
            'C CS2VM/WASM could not allocate a session', 'CREATE');
        this.bundle.sessions.set(this.session, this);

        for( const record of this.program.scripts ) {
            const bytes = programBytes(record.data);
            const loaded = withBytes(this.api, bytes, (pointer) =>
                this.api._cs2w_session_load_script(
                    this.session, scriptId(record.id), pointer, bytes.length));
            if( !loaded ) throw this.sessionError(`could not load script ${record.id}`, 'DECODE');
        }
        if( !this.api._cs2w_session_seal(this.session) )
            throw this.sessionError('could not seal the C script registry', 'SEAL');
    }

    invokeIntent(intent) {
        this.assertLive();
        const component = hostRef(this.host, intent?.component);
        const componentId = packedComponentId(component);
        this.host.setActive(component);
        this.host.setActive(component, { dot: true });
        const viewport = this.host.viewport || { width: 512, height: 334 };
        const invocation = this.api._cs2w_invocation_create(
            this.session,
            scriptId(intent?.hook?.scriptId),
            componentId,
            componentId,
            positiveDimension(viewport.width, 512),
            positiveDimension(viewport.height, 334),
        ) | 0;
        if( !invocation ) throw this.sessionError(
            `could not create invocation for script ${intent?.hook?.scriptId}`, 'INVOKE');
        if( this.fastHost && !this.api._cs2w_invocation_set_fast_host(invocation, 1) )
            throw new WasmCS2RuntimeError(
                'C CS2VM/WASM rejected the fast HOST transaction', 'INVOKE');

        this.hostErrors.delete(invocation);
        try {
            for( const raw of intent?.hook?.args || [] ) this.addArgument(invocation, raw);
            this.setEvent(invocation, intent, component);
            const status = this.api._cs2w_invocation_run(invocation) | 0;
            if( status === ABI.runYielded ) throw new WasmCS2RuntimeError(
                'C CS2VM/WASM yielded from a synchronous browser HOST request', 'YIELD');
            if( status !== ABI.runDone ) throw this.invocationError(invocation);
            return {
                status: 'done',
                scriptId: intent.hook.scriptId,
                hostRequests: this.api._cs2w_invocation_host_call_count(invocation) | 0,
            };
        } finally {
            this.hostErrors.delete(invocation);
            this.api._cs2w_invocation_destroy(invocation);
        }
    }

    addArgument(invocation, raw) {
        const typed = raw && typeof raw === 'object' && 'type' in raw && 'value' in raw;
        const value = typed ? raw.value : raw;
        const string = typed
            ? ['string', 's', 'text'].includes(String(raw.type).toLowerCase())
            : typeof value === 'string';
        const ok = string
            ? withString(this.api, value ?? '', (pointer) =>
                this.api._cs2w_invocation_add_string_arg(invocation, pointer))
            : this.api._cs2w_invocation_add_int_arg(invocation, intArgument(value));
        if( !ok ) throw new WasmCS2RuntimeError('C CS2VM/WASM rejected a hook argument', 'ARGUMENT');
    }

    setEvent(invocation, intent, component) {
        const locals = intent?.locals || {};
        const drag = hostRef(this.host, intent?.dragTarget, false);
        const session = this.host.session || {};
        const windowMode = Number(session.windowMode);
        const defaultWindowMode = Number(session.defaultWindowMode);
        const fields = [
            [EVENT_I32.mouseX, locals.eventMouseX ?? locals.mouseX ?? 0],
            [EVENT_I32.mouseY, locals.eventMouseY ?? locals.mouseY ?? 0],
            [EVENT_I32.componentId, packedComponentId(component)],
            [EVENT_I32.componentSubId, component?.subId ?? -1],
            [EVENT_I32.opIndex, locals.opIndex ?? 1],
            [EVENT_I32.dragTargetId, packedComponentId(drag)],
            [EVENT_I32.dragTargetSubId, drag?.subId ?? -1],
            [EVENT_I32.keyTyped, locals.keyTyped ?? -1],
            [EVENT_I32.keyPressed, locals.keyPressed ?? -1],
            [EVENT_I32.opSubIndex, locals.opSubIndex ?? 0],
            [EVENT_I32.windowMode, windowMode === 1 || windowMode === 2 ? windowMode : 2],
            [EVENT_I32.defaultWindowMode,
                defaultWindowMode === 1 || defaultWindowMode === 2 ? defaultWindowMode : 2],
        ];
        for( const [field, value] of fields ) if( !this.api._cs2w_invocation_set_event_i32(
            invocation, field, Number(value) | 0) ) throw new WasmCS2RuntimeError(
            `C CS2VM/WASM rejected event field ${field}`, 'EVENT');

        let opBase = '';
        try { opBase = this.host.read('if_getopbase', component) ?? ''; }
        catch { /* Components with no op-base expose an empty ScriptEvent value. */ }
        const stringOk = withString(this.api, opBase, (pointer) =>
            this.api._cs2w_invocation_set_event_string(
                invocation, EVENT_STRING_OPBASE, pointer));
        if( !stringOk ) throw new WasmCS2RuntimeError(
            'C CS2VM/WASM rejected the event operation base', 'EVENT');
    }

    hostExec(invocation, thread, requestPointer, kind) {
        try {
            const request = reflectRequest(
                this.api, requestPointer, kind, thread, this.bundle.requestSchemas);
            normalizeSetOn(request);
            normalizeDbRequest(this.api, thread, request);
            const result = this.host.request(request);
            if( result && typeof result.then === 'function' )
                throw new WasmCS2RuntimeError('HostRuntime returned a Promise', 'ASYNC_HOST');
            writeHostResult(this.api, thread, kind, request, result, this.host);
            return ABI.hostOk;
        } catch( error ) {
            this.hostErrors.set(invocation, error);
            return ABI.hostError;
        }
    }

    fastQuery(invocation, queryKind, key, outputPointer, capacity) {
        try {
            const width = queryKind === FAST_QUERY_INVENTORY ? 3
                : queryKind === FAST_QUERY_CHILDREN ? 2
                    : queryKind >= FAST_QUERY_SCALAR_MIN && queryKind <= FAST_QUERY_SCALAR_MAX
                        ? 1 : 0;
            if( !width || !Number.isInteger(capacity) || capacity < 0 || capacity > 65536 )
                throw new WasmCS2RuntimeError('invalid fast HOST snapshot query', 'FAST_HOST');
            const rows = queryKind === FAST_QUERY_INVENTORY
                ? this.host.fastHostInventorySnapshot(key)
                : queryKind === FAST_QUERY_CHILDREN
                    ? this.host.fastHostChildrenSnapshot(key)
                    : this.host.fastHostValueSnapshot(queryKind, key);
            if( rows === null ) return FAST_QUERY_MISSING;
            if( !(rows instanceof Int32Array) || rows.length % width !== 0 )
                throw new WasmCS2RuntimeError('malformed fast HOST snapshot', 'FAST_HOST');
            const count = rows.length / width;
            const copyCount = Math.min(count, capacity);
            const views = currentHeapViews(this.api);
            const address = Number(outputPointer) >>> 0;
            heapBounds(views, address, copyCount * width * 4, 'fast HOST snapshot');
            if( address % 4 !== 0 ) throw new WasmCS2RuntimeError(
                'unaligned fast HOST snapshot output', 'FAST_HOST');
            views.i32.set(rows.subarray(0, copyCount * width), address >>> 2);
            return count | 0;
        } catch( error ) {
            this.hostErrors.set(invocation, error);
            return ABI.hostError;
        }
    }

    fastScalarQuery(invocation, requestKind, a, b, c, intOutputPointer,
        stringOutputPointer, stringCapacity, stringLengthOutputPointer,
        cacheableOutputPointer) {
        try {
            const result = this.host.fastHostScalarDataValue(requestKind, a, b, c);
            if( result && typeof result.then === 'function' )
                throw new WasmCS2RuntimeError('HostRuntime returned a Promise', 'ASYNC_HOST');
            const views = currentHeapViews(this.api);
            const intAddress = Number(intOutputPointer) >>> 0;
            const lengthAddress = Number(stringLengthOutputPointer) >>> 0;
            const cacheableAddress = Number(cacheableOutputPointer) >>> 0;
            heapBounds(views, intAddress, 4, 'fast HOST scalar integer result');
            heapBounds(views, lengthAddress, 4, 'fast HOST scalar string length');
            heapBounds(views, cacheableAddress, 4, 'fast HOST scalar cacheability');
            if( intAddress % 4 !== 0 || lengthAddress % 4 !== 0 ||
                cacheableAddress % 4 !== 0 )
                throw new WasmCS2RuntimeError(
                    'unaligned fast HOST scalar result', 'FAST_HOST');
            views.data.setInt32(cacheableAddress,
                this.host.fastHostScalarDataCacheable(requestKind, a, b, c) ? 1 : 0, true);
            if( typeof result === 'string' ) {
                const bytes = UTF8_ENCODER.encode(result);
                views.data.setInt32(lengthAddress, bytes.length, true);
                if( !Number.isInteger(stringCapacity) || stringCapacity < 0 ||
                    stringCapacity > 1048576 ) throw new WasmCS2RuntimeError(
                    'invalid fast HOST scalar string capacity', 'FAST_HOST');
                if( bytes.length < stringCapacity ) {
                    const stringAddress = Number(stringOutputPointer) >>> 0;
                    heapBounds(views, stringAddress, bytes.length + 1,
                        'fast HOST scalar string result');
                    views.u8.set(bytes, stringAddress);
                    views.u8[stringAddress + bytes.length] = 0;
                }
                return 2;
            }
            views.data.setInt32(intAddress, Number(result) | 0, true);
            return 1;
        } catch( error ) {
            this.hostErrors.set(invocation, error);
            return ABI.hostError;
        }
    }

    fastFlush(invocation, recordsPointer, recordCount, arenaPointer, arenaSize) {
        try {
            if( !Number.isInteger(recordCount) || recordCount < 0 || recordCount > 65536 ||
                !Number.isInteger(arenaSize) || arenaSize < 0 )
                throw new WasmCS2RuntimeError('invalid fast HOST transaction', 'FAST_HOST');
            const views = currentHeapViews(this.api);
            const records = Number(recordsPointer) >>> 0;
            const arena = Number(arenaPointer) >>> 0;
            heapBounds(views, records, recordCount * FAST_RECORD_WORDS * 4,
                'fast HOST records');
            heapBounds(views, arena, arenaSize, 'fast HOST payload');
            if( records % 4 !== 0 ) throw new WasmCS2RuntimeError(
                'unaligned fast HOST records', 'FAST_HOST');
            /* HostRuntime can consume the C heap view synchronously. This
             * avoids materialising thousands of short-lived request objects
             * for one bank redraw. The host is forbidden from retaining these
             * borrowed views: a later C allocation may grow/detach the heap. */
            if( typeof this.host.requestFastPackedBatch === 'function' ) {
                const packedRecords = views.i32.subarray(records >>> 2,
                    (records >>> 2) + recordCount * FAST_RECORD_WORDS);
                const packedArena = views.u8.subarray(arena, arena + arenaSize);
                const result = this.host.requestFastPackedBatch(
                    packedRecords, recordCount, packedArena);
                if( result && typeof result.then === 'function' )
                    throw new WasmCS2RuntimeError('HostRuntime returned a Promise', 'ASYNC_HOST');
                return ABI.hostOk;
            }
            const requests = new Array(recordCount);
            for( let index = 0; index < recordCount; index++ ) {
                const base = records + index * FAST_RECORD_WORDS * 4;
                const word = (offset) => heapI32(views, base + offset * 4,
                    'fast HOST record');
                const kind = word(0);
                let request;
                if( kind === 100 ) request = {
                    kind: 'CC_CREATE', parent_id: word(1), component_type: word(2),
                    child_index: word(3), is_nested: word(4), dot_operand: word(5),
                    _fast_token: word(7), _fast_previous_id: word(8),
                    _fast_previous_temporary: Boolean(word(9)),
                };
                else if( kind === 200 ) request = {
                    kind: 'CC_FIND', parent_id: word(1), sub_id: word(2),
                    dot_operand: Boolean(word(3)), expected_component_id: word(4),
                };
                else if( kind === 1000 || kind === 2000 ) request = {
                    kind: kind === 1000 ? 'CC_SETPOSITION' : 'IF_SETPOSITION',
                    component_id: word(1), x: word(2), y: word(3),
                    xmode: word(4), ymode: word(5),
                };
                else if( kind === 1001 || kind === 2001 ) request = {
                    kind: kind === 1001 ? 'CC_SETSIZE' : 'IF_SETSIZE', component_id: word(1),
                    width: word(2), height: word(3), wmode: word(4), hmode: word(5),
                };
                else if( kind === 1003 || kind === 2003 ) request = {
                    kind: kind === 1003 ? 'CC_SETHIDE' : 'IF_SETHIDE',
                    component_id: word(1), hidden: word(2),
                };
                else if( kind === 1103 || kind === 2103 ) request = {
                    kind: kind === 1103 ? 'CC_SETTRANS' : 'IF_SETTRANS',
                    component_id: word(1), trans: word(2),
                };
                else if( kind === 1101 ) request = {
                    kind: 'CC_SETCOLOUR', component_id: word(1), colour: word(2),
                };
                else if( kind === 1102 ) request = {
                    kind: 'CC_SETFILL', component_id: word(1), filled: word(2),
                };
                else if( kind === 1105 ) request = {
                    kind: 'CC_SETGRAPHIC', component_id: word(1), graphic_id: word(2),
                };
                else if( kind === 1112 ) request = {
                    kind: 'CC_SETTEXT', component_id: word(1),
                    text: decodeFastString(views, arena, arenaSize, word),
                };
                else if( kind === 1113 ) request = {
                    kind: 'CC_SETTEXTFONT', component_id: word(1), font_id: word(2),
                };
                else if( kind === 1114 ) request = {
                    kind: 'CC_SETTEXTALIGN', component_id: word(1),
                    x_align: word(2), y_align: word(3), line_height: word(4),
                };
                else if( kind === 1115 ) request = {
                    kind: 'CC_SETTEXTSHADOW', component_id: word(1), shadowed: word(2),
                };
                else if( kind === 1200 || kind === 1205 || kind === 1212 ) request = {
                    kind: kind === 1200 ? 'CC_SETOBJECT'
                        : kind === 1205 ? 'CC_SETOBJECT_NONUM' : 'CC_SETOBJECT_ALWAYS_NUM',
                    component_id: word(1), obj_id: word(2), count: word(3), num_mode: word(4),
                };
                else if( kind === 1307 || kind === 2307 ) request = {
                    kind: kind === 1307 ? 'CC_CLEAROPS' : 'IF_CLEAROPS',
                    component_id: word(1),
                };
                else if( kind === 1300 || kind === 1305 ) request = {
                    kind: kind === 1300 ? 'CC_SETOP' : 'CC_SETOPBASE',
                    component_id: word(1), index: word(2),
                    text: decodeFastString(views, arena, arenaSize, word),
                };
                else if( kind === 1302 || kind === 1303 || kind === 1304 ) request = {
                    kind: kind === 1302 ? 'CC_SETDRAGGABLEBEHAVIOR'
                        : kind === 1303 ? 'CC_SETDRAGDEADZONE' : 'CC_SETDRAGDEADTIME',
                    component_id: word(1), behavior: word(2), zone: word(2), time: word(2),
                };
                else if( kind === 1403 || kind === 1404 || kind === 1405 || kind === 1409 ||
                    kind === 1410 || kind === 1412 || kind === 2403 ||
                    kind === 2404 || kind === 2409 ) request = decodeFastHook(
                    views, arena, arenaSize, word, kind === 1403 ? 'CC_SETONMOUSEOVER'
                        : kind === 1404 ? 'CC_SETONMOUSELEAVE'
                            : kind === 1405 ? 'CC_SETONDRAG'
                                : kind === 1409 ? 'CC_SETONOP'
                                    : kind === 1410 ? 'CC_SETONDRAGCOMPLETE'
                                    : kind === 1412 ? 'CC_SETONMOUSEREPEAT'
                            : kind === 2403 ? 'IF_SETONMOUSEOVER'
                                : kind === 2404 ? 'IF_SETONMOUSELEAVE' : 'IF_SETONOP');
                else throw new WasmCS2RuntimeError(
                    `unsupported fast HOST record ${kind}`, 'FAST_HOST');
                if( word(11) ) request._fast_temporary_component = true;
                if( kind === 1403 || kind === 1404 || kind === 1405 || kind === 1409 ||
                    kind === 1410 ||
                    kind === 1412 || kind === 2403 || kind === 2404 || kind === 2409 )
                    normalizeSetOn(request);
                requests[index] = request;
            }
            const result = this.host.requestFastBatch(requests);
            if( result && typeof result.then === 'function' )
                throw new WasmCS2RuntimeError('HostRuntime returned a Promise', 'ASYNC_HOST');
            for( let index = 0; index < requests.length; index++ ) {
                if( requests[index].kind !== 'CC_CREATE' ) continue;
                views.i32[(records >>> 2) + index * FAST_RECORD_WORDS + 6] =
                    Number(requests[index].result_component_id ?? -1) | 0;
            }
            return ABI.hostOk;
        } catch( error ) {
            this.hostErrors.set(invocation, error);
            return ABI.hostError;
        }
    }

    invocationError(invocation) {
        const hostError = this.hostErrors.get(invocation);
        const script = this.api._cs2w_invocation_error_script_id(invocation) | 0;
        const pc = this.api._cs2w_invocation_error_pc(invocation) | 0;
        const opcode = this.api._cs2w_invocation_error_opcode(invocation) | 0;
        const detail = `script ${script} pc ${pc} opcode ${opcode}`;
        return new WasmCS2RuntimeError(
            `${hostError?.message || 'C CS2VM execution failed'} (${detail})`,
            hostError ? 'HOST' : 'VM', { scriptId: script, pc, opcode, cause: hostError });
    }

    sessionError(prefix, code) {
        const pointer = this.api._cs2w_session_last_error_message(this.session);
        const detail = readCString(this.api, pointer);
        return new WasmCS2RuntimeError(detail ? `${prefix}: ${detail}` : prefix, code, {
            sessionError: this.api._cs2w_session_last_error(this.session) | 0,
        });
    }

    assertLive() {
        if( this.destroyed || !this.session )
            throw new WasmCS2RuntimeError('C CS2VM/WASM session is destroyed', 'DESTROYED');
    }

    destroy() {
        if( this.destroyed ) return;
        this.destroyed = true;
        if( this.session ) {
            this.bundle.sessions.delete(this.session);
            this.api._cs2w_session_destroy(this.session);
            this.session = 0;
        }
        this.hostErrors.clear();
    }
}

async function defaultModule(moduleUrl, wasmUrl) {
    if( !defaultModulePromise ) defaultModulePromise = import(moduleUrl).then((loaded) => {
        if( typeof loaded.default !== 'function' ) throw new WasmCS2RuntimeError(
            'C CS2VM/WASM module has no default factory', 'MODULE');
        return instantiateModule(loaded.default, wasmUrl);
    }).catch((error) => {
        defaultModulePromise = null;
        throw error;
    });
    return defaultModulePromise;
}

async function instantiateModule(factory, wasmUrl) {
    const sessions = new Map();
    const api = await factory({
        locateFile(path) {
            return String(path).endsWith('.wasm') ? wasmUrl : `/cs2vm-wasm/${path}`;
        },
        cs2HostExec(session, invocation, thread, request, kind) {
            const runtime = sessions.get(Number(session));
            return runtime
                ? runtime.hostExec(Number(invocation), Number(thread), Number(request), Number(kind))
                : ABI.hostError;
        },
        cs2FastHostQuery(session, invocation, queryKind, key, output, capacity) {
            const runtime = sessions.get(Number(session));
            return runtime ? runtime.fastQuery(Number(invocation), Number(queryKind), Number(key),
                Number(output), Number(capacity)) : ABI.hostError;
        },
        cs2FastHostScalarQuery(session, invocation, requestKind, a, b, c,
            intOutput, stringOutput, stringCapacity, stringLengthOutput, cacheableOutput) {
            const runtime = sessions.get(Number(session));
            return runtime ? runtime.fastScalarQuery(Number(invocation), Number(requestKind),
                Number(a), Number(b), Number(c), Number(intOutput), Number(stringOutput),
                Number(stringCapacity), Number(stringLengthOutput),
                Number(cacheableOutput)) : ABI.hostError;
        },
        cs2FastHostFlush(session, invocation, records, recordCount, arena, arenaSize) {
            const runtime = sessions.get(Number(session));
            return runtime ? runtime.fastFlush(Number(invocation), Number(records),
                Number(recordCount), Number(arena), Number(arenaSize)) : ABI.hostError;
        },
    });
    validateModule(api);
    /* Request names, field names, and field types come from the compiled host
     * schema and are immutable for the lifetime of a module. Keep them beside
     * the shared Emscripten instance instead of reflecting and UTF-8 decoding
     * the same metadata for every C -> JavaScript HOST call. */
    return { api, sessions, requestSchemas: new Map() };
}

function validateModule(api) {
    const required = [
        '_malloc', '_free', '_cs2w_session_create', '_cs2w_session_destroy',
        '_cs2w_session_load_script', '_cs2w_session_seal', '_cs2w_session_last_error',
        '_cs2w_session_last_error_message', '_cs2w_invocation_create',
        '_cs2w_invocation_destroy', '_cs2w_invocation_add_int_arg',
        '_cs2w_invocation_add_string_arg', '_cs2w_invocation_set_event_i32',
        '_cs2w_invocation_set_event_string', '_cs2w_invocation_run',
        '_cs2w_invocation_error_opcode', '_cs2w_invocation_error_pc',
        '_cs2w_invocation_error_script_id', '_cs2w_invocation_host_call_count',
        '_cs2w_request_kind_name', '_cs2w_request_field_count',
        '_cs2w_request_field_name', '_cs2w_request_field_kind',
        '_cs2w_request_field_length', '_cs2w_request_field_i32',
        '_cs2w_request_field_string', '_cs2w_thread_pop_int',
        '_cs2w_thread_pop_string', '_cs2w_thread_push_int',
        '_cs2w_thread_push_string', '_cs2w_thread_set_target',
        '_cs2w_thread_set_children',
        '_cs2w_thread_current_operand',
    ];
    if( !api?.HEAPU8 ) throw new WasmCS2RuntimeError(
        'C CS2VM/WASM module does not export HEAPU8', 'MODULE');
    for( const name of required ) if( typeof api[name] !== 'function' )
        throw new WasmCS2RuntimeError(`C CS2VM/WASM module is missing ${name}`, 'MODULE');
}

function reflectRequest(api, pointer, kind, thread, schemas = null) {
    const schema = reflectedSchema(api, kind, schemas);
    const result = { kind: schema.name, opcode: kind };
    const views = schema.direct ? currentHeapViews(api) : null;
    const rawRequestAddress = Number(pointer);
    const requestAddress = rawRequestAddress >>> 0;
    if( schema.direct && (!Number.isInteger(rawRequestAddress) || requestAddress === 0) )
        throw new WasmCS2RuntimeError(
            `HOST request ${schema.name} has an invalid memory address`, 'REFLECT');
    for( const field of schema.fields ) {
        let length;
        if( schema.direct ) length = directFieldLength(
            views, requestAddress, field, schema.name);
        else {
            length = field.fixedLength;
            if( length !== null ) {
                result[field.outputName] = reflectedValue(
                    api, pointer, field.index, field.kind, length);
                continue;
            }
            length = api._cs2w_request_field_length(pointer, field.index) | 0;
            if( length < 0 || length > MAX_REFLECTED_VALUES ) throw new WasmCS2RuntimeError(
                `HOST request ${schema.name} has invalid field ${field.index}`, 'REFLECT');
        }
        result[field.outputName] = schema.direct
            ? directReflectedValue(api, views, requestAddress, field, length, schema.name)
            : reflectedValue(api, pointer, field.index, field.kind, length);
    }
    /* Several producer structs carry an exact dot target. Keep that value:
     * the current bytecode operand is only a fallback for requests whose
     * schema has no dot_operand field (for example a plain state read). */
    if( Object.prototype.hasOwnProperty.call(result, 'dot_operand') )
        result.dot_operand = Boolean(result.dot_operand);
    else result.dot_operand = Boolean(api._cs2w_thread_current_operand(thread));
    return result;
}

function reflectedSchema(api, kind, schemas) {
    const cached = schemas?.get(kind);
    if( cached ) return cached;

    const name = readCString(api, api._cs2w_request_kind_name(kind)).toUpperCase();
    if( !name ) throw new WasmCS2RuntimeError(`HOST request ${kind} has no name`, 'REFLECT');
    const count = api._cs2w_request_field_count(kind) | 0;
    if( count < 0 || count > MAX_REFLECTED_FIELDS ) throw new WasmCS2RuntimeError(
        `HOST request ${name} has invalid field count ${count}`, 'REFLECT');
    const fields = [];
    const direct = directReflectionAvailable(api);
    for( let index = 0; index < count; index++ ) {
        const fieldName = readCString(api, api._cs2w_request_field_name(kind, index));
        const fieldKind = api._cs2w_request_field_kind(kind, index) | 0;
        if( !fieldName || !validReflectedFieldKind(fieldKind) ) throw new WasmCS2RuntimeError(
            `HOST request ${name} has invalid field ${index}`, 'REFLECT');
        const layout = direct ? reflectedFieldLayout(api, kind, index, fieldKind, name) : null;
        fields.push(Object.freeze({
            index,
            kind: fieldKind,
            outputName: fieldName === 'kind' ? 'value_kind' : fieldName,
            /* Only arrays carry a request-specific length. Scalar, string,
             * boolean, byte and u64 shapes are fixed by the compiled schema,
             * so asking C for their length on every call is pure boundary
             * overhead. */
            fixedLength: reflectedFieldLength(fieldKind),
            ...layout,
        }));
    }
    const schema = Object.freeze({ name, direct, fields: Object.freeze(fields) });
    schemas?.set(kind, schema);
    return schema;
}

function directReflectionAvailable(api) {
    if( DIRECT_REFLECTION_CACHE.has(api) ) return DIRECT_REFLECTION_CACHE.get(api);
    const exports = [
        '_cs2w_request_field_offset', '_cs2w_request_field_capacity',
        '_cs2w_request_field_stride', '_cs2w_request_field_count_offset',
        '_cs2w_request_pointer_size',
    ];
    const available = exports.every((name) => typeof api?.[name] === 'function') &&
        (api._cs2w_request_pointer_size() | 0) === 4;
    DIRECT_REFLECTION_CACHE.set(api, available);
    return available;
}

function reflectedFieldLayout(api, kind, index, fieldKind, requestName) {
    const offset = api._cs2w_request_field_offset(kind, index) | 0;
    const capacity = api._cs2w_request_field_capacity(kind, index) | 0;
    const stride = api._cs2w_request_field_stride(kind, index) | 0;
    const countOffset = api._cs2w_request_field_count_offset(kind, index) | 0;
    const scalarCapacity = fieldKind === ABI.fieldU64 ? 2 : 1;
    const validCapacity = capacity >= 0 && capacity <= MAX_REFLECTED_VALUES;
    const validStride = stride >= 0 &&
        (fieldKind !== ABI.fieldStringArray || (stride > 0 && stride <= 65536));
    const validShape = fieldKind === ABI.fieldI32Pointer
        ? capacity === 0
        : fieldKind === ABI.fieldI32Array || fieldKind === ABI.fieldStringArray
            ? true : capacity === scalarCapacity;
    if( offset < 0 || countOffset < -1 || !validCapacity || !validStride || !validShape )
        throw new WasmCS2RuntimeError(
            `HOST request ${requestName} has invalid layout for field ${index}`, 'REFLECT');
    return { offset, capacity, stride, countOffset };
}

function validReflectedFieldKind(kind) {
    return kind >= ABI.fieldI32 && kind <= ABI.fieldStringArray;
}

function reflectedFieldLength(kind) {
    if( kind === ABI.fieldI32Array || kind === ABI.fieldI32Pointer ||
        kind === ABI.fieldStringArray ) return null;
    return kind === ABI.fieldU64 ? 2 : 1;
}

function currentHeapViews(api) {
    const heap = api?.HEAPU8;
    if( !(heap instanceof Uint8Array) ) throw new WasmCS2RuntimeError(
        'C CS2VM/WASM module has no byte-addressable heap', 'REFLECT');
    let views = HEAP_VIEW_CACHE.get(api);
    if( !views || views.buffer !== heap.buffer || views.byteOffset !== heap.byteOffset ||
        views.byteLength !== heap.byteLength ) {
        views = {
            buffer: heap.buffer,
            byteOffset: heap.byteOffset,
            byteLength: heap.byteLength,
            u8: heap,
            data: new DataView(heap.buffer, heap.byteOffset, heap.byteLength),
            i32: heap.byteOffset % 4 === 0
                ? new Int32Array(heap.buffer, heap.byteOffset, Math.floor(heap.byteLength / 4))
                : null,
            u32: heap.byteOffset % 4 === 0
                ? new Uint32Array(heap.buffer, heap.byteOffset, Math.floor(heap.byteLength / 4))
                : null,
        };
        HEAP_VIEW_CACHE.set(api, views);
    }
    return views;
}

function directFieldLength(views, request, field, requestName) {
    let length = field.capacity;
    if( field.countOffset >= 0 ) {
        length = heapI32(views, request + field.countOffset, requestName, field.index);
        if( length < 0 ) return 0;
        if( field.capacity > 0 && length > field.capacity ) length = field.capacity;
        else if( field.capacity === 0 && length > MAX_REFLECTED_VALUES )
            length = MAX_REFLECTED_VALUES;
    }
    if( length < 0 || length > MAX_REFLECTED_VALUES ) throw new WasmCS2RuntimeError(
        `HOST request ${requestName} has invalid field ${field.index}`, 'REFLECT');
    return length;
}

function directReflectedValue(api, views, request, field, length, requestName) {
    const address = request + field.offset;
    const detail = `${requestName} field ${field.index}`;
    switch( field.kind ) {
    case ABI.fieldI32: return heapI32(views, address, detail);
    case ABI.fieldBool: return heapU8(views, address, detail) ? 1 : 0;
    case ABI.fieldU8: return heapU8(views, address, detail);
    case ABI.fieldString:
    {
        const pointer = heapU32(views, address, detail);
        return pointer ? readCString(api, pointer) : null;
    }
    case ABI.fieldI32Array:
        return directI32Array(views, address, length, detail);
    case ABI.fieldI32Pointer:
    {
        const pointer = heapU32(views, address, detail);
        if( !pointer ) return new Array(length).fill(0);
        return directI32Array(views, pointer, length, detail);
    }
    case ABI.fieldU64:
        return [heapI32(views, address, detail), heapI32(views, address + 4, detail)];
    case ABI.fieldStringArray:
    {
        heapBounds(views, address, length * field.stride, detail);
        const result = new Array(length);
        for( let element = 0; element < length; element++ )
            result[element] = readCString(api, address + element * field.stride, field.stride);
        return result;
    }
    default: throw new WasmCS2RuntimeError(
        `unsupported reflected HOST field kind ${field.kind}`, 'REFLECT');
    }
}

function directI32Array(views, address, length, detail) {
    heapBounds(views, address, length * 4, detail);
    const result = new Array(length);
    if( views.i32 && address % 4 === 0 ) {
        const start = address >>> 2;
        for( let element = 0; element < length; element++ )
            result[element] = views.i32[start + element];
    } else for( let element = 0; element < length; element++ )
        result[element] = views.data.getInt32(address + element * 4, true);
    return result;
}

function heapBounds(views, address, bytes, detail) {
    if( !Number.isSafeInteger(address) || !Number.isSafeInteger(bytes) || address < 0 ||
        bytes < 0 || address > views.byteLength - bytes ) throw new WasmCS2RuntimeError(
        `WASM request memory is out of bounds (${detail})`, 'REFLECT');
}

function heapI32(views, address, detail, fieldIndex = '') {
    heapBounds(views, address, 4, fieldIndex === '' ? detail : `${detail} field ${fieldIndex}`);
    return views.i32 && address % 4 === 0
        ? views.i32[address >>> 2] : views.data.getInt32(address, true);
}

function heapU32(views, address, detail) {
    heapBounds(views, address, 4, detail);
    return views.u32 && address % 4 === 0
        ? views.u32[address >>> 2] : views.data.getUint32(address, true);
}

function heapU8(views, address, detail) {
    heapBounds(views, address, 1, detail);
    return views.u8[address];
}

function reflectedValue(api, pointer, index, kind, length) {
    if( kind === ABI.fieldString ) {
        const value = api._cs2w_request_field_string(pointer, index, 0);
        return value ? readCString(api, value) : null;
    }
    if( kind === ABI.fieldStringArray ) {
        const result = new Array(length);
        for( let element = 0; element < length; element++ ) {
            const value = api._cs2w_request_field_string(pointer, index, element);
            result[element] = value ? readCString(api, value) : null;
        }
        return result;
    }
    if( kind === ABI.fieldU64 ) return [
        api._cs2w_request_field_i32(pointer, index, 0) | 0,
        api._cs2w_request_field_i32(pointer, index, 1) | 0,
    ];
    if( kind === ABI.fieldI32Array || kind === ABI.fieldI32Pointer ) {
        const result = new Array(length);
        for( let element = 0; element < length; element++ )
            result[element] = api._cs2w_request_field_i32(pointer, index, element) | 0;
        return result;
    }
    if( kind === ABI.fieldI32 || kind === ABI.fieldBool || kind === ABI.fieldU8 )
        return api._cs2w_request_field_i32(pointer, index, 0) | 0;
    throw new WasmCS2RuntimeError(`unsupported reflected HOST field kind ${kind}`, 'REFLECT');
}

function decodeFastString(views, arena, arenaSize, word) {
    const offset = word(3);
    const length = word(4);
    if( offset < 0 || length < 0 || offset > arenaSize || length > arenaSize - offset )
        throw new WasmCS2RuntimeError('malformed fast HOST string record', 'FAST_HOST');
    const start = arena + offset;
    heapBounds(views, start, length, 'fast HOST string');
    return UTF8_DECODER.decode(views.u8.subarray(start, start + length));
}

function decodeFastHook(views, arena, arenaSize, word, kind) {
    const triggerCount = word(3);
    const intCount = word(4);
    const stringCount = word(7);
    const signatureLength = word(8);
    const payloadOffset = word(9);
    const payloadLength = word(10);
    if( triggerCount < 0 || triggerCount > MAX_REFLECTED_VALUES ||
        intCount < 0 || intCount > 64 || stringCount < 0 || stringCount > 16 ||
        signatureLength < 0 || signatureLength > 65 || payloadOffset < 0 ||
        payloadLength < 4 || payloadOffset > arenaSize - payloadLength )
        throw new WasmCS2RuntimeError('malformed fast HOST hook record', 'FAST_HOST');
    let cursor = arena + payloadOffset;
    const storedSignatureLength = heapI32(views, cursor, 'fast HOST hook signature');
    cursor += 4;
    if( storedSignatureLength !== signatureLength ) throw new WasmCS2RuntimeError(
        'fast HOST hook signature length mismatch', 'FAST_HOST');
    heapBounds(views, cursor, signatureLength, 'fast HOST hook signature');
    const signature = UTF8_DECODER.decode(views.u8.subarray(cursor, cursor + signatureLength));
    cursor += (signatureLength + 3) & ~3;
    const trigger_ids = directI32Array(views, cursor, triggerCount, 'fast HOST hook triggers');
    cursor += triggerCount * 4;
    const int_args = directI32Array(views, cursor, intCount, 'fast HOST hook integers');
    cursor += intCount * 4;
    const str_args = new Array(stringCount);
    for( let index = 0; index < stringCount; index++ ) {
        heapBounds(views, cursor, FAST_HOOK_STRING_LENGTH, 'fast HOST hook string');
        let end = cursor;
        const limit = cursor + FAST_HOOK_STRING_LENGTH;
        while( end < limit && views.u8[end] !== 0 ) end++;
        str_args[index] = UTF8_DECODER.decode(views.u8.subarray(cursor, end));
        cursor = limit;
    }
    if( cursor > arena + payloadOffset + payloadLength ) throw new WasmCS2RuntimeError(
        'fast HOST hook payload overflow', 'FAST_HOST');
    return {
        kind, component_id: word(1), script_id: word(2), signature,
        trigger_ids, trigger_count: triggerCount,
        int_args, int_arg_count: intCount,
        str_arg_mask: [word(5), word(6)], str_arg_count: stringCount, str_args,
    };
}

function normalizeSetOn(request) {
    if( !/^(?:CC|IF)_(?:INPUT_)?SETON/.test(request.kind) ) return;
    const ints = Array.isArray(request.int_args) ? request.int_args : [];
    const strings = Array.isArray(request.str_args) ? request.str_args : [];
    const count = Math.max(0, Number(request.int_arg_count ?? ints.length) | 0);
    const mask = Array.isArray(request.str_arg_mask) ? request.str_arg_mask : [0, 0];
    let stringAt = 0;
    request.args = Array.from({ length: count }, (_, index) => {
        const string = index < 32
            ? Boolean((mask[0] >>> index) & 1)
            : Boolean((mask[1] >>> (index - 32)) & 1);
        return string ? strings[stringAt++] ?? '' : ints[index] ?? 0;
    });
    request.triggerIds = Array.isArray(request.trigger_ids) ? request.trigger_ids : [];
}

/* DB bytecode deliberately reaches the C HOST with its operands still on the
 * thread stacks: the search type tag chooses whether its value is an int or a
 * string. Pop here in the same reverse-source order as exec_db(), then expose
 * stable named fields to the JavaScript DB store. */
function normalizeDbRequest(api, thread, request) {
    if( !DB_REQUESTS.has(request.kind) ) return request;

    if( request.kind === 'DB_GETROW' ) {
        request.index = popThreadInt(api, thread, 'DB_GETROW index');
    } else if( request.kind === 'DB_GETROWTABLE' ) {
        request.rowId = popThreadInt(api, thread, 'DB_GETROWTABLE row');
    } else if( request.kind === 'DB_GETFIELDCOUNT' ) {
        request.column = popThreadInt(api, thread, 'DB_GETFIELDCOUNT column');
        request.rowId = popThreadInt(api, thread, 'DB_GETFIELDCOUNT row');
    } else if( request.kind === 'DB_GETFIELD' ) {
        request.index = popThreadInt(api, thread, 'DB_GETFIELD index');
        request.column = popThreadInt(api, thread, 'DB_GETFIELD column');
        request.rowId = popThreadInt(api, thread, 'DB_GETFIELD row');
    } else if( request.kind === 'DB_FINDALL' || request.kind === 'DB_FINDALL_WITH_COUNT' ) {
        request.tableId = popThreadInt(api, thread, `${request.kind} table`);
    } else if( DB_FIND_REQUESTS.has(request.kind) ) {
        request.typeTag = popThreadInt(api, thread, `${request.kind} type tag`);
        request.value = request.typeTag === 2
            ? popThreadString(api, thread, `${request.kind} string value`)
            : popThreadInt(api, thread, `${request.kind} integer value`);
        request.column = popThreadInt(api, thread, `${request.kind} column`);
    }
    return request;
}

function popThreadInt(api, thread, description) {
    return withI32Out(api, (pointer, view) => {
        if( !api._cs2w_thread_pop_int(thread, pointer) ) throw new WasmCS2RuntimeError(
            `C CS2VM could not pop ${description}`, 'HOST_STACK');
        return view.getInt32(0, true);
    });
}

function popThreadString(api, thread, description) {
    return withI32Out(api, (pointer, view) => {
        if( !api._cs2w_thread_pop_string(thread, pointer) ) throw new WasmCS2RuntimeError(
            `C CS2VM could not pop ${description}`, 'HOST_STACK');
        return readCString(api, view.getUint32(0, true));
    });
}

function writeHostResult(api, thread, kind, request, result, host) {
    if( CHILD_ITERATOR_COMMANDS.has(request.kind) ) {
        const refs = Array.isArray(result) ? result : host?.childIteration?.refs || [];
        const children = new Array(Math.min(refs.length, MAX_CHILD_ITERATOR));
        for( let index = 0; index < children.length; index++ )
            children[index] = childSubId(refs[index]);
        const parent = request.uid ?? request.parent_id ?? request.parentId;
        const ok = withInt32Array(api, children, (pointer) =>
            api._cs2w_thread_set_children(thread, resultInt(parent), pointer, children.length));
        if( !ok ) throw new WasmCS2RuntimeError(
            'C CS2VM rejected a HOST child iterator result', 'HOST_RESULT');
        /* The C handlers for 211/212 push their own count after HOST returns.
         * IF_CHILDREN_FIND has no stack result, but (like the desktop HOST)
         * selects its parent as the current CC target. */
        if( request.kind.startsWith('IF_') ) api._cs2w_thread_set_target(
            thread, request.dot_operand ? 1 : 0, resultInt(parent));
        return;
    }

    if( request.kind === 'DB_GETFIELD' ) {
        writeDbFieldResult(api, thread, result);
        return;
    }

    const command = CS2_COMMANDS.get(kind);
    const pattern = specialResultPattern(request) ?? BASE_RESULTS[request.kind] ?? command?.defs ?? '';
    if( request.kind === 'MINIMENU_FINDCOMPONENT' && result ) {
        const ref = host?.activeRef?.() || null;
        if( ref ) {
            const id = packedComponentId(ref);
            /* The C client latches both implicit component operands regardless
             * of the opcode's dot bit, then pushes the boolean success value. */
            api._cs2w_thread_set_target(thread, 0, id);
            api._cs2w_thread_set_target(thread, 1, id);
        }
    }
    if( TARGET_COMMANDS.has(request.kind) ) {
        const ref = resultRef(result);
        if( ref ) api._cs2w_thread_set_target(
            thread, request.dot_operand ? 1 : 0, packedComponentId(ref));
    }
    if( !pattern ) return;
    let values;
    if( TARGET_COMMANDS.has(request.kind) && pattern === 'i' ) values = [result ? 1 : 0];
    else values = resultValues(pattern, result);
    for( let index = 0; index < pattern.length; index++ ) {
        const value = values[index];
        const type = pattern[index] === '?' ? (typeof value === 'string' ? 's' : 'i') : pattern[index];
        if( type === 's' ) {
            const ok = withString(api, value ?? '', (pointer) =>
                api._cs2w_thread_push_string(thread, pointer));
            if( !ok ) throw new WasmCS2RuntimeError('C CS2VM rejected a HOST string result', 'HOST_RESULT');
        } else {
            const ok = api._cs2w_thread_push_int(thread, resultInt(value));
            if( !ok ) throw new WasmCS2RuntimeError('C CS2VM rejected a HOST integer result', 'HOST_RESULT');
        }
    }
}

/* DB_GETFIELD is the one HOST result whose arity and stack banks are selected
 * by cache data rather than static opcode metadata. The DB handler returns its
 * exact field-order signature alongside the parallel values. */
function writeDbFieldResult(api, thread, result) {
    const pattern = result?.pattern;
    const values = result?.values;
    if( typeof pattern !== 'string' || !Array.isArray(values) || pattern.length === 0 ||
        pattern.length !== values.length || pattern.length > MAX_REFLECTED_VALUES ||
        !/^[is]+$/.test(pattern) ) throw new WasmCS2RuntimeError(
        'DB_GETFIELD HOST result must be non-empty { pattern: "is...", values: [...] }',
        'HOST_RESULT');

    for( let index = 0; index < pattern.length; index++ ) {
        if( pattern[index] === 's' ) {
            const ok = withString(api, values[index] ?? '', (pointer) =>
                api._cs2w_thread_push_string(thread, pointer));
            if( !ok ) throw new WasmCS2RuntimeError(
                'C CS2VM rejected a DB_GETFIELD string result', 'HOST_RESULT');
        } else if( !api._cs2w_thread_push_int(thread, resultInt(values[index])) ) {
            throw new WasmCS2RuntimeError(
                'C CS2VM rejected a DB_GETFIELD integer result', 'HOST_RESULT');
        }
    }
}

function specialResultPattern(request) {
    if( request.kind === 'ENUM' ) return Number(request.output_type) === 's'.charCodeAt(0) ? 's' : 'i';
    if( request.kind === 'CC_GETCOMPONENTPARAM' ) return 'i';
    if( POLYMORPHIC_RESULTS.has(request.kind) ) return '?';
    if( LEGACY_SCALAR_RESULT_REQUESTS.has(request.kind) ) return 'i';
    return null;
}

function childSubId(value) {
    const ref = resultRef(value) || value;
    const subId = Number(ref?.subId ?? ref?.sub_id ?? ref?.childIndex ?? ref?.child_index);
    if( !Number.isInteger(subId) || subId < -0x80000000 || subId > 0x7fffffff )
        throw new WasmCS2RuntimeError('HOST child iterator returned an invalid sub-id', 'HOST_RESULT');
    return subId | 0;
}

function resultValues(pattern, result) {
    if( Array.isArray(result) ) return result;
    if( result && Array.isArray(result.values) ) return result.values;
    if( result && typeof result === 'object' &&
        (Array.isArray(result.ints) || Array.isArray(result.strings)) ) {
        const ints = result.ints || [];
        const strings = result.strings || [];
        const values = new Array(pattern.length);
        let intIndex = 0;
        let stringIndex = 0;
        for( let index = 0; index < pattern.length; index++ )
            values[index] = pattern[index] === 's'
                ? strings[stringIndex++] : ints[intIndex++];
        return values;
    }
    return [result];
}

function resultInt(value) {
    const ref = resultRef(value);
    if( ref ) return packedComponentId(ref);
    if( typeof value === 'boolean' ) return value ? 1 : 0;
    const number = Number(value ?? 0);
    if( !Number.isFinite(number) ) throw new WasmCS2RuntimeError(
        `HOST integer result ${value} is not finite`, 'HOST_RESULT');
    return Math.trunc(number) | 0;
}

function resultRef(value) {
    if( !value || typeof value !== 'object' ) return null;
    const candidate = value.target ?? value.ref ?? value;
    return Number.isInteger(candidate?.componentId) || Number.isInteger(candidate?.component_id) ||
        typeof candidate?.key === 'string' ? candidate : null;
}

function hostRef(host, value, required = true) {
    let ref = null;
    try { ref = host.ref(value); }
    catch { ref = value?.ref || value || null; }
    if( !ref ) ref = value?.ref || value || null;
    if( required && !ref ) throw new WasmCS2RuntimeError('hook component reference is missing', 'BAD_HOOK');
    return ref;
}

function packedComponentId(value) {
    if( value === null || value === undefined ) return -1;
    const id = value.componentId ?? value.component_id;
    if( Number.isInteger(id) ) return id | 0;
    if( Number.isInteger(value) ) return value | 0;
    throw new WasmCS2RuntimeError('component has no packed id', 'BAD_COMPONENT');
}

function intArgument(value) {
    if( value && typeof value === 'object' ) return packedComponentId(value.ref || value);
    if( typeof value === 'boolean' ) return value ? 1 : 0;
    const number = Number(value ?? 0);
    if( !Number.isFinite(number) ) throw new WasmCS2RuntimeError(
        `hook integer argument ${value} is not finite`, 'BAD_HOOK');
    return Math.trunc(number) | 0;
}

function validateProgram(program) {
    if( !program || program.available !== true || !Array.isArray(program.scripts) )
        throw new WasmCS2RuntimeError('original clientscript bytecode is unavailable', 'NO_PROGRAM');
}

function normalizeDialect(value) {
    const dialect = String(value || 'canonical').toLowerCase().replaceAll('_', '-');
    if( ['canonical', 'osrs', 'oldschool'].includes(dialect) ) return ABI.dialectCanonical;
    if( ['rs2', 'rs2-dat2', '634'].includes(dialect) ) return ABI.dialectRs2Dat2;
    throw new WasmCS2RuntimeError(`unsupported CS2 dialect ${value}`, 'BAD_PROGRAM');
}

function normalizeRevision(value) {
    if( Number.isInteger(value) ) return value;
    const match = /(\d+)/.exec(String(value ?? ''));
    return match ? Number(match[1]) : 0;
}

function scriptId(value) {
    const id = Number(value);
    if( !Number.isSafeInteger(id) || id < 0 || id > 0x7fffffff )
        throw new WasmCS2RuntimeError(`invalid script id ${value}`, 'BAD_PROGRAM');
    return id;
}

function positiveDimension(value, fallback) {
    const number = Number(value);
    return Number.isInteger(number) && number > 0 ? number : fallback;
}

function programBytes(value) {
    if( value instanceof Uint8Array ) return value;
    if( value instanceof ArrayBuffer ) return new Uint8Array(value);
    const binary = atob(String(value || ''));
    const bytes = new Uint8Array(binary.length);
    for( let index = 0; index < binary.length; index++ ) bytes[index] = binary.charCodeAt(index);
    if( bytes.length === 0 ) throw new WasmCS2RuntimeError('clientscript bytecode is empty', 'BAD_PROGRAM');
    return bytes;
}

function withBytes(api, bytes, callback) {
    const pointer = api._malloc(Math.max(1, bytes.length));
    if( !pointer ) throw new WasmCS2RuntimeError('WASM byte allocation failed', 'OUT_OF_MEMORY');
    try {
        api.HEAPU8.set(bytes, pointer);
        return callback(pointer);
    } finally { api._free(pointer); }
}

function withInt32Array(api, values, callback) {
    if( values.length === 0 ) return callback(0);
    const pointer = api._malloc(values.length * 4);
    if( !pointer ) throw new WasmCS2RuntimeError('WASM integer allocation failed', 'OUT_OF_MEMORY');
    try {
        new Int32Array(api.HEAPU8.buffer, pointer, values.length).set(values);
        return callback(pointer);
    } finally { api._free(pointer); }
}

function withI32Out(api, callback) {
    const pointer = api._malloc(4);
    if( !pointer ) throw new WasmCS2RuntimeError('WASM stack-result allocation failed', 'OUT_OF_MEMORY');
    try {
        const view = new DataView(api.HEAPU8.buffer, pointer, 4);
        view.setUint32(0, 0, true);
        return callback(pointer, view);
    } finally { api._free(pointer); }
}

function withString(api, value, callback) {
    const bytes = UTF8_ENCODER.encode(String(value ?? ''));
    const pointer = api._malloc(bytes.length + 1);
    if( !pointer ) throw new WasmCS2RuntimeError('WASM string allocation failed', 'OUT_OF_MEMORY');
    try {
        api.HEAPU8.set(bytes, pointer);
        api.HEAPU8[pointer + bytes.length] = 0;
        return callback(pointer);
    } finally { api._free(pointer); }
}

function readCString(api, pointer, maxBytes = 65536) {
    pointer = Number(pointer);
    if( !pointer ) return '';
    const heap = api.HEAPU8;
    if( !Number.isSafeInteger(pointer) || pointer < 0 || pointer >= heap.length ||
        !Number.isSafeInteger(maxBytes) || maxBytes <= 0 ) throw new WasmCS2RuntimeError(
        'WASM string pointer is out of bounds', 'REFLECT');
    let end = pointer;
    const limit = Math.min(heap.length, pointer + maxBytes);
    while( end < limit && heap[end] !== 0 ) end++;
    if( end === limit ) throw new WasmCS2RuntimeError('WASM string is not terminated', 'REFLECT');
    return UTF8_DECODER.decode(heap.subarray(pointer, end));
}

export const __wasmRuntimeTest = Object.freeze({
    normalizeRevision,
    normalizeDialect,
    normalizeSetOn,
    normalizeDbRequest,
    reflectedSchema,
    specialResultPattern,
    programBytes,
    reflectRequest,
    writeDbFieldResult,
    writeHostResult,
});
