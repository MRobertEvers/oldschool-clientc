import { z } from 'zod';

/**
 * Schema for integer enum values.
 * Protobuf enums serialize as their numeric values in JSON.
 */
const enumValueSchema = z.number().int().nonnegative();

export const coordsSchema = z.object({
  x: z.number().int(),
  y: z.number().int(),
});

export const timestampSchema = z.object({
  seconds: z.number().int(),
  nanos: z.number().int().nonnegative(),
});

// =============================================================================
// server_message.proto schema
// =============================================================================

// ServerMessage.User
export const userSchema = z.object({
  id: z.number().int().nonnegative(),
  name: z.string(),
});

// ServerMessage.Error
export const errorSchema = z.object({
  type: enumValueSchema,
  username: z.string(),
  message: z.string().optional(),
});

// ServerMessage.GameState.PlayerInfo
export const playerInfoSchema = z.object({
  username: z.string(),
  overallExperience: z.number().int().nonnegative(),
  attackExperience: z.number().int().nonnegative(),
  strengthExperience: z.number().int().nonnegative(),
  defenceExperience: z.number().int().nonnegative(),
  hitpointsExperience: z.number().int().nonnegative(),
  rangedExperience: z.number().int().nonnegative(),
  prayerExperience: z.number().int().nonnegative(),
  magicExperience: z.number().int().nonnegative(),
  accountHash: z.string(),
});

// ServerMessage.GameState
export const gameStateSchema = z.object({
  state: enumValueSchema,
  playerInfo: playerInfoSchema.optional(),
});

// ServerMessage.PastChallenge
export const pastChallengeSchema = z.object({
  id: z.string(),
  status: enumValueSchema,
  stage: enumValueSchema,
  mode: enumValueSchema,
  party: z.array(z.string()),
  challenge: enumValueSchema,
  timestamp: timestampSchema.optional(),
  challengeTicks: z.number().int().nonnegative(),
});

// ServerMessage.ServerStatus
export const serverStatusSchema = z.object({
  status: enumValueSchema,
  shutdownTime: timestampSchema.optional(),
});

// ServerMessage.PlayerState
export const playerStateSchema = z.object({
  username: z.string(),
  challengeId: z.string(),
  challenge: enumValueSchema,
  mode: enumValueSchema,
});

// ServerMessage.ChallengeStateConfirmation
export const challengeStateConfirmationSchema = z.object({
  isValid: z.boolean(),
  username: z.string().optional(),
  challenge: enumValueSchema.optional(),
  mode: enumValueSchema.optional(),
  stage: enumValueSchema.optional(),
  party: z.array(z.string()).optional(),
  spectator: z.boolean().optional(),
});

// ChallengeStartRequest
export const challengeStartRequestSchema = z.object({
  challenge: enumValueSchema,
  mode: enumValueSchema.optional(),
  stage: enumValueSchema.optional(),
  party: z.array(z.string()),
  spectator: z.boolean(),
});

// ChallengeEndRequest
export const challengeEndRequestSchema = z.object({
  overallTimeTicks: z.number().int(),
  challengeTimeTicks: z.number().int(),
  soft: z.boolean().optional(),
});

// ChallengeUpdate.StageUpdate
export const stageUpdateSchema = z.object({
  stage: enumValueSchema,
  status: enumValueSchema,
  accurate: z.boolean(),
  recordedTicks: z.number().int().nonnegative(),
  gameServerTicks: z.number().int().optional(),
  gameTicksPrecise: z.boolean(),
});

// ChallengeUpdate
export const challengeUpdateSchema = z.object({
  mode: enumValueSchema.optional(),
  party: z.array(z.string()).optional(),
  stageUpdate: stageUpdateSchema.optional(),
});

// AttackDefinition.Projectile
export const projectileSchema = z.object({
  id: z.number().int().nonnegative(),
  startCycleOffset: z.number().int().nonnegative(),
  weaponId: z.number().int().optional(),
});

export const weaponProjectileSchema = projectileSchema.extend({
  weaponId: z.number().int(),
});

