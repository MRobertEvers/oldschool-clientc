/*
 * Ids.
 *
 * An interface id and a script id are not properties of a build — they are
 * properties of the content tree, and other things point at them: the server sends
 * a component uid, a varc binding names a script, a saved layout remembers an
 * interface. So ids are allocated once, written down, and never reused. The pack
 * files are where they are written down, because they are already the tree's
 * authority: `pack/3_interfaces.pack` and `pack/12_clientscripts.pack` are what
 * cachepack reads to decide what an archive id means, and a name with no line in
 * them is an asset that does not get written at all.
 *
 * Allocation only ever appends past the highest id in the file. A name that
 * disappears from the sources keeps its line, and its id stays spent: recycling it
 * would silently hand one script's id to a different script, and everything that
 * remembered the old one would then be pointing at the new one.
 */

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

export class PackFile {
    constructor(path) {
        this.path = path;
        this.byName = new Map();
        this.byId = new Map();
        this.header = [];
        this.dirty = false;

        if( existsSync(path) ) {
            const text = readFileSync(path, 'utf8');
            for( const rawLine of text.split('\n') ) {
                const line = rawLine.trim();
                if( !line ) continue;
                if( line.startsWith('//') || line.startsWith(';') ) { this.header.push(line); continue; }
                const split = line.indexOf('=');
                if( split < 0 ) continue;
                const id = Number.parseInt(line.slice(0, split), 10);
                const name = line.slice(split + 1).trim();
                if( Number.isNaN(id) ) continue;
                this.byName.set(name, id);
                this.byId.set(id, name);
            }
        }
    }

    /** The id this name already has, or the next free one. */
    idFor(name) {
        const existing = this.byName.get(name);
        if( existing !== undefined ) return existing;

        let id = 0;
        for( const known of this.byId.keys() )
            if( known >= id ) id = known + 1;

        this.byName.set(name, id);
        this.byId.set(id, name);
        this.dirty = true;
        return id;
    }

    has(name) {
        return this.byName.has(name);
    }

    write() {
        if( !this.dirty ) return false;
        const ids = [...this.byId.keys()].sort((a, b) => a - b);
        const body = ids.map((id) => `${id}=${this.byId.get(id)}`).join('\n');
        writeFileSync(this.path, (this.header.length ? this.header.join('\n') + '\n' : '') + body + '\n');
        return true;
    }
}

export class Ledger {
    constructor(contentDir) {
        this.interfaces = new PackFile(join(contentDir, 'pack', '3_interfaces.pack'));
        this.scripts = new PackFile(join(contentDir, 'pack', '12_clientscripts.pack'));
    }

    interfaceId(name) {
        return this.interfaces.idFor(name);
    }

    scriptId(name) {
        return this.scripts.idFor(name);
    }

    write() {
        const wrote = [];
        if( this.interfaces.write() ) wrote.push(this.interfaces.path);
        if( this.scripts.write() ) wrote.push(this.scripts.path);
        return wrote;
    }
}
