/*
 * An `.if` file, read and written without losing anything.
 *
 * This is the round trip's load-bearing piece, and its whole design follows
 * from one requirement: **an interface imported and exported with no edits
 * must be byte-identical.** Not "semantically equivalent", not "the fields we
 * model are preserved" — identical, because the content tree is under version
 * control and a diff full of reformatting hides the one line that matters.
 *
 * That rules out the obvious design. Parsing into a typed model and
 * re-serialising from it loses every field the model does not know about, and
 * an IF3 component has more fields than any vocabulary will keep up with. So
 * the record below keeps the ORIGINAL TEXT of every block and rewrites only
 * the lines that actually changed:
 *
 *   - a block nobody touched is re-emitted verbatim, comments and all;
 *   - a field that was edited is replaced in place, keeping its position;
 *   - a field that was added is appended;
 *   - a field the vocabulary has never heard of is simply left alone.
 *
 * The parsed view sits alongside the text as an index into it, not as a
 * replacement for it.
 */

/** Lines that open a block: `[name]`. */
const BLOCK_RE = /^\[([^\]]*)\]\s*$/;
/**
 * A field assignment: `key=value`, where value may be empty.
 *
 * Matched against the RAW line, never a trimmed one. A trailing space is data:
 * `text=Reward: ` is a label a script appends a number to, and thirty of the
 * tree's interfaces carry a `name= ` whose whole content is one space. Trimming
 * before the match reads them all back a character short, and the loss only
 * shows once the value is written somewhere.
 */
const FIELD_RE = /^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=([\s\S]*)$/;

export class IfRecordError extends Error {
    constructor(message) {
        super(message);
        this.name = 'IfRecordError';
    }
}

/**
 * Parse an `.if` file.
 *
 * Everything before the first block is the preamble and is kept as-is; it is
 * where the generated-file marker and the human comments live, and dropping it
 * would make every import look like a rewrite.
 */
export function parseIf(text) {
    const lines = String(text ?? '').split('\n');
    const preamble = [];
    const blocks = [];
    let current = null;

    for( const line of lines )
    {
        const opened = BLOCK_RE.exec(line);
        if( opened )
        {
            current = { name: opened[1], header: line, lines: [], fields: new Map() };
            blocks.push(current);
            continue;
        }
        if( !current ) { preamble.push(line); continue; }

        const index = current.lines.length;
        current.lines.push(line);

        const field = FIELD_RE.exec(line);
        if( !field ) continue;
        /*
         * FIRST occurrence wins the index, and later ones are recorded too.
         * A duplicate key is not something to normalise away — the encoder may
         * read them all (an `op` list is written as repeated keys in some
         * hand-authored files), and silently keeping one would change the
         * interface.
         */
        const key = field[1];
        if( !current.fields.has(key) ) current.fields.set(key, []);
        current.fields.get(key).push({ index, value: field[2] });
    }

    return new IfRecord({ preamble, blocks });
}

export class IfRecord {
    constructor({ preamble = [], blocks = [] } = {}) {
        this.preamble = preamble;
        this.blocks = blocks;
        this._byName = new Map(blocks.map((block) => [block.name, block]));
        /** Blocks whose text this session changed; the rest re-emit verbatim. */
        this.dirty = new Set();
    }

    block(name) { return this._byName.get(name) ?? null; }
    names() { return this.blocks.map((block) => block.name); }

    /** A field's value, or null. The FIRST occurrence, matching the decoder. */
    get(blockName, key) {
        const entries = this.block(blockName)?.fields.get(key);
        return entries && entries.length ? entries[0].value : null;
    }

    /** Every value for a repeated key, in file order. */
    getAll(blockName, key) {
        const entries = this.block(blockName)?.fields.get(key);
        return entries ? entries.map((entry) => entry.value) : [];
    }

