/*
 * React-side world-map HOST state.
 *
 * The C CS2VM still owns opcode decoding. This module mirrors the current
 * RS_WorldMapState / exec_worldmap implementation with ordinary JavaScript
 * records so the browser HOST can answer every WORLDMAP_* request without a
 * second script interpreter. It also understands cachepack's friendly
 * details.wma and compositemap.wmc files.
 */

import { packName } from './pack.js';

const REGION_TILES = 64;
const CHUNK_TILES = 8;
const CHUNKS_PER_REGION = 8;
const ZOOM_SCALE_ONE = 256;
const DEFAULT_ZOOM = 100;
const DEFAULT_MAX_FLASH_COUNT = 3;
const DEFAULT_CYCLES_PER_FLASH = 50;
const DEFAULT_DISPLAY_WIDTH = 512;
const DEFAULT_DISPLAY_HEIGHT = 334;
const PAN_STEPS = 8;
const ZOOM_STEPS = 30;
const WORLDMAP_STATE = Symbol('cs2dom.worldmap.state');

const REQUEST_NAMES = Object.freeze([
    'WORLDMAP_INIT',
    'WORLDMAP_GETMAPNAME',
    'WORLDMAP_SETMAP',
    'WORLDMAP_GETZOOM',
    'WORLDMAP_SETZOOM',
    'WORLDMAP_ISLOADED',
    'WORLDMAP_JUMPTODISPLAYCOORD',
    'WORLDMAP_JUMPTODISPLAYCOORD_INSTANT',
    'WORLDMAP_JUMPTOSOURCECOORD',
    'WORLDMAP_JUMPTOSOURCECOORD_INSTANT',
    'WORLDMAP_GETDISPLAYPOSITION',
    'WORLDMAP_GETCONFIGORIGIN',
    'WORLDMAP_GETCONFIGSIZE',
    'WORLDMAP_GETCONFIGBOUNDS',
    'WORLDMAP_GETCONFIGZOOM',
    'WORLDMAP_GETDISPLAYCOORD_CURRENT',
    'WORLDMAP_GETCURRENTMAP',
    'WORLDMAP_GETDISPLAYCOORD',
    'WORLDMAP_GETSOURCECOORD',
    'WORLDMAP_JUMPTOMAP',
    'WORLDMAP_JUMPTOMAP_INSTANT',
    'WORLDMAP_COORDINMAP',
    'WORLDMAP_GETSIZE',
    'WORLDMAP_GETMAP',
    'WORLDMAP_SETMAXFLASHCOUNT',
    'WORLDMAP_RESETMAXFLASHCOUNT',
    'WORLDMAP_SETCYCLESPERFLASH',
    'WORLDMAP_RESETCYCLESPERFLASH',
    'WORLDMAP_PERPETUALFLASH',
    'WORLDMAP_FLASHELEMENT',
    'WORLDMAP_FLASHELEMENTCATEGORY',
    'WORLDMAP_STOPCURRENTFLASHES',
    'WORLDMAP_DISABLEELEMENTS',
    'WORLDMAP_DISABLEELEMENT',
    'WORLDMAP_DISABLEELEMENTCATEGORY',
    'WORLDMAP_GETDISABLEELEMENTS',
    'WORLDMAP_GETDISABLEELEMENT',
    'WORLDMAP_GETDISABLEELEMENTCATEGORY',
    'WORLDMAP_GETNEARESTICON',
    'WORLDMAP_LISTELEMENT_START',
    'WORLDMAP_LISTELEMENT_NEXT',
    'WORLDMAP_ELEMENT',
    'WORLDMAP_ELEMENTCOORD1',
    'WORLDMAP_ELEMENTCOORD',
]);

/** Exact generated HOST request names handled here. */
export const WORLDMAP_REQUESTS = new Set(REQUEST_NAMES);
/* Spaced spelling is convenient at integration sites. */
export const WORLD_MAP_REQUESTS = WORLDMAP_REQUESTS;

/** Pack a world-map coord exactly as the native cache bridge does. */
export function packWorldMapCoord(plane, x, y) {
    return (((integer(plane, 0) & 0x3) << 28) |
        ((integer(x, 0) & 0x3fff) << 14) |
        (integer(y, 0) & 0x3fff)) >>> 0;
}

/** Unpack a packed coord into a plain `{ plane, x, y }` record. */
export function unpackWorldMapCoord(packed) {
    const value = integer(packed, 0) >>> 0;
    return {
        plane: (value >>> 28) & 0x3,
        x: (value >>> 14) & 0x3fff,
        y: value & 0x3fff,
    };
}

/**
 * Parse cachepack's friendly `details.wma` representation.
 *
 * `compack` may be its text, a name->id Map, or a name->id object. The return
 * value is an ordered array of normalized areas; order is significant for
 * WORLDMAP_GETMAP when areas overlap.
 */
export function parseWorldMapDetails(text, compack = '') {
    const symbols = symbolIds(compack);
    return configBlocks(text).map((block, ordinal) => {
        const raw = {
            id: blockId(block.name, symbols, ordinal),
            internalName: block.name,
            externalName: '',
            origin: 0,
            backgroundColour: 0,
            isMain: false,
            zoom: 0,
            sections: [],
        };
        const sections = new Map();
        const dstChunkYHigh = new Map();

        for( const { key, value } of block.entries ) {
            if( key === 'internal' ) raw.internalName = unescapeConfig(value);
            else if( key === 'external' ) raw.externalName = unescapeConfig(value);
            else if( key === 'origin' ) raw.origin = parseInteger(value, 0);
            else if( key === 'background' ) raw.backgroundColour = parseInteger(value, 0) | 0;
            else if( key === 'main' ) raw.isMain = parseBoolean(value);
            else if( key === 'zoom' ) raw.zoom = parseInteger(value, 0);
            else {
                let match = /^section(\d+)$/.exec(key);
                if( match ) {
                    const index = Number(match[1]) - 1;
                    const fields = value.split(',').map((field) => parseInteger(field.trim(), 0));
                    if( index >= 0 && fields.length >= 18 ) sections.set(index, {
                        type: fields[0], min_plane: fields[1], planes: fields[2],
                        src_region_x: fields[3], src_region_y: fields[4],
                        src_region_x_end: fields[5], src_region_y_end: fields[6],
                        src_chunk_x_low: fields[7], src_chunk_y_low: fields[8],
                        src_chunk_x_high: fields[9], src_chunk_y_high: fields[10],
                        dst_region_x: fields[11], dst_region_y: fields[12],
                        dst_region_x_end: fields[13], dst_region_y_end: fields[14],
                        dst_chunk_x_low: fields[15], dst_chunk_y_low: fields[16],
                        dst_chunk_x_high: fields[17],
                    });
                    continue;
                }
                match = /^section(\d+)_dst_chunk_y_high$/.exec(key);
                if( match ) dstChunkYHigh.set(
                    Number(match[1]) - 1, parseInteger(value, 0));
            }
        }

        for( const index of [...sections.keys()].sort((left, right) => left - right) ) {
            const section = sections.get(index);
            section.dst_chunk_y_high = dstChunkYHigh.get(index) ?? 0;
            raw.sections.push(section);
        }
        return normalizeArea(raw, ordinal);
    });
}

