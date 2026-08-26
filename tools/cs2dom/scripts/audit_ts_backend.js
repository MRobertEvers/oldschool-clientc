#!/usr/bin/env node

/*
 * Audit real cache interface programs against the fail-closed TypeScript CS2
 * backend gate. Stdout is always one deterministic JSON document; optional
 * progress goes to stderr so the output can be checked into CI artifacts.
 */

import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { compileInterfaceProgram } from '../src/bytecode.js';
import { contentInterfaceCatalog, openContentInterface } from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';
import { decodeCS2BytecodeProgram } from '../web/cs2_bytecode_decoder.js';
import {
    CS2_BACKEND_COVERAGE_SCHEMA,
    aggregateCS2BackendCoverage,
    auditCS2BackendRegistry,
} from '../web/cs2_backend_coverage.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const REPO = resolve(CS2DOM, '../..');
const DEFAULT_FILTERS = Object.freeze(['bankmain', 'pirate_combilock', 'ca_tasks']);

const options = parseArguments(process.argv.slice(2));
if( options.help ) {
    process.stdout.write(help());
    process.exit(0);
}

try {
    const report = runAudit(options);
    process.stdout.write(`${JSON.stringify(report, null, options.compact ? 0 : 2)}\n`);
    if( report.failures.length ) process.exitCode = 1;
} catch( error ) {
    process.stderr.write(`cs2dom TS backend audit: ${error?.message || String(error)}\n`);
    process.exitCode = 1;
}

function runAudit(options) {
    const baseProject = {
        cache: options.cache,
        content: options.content,
        unpackedContent: options.content,
        revision: options.revision,
    };
    let project = baseProject;
    if( options.source === 'dat2' || existsSync(join(options.cache, 'main_file_cache.dat2')) ) {
        project = prepareDat2Project(baseProject, {
            log: options.verbose ? (message) => process.stderr.write(`${message}\n`) : () => {},
        });
    }
    if( options.source === 'dat2' && !project.dat2Content )
        throw new Error(`Dat2 source is unavailable at ${options.cache}`);

    const contentRoot = options.source === 'dat2' ? project.dat2Content : options.content;
    if( !existsSync(contentRoot) )
        throw new Error(`content source is unavailable at ${contentRoot}`);
    const catalog = contentInterfaceCatalog(contentRoot, { source: options.source })
        .sort(compareInterface);
    const selected = selectInterfaces(catalog, options.filters, options.all);
    if( selected.length === 0 )
        throw new Error(`no interfaces matched ${options.filters.join(', ') || '(empty filter)'}`);

    const interfaces = [];
    const failures = [];
    for( const record of selected ) {
        if( options.verbose ) process.stderr.write(`auditing ${record.name}\n`);
        try {
            const opened = openContentInterface(contentRoot, record.name, {
                source: options.source,
            });
            const program = compileInterfaceProgram(project, opened);
            if( !program.available ) throw new Error(
                (program.warnings || []).join('; ') || 'interface program is unavailable');
            const registry = decodeCS2BytecodeProgram(program);
            const coverage = auditCS2BackendRegistry(registry);
            interfaces.push(Object.freeze({
                interfaceId: record.interfaceId,
                name: record.name,
                source: options.source,
                program: Object.freeze({
                    entryScriptIds: Object.freeze([...registry.entryScriptIds]),
                    warningCount: (program.warnings || []).length,
                    fallbackCount: (program.fallbacks || []).length,
                }),
                coverage,
            }));
        } catch( error ) {
            failures.push(Object.freeze({
                interfaceId: record.interfaceId,
                name: record.name,
                code: typeof error?.code === 'string' ? error.code : null,
                message: error?.message || String(error),
            }));
        }
    }

    const successfulCoverage = interfaces.map((item) => item.coverage);
    return Object.freeze({
        schema: CS2_BACKEND_COVERAGE_SCHEMA,
        source: options.source,
        revision: options.revision,
        filters: Object.freeze([...options.filters]),
        catalogInterfaceCount: catalog.length,
        selectedInterfaceCount: selected.length,
        decodedInterfaceCount: interfaces.length,
        failedInterfaceCount: failures.length,
        totals: aggregateCS2BackendCoverage(successfulCoverage),
        interfaces: Object.freeze(interfaces),
        failures: Object.freeze(failures),
    });
}

function selectInterfaces(catalog, filters, all) {
    if( all ) return catalog;
    const selected = new Map();
    for( const rawFilter of filters ) {
        const filter = rawFilter.toLowerCase();
        const exact = catalog.filter((record) => record.name.toLowerCase() === filter);
        const matches = exact.length ? exact : catalog.filter((record) =>
            record.name.toLowerCase().includes(filter));
        for( const record of matches ) selected.set(record.interfaceId, record);
    }
    return [...selected.values()].sort(compareInterface);
}

function compareInterface(left, right) {
    return left.interfaceId - right.interfaceId || left.name.localeCompare(right.name);
}

function parseArguments(args) {
    const options = {
        source: 'dat2',
        content: join(REPO, 'OSRS-Content', 'osrs239-content'),
        cache: join(REPO, 'cache.osrs239'),
        revision: 'osrs239',
        filters: [],
        all: false,
        compact: false,
        verbose: false,
        help: false,
    };
    for( let index = 0; index < args.length; index++ ) {
        const argument = args[index];
        if( argument === '--source' ) options.source = requiredValue(args, ++index, argument);
        else if( argument === '--content' )
            options.content = resolve(requiredValue(args, ++index, argument));
        else if( argument === '--cache' )
            options.cache = resolve(requiredValue(args, ++index, argument));
        else if( argument === '--revision' || argument === '--rev' )
            options.revision = requiredValue(args, ++index, argument);
        else if( argument === '--filter' ) options.filters.push(
            ...requiredValue(args, ++index, argument).split(',').map((value) => value.trim())
                .filter(Boolean));
        else if( argument === '--all' ) options.all = true;
        else if( argument === '--compact' ) options.compact = true;
        else if( argument === '--verbose' ) options.verbose = true;
        else if( argument === '--help' || argument === '-h' ) options.help = true;
        else throw new Error(`unknown argument ${argument}`);
    }
    if( options.source !== 'content' && options.source !== 'dat2' )
        throw new Error(`--source must be content or dat2, got ${options.source}`);
    if( options.all && options.filters.length )
        throw new Error('--all and --filter are mutually exclusive');
    if( !options.all && options.filters.length === 0 ) options.filters.push(...DEFAULT_FILTERS);
    return options;
}

function requiredValue(args, index, option) {
    if( index >= args.length || args[index].startsWith('--'))
        throw new Error(`${option} requires a value`);
    return args[index];
}

function help() {
    return `Usage: node scripts/audit_ts_backend.js [options]\n\n` +
        `Audits exact cache clientscript closures. The default filter is:\n` +
        `  ${DEFAULT_FILTERS.join(',')}\n\n` +
        `Options:\n` +
        `  --filter NAME[,NAME]  Exact name, or substring if no exact match\n` +
        `  --all                Audit every interface (explicitly slow)\n` +
        `  --source TYPE        dat2 (default) or content\n` +
        `  --cache PATH         Dat2 cache directory\n` +
        `  --content PATH       OSRS-Content directory\n` +
        `  --revision NAME      cachepack revision (default osrs239)\n` +
        `  --compact            Emit compact JSON\n` +
        `  --verbose            Progress on stderr; stdout stays JSON\n`;
}