    /**
     * Set a field, in place if it exists and appended if it does not.
     *
     * `null` removes it. In place matters: moving a field to the end of its
     * block turns a one-line edit into a whole-block diff, which is exactly
     * the noise this record exists to avoid.
     */
    set(blockName, key, value) {
        const block = this.block(blockName);
        if( !block ) throw new IfRecordError(`no block [${blockName}]`);
        const entries = block.fields.get(key);

        if( value === null || value === undefined )
        {
            if( !entries ) return false;
            /* Remove every occurrence, highest index first so the earlier
             * indexes stay valid while splicing. */
            for( const entry of [...entries].sort((a, b) => b.index - a.index) )
                block.lines.splice(entry.index, 1);
            block.fields.delete(key);
            this._reindex(block);
            this.dirty.add(blockName);
            return true;
        }

        const text = `${key}=${value}`;
        if( entries && entries.length )
        {
            if( block.lines[entries[0].index] === text ) return false;
            block.lines[entries[0].index] = text;
            entries[0].value = String(value);
        }
        else
        {
            /*
             * Append BEFORE the block's trailing blank lines.
             *
             * Those blanks separate this block from the next and belong to
             * neither; pushing past them puts the new field after the gap,
             * where it reads as part of the following block. The blank stays
             * in the file — dropping it would be a diff of its own.
             */
            let at = block.lines.length;
            while( at > 0 && block.lines[at - 1].trim() === '' ) at--;
            block.lines.splice(at, 0, text);
            this._reindex(block);
        }
        this.dirty.add(blockName);
        return true;
    }

    /** Add a block at the end. Its `.compack` entry is the caller's business. */
    addBlock(name, fields = {}) {
        if( this._byName.has(name) ) throw new IfRecordError(`block [${name}] already exists`);
        const block = { name, header: `[${name}]`, lines: [], fields: new Map() };
        this.blocks.push(block);
        this._byName.set(name, block);
        this.dirty.add(name);
        for( const [key, value] of Object.entries(fields) ) this.set(name, key, value);
        return block;
    }

    removeBlock(name) {
        const index = this.blocks.findIndex((block) => block.name === name);
        if( index < 0 ) return false;
        this.blocks.splice(index, 1);
        this._byName.delete(name);
        this.dirty.add(name);
        return true;
    }

    _reindex(block) {
        block.fields.clear();
        block.lines.forEach((line, index) => {
            const field = FIELD_RE.exec(line);
            if( !field ) return;
            if( !block.fields.has(field[1]) ) block.fields.set(field[1], []);
            block.fields.get(field[1]).push({ index, value: field[2] });
        });
    }

    /**
     * Write the file back.
     *
     * Untouched blocks are re-emitted from their own lines, so their spacing,
     * comments and field order survive exactly. There is no canonical form
     * being imposed — the file that comes out is the file that went in, plus
     * the edits.
     */
    toText() {
        const out = [...this.preamble];
        for( const block of this.blocks )
        {
            out.push(block.header);
            out.push(...block.lines);
        }
        return out.join('\n');
    }

    /** Which blocks changed, for a report or a targeted recompile. */
    changed() { return [...this.dirty].sort(); }
}

/* -------------------------------------------------------------------------
 * The .compack beside it
 * ---------------------------------------------------------------------- */

/**
 * `name=fileId` per line: which block is which component.
 *
 * Not derivable and not optional. A component's uid is
 * `(interface << 16) | fileId` and scripts reference it by that number, so an
 * invented id moves a component every time the tree changes — and the script
 * that addressed it then addresses something else.
 */
export function parseCompack(text) {
    const byName = new Map();
    const order = [];
    for( const raw of String(text ?? '').split('\n') )
    {
        const line = raw.replace(/\/\/.*$/, '').trim();
        if( !line ) continue;
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split).trim());
        const name = line.slice(split + 1).trim();
        if( !Number.isInteger(id) || !name ) continue;
        byName.set(name, id);
        order.push({ id, name });
    }
    return { byName, order };
}

export function emitCompack({ order }) {
    return `${order.map((entry) => `${entry.id}=${entry.name}`).join('\n')}\n`;
}

/**
 * The next free file id.
 *
 * Only ever past the highest, never a gap. A recycled id hands one component's
 * uid to a different component, and every script that referenced the old one
 * silently addresses the new one — the same rule the interface and script id
 * ledgers already follow.
 */
export function nextFileId({ order }) {
    let highest = -1;
    for( const entry of order ) if( entry.id > highest ) highest = entry.id;
    return highest + 1;
}