/** Parse cachepack's friendly `compositemap.wmc` representation. */
export function parseWorldMapComposites(text, compack = '') {
    const symbols = symbolIds(compack);
    return configBlocks(text).map((block, ordinal) => {
        const composite = {
            id: blockId(block.name, symbols, ordinal),
            internalName: block.name,
            data0Count: 0,
            regions: [],
            icons: [],
        };
        const regions = new Map();
        const icons = new Map();

        for( const { key, value } of block.entries ) {
            if( key === 'data0' ) {
                composite.data0Count = parseInteger(value, 0);
                continue;
            }
            let match = /^region(\d+)$/.exec(key);
            if( match ) {
                const index = Number(match[1]) - 1;
                const fields = value.split(',').map((field) => parseInteger(field.trim(), 0));
                if( index >= 0 && fields.length >= 13 ) regions.set(index, {
                    kind: fields[0], minPlane: fields[1], planes: fields[2],
                    srcRegionX: fields[3], srcRegionY: fields[4],
                    srcChunkX: fields[5], srcChunkY: fields[6],
                    dstRegionX: fields[7], dstRegionY: fields[8],
                    dstChunkX: fields[9], dstChunkY: fields[10],
                    groupId: fields[11], fileId: fields[12],
                });
                continue;
            }
            match = /^icon(\d+)$/.exec(key);
            if( match ) {
                const index = Number(match[1]) - 1;
                const fields = value.split(',');
                if( index >= 0 && fields.length >= 3 ) icons.set(index, {
                    element: parseInteger(fields[0].trim(), -1),
                    coord: parseInteger(fields[1].trim(), 0),
                    hidden: parseBoolean(fields.slice(2).join(',').trim()),
                });
            }
        }

        composite.regions = [...regions.keys()].sort((a, b) => a - b)
            .map((index) => regions.get(index));
        composite.icons = [...icons.keys()].sort((a, b) => a - b)
            .map((index) => icons.get(index));
        return composite;
    });
}

/** Parse and join one friendly details/composite pair. */
export function parseWorldMapFiles(detailsText, compositeText, options = {}) {
    const details = parseWorldMapDetails(
        detailsText, options.detailsCompack ?? options.compack ?? '');
    const composites = parseWorldMapComposites(
        compositeText, options.compositeCompack ?? options.compack ?? '');
    return mergeWorldMapData(details, composites, options.mapElements);
}

/**
 * Normalize either friendly-file input or decoded Dat2/hostData records.
 *
 * Accepted forms include `{ worldMap, mapElements }`, `{ areas }`, a bare area
 * array, `{ details, composites }`, and `{ detailsWma, compositeWmc }`.
 */
export function normalizeWorldMapData(input = {}, suppliedMapElements) {
    const outer = record(input);
    let source = outer.worldMap ?? outer.worldmap ?? input;
    if( typeof source === 'string' ) source = { detailsWma: source };
    const value = record(source);
    const mapElements = suppliedMapElements ?? outer.mapElements ?? outer.map_elements ??
        value.mapElements ?? value.map_elements ?? {};

    const detailsText = firstDefined(
        value.detailsWma, value.details_wma,
        typeof value.details === 'string' ? value.details : undefined);
    if( detailsText !== undefined ) return parseWorldMapFiles(
        detailsText,
        firstDefined(value.compositeWmc, value.compositemapWmc,
            value.composite_wmc, value.compositemap_wmc,
            typeof value.composites === 'string' ? value.composites : undefined,
            typeof value.compositemap === 'string' ? value.compositemap : undefined, ''),
        {
            detailsCompack: firstDefined(value.detailsCompack, value.details_compack, ''),
            compositeCompack: firstDefined(
                value.compositeCompack, value.compositemapCompack,
                value.composite_compack, value.compositemap_compack, ''),
            mapElements,
        });

    let details = value.areas ?? value.records ??
        (Array.isArray(source) ? source : value.details ?? []);
    let composites = value.composites ?? value.compositemap ?? [];
    details = collection(details);
    composites = collection(composites);
    return mergeWorldMapData(details, composites, mapElements);
}

/** Construct native-equivalent state and select the main (or first) area. */
export function createWorldMapState(source = {}, restore = null) {
    const data = normalizeWorldMapData(source);
    const state = {
        areas: data.areas,
        mapElements: data.mapElements,
        currentMapId: -1,
        zoomPercentage: DEFAULT_ZOOM,
        zoomScaleFp: zoomScaleFpFor(DEFAULT_ZOOM),
        zoomScaleNowFp: zoomScaleFpFor(DEFAULT_ZOOM),
        displayX: 0,
        displayY: 0,
        targetX: -1,
        targetY: -1,
        displayPixelWidth: 0,
        displayPixelHeight: 0,
        displayWidth: 0,
        displayHeight: 0,
        elementsEnabled: true,
        perpetualFlash: false,
        maxFlashCount: DEFAULT_MAX_FLASH_COUNT,
        cyclesPerFlash: DEFAULT_CYCLES_PER_FLASH,
        flashCount: -1,
        flashCycle: -1,
        disabledElements: [],
        disabledCategories: [],
        flashingElements: [],
        flashingCategories: [],
        iconIndex: 0,
        eventElement: -1,
        eventCoord1: -1,
        eventCoord2: -1,
    };
    Object.defineProperty(state, WORLDMAP_STATE, { value: true });
    recomputeDisplaySize(state);
    selectInitialArea(state);

    const seed = restore === null && looksLikeRuntimeState(source) ? record(source) : record(restore);
    if( Object.keys(seed).length ) restoreRuntimeState(state, seed);
    return state;
}

/** Return a detached, persistence-safe snapshot. */
export function snapshotWorldMapState(state) {
    assertState(state);
    /* Areas and map-element configs are immutable hostData. Keeping them out of
     * every React snapshot avoids cloning the 440KB friendly composite map on
     * each script boundary. Pass this record as createWorldMapState's second
     * argument when restoring. */
    return {
        currentMapId: state.currentMapId,
        zoomPercentage: state.zoomPercentage,
        zoomScaleFp: state.zoomScaleFp,
        zoomScaleNowFp: state.zoomScaleNowFp,
        displayX: state.displayX,
        displayY: state.displayY,
        targetX: state.targetX,
        targetY: state.targetY,
        displayPixelWidth: state.displayPixelWidth,
        displayPixelHeight: state.displayPixelHeight,
        displayWidth: state.displayWidth,
        displayHeight: state.displayHeight,
        elementsEnabled: state.elementsEnabled,
        perpetualFlash: state.perpetualFlash,
        maxFlashCount: state.maxFlashCount,
        cyclesPerFlash: state.cyclesPerFlash,
        flashCount: state.flashCount,
        flashCycle: state.flashCycle,
        disabledElements: [...state.disabledElements],
        disabledCategories: [...state.disabledCategories],
        flashingElements: [...state.flashingElements],
        flashingCategories: [...state.flashingCategories],
        iconIndex: state.iconIndex,
        eventElement: state.eventElement,
        eventCoord1: state.eventCoord1,
        eventCoord2: state.eventCoord2,
    };
}

