/*
 * A deliberately bounded interpreter for the source-form CS2 shipped beside an
 * unpacked interface.  It is not a second game VM: it models the part of CS2
 * which paints interfaces (component reads/writes, control flow, locals and
 * procedure calls).  Unknown game reads are zero and unknown commands are
 * harmless, while limits keep a bad cache script from hanging the dev server.
 */

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { ELEMENTS, IF_TYPE } from './components.js';

const TYPE_KIND = new Map([
    [IF_TYPE.layer, 'Layer'], [IF_TYPE.rectangle, 'Rect'], [IF_TYPE.text, 'Text'],
    [IF_TYPE.graphic, 'Graphic'], [IF_TYPE.model, 'Model'], [IF_TYPE.line, 'Line'],
]);

const CONSTANTS = {
    true: true, false: false, null: null,
    min_32bit_int: -2147483648,
    iftype_layer: 0, iftype_rectangle: 3, iftype_text: 4, iftype_graphic: 5,
    iftype_model: 6, iftype_line: 9,
    setpos_abs_left: 0, setpos_abs_top: 0, setpos_abs_centre: 1,
    setpos_abs_center: 1, setpos_abs_right: 2, setpos_abs_bottom: 2,
    setsize_abs: 0, setsize_minus: 1, setsize_proportional: 2,
    settextalign_left: 0, settextalign_top: 0, settextalign_centre: 1,
    settextalign_center: 1, settextalign_right: 2, settextalign_bottom: 2,
};

/** Execute every cache-authored onload hook and return its observable state. */
export function runCacheHooks(ir, contentDir, state = {}, warnings = []) {
    const runtime = new Runtime(ir, contentDir, state, warnings);
    /* Snapshot: scripts can append dynamic components while this loop runs. Only
     * cache components own automatic onload hooks. */
    for( const component of [...ir.components] ) {
        const hook = component.hooks?.onload;
        if( hook?.script?.id >= 0 ) runtime.invokeId(hook.script.id, hook.args || [], component);
    }
    runtime.finish();
    return {
        dependencies: runtime.dependencies,
        warnings: runtime.runtimeWarnings,
        scripts: [...runtime.cache.entries()]
            .filter(([, script]) => script)
            .map(([name, script]) => ({ name: script.name || name, source: script.source, file: script.file })),
    };
}

class Runtime {
    constructor(ir, contentDir, state, warnings) {
        this.ir = ir;
        this.contentDir = contentDir;
        this.state = state;
        this.runtimeWarnings = warnings;
        this.byId = new Map(ir.components.map((component) => [component.fileId, component]));
        this.byUid = new Map(ir.components.map((component) => [ir.interfaceId * 65536 + component.fileId, component]));
        this.scriptNames = readPack(join(contentDir, 'pack', '12_clientscripts.pack'));
        this.spriteIds = reversePack(join(contentDir, 'pack', '8_sprites.pack'));
        this.cache = new Map();
        this.dependencies = new Map();
        this.current = null;
        this.dotCurrent = null;
        this.steps = 0;
        this.depth = 0;
        this.dynamicId = 0;
        this.dynamicByParent = new Map();
        this.warned = new Set();
    }

    invokeId(id, rawArgs, current) {
        const name = this.scriptNames.get(id);
        if( !name ) return this.warnOnce(`script ${id} has no name`);
        const args = rawArgs.map((arg) => this.hookArg(arg, current));
        return this.invoke(name, args, current);
    }

    hookArg(arg, current) {
        if( arg?.type === 'string' ) return arg.value;
        const value = arg?.value ?? arg;
        if( value === -2147483645 ) return current;
        return this.component(value) || value;
    }

