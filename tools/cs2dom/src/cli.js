/*
 * The command line.
 *
 *   cs2dom build [--project DIR] [--dry-run] [--no-verify]
 *   cs2dom cachegen [--project DIR] [--out FILE]
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
        else if( arg === '--dry-run' ) flags.dryRun = true;
        else if( arg === '--no-verify' ) flags.noVerify = true;
        else if( arg === '--quiet' ) flags.quiet = true;
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
