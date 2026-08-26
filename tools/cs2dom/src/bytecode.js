/* Node-side transport for the browser C CS2VM/WASM runtime.
 *
 * Source remains cs2dom's authoring format. The existing cache compiler lowers
 * that source to ordinary .cs2b records, then the focused C CS2VM module
 * executes those records while JavaScript HostRuntime owns the React tree. No
 * native framebuffer participates in the live preview. Compilation is
 * content-addressed so host-state saves never rebuild an unchanged program.
 */

import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { collectInterfaceScripts } from './native_overlay.js';
import { compileScripts, findRepoRoot } from './verify.js';
import { packName } from './pack.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const PROGRAM_SCHEMA = 'cs2dom-bytecode/1';
const BINARY_SCRIPT_CACHE = new Map();

export function createBytecodePrograms(project, { log = () => {} } = {}) {
    const cache = new Map();
    const imported = new Map();
    return (result) => {
        /* Imported trees are read-only for the lifetime of the dev server.
         * Avoid rewalking hundreds of source files (and the 9k-script Dat2
         * archive) every time Save state asks for a fresh preview snapshot. */
        const importedKey = result?.source === 'content' || result?.source === 'dat2'
            ? `${result.source}:${result.contentDir || ''}:${result.name || ''}` : null;
        if( importedKey && imported.has(importedKey) ) return imported.get(importedKey);
        const input = programInput(project, result);
        const key = programKey(project, result, input);
        if( cache.has(key) ) return cache.get(key);
        const program = compileProgram(project, result, input, log);
        cache.set(key, program);
        if( importedKey ) imported.set(importedKey, program);
        while( cache.size > 32 ) cache.delete(cache.keys().next().value);
        return program;
    };
}

export function compileInterfaceProgram(project, result, options = {}) {
    return compileProgram(project, result, programInput(project, result), options.log || (() => {}));
}

function programInput(project, result) {
    const contentDir = result?.contentDir || project.unpackedContent || project.content ||
        project.dat2Content || null;
    let sources = [];
    if( result?.source === 'content' || result?.source === 'dat2' ) {
        try {
            sources = collectInterfaceScripts(contentDir, result.name, {
                cs2Names: resolveNames(project),
            });
        }
        catch { sources = (result.scripts || []).filter(isSourceRecord); }
    } else {
        sources = (result?.scripts || []).filter((script) => Number.isInteger(script.id) &&
            typeof script.source === 'string');
    }
    sources = dedupe(sources.filter((script) => Number.isInteger(script.id) && script.id >= 0));
    const exactRawFallback = result?.source === 'content'
        ? exactDat2Fallback(project, contentDir) : null;
    const rawDirectory = result?.source === 'dat2' && project.dat2RawScripts
        ? project.dat2RawScripts : exactRawFallback?.directory ||
            (contentDir ? join(contentDir, 'scripts') : null);
    const raw = contentDir && rawDirectory ? binaryScripts(contentDir, rawDirectory) : [];
    return {
        contentDir, sources, raw, exactRawFallback,
        /* Dat2 owns these exact cache payloads. Prefer them over recompiling
         * readable decompiler output; source remains useful for finding the
         * selected interface's transitive script closure. */
        preferRaw: result?.source === 'dat2' && rawDirectory === project.dat2RawScripts,
    };
}