/** Update the world-map surface's actual pixel dimensions. */
export function setWorldMapDisplayPixelSize(state, width, height) {
    assertState(state);
    const before = runtimeSignature(state);
    state.displayPixelWidth = integer(width, 0) > 0 ? integer(width, 0) : 0;
    state.displayPixelHeight = integer(height, 0) > 0 ? integer(height, 0) : 0;
    recomputeDisplaySize(state);
    return before !== runtimeSignature(state);
}

/** Set what the three world-map event getter opcodes expose to a hook. */
export function setWorldMapEvent(state, event = {}) {
    assertState(state);
    const before = runtimeSignature(state);
    state.eventElement = integer(event.element ?? event.elementId ?? event.element_id, -1);
    state.eventCoord1 = integer(event.coord1 ?? event.sourceCoord ?? event.source_coord, -1);
    state.eventCoord2 = integer(event.coord2 ?? event.displayCoord ?? event.display_coord, -1);
    return before !== runtimeSignature(state);
}

/** One native client cycle of flash, zoom and pan transitions. */
export function cycleWorldMapState(state) {
    assertState(state);
    let changed = cycleFlash(state);
    changed = cycleZoom(state) || changed;
    if( state.targetX === -1 || state.targetY === -1 ) return changed;

    const deltaX = state.targetX - state.displayX;
    const deltaY = state.targetY - state.displayY;
    let stepX = deltaX;
    let stepY = deltaY;
    if( deltaX !== 0 ) {
        const span = Math.abs(deltaX);
        stepX = Math.trunc(deltaX / Math.min(span, PAN_STEPS));
    }
    if( deltaY !== 0 ) {
        const span = Math.abs(deltaY);
        stepY = Math.trunc(deltaY / Math.min(span, PAN_STEPS));
    }
    state.displayX += stepX;
    state.displayY += stepY;
    if( state.displayX === state.targetX && state.displayY === state.targetY ) {
        state.targetX = -1;
        state.targetY = -1;
    }
    return changed || stepX !== 0 || stepY !== 0;
}

/** Renderer visibility predicate, matching RS_WorldMap_IconVisible. */
export function isWorldMapIconVisible(state, elementId, categoryId = undefined) {
    assertState(state);
    const element = integer(elementId, -1);
    const category = categoryId === undefined ? elementCategory(state, element)
        : integer(categoryId, -1);
    return state.elementsEnabled && !state.disabledElements.includes(element) &&
        (category < 0 || !state.disabledCategories.includes(category));
}

/** Renderer flash predicate, matching RS_WorldMap_ShouldFlashIcon. */
export function shouldFlashWorldMapIcon(state, elementId, categoryId = undefined) {
    assertState(state);
    if( !hasActiveFlashes(state) || state.flashCycle < 0 || state.cyclesPerFlash <= 0 )
        return false;
    if( state.flashCycle % state.cyclesPerFlash >= Math.trunc(state.cyclesPerFlash / 2) )
        return false;
    const element = integer(elementId, -1);
    if( state.flashingElements.includes(element) ) return true;
    const category = categoryId === undefined ? elementCategory(state, element)
        : integer(categoryId, -1);
    return category >= 0 && state.flashingCategories.includes(category);
}

/** World/source coord membership helper exposed for map renderers. */
export function worldMapAreaContainsCoord(area, packedOrPlane, x, y) {
    const coord = x === undefined ? unpackWorldMapCoord(packedOrPlane)
        : { plane: integer(packedOrPlane, 0), x: integer(x, 0), y: integer(y, 0) };
    return areaContainsCoord(area, coord.plane, coord.x, coord.y);
}

/** Display/surface position membership helper exposed for map renderers. */
export function worldMapAreaContainsPosition(area, x, y) {
    return areaContainsPosition(area, integer(x, 0), integer(y, 0));
}

/** Transform one packed source coord into `[displayX, displayY]` or null. */
export function worldMapSourceToDisplay(area, packedCoord) {
    const { plane, x, y } = unpackWorldMapCoord(packedCoord);
    return areaPosition(area, plane, x, y);
}

/** Transform a display position into its packed source coord, or -1. */
export function worldMapDisplayToSource(area, packedOrX, suppliedY) {
    const display = suppliedY === undefined ? unpackWorldMapCoord(packedOrX)
        : { x: integer(packedOrX, 0), y: integer(suppliedY, 0) };
    const coord = areaCoord(area, display.x, display.y);
    return coord ? packWorldMapCoord(coord.plane, coord.x, coord.y) : -1;
}

