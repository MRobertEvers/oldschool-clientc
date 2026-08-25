/*
 * Pure React-host state for the C client's chat and social HOST requests.
 *
 * The C CS2VM still decodes the opcodes.  This module only owns the state that
 * the browser can answer truthfully: local chat history/settings, the
 * friend/ignore stores, and descriptions of outbound service requests.  It is
 * deliberately free of DOM and HostRuntime dependencies so a saved snapshot
 * can be JSON round-tripped and restored by createChatSocialState().
 */

export const SOCIAL_STATUS = Object.freeze({
    loading: 0,
    connecting: 1,
    connected: 2,
});

export const SOCIAL_LIMITS = Object.freeze({
    friends: 200,
    ignores: 100,
    nameBytes: 64,
    serviceNameBytes: 32,
});

export const CHAT_LIMITS = Object.freeze({
    types: 128,
    linesPerType: 100,
    senderBytes: 64,
    textBytes: 200,
    serviceNameBytes: 32,
    serviceTextBytes: 200,
});

const SOCIAL_NAMES = Object.freeze([
    'FRIEND_COUNT',
    'FRIEND_GETNAME',
    'FRIEND_GETWORLD',
    'FRIEND_GETRANK',
    'FRIEND_ADD',
    'FRIEND_DEL',
    'FRIEND_TEST',
    'IGNORE_COUNT',
    'IGNORE_GETNAME',
    'IGNORE_ADD',
    'IGNORE_DEL',
    'IGNORE_TEST',
]);

const CHAT_NAMES = Object.freeze([
    'MES',
    'STAFFMODLEVEL',
    'CHAT_GETFILTER_PUBLIC',
    'CHAT_SETFILTER',
    'CHAT_GETHISTORY_BYTYPEANDLINE',
    'CHAT_GETHISTORY_BYUID',
    'CHAT_GETFILTER_PRIVATE',
    'CHAT_SENDPUBLIC',
    'CHAT_SENDPRIVATE',
    'CHAT_SENDCLAN',
    'CHAT_PLAYERNAME',
    'CHAT_GETFILTER_TRADE',
    'CHAT_GETHISTORYLENGTH',
    'CHAT_GETNEXTUID',
    'CHAT_GETPREVUID',
    'DOCHEAT',
    'CHAT_SETMESSAGEFILTER',
    'CHAT_GETMESSAGEFILTER',
    'CHAT_SETTIMESTAMPS',
    'CHAT_GETTIMESTAMPS',
    'CHAT_GETHISTORYEX_BYTYPEANDLINE',
    'CHAT_GETHISTORYEX_BYUID',
]);

function immutableSet(values) {
    const target = new Set(values);
    const immutable = () => {
        throw new TypeError('request-name sets are immutable');
    };
    return Object.freeze(new Proxy(target, {
        get(set, property) {
            if( property === 'add' || property === 'delete' || property === 'clear' )
                return immutable;
            const value = Reflect.get(set, property, set);
            return typeof value === 'function' ? value.bind(set) : value;
        },
    }));
}

/** Exact names emitted by the generated C CS2VM HOST bridge. */
export const SOCIAL_REQUEST_NAMES = immutableSet(SOCIAL_NAMES);
export const CHAT_REQUEST_NAMES = immutableSet(CHAT_NAMES);
export const CHAT_SOCIAL_REQUEST_NAMES = immutableSet([...SOCIAL_NAMES, ...CHAT_NAMES]);

const SOCIAL_STATE = Symbol('cs2dom.social.state');
const CHAT_STATE = Symbol('cs2dom.chat.state');
const UTF8 = new TextEncoder();

function brand(state, symbol) {
    Object.defineProperty(state, symbol, { value: true });
    return state;
}

function assertState(state, symbol, label) {
    if( !state || state[symbol] !== true ) throw new TypeError(`${label} state is invalid`);
}

function int32(value, fallback = 0) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) | 0 : fallback | 0;
}

function cString(value) {
    const string = String(value ?? '');
    const nul = string.indexOf('\0');
    return nul < 0 ? string : string.slice(0, nul);
}

/* C's buffers include the trailing NUL. Keep the largest valid UTF-8 prefix
 * that fits before it; unlike a raw snprintf truncation, this cannot leave a
 * malformed JavaScript string at a multibyte boundary. */
function truncateUtf8(value, bufferBytes) {
    const source = cString(value);
    const maximum = Math.max(0, bufferBytes - 1);
    let bytes = 0;
    let result = '';
    for( const character of source ) {
        const length = UTF8.encode(character).length;
        if( bytes + length > maximum ) break;
        result += character;
        bytes += length;
    }
    return result;
}