function compileProgram(project, result, input, log) {
    const root = findRepoRoot(HERE);
    if( !root ) return unavailable('repository root/CS2 compiler was not found');
    const names = resolveNames(project);
    const revision = project.revision || null;
    const roots = hookIds(result?.ir);
    const rawIds = new Set(input.raw.map((script) => script.id));
    const selectedIds = new Set([...roots, ...input.sources.map((script) => script.id)]);
    const selectedRaw = input.raw.filter((script) => selectedIds.has(script.id));
    const rawProgramComplete = input.preferRaw &&
        [...selectedIds].every((id) => rawIds.has(id));
    let compiled = { ok: true, bytecode: [], failures: [], output: '' };
    const fallbackFailures = new Map();
    if( input.sources.length && !rawProgramComplete ) {
        const sources = normalizeCompilerSources(input.sources, hookIds(result?.ir));
        const rawById = new Map(selectedRaw.map((script) => [script.id, script]));
        const fallbackIds = new Set();
        if( input.exactRawFallback ) {
            for( const source of sources ) {
                if( !rawById.has(source.id) || !unconditionalSelfRecursiveStub(source) ) continue;
                fallbackIds.add(source.id);
                fallbackFailures.set(source.id, {
                    id: source.id,
                    name: source.name,
                    fallbackKind: 'lossy-stub',
                    message: 'decompiler emitted an unconditional self-recursive stub',
                });
            }
        }
        const compileOptions = {
            repoRoot: root,
            names,
            revision,
            cache: project.cache || null,
            rawScripts: selectedRaw,
            returnBytecode: true,
        };
        let active = sources.filter((script) => !fallbackIds.has(script.id));
        compiled = compileSourceClosure(active, compileOptions);

        /* OSRS-Content is decompiler output. A small number of records are
         * provably lossy (for example a callback alias assembled from unrelated
         * symbols) and cannot round-trip. If, and only if, prepareDat2Project
         * supplied bytecode whose cache/revision/CRC identity matches this
         * content tree, preserve the successfully authored records and replace
         * each compiler-failed record with that exact original cache payload. */
        if( input.exactRawFallback ) {
            for( let pass = 0; pass < sources.length; pass++ ) {
                let added = 0;
                for( const failure of compiled.failures || [] ) {
                    if( fallbackIds.has(failure.id) || !rawById.has(failure.id) ) continue;
                    fallbackIds.add(failure.id);
                    fallbackFailures.set(failure.id, failure);
                    added++;
                }
                /* A source record can compile cleanly yet still be provably
                 * lossy. Array arguments are one rev-239 decompiler failure:
                 * the rebuilt footer advertises a different int/string
                 * contract, so GOSUB fails before the callee can run. Only an
                 * exact cache footer is authoritative enough to replace such
                 * an otherwise successful record. */
                for( const script of compiled.bytecode || [] ) {
                    if( fallbackIds.has(script.id) || !rawById.has(script.id) ) continue;
                    const rebuilt = rawArgumentSignature(script);
                    const exact = rawArgumentSignature(rawById.get(script.id));
                    if( !rebuilt || !exact || sameArgumentSignature(rebuilt, exact) ) continue;
                    fallbackIds.add(script.id);
                    fallbackFailures.set(script.id, {
                        id: script.id,
                        name: script.name,
                        fallbackKind: 'signature-mismatch',
                        message: `decompiled argument signature ${formatArgumentSignature(rebuilt)} ` +
                            `does not match exact cache signature ${formatArgumentSignature(exact)}`,
                    });
                    added++;
                }
                if( !added ) break;
                active = sources.filter((script) => !fallbackIds.has(script.id));
                compiled = compileSourceClosure(active, compileOptions);
            }
        }
    }
    if( !compiled.ok ) {
        const detail = compiled.failures?.map((failure) =>
            `${failure.name} (${failure.id}): ${failure.message}`).join('; ') ||
            String(compiled.output || 'compiler did not produce a program').trim();
        log(`  C CS2VM/WASM bytecode unavailable for ${result?.name || 'interface'}: ${detail}`);
        return unavailable(detail);
    }

    const records = new Map();
    const rawNeeded = rawProgramComplete
        ? rawDependencyIds(input.raw, input.contentDir, selectedIds)
        : referencedRawIds(input.sources, input.raw, roots);
    for( const id of fallbackFailures.keys() ) rawNeeded.add(id);
    if( fallbackFailures.size ) {
        const compiledIds = new Set((compiled.bytecode || []).map((script) => script.id));
        for( const [id, from] of exactRawDependencyClosure(
            input.raw, input.contentDir, fallbackFailures.keys()) ) {
            rawNeeded.add(id);
            if( fallbackFailures.has(id) || compiledIds.has(id) ) continue;
            const raw = input.raw.find((script) => script.id === id);
            fallbackFailures.set(id, {
                id,
                name: raw?.name || `script_${id}`,
                fallbackKind: 'dependency',
                message: `transitive dependency of exact Dat2 fallback script ${from}`,
            });
        }
    }
    for( const script of input.raw )
        if( rawNeeded.has(script.id) ) records.set(script.id, bytecodeRecord(script));
    for( const script of compiled.bytecode || [] ) records.set(script.id, bytecodeRecord(script));
    const missingRoots = roots.filter((id) => !records.has(id));
    const fallbacks = [...fallbackFailures].sort(([left], [right]) => left - right)
        .map(([id, failure]) => ({
            id,
            name: failure.name || input.raw.find((script) => script.id === id)?.name ||
                `script_${id}`,
            source: 'exact-dat2',
            cache: input.exactRawFallback.cache,
            revision: input.exactRawFallback.revision,
            reasonKind: failure.fallbackKind || 'compile-failure',
            reason: failure.message,
        }));
    const warnings = [
        ...(missingRoots.length
            ? [`hook scripts unavailable in extracted content: ${missingRoots.join(', ')}`] : []),
        ...fallbacks.map((fallback) => fallback.reasonKind === 'dependency'
            ? `${fallback.name} (${fallback.id}) uses exact ${fallback.revision} Dat2 bytecode ` +
                `as a dependency of another exact fallback: ${fallback.reason}`
            : `${fallback.name} (${fallback.id}) uses exact ${fallback.revision} Dat2 bytecode ` +
                `because its decompiled source is not executable as-is: ${fallback.reason}`),
    ];
    return {
        schema: PROGRAM_SCHEMA,
        available: missingRoots.length === 0,
        dialect: 'osrs',
        revision,
        entries: roots,
        scripts: [...records.values()].sort((left, right) => left.id - right.id),
        warnings,
        fallbacks,
    };
}

