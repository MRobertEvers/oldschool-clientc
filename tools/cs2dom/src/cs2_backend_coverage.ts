/*
 * Deterministic evidence report for the TypeScript CS2 migration.
 *
 * Decoding, execution review, and backend eligibility are deliberately three
 * different facts:
 *
 *   - the strict decoder proves that an opcode's cache wire shape is known;
 *   - generated core semantics / reviewed Host rows prove executable behavior;
 *   - the engine router proves that one complete GOSUB closure can use TS.
 *
 * In particular, presence in the 633-row Host schema is not execution support.
 * This module consumes the router's fail-closed closure analysis rather than
 * growing a second backend-selection implementation for reporting.
 */

import {
    CS2_HOST_REQUEST_METADATA_BY_OPCODE,
    cs2HostOpcodeHasReviewedExecutableSemantics,
} from './generated/cs2_host.js';
import { CS2_CORE_DISPATCH_BY_OPCODE } from './generated/cs2_opcode_semantics.js';
import type { CS2DecodedCoreScript, CS2CoreScriptRegistry } from './cs2_bytecode_decoder.js';
import {
    analyzeCS2EngineClosure,
} from './cs2_engine_router.js';

export const CS2_BACKEND_COVERAGE_SCHEMA = 'cs2dom-ts-backend-coverage/2';

export type CS2OpcodeReviewClass =
    | 'core-reviewed'
    | 'core-unreviewed'
    | 'host-reviewed'
    | 'host-schema-only';

export interface CS2OpcodeFrequency {
    readonly opcode: number;
    readonly name: string;
    /** Strict decoding succeeded, so the operand's cache wire shape is known. */
    readonly decodeKnown: true;
    readonly reviewClass: CS2OpcodeReviewClass;
    readonly executableReviewed: boolean;
    readonly count: number;
}

export interface CS2BackendEntryCoverage {
    readonly entryScriptId: number;
    readonly name: string;
    readonly completeGosubClosure: boolean;
    readonly tsEligible: boolean;
    readonly scriptIds: readonly number[];
    readonly scriptCount: number;
    readonly instructionCount: number;
    readonly executableReviewedInstructionCount: number;
    readonly unsupportedCoreOpcodes: readonly CS2OpcodeFrequency[];
    readonly schemaOnlyHostOpcodes: readonly CS2OpcodeFrequency[];
    readonly unimplementedHostOpcodes: readonly CS2OpcodeFrequency[];
    readonly unresolvedDynamicHookOpcodes: readonly CS2OpcodeFrequency[];
    readonly unresolvedDynamicHookSourceScriptIds: readonly number[];
    readonly unresolvedInterfaceGroupOpcodes: readonly CS2OpcodeFrequency[];
    readonly missingGosubTargets: readonly number[];
}

export interface CS2BackendRegistryCoverage {
    readonly dialect: string;
    readonly revision: number;
    readonly decodeKnown: true;
    /** All records transported in the interface program, reachable or not. */
    readonly registryScriptCount: number;
    readonly registryInstructionCount: number;
    /** Unique scripts reachable from the union of declared entry closures. */
    readonly reachableScriptCount: number;
    readonly reachableInstructionCount: number;
    /** Sum of each entry closure; shared helpers intentionally count per entry. */
    readonly entryClosureInstructionCount: number;
    readonly completeClosureCount: number;
    readonly tsEligibleClosureCount: number;
    readonly interfaceTsEligible: boolean;
    readonly entryClosures: readonly CS2BackendEntryCoverage[];
    readonly opcodeFrequencies: readonly CS2OpcodeFrequency[];
    readonly unsupportedCoreOpcodes: readonly CS2OpcodeFrequency[];
    readonly schemaOnlyHostOpcodes: readonly CS2OpcodeFrequency[];
    readonly unimplementedHostOpcodes: readonly CS2OpcodeFrequency[];
    readonly unresolvedDynamicHookOpcodes: readonly CS2OpcodeFrequency[];
    readonly unresolvedDynamicHookSourceScriptIds: readonly number[];
    readonly unresolvedInterfaceGroupOpcodes: readonly CS2OpcodeFrequency[];
    readonly missingGosubTargets: readonly number[];
}

