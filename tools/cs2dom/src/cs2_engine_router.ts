/*
 * Whole-closure backend selection for browser CS2 execution.
 *
 * This module is deliberately fail-closed.  A Dat2 program is decoded in full
 * before a TypeScript backend is selected, every opcode-40 dependency is
 * traversed, and HOST catalogue membership is never treated as executable
 * behavior.  The generated executableReviewed bit is necessary (and a real TS
 * implementation is additionally required) before a HOST opcode may cross the
 * routing boundary.
 *
 * Production remains on C/WASM unless a caller explicitly requests `auto` or
 * `typescript`.  Once selected, one backend owns the complete session; there
 * is no mid-script fallback or migration of stacks/frames/tree state.
 */

import {
    CS2_HOST_REQUEST_METADATA_BY_OPCODE,
    cs2HostOpcodeHasReviewedExecutableSemantics,
} from './generated/cs2_host.js';
import type { CS2Host } from './generated/cs2_host.js';
import { CS2_OPCODE_SEMANTICS } from './generated/cs2_opcode_semantics.js';
import type { CS2Dialect, CS2OpcodeSemantics } from './generated/cs2_opcode_semantics.js';
import {
    CS2CoreVM,
    CS2_CORE_DEFAULT_CYCLE_LIMIT,
} from './cs2_vm_core.js';
import type {
    CS2CoreInstruction,
    CS2CoreRunResult,
    CS2CoreScript,
    CS2CoreState,
    CS2CoreStringValue,
} from './cs2_vm_core.js';
import {
    CS2_DIRECT_HOST_EXECUTABLE_OPCODE_SET,
    executeCS2DirectHostInstruction,
} from './cs2_host_adapter.js';
import type {
    CS2DirectComponentTarget,
    CS2DirectHost,
    CS2DirectHostState,
} from './cs2_host_adapter.js';
import {
    CS2CoreScriptRegistry,
    decodeCS2BytecodeProgram,
} from './cs2_bytecode_decoder.js';
import type { CS2BytecodeProgramRecord } from './cs2_bytecode_decoder.js';

export type CS2EngineMode = 'wasm' | 'typescript' | 'auto';
export type CS2EngineBackend = 'wasm' | 'typescript';
export type CS2EngineSelectionReason =
    | 'production-default'
    | 'reviewed-typescript-closure'
    | 'typescript-closure-unsupported'
    | 'typescript-decode-failed';

export interface CS2EngineClosureCoverage {
    readonly supported: boolean;
    readonly dialect: CS2Dialect;
    /** Entry points declared by the transported bytecode record. */
    readonly programEntryScriptIds: readonly number[];
    /** Additional entry points discovered from the actual static UI hook tree. */
    readonly hookEntryScriptIds: readonly number[];
    /** Union of transported and UI hook roots used for closure traversal/runtime admission. */
    readonly entryScriptIds: readonly number[];
    readonly scriptIds: readonly number[];
    readonly unsupportedCoreOpcodes: readonly number[];
    readonly unreviewedHostOpcodes: readonly number[];
    readonly unimplementedHostOpcodes: readonly number[];
    /** Runtime listener writes whose installed clientscript root is stack data.
     * Until that value is proven and added to the admitted roots, the complete
     * executable closure is unknowable and TypeScript routing must fail. */
    readonly unresolvedDynamicHookOpcodes: readonly number[];
    /** Reachable scripts containing at least one unresolved listener write. */
    readonly unresolvedDynamicHookSourceScriptIds: readonly number[];
    /** CC_CREATE/CC_FIND can suspend C while an interface group is loaded.
     * Direct TypeScript execution is synchronous, so these remain unresolved
     * unless the caller proves every referenced group is already mounted. */
    readonly unresolvedInterfaceGroupOpcodes: readonly number[];
    readonly allReferencedInterfaceGroupsPreloaded: boolean;
    readonly unknownOpcodes: readonly number[];
    readonly missingScriptIds: readonly number[];
    /** A non-empty registry without a declared hook entry cannot be routed safely. */
    readonly missingEntryPoints: boolean;
}

export interface CS2EngineDecodeFailure {
    readonly name: string;
    readonly code: string | null;
    readonly message: string;
}

export interface CS2EnginePlan {
    readonly requestedMode: CS2EngineMode;
    readonly backend: CS2EngineBackend;
    readonly reason: CS2EngineSelectionReason;
    readonly registry: CS2CoreScriptRegistry | null;
    readonly coverage: CS2EngineClosureCoverage | null;
    readonly decodeFailure: CS2EngineDecodeFailure | null;
}

