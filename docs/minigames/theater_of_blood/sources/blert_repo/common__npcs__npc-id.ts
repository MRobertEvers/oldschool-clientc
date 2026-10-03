import { NpcId } from '../generated/npc_id';

export { NpcId };

const MAIDEN_ENTRY_IDS: number[] = [
  NpcId.MAIDEN_ENTRY,
  NpcId.MAIDEN_ENTRY_10815,
  NpcId.MAIDEN_ENTRY_10816,
  NpcId.MAIDEN_ENTRY_10817,
  NpcId.MAIDEN_ENTRY_10818,
  NpcId.MAIDEN_ENTRY_10819,
];

const MAIDEN_REGULAR_IDS: number[] = [
  NpcId.MAIDEN_REGULAR,
  NpcId.MAIDEN_REGULAR_8361,
  NpcId.MAIDEN_REGULAR_8362,
  NpcId.MAIDEN_REGULAR_8363,
  NpcId.MAIDEN_REGULAR_8364,
  NpcId.MAIDEN_REGULAR_8365,
];

const MAIDEN_HARD_IDS: number[] = [
  NpcId.MAIDEN_HARD,
  NpcId.MAIDEN_HARD_10823,
  NpcId.MAIDEN_HARD_10824,
  NpcId.MAIDEN_HARD_10825,
  NpcId.MAIDEN_HARD_10826,
  NpcId.MAIDEN_HARD_10827,
];

const NYLOCAS_VASILIAS_ENTRY_IDS: number[] = [
  NpcId.NYLOCAS_VASILIAS_MELEE_ENTRY,
  NpcId.NYLOCAS_VASILIAS_RANGE_ENTRY,
  NpcId.NYLOCAS_VASILIAS_MAGE_ENTRY,
];

const NYLOCAS_VASILIAS_REGULAR_IDS: number[] = [
  NpcId.NYLOCAS_VASILIAS_MELEE_REGULAR,
  NpcId.NYLOCAS_VASILIAS_RANGE_REGULAR,
  NpcId.NYLOCAS_VASILIAS_MAGE_REGULAR,
];

const NYLOCAS_VASILIAS_HARD_IDS: number[] = [
  NpcId.NYLOCAS_VASILIAS_MELEE_HARD,
  NpcId.NYLOCAS_VASILIAS_RANGE_HARD,
  NpcId.NYLOCAS_VASILIAS_MAGE_HARD,
];

export class Npc {
  static isMaidenEntry(npcId: number): boolean {
    return MAIDEN_ENTRY_IDS.includes(npcId);
  }

  static isMaidenRegular(npcId: number): boolean {
    return MAIDEN_REGULAR_IDS.includes(npcId);
  }

  static isMaidenHard(npcId: number): boolean {
    return MAIDEN_HARD_IDS.includes(npcId);
  }

  static isMaiden(npcId: number): boolean {
    return (
      Npc.isMaidenEntry(npcId) ||
      Npc.isMaidenRegular(npcId) ||
      Npc.isMaidenHard(npcId)
    );
  }

  private static readonly MAIDEN_MATOMENOS_IDS: number[] = [
    NpcId.MAIDEN_MATOMENOS_ENTRY,
    NpcId.MAIDEN_MATOMENOS_REGULAR,
    NpcId.MAIDEN_MATOMENOS_HARD,
  ];

  static isMaidenMatomenos(npcId: number): boolean {
    return Npc.MAIDEN_MATOMENOS_IDS.includes(npcId);
  }

  private static readonly MAIDEN_BLOOD_SPAWN_IDS: number[] = [
    NpcId.MAIDEN_BLOOD_SPAWN_ENTRY,
    NpcId.MAIDEN_BLOOD_SPAWN_REGULAR,
    NpcId.MAIDEN_BLOOD_SPAWN_HARD,
  ];

  static isMaidenBloodSpawn(npcId: number): boolean {
    return Npc.MAIDEN_BLOOD_SPAWN_IDS.includes(npcId);
  }

  private static readonly BLOAT_IDS: number[] = [
    NpcId.BLOAT_ENTRY,
    NpcId.BLOAT_REGULAR,
    NpcId.BLOAT_HARD,
  ];

