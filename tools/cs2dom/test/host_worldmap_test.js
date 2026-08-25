import assert from 'node:assert/strict';

import {
    WORLDMAP_REQUESTS,
    createWorldMapState,
    cycleWorldMapState,
    handleWorldMapRequest,
    isWorldMapIconVisible,
    normalizeWorldMapData,
    packWorldMapCoord,
    parseWorldMapComposites,
    parseWorldMapDetails,
    parseWorldMapFiles,
    setWorldMapDisplayPixelSize,
    setWorldMapEvent,
    shouldFlashWorldMapIcon,
    snapshotWorldMapState,
    unpackWorldMapCoord,
    worldMapAreaContainsCoord,
    worldMapAreaContainsPosition,
    worldMapDisplayToSource,
    worldMapSourceToDisplay,
} from '../src/host_worldmap.js';

assert.equal(WORLDMAP_REQUESTS.size, 44);

const sourceOrigin = packWorldMapCoord(0, 50 * 64 + 5, 50 * 64 + 6);
const sourceIconA = packWorldMapCoord(0, 50 * 64 + 8, 50 * 64 + 9);
const sourceIconB = packWorldMapCoord(0, 50 * 64 + 30, 50 * 64 + 31);
const sourceOther = packWorldMapCoord(0, 30 * 64 + 2, 31 * 64 + 3);
const details = [
    '// friendly world-map fixture',
    '[main]',
    'internal=main',
    'external=Gielinor Surface',
    `origin=${sourceOrigin}`,
    'background=0xFF000000',
    'main=yes',
    'zoom=75',
    'section1=0,0,2,50,50,51,51,0,0,0,0,60,70,61,71,0,0,0',
    'section1_dst_chunk_y_high=0',
    '[other]',
    'internal=other',
    'external=Other Plane',
    `origin=${sourceOther}`,
    'background=0xFF000000',
    'main=no',
    'zoom=100',
    'section1=1,0,1,30,31,0,0,0,0,0,0,40,41,0,0,0,0,0',
    'section1_dst_chunk_y_high=0',
].join('\n');
const composites = [
    '[main]',
    'data0=1',
    'region1=0,0,2,50,50,0,0,60,70,0,0,-1,-1',
    `icon1=100,${sourceIconA},no`,
    `icon2=100,${sourceIconB},yes`,
    `icon3=101,${sourceIconA},no`,
    '[other]',
    'data0=1',
    'region1=0,0,1,30,31,0,0,40,41,0,0,-1,-1',
    `icon1=102,${sourceOther},no`,
].join('\n');
const compack = '7=main\n8=other\n';
const mapElements = {
    100: { category: 5, name: 'Bank' },
    101: { category: 6, name: 'Quest' },
    102: { category: -1, name: 'Uncategorised' },
};

const parsedDetails = parseWorldMapDetails(details, compack);
assert.deepEqual(parsedDetails.map((area) => area.id), [7, 8]);
assert.equal(parsedDetails[0].backgroundColour, -16777216);
assert.equal(parsedDetails[0].sections[0].srcChunkXEnd, 7);
assert.equal(parsedDetails[0].sections[0].dstChunkYEnd, 7,
    'whole-region sections normalize to all eight chunks');
assert.deepEqual(
    [parsedDetails[0].regionLowX, parsedDetails[0].regionHighX,
        parsedDetails[0].regionLowY, parsedDetails[0].regionHighY],
    [60, 61, 70, 71]);

const parsedComposites = parseWorldMapComposites(composites, compack);
assert.equal(parsedComposites[0].regions[0].groupId, -1);
assert.equal(parsedComposites[0].icons[1].hidden, true);
const parsed = parseWorldMapFiles(details, composites, {
    detailsCompack: compack,
    compositeCompack: compack,
    mapElements,
});
assert.equal(parsed.areas[0].icons.length, 3);
assert.equal(parsed.mapElements[100].category, 5);
assert.deepEqual(normalizeWorldMapData({
    worldMap: { areas: parsed.areas }, mapElements,
}).areas, parsed.areas);

assert.deepEqual(unpackWorldMapCoord(packWorldMapCoord(3, 16383, 1234)), {
    plane: 3, x: 16383, y: 1234,
});
const main = parsed.areas[0];
assert.equal(worldMapAreaContainsCoord(main, sourceOrigin), true);
assert.equal(worldMapAreaContainsCoord(main, packWorldMapCoord(2, 3205, 3206)), false);
assert.equal(worldMapAreaContainsPosition(main, 60 * 64, 70 * 64), true);
assert.deepEqual(worldMapSourceToDisplay(main, sourceOrigin), [60 * 64 + 5, 70 * 64 + 6]);
assert.equal(worldMapDisplayToSource(main,
    packWorldMapCoord(0, 60 * 64 + 5, 70 * 64 + 6)), sourceOrigin);

const state = createWorldMapState(parsed);
assert.equal(state.currentMapId, 7);
assert.equal(state.zoomPercentage, 75);
assert.deepEqual([state.displayX, state.displayY], [60 * 64 + 5, 70 * 64 + 6]);
assert.deepEqual([state.displayWidth, state.displayHeight], [171, 112]);
assert.doesNotThrow(() => JSON.stringify(state));

