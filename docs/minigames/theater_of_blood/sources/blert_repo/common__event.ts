import { Event as EventProto } from './generated/event_pb';
import { RawItemDelta } from './item-delta';
import { RawPrayerSet } from './prayer-set';
import {
  EquipmentSlot,
  Handicap,
  MaidenCrabProperties,
  Maze,
  NpcAttack,
  NyloProperties,
  PlayerAttack,
  PlayerSpell,
  RawSkillLevel,
  Stage,
  VerzikCrabProperties,
  VerzikPhase,
  XarpusPhase,
} from './challenge';

export enum EventType {
  PLAYER_UPDATE = EventProto.Type.PLAYER_UPDATE,
  PLAYER_ATTACK = EventProto.Type.PLAYER_ATTACK,
  PLAYER_DEATH = EventProto.Type.PLAYER_DEATH,
  PLAYER_SPELL = EventProto.Type.PLAYER_SPELL,
  NPC_SPAWN = EventProto.Type.NPC_SPAWN,
  NPC_UPDATE = EventProto.Type.NPC_UPDATE,
  NPC_DEATH = EventProto.Type.NPC_DEATH,
  NPC_ATTACK = EventProto.Type.NPC_ATTACK,
  TOB_MAIDEN_CRAB_LEAK = EventProto.Type.TOB_MAIDEN_CRAB_LEAK,
  TOB_MAIDEN_BLOOD_SPLATS = EventProto.Type.TOB_MAIDEN_BLOOD_SPLATS,
  TOB_BLOAT_DOWN = EventProto.Type.TOB_BLOAT_DOWN,
  TOB_BLOAT_UP = EventProto.Type.TOB_BLOAT_UP,
  TOB_BLOAT_HANDS_DROP = EventProto.Type.TOB_BLOAT_HANDS_DROP,
  TOB_BLOAT_HANDS_SPLAT = EventProto.Type.TOB_BLOAT_HANDS_SPLAT,
  TOB_NYLO_WAVE_SPAWN = EventProto.Type.TOB_NYLO_WAVE_SPAWN,
  TOB_NYLO_WAVE_STALL = EventProto.Type.TOB_NYLO_WAVE_STALL,
  TOB_NYLO_CLEANUP_END = EventProto.Type.TOB_NYLO_CLEANUP_END,
  TOB_NYLO_BOSS_SPAWN = EventProto.Type.TOB_NYLO_BOSS_SPAWN,
  TOB_SOTE_MAZE_PROC = EventProto.Type.TOB_SOTE_MAZE_PROC,
  TOB_SOTE_MAZE_PATH = EventProto.Type.TOB_SOTE_MAZE_PATH,
  TOB_SOTE_MAZE_END = EventProto.Type.TOB_SOTE_MAZE_END,
  TOB_XARPUS_PHASE = EventProto.Type.TOB_XARPUS_PHASE,
  TOB_XARPUS_EXHUMED = EventProto.Type.TOB_XARPUS_EXHUMED,
  TOB_XARPUS_SPLAT = EventProto.Type.TOB_XARPUS_SPLAT,
  TOB_VERZIK_PHASE = EventProto.Type.TOB_VERZIK_PHASE,
  TOB_VERZIK_ATTACK_STYLE = EventProto.Type.TOB_VERZIK_ATTACK_STYLE,
  TOB_VERZIK_DAWN = EventProto.Type.TOB_VERZIK_DAWN,
  TOB_VERZIK_BOUNCE = EventProto.Type.TOB_VERZIK_BOUNCE,
  TOB_VERZIK_YELLOWS = EventProto.Type.TOB_VERZIK_YELLOWS,
  TOB_VERZIK_HEAL = EventProto.Type.TOB_VERZIK_HEAL,
  TOB_VERZIK_DAWN_DROP = EventProto.Type.TOB_VERZIK_DAWN_DROP,
  COLOSSEUM_HANDICAP_CHOICE = EventProto.Type.COLOSSEUM_HANDICAP_CHOICE,
  COLOSSEUM_DOOM_APPLIED = EventProto.Type.COLOSSEUM_DOOM_APPLIED,
  COLOSSEUM_TOTEM_HEAL = EventProto.Type.COLOSSEUM_TOTEM_HEAL,
  COLOSSEUM_REENTRY_POOLS = EventProto.Type.COLOSSEUM_REENTRY_POOLS,
  COLOSSEUM_SOL_DUST = EventProto.Type.COLOSSEUM_SOL_DUST,
  COLOSSEUM_SOL_GRAPPLE = EventProto.Type.COLOSSEUM_SOL_GRAPPLE,
  COLOSSEUM_SOL_POOLS = EventProto.Type.COLOSSEUM_SOL_POOLS,
  COLOSSEUM_SOL_LASERS = EventProto.Type.COLOSSEUM_SOL_LASERS,
  MOKHAIOTL_ATTACK_STYLE = EventProto.Type.MOKHAIOTL_ATTACK_STYLE,
  MOKHAIOTL_ORB = EventProto.Type.MOKHAIOTL_ORB,
  MOKHAIOTL_OBJECTS = EventProto.Type.MOKHAIOTL_OBJECTS,
  MOKHAIOTL_LARVA_LEAK = EventProto.Type.MOKHAIOTL_LARVA_LEAK,
  MOKHAIOTL_SHOCKWAVE = EventProto.Type.MOKHAIOTL_SHOCKWAVE,
}