  static isBloat(npcId: number): boolean {
    return Npc.BLOAT_IDS.includes(npcId);
  }

  private static readonly NYLOCAS_ISCHYROS_SMALL_IDS: number[] = [
    NpcId.NYLOCAS_ISCHYROS_SMALL_ENTRY,
    NpcId.NYLOCAS_ISCHYROS_SMALL_REGULAR,
    NpcId.NYLOCAS_ISCHYROS_SMALL_HARD,
    NpcId.NYLOCAS_ISCHYROS_SMALL_AGGRO_ENTRY,
    NpcId.NYLOCAS_ISCHYROS_SMALL_AGGRO_REGULAR,
    NpcId.NYLOCAS_ISCHYROS_SMALL_AGGRO_HARD,
  ];

  static isNylocasIschyrosSmall(npcId: number): boolean {
    return Npc.NYLOCAS_ISCHYROS_SMALL_IDS.includes(npcId);
  }

  private static readonly NYLOCAS_ISCHYROS_BIG_IDS: number[] = [
    NpcId.NYLOCAS_ISCHYROS_BIG_ENTRY,
    NpcId.NYLOCAS_ISCHYROS_BIG_REGULAR,
    NpcId.NYLOCAS_ISCHYROS_BIG_HARD,
    NpcId.NYLOCAS_ISCHYROS_BIG_AGGRO_ENTRY,
    NpcId.NYLOCAS_ISCHYROS_BIG_AGGRO_REGULAR,
    NpcId.NYLOCAS_ISCHYROS_BIG_AGGRO_HARD,
  ];

  static isNylocasIschyrosBig(npcId: number): boolean {
    return Npc.NYLOCAS_ISCHYROS_BIG_IDS.includes(npcId);
  }

  static isNylocasIschyros(npcId: number): boolean {
    return Npc.isNylocasIschyrosSmall(npcId) || Npc.isNylocasIschyrosBig(npcId);
  }

  private static readonly NYLOCAS_TOXOBOLOS_SMALL_IDS: number[] = [
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_ENTRY,
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_REGULAR,
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_HARD,
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_AGGRO_ENTRY,
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_AGGRO_REGULAR,
    NpcId.NYLOCAS_TOXOBOLOS_SMALL_AGGRO_HARD,
  ];

  static isNylocasToxobolosSmall(npcId: number): boolean {
    return Npc.NYLOCAS_TOXOBOLOS_SMALL_IDS.includes(npcId);
  }

  private static readonly NYLOCAS_TOXOBOLOS_BIG_IDS: number[] = [
    NpcId.NYLOCAS_TOXOBOLOS_BIG_ENTRY,
    NpcId.NYLOCAS_TOXOBOLOS_BIG_REGULAR,
    NpcId.NYLOCAS_TOXOBOLOS_BIG_HARD,
    NpcId.NYLOCAS_TOXOBOLOS_BIG_AGGRO_ENTRY,
    NpcId.NYLOCAS_TOXOBOLOS_BIG_AGGRO_REGULAR,
    NpcId.NYLOCAS_TOXOBOLOS_BIG_AGGRO_HARD,
  ];

  static isNylocasToxobolosBig(npcId: number): boolean {
    return Npc.NYLOCAS_TOXOBOLOS_BIG_IDS.includes(npcId);
  }

  static isNylocasToxobolos(npcId: number): boolean {
    return (
      Npc.isNylocasToxobolosSmall(npcId) || Npc.isNylocasToxobolosBig(npcId)
    );
  }

  private static readonly NYLOCAS_HAGIOS_SMALL_IDS: number[] = [
    NpcId.NYLOCAS_HAGIOS_SMALL_ENTRY,
    NpcId.NYLOCAS_HAGIOS_SMALL_REGULAR,
    NpcId.NYLOCAS_HAGIOS_SMALL_HARD,
    NpcId.NYLOCAS_HAGIOS_SMALL_AGGRO_ENTRY,
    NpcId.NYLOCAS_HAGIOS_SMALL_AGGRO_REGULAR,
    NpcId.NYLOCAS_HAGIOS_SMALL_AGGRO_HARD,
  ];