function requestName(kind) {
    return String(kind ?? '').trim().toUpperCase();
}

function outcome(result, changed, service = null) {
    const value = { result, changed: Boolean(changed) };
    if( service ) value.service = service;
    return value;
}

/**
 * The exact net/jbase37.c key, represented as a decimal string so it remains
 * JSON serializable. Only the first twelve UTF-8 bytes participate; unknown
 * bytes occupy a base-37 digit with value zero.
 */
export function socialNameKey(value) {
    const bytes = UTF8.encode(cString(value));
    let key = 0n;
    for( let index = 0; index < Math.min(bytes.length, 12); index++ ) {
        const character = bytes[index];
        key *= 37n;
        if( character >= 65 && character <= 90 ) key += BigInt(character - 65 + 1);
        else if( character >= 97 && character <= 122 ) key += BigInt(character - 97 + 1);
        else if( character >= 48 && character <= 57 ) key += BigInt(character - 48 + 27);
    }
    return key.toString(10);
}

function asciiFold(value) {
    return value.replace(/[A-Z]/g, (character) => character.toLowerCase());
}

/** Reference JString.toScreenName used by FRIEND/IGNORE_GETNAME. */
export function displaySocialName(value) {
    const source = truncateUtf8(value, SOCIAL_LIMITS.nameBytes);
    let wordStart = true;
    let result = '';
    for( const raw of source ) {
        let character = raw === '_' ? ' ' : raw;
        if( wordStart && character >= 'a' && character <= 'z' )
            character = character.toUpperCase();
        wordStart = character === ' ';
        result += character;
    }
    return result;
}

function findSocialEntry(entries, requestedName) {
    const name = cString(requestedName);
    const key = socialNameKey(name);
    if( key !== '0' ) return entries.findIndex((entry) => socialNameKey(entry.name) === key);
    const literal = asciiFold(name);
    return entries.findIndex((entry) => asciiFold(entry.name) === literal);
}

function addFriend(state, requestedName, world) {
    const name = cString(requestedName);
    if( !name || state.friends.length >= SOCIAL_LIMITS.friends ||
        findSocialEntry(state.friends, name) >= 0 ) return false;
    state.friends.push({
        name: truncateUtf8(name, SOCIAL_LIMITS.nameBytes),
        world: int32(world),
    });
    return true;
}

function addIgnore(state, requestedName) {
    const name = cString(requestedName);
    if( !name || state.ignores.length >= SOCIAL_LIMITS.ignores ||
        findSocialEntry(state.ignores, name) >= 0 ) return false;
    state.ignores.push({ name: truncateUtf8(name, SOCIAL_LIMITS.nameBytes) });
    return true;
}

/** Connected and empty by default, matching RS_Social_Init. */
export function createSocialState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    const state = brand({
        serverStatus: int32(seed.serverStatus ?? seed.server_status, SOCIAL_STATUS.connected),
        nodeId: int32(seed.nodeId ?? seed.node_id, 1),
        friends: [],
        ignores: [],
    }, SOCIAL_STATE);

    const friends = Array.isArray(seed.friends) ? seed.friends : [];
    for( const candidate of friends ) {
        const entry = typeof candidate === 'string' ? { name: candidate } : candidate;
        if( !entry || typeof entry !== 'object' ) continue;
        addFriend(state, entry.name, entry.world ?? 0);
    }
    const ignores = Array.isArray(seed.ignores) ? seed.ignores : [];
    for( const candidate of ignores ) {
        const entry = typeof candidate === 'string' ? { name: candidate } : candidate;
        if( !entry || typeof entry !== 'object' ) continue;
        addIgnore(state, entry.name);
    }
    return state;
}

/** Detach a social snapshot suitable for persistence or a React state update. */
export function snapshotSocialState(state) {
    assertState(state, SOCIAL_STATE, 'social');
    return {
        serverStatus: state.serverStatus,
        nodeId: state.nodeId,
        friends: state.friends.map((entry) => ({ name: entry.name, world: entry.world })),
        ignores: state.ignores.map((entry) => ({ name: entry.name })),
    };
}

function socialService(kind, name) {
    return {
        kind,
        name: truncateUtf8(name, SOCIAL_LIMITS.serviceNameBytes),
    };
}

