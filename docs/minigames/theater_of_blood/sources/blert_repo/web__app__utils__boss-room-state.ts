import {
  Challenge,
  ChallengeType,
  ColosseumChallenge,
  Coords,
  Event,
  EventType,
  InfernoChallenge,
  MokhaiotlChallenge,
  RoomNpcMap as RawRoomNpcMap,
  Skill,
  SkillLevel,
  SplitType,
  Stage,
  TobRaid,
  TobRooms,
} from '@blert/common';
import { useSearchParams } from 'next/navigation';
import {
  SetStateAction,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';

import { ChallengeContext, useLiveChallenge } from '@/challenge-context';
import { AnyEntity, NpcEntity, PlayerEntity } from '@/components/map-renderer';
import { useLiveStageState } from '@/utils/live-stage-state';
import { clamp } from '@/utils/math';

import { challengeApiUrl, npcImageUrl } from './url';

export * from './stage-state';

import {
  buildEventMaps,
  computeNpcState,
  computePlayerState,
  EventState,
  PlayerStateMap,
  RoomNpcMap,
  StageState,
  toBcf,
} from './stage-state';

/**
 * Returns a stable reference to the events of a given type from the event map.
 * The reference only changes when the filtered contents change.
 *
 * @param eventsByType The event map from which to extract events.
 * @param type The event type to extract.
 * @param filter Optional filter function to restrict which events are included.
 * @returns A stable reference to the filtered events of the given type.
 */
export function useStableEvents<T extends Event>(
  eventsByType: Partial<Record<EventType, Event[]>>,
  type: EventType,
  filter?: (event: T) => boolean,
): T[] {
  const ref = useRef<T[]>([]);
  const rawCountRef = useRef(0);
  const rawCount = eventsByType[type]?.length ?? 0;

  if (rawCountRef.current !== rawCount) {
    rawCountRef.current = rawCount;
    let result: T[];
    if (filter !== undefined) {
      result = ((eventsByType[type] ?? []) as T[]).filter(filter);
    } else {
      result = ((eventsByType[type] ?? []) as T[]).slice();
    }
    if (ref.current.length !== result.length) {
      ref.current = result;
    }
  }

  return ref.current;
}

export const usePlayingState = (
  firstTick: number,
  totalTicks: number,
  isStreaming = false,
) => {
  const searchParams = useSearchParams();
  const initialTick = Number.parseInt(
    searchParams.get('tick') ?? String(firstTick),
    10,
  );
  const normalizedInitialTick = Number.isNaN(initialTick)
    ? firstTick
    : initialTick;
  const maxTick = Math.max(firstTick, totalTicks - 1);

  const [currentTick, setCurrentTick] = useState(() =>
    isStreaming ? maxTick : clamp(normalizedInitialTick, firstTick, maxTick),
  );
  const [playing, setPlaying] = useState(false);
  const [following, setFollowing] = useState(isStreaming);
  const effectiveTickRef = useRef(currentTick);

  // Keep a synced tick reference for functional updates while following.
  const displayTick = following ? maxTick : currentTick;
  effectiveTickRef.current = displayTick;

  const setTick = useCallback(
    (tickOrUpdater: SetStateAction<number>) => {
      setFollowing(false);
      const baseTick = effectiveTickRef.current;
      const nextTick =
        typeof tickOrUpdater === 'number'
          ? tickOrUpdater
          : tickOrUpdater(baseTick);
      const clampedTick = clamp(
        Number.isNaN(nextTick) ? firstTick : nextTick,
        firstTick,
        maxTick,
      );
      effectiveTickRef.current = clampedTick;
      setCurrentTick(clampedTick);
    },
    [firstTick, maxTick],
  );

  const advanceTick = useCallback(() => {
    const tick = effectiveTickRef.current;

    if (tick < maxTick) {
      const nextTick = tick + 1;
      effectiveTickRef.current = nextTick;
      setCurrentTick(nextTick);
      return;
    }

    if (isStreaming) {
      setFollowing(true);
      return;
    }

    setPlaying(false);
    effectiveTickRef.current = firstTick;
    setCurrentTick(firstTick);
  }, [firstTick, maxTick, isStreaming]);

  const jumpToLive = useCallback(() => {
    effectiveTickRef.current = maxTick;
    setFollowing(true);
    setPlaying(false);
  }, [maxTick]);

  // Clamp if totalTicks shrinks (e.g. stage change).
  useEffect(() => {
    if (!following) {
      setCurrentTick((tick) => {
        const clampedTick = clamp(tick, firstTick, maxTick);
        effectiveTickRef.current = clampedTick;
        return clampedTick;
      });
    }
  }, [following, firstTick, maxTick]);

  // Sync following with streaming state. When streaming stops, set
  // `currentTick` to the last tick of the stage so the scrubber stays in place.
  const liveTickRef = useRef({ tick: maxTick, wasStreaming: isStreaming });
  liveTickRef.current.tick = maxTick;

  useEffect(() => {
    if (liveTickRef.current.wasStreaming && !isStreaming) {
      effectiveTickRef.current = liveTickRef.current.tick;
      setCurrentTick(liveTickRef.current.tick);
    }
    liveTickRef.current.wasStreaming = isStreaming;
    setFollowing(isStreaming);
  }, [isStreaming]);

  useEffect(() => {
    const listener = (e: KeyboardEvent) => {
      if (document.activeElement?.tagName === 'INPUT') {
        return;
      }

      if (e.key === 'ArrowLeft') {
        e.preventDefault();
        setTick((tick) => tick - 1);
      } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        setTick((tick) => tick + 1);
      } else if (e.key === ' ') {
        e.preventDefault();
        setCurrentTick(effectiveTickRef.current);
        setFollowing(false);
        setPlaying((playing) => !playing);
      } else if (e.key === 'l' || e.key === 'L') {
        e.preventDefault();
        jumpToLive();
      }
    };

    window.addEventListener('keydown', listener);
    return () => window.removeEventListener('keydown', listener);
  }, [setTick, jumpToLive]);

  return {
    currentTick: displayTick,
    advanceTick,
    setTick,
    playing,
    setPlaying,
    following,
    jumpToLive,
  };
};