  static isNylocasHagiosSmall(npcId: number): boolean {
    return Npc.NYLOCAS_HAGIOS_SMALL_IDS.includes(npcId);
  }

  private static readonly NYLOCAS_HAGIOS_BIG_IDS: number[] = [
    NpcId.NYLOCAS_HAGIOS_BIG_ENTRY,
    NpcId.NYLOCAS_HAGIOS_BIG_REGULAR,
    NpcId.NYLOCAS_HAGIOS_BIG_HARD,
    NpcId.NYLOCAS_HAGIOS_BIG_AGGRO_ENTRY,
    NpcId.NYLOCAS_HAGIOS_BIG_AGGRO_REGULAR,
    NpcId.NYLOCAS_HAGIOS_BIG_AGGRO_HARD,
  ];

  static isNylocasHagiosBig(npcId: number): boolean {
    return Npc.NYLOCAS_HAGIOS_BIG_IDS.includes(npcId);
  }

  static isNylocasHagios(npcId: number): boolean {
    return Npc.isNylocasHagiosSmall(npcId) || Npc.isNylocasHagiosBig(npcId);
  }

  static isNylocas(npcId: number): boolean {
    return (
      Npc.isNylocasIschyros(npcId) ||
      Npc.isNylocasToxobolos(npcId) ||
      Npc.isNylocasHagios(npcId)
    );
  }

  private static readonly NYLOCAS_PRINKIPAS_IDS: number[] = [
    NpcId.NYLOCAS_PRINKIPAS_MELEE,
    NpcId.NYLOCAS_PRINKIPAS_MAGE,
    NpcId.NYLOCAS_PRINKIPAS_RANGE,
  ];

  static isNylocasPrinkipas(npcId: number): boolean {
    return Npc.NYLOCAS_PRINKIPAS_IDS.includes(npcId);
  }

  private static readonly NYLOCAS_VASILIAS_DROPPING_IDS: number[] = [
    NpcId.NYLOCAS_VASILIAS_DROPPING_ENTRY,
    NpcId.NYLOCAS_VASILIAS_DROPPING_REGULAR,
    NpcId.NYLOCAS_VASILIAS_DROPPING_HARD,
  ];

  static isNylocasVasiliasDropping(npcId: number): boolean {
    return Npc.NYLOCAS_VASILIAS_DROPPING_IDS.includes(npcId);
  }

  static isNylocasVasiliasEntry(npcId: number): boolean {
    return NYLOCAS_VASILIAS_ENTRY_IDS.includes(npcId);
  }

  static isNylocasVasiliasRegular(npcId: number): boolean {
    return NYLOCAS_VASILIAS_REGULAR_IDS.includes(npcId);
  }

  static isNylocasVasiliasHard(npcId: number): boolean {
    return NYLOCAS_VASILIAS_HARD_IDS.includes(npcId);
  }

  static isNylocasVasilias(npcId: number): boolean {
    return (
      Npc.isNylocasVasiliasEntry(npcId) ||
      Npc.isNylocasVasiliasRegular(npcId) ||
      Npc.isNylocasVasiliasHard(npcId)
    );
  }

  private static readonly SOTETSEG_IDS: number[] = [
    NpcId.SOTETSEG_IDLE_ENTRY,
    NpcId.SOTETSEG_IDLE_REGULAR,
    NpcId.SOTETSEG_IDLE_HARD,
    NpcId.SOTETSEG_ENTRY,
    NpcId.SOTETSEG_REGULAR,
    NpcId.SOTETSEG_HARD,
  ];

  static isSotetseg(npcId: number): boolean {
    return Npc.SOTETSEG_IDS.includes(npcId);
  }

  private static readonly XARPUS_ENTRY_IDS: number[] = [
    NpcId.XARPUS_IDLE_ENTRY,
    NpcId.XARPUS_P1_ENTRY,
    NpcId.XARPUS_ENTRY,
  ];

  static isXarpusEntry(npcId: number): boolean {
    return Npc.XARPUS_ENTRY_IDS.includes(npcId);
  }

  private static readonly XARPUS_REGULAR_IDS: number[] = [
    NpcId.XARPUS_IDLE_REGULAR,
    NpcId.XARPUS_P1_REGULAR,
    NpcId.XARPUS_REGULAR,
  ];