export interface CS2EnginePlanOptions {
    readonly mode?: CS2EngineMode;
    /** Static hook roots discovered from the UI IR before a Host is constructed. */
    readonly hookEntryScriptIds?: readonly number[];
    /** Explicit proof required by synchronous CC_CREATE/CC_FIND execution.
     * Do not set this merely because the primary interface is mounted. */
    readonly allReferencedInterfaceGroupsPreloaded?: boolean;
}

export type CS2EngineSelectionErrorCode =
    | 'BAD_ENGINE_MODE'
    | 'BAD_ENTRY_SCRIPT'
    | 'TYPESCRIPT_CLOSURE_UNSUPPORTED'
    | 'TYPESCRIPT_RUNTIME_DESTROYED'
    | 'TYPESCRIPT_SCRIPT_NOT_ROUTED'
    | 'TYPESCRIPT_HOST_SURFACE_MISSING'
    | 'TYPESCRIPT_BAD_ARGUMENT'
    | 'TYPESCRIPT_EXECUTION_FAILED';

export class CS2EngineSelectionError extends Error {
    readonly code: CS2EngineSelectionErrorCode;
    readonly coverage: CS2EngineClosureCoverage | null;
    readonly detail: unknown;

    constructor(
        code: CS2EngineSelectionErrorCode,
        message: string,
        options: {
            readonly coverage?: CS2EngineClosureCoverage | null;
            readonly detail?: unknown;
        } = {},
    ) {
        super(message);
        this.name = 'CS2EngineSelectionError';
        this.code = code;
        this.coverage = options.coverage ?? null;
        this.detail = options.detail ?? null;
    }
}

const CORE_BY_OPCODE: ReadonlyMap<number, CS2OpcodeSemantics> = new Map(
    CS2_OPCODE_SEMANTICS.map((row) => [row.opcode, row]),
);

/* Implementation and review are independent gates. The generated schema may
 * declare review only; this set is exported by the separately tested direct
 * executor and cannot be expanded by catalogue generation. */
const IMPLEMENTED_TYPESCRIPT_HOST_OPCODES: ReadonlySet<number> =
    CS2_DIRECT_HOST_EXECUTABLE_OPCODE_SET;

function installsRuntimeHook(opcode: number): boolean {
    const name = CS2_HOST_REQUEST_METADATA_BY_OPCODE[opcode]?.name;
    return typeof name === 'string' && name.includes('_SETON');
}

function sorted(values: ReadonlySet<number>): readonly number[] {
    return Object.freeze([...values].sort((left, right) => left - right));
}

interface CS2EngineClosureRoots {
    readonly programEntryScriptIds: readonly number[];
    readonly hookEntryScriptIds: readonly number[];
    readonly entryScriptIds: readonly number[];
}

function checkedEntryIds(
    entries: readonly number[] | undefined,
    description: string,
): readonly number[] {
    if( entries !== undefined && !Array.isArray(entries) )
        throw new CS2EngineSelectionError(
            'BAD_ENTRY_SCRIPT', `TypeScript CS2 ${description} must be an array`);
    const result = new Set<number>();
    for( const value of entries ?? [] ) {
        const id = Number(value);
        if( !Number.isSafeInteger(id) || id < 0 || id > 0x7fffffff )
            throw new CS2EngineSelectionError(
                'BAD_ENTRY_SCRIPT', `invalid ${description} clientscript id ${String(value)}`);
        result.add(id);
    }
    return sorted(result);
}

function planEntryRoots(
    registry: CS2CoreScriptRegistry,
    hookEntries: readonly number[] | undefined,
): CS2EngineClosureRoots {
    const program = checkedEntryIds(registry.entryScriptIds, 'program entry');
    const hooks = checkedEntryIds(hookEntries, 'static hook');
    return Object.freeze({
        programEntryScriptIds: program,
        hookEntryScriptIds: hooks,
        entryScriptIds: sorted(new Set([...program, ...hooks])),
    });
}

/**
 * Analyze every script reachable through opcode 40 from every declared entry.
 * The function is pure: it does not construct a Host, VM, or mutable UI tree.
 */
