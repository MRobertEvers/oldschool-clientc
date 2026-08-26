/*
 * The existing host services, on the generated surface.
 *
 * `host_db.js`, `host_worldmap.js`, `host_activity.js` and the rest were
 * written against a tagged-request API: one object per call, dispatched by
 * name. That API is the boundary this redesign removes, but the SERVICES
 * behind it are correct and hard-won — the DB iterator's filter semantics, the
 * world map's session state, the chat store's ordering — and rewriting them to
 * change a calling convention would be throwing away the part that works.
 *
 * So they are adapted rather than rewritten. Each installer here turns a
 * positional call into the record that service expects, in the argument order
 * the DECOMPILER's command table gives (which is push order, not the C struct's
 * field order — see scripts/gen_host_surface.py for why those differ).
 *
 * The adaptation is per method and explicit. A generic name-mangling bridge
 * would be shorter and would silently transpose an argument the first time a
 * signature disagreed, which is exactly the class of bug the old runtime's
 * generic request path kept producing.
 */

import {
    createDbState, decodeDbColumn, handleDbRequest,
} from './host_db.js';
import { createWorldMapState, handleWorldMapRequest } from './host_worldmap.js';
import { createLootState, handleLootRequest } from './host_loot.js';
import { createOverlayState, handleOverlayRequest } from './host_overlay.js';

/** Install the DB family. `host.db` is a state object from `createDbState`. */
export function installDbOps(HostKernel) {
    const proto = HostKernel.prototype;

    /*
     * A `dbcolumn` literal packs (table << 12) | (column << 4) | (field + 1),
     * and field 0 means "the whole tuple". None of that is in the bytecode —
     * it is in the dbtable config the column names — which is why the C
     * decompiler needs a column-type hook to get the stack shape right at all.
     */
    proto.db_getfield = function (rowId, column, tupleIndex) {
        this.calls++;
        /*
         * UNWRAPPED. `handleDbRequest` answers `{pattern, values}` because the
         * WASM bridge needs to know which stack each value goes on; a script
         * expression wants the value itself, and a whole-tuple read wants the
         * tuple as the compound the emitter models a multi-slot result with.
         *
         * Handing the record through put "[object Object]" into every text a
         * DB row supplies — the music list's 857 track names among them.
         */
        const answer = handleDbRequest(this.db, 'DB_GETFIELD', {
            rowId, column, index: tupleIndex,
        });
        if( !answer || !Array.isArray(answer.values) ) return answer;
        return answer.values.length === 1 ? answer.values[0] : answer.values;
    };

    proto.db_getfieldcount = function (rowId, column) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_GETFIELDCOUNT', { rowId, column });
    };

    proto.db_getrowtable = function (rowId) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_GETROWTABLE', { rowId });
    };

    proto.db_getrow = function (index) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_GETROW', { index });
    };

    /*
     * The iterator is ONE cursor, shared by every find in flight.
     *
     * That is the reference's own design and scripts depend on it: `db_find`
     * seeds it, `db_find_filter` narrows what is already there, and
     * `db_findnext` walks it. A per-call iterator would make the filter forms
     * meaningless.
     */
    proto.db_findnext = function () {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FINDNEXT', {});
    };

    proto.db_find = function (column, value) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FIND', { column, value });
    };

    proto.db_find_with_count = function (column, value) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FIND_WITH_COUNT', { column, value });
    };

    proto.db_find_filter = function (column, value) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FIND_FILTER', { column, value });
    };

    proto.db_find_filter_with_count = function (column, value) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FIND_FILTER_WITH_COUNT', { column, value });
    };

    proto.db_findall = function (tableId) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FINDALL', { tableId });
    };

    proto.db_findall_with_count = function (tableId) {
        this.calls++;
        return handleDbRequest(this.db, 'DB_FINDALL_WITH_COUNT', { tableId });
    };
}

/**
 * Text measurement.
 *
 * `parawidth` and `paraheight` answer how wide, and how many lines, a string
 * wraps to — and the subtlety is that MARKUP IS SKIPPED. The renderer consumes
 * `<col=…>`, `</col>`, `<lt>`, `<gt>` and `@xxx@` without drawing glyphs for
 * them, so a byte walk that counts them measures a string far wider than the
 * one that appears, and the widget sized from it is wrong.
 *
 * The measurement needs real font metrics, so it is delegated: a caller that
 * has decoded fonts supplies `host.fonts`, and one that has not gets a park
 * rather than a plausible number.
 */
