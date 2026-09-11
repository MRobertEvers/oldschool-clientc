/*
 * The command line.
 *
 *   cs2dom build [--project DIR] [--dry-run] [--no-verify]
 *   cs2dom dev [--project DIR] [--cache DIR --rev NAME] [--port N] [--no-open]
      Watch ui/*.tsx, rebuild on save and show the result in a browser: the
      authored, imported content and Dat2 interfaces in the same live DOM/React
      preview, with host-state controls and the .if/.cs2 records. The C client is
      retained as a render oracle. Nothing is written to the content tree.

  cs2dom cachegen [--project DIR] [--out FILE]
 *   cs2dom check [--project DIR]
 *   cs2dom ops
 *
 * `build` verifies by default: every script it generates is handed to the real CS2
 * compiler before anything is written, so a tree never ends up holding source that
 * cannot bake. `--no-verify` exists for working without the built tool, and says so.
 */

import { writeFileSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, execFileSync } from 'node:child_process';

import { build, loadProject } from './build.js';
import { generate, DEFAULT_TABLES } from './cachegen.js';
import { compileScripts, findRepoRoot, CS2_TOOL, CS2_TOOL_BUILD } from './verify.js';
import { OPS } from './ops.js';
import { ELEMENTS, EVENTS } from './components.js';
import { Cs2domError } from './ir.js';

const HERE = dirname(fileURLToPath(import.meta.url));

export function main(argv) {
    const [command, ...rest] = argv;
    const flags = parseFlags(rest);

    switch( command ) {
        case 'build': return commandBuild(flags);
        case 'dev-canvas': case 'dev': case 'start': return commandDevCanvas(flags);
        case 'cachegen': return commandCacheGen(flags);
        case 'check': return commandCheck(flags);
        case 'ops': return commandOps();
        case undefined: case '-h': case '--help': return usage(0);
        default:
            process.stderr.write(`cs2dom: unknown command '${command}'\n`);
            return usage(1);
    }
}

function usage(code) {
    process.stdout.write(
        'cs2dom — compile React components into CS2 source and IF3 interface records\n\n' +
        '  cs2dom build [--project DIR] [--dry-run] [--no-verify]\n' +
        '      Render every ui/*.tsx, write interfaces/<name>.if, its .compack and\n' +
        '      scripts/<name>.cs2 into the content tree, and allocate ids in the pack\n' +
        '      files. Bake afterwards with: make -C src torirsserver-cache\n\n' +
        '  cs2dom dev [--project DIR] [--cache DIR --rev NAME] [--port N] [--no-open]\n' +
        '      Open an interface in the browser and run it in the REAL client —\n' +
        '      build-web/torirs.wasm, drawing with toridraw and hit-testing with its\n' +
        '      own code. Three panes: the client, the state to drive it with, and the\n' +
        '      .if / .compack / .cs2 / JavaScript it compiles to.\n\n' +
        '      Needs the client built first: make -C src web\n' +
        '      --cache opens a Dat2 cache directly; --rev names its cachepack profile.\n\n' +
        '  cs2dom cachegen [--project DIR] [--out FILE]\n' +
        '      Regenerate cache.gen.ts — sprite, font, varp, varbit and interface ids\n' +
        '      as typed constants, read from the content tree.\n\n' +
        '  cs2dom check [--project DIR]\n' +
        '      Type-check the components against the runtime types.\n\n' +
        '  cs2dom ops\n' +
        '      Print the operation vocabulary: every prop that can change at runtime\n' +
        '      and the CS2 command it changes with.\n');
    return code;
}

function parseFlags(args) {
    const flags = { project: process.cwd() };
    for( let i = 0; i < args.length; i++ ) {
        const arg = args[i];
        if( arg === '--project' ) flags.project = args[++i];
        else if( arg === '--out' ) flags.out = args[++i];
        else if( arg === '--cache' ) flags.cache = args[++i];
        else if( arg === '--rev' ) flags.rev = args[++i];
        else if( arg === '--dry-run' ) flags.dryRun = true;
        else if( arg === '--no-verify' ) flags.noVerify = true;
        else if( arg === '--quiet' ) flags.quiet = true;
        else if( arg === '--port' ) flags.port = Number(args[++i]);
        else if( arg === '--no-open' ) flags.noOpen = true;
        else throw new Cs2domError(`unknown option '${arg}'`);
    }
    return flags;
}

