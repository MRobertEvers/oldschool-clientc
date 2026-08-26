import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

import { IF_TYPE } from '../src/components.js';
import { createHostRuntime, HostRuntime, HostRuntimeError } from '../src/host_runtime.js';

function component(fileId, name, type, layer, extra = {}) {
    const props = {
        x: 0, y: 0, width: 128, height: 96,
        xMode: 0, yMode: 0, widthMode: 0, heightMode: 0,
        hidden: false, transparency: 0,
        ...(type === IF_TYPE.layer ? { scrollWidth: 256, scrollHeight: 192 } : {}),
        ...(type === IF_TYPE.text ? { text: '', font: 1, color: 0 } : {}),
        ...extra,
    };
    return {
        fileId, name, type, layer,
        kind: type === IF_TYPE.text ? 'Text'
            : type === IF_TYPE.graphic ? 'Graphic' : 'Layer',
        props, static: props, authoredProps: new Set(), dynamic: [], ops: [],
        events: {}, hooks: {}, triggers: {}, dependencies: [], rawFields: {},
    };
}

function directHost(recordChanges = false) {
    const host = createHostRuntime({
        interfaceId: 321,
        components: [
            component(0, 'root', IF_TYPE.layer, null),
            component(1, 'label', IF_TYPE.text, 0, { x: 4, y: 5 }),
        ],
    }, {
        recordChanges,
        clientClock: 777,
        state: { 'varbit:5': 41, 'varc:6': 42 },
        hostData: {
            enums: {
                10: { string: true, values: { 3: 'three' }, defaultString: 'missing' },
                11: { values: { 4: 44 }, defaultInt: -4 },
            },
            structs: { 20: { params: { 7: 700, 8: { string: 'seven' } } } },
            params: { 7: { defaultInt: -7 }, 8: { string: true, defaultString: '' } },
        },
    });
    host.request = () => { throw new Error('tagged request path was used'); };
    return host;
}

/* Change-recording mode retains deleted records until direct finalization;
 * replacement must still leave exactly one live incarnation in the IR. */
{
    const host = directHost(true);
    const root = host.ref('root');
    const before = host.commitRevision;
    host.beginCS2DirectInvocation();
    const first = host.CC_CREATE(root.componentId, IF_TYPE.text, 12, 0, 0, 0);
    const second = host.CC_CREATE(root.componentId, IF_TYPE.text, 12, 0, 0, 0);
    assert.equal(host.commitRevision, before);
    host.endCS2DirectInvocation();
    assert.equal(host.commitRevision, before + 1);
    assert.equal(host.resolve(first), null);
    assert(host.resolve(second));
    assert.equal(host.ir.components.filter((entry) => entry.runtimeDynamic).length, 1);
}

const DIRECT_ARITIES = Object.freeze({
    PUSH_VARBIT: 1, PUSH_VARC_INT: 1, CC_CREATE: 6, CC_DELETEALL: 1, CC_FIND: 3,
    CC_SETPOSITION: 5, CC_SETSIZE: 5, CC_SETHIDE: 2, CC_SETNOCLICKTHROUGH: 2,
    CC_SETSCROLLPOS: 3, CC_SETCOLOUR: 2, CC_SETFILL: 2, CC_SETTRANS: 2,
    CC_SETGRAPHIC: 2, CC_SETTILING: 2, CC_SETTEXT: 2, CC_SETTEXTFONT: 2,
    CC_SETTEXTALIGN: 4, CC_SETTEXTSHADOW: 2, CC_SETOP: 3,
    CC_SETDRAGGABLE: 3, CC_SETDRAGGABLEBEHAVIOR: 2,
    CC_SETONCLICK: 10, CC_SETONHOLD: 10, CC_SETONMOUSEOVER: 10,
    CC_SETONMOUSELEAVE: 10, CC_SETONDRAG: 10, CC_SETONVARTRANSMIT: 10,
    CC_SETONTIMER: 10, CC_SETONOP: 10, CC_SETONDRAGCOMPLETE: 10,
    CC_SETONMOUSEREPEAT: 10, CC_SETONSCROLLWHEEL: 10, CC_SETONKEY: 10,
    CC_GETY: 1, CC_GETHEIGHT: 1, CC_GETID: 1, CC_SETCOMPONENTPARAM: 5,
    IF_SETPOSITION: 5, IF_SETSIZE: 5, IF_SETHIDE: 2, IF_SETSCROLLPOS: 3,
    IF_SETCOLOUR: 2, IF_SETOP: 3, IF_SETOPBASE: 2,
    IF_SETONVARTRANSMIT: 10, IF_SETONTIMER: 10, IF_SETONOP: 10,
    IF_SETONSCROLLWHEEL: 10, IF_GETWIDTH: 1, IF_GETHEIGHT: 1,
    IF_GETSCROLLHEIGHT: 1, IF_SETPARAM: 5, CLIENTCLOCK: 1, ENUM: 4,
    ENUM_GETOUTPUTCOUNT: 1, STRUCT_PARAM: 2,
});
let directBatchBenchmarkMs = 0;