/** Execute one reflected WORLDMAP_* request. Pair results are always arrays. */
export function handleWorldMapRequest(state, request = {}) {
    assertState(state);
    const kind = String(request.kind ?? request._kind ?? '').toUpperCase();
    if( !WORLDMAP_REQUESTS.has(kind) )
        throw new RangeError(`unknown world-map HOST request ${kind || '(empty)'}`);

    const before = runtimeSignature(state);
    let result = null;
    let area;
    let pair;

    switch( kind ) {
    case 'WORLDMAP_INIT':
        selectInitialArea(state);
        break;
    case 'WORLDMAP_GETMAPNAME':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area?.externalName ?? '';
        break;
    case 'WORLDMAP_SETMAP': {
        /*
         * RECORDS the id, whether or not an area with it is loaded.
         *
         * `RS_WorldMap_SetCurrentMapId(map, arg0)` is the whole handler — the
         * reference has a separate `WORLDMAP_ISLOADED` for the other question,
         * and `WORLDMAP_GETCURRENTMAP` reads back exactly what was set.
         *
         * Refusing an unknown id aborted the script that asked, and with it
         * everything after: `worldmap` built 329 components instead of 352.
         */
        const mapId = requestInteger(request, 0, 'mapId', 'map_id');
        area = areaById(state, mapId);
        if( area ) selectArea(state, area);
        else state.currentMapId = mapId;
        break;
    }
    case 'WORLDMAP_GETZOOM':
        result = state.zoomPercentage;
        break;
    case 'WORLDMAP_SETZOOM':
        setZoom(state, requestInteger(request, 0, 'zoom', 'value'));
        break;
    case 'WORLDMAP_ISLOADED':
        result = state.areas.length > 0 ? 1 : 0;
        break;
    case 'WORLDMAP_JUMPTODISPLAYCOORD':
        jumpToDisplayCoord(state, requestInteger(request, 0, 'coord', 'displayCoord',
            'display_coord'), false);
        break;
    case 'WORLDMAP_JUMPTODISPLAYCOORD_INSTANT':
        jumpToDisplayCoord(state, requestInteger(request, 0, 'coord', 'displayCoord',
            'display_coord'), true);
        break;
    case 'WORLDMAP_JUMPTOSOURCECOORD':
        jumpToSourceCoord(state, requestInteger(request, 0, 'coord', 'sourceCoord',
            'source_coord'), false);
        break;
    case 'WORLDMAP_JUMPTOSOURCECOORD_INSTANT':
        jumpToSourceCoord(state, requestInteger(request, 0, 'coord', 'sourceCoord',
            'source_coord'), true);
        break;
    case 'WORLDMAP_GETDISPLAYPOSITION':
        result = [state.displayX, state.displayY];
        break;
    case 'WORLDMAP_GETCONFIGORIGIN':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area?.origin ?? 0;
        break;
    case 'WORLDMAP_GETCONFIGSIZE':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area ? [areaWidth(area), areaHeight(area)] : [0, 0];
        break;
    case 'WORLDMAP_GETCONFIGBOUNDS':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area ? areaBounds(area) : [0, 0, 0, 0];
        break;
    case 'WORLDMAP_GETCONFIGZOOM':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area?.zoom ?? -1;
        break;
    case 'WORLDMAP_GETDISPLAYCOORD_CURRENT':
        area = currentArea(state);
        if( !area ) result = [-1, -1];
        else {
            const coord = areaCoord(area, state.displayX, state.displayY);
            result = coord ? [coord.x, coord.y] : [state.displayX, state.displayY];
        }
        break;
    case 'WORLDMAP_GETCURRENTMAP':
        result = state.currentMapId;
        break;
    case 'WORLDMAP_GETDISPLAYCOORD':
        area = currentArea(state);
        pair = area ? worldMapSourceToDisplay(
            area, requestInteger(request, 0, 'coord', 'sourceCoord', 'source_coord')) : null;
        result = pair ?? [-1, -1];
        break;
    case 'WORLDMAP_GETSOURCECOORD':
        area = currentArea(state);
        result = area ? worldMapDisplayToSource(
            area, requestInteger(request, 0, 'coord', 'displayCoord', 'display_coord')) : -1;
        break;
    case 'WORLDMAP_JUMPTOMAP':
    case 'WORLDMAP_JUMPTOMAP_INSTANT':
        jumpToMap(
            state,
            requestInteger(request, 0, 'mapId', 'map_id'),
            requestInteger(request, 1, 'fallbackCoord', 'fallback_coord', 'coord'));
        break;
    case 'WORLDMAP_COORDINMAP':
        area = areaById(state, requestInteger(request, 0, 'mapId', 'map_id'));
        result = area && worldMapAreaContainsCoord(
            area, requestInteger(request, 1, 'coord', 'sourceCoord', 'source_coord')) ? 1 : 0;
        break;
    case 'WORLDMAP_GETSIZE':
        result = [state.displayWidth, state.displayHeight];
        break;
    case 'WORLDMAP_GETMAP':
        result = mapAtCoord(state, requestInteger(request, 0, 'coord', 'sourceCoord',
            'source_coord'));
        break;
    case 'WORLDMAP_SETMAXFLASHCOUNT': {
        const count = requestInteger(request, 0, 'count', 'value');
        if( count >= 1 ) state.maxFlashCount = count;
        break;
    }
    case 'WORLDMAP_RESETMAXFLASHCOUNT':
        state.maxFlashCount = DEFAULT_MAX_FLASH_COUNT;
        break;
    case 'WORLDMAP_SETCYCLESPERFLASH': {
        const cycles = requestInteger(request, 0, 'cycles', 'value');
        if( cycles >= 1 ) state.cyclesPerFlash = cycles;
        break;
    }
    case 'WORLDMAP_RESETCYCLESPERFLASH':
        state.cyclesPerFlash = DEFAULT_CYCLES_PER_FLASH;
        break;
    case 'WORLDMAP_PERPETUALFLASH':
        state.perpetualFlash = requestInteger(request, 0, 'enabled', 'value') === 1;
        break;
    case 'WORLDMAP_FLASHELEMENT':
        flashElement(state, requestInteger(request, 0, 'elementId', 'element_id', 'element'));
        break;
    case 'WORLDMAP_FLASHELEMENTCATEGORY':
        flashCategory(state, requestInteger(request, 0, 'categoryId', 'category_id', 'category'));
        break;
    case 'WORLDMAP_STOPCURRENTFLASHES':
        stopFlashes(state);
        break;
    case 'WORLDMAP_DISABLEELEMENTS':
        state.elementsEnabled = requestInteger(request, 0, 'enabled', 'value') === 1;
        break;
    case 'WORLDMAP_DISABLEELEMENT':
        setIdEnabled(
            state.disabledElements,
            requestInteger(request, 0, 'elementId', 'element_id', 'element'),
            requestInteger(request, 1, 'enabled', 'value') === 1);
        break;
    case 'WORLDMAP_DISABLEELEMENTCATEGORY':
        setIdEnabled(
            state.disabledCategories,
            requestInteger(request, 0, 'categoryId', 'category_id', 'category'),
            requestInteger(request, 1, 'enabled', 'value') === 1);
        break;
    case 'WORLDMAP_GETDISABLEELEMENTS':
        result = state.elementsEnabled ? 1 : 0;
        break;
    case 'WORLDMAP_GETDISABLEELEMENT':
        result = state.disabledElements.includes(
            requestInteger(request, 0, 'elementId', 'element_id', 'element')) ? 0 : 1;
        break;
    case 'WORLDMAP_GETDISABLEELEMENTCATEGORY':
        result = state.disabledCategories.includes(
            requestInteger(request, 0, 'categoryId', 'category_id', 'category')) ? 0 : 1;
        break;
    case 'WORLDMAP_GETNEARESTICON':
        result = nearestIcon(
            state,
            requestInteger(request, 0, 'elementId', 'element_id', 'element'),
            requestInteger(request, 1, 'sourceCoord', 'source_coord', 'coord'));
        break;
    case 'WORLDMAP_LISTELEMENT_START':
        state.iconIndex = 0;
        result = nextIcon(state);
        break;
    case 'WORLDMAP_LISTELEMENT_NEXT':
        result = nextIcon(state);
        break;
    case 'WORLDMAP_ELEMENT':
        result = state.eventElement;
        break;
    case 'WORLDMAP_ELEMENTCOORD1':
        result = state.eventCoord1;
        break;
    case 'WORLDMAP_ELEMENTCOORD':
        result = state.eventCoord2;
        break;
    }

    return { result, changed: before !== runtimeSignature(state) };
}

function mergeWorldMapData(rawAreas, rawComposites, rawMapElements) {
    const composites = collection(rawComposites);
    const byId = new Map();
    const byName = new Map();
    for( const value of composites ) {
        const entry = record(value);
        const id = integer(entry.id, -1);
        if( id >= 0 ) byId.set(id, entry);
        const name = String(entry.internalName ?? entry.internal_name ?? entry.internal ?? '');
        if( name ) byName.set(name, entry);
    }

    const areas = collection(rawAreas).map((value, index) => {
        const source = record(value);
        const id = integer(source.id, index);
        const name = String(source.internalName ?? source.internal_name ?? source.internal ?? '');
        const composite = byId.get(id) ?? byName.get(name);
        return normalizeArea(composite ? {
            ...source,
            icons: composite.icons ?? source.icons,
            regions: composite.regions ?? composite.regionSources ?? source.regions,
            data0Count: composite.data0Count ?? composite.data0_count ?? source.data0Count,
        } : source, index);
    });
    return { areas, mapElements: normalizeMapElements(rawMapElements) };
}

