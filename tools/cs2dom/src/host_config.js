/*
 * The cache tables a script reads: enums, structs, params, objects, inventories,
 * stats.
 *
 * Ported handler by handler from `src/game/rs_cs2_host.c`, and the part worth
 * porting carefully is not the hit — it is the MISS. Every one of these
 * lookups answers something specific when the record is absent, the key is
 * unknown, or the id is negative, and those answers are load-bearing:
 *
 *   - an enum lookup that misses pushes -1, and scripts feed that straight
 *     into `struct_param`, so struct -1 must be a valid input rather than a
 *     load;
 *   - a string-output enum answers `"null"`, which is what the text a widget
 *     draws will literally say if the id is wrong — a silent "" would look
 *     like a layout bug instead;
 *   - a param that a struct does not carry answers the ParamType's default,
 *     which is what the scripts' `= -1` guards are testing for;
 *   - `inv_size` reads the immutable inventory TYPE, not the live container,
 *     because a script asks before the container exists and a runtime miss
 *     says nothing about the answer. Returning the live length instead once
 *     teleported every login into the Gauntlet.
 *
 * A negative id is never awaited. There is no archive behind it, and the C
 * planner asserts on a load for one.
 */

import { HOST_PARK } from './generated/cs2_host_park.js';

/**
 * The cache tables, in the shape `host_data.js` already parses them into.
 *
 * Absent tables are not an error here — a preview may be opened against a
 * cache that has no DB, and the miss answers above are defined for that. What
 * IS an error is a table that is present and disagrees with the C host, which
 * is what the tests pin.
 */
export class HostConfig {
    constructor({
        enums = {}, structs = {}, params = {}, objects = {}, npcs = {}, locs = {},
        inventories = {}, mapElements = {},
    } = {}) {
        this.enums = enums;
        this.structs = structs;
        this.params = params;
        this.objects = objects;
        this.npcs = npcs;
        this.locs = locs;
        this.inventories = inventories;
        this.mapElements = mapElements;
    }

    has(kind, id) {
        const table = this[kind];
        return !!table && Object.prototype.hasOwnProperty.call(table, String(id));
    }

    get(kind, id) {
        const table = this[kind];
        return table ? table[String(id)] : undefined;
    }
}

/** Live inventory contents and stat levels — server state, not cache tables. */
export class HostPlayerState {
    constructor({ inventories = new Map(), stats = new Map() } = {}) {
        /** inv id -> [{ obj, count }], sparse by slot. */
        this.inventories = new Map(inventories);
        /** stat index -> { level, base, xp }. */
        this.stats = new Map(stats);
    }

    slots(invId) { return this.inventories.get(invId) ?? []; }

    stat(index) { return this.stats.get(index) ?? { level: 0, base: 0, xp: 0 }; }
}

/**
 * Install the config-backed operations onto a kernel prototype.
 *
 * Written as an installer rather than as more methods on HostKernel so the
 * kernel file stays about the tree. Each method here is one C handler.
 */
