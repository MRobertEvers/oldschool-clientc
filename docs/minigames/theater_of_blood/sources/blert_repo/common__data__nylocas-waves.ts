import nylocasWavesJson from './nylocas-waves.json';

export type NyloStyleJson = 'melee' | 'magic' | 'ranged';

export type NyloJson = {
  rotation: NyloStyleJson[];
  big: boolean;
  aggro: boolean;
};

export type LaneSpawnJson = [NyloJson, NyloJson] | [NyloJson | null, null];

export type NylocasWaveJson = {
  /** Wave number. */
  num: number;
  /** Ticks until next wave spawns. */
  naturalStall: number;

  // [0]: north, [1]: south
  east: LaneSpawnJson;
  // [0]: east, [1]: west
  south: LaneSpawnJson;
  // [0]: north, [1]: south
  west: LaneSpawnJson;
};

export const NYLOCAS_WAVES = nylocasWavesJson as NylocasWaveJson[];