export function analyzeCS2EngineClosure(
    registry: CS2CoreScriptRegistry,
    entries?: readonly number[],
): CS2EngineClosureCoverage {
    /* Preserve the public audit API's historical override semantics. Plan
     * preparation uses analyzeCS2EngineClosureFromRoots below to attach the
     * distinct program/static-hook provenance while traversing their union. */
    const selected = checkedEntryIds(
        entries ?? registry.entryScriptIds, 'closure entry');
    return analyzeCS2EngineClosureFromRoots(registry, Object.freeze({
        programEntryScriptIds: selected,
        hookEntryScriptIds: Object.freeze([]),
        entryScriptIds: selected,
    }));
}

function analyzeCS2EngineClosureFromRoots(
    registry: CS2CoreScriptRegistry,
    roots: CS2EngineClosureRoots,
    allReferencedInterfaceGroupsPreloaded = false,
): CS2EngineClosureCoverage {
    const entryScriptIds = roots.entryScriptIds;
    const missingScriptIds = new Set<number>();
    const unsupportedCoreOpcodes = new Set<number>();
    const unreviewedHostOpcodes = new Set<number>();
    const unimplementedHostOpcodes = new Set<number>();
    const unresolvedDynamicHookOpcodes = new Set<number>();
    const unresolvedDynamicHookSourceScriptIds = new Set<number>();
    const unresolvedInterfaceGroupOpcodes = new Set<number>();
    const unknownOpcodes = new Set<number>();
    const visited = new Set<number>();
    const pending = [...entryScriptIds];

    while( pending.length ) {
        const scriptId = pending.pop()!;
        if( visited.has(scriptId) ) continue;
        const script = registry.get(scriptId);
        if( !script ) {
            missingScriptIds.add(scriptId);
            continue;
        }
        visited.add(scriptId);
        for( const instruction of script.instructions ) {
            const opcode = instruction.opcode | 0;
            const core = CORE_BY_OPCODE.get(opcode);
            if( core ) {
                if( !core.dialects.includes(registry.dialect) )
                    unsupportedCoreOpcodes.add(opcode);
            } else if( CS2_HOST_REQUEST_METADATA_BY_OPCODE[opcode] !== undefined ) {
                if( !cs2HostOpcodeHasReviewedExecutableSemantics(opcode) )
                    unreviewedHostOpcodes.add(opcode);
                else if( !IMPLEMENTED_TYPESCRIPT_HOST_OPCODES.has(opcode) )
                    unimplementedHostOpcodes.add(opcode);
                if( installsRuntimeHook(opcode) ) {
                    unresolvedDynamicHookOpcodes.add(opcode);
                    unresolvedDynamicHookSourceScriptIds.add(scriptId);
                }
                if( !allReferencedInterfaceGroupsPreloaded &&
                    (opcode === 100 || opcode === 200) )
                    unresolvedInterfaceGroupOpcodes.add(opcode);
            } else {
                unknownOpcodes.add(opcode);
            }

            if( opcode !== 40 ) continue;
            const targetId = instruction.intOperand | 0;
            if( registry.has(targetId) ) pending.push(targetId);
            else missingScriptIds.add(targetId);
        }
    }

    const missingEntryPoints = registry.size > 0 && entryScriptIds.length === 0;
    const coverage = {
        supported: !missingEntryPoints && missingScriptIds.size === 0 &&
            unsupportedCoreOpcodes.size === 0 && unreviewedHostOpcodes.size === 0 &&
            unimplementedHostOpcodes.size === 0 &&
            unresolvedDynamicHookOpcodes.size === 0 &&
            unresolvedInterfaceGroupOpcodes.size === 0 && unknownOpcodes.size === 0,
        dialect: registry.dialect,
        programEntryScriptIds: roots.programEntryScriptIds,
        hookEntryScriptIds: roots.hookEntryScriptIds,
        entryScriptIds,
        scriptIds: sorted(visited),
        unsupportedCoreOpcodes: sorted(unsupportedCoreOpcodes),
        unreviewedHostOpcodes: sorted(unreviewedHostOpcodes),
        unimplementedHostOpcodes: sorted(unimplementedHostOpcodes),
        unresolvedDynamicHookOpcodes: sorted(unresolvedDynamicHookOpcodes),
        unresolvedDynamicHookSourceScriptIds: sorted(unresolvedDynamicHookSourceScriptIds),
        unresolvedInterfaceGroupOpcodes: sorted(unresolvedInterfaceGroupOpcodes),
        allReferencedInterfaceGroupsPreloaded,
        unknownOpcodes: sorted(unknownOpcodes),
        missingScriptIds: sorted(missingScriptIds),
        missingEntryPoints,
    } satisfies CS2EngineClosureCoverage;
    return Object.freeze(coverage);
}

