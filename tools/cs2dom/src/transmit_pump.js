/*
 * How state reaches the screen.
 *
 * There is no diffing anywhere in this system. A var changes, and the hooks
 * that declared an interest in it are RE-RUN — the script rewrites whatever it
 * owns. That is the entire reactivity model, and this file is its scheduler.
 *
 * Ported from `RS_CS2_PumpTransmits` and the dispatch tasks in
 * `task_cs2_run.c`. Three gates, in this order, and each of them exists
 * because of a specific failure:
 *
 * 1. THE COMPONENT IS GONE -> mark seen, never fire. A reclaimed component's
 *    hook must not run against whatever now occupies its id.
 *
 * 2. THE COMPONENT IS HIDDEN -> record pending work, do NOT advance the
 *    serial. This is the subtle one. A hidden hook that advanced its serial
 *    would look up to date when the panel opened, and the panel would show
 *    whatever it was built with. Deferring the work by leaving the serial
 *    behind is what makes it fire exactly once on reveal.
 *
 * 3. THE HOOK HAS ALREADY SEEN THIS SERIAL -> skip. This is what makes a quiet
 *    tick free: the pump may traverse, but it runs no scripts.
 *
 * Above all three sits a coarse dirty flag, so a tick where nothing changed
 * does not traverse at all.
 */

import { HOOK_SLOTS } from './uitree.js';

/**
 * Transmit kinds and how each decides whether a hook cares.
 *
 * The var and inv families carry a TRIGGER LIST — the ids the hook watches —
 * and only re-run when the change intersects it. The rest carry none at all:
 * the reference bumps one stamp per chat message, per friend-list change, and
 * every registered hook re-runs against it. That is not laziness in the
 * reference; there is nowhere in the wire format to put a trigger for them.
 */
const TRANSMIT_KINDS = Object.freeze({
    var: { slot: 'onVarTransmit', filtered: true },
    inv: { slot: 'onInvTransmit', filtered: true },
    /* "misc" is run energy and run weight at this revision — the two
     * transmits with no registry of their own — not the skill channel. */
    misc: { slot: 'onMiscTransmit', filtered: false },
    stat: { slot: 'onStatTransmit', filtered: true },
    friend: { slot: 'onFriendTransmit', filtered: false },
    chat: { slot: 'onChatTransmit', filtered: false },
});

export const TRANSMIT_SLOTS = Object.freeze(
    Object.values(TRANSMIT_KINDS).map((kind) => kind.slot));

export function createTransmitPump(options) {
    return new TransmitPump(options);
}

export class TransmitPump {
    constructor({ tree, driver }) {
        this.tree = tree;
        this.driver = driver;

        /*
         * One serial per kind, and one "last seen" per hook.
         *
         * Serials start at 1 so a freshly registered hook — which has seen
         * nothing, i.e. 0 — fires once on the first pass rather than looking
         * already up to date.
         */
        this.serials = { var: 1, inv: 1, misc: 1, friend: 1, chat: 1 };
        this.changedIds = { var: new Set(), inv: new Set() };
        this.dirty = { var: false, inv: false, misc: false, friend: false, chat: false };

        /** `${kind}:${nodeIndex}` -> the serial that hook last ran against. */
        this._lastSeen = new Map();
        /** Hooks that a change reached while hidden, to resume on reveal. */
        this._pendingUnhide = new Set();
        /** Set when any component was revealed since the last pass. */
        this.widgetsLoadedDirty = false;

        this.stats = { passes: 0, traversals: 0, dispatched: 0, deferred: 0 };
    }

    /* --------------------------------------------------------------
     * Recording change
     * ----------------------------------------------------------- */

    /** A var, varbit or varc changed from the server. */
    noteVarChanged(id) {
        this.serials.var++;
        this.changedIds.var.add(id);
        this.dirty.var = true;
    }

    /** A container changed. `id` null means "all of them". */
    noteInvChanged(id = null) {
        this.serials.inv++;
        if( id === null ) this.changedIds.inv.clear();
        else this.changedIds.inv.add(id);
        this.dirty.inv = true;
    }

    noteChanged(kind) {
        this.serials[kind]++;
        this.dirty[kind] = true;
    }

    /** A component became visible; its deferred hooks can run now. */
    noteWidgetsLoaded() {
        this.widgetsLoadedDirty = true;
    }

    /**
     * Is there anything for a pass to do?
     *
     * The pump's own early-out, exported so a caller can prove that a quiet
     * tick performs no traversal at all rather than merely dispatching nothing.
     */
    pending() {
        if( this.widgetsLoadedDirty ) return true;
        for( const flag of Object.values(this.dirty) ) if( flag ) return true;
        return false;
    }

    /* --------------------------------------------------------------
     * The pass
     * ----------------------------------------------------------- */

    /**
     * Queue every transmit hook that is now out of date.
     *
     * Returns how many were dispatched, which the driver uses to decide
     * whether the frame has settled.
     */
    pump() {
        if( !this.pending() ) return 0;
        this.stats.passes++;

        let dispatched = 0;
        for( const [kind, { slot, filtered }] of Object.entries(TRANSMIT_KINDS) )
        {
            if( !this.dirty[kind] && !this.widgetsLoadedDirty ) continue;
            dispatched += this._pumpKind(kind, slot, filtered);
        }

        for( const kind of Object.keys(this.dirty) ) this.dirty[kind] = false;
        this.changedIds.var.clear();
        this.changedIds.inv.clear();
        this.widgetsLoadedDirty = false;
        this.stats.dispatched += dispatched;
        return dispatched;
    }