function normalizeArea(value, fallbackId) {
    const source = record(value);
    const sections = collection(source.sections).map(normalizeSection);
    let regionLowX = 0;
    let regionHighX = 0;
    let regionLowY = 0;
    let regionHighY = 0;
    for( let index = 0; index < sections.length; index++ ) {
        const section = sections[index];
        if( index === 0 ) {
            regionLowX = section.dstRegionX;
            regionHighX = section.dstRegionXEnd;
            regionLowY = section.dstRegionY;
            regionHighY = section.dstRegionYEnd;
        } else {
            regionLowX = Math.min(regionLowX, section.dstRegionX);
            regionHighX = Math.max(regionHighX, section.dstRegionXEnd);
            regionLowY = Math.min(regionLowY, section.dstRegionY);
            regionHighY = Math.max(regionHighY, section.dstRegionYEnd);
        }
    }

    return {
        id: integer(source.id, fallbackId),
        internalName: String(
            source.internalName ?? source.internal_name ?? source.internal ?? ''),
        externalName: String(
            source.externalName ?? source.external_name ?? source.external ?? ''),
        origin: integer(source.origin, 0),
        backgroundColour: integer(
            source.backgroundColour ?? source.background_color ?? source.background, 0) | 0,
        isMain: parseBoolean(source.isMain ?? source.is_main ?? source.main),
        zoom: integer(source.zoom, 0),
        sections,
        icons: collection(source.icons).map((entry) => {
            const icon = record(entry);
            return {
                element: integer(icon.element ?? icon.elementId ?? icon.element_id, -1),
                coord: integer(icon.coord, 0),
                hidden: parseBoolean(icon.hidden),
            };
        }),
        regions: collection(source.regions ?? source.regionSources ?? source.region_sources)
            .map(normalizeRegion),
        data0Count: integer(source.data0Count ?? source.data0_count ?? source.data0, 0),
        regionLowX,
        regionHighX,
        regionLowY,
        regionHighY,
    };
}

function normalizeSection(value) {
    const source = record(value);
    const kind = integer(source.kind ?? source.type, 0);
    const srcRegionX = fieldInt(source, 0, 'srcRegionX', 'src_region_x');
    const srcRegionY = fieldInt(source, 0, 'srcRegionY', 'src_region_y');
    const dstRegionX = fieldInt(source, 0, 'dstRegionX', 'dst_region_x');
    const dstRegionY = fieldInt(source, 0, 'dstRegionY', 'dst_region_y');
    const srcChunkX = fieldInt(source, 0,
        'srcChunkX', 'src_chunk_x', 'srcChunkXLow', 'src_chunk_x_low');
    const srcChunkY = fieldInt(source, 0,
        'srcChunkY', 'src_chunk_y', 'srcChunkYLow', 'src_chunk_y_low');
    const dstChunkX = fieldInt(source, 0,
        'dstChunkX', 'dst_chunk_x', 'dstChunkXLow', 'dst_chunk_x_low');
    const dstChunkY = fieldInt(source, 0,
        'dstChunkY', 'dst_chunk_y', 'dstChunkYLow', 'dst_chunk_y_low');

    let srcRegionXEnd = srcRegionX;
    let srcRegionYEnd = srcRegionY;
    let dstRegionXEnd = dstRegionX;
    let dstRegionYEnd = dstRegionY;
    let srcChunkXEnd = srcChunkX;
    let srcChunkYEnd = srcChunkY;
    let dstChunkXEnd = dstChunkX;
    let dstChunkYEnd = dstChunkY;

    if( kind === 0 ) {
        srcRegionXEnd = fieldInt(source, srcRegionX, 'srcRegionXEnd', 'src_region_x_end');
        srcRegionYEnd = fieldInt(source, srcRegionY, 'srcRegionYEnd', 'src_region_y_end');
        dstRegionXEnd = fieldInt(source, dstRegionX, 'dstRegionXEnd', 'dst_region_x_end');
        dstRegionYEnd = fieldInt(source, dstRegionY, 'dstRegionYEnd', 'dst_region_y_end');
        srcChunkXEnd = CHUNKS_PER_REGION - 1;
        srcChunkYEnd = CHUNKS_PER_REGION - 1;
        dstChunkXEnd = CHUNKS_PER_REGION - 1;
        dstChunkYEnd = CHUNKS_PER_REGION - 1;
    } else if( kind === 1 ) {
        srcChunkXEnd = CHUNKS_PER_REGION - 1;
        srcChunkYEnd = CHUNKS_PER_REGION - 1;
        dstChunkXEnd = CHUNKS_PER_REGION - 1;
        dstChunkYEnd = CHUNKS_PER_REGION - 1;
    } else if( kind === 2 ) {
        srcChunkXEnd = fieldInt(source, srcChunkX,
            'srcChunkXEnd', 'src_chunk_x_end', 'srcChunkXHigh', 'src_chunk_x_high');
        srcChunkYEnd = fieldInt(source, srcChunkY,
            'srcChunkYEnd', 'src_chunk_y_end', 'srcChunkYHigh', 'src_chunk_y_high');
        dstChunkXEnd = fieldInt(source, dstChunkX,
            'dstChunkXEnd', 'dst_chunk_x_end', 'dstChunkXHigh', 'dst_chunk_x_high');
        dstChunkYEnd = fieldInt(source, dstChunkY,
            'dstChunkYEnd', 'dst_chunk_y_end', 'dstChunkYHigh', 'dst_chunk_y_high');
    }

    return {
        kind,
        minPlane: fieldInt(source, 0, 'minPlane', 'min_plane'),
        planes: fieldInt(source, 0, 'planes'),
        srcRegionX,
        srcRegionY,
        srcRegionXEnd,
        srcRegionYEnd,
        srcChunkX,
        srcChunkY,
        srcChunkXEnd,
        srcChunkYEnd,
        dstRegionX,
        dstRegionY,
        dstRegionXEnd,
        dstRegionYEnd,
        dstChunkX,
        dstChunkY,
        dstChunkXEnd,
        dstChunkYEnd,
    };
}

function normalizeRegion(value) {
    const source = record(value);
    return {
        kind: integer(source.kind, 0),
        minPlane: fieldInt(source, 0, 'minPlane', 'min_plane'),
        planes: fieldInt(source, 0, 'planes'),
        srcRegionX: fieldInt(source, 0, 'srcRegionX', 'src_region_x'),
        srcRegionY: fieldInt(source, 0, 'srcRegionY', 'src_region_y'),
        srcChunkX: fieldInt(source, 0, 'srcChunkX', 'src_chunk_x'),
        srcChunkY: fieldInt(source, 0, 'srcChunkY', 'src_chunk_y'),
        dstRegionX: fieldInt(source, 0, 'dstRegionX', 'dst_region_x'),
        dstRegionY: fieldInt(source, 0, 'dstRegionY', 'dst_region_y'),
        dstChunkX: fieldInt(source, 0, 'dstChunkX', 'dst_chunk_x'),
        dstChunkY: fieldInt(source, 0, 'dstChunkY', 'dst_chunk_y'),
        groupId: fieldInt(source, -1, 'groupId', 'group_id'),
        fileId: fieldInt(source, -1, 'fileId', 'file_id'),
    };
}