function normalizeMode(value: CS2EngineMode | undefined): CS2EngineMode {
    const mode = value ?? 'wasm';
    if( mode === 'wasm' || mode === 'typescript' || mode === 'auto' ) return mode;
    throw new CS2EngineSelectionError(
        'BAD_ENGINE_MODE',
        `unknown CS2 engine mode ${String(value)}; expected wasm, typescript, or auto`,
    );
}

function frozenPlan(plan: CS2EnginePlan): CS2EnginePlan {
    return Object.freeze(plan);
}

/**
 * Decode and choose before any Host/tree object exists. `wasm` deliberately
 * skips the migration decoder so the reference production path cannot regress
 * on incomplete TypeScript coverage. `auto` attempts the exact decoder and
 * falls back wholly to C/WASM on decode or closure rejection.
 */
export function prepareCS2EnginePlan(
    program: CS2BytecodeProgramRecord,
    options: CS2EnginePlanOptions = {},
): CS2EnginePlan {
    const requestedMode = normalizeMode(options.mode);
    if( requestedMode === 'wasm' ) return frozenPlan({
        requestedMode,
        backend: 'wasm',
        reason: 'production-default',
        registry: null,
        coverage: null,
        decodeFailure: null,
    });

    let registry: CS2CoreScriptRegistry;
    try {
        registry = decodeCS2BytecodeProgram(program);
    } catch( error ) {
        if( requestedMode === 'typescript' ) throw error;
        return frozenPlan({
            requestedMode,
            backend: 'wasm',
            reason: 'typescript-decode-failed',
            registry: null,
            coverage: null,
            decodeFailure: Object.freeze({
                name: error instanceof Error ? error.name : 'Error',
                code: typeof (error as { code?: unknown })?.code === 'string'
                    ? String((error as { code: string }).code) : null,
                message: error instanceof Error ? error.message : String(error),
            }),
        });
    }

    const coverage = analyzeCS2EngineClosureFromRoots(
        registry, planEntryRoots(registry, options.hookEntryScriptIds),
        options.allReferencedInterfaceGroupsPreloaded === true);
    if( coverage.supported ) return frozenPlan({
        requestedMode,
        backend: 'typescript',
        reason: 'reviewed-typescript-closure',
        registry,
        coverage,
        decodeFailure: null,
    });
    if( requestedMode === 'typescript' ) throw new CS2EngineSelectionError(
        'TYPESCRIPT_CLOSURE_UNSUPPORTED',
        unsupportedClosureMessage(coverage),
        { coverage },
    );
    return frozenPlan({
        requestedMode,
        backend: 'wasm',
        reason: 'typescript-closure-unsupported',
        registry,
        coverage,
        decodeFailure: null,
    });
}

function unsupportedClosureMessage(coverage: CS2EngineClosureCoverage): string {
    const reasons: string[] = [];
    if( coverage.missingEntryPoints ) reasons.push('no declared entry scripts');
    if( coverage.missingScriptIds.length )
        reasons.push(`missing scripts ${coverage.missingScriptIds.join(', ')}`);
    if( coverage.unsupportedCoreOpcodes.length )
        reasons.push(`unsupported core opcodes ${coverage.unsupportedCoreOpcodes.join(', ')}`);
    if( coverage.unreviewedHostOpcodes.length )
        reasons.push(`unreviewed Host opcodes ${coverage.unreviewedHostOpcodes.join(', ')}`);
    if( coverage.unimplementedHostOpcodes.length )
        reasons.push(`unimplemented Host opcodes ${coverage.unimplementedHostOpcodes.join(', ')}`);
    if( coverage.unresolvedDynamicHookOpcodes.length )
        reasons.push('unresolved runtime hook installers ' +
            `${coverage.unresolvedDynamicHookOpcodes.join(', ')} in scripts ` +
            `${coverage.unresolvedDynamicHookSourceScriptIds.join(', ')}`);
    if( coverage.unresolvedInterfaceGroupOpcodes.length )
        reasons.push('Host opcodes requiring unproven preloaded interface groups ' +
            coverage.unresolvedInterfaceGroupOpcodes.join(', '));
    if( coverage.unknownOpcodes.length )
        reasons.push(`unknown opcodes ${coverage.unknownOpcodes.join(', ')}`);
    return `TypeScript CS2 closure is not executable: ${reasons.join('; ') || 'unknown reason'}`;
}

export interface CS2IntentHookArgument {
    readonly type?: string;
    readonly value?: unknown;
    readonly ref?: unknown;
}