type StageInfo = {
  ticks: number;
  npcs: RawRoomNpcMap;
};

export function getStageInfo(
  challenge: Challenge | null,
  stage: Stage,
  attempt?: number,
): StageInfo {
  if (challenge === null) {
    return { ticks: -1, npcs: {} };
  }

  if (challenge.type === ChallengeType.TOB) {
    const raid = challenge as TobRaid;
    let room: keyof TobRooms = 'maiden';
    let split: SplitType;
    switch (stage) {
      case Stage.TOB_MAIDEN:
        room = 'maiden';
        split = SplitType.TOB_MAIDEN;
        break;
      case Stage.TOB_BLOAT:
        room = 'bloat';
        split = SplitType.TOB_BLOAT;
        break;
      case Stage.TOB_NYLOCAS:
        room = 'nylocas';
        split = SplitType.TOB_NYLO_ROOM;
        break;
      case Stage.TOB_SOTETSEG:
        room = 'sotetseg';
        split = SplitType.TOB_SOTETSEG;
        break;
      case Stage.TOB_XARPUS:
        room = 'xarpus';
        split = SplitType.TOB_XARPUS;
        break;
      case Stage.TOB_VERZIK:
        room = 'verzik';
        split = SplitType.TOB_VERZIK_ROOM;
        break;
    }

    return {
      ticks: challenge.splits[split!] ?? -1,
      npcs: raid.tobRooms[room]?.npcs ?? {},
    };
  }

  if (challenge.type === ChallengeType.COLOSSEUM) {
    const colosseum = challenge as ColosseumChallenge;
    const waveIndex = stage - Stage.COLOSSEUM_WAVE_1;
    const split: SplitType = SplitType.COLOSSEUM_WAVE_1 + waveIndex;
    return {
      ticks: challenge.splits[split] ?? -1,
      npcs: colosseum.colosseum.waves[waveIndex]?.npcs ?? {},
    };
  }

  if (challenge.type === ChallengeType.INFERNO) {
    const inferno = challenge as InfernoChallenge;
    const waveIndex = stage - Stage.INFERNO_WAVE_1;
    return {
      ticks: inferno.inferno.waves[waveIndex]?.ticks ?? -1,
      npcs: inferno.inferno.waves[waveIndex]?.npcs ?? {},
    };
  }

  if (challenge.type === ChallengeType.MOKHAIOTL) {
    const mokhaiotl = challenge as MokhaiotlChallenge;
    let delveIndex;
    if (attempt !== undefined) {
      delveIndex = attempt + 7;
    } else {
      delveIndex = stage - Stage.MOKHAIOTL_DELVE_1;
    }
    return {
      ticks: mokhaiotl.mokhaiotl.delves[delveIndex]?.challengeTicks ?? -1,
      npcs: mokhaiotl.mokhaiotl.delves[delveIndex]?.npcs ?? {},
    };
  }

  return { ticks: -1, npcs: {} };
}

