/*
 * One interface, running.
 *
 * This is the composition the redesign is for: one tree, one thread, one
 * canvas. Everything below is already built and tested in isolation; the only
 * thing this file adds is the ORDER, which is the C client's frame contract
 * (docs/CS2_EXECUTION.md) and not a scheduling choice:
 *
 *     tick (20 ms)      timers, then the transmit pump
 *     settle            drain CS2 to a fixed point, resolving layout
 *     interact          hit-test the queued input, dispatch its intents
 *     settle again      because a hook queues more work
 *     paint             one frame, only if the emit walk says it changed
 *
 * The gate that makes this cheap is `frame()` returning false when nothing
 * moved. An idle interface runs no scripts, walks no tree and paints no
 * pixels — which is the same answer the C client gives, reached the same way.
 *
 * WHILE A SCRIPT IS PARKED NOTHING IS PAINTED OR CLICKED. The tree is validly
 * intermediate at that point: earlier operations stayed applied and the script
 * has not finished deciding what the frame looks like. Input queues, the
 * previous frame keeps showing, and settlement resumes when the load lands.
 */

import { createUITree } from './uitree.js';
import { createHostKernel, HostState, StoreAssetSource } from './host_kernel.js';
import { createDriver, ScriptRegistry } from './cs2_driver.js';
import { attachTransmitPump } from './transmit_pump.js';
import { attachLayout, DEFAULT_ROOT } from './layout.js';
import { createEmitter } from './emit.js';
import { createHitTester } from './hit_test.js';
import { createPainter } from './painter.js';

/** The 20 ms logic tick, with bounded catch-up so a stall cannot avalanche. */
export const TICK_MS = 20;
const MAX_CATCHUP_TICKS = 5;

export function createSession(options = {}) {
    return new InterfaceSession(options);
}

export class InterfaceSession {
    constructor({
        root = DEFAULT_ROOT, surface = null, scripts = null, loader = null,
        state = null, config = null, player = null, assets = null,
        sprites = null, fonts = null, models = null, onWarning = null,
    } = {}) {
        this.tree = createUITree();
        /*
         * The kernel's "is it loaded?" and the painter's "give me the image"
         * must consult the SAME stores, or a park is never satisfiable: the
         * loader fills one, the kernel re-checks the other, and the retry loop
         * spins forever without an error. An explicit `assets` overrides, for
         * a test that is about something else.
         */
        this.host = createHostKernel({
            tree: this.tree, state: state ?? new HostState(), config, player, fonts,
            assets: assets ?? new StoreAssetSource({ sprites, fonts, models, config }),
        });
        this.scripts = scripts ?? new ScriptRegistry();
        this.driver = createDriver({
            host: this.host, scripts: this.scripts, loader, onWarning,
        });
        this.pump = attachTransmitPump(this.driver, this.tree);
        this.layout = attachLayout(this.host, { root });
        /*
         * The driver resolves layout at the end of each settlement pass. Its
         * own default merely clears the stale flag — which, with a real layout
         * attached, means the flag is cleared and nothing is laid out, so the
         * next `resolve()` skips and every box stays at its authored value.
         */
        this.driver.resolveLayout = () => this.layout.resolve();
        this.emitter = createEmitter({ tree: this.tree, layout: this.layout });
        this.hits = createHitTester({ tree: this.tree, layout: this.layout });
        this.painter = surface
            ? createPainter({ surface, sprites, fonts, models })
            : null;

        /*
         * Input is QUEUED, not handled.
         *
         * A pointer event that arrived while a script was parked must be
         * answered against the settled tree, not the intermediate one — and a
         * DOM handler must never mutate the tree directly, which is how the
         * old runtime ended up needing a worker protocol to keep the two
         * apart.
         */
        this.input = [];
        this.hoveredComponentId = -1;
        this.pressed = null;

        this.clockMs = 0;
        this._tickDebt = 0;
        this.stats = { frames: 0, painted: 0, ticks: 0, intents: 0 };
    }

    /* --------------------------------------------------------------
     * Input
     * ----------------------------------------------------------- */

    /** Queue a normalized pointer or key event. */
    post(event) {
        /* Motion coalesces: only the latest position matters, and a burst of
         * them would otherwise each cost a hit test and a hover dispatch. */
        if( event.type === 'move' )
        {
            const last = this.input[this.input.length - 1];
            if( last && last.type === 'move' ) { this.input[this.input.length - 1] = event; return; }
        }
        this.input.push(event);
    }

    /* --------------------------------------------------------------
     * The frame
     * ----------------------------------------------------------- */

    /**
     * Advance to `nowMs` and produce at most one frame.
     *
     * Returns true when something was painted. False means the interface is
     * idle or still settling, and in both cases the previous frame stands.
     */
    async frame(nowMs) {
        this.stats.frames++;

        this._advanceClock(nowMs);
        /* `wait: false`: a frame must never block on a load. A parked script
         * leaves the previous frame showing and the next frame resumes it. */
        await this.driver.settle({ wait: false });
        if( !this.driver.settled ) return false; /* parked: do not interact or paint */

        if( this.input.length )
        {
            this._interact();
            await this.driver.settle({ wait: false });
            if( !this.driver.settled ) return false;
        }

        this.layout.resolve();
        const changed = this.emitter.walk({ hoveredComponentId: this.hoveredComponentId });
        if( !changed ) return false;

        if( this.painter )
        {
            this.painter.paint(this.emitter.commands, {
                width: this.layout.root.width, height: this.layout.root.height,
            });
            this.stats.painted++;
        }
        return true;
    }