  static isXarpusRegular(npcId: number): boolean {
    return Npc.XARPUS_REGULAR_IDS.includes(npcId);
  }

  private static readonly XARPUS_HARD_IDS: number[] = [
    NpcId.XARPUS_IDLE_HARD,
    NpcId.XARPUS_P1_HARD,
    NpcId.XARPUS_HARD,
  ];

  static isXarpusHard(npcId: number): boolean {
    return Npc.XARPUS_HARD_IDS.includes(npcId);
  }

  static isXarpus(npcId: number): boolean {
    return (
      Npc.isXarpusEntry(npcId) ||
      Npc.isXarpusRegular(npcId) ||
      Npc.isXarpusHard(npcId)
    );
  }

  private static readonly VERZIK_P1_IDS: number[] = [
    NpcId.VERZIK_P1_ENTRY,
    NpcId.VERZIK_P1_ENTRY_10832,
    NpcId.VERZIK_P1_REGULAR,
    NpcId.VERZIK_P1_REGULAR_8371,
    NpcId.VERZIK_P1_HARD,
    NpcId.VERZIK_P1_HARD_10849,
  ];

  static isVerzikP1(npcId: number): boolean {
    return Npc.VERZIK_P1_IDS.includes(npcId);
  }

  private static readonly VERZIK_P2_IDS: number[] = [
    NpcId.VERZIK_P2_ENTRY,
    NpcId.VERZIK_P2_REGULAR,
    NpcId.VERZIK_P2_HARD,
  ];

  static isVerzikP2(npcId: number): boolean {
    return Npc.VERZIK_P2_IDS.includes(npcId);
  }

  private static readonly VERZIK_P3_TRANSITION_IDS: number[] = [
    NpcId.VERZIK_P3_TRANSITION_ENTRY,
    NpcId.VERZIK_P3_TRANSITION_REGULAR,
    NpcId.VERZIK_P3_TRANSITION_HARD,
  ];

  static isVerzikP3Transition(npcId: number): boolean {
    return Npc.VERZIK_P3_TRANSITION_IDS.includes(npcId);
  }

  private static readonly VERZIK_P3_IDS: number[] = [
    NpcId.VERZIK_P3_ENTRY,
    NpcId.VERZIK_P3_ENTRY_10836,
    NpcId.VERZIK_P3_REGULAR,
    NpcId.VERZIK_P3_REGULAR_8375,
    NpcId.VERZIK_P3_HARD,
    NpcId.VERZIK_P3_HARD_10853,
  ];

  static isVerzikP3(npcId: number): boolean {
    return Npc.VERZIK_P3_IDS.includes(npcId);
  }

  static isVerzik(npcId: number): boolean {
    return (
      Npc.isVerzikP1(npcId) ||
      Npc.isVerzikP2(npcId) ||
      Npc.isVerzikP3Transition(npcId) ||
      Npc.isVerzikP3(npcId)
    );
  }

  static isVerzikPillar(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.VERZIK_PILLAR;
  }

  private static readonly VERZIK_ISCHYROS_IDS: number[] = [
    NpcId.VERZIK_NYLOCAS_ISCHYROS_ENTRY,
    NpcId.VERZIK_NYLOCAS_ISCHYROS_REGULAR,
    NpcId.VERZIK_NYLOCAS_ISCHYROS_HARD,
  ];

  static isVerzikIschyros(npcId: number): boolean {
    return Npc.VERZIK_ISCHYROS_IDS.includes(npcId);
  }

  private static readonly VERZIK_TOXOBOLOS_IDS: number[] = [
    NpcId.VERZIK_NYLOCAS_TOXOBOLOS_ENTRY,
    NpcId.VERZIK_NYLOCAS_TOXOBOLOS_REGULAR,
    NpcId.VERZIK_NYLOCAS_TOXOBOLOS_HARD,
  ];

  static isVerzikToxobolos(npcId: number): boolean {
    return Npc.VERZIK_TOXOBOLOS_IDS.includes(npcId);
  }

