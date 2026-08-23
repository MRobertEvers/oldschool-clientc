/*
 * Running a .tsx to find out what it draws.
 *
 * A component tree is discovered by rendering, not by reading the source: props,
 * composition, helper functions, loops over real data and imports of the generated
 * cache bindings all behave the way they do in any other TypeScript, and what falls
 * out is a tree of plain objects. That is the whole reason the compiler executes
 * rather than pattern-matches — a component is a function, and the honest way to
 * learn what a function returns is to call it.
 *
 * Modules run in a `vm` context with nothing ambient: no fs, no process, no network.
 * Not a security boundary — the build runs code the author wrote — but a build that
 * cannot reach the filesystem is a build whose output depends only on its inputs.
 */

import { createRequire } from 'node:module';
import { readFileSync, existsSync } from 'node:fs';
import { dirname, resolve, extname } from 'node:path';
import vm from 'node:vm';

import { compileSource } from './transform.js';
import * as runtime from './runtime.js';
import * as expr from './expr.js';

const EXTENSIONS = ['', '.ts', '.tsx', '.js'];

/** Everything a rewritten module calls, plus a console for author debugging. */
function makeGlobals(log) {
    return {
        __jsx: runtime.jsx,
        __fragment: runtime.fragment,
        __op: expr.__op,
        __logic: expr.__logic,
        __not: expr.__not,
        __cond: expr.__cond,
        __tmpl: expr.template,
        console: { log: (...args) => log(args.map(String).join(' ')), warn: () => {}, error: () => {} },
        Math, JSON, Object, Array, String, Number, Boolean, Error, Map, Set, Symbol,
    };
}

export class ModuleGraph {
    constructor(options = {}) {
        this.cache = new Map();
        this.log = options.log || (() => {});
        this.context = vm.createContext(makeGlobals(this.log));
        this.loaded = [];
    }

    /** Load a module by absolute path, executing it once and caching the exports. */
    load(path) {
        const resolved = resolveFile(path);
        if( this.cache.has(resolved) )
            return this.cache.get(resolved);

        const source = readFileSync(resolved, 'utf8');
        const js = extname(resolved) === '.js' ? source : compileSource(source, resolved);

        const module = { exports: {} };
        /* Seed the cache before running, so a cycle resolves to the partial module
         * rather than looping — the same contract Node gives CommonJS. */
        this.cache.set(resolved, module.exports);
        this.loaded.push(resolved);

        const require = (request) => this.resolveRequest(request, resolved);
        const wrapper = vm.runInContext(
            `(function (exports, require, module, __filename, __dirname) {\n${js}\n})`,
            this.context,
            { filename: resolved });

        wrapper(module.exports, require, module, resolved, dirname(resolved));
        this.cache.set(resolved, module.exports);
        return module.exports;
    }

    resolveRequest(request, fromFile) {
        if( request === 'cs2dom' || request.startsWith('@cs2dom/') )
            return runtime;
        if( request.startsWith('.') || request.startsWith('/') )
            return this.load(resolve(dirname(fromFile), request));
        throw new Error(
            `${fromFile}: cannot import '${request}' — a component may import 'cs2dom' ` +
            `and its own files, nothing else`);
    }
}

function resolveFile(path) {
    for( const ext of EXTENSIONS ) {
        const candidate = path + ext;
        if( existsSync(candidate) && extname(candidate) )
            return candidate;
    }
    throw new Error(`no such module: ${path}`);
}

/**
 * Render one interface module.
 *
 * The default export is called with no props — an interface is a root, so anything
 * it needs comes from state rather than from a parent. What comes back is the node
 * tree plus the state every hook declared, in declaration order.
 */
export function renderModule(graph, path, context) {
    const exports = graph.load(path);
    const component = exports.default || exports.Interface;
    if( typeof component !== 'function' )
        throw new Error(`${path}: expected a default-exported component function`);

    const session = runtime.beginRender(context);
    let tree;
    try {
        tree = component({});
    } finally {
        runtime.endRender();
    }

    if( Array.isArray(tree) )
        throw new Error(`${path}: an interface must return one root component, not a fragment`);
    if( !runtime.isNode(tree) )
        throw new Error(`${path}: an interface must return a component`);

    return { tree, states: session.states, meta: exports.meta || {} };
}
