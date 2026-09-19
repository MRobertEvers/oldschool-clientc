/*
 * The gate: does the generated CS2 actually compile?
 *
 * Not with a model of the language — with `3rd/rscache/tools/cs2/cs2`, the compiler
 * cachepack itself calls. A dialect the emitter drifts away from is the failure mode
 * this whole design is most exposed to (cs2_compile.c accepts what cs2_gen.c emits
 * and guesses at nothing), and the only honest way to know is to hand the real
 * compiler the real output.
 *
 * Failures come back with the compiler's own message and the source line it names,
 * because a generated file is still a file someone has to read.
 */

import { spawnSync } from 'node:child_process';
import {
    existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

/** Where the tool lives relative to the repo root, and how to build it. */
export const CS2_TOOL = '3rd/rscache/tools/cs2/cs2';
export const CS2_TOOL_BUILD = 'make -C 3rd/rscache/tools cs2';

export function findRepoRoot(from) {
    let dir = resolve(from);
    for( let i = 0; i < 8; i++ ) {
        if( existsSync(join(dir, CS2_TOOL)) ) return dir;
        dir = resolve(dir, '..');
    }
    return null;
}

/**
 * Compile a set of `{ id, name, source }` scripts and report per-script results.
 *
 * The tool takes a directory of `<id>.cs2`, so the sources are staged into a temp
 * one — scratch, never the content tree.
 */
export function compileScripts(scripts, {
    repoRoot,
    scratch = tmpdir(),
    names = null,
    revision = null,
    cache = null,
    rawScripts = [],
    returnBytecode = false,
} = {}) {
    const tool = join(repoRoot, CS2_TOOL);
    if( !existsSync(tool) )
        return { ok: false, missingTool: true, tool, failures: [], compiled: 0 };

    const dir = mkdtempSync(join(scratch, 'cs2dom-verify-'));
    const src = join(dir, 'src');
    const out = join(dir, 'out');
    const raw = join(dir, 'raw');
    mkdirSync(src); mkdirSync(out); mkdirSync(raw);

    const byId = new Map();
    for( const script of scripts ) {
        writeFileSync(join(src, `${script.id}.cs2`), script.source);
        byId.set(String(script.id), script);
    }
    for( const script of rawScripts ) {
        if( !Number.isInteger(script.id) || script.id < 0 ) continue;
        const bytes = script.bytes ||
            (script.file && existsSync(script.file) ? readFileSync(script.file) : null);
        if( !bytes ) continue;
        writeFileSync(join(raw, String(script.id)), bytes);
    }

    /* Both streams: the tool reports failures on stderr and its tally on stdout,
     * and a verifier that read only one of them would call a failed run clean. */
    /* A real Dat2 cache is the best signature oracle for edited source: every
     * unchanged callee retains its original trigger/argument contract. Raw
     * records are the cache-less fallback used by unit fixtures and authored
     * projects. */
    const args = cache
        ? ['compile', '--cache', cache, '--src', src, '--out', out]
        : ['compile', '--raw', raw, '--src', src, '--out', out];
    if( revision ) args.push('--rev', revision);
    if( names ) args.push('--names', names);
    const run = spawnSync(tool, args, { encoding: 'utf8' });
    const output = `${run.stdout || ''}${run.stderr || ''}`;

    const failures = [];
    for( const line of output.split('\n') ) {
        const failed = /^FAIL (\d+)\.cs2: (.*)$/.exec(line.trim());
        if( failed ) {
            const script = byId.get(failed[1]);
            failures.push({ id: Number(failed[1]), name: script ? script.name : '?', message: failed[2],
                            source: script ? script.source : '' });
        }
    }
    const counted = /compiled (\d+), failed (\d+)/.exec(output);

    const bytecode = [];
    if( returnBytecode && existsSync(out) ) {
        for( const file of readdirSync(out) ) {
            if( !/^\d+$/.test(file) ) continue;
            const id = Number(file);
            const script = byId.get(file);
            bytecode.push({ id, name: script?.name || `script_${id}`, bytes: readFileSync(join(out, file)) });
        }
        bytecode.sort((left, right) => left.id - right.id);
    }

    rmSync(dir, { recursive: true, force: true });

    return {
        ok: failures.length === 0 && !!counted,
        missingTool: false,
        tool,
        compiled: counted ? Number(counted[1]) : 0,
        failures,
        output,
        bytecode,
    };
}