export function installTextMeasureOps(HostKernel) {
    const proto = HostKernel.prototype;

    /*
     * `parawidth(text, maxWidth, font)` — the argument order is the CALL's.
     *
     * The C request struct lists font first and the surface lists it LAST,
     * because the surface is generated from the decompiler's prototype pool,
     * which is push order. Taking the struct's order here transposes the font
     * id with the wrap width: font 190 does not exist, so every measurement
     * answered 0 and every widget sized from one laid out at its padding.
     */
    proto.parawidth = function (text, width, fontId) {
        this.calls++;
        if( !this.fonts || !this.fonts.has(fontId) )
        {
            if( fontId >= 0 && !this._awaitSpent('font', fontId) )
                return this._park('font', fontId);
            return 0;
        }
        return this.fonts.measureWidth(fontId, stripMarkup(text), width | 0);
    };

    proto.paraheight = function (text, width, fontId) {
        this.calls++;
        if( !this.fonts || !this.fonts.has(fontId) )
        {
            if( fontId >= 0 && !this._awaitSpent('font', fontId) )
                return this._park('font', fontId);
            return 0;
        }
        return this.fonts.measureHeight(fontId, stripMarkup(text), width | 0);
    };
}

/**
 * Remove what the renderer will not draw.
 *
 * Deliberately the same grammar the font decoder uses
 * (`ToriDraw_FontMarkupTokenLength`): `<...>` tags and `@xxx@` colour codes.
 * A second, looser copy of this rule is how the C client's measurement and its
 * renderer came to disagree in the first place.
 */
export function stripMarkup(text) {
    return String(text ?? '')
        /*
         * `<br>` is a LINE BREAK, not markup to drop. It has to survive as one
         * before the rest of the tags go, because the wrapper counts lines and
         * a stripped `<br>` silently joins two of them.
         *
         * The kudos list is the witness: its rows are
         * `"<str=ffffff><title><br><str=ffffff><detail>"`, two lines each. With
         * the break stripped every row measured as one, the list's scroll
         * extent came out short by 15 pixels per row, and the scrollbar thumb
         * was sized against a content height that did not exist.
         */
        .replace(/<br\s*\/?>/gi, '\n')
        .replace(/<[^>]*>/g, '')
        .replace(/@[a-z]{3}@/gi, '');
}

/**
 * The services a preview cannot answer truthfully, and says so.
 *
 * Sound, logout, chat sends and the rest reach another system in the real
 * client. A preview has no such system; recording the intent and telling the
 * page is honest, and pretending the server accepted it is not. These are
 * therefore no-ops that EMIT, not no-ops that lie.
 */
export function installIntentOps(HostKernel) {
    const proto = HostKernel.prototype;

    const INTENTS = {
        sound_synth: ['synth', ['id', 'loops', 'delay']],
        sound_song: ['song', ['id']],
        sound_jingle: ['jingle', ['id', 'delay']],
        mes: ['message', ['text']],
        chat_sendpublic: ['chat', ['text']],
        chat_sendprivate: ['privateChat', ['name', 'text']],
        openurl: ['openUrl', ['url', 'newTab']],
        logout: ['logout', []],
        setidlepopup: ['idlePopup', ['text']],
    };

    for( const [method, [intent, fields]] of Object.entries(INTENTS) )
    {
        proto[method] = function (...args) {
            this.calls++;
            const payload = {};
            fields.forEach((field, index) => { payload[field] = args[index]; });
            this.intents.push({ intent, ...payload });
            this.onIntent?.(intent, payload);
        };
    }
}

export { createDbState, decodeDbColumn };


/* -------------------------------------------------------------------------
 * The world map
 * ---------------------------------------------------------------------- */

/**
 * Install the world-map family over `host_worldmap.js`.
 *
 * The service behind it is the old runtime's and it is right: the session
 * state, the source/display coordinate conversion, the flash and disable
 * registries and the element iterator were all worked out against the real
 * cache. What is replaced is the calling convention, one method at a time.
 *
 * Each mapping is written out rather than derived from the name. A generic
 * bridge is shorter and transposes an argument the first time a signature
 * disagrees — `worldmap_jumptomap(map, coord)` and
 * `worldmap_coordinmap(map, coord)` take their two arguments in the same
 * order, `worldmap_getnearesticon(x, y)` does not take a map at all, and
 * nothing about their names says so.
 */