export function useStageEvents<T extends Challenge>(
  stage: Stage,
  attempt?: number,
): StageState<T> {
  const [rawChallenge] = useContext(ChallengeContext) as [T | null, unknown];
  const live = useLiveChallenge();

  const { isLive, liveSplits, setRequestedStage } = live;

  const challenge = useMemo<T | null>(
    () =>
      rawChallenge !== null
        ? { ...rawChallenge, splits: { ...liveSplits, ...rawChallenge.splits } }
        : null,
    [rawChallenge, liveSplits],
  );

  const hasStaticData = getStageInfo(rawChallenge, stage, attempt).ticks > 0;

  // Request live streaming for a stage that has not yet been processed.
  useEffect(() => {
    if (isLive && !hasStaticData) {
      setRequestedStage(stage);
    }
    return () => setRequestedStage(null);
  }, [isLive, hasStaticData, setRequestedStage, stage]);

  const liveResult = useLiveStageState(
    challenge,
    hasStaticData,
    stage,
    attempt,
  );

  // Cache the last live result for bridging the live to processed transition.
  const previousLiveRef = useRef<StageState<T> | null>(null);
  if (liveResult !== null) {
    previousLiveRef.current = liveResult;
  }

  const [loading, setLoading] = useState(true);

  const isLiveStage = liveResult !== null;

  // Atomically set loading to true when transitioning from live to static.
  const [wasLiveStage, setWasLiveStage] = useState(isLiveStage);
  if (wasLiveStage !== isLiveStage) {
    setWasLiveStage(isLiveStage);
    if (wasLiveStage && hasStaticData) {
      setLoading(true);
    }
  }

  const [events, setEvents] = useState<Event[]>([]);
  const [eventState, setEventState] = useState<EventState>({
    eventsByTick: {},
    eventsByType: {},
    playerState: new Map(),
    npcState: new Map(),
    bcf: {
      version: '1.0',
      name: '',
      description: '',
      config: { totalTicks: 0, rowOrder: [], startTick: 0 },
      timeline: { actors: [], ticks: [] },
    },
  });
  const challengeRef = useRef(challenge);

  let { ticks: totalTicks } = getStageInfo(
    challengeRef.current,
    stage,
    attempt,
  );
  if (totalTicks === -1 && events.length > 0) {
    totalTicks = events[events.length - 1].tick;
  }

  useEffect(() => {
    challengeRef.current = challenge;
  }, [challenge]);

  // Fetch events for the stage from the API when static data is available.
  useEffect(() => {
    if (isLiveStage) {
      return;
    }
    if (!hasStaticData) {
      setLoading(false);
      return;
    }

    let isActive = true;
    const c = challengeRef.current;
    if (c === null) {
      return;
    }

    setLoading(true);
    const getEvents = async () => {
      let evts: Event[] = [];

      try {
        const url = `${challengeApiUrl(c.type, c.uuid)}/events?stage=${stage}${
          attempt !== undefined ? `&attempt=${attempt}` : ''
        }`;
        evts = (await fetch(url).then((res) => res.json())) as Event[];
      } catch {
        if (isActive) {
          setEvents([]);
          setLoading(false);
        }
        return;
      }

      if (!isActive) {
        return;
      }

      setEvents(evts);

      if (evts.length > 0) {
        const { ticks, npcs } = getStageInfo(c, stage, attempt);
        let totalTicks = ticks;
        if (totalTicks === -1) {
          totalTicks = evts[evts.length - 1].tick;
        }

        const [eventsByTick, eventsByType] = buildEventMaps(evts);
        const playerState = computePlayerState(
          c.party.map((p) => p.username),
          totalTicks,
          eventsByTick,
          eventsByType,
        );
        const npcState = computeNpcState(
          npcs,
          totalTicks,
          eventsByTick,
          eventsByType,
        );

        setEventState({
          eventsByTick,
          eventsByType,
          playerState,
          npcState,
          bcf: toBcf(
            c,
            stage,
            totalTicks,
            eventsByTick,
            eventsByType,
            playerState,
            npcState,
          ),
        });
      }

      setLoading(false);
    };

    void getEvents();

    return () => {
      isActive = false;
    };
  }, [stage, attempt, isLiveStage, hasStaticData]);

  if (liveResult !== null) {
    return liveResult;
  }

  // When transitioning out of live, bridge with cached live data until static
  // data is fully available.
  if (previousLiveRef.current !== null && (!hasStaticData || loading)) {
    return previousLiveRef.current;
  }

  previousLiveRef.current = null;

  return {
    challenge,
    events,
    totalTicks,
    loading,
    isLive: false,
    isStreaming: false,
    ...eventState,
  };
}