function commandBuild(flags) {
    const project = loadProject(flags.project);
    const say = flags.quiet ? () => {} : (text) => process.stdout.write(text + '\n');

    /* Render and emit first, write second: a build that cannot compile its own
     * output should leave the tree exactly as it found it. */
    const planned = build(project, { dryRun: true });

    if( !flags.noVerify ) {
        const repoRoot = findRepoRoot(project.content) || findRepoRoot(HERE);
        const scripts = planned.results.flatMap((r) => r.scripts);
        const verified = compileScripts(scripts, { repoRoot });

        if( verified.missingTool ) {
            process.stderr.write(
                `cs2dom: ${CS2_TOOL} is not built, so the generated CS2 was not checked.\n` +
                `        Build it with \`${CS2_TOOL_BUILD}\`, or pass --no-verify.\n`);
            return 1;
        }
        if( !verified.ok ) {
            process.stderr.write(`cs2dom: the generated CS2 does not compile\n\n`);
            for( const failure of verified.failures ) {
                process.stderr.write(`  ${failure.name} (script ${failure.id}): ${failure.message}\n`);
                const lines = failure.source.split('\n');
                const at = /line (\d+)/.exec(failure.message);
                if( at ) {
                    const n = Number(at[1]);
                    for( let i = Math.max(0, n - 2); i < Math.min(lines.length, n + 1); i++ )
                        process.stderr.write(`      ${String(i + 1).padStart(3)} | ${lines[i]}\n`);
                }
                process.stderr.write('\n');
            }
            return 1;
        }
        say(`verified ${verified.compiled} script${verified.compiled === 1 ? '' : 's'} against ${CS2_TOOL}`);
    }

    if( flags.dryRun ) {
        report(say, planned, project, true);
        return 0;
    }

    const result = build(project, { dryRun: false });
    report(say, result, project, false);
    for( const warning of result.warnings )
        process.stderr.write(`cs2dom: warning: ${warning}\n`);
    return 0;
}

function report(say, result, project, dryRun) {
    for( const built of result.results ) {
        const scripts = built.scripts.length;
        say(`${dryRun ? 'would build' : 'built'} ${built.name} — interface ${built.interfaceId}, ` +
            `${built.componentCount} component${built.componentCount === 1 ? '' : 's'}, ` +
            `${scripts} script${scripts === 1 ? '' : 's'}`);
    }
    if( !dryRun ) {
        say(`wrote ${result.written.length} files into ${project.content}`);
        for( const path of result.ledgerWrites ) say(`updated ${path}`);
        say('bake it in with: make -C src torirsserver-cache');
    }
}

/*
 * The port is taken: say by WHAT, and offer to end it.
 *
 * It is nearly always this same server left running from an earlier session,
 * and killing it is what the user was going to do by hand anyway -- but it
 * may also be something else entirely, so the process is named before the
 * question is asked and nothing is killed without an answer. Without a
 * terminal there is nobody to ask: the condition is reported and the run
 * stops, which is what a script or a CI job needs.
 */
async function offerToKillPortHolder(port) {
    const holder = portHolder(port);
    if( !holder )
    {
        process.stderr.write(`cs2dom: port ${port} is in use and the holder could not be identified\n`);
        return false;
    }

    process.stderr.write(`cs2dom: port ${port} is held by pid ${holder.pid} — ${holder.command}\n`);
    if( !process.stdin.isTTY )
    {
        process.stderr.write(`cs2dom: not a terminal, so nothing was killed; use --port to pick another\n`);
        return false;
    }
    if( !await confirm(`kill ${holder.pid} and take the port? [y/N] `) )
        return false;

    return killAndWait(holder.pid, port);
}

/** The pid and command line listening on `port`, or null. */
function portHolder(port) {
    try
    {
        const pid = execFileSync('lsof', ['-ti', `tcp:${port}`, '-sTCP:LISTEN'], { encoding: 'utf8' })
            .split('\n')[0].trim();
        if( !pid ) return null;
        const command = execFileSync('ps', ['-p', pid, '-o', 'command='], { encoding: 'utf8' }).trim();
        return { pid: Number(pid), command: command || '(unknown)' };
    }
    catch { return null; }
}

/*
 * One yes/no on the terminal.
 *
 * `end` answers NO. A terminal whose input has already closed -- a pipe that
 * delivered its line before this ran, a job put in the background -- never
 * delivers `data`, and waiting on it alone hangs the start with a prompt on
 * screen and nothing able to answer it.
 */
function confirm(question) {
    return new Promise((answered) => {
        process.stdout.write(question);
        process.stdin.setEncoding('utf8');
        const done = (value) => {
            process.stdin.off('data', onData);
            process.stdin.off('end', onEnd);
            process.stdin.pause();
            answered(value);
        };
        const onData = (line) => done(/^\s*y(es)?\s*$/i.test(line));
        const onEnd = () => { process.stdout.write('\n'); done(false); };
        process.stdin.once('data', onData);
        process.stdin.once('end', onEnd);
        process.stdin.resume();
    });
}