type MutableFrequency = {
    opcode: number;
    name: string;
    decodeKnown: true;
    reviewClass: CS2OpcodeReviewClass;
    executableReviewed: boolean;
    count: number;
};

/**
 * Analyze a registry decoded atomically by decodeCS2BytecodeProgram().
 * Results contain no timings, filesystem paths, or traversal-order artifacts.
 */
export function auditCS2BackendRegistry(
    registry: CS2CoreScriptRegistry,
): CS2BackendRegistryCoverage {
    const declaredEntries = sortedNumbers(registry.entryScriptIds);
    const entryClosures = declaredEntries.map((entryScriptId) =>
        auditEntry(registry, entryScriptId));
    const unionCoverage = analyzeCS2EngineClosure(registry, declaredEntries);
    const reachableScripts = scriptsForIds(registry, unionCoverage.scriptIds);
    const registryScripts = [...registry.values()].sort((left, right) => left.id - right.id);
    const frequencies = opcodeFrequencies(
        reachableScripts, new Set(unionCoverage.unsupportedCoreOpcodes));
    const missingGosubTargets = sortedNumbers(new Set(
        entryClosures.flatMap((entry) => entry.missingGosubTargets),
    ));
    const completeClosureCount = entryClosures.filter(
        (entry) => entry.completeGosubClosure).length;
    const tsEligibleClosureCount = entryClosures.filter((entry) => entry.tsEligible).length;

    return deepFreeze({
        dialect: registry.dialect,
        revision: registry.revision,
        decodeKnown: true as const,
        registryScriptCount: registry.size,
        registryInstructionCount: instructionCount(registryScripts),
        reachableScriptCount: reachableScripts.length,
        reachableInstructionCount: instructionCount(reachableScripts),
        entryClosureInstructionCount: entryClosures.reduce(
            (total, entry) => total + entry.instructionCount, 0),
        completeClosureCount,
        tsEligibleClosureCount,
        interfaceTsEligible: entryClosures.length > 0 &&
            tsEligibleClosureCount === entryClosures.length,
        entryClosures,
        opcodeFrequencies: frequencies,
        unsupportedCoreOpcodes: selectReviewClasses(frequencies, ['core-unreviewed']),
        schemaOnlyHostOpcodes: selectReviewClasses(frequencies, ['host-schema-only']),
        unimplementedHostOpcodes: frequenciesForOpcodes(
            frequencies, unionCoverage.unimplementedHostOpcodes),
        unresolvedDynamicHookOpcodes: frequenciesForOpcodes(
            frequencies, unionCoverage.unresolvedDynamicHookOpcodes),
        unresolvedDynamicHookSourceScriptIds:
            unionCoverage.unresolvedDynamicHookSourceScriptIds,
        unresolvedInterfaceGroupOpcodes: frequenciesForOpcodes(
            frequencies, unionCoverage.unresolvedInterfaceGroupOpcodes),
        missingGosubTargets,
    });
}

function auditEntry(
    registry: CS2CoreScriptRegistry,
    entryScriptId: number,
): CS2BackendEntryCoverage {
    const coverage = analyzeCS2EngineClosure(registry, [entryScriptId]);
    const scripts = scriptsForIds(registry, coverage.scriptIds);
    const frequencies = opcodeFrequencies(
        scripts, new Set(coverage.unsupportedCoreOpcodes));
    const completeGosubClosure = coverage.missingScriptIds.length === 0;
    const executableReviewedInstructionCount = frequencies
        .filter((row) => row.executableReviewed)
        .reduce((total, row) => total + row.count, 0);
    return deepFreeze({
        entryScriptId,
        name: registry.get(entryScriptId)?.name ?? `script_${entryScriptId}`,
        completeGosubClosure,
        tsEligible: coverage.supported,
        scriptIds: coverage.scriptIds,
        scriptCount: coverage.scriptIds.length,
        instructionCount: instructionCount(scripts),
        executableReviewedInstructionCount,
        unsupportedCoreOpcodes: selectReviewClasses(frequencies, ['core-unreviewed']),
        schemaOnlyHostOpcodes: selectReviewClasses(frequencies, ['host-schema-only']),
        unimplementedHostOpcodes: frequenciesForOpcodes(
            frequencies, coverage.unimplementedHostOpcodes),
        unresolvedDynamicHookOpcodes: frequenciesForOpcodes(
            frequencies, coverage.unresolvedDynamicHookOpcodes),
        unresolvedDynamicHookSourceScriptIds:
            coverage.unresolvedDynamicHookSourceScriptIds,
        unresolvedInterfaceGroupOpcodes: frequenciesForOpcodes(
            frequencies, coverage.unresolvedInterfaceGroupOpcodes),
        missingGosubTargets: coverage.missingScriptIds,
    });
}

