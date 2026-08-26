/*
 * What an interface compiles to.
 *
 * This is the tool's own subject and the half no renderer supplies: given an
 * interface record, which scripts does it install, and what do those scripts
 * look like as CS2 and as JavaScript? The answer needs the C decompiler and the
 * emitter, and nothing else -- no tree, no layout, no canvas.
 *
 * Extracted from the old dev server so it outlives it: the preview is the wasm
 * client now, but the records pane is the reason cs2dom exists.
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';

import { emitScript } from './cs2_js_emit.js';

/**
 * The scripts an interface installs, read off its own blocks.
 *
 * A hook field is `on<something>` and its first entry is `i:<script id>`. Taken
 * from the RECORD rather than from a dependency graph so the answer is what
 * THIS interface installs; the closure beneath them arrives through the
 * emitter's own procs and hooks lists in lowerClosure.
 *
 * A negative id is the cache's "no script" and is not one: -1 dropped here is
 * -1 the decompiler is never asked about.
 */
export function hookScriptIds(record) {
    const ids = new Set();
    for( const block of record.blocks )
        for( const [key, entries] of block.fields )
        {
            if( !/^on[a-z]+$/.test(key) ) continue;
            for( const entry of entries )
            {
                const first = entry.value.split(',')[0];
                const match = /^i:(-?\d+)$/.exec(first.trim());
                if( match && Number(match[1]) >= 0 ) ids.add(Number(match[1]));
            }
        }
    return [...ids];
}

/**
 * Lower a set of root scripts and everything they reach.
 *
 * Breadth-first over procs and hooks, with `seen` guarding the cycle -- CS2
 * calls itself, and a script that gosubs a proc that gosubs it back is ordinary
 * rather than exceptional.
 *
 * A script that will not decompile or will not lower is RECORDED, not thrown:
 * one bad script in a closure of forty should cost that one script's source
 * pane, not the whole interface's.
 */
export function lowerClosure(state, roots) {
    const scripts = {};
    const cs2Parts = [];
    const errors = [];
    const seen = new Set();
    const queue = [...roots];

    while( queue.length )
    {
        const id = queue.shift();
        if( seen.has(id) ) continue;
        seen.add(id);

        const ast = syntaxTree(state, id);
        if( !ast ) { errors.push(`script ${id}: could not decompile`); continue; }
        try
        {
            const result = emitScript(ast);
            scripts[id] = result.code;
            cs2Parts.push(`// ${result.name} — ${id}`);
            for( const dependency of [...result.procs, ...result.hooks] )
                if( !seen.has(dependency) ) queue.push(dependency);
        }
        catch( error ) { errors.push(`script ${id}: ${error.message}`); }
    }
    return { scripts, cs2Source: cs2Parts.join('\n'), errors };
}

/** One script's syntax tree, from the C decompiler, cached for the process. */
export function syntaxTree(state, id) {
    if( state.asts.has(id) ) return state.asts.get(id);
    let tree = null;
    if( state.cs2 && state.cache && existsSync(state.cs2) )
    {
        try
        {
            const args = ['decompile', '--cache', state.cache, '--emit', 'ast-json', '--quiet'];
            if( state.revision ) args.push('--rev', state.revision);
            if( state.names ) args.push('--names', state.names);
            args.push(String(id));
            const stdout = execFileSync(state.cs2, args, { encoding: 'utf8' });
            /* The tool prints its summary to stderr and the document to stdout,
             * so the first `{` is the start of the tree. */
            const start = stdout.indexOf('{');
            if( start >= 0 ) tree = JSON.parse(stdout.slice(start));
        }
        catch { tree = null; }
    }
    state.asts.set(id, tree);
    return tree;
}