export interface CS2RuntimeIntent {
    readonly hook?: {
        readonly scriptId?: number;
        readonly args?: readonly unknown[];
    };
    readonly component?: unknown;
    readonly dragTarget?: unknown;
    readonly locals?: Readonly<Record<string, unknown>>;
}

export interface TypeScriptCS2InvocationResult {
    readonly status: 'done';
    readonly scriptId: number;
    readonly hostRequests: number;
    readonly fastScalarL1Hits: 0;
    readonly fastScalarL1Misses: 0;
    readonly cycles: number;
    readonly intStack: readonly number[];
    readonly stringStack: readonly CS2CoreStringValue[];
}

export interface TypeScriptCS2Runtime {
    readonly backend: 'typescript';
    readonly plan: CS2EnginePlan;
    invokeIntent(intent: CS2RuntimeIntent): TypeScriptCS2InvocationResult;
    destroy(): void;
}

export interface CreateTypeScriptCS2RuntimeOptions {
    readonly program: CS2BytecodeProgramRecord;
    readonly plan?: CS2EnginePlan;
    /**
     * Reviewed positional Host implementation. Core-only closures may omit it.
     * A tagged request() compatibility adapter is intentionally not accepted.
     */
    readonly host?: unknown;
    readonly cycleLimit?: number;
}

/** Real synchronous runtime for fully reviewed core + direct-Host closures. */
export async function createTypeScriptCS2Runtime({
    program,
    plan = prepareCS2EnginePlan(program, { mode: 'typescript' }),
    host = null,
    cycleLimit = CS2_CORE_DEFAULT_CYCLE_LIMIT,
}: CreateTypeScriptCS2RuntimeOptions): Promise<TypeScriptCS2Runtime> {
    if( plan.backend !== 'typescript' || !plan.registry || !plan.coverage?.supported )
        throw new CS2EngineSelectionError(
            'TYPESCRIPT_CLOSURE_UNSUPPORTED',
            'TypeScript runtime requires a preflight-approved complete closure',
            { coverage: plan.coverage },
        );
    if( !Number.isInteger(cycleLimit) || cycleLimit <= 0 )
        throw new RangeError('CS2 cycle limit must be a positive integer');
    const directHost = directHostForPlan(plan, host);
    const directLifecycle = directHostLifecycleForPlan(plan, host, directHost);
    return new TypeScriptCoreSession(plan, host, directHost, directLifecycle, cycleLimit);
}

interface CS2DirectHostLifecycle {
    beginCS2DirectInvocation(): unknown;
    endCS2DirectInvocation(error?: unknown): unknown;
}

class TypeScriptCoreSession implements TypeScriptCS2Runtime {
    readonly backend = 'typescript' as const;
    readonly plan: CS2EnginePlan;
    readonly #registry: CS2CoreScriptRegistry;
    readonly #entryIds: ReadonlySet<number>;
    readonly #host: unknown;
    readonly #directHost: CS2DirectHost | null;
    readonly #directLifecycle: CS2DirectHostLifecycle | null;
    readonly #cycleLimit: number;
    #destroyed = false;

    constructor(
        plan: CS2EnginePlan,
        host: unknown,
        directHost: CS2DirectHost | null,
        directLifecycle: CS2DirectHostLifecycle | null,
        cycleLimit: number,
    ) {
        this.plan = plan;
        this.#registry = plan.registry!;
        this.#entryIds = new Set(plan.coverage!.entryScriptIds);
        this.#host = host;
        this.#directHost = directHost;
        this.#directLifecycle = directLifecycle;
        this.#cycleLimit = cycleLimit;
    }