/** Aggregate already-decoded interface reports without re-counting a script
 * within an interface. Identical cache scripts in different interfaces remain
 * separate observations, which is the useful corpus-level weighting. */
export function aggregateCS2BackendCoverage(
    interfaces: readonly CS2BackendRegistryCoverage[],
): Readonly<{
    registryScriptCount: number;
    registryInstructionCount: number;
    reachableScriptCount: number;
    reachableInstructionCount: number;
    entryClosureInstructionCount: number;
    entryClosureCount: number;
    completeClosureCount: number;
    tsEligibleClosureCount: number;
    tsEligibleInterfaceCount: number;
    opcodeFrequencies: readonly CS2OpcodeFrequency[];
    unsupportedCoreOpcodes: readonly CS2OpcodeFrequency[];
    schemaOnlyHostOpcodes: readonly CS2OpcodeFrequency[];
    unimplementedHostOpcodes: readonly CS2OpcodeFrequency[];
    unresolvedDynamicHookOpcodes: readonly CS2OpcodeFrequency[];
    unresolvedDynamicHookSourceScriptIds: readonly number[];
    unresolvedInterfaceGroupOpcodes: readonly CS2OpcodeFrequency[];
    missingGosubTargets: readonly number[];
}> {
    const frequencies = mergeFrequencies(interfaces.map((item) => item.opcodeFrequencies));
    return deepFreeze({
        registryScriptCount: sum(interfaces, 'registryScriptCount'),
        registryInstructionCount: sum(interfaces, 'registryInstructionCount'),
        reachableScriptCount: sum(interfaces, 'reachableScriptCount'),
        reachableInstructionCount: sum(interfaces, 'reachableInstructionCount'),
        entryClosureInstructionCount: sum(interfaces, 'entryClosureInstructionCount'),
        entryClosureCount: interfaces.reduce(
            (total, item) => total + item.entryClosures.length, 0),
        completeClosureCount: sum(interfaces, 'completeClosureCount'),
        tsEligibleClosureCount: sum(interfaces, 'tsEligibleClosureCount'),
        tsEligibleInterfaceCount: interfaces.filter((item) => item.interfaceTsEligible).length,
        opcodeFrequencies: frequencies,
        unsupportedCoreOpcodes: selectReviewClasses(frequencies, ['core-unreviewed']),
        schemaOnlyHostOpcodes: selectReviewClasses(frequencies, ['host-schema-only']),
        unimplementedHostOpcodes: mergeFrequencies(
            interfaces.map((item) => item.unimplementedHostOpcodes)),
        unresolvedDynamicHookOpcodes: mergeFrequencies(
            interfaces.map((item) => item.unresolvedDynamicHookOpcodes)),
        unresolvedDynamicHookSourceScriptIds: sortedNumbers(new Set(
            interfaces.flatMap((item) => item.unresolvedDynamicHookSourceScriptIds),
        )),
        unresolvedInterfaceGroupOpcodes: mergeFrequencies(
            interfaces.map((item) => item.unresolvedInterfaceGroupOpcodes)),
        missingGosubTargets: sortedNumbers(new Set(
            interfaces.flatMap((item) => item.missingGosubTargets),
        )),
    });
}