export const isPlayerEvent = (event: BaseEvent): event is PlayerEvent => {
  return (
    event.type === EventType.PLAYER_UPDATE ||
    event.type === EventType.PLAYER_ATTACK ||
    event.type === EventType.PLAYER_DEATH ||
    event.type === EventType.PLAYER_SPELL
  );
};

export const isNpcEvent = (event: BaseEvent): event is NpcEvent => {
  return (
    event.type === EventType.NPC_SPAWN ||
    event.type === EventType.NPC_UPDATE ||
    event.type === EventType.NPC_DEATH
  );
};

export interface BaseEvent {
  type: EventType;
  stage: Stage;
  tick: number;
  xCoord: number;
  yCoord: number;
  acc?: boolean;
}

export interface BasePlayerEvent extends BaseEvent {
  player: BasicPlayer;
}

export interface PlayerUpdateEvent extends BasePlayerEvent {
  type: EventType.PLAYER_UPDATE;
  player: Player;
}

export interface PlayerAttackEvent extends BasePlayerEvent {
  type: EventType.PLAYER_ATTACK;
  attack: Attack;
}

export interface PlayerSpellEvent extends BasePlayerEvent {
  type: EventType.PLAYER_SPELL;
  spell: Spell;
}

export interface PlayerDeathEvent extends BasePlayerEvent {
  type: EventType.PLAYER_DEATH;
}

export type PlayerEvent =
  PlayerUpdateEvent | PlayerAttackEvent | PlayerSpellEvent | PlayerDeathEvent;

export interface BaseNpcEvent extends BaseEvent {
  npc: EventNpc;
}

export interface NpcSpawnEvent extends BaseNpcEvent {
  type: EventType.NPC_SPAWN;
}

export interface NpcUpdateEvent extends BaseNpcEvent {
  type: EventType.NPC_UPDATE;
}

export interface NpcDeathEvent extends BaseNpcEvent {
  type: EventType.NPC_DEATH;
}

export type NpcEvent = NpcSpawnEvent | NpcUpdateEvent | NpcDeathEvent;

export interface NpcAttackEvent extends BaseEvent {
  type: EventType.NPC_ATTACK;
  npc: BasicEventNpc;
  npcAttack: NpcAttackAction;
}

export interface MaidenBloodSplatsEvent extends BaseEvent {
  type: EventType.TOB_MAIDEN_BLOOD_SPLATS;
  maidenBloodSplats: Coords[];
}

