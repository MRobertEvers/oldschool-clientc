/*
 * Opening interfaces that already live in an unpacked content tree.
 *
 * Authored cs2dom components travel TSX -> rendered nodes -> IR.  Cache interfaces
 * travel the other way here: .if + .compack -> the same IR-shaped component list.
 * Keeping the last step shared is important: imported and authored interfaces use
 * exactly the same layout and preview code, rather than two almost-equal renderers.
 *
 * The generated TSX is a read-only, React-style view.  Static fields round-trip
 * through it; raw hook bindings and fields outside cs2dom's vocabulary are kept as
 * comments because pretending they are ordinary callbacks would change the cache's
 * sentinel-argument semantics.
 */

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { ELEMENTS, EVENTS, IF_TYPE } from './components.js';
import { runCacheHooks } from './cache_runtime.js';
import { packName } from './pack.js';

const KIND_BY_TYPE = new Map([
    [IF_TYPE.layer, 'Layer'],
    [IF_TYPE.rectangle, 'Rect'],
    [IF_TYPE.text, 'Text'],
    [IF_TYPE.graphic, 'Graphic'],
    [IF_TYPE.model, 'Model'],
    [IF_TYPE.tooltip, 'Text'],
    [IF_TYPE.line, 'Line'],
]);

const EVENT_BY_FIELD = new Map(
    Object.entries(EVENTS).map(([prop, definition]) => [definition.field, prop]));
/** Return the content interfaces that have readable .if records. */
export function contentInterfaceCatalog(contentDir, options = {}) {
    const source = options.source || 'content';
    const interfaces = readPack(join(contentDir, 'pack', '3_interfaces.pack'));
    return [...interfaces]
        .filter(([, name]) => existsSync(join(contentDir, 'interfaces', `${name}.if`)))
        .map(([interfaceId, name]) => ({
            key: `${source}:${name}`,
            name,
            label: `${name} · ${source === 'dat2' ? 'Dat2' : 'cache'} ${interfaceId}`,
            interfaceId,
            source,
        }));
}

/**
 * Decompile one content interface into the IR shape consumed by preview.js.
 * No content files are changed.
 */
export function openContentInterface(contentDir, name, options = {}) {
    const source = options.source || 'content';
    const interfacePath = join(contentDir, 'interfaces', `${name}.if`);
    const compackPath = join(contentDir, 'interfaces', `${name}.compack`);
    if( !existsSync(interfacePath) )
        throw new Error(`no content interface named '${name}' (${interfacePath})`);

    const interfaceText = readFileSync(interfacePath, 'utf8');
    const compackText = existsSync(compackPath) ? readFileSync(compackPath, 'utf8') : '';
    const interfaceId = idForName(join(contentDir, 'pack', '3_interfaces.pack'), name);
    if( interfaceId === null )
        throw new Error(`${name}.if exists, but ${name} is absent from pack/3_interfaces.pack`);

    const fileIds = compackByName(compackText);
    const blocks = parseBlocks(interfaceText);
    const warnings = [];
    const components = blocks.map((block, index) =>
        importComponent(block, fileIds.get(block.name) ?? index, interfaceId, warnings));
    const ir = { name, interfaceId, components, scripts: [], states: [] };
    const scripts = linkedScripts(contentDir, components, warnings);

    return {
        name,
        interfaceId,
        file: interfacePath,
        source,
        contentDir,
        interfaceText,
        compackText,
        scripts,
        reactSource: emitReactView(ir),
        ir,
        warnings,
        componentCount: components.length,
    };
}

