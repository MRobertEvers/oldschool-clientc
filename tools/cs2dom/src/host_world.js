/*
 * The world the interface talks ABOUT: highlights, the right-click menu, the
 * camera and viewport, the social lists, and the client operations a script
 * installs on entities.
 *
 * ------------------------------------------------------------------
 * Three kinds of answer, and only three
 * ------------------------------------------------------------------
 *
 * 1. STATE THE PREVIEW OWNS. Highlights, camera angles, ui zoom, the safe
 *    area, the pointer. A script switches these on and reads them back, and
 *    modelling them is what makes a panel behave. They are stored.
 *
 * 2. THE REFERENCE'S OWN "NOTHING". No world means no npc under the cursor
 *    and no menu open, and `-1` / `0` / `""` are what the C client answers in
 *    exactly that situation — not placeholders. The values here were read off
 *    `rs_cs2_host.c`, including the ones that are surprising: `safearea` is
 *    the whole canvas, `uizoom_getdefault` is 100 and not 1000 (the reset
 *    button feeds it straight into the interface-scale option, whose clamp
 *    turns 1000 into 400%), and the viewport zoom starts at 256 on both ends
 *    because a script can read it before any `setfov` has run.
 *
 * 3. RECORDED, NOT PERFORMED. Adding a friend, cancelling a notification,
 *    forcing the camera. A preview has no server and no scene; writing down
 *    what the script asked for is honest, and answering as though it happened
 *    makes a panel look right and behave wrongly.
 *
 * What is NOT here is a fabricated friends list or a plausible hiscore. An
 * invented name resolves to a real player somewhere and a script will act on
 * it; an empty list is a state the reference reaches every time someone plays
 * with no friends online.
 */

/** `minimenu_type`: the acting row's subject kind. */
export const MINIMENU_TYPE = Object.freeze({
    NONE: 0, NPC: 1, LOC: 2, OBJ: 3, PLAYER: 4, COMPONENT: 7,
});

/**
 * The interface-scale reset value.
 *
 * 100, not 1000. Script 3334 ("Reset interface scaling") is the only caller
 * and its whole body is `deviceoption_set(27, uizoom_getdefault)`, feeding it
 * into an option whose domain is `max(100, min(400, v))` — so a 1000 here
 * makes the reset button ask for 1000% and get 400%.
 */
const UIZOOM_DEFAULT = 100;

/** State the preview owns on behalf of the world. */
export class WorldState {
    constructor({ canvasWidth = 765, canvasHeight = 503, mouseX = 0, mouseY = 0 } = {}) {
        this.canvasWidth = canvasWidth;
        this.canvasHeight = canvasHeight;
        this.mouseX = mouseX;
        this.mouseY = mouseY;

        /** Highlight sets, one per family, keyed by the subject's own tuple. */
        this.highlights = new Map();
        /** Per-family appearance, from the `*_setup` calls. */
        this.highlightSetup = new Map();

        /* Viewport and camera. The zoom pair starts at 256 on BOTH ends and
         * the fov clamp at 1..32767, because a script can read either before
         * any setter has run and zero would letterbox the viewport away. */
        this.zoomNear = 256;
        this.zoomFar = 256;
        this.fovNear = 512;
        this.fovFar = 512;
        this.fovMin = 1;
        this.fovMax = 32767;
        this.cameraAngleX = 0;
        this.cameraAngleY = 0;
        this.followHeight = 0;
        this.uiZoom = UIZOOM_DEFAULT;
        this.minimapZoom = 0;
        this.minimapIconZoomLimit = 0;

        /** Client operations a script installed on entity kinds. */
        this.clientOps = new Map();
        /** What was asked of a system this preview does not have. */
        this.intents = [];
    }

    highlightSet(family) {
        if( !this.highlights.has(family) ) this.highlights.set(family, new Set());
        return this.highlights.get(family);
    }
}

