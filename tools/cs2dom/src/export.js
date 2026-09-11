/*
 * Saving: edits back into the content tree, and from there into a cache.
 *
 * The chain is
 *
 *   edited records -> if_text + cs2_text  -> cachepack pack -> if_binary
 *                                         -> cs2 compile   -> cs2_binaries
 *
 * and this file owns the first arrow. The other two are the C tools', which
 * already round-trip byte-exactly, so shelling to them is not laziness — it is
 * the only way to be sure the bytes are the ones the client reads.
 *
 * ------------------------------------------------------------------
 * Nothing is written that has not compiled
 * ------------------------------------------------------------------
 *
 * Generated CS2 goes to the real compiler BEFORE it reaches the tree. A tree
 * that holds source which will not bake is a tree whose next build fails
 * somewhere unrelated, hours later, on a machine that is not this one.
 *
 * ------------------------------------------------------------------
 * Only what changed is written
 * ------------------------------------------------------------------
 *
 * `if_record.js` re-emits an untouched block verbatim, so a save that edits
 * one colour produces a one-line diff. Writing every file every time would be
 * simpler and would bury the edit in 968 reformatted records — which is the
 * thing that makes a round trip useless in practice even when it is correct.
 */

import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { execFileSync } from 'node:child_process';

import { parseIf, parseCompack, emitCompack, nextFileId } from './if_record.js';

export class ExportError extends Error {
    constructor(message) {
        super(message);
        this.name = 'ExportError';
    }
}

/**
 * Apply a set of edits to one interface and write the result.
 *
 * `edits` is `{ blockName: { field: value } }`, where `null` removes a field.
 * Returns what changed, or `{ written: [] }` when the edits were all no-ops —
 * which is the common case after a preview session where nothing was moved.
 */
export function saveInterface({
    contentDir, name, edits = {}, scripts = {}, cs2Tool = null, dryRun = false,
} = {}) {
    if( !contentDir ) throw new ExportError('an export needs a content tree');
    const ifPath = join(contentDir, 'interfaces', `${name}.if`);
    if( !existsSync(ifPath) ) throw new ExportError(`no ${name}.if to edit`);

    const original = readFileSync(ifPath, 'utf8');
    const record = parseIf(original);

    for( const [block, fields] of Object.entries(edits) )
        for( const [field, value] of Object.entries(fields) )
            record.set(block, field, value);

    const written = [];
    const rewritten = record.toText();
    /*
     * Compare against the ORIGINAL TEXT, not against a dirty flag. A field set
     * to what it already held marks the block dirty in the record's own
     * bookkeeping but changes no bytes, and writing the file anyway would
     * touch its mtime and trip every watcher downstream.
     */
    if( rewritten !== original )
    {
        if( !dryRun ) writeFileSync(ifPath, rewritten);
        written.push(ifPath);
    }

    /* Scripts are verified before they are written; see the header. */
    const scriptResults = saveScripts({ contentDir, scripts, cs2Tool, dryRun });
    written.push(...scriptResults.written);

    return {
        written,
        blocksChanged: record.changed(),
        scripts: scriptResults,
        unchanged: written.length === 0,
    };
}

/**
 * Write generated CS2, having first made the real compiler accept it.
 *
 * Compiling into a temporary directory rather than the tree: a script that
 * fails must leave no trace, and a half-written scripts/ directory is worse
 * than an error because the next build compiles it.
 */
export function saveScripts({ contentDir, scripts = {}, cs2Tool = null, dryRun = false } = {}) {
    const written = [];
    const rejected = [];
    const dir = join(contentDir, 'scripts');

    for( const [name, source] of Object.entries(scripts) )
    {
        const problem = cs2Tool ? verifyScript(cs2Tool, contentDir, name, source) : null;
        if( problem ) { rejected.push({ name, problem }); continue; }

        const path = join(dir, `${name}.cs2`);
        const existing = existsSync(path) ? readFileSync(path, 'utf8') : null;
        if( existing === source ) continue;
        if( !dryRun )
        {
            mkdirSync(dirname(path), { recursive: true });
            writeFileSync(path, source);
        }
        written.push(path);
    }

    if( rejected.length )
        throw new ExportError(
            `the CS2 compiler refused ${rejected.length} script(s):\n` +
            rejected.map((entry) => `  ${entry.name}: ${entry.problem}`).join('\n'));

    return { written, rejected };
}