type CustomEntitiesCallback = (tick: number) => AnyEntity[];
type ModifyEntityCallback = (tick: number, entity: AnyEntity) => AnyEntity;

type CustomEntitiesOptions = {
  customEntitiesForTick?: CustomEntitiesCallback;
  modifyEntity?: ModifyEntityCallback;
};

function buildEntitiesForTick(
  tick: number,
  totalTicks: number,
  partyOrb: Record<string, number>,
  playerState: PlayerStateMap,
  npcState: RoomNpcMap,
  modifyEntity: ModifyEntityCallback,
  customEntitiesForTick: CustomEntitiesCallback,
): AnyEntity[] {
  const entitiesForTick: AnyEntity[] = [];

  for (const [playerName, state] of playerState) {
    const ps = state?.at(tick);
    if (!ps) {
      continue;
    }

    const orb = partyOrb[playerName] ?? 7;

    let nextPosition: Coords | undefined = undefined;
    let nextHitpoints: SkillLevel | undefined = undefined;
    if (tick < totalTicks - 1) {
      const nextState = state?.at(tick + 1);
      if (nextState) {
        nextPosition = { x: nextState.xCoord, y: nextState.yCoord };
        nextHitpoints = nextState.skills[Skill.HITPOINTS];
      }
    }

    const playerEntity = new PlayerEntity(
      { x: ps.xCoord, y: ps.yCoord },
      playerName,
      orb,
      { current: ps.skills[Skill.HITPOINTS], next: nextHitpoints },
      nextPosition,
    );

    entitiesForTick.push(modifyEntity(tick, playerEntity));
  }

  for (const [roomId, npc] of npcState) {
    const ns = npc.stateByTick[tick];
    if (!ns) {
      continue;
    }

    let nextPosition: Coords | undefined = undefined;
    let nextHitpoints: SkillLevel | undefined = undefined;
    if (tick < totalTicks - 1) {
      const nextState = npc.stateByTick[tick + 1];
      if (nextState) {
        nextPosition = nextState.position;
        nextHitpoints = nextState.hitpoints;
      }
    }

    let npcEntity = new NpcEntity(
      ns.position,
      ns.id,
      roomId,
      { current: ns.hitpoints, next: nextHitpoints },
      ns.prayers,
      nextPosition,
    );

    npcEntity = modifyEntity(tick, npcEntity) as NpcEntity;
    entitiesForTick.push(npcEntity);
  }

  entitiesForTick.push(...customEntitiesForTick(tick));

  return entitiesForTick;
}

/**
 * Returns a function that builds entities for a given tick in a stage, plus
 * accumulated preload entity sprite URLs.
 *
 * @param challenge Challenge to which the stage belongs.
 * @param playerState Player state for the stage.
 * @param npcState NPC state for the stage.
 * @param totalTicks Total number of ticks in the stage.
 * @param options Options to configure the entities.
 * @returns A function that builds entities for a given tick.
 */
export function useMapEntities(
  challenge: Challenge | null,
  playerState: PlayerStateMap,
  npcState: RoomNpcMap,
  totalTicks: number,
  options: CustomEntitiesOptions = {},
): (tick: number) => AnyEntity[] {
  const { customEntitiesForTick = () => [], modifyEntity = (_, e) => e } =
    options;

  const partyOrb = useMemo(() => {
    if (challenge === null) {
      return {};
    }
    return challenge.party.reduce(
      (acc, p, i) => {
        acc[p.username] = i;
        return acc;
      },
      {} as Record<string, number>,
    );
  }, [challenge]);

  return useCallback(
    (tick: number): AnyEntity[] => {
      if (challenge === null || tick < 0 || tick >= totalTicks) {
        return [];
      }
      return buildEntitiesForTick(
        tick,
        totalTicks,
        partyOrb,
        playerState,
        npcState,
        modifyEntity,
        customEntitiesForTick,
      );
    },
    [
      challenge,
      totalTicks,
      partyOrb,
      playerState,
      npcState,
      modifyEntity,
      customEntitiesForTick,
    ],
  );
}

/**
 * Collects sprite URLs from state maps for preloading.
 *
 * @param npcState NPC state for the stage.
 * @returns Preload URLs for the stage.
 */
export function usePreloads(npcState: RoomNpcMap, skip = false): string[] {
  return useMemo(() => {
    if (skip) {
      return [];
    }
    const urls = new Set<string>();
    for (const [, npc] of npcState) {
      for (const state of npc.stateByTick) {
        if (state !== null) {
          urls.add(npcImageUrl(state.id));
        }
      }
    }
    return Array.from(urls);
  }, [npcState, skip]);
}
