import {
  camelToSnakeObject,
  ChallengeMode,
  ChallengeStatus,
  ChallengeType,
  DataRepository,
  InfernoChallengeStats,
  InfernoData,
  Npc,
  NpcAttack,
  PriceTracker,
  SplitType,
  Stage,
  StageStatus,
} from '@blert/common';

import { Event } from '@blert/common/generated/event_pb';

import ChallengeProcessor, {
  ChallengeSplitWithId,
  InitializedFields,
} from './challenge-processor';
import sql from '../db';
import { MergedEvents } from '../merging';

function waveToSplit(wave: number): SplitType | null {
  switch (wave) {
    case 9:
      return SplitType.INFERNO_WAVE_9_START;
    case 18:
      return SplitType.INFERNO_WAVE_18_START;
    case 25:
      return SplitType.INFERNO_WAVE_25_START;
    case 35:
      return SplitType.INFERNO_WAVE_35_START;
    case 42:
      return SplitType.INFERNO_WAVE_42_START;
    case 50:
      return SplitType.INFERNO_WAVE_50_START;
    case 57:
      return SplitType.INFERNO_WAVE_57_START;
    case 60:
      return SplitType.INFERNO_WAVE_60_START;
    case 63:
      return SplitType.INFERNO_WAVE_63_START;
    case 66:
      return SplitType.INFERNO_WAVE_66_START;
    case 67:
      return SplitType.INFERNO_WAVE_67_START;
    case 68:
      return SplitType.INFERNO_WAVE_68_START;
    case 69:
      return SplitType.INFERNO_WAVE_69_START;
    default:
      return null;
  }
}

function stageToWave(stage: Stage): number {
  return stage - Stage.INFERNO_WAVE_1 + 1;
}

enum Pillar {
  UNKNOWN,
  WEST,
  EAST,
  SOUTH,
}

function pillarFromCoords(x: number, y: number): Pillar {
  if (x === 2257 && y === 5349) {
    return Pillar.WEST;
  }
  if (x === 2274 && y === 5351) {
    return Pillar.EAST;
  }
  if (x === 2267 && y === 5335) {
    return Pillar.SOUTH;
  }
  return Pillar.UNKNOWN;
}

type InfernoState = {
  data: InfernoData;
  meleerDigs: number;
  magerRevives: number;
};

export default class InfernoProcessor extends ChallengeProcessor {
  private infernoData: InfernoData;
  private waveStartTick: number | null;
  private meleerDigs: number;
  private magerRevives: number;

  constructor(
    dataRepository: DataRepository,
    priceTracker: PriceTracker,
    uuid: string,
    stage: Stage,
    stageStatus: StageStatus,
    party: string[],
    extraFields: InitializedFields = {},
  ) {
    super(
      dataRepository,
      priceTracker,
      ChallengeType.INFERNO,
      Stage.INFERNO_WAVE_1,
      Stage.INFERNO_WAVE_69,
      uuid,
      ChallengeMode.NO_MODE,
      stage,
      stageStatus,
      party,
      extraFields,
    );

    if (extraFields.customData) {
      const customData = extraFields.customData as InfernoState;
      this.infernoData = customData.data;
      this.meleerDigs = customData.meleerDigs;
      this.magerRevives = customData.magerRevives;
    } else {
      this.infernoData = {
        waves: [],
      };
      this.meleerDigs = 0;
      this.magerRevives = 0;
    }

    this.waveStartTick = null;
  }

  protected override async onCreate(): Promise<void> {
    await Promise.all([
      sql`
        INSERT INTO inferno_challenge_stats (challenge_id)
        VALUES (${this.getDatabaseId()})
      `,
      this.getDataRepository().saveInfernoChallengeData(
        this.getUuid(),
        this.infernoData,
      ),
    ]);
  }

  protected override async onFinish(
    finalChallengeTicks: number,
  ): Promise<void> {
    this.setChallengeSplit(SplitType.INFERNO_CHALLENGE, finalChallengeTicks);
    this.setChallengeSplit(SplitType.INFERNO_OVERALL, finalChallengeTicks);

    for (const username of this.getParty()) {
      const stats = this.getCurrentStageStats(username);
      switch (this.getChallengeStatus()) {
        case ChallengeStatus.COMPLETED:
          stats.infernoCompletions += 1;
          break;
        case ChallengeStatus.RESET:
          stats.infernoResets += 1;
          break;
        case ChallengeStatus.WIPED:
          stats.infernoWipes += 1;
          break;
      }
    }

    const timesAccurate =
      this.hasFullyRecordedUpTo(Stage.INFERNO_WAVE_69) &&
      this.getChallengeStatus() === ChallengeStatus.COMPLETED;

    if (
      timesAccurate &&
      finalChallengeTicks === this.getTotalChallengeTicks()
    ) {
      await this.setSplitsAccurate();
    }
  }