    invokeIntent(intent: CS2RuntimeIntent): TypeScriptCS2InvocationResult {
        if( this.#destroyed ) throw new CS2EngineSelectionError(
            'TYPESCRIPT_RUNTIME_DESTROYED', 'TypeScript CS2 runtime is destroyed');
        const scriptId = checkedScriptId(intent?.hook?.scriptId);
        const script = this.#registry.get(scriptId);
        if( !script || !this.#entryIds.has(scriptId) ) throw new CS2EngineSelectionError(
            'TYPESCRIPT_SCRIPT_NOT_ROUTED',
            `clientscript ${scriptId} was not part of the selected entry closure`,
        );
        const argumentsByKind = hookArguments(intent, this.#host);
        validateArgumentCounts(script, argumentsByKind.ints.length, argumentsByKind.strings.length);
        const initialTarget = directComponentTarget(intent?.component, this.#host);
        setRuntimeActiveTargets(this.#host, initialTarget);
        let directState: CS2DirectHostState | null = null;
        let hostRequests = 0;
        const externalOpcodeExecutor = this.#directHost ? {
            implementedOpcodes: IMPLEMENTED_TYPESCRIPT_HOST_OPCODES,
            execute: (state: CS2CoreState, instruction: CS2CoreInstruction): void => {
                directState ||= {
                    intStack: state.intStack,
                    /* Every Host string operand is checked below before the
                     * direct adapter observes this shared stack. Array handles
                     * therefore retain their core-only identity and are never
                     * silently coerced into Host text. */
                    stringStack: state.stringStack as Array<string | null>,
                    activeComponent: initialTarget,
                    dotComponent: initialTarget,
                    dialect: this.#registry.dialect,
                };
                assertDirectHostStringOperands(state.stringStack, state.intStack, instruction);
                hostRequests++;
                executeCS2DirectHostInstruction(directState, instruction, this.#directHost!);
            },
        } : undefined;
        const vm = new CS2CoreVM(script, {
            dialect: this.#registry.dialect,
            scripts: this.#registry,
            intLocals: argumentsByKind.ints,
            stringLocals: argumentsByKind.strings,
            externalOpcodeExecutor,
        });
        let invocationError: unknown = null;
        this.#directLifecycle?.beginCS2DirectInvocation.call(this.#host);
        try {
            const result = vm.run(this.#cycleLimit);
            if( result.status !== 'done' ) throw executionError(scriptId, result);
            return Object.freeze({
                status: 'done',
                scriptId,
                hostRequests,
                fastScalarL1Hits: 0,
                fastScalarL1Misses: 0,
                cycles: result.cycles,
                intStack: Object.freeze([...vm.state.intStack]),
                stringStack: Object.freeze([...vm.state.stringStack]),
            });
        } catch( error ) {
            invocationError = error;
            throw error;
        } finally {
            try {
                this.#directLifecycle?.endCS2DirectInvocation.call(
                    this.#host, invocationError);
            } catch( endError ) {
                /* A batch finalizer must not replace the VM/Host error which
                 * caused it to unwind. On a successful VM run its own failure
                 * is the exact invocation failure. */
                if( invocationError === null ) throw endError;
            }
        }
    }

    destroy(): void {
        this.#destroyed = true;
    }
}

function directHostLifecycleForPlan(
    plan: CS2EnginePlan,
    value: unknown,
    directHost: CS2DirectHost | null,
): CS2DirectHostLifecycle | null {
    if( !directHost ) return null;
    const candidate = value as Partial<CS2DirectHostLifecycle> | null;
    const begin = typeof candidate?.beginCS2DirectInvocation === 'function';
    const end = typeof candidate?.endCS2DirectInvocation === 'function';
    if( !begin || !end ) throw new CS2EngineSelectionError(
        'TYPESCRIPT_HOST_SURFACE_MISSING',
        'TypeScript CS2 Host closures require both beginCS2DirectInvocation and ' +
            'endCS2DirectInvocation',
        { coverage: plan.coverage },
    );
    return candidate as CS2DirectHostLifecycle;
}

function directHostForPlan(plan: CS2EnginePlan, value: unknown): CS2DirectHost | null {
    const required = new Set<string>();
    for( const scriptId of plan.coverage?.scriptIds ?? [] ) {
        const script = plan.registry?.get(scriptId);
        for( const instruction of script?.instructions ?? [] ) {
            if( !IMPLEMENTED_TYPESCRIPT_HOST_OPCODES.has(instruction.opcode) ) continue;
            const metadata = CS2_HOST_REQUEST_METADATA_BY_OPCODE[instruction.opcode];
            if( metadata ) required.add(metadata.name);
        }
    }
    if( required.size === 0 ) return null;
    const candidate = value as Partial<CS2Host> | null;
    const missing = [...required].filter((kind) =>
        typeof candidate?.[kind as keyof CS2Host] !== 'function').sort();
    if( missing.length ) throw new CS2EngineSelectionError(
        'TYPESCRIPT_HOST_SURFACE_MISSING',
        `TypeScript CS2 Host is missing positional method${missing.length === 1 ? '' : 's'} ` +
            missing.join(', '),
        { coverage: plan.coverage },
    );
    return candidate as CS2DirectHost;
}

function directComponentTarget(intentTarget: unknown, host: unknown): CS2DirectComponentTarget {
    const candidate = host as { ref?: (value: unknown) => unknown } | null;
    let value = intentTarget;
    if( typeof candidate?.ref === 'function' ) {
        try { value = candidate.ref(intentTarget) ?? intentTarget; }
        catch { /* A missing event component has the native null target. */ }
    }
    if( typeof value === 'number' && Number.isInteger(value) ) return value | 0;
    const ref = looseRef(value);
    if( !ref ) return null;
    if( Number.isInteger(ref.componentId) )
        return ref as unknown as CS2DirectComponentTarget;
    if( !Number.isInteger(ref.component_id) ) return null;
    return {
        componentId: Number(ref.component_id) | 0,
        ...(Number.isInteger(ref.subId ?? ref.sub_id)
            ? { subId: Number(ref.subId ?? ref.sub_id) | 0 } : {}),
        ...(Number.isInteger(ref.generation)
            ? { generation: Number(ref.generation) | 0 } : {}),
    };
}

function setRuntimeActiveTargets(host: unknown, target: CS2DirectComponentTarget): void {
    const candidate = host as {
        setActive?: (value: CS2DirectComponentTarget, options?: { dot?: boolean }) => unknown;
    } | null;
    if( typeof candidate?.setActive !== 'function' ) return;
    candidate.setActive(target);
    candidate.setActive(target, { dot: true });
}

const DIRECT_SET_ON_OPCODES: ReadonlySet<number> = new Set([
    1400, 1401, 1403, 1404, 1405, 1407, 1408, 1409, 1410, 1412, 1417, 1419,
    2407, 2408, 2409, 2417,
]);
const DIRECT_ONE_STRING_OPCODES: ReadonlySet<number> = new Set([
    1112, 1300, 2300, 2305,
]);

/** Reject core array handles before the narrower Host string stack sees them. */
function assertDirectHostStringOperands(
    strings: readonly CS2CoreStringValue[],
    ints: readonly number[],
    instruction: CS2CoreInstruction,
): void {
    const opcode = instruction.opcode | 0;
    if( DIRECT_ONE_STRING_OPCODES.has(opcode) ) {
        assertHostTexts(strings, 1, opcode);
        return;
    }
    if( opcode === 1704 || opcode === 2704 ) {
        const valueKind = ints[ints.length - 1] | 0;
        if( valueKind === 2 || (opcode === 2704 && valueKind === 115) )
            assertHostTexts(strings, 1, opcode);
        return;
    }
    if( !DIRECT_SET_ON_OPCODES.has(opcode) ) return;
    assertHostTexts(strings, 1, opcode);
    const signatureValue = strings[strings.length - 1];
    if( signatureValue == null ) return;
    const signature = signatureValue as string;
    const parseLength = signature.endsWith('Y') ? signature.length - 1 : signature.length;
    let stringArguments = 0;
    for( let index = 0; index < parseLength; index++ )
        if( signature[index] === 's' || signature[index] === 'W' || signature[index] === 'X' )
            stringArguments++;
    assertHostTexts(strings, stringArguments + 1, opcode);
}

function assertHostTexts(
    strings: readonly CS2CoreStringValue[],
    count: number,
    opcode: number,
): void {
    const start = strings.length - count;
    for( let index = Math.max(0, start); index < strings.length; index++ ) {
        const value = strings[index];
        if( value !== null && typeof value !== 'string' ) throw new TypeError(
            `CS2 array handle cannot be consumed as Host text by opcode ${opcode}`);
    }
}

function validateArgumentCounts(script: CS2CoreScript, ints: number, strings: number): void {
    const intLocals = Number(script.intLocalCount ?? 0);
    const stringLocals = Number(script.stringLocalCount ?? 0);
    if( ints <= intLocals && strings <= stringLocals ) return;
    throw new CS2EngineSelectionError(
        'TYPESCRIPT_BAD_ARGUMENT',
        `clientscript ${script.id ?? -1} has ${intLocals}/${stringLocals} int/string locals, ` +
            `but the hook supplied ${ints}/${strings}`,
    );
}

function executionError(scriptId: number, result: CS2CoreRunResult): CS2EngineSelectionError {
    const detail = result.error;
    return new CS2EngineSelectionError(
        'TYPESCRIPT_EXECUTION_FAILED',
        detail?.message || `clientscript ${scriptId} failed in the TypeScript VM`,
        { detail },
    );
}

const EVENT_SENTINELS = Object.freeze(new Map<number, keyof ReturnType<typeof eventInts>>([
    [-2147483647, 'mouseX'],
    [-2147483646, 'mouseY'],
    [-2147483645, 'componentId'],
    [-2147483644, 'opIndex'],
    [-2147483643, 'componentSubId'],
    [-2147483642, 'dragTargetId'],
    [-2147483641, 'dragTargetSubId'],
    [-2147483640, 'keyTyped'],
    [-2147483639, 'keyPressed'],
    [-2147483638, 'opSubIndex'],
]));

function hookArguments(
    intent: CS2RuntimeIntent,
    host: unknown,
): { readonly ints: number[]; readonly strings: string[] } {
    const ints: number[] = [];
    const strings: string[] = [];
    const event = eventInts(intent);
    for( const raw of intent?.hook?.args ?? [] ) {
        const typed = raw !== null && typeof raw === 'object' &&
            'type' in raw && 'value' in raw;
        const record = typed ? raw as CS2IntentHookArgument : null;
        const value = record ? record.value : raw;
        const isString = record
            ? ['string', 's', 'text'].includes(String(record.type).toLowerCase())
            : typeof value === 'string';
        if( isString ) {
            const text = String(value ?? '');
            strings.push(text === 'event_opbase' ? eventOpBase(intent, host) : text);
            continue;
        }
        const integer = intHookArgument(value);
        const eventField = EVENT_SENTINELS.get(integer);
        ints.push(eventField ? event[eventField] : integer);
    }
    return { ints, strings };
}

function eventInts(intent: CS2RuntimeIntent) {
    const locals = intent?.locals ?? {};
    const component = looseRef(intent?.component);
    const drag = looseRef(intent?.dragTarget);
    return {
        mouseX: integerOr(locals.eventMouseX ?? locals.mouseX, 0),
        mouseY: integerOr(locals.eventMouseY ?? locals.mouseY, 0),
        componentId: packedComponentId(component, -1),
        componentSubId: integerOr(component?.subId ?? component?.sub_id, -1),
        opIndex: integerOr(locals.opIndex, 1),
        dragTargetId: packedComponentId(drag, -1),
        dragTargetSubId: integerOr(drag?.subId ?? drag?.sub_id, -1),
        keyTyped: integerOr(locals.keyTyped, -1),
        keyPressed: integerOr(locals.keyPressed, -1),
        opSubIndex: integerOr(locals.opSubIndex, 0),
    };
}

function eventOpBase(intent: CS2RuntimeIntent, host: unknown): string {
    const explicit = intent?.locals?.eventOpBase ?? intent?.locals?.opBase;
    if( explicit !== undefined && explicit !== null ) return String(explicit);
    const candidate = host as {
        read?: (kind: string, component: unknown) => unknown;
        ref?: (component: unknown) => unknown;
    } | null;
    if( typeof candidate?.read !== 'function' ) return '';
    let component = intent?.component;
    try { component = candidate.ref?.(component) ?? component; }
    catch { /* A missing op-base has the empty ScriptEvent value. */ }
    try { return String(candidate.read('if_getopbase', component) ?? ''); }
    catch { return ''; }
}

function looseRef(value: unknown): Record<string, unknown> | null {
    if( value === null || value === undefined ) return null;
    if( typeof value === 'number' ) return { componentId: value };
    if( typeof value !== 'object' ) return null;
    const record = value as Record<string, unknown>;
    const nested = record.ref;
    return nested && typeof nested === 'object' ? nested as Record<string, unknown> : record;
}

function packedComponentId(value: Record<string, unknown> | null, fallback: number): number {
    const id = value?.componentId ?? value?.component_id;
    return Number.isInteger(id) ? Number(id) | 0 : fallback;
}

function integerOr(value: unknown, fallback: number): number {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) | 0 : fallback;
}

function intHookArgument(value: unknown): number {
    const ref = looseRef(value);
    if( ref && typeof value === 'object' ) {
        const id = packedComponentId(ref, Number.NaN);
        if( Number.isFinite(id) ) return id;
    }
    if( typeof value === 'boolean' ) return value ? 1 : 0;
    const number = Number(value ?? 0);
    if( !Number.isFinite(number) ) throw new CS2EngineSelectionError(
        'TYPESCRIPT_BAD_ARGUMENT', `hook integer argument ${String(value)} is not finite`);
    return Math.trunc(number) | 0;
}

function checkedScriptId(value: unknown): number {
    const id = Number(value);
    if( !Number.isSafeInteger(id) || id < 0 || id > 0x7fffffff )
        throw new CS2EngineSelectionError(
            'TYPESCRIPT_BAD_ARGUMENT', `invalid hook script id ${String(value)}`);
    return id;
}
