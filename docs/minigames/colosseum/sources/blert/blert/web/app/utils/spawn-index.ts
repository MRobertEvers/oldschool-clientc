import {
  ChallengeType,
  Coords,
  NpcId,
  Stage,
  isColosseumStage,
  isInfernoStage,
} from '@blert/common';

type LosTool = {
  host: string;
  npcTypes: number[];
  playerHash: boolean;
};

type Arena = {
  type: ChallengeType;
  base: Coords;
  width: number;
  height: number;
  npcTypes: NpcId[];
  spawnTiles: Coords[];
  /** Every value a spawn on a spawn tile can have, by local tile bits. */
  tileSpawns: Map<number, number[]>;
  losTool: LosTool;
};

const COLOSSEUM_ARENA: Arena = {
  type: ChallengeType.COLOSSEUM,
  base: { x: 1808, y: 3123 },
  width: 34,
  height: 34,
  npcTypes: [
    NpcId.SERPENT_SHAMAN,
    NpcId.JAVELIN_COLOSSUS,
    NpcId.MANTICORE,
    NpcId.SHOCKWAVE_COLOSSUS,
  ],
  spawnTiles: [
    { x: 1811, y: 3109 },
    { x: 1817, y: 3106 },
    { x: 1825, y: 3114 },
    { x: 1821, y: 3109 },
    { x: 1827, y: 3109 },
    { x: 1836, y: 3109 },
    { x: 1832, y: 3107 },
    { x: 1811, y: 3104 },
    { x: 1836, y: 3104 },
    { x: 1821, y: 3103 },
    { x: 1827, y: 3103 },
    { x: 1824, y: 3099 },
  ],
  tileSpawns: new Map(),
  losTool: {
    host: 'los.colosim.com',
    npcTypes: [1, 2, 4, 6],
    playerHash: true,
  },
};

const INFERNO_ARENA: Arena = {
  type: ChallengeType.INFERNO,
  base: { x: 2257, y: 5358 },
  width: 29,
  height: 30,
  npcTypes: [
    NpcId.JAL_MEJRAH,
    NpcId.JAL_AK,
    NpcId.JAL_IMKOT,
    NpcId.JAL_XIL,
    NpcId.JAL_ZEK,
  ],
  spawnTiles: [
    { x: 2258, y: 5330 },
    { x: 2262, y: 5335 },
    { x: 2272, y: 5330 },
    { x: 2258, y: 5353 },
    { x: 2273, y: 5341 },
    { x: 2279, y: 5353 },
    { x: 2260, y: 5347 },
    { x: 2280, y: 5333 },
    { x: 2280, y: 5346 },
  ],
  tileSpawns: new Map(),
  losTool: {
    host: 'ifreedive-osrs.github.io',
    npcTypes: [1, 2, 5, 6, 7],
    playerHash: false,
  },
};

for (const arena of [COLOSSEUM_ARENA, INFERNO_ARENA]) {
  for (const tile of arena.spawnTiles) {
    const local = toLocal(arena, tile.x, tile.y);
    const bits = packSpawn(local.x, local.y);
    arena.tileSpawns.set(
      bits,
      arena.npcTypes.map((_, type) => (type << 10) | bits),
    );
  }
}

function arenaForStage(stage: Stage): Arena | null {
  if (isColosseumStage(stage)) {
    return COLOSSEUM_ARENA;
  }
  if (isInfernoStage(stage)) {
    return INFERNO_ARENA;
  }
  return null;
}

function arenaForType(type: ChallengeType): Arena | null {
  switch (type) {
    case ChallengeType.COLOSSEUM:
      return COLOSSEUM_ARENA;
    case ChallengeType.INFERNO:
      return INFERNO_ARENA;
    default:
      return null;
  }
}

/** Returns the arena an NPC spawns in and its type code within it, if any. */
function spawnType(npcId: NpcId): [Arena, number] | null {
  for (const arena of [COLOSSEUM_ARENA, INFERNO_ARENA]) {
    const type = arena.npcTypes.indexOf(npcId);
    if (type !== -1) {
      return [arena, type];
    }
  }
  return null;
}

export type SpawnedNpc<T = NpcId> = Coords & {
  npcId: T;
};