const attackCategorySchema = z.enum(['MELEE', 'RANGED', 'MAGIC']);

// AttackDefinition
export const attackDefinitionSchema = z.object({
  protoId: enumValueSchema,
  name: z.string(),
  weaponIds: z.array(z.number().int()),
  animationIds: z.array(z.number().int()),
  cooldown: z.number().int().nonnegative(),
  projectile: projectileSchema.optional(),
  weaponProjectiles: z.array(weaponProjectileSchema).optional(),
  continuousAnimation: z.boolean().optional(),
  animationFrameMin: z.number().int().nonnegative().optional(),
  animationFrameMax: z.number().int().nonnegative().optional(),
  attackerGraphicIds: z.array(z.number().int()).optional(),
  category: attackCategorySchema,
});

// SpellDefinition.Graphic
export const graphicSchema = z.object({
  id: z.number().int().nonnegative(),
  durationTicks: z.number().int().nonnegative(),
  maxFrame: z.number().int().nonnegative(),
});

// SpellDefinition
export const spellDefinitionSchema = z.object({
  id: enumValueSchema,
  name: z.string(),
  animationIds: z.array(z.number().int()),
  graphics: z.array(graphicSchema).optional(),
  targetGraphics: z.array(graphicSchema).optional(),
  stallTicks: z.number().int().nonnegative(),
});

// =============================================================================
// event.proto schemas
// =============================================================================

// Event.Player.EquippedItem
export const equippedItemSchema = z.object({
  slot: enumValueSchema,
  id: z.number().int(),
  quantity: z.number().int(),
});

// Event.Player
export const playerSchema = z.object({
  name: z.string(),
  offCooldownTick: z.number().int().nonnegative().optional(),
  hitpoints: z.number().int().optional(),
  prayer: z.number().int().optional(),
  attack: z.number().int().optional(),
  strength: z.number().int().optional(),
  defence: z.number().int().optional(),
  ranged: z.number().int().optional(),
  magic: z.number().int().optional(),
  equipmentDeltas: z.array(z.number().int()).optional(),
  activePrayers: z.number().int().optional(),
  dataSource: enumValueSchema.optional(),
  partyIndex: z.number().int().optional(),
  snapshot: z.boolean().optional(),
});

// Event.Npc.MaidenCrab
export const maidenCrabSchema = z.object({
  spawn: enumValueSchema,
  position: enumValueSchema,
  scuffed: z.boolean(),
});

// Event.Npc.Nylo
export const nyloSchema = z.object({
  wave: z.number().int(),
  parentRoomId: z.number().int(),
  big: z.boolean(),
  style: enumValueSchema,
  spawnType: enumValueSchema,
});

// Event.Npc.VerzikCrab
export const verzikCrabSchema = z.object({
  phase: enumValueSchema,
  spawn: enumValueSchema,
});

// Event.Npc
export const npcSchema = z.object({
  id: z.number().int(),
  roomId: z.number().int(),
  hitpoints: z.number().int().optional(),
  activePrayers: z.number().int().optional(),
  basic: z.object({}).optional(),
  maidenCrab: maidenCrabSchema.optional(),
  nylo: nyloSchema.optional(),
  verzikCrab: verzikCrabSchema.optional(),
});

// Event.Attack
export const attackSchema = z.object({
  type: enumValueSchema,
  weapon: equippedItemSchema.optional(),
  target: npcSchema.optional(),
  distanceToTarget: z.number().int(),
});

// Event.Spell
export const spellSchema = z.object({
  type: enumValueSchema,
  noTarget: z.object({}).optional(),
  targetPlayer: z.string().optional(),
  targetNpc: npcSchema.optional(),
});

// Event.NpcAttacked
export const npcAttackSchema = z.object({
  attack: enumValueSchema,
  target: z.string().optional(),
});

// Event.AttackStyle
export const attackStyleSchema = z.object({
  style: enumValueSchema,
  npcAttackTick: z.number().int(),
});

