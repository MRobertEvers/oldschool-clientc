/*
 * The build: .tsx in, content tree out.
 *
 * Output goes straight into the cachepack tree — `interfaces/<name>.if`,
 * `interfaces/<name>.compack`, `scripts/<name>.cs2`, and the two pack files that
 * hold the ids. There is no private format in between, so the next step is the bake
 * the tree already has (`make -C src torirsserver-cache`), and a generated
 * interface is diffable beside a hand-authored one.
 *
 * Files this compiler did not write are never overwritten. Generated files carry a
 * marker line, and a build that finds a file without one stops and says so rather
 * than replacing whatever was there.
 */

import { readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, basename, extname, resolve, dirname } from 'node:path';

import { ModuleGraph, renderModule } from './loader.js';
import { lower, Cs2domError } from './ir.js';
import { emitInterface, emitCompack } from './emit_if.js';
import { emitScript } from './emit_cs2.js';
import { Ledger } from './ledger.js';

const MARKER = 'cs2dom';

export function loadProject(projectDir) {
    const path = join(projectDir, 'cs2dom.json');
    if( !existsSync(path) )
        throw new Cs2domError(`no cs2dom.json in ${projectDir}`);
    const config = JSON.parse(readFileSync(path, 'utf8'));
    const root = resolve(projectDir);
    return {
        root,
        sources: resolve(root, config.sources || 'ui'),
        content: resolve(root, config.content),
        varcPool: config.varcPool || null,
        prefix: config.prefix || '',
        cachegen: config.cachegen || null,
    };
}

function sourceFiles(dir) {
    return readdirSync(dir)
        .filter((f) => extname(f) === '.tsx')
        .filter((f) => !f.startsWith('_'))
        .sort()
        .map((f) => join(dir, f));
}

/**
 * Build every interface in the project.
 *
 * `dryRun` renders and emits into memory without touching the tree — which is what
 * the tests use, and what makes it safe to run a build against real content to see
 * what it would say.
 */
export function build(project, { dryRun = false, log = () => {} } = {}) {
    const ledger = new Ledger(project.content);
    const graph = new ModuleGraph({ log });
    const cacheContext = readCacheContext(project);

    const results = [];
    const warnings = [];

    for( const file of sourceFiles(project.sources) ) {
        const name = project.prefix + basename(file, '.tsx');
        const interfaceId = ledger.interfaceId(name);

        const rendered = renderModule(graph, file, {
            varcPool: project.varcPool,
            varbitVarp: cacheContext.varbitVarp,
        });

        const ir = lower({
            tree: rendered.tree,
            states: rendered.states,
            name,
            interfaceId,
            scriptId: (scriptName) => ledger.scriptId(scriptName),
            ranges: cacheContext.counts,
        });

        const scripts = ir.scripts.map((script) => {
            const { source, warnings: scriptWarnings } = emitScript(script, interfaceId);
            for( const warning of scriptWarnings )
                warnings.push(`${name}/${script.name}: ${warning}`);
            return { name: script.name, id: script.id, source };
        });

        results.push({
            name,
            file,
            interfaceId,
            componentCount: ir.components.length,
            interfaceText: emitInterface(ir),
            compackText: emitCompack(ir),
            scripts,
            ir,
        });
    }

    if( dryRun )
        return { results, warnings, written: [], ledgerWrites: [] };

    const written = [];
    for( const result of results ) {
        written.push(writeGenerated(join(project.content, 'interfaces', `${result.name}.if`), result.interfaceText));
        written.push(writeGenerated(join(project.content, 'interfaces', `${result.name}.compack`), result.compackText, false));
        for( const script of result.scripts )
            written.push(writeGenerated(join(project.content, 'scripts', `${script.name}.cs2`), script.source));
    }

    const ledgerWrites = ledger.write();
    return { results, warnings, written: written.filter(Boolean), ledgerWrites };
}

/**
 * Write a generated file, refusing to stand on anything this compiler did not
 * produce. `.compack` has no comment syntax, so its ownership is inferred from the
 * `.if` beside it having been ours.
 */
function writeGenerated(path, text, marked = true) {
    if( existsSync(path) && marked ) {
        const existing = readFileSync(path, 'utf8');
        if( !existing.includes(MARKER) )
            throw new Cs2domError(
                `${path} was not written by cs2dom — refusing to overwrite it. ` +
                `Rename the component, or move the file aside if it is stale.`);
    }
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, text);
    return path;
}

/**
 * What the build needs to know about the cache: today just which varp a varbit
 * lives in, because that is the trigger a varbit's update binds to. Read from the
 * content tree so it reflects uncommitted edits, and empty when the configs are not
 * unpacked — in which case useVarbit asks for the varp explicitly.
 */
function readCacheContext(project) {
    return {
        varbitVarp: readVarbitVarps(project.content),
        counts: readCounts(project.content),
    };
}

/**
 * The highest id each config table defines.
 *
 * This is what turns `useVarp(99999)` into an error naming the range instead of a
 * component that never moves. Where the tree does not carry a table, the count is
 * absent and the check is skipped rather than guessed at.
 */
function readCounts(contentDir) {
    const counts = {};
    const tables = { varp: 'all.varp.compack', varbit: 'all.varbit.compack',
                     varc: 'all.varc.compack', inv: 'all.inv.compack' };
    for( const [kind, file] of Object.entries(tables) ) {
        const path = join(contentDir, 'configs', file);
        if( !existsSync(path) ) continue;
        let highest = -1;
        for( const id of readCompack(path).values() )
            if( id > highest ) highest = id;
        if( highest >= 0 ) counts[kind] = highest;
    }
    return counts;
}

/**
 * varbit id -> the varp it is packed into.
 *
 * The tree states this in two halves: `configs/all.varbit` holds one named block per
 * varbit with `basevar=<varp name>`, and the `.compack` files turn both names back
 * into ids. Reading it from the tree rather than from a built cache is deliberate —
 * a varbit the author added this morning is one the build should already know.
 */
export function readVarbitVarps(contentDir) {
    const varbits = readNamedConfig(join(contentDir, 'configs', 'all.varbit'));
    const varbitIds = readCompack(join(contentDir, 'configs', 'all.varbit.compack'));
    const varpIds = readCompack(join(contentDir, 'configs', 'all.varp.compack'));

    const varbitVarp = {};
    for( const [name, fields] of varbits ) {
        const id = varbitIds.get(name);
        const varp = varpIds.get(fields.basevar);
        if( id !== undefined && varp !== undefined ) varbitVarp[id] = varp;
    }
    return varbitVarp;
}

/** `[name]` blocks of `key=value`, the shape every cachepack config file has. */
export function readNamedConfig(path) {
    const blocks = new Map();
    if( !existsSync(path) ) return blocks;
    let current = null;
    for( const rawLine of readFileSync(path, 'utf8').split('\n') ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const opened = /^\[(.+)\]$/.exec(line);
        if( opened ) { current = {}; blocks.set(opened[1], current); continue; }
        const split = line.indexOf('=');
        if( split > 0 && current ) current[line.slice(0, split)] = line.slice(split + 1);
    }
    return blocks;
}

/** `<id>=<name>` — read as name -> id, which is the direction callers want. */
export function readCompack(path) {
    const byName = new Map();
    if( !existsSync(path) ) return byName;
    for( const rawLine of readFileSync(path, 'utf8').split('\n') ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const split = line.indexOf('=');
        if( split < 0 ) continue;
        const id = Number.parseInt(line.slice(0, split), 10);
        if( !Number.isNaN(id) ) byName.set(line.slice(split + 1).trim(), id);
    }
    return byName;
}