// y is inverted from the game because that's what popular spawn analysis tools
// use and we want to be compatible.

function toWorld(arena: Arena, x: number, y: number): Coords {
  return { x: arena.base.x + x, y: arena.base.y - y };
}

function toLocal(arena: Arena, x: number, y: number): Coords {
  return { x: x - arena.base.x, y: arena.base.y - y };
}

function inArena(arena: Arena, tile: Coords): boolean {
  return (
    tile.x >= arena.base.x &&
    tile.x < arena.base.x + arena.width &&
    tile.y <= arena.base.y &&
    tile.y > arena.base.y - arena.height
  );
}

/** Returns the arena in which a world tile lies, if any. */
function arenaAt(tile: Coords): Arena | null {
  if (inArena(INFERNO_ARENA, tile)) {
    return INFERNO_ARENA;
  }
  if (inArena(COLOSSEUM_ARENA, tile)) {
    return COLOSSEUM_ARENA;
  }
  return null;
}

/**
 * Parses a stored wave spawn into an NPC with coords.
 * @param stage Stage in which the spawn occurred.
 * @param value The stored spawn value.
 * @returns The parsed spawn, or null if the value is invalid.
 */
export function decodeSpawn(stage: Stage, value: number): SpawnedNpc | null {
  const arena = arenaForStage(stage);
  if (arena === null) {
    return null;
  }

  const npcId = arena.npcTypes[value >> 10];
  if (npcId === undefined) {
    return null;
  }

  return { npcId, ...toWorld(arena, (value >> 5) & 0x1f, value & 0x1f) };
}

/**
 * Encodes a wave spawn into its stored value.
 * @param npc The NPC spawn with coordinates in either global or local space.
 * @returns The encoded spawn alongside the challenge to which it belongs, or
 *   `null` if the NPC and tile are incompatible.
 */
export function encodeSpawn(
  npc: SpawnedNpc<NpcId | string>,
): { type: ChallengeType; value: number } | null {
  const npcId =
    typeof npc.npcId === 'string' ? npcFromToken(npc.npcId) : npc.npcId;
  const spawn = npcId === null ? null : spawnType(npcId);
  if (spawn === null) {
    return null;
  }

  const [arena, type] = spawn;
  const at = arenaAt(npc);
  if (at !== null && at !== arena) {
    return null;
  }

  const local = at === null ? npc : toLocal(arena, npc.x, npc.y);
  return { type: arena.type, value: packSpawn(local.x, local.y, type) };
}

/**
 * Encodes every stored value a spawn on a tile can have.
 * @param type Challenge whose arena the tile is in.
 * @param tile Coordinates of the tile in either global or local space.
 * @returns The values, or null if the tile is not a spawn tile.
 */
export function encodeTile(type: ChallengeType, tile: Coords): number[] | null {
  const arena = arenaForType(type);
  if (arena === null) {
    return null;
  }

  const local = inArena(arena, tile) ? toLocal(arena, tile.x, tile.y) : tile;
  return arena.tileSpawns.get(packSpawn(local.x, local.y)) ?? null;
}

/**
 * Parses a stored player start tile into world coords.
 * @param stage Stage in which the spawn occurred.
 * @param value The stored player start tile value..
 * @returns The parsed coords, or null if the value is invalid.
 */
export function decodePlayerTile(stage: Stage, value: number): Coords | null {
  const arena = arenaForStage(stage);
  if (arena === null) {
    return null;
  }

  return toWorld(arena, value & 0xff, (value >> 8) & 0xff);
}

/**
 * Encodes a player's start tile into its stored value.
 * @param tile Coordinates of the in tile either global or local space.
 * @returns The stored value, alongside the challenge whose arena contains the
 *   tile if it is global.
 */
export function encodePlayerTile(tile: Coords): {
  type: ChallengeType | null;
  value: number;
} {
  const arena = arenaAt(tile);
  const local = arena === null ? tile : toLocal(arena, tile.x, tile.y);
  return { type: arena?.type ?? null, value: (local.y << 8) | local.x };
}

export type LosToolSpawn = {
  type: ChallengeType;
  npcs: SpawnedNpc[];
  player: Coords | null;
};

const LOS_TOOL_MOB_REGEX = /^(\d{2})(\d{2})(\d+)[A-Za-z]*$/;