/** Execute one FRIEND_* or IGNORE_* request against a social state. */
export function handleSocialRequest(state, kind, request = {}) {
    assertState(state, SOCIAL_STATE, 'social');
    const name = requestName(kind);
    if( !SOCIAL_REQUEST_NAMES.has(name) )
        throw new RangeError(`unknown social HOST request ${name || '(empty)'}`);

    const index = int32(request.index);
    const requestedName = cString(request.name);
    switch( name ) {
    case 'FRIEND_COUNT':
        return outcome(state.serverStatus === SOCIAL_STATUS.connected ? state.friends.length : -1, false);
    case 'IGNORE_COUNT':
        return outcome(state.ignores.length, false);
    case 'FRIEND_GETNAME': {
        const entry = index >= 0 && index < state.friends.length ? state.friends[index] : null;
        return outcome([entry ? displaySocialName(entry.name) : '', ''], false);
    }
    case 'IGNORE_GETNAME': {
        const entry = index >= 0 && index < state.ignores.length ? state.ignores[index] : null;
        return outcome([entry ? displaySocialName(entry.name) : '', ''], false);
    }
    case 'FRIEND_GETWORLD':
        return outcome(index >= 0 && index < state.friends.length ? state.friends[index].world : 0,
            false);
    case 'FRIEND_GETRANK':
        return outcome(0, false);
    case 'FRIEND_TEST':
        return outcome(findSocialEntry(state.friends, requestedName) >= 0 ? 1 : 0, false);
    case 'IGNORE_TEST':
        return outcome(findSocialEntry(state.ignores, requestedName) >= 0 ? 1 : 0, false);
    case 'FRIEND_ADD': {
        if( !requestedName ) return outcome(null, false);
        const changed = addFriend(state, requestedName, 0);
        return outcome(null, changed, socialService('friend_add', requestedName));
    }
    case 'FRIEND_DEL': {
        if( !requestedName ) return outcome(null, false);
        const found = findSocialEntry(state.friends, requestedName);
        if( found >= 0 ) state.friends.splice(found, 1);
        return outcome(null, found >= 0, socialService('friend_del', requestedName));
    }
    case 'IGNORE_ADD': {
        if( !requestedName ) return outcome(null, false);
        const changed = addIgnore(state, requestedName);
        return outcome(null, changed, socialService('ignore_add', requestedName));
    }
    case 'IGNORE_DEL': {
        if( !requestedName ) return outcome(null, false);
        const found = findSocialEntry(state.ignores, requestedName);
        if( found >= 0 ) state.ignores.splice(found, 1);
        return outcome(null, found >= 0, socialService('ignore_del', requestedName));
    }
    default:
        throw new RangeError(`unhandled social HOST request ${name}`);
    }
}

function seededMessage(candidate) {
    if( !candidate || typeof candidate !== 'object' ) return null;
    const type = int32(candidate.type, -1);
    const uid = int32(candidate.uid, -1);
    if( type < 0 || type >= CHAT_LIMITS.types || uid < 0 ) return null;
    return {
        uid,
        clock: int32(candidate.clock),
        type,
        name: truncateUtf8(candidate.name, CHAT_LIMITS.senderBytes),
        sender: truncateUtf8(candidate.sender, CHAT_LIMITS.senderBytes),
        text: truncateUtf8(candidate.text, CHAT_LIMITS.textBytes),
    };
}

/** Empty client-side chat store and settings, optionally restored from JSON. */
export function createChatState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    const filters = seed.filters && typeof seed.filters === 'object' ? seed.filters : {};
    const state = brand({
        filters: {
            public: int32(filters.public ?? seed.publicMode ?? seed.public_mode),
            private: int32(filters.private ?? seed.privateMode ?? seed.private_mode),
            trade: int32(filters.trade ?? seed.tradeMode ?? seed.trade_mode),
        },
        playerName: truncateUtf8(seed.playerName ?? seed.player_name, CHAT_LIMITS.serviceNameBytes),
        messageFilter: truncateUtf8(seed.messageFilter ?? seed.message_filter, CHAT_LIMITS.textBytes),
        timestamps: int32(seed.timestamps),
        clientClock: int32(seed.clientClock ?? seed.client_clock, 100),
        nextUid: 0,
        messages: [],
    }, CHAT_STATE);

    const perType = new Int16Array(CHAT_LIMITS.types);
    const seenUids = new Set();
    for( const candidate of Array.isArray(seed.messages) ? seed.messages : [] ) {
        const message = seededMessage(candidate);
        if( !message || seenUids.has(message.uid) ||
            perType[message.type] >= CHAT_LIMITS.linesPerType ) continue;
        state.messages.push(message);
        seenUids.add(message.uid);
        perType[message.type]++;
    }
    let nextUid = int32(seed.nextUid ?? seed.next_uid, 0);
    if( nextUid < 0 ) nextUid = 0;
    for( const message of state.messages ) {
        if( message.uid >= nextUid ) nextUid = message.uid + 1;
    }
    state.nextUid = int32(nextUid);
    return state;
}