    invoke(name, args = [], current = this.current) {
        if( this.depth >= 48 || this.steps >= 250000 ) return 0;
        const script = this.load(name);
        if( !script ) return 0;
        const previous = this.current;
        const previousDot = this.dotCurrent;
        this.current = current;
        this.dotCurrent = null;
        this.depth++;
        const env = bindParameters(script.params, args);
        let result = 0;
        try { result = this.exec(script.body, env); }
        catch( signal ) {
            if( signal?.returnValue !== undefined ) result = signal.returnValue;
            else this.warnOnce(`${name}: ${signal.message || signal}`);
        }
        this.depth--;
        this.current = previous;
        this.dotCurrent = previousDot;
        return result;
    }

    load(name) {
        if( this.cache.has(name) ) return this.cache.get(name);
        /* Decompilation preserves unresolved calls as ~scriptNNN even when the
         * cache pack can name that script. The native VM calls by id, so resolve
         * that spelling through the same clientscripts pack before opening the
         * source file. Cache under the requested alias because callers use it as
         * the procedure identity. */
        const numeric = /^script(\d+)$/.exec(name);
        const canonical = numeric ? this.scriptNames.get(Number(numeric[1])) || name : name;
        const path = join(this.contentDir, 'scripts', `${canonical}.cs2`);
        if( !existsSync(path) ) {
            this.cache.set(name, null);
            return null;
        }
        try {
            const source = readFileSync(path, 'utf8');
            const parsed = { ...parseScript(source), source, file: path, name: canonical };
            this.cache.set(name, parsed);
            return parsed;
        } catch( error ) {
            this.warnOnce(`${name}: preview parser stopped at ${error.message}`);
            this.cache.set(name, null);
            return null;
        }
    }

    exec(statements, env) {
        for( const statement of statements ) {
            if( ++this.steps > 250000 ) throw new Error('preview instruction limit reached');
            switch( statement.kind ) {
                case 'declare': env.set(statement.name, statement.value ? this.eval(statement.value, env) : 0); break;
                case 'declareArray': env.set(statement.name, new Array(Math.max(0, Number(this.eval(statement.size, env)) || 0)).fill(0)); break;
                case 'assign': this.assign(statement.name, this.eval(statement.value, env), env); break;
                case 'assignMany': {
                    const values = statement.values.map((value) => this.eval(value, env));
                    statement.names.forEach((name, index) => this.assign(name, values[index] ?? values[0]?.[index] ?? 0, env));
                    break;
                }
                case 'assignArray': {
                    const array = env.get(statement.name);
                    if( Array.isArray(array) ) array[Number(this.eval(statement.index, env)) || 0] = this.eval(statement.value, env);
                    break;
                }
                case 'expr': this.eval(statement.value, env); break;
                case 'if': {
                    let ran = false;
                    for( const branch of statement.branches ) {
                        if( branch.test === null || this.truthy(this.eval(branch.test, env)) ) {
                            this.exec(branch.body, env); ran = true; break;
                        }
                    }
                    void ran;
                    break;
                }
                case 'while': {
                    let count = 0;
                    while( this.truthy(this.eval(statement.test, env)) && count++ < 4096 )
                        this.exec(statement.body, env);
                    break;
                }
                case 'switch': {
                    const value = this.eval(statement.value, env);
                    const branch = statement.cases.find((item) => item.values === null ||
                        item.values.some((candidate) => this.eval(candidate, env) === value));
                    if( branch ) this.exec(branch.body, env);
                    break;
                }
                case 'return': throw { returnValue: statement.value ? this.eval(statement.value, env) : 0 };
                case 'break': return;
                default: break;
            }
        }
        return 0;
    }

