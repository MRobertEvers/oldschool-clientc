/*
 * Running CS2, and knowing when the frame is finished.
 *
 * Ported from the contract in docs/CS2_EXECUTION.md, which the C client states
 * as one invariant: **CS2 always drains to a fixed point before the client
 * interacts with or renders the tree.** A cooperative yield is a scheduler
 * detail, not a frame boundary.
 *
 *     enqueue CS2
 *       -> run every ready task -> resolve layout -> enqueue follow-ups ┐
 *       ^                                                               │
 *       └────────────────── repeat until no work remains ───────────────┘
 *       -> interact -> paint one committed frame
 *
 * Two things follow from that and neither is optional:
 *
 * WHILE A SCRIPT IS PARKED THE TREE IS VALIDLY INTERMEDIATE. Earlier
 * operations stayed applied — the C VM rolls back the parked opcode, not the
 * script — so nothing may hit-test or paint against it. The previous committed
 * frame keeps showing, and input queues.
 *
 * FINISHING A TASK CAN CREATE MORE WORK. Resize, trigger-op and transmit hooks
 * are queued by the scripts that ran, so "the queue is empty" is not the same
 * question as "the frame has settled". The loop asks the second one.
 */

import { HOST_PARK } from './generated/cs2_host_park.js';

export class CS2DriverError extends Error {
    constructor(message) {
        super(message);
        this.name = 'CS2DriverError';
    }
}

/**
 * A ceiling on settlement passes.
 *
 * Not a scheduling budget — a runaway guard. A script that queues a follow-up
 * which queues it back never settles, and the honest failure is to say so
 * rather than to hang the page. The C client has no such limit because its
 * runner is driven by a frame loop that can simply not finish; a browser tab
 * that does the same is a tab that has to be killed.
 */
const MAX_SETTLE_PASSES = 64;

export function createDriver(options) {
    return new CS2Driver(options);
}

export class CS2Driver {
    constructor({ host, scripts, loader = null, onWarning = null, onScriptError = null } = {}) {
        if( !host ) throw new CS2DriverError('a driver needs a host');
        if( !scripts ) throw new CS2DriverError('a driver needs a script registry');
        this.host = host;
        this.scripts = scripts;
        this.loader = loader;
        this.onWarning = onWarning;
        /*
         * What to do with a script that throws.
         *
         * Absent, the error propagates and the drain stops — which is right
         * for a page, because a script that cannot run has left the tree in a
         * state nobody designed and painting it shows a lie. A harness that is
         * MEASURING the gap supplies a handler: the reference VM aborts the
         * offending script and keeps running, so containing it here reports
         * every missing operation in one pass instead of one per run.
         */
        this.onScriptError = onScriptError;

        /** Serial FIFO. Dispatching a hook enqueues; it never runs inline. */
        this.queue = [];
        /** A task suspended on a load, kept so a later frame can resume it. */
        this.parked = null;
        /*
         * The load that task is waiting for, and whether it has landed.
         *
         * The load is STARTED and not awaited inside the drain loop. A frame
         * loop must not block on I/O — it has to return, leave the previous
         * frame showing and come back — so `settle({ wait: false })` answers
         * "still parked" immediately. A headless caller that just wants the
         * work finished passes the default and awaits.
         */
        this.parkedOn = null;
        this.parkedReady = false;

        this.settled = true;
        this.stats = { invocations: 0, parks: 0, passes: 0, followUps: 0, errors: 0 };

        host.onLayoutNeeded = () => this.resolveLayout();
    }

    /* --------------------------------------------------------------
     * Dispatch
     * ----------------------------------------------------------- */

    /**
     * Queue one script invocation.
     *
     * `event` is the context `event_mousex` and friends read; it belongs to
     * the dispatch, not to the host, because a queued hook must see the event
     * that caused it and not whatever the pointer did since.
     */
    dispatch(scriptId, args = [], {
        event = null, reason = 'dispatch', componentId = -1,
    } = {}) {
        this.queue.push({ scriptId, args, event, reason, componentId });
        this.settled = false;
    }