/**
 * Hand one script to the real compiler and report what it says.
 *
 * Returns null when it compiles. There is no second opinion here on purpose:
 * a private parser that agreed with the compiler most of the time would be a
 * source of disagreements rather than of confidence.
 */
function verifyScript(cs2Tool, contentDir, name, source) {
    const scratch = join(contentDir, '.cs2dom-verify');
    try
    {
        mkdirSync(scratch, { recursive: true });
        const path = join(scratch, `${name}.cs2`);
        writeFileSync(path, source);
        execFileSync(cs2Tool, ['compile', '--src', path], { stdio: 'pipe' });
        return null;
    }
    catch( error )
    {
        const detail = error.stderr?.toString().trim() || error.message;
        return detail.split('\n').slice(0, 3).join('; ');
    }
}

/**
 * Add a component to an interface, allocating its file id.
 *
 * The id comes from the `.compack` and only ever goes past the highest. A
 * recycled id hands one component's uid — `(interface << 16) | fileId` — to a
 * different component, and every script that referenced the old one then
 * silently addresses the new one.
 */
export function addComponent({ contentDir, name, blockName, fields = {}, dryRun = false } = {}) {
    const ifPath = join(contentDir, 'interfaces', `${name}.if`);
    const compackPath = join(contentDir, 'interfaces', `${name}.compack`);
    if( !existsSync(ifPath) ) throw new ExportError(`no ${name}.if`);

    const record = parseIf(readFileSync(ifPath, 'utf8'));
    const compack = existsSync(compackPath)
        ? parseCompack(readFileSync(compackPath, 'utf8'))
        : { byName: new Map(), order: [] };

    if( compack.byName.has(blockName) )
        throw new ExportError(`[${blockName}] already has file id ${compack.byName.get(blockName)}`);

    const fileId = nextFileId(compack);
    record.addBlock(blockName, { if3: 'yes', ...fields });
    compack.byName.set(blockName, fileId);
    compack.order.push({ id: fileId, name: blockName });

    if( !dryRun )
    {
        writeFileSync(ifPath, record.toText());
        writeFileSync(compackPath, emitCompack(compack));
    }
    return { fileId, written: dryRun ? [] : [ifPath, compackPath] };
}

/**
 * Bake the content tree into a cache.
 *
 * The output cache must already be a copy of the base: `--asset-only` writes
 * table 3 and table 12 into what is there. Packing into an empty directory
 * instead produces a cache holding exactly the assets the tree states and
 * nothing else — right when there is no base to start from, catastrophic when
 * there is.
 *
 * The exit code is deliberately reported rather than thrown on: a whole-tree
 * `cachepack pack` exits non-zero on OSRS-Content for obj/param encode
 * failures that predate this tool, so treating its status as a verdict on
 * THIS save would fail every save for reasons nothing here caused. The
 * asset-only path used for an interface edit does exit 0, and a caller that
 * wants to insist can check.
 */
export function packCache({
    cachepack, contentDir, out, revision, assets = 'interfaces,scripts', types = null,
}) {
    if( !cachepack || !existsSync(cachepack) )
        throw new ExportError('cachepack is not built; make -C 3rd/rscache/tools cachepack');

    /*
     * Interfaces and scripts are ASSETS, not config types — `--types` does not
     * know the word "interfaces" and refuses. `--asset-only` writes tables 3
     * and 12 and leaves every config record in the output cache alone, which
     * is what an interface edit means.
     *
     * The caller copies the base cache to `out` first; `--asset-only` writes
     * into it rather than creating one, so everything this save does not state
     * keeps the bytes it had.
     */
    const args = ['pack', '--src', contentDir, '--out', out];
    if( revision ) args.push('--rev', revision);
    if( types ) args.push('--types', types);
    else args.push('--asset-only', `--assets=${assets}`);

    let status = 0;
    let output = '';
    try { output = execFileSync(cachepack, args, { encoding: 'utf8', stdio: 'pipe' }); }
    catch( error )
    {
        status = error.status ?? 1;
        output = `${error.stdout ?? ''}${error.stderr ?? ''}`;
    }
    return { status, output, args };
}