/** Detach a chat snapshot suitable for persistence or a React state update. */
export function snapshotChatState(state) {
    assertState(state, CHAT_STATE, 'chat');
    return {
        filters: { ...state.filters },
        playerName: state.playerName,
        messageFilter: state.messageFilter,
        timestamps: state.timestamps,
        clientClock: state.clientClock,
        nextUid: state.nextUid,
        messages: state.messages.map((message) => ({ ...message })),
    };
}

/** Add one native-shaped chat node and return the inserted JSON record. */
export function addChatMessage(state, { type = 0, name = '', sender = '', text = '', clock = 0 } = {}) {
    assertState(state, CHAT_STATE, 'chat');
    const messageType = int32(type, -1);
    if( messageType < 0 || messageType >= CHAT_LIMITS.types )
        throw new RangeError(`chat type ${messageType} is outside 0..${CHAT_LIMITS.types - 1}`);

    const message = {
        uid: state.nextUid | 0,
        clock: int32(clock),
        type: messageType,
        name: truncateUtf8(name, CHAT_LIMITS.senderBytes),
        sender: truncateUtf8(sender, CHAT_LIMITS.senderBytes),
        text: truncateUtf8(text, CHAT_LIMITS.textBytes),
    };
    state.nextUid = (state.nextUid + 1) | 0;
    state.messages.unshift(message);

    let count = 0;
    for( let index = 0; index < state.messages.length; index++ ) {
        if( state.messages[index].type !== messageType ) continue;
        count++;
        if( count > CHAT_LIMITS.linesPerType ) {
            state.messages.splice(index, 1);
            break;
        }
    }
    return message;
}

/** MES helper: a type-0 system line, stamped with the supplied client clock. */
export function addMesMessage(state, text, clock = state?.clientClock ?? 100) {
    return addChatMessage(state, { type: 0, text, clock });
}

function messageByUid(state, uid) {
    const wanted = int32(uid, -1);
    if( wanted < 0 ) return null;
    return state.messages.find((message) => message.uid === wanted) ?? null;
}

function messageByTypeAndLine(state, type, line) {
    const wantedType = int32(type, -1);
    let wantedLine = int32(line, -1);
    if( wantedType < 0 || wantedType >= CHAT_LIMITS.types || wantedLine < 0 ) return null;
    for( const message of state.messages ) {
        if( message.type !== wantedType ) continue;
        if( wantedLine-- === 0 ) return message;
    }
    return null;
}

function friendState(social, message) {
    if( !social || !message?.sender ) return 0;
    if( findSocialEntry(social.friends, message.sender) >= 0 ) return 1;
    if( findSocialEntry(social.ignores, message.sender) >= 0 ) return 2;
    return 0;
}

function historyResult(message, byUid, extended, social) {
    const result = message
        ? [
            byUid ? message.type : message.uid,
            message.clock,
            message.name,
            message.sender,
            message.text,
            friendState(social, message),
        ]
        : [-1, 0, '', '', '', 0];
    if( extended ) result.push('', 0);
    return result;
}

function serviceText(value) {
    return truncateUtf8(value, CHAT_LIMITS.serviceTextBytes);
}