/** The families that take a `_setup`, `_on`, `_off`, `_get` and `_clear`. */
const HIGHLIGHT_FAMILIES = Object.freeze({
    /* subject arity: how many leading arguments identify the thing, before
     * the trailing slot argument every member of the family carries. */
    npc: 2, npctype: 1, loc: 3, loctype: 1, obj: 3, objtype: 1,
    player: 1, tile: 2, group: 1,
});

export function installWorldOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* --------------------------------------------------------------
     * Highlights
     * ----------------------------------------------------------- */

    /*
     * Nine families, one shape. The KEY is the whole subject tuple, not just
     * the id: `highlight_obj_on(obj, coord, ...)` names one pile on one tile,
     * and keying by obj alone would switch off a highlight on a pile the
     * script never mentioned.
     *
     * The trailing argument every member carries is the highlight SLOT — the
     * `_setup` id whose colour and thickness this subject uses — so a family
     * can carry several independent highlight styles at once.
     */
    for( const [family, arity] of Object.entries(HIGHLIGHT_FAMILIES) )
    {
        const key = (args) => args.slice(0, arity + 1).map(String).join(':');

        proto[`highlight_${family}_setup`] = function (...args) {
            this.calls++;
            this.world.highlightSetup.set(`${family}:${args[0] | 0}`, {
                slot: args[0] | 0, values: args.slice(1).map((v) => v | 0),
            });
        };
        proto[`highlight_${family}_on`] = function (...args) {
            this.calls++;
            this.world.highlightSet(family).add(key(args));
        };
        proto[`highlight_${family}_off`] = function (...args) {
            this.calls++;
            this.world.highlightSet(family).delete(key(args));
        };
        proto[`highlight_${family}_get`] = function (...args) {
            this.calls++;
            return this.world.highlightSet(family).has(key(args)) ? 1 : 0;
        };
        /* `_clear` takes the SLOT and drops every subject using it. */
        proto[`highlight_${family}_clear`] = function (slot) {
            this.calls++;
            const suffix = `:${slot | 0}`;
            const set = this.world.highlightSet(family);
            for( const entry of [...set] ) if( entry.endsWith(suffix) ) set.delete(entry);
        };
    }

    proto.highlight_clear = function () {
        this.calls++;
        this.world.highlights.clear();
        this.client.highlights.clear();
    };

    /* --------------------------------------------------------------
     * The right-click menu
     * ----------------------------------------------------------- */

    /*
     * A preview has no open menu, and every one of these answers what the
     * reference answers with none: type NONE, no subject of any kind, no
     * entries. `minimenu_isopen` is the one a script branches on, so it must
     * be honest rather than optimistic — a panel that believes a menu is open
     * will wait for a click that never comes.
     */
    proto.minimenu_type = function () { this.calls++; return MINIMENU_TYPE.NONE; };
    proto.minimenu_isopen = function () { this.calls++; return 0; };
    proto.minimenu_numops = function () { this.calls++; return 0; };
    proto.minimenu_hovered_index = function () { this.calls++; return -1; };
    proto.minimenu_getscroll = function () { this.calls++; return 0; };
    /* Two strings: the row's op and its subject text. */
    proto.minimenu_entry = function () { this.calls++; return ['', '']; };
    for( const kind of ['npc', 'loc', 'obj', 'player', 'component'] )
        proto[`minimenu_find${kind}`] = function () { this.calls++; return 0; };

    proto.minimenu_resetorder = function () { this.calls++; };
    proto.minimenu_togglescroll = function () { this.calls++; };
    proto.minimenu_setorderedit = function (mode) { this.calls++; void mode; };
    proto.minimenu_setblockmode = function (a, b) { this.calls++; void a; void b; };

    /* --------------------------------------------------------------
     * Camera, viewport, zoom, safe area
     * ----------------------------------------------------------- */

    proto.cam_forceangle = function (angleX, angleY) {
        this.calls++;
        this.world.cameraAngleX = angleX | 0;
        this.world.cameraAngleY = angleY | 0;
    };
    proto.cam_getangle_xa = function () { this.calls++; return this.world.cameraAngleX; };
    proto.cam_getangle_ya = function () { this.calls++; return this.world.cameraAngleY; };
    proto.cam_setfollowheight = function (height) {
        this.calls++;
        this.world.followHeight = height | 0;
    };
    proto.cam_getfollowheight = function () { this.calls++; return this.world.followHeight; };
    /* A write, despite the name — the reference's `cam_getyaw` takes an
     * argument and returns nothing at this revision. */
    proto.cam_getyaw = function (yaw) { this.calls++; void yaw; };

    proto.viewport_setfov = function (near, far) {
        this.calls++;
        this.world.fovNear = near | 0;
        this.world.fovFar = far | 0;
    };
    proto.viewport_setzoom = function (near, far) {
        this.calls++;
        this.world.zoomNear = near | 0;
        this.world.zoomFar = far | 0;
    };
    proto.viewport_clampfov = function (minNear, maxNear, minFar, maxFar) {
        this.calls++;
        /* `viewport_clampfov(0,0,0,0)` RESTORES the unset range rather than
         * clamping everything away — zero is outside both ends. */
        const allZero = !(minNear | 0) && !(maxNear | 0) && !(minFar | 0) && !(maxFar | 0);
        this.world.fovMin = allZero ? 1 : minNear | 0;
        this.world.fovMax = allZero ? 32767 : maxFar | 0;
    };
    proto.viewport_getzoom = function () {
        this.calls++;
        return [this.world.zoomNear, this.world.zoomFar];
    };
    proto.viewport_getfov = function () {
        this.calls++;
        return [this.world.fovNear, this.world.fovFar];
    };
    proto.viewport_geteffectivesize = function () {
        this.calls++;
        return [this.world.canvasWidth, this.world.canvasHeight];
    };

    proto.uizoom_set = function (value) { this.calls++; this.world.uiZoom = value | 0; };
    proto.uizoom_get = function () { this.calls++; return this.world.uiZoom; };
    proto.uizoom_reset = function () { this.calls++; this.world.uiZoom = UIZOOM_DEFAULT; };
    proto.uizoom_getdefault = function () { this.calls++; return UIZOOM_DEFAULT; };

    /* The safe area is the WHOLE canvas here, which is the reference's answer
     * on a desktop: the inset exists for notched phone displays. */
    proto.safearea_getminx = function () { this.calls++; return 0; };
    proto.safearea_getminy = function () { this.calls++; return 0; };
    proto.safearea_getmaxx = function () { this.calls++; return this.world.canvasWidth; };
    proto.safearea_getmaxy = function () { this.calls++; return this.world.canvasHeight; };
    /* The `_alt` form takes two arguments and returns nothing. */
    proto.safearea_getmaxy_alt = function (a, b) { this.calls++; void a; void b; };

    proto.minimap_setzoom = function (zoom) { this.calls++; this.world.minimapZoom = zoom | 0; };
    proto.minimap_getzoom = function () { this.calls++; return this.world.minimapZoom; };
    proto.minimap_seticonzoomlimit = function (limit) {
        this.calls++;
        this.world.minimapIconZoomLimit = limit | 0;
    };

    proto.mouse_getx = function () { this.calls++; return this.world.mouseX; };
    proto.mouse_gety = function () { this.calls++; return this.world.mouseY; };

    /* --------------------------------------------------------------
     * Client operations on entities
     * ----------------------------------------------------------- */

    /*
     * `clientop_npc_set(slot, text, flags)` installs a right-click entry the
     * CLIENT adds to every npc of a kind. Real state: the menu builder reads
     * it back, so it is stored rather than recorded.
     */
    for( const kind of ['npc', 'loc', 'obj', 'player'] )
    {
        proto[`clientop_${kind}_set`] = function (slot, text, flags) {
            this.calls++;
            this.world.clientOps.set(`${kind}:${slot | 0}`,
                { text: String(text ?? ''), flags: flags | 0 });
        };
        proto[`clientop_${kind}_del`] = function (slot) {
            this.calls++;
            this.world.clientOps.delete(`${kind}:${slot | 0}`);
        };
    }

    /* The tile family, same shape as the other four. */
    proto.clientop_tile_set = function (slot, text, flags) {
        this.calls++;
        this.world.clientOps.set(`tile:${slot | 0}`,
            { text: String(text ?? ''), flags: flags | 0 });
    };
    proto.clientop_tile_del = function (slot) {
        this.calls++;
        this.world.clientOps.delete(`tile:${slot | 0}`);
    };

    /* --------------------------------------------------------------
     * The acting subject, and the scene
     * ----------------------------------------------------------- */

    /*
     * `loc_type`, `npc_name`, `obj_coord` and the rest answer for whatever the
     * right-click menu latched. With no menu and no world there is nothing
     * latched, and the reference's own answers apply: -1 for an id or a coord,
     * "" for a name.
     *
     * -1 rather than 0 matters here more than anywhere: obj 0 is a real item,
     * loc 0 is a real object and coord 0 is the corner of the map, so a zero
     * would send a script off to act on something.
     */
    for( const method of ['loc_type', 'obj_type', 'npc_type', 'loc_coord',
        'obj_coord', 'tile_coord', 'destinationcoord', 'uid', 'self_player_uid',
        'obj_owner'] )
        proto[method] = function () { this.calls++; return -1; };

    proto.npc_name = function () { this.calls++; return ''; };

    /*
     * The three unnamed client-op context getters that answer a STRING —
     * 6800 the acting loc's name, 6850 the acting obj's, 6853 its count — and
     * the two minimenu ones, 7106 the acting row's tile and 7107 its obj.
     * They have no name in the decompiler's table and a real answer in the C
     * host, so they are implemented under the numbers the surface gives them.
     */
    proto.op6800 = function () { this.calls++; return ''; };
    proto.op6850 = function () { this.calls++; return ''; };
    proto.op6853 = function () { this.calls++; return -1; };
    proto.op7106 = function () { this.calls++; return -1; };
    proto.op7107 = function () { this.calls++; return -1; };

    /** `nc_name(npc)` — an npc TYPE's name, from the cache and not the world. */
    proto.nc_name = function (npcId) {
        this.calls++;
        if( npcId < 0 ) return '';
        return this.config.get('npcs', npcId)?.name ?? 'null';
    };

    /** `loc_find(coord, type)` — no scene means nothing to find. */
    proto.loc_find = function (coord, locType) { this.calls++; void coord; void locType; return 0; };

    /* `coord_inscene` asks whether a tile is inside the LOADED scene. There is
     * no scene, so nothing is — and answering 1 would send a script walking. */
    proto.coord_inscene = function (coord) { this.calls++; void coord; return 0; };

    /* Not a moderator. 0 is the reference's own constant here, not a stub. */
    proto.staffmodlevel = function () { this.calls++; return 0; };

    /* --------------------------------------------------------------
     * The social lists
     * ----------------------------------------------------------- */

    /*
     * EMPTY, and empty is a state the reference reaches every time someone
     * plays with nobody online. A fabricated name is worse than useless: a
     * script will offer to message it.
     *
     * `friend_getname` answers TWO strings — the display name and the account
     * name — because a display name can be changed and the friend list is
     * keyed on the other one.
     */
    proto.friend_count = function () { this.calls++; return 0; };
    proto.ignore_count = function () { this.calls++; return 0; };
    proto.friend_getname = function (index) { this.calls++; void index; return ['', '']; };
    proto.ignore_getname = function (index) { this.calls++; void index; return ['', '']; };
    proto.friend_getworld = function (index) { this.calls++; void index; return 0; };
    proto.friend_getrank = function (index) { this.calls++; void index; return 0; };
    proto.friend_test = function (name) { this.calls++; void name; return 0; };
    proto.ignore_test = function (name) { this.calls++; void name; return 0; };

    const SOCIAL_INTENTS = {
        friend_add: ['friendAdd', ['name']],
        friend_del: ['friendDelete', ['name']],
        friend_setrank: ['friendSetRank', ['name', 'rank']],
        ignore_add: ['ignoreAdd', ['name']],
        ignore_del: ['ignoreDelete', ['name']],
    };
    for( const [method, [intent, fields]] of Object.entries(SOCIAL_INTENTS) )
    {
        proto[method] = function (...args) {
            this.calls++;
            const payload = {};
            fields.forEach((field, index) => { payload[field] = args[index]; });
            this.world.intents.push({ intent, ...payload });
            this.onIntent?.(intent, payload);
        };
    }

    /*
     * The sort family stages a comparison and `_apply` runs it. With no list
     * to sort both halves are no-ops — but they are ACCEPTED, because the
     * panel that calls them then reads the list back and must not abort
     * partway through building an empty one.
     */
    for( const list of ['friendlist', 'friendschat'] )
    {
        for( const key of ['reset', 'apply'] )
            proto[`${list}_sort_${key}`] = function () { this.calls++; };
        for( const key of ['legacy', 'name', 'rank', 'world',
            'lastworldchange', 'online_world'] )
            proto[`${list}_sort_${key}`] = function (ascending) {
                this.calls++;
                void ascending;
            };
    }

    /* --------------------------------------------------------------
     * Notifications, hiscores, the local player
     * ----------------------------------------------------------- */

    /*
     * A browser CAN show a notification, but only with permission this
     * runtime has not asked for — so `supported` answers 0 and the request is
     * recorded. Answering 1 and then silently showing nothing is the shape
     * that makes a "you have been notified" panel lie.
     */
    proto.local_notification_supported = function () { this.calls++; return 0; };
    proto.local_notification = function (title, body, delay, id) {
        this.calls++;
        const payload = { title: String(title ?? ''), body: String(body ?? ''),
            delay: delay | 0, id: id | 0 };
        this.world.intents.push({ intent: 'notification', ...payload });
        this.onIntent?.('notification', payload);
        return 0;
    };
    proto.local_notification_cancel = function (id) { this.calls++; void id; };
    proto.local_notification_cancelall = function () { this.calls++; };

    /* No hiscore lookup has run, which is status 0 and no error text. */
    proto.hiscores_status = function () { this.calls++; return 0; };
    proto.hiscores_error = function () { this.calls++; return ''; };

    /*
     * The local player. `p_name` is empty rather than invented, `p_findself`
     * answers 0 because there is no player to find, and the route is empty —
     * `p_routelength` 0 means every `p_route(i)` is out of range, so the
     * coordinate it would answer never gets asked for.
     */
    proto.p_name = function () { this.calls++; return ''; };
    proto.p_findself = function () { this.calls++; return 0; };
    proto.p_routelength = function () { this.calls++; return 0; };
    proto.p_route = function (index) { this.calls++; void index; return -1; };

    /* --------------------------------------------------------------
     * Map elements
     * ----------------------------------------------------------- */

    /*
     * A map element's label, category and icon come from the cache's
     * mapelement table. The content tree carries it; where a record is absent
     * the answers are the reference's own miss values, and `mec_sprite`
     * answers a PAIR because an element has a normal and a hovered icon.
     */
    proto.mec_text = function (mecId) {
        this.calls++;
        return this.config.get('mapElements', mecId)?.name ?? '';
    };
    proto.mec_textsize = function (mecId) {
        this.calls++;
        return this.config.get('mapElements', mecId)?.textSize ?? 0;
    };
    proto.mec_category = function (mecId) {
        this.calls++;
        return this.config.get('mapElements', mecId)?.category ?? -1;
    };
    proto.mec_sprite = function (mecId) {
        this.calls++;
        const record = this.config.get('mapElements', mecId);
        return [record?.sprite ?? -1, record?.spriteOver ?? -1];
    };
}