  private static readonly VERZIK_HAGIOS_IDS: number[] = [
    NpcId.VERZIK_NYLOCAS_HAGIOS_ENTRY,
    NpcId.VERZIK_NYLOCAS_HAGIOS_REGULAR,
    NpcId.VERZIK_NYLOCAS_HAGIOS_HARD,
  ];

  static isVerzikHagios(npcId: number): boolean {
    return Npc.VERZIK_HAGIOS_IDS.includes(npcId);
  }

  static isVerzikNylocas(npcId: number): boolean {
    return (
      Npc.isVerzikIschyros(npcId) ||
      Npc.isVerzikToxobolos(npcId) ||
      Npc.isVerzikHagios(npcId)
    );
  }

  private static readonly VERZIK_ATHANATOS_IDS: number[] = [
    NpcId.VERZIK_ATHANATOS_ENTRY,
    NpcId.VERZIK_ATHANATOS_REGULAR,
    NpcId.VERZIK_ATHANATOS_HARD,
  ];

  static isVerzikAthanatos(npcId: number): boolean {
    return Npc.VERZIK_ATHANATOS_IDS.includes(npcId);
  }

  private static readonly VERZIK_MATOMENOS_IDS: number[] = [
    NpcId.VERZIK_MATOMENOS_ENTRY,
    NpcId.VERZIK_MATOMENOS_REGULAR,
    NpcId.VERZIK_MATOMENOS_HARD,
  ];

  static isVerzikMatomenos(npcId: number): boolean {
    return Npc.VERZIK_MATOMENOS_IDS.includes(npcId);
  }

  static isJaguarWarrior(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.JAGUAR_WARRIOR;
  }

  static isSerpentShaman(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.SERPENT_SHAMAN;
  }

  static isMinotaur(npcId: number): boolean {
    return (
      (npcId as NpcId) === NpcId.MINOTAUR ||
      (npcId as NpcId) === NpcId.MINOTAUR_RED_FLAG
    );
  }

  static isFremennikArcher(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.FREMENNIK_ARCHER;
  }

  static isFremennikSeer(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.FREMENNIK_SEER;
  }

  static isFremennikBerserker(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.FREMENNIK_BERSERKER;
  }

  static isFremennik(npcId: number): boolean {
    return (
      Npc.isFremennikArcher(npcId) ||
      Npc.isFremennikSeer(npcId) ||
      Npc.isFremennikBerserker(npcId)
    );
  }

  static isJavelinColossus(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.JAVELIN_COLOSSUS;
  }

  static isManticore(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.MANTICORE;
  }

  static isShockwaveColossus(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.SHOCKWAVE_COLOSSUS;
  }

  static isSolHeredit(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.SOL_HEREDIT;
  }

  static isBeeSwarm(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.BEE_SWARM;
  }

  static isLaserPrism(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.LASER_PRISM;
  }

  static isHealingTotem(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.HEALING_TOTEM;
  }

  static isSolarflare(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.SOLARFLARE;
  }

  private static readonly BLOBLET_IDS: number[] = [
    NpcId.JAL_AKREK_KET,
    NpcId.JAL_AKREK_MEJ,
    NpcId.JAL_AKREK_XIL,
  ];

  static isBloblet(npcId: number): boolean {
    return Npc.BLOBLET_IDS.includes(npcId);
  }

  private static readonly MOKHAIOTL_IDS: number[] = [
    NpcId.MOKHAIOTL,
    NpcId.MOKHAIOTL_BURROWED,
  ];

  static isMokhaiotl(npcId: number): boolean {
    return Npc.MOKHAIOTL_IDS.includes(npcId);
  }

  private static readonly DEMONIC_LARVA_IDS: number[] = [
    NpcId.DEMONIC_LARVA,
    NpcId.DEMONIC_RANGE_LARVA,
    NpcId.DEMONIC_MAGIC_LARVA,
    NpcId.DEMONIC_MELEE_LARVA,
  ];

  static isDemonicLarva(npcId: number): boolean {
    return Npc.DEMONIC_LARVA_IDS.includes(npcId);
  }

  static isJalZek(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.JAL_ZEK;
  }

  static isRockySupport(npcId: number): boolean {
    return (npcId as NpcId) === NpcId.ROCKY_SUPPORT;
  }
}