function importComponent(block, fileId, interfaceId, warnings) {
    const type = integer(block.fields.type, 0);
    const kind = KIND_BY_TYPE.get(type) || `Unknown${type}`;
    const definition = ELEMENTS[kind];
    const staticProps = {};
    const authoredProps = new Set();
    const knownFields = new Set(['if3', 'type', 'layer']);

    if( definition ) {
        for( const [prop, schema] of Object.entries(definition.props) ) {
            staticProps[prop] = schema.default;
            knownFields.add(schema.field);
            if( block.fields[schema.field] !== undefined ) {
                staticProps[prop] = fieldValue(block.fields[schema.field], schema.type);
                authoredProps.add(prop);
            }
        }
    }

    const ops = [];
    for( let index = 1; index <= 10; index++ ) {
        const field = `op${index}`;
        if( block.fields[field] !== undefined ) {
            ops.push({ index, text: block.fields[field] });
            knownFields.add(field);
        }
    }

    const hooks = {};
    const events = {};
    const scriptBindings = [];
    for( const [field, raw] of Object.entries(block.fields) ) {
        if( !field.startsWith('on') ) continue;
        knownFields.add(field);
        const binding = parseHook(raw);
        hooks[field] = { script: { id: binding.scriptId }, args: binding.args };
        scriptBindings.push({ field, ...binding });
        const event = EVENT_BY_FIELD.get(field);
        if( event ) events[event] = binding;
    }

    const triggers = {};
    for( const field of ['varptriggers', 'invtriggers', 'stattriggers'] ) {
        if( block.fields[field] === undefined ) continue;
        knownFields.add(field);
        triggers[field] = numberList(block.fields[field]);
    }

    const rawFields = {};
    for( const [field, value] of Object.entries(block.fields) )
        if( !knownFields.has(field) ) rawFields[field] = value;

    /* Client-code models (most importantly the local player's appearance) do
     * not carry a static model id. Keep the selector available to the preview
     * even though it is not an authorable cs2dom prop. */
    if( kind === 'Model' && block.fields.clientcode !== undefined )
        staticProps.clientCode = integer(block.fields.clientcode, -1);

    if( !definition )
        warnings.push(`${block.name}: interface component type ${type} is not in cs2dom's element vocabulary`);

    return {
        fileId,
        name: block.name,
        kind,
        type,
        /* GETTARGETMASK has different cache semantics for the two widget
         * families.  IF3 stores its six target bits inside the raw events word
         * at bits 11..16; IF1's exported clickmask is already the decoded
         * target mask.  Keep the decode family on the IR instead of throwing
         * away the known `if3` field, so the live HOST can answer exactly as
         * UITree's native builder does. */
        if3: fieldValue(block.fields.if3 ?? 'yes', 'boolean'),
        layer: parentFileId(block.fields.layer, interfaceId),
        props: { ...staticProps },
        static: staticProps,
        authoredProps,
        dynamic: [],
        ops,
        events,
        hooks,
        triggers,
        dependencies: [],
        scriptBindings,
        rawFields,
        legacyTooltip: type === IF_TYPE.tooltip,
    };
}

/** Apply source-form cache hooks to a freshly imported IR. */
export function executeContentHooks(result, state = {}) {
    if( !result?.contentDir ) return { dependencies: new Map(), warnings: [] };
    const execution = runCacheHooks(result.ir, sourceRuntimeOptions(result.contentDir, result.scripts),
        state, result.warnings);
    const seen = new Set(result.scripts.map((script) => script.name));
    for( const script of execution.scripts ) {
        if( seen.has(script.name) ) continue;
        seen.add(script.name);
        result.scripts.push(script);
    }
    return execution;
}

/** Browser-safe source records for one live HostRuntime session. */
export function contentRuntimeManifest(result) {
    if( !result?.contentDir ) return sourceManifest(result?.scripts || []);
    const scriptNames = readPack(join(result.contentDir, 'pack', '12_clientscripts.pack'));
    const sprites = readPack(join(result.contentDir, 'pack', '8_sprites.pack'));
    const scripts = sourceClosure(result.contentDir, result.scripts || [], scriptNames);
    const usedText = scripts.map((script) => script.source).join('\n');
    const spriteIds = new Map();
    for( const [id, name] of sprites ) {
        if( usedText.includes(`"${name}"`) || usedText.includes(`"${name.replace('_', ',')}"`) )
            spriteIds.set(name, id);
    }
    return sourceManifest(scripts, scriptNames, spriteIds);
}