export interface BloatDownEvent extends BaseEvent {
  type: EventType.TOB_BLOAT_DOWN;
  bloatDown: BloatDown;
}

export interface BloatUpEvent extends BaseEvent {
  type: EventType.TOB_BLOAT_UP;
}

export interface BloatHandsDropEvent extends BaseEvent {
  type: EventType.TOB_BLOAT_HANDS_DROP;
  bloatHands: Coords[];
}

export interface BloatHandsSplatEvent extends BaseEvent {
  type: EventType.TOB_BLOAT_HANDS_SPLAT;
  bloatHands: Coords[];
}

export interface NyloWaveSpawnEvent extends BaseEvent {
  type: EventType.TOB_NYLO_WAVE_SPAWN;
  nyloWave: NyloWave;
}

export interface NyloWaveStallEvent extends BaseEvent {
  type: EventType.TOB_NYLO_WAVE_STALL;
  nyloWave: NyloWave;
}

export interface NyloCleanupEndEvent extends BaseEvent {
  type: EventType.TOB_NYLO_CLEANUP_END;
}

export interface NyloBossSpawnEvent extends BaseEvent {
  type: EventType.TOB_NYLO_BOSS_SPAWN;
}

export interface SoteMazeEvent extends BaseEvent {
  type: EventType.TOB_SOTE_MAZE_PROC | EventType.TOB_SOTE_MAZE_END;
  soteMaze: SoteMaze;
}

export interface SoteMazePathEvent extends BaseEvent {
  type: EventType.TOB_SOTE_MAZE_PATH;
  soteMaze: SoteMazePath;
}

export interface XarpusPhaseEvent extends BaseEvent {
  type: EventType.TOB_XARPUS_PHASE;
  xarpusPhase: XarpusPhase;
}

export interface XarpusExhumedEvent extends BaseEvent {
  type: EventType.TOB_XARPUS_EXHUMED;
  xarpusExhumed: XarpusExhumed;
}

export interface XarpusSplatEvent extends BaseEvent {
  type: EventType.TOB_XARPUS_SPLAT;
  xarpusSplat: XarpusSplat;
}

export interface VerzikPhaseEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_PHASE;
  verzikPhase: VerzikPhase;
}

export interface VerzikAttackStyleEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_ATTACK_STYLE;
  verzikAttack: NpcAttackStyle;
}

export interface VerzikDawnEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_DAWN;
  verzikDawn: {
    attackTick: number;
    damage: number;
    player: string;
  };
}

export interface VerzikBounceEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_BOUNCE;
  verzikBounce: {
    npcAttackTick: number;
    playersInRange: number;
    playersNotInRange: number;
    bouncedPlayer?: string;
  };
}

export interface VerzikYellowsEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_YELLOWS;
  verzikYellows: Coords[];
}

export interface VerzikHealEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_HEAL;
  verzikHeal: {
    player: string;
    healAmount: number;
  };
}

export interface VerzikDawnDropEvent extends BaseEvent {
  type: EventType.TOB_VERZIK_DAWN_DROP;
  verzikDawnDrop: {
    dropped: boolean;
  };
}

export interface HandicapChoiceEvent extends BaseEvent {
  type: EventType.COLOSSEUM_HANDICAP_CHOICE;
  handicap: Handicap;
}

export interface ColosseumDoomAppliedEvent extends BaseEvent {
  type: EventType.COLOSSEUM_DOOM_APPLIED;
}

export interface ColosseumTotemHealEvent extends BaseEvent {
  type: EventType.COLOSSEUM_TOTEM_HEAL;
  colosseumTotemHeal: {
    source: BasicEventNpc;
    target: BasicEventNpc;
    startTick: number;
    healAmount: number;
  };
}

