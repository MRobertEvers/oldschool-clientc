import assert from 'node:assert/strict';

import {
    CHAT_REQUEST_NAMES,
    CHAT_SOCIAL_REQUEST_NAMES,
    SOCIAL_REQUEST_NAMES,
    SOCIAL_STATUS,
    addChatMessage,
    createChatSocialState,
    createChatState,
    createSocialState,
    displaySocialName,
    handleChatRequest,
    handleChatSocialRequest,
    handleSocialRequest,
    snapshotChatSocialState,
    snapshotChatState,
    snapshotSocialState,
    socialNameKey,
} from '../src/host_chat_social.js';

assert.equal(SOCIAL_REQUEST_NAMES.size, 12);
assert.equal(CHAT_REQUEST_NAMES.size, 22);
assert.equal(CHAT_SOCIAL_REQUEST_NAMES.size, 34);
assert(SOCIAL_REQUEST_NAMES.has('FRIEND_ADD'));
assert(CHAT_REQUEST_NAMES.has('CHAT_GETHISTORYEX_BYUID'));
assert.throws(() => SOCIAL_REQUEST_NAMES.add('NOPE'), TypeError);
assert.throws(() => CHAT_REQUEST_NAMES.clear(), TypeError);

/* Base-37 identity is case folded, preserves zero-valued separator slots and
 * stops after the native routine's first twelve bytes. */
assert.equal(socialNameKey('bob_smith'), socialNameKey('BOB SMITH'));
assert.notEqual(socialNameKey('bob'), socialNameKey('b o b'));
assert.equal(socialNameKey('abcdefghijkl-one'), socialNameKey('abcdefghijkl-two'));
assert.equal(displaySocialName('bob_smith'), 'Bob Smith');
assert.equal(displaySocialName('aLICE__mcDUFF'), 'ALICE  McDUFF');

const social = createSocialState();
assert.doesNotThrow(() => JSON.stringify(social));
assert.deepEqual(handleSocialRequest(social, 'FRIEND_COUNT'), { result: 0, changed: false });
assert.deepEqual(handleSocialRequest(social, 'IGNORE_COUNT'), { result: 0, changed: false });
assert.deepEqual(handleSocialRequest(social, 'FRIEND_GETNAME', { index: -1 }), {
    result: ['', ''], changed: false,
});
assert.deepEqual(handleSocialRequest(social, 'IGNORE_GETNAME', { index: 10 }), {
    result: ['', ''], changed: false,
});
assert.equal(handleSocialRequest(social, 'FRIEND_GETWORLD', { index: 0 }).result, 0);
assert.equal(handleSocialRequest(social, 'FRIEND_GETRANK', { index: 0 }).result, 0);

social.serverStatus = SOCIAL_STATUS.loading;
assert.equal(handleSocialRequest(social, 'FRIEND_COUNT').result, -1);
assert.equal(handleSocialRequest(social, 'IGNORE_COUNT').result, 0,
    'ignore count is never a loading sentinel');
social.serverStatus = SOCIAL_STATUS.connected;

let answer = handleSocialRequest(social, 'FRIEND_ADD', { name: 'bob_smith' });
assert.deepEqual(answer, {
    result: null,
    changed: true,
    service: { kind: 'friend_add', name: 'bob_smith' },
});
assert.equal(social.friends[0].world, 0, 'a locally added friend starts offline');
assert.deepEqual(handleSocialRequest(social, 'FRIEND_GETNAME', { index: 0 }).result,
    ['Bob Smith', '']);
assert.equal(handleSocialRequest(social, 'FRIEND_TEST', { name: 'BOB SMITH' }).result, 1);

answer = handleSocialRequest(social, 'FRIEND_ADD', { name: 'Bob Smith' });
assert.equal(answer.changed, false, 'a normalized duplicate does not alter the local list');
assert.deepEqual(answer.service, { kind: 'friend_add', name: 'Bob Smith' },
    'the native client still queues the requested add');