function normalizeMapElements(value) {
    const result = {};
    if( value instanceof Map ) {
        for( const [id, entry] of value ) addMapElement(result, id, entry);
    } else if( Array.isArray(value) ) {
        value.forEach((entry, index) => addMapElement(result, record(entry).id ?? index, entry));
    } else {
        for( const [id, entry] of Object.entries(record(value)) ) addMapElement(result, id, entry);
    }
    return result;
}

function addMapElement(result, rawId, value) {
    const id = integer(rawId, -1);
    if( id < 0 ) return;
    if( Number.isFinite(Number(value)) ) {
        result[id] = { category: integer(value, -1) };
        return;
    }
    const entry = record(value);
    result[id] = {
        ...entry,
        category: integer(entry.category, -1),
    };
}

function restoreRuntimeState(state, seed) {
    const initialArea = currentArea(state);
    const requestedArea = areaById(state, integer(
        seed.currentMapId ?? seed.current_map_id, initialArea?.id ?? -1));
    if( requestedArea ) state.currentMapId = requestedArea.id;
    state.zoomPercentage = normalizeZoom(integer(
        seed.zoomPercentage ?? seed.zoom_percentage, state.zoomPercentage));
    state.zoomScaleFp = positiveInteger(
        seed.zoomScaleFp ?? seed.zoom_scale_fp, zoomScaleFpFor(state.zoomPercentage));
    state.zoomScaleNowFp = positiveInteger(
        seed.zoomScaleNowFp ?? seed.zoom_scale_now_fp, state.zoomScaleFp);
    state.displayX = integer(seed.displayX ?? seed.display_x, state.displayX);
    state.displayY = integer(seed.displayY ?? seed.display_y, state.displayY);
    state.targetX = integer(seed.targetX ?? seed.target_x, -1);
    state.targetY = integer(seed.targetY ?? seed.target_y, -1);
    state.displayPixelWidth = Math.max(0, integer(
        seed.displayPixelWidth ?? seed.display_pixel_width, state.displayPixelWidth));
    state.displayPixelHeight = Math.max(0, integer(
        seed.displayPixelHeight ?? seed.display_pixel_height, state.displayPixelHeight));
    state.elementsEnabled = booleanOr(seed.elementsEnabled ?? seed.elements_enabled, true);
    state.perpetualFlash = booleanOr(seed.perpetualFlash ?? seed.perpetual_flash, false);
    state.maxFlashCount = positiveInteger(
        seed.maxFlashCount ?? seed.max_flash_count, DEFAULT_MAX_FLASH_COUNT);
    state.cyclesPerFlash = positiveInteger(
        seed.cyclesPerFlash ?? seed.cycles_per_flash, DEFAULT_CYCLES_PER_FLASH);
    state.flashCount = integer(seed.flashCount ?? seed.flash_count, -1);
    state.flashCycle = integer(seed.flashCycle ?? seed.flash_cycle, -1);
    state.disabledElements = uniqueIntegers(seed.disabledElements ?? seed.disabled_elements);
    state.disabledCategories = uniqueIntegers(seed.disabledCategories ?? seed.disabled_categories);
    state.flashingElements = uniqueIntegers(seed.flashingElements ?? seed.flashing_elements);
    state.flashingCategories = uniqueIntegers(seed.flashingCategories ?? seed.flashing_categories);
    state.iconIndex = Math.max(0, integer(seed.iconIndex ?? seed.icon_index, 0));
    state.eventElement = integer(seed.eventElement ?? seed.event_element, -1);
    state.eventCoord1 = integer(seed.eventCoord1 ?? seed.event_coord1, -1);
    state.eventCoord2 = integer(seed.eventCoord2 ?? seed.event_coord2, -1);
    recomputeDisplaySize(state);
}

function selectInitialArea(state) {
    const area = state.areas.find((entry) => entry.isMain) ?? state.areas[0] ?? null;
    if( area ) selectArea(state, area);
}

function selectArea(state, area) {
    state.currentMapId = area.id;
    state.iconIndex = 0;
    if( area.zoom > 0 ) setZoomInstant(state, area.zoom);
    const origin = unpackWorldMapCoord(area.origin);
    jumpToSourceOrOriginInstant(state, origin.plane, origin.x, origin.y);
}

function jumpToSourceOrOriginInstant(state, plane, x, y) {
    const area = currentArea(state);
    if( !area ) return;
    let position = areaPosition(area, plane, x, y);
    if( position ) {
        setDisplayPosition(state, position[0], position[1]);
        return;
    }
    const origin = unpackWorldMapCoord(area.origin);
    position = areaPosition(area, origin.plane, origin.x, origin.y);
    if( position ) {
        setDisplayPosition(state, position[0], position[1]);
        return;
    }
    const [minX, minY, maxX, maxY] = areaBounds(area);
    setDisplayPosition(state, Math.trunc((minX + maxX) / 2), Math.trunc((minY + maxY) / 2));
}

function jumpToDisplayCoord(state, packed, instant) {
    const area = currentArea(state);
    const { x, y } = unpackWorldMapCoord(packed);
    if( instant ) {
        if( area ) setDisplayPosition(state, x, y);
        return;
    }
    if( !areaContainsPosition(area, x, y) ) return;
    state.targetX = x;
    state.targetY = y;
}

function jumpToSourceCoord(state, packed, instant) {
    const area = currentArea(state);
    const position = area ? worldMapSourceToDisplay(area, packed) : null;
    if( !position ) return;
    if( instant ) setDisplayPosition(state, position[0], position[1]);
    else {
        state.targetX = position[0];
        state.targetY = position[1];
    }
}

function jumpToMap(state, mapId, fallbackCoord) {
    const area = areaById(state, mapId);
    if( !area ) return;
    state.currentMapId = area.id;
    state.iconIndex = 0;
    if( area.zoom > 0 ) setZoomInstant(state, area.zoom);
    const fallback = unpackWorldMapCoord(fallbackCoord);
    /* Native has no local-player provider yet, so both opcodes use fallback. */
    jumpToSourceOrOriginInstant(state, fallback.plane, fallback.x, fallback.y);
}

function setDisplayPosition(state, x, y) {
    state.displayX = x;
    state.displayY = y;
    state.targetX = -1;
    state.targetY = -1;
}

function setZoom(state, zoom) {
    state.zoomPercentage = normalizeZoom(zoom);
    state.zoomScaleFp = zoomScaleFpFor(state.zoomPercentage);
}

function setZoomInstant(state, zoom) {
    setZoom(state, zoom);
    state.zoomScaleNowFp = state.zoomScaleFp;
    recomputeDisplaySize(state);
}

function normalizeZoom(zoom) {
    return [25, 37, 50, 75, 100].includes(zoom) ? zoom : 200;
}