/*
 * TERM, then wait for the port to actually free.
 *
 * Relisting immediately loses the race: the process is gone before the
 * kernel has released the socket, and the retry fails with the same
 * EADDRINUSE it was meant to clear. KILL is the fallback for a process that
 * ignores TERM, and a port still held after that is reported rather than
 * retried into another crash.
 */
async function killAndWait(pid, port) {
    try { process.kill(pid, 'SIGTERM'); }
    catch { return true; /* already gone */ }

    for( let attempt = 0; attempt < 50; attempt++ )
    {
        await new Promise((tick) => setTimeout(tick, 100));
        if( !portHolder(port) ) return true;
        if( attempt === 20 )
        {
            try { process.kill(pid, 'SIGKILL'); }
            catch { /* it exited between the check and the signal */ }
        }
    }
    process.stderr.write(`cs2dom: pid ${pid} would not release port ${port}\n`);
    return false;
}

/**
 * The dev server.
 *
 * The preview is the official client compiled to WebAssembly; this server hands
 * the browser that build, an io_server to answer its cache reads, and the two
 * things the client has no opinion about — which interface to open, and what
 * that interface compiles to.
 */
function commandDevCanvas(flags) {
    const project = loadProject(flags.project);
    if( flags.cache ) project.cache = resolve(flags.cache);
    if( flags.rev ) project.revision = flags.rev;
    /* Imported here rather than at the top: a build should not pay for the
     * server, and the server should not pay for the compiler. */
    return import('./dev_client.js').then(({ serveClient }) => {
        serveClient({
            root: resolve(fileURLToPath(new URL('..', import.meta.url))),
            contentDir: project.content ?? null,
            cache: project.cache ?? null,
            revision: project.revision ?? null,
            names: project.cs2Names ?? null,
            port: flags.port || 8099,
            onAddressInUse: offerToKillPortHolder,
            onListen: (address) => {
                process.stdout.write(`cs2dom: ${address}\n`);
                if( !flags.noOpen ) openBrowser(address);
            },
        });
        /* The server owns the process from here; there is no exit code. */
        return new Promise(() => {});
    });
}

/** Open the page, or carry on: a server that cannot is still a server. */
function openBrowser(address) {
    const command = process.platform === 'darwin' ? 'open'
        : process.platform === 'win32' ? 'start' : 'xdg-open';
    try { spawn(command, [address], { stdio: 'ignore', detached: true }).unref(); }
    catch { /* nothing to do about it */ }
}

function commandCacheGen(flags) {
    const project = loadProject(flags.project);
    const out = flags.out ? resolve(flags.out) : join(project.sources, 'cache.gen.ts');
    writeFileSync(out, generate(project.content, project.cachegen || DEFAULT_TABLES));
    process.stdout.write(`wrote ${out}\n`);
    return 0;
}

async function commandCheck(flags) {
    const project = loadProject(flags.project);
    const ts = (await import('typescript')).default;

    const config = {
        target: ts.ScriptTarget.ES2022,
        module: ts.ModuleKind.CommonJS,
        jsx: ts.JsxEmit.React,
        jsxFactory: '__jsx',
        strict: true,
        noEmit: true,
        moduleResolution: ts.ModuleResolutionKind.Node10,
        baseUrl: project.root,
        paths: { cs2dom: [join(HERE, '..', 'types', 'cs2dom.d.ts')] },
        types: [],
    };

    const files = [];
    const { readdirSync } = await import('node:fs');
    for( const file of readdirSync(project.sources) )
        if( file.endsWith('.tsx') || file.endsWith('.ts') ) files.push(join(project.sources, file));

    const program = ts.createProgram(files, config);
    const diagnostics = ts.getPreEmitDiagnostics(program);
    if( diagnostics.length === 0 ) {
        process.stdout.write(`checked ${files.length} files, no errors\n`);
        return 0;
    }
    process.stderr.write(ts.formatDiagnosticsWithColorAndContext(diagnostics, {
        getCanonicalFileName: (f) => f,
        getCurrentDirectory: () => project.root,
        getNewLine: () => '\n',
    }));
    return 1;
}

function commandOps() {
    process.stdout.write('element   prop              command                 note\n');
    for( const [element, definition] of Object.entries(ELEMENTS) ) {
        for( const [prop, schema] of Object.entries(definition.props) ) {
            const command = schema.op || '—';
            const note = schema.op ? '' : 'fixed at build time';
            process.stdout.write(
                `${element.padEnd(9)} ${prop.padEnd(17)} ${command.padEnd(23)} ${note}\n`);
        }
    }
    process.stdout.write('\nevents: ' + Object.keys(EVENTS).join(', ') + '\n');
    process.stdout.write('commands with a recorded argument order: ' + Object.keys(OPS).length + '\n');
    return 0;
}