// Event.BloatDown
export const bloatDownSchema = z
  .object({
    downNumber: z.number().int(),
  })
  .and(
    z.union([
      z.object({ upTicks: z.number().int() }),
      z.object({ walkTime: z.number().int() }),
    ]),
  );

// Event.NyloWave
export const nyloWaveSchema = z.object({
  wave: z.number().int(),
  nylosAlive: z.number().int(),
  roomCap: z.number().int(),
});

// Event.SoteMaze
export const soteMazeSchema = z.object({
  maze: enumValueSchema,
  overworldTiles: z.array(coordsSchema).optional(),
  overworldPivots: z.array(coordsSchema).optional(),
  underworldPivots: z.array(coordsSchema).optional(),
  chosenPlayer: z.string().optional(),
});

// Event.XarpusExhumed
export const xarpusExhumedSchema = z.object({
  spawnTick: z.number().int(),
  healAmount: z.number().int(),
  healTicks: z.array(z.number().int()),
});

// Event.XarpusSplat
export const xarpusSplatSchema = z.object({
  source: enumValueSchema,
  bounceFrom: coordsSchema.optional(),
});

// Event.VerzikBounce
export const verzikBounceSchema = z.object({
  npcAttackTick: z.number().int(),
  playersInRange: z.number().int(),
  playersNotInRange: z.number().int(),
  bouncedPlayer: z.string().optional(),
});

// Event.VerzikHeal
export const verzikHealSchema = z.object({
  player: z.string(),
  healAmount: z.number().int(),
});

// Event.VerzikDawn
export const verzikDawnSchema = z.object({
  attackTick: z.number().int(),
  damage: z.number().int(),
  player: z.string(),
});

// Event.VerzikDawnDrop
export const verzikDawnDropSchema = z.object({
  dropped: z.boolean(),
});

// Event.ColosseumTotemHeal
export const colosseumTotemHealSchema = z.object({
  source: npcSchema,
  target: npcSchema,
  startTick: z.number().int(),
  healAmount: z.number().int(),
});

// Event.ColosseumReentryPools
export const colosseumReentryPoolsSchema = z.object({
  primarySpawned: z.array(coordsSchema),
  secondarySpawned: z.array(coordsSchema),
  primaryDespawned: z.array(coordsSchema),
  secondaryDespawned: z.array(coordsSchema),
});

// Event.ColosseumSolDust
export const colosseumSolDustSchema = z.object({
  pattern: enumValueSchema,
  direction: enumValueSchema.optional(),
});

// Event.ColosseumSolGrapple
export const colosseumSolGrappleSchema = z.object({
  attackTick: z.number().int(),
  target: enumValueSchema,
  outcome: enumValueSchema,
});

// Event.ColosseumSolPools
export const colosseumSolPoolsSchema = z.object({
  pools: z.array(coordsSchema),
});

// Event.ColosseumSolLasers
export const colosseumSolLasersSchema = z.object({
  phase: enumValueSchema,
});

// Event.MokhaiotlOrb
export const mokhaiotlOrbSchema = z.object({
  source: enumValueSchema,
  sourcePoint: coordsSchema.optional(),
  style: enumValueSchema,
  startTick: z.number().int(),
  endTick: z.number().int(),
});

// Event.MokhaiotlObjects
export const mokhaiotlObjectsSchema = z.object({
  rocksSpawned: z.array(coordsSchema).optional(),
  rocksDespawned: z.array(coordsSchema).optional(),
  splatsSpawned: z.array(coordsSchema).optional(),
  splatsDespawned: z.array(coordsSchema).optional(),
});

// Event.MokhaiotlLarvaLeak
export const mokhaiotlLarvaLeakSchema = z.object({
  roomId: z.number(),
  healAmount: z.number().int(),
});

// Event.MokhaiotlShockwave
export const mokhaiotlShockwaveSchema = z.object({
  tiles: z.array(coordsSchema),
});

// Event.InfernoWaveStart
export const infernoWaveStartSchema = z.object({
  wave: z.number().int(),
  overallTicks: z.number().int(),
});