    /**
     * Run the logic ticks owed since the last frame.
     *
     * Fixed 20 ms with bounded catch-up: a stall that produced fifty ticks'
     * worth of debt must not run fifty ticks, because the next frame would
     * then stall too and the client would never recover.
     */
    _advanceClock(nowMs) {
        if( this.clockMs === 0 ) { this.clockMs = nowMs; return; }
        this._tickDebt += nowMs - this.clockMs;
        this.clockMs = nowMs;

        let ticks = 0;
        while( this._tickDebt >= TICK_MS && ticks < MAX_CATCHUP_TICKS )
        {
            this._tickDebt -= TICK_MS;
            ticks++;
            this.stats.ticks++;
            this.host.clock.advance(1);
            this.pump.tick();
        }
        if( ticks === MAX_CATCHUP_TICKS ) this._tickDebt = 0;
    }

    /* --------------------------------------------------------------
     * Interaction
     * ----------------------------------------------------------- */

    /**
     * Turn queued input into hook dispatches.
     *
     * This produces INTENTS and enqueues them; it never runs a script itself,
     * which is the C client's split (UI returns intents, the app dispatches)
     * and is what keeps a DOM event handler from re-entering the VM.
     */
    _interact() {
        const events = this.input;
        this.input = [];

        for( const event of events )
        {
            switch( event.type )
            {
            case 'move': this._pointerMove(event); break;
            case 'down': this._pointerDown(event); break;
            case 'up': this._pointerUp(event); break;
            case 'wheel': this._wheel(event); break;
            case 'key': this._key(event); break;
            default: break;
            }
        }
    }

    _pointerMove(event) {
        const hovered = this.hits.hoverTarget(event.x, event.y, {
            hoveredComponentId: this.hoveredComponentId,
        });
        if( hovered !== this.hoveredComponentId )
        {
            /* Leave before enter, so a script that owns both sees them in the
             * order the pointer actually moved. */
            this._dispatchOn(this.hoveredComponentId, 'onMouseLeave', event);
            this.hoveredComponentId = hovered;
            this._dispatchOn(hovered, 'onMouseOver', event);
        }
        else if( hovered >= 0 )
        {
            this._dispatchOn(hovered, 'onMouseRepeat', event);
        }
    }

    _pointerDown(event) {
        const node = this.hits.hitTest(event.x, event.y, {
            hoveredComponentId: this.hoveredComponentId,
        });
        this.pressed = node ? this.tree.ref(node.index) : null;
        if( node ) this._dispatchNode(node, 'onHold', event);
    }

    _pointerUp(event) {
        const node = this.hits.hitTest(event.x, event.y, {
            hoveredComponentId: this.hoveredComponentId,
        });
        const pressed = this.tree.resolve(this.pressed);
        this.pressed = null;
        if( !node ) return;
        /* A click is press and release on the SAME component — a press that
         * slid off is a cancelled click, not a click on whatever it landed on. */
        if( pressed && pressed.index !== node.index ) return;
        this._dispatchNode(node, 'onOp', event) || this._dispatchNode(node, 'onClick', event);
    }

    _wheel(event) {
        const node = this.hits.hitTest(event.x, event.y, {
            hoveredComponentId: this.hoveredComponentId,
        });
        if( node ) this._dispatchNode(node, 'onScrollWheel', event);
    }

    _key(event) {
        for( const index of this.tree.nodesWithHook('onKey') )
        {
            const node = this.tree.at(index);
            if( !node || this.tree.hiddenByAncestor(index) ) continue;
            this._dispatchNode(node, 'onKey', event);
        }
    }

    _dispatchOn(componentId, slot, event) {
        if( componentId < 0 ) return false;
        const node = this.tree.findByComponentId(componentId);
        return node ? this._dispatchNode(node, slot, event) : false;
    }

    /**
     * Queue one hook with the event context it must see.
     *
     * The context travels with the DISPATCH rather than being written into the
     * host, because a queued hook has to read the event that caused it and not
     * whatever the pointer has done since.
     */
    _dispatchNode(node, slot, event) {
        const binding = node.hooks?.[slot];
        if( !binding || binding.scriptId <= 0 ) return false;
        this.driver.dispatchHook(binding, {
            reason: slot,
            event: {
                mousex: event.x | 0, mousey: event.y | 0,
                com: node.componentId, comsubid: node.subId,
                opindex: event.op ?? 1,
                key: event.key ?? -1, keychar: event.char ?? -1,
            },
        });
        this.stats.intents++;
        return true;
    }

    /* --------------------------------------------------------------
     * Server-driven state
     * ----------------------------------------------------------- */

    /**
     * Apply a batch of server updates as ONE transaction.
     *
     * The C client holds every UI-affecting packet until the tick boundary so
     * a script observes the whole update at once; applying them one at a time
     * makes packet order matter, and it does not.
     */
    applyServerUpdate(update) {
        for( const [id, value] of Object.entries(update.varps ?? {}) )
        {
            this.host.state.setVarp(Number(id), value);
            this.pump.noteVarChanged(Number(id));
        }
        for( const [id, value] of Object.entries(update.varbits ?? {}) )
        {
            this.host.state.setVarbit(Number(id), value);
            this.pump.noteVarChanged(Number(id));
        }
        for( const id of update.inventories ?? [] ) this.pump.noteInvChanged(id);
        for( const scriptId of update.runScripts ?? [] )
            this.driver.dispatch(scriptId, [], { reason: 'server' });
    }

    /** Resize the canvas; queues the on-resize hooks the cache registered. */
    resize(width, height) {
        if( !this.layout.setRoot({ width, height }) ) return false;
        for( const index of this.tree.nodesWithHook('onResize') )
        {
            const node = this.tree.at(index);
            if( node ) this._dispatchNode(node, 'onResize', { x: 0, y: 0 });
        }
        return true;
    }
}