const seen = new Set();
function call(kind, request = {}) {
    seen.add(kind);
    return handleWorldMapRequest(state, { kind, ...request });
}

assert.equal(call('WORLDMAP_GETMAPNAME', { mapId: 7 }).result, 'Gielinor Surface');
assert.equal(call('WORLDMAP_GETCONFIGORIGIN', { arg0: 7 }).result, sourceOrigin);
assert.deepEqual(call('WORLDMAP_GETCONFIGSIZE', { map_id: 7 }).result, [128, 128]);
assert.deepEqual(call('WORLDMAP_GETCONFIGBOUNDS', { arg0: 7 }).result,
    [60 * 64, 70 * 64, 62 * 64 - 1, 72 * 64 - 1]);
assert.equal(call('WORLDMAP_GETCONFIGZOOM', { mapId: 7 }).result, 75);
assert.equal(call('WORLDMAP_ISLOADED').result, 1);
assert.equal(call('WORLDMAP_GETCURRENTMAP').result, 7);
assert.equal(call('WORLDMAP_GETZOOM').result, 75);
assert.deepEqual(call('WORLDMAP_GETDISPLAYPOSITION').result,
    [60 * 64 + 5, 70 * 64 + 6]);
assert.deepEqual(call('WORLDMAP_GETDISPLAYCOORD_CURRENT').result,
    [50 * 64 + 5, 50 * 64 + 6]);
assert.deepEqual(call('WORLDMAP_GETDISPLAYCOORD', { sourceCoord: sourceIconA }).result,
    [60 * 64 + 8, 70 * 64 + 9]);
const displayIconA = packWorldMapCoord(0, 60 * 64 + 8, 70 * 64 + 9);
const displayIconB = packWorldMapCoord(0, 60 * 64 + 30, 70 * 64 + 31);
assert.equal(call('WORLDMAP_GETSOURCECOORD', { displayCoord: displayIconA }).result,
    sourceIconA);
assert.equal(call('WORLDMAP_COORDINMAP', { mapId: 7, coord: sourceIconA }).result, 1);
assert.equal(call('WORLDMAP_GETMAP', { coord: sourceIconA }).result, 7);
assert.deepEqual(call('WORLDMAP_GETSIZE').result, [171, 112]);

/* SETZOOM changes the reported step immediately but live size eases on ticks. */
assert.equal(call('WORLDMAP_SETZOOM', { zoom: 123 }).changed, true);
assert.equal(call('WORLDMAP_GETZOOM').result, 200);
assert.deepEqual(call('WORLDMAP_GETSIZE').result, [171, 112]);
assert.equal(cycleWorldMapState(state), true);
assert(state.zoomScaleNowFp > 768 && state.zoomScaleNowFp < 2048);
call('WORLDMAP_INIT');
assert.equal(state.zoomPercentage, 75, 'INIT reselects and snaps the main area zoom');

/* Non-instant display jumps pan over ticks; instant jumps accept off-surface coords. */
const displayTarget = packWorldMapCoord(0, 60 * 64 + 21, 70 * 64 + 22);
call('WORLDMAP_JUMPTODISPLAYCOORD', { coord: displayTarget });
assert.deepEqual([state.targetX, state.targetY], [60 * 64 + 21, 70 * 64 + 22]);
cycleWorldMapState(state);
assert.deepEqual([state.displayX, state.displayY], [60 * 64 + 7, 70 * 64 + 8]);
call('WORLDMAP_JUMPTODISPLAYCOORD_INSTANT', {
    coord: packWorldMapCoord(0, 123, 456),
});
assert.deepEqual([state.displayX, state.displayY, state.targetX, state.targetY],
    [123, 456, -1, -1]);

call('WORLDMAP_JUMPTOSOURCECOORD', { coord: sourceIconB });
assert.deepEqual([state.targetX, state.targetY], [60 * 64 + 30, 70 * 64 + 31]);
call('WORLDMAP_JUMPTOSOURCECOORD_INSTANT', { coord: sourceIconA });
assert.deepEqual([state.displayX, state.displayY], [60 * 64 + 8, 70 * 64 + 9]);

call('WORLDMAP_SETMAP', { arg0: 8 });
assert.equal(state.currentMapId, 8);
assert.equal(state.zoomPercentage, 100);
call('WORLDMAP_INIT');
assert.equal(state.currentMapId, 7);
call('WORLDMAP_JUMPTOMAP', { mapId: 8, fallbackCoord: sourceOther });
assert.deepEqual([state.currentMapId, state.displayX, state.displayY],
    [8, 40 * 64 + 2, 41 * 64 + 3]);
call('WORLDMAP_JUMPTOMAP_INSTANT', { arg0: 7, arg1: sourceOrigin });
assert.equal(state.currentMapId, 7);

