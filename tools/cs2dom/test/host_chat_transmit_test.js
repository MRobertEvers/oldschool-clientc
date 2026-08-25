import assert from 'node:assert/strict';

import { createHostRuntime } from '../src/host_runtime.js';

function layer(fileId, name, hooks = {}, { hidden = false, parent = null } = {}) {
    return {
        fileId,
        name,
        kind: 'Layer',
        type: 0,
        layer: parent,
        static: {
            x: 0,
            y: 0,
            width: 100,
            height: 100,
            widthMode: 0,
            heightMode: 0,
            hidden,
        },
        hooks,
        events: {},
        ops: [],
        triggers: {},
        dynamic: [],
        dependencies: [],
        rawFields: {},
    };
}

function binding(scriptId) {
    return { script: { id: scriptId }, args: [] };
}

function fixtureIr() {
    return {
        interfaceId: 707,
        components: [
            layer(0, 'root', { onfriendtransmit: binding(11) }),
            layer(1, 'chat', { onchattransmit: binding(12) }, { parent: 0 }),
            layer(2, 'hidden', {
                onfriendtransmit: binding(13),
                onchattransmit: binding(14),
            }, { hidden: true, parent: 0 }),
        ],
    };
}

/* A local social mutation only raises the native-style dirty flag.  Its hook
 * runs at the next tick boundary, and a mutation made by that hook is retained
 * for one more tick instead of synchronously re-entering the listener. */
const seen = [];
let nestedMutation = false;
let nestedReturnedAt = -1;
let rewriteChatBinding = false;
const host = createHostRuntime(fixtureIr(), {
    viewport: { width: 100, height: 100 },
    invoke: (intent, runtime) => {
        seen.push(intent);
        if( intent.hook.canonical !== 'on_friend_transmit' ) return;
        if( !nestedMutation ) {
            nestedMutation = true;
            runtime.request({ kind: 'FRIEND_ADD', name: 'bob' });
            nestedReturnedAt = seen.length;
        }
        if( rewriteChatBinding )
            runtime.setHook('chat', 'on_chat_transmit', binding(99));
    },
});

assert.equal(host.request({ kind: 'CLIENTCLOCK' }), 100,
    'the native client clock starts at 100');
assert.equal(host.request({ kind: 'FRIEND_ADD', name: 'alice' }), null);
assert.equal(seen.length, 0, 'a friend mutation dispatched its hook synchronously');
assert.deepEqual(host.snapshot().pendingTransmits, { friend: true, chat: false });

let tick = host.dispatch({ type: 'tick' });
assert.deepEqual(seen.map((intent) => intent.hook.canonical), ['on_friend_transmit']);
assert.equal(tick.intents.length, 1);
assert.deepEqual(tick.intents[0].event, { type: 'transmit', kind: 'friend' });
assert.equal(nestedReturnedAt, 1,
    'a social mutation inside the listener synchronously re-entered it');
assert.equal(host.request({ kind: 'FRIEND_COUNT' }), 2);
assert.deepEqual(host.snapshot().pendingTransmits, { friend: true, chat: false },
    'the listener mutation was cleared with the already-pumped dirty flag');

seen.length = 0;
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(seen.map((intent) => intent.hook.canonical), ['on_friend_transmit']);
assert.equal(tick.intents.length, 1);
assert.deepEqual(host.snapshot().pendingTransmits, { friend: false, chat: false });

seen.length = 0;
host.dispatch({ type: 'tick' });
assert.equal(seen.length, 0, 'a cleared friend dirty flag fired forever');

/* The C pump queues friend before chat and snapshots both hook records before
 * either runs.  Rebinding the chat listener from the friend callback therefore
 * affects the following pump, not the already-queued chat task. */
rewriteChatBinding = true;
host.request({ kind: 'CHAT_SETFILTER', public_mode: 1, private_mode: 2, trade_mode: 3 });
host.request({ kind: 'MES', text: 'first line' });
assert.equal(seen.length, 0, 'chat/social requests ran transmit hooks before a tick');
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => [
    intent.hook.canonical,
    intent.hook.scriptId,
    intent.event.kind,
]), [
    ['on_friend_transmit', 11, 'friend'],
    ['on_chat_transmit', 12, 'chat'],
]);
assert(!tick.intents.some((intent) => intent.hook.scriptId === 13 || intent.hook.scriptId === 14),
    'hidden transmit listeners were included in the native-style snapshot');

/* Multiple writes to one channel coalesce to one listener pass per tick. */
rewriteChatBinding = false;
seen.length = 0;
host.request({ kind: 'MES', text: 'second line' });
host.request({ kind: 'MES', text: 'third line' });
host.request({ kind: 'CHAT_SETMESSAGEFILTER', text: 'line' });
tick = host.dispatch({ type: 'tick' });
assert.deepEqual(tick.intents.map((intent) => [intent.hook.canonical, intent.hook.scriptId]), [
    ['on_chat_transmit', 99],
]);

/* A duplicate add emits its outbound service request but does not repaint the
 * locally unchanged friends list. */
seen.length = 0;
host.request({ kind: 'FRIEND_ADD', name: 'ALICE' });
assert.deepEqual(host.snapshot().pendingTransmits, { friend: false, chat: false });
host.dispatch({ type: 'tick' });
assert.equal(seen.length, 0);

/* CLIENTCLOCK is independent of a caller-supplied tick number, persists in a
 * host snapshot, and is the authoritative timestamp for MES history. */
const seeded = createHostRuntime(fixtureIr(), {
    state: { clock: 444, clientClock: 777 },
    viewport: { width: 100, height: 100 },
});
assert.equal(seeded.request({ kind: 'CLIENTCLOCK' }), 777,
    'state.clientClock did not override the legacy state.clock seed');
seeded.request({ kind: 'MES', text: 'saved', clock: 9999 });
assert.equal(seeded.request({
    kind: 'CHAT_GETHISTORY_BYTYPEANDLINE', type: 0, line: 0,
})[1], 777, 'MES trusted a caller clock instead of the native HOST clock');

const saved = seeded.snapshot();
assert.equal(saved.clientClock, 777);
assert.equal(saved.state.clock, 777);
assert.equal(saved.chatSocial.chat.clientClock, 777);
assert.deepEqual(saved.pendingTransmits, { friend: false, chat: true });

const restoredSeen = [];
const restored = createHostRuntime(fixtureIr(), {
    ...saved,
    invoke: (intent) => restoredSeen.push(intent),
});
assert.equal(restored.request({ kind: 'CLIENTCLOCK' }), 777);
assert.equal(restoredSeen.length, 0, 'restoring a pending transmit dispatched it eagerly');
restored.dispatch({ type: 'tick', cycle: 5000 });
assert.equal(restored.request({ kind: 'CLIENTCLOCK' }), 778,
    'an explicit cycle replaced the once-per-tick client clock increment');
assert.deepEqual(restoredSeen.map((intent) => intent.hook.canonical), ['on_chat_transmit']);
restored.request({ kind: 'MES', text: 'after restore' });
assert.equal(restored.snapshot().chatSocial.chat.messages[0].clock, 778);

console.log('host chat/friend deferred transmit tests passed');