export interface ColosseumReentryPoolsEvent extends BaseEvent {
  type: EventType.COLOSSEUM_REENTRY_POOLS;
  colosseumReentryPools: {
    primarySpawned: Coords[];
    secondarySpawned: Coords[];
    primaryDespawned: Coords[];
    secondaryDespawned: Coords[];
  };
}

export interface ColosseumSolDustEvent extends BaseEvent {
  type: EventType.COLOSSEUM_SOL_DUST;
  colosseumSolDust: {
    pattern: SolDustPattern;
    direction?: SolDustDirection;
  };
}

export interface ColosseumSolGrappleEvent extends BaseEvent {
  type: EventType.COLOSSEUM_SOL_GRAPPLE;
  colosseumSolGrapple: {
    attackTick: number;
    target: EquipmentSlot;
    outcome: SolGrappleOutcome;
  };
}

export interface ColosseumSolPoolsEvent extends BaseEvent {
  type: EventType.COLOSSEUM_SOL_POOLS;
  colosseumSolPools: {
    pools: Coords[];
  };
}

export interface ColosseumSolLasersEvent extends BaseEvent {
  type: EventType.COLOSSEUM_SOL_LASERS;
  colosseumSolLasers: {
    phase: SolLaserPhase;
  };
}

export interface MokhaiotlAttackStyleEvent extends BaseEvent {
  type: EventType.MOKHAIOTL_ATTACK_STYLE;
  mokhaiotlAttackStyle: NpcAttackStyle;
}

export interface MokhaiotlOrbEvent extends BaseEvent {
  type: EventType.MOKHAIOTL_ORB;
  mokhaiotlOrb: MokhaiotlOrb;
}

export interface MokhaiotlObjectsEvent extends BaseEvent {
  type: EventType.MOKHAIOTL_OBJECTS;
  mokhaiotlObjects: {
    rocksSpawned: Coords[];
    rocksDespawned: Coords[];
    splatsSpawned: Coords[];
    splatsDespawned: Coords[];
  };
}

export interface MokhaiotlLarvaLeakEvent extends BaseEvent {
  type: EventType.MOKHAIOTL_LARVA_LEAK;
  mokhaiotlLarvaLeak: {
    roomId: number;
    healAmount: number;
  };
}

export interface MokhaiotlShockwaveEvent extends BaseEvent {
  type: EventType.MOKHAIOTL_SHOCKWAVE;
  mokhaiotlShockwave: {
    tiles: Coords[];
  };
}

export type MokhaiotlOrb = {
  source: MokhaiotlOrbSource;
  sourcePoint: Coords;
  style: AttackStyle;
  startTick: number;
  endTick: number;
};

export enum MokhaiotlOrbSource {
  UNKNOWN = 0,
  MOKHAIOTL = 1,
  BALL = 2,
}

export type NpcAttackStyle = {
  style: AttackStyle;
  npcAttackTick: number;
};

export type Event =
  | PlayerUpdateEvent
  | PlayerAttackEvent
  | PlayerDeathEvent
  | PlayerSpellEvent
  | NpcSpawnEvent
  | NpcUpdateEvent
  | NpcDeathEvent
  | NpcAttackEvent
  | MaidenBloodSplatsEvent
  | BloatDownEvent
  | BloatUpEvent
  | BloatHandsDropEvent
  | BloatHandsSplatEvent
  | NyloWaveSpawnEvent
  | NyloWaveStallEvent
  | NyloCleanupEndEvent
  | NyloBossSpawnEvent
  | SoteMazeEvent
  | SoteMazePathEvent
  | XarpusExhumedEvent
  | XarpusSplatEvent
  | XarpusPhaseEvent
  | VerzikPhaseEvent
  | VerzikAttackStyleEvent
  | VerzikBounceEvent
  | VerzikDawnEvent
  | VerzikYellowsEvent
  | VerzikHealEvent
  | VerzikDawnDropEvent
  | HandicapChoiceEvent
  | ColosseumDoomAppliedEvent
  | ColosseumTotemHealEvent
  | ColosseumReentryPoolsEvent
  | ColosseumSolDustEvent
  | ColosseumSolGrappleEvent
  | ColosseumSolPoolsEvent
  | ColosseumSolLasersEvent
  | MokhaiotlAttackStyleEvent
  | MokhaiotlOrbEvent
  | MokhaiotlObjectsEvent
  | MokhaiotlLarvaLeakEvent
  | MokhaiotlShockwaveEvent;