/* Flash controls keep their exact validation/default rules. */
assert.equal(call('WORLDMAP_SETMAXFLASHCOUNT', { count: 0 }).changed, false);
call('WORLDMAP_SETMAXFLASHCOUNT', { count: 2 });
call('WORLDMAP_SETCYCLESPERFLASH', { cycles: 1 });
call('WORLDMAP_FLASHELEMENT', { elementId: 100 });
assert.equal(shouldFlashWorldMapIcon(state, 100), false,
    'a one-tick cycle has an empty integer first half, matching native');
cycleWorldMapState(state);
assert.equal(state.flashCount, 1);
cycleWorldMapState(state);
assert.deepEqual(state.flashingElements, []);
call('WORLDMAP_RESETMAXFLASHCOUNT');
call('WORLDMAP_RESETCYCLESPERFLASH');
assert.deepEqual([state.maxFlashCount, state.cyclesPerFlash], [3, 50]);
call('WORLDMAP_PERPETUALFLASH', { enabled: 1 });
call('WORLDMAP_FLASHELEMENTCATEGORY', { categoryId: 5 });
assert.equal(shouldFlashWorldMapIcon(state, 100, 5), true);
call('WORLDMAP_STOPCURRENTFLASHES');
assert.equal(shouldFlashWorldMapIcon(state, 100, 5), false);

/* The strangely named DISABLE getters/setters expose enabled state in C. */
call('WORLDMAP_DISABLEELEMENTS', { enabled: 0 });
assert.equal(call('WORLDMAP_GETDISABLEELEMENTS').result, 0);
assert.equal(isWorldMapIconVisible(state, 100), false);
call('WORLDMAP_DISABLEELEMENTS', { enabled: 1 });
call('WORLDMAP_DISABLEELEMENT', { elementId: 100, enabled: 0 });
assert.equal(call('WORLDMAP_GETDISABLEELEMENT', { elementId: 100 }).result, 0);
call('WORLDMAP_DISABLEELEMENT', { arg0: 100, arg1: 1 });
call('WORLDMAP_DISABLEELEMENTCATEGORY', { categoryId: 5, enabled: 0 });
assert.equal(call('WORLDMAP_GETDISABLEELEMENTCATEGORY', { categoryId: 5 }).result, 0);
assert.equal(isWorldMapIconVisible(state, 100), false);
call('WORLDMAP_DISABLEELEMENTCATEGORY', { arg0: 5, arg1: 1 });
assert.equal(isWorldMapIconVisible(state, 100), true);

/* Native nearest lookup and list iteration both return display packed coords. */
assert.equal(call('WORLDMAP_GETNEARESTICON', {
    elementId: 100,
    sourceCoord: packWorldMapCoord(0, 60 * 64 + 9, 70 * 64 + 10),
}).result, displayIconA);
assert.deepEqual(call('WORLDMAP_LISTELEMENT_START').result, [100, displayIconA]);
assert.deepEqual(call('WORLDMAP_LISTELEMENT_NEXT').result, [100, displayIconB],
    'current C iteration does not suppress an icon solely for hidden=yes');
assert.deepEqual(call('WORLDMAP_LISTELEMENT_NEXT').result, [101, displayIconA]);
assert.deepEqual(call('WORLDMAP_LISTELEMENT_NEXT').result, [-1, -1]);

setWorldMapEvent(state, { element: 101, coord1: sourceIconA, coord2: displayIconA });
assert.equal(call('WORLDMAP_ELEMENT').result, 101);
assert.equal(call('WORLDMAP_ELEMENTCOORD1').result, sourceIconA);
assert.equal(call('WORLDMAP_ELEMENTCOORD').result, displayIconA);

assert.deepEqual([...seen].sort(), [...WORLDMAP_REQUESTS].sort(),
    `missing request coverage: ${[...WORLDMAP_REQUESTS].filter((name) => !seen.has(name))}`);

setWorldMapDisplayPixelSize(state, 320, 200);
assert.deepEqual(handleWorldMapRequest(state, { kind: 'WORLDMAP_GETSIZE' }).result, [107, 67]);
const snapshot = snapshotWorldMapState(state);
assert.equal(Object.hasOwn(snapshot, 'areas'), false,
    'static cache records must not be cloned into every host snapshot');
const restored = createWorldMapState(parsed, snapshot);
assert.equal(JSON.stringify(snapshotWorldMapState(restored)), JSON.stringify(snapshot));
snapshot.disabledElements.push(999);
assert.equal(state.disabledElements.includes(999), false);

const empty = createWorldMapState();
assert.equal(handleWorldMapRequest(empty, { kind: 'WORLDMAP_ISLOADED' }).result, 0);
assert.equal(handleWorldMapRequest(empty, { kind: 'WORLDMAP_GETCURRENTMAP' }).result, -1);
assert.deepEqual(handleWorldMapRequest(empty, {
    kind: 'WORLDMAP_GETDISPLAYCOORD_CURRENT',
}).result, [-1, -1]);
assert.deepEqual(handleWorldMapRequest(empty, {
    kind: 'WORLDMAP_LISTELEMENT_START',
}).result, [-1, -1]);
assert.throws(() => handleWorldMapRequest(state, { kind: 'WORLDMAP_NOT_REAL' }),
    /unknown world-map HOST request/);

console.log('host_worldmap_test: ok (44 request kinds)');