assert.equal(Object.keys(DIRECT_ARITIES).length, 57);
for( const [name, arity] of Object.entries(DIRECT_ARITIES) ) {
    assert.equal(typeof HostRuntime.prototype[name], 'function', `${name} is not a prototype method`);
    assert.equal(HostRuntime.prototype[name].length, arity, `${name} has unstable arity`);
}

/* Allocation/performance regression: a ca_tasks-sized direct builder should
 * retain only minimal VM targets. Timing is reported for comparison without a
 * machine-dependent hard assertion; the index/ref counts are exact. */
{
    const host = directHost();
    const root = host.ref('root');
    const keysBefore = host.byKey.size;
    const before = performance.now();
    host.beginCS2DirectInvocation();
    for( let index = 0; index < 4000; index++ ) {
        const child = host.CC_CREATE(
            root.componentId, IF_TYPE.text, index, 0, 0, 0);
        host.CC_SETPOSITION(child.componentId, index & 511, index >> 9, 0, 0);
        host.CC_SETSIZE(child.componentId, 32, 16, 0, 0);
        host.CC_SETTEXT(child.componentId, 'row');
        host.CC_SETCOLOUR(child.componentId, 0xff00);
        host.CC_SETOP(child.componentId, 1, 'Use');
    }
    host.endCS2DirectInvocation();
    directBatchBenchmarkMs = performance.now() - before;
    assert.equal(host.byKey.size, keysBefore,
        'direct builder eagerly populated the public component-key index');
    assert.equal(host.pendingPublicIndexes.length, 4000);
    assert.equal(host.ir.components.filter((component) =>
        host.meta.get(component)?.ref !== null).length, 2,
    'direct builder eagerly materialized public component refs');
}

/* A direct create/find target is generation-stable without eagerly entering
 * the public key/ref index. The first explicit public ref observation alone
 * pays that cost. */
{
    const host = directHost();
    const root = host.ref('root');
    const keysBefore = host.byKey.size;
    host.beginCS2DirectInvocation();
    const child = host.CC_CREATE(root.componentId, IF_TYPE.text, 77, 0, 0, 0);
    const component = host._component(child);
    const meta = host.meta.get(component);
    assert.equal(Object.hasOwn(child, 'key'), false);
    assert.equal(meta.ref, null);
    assert.equal(meta.key, null);
    assert.equal(host.byKey.size, keysBefore);
    host.CC_SETPOSITION(child.componentId, 3, 4, 0, 0);
    host.CC_SETTEXT(child.componentId, 'private');
    host.CC_SETONCLICK(child.componentId, 44, 'i', [], 0,
        [9], 1, [0, 0], 0, []);
    const found = host.CC_FIND(root.componentId, 77, 1);
    assert.strictEqual(found, child, 'CC_FIND allocated a second direct target');
    assert.equal(meta.ref, null);
    assert.equal(meta.key, null);
    assert.equal(host.byKey.size, keysBefore);
    host.endCS2DirectInvocation();
    assert.equal(meta.ref, null);
    assert.equal(host.byKey.size, keysBefore);

    const publicRef = host.ref(child);
    assert(publicRef.key);
    assert.equal(publicRef.generation, child.generation);
    assert.equal(host.byKey.size, keysBefore + 1);

    host.beginCS2DirectInvocation();
    const replacement = host.CC_CREATE(root.componentId, IF_TYPE.text, 77, 0, 0, 0);
    host.endCS2DirectInvocation();
    assert.equal(host.resolve(child), null,
        'minimal direct target crossed a replacement generation fence');
    assert(host.resolve(replacement));
}