/** Execute one CHAT_* / MES / DOCHEAT request against a chat state. */
export function handleChatRequest(state, kind, request = {}, social = null) {
    assertState(state, CHAT_STATE, 'chat');
    if( social !== null ) assertState(social, SOCIAL_STATE, 'social');
    const name = requestName(kind);
    if( !CHAT_REQUEST_NAMES.has(name) )
        throw new RangeError(`unknown chat HOST request ${name || '(empty)'}`);

    switch( name ) {
    case 'CHAT_GETFILTER_PUBLIC': return outcome(state.filters.public, false);
    case 'CHAT_GETFILTER_PRIVATE': return outcome(state.filters.private, false);
    case 'CHAT_GETFILTER_TRADE': return outcome(state.filters.trade, false);
    case 'CHAT_PLAYERNAME': return outcome(state.playerName, false);
    case 'STAFFMODLEVEL': return outcome(0, false);
    case 'CHAT_SETFILTER': {
        const publicMode = int32(request.public_mode ?? request.publicMode);
        const privateMode = int32(request.private_mode ?? request.privateMode);
        const tradeMode = int32(request.trade_mode ?? request.tradeMode);
        state.filters.public = publicMode;
        state.filters.private = privateMode;
        state.filters.trade = tradeMode;
        return outcome(null, true, {
            kind: 'chat_setmode',
            modes: [publicMode, privateMode, tradeMode],
        });
    }
    case 'CHAT_GETHISTORYLENGTH': {
        const type = int32(request.type, -1);
        if( type < 0 || type >= CHAT_LIMITS.types ) return outcome(0, false);
        return outcome(state.messages.reduce((count, message) =>
            count + (message.type === type ? 1 : 0), 0), false);
    }
    case 'CHAT_GETNEXTUID':
    case 'CHAT_GETPREVUID': {
        const message = messageByUid(state, request.uid);
        if( !message ) return outcome(-1, false);
        const index = state.messages.indexOf(message);
        const nextIndex = name === 'CHAT_GETNEXTUID' ? index - 1 : index + 1;
        return outcome(nextIndex >= 0 && nextIndex < state.messages.length
            ? state.messages[nextIndex].uid : -1, false);
    }
    case 'CHAT_GETHISTORY_BYUID':
    case 'CHAT_GETHISTORYEX_BYUID': {
        const extended = name === 'CHAT_GETHISTORYEX_BYUID';
        return outcome(historyResult(messageByUid(state, request.uid), true, extended, social), false);
    }
    case 'CHAT_GETHISTORY_BYTYPEANDLINE':
    case 'CHAT_GETHISTORYEX_BYTYPEANDLINE': {
        const extended = name === 'CHAT_GETHISTORYEX_BYTYPEANDLINE';
        const message = messageByTypeAndLine(state, request.type, request.line);
        return outcome(historyResult(message, false, extended, social), false);
    }
    case 'CHAT_SETMESSAGEFILTER':
        state.messageFilter = truncateUtf8(request.text, CHAT_LIMITS.textBytes);
        return outcome(null, true);
    case 'CHAT_GETMESSAGEFILTER':
        return outcome(state.messageFilter, false);
    case 'CHAT_SETTIMESTAMPS':
        state.timestamps = int32(request.timestamps);
        return outcome(null, true);
    case 'CHAT_GETTIMESTAMPS':
        return outcome(state.timestamps, false);
    case 'CHAT_SENDPRIVATE': {
        const target = cString(request.name);
        const text = cString(request.text);
        if( !target || !text ) return outcome(null, false);
        return outcome(null, false, {
            kind: 'message_private',
            name: truncateUtf8(target, CHAT_LIMITS.serviceNameBytes),
            text: serviceText(text),
        });
    }
    case 'CHAT_SENDPUBLIC': {
        const text = cString(request.text);
        if( !text ) return outcome(null, false);
        return outcome(null, false, {
            kind: 'message_public',
            text: serviceText(text),
            colour_effect: int32(request.colour_effect ?? request.colourEffect),
        });
    }
    case 'DOCHEAT': {
        const text = cString(request.text);
        if( !text ) return outcome(null, false);
        return outcome(null, false, { kind: 'cheat', text: serviceText(text) });
    }
    case 'CHAT_SENDCLAN':
        return outcome(null, false);
    case 'MES':
        addMesMessage(state, request.text, request.clock ?? state.clientClock);
        return outcome(null, true);
    default:
        throw new RangeError(`unhandled chat HOST request ${name}`);
    }
}

/** Construct the complete pure state used by handleChatSocialRequest. */
export function createChatSocialState(seed = {}) {
    if( !seed || typeof seed !== 'object' ) seed = {};
    return {
        chat: createChatState(seed.chat),
        social: createSocialState(seed.social),
    };
}

/** Detach both stores without retaining aliases to mutable runtime arrays. */
export function snapshotChatSocialState(state) {
    if( !state || typeof state !== 'object' ) throw new TypeError('chat/social state is invalid');
    return {
        chat: snapshotChatState(state.chat),
        social: snapshotSocialState(state.social),
    };
}

/** Dispatch either family while sharing social identity with chat history. */
export function handleChatSocialRequest(state, kind, request = {}) {
    if( !state || typeof state !== 'object' ) throw new TypeError('chat/social state is invalid');
    const name = requestName(kind);
    if( SOCIAL_REQUEST_NAMES.has(name) ) return handleSocialRequest(state.social, name, request);
    if( CHAT_REQUEST_NAMES.has(name) ) return handleChatRequest(state.chat, name, request, state.social);
    throw new RangeError(`unknown chat/social HOST request ${name || '(empty)'}`);
}