    eval(node, env) {
        if( !node ) return 0;
        switch( node.kind ) {
            case 'literal': return typeof node.value === 'string'
                ? scriptString(node.value, env) : node.value;
            case 'name': return this.readName(node.value, env);
            case 'unary': {
                const value = this.eval(node.value, env);
                return node.op === '-' ? -Number(value || 0) : !this.truthy(value);
            }
            case 'binary': {
                const left = this.eval(node.left, env);
                if( node.op === '&' && !this.truthy(left) ) return false;
                if( node.op === '|' && this.truthy(left) ) return true;
                const right = this.eval(node.right, env);
                switch( node.op ) {
                    case '+': return left + right;
                    case '-': return Number(left) - Number(right);
                    case '*': return Number(left) * Number(right);
                    case '/': return Number(right) === 0 ? 0 : Math.trunc(Number(left) / Number(right));
                    case '%': return Number(right) === 0 ? 0 : Number(left) % Number(right);
                    case '=': return left === right;
                    case '!': return left !== right;
                    case '<': return left < right;
                    case '>': return left > right;
                    case '<=': return left <= right;
                    case '>=': return left >= right;
                    case '&': return this.truthy(right);
                    case '|': return this.truthy(right);
                    default: return 0;
                }
            }
            case 'call': return this.call(node.name, node.args.map((arg) => this.eval(arg, env)), env);
            case 'tuple': return node.values.map((value) => this.eval(value, env));
            case 'select': return this.truthy(this.eval(node.test, env))
                ? this.eval(node.yes, env) : this.eval(node.no, env);
            default: return 0;
        }
    }

    readName(name, env) {
        if( env.has(name) ) return env.get(name);
        if( name.startsWith('~') ) return this.invoke(name.slice(1), [], this.current);
        if( name === 'cc_getid' ) return this.current?.subId ?? -1;
        if( name === '.cc_getid' ) return this.dotCurrent?.subId ?? -1;
        if( name.startsWith('^') ) return CONSTANTS[name.slice(1)] ?? 0;
        if( name.startsWith('%') ) return this.stateRead(name);
        if( /^interface_\d+:\d+$/.test(name) ) return this.componentName(name);
        if( name === 'true' ) return true;
        if( name === 'false' ) return false;
        if( name === 'null' ) return null;
        return name;
    }

    assign(name, value, env) {
        if( name.startsWith('%') ) this.stateWrite(name, value);
        else env.set(name, value);
    }

    stateKey(name) {
        let match = /^%varp(\d+)$/.exec(name);
        if( match ) return ['varp', Number(match[1])];
        match = /^%varbit(\d+)$/.exec(name);
        if( match ) return ['varbit', Number(match[1])];
        match = /^%varcint(\d+)$/.exec(name);
        if( match ) return ['varc', Number(match[1])];
        match = /^%varcstring(\d+)$/.exec(name);
        if( match ) return ['varcstr', Number(match[1])];
        return null;
    }

    stateRead(name) {
        const parsed = this.stateKey(name);
        if( !parsed ) return 0;
        const [kind, id] = parsed;
        const key = `${kind}:${id}`;
        this.dependencies.set(key, { kind, id });
        return key in this.state ? this.state[key] : (kind === 'varcstr' ? '' : 0);
    }

    stateWrite(name, value) {
        const parsed = this.stateKey(name);
        if( parsed ) this.state[`${parsed[0]}:${parsed[1]}`] = value;
    }