function unconditionalSelfRecursiveStub(script) {
    const source = String(script?.source || '')
        .replace(/\/\*[\s\S]*?\*\//g, '')
        .replace(/^\s*\/\/.*$/gm, '')
        .trim();
    const header = /^\s*\[(?:proc|clientscript),([^\]]+)\][^\n]*\n/.exec(source);
    if( !header ) return false;
    const body = source.slice(header[0].length).trim();
    const call = /^~([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)\s*(?:\([^;]*\))?\s*;$/.exec(body);
    if( !call ) return false;
    return call[1] === header[1] || call[1] === script.name ||
        call[1] === `script${script.id}` || call[1] === `script_${script.id}`;
}

function compileSourceClosure(sources, options) {
    if( !sources.length )
        return { ok: true, bytecode: [], failures: [], output: 'compiled 0, failed 0' };
    let compiled = compileScripts(sources, options);
    /* A callback descriptor needs the return stack types of each nested
     * `~proc` argument. With no Dat2 cache, the C compiler can only learn those
     * types from bytecode in its raw script store. Its first pass still emits
     * every independent source record, so feed those exact bytes back as a
     * signature oracle and retry the failed records. */
    let oracle = mergeScriptBytes(options.rawScripts, compiled.bytecode);
    let oracleCount = (options.rawScripts || []).length;
    for( let pass = 0; !compiled.ok && pass < 4 && oracle.length > oracleCount; pass++ ) {
        oracleCount = oracle.length;
        compiled = compileScripts(sources, { ...options, rawScripts: oracle });
        oracle = mergeScriptBytes(oracle, compiled.bytecode);
    }
    return compiled;
}

function mergeScriptBytes(...groups) {
    const records = new Map();
    for( const group of groups )
        for( const script of group || [] )
            if( Number.isInteger(script.id) && script.id >= 0 ) records.set(script.id, script);
    return [...records.values()].sort((left, right) => left.id - right.id);
}

function exactRawDependencyClosure(rawScripts, contentDir, rootIds) {
    const byId = new Map((rawScripts || []).map((script) => [script.id, script]));
    const byName = new Map();
    for( const script of rawScripts || [] ) {
        byName.set(script.name, script.id);
        byName.set(`script${script.id}`, script.id);
        byName.set(`script_${script.id}`, script.id);
    }
    const result = new Map();
    const pending = [...rootIds].map((id) => [id, id]);
    const visited = new Set();
    while( pending.length ) {
        const [id, origin] = pending.shift();
        if( visited.has(id) ) continue;
        visited.add(id);
        const script = byId.get(id);
        if( !script ) continue;
        const targets = new Set(rawGosubTargets(script));
        const sourcePath = contentDir && script.name
            ? join(contentDir, 'scripts', `${script.name}.cs2`) : null;
        if( sourcePath && existsSync(sourcePath) ) {
            const source = readFileSync(sourcePath, 'utf8');
            for( const pattern of [
                /~([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g,
                /["']([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({]|["']))/g,
            ] ) {
                for( const match of source.matchAll(pattern) ) {
                    let target = byName.get(match[1]);
                    if( !Number.isInteger(target) ) {
                        const suffix = /(?:^|_)(\d+)$/.exec(match[1]);
                        const numeric = suffix ? Number(suffix[1]) : null;
                        if( Number.isInteger(numeric) && byId.has(numeric) ) target = numeric;
                    }
                    if( Number.isInteger(target) ) targets.add(target);
                }
            }
        }
        for( const target of targets ) {
            if( !byId.has(target) || visited.has(target) ) continue;
            if( !result.has(target) ) result.set(target, id);
            pending.push([target, origin]);
        }
    }
    return result;
}

function rawDependencyIds(rawScripts, contentDir, rootIds) {
    const result = new Set(rootIds);
    for( const id of exactRawDependencyClosure(rawScripts, contentDir, result).keys() )
        result.add(id);
    return result;
}

function rawGosubTargets(script) {
    let bytes;
    try { bytes = Buffer.from(script.bytes ?? readFileSync(script.file)); }
    catch { return []; }
    const decoded = rawScriptInstructions(bytes);
    return decoded ? decoded.instructions
        .filter(({ opcode, operand }) => opcode === 40 && operand >= 0)
        .map(({ operand }) => operand) : [];
}

function rawScriptInstructions(bytes) {
    if( bytes.length < 16 ) return null;
    const trailerLength = bytes.readUInt16BE(bytes.length - 2);
    for( const footerSize of [14, 18] ) {
        const trailer = bytes.length - footerSize - trailerLength;
        if( trailer <= 0 || trailer + 4 > bytes.length - 2 ) continue;
        const opCount = bytes.readInt32BE(trailer);
        if( opCount <= 0 || opCount > 65536 ) continue;
        let offset = bytes.indexOf(0) + 1;
        if( offset <= 0 || offset >= trailer ) continue;
        const instructions = [];
        let valid = true;
        for( let opIndex = 0; opIndex < opCount && valid; opIndex++ ) {
            if( offset + 2 > trailer ) { valid = false; break; }
            const opcode = bytes.readUInt16BE(offset);
            offset += 2;
            let operand = 0;
            if( opcode === 61 ) {
                if( offset + 8 > trailer ) { valid = false; break; }
                operand = bytes.subarray(offset, offset + 8);
                offset += 8;
            } else if( opcode >= 100 || [21, 38, 39, 62, 63].includes(opcode) ) {
                if( offset + 1 > trailer ) { valid = false; break; }
                operand = bytes.readInt8(offset);
                offset++;
            } else if( opcode === 3 ) {
                const end = bytes.indexOf(0, offset);
                if( end < 0 || end >= trailer ) { valid = false; break; }
                operand = bytes.toString('utf8', offset, end);
                offset = end + 1;
            } else {
                if( offset + 4 > trailer ) { valid = false; break; }
                operand = bytes.readInt32BE(offset);
                offset += 4;
            }
            instructions.push({ opcode, operand });
        }
        if( valid && offset === trailer )
            return { footerSize, trailer, instructions };
    }
    return null;
}

function rawArgumentSignature(script) {
    let bytes;
    try { bytes = Buffer.from(script.bytes ?? readFileSync(script.file)); }
    catch { return null; }
    const decoded = rawScriptInstructions(bytes);
    if( !decoded ) return null;
    const offset = decoded.trailer + (decoded.footerSize === 18 ? 10 : 8);
    return {
        ints: bytes.readUInt16BE(offset),
        strings: bytes.readUInt16BE(offset + 2),
        longs: decoded.footerSize === 18 ? bytes.readUInt16BE(offset + 4) : 0,
    };
}

function sameArgumentSignature(left, right) {
    return left.ints === right.ints && left.strings === right.strings &&
        left.longs === right.longs;
}

function formatArgumentSignature(signature) {
    return `${signature.ints}i/${signature.strings}s/${signature.longs}l`;
}

function referencedRawIds(sources, raw, roots) {
    const byName = new Map();
    for( const script of raw ) {
        byName.set(script.name, script.id);
        byName.set(`script${script.id}`, script.id);
        byName.set(`script_${script.id}`, script.id);
    }
    const needed = new Set(roots.filter((id) => raw.some((script) => script.id === id)));
    for( const source of sources ) {
        for( const match of source.source.matchAll(
            /~([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g) ) {
            const id = byName.get(match[1]);
            if( Number.isInteger(id) ) needed.add(id);
        }
        for( const match of source.source.matchAll(
            /["']([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({]|["']))/g) ) {
            const id = byName.get(match[1]);
            if( Number.isInteger(id) ) needed.add(id);
        }
    }
    return needed;
}

/* Editable content occasionally keeps a friendly pack/file alias while the
 * decompiled header exposes the underlying procedure name (for example
 * `mobile_billing_open_2496` -> `mobile_billing_open`). It can also contain a
 * helper decompiled as a clientscript but used exclusively through GOSUB. The
 * bytecode only stores ids, not those source-level labels. Normalize the
 * compiler's private copy so those aliases/roles resolve without changing the
 * user's readable `.cs2` file. */
export function normalizeCompilerSources(records, rootIds = []) {
    const byAlias = new Map();
    const byRoleAlias = new Map();
    const metadata = new Map();
    for( const record of records ) {
        const header = /\[([A-Za-z_][A-Za-z0-9_]*),([^\]]+)\]/.exec(record.source);
        if( !header ) continue;
        /* Trigger-specific decompiles use headers such as
         * `[worldmapelementmouseleave,710]`; deferred widget callbacks still
         * address that record by its pack alias. The compiler-only name is
         * therefore the friendly alias when the header's second arm is numeric. */
        const compilerName = /^(?=.*[A-Za-z_])[A-Za-z0-9_]+$/.test(record.compilerName || '')
            ? record.compilerName : null;
        const compilerRole = ['clientscript', 'proc'].includes(record.compilerRole)
            ? record.compilerRole : null;
        const declared = compilerName ||
            (/^(?=.*[A-Za-z_])[A-Za-z0-9_]+$/.test(header[2]) ? header[2] : record.name);
        const value = {
            record,
            role: compilerRole || header[1],
            declared,
            compilerRole,
            roles: new Set(),
        };
        metadata.set(record.id, value);
        byAlias.set(record.name, value);
        byAlias.set(value.declared, value);
        byAlias.set(`script${record.id}`, value);
        byAlias.set(`script_${record.id}`, value);
        byRoleAlias.set(`${value.role}\0${record.name}`, value);
        byRoleAlias.set(`${value.role}\0${value.declared}`, value);
        byRoleAlias.set(`${value.role}\0script${record.id}`, value);
        byRoleAlias.set(`${value.role}\0script_${record.id}`, value);
    }
    /* Content authors routinely replace a decompiler placeholder with a
     * meaningful pack name while another source file still carries the old
     * `..._<script id>` spelling. Bytecode identity is the numeric id, so use
     * that suffix as a compiler-only compatibility alias. This keeps readable
     * source edits independent without modifying the user's files. */
    const valueForAlias = (alias, expectedRole, allowNumericSuffix = true) => {
        const typed = expectedRole ? byRoleAlias.get(`${expectedRole}\0${alias}`) : null;
        if( typed ) return typed;
        const direct = byAlias.get(alias);
        if( direct ) {
            if( expectedRole && direct.role !== expectedRole ) {
                const alternate = byRoleAlias.get(`${expectedRole}\0${direct.declared}`);
                if( alternate ) return alternate;
            }
            return direct;
        }
        if( !allowNumericSuffix ) return null;
        const suffix = /(?:^|_)(\d+)$/.exec(alias);
        const suffixed = suffix ? metadata.get(Number(suffix[1])) : null;
        if( suffixed && expectedRole && suffixed.role !== expectedRole )
            return byRoleAlias.get(`${expectedRole}\0${suffixed.declared}`) || suffixed;
        return suffixed;
    };
    const declaredForAlias = (alias, expectedRole, allowNumericSuffix = true) =>
        valueForAlias(alias, expectedRole, allowNumericSuffix)?.declared || alias;
    for( const id of rootIds ) metadata.get(id)?.roles.add('clientscript');
    for( const record of records ) {
        for( const match of record.source.matchAll(
            /~([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g) )
            valueForAlias(match[1], 'proc')?.roles.add('proc');
        for( const match of record.source.matchAll(
            /(?:cc|if)_seton[a-z0-9_]*\(\s*["']([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g) )
            valueForAlias(match[1], 'clientscript')?.roles.add('clientscript');
    }

    return records.map((record) => {
        let source = record.source.replace(/~([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g,
            (whole, alias) => `~${declaredForAlias(alias, 'proc')}`);
        /* Numeric-suffix recovery is safe only where the syntax requires a
         * clientscript. A graphic such as "graphic_5773" must not become the
         * unrelated script whose id happens to be 5773. */
        source = source.replace(
            /((?:cc|if)_seton[a-z0-9_]*\(\s*["'])([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)/g,
            (whole, prefix, alias) =>
                `${prefix}${declaredForAlias(alias, 'clientscript')}`);
        source = source.replace(
            /(["'])([A-Za-z0-9_]*[A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({][^"']*)?["'])/g,
            (whole, quote, alias) =>
                `${quote}${declaredForAlias(alias, 'clientscript', false)}`);
        const value = metadata.get(record.id);
        if( value ) {
            const role = value.compilerRole ||
                (value.roles.has('proc') && !value.roles.has('clientscript')
                    ? 'proc' : value.roles.has('clientscript') && !value.roles.has('proc')
                        ? 'clientscript' : value.role);
            source = source.replace(
                /\[([A-Za-z_][A-Za-z0-9_]*),([^\]]+)\]/,
                `[${role},${value.declared}]`);
        }
        return { ...record, source };
    });
}

function binaryScripts(contentDir, directory = join(contentDir, 'scripts')) {
    const key = `${resolve(contentDir)}\0${resolve(directory)}`;
    if( BINARY_SCRIPT_CACHE.has(key) ) return BINARY_SCRIPT_CACHE.get(key);
    const names = readPack(join(contentDir, 'pack', '12_clientscripts.pack'));
    const ids = new Map([...names].map(([id, name]) => [name, id]));
    if( !existsSync(directory) ) return [];
    const records = readdirSync(directory)
        .filter((file) => ['.cs2b', '.bin'].includes(extname(file)))
        .map((file) => {
            const basename = file.slice(0, -extname(file).length);
            const numeric = /^script_?(\d+)$/.exec(basename);
            const numericId = numeric ? Number(numeric[1]) : null;
            const id = ids.get(basename) ??
                (Number.isInteger(numericId) && names.has(numericId) ? numericId : null);
            return Number.isInteger(id) ? {
                id, name: names.get(id) || basename, file: join(directory, file),
            } : null;
        }).filter(Boolean);
    BINARY_SCRIPT_CACHE.set(key, records);
    return records;
}

/* Exact fallback is deliberately stricter than "a raw directory exists". The
 * extracted marker must name the configured cache and revision, and cachepack's
 * source fingerprints in both content trees must agree. */
function exactDat2Fallback(project, contentDir) {
    if( !project.dat2RawScripts || !project.cache || !project.revision || !contentDir )
        return null;
    const directory = resolve(project.dat2RawScripts);
    const derived = resolve(directory, '..', '..');
    const markerPath = join(derived, '.cs2dom-ready.json');
    const contentMeta = cacheSourceIdentity(join(contentDir, 'meta.ini'));
    const derivedMeta = cacheSourceIdentity(join(derived, 'meta.ini'));
    if( !existsSync(directory) || !existsSync(markerPath) || !contentMeta || !derivedMeta )
        return null;
    try {
        const marker = JSON.parse(readFileSync(markerPath, 'utf8'));
        const cache = resolve(project.cache);
        if( resolve(marker.cache || '') !== cache || marker.revision !== project.revision ||
            contentMeta.revision !== project.revision ||
            derivedMeta.revision !== project.revision ||
            contentMeta.size !== derivedMeta.size || contentMeta.crc !== derivedMeta.crc )
            return null;
        return {
            directory,
            cache,
            revision: project.revision,
            size: contentMeta.size,
            crc: contentMeta.crc,
        };
    } catch {
        return null;
    }
}

function cacheSourceIdentity(path) {
    if( !existsSync(path) ) return null;
    const values = new Map();
    for( const raw of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const match = /^\s*([a-z0-9_]+)\s*=\s*(.*?)\s*$/i.exec(raw);
        if( match ) values.set(match[1], match[2]);
    }
    const revision = values.get('rev_name');
    const size = Number(values.get('dat2_size'));
    const crc = values.get('dat2_crc32');
    return revision && Number.isSafeInteger(size) && size >= 0 && /^[0-9a-f]+$/i.test(crc || '')
        ? { revision, size, crc: crc.toLowerCase() } : null;
}

export const __bytecodeTest = Object.freeze({
    exactDat2Fallback,
    rawArgumentSignature,
    unconditionalSelfRecursiveStub,
});

function bytecodeRecord(script) {
    const bytes = Buffer.from(script.bytes ?? readFileSync(script.file));
    return { id: script.id, name: script.name || `script_${script.id}`, data: bytes.toString('base64') };
}

function hookIds(ir) {
    const ids = new Set();
    for( const component of ir?.components || [] ) {
        for( const hook of Object.values(component.hooks || {}) ) {
            const id = Number(hook?.script?.id ?? hook?.scriptId ?? -1);
            if( Number.isInteger(id) && id >= 0 ) ids.add(id);
        }
    }
    return [...ids].sort((left, right) => left - right);
}

function programKey(project, result, input) {
    const hash = createHash('sha256');
    hash.update(`${PROGRAM_SCHEMA}\0${project.revision || ''}\0${result?.source || ''}\0${result?.name || ''}`);
    for( const script of input.sources ) {
        hash.update(`\0s${script.id}:${script.name || ''}\0`);
        hash.update(script.source || '');
    }
    const selectedRawIds = input.preferRaw || input.exactRawFallback
        ? rawDependencyIds(input.raw, input.contentDir,
            new Set([...hookIds(result?.ir), ...input.sources.map((script) => script.id)]))
        : null;
    for( const script of input.raw ) {
        if( selectedRawIds && !selectedRawIds.has(script.id) ) continue;
        const info = statSync(script.file);
        hash.update(`\0b${script.id}:${info.size}:${info.mtimeMs}`);
    }
    return hash.digest('hex');
}

function resolveNames(project) {
    const candidates = [
        project.cs2Names,
        process.env.CACHEPACK_CS2_NAMES,
        join(homedir(), 'Documents', 'git_repos', 'cs2', 'src', 'main', 'resources',
            'org', 'runestar', 'cs2'),
    ].filter(Boolean).map((path) => resolve(path));
    return candidates.find((path) => existsSync(path) && statSync(path).isDirectory()) || null;
}

function readPack(path) {
    const result = new Map();
    if( !existsSync(path) ) return result;
    for( const raw of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        const name = packName(line.slice(split + 1));
        if( Number.isInteger(id) && name ) result.set(id, name);
    }
    return result;
}

function isSourceRecord(script) {
    return typeof script?.source === 'string' && (!script.file || extname(script.file) === '.cs2');
}

function dedupe(records) {
    return [...new Map(records.map((record) => [record.id, record])).values()]
        .sort((left, right) => left.id - right.id);
}

function unavailable(reason) {
    return {
        schema: PROGRAM_SCHEMA, available: false, dialect: 'osrs', revision: null,
        entries: [], scripts: [], warnings: [reason],
    };
}