/* Reads and cache-data calls stay positional even when the reflected request
 * surface is deliberately made unusable. */
{
    const host = directHost();
    assert.equal(host.PUSH_VARBIT(5), 41);
    assert.equal(host.PUSH_VARC_INT(6), 42);
    assert.equal(host.CLIENTCLOCK(0), 777);
    assert.equal(host.ENUM(105, 115, 10, 3), 'three');
    assert.equal(host.ENUM(105, 105, 11, 4), 44);
    assert.equal(host.ENUM_GETOUTPUTCOUNT(10), 1);
    assert.equal(host.STRUCT_PARAM(20, 7), 700);
    assert.equal(host.STRUCT_PARAM(20, 8), 'seven');
}

/* One direct transaction covers create/find, fresh setters, hooks, parameters,
 * getters and deletion while publishing exactly one renderer revision. */
{
    const host = directHost();
    const root = host.ref('root');
    const label = host.ref('label');
    const before = host.commitRevision;
    host.beginCS2DirectInvocation();
    const child = host.CC_CREATE(
        root.componentId, IF_TYPE.text, 9, 0, 0, 0);
    assert(child);
    host.CC_SETPOSITION(child.componentId, 12, 13, 0, 0);
    host.CC_SETSIZE(child.componentId, 80, 20, 0, 0);
    host.CC_SETHIDE(child.componentId, false);
    host.CC_SETNOCLICKTHROUGH(child.componentId, 1);
    host.CC_SETCOLOUR(child.componentId, 0x123456);
    host.CC_SETTRANS(child.componentId, 17);
    host.CC_SETTEXT(child.componentId, 'direct');
    host.CC_SETTEXTFONT(child.componentId, 7);
    host.CC_SETTEXTALIGN(child.componentId, 1, 2, 14);
    host.CC_SETTEXTSHADOW(child.componentId, 1);
    host.CC_SETOP(child.componentId, 2, 'Use');
    host.CC_SETDRAGGABLE(child.componentId, root.componentId, -1);
    host.CC_SETDRAGGABLEBEHAVIOR(child.componentId, 1);
    host.CC_SETONCLICK(child.componentId, 55, 'isY', [4, 5], 2,
        [9, 0], 2, [2, 0], 1, ['hook']);
    host.CC_SETONVARTRANSMIT(child.componentId, 56, 'iY', [7], 1,
        [11], 1, [0, 0], 0, []);
    host.CC_SETCOMPONENTPARAM(child.componentId, 1, 99, null, 105);
    host.IF_SETPARAM(child.componentId, 2, 0, 'value', 2);
    host.IF_SETOPBASE(child.componentId, 'Choose');
    assert.equal(host.CC_GETY(child.componentId), 13);
    assert.equal(host.CC_GETHEIGHT(child.componentId), 20);
    assert.equal(host.CC_GETID(child.componentId), 9);
    assert.equal(host.IF_GETWIDTH(child.componentId), 80);
    assert.equal(host.IF_GETHEIGHT(child.componentId), 20);
    assert.equal(host.IF_GETSCROLLHEIGHT(root.componentId), 192);
    const found = host.CC_FIND(root.componentId, 9, 1);
    assert.equal(found?.generation, child.generation);
    assert.equal(host.activeRef({ dot: true })?.generation, child.generation);
    host.IF_SETPOSITION(label.componentId, 8, 9, 0, 0);
    assert.equal(host.commitRevision, before,
        'a direct setter published before endCS2DirectInvocation');
    host.endCS2DirectInvocation();
    assert.equal(host.commitRevision, before + 1);

    const live = host.resolve(child);
    assert.equal(live.props.text, 'direct');
    assert.equal(live.props.color, 0x123456);
    assert.equal(live.props.noClickThrough, true);
    assert.equal(live.runtime.params[1].value, 99);
    assert.equal(live.runtime.params[2].string, 'value');
    assert.equal(live.runtime.opBase, 'Choose');
    assert.deepEqual(host._component(child).hooks.on_click, {
        script: { id: 55 }, args: [9, 'hook'], signature: 'isY', triggerIds: [4, 5],
    });

    const oldGeneration = child.generation;
    host.beginCS2DirectInvocation();
    const replacement = host.CC_CREATE(
        root.componentId, IF_TYPE.text, 9, 0, 0, 0);
    host.endCS2DirectInvocation();
    assert.notEqual(replacement.generation, oldGeneration);
    assert.equal(host.resolve(child), null, 'recreate did not fence the stale generation');

    host.beginCS2DirectInvocation();
    host.CC_DELETEALL(root.componentId);
    host.endCS2DirectInvocation();
    assert.equal(host.CC_FIND(root.componentId, 9, 0), null);
    assert.equal(host.ir.components.some((entry) => entry.runtimeDynamic), false,
        'direct delete batch left retired components in the render tree');
}