  protected override async onStageFinished(
    stage: Stage,
    events: MergedEvents,
  ): Promise<void> {
    const wave = stageToWave(stage);

    if (this.waveStartTick !== null) {
      // Override total challenge ticks based on reported start time.
      this.setTotalChallengeTicks(this.waveStartTick + events.getLastTick());

      const split = waveToSplit(wave);
      if (split !== null) {
        this.setChallengeSplit(split, this.waveStartTick);
      }
    } else {
      // The base processor has already included this wave's tick count.
      // 6 ticks is the interval between waves.
      this.setTotalChallengeTicks(this.getTotalChallengeTicks() + 6);
    }

    this.setStageSplit(
      SplitType.INFERNO_WAVE_1_TIME + (wave - 1),
      events.getLastTick(),
      0,
      true,
    );

    const state = this.getStageState();
    this.infernoData.waves.push({
      stage,
      ticksLost: events.getMissingTickCount(),
      npcs: Object.fromEntries(state?.npcs ?? []),
      ticks: events.getLastTick(),
      startTick:
        this.waveStartTick ??
        this.getTotalChallengeTicks() - events.getLastTick(),
    });

    await Promise.all([
      this.updateChallengeStats({
        meleerDigs: this.meleerDigs,
        magerRevives: this.magerRevives,
      }),
      this.getDataRepository().saveInfernoChallengeData(
        this.getUuid(),
        this.infernoData,
      ),
    ]);
  }

  protected override async processChallengeEvent(
    _: MergedEvents,
    event: Event,
  ): Promise<boolean> {
    switch (event.getType()) {
      case Event.Type.NPC_DEATH: {
        const npc = event.getNpc()!;
        if (Npc.isRockySupport(npc.getId())) {
          const pillar = pillarFromCoords(event.getXCoord(), event.getYCoord());
          const wave = stageToWave(this.getStage());
          switch (pillar) {
            case Pillar.WEST:
              await this.updateChallengeStats({
                westPillarCollapseWave: wave,
              });
              break;
            case Pillar.EAST:
              await this.updateChallengeStats({
                eastPillarCollapseWave: wave,
              });
              break;
            case Pillar.SOUTH:
              await this.updateChallengeStats({
                southPillarCollapseWave: wave,
              });
              break;
          }
        }
        break;
      }

      case Event.Type.NPC_ATTACK: {
        const attack = event.getNpcAttack()!;
        switch (attack.getAttack()) {
          case NpcAttack.INFERNO_MELEER_DIG:
            this.meleerDigs += 1;
            break;
          case NpcAttack.INFERNO_MAGER_RESURRECT:
            this.magerRevives += 1;
            break;
        }
        break;
      }

      case Event.Type.INFERNO_WAVE_START: {
        const waveStart = event.getInfernoWaveStart()!;
        this.waveStartTick = waveStart.getOverallTicks();
        return false;
      }
    }
    return true;
  }

  protected override getCustomData(): InfernoState {
    return {
      data: this.infernoData,
      meleerDigs: this.meleerDigs,
      magerRevives: this.magerRevives,
    };
  }

  protected override hasFullyRecordedUpTo(stage: Stage): boolean {
    if (stage < Stage.INFERNO_WAVE_1 || stage > Stage.INFERNO_WAVE_69) {
      return false;
    }

    const recordedStages = new Set(
      this.infernoData.waves.map((wave) => wave.stage),
    );

    for (let s = Stage.INFERNO_WAVE_1; s <= stage; s++) {
      if (!recordedStages.has(s)) {
        return false;
      }
    }

    return true;
  }

  protected override isRetriable(_: Stage): boolean {
    return false;
  }

  private async setSplitsAccurate(): Promise<void> {
    const splits = await sql<ChallengeSplitWithId[]>`
      UPDATE challenge_splits
      SET accurate = true
      WHERE challenge_id = ${this.getDatabaseId()}
      RETURNING id, challenge_id, type, scale, ticks, accurate
    `;

    await this.updatePersonalBests(splits);
  }

  private async updateChallengeStats(
    stats: Partial<InfernoChallengeStats>,
  ): Promise<void> {
    await sql`
      UPDATE inferno_challenge_stats
      SET ${sql(camelToSnakeObject(stats))}
      WHERE challenge_id = ${this.getDatabaseId()};
    `;
  }
}
