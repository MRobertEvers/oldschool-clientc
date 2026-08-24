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

const HERE = dirname(fileURLToPath(import.meta.url));
const PROGRAM_SCHEMA = 'cs2dom-bytecode/1';

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
        try { sources = collectInterfaceScripts(contentDir, result.name); }
        catch { sources = (result.scripts || []).filter(isSourceRecord); }
    } else {
        sources = (result?.scripts || []).filter((script) => Number.isInteger(script.id) &&
            typeof script.source === 'string');
    }
    sources = dedupe(sources.filter((script) => Number.isInteger(script.id) && script.id >= 0));
    const rawDirectory = result?.source === 'dat2' && project.dat2RawScripts
        ? project.dat2RawScripts : contentDir ? join(contentDir, 'scripts') : null;
    const raw = contentDir && rawDirectory ? binaryScripts(contentDir, rawDirectory) : [];
    return {
        contentDir, sources, raw,
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
    const rawProgramComplete = input.preferRaw &&
        [...selectedIds].every((id) => rawIds.has(id));
    let compiled = { ok: true, bytecode: [], failures: [], output: '' };
    if( input.sources.length && !rawProgramComplete ) {
        const sources = normalizeCompilerSources(input.sources, hookIds(result?.ir));
        compiled = compileScripts(sources, {
            repoRoot: root,
            names,
            revision,
            cache: project.cache || null,
            rawScripts: input.raw,
            returnBytecode: true,
        });
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
        ? selectedIds : referencedRawIds(input.sources, input.raw, roots);
    for( const script of input.raw )
        if( rawNeeded.has(script.id) ) records.set(script.id, bytecodeRecord(script));
    for( const script of compiled.bytecode || [] ) records.set(script.id, bytecodeRecord(script));
    const missingRoots = roots.filter((id) => !records.has(id));
    const warnings = missingRoots.length
        ? [`hook scripts unavailable in extracted content: ${missingRoots.join(', ')}`] : [];
    return {
        schema: PROGRAM_SCHEMA,
        available: missingRoots.length === 0,
        dialect: 'osrs',
        revision,
        entries: roots,
        scripts: [...records.values()].sort((left, right) => left.id - right.id),
        warnings,
    };
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
        for( const match of source.source.matchAll(/~([A-Za-z_][A-Za-z0-9_]*)/g) ) {
            const id = byName.get(match[1]);
            if( Number.isInteger(id) ) needed.add(id);
        }
        for( const match of source.source.matchAll(
            /["']([A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({]|["']))/g) ) {
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
    const metadata = new Map();
    for( const record of records ) {
        const header = /\[([A-Za-z_][A-Za-z0-9_]*),([^\]]+)\]/.exec(record.source);
        if( !header ) continue;
        /* Trigger-specific decompiles use headers such as
         * `[worldmapelementmouseleave,710]`; deferred widget callbacks still
         * address that record by its pack alias. The compiler-only name is
         * therefore the friendly alias when the header's second arm is numeric. */
        const declared = /^[A-Za-z_][A-Za-z0-9_]*$/.test(header[2])
            ? header[2] : record.name;
        const value = { record, role: header[1], declared, roles: new Set() };
        metadata.set(record.id, value);
        byAlias.set(record.name, value);
        byAlias.set(value.declared, value);
        byAlias.set(`script${record.id}`, value);
        byAlias.set(`script_${record.id}`, value);
    }
    for( const id of rootIds ) metadata.get(id)?.roles.add('clientscript');
    for( const record of records ) {
        for( const match of record.source.matchAll(/~([A-Za-z_][A-Za-z0-9_]*)/g) )
            byAlias.get(match[1])?.roles.add('proc');
        for( const match of record.source.matchAll(
            /(?:cc|if)_seton[a-z0-9_]*\(\s*["']([A-Za-z_][A-Za-z0-9_]*)/g) )
            byAlias.get(match[1])?.roles.add('clientscript');
    }

    return records.map((record) => {
        let source = record.source.replace(/~([A-Za-z_][A-Za-z0-9_]*)/g,
            (whole, alias) => `~${byAlias.get(alias)?.declared || alias}`);
        source = source.replace(/(["'])([A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({][^"']*)?["'])/g,
            (whole, quote, alias) => `${quote}${byAlias.get(alias)?.declared || alias}`);
        const value = metadata.get(record.id);
        if( value ) {
            const role = value.roles.has('proc') && !value.roles.has('clientscript')
                ? 'proc' : value.roles.has('clientscript') && !value.roles.has('proc')
                    ? 'clientscript' : value.role;
            source = source.replace(
                /\[([A-Za-z_][A-Za-z0-9_]*),([^\]]+)\]/,
                `[${role},${value.declared}]`);
        }
        return { ...record, source };
    });
}

function binaryScripts(contentDir, directory = join(contentDir, 'scripts')) {
    const names = readPack(join(contentDir, 'pack', '12_clientscripts.pack'));
    const ids = new Map([...names].map(([id, name]) => [name, id]));
    if( !existsSync(directory) ) return [];
    return readdirSync(directory).filter((file) => ['.cs2b', '.bin'].includes(extname(file)))
        .map((file) => {
            const name = file.slice(0, -extname(file).length);
            const id = ids.get(name);
            return Number.isInteger(id) ? {
                id, name, file: join(directory, file),
            } : null;
        }).filter(Boolean);
}

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
    const selectedRawIds = input.preferRaw
        ? new Set([...hookIds(result?.ir), ...input.sources.map((script) => script.id)]) : null;
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
        const name = line.slice(split + 1).trim();
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
