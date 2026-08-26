/*
 * The chatbox's client state: the three channel filters, the search box, the
 * timestamp toggle, and the message ring the scrollback is built from.
 *
 * ------------------------------------------------------------------
 * An empty history is an ANSWER
 * ------------------------------------------------------------------
 *
 * A preview has received no messages, and that is a state the reference has
 * too — `rs_cs2_host.c` answers a NULL chat store with exactly the values a
 * message that has fallen out of its ring gives: length 0, uid -1, and the
 * six-tuple `(-1, 0, "", "", "", 0)`. So the chatbox script runs its whole
 * rebuild and writes no lines, which is what an empty chatbox is.
 *
 * Inventing a line or two would be worse than useless here: the scrollback
 * builder positions each line from the height of the one before it, so a
 * fabricated message moves every real one that follows.
 *
 * ------------------------------------------------------------------
 * The tuple order is the trap
 * ------------------------------------------------------------------
 *
 * The by-uid forms return the TYPE first and the by-type-and-line forms
 * return the UID. Same six values otherwise. Getting it backwards puts a chat
 * type where the script expects a message handle, and the walk then reads
 * message 2 forever.
 */

/** Chat filter channels, in the order `chat_setfilter` takes them. */
export const CHAT_FILTER = Object.freeze({ PUBLIC: 0, PRIVATE: 1, TRADE: 2 });

export class ChatState {
    constructor({
        filters = [0, 0, 0], messageFilter = '', timestamps = 0,
        playerName = '', messages = [],
    } = {}) {
        this.filters = [...filters];
        this.messageFilter = messageFilter;
        this.timestamps = timestamps;
        this.playerName = playerName;
        /** `{ uid, type, clock, name, sender, text, friendState }`, oldest first. */
        this.messages = [...messages];
        /** Bumped whenever a rebuild is due; the same channel a new message uses. */
        this.serial = 0;
    }

    byType(type) {
        return this.messages.filter((message) => message.type === (type | 0));
    }

    byUid(uid) {
        return this.messages.find((message) => message.uid === (uid | 0)) ?? null;
    }
}

/** The six values a missing message answers with, in the reference's order. */
function historyTuple(node, byUid, extended) {
    const head = node ? (byUid ? node.type : node.uid) : -1;
    const tuple = [
        head | 0,
        node ? node.clock | 0 : 0,
        node ? String(node.name ?? '') : '',
        node ? String(node.sender ?? '') : '',
        node ? String(node.text ?? '') : '',
        node ? node.friendState | 0 : 0,
    ];
    /* The `ex` forms carry two more that are reserved at this revision; the
     * reference pushes "" and 0 unconditionally, node or no node. */
    return extended ? [...tuple, '', 0] : tuple;
}

export function installChatOps(HostKernel) {
    const proto = HostKernel.prototype;

    /* --------------------------------------------------------------
     * Filters and settings
     * ----------------------------------------------------------- */

    proto.chat_setfilter = function (publicMode, privateMode, tradeMode) {
        this.calls++;
        this.chat.filters = [publicMode | 0, privateMode | 0, tradeMode | 0];
        this.chat.serial++;
    };

    proto.chat_getfilter_public = function () {
        this.calls++;
        return this.chat.filters[CHAT_FILTER.PUBLIC] | 0;
    };
    proto.chat_getfilter_private = function () {
        this.calls++;
        return this.chat.filters[CHAT_FILTER.PRIVATE] | 0;
    };
    proto.chat_getfilter_trade = function () {
        this.calls++;
        return this.chat.filters[CHAT_FILTER.TRADE] | 0;
    };

    /*
     * The search box above the tabs. A filter change re-selects which lines
     * are visible, so it bumps the same serial a new message does — it is the
     * same rebuild, and giving it a channel of its own would mean two paths
     * that can disagree about when the scrollback is stale.
     */
    proto.chat_setmessagefilter = function (text) {
        this.calls++;
        this.chat.messageFilter = String(text ?? '');
        this.chat.serial++;
    };
    proto.chat_getmessagefilter = function () {
        this.calls++;
        return this.chat.messageFilter;
    };

    proto.chat_settimestamps = function (mode) {
        this.calls++;
        this.chat.timestamps = mode | 0;
        this.chat.serial++;
    };
    proto.chat_gettimestamps = function () {
        this.calls++;
        return this.chat.timestamps;
    };

    /* The name the chat model echoes with, so a public line and the input line
     * above it cannot spell the player differently. Empty with no player. */
    proto.chat_playername = function () { this.calls++; return this.chat.playerName; };

    /* --------------------------------------------------------------
     * The ring
     * ----------------------------------------------------------- */

    proto.chat_gethistorylength = function (type) {
        this.calls++;
        return this.chat.byType(type).length;
    };

    /* -1 for "no such message", never 0 — 0 is a real uid. */
    proto.chat_getnextuid = function (uid) {
        this.calls++;
        const index = this.chat.messages.findIndex((message) => message.uid === (uid | 0));
        if( index < 0 ) return -1;
        return this.chat.messages[index + 1]?.uid ?? -1;
    };
    proto.chat_getprevuid = function (uid) {
        this.calls++;
        const index = this.chat.messages.findIndex((message) => message.uid === (uid | 0));
        if( index <= 0 ) return -1;
        return this.chat.messages[index - 1].uid;
    };

    proto.chat_gethistory_byuid = function (uid) {
        this.calls++;
        return historyTuple(this.chat.byUid(uid), true, false);
    };
    proto.chat_gethistoryex_byuid = function (uid) {
        this.calls++;
        return historyTuple(this.chat.byUid(uid), true, true);
    };
    proto.chat_gethistory_bytypeandline = function (type, line) {
        this.calls++;
        return historyTuple(this.chat.byType(type)[line | 0] ?? null, false, false);
    };
    proto.chat_gethistoryex_bytypeandline = function (type, line) {
        this.calls++;
        return historyTuple(this.chat.byType(type)[line | 0] ?? null, false, true);
    };

    /* --------------------------------------------------------------
     * Sending
     * ----------------------------------------------------------- */

    /*
     * Recorded, not performed. A preview has no server to accept a line, and
     * echoing it into the local ring would show the player a message that was
     * never sent — which is exactly the failure the real client's
     * optimistic-echo bugs produce.
     */
    const SENT = {
        chat_sendpublic: ['chatPublic', ['text', 'mode']],
        chat_sendprivate: ['chatPrivate', ['name', 'text']],
        chat_sendclan: ['chatClan', ['text', 'channel', 'mode']],
        chat_sendabusereport: ['abuseReport', ['name', 'rule', 'mute']],
    };
    for( const [method, [intent, fields]] of Object.entries(SENT) )
    {
        proto[method] = function (...args) {
            this.calls++;
            const payload = {};
            fields.forEach((field, index) => { payload[field] = args[index]; });
            this.client.intents.push({ intent, ...payload });
            this.onIntent?.(intent, payload);
        };
    }
}
