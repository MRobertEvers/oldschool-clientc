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
const TARGET_COMMANDS = new Set([
    'CC_CREATE', 'CC_COPY', 'CC_CREATECHILD', 'CC_CREATESIBLING',
    'CC_FIND', 'IF_FIND', 'OVERLAY_FIND', 'OVERLAY_CC_FIND',
    'CC_CHILDREN_FINDNEXT', 'IF_CHILDREN_FIND',
]);
const STATE_WRITES = new Set([
    'POP_VAR', 'POP_VARBIT', 'POP_VARC_INT', 'POP_VARC_STRING',
    'POP_VARC_STRING_OLD',
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
} = {}) {
    validateProgram(program);
    if( !host || typeof host.request !== 'function' )
        throw new WasmCS2RuntimeError('C CS2VM/WASM requires a synchronous HostRuntime', 'BAD_HOST');

    const bundle = moduleFactory
        ? await instantiateModule(moduleFactory, wasmUrl)
        : await defaultModule(moduleUrl, wasmUrl);
    const runtime = new WasmSession(bundle, program, host);
    try { runtime.load(); }
    catch( error ) {
        runtime.destroy();
        throw error;
    }
    return runtime;
}

class WasmSession {
    constructor(bundle, program, host) {
        this.bundle = bundle;
        this.api = bundle.api;
        this.program = program;
        this.host = host;
        this.session = 0;
        this.destroyed = false;
        this.hostErrors = new Map();
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
            [EVENT_I32.windowMode, 1],
            [EVENT_I32.defaultWindowMode, 1],
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
            const request = reflectRequest(this.api, requestPointer, kind, thread);
            if( STATE_WRITES.has(request.kind) ) request.transmit = false;
            normalizeSetOn(request);
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
    });
    validateModule(api);
    return { api, sessions };
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
        '_cs2w_request_field_string', '_cs2w_thread_push_int',
        '_cs2w_thread_push_string', '_cs2w_thread_set_target',
        '_cs2w_thread_set_children',
        '_cs2w_thread_current_operand',
    ];
    if( !api?.HEAPU8 ) throw new WasmCS2RuntimeError(
        'C CS2VM/WASM module does not export HEAPU8', 'MODULE');
    for( const name of required ) if( typeof api[name] !== 'function' )
        throw new WasmCS2RuntimeError(`C CS2VM/WASM module is missing ${name}`, 'MODULE');
}

function reflectRequest(api, pointer, kind, thread) {
    const kindName = readCString(api, api._cs2w_request_kind_name(kind)).toUpperCase();
    if( !kindName ) throw new WasmCS2RuntimeError(`HOST request ${kind} has no name`, 'REFLECT');
    const count = api._cs2w_request_field_count(kind) | 0;
    if( count < 0 || count > MAX_REFLECTED_FIELDS ) throw new WasmCS2RuntimeError(
        `HOST request ${kindName} has invalid field count ${count}`, 'REFLECT');
    const result = { kind: kindName, opcode: kind };
    for( let index = 0; index < count; index++ ) {
        const name = readCString(api, api._cs2w_request_field_name(kind, index));
        const fieldKind = api._cs2w_request_field_kind(kind, index) | 0;
        const length = api._cs2w_request_field_length(pointer, index) | 0;
        if( !name || length < 0 || length > MAX_REFLECTED_VALUES )
            throw new WasmCS2RuntimeError(`HOST request ${kindName} has invalid field ${index}`, 'REFLECT');
        const value = reflectedValue(api, pointer, index, fieldKind, length);
        if( name === 'kind' ) result.value_kind = value;
        else result[name] = value;
    }
    result.dot_operand = Boolean(api._cs2w_thread_current_operand(thread));
    return result;
}