/* Missing explicit targets are native no-ops/default reads, not stale-ref
 * exceptions, across both CC and IF direct methods. */
{
    const host = directHost();
    host.beginCS2DirectInvocation();
    assert.equal(host.CC_CREATE(-1, IF_TYPE.text, 1, 0, 0, 0), null);
    assert.equal(host.CC_FIND(-1, 1, 0), null);
    host.CC_SETTEXT(-1, 'ignored');
    host.IF_SETCOLOUR(-1, 1);
    host.CC_DELETEALL(-1);
    assert.equal(host.CC_GETY(-1), 0);
    assert.equal(host.CC_GETID(-1), -1);
    assert.equal(host.IF_GETWIDTH(-1), 0);
    host.endCS2DirectInvocation();
}

/* Nested direct ownership must leave settlement to the existing Host boundary,
 * and failed outer direct work remains private until a later successful one. */
{
    const host = directHost();
    const label = host.ref('label');
    const beforeNested = host.commitRevision;
    host._boundary(() => {
        host.beginCS2DirectInvocation();
        host.IF_SETCOLOUR(label.componentId, 123);
        host.endCS2DirectInvocation();
        assert.equal(host.commitRevision, beforeNested,
            'nested direct invocation committed independently');
    });
    assert.equal(host.commitRevision, beforeNested + 1);
    assert(host.consumeTreeDelta(), 'nested direct mutation did not publish at outer settlement');

    const beforeFailure = host.commitRevision;
    const failure = new Error('fixture failure');
    host.beginCS2DirectInvocation();
    host.IF_SETCOLOUR(label.componentId, 456);
    host.endCS2DirectInvocation(failure);
    assert.equal(host.commitRevision, beforeFailure,
        'failed direct invocation published a renderer commit');
    assert.equal(host.consumeTreeDelta(), null,
        'failed direct invocation exposed its partial mutation');
    host.beginCS2DirectInvocation();
    host.endCS2DirectInvocation();
    assert.equal(host.commitRevision, beforeFailure + 1,
        'successful no-op did not settle the prior partial mutation');
    assert.equal(host.projectRenderKey(host.renderKey(label)).props.color, 456);
    assert.throws(() => host.endCS2DirectInvocation(), (error) =>
        error instanceof HostRuntimeError && error.code === 'BAD_STATE');
}

console.log(`direct HostRuntime tests passed (4000 rows / 24000 positional calls: ` +
    `${directBatchBenchmarkMs.toFixed(3)}ms)`);