    /**
     * Fire every registered transmit hook ONCE, whatever its trigger list says.
     *
     * This is the mount pass, and it is the reference's own:
     * `RS_CS2_RegisterCacheTransmitHooks` is called from both bake paths
     * "before their initial transmit dispatch, so a mount paints from the
     * cache hook on the same pass a CS2-registered one would".
     *
     * It matters because a cache-authored transmit hook is the ONLY thing that
     * ever paints some widgets. The combat tab's auto-retaliate button is the
     * standing example — `onload=i:325`, `onvarptransmit=i:325`,
     * `varptriggers=172` — and without the mount pass a widget whose handler
     * lives entirely in a transmit hook shows its authored value forever.
     *
     * The trigger filter is deliberately bypassed: at mount nothing has
     * changed yet, so filtering would dispatch nothing at all. Every hook's
     * serial is left where it is, so an actual change still re-runs it.
     */
    dispatchAll() {
        let dispatched = 0;
        for( const [kind, { slot }] of Object.entries(TRANSMIT_KINDS) )
        {
            void kind;
            for( const index of this.tree.nodesWithHook(slot) )
            {
                const node = this.tree.at(index);
                const binding = node?.hooks?.[slot];
                if( !binding || binding.scriptId <= 0 ) continue;
                if( this.tree.hiddenByAncestor(index) ) continue;
                this.driver.dispatchHook(binding,
                    { reason: 'mount-transmit', componentId: node.componentId });
                dispatched++;
            }
        }
        this.stats.dispatched += dispatched;
        return dispatched;
    }

    _pumpKind(kind, slot, filtered) {
        const serial = this.serials[kind];
        const changed = this.changedIds[kind];
        let dispatched = 0;

        for( const index of this.tree.nodesWithHook(slot) )
        {
            this.stats.traversals++;
            const node = this.tree.at(index);
            const key = `${kind}:${index}`;

            /* (1) Gone. Mark seen so it can never fire against a stranger. */
            if( !node )
            {
                this._lastSeen.set(key, serial);
                this._pendingUnhide.delete(key);
                continue;
            }

            const binding = node.hooks?.[slot];
            if( !binding || binding.scriptId <= 0 ) continue;

            const deferred = this._pendingUnhide.has(key);
            const interested = !filtered
                || deferred
                || changed.size === 0        /* "everything changed" */
                || this._watches(binding, changed);
            if( !interested ) continue;

            /* (2) Hidden. Record the work and LEAVE THE SERIAL BEHIND. */
            if( this.tree.hiddenByAncestor(index) )
            {
                this._pendingUnhide.add(key);
                this.stats.deferred++;
                continue;
            }

            /* (3) Already up to date — unless this is deferred work resuming,
             * whose whole point is that its serial is behind. */
            if( !deferred && (this._lastSeen.get(key) ?? 0) >= serial ) continue;

            this._lastSeen.set(key, serial);
            this._pendingUnhide.delete(key);
            /* The BOUND COMPONENT travels with the dispatch: the event
             * locals in a hook's argument list — `event_com`, `event_comsubid`
             * — are resolved against it, and a transmit hook whose script
             * starts with a bare `cc_settext` writes to whatever the dispatch
             * selected. Without it a cache-authored hook re-registers against
             * component -1 and paints nothing. */
            this.driver.dispatchHook(binding,
                { reason: `${kind}-transmit`, componentId: node.componentId });
            dispatched++;
        }
        return dispatched;
    }

    /** Does this hook's trigger list name any of the changed ids? */
    _watches(binding, changed) {
        const triggers = binding.triggers;
        if( !triggers || triggers.length === 0 ) return false;
        for( const trigger of triggers ) if( changed.has(trigger) ) return true;
        return false;
    }

    /* --------------------------------------------------------------
     * Timers
     * ----------------------------------------------------------- */

    /**
     * Fire every `onTimer` hook. Once per 20 ms logic tick.
     *
     * No serial and no filter: a timer hook runs every tick by definition. A
     * hidden one is skipped, because a closed panel ticking its clock is the
     * work the retention gate exists to avoid.
     */
    tick() {
        let dispatched = 0;
        for( const index of this.tree.nodesWithHook('onTimer') )
        {
            const node = this.tree.at(index);
            if( !node ) continue;
            const binding = node.hooks?.onTimer;
            if( !binding || binding.scriptId <= 0 ) continue;
            if( this.tree.hiddenByAncestor(index) ) continue;
            this.driver.dispatchHook(binding,
                { reason: 'timer', componentId: node.componentId });
            dispatched++;
        }
        return dispatched;
    }
}

/**
 * A driver whose follow-up work is the transmit pump.
 *
 * Composed rather than built in, so the base driver stays testable without a
 * tree full of hooks and so a caller can add its own follow-up sources (resize,
 * trigger-op) the same way.
 */
export function attachTransmitPump(driver, tree) {
    const pump = createTransmitPump({ tree, driver });
    driver.pump = pump;
    driver.collectFollowUps = () => pump.pump();
    return pump;
}

export { TRANSMIT_KINDS, HOOK_SLOTS };