    /** Queue a hook binding as the tree recorded it. */
    dispatchHook(binding, { event = null, reason = 'hook', componentId = -1 } = {}) {
        if( !binding || binding.scriptId === undefined || binding.scriptId < 0 ) return;
        this.dispatch(binding.scriptId, binding.args ?? [],
            { event, reason, componentId: binding.componentId ?? componentId });
    }

    /* --------------------------------------------------------------
     * Settlement
     * ----------------------------------------------------------- */

    /**
     * Drain to a fixed point.
     *
     * Returns when there is no work left, or when a park needs an asynchronous
     * load — in which case `settled` stays false, the caller must not paint,
     * and `settle()` is called again once the loader resolves.
     */
    async settle({ wait = true } = {}) {
        for( let pass = 0; pass < MAX_SETTLE_PASSES; pass++ )
        {
            this.stats.passes++;

            while( this.parked || this.queue.length )
            {
                if( this.parked && !this.parkedReady )
                {
                    if( !wait ) return false;
                    await this.parkedOn;
                }
                const finished = await this._runNext();
                if( !finished ) return false; /* still waiting on a load */
            }

            this.resolveLayout();

            const followUps = this.collectFollowUps();
            if( followUps === 0 )
            {
                this.settled = true;
                return true;
            }
            this.stats.followUps += followUps;
        }
        throw new CS2DriverError(
            `settlement did not converge in ${MAX_SETTLE_PASSES} passes — a follow-up is ` +
            're-queueing itself');
    }

    /**
     * Run one task to completion or to a park it cannot service synchronously.
     *
     * Returns true when the task finished, false when the driver is waiting on
     * an asynchronous load. A park whose asset the loader already has is
     * serviced in place, which is the common case and costs no await.
     */
    async _runNext() {
        let task = this.parked;
        if( task )
        {
            /* Resuming: the load this task waited for has landed. */
            this.parked = null;
            this.parkedOn = null;
            this.parkedReady = false;
            this.host.clearPending();
        }
        else
        {
            const next = this.queue.shift();
            const fn = this.scripts.get(next.scriptId);
            if( !fn )
            {
                this.warn(`no script ${next.scriptId} (${next.reason})`);
                return true;
            }
            if( next.event ) Object.assign(this.host.eventContext, next.event);
            this.host.beginInvocation();
            /*
             * The bound component is SELECTED before the first opcode runs,
             * and the event locals in the argument list are resolved against
             * it. Both are the reference's own order — a hook whose script
             * starts with a bare `cc_settext` is writing to whatever the
             * dispatch selected, and a hook argument of -2147483645 is
             * `event_com`, not the number minus two billion.
             */
            const target = next.componentId ?? -1;
            if( target >= 0 )
            {
                const node = this.host.tree.findByComponentId(target);
                if( node ) { this.host.setActive(node.index); this.host.setDot(node.index); }
            }
            const args = this.host.resolveHookArgs(next.args, target);
            task = { generator: fn(this.host, ...args), request: next };
            this.stats.invocations++;
        }
        /* Which dispatch is on the stack right now. Debug plumbing only: a
         * difference in what a run BUILT is always traced back to the script
         * that built it, and nothing else can name that script. */
        this.running = task.request;
        {
        }

        let lastServiced = null;
        let lastServicedConsumed = -1;
        for(;;)
        {
            let step;
            try
            {
                step = task.generator.next();
            }
            catch( error )
            {
                /* The task is dead either way; drop it before deciding
                 * whether to re-throw, or a contained error leaves a corpse
                 * parked and the next resume steps a finished generator. */
                this.parked = null;
                this.stats.errors++;
                if( !this.onScriptError ) throw error;
                this.onScriptError(error, task.request);
                return true;
            }
            if( step.done )
            {
                this.parked = null;
                return true;
            }

            /*
             * The generator suspended, which by construction means the host
             * answered HOST_PARK and recorded what it wants. Nothing has been
             * half-applied: a parking method returns before it mutates.
             */
            const pending = this.host.pending;
            if( !pending )
                throw new CS2DriverError(
                    `script ${task.request.scriptId} suspended without a pending load`);

            this.stats.parks++;
            const key = `${pending.kind}:${pending.id}:${pending.extra}`;
            /*
             * A park the loader just satisfied must not come straight back.
             * If it does, the host is asking a different question than the one
             * the loader answered, and the retry loop would spin with no error
             * and no frame. Saying so is far better than hanging the tab.
             */
            /*
             * A park the loader just satisfied must not come straight back
             * HAVING CONSUMED NOTHING.
             *
             * Coming back after consuming the answer is a loop — a script
             * walking an enum key by key parks on the same enum on every
             * iteration — and treating that as a spin refuses a script that is
             * making progress. What is a hang is the same key with the await
             * record untouched: the host is asking a different question than
             * the one the loader answered, and the retry would spin with no
             * error and no frame.
             */
            if( key === lastServiced && this.host.awaitsConsumed === lastServicedConsumed )
                throw new CS2DriverError(
                    `script ${task.request.scriptId} re-parked on ${key} immediately after ` +
                    'it was serviced — the host and the loader disagree about what "loaded" means');

            const serviced = this.serviceSync(pending);
            if( serviced )
            {
                lastServiced = key;
                lastServicedConsumed = this.host.awaitsConsumed;
                this.host.clearPending();
                continue; /* the retry loop in the generated code re-runs the call */
            }

            if( !this.loader )
                throw new CS2DriverError(
                    `script ${task.request.scriptId} needs ${pending.kind} ${pending.id} ` +
                    'and the driver has no loader');

            /*
             * Start the load; do NOT await it here. Returning promptly is what
             * lets the caller decide — a frame loop leaves the previous frame
             * up and comes back, a headless caller awaits `parkedOn`.
             */
            this.parked = task;
            this.parkedReady = false;
            this.settled = false;
            this.parkedOn = Promise.resolve(
                this.loader.load(pending.kind, pending.id, pending.extra))
                .then(() => { this.parkedReady = true; });
            return false;
        }
    }

