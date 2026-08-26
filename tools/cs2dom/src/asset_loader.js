/*
 * Servicing a park.
 *
 * When a host method needs something that is not loaded it answers HOST_PARK
 * and names a CLASS — sprite, font, model, component, enum, struct, obj, db.
 * Those class names are generated from the C client's own yield planner
 * (`generated/cs2_host_park.js`), so this file's job is only to route each one
 * to whatever can produce it.
 *
 * ------------------------------------------------------------------
 * Synchronous first
 * ------------------------------------------------------------------
 *
 * `loadSync` is tried before anything is awaited, and it is not an
 * optimisation: the C planner only yields when the answer is genuinely
 * absent, and most parks in a mass rebuild are for something already decoded.
 * Awaiting those would turn a 3,000-row rebuild into 3,000 microtask hops and
 * spread one logical tick over many frames.
 *
 * ------------------------------------------------------------------
 * A load that cannot succeed must still complete
 * ------------------------------------------------------------------
 *
 * An id this cache does not have will never arrive. The host records that it
 * already waited once (`_awaitSpent`) and completes with the miss answer on
 * the retry, so a loader that resolves to nothing is a correct outcome, not a
 * hang. What a loader must never do is reject silently or never settle.
 */

/** Everything a park can be waiting for; mirrors PARK_CLASSES. */
export const LOAD_CLASSES = Object.freeze([
    'component', 'db', 'enum', 'font', 'loc', 'mapelement', 'model',
    'npc', 'obj', 'script', 'sprite', 'struct', 'worldmap', 'inv',
]);

export function createLoader(options = {}) {
    return new AssetLoader(options);
}

export class AssetLoader {
    constructor({
        sprites = null, fonts = null, models = null,
        interfaces = null, configs = null, scripts = null,
        onWarning = null,
    } = {}) {
        this.sprites = sprites;
        this.fonts = fonts;
        this.models = models;
        /** Bakes an interface group into the tree. */
        this.interfaces = interfaces;
        /** Fetches an enum/struct/param/obj/npc/loc record into HostConfig. */
        this.configs = configs;
        /** Compiles and registers a script closure. */
        this.scripts = scripts;
        this.onWarning = onWarning;

        this.stats = { sync: 0, async: 0, unresolved: 0 };
        /** Every class/id this session could not produce, for a report. */
        this.missing = new Set();
    }

    /**
     * Answer without suspending, if possible.
     *
     * Returning true means the retry will now succeed. Returning false is not
     * a failure — it means "ask me again asynchronously".
     */
    loadSync(kind, id, extra = null) {
        switch( kind )
        {
        case 'sprite': return this._hit(this.sprites?.has(id));
        case 'font': return this._hit(this.fonts?.has(id));
        case 'component': return this._hit(this.interfaces?.hasGroup?.(id));
        case 'enum':
        case 'struct':
        case 'obj':
        case 'npc':
        case 'loc':
        case 'inv':
            return this._hit(this.configs?.hasSync?.(kind, id, extra));
        case 'script': return this._hit(this.scripts?.has?.(id));
        default: return false;
        }
    }

    _hit(present) {
        if( !present ) return false;
        this.stats.sync++;
        return true;
    }

    /**
     * Produce it, or settle having failed.
     *
     * Every branch resolves. A rejection here would abandon a parked script
     * with no way to finish it, and the host's own miss answers are the
     * correct outcome for an id that does not exist.
     */
    async load(kind, id, extra = null) {
        this.stats.async++;
        try
        {
            const produced = await this._load(kind, id, extra);
            if( produced ) return true;
        }
        catch( error )
        {
            this.warn(`loading ${kind} ${id} failed: ${error.message}`);
        }
        this.stats.unresolved++;
        this.missing.add(`${kind}:${id}`);
        return false;
    }

    async _load(kind, id, extra) {
        switch( kind )
        {
        case 'sprite': return this.sprites ? !!(await this.sprites.load(id)) : false;
        case 'font': return this.fonts ? !!(await this.fonts.load(id)) : false;
        /* A model park carries no pose — the widget's pose is read at paint
         * time, and the park is only about the model record existing. */
        case 'model': return this.models ? !!(await this.models.load(id, extra ?? {})) : false;
        case 'component': return this.interfaces
            ? !!(await this.interfaces.mount(id)) : false;
        case 'script': return this.scripts ? !!(await this.scripts.load(id)) : false;
        case 'enum':
        case 'struct':
        case 'obj':
        case 'npc':
        case 'loc':
        case 'inv':
        case 'db':
        case 'mapelement':
        case 'worldmap':
            return this.configs ? !!(await this.configs.load(kind, id, extra)) : false;
        default:
            this.warn(`no loader for '${kind}'`);
            return false;
        }
    }

    warn(message) {
        if( this.onWarning ) this.onWarning(message);
    }

    /** What this session asked for and never got — a report, not an error. */
    report() {
        return { ...this.stats, missing: [...this.missing].sort() };
    }
}

/**
 * A loader that has everything, for tests about anything but loading.
 *
 * Distinct from "a loader with no sources", which parks once per id and then
 * completes with the miss answer — both are useful and they are not the same
 * thing.
 */
export const READY_LOADER = Object.freeze({
    loadSync: () => true,
    load: async () => true,
});