    call(rawName, args, env) {
        const dotted = rawName.startsWith('.');
        const name = dotted ? rawName.slice(1) : rawName;
        if( name.startsWith('$') && Array.isArray(env.get(name)) )
            return env.get(name)[Number(args[0]) || 0] ?? 0;
        if( name.startsWith('~') ) return this.invoke(name.slice(1), args, this.current);
        if( name === 'calc' ) return args[0] ?? 0;
        if( name === 'tostring' ) return String(args[0] ?? '');
        if( name === 'min' ) return Math.min(Number(args[0]), Number(args[1]));
        if( name === 'max' ) return Math.max(Number(args[0]), Number(args[1]));
        if( name === 'testbit' ) return ((Number(args[0]) >>> Number(args[1])) & 1) !== 0;
        if( name === 'if_getwidth' ) return this.box(this.component(args[0]))?.w ?? 0;
        if( name === 'if_getheight' ) return this.box(this.component(args[0]))?.h ?? 0;
        if( name === 'if_getx' ) return this.box(this.component(args[0]))?.relX ?? 0;
        if( name === 'if_gety' ) return this.box(this.component(args[0]))?.relY ?? 0;
        if( name === 'if_getscrollwidth' ) {
            const component = this.component(args[0]);
            return component?.static.scrollWidth || this.box(component)?.w || 0;
        }
        if( name === 'if_getscrollheight' ) {
            const component = this.component(args[0]);
            return component?.static.scrollHeight || this.box(component)?.h || 0;
        }
        if( name === 'if_getscrollx' ) return this.component(args[0])?.static.scrollX ?? 0;
        if( name === 'if_getscrolly' ) return this.component(args[0])?.static.scrollY ?? 0;
        if( name === 'if_getlayer' ) {
            const component = this.component(args[0]);
            return component?.layer === null ? null : this.byId.get(component?.layer) || null;
        }
        if( name === 'if_gethide' ) return Boolean(this.component(args[0])?.static.hidden);
        if( name === 'if_gettext' ) return this.component(args[0])?.static.text ?? '';
        if( name === 'cc_create' ) return this.createDynamic(args, dotted);
        if( name === 'cc_find' ) {
            const parent = this.component(args[0]);
            const found = parent ? this.dynamicByParent.get(`${parent.fileId}:${Number(args[1]) || 0}`) : null;
            if( dotted ) this.dotCurrent = found || null; else this.current = found || null;
            return Boolean(found);
        }
        if( name === 'cc_deleteall' ) {
            const parent = this.component(args[0]);
            if( parent ) {
                this.ir.components = this.ir.components.filter((component) => {
                    if( component.runtimeDynamic && component.layer === parent.fileId ) {
                        this.dynamicByParent.delete(`${parent.fileId}:${component.subId}`);
                        this.byId.delete(component.fileId);
                        return false;
                    }
                    return true;
                });
            }
            return 0;
        }

        const target = name.startsWith('cc_')
            ? (dotted ? this.dotCurrent : this.current)
            : this.component(args.at(-1));
        const values = name.startsWith('cc_') ? args : args.slice(0, -1);
        if( target && this.mutate(name.replace(/^cc_/, 'if_'), target, values) ) return 0;

        /* A bare identifier followed by parentheses is a host command in CS2,
         * except for named procedures which always carry the `~` sigil. Unknown
         * reads intentionally resolve to zero. */
        void env;
        return 0;
    }

    mutate(name, target, a) {
        const props = target.static;
        switch( name ) {
            case 'if_sethide': props.hidden = Boolean(a[0]); return true;
            case 'if_settext': props.text = interpolate(String(a[0] ?? ''), this); return true;
            case 'if_setcolour': props.color = Number(a[0]) | 0; return true;
            case 'if_setfill': props.fill = Boolean(a[0]); return true;
            case 'if_settrans': props.transparency = Number(a[0]) | 0; return true;
            case 'if_setgraphic': props.sprite = this.sprite(a[0]); return true;
            case 'if_settiling': props.tiled = Boolean(a[0]); return true;
            case 'if_setoutline': props.outline = Number(a[0]) | 0; return true;
            case 'if_setgraphicshadow': props.shadow = Number(a[0]) | 0; return true;
            case 'if_sethflip': props.hFlip = Boolean(a[0]); return true;
            case 'if_setvflip': props.vFlip = Boolean(a[0]); return true;
            case 'if_settextfont': props.font = this.namedId(a[0]); return true;
            case 'if_settextshadow': props.shadow = Boolean(a[0]); return true;
            case 'if_settextalign': [props.halign, props.valign, props.lineHeight] = a.map(Number); return true;
            case 'if_setposition': [props.x, props.y, props.xMode, props.yMode] = a.map(Number); return true;
            case 'if_setsize': [props.width, props.height, props.widthMode, props.heightMode] = a.map(Number); return true;
            case 'if_setscrollsize': [props.scrollWidth, props.scrollHeight] = a.map(Number); return true;
            case 'if_setscrollpos': [props.scrollX, props.scrollY] = a.map(Number); return true;
            case 'if_setmodel': props.model = Number(a[0]); return true;
            case 'if_setmodelanim': props.seq = Number(a[0]); return true;
            case 'if_setmodelorthog': props.orthographic = Boolean(a[0]); return true;
            case 'if_setmodelangle':
                [props.xOffset, props.yOffset, props.xAngle, props.yAngle, props.zAngle, props.zoom] = a.map(Number);
                return true;
            default: return false;
        }
    }

