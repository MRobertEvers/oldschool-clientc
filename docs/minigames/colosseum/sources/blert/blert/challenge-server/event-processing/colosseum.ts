import {
  ChallengeMode,
  ChallengeStatus,
  ChallengeType,
  ColosseumData,
  DataRepository,
  HANDICAP_LEVEL_VALUE_INCREMENT,
  Handicap,
  handicapBase,
  handicapLevel,
  PriceTracker,
  SplitType,
  Stage,
  StageStatus,
} from '@blert/common';
import { Event } from '@blert/common/generated/event_pb';

import ChallengeProcessor, { InitializedFields } from './challenge-processor';
import sql from '../db';
import { MergedEvents } from '../merging';

function waveIndex(stage: Stage): number {
  return stage - Stage.COLOSSEUM_WAVE_1;
}

type CustomData = {
  colosseumData: ColosseumData;
  handicapLevels: number[];
};

export default class ColosseumProcessor extends ChallengeProcessor {
  private colosseumData: ColosseumData;
  private handicapLevels: number[];

  private selectedHandicap: Handicap | null;
  private waveHandicapOptions: Handicap[];

  public constructor(
    dataRepository: DataRepository,
    priceTracker: PriceTracker,
    uuid: string,
    mode: ChallengeMode,
    stage: Stage,
    stageStatus: StageStatus,
    party: string[],
    extraFields: InitializedFields = {},
  ) {
    super(
      dataRepository,
      priceTracker,
      ChallengeType.COLOSSEUM,
      Stage.COLOSSEUM_WAVE_1,
      Stage.COLOSSEUM_WAVE_12,
      uuid,
      mode,
      stage,
      stageStatus,
      party,
      extraFields,
    );

    this.selectedHandicap = null;
    this.waveHandicapOptions = [];

    if (extraFields.customData) {
      const customData = extraFields.customData as CustomData;
      this.colosseumData = customData.colosseumData;
      this.handicapLevels = customData.handicapLevels;
    } else {
      this.colosseumData = {
        waves: [],
        handicaps: [],
      };
      this.handicapLevels = Array<number>(14).fill(0);
    }
  }

  protected override async onCreate(): Promise<void> {
    await Promise.all([
      sql`
        INSERT INTO colosseum_challenge_stats (challenge_id)
        VALUES (${this.getDatabaseId()})
      `,
      this.getDataRepository().saveColosseumChallengeData(
        this.getUuid(),
        this.colosseumData,
      ),
    ]);
  }

  protected override onFinish(finalChallengeTicks: number): Promise<void> {
    this.setChallengeSplit(SplitType.COLOSSEUM_CHALLENGE, finalChallengeTicks);

    for (const username of this.getParty()) {
      const stats = this.getCurrentStageStats(username);
      switch (this.getChallengeStatus()) {
        case ChallengeStatus.COMPLETED:
          stats.colosseumCompletions += 1;
          break;
        case ChallengeStatus.RESET:
          stats.colosseumResets += 1;
          break;
        case ChallengeStatus.WIPED:
          stats.colosseumWipes += 1;
          break;
      }
    }

    return Promise.resolve();
  }

  protected override async onStageFinished(
    stage: Stage,
    events: MergedEvents,
  ): Promise<void> {
    const state = this.getStageState();

    this.colosseumData.waves.push({
      stage,
      ticksLost: events.getMissingTickCount(),
      handicap: this.selectedHandicap ?? 0,
      options: this.waveHandicapOptions,
      npcs: Object.fromEntries(state?.npcs ?? []),
    });

    this.setStageSplit(
      SplitType.COLOSSEUM_WAVE_1 + waveIndex(stage),
      events.getLastTick(),
      0,
      true,
    );

    if (
      events.getStatus() === StageStatus.COMPLETED &&
      stage > Stage.COLOSSEUM_WAVE_1 &&
      stage < Stage.COLOSSEUM_WAVE_12 &&
      this.hasFullyRecordedUpTo(stage)
    ) {
      this.setChallengeSplit(
        SplitType.COLOSSEUM_WAVE_3_START + (waveIndex(stage) - 1),
        this.getTotalChallengeTicks(),
        this.isPartyUnchanged() && events.hasPreciseServerTickCount(),
      );
    }

    // Store all handicaps selected, once per level.
    const handicaps = this.colosseumData.handicaps.flatMap((handicap) =>
      Array<number>(handicapLevel(handicap)).fill(handicapBase(handicap)),
    );

    await Promise.all([
      sql`
        UPDATE colosseum_challenge_stats
        SET handicaps = ${handicaps}
        WHERE challenge_id = ${this.getDatabaseId()}
      `,
      this.getDataRepository().saveColosseumChallengeData(
        this.getUuid(),
        this.colosseumData,
      ),
    ]);
  }

  protected override processChallengeEvent(
    _allEvents: MergedEvents,
    event: Event,
  ): Promise<boolean> {
    if (event.getType() === Event.Type.COLOSSEUM_HANDICAP_CHOICE) {
      const handicap = event.getHandicap();

      this.selectedHandicap = this.levelHandicap(handicap);
      this.waveHandicapOptions = event
        .getHandicapOptionsList()
        .map((handicap) => this.levelHandicap(handicap));

      this.handicapLevels[handicap]++;

      const index = this.colosseumData.handicaps.indexOf(
        this.selectedHandicap - HANDICAP_LEVEL_VALUE_INCREMENT,
      );
      if (index !== -1) {
        this.colosseumData.handicaps[index] += HANDICAP_LEVEL_VALUE_INCREMENT;
      } else {
        this.colosseumData.handicaps.push(handicap);
      }
    }
    return Promise.resolve(true);
  }

  protected override getCustomData(): CustomData | null {
    return {
      colosseumData: this.colosseumData,
      handicapLevels: this.handicapLevels,
    };
  }

  protected override hasFullyRecordedUpTo(stage: Stage): boolean {
    if (stage < Stage.COLOSSEUM_WAVE_1 || stage > Stage.COLOSSEUM_WAVE_12) {
      return false;
    }

    const recordedStages = new Set(
      this.colosseumData.waves.map((wave) => wave.stage),
    );

    for (let s = Stage.COLOSSEUM_WAVE_1; s <= stage; s++) {
      if (!recordedStages.has(s)) {
        return false;
      }
    }

    return true;
  }

  protected override isRetriable(_: Stage): boolean {
    return false;
  }

  /**
   * Adjusts a handicap's ID based on the level of the handicap.
   * @param handicap The base handicap ID.
   * @returns The leveled handicap ID.
   */
  private levelHandicap(handicap: number): Handicap {
    const increment =
      this.handicapLevels[handicap] * HANDICAP_LEVEL_VALUE_INCREMENT;
    return handicap + increment;
  }
}