function zoomScaleFpFor(zoom) {
    if( zoom === 25 ) return ZOOM_SCALE_ONE;
    if( zoom === 37 ) return ZOOM_SCALE_ONE * 3 / 2;
    if( zoom === 50 ) return ZOOM_SCALE_ONE * 2;
    if( zoom === 75 ) return ZOOM_SCALE_ONE * 3;
    if( zoom === 100 ) return ZOOM_SCALE_ONE * 4;
    return ZOOM_SCALE_ONE * 8;
}

function recomputeDisplaySize(state) {
    const scale = state.zoomScaleNowFp > 0 ? state.zoomScaleNowFp : ZOOM_SCALE_ONE;
    const width = state.displayPixelWidth > 0 ? state.displayPixelWidth : DEFAULT_DISPLAY_WIDTH;
    const height = state.displayPixelHeight > 0 ? state.displayPixelHeight : DEFAULT_DISPLAY_HEIGHT;
    state.displayWidth = Math.trunc((width * ZOOM_SCALE_ONE + scale - 1) / scale);
    state.displayHeight = Math.trunc((height * ZOOM_SCALE_ONE + scale - 1) / scale);
}

function cycleZoom(state) {
    let current = state.zoomScaleNowFp;
    const target = state.zoomScaleFp;
    if( target <= 0 ) return false;
    if( current <= 0 ) {
        state.zoomScaleNowFp = target;
        recomputeDisplaySize(state);
        return true;
    }
    if( current === target ) return false;
    const step = Math.max(1, Math.trunc(current / ZOOM_STEPS));
    if( current < target ) current = Math.min(target, current + step);
    else current = Math.max(target, current - step);
    state.zoomScaleNowFp = current;
    recomputeDisplaySize(state);
    return true;
}

function cycleFlash(state) {
    if( !hasActiveFlashes(state) ) return false;
    state.flashCycle = (state.flashCycle < 0 ? 0 : state.flashCycle) + 1;
    if( state.cyclesPerFlash > 0 && state.flashCycle % state.cyclesPerFlash === 0 ) {
        state.flashCount = (state.flashCount < 0 ? 0 : state.flashCount) + 1;
        if( state.flashCount >= state.maxFlashCount && !state.perpetualFlash )
            stopFlashes(state);
    }
    return true;
}

function hasActiveFlashes(state) {
    return state.flashingElements.length > 0 || state.flashingCategories.length > 0;
}

function flashElement(state, id) {
    state.flashingElements = [id];
    state.flashingCategories = [];
    state.flashCount = 0;
    state.flashCycle = 0;
}

function flashCategory(state, id) {
    state.flashingElements = [];
    state.flashingCategories = [id];
    state.flashCount = 0;
    state.flashCycle = 0;
}

function stopFlashes(state) {
    state.flashingElements = [];
    state.flashingCategories = [];
    state.flashCount = -1;
    state.flashCycle = -1;
}

function setIdEnabled(disabled, id, enabled) {
    const index = disabled.indexOf(id);
    if( enabled ) {
        if( index >= 0 ) {
            disabled[index] = disabled[disabled.length - 1];
            disabled.pop();
        }
    } else if( index < 0 ) disabled.push(id);
}

function nextIcon(state) {
    const area = currentArea(state);
    if( !area ) return [-1, -1];
    while( state.iconIndex < area.icons.length ) {
        const icon = area.icons[state.iconIndex++];
        if( !iconVisibleForIteration(state, area, icon) ) continue;
        return [icon.element, iconDisplayCoord(area, icon)];
    }
    return [-1, -1];
}

function iconVisibleForIteration(state, area, icon) {
    if( !state.elementsEnabled || icon.element < 0 ||
        state.disabledElements.includes(icon.element) ) return false;
    const category = elementCategory(state, icon.element);
    if( category >= 0 && state.disabledCategories.includes(category) ) return false;
    const coord = unpackWorldMapCoord(icon.coord);
    /* `hidden` is retained for the renderer but current rs_worldmap.c does not
     * consult it for the script-visible icon iterator. */
    return areaContainsCoord(area, coord.plane, coord.x, coord.y);
}

function iconDisplayCoord(area, icon) {
    const coord = unpackWorldMapCoord(icon.coord);
    const display = areaPosition(area, coord.plane, coord.x, coord.y);
    return display ? packWorldMapCoord(coord.plane, display[0], display[1]) : icon.coord;
}

function nearestIcon(state, elementId, packedSourceCoord) {
    const area = currentArea(state);
    if( !area ) return -1;
    const source = unpackWorldMapCoord(packedSourceCoord);
    /* This deliberately mirrors current native behavior: the query x/y is
     * checked and compared in map-surface space. */
    if( !areaContainsPosition(area, source.x, source.y) ) return -1;
    let nearest = null;
    let nearestDistance = 0;
    for( const icon of area.icons ) {
        if( icon.element !== elementId || !iconVisibleForIteration(state, area, icon) ) continue;
        const display = unpackWorldMapCoord(iconDisplayCoord(area, icon));
        const dx = display.x - source.x;
        const dy = display.y - source.y;
        const distance = dx * dx + dy * dy;
        if( nearest !== null && distance >= nearestDistance ) continue;
        nearest = icon;
        nearestDistance = distance;
    }
    return nearest ? iconDisplayCoord(area, nearest) : -1;
}

function elementCategory(state, elementId) {
    return integer(state.mapElements[String(elementId)]?.category, -1);
}

function mapAtCoord(state, packed) {
    for( const area of state.areas )
        if( worldMapAreaContainsCoord(area, packed) ) return area.id;
    return -1;
}

function areaContainsCoord(area, plane, x, y) {
    if( !area ) return false;
    return area.sections.some((section) => sectionContainsCoord(section, plane, x, y));
}

function areaContainsPosition(area, x, y) {
    if( !area ) return false;
    return area.sections.some((section) => sectionContainsPosition(section, x, y));
}

function areaPosition(area, plane, x, y) {
    if( !area ) return null;
    for( const section of area.sections ) {
        if( !sectionContainsCoord(section, plane, x, y) ) continue;
        return [sectionDstXLow(section) - sectionSrcXLow(section) + x,
            sectionDstYLow(section) - sectionSrcYLow(section) + y];
    }
    return null;
}

function areaCoord(area, x, y) {
    if( !area ) return null;
    for( const section of area.sections ) {
        if( !sectionContainsPosition(section, x, y) ) continue;
        return {
            plane: section.minPlane,
            x: sectionSrcXLow(section) - sectionDstXLow(section) + x,
            y: sectionSrcYLow(section) - sectionDstYLow(section) + y,
        };
    }
    return null;
}

function sectionContainsCoord(section, plane, x, y) {
    return plane >= section.minPlane && plane < section.minPlane + section.planes &&
        x >= sectionSrcXLow(section) && x <= sectionSrcXHigh(section) &&
        y >= sectionSrcYLow(section) && y <= sectionSrcYHigh(section);
}

function sectionContainsPosition(section, x, y) {
    return x >= sectionDstXLow(section) && x <= sectionDstXHigh(section) &&
        y >= sectionDstYLow(section) && y <= sectionDstYHigh(section);
}