/**
 * Parses a colosim or ifreedive spawn URL.
 * @param link The LoS tool URL.
 * @returns The parsed spawn, or null if the URL does not contain one.
 */
export function decodeLosToolUrl(
  link: string,
): Omit<LosToolSpawn, 'player'> | null {
  let url: URL;
  try {
    url = new URL(link);
  } catch {
    return null;
  }

  const arena = [COLOSSEUM_ARENA, INFERNO_ARENA].find(
    (arena) => arena.losTool.host === url.host,
  );
  if (arena === undefined) {
    return null;
  }

  const npcs: SpawnedNpc[] = [];
  for (const token of url.search.slice(1).split('.')) {
    const match = LOS_TOOL_MOB_REGEX.exec(token);
    if (match === null) {
      continue;
    }
    const type = arena.losTool.npcTypes.indexOf(Number(match[3]));
    const tile = toWorld(arena, Number(match[1]), Number(match[2]));
    if (type !== -1 && inArena(arena, tile)) {
      npcs.push({ npcId: arena.npcTypes[type], ...tile });
    }
  }

  return { type: arena.type, npcs };
}

/**
 * Builds a colosim or ifreedive link to a spawn.
 * @param spawn The spawn to show in the LoS tool.
 * @returns The constructed URL, or null if the challenge type is not supported.
 */
export function encodeLosToolUrl(spawn: LosToolSpawn): string | null {
  const arena = arenaForType(spawn.type);
  if (arena === null) {
    return null;
  }

  let url = `https://${arena.losTool.host}/?`;
  for (const npc of spawn.npcs) {
    const local = toLocal(arena, npc.x, npc.y);
    const type = arena.losTool.npcTypes[arena.npcTypes.indexOf(npc.npcId)];
    url +=
      `${String(local.x).padStart(2, '0')}` +
      `${String(local.y).padStart(2, '0')}${type}.`;
  }
  if (arena.losTool.playerHash && spawn.player !== null) {
    url += `#${encodePlayerTile(spawn.player).value}`;
  }
  return url;
}

/**
 * Builds the value of a `spawn` search parameter.
 * @param stage Stage in which the NPCs spawn.
 * @param npcs NPCs the spawn must contain.
 * @returns The parameter value.
 */
export function spawnQueryParam(stage: Stage, npcs: SpawnedNpc[]): string {
  const clauses = npcs.map(
    (npc) => `npc:${NPC_ALIASES[npc.npcId][0]}@${npc.x}.${npc.y}`,
  );
  return [`stage:${stage}`, ...clauses].join(';');
}

function packSpawn(x: number, y: number, type: number = 0): number {
  return (type << 10) | (x << 5) | y;
}

/** Names accepted for an NPC in a query. The first is used to build one. */
const NPC_ALIASES: Record<number, string[]> = {
  [NpcId.SERPENT_SHAMAN]: ['shaman', 'serpent-shaman'],
  [NpcId.JAVELIN_COLOSSUS]: ['javelin-colossus', 'javelin'],
  [NpcId.MANTICORE]: ['manticore', 'manti'],
  [NpcId.SHOCKWAVE_COLOSSUS]: ['shockwave-colossus', 'shockwave'],
  [NpcId.JAL_MEJRAH]: ['bat', 'jal-mejrah'],
  [NpcId.JAL_AK]: ['blob', 'jal-ak'],
  [NpcId.JAL_IMKOT]: ['meleer', 'melee', 'jal-imkot'],
  [NpcId.JAL_XIL]: ['ranger', 'range', 'jal-xil'],
  [NpcId.JAL_ZEK]: ['mager', 'mage', 'jal-zek'],
};

const NPC_BY_ALIAS: Record<string, number> = Object.fromEntries(
  Object.entries(NPC_ALIASES).flatMap(([id, aliases]) =>
    aliases.map((alias) => [alias, Number(id)]),
  ),
);

function npcFromToken(token: string): NpcId | null {
  const fromAlias = NPC_BY_ALIAS[token];
  if (fromAlias !== undefined) {
    return fromAlias;
  }
  if (/^\d+$/.test(token) && NPC_ALIASES[Number(token)] !== undefined) {
    return Number(token);
  }
  return null;
}