function reflectedValue(api, pointer, index, kind, length) {
    if( kind === ABI.fieldString ) {
        const value = api._cs2w_request_field_string(pointer, index, 0);
        return value ? readCString(api, value) : null;
    }
    if( kind === ABI.fieldStringArray ) return Array.from({ length }, (_, element) =>
        api._cs2w_request_field_string(pointer, index, element)).map((value) =>
        value ? readCString(api, value) : null);
    if( kind === ABI.fieldU64 ) return [
        api._cs2w_request_field_i32(pointer, index, 0) | 0,
        api._cs2w_request_field_i32(pointer, index, 1) | 0,
    ];
    if( kind === ABI.fieldI32Array || kind === ABI.fieldI32Pointer )
        return Array.from({ length }, (_, element) =>
            api._cs2w_request_field_i32(pointer, index, element) | 0);
    if( kind === ABI.fieldI32 || kind === ABI.fieldBool || kind === ABI.fieldU8 )
        return api._cs2w_request_field_i32(pointer, index, 0) | 0;
    throw new WasmCS2RuntimeError(`unsupported reflected HOST field kind ${kind}`, 'REFLECT');
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

function writeHostResult(api, thread, kind, request, result, host) {
    if( CHILD_ITERATOR_COMMANDS.has(request.kind) ) {
        const refs = Array.isArray(result) ? result : host?.childIteration?.refs || [];
        const children = refs.slice(0, MAX_CHILD_ITERATOR).map((ref) => childSubId(ref));
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

    const command = CS2_COMMANDS.get(kind);
    const pattern = specialResultPattern(request) ?? BASE_RESULTS[request.kind] ?? command?.defs ?? '';
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

function specialResultPattern(request) {
    if( request.kind === 'ENUM' ) return Number(request.output_type) === 's'.charCodeAt(0) ? 's' : 'i';
    if( request.kind === 'CC_GETCOMPONENTPARAM' ) return 'i';
    if( POLYMORPHIC_RESULTS.has(request.kind) ) return '?';
    return null;
}

function childSubId(value) {
    const ref = resultRef(value) || value;
    const subId = Number(ref?.subId ?? ref?.sub_id ?? ref?.childIndex ?? ref?.child_index);
    if( !Number.isInteger(subId) || subId < 0 || subId > 0xffff )
        throw new WasmCS2RuntimeError('HOST child iterator returned an invalid sub-id', 'HOST_RESULT');
    return subId | 0;
}

function resultValues(pattern, result) {
    if( Array.isArray(result) ) return result;
    if( result && Array.isArray(result.values) ) return result.values;
    if( result && typeof result === 'object' &&
        (Array.isArray(result.ints) || Array.isArray(result.strings)) ) {
        const ints = [...(result.ints || [])];
        const strings = [...(result.strings || [])];
        return [...pattern].map((type) => type === 's' ? strings.shift() : ints.shift());
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

function withString(api, value, callback) {
    const bytes = new TextEncoder().encode(String(value ?? ''));
    const pointer = api._malloc(bytes.length + 1);
    if( !pointer ) throw new WasmCS2RuntimeError('WASM string allocation failed', 'OUT_OF_MEMORY');
    try {
        api.HEAPU8.set(bytes, pointer);
        api.HEAPU8[pointer + bytes.length] = 0;
        return callback(pointer);
    } finally { api._free(pointer); }
}

function readCString(api, pointer) {
    pointer = Number(pointer) | 0;
    if( !pointer ) return '';
    const heap = api.HEAPU8;
    let end = pointer;
    const limit = Math.min(heap.length, pointer + 65536);
    while( end < limit && heap[end] !== 0 ) end++;
    if( end === limit ) throw new WasmCS2RuntimeError('WASM string is not terminated', 'REFLECT');
    return new TextDecoder().decode(heap.subarray(pointer, end));
}

export const __wasmRuntimeTest = Object.freeze({
    normalizeRevision,
    normalizeDialect,
    normalizeSetOn,
    specialResultPattern,
    programBytes,
    reflectRequest,
    writeHostResult,
});
