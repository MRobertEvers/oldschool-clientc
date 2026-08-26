/*
 * The client-owned state a script reads that is neither a component nor a
 * cache record: arrays, the interface stack, options, the clock, coordinates,
 * randomness, and the operations that reach a system a preview does not have.
 *
 * ------------------------------------------------------------------
 * "A preview does not have a server" is an answer, not an excuse
 * ------------------------------------------------------------------
 *
 * Several of these are honest zeroes. `npc_uid` with no world is -1 because
 * that is what the reference answers with no entity, not because the value is
 * unknown; `coord` is -1 with no local player for the same reason, and zero
 * would be a real tile in the corner of the map that a script comparing
 * against a box could not tell from a position.
 *
 * What is NOT done here is inventing a plausible value. A fabricated player
 * name or friend list makes a panel look right and behave wrongly, and the
 * wrongness surfaces somewhere unrelated. Where there is no honest answer the
 * method still throws.
 */

import { HOST_PARK } from './generated/cs2_host_park.js';
import * as K from './cs2_intrinsics.js';

/**
 * What `clienttype` answers.
 *
 * TEN, which is what `CS2VM2_Op_ClientType` pushes. The cache's own
 * `clienttype` names only go up to 4 (`desktop`, `android`, `ios`,
 * `enhanced`), so 10 looks arbitrary until you read what the scripts do with
 * it: `[clientscript,duel_options_exclamations]` gates on
 * `clienttype = ^clienttype_enhanced | clienttype = 5 | clienttype = 10`, and
 * a whole family of layout scripts hangs off that answer.
 *
 * Zero is not a neutral default here — it is "none of the above". The bank
 * is the case: `script9580` opens with `if (~script100 = 0) { return; }`,
 * `~script100` is that clienttype gate, and with 0 the script returned before
 * unhiding the divider beside the tab strip or building the first tab. Three
 * of bankmain's sixty-six draw commands simply did not exist.
 */
const CLIENTTYPE_DEFAULT = 10;

/** Interface mount types, as `if_openwidget` numbers them. */
export const MOUNT_TYPE = Object.freeze({ MODAL: 0, OVERLAY: 1, TAB: 3 });

/** Where a preview's client-side state lives. */
export class ClientState {
    constructor({
        windowMode = 1, defaultWindowMode = 1, clientType = CLIENTTYPE_DEFAULT,
        options = new Map(), coord = -1, world = 301,
        runEnergy = 100, runWeight = 0, mobile = false,
    } = {}) {
        this.windowMode = windowMode;
        this.defaultWindowMode = defaultWindowMode;
        this.clientType = clientType;
        /* One map for all three option families; the id space is disjoint and
         * the C host keys them the same way. */
        this.options = new Map(options);
        this.coord = coord;
        this.world = world;
        /*
         * `runenergy_visible` / `runweight_visible` read the PLAYER'S VALUE
         * despite their names — `rs_cs2_host.c` answers `stats->run_energy`
         * and `stats->run_weight`, not a visibility flag.
         *
         * A HUNDRED before a server says otherwise, which is what
         * `RS_PlayerStats_Init` seeds: a client that has not logged in has a
         * full energy globe, not an empty one. Zero coloured the minimap's
         * run orb red and picked its empty filler sprite.
         */
        this.runEnergy = runEnergy;
        /** Display toggles a script sets and a renderer reads. */
        this.toggles = {};
        /** Keys currently down / pressed this tick; both polled, not delivered. */
        this.keysHeld = new Set();
        this.keysPressed = new Set();
        this.runWeight = runWeight;
        /* The C client pushes a constant 0 for `on_mobile`; a browser preview
         * on a desktop canvas is the same answer, not a placeholder. */
        this.mobile = mobile;
        /** Mounted sub-interfaces: `parentComponentId -> { group, type }`. */
        this.mounts = new Map();
        /** What a paused server script armed with `if_addresumebutton`. */
        this.pausedComponent = -1;
        /** Highlights the scripts have switched on, by kind and id. */
        this.highlights = new Set();
        /** Every op the preview recorded rather than performed. */
        this.intents = [];
    }
}

