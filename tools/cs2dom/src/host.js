/*
 * Host state: everything a script reads that is not a component.
 *
 * A CS2 script reaching outside itself does it through the VM's host requests —
 * `CS2VM_HOST_REQUEST_VARS_READ_VARP`, `..._STAT`, `..._INVS_GET_NUM`,
 * `..._ENUM_LOOKUP` and the rest of the 242 in src/cs2vm2/cs2vm2_host.h. Reading
 * component props is one slice of that surface; this file is the other, and the
 * preview needs it for the same reason the client does: an interface that shows an
 * inventory count cannot be looked at without an inventory to count.
 *
 * Each slice declares three things:
 *
 *   - **the range its ids live in**, so `useStat(40)` is a compile error naming the
 *     range rather than a silent read of nothing. Ranges come from the content tree
 *     where the tree states them (a varp id is bounded by configs/all.varp.compack)
 *     and from the client where it is a fixed table (there are 23 stats);
 *   - **how the preview answers a read**, against a state object the page owns;
 *   - **what control the page offers**, because a slice nobody can move is a slice
 *     that cannot be tested.
 *
 * A read this file does not model is not answered with a zero and forgotten — it is
 * recorded, and the page lists it as unmodelled. A preview that quietly invents
 * values is worse than one that admits which parts it is guessing.
 */

/** Fixed-size slices the client defines rather than the cache. */
export const STAT_COUNT = 23;

/**
 * The slices, keyed by the name a state source or a command uses.
 *
 * `limit(context)` returns the highest valid id, given what the content tree knows;
 * null means the tree could not say, and the range check is skipped rather than
 * guessed.
 */
export const SLICES = {
    varp: {
        label: 'server variable',
        request: 'VARS_READ_VARP',
        limit: (c) => c.counts.varp ?? null,
        control: { min: 0, max: 10000, step: 1 },
        read: (state, source) => value(state, source, 0),
    },
    varbit: {
        label: 'packed variable field',
        request: 'VARS_READ_VARBIT',
        limit: (c) => c.counts.varbit ?? null,
        control: { min: 0, max: 15, step: 1 },
        read: (state, source) => value(state, source, 0),
    },
    varc: {
        label: 'client variable',
        request: 'VARS_READ_VARC_INT',
        /* A varc the interface allocates for itself is not in the cache's table,
         * so the tree's count is a floor rather than a ceiling. */
        limit: () => null,
        control: { min: -1, max: 1000, step: 1 },
        /* The production VarCManager's unset integer sentinel is -1. Authored
         * useState values still carry their explicit `initial` override. */
        read: (state, source) => value(state, source, source.initial ?? -1),
    },
    varcstr: {
        label: 'client string variable',
        request: 'VARS_READ_VARC_STRING',
        limit: () => null,
        control: { kind: 'text' },
        read: (state, source) => value(state, source, source.initial ?? ''),
    },
    stat: {
        label: 'skill level',
        request: 'STAT',
        limit: () => STAT_COUNT - 1,
        control: { min: 1, max: 99, step: 1, initial: 1 },
        read: (state, source) => value(state, source, source.id === 3 ? 10 : 1),
    },
    inv: {
        label: 'inventory',
        request: 'INVS_GET_NUM',
        limit: (c) => c.counts.inv ?? null,
        control: { kind: 'inventory' },
        read: (state, source) => value(state, source, 0),
    },
};

function value(state, source, fallback) {
    const key = `${source.kind}:${source.id}`;
    return key in state ? state[key] : fallback;
}

/**
 * Commands that read host state, and how the preview answers them.
 *
 * `state` here is the same object the slices read, with inventory contents under
 * `inv:<id>` as `{ <obj id>: count }` — the shape the page's inventory editor
 * writes and `inv_getnum` asks about.
 */
export const HOST_READS = {
    inv_getnum: {
        request: 'INVS_GET_NUM',
        evaluate: (args, state) => {
            const contents = state[`invobj:${args[0]}`];
            return contents ? (contents[args[1]] || 0) : 0;
        },
    },
    inv_total: {
        request: 'INVS_GET_TOTAL',
        evaluate: (args, state) => {
            const contents = state[`invobj:${args[0]}`];
            return contents ? Object.values(contents).reduce((a, b) => a + b, 0) : 0;
        },
    },
    inv_size: {
        request: 'INVS_GET_SIZE',
        /* Source-only analysis may carry an explicit fixture override. The
         * live HOST reads immutable InvType data; an unavailable type is zero,
         * never a guessed backpack capacity. */
        evaluate: (args, state) => state[`invsize:${args[0]}`] ?? 0,
    },
    stat: { request: 'STAT', evaluate: (args, state) =>
        state[`stat:${args[0]}`] ?? (Number(args[0]) === 3 ? 10 : 1) },
    stat_base: { request: 'STAT_BASE', evaluate: (args, state) =>
        state[`stat:${args[0]}`] ?? (Number(args[0]) === 3 ? 10 : 1) },
    clientclock: { request: 'CLIENTCLOCK', evaluate: (args, state) => state['clock'] ?? 0 },
};

/**
 * Reads with no model, and there are two honest reasons for that.
 *
 * `enum` and the db commands answer out of the cache, which the preview could read
 * but does not decode yet; the world reads answer out of a running game, which it
 * never will. Either way the page says so rather than showing a zero as if it meant
 * something.
 */
export const UNMODELLED = {
    enum: 'reads an enum out of the cache, which the preview does not decode yet',
    db_getfield: 'reads a dbtable row out of the cache',
    db_find: 'searches a dbtable in the cache',
};

/**
 * Check an id against its slice.
 *
 * Called while lowering, so a bad id is a build error at the line that wrote it
 * rather than a component that renders blank against a live cache.
 */
export function checkRange(kind, id, context) {
    const slice = SLICES[kind];
    if( !slice ) return null;
    if( !Number.isInteger(id) || id < 0 )
        return `${slice.label} id must be a whole number, got ${id}`;
    const limit = slice.limit(context);
    if( limit !== null && id > limit )
        return `${slice.label} ${id} is outside the range this cache defines (0..${limit})`;
    return null;
}

/** The id ceilings the content tree states, for `checkRange`. */
export function rangeContext(counts) {
    return { counts: counts || {} };
}