// Event
export const eventSchema = z.object({
  type: enumValueSchema,
  challengeId: z.string().optional(),
  stage: enumValueSchema,
  tick: z.number().int(),
  xCoord: z.number().int(),
  yCoord: z.number().int(),

  // Event-specific fields
  player: playerSchema.optional(),
  playerAttack: attackSchema.optional(),
  npc: npcSchema.optional(),
  npcAttack: npcAttackSchema.optional(),
  playerSpell: spellSchema.optional(),

  // ToB events
  maidenBloodSplats: z.array(coordsSchema).optional(),
  bloatDown: bloatDownSchema.optional(),
  bloatHands: z.array(coordsSchema).optional(),
  nyloWave: nyloWaveSchema.optional(),
  soteMaze: soteMazeSchema.optional(),
  xarpusPhase: enumValueSchema.optional(),
  xarpusExhumed: xarpusExhumedSchema.optional(),
  xarpusSplat: xarpusSplatSchema.optional(),
  verzikPhase: enumValueSchema.optional(),
  verzikAttackStyle: attackStyleSchema.optional(),
  verzikYellows: z.array(coordsSchema).optional(),
  verzikBounce: verzikBounceSchema.optional(),
  verzikHeal: verzikHealSchema.optional(),
  verzikDawn: verzikDawnSchema.optional(),
  verzikDawnDrop: verzikDawnDropSchema.optional(),

  // Colosseum events
  handicap: enumValueSchema.optional(),
  handicapOptions: z.array(enumValueSchema).optional(),
  colosseumTotemHeal: colosseumTotemHealSchema.optional(),
  colosseumReentryPools: colosseumReentryPoolsSchema.optional(),
  colosseumSolDust: colosseumSolDustSchema.optional(),
  colosseumSolGrapple: colosseumSolGrappleSchema.optional(),
  colosseumSolPools: colosseumSolPoolsSchema.optional(),
  colosseumSolLasers: colosseumSolLasersSchema.optional(),

  // Mokhaiotl events
  mokhaiotlAttackStyle: attackStyleSchema.optional(),
  mokhaiotlOrb: mokhaiotlOrbSchema.optional(),
  mokhaiotlObjects: mokhaiotlObjectsSchema.optional(),
  mokhaiotlLarvaLeak: mokhaiotlLarvaLeakSchema.optional(),
  mokhaiotlShockwave: mokhaiotlShockwaveSchema.optional(),

  // Inferno events
  infernoWaveStart: infernoWaveStartSchema.optional(),
});

export const serverMessageSchema = z.object({
  type: enumValueSchema,
  user: userSchema.optional(),
  error: errorSchema.optional(),
  activeChallengeId: z.string().optional(),
  recentRecordings: z.array(pastChallengeSchema).optional(),
  challengeEvents: z.array(eventSchema).optional(),
  serverStatus: serverStatusSchema.optional(),
  gameState: gameStateSchema.optional(),
  playerState: z.array(playerStateSchema).optional(),
  challengeStateConfirmation: challengeStateConfirmationSchema.optional(),
  challengeStartRequest: challengeStartRequestSchema.optional(),
  challengeEndRequest: challengeEndRequestSchema.optional(),
  challengeUpdate: challengeUpdateSchema.optional(),
  attackDefinitions: z.array(attackDefinitionSchema).optional(),
  spellDefinitions: z.array(spellDefinitionSchema).optional(),
  requestId: z.number().int().nonnegative().optional(),
});

export type ServerMessageJson = z.infer<typeof serverMessageSchema>;
export type EventJson = z.infer<typeof eventSchema>;
export type ChallengeStartRequestJson = z.infer<
  typeof challengeStartRequestSchema
>;
export type ChallengeEndRequestJson = z.infer<typeof challengeEndRequestSchema>;
export type ChallengeUpdateJson = z.infer<typeof challengeUpdateSchema>;
export type GameStateJson = z.infer<typeof gameStateSchema>;
export type ChallengeStateConfirmationJson = z.infer<
  typeof challengeStateConfirmationSchema
>;