handleSocialRequest(social, 'FRIEND_ADD', { name: 'carol' });
handleSocialRequest(social, 'FRIEND_ADD', { name: 'dave' });
answer = handleSocialRequest(social, 'FRIEND_DEL', { name: 'CAROL' });
assert.equal(answer.changed, true);
assert.deepEqual(social.friends.map((entry) => entry.name), ['bob_smith', 'dave'],
    'deletion closes the gap without swapping the tail');
answer = handleSocialRequest(social, 'FRIEND_DEL', { name: 'nobody' });
assert.equal(answer.changed, false);
assert.equal(answer.service.kind, 'friend_del');
assert.deepEqual(handleSocialRequest(social, 'FRIEND_ADD', { name: '' }), {
    result: null, changed: false,
});

answer = handleSocialRequest(social, 'IGNORE_ADD', { name: 'eve' });
assert.equal(answer.changed, true);
assert.equal(handleSocialRequest(social, 'IGNORE_TEST', { name: 'EVE' }).result, 1);
assert.equal(handleSocialRequest(social, 'FRIEND_TEST', { name: 'eve' }).result, 0);
assert.deepEqual(handleSocialRequest(social, 'IGNORE_GETNAME', { index: 0 }).result, ['Eve', '']);
assert.equal(handleSocialRequest(social, 'IGNORE_DEL', { name: 'Eve' }).changed, true);

const zeroHash = createSocialState({ friends: ['___'] });
assert.equal(handleSocialRequest(zeroHash, 'FRIEND_TEST', { name: '___' }).result, 1);
assert.equal(handleSocialRequest(zeroHash, 'FRIEND_TEST', { name: '__' }).result, 0,
    'hash-zero names use the native literal fallback');

const collision = createSocialState();
assert.equal(handleSocialRequest(collision, 'FRIEND_ADD', {
    name: 'abcdefghijkl-one',
}).changed, true);
assert.equal(handleSocialRequest(collision, 'FRIEND_ADD', {
    name: 'abcdefghijkl-two',
}).changed, false, 'the first twelve base-37 digits are the complete identity');

const fullSocial = createSocialState();
for( let index = 0; index < 200; index++ ) {
    assert.equal(handleSocialRequest(fullSocial, 'FRIEND_ADD', {
        name: `friend${index}`,
    }).changed, true);
}
answer = handleSocialRequest(fullSocial, 'FRIEND_ADD', { name: 'overflow' });
assert.equal(answer.changed, false);
assert.equal(answer.service.kind, 'friend_add', 'a full local list still emits the service intent');
for( let index = 0; index < 100; index++ ) {
    assert.equal(handleSocialRequest(fullSocial, 'IGNORE_ADD', {
        name: `ignore${index}`,
    }).changed, true);
}
assert.equal(handleSocialRequest(fullSocial, 'IGNORE_ADD', { name: 'overflow' }).changed, false);

const longTarget = 'x'.repeat(80);
answer = handleSocialRequest(createSocialState(), 'FRIEND_ADD', { name: longTarget });
assert.equal(answer.service.name.length, 31, 'outbound names use the C host queue buffer');
assert.equal(answer.changed, true);

const socialSnapshot = snapshotSocialState(social);
assert.deepEqual(JSON.parse(JSON.stringify(createSocialState(socialSnapshot))), socialSnapshot);
socialSnapshot.friends[0].name = 'mutated';
assert.notEqual(social.friends[0].name, 'mutated', 'social snapshots do not alias live state');

/* Chat settings are local round trips; sends produce outbound intents but do
 * not fabricate an incoming history line. */