export function installWorldMapOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* The service takes ONE tagged request object, `kind` included — it was
     * written against the generated request manifest, where the tag travels
     * with the fields. */
    /* ...and it answers `{ result, changed }`: the second half is the session
     * dirty flag a renderer uses to decide whether to repaint the map. A
     * script wants the value, so the wrapper unwraps it — returning the
     * envelope would push an object where the stack expects an int, and every
     * comparison against it would be false. */
    const ask = (host, kind, payload) =>
        handleWorldMapRequest(host.worldMap, { kind, ...payload }).result;

    /* No arguments, no results. */
    for( const name of ['worldmap_init', 'worldmap_resetmaxflashcount',
        'worldmap_resetcyclesperflash', 'worldmap_stopcurrentflashes'] )
        proto[name] = function () {
            this.calls++;
            ask(this, name.toUpperCase(), {});
        };

    /* No arguments, one or more results. */
    for( const name of ['worldmap_getzoom', 'worldmap_isloaded',
        'worldmap_getdisplayposition', 'worldmap_getdisplaycoord_current',
        'worldmap_getcurrentmap', 'worldmap_getsize', 'worldmap_getdisableelements',
        'worldmap_listelement_start', 'worldmap_listelement_next',
        'worldmap_element', 'worldmap_elementcoord1', 'worldmap_elementcoord'] )
        proto[name] = function () {
            this.calls++;
            return ask(this, name.toUpperCase(), {});
        };

    /* One integer argument; the service reads it under several names, so the
     * payload states all of the ones that member accepts. */
    const ONE_ARG = {
        worldmap_getmapname: 'mapId',
        worldmap_setmap: 'mapId',
        worldmap_setzoom: 'zoom',
        worldmap_jumptodisplaycoord: 'coord',
        worldmap_jumptodisplaycoord_instant: 'coord',
        worldmap_jumptosourcecoord: 'coord',
        worldmap_jumptosourcecoord_instant: 'coord',
        worldmap_getconfigorigin: 'mapId',
        worldmap_getconfigsize: 'mapId',
        worldmap_getconfigbounds: 'mapId',
        worldmap_getconfigzoom: 'mapId',
        worldmap_getdisplaycoord: 'coord',
        worldmap_getsourcecoord: 'coord',
        worldmap_getmap: 'mapId',
        worldmap_setmaxflashcount: 'count',
        worldmap_setcyclesperflash: 'cycles',
        worldmap_perpetualflash: 'enabled',
        worldmap_flashelement: 'element',
        worldmap_flashelementcategory: 'category',
        worldmap_disableelements: 'disabled',
        worldmap_getdisableelement: 'element',
        worldmap_getdisableelementcategory: 'category',
    };
    for( const [name, field] of Object.entries(ONE_ARG) )
        proto[name] = function (value) {
            this.calls++;
            return ask(this, name.toUpperCase(), { [field]: value, 0: value });
        };

    /* Two arguments. */
    proto.worldmap_jumptomap = function (mapId, coord) {
        this.calls++;
        return ask(this, 'WORLDMAP_JUMPTOMAP', { mapId, coord, 0: mapId, 1: coord });
    };
    proto.worldmap_jumptomap_instant = function (mapId, coord) {
        this.calls++;
        return ask(this, 'WORLDMAP_JUMPTOMAP_INSTANT', { mapId, coord, 0: mapId, 1: coord });
    };
    proto.worldmap_coordinmap = function (mapId, coord) {
        this.calls++;
        return ask(this, 'WORLDMAP_COORDINMAP', { mapId, coord, 0: mapId, 1: coord });
    };
    proto.worldmap_disableelement = function (element, disabled) {
        this.calls++;
        return ask(this, 'WORLDMAP_DISABLEELEMENT',
            { element, disabled, 0: element, 1: disabled });
    };
    proto.worldmap_disableelementcategory = function (category, disabled) {
        this.calls++;
        return ask(this, 'WORLDMAP_DISABLEELEMENTCATEGORY',
            { category, disabled, 0: category, 1: disabled });
    };
    /* Takes a POSITION, not a map: the nearest icon to a display point. */
    proto.worldmap_getnearesticon = function (x, y) {
        this.calls++;
        return ask(this, 'WORLDMAP_GETNEARESTICON', { x, y, 0: x, 1: y });
    };
}



/* -------------------------------------------------------------------------
 * The loot tracker
 * ---------------------------------------------------------------------- */

/**
 * Install the loot family over `host_loot.js`.
 *
 * The service takes `{ kind, int_args, name }`, which is the generated
 * request's own shape: ints in one array, the single string beside it. The
 * mapping is therefore about which POSITION each argument takes in that
 * array, and that is the part worth writing out — `loot_aux_lookup` has its
 * string SECOND with two ints after it that the store deliberately ignores,
 * and a naive "ints then strings" flattening would put the kind where an
 * ignored argument goes.
 */