export enum DataSource {
  PRIMARY = EventProto.Player.DataSource.PRIMARY,
  SECONDARY = EventProto.Player.DataSource.SECONDARY,
}

export interface BasicPlayer {
  name: string;
}

export interface Player extends BasicPlayer {
  source: DataSource;
  offCooldownTick: number;
  hitpoints?: RawSkillLevel;
  prayer?: RawSkillLevel;
  attack?: RawSkillLevel;
  strength?: RawSkillLevel;
  defence?: RawSkillLevel;
  ranged?: RawSkillLevel;
  magic?: RawSkillLevel;
  prayerSet: RawPrayerSet;
  equipmentDeltas?: RawItemDelta[];
  /**
   * If true, this event carries the player's complete state. Delta-based fields
   * should be applied as if the previous state was null.
   */
  snapshot?: boolean;
}

export type Item = {
  id: number;
  name: string;
  quantity: number;
};

export interface BasicEventNpc {
  id: number;
  roomId: number;
}

export interface EventNpc extends BasicEventNpc {
  hitpoints: RawSkillLevel;
  prayers: RawPrayerSet;
  maidenCrab?: MaidenCrabProperties;
  nylo?: NyloProperties;
  verzikCrab?: VerzikCrabProperties;
}

export type Attack = {
  type: PlayerAttack;
  weapon?: Item;
  target?: BasicEventNpc;
  distanceToTarget: number;
};

export enum SpellTarget {
  NONE,
  PLAYER,
  NPC,
}

export type Spell = {
  type: PlayerSpell;
  target:
    | { type: SpellTarget.NONE }
    | { type: SpellTarget.PLAYER; player: string }
    | { type: SpellTarget.NPC; npc: BasicEventNpc };
};

export type NpcAttackAction = {
  /** Style of the attack. */
  attack: NpcAttack;
  /** Username of the player the attack targets. Undefined if no target. */
  target?: string;
};

export type Coords = {
  x: number;
  y: number;
};

export type BloatDown = {
  downNumber: number;
  upTicks: number;
  walkTime: number;
};

export type NyloWave = {
  wave: number;
  nylosAlive: number;
  roomCap: number;
};

export type SoteMaze = {
  maze: Maze;
};

export type SoteMazePath = SoteMaze & { activeTiles: Coords[] };

export type XarpusExhumed = {
  spawnTick: number;
  healAmount: number;
  healTicks: number[];
};

export enum XarpusSplatSource {
  UNKNOWN = 0,
  XARPUS = 1,
  BOUNCE = 2,
}

export type XarpusSplat = {
  source: XarpusSplatSource;
  bounceFrom: Coords | null;
};

export enum AttackStyle {
  MELEE = EventProto.AttackStyle.Style.MELEE,
  RANGE = EventProto.AttackStyle.Style.RANGE,
  MAGE = EventProto.AttackStyle.Style.MAGE,
}

export enum SolDustPattern {
  TRIDENT_1 = 0,
  TRIDENT_2 = 1,
  SHIELD_1 = 2,
  SHIELD_2 = 3,
}

export enum SolDustDirection {
  NORTH = 0,
  EAST = 1,
  SOUTH = 2,
  WEST = 3,
}

export enum SolGrappleOutcome {
  HIT = 0,
  DEFEND = 1,
  PARRY = 2,
}

export enum SolLaserPhase {
  SCAN = 0,
  SHOT = 1,
}
