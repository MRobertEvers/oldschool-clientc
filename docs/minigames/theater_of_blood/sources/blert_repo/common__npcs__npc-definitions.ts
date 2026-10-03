import npcDefinitionsJson from '../generated/npc_definitions.json';
import {
  ChallengeMode,
  MaidenCrab,
  MaidenCrabPosition,
  MaidenCrabSpawn,
  Nylo,
  NyloSpawn,
  NyloStyle,
  RoomNpc,
  RoomNpcType,
  VerzikCrab,
  VerzikCrabSpawn,
} from '../challenge';

export type NpcDefinition = {
  fullName: string;
  shortName: string;
  canonicalId: number;
  semanticId: boolean;
  size: number;
  mode: ChallengeMode;
  /** Maximum tiles moved per tick. */
  maxSpeed: number;
};

function parseMode(name: string): ChallengeMode {
  const mode = ChallengeMode[name as keyof typeof ChallengeMode];
  if (mode === undefined) {
    throw new Error(`Unknown challenge mode ${name}`);
  }
  return mode;
}

const NPC_DEFINITIONS: ReadonlyMap<number, NpcDefinition> = new Map(
  npcDefinitionsJson.map((definition) => [
    definition.id,
    {
      fullName: definition.fullName,
      shortName: definition.shortName,
      canonicalId: definition.canonicalId,
      semanticId: definition.semanticId,
      size: definition.size,
      mode: parseMode(definition.mode),
      maxSpeed: definition.maxSpeed,
    },
  ]),
);

export function getNpcDefinition(npcId: number): NpcDefinition | null {
  return NPC_DEFINITIONS.get(npcId) ?? null;
}

function maidenCrabSpawnString(spawn: MaidenCrabSpawn) {
  switch (spawn) {
    case MaidenCrabSpawn.SEVENTIES:
      return '70s';
    case MaidenCrabSpawn.FIFTIES:
      return '50s';
    case MaidenCrabSpawn.THIRTIES:
      return '30s';
  }
}

function maidenCrabPositionString(position: MaidenCrabPosition) {
  switch (position) {
    case MaidenCrabPosition.S1:
      return 'S1';
    case MaidenCrabPosition.N1:
      return 'N1';
    case MaidenCrabPosition.S2:
      return 'S2';
    case MaidenCrabPosition.N2:
      return 'N2';
    case MaidenCrabPosition.S3:
      return 'S3';
    case MaidenCrabPosition.N3:
      return 'N3';
    case MaidenCrabPosition.N4_INNER:
      return 'N4 Inner';
    case MaidenCrabPosition.N4_OUTER:
      return 'N4 Outer';
    case MaidenCrabPosition.S4_INNER:
      return 'S4 Inner';
    case MaidenCrabPosition.S4_OUTER:
      return 'S4 Outer';
  }
}

function nyloStyleToString(style: NyloStyle): string {
  switch (style) {
    case NyloStyle.MELEE:
      return 'melee';
    case NyloStyle.RANGE:
      return 'range';
    case NyloStyle.MAGE:
      return 'mage';
  }
}

function nyloSpawnToString(spawn: NyloSpawn): string {
  switch (spawn) {
    case NyloSpawn.EAST:
      return 'east';
    case NyloSpawn.WEST:
      return 'west';
    case NyloSpawn.SOUTH:
      return 'south';
    case NyloSpawn.SPLIT:
      return 'split';
    case NyloSpawn.UNKNOWN:
      return 'unknown';
  }
}

function verzikSpawnToString(spawn: VerzikCrabSpawn): string {
  switch (spawn) {
    case VerzikCrabSpawn.UNKNOWN:
      return 'unknown';
    case VerzikCrabSpawn.NORTH:
      return 'north';
    case VerzikCrabSpawn.NORTHEAST:
      return 'northeast';
    case VerzikCrabSpawn.NORTHWEST:
      return 'northwest';
    case VerzikCrabSpawn.EAST:
      return 'east';
    case VerzikCrabSpawn.SOUTH:
      return 'south';
    case VerzikCrabSpawn.SOUTHEAST:
      return 'southeast';
    case VerzikCrabSpawn.SOUTHWEST:
      return 'southwest';
    case VerzikCrabSpawn.WEST:
      return 'west';
  }
}

/**
 * Returns a human-readable name for the given NPC, including metadata about
 * the NPC's type.
 *
 * @param npc The NPC to get the friendly name for.
 * @param allNpcs An optional map of all NPCs in the room, used for resolving
 *   any NPCs referenced by the given NPC.
 * @returns A friendly name for the NPC.
 */
export function npcFriendlyName(
  npc: RoomNpc,
  allNpcs?: Map<number, RoomNpc>,
): string {
  switch (npc.type) {
    case RoomNpcType.MAIDEN_CRAB:
      const maidenCrab = (npc as MaidenCrab).maidenCrab;
      const position = maidenCrabPositionString(maidenCrab.position);
      const spawn = maidenCrabSpawnString(maidenCrab.spawn);
      return `${spawn} ${position}`;

    case RoomNpcType.NYLO: {
      const nylo = (npc as Nylo).nylo;
      const wave = nylo.wave === 0 ? 'Unknown' : nylo.wave;
      const style = nyloStyleToString(nylo.style);

      if (nylo.spawnType === NyloSpawn.SPLIT && !nylo.big) {
        let name = `${wave} ${style} split`;

        if (allNpcs !== undefined) {
          const parent = allNpcs.get(nylo.parentRoomId);
          if (parent !== undefined) {
            const parentName = npcFriendlyName(parent, allNpcs);
            name += ` (from ${parentName})`;
          }
        }
        return name;
      }

      let name;
      if (nylo.spawnType === NyloSpawn.UNKNOWN) {
        name = `${wave} ${style}`;
      } else {
        name = `${wave} ${nyloSpawnToString(nylo.spawnType)} ${style}`;
      }

      if (nylo.big) {
        name += ' big';
      }
      return name;
    }

    case RoomNpcType.VERZIK_CRAB:
      const verzikCrab = (npc as VerzikCrab).verzikCrab;
      return `${verzikCrab.phase} ${verzikSpawnToString(verzikCrab.spawn)} crab`;

    case RoomNpcType.BASIC:
      // No special handling.
      break;
  }

  return getNpcDefinition(npc.spawnNpcId)?.fullName ?? 'Unknown NPC';
}