export function installConfigOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* --------------------------------------------------------------
     * Enums
     * ----------------------------------------------------------- */

    /**
     * `enum(inputType, outputType, enumId, key)`.
     *
     * The output type is the CHARACTER code the bytecode carried, and it wins
     * over the enum's own declaration: a script asking for a string gets a
     * string even from an int enum, because the stack shape is decided by the
     * opcode and not by the data.
     */
    proto.enum = function (inputType, outputType, enumId, key) {
        this.calls++;
        const record = this.config.get('enums', enumId);
        if( !record )
        {
            if( enumId >= 0 && !this._awaitSpent('enum', enumId) )
                return this._park('enum', enumId);
            return outputType === CHAR_S ? 'null' : -1;
        }
        if( outputType === CHAR_S || record.string )
        {
            const value = record.values[String(key)];
            return value === undefined ? (record.defaultString ?? 'null') : String(value);
        }
        /* A string-output enum asked for an int has no int to give. The C
         * handler answers -1 rather than coercing, and a script comparing
         * against a real id can tell -1 from a wrong number. */
        if( record.string ) return -1;
        const value = record.values[String(key)];
        return value === undefined ? (record.defaultInt ?? 0) : value | 0;
    };

    /**
     * `enum_string(enum, key)` — the same lookup, forced to a STRING result.
     *
     * A separate opcode rather than a flag on `enum`, because the stack shape
     * is decided by the opcode: this one always leaves a string, whatever the
     * enum's own declared output type says.
     */
    proto.enum_string = function (enumId, key) {
        return this.enum(0, CHAR_S, enumId, key);
    };

    proto.enum_getoutputcount = function (enumId) {
        this.calls++;
        const record = this.config.get('enums', enumId);
        if( !record )
        {
            if( enumId >= 0 && !this._awaitSpent('enum', enumId) )
                return this._park('enum', enumId);
            return 0;
        }
        return Object.keys(record.values).length;
    };

    /* --------------------------------------------------------------
     * Structs and params
     * ----------------------------------------------------------- */

    /**
     * `struct_param(struct, param)`.
     *
     * Both records are wanted and ONE park loads both: the struct carries the
     * value, the ParamType decides string-versus-int and supplies the default
     * the struct may omit. Struct -1 is a legitimate input — an enum lookup
     * that missed — so it falls through to the param default rather than being
     * awaited.
     */
    proto.struct_param = function (structId, paramId) {
        this.calls++;
        const record = this.config.get('structs', structId);
        const param = this.config.get('params', paramId);
        if( (!record && structId >= 0) || (!param && paramId >= 0) )
        {
            if( !this._awaitSpent('struct', structId, paramId) )
                return this._park('struct', structId, paramId);
            /* Still missing after the load: complete with what did arrive. */
        }
        return paramValue(record?.params, param, paramId);
    };

    proto.oc_param = function (objId, paramId) {
        this.calls++;
        const record = this.config.get('objects', objId);
        const param = this.config.get('params', paramId);
        /* obj -1 is a bank's empty slot, a valid input the scripts pass. */
        if( (!record && objId >= 0) || (!param && paramId >= 0) )
        {
            if( !this._awaitSpent('obj', objId, paramId) )
                return this._park('obj', objId, paramId);
        }
        return paramValue(record?.params, param, paramId);
    };

    proto.nc_param = function (npcId, paramId) {
        this.calls++;
        const record = this.config.get('npcs', npcId);
        const param = this.config.get('params', paramId);
        if( (!record && npcId >= 0) || (!param && paramId >= 0) )
        {
            if( !this._awaitSpent('npc', npcId, paramId) )
                return this._park('npc', npcId, paramId);
        }
        return paramValue(record?.params, param, paramId);
    };

    proto.lc_param = function (locId, paramId) {
        this.calls++;
        const record = this.config.get('locs', locId);
        const param = this.config.get('params', paramId);
        if( (!record && locId >= 0) || (!param && paramId >= 0) )
        {
            if( !this._awaitSpent('loc', locId, paramId) )
                return this._park('loc', locId, paramId);
        }
        return paramValue(record?.params, param, paramId);
    };

    /* --------------------------------------------------------------
     * Objects
     * ----------------------------------------------------------- */

    /*
     * `oc_name` and friends answer for obj -1 WITHOUT loading.
     *
     * A bank's empty slot is obj -1 and scripts ask about it freely. Awaiting
     * a negative id asserts in the C planner, so the guard comes first and the
     * answer is the reference's own: "null", zero, the identity.
     */
    const OBJECT_FIELDS = {
        oc_name: ['name', 'null'],
        oc_cost: ['cost', 0],
        oc_stackable: ['stackable', 0],
        oc_members: ['members', 0],
    };
    for( const [method, [field, absent]] of Object.entries(OBJECT_FIELDS) )
    {
        proto[method] = function (objId) {
            this.calls++;
            if( objId < 0 ) return absent;
            const record = this.config.get('objects', objId);
            if( !record )
            {
                if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
                return absent;
            }
            const value = record[field];
            if( value === undefined ) return absent;
            return typeof value === 'boolean' ? (value ? 1 : 0) : value;
        };
    }

    /*
     * `oc_placeholder` / `oc_unplaceholder` — the bank-placeholder form of an
     * obj and back.
     *
     * ONE function, both directions, because the cache states the linkage once
     * and reading it twice is how the two answers drift. An ITEM states a link
     * and no template; a PLACEHOLDER states both and its link is the item.
     *
     * Either direction answers the INPUT id when there is no other form, and
     * that is not a fallback — it is what makes `oc_unplaceholder($obj) ! $obj`
     * the client's "is this slot a placeholder" test (script 278,
     * `bankmain_drawitem`). Answering -1 for "no other form" would report
     * every ordinary item as a placeholder.
     */
    for( const [method, wantPlaceholder] of [['oc_placeholder', true], ['oc_unplaceholder', false]] )
    {
        proto[method] = function (objId) {
            this.calls++;
            /* obj -1 is an empty slot, a valid script input; the yield planner
             * asserts on a negative id, so it never loads for one. */
            if( objId < 0 ) return objId;
            const record = this.config.get('objects', objId);
            if( !record )
            {
                if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
                return objId;
            }
            if( (record.placeholderLink ?? 0) > 0 )
            {
                const isPlaceholder = (record.placeholderTemplate ?? -1) >= 0;
                if( isPlaceholder !== wantPlaceholder ) return record.placeholderLink;
            }
            return objId;
        };
    }

    /*
     * `oc_cert` / `oc_uncert` — the NOTE form of an obj and back.
     *
     * The same two-field linkage as the placeholder pair, and read the same
     * way: an item states a link and no template, a note states both and its
     * link is the item. Either direction answers the INPUT id when there is no
     * other form, which is what makes `oc_cert($obj) ! $obj` the client's "can
     * this be noted" test.
     */
    for( const [method, wantCert] of [['oc_cert', true], ['oc_uncert', false]] )
    {
        proto[method] = function (objId) {
            this.calls++;
            if( objId < 0 ) return objId;
            const record = this.config.get('objects', objId);
            if( !record )
            {
                if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
                return objId;
            }
            if( (record.certLink ?? 0) > 0 )
            {
                const isCert = (record.certTemplate ?? -1) >= 0;
                if( isCert !== wantCert ) return record.certLink;
            }
            return objId;
        };
    }

    /**
     * `oc_op(obj, index)` / `oc_iop(obj, index)` — the GROUND and INVENTORY
     * right-click actions.
     *
     * The index is ZERO-based here, unlike `cc_getop`'s: the C reads
     * `ground_actions[op_index]` straight. Two different conventions in one
     * codebase is unpleasant and it is the reference's, so it is written down
     * rather than quietly normalised.
     */
    for( const [method, field] of [['oc_op', 'ops'], ['oc_iop', 'invOps']] )
    {
        proto[method] = function (objId, index) {
            this.calls++;
            if( objId < 0 ) return '';
            const record = this.config.get('objects', objId);
            if( !record )
            {
                if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
                return '';
            }
            const slot = index | 0;
            const ops = record[field];
            if( !ops || slot < 0 || slot >= ops.length ) return '';
            return ops[slot] ?? '';
        };
    }

    proto.oc_examine = function (objId) {
        this.calls++;
        if( objId < 0 ) return '';
        const record = this.config.get('objects', objId);
        if( !record )
        {
            if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
            return '';
        }
        return record.examine ?? '';
    };

    /*
     * Equip slots and weight. -1 is "not equippable" and 0 is a real slot, so
     * an obj with no `wearpos` cannot answer zero; weight is in the cache's
     * own units and 0 is a legitimate weight.
     */
    for( const [method, field, absent] of [
        ['oc_wearpos', 'wearpos', -1], ['oc_wearpos2', 'wearpos2', -1],
        ['oc_wearpos3', 'wearpos3', -1], ['oc_weight', 'weight', 0],
    ] )
    {
        proto[method] = function (objId) {
            this.calls++;
            if( objId < 0 ) return absent;
            const record = this.config.get('objects', objId);
            return record ? (record[field] ?? absent) : absent;
        };
    }

    /**
     * `oc_shiftclickiop(obj)` — which inventory op a shift-click runs, ONE-BASED.
     *
     * Three states, and the third is the one that matters:
     *
     *   >= 0  the obj names its slot — but only if that slot has an op
     *   -1    the obj opted out
     *   -2    UNSTATED, and then the rule is "Drop, if it is the fifth op"
     *
     * Almost nothing states the field, so the -2 branch is what actually
     * drives shift-drop across the game. The answer is `index + 1` because
     * script6012 feeds it to `cc_getop`, which is one-based; returning the
     * zero-based index promotes every op one slot too early.
     */
    proto.oc_shiftclickiop = function (objId) {
        this.calls++;
        if( objId < 0 ) return -1;
        const record = this.config.get('objects', objId);
        if( !record )
        {
            if( !this._awaitSpent('obj', objId) ) return this._park('obj', objId);
            return -1;
        }
        const ops = record.invOps ?? [];
        const stated = record.shiftClickDrop ?? -2;
        let index = -1;
        if( stated >= 0 )
            index = stated < ops.length && ops[stated] ? stated : -1;
        else if( stated === -2 )
            index = (ops[ops.length - 1] ?? '').toLowerCase() === 'drop' ? ops.length - 1 : -1;
        return index < 0 ? -1 : index + 1;
    };

    /*
     * `oc_isubop(obj, op, sub)` — a submenu under an inventory op. Nothing in
     * the cache's obj record carries one, and the reference answers the empty
     * string for exactly that reason.
     */
    proto.oc_isubop = function (objId, opIndex, subIndex) { this.calls++; return ''; };

    /**
     * `oc_find(query, members)` / `oc_findnext` / `oc_findreset` — a STATEFUL
     * name search.
     *
     * `find` scans every object for a lower-cased substring and answers the
     * COUNT; `findnext` walks the matches in ascending id order and answers -1
     * when they run out; `findreset` clears them. The cursor lives on the host
     * because the three opcodes are three separate calls, and a search that
     * restarted on every `findnext` would return the first match forever.
     */
    proto.oc_find = function (query, members) {
        this.calls++;
        this.objSearch = { results: [], index: 0 };
        const text = String(query ?? '').toLowerCase();
        if( !text ) return 0;
        const table = this.config.objects ?? {};
        const results = [];
        for( const key of Object.keys(table) )
        {
            const name = table[key].name;
            if( !name || name === 'null' ) continue;
            if( !name.toLowerCase().includes(text) ) continue;
            /* `members` filters to member items when set; a free world asks
             * for everything, which is what 0 means. */
            if( members && !table[key].members ) continue;
            results.push(Number(key));
        }
        results.sort((a, b) => a - b);
        this.objSearch = { results, index: 0 };
        return results.length;
    };

    proto.oc_findnext = function () {
        this.calls++;
        const search = this.objSearch;
        if( !search || search.index >= search.results.length ) return -1;
        return search.results[search.index++];
    };

    proto.oc_findreset = function () {
        this.calls++;
        this.objSearch = { results: [], index: 0 };
    };

    /* --------------------------------------------------------------
     * Inventories
     * ----------------------------------------------------------- */

    /**
     * `inv_size(inv)` — the inventory TYPE's capacity.
     *
     * Not the live container's used length. A script asks before
     * UPDATE_INV_FULL has created the container (both the inventory and
     * equipment onLoads do), so a runtime miss says nothing about the answer,
     * and answering with the used prefix instead makes every slot past it
     * invisible.
     */
    proto.inv_size = function (invId) {
        this.calls++;
        if( invId < 0 ) return 0;
        const record = this.config.get('inventories', invId);
        if( record === undefined )
        {
            if( !this._awaitSpent('inv', invId) ) return this._park('inv', invId);
            return 0;
        }
        return typeof record === 'number' ? record : (record.size ?? 0);
    };

    proto.inv_getobj = function (invId, slot) {
        this.calls++;
        const entry = this.player.slots(invId)[slot | 0];
        return entry ? entry.obj : -1;
    };

    proto.inv_getnum = function (invId, slot) {
        this.calls++;
        const entry = this.player.slots(invId)[slot | 0];
        return entry ? entry.count : 0;
    };

    /** How many of `obj` the container holds, across every slot. */
    proto.inv_total = function (invId, objId) {
        this.calls++;
        let total = 0;
        for( const entry of this.player.slots(invId) )
            if( entry && entry.obj === objId ) total += entry.count;
        return total;
    };

    /* --------------------------------------------------------------
     * Stats
     * ----------------------------------------------------------- */

    proto.stat = function (index) { this.calls++; return this.player.stat(index).level; };
    proto.stat_base = function (index) { this.calls++; return this.player.stat(index).base; };
    proto.stat_xp = function (index) { this.calls++; return this.player.stat(index).xp; };

    /* --------------------------------------------------------------
     * World facts a preview can answer honestly
     * ----------------------------------------------------------- */

    /* The preview is not a member's world unless it is told it is; `false` is
     * a real answer, not a placeholder, and scripts branch on it. */
    proto.map_members = function () { this.calls++; return this.world.members ? 1 : 0; };
    proto.map_world = function () { this.calls++; return this.world.world; };
    proto.clienttype = function () { this.calls++; return this.world.clientType; };
}

const CHAR_S = 's'.charCodeAt(0);

/**
 * One param read, shared by struct/obj/npc/loc.
 *
 * The ParamType decides the STACK, and the record decides the value. A string
 * param always answers a string (the record's, or the type's default, or "");
 * an int param answers the record's int or the type's default, and 0 when
 * there is no type at all.
 */
function paramValue(params, param, paramId) {
    const found = params ? params[String(paramId)] : undefined;
    if( param && param.string )
        return found !== undefined ? String(found) : (param.defaultString ?? '');
    if( found !== undefined )
        return typeof found === 'string' ? found : found | 0;
    return param ? (param.defaultInt ?? 0) : 0;
}