    createDynamic(args, dotted) {
        const parent = this.component(args[0]);
        const type = Number(args[1]);
        if( !parent || !TYPE_KIND.has(type) ) return 0;
        const kind = TYPE_KIND.get(type);
        const definition = ELEMENTS[kind];
        const staticProps = Object.fromEntries(Object.entries(definition.props).map(([key, schema]) => [key, schema.default]));
        const subId = Number(args[2]) || 0;
        const component = {
            fileId: `d${this.dynamicId++}`, name: `${parent.name}[${Number(args[2]) || 0}]`,
            kind, type, layer: parent.fileId, subId, props: staticProps, static: staticProps,
            authoredProps: new Set(), dynamic: [], ops: [], events: {}, hooks: {}, triggers: {},
            dependencies: [], scriptBindings: [], rawFields: {}, runtimeDynamic: true,
        };
        this.ir.components.push(component);
        this.byId.set(component.fileId, component);
        this.dynamicByParent.set(`${parent.fileId}:${subId}`, component);
        if( dotted ) this.dotCurrent = component;
        else this.current = component;
        return component;
    }

    component(value) {
        if( value && typeof value === 'object' && value.static ) return value;
        if( typeof value === 'string' && /^interface_\d+:\d+$/.test(value) ) return this.componentName(value);
        if( Number.isInteger(value) ) return this.byUid.get(value) || this.byId.get(value) || null;
        return null;
    }

    componentName(value) {
        const match = /^interface_(\d+):(\d+)$/.exec(value);
        return match ? this.byUid.get(Number(match[1]) * 65536 + Number(match[2])) || null : null;
    }

    /** The same root-to-node, on-demand layout the C CS2 host getters use. */
    box(component, visiting = new Set()) {
        if( !component || visiting.has(component) ) return null;
        visiting.add(component);
        const parent = component.layer === null ? null : this.byId.get(component.layer);
        const parentBox = parent
            ? this.box(parent, visiting)
            : { x: 0, y: 0, w: 512, h: 334 };
        if( !parentBox ) return null;
        const props = component.static;
        let w = dim(props.widthMode | 0, props.width | 0, parentBox.w);
        let h = dim(props.heightMode | 0, props.height | 0, parentBox.h);
        const aspectW = Math.max(1, Number(props.aspectW) || 1);
        const aspectH = Math.max(1, Number(props.aspectH) || 1);
        if( (props.widthMode | 0) === 4 ) w = Math.trunc(aspectW * h / aspectH);
        if( (props.heightMode | 0) === 4 ) h = Math.trunc(aspectH * w / aspectW);
        w = Math.max(0, w);
        h = Math.max(0, h);
        const relX = axis(props.xMode | 0, props.x | 0, parentBox.w, w);
        const relY = axis(props.yMode | 0, props.y | 0, parentBox.h, h);
        return { x: parentBox.x + relX, y: parentBox.y + relY, w, h, relX, relY };
    }

    sprite(value) {
        if( typeof value === 'number' ) return value;
        const normalized = String(value).replace(',', '_');
        return this.spriteIds.get(normalized) ?? this.spriteIds.get(String(value)) ?? -1;
    }

