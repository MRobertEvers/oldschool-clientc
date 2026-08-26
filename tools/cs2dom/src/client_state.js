/* Client-global state established outside any individual interface.
 *
 * Revision 239's login protocol runs clientscript 605 with these four values.
 * That script writes camera_zoom_{small,big}_{min,max} (varcs 1338..1341).
 * Both settings zoom sliders divide by the resulting ranges, so an isolated
 * interface session must carry the same login state as the real client.
 *
 * Source of truth:
 *   src/torirsserver/torirs_server_world.c (zoom_limits)
 *   src/torirsserver/test/mock239_runclientscript_test.c (literal wire gate)
 */
export const OSRS239_LOGIN_STATE = Object.freeze({
    'varc:1338': 128,
    'varc:1339': 896,
    'varc:1340': 128,
    'varc:1341': 896,
});

/** Return a mutable session state with explicit user/interface overrides. */
export function osrs239ClientState(overrides = {}) {
    return { ...OSRS239_LOGIN_STATE, ...overrides };
}

/**
 * Apply only the client-global bootstrap belonging to the bytecode revision.
 * Unknown revisions remain untouched rather than silently borrowing rev-239
 * values. Explicit preview/Host-state values always win over the bootstrap.
 */
export function clientStateForRevision(revision, overrides = {}) {
    const normalized = String(revision ?? '').toLowerCase().replace(/[^a-z0-9]/g, '');
    return normalized === 'osrs239' || normalized === '239'
        ? osrs239ClientState(overrides)
        : { ...overrides };
}