    /**
     * Try to satisfy a park without suspending.
     *
     * Most parks are for something already decoded — the interface group is in
     * the tree, the sprite is in the atlas — and the C client's own planner
     * only yields when the answer is genuinely absent. Servicing those here
     * keeps a mass rebuild free of awaits.
     */
    serviceSync(pending) {
        return this.loader?.loadSync?.(pending.kind, pending.id, pending.extra) ?? false;
    }

    /* --------------------------------------------------------------
     * Follow-ups
     * ----------------------------------------------------------- */

    /**
     * Queue the work the pass just finished created, and say how much.
     *
     * Overridden by whatever owns the transmit pump and the resize/trigger
     * queues; the base driver has none, so a single pass settles. Returning a
     * count rather than a boolean lets the loop report which pass grew.
     */
    collectFollowUps() {
        return 0;
    }

    /* --------------------------------------------------------------
     * Layout
     * ----------------------------------------------------------- */

    /**
     * Resolve geometry if anything invalidated it.
     *
     * Called at the end of each settlement pass AND on demand from a geometry
     * getter, because a script reads a computed width immediately after
     * setting a size and must see the new one.
     */
    resolveLayout() {
        if( !this.host.tree.layoutStale ) return false;
        this.onResolveLayout?.();
        this.host.tree.layoutStale = false;
        return true;
    }

    warn(message) {
        if( this.onWarning ) this.onWarning(message);
        else throw new CS2DriverError(message);
    }
}

/**
 * A script registry over generated modules.
 *
 * `cs2_<id>` is the exported name the emitter uses, so a module can be dropped
 * in whole and the registry finds every script in it.
 */
export class ScriptRegistry {
    constructor() {
        this.byId = new Map();
    }

    /** Add every `cs2_<id>` export of a module. */
    addModule(module) {
        for( const [name, value] of Object.entries(module) )
        {
            const match = /^cs2_(\d+)$/.exec(name);
            if( match && typeof value === 'function' )
                this.byId.set(Number(match[1]), value);
        }
        return this;
    }

    add(id, fn) { this.byId.set(id, fn); return this; }
    get(id) { return this.byId.get(id); }
    has(id) { return this.byId.has(id); }
    get size() { return this.byId.size; }
}

export { HOST_PARK };