function sectionSrcXLow(section) {
    return section.srcRegionX * REGION_TILES + section.srcChunkX * CHUNK_TILES;
}
function sectionSrcYLow(section) {
    return section.srcRegionY * REGION_TILES + section.srcChunkY * CHUNK_TILES;
}
function sectionDstXLow(section) {
    return section.dstRegionX * REGION_TILES + section.dstChunkX * CHUNK_TILES;
}
function sectionDstYLow(section) {
    return section.dstRegionY * REGION_TILES + section.dstChunkY * CHUNK_TILES;
}
function sectionSrcXHigh(section) {
    return section.srcRegionXEnd * REGION_TILES +
        section.srcChunkXEnd * CHUNK_TILES + CHUNK_TILES - 1;
}
function sectionSrcYHigh(section) {
    return section.srcRegionYEnd * REGION_TILES +
        section.srcChunkYEnd * CHUNK_TILES + CHUNK_TILES - 1;
}
function sectionDstXHigh(section) {
    return section.dstRegionXEnd * REGION_TILES +
        section.dstChunkXEnd * CHUNK_TILES + CHUNK_TILES - 1;
}
function sectionDstYHigh(section) {
    return section.dstRegionYEnd * REGION_TILES +
        section.dstChunkYEnd * CHUNK_TILES + CHUNK_TILES - 1;
}

function areaWidth(area) {
    return (area.regionHighX - area.regionLowX + 1) * REGION_TILES;
}

function areaHeight(area) {
    return (area.regionHighY - area.regionLowY + 1) * REGION_TILES;
}

function areaBounds(area) {
    return [
        area.regionLowX * REGION_TILES,
        area.regionLowY * REGION_TILES,
        area.regionHighX * REGION_TILES + REGION_TILES - 1,
        area.regionHighY * REGION_TILES + REGION_TILES - 1,
    ];
}

function currentArea(state) {
    return areaById(state, state.currentMapId);
}

function areaById(state, id) {
    return state.areas.find((area) => area.id === id) ?? null;
}

function requestInteger(request, index, ...names) {
    for( const name of names ) {
        if( Object.prototype.hasOwnProperty.call(request, name) )
            return integer(request[name], 0);
    }
    if( Array.isArray(request.args) && request.args[index] !== undefined )
        return integer(request.args[index], 0);
    return integer(request[`arg${index}`], 0);
}

function runtimeSignature(state) {
    return [
        state.currentMapId,
        state.zoomPercentage, state.zoomScaleFp, state.zoomScaleNowFp,
        state.displayX, state.displayY, state.targetX, state.targetY,
        state.displayPixelWidth, state.displayPixelHeight,
        state.displayWidth, state.displayHeight,
        state.elementsEnabled ? 1 : 0, state.perpetualFlash ? 1 : 0,
        state.maxFlashCount, state.cyclesPerFlash, state.flashCount, state.flashCycle,
        state.disabledElements.join(','), state.disabledCategories.join(','),
        state.flashingElements.join(','), state.flashingCategories.join(','),
        state.iconIndex, state.eventElement, state.eventCoord1, state.eventCoord2,
    ].join('|');
}

function configBlocks(text) {
    const blocks = [];
    let current = null;
    for( const rawLine of String(text ?? '').split(/\r?\n/) ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            current = { name: opened[1].trim(), entries: [] };
            blocks.push(current);
            continue;
        }
        if( !current ) continue;
        const equals = line.indexOf('=');
        if( equals <= 0 ) continue;
        current.entries.push({
            key: line.slice(0, equals).trim(),
            value: line.slice(equals + 1),
        });
    }
    return blocks;
}

function symbolIds(value) {
    const result = new Map();
    if( value instanceof Map ) {
        for( const [name, id] of value ) {
            if( Number.isInteger(Number(id)) ) result.set(String(name), Number(id));
        }
        return result;
    }
    if( value && typeof value === 'object' && !Array.isArray(value) ) {
        for( const [name, id] of Object.entries(value) ) {
            if( Number.isInteger(Number(id)) ) result.set(name, Number(id));
            else if( Number.isInteger(Number(name)) && typeof id === 'string' )
                result.set(id, Number(name));
        }
        return result;
    }
    for( const rawLine of String(value ?? '').split(/\r?\n/) ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const match = /^(\d+)=([^=]+)$/.exec(line);
        if( match ) {
            const name = packName(match[2]);
            if( name ) result.set(name, Number(match[1]));
        }
    }
    return result;
}

function blockId(name, symbols, ordinal) {
    if( symbols.has(name) ) return symbols.get(name);
    if( /^\d+$/.test(name) ) return Number(name);
    const suffixed = /(?:^|_)(\d+)$/.exec(name);
    return suffixed ? Number(suffixed[1]) : ordinal;
}

function collection(value) {
    if( value instanceof Map ) return [...value.entries()].map(([key, entry]) =>
        withFallbackId(entry, key));
    if( Array.isArray(value) ) return value;
    if( !value || typeof value !== 'object' ) return [];
    return Object.entries(value).map(([key, entry]) => withFallbackId(entry, key));
}

function withFallbackId(value, rawId) {
    const entry = record(value);
    if( entry.id !== undefined ) return entry;
    const id = Number(rawId);
    return Number.isInteger(id) ? { ...entry, id } : entry;
}

function unescapeConfig(value) {
    return String(value).replace(/\\([\\nrt,=])/g, (_match, escaped) => ({
        '\\': '\\', n: '\n', r: '\r', t: '\t', ',': ',', '=': '=',
    })[escaped]);
}

function parseInteger(value, fallback) {
    const number = typeof value === 'string' && /^[-+]?0x[\da-f]+$/i.test(value.trim())
        ? Number.parseInt(value, 16) : Number(value);
    return Number.isFinite(number) ? Math.trunc(number) : fallback;
}

function integer(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) : fallback;
}

function positiveInteger(value, fallback) {
    const number = integer(value, fallback);
    return number > 0 ? number : fallback;
}

function fieldInt(source, fallback, ...names) {
    for( const name of names )
        if( Object.prototype.hasOwnProperty.call(source, name) )
            return integer(source[name], fallback);
    return fallback;
}

function parseBoolean(value) {
    if( value === undefined || value === null ) return false;
    if( typeof value === 'string' ) {
        const normalized = value.trim().toLowerCase();
        if( normalized === 'yes' || normalized === 'true' ) return true;
        if( normalized === 'no' || normalized === 'false' || normalized === '' ) return false;
    }
    const number = Number(value);
    return Number.isFinite(number) && number !== 0;
}

function booleanOr(value, fallback) {
    return value === undefined || value === null ? fallback : parseBoolean(value);
}

function uniqueIntegers(value) {
    const result = [];
    for( const entry of Array.isArray(value) ? value : [] ) {
        const id = integer(entry, 0);
        if( !result.includes(id) ) result.push(id);
    }
    return result;
}

function record(value) {
    return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
}

function firstDefined(...values) {
    return values.find((value) => value !== undefined && value !== null);
}

function looksLikeRuntimeState(value) {
    return Boolean(value && typeof value === 'object' &&
        (value.currentMapId !== undefined || value.current_map_id !== undefined));
}

function assertState(state) {
    if( !state || state[WORLDMAP_STATE] !== true )
        throw new TypeError('world-map state is invalid');
}