    namedId(value) {
        if( typeof value === 'number' ) return value;
        const match = /(\d+)$/.exec(String(value));
        return match ? Number(match[1]) : 0;
    }

    truthy(value) { return Boolean(value); }
    warnOnce(message) {
        if( this.warned.has(message) ) return;
        this.warned.add(message);
        this.runtimeWarnings.push(message);
    }

    finish() {
        /* Empty dynamic inventory slots have no pixels. Keeping thousands of
         * them would only make the browser tree unusable; decorated children
         * remain and are laid out normally. */
        this.ir.components = this.ir.components.filter((component) => !component.runtimeDynamic ||
            component.kind === 'Text' && component.static.text ||
            component.kind === 'Graphic' && component.static.sprite >= 0 ||
            component.kind === 'Rect' && (component.static.fill || component.static.transparency < 255) ||
            component.kind === 'Model' && component.static.model >= 0);
        for( const source of this.dependencies.values() )
            (this.ir.components[0]?.dependencies || []).push(source);
    }
}

function interpolate(value) {
    /* Most decompiled strings contain runtime <tostring(...)> tags. The parser
     * has already evaluated explicit tostring calls; retain unknown rich-text
     * tags because the browser can display their text safely. */
    return value.replace(/<br>/g, '\n').replace(/<[^>]+>/g, '');
}

/* ---- parser ------------------------------------------------------------ */

function parseScript(source) {
    const header = /^\s*(?:\/\/[^\n]*\n\s*)*\[[^\]]+\]\(([^)]*)\)(?:\([^)]*\))?/m.exec(source);
    if( !header ) throw new Error('missing script header');
    const params = [...header[1].matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s+(\$[A-Za-z0-9_]+)/g)]
        .map((match) => ({ type: match[1], name: match[2] }));
    const bodyStart = source.indexOf('\n', header.index + header[0].length);
    const parser = new Parser(tokenize(bodyStart < 0 ? '' : source.slice(bodyStart + 1)));
    return { params, body: parser.statements() };
}

function tokenize(source) {
    const tokens = [];
    for( let i = 0; i < source.length; ) {
        const c = source[i];
        if( /\s/.test(c) ) { i++; continue; }
        if( c === '/' && source[i + 1] === '/' ) { while( i < source.length && source[i] !== '\n' ) i++; continue; }
        if( c === '"' ) {
            let value = ''; i++;
            while( i < source.length && source[i] !== '"' ) {
                if( source[i] === '\\' && i + 1 < source.length ) {
                    const escaped = source[++i]; value += escaped === 'n' ? '\n' : escaped; i++;
                } else value += source[i++];
            }
            i++; tokens.push({ type: 'literal', value }); continue;
        }
        const number = /^(?:0x[0-9a-f]+|\d+)/i.exec(source.slice(i));
        if( number ) { tokens.push({ type: 'literal', value: Number(number[0]) }); i += number[0].length; continue; }
        const ident = /^[.$%^~A-Za-z_][.$%^~A-Za-z0-9_:]*/.exec(source.slice(i));
        if( ident ) { tokens.push({ type: 'name', value: ident[0] }); i += ident[0].length; continue; }
        const pair = source.slice(i, i + 2);
        if( ['<=', '>=', '==', '!='].includes(pair) ) { tokens.push({ type: pair[0] === '=' ? '=' : pair[0] === '!' ? '!' : pair, value: pair }); i += 2; continue; }
        if( '{}(),;:+-*/%=!<>&|?'.includes(c) ) { tokens.push({ type: c, value: c }); i++; continue; }
        i++;
    }
    tokens.push({ type: 'eof', value: '' });
    return tokens;
}

