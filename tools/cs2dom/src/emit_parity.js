/*
 * Comparing this runtime's draw list against the C client's.
 *
 * ------------------------------------------------------------------
 * Why the emit list and not a screenshot
 * ------------------------------------------------------------------
 *
 * A screenshot proves the pixels and tells you nothing about why they differ.
 * The emit list is the layer where every decision this port makes is visible —
 * what was pruned, where it was laid out, what clip it got, in what order it
 * was drawn — and a mismatch is a DIFF: "command 412, y=80 versus y=93". An
 * image diff of the same failure is a smear of red.
 *
 * It is also the honest boundary. Below it are the rasteriser's decisions —
 * how a glyph antialiases, how a tinted blit composites — which are toridraw's
 * and the browser's respectively, and which this port does not claim to
 * reproduce byte for byte. Above it is everything the redesign is responsible
 * for.
 *
 * A screenshot comparison remains worth having on top of this, for the things
 * only pixels show. It is not a substitute for this one, and this one is not a
 * substitute for it.
 *
 * ------------------------------------------------------------------
 * What is compared, and what is not
 * ------------------------------------------------------------------
 *
 * Compared: the sequence of (kind, component, box, clip, colour, fill,
 * transparency, sprite/model id). That is the whole of what the C client's
 * `TORIRS_DUMP_EMIT_EXIT` reports and the whole of what a painter needs.
 *
 * Not compared: node indexes and scene ids. An index is storage — the C tree's
 * free list hands slots out in a different order than a fresh JavaScript tree
 * does, and requiring them to agree would fail on a difference that means
 * nothing. A scene id is the C client's texture-atlas handle, which has no
 * counterpart here at all.
 */

import { EMIT_KIND } from './emit.js';

/** The C client's `EMIT_EXIT[...]` line. */
const EMIT_LINE = new RegExp(
    'EMIT_EXIT\\[(\\d+)\\] kind=(-?\\d+) com=0x([0-9a-f]+) \\(\\d+\\|\\d+\\) '
    + 'x=(-?\\d+) y=(-?\\d+) w=(-?\\d+) h=(-?\\d+) scene=(-?\\d+) model=(-?\\d+) '
    + 'color=0x([0-9a-f]+) filled=(-?\\d+) trans=(-?\\d+) tiled=(-?\\d+) '
    + 'clip=(-?\\d+),(-?\\d+) (-?\\d+)x(-?\\d+)');

/**
 * `enum UITreeEmitKind` -> this runtime's kinds.
 *
 * The C enum carries kinds this port has no equivalent for — the world
 * viewport, the minimap, the scrollbar chrome the IF1 path draws — and those
 * are not failures to reproduce. They are content the browser preview does not
 * host, so they are filtered rather than mismatched.
 */
const C_KIND = new Map([
    [1, EMIT_KIND.SPRITE],
    [2, EMIT_KIND.TEXT],
    [3, EMIT_KIND.RECT],
    [4, EMIT_KIND.LINE],
    [6, EMIT_KIND.MODEL],
]);

/**
 * Kinds the C client draws that this preview does not host.
 *
 * Their NUMBERS come from `enum UITreeEmitKind` in src/ui/uitree_emit.h and
 * are checked against it by the test — guessing them is exactly the mistake
 * that makes an oracle report false differences on every command, and MODEL
 * being 6 rather than 5 (5 is ARC) is the one that was guessed wrong first.
 *
 *   0 NONE            5 ARC             7 CC_OBJ
 *   8/9 SCROLLBAR     10 WORLD          11 MINIMAP
 *   12 COMPASS        13 ENTITY_OVERLAY 14 WORLDMAP
 *   15 DEBUG_OVERLAY
 */
export const UNHOSTED_KINDS = new Set([0, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15]);

/** The names, so a report can say what it filtered rather than a number. */
export const C_KIND_NAMES = Object.freeze([
    'NONE', 'SPRITE', 'TEXT', 'RECT', 'LINE', 'ARC', 'MODEL', 'CC_OBJ',
    'SCROLLBAR_V', 'SCROLLBAR_H', 'WORLD', 'MINIMAP', 'COMPASS',
    'ENTITY_OVERLAY', 'WORLDMAP', 'DEBUG_OVERLAY',
]);

export function parseCEmitDump(text) {
    const commands = [];
    for( const line of String(text ?? '').split('\n') )
    {
        const match = EMIT_LINE.exec(line);
        if( !match ) continue;
        const cKind = Number(match[2]);
        commands.push({
            index: Number(match[1]),
            cKind,
            kind: C_KIND.get(cKind) ?? null,
            componentId: Number.parseInt(match[3], 16),
            x: Number(match[4]), y: Number(match[5]),
            width: Number(match[6]), height: Number(match[7]),
            scene: Number(match[8]),
            model: Number(match[9]),
            colour: Number.parseInt(match[10], 16),
            filled: Number(match[11]),
            trans: Number(match[12]),
            tiled: Number(match[13]),
            clip: {
                x: Number(match[14]), y: Number(match[15]),
                width: Number(match[16]), height: Number(match[17]),
            },
        });
    }
    return commands;
}

/** This runtime's commands, in the same shape. */
export function normalizeJsCommands(commands) {
    return commands.map((command, index) => ({
        index,
        kind: command.kind,
        componentId: command.componentId,
        x: command.x, y: command.y,
        width: command.width, height: command.height,
        colour: command.props.colour ?? -1,
        /*
         * `filled` belongs to a RECT and to nothing else. The C descriptor is
         * zeroed per command and only `case UIELEM_RS_RECT` writes the field,
         * so a SPRITE whose component happens to carry `fill=yes` reports 0
         * there. Reporting the prop for every kind made `dt2_warmind_puzzle`
         * differ on all 141 commands over a field the reference never set.
         */
        filled: command.kind === EMIT_KIND.RECT && command.props.filled ? 1 : 0,
        trans: command.trans | 0,
        tiled: command.props.tiled ? 1 : 0,
        clip: { ...command.clip },
    }));
}