function sourceClosure(contentDir, roots, scriptNames) {
    const idByName = new Map([...scriptNames].map(([id, name]) => [name, id]));
    const records = new Map();
    const queue = [];
    const add = (record) => {
        if( !record?.name || records.has(record.name) ) return;
        const normalized = {
            ...record,
            id: Number.isInteger(record.id) ? record.id : idByName.get(record.name) ?? null,
        };
        records.set(normalized.name, normalized);
        queue.push(normalized);
    };
    for( const script of roots ) add(script);
    while( queue.length ) {
        const script = queue.shift();
        const dependencies = new Set([
            ...[...script.source.matchAll(/~([A-Za-z_][A-Za-z0-9_]*)\s*\(/g)].map((match) => match[1]),
            ...[...script.source.matchAll(/["']([A-Za-z_][A-Za-z0-9_]*)\s*\(/g)].map((match) => match[1]),
            /* Deferred callbacks are also commonly written as a bare script
             * name (for example cc_setonclick("closebutton_click", null)). */
            ...[...script.source.matchAll(
                /(?:cc|if)_seton[a-z0-9_]*\(\s*["']([A-Za-z_][A-Za-z0-9_]*)/g,
            )].map((match) => match[1]),
        ]);
        for( const dependency of dependencies ) {
            const numeric = /^script(\d+)$/.exec(dependency);
            const name = numeric ? scriptNames.get(Number(numeric[1])) || dependency : dependency;
            if( records.has(name) ) continue;
            const file = join(contentDir, 'scripts', `${name}.cs2`);
            if( existsSync(file) ) add({ name, file, source: readFileSync(file, 'utf8') });
        }
    }
    return [...records.values()];
}

function sourceRuntimeOptions(contentDir, scripts) {
    const scriptNames = readPack(join(contentDir, 'pack', '12_clientscripts.pack'));
    const sprites = readPack(join(contentDir, 'pack', '8_sprites.pack'));
    return {
        scripts,
        scriptNames,
        spriteIds: new Map([...sprites].map(([id, name]) => [name, id])),
        loadScript(name) {
            const file = join(contentDir, 'scripts', `${name}.cs2`);
            return existsSync(file) ? { name, file, source: readFileSync(file, 'utf8') } : null;
        },
    };
}

function sourceManifest(scripts, scriptNames = new Map(), spriteIds = new Map()) {
    const records = scripts.filter((script) => typeof script?.source === 'string').map((script) => ({
        id: Number.isInteger(script.id) ? script.id : null,
        name: script.name,
        source: script.source,
    }));
    const names = new Map();
    for( const script of records ) if( Number.isInteger(script.id) ) names.set(script.id, script.name);
    return {
        scripts: records,
        scriptNames: Object.fromEntries(names),
        spriteIds: Object.fromEntries(spriteIds),
    };
}

/** `[name]` blocks, preserving everything after the first '=' verbatim. */
export function parseBlocks(text) {
    const blocks = [];
    let current = null;
    for( const rawLine of text.split(/\r?\n/) ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const opened = /^\[(.+)\]$/.exec(line);
        if( opened ) {
            current = { name: opened[1], fields: {} };
            blocks.push(current);
            continue;
        }
        const split = rawLine.indexOf('=');
        if( split > 0 && current )
            current.fields[rawLine.slice(0, split).trim()] = rawLine.slice(split + 1);
    }
    return blocks;
}

function parentFileId(raw, interfaceId) {
    if( raw === undefined || raw === '' ) return null;
    const uid = integer(raw, -1);
    if( uid < 0 ) return null;
    /* IF3 stores the full uid even though the component archive already supplies
     * the high half.  An external parent cannot be laid out from this one archive,
     * so leave it unattached and let the viewport be its parent. */
    const owner = Math.floor(uid / 65536);
    return owner === interfaceId ? uid & 0xffff : null;
}

function parseHook(raw) {
    const values = splitArguments(raw).map(parseTypedArgument);
    const first = values.shift();
    return {
        scriptId: first && first.type === 'int' ? first.value : -1,
        args: values,
        raw,
    };
}

/* Strings in hook records may contain commas.  cachepack's format has no quoting;
 * the next `,i:` / `,s:` prefix is the delimiter. */
function splitArguments(raw) {
    return String(raw || '').split(/,(?=[is]:)/);
}

function parseTypedArgument(token) {
    if( token.startsWith('i:') ) return { type: 'int', value: integer(token.slice(2), 0) };
    if( token.startsWith('s:') ) return { type: 'string', value: token.slice(2) };
    return { type: 'raw', value: token };
}

function linkedScripts(contentDir, components, warnings) {
    const ids = new Set();
    for( const component of components )
        for( const binding of component.scriptBindings )
            if( binding.scriptId >= 0 ) ids.add(binding.scriptId);

    const names = readPack(join(contentDir, 'pack', '12_clientscripts.pack'));
    const scripts = [];
    for( const id of ids ) {
        const name = names.get(id) || `script_${id}`;
        const sourcePath = join(contentDir, 'scripts', `${name}.cs2`);
        if( existsSync(sourcePath) ) {
            scripts.push({ id, name, source: readFileSync(sourcePath, 'utf8'), file: sourcePath });
            continue;
        }

        const binary = ['cs2b', 'bin']
            .map((ext) => join(contentDir, 'scripts', `${name}.${ext}`))
            .find(existsSync);
        if( binary ) {
            scripts.push({
                id, name, file: binary,
                source: `// Script ${id} is binary (${binary}).\n// It could not be decompiled by cachepack.\n`,
            });
        } else {
            warnings.push(`script ${id} (${name}) is referenced but has no source or binary record`);
        }
    }
    return scripts;
}

/** Emit a valid static TSX view plus comments for cache-only behaviour. */
export function emitReactView(ir) {
    const byId = new Map(ir.components.map((component) => [component.fileId, component]));
    const children = new Map(ir.components.map((component) => [component.fileId, []]));
    const roots = [];
    for( const component of ir.components ) {
        if( component.layer !== null && byId.has(component.layer) )
            children.get(component.layer).push(component);
        else
            roots.push(component);
    }

    const usedKinds = [...new Set(ir.components
        .map((component) => component.kind)
        .filter((kind) => ELEMENTS[kind]))];
    const out = [
        '/*',
        ` * Decompiled view of content interface ${ir.name} (${ir.interfaceId}).`,
        ' * Static fields are executable cs2dom TSX. Hook bindings and fields outside',
        ' * the authored vocabulary stay in comments so their cache semantics are not lost.',
        ' */',
        `import { ${usedKinds.join(', ')} } from 'cs2dom';`,
        '',
        `export default function ${componentFunctionName(ir.name)}() {`,
    ];

    for( const component of ir.components ) {
        if( component.legacyTooltip )
            out.push(`    // ${component.name}: legacy IF1 type 8, presented as React-style <Text>.`);
        if( component.scriptBindings.length ) {
            const summary = component.scriptBindings
                .map((binding) => `${binding.field}=${binding.raw}`)
                .join('; ');
            out.push(`    // ${component.name} cache hooks: ${escapeComment(summary)}`);
        }
        if( Object.keys(component.rawFields).length ) {
            const summary = Object.entries(component.rawFields)
                .map(([key, value]) => `${key}=${value}`).join('; ');
            out.push(`    // ${component.name} unmapped fields: ${escapeComment(summary)}`);
        }
    }
    out.push('    return (');

    if( roots.length === 1 ) emitComponent(out, roots[0], children, 2);
    else {
        out.push('        <>');
        for( const root of roots ) emitComponent(out, root, children, 3);
        out.push('        </>');
    }
    out.push('    );', '}', '');
    return out.join('\n');
}

function emitComponent(out, component, children, depth) {
    const indent = '    '.repeat(depth);
    if( !ELEMENTS[component.kind] ) {
        out.push(`${indent}{/* unsupported component ${escapeComment(component.name)}, type ${component.type} */}`);
        return;
    }

    const attributes = [`id=${tsxValue(component.name)}`];
    for( const prop of component.authoredProps )
        attributes.push(`${prop}=${tsxValue(component.static[prop])}`);
    if( component.ops.length )
        attributes.push(`ops={${JSON.stringify(component.ops.map((op) => [op.index, op.text]))}}`);

    const kids = children.get(component.fileId) || [];
    const open = `<${component.kind} ${attributes.join(' ')}>`;
    if( kids.length === 0 ) {
        out.push(`${indent}${open.slice(0, -1)} />`);
        return;
    }
    out.push(`${indent}${open}`);
    for( const child of kids ) emitComponent(out, child, children, depth + 1);
    out.push(`${indent}</${component.kind}>`);
}

function tsxValue(value) {
    if( typeof value === 'string' ) return `{${JSON.stringify(value)}}`;
    if( typeof value === 'boolean' ) return value ? '{true}' : '{false}';
    return `{${String(value)}}`;
}

function componentFunctionName(name) {
    const cleaned = name.replace(/[^a-z0-9]+/gi, '_')
        .replace(/(^|_)([a-z])/g, (_, __, letter) => letter.toUpperCase());
    return /^[A-Za-z_$]/.test(cleaned) ? cleaned : `Interface${cleaned}`;
}

function escapeComment(value) {
    return String(value).replace(/\*\//g, '* /').replace(/[\r\n]+/g, ' ');
}

function fieldValue(raw, type) {
    if( type === 'boolean' ) return raw === 'yes' || raw === 'true' || raw === '1';
    if( type === 'int' ) return integer(raw, 0);
    return raw;
}

function integer(value, fallback) {
    const number = Number.parseInt(value, 10);
    return Number.isNaN(number) ? fallback : number;
}

function numberList(value) {
    return String(value).split(',').map((item) => integer(item, 0));
}

function compackByName(text) {
    const result = new Map();
    for( const line of text.split(/\r?\n/) ) {
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const name = packName(line.slice(split + 1));
        if( name ) result.set(name, integer(line.slice(0, split), 0));
    }
    return result;
}

/** Read an id=name pack in its natural id -> name direction. */
function readPack(path) {
    const result = new Map();
    if( !existsSync(path) ) return result;
    for( const line of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const split = line.indexOf('=');
        if( split < 1 || line.trimStart().startsWith('//') ) continue;
        const id = Number.parseInt(line.slice(0, split), 10);
        const name = packName(line.slice(split + 1));
        if( !Number.isNaN(id) && name ) result.set(id, name);
    }
    return result;
}

function idForName(path, name) {
    for( const [id, candidate] of readPack(path) )
        if( candidate === name ) return id;
    return null;
}