export function installLootOps(HostKernel) {
    const proto = HostKernel.prototype;

    const ask = (host, kind, intArgs = [], name = '') =>
        handleLootRequest(host.loot, { kind, int_args: intArgs, name }).result;

    /* Every member, by the shape of its arguments. `i` an int, `s` the string;
     * the string travels beside the ints rather than in them. */
    const LOOT_METHODS = {
        loot_aux_upsert2: 'is',
        loot_aux_upsert: 'isi',
        loot_aux_remove: 'isi',
        loot_aux_get: 'ii',
        loot_aux_count: 'i',
        loot_aux_lookup: 'isii',
        loot_aux_clear: 'i',
        loot_aux_count_total: '',
        loot_source_count: '',
        loot_source_name: 'i',
        loot_source_name2: 'i',
        loot_source_itemcount: 's',
        loot_source_totalval: 's',
        loot_begin_query: 'iii',
        loot_query_id: 'i',
        loot_row_count_byname: 's',
        loot_row_count_byid: 'i',
        loot_row_byname: 'si',
        loot_row_byid: 'ii',
        loot_clear_all: '',
        loot_clear_source: 's',
        loot_remove_byid: 'i',
        loot_ignore_add: 's',
        loot_ignore_remove: 's',
        loot_ignore_clear: '',
        loot_ground_count: '',
        loot_ground_name: 'i',
        loot_source_ignore_add: 's',
        loot_source_ignore_remove: 's',
        loot_srclist_count: '',
        loot_srclist_name: 'i',
        loot_add: 'siii',
    };

    for( const [method, shape] of Object.entries(LOOT_METHODS) )
    {
        const kind = method.toUpperCase();
        proto[method] = function (...args) {
            this.calls++;
            const ints = [];
            let name = '';
            /* Positions are kept: an int that comes AFTER the string still
             * lands in the slot its index says, because the store reads
             * `int_args[2]` by number and not by order of appearance. */
            shape.split('').forEach((code, index) => {
                if( code === 's' ) name = args[index] == null ? '' : String(args[index]);
                else ints[index] = args[index] | 0;
            });
            return ask(this, kind, [...ints].map((value) => value ?? 0), name);
        };
    }
}

/* -------------------------------------------------------------------------
 * Entity overlays
 * ---------------------------------------------------------------------- */

/**
 * Install the overlay family over `host_overlay.js`.
 *
 * The service resolves its SUBJECT through adapters — which npc, which loc,
 * which player the request is about — because the CS2 side never names one:
 * the acting subject is whatever the minimenu latched. A preview has no world,
 * so the adapters answer nothing and every create returns -1, which is the
 * reference's own answer when the subject has gone.
 */
export function installOverlayOps(HostKernel) {
    const proto = HostKernel.prototype;

    /*
     * `[surface arity, service arity]`.
     *
     * They differ for the two `_get` forms that take no stack argument: the
     * service was written against the C request, which carries the overlay
     * SLOT in a field the bytecode leaves implicit at zero. Padding the short
     * side is right; passing the wrong count makes `requestArgs` reject the
     * whole request as malformed and the answer comes back `null` — which is
     * neither -1 nor an index, and every comparison against it is false.
     */
    const OVERLAY_METHODS = {
        overlay_npc_create: [5, 5], overlay_loc_create: [5, 5],
        overlay_player_create: [5, 5], overlay_coord_create: [6, 6],
        overlay_npc_get: [0, 1], overlay_loc_get: [1, 1],
        overlay_player_get: [0, 1], overlay_coord_get: [2, 2],
        overlay_npc_destroy: [1, 1], overlay_loc_destroy: [1, 1],
        overlay_player_destroy: [1, 1], overlay_coord_destroy: [2, 2],
    };

    /*
     * The four component-side members. `overlay_cc_create` builds a widget
     * inside an overlay record and `overlay_find` selects one; they take an
     * overlay HANDLE where the rest take a subject, which is why they are not
     * in the table above.
     */
    const OVERLAY_COMPONENT_METHODS = {
        overlay_cc_create: 3, overlay_cc_deleteall: 1,
        overlay_find: 1, overlay_cc_find: 2,
    };
    for( const [method, argCount] of Object.entries(OVERLAY_COMPONENT_METHODS) )
    {
        const kind = method.toUpperCase();
        const call = function (...args) {
            this.calls++;
            const overlayArgs = Array.from({ length: argCount },
                (unused, index) => args[index] | 0);
            const answer = handleOverlayRequest(
                this.overlays, kind, { args: overlayArgs, arg_count: argCount },
                this.overlayAdapters ?? {});
            return answer.value === null ? -1 : answer.value;
        };
        proto[method] = call;
        if( method === 'overlay_cc_find' ) proto.dot_overlay_cc_find = call;
    }

    for( const [method, [, serviceArity]] of Object.entries(OVERLAY_METHODS) )
    {
        const kind = method.toUpperCase();
        proto[method] = function (...args) {
            this.calls++;
            const overlayArgs = Array.from({ length: serviceArity },
                (unused, index) => args[index] | 0);
            const answer = handleOverlayRequest(
                this.overlays, kind,
                { args: overlayArgs, arg_count: serviceArity },
                this.overlayAdapters ?? {});
            /* `null` is the service's "malformed"; -1 is its "no such
             * overlay", and only the second is an answer a script can use. */
            return answer.value === null ? -1 : answer.value;
        };
    }
}

export { createWorldMapState, createLootState, createOverlayState };