const chat = createChatState();
assert.deepEqual(chat.filters, { public: 0, private: 0, trade: 0 });
assert.equal(handleChatRequest(chat, 'CHAT_PLAYERNAME').result, '');
assert.equal(handleChatRequest(chat, 'STAFFMODLEVEL').result, 0);
answer = handleChatRequest(chat, 'CHAT_SETFILTER', {
    public_mode: 1,
    private_mode: 2,
    trade_mode: 3,
});
assert.deepEqual(answer, {
    result: null,
    changed: true,
    service: { kind: 'chat_setmode', modes: [1, 2, 3] },
});
assert.equal(handleChatRequest(chat, 'CHAT_GETFILTER_PUBLIC').result, 1);
assert.equal(handleChatRequest(chat, 'CHAT_GETFILTER_PRIVATE').result, 2);
assert.equal(handleChatRequest(chat, 'CHAT_GETFILTER_TRADE').result, 3);
assert.equal(handleChatRequest(chat, 'CHAT_SETFILTER', {
    public_mode: 1, private_mode: 2, trade_mode: 3,
}).changed, true, 'the C host dirties/transmits even an identical mode write');

answer = handleChatRequest(chat, 'CHAT_SETMESSAGEFILTER', { text: 'needle' });
assert.deepEqual(answer, { result: null, changed: true });
assert.equal(handleChatRequest(chat, 'CHAT_GETMESSAGEFILTER').result, 'needle');
assert.equal(handleChatRequest(chat, 'CHAT_SETMESSAGEFILTER', { text: 'needle' }).changed, true);
handleChatRequest(chat, 'CHAT_SETTIMESTAMPS', { timestamps: 7 });
assert.equal(handleChatRequest(chat, 'CHAT_GETTIMESTAMPS').result, 7);

const beforeSend = chat.messages.length;
answer = handleChatRequest(chat, 'CHAT_SENDPRIVATE', { name: 'Bob', text: 'hi there' });
assert.deepEqual(answer.service, { kind: 'message_private', name: 'Bob', text: 'hi there' });
assert.equal(answer.changed, false);
answer = handleChatRequest(chat, 'CHAT_SENDPUBLIC', { text: 'hello', colour_effect: 0x302 });
assert.deepEqual(answer.service, {
    kind: 'message_public', text: 'hello', colour_effect: 0x302,
});
assert.deepEqual(handleChatRequest(chat, 'DOCHEAT', { text: 'tele 0,50,50' }).service, {
    kind: 'cheat', text: 'tele 0,50,50',
});
assert.equal(chat.messages.length, beforeSend, 'outbound sends do not echo into local history');
assert.deepEqual(handleChatRequest(chat, 'CHAT_SENDPRIVATE', { name: '', text: 'hi' }), {
    result: null, changed: false,
});
assert.deepEqual(handleChatRequest(chat, 'CHAT_SENDPUBLIC', { text: '' }), {
    result: null, changed: false,
});
assert.deepEqual(handleChatRequest(chat, 'DOCHEAT', { text: '' }), {
    result: null, changed: false,
});
assert.deepEqual(handleChatRequest(chat, 'CHAT_SENDCLAN', { text: 'ignored' }), {
    result: null, changed: false,
});

/* MES enters the same type-0 history as server messages, including an empty
 * line, and uses the host clock supplied with the request. */
answer = handleChatRequest(chat, 'MES', { text: 'Welcome', clock: 123 });
assert.deepEqual(answer, { result: null, changed: true });
assert.deepEqual(chat.messages[0], {
    uid: 0, clock: 123, type: 0, name: '', sender: '', text: 'Welcome',
});
handleChatRequest(chat, 'MES', { text: '', clock: 124 });
assert.equal(handleChatRequest(chat, 'CHAT_GETHISTORYLENGTH', { type: 0 }).result, 2);
assert.equal(handleChatRequest(chat, 'CHAT_GETHISTORYLENGTH', { type: -1 }).result, 0);

const historySocial = createSocialState({
    friends: ['bob'],
    ignores: ['mallory'],
});
addChatMessage(chat, {
    type: 2, name: '<img=0>Bob', sender: 'BOB', text: 'friend line', clock: 125,
});
addChatMessage(chat, {
    type: 2, name: 'Mallory', sender: 'mallory', text: 'ignored line', clock: 126,
});

assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORY_BYTYPEANDLINE', {
    type: 2, line: 0,
}, historySocial).result, [3, 126, 'Mallory', 'mallory', 'ignored line', 2]);
assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORY_BYTYPEANDLINE', {
    type: 2, line: 1,
}, historySocial).result, [2, 125, '<img=0>Bob', 'BOB', 'friend line', 1]);
assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORY_BYUID', {
    uid: 2,
}, historySocial).result, [2, 125, '<img=0>Bob', 'BOB', 'friend line', 1]);
assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORYEX_BYUID', {
    uid: 3,
}, historySocial).result, [2, 126, 'Mallory', 'mallory', 'ignored line', 2, '', 0]);
assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORY_BYUID', {
    uid: 999,
}, historySocial).result, [-1, 0, '', '', '', 0]);
assert.deepEqual(handleChatRequest(chat, 'CHAT_GETHISTORYEX_BYTYPEANDLINE', {
    type: 2, line: 999,
}, historySocial).result, [-1, 0, '', '', '', 0, '', 0]);

assert.equal(handleChatRequest(chat, 'CHAT_GETPREVUID', { uid: 3 }).result, 2);
assert.equal(handleChatRequest(chat, 'CHAT_GETNEXTUID', { uid: 2 }).result, 3);
assert.equal(handleChatRequest(chat, 'CHAT_GETNEXTUID', { uid: 3 }).result, -1);
assert.equal(handleChatRequest(chat, 'CHAT_GETPREVUID', { uid: 999 }).result, -1);

/* Each type owns a 100-line ring. Evicting one type's oldest node removes it
 * from global uid order without disturbing interleaved types. */
const ring = createChatState();
addChatMessage(ring, { type: 7, text: 'keep-me' });
for( let index = 0; index < 101; index++ )
    addChatMessage(ring, { type: 0, text: `m${index}`, clock: index });
assert.equal(handleChatRequest(ring, 'CHAT_GETHISTORYLENGTH', { type: 0 }).result, 100);
assert.equal(handleChatRequest(ring, 'CHAT_GETHISTORYLENGTH', { type: 7 }).result, 1);
assert.equal(handleChatRequest(ring, 'CHAT_GETHISTORY_BYUID', { uid: 1 }).result[0], -1,
    'the oldest type-0 node was unlinked');
assert.equal(handleChatRequest(ring, 'CHAT_GETHISTORY_BYUID', { uid: 0 }).result[4], 'keep-me');
assert.equal(handleChatRequest(ring, 'CHAT_GETHISTORY_BYTYPEANDLINE', {
    type: 0, line: 99,
}).result[4], 'm1');

const combined = createChatSocialState({
    chat: JSON.parse(JSON.stringify(chat)),
    social: JSON.parse(JSON.stringify(historySocial)),
});
assert.equal(handleChatSocialRequest(combined, 'FRIEND_TEST', { name: 'Bob' }).result, 1);
assert.equal(handleChatSocialRequest(combined, 'CHAT_GETHISTORYEX_BYUID', {
    uid: 3,
}).result[5], 2);
const chatSnapshot = snapshotChatState(chat);
chatSnapshot.messages[0].text = 'mutated';
assert.notEqual(chat.messages[0].text, 'mutated', 'chat snapshots do not alias live state');
const combinedSnapshot = snapshotChatSocialState(combined);
assert.deepEqual(JSON.parse(JSON.stringify(createChatSocialState(combinedSnapshot))), combinedSnapshot);

assert.throws(() => handleSocialRequest(social, 'NOT_SOCIAL'), RangeError);
assert.throws(() => handleChatRequest(chat, 'NOT_CHAT'), RangeError);
assert.throws(() => handleChatSocialRequest(combined, 'NOT_ANYTHING'), RangeError);

process.stdout.write('host_chat_social_test: ok\n');