/**
 * Compare two draw lists.
 *
 * Returns every difference, not the first: a layout change usually moves many
 * commands, and stopping at one turns a single cause into many runs of the
 * comparison. The differences are grouped by KIND of difference so a report
 * can say "eleven boxes moved" rather than listing eleven near-identical rows.
 */
export function compareEmit(cCommands, jsCommands, { ignore = [] } = {}) {
    const skip = new Set(ignore);
    const expected = cCommands.filter((command) =>
        command.kind !== null && !UNHOSTED_KINDS.has(command.cKind)
        && !skip.has(command.componentId));
    const actual = jsCommands.filter((command) => !skip.has(command.componentId));

    const differences = [];
    const limit = Math.max(expected.length, actual.length);

    for( let i = 0; i < limit; i++ )
    {
        const want = expected[i];
        const got = actual[i];
        if( !want ) { differences.push(extra(got, i)); continue; }
        if( !got ) { differences.push(missing(want, i)); continue; }

        for( const [field, a, b] of scalarFields(want, got) )
            if( a !== b ) differences.push({
                at: i, field, expected: a, actual: b,
                component: want.componentId,
            });
    }

    /*
     * How much of the difference is ALIGNMENT.
     *
     * The comparison is positional on purpose — draw order is half of what an
     * emit list means — but that makes one missing command at the head report
     * every field of every later command as wrong, and a report that says 731
     * differences when the real answer is "one command short, everything else
     * in step" is a report nobody can track progress against. So the shift is
     * measured and stated beside the raw count rather than compensated for.
     */
    const alignment = measureAlignment(expected, actual);

    return {
        expectedCount: expected.length,
        actualCount: actual.length,
        alignment,
        differences,
        /* One line per KIND of difference, so a report is readable when a
         * single cause moved every box on the screen. */
        summary: summarise(differences),
        matches: differences.length === 0,
    };
}

/**
 * The best constant shift between the two lists, and what it buys.
 *
 * `prefix` is how many leading commands agree on every field — the honest
 * "how far in do the two runs stay identical". `offset`/`matched` say whether
 * sliding one list past the other explains the rest: a large `matched` at a
 * non-zero `offset` means commands are MISSING or EXTRA, not misplaced, and
 * that is a different bug from geometry that disagrees everywhere.
 */
function measureAlignment(expected, actual) {
    const same = (want, got) => !!want && !!got
        && scalarFields(want, got).every(([, a, b]) => a === b);

    let prefix = 0;
    while( prefix < expected.length && prefix < actual.length
        && same(expected[prefix], actual[prefix]) ) prefix++;

    let best = { offset: 0, matched: 0 };
    const reach = Math.min(32, Math.max(expected.length, actual.length));
    for( let offset = -reach; offset <= reach; offset++ )
    {
        let matched = 0;
        for( let i = 0; i < expected.length; i++ )
            if( same(expected[i], actual[i + offset]) ) matched++;
        if( matched > best.matched ) best = { offset, matched };
    }
    return { prefix, ...best };
}

function scalarFields(want, got) {
    return [
        ['kind', want.kind, got.kind],
        ['componentId', want.componentId, got.componentId],
        ['x', want.x, got.x],
        ['y', want.y, got.y],
        ['width', want.width, got.width],
        ['height', want.height, got.height],
        ['trans', want.trans, got.trans],
        ['filled', want.filled, got.filled],
        ['tiled', want.tiled, got.tiled],
        ['clip.x', want.clip.x, got.clip.x],
        ['clip.y', want.clip.y, got.clip.y],
        ['clip.width', want.clip.width, got.clip.width],
        ['clip.height', want.clip.height, got.clip.height],
        /* Colour last: a -1 on either side means "this kind carries none",
         * and comparing it would fail every rectangle that inherits one. */
        ...(want.colour >= 0 && got.colour >= 0
            ? [['colour', want.colour, got.colour]] : []),
    ];
}

function missing(want, at) {
    return { at, field: 'present', expected: describe(want), actual: null,
             component: want.componentId };
}

function extra(got, at) {
    return { at, field: 'present', expected: null, actual: describe(got),
             component: got.componentId };
}

function describe(command) {
    return `${command.kind}@${command.x},${command.y} ${command.width}x${command.height}`;
}

function summarise(differences) {
    const byField = new Map();
    for( const difference of differences )
    {
        if( !byField.has(difference.field) ) byField.set(difference.field, []);
        byField.get(difference.field).push(difference);
    }
    return [...byField].map(([field, entries]) => ({
        field,
        count: entries.length,
        first: entries[0],
    })).sort((a, b) => b.count - a.count);
}

/**
 * A stable fingerprint of a draw list.
 *
 * For a gate that only needs to know THAT something changed — a regression
 * check across a corpus — where the full comparison is for finding out what.
 */
export function emitFingerprint(commands) {
    const parts = commands.map((command) =>
        `${command.kind}|${command.componentId}|${command.x},${command.y}|`
        + `${command.width}x${command.height}|${command.clip.x},${command.clip.y},`
        + `${command.clip.width}x${command.clip.height}|${command.trans}`);
    return hash(parts.join('\n'));
}

/** FNV-1a: enough to notice a change, and it needs no dependency. */
function hash(text) {
    let value = 0x811c9dc5;
    for( let i = 0; i < text.length; i++ )
    {
        value ^= text.charCodeAt(i);
        value = Math.imul(value, 0x01000193) >>> 0;
    }
    return value.toString(16).padStart(8, '0');
}