export function installClientOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* --------------------------------------------------------------
     * Arrays that are not handle-local
     * ----------------------------------------------------------- */

    /**
     * `array_new(size, type)` — a fresh handle, distinct from `define_array`.
     *
     * `define_array` declares a local; this one returns a handle a script
     * keeps in a variable it already has. Same storage, different statement,
     * which is why the emitter lowers them differently.
     */
    proto.array_new = function (typeCode, length, capacity) {
        this.calls++;
        /* `capacity` is a reserved length the reference does not use for the
         * cells it hands back; `length` is what the array holds. Reading them
         * the other way round makes every `array_new` either empty or huge. */
        const size = Math.max(0, Math.min(length | 0, Math.max(length | 0, capacity | 0)));
        return K.defineArray(size, typeCode === 115 || typeCode === 2 ? 'string' : 'int');
    };

    proto.array_join = function (array, separator) {
        this.calls++;
        return (array ?? []).join(String(separator ?? ''));
    };

    proto.array_split = function (text, separator) {
        this.calls++;
        return String(text ?? '').split(String(separator ?? ''));
    };

    /**
     * `array_count_matches(handle, value, start, end, valueType)`.
     *
     * The RANGE and the TYPE are arguments, not decoration. `valueType` -1
     * means "no value": nothing was pushed and nothing can match, so the
     * answer is zero rather than a count of whatever `undefined` equals. A
     * negative `end` means "to the end", which is how every call site asks for
     * the whole array.
     */
    proto.array_count_matches = function (array, value, start, end, valueType) {
        this.calls++;
        const cells = array ?? [];
        if( (valueType | 0) === -1 ) return 0;
        const first = Math.max(0, start | 0);
        const last = (end | 0) < 0 || (end | 0) > cells.length ? cells.length : end | 0;
        let count = 0;
        for( let i = first; i < last; i++ ) if( cells[i] === value ) count++;
        return count;
    };

    /* --------------------------------------------------------------
     * The interface stack
     * ----------------------------------------------------------- */

    /**
     * `if_openwidget(parent, group, type)` — mount a sub-interface.
     *
     * Parks on the group like every other component operation: the pack has to
     * be in the tree before anything can be mounted from it, and the park test
     * comes before any mutation because the retry re-runs the whole method.
     */
    proto.if_openwidget = function (parentId, groupId, type) {
        this.calls++;
        if( !this.tree.hasGroup(groupId) && !this._awaitSpent('component', groupId) )
            return this._park('component', groupId);
        this.client.mounts.set(parentId, { group: groupId, type });
        return undefined;
    };

    /**
     * `if_close()` — NO arguments, and not a local close.
     *
     * This is what every interface's close button runs: `steelborder` binds op
     * 1 to clientscript 29, whose entire body is `if_close`. The reference
     * sends CLOSE_MODAL and waits for the SERVER to unmount — so unmounting
     * here would show a panel closing that the server still believes is open,
     * and taking a component id would make the script's zero arguments read as
     * "close component undefined".
     */
    proto.if_close = function () {
        this.calls++;
        this.client.intents.push({ intent: 'closeModal' });
        this.onIntent?.('closeModal', {});
    };

    /** The local unmount the page does once a close is accepted. */
    proto.closeMount = function (componentId) {
        /* The tree keeps the pack: re-opening must not re-bake, which is the
         * reference's own behaviour and what makes a tab switch cheap. */
        this.client.mounts.delete(componentId);
    };

    proto.if_hassub_at = function (componentId) {
        this.calls++;
        return this.client.mounts.has(componentId) ? 1 : 0;
    };

    proto.if_getsubid = function (componentId) {
        this.calls++;
        return this.client.mounts.get(componentId)?.group ?? -1;
    };

    /** `if_gettop` — the interface currently mounted as the root modal. */
    proto.if_gettop = function () {
        this.calls++;
        for( const [, mount] of this.client.mounts )
            if( mount.type === MOUNT_TYPE.MODAL ) return mount.group;
        /*
         * -1, not 0. Script 900 maps this to an enum id and answers -1 for a
         * top-level interface it does not know — booting one interface on its
         * own, with no gameframe, is exactly that case, and zero would be a
         * real interface id.
         */
        return -1;
    };

    /* --------------------------------------------------------------
     * Scroll extents
     * ----------------------------------------------------------- */

    proto.if_getscrollwidth = function (id) {
        return this._read(this._target(id), 'scrollWidth', 0);
    };
    proto.if_getscrollheight = function (id) {
        return this._read(this._target(id), 'scrollHeight', 0);
    };
    proto.cc_getscrollwidth = function () {
        return this._read(this.activeNode(), 'scrollWidth', 0);
    };
    proto.cc_getscrollheight = function () {
        return this._read(this.activeNode(), 'scrollHeight', 0);
    };

    /* --------------------------------------------------------------
     * Options
     * ----------------------------------------------------------- */

    /*
     * The three option families share one id space in the C host and are read
     * and written the same way; keeping three maps here would only invite one
     * of them to drift.
     */
    for( const family of ['gameoption', 'deviceoption', 'clientoption'] )
    {
        proto[`${family}_get`] = function (id) {
            this.calls++;
            return this.client.options.get(id | 0) ?? 0;
        };
        proto[`${family}_set`] = function (id, value) {
            this.calls++;
            this.client.options.set(id | 0, value | 0);
        };
    }

    proto.getwindowmode = function () { this.calls++; return this.client.windowMode; };
    proto.setwindowmode = function (mode) { this.calls++; this.client.windowMode = mode | 0; };
    proto.getdefaultwindowmode = function () {
        this.calls++;
        return this.client.defaultWindowMode;
    };
    proto.setdefaultwindowmode = function (mode) {
        this.calls++;
        this.client.defaultWindowMode = mode | 0;
    };

    /**
     * `deviceoption_getrange(option)` — (min, max), in that order.
     *
     * Min is always 0; max is 100 for the MASTER VOLUME (device option 19) and
     * 255 for everything else. The two ranges are not interchangeable: a
     * slider built against 255 for the master volume runs off its own track at
     * 40% of the way along.
     */
    const DEVICEOPTION_MASTER_VOLUME = 19;
    proto.deviceoption_getrange = function (optionId) {
        this.calls++;
        return [0, (optionId | 0) === DEVICEOPTION_MASTER_VOLUME ? 100 : 255];
    };

    /*
     * The three volume channels and the handful of display toggles are DEVICE
     * OPTIONS with their own opcodes — the same storage the `deviceoption_*`
     * family uses, reached by name. Keeping them in one map is what stops the
     * settings panel and the volume slider disagreeing about the same value.
     */
    const NAMED_DEVICE_OPTIONS = {
        volumemusic: 21, volumesounds: 22, volumeareasounds: 23,
        removeroofs: 24, brightness: 25, taptodrop: 26,
        hideusername: 28, rememberusername: 29,
    };
    for( const [name, id] of Object.entries(NAMED_DEVICE_OPTIONS) )
    {
        proto[`set${name}`] = function (value) {
            this.calls++;
            this.client.options.set(id, value | 0);
        };
        proto[`get${name}`] = function () {
            this.calls++;
            return this.client.options.get(id) ?? 0;
        };
    }

    /* Client toggles with no getter: a script sets them and the renderer
     * reads them, so they are stored under their own names rather than
     * discarded. */
    for( const [method, field] of Object.entries({
        setantidrag: 'antiDrag',
        setminimaplock: 'minimapLocked',
        setmousecam: 'mouseCam',
        setshiftclickdrop: 'shiftClickDrop',
        setshowmouseovertext: 'showMouseOverText',
        setshowmousecross: 'showMouseCross',
        setshowloadingmessages: 'showLoadingMessages',
        renderself: 'renderSelf',
        setfolloweropslowpriority: 'followerOpsLowPriority',
    }) )
        proto[method] = function (value) {
            this.calls++;
            this.client.toggles[field] = !!value;
        };

    /*
     * A key the pointer never pressed. Both answer 0 with no keyboard state,
     * and that is the reference's answer between key events too — these are
     * polled, not delivered.
     */
    proto.keyheld = function (key) { this.calls++; return this.client.keysHeld.has(key | 0) ? 1 : 0; };
    proto.keypressed = function (key) {
        this.calls++;
        return this.client.keysPressed.has(key | 0) ? 1 : 0;
    };

    /* --------------------------------------------------------------
     * Coordinates
     * ----------------------------------------------------------- */

    /**
     * `coord` — the local player's tile, packed.
     *
     * MINUS ONE with no local player, which is the reference's own answer.
     * Zero is a real tile in the corner of the map, and a script comparing
     * against a box cannot tell it from a position — which is how "the player
     * is in the north-west corner" becomes a branch nobody meant to take.
     */
    proto.coord = function () { this.calls++; return this.client.coord; };

    proto.coordx = function (coord) { this.calls++; return unpackCoord(coord).x; };
    proto.coordy = function (coord) { this.calls++; return unpackCoord(coord).y; };
    proto.coordz = function (coord) { this.calls++; return unpackCoord(coord).level; };

    /*
     * Named "visible", answers the VALUE. `rs_cs2_host.c` pushes
     * `stats->run_energy` and `stats->run_weight`, and the gameframe prints
     * the weight with "kg" after it — a 0/1 there would read as "0kg".
     */
    proto.runenergy_visible = function () { this.calls++; return this.client.runEnergy; };
    proto.runweight_visible = function () { this.calls++; return this.client.runWeight; };

    /* Constant 0 in the reference, and the same answer here for the same
     * reason: this is not a mobile client. */
    proto.on_mobile = function () { this.calls++; return this.client.mobile ? 1 : 0; };

    proto.map_world = function () { this.calls++; return this.client.world; };
    proto.clienttype = function () { this.calls++; return this.client.clientType; };

    /**
     * `fromdate(runeday)` — a date string from a day count.
     *
     * Day 0 is 1 January 2002, the epoch the game's own timestamps are
     * measured from; the format is the reference's `d MMMM yyyy`. Computed
     * rather than fetched, so it needs no clock — which matters, because a
     * preview that formatted "today" would render differently every day and
     * could not be compared against anything.
     */
    const RUNEDAY_EPOCH_UTC = Date.UTC(2002, 0, 1);
    const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
        'August', 'September', 'October', 'November', 'December'];

    proto.fromdate = function (runeday) {
        this.calls++;
        const date = new Date(RUNEDAY_EPOCH_UTC + (runeday | 0) * 86400000);
        return `${date.getUTCDate()} ${MONTHS[date.getUTCMonth()]} ${date.getUTCFullYear()}`;
    };

    /*
     * The pre-rev-634 varc-string pair. The id travels in the OPCODE OPERAND,
     * not on the stack, which is why the surface lists no arguments for the
     * reader and one for the writer — and why both address varc-string slot
     * 0 here: nothing in this cache emits them, and inventing an operand
     * channel for two dead opcodes would be a channel nothing tests.
     */
    proto.push_varc_string_old = function () {
        this.calls++;
        return this.state.varcString(0);
    };
    proto.pop_varc_string_old = function (value) {
        this.calls++;
        this.state.setVarcString(0, value);
    };

    /* --------------------------------------------------------------
     * Randomness and time
     * ----------------------------------------------------------- */

    /**
     * `random(n)` — 0..n-1.
     *
     * Seeded and injectable, because a preview that renders differently on
     * every reload cannot be compared against anything. The C client uses the
     * platform generator; reproducibility matters more here than matching its
     * particular sequence, which nothing observes.
     */
    proto.random = function (bound) {
        this.calls++;
        return bound > 0 ? this.client.random(bound) : 0;
    };

    proto.randominc = function (bound) {
        this.calls++;
        return bound > 0 ? this.client.random(bound + 1) : 0;
    };

    /* --------------------------------------------------------------
     * Operations that reach a system a preview does not have
     * ----------------------------------------------------------- */

    /*
     * Recorded, not performed, and not faked. `if_triggeroplocal` clicks a
     * component in the real client and `resume_countdialog` answers a paused
     * server script; a preview has neither, so the intent is written down and
     * the page is told. Answering as though it happened is what makes a panel
     * look right and behave wrongly.
     */
    const RECORDED = {
        if_triggeroplocal: ['triggerOpLocal', ['component', 'subId']],
        if_triggerop: ['triggerOp', ['component', 'op']],
        cc_triggerop: ['triggerOp', ['op']],
        resume_countdialog: ['resumeCount', ['value']],
        resume_namedialog: ['resumeName', ['name']],
        resume_stringdialog: ['resumeString', ['text']],
        resume_objdialog: ['resumeObj', ['obj']],
        cc_resume_pausebutton: ['resumePause', []],
        if_resume_pausebutton: ['resumePause', ['component']],
        bug_report: ['bugReport', ['text']],
        setidlepopup: ['idlePopup', ['text']],
        /* A cheat is a SERVER command. Running it locally would be inventing
         * a result; recording it lets a dev page offer to send it. */
        docheat: ['cheat', ['text']],
        sound_song_withsecondary: ['song',
            ['id', 'secondary', 'fadeOut', 'fadeIn', 'a', 'b']],
    };
    for( const [method, [intent, fields]] of Object.entries(RECORDED) )
    {
        proto[method] = function (...args) {
            this.calls++;
            const payload = {};
            fields.forEach((field, index) => { payload[field] = args[index]; });
            this.client.intents.push({ intent, ...payload });
            this.onIntent?.(intent, payload);
        };
        /* `.cc_resume_pausebutton` is the only dot form in this group, and it
         * records the same intent: which cursor selected the component does
         * not change what the server is being told. */
        if( method.startsWith('cc_') ) proto[`dot_${method}`] = proto[method];
    }

    /* Highlights are client state a script switches on and off and reads back;
     * modelling them is cheap and refusing them would break the tutorial. */
    /*
     * The highlight family takes the SUBJECT and where to look for it — an
     * npc uid with a coord, a loc id with a coord and a shape. The extra
     * arguments are what make two of the same npc distinguishable, so the key
     * carries them; a key built from the id alone switches off a highlight the
     * script did not ask about.
     */
    for( const [method, kind, on] of [
        ['highlight_npc_on', 'npc', true], ['highlight_npc_off', 'npc', false],
        ['highlight_loc_on', 'loc', true], ['highlight_loc_off', 'loc', false],
    ] )
    {
        proto[method] = function (...args) {
            this.calls++;
            const key = `${kind}:${args.map((value) => String(value)).join(':')}`;
            if( on ) this.client.highlights.add(key);
            else this.client.highlights.delete(key);
        };
    }
    proto.highlight_clear = function () { this.calls++; this.client.highlights.clear(); };

    /* --------------------------------------------------------------
     * Live-world answers a preview cannot give
     * ----------------------------------------------------------- */

    /*
     * These have no honest answer without a world, and the reference's own
     * "nothing there" value is the right one — a preview with no entities
     * genuinely has no npc under the cursor.
     */
    proto.npc_uid = function () { this.calls++; return -1; };
    proto.npc_creationcycle = function () { this.calls++; return -1; };
    proto.player_uid = function () { this.calls++; return -1; };
}

/**
 * A packed coord: `(level << 28) | (x << 14) | y`.
 *
 * -1 means "no coord", and its parts are -1 rather than the bits of -1 —
 * `coordx(-1)` answering 16383 would put the player at the map's edge.
 */
export function unpackCoord(coord) {
    const value = coord | 0;
    if( value < 0 ) return { level: -1, x: -1, y: -1 };
    return {
        level: (value >> 28) & 0x3,
        x: (value >> 14) & 0x3fff,
        y: value & 0x3fff,
    };
}

export function packCoord(level, x, y) {
    return ((level & 0x3) << 28) | ((x & 0x3fff) << 14) | (y & 0x3fff);
}

/**
 * A deterministic generator.
 *
 * xorshift32, seeded: a preview that renders differently on every reload
 * cannot be compared against a reference, and the C client's particular
 * sequence is not something any script observes.
 */
export function createRandom(seed = 0x2545f491) {
    let state = seed >>> 0 || 1;
    return (bound) => {
        state ^= state << 13; state >>>= 0;
        state ^= state >>> 17;
        state ^= state << 5; state >>>= 0;
        return state % bound;
    };
}

export { HOST_PARK };