class Parser {
    constructor(tokens) { this.tokens = tokens; this.at = 0; }
    peek(value = null) { const token = this.tokens[this.at]; return value === null ? token : token.value === value; }
    take(value = null) { const token = this.tokens[this.at++]; if( value !== null && token.value !== value ) throw new Error(`expected '${value}', got '${token.value}'`); return token; }
    maybe(value) { if( this.peek(value) ) { this.at++; return true; } return false; }

    statements(stop = '}') {
        const out = [];
        while( this.peek().type !== 'eof' && !this.peek(stop) ) {
            const statement = this.statement();
            if( statement ) out.push(statement);
        }
        return out;
    }

    block() { this.take('{'); const value = this.statements(); this.take('}'); return value; }

    statement() {
        if( this.maybe(';') ) return null;
        if( this.peek('if') ) return this.ifStatement();
        if( this.peek('while') ) return this.whileStatement();
        if( this.peek().value.startsWith('switch') ) return this.switchStatement();
        if( this.maybe('return') ) {
            const wrapped = this.maybe('(');
            let value = null;
            if( !this.peek(wrapped ? ')' : ';') ) {
                const values = [this.expression()];
                while( this.maybe(',') ) values.push(this.expression());
                value = values.length === 1 ? values[0] : { kind: 'tuple', values };
            }
            if( wrapped ) this.take(')');
            this.maybe(';'); return { kind: 'return', value };
        }
        if( this.maybe('break') || this.maybe('continue') ) { this.maybe(';'); return { kind: 'break' }; }
        if( /^def_/.test(this.peek().value) ) {
            this.take(); const name = this.take().value;
            if( this.maybe('(') ) {
                const size = this.expression(); this.take(')'); this.maybe(';');
                return { kind: 'declareArray', name, size };
            }
            const value = this.maybe('=') ? this.expression() : null;
            this.maybe(';'); return { kind: 'declare', name, value };
        }
        if( this.peek().type === 'name' && this.peek().value.startsWith('$') && this.tokens[this.at + 1]?.value === '(' ) {
            const save = this.at;
            const name = this.take().value; this.take('('); const index = this.expression(); this.take(')');
            if( this.maybe('=') ) {
                const value = this.expression(); this.maybe(';');
                return { kind: 'assignArray', name, index, value };
            }
            this.at = save;
        }
        if( this.peek().type === 'name' && this.tokens[this.at + 1]?.value === ',' ) {
            const names = [this.take().value];
            while( this.maybe(',') ) names.push(this.take().value);
            this.take('=');
            const values = [this.expression()];
            while( this.maybe(',') ) values.push(this.expression());
            this.maybe(';'); return { kind: 'assignMany', names, values };
        }
        if( this.peek().type === 'name' && this.tokens[this.at + 1]?.value === '=' ) {
            const name = this.take().value; this.take('='); const value = this.expression();
            this.maybe(';'); return { kind: 'assign', name, value };
        }
        const value = this.expression(); this.maybe(';'); return { kind: 'expr', value };
    }

    ifStatement() {
        const branches = [];
        this.take('if'); this.take('('); const test = this.expression(); this.take(')');
        branches.push({ test, body: this.peek('{') ? this.block() : [this.statement()] });
        while( this.maybe('else') ) {
            if( this.peek('if') ) {
                this.take('if'); this.take('('); const next = this.expression(); this.take(')');
                branches.push({ test: next, body: this.peek('{') ? this.block() : [this.statement()] });
            } else { branches.push({ test: null, body: this.peek('{') ? this.block() : [this.statement()] }); break; }
        }
        return { kind: 'if', branches };
    }

    whileStatement() {
        this.take('while'); this.take('('); const test = this.expression(); this.take(')');
        return { kind: 'while', test, body: this.peek('{') ? this.block() : [this.statement()] };
    }