function opcodeFrequencies(
    scripts: readonly CS2DecodedCoreScript[],
    dialectUnsupportedCore: ReadonlySet<number> = new Set<number>(),
): readonly CS2OpcodeFrequency[] {
    const rows = new Map<number, MutableFrequency>();
    for( const script of scripts ) {
        for( const instruction of script.instructions ) {
            const opcode = instruction.opcode | 0;
            let row = rows.get(opcode);
            if( !row ) {
                const reviewClass = reviewClassForOpcode(opcode, dialectUnsupportedCore);
                row = {
                    opcode,
                    name: instruction.opcodeName,
                    decodeKnown: true,
                    reviewClass,
                    executableReviewed: reviewClass === 'core-reviewed' ||
                        reviewClass === 'host-reviewed',
                    count: 0,
                };
                rows.set(opcode, row);
            }
            row.count++;
        }
    }
    return Object.freeze([...rows.values()]
        .sort((left, right) => left.opcode - right.opcode)
        .map((row) => Object.freeze({ ...row })));
}

function reviewClassForOpcode(
    opcode: number,
    dialectUnsupportedCore: ReadonlySet<number>,
): CS2OpcodeReviewClass {
    if( CS2_CORE_DISPATCH_BY_OPCODE[opcode] !== undefined )
        return dialectUnsupportedCore.has(opcode) ? 'core-unreviewed' : 'core-reviewed';
    if( CS2_HOST_REQUEST_METADATA_BY_OPCODE[opcode] === undefined ) return 'core-unreviewed';
    return cs2HostOpcodeHasReviewedExecutableSemantics(opcode)
        ? 'host-reviewed' : 'host-schema-only';
}

function scriptsForIds(
    registry: CS2CoreScriptRegistry,
    ids: readonly number[],
): readonly CS2DecodedCoreScript[] {
    const result: CS2DecodedCoreScript[] = [];
    for( const id of ids ) {
        const script = registry.get(id);
        if( script ) result.push(script);
    }
    return result;
}

function instructionCount(scripts: readonly CS2DecodedCoreScript[]): number {
    return scripts.reduce((total, script) => total + script.instructions.length, 0);
}

function selectReviewClasses(
    frequencies: readonly CS2OpcodeFrequency[],
    classes: readonly CS2OpcodeReviewClass[],
): readonly CS2OpcodeFrequency[] {
    const selected = new Set(classes);
    return Object.freeze(frequencies.filter((row) => selected.has(row.reviewClass)));
}

function frequenciesForOpcodes(
    frequencies: readonly CS2OpcodeFrequency[],
    opcodes: readonly number[],
): readonly CS2OpcodeFrequency[] {
    const selected = new Set(opcodes);
    return Object.freeze(frequencies.filter((row) => selected.has(row.opcode)));
}

function mergeFrequencies(
    groups: readonly (readonly CS2OpcodeFrequency[])[],
): readonly CS2OpcodeFrequency[] {
    const rows = new Map<number, MutableFrequency>();
    for( const group of groups ) {
        for( const source of group ) {
            const row = rows.get(source.opcode);
            if( row ) row.count += source.count;
            else rows.set(source.opcode, { ...source });
        }
    }
    return Object.freeze([...rows.values()]
        .sort((left, right) => left.opcode - right.opcode)
        .map((row) => Object.freeze({ ...row })));
}

function sortedNumbers(values: Iterable<number>): readonly number[] {
    return Object.freeze([...new Set(values)].sort((left, right) => left - right));
}

function sum<K extends keyof CS2BackendRegistryCoverage>(
    values: readonly CS2BackendRegistryCoverage[],
    key: K,
): number {
    return values.reduce((total, item) => total + Number(item[key]), 0);
}

function deepFreeze<T>(value: T): T {
    if( value && typeof value === 'object' && !Object.isFrozen(value) ) {
        Object.freeze(value);
        for( const child of Object.values(value as Record<string, unknown>) ) deepFreeze(child);
    }
    return value;
}