    switchStatement() {
        this.take(); this.take('('); const value = this.expression(); this.take(')'); this.take('{');
        const cases = [];
        while( !this.peek('}') && this.peek().type !== 'eof' ) {
            let caseValues;
            if( this.maybe('case') ) {
                caseValues = [this.expression()];
                while( this.maybe(',') ) caseValues.push(this.expression());
            }
            else if( this.maybe('default') ) caseValues = null;
            else { this.take(); continue; }
            this.take(':');
            const body = [];
            while( !this.peek('case') && !this.peek('default') && !this.peek('}') ) {
                const statement = this.statement(); if( statement ) body.push(statement);
            }
            cases.push({ values: caseValues, body });
        }
        this.take('}'); return { kind: 'switch', value, cases };
    }

    expression(min = 0) {
        let left;
        if( this.maybe('-') ) left = { kind: 'unary', op: '-', value: this.expression(8) };
        else if( this.maybe('!') ) left = { kind: 'unary', op: '!', value: this.expression(8) };
        else if( this.maybe('(') ) { left = this.expression(); this.take(')'); }
        else {
            const token = this.take();
            if( token.type === 'literal' ) left = { kind: 'literal', value: token.value };
            else if( token.type === 'name' ) {
                if( this.maybe('(') ) {
                    const args = [];
                    if( !this.peek(')') ) do { args.push(this.expression()); } while( this.maybe(',') );
                    this.take(')'); left = { kind: 'call', name: token.value, args };
                } else left = { kind: 'name', value: token.value };
            } else throw new Error(`unexpected '${token.value}'`);
        }

        const precedence = { '|': 1, '&': 2, '=': 3, '!': 3, '<': 4, '>': 4, '<=': 4, '>=': 4, '+': 5, '-': 5, '*': 6, '/': 6, '%': 6 };
        while( precedence[this.peek().value] > min ) {
            const op = this.take().value;
            left = { kind: 'binary', op, left, right: this.expression(precedence[op]) };
        }
        if( min === 0 && this.maybe('?') ) {
            const yes = this.expression(); this.take(':'); const no = this.expression();
            left = { kind: 'select', test: left, yes, no };
        }
        return left;
    }
}

function readPack(path) {
    const result = new Map();
    if( !existsSync(path) ) return result;
    for( const line of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number.parseInt(line.slice(0, split), 10);
        if( !Number.isNaN(id) ) result.set(id, line.slice(split + 1).trim());
    }
    return result;
}

function reversePack(path) {
    return new Map([...readPack(path)].map(([id, name]) => [name, id]));
}

function bindParameters(params, args) {
    const env = new Map();
    /* CS2 has independent int and string stacks. Components, objects, fonts,
     * enums and booleans all live on the int stack; source decompilation may
     * interleave the two stack families differently at a call site (notably
     * steelborder(component, string, flags)). Bind in declaration order within
     * each real VM stack, exactly as PushCallScript does. */
    const ints = args.filter((value) => typeof value !== 'string');
    const strings = args.filter((value) => typeof value === 'string');
    let intAt = 0;
    let stringAt = 0;
    for( const param of params ) {
        env.set(param.name, param.type === 'string'
            ? strings[stringAt++] ?? ''
            : ints[intAt++] ?? 0);
    }
    return env;
}

function scriptString(value, env) {
    return value.replace(/<tostring\((\$[A-Za-z0-9_]+)\)>/g,
        (_, name) => String(env.get(name) ?? 0));
}

const mulShift14 = (a, b) => Math.trunc((a * b) / 16384);
function dim(mode, base, parent) {
    return mode === 1 ? parent - base : mode === 2 ? mulShift14(parent, base) : base;
}
function axis(mode, base, parent, self) {
    if( mode === 1 ) return Math.trunc((parent - self) / 2) + base;
    if( mode === 2 ) return parent - base - self;
    if( mode === 3 ) return mulShift14(parent, base);
    if( mode === 4 ) return Math.trunc((parent - self) / 2) + mulShift14(parent, base);
    if( mode === 5 ) return parent - mulShift14(parent, base) - self;
    return base;
}
