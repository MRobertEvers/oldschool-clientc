import { BCFAction, BCFAttackAction } from '@blert/bcf';
import {
  NpcAttack,
  PlayerAttack,
  PlayerSpell,
  attackDefinitions,
} from '@blert/common';

import { getItemImageUrl } from '@/utils/item';
import { simpleItemCache } from '@/utils/item-cache/simple';
import { SPELL_METADATA } from '@/utils/spell';

export const enum CombatStyle {
  MELEE,
  RANGED,
  MAGIC,
}

export const enum TagColor {
  RED = 'red',
  GREEN = 'green',
  BLUE = 'blue',
  YELLOW = 'yellow',
}

type AttackMetadata = {
  tagColor: TagColor | undefined;
  letter: string;
  ranged: boolean;
  special: boolean;
  style: CombatStyle | null;
  verb: string;
};

type NpcAttackDescriptionFunction = (
  target: React.ReactNode | null,
) => React.ReactNode;

type NpcAttackMetadata = {
  imageUrl: string;
  description: NpcAttackDescriptionFunction;
};

export const ATTACK_METADATA: Record<PlayerAttack, AttackMetadata> = {
  [PlayerAttack.ABYSSAL_BLUDGEON]: {
    tagColor: TagColor.RED,
    letter: 'BLD',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'bludgeoned',
  },
  [PlayerAttack.ACCURSED_SCEPTRE_AUTO]: {
    tagColor: TagColor.YELLOW,
    letter: 'as',
    ranged: false,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'sceptred',
  },
  [PlayerAttack.ACCURSED_SCEPTRE_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'AS',
    ranged: false,
    special: true,
    style: CombatStyle.MAGIC,
    verb: 'sceptre specced',
  },
  [PlayerAttack.AGS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'AGS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: "AGS'd",
  },
  [PlayerAttack.ARCLIGHT_AUTO]: {
    tagColor: TagColor.RED,
    letter: 'arc',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'arclighted',
  },
  [PlayerAttack.ARCLIGHT_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'ARC',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'arclight specced',
  },
  [PlayerAttack.ATLATL_AUTO]: {
    tagColor: TagColor.GREEN,
    letter: 'atl',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'atlatled',
  },
  [PlayerAttack.ATLATL_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'ATL',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'atlatled',
  },
  [PlayerAttack.BGS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'BGS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: "BGS'd",
  },
  [PlayerAttack.BLOWPIPE]: {
    tagColor: TagColor.GREEN,
    letter: 'BP',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'piped',
  },
  [PlayerAttack.BLOWPIPE_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'BPs',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'pipe specced',
  },
  [PlayerAttack.BOWFA]: {
    tagColor: TagColor.GREEN,
    letter: 'BFa',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'bowed',
  },
  [PlayerAttack.BURNING_CLAW_SCRATCH]: {
    tagColor: TagColor.RED,
    letter: 'bc',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'claw scratched',
  },
  [PlayerAttack.BURNING_CLAW_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'BC',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'clawed',
  },
  [PlayerAttack.CHALLY_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'CH',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'challied',
  },
  [PlayerAttack.CHALLY_SWIPE]: {
    tagColor: TagColor.YELLOW,
    letter: 'ch',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'swiped',
  },
  [PlayerAttack.CHIN_BLACK]: {
    tagColor: TagColor.GREEN,
    letter: 'CCB',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'chinned',
  },
  [PlayerAttack.CHIN_GREY]: {
    tagColor: TagColor.GREEN,
    letter: 'CCG',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'chinned',
  },
  [PlayerAttack.CHIN_RED]: {
    tagColor: TagColor.GREEN,
    letter: 'CCR',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'chinned',
  },
  [PlayerAttack.CLAW_SCRATCH]: {
    tagColor: TagColor.RED,
    letter: 'c',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'claw scratched',
  },
  [PlayerAttack.CLAW_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'C',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'clawed',
  },
  [PlayerAttack.DARK_DEMONBANE]: {
    tagColor: TagColor.BLUE,
    letter: 'DEM',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'demonbaned',
  },
  [PlayerAttack.DARKLIGHT_AUTO]: {
    tagColor: TagColor.RED,
    letter: 'dl',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'darklighted',
  },
  [PlayerAttack.DARKLIGHT_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'DL',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'darklight specced',
  },
  [PlayerAttack.DAWN_AUTO]: {
    tagColor: TagColor.YELLOW,
    letter: 'db',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'dawned',
  },
  [PlayerAttack.DAWN_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'DB',
    ranged: true,
    special: true,
    style: null,
    verb: 'dawned',
  },
  [PlayerAttack.DART]: {
    tagColor: TagColor.GREEN,
    letter: 'D',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'threw a dart at',
  },
  [PlayerAttack.DDS_POKE]: {
    tagColor: TagColor.YELLOW,
    letter: 'dds',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'poked',
  },
  [PlayerAttack.DDS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'DDS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'DDSed',
  },
  [PlayerAttack.DHAROKS_GREATAXE]: {
    tagColor: TagColor.RED,
    letter: 'DH',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hacked',
  },
  [PlayerAttack.DINHS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'BW',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'dinhsed',
  },
  [PlayerAttack.DRAGON_HUNTER_LANCE]: {
    tagColor: TagColor.RED,
    letter: 'DHL',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'poked',
  },
  [PlayerAttack.DRAGON_KNIFE_AUTO]: {
    tagColor: TagColor.GREEN,
    letter: 'dk',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'knifed',
  },
  [PlayerAttack.DRAGON_KNIFE_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'DK',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'knifed',
  },
  [PlayerAttack.DRAGON_SCIMITAR]: {
    tagColor: TagColor.RED,
    letter: 'DS',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'slashed',
  },
  [PlayerAttack.DUAL_MACUAHUITL]: {
    tagColor: TagColor.RED,
    letter: 'DMC',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'pummeled',
  },
  [PlayerAttack.EARTHBOUND_TECPATL]: {
    tagColor: TagColor.RED,
    letter: 'ET',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'poked',
  },
  [PlayerAttack.ELDER_MAUL]: {
    tagColor: TagColor.RED,
    letter: 'eld',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'maul whacked',
  },
  [PlayerAttack.ELDER_MAUL_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'ELD',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'mauled',
  },
  [PlayerAttack.EMBERLIGHT_AUTO]: {
    tagColor: TagColor.RED,
    letter: 'em',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'embered',
  },
  [PlayerAttack.EMBERLIGHT_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'EMB',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'ember specced',
  },
  [PlayerAttack.EYE_OF_AYAK_AUTO]: {
    tagColor: TagColor.BLUE,
    letter: 'aya',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'ayaked',
  },
  [PlayerAttack.EYE_OF_AYAK_SPEC]: {
    tagColor: TagColor.BLUE,
    letter: 'AYA',
    ranged: true,
    special: true,
    style: CombatStyle.MAGIC,
    verb: 'ayak specced',
  },
  [PlayerAttack.FANG_STAB]: {
    tagColor: TagColor.RED,
    letter: 'FNG',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'fanged',
  },
  [PlayerAttack.GLACIAL_TEMOTLI]: {
    tagColor: TagColor.RED,
    letter: 'GT',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'pummeled',
  },
  [PlayerAttack.GOBLIN_PAINT_CANNON]: {
    tagColor: TagColor.RED,
    letter: 'GPC',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'kicked',
  },
  [PlayerAttack.GODSWORD_SMACK]: {
    tagColor: TagColor.YELLOW,
    letter: 'gs',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'smacked',
  },
  [PlayerAttack.GUTHANS_WARSPEAR]: {
    tagColor: TagColor.RED,
    letter: 'GW',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'attacked',
  },
  [PlayerAttack.HAM_JOINT]: {
    tagColor: TagColor.RED,
    letter: 'HAM',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hammed',
  },
  [PlayerAttack.HAMMER_BOP]: {
    tagColor: TagColor.RED,
    letter: 'h',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hammer bopped',
  },
  [PlayerAttack.HAMMER_SPEC]: {
    tagColor: TagColor.RED,
    letter: 'H',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'hammered',
  },
  [PlayerAttack.ICE_RUSH]: {
    tagColor: TagColor.BLUE,
    letter: 'RSH',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'rushed',
  },
  [PlayerAttack.INQUISITORS_MACE]: {
    tagColor: TagColor.RED,
    letter: 'IM',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'bashed',
  },
  [PlayerAttack.KARILS_CROSSBOW]: {
    tagColor: TagColor.GREEN,
    letter: 'KC',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'attacked',
  },
  [PlayerAttack.KICK]: {
    tagColor: undefined,
    letter: 'k',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'kicked',
  },
  [PlayerAttack.KODAI_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.KODAI_BASH]: {
    tagColor: TagColor.BLUE,
    letter: 'kb',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'kodai bashed',
  },
  [PlayerAttack.NM_STAFF_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.NM_STAFF_BASH]: {
    tagColor: TagColor.BLUE,
    letter: 'vnm',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'nightmare bashed',
  },
  [PlayerAttack.NOXIOUS_HALBERD]: {
    tagColor: TagColor.RED,
    letter: 'NH',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hallied',
  },
  [PlayerAttack.PUNCH]: {
    tagColor: undefined,
    letter: 'p',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'punched',
  },
  [PlayerAttack.RAPIER]: {
    tagColor: TagColor.RED,
    letter: 'R',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'stabbed',
  },
  [PlayerAttack.SAELDOR]: {
    tagColor: TagColor.RED,
    letter: 'B',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'slashed',
  },
  [PlayerAttack.SANG]: {
    tagColor: TagColor.BLUE,
    letter: 'T',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'sanged',
  },
  [PlayerAttack.SANG_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.SCEPTRE_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.SCORCHING_BOW_AUTO]: {
    tagColor: TagColor.GREEN,
    letter: 'sco',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'scobowed',
  },
  [PlayerAttack.SCORCHING_BOW_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'SCO',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'scobo specced',
  },
  [PlayerAttack.SCYTHE]: {
    tagColor: TagColor.RED,
    letter: 'S',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'scythed',
  },
  [PlayerAttack.SCYTHE_UNCHARGED]: {
    tagColor: TagColor.RED,
    letter: 's',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'scythed',
  },
  [PlayerAttack.SGS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'SGS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: "SGS'd",
  },
  [PlayerAttack.SHADOW]: {
    tagColor: TagColor.BLUE,
    letter: 'Sh',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'shadowed',
  },
  [PlayerAttack.SHADOW_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.SOTD_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.SOULREAPER_AXE]: {
    tagColor: TagColor.RED,
    letter: 'AXE',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hacked at',
  },
  [PlayerAttack.STAFF_OF_LIGHT_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.STAFF_OF_LIGHT_SWIPE]: {
    tagColor: TagColor.BLUE,
    letter: 'SOL',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'swiped',
  },
  [PlayerAttack.SULPHUR_BLADES]: {
    tagColor: TagColor.RED,
    letter: 'SUL',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'slashed',
  },
  [PlayerAttack.SWIFT_BLADE]: {
    tagColor: TagColor.RED,
    letter: 'SB',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'swifted',
  },
  [PlayerAttack.TENT_WHIP]: {
    tagColor: TagColor.RED,
    letter: 'TW',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'whipped',
  },
  [PlayerAttack.TORAGS_HAMMERS]: {
    tagColor: TagColor.RED,
    letter: 'TH',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'attacked',
  },
  [PlayerAttack.TOXIC_TRIDENT]: {
    tagColor: TagColor.BLUE,
    letter: 'T',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'tridented',
  },
  [PlayerAttack.TOXIC_TRIDENT_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.TOXIC_STAFF_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.TOXIC_STAFF_SWIPE]: {
    tagColor: TagColor.BLUE,
    letter: 'TS',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'swiped',
  },
  [PlayerAttack.TRIDENT]: {
    tagColor: TagColor.BLUE,
    letter: 'T',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'tridented',
  },
  [PlayerAttack.TRIDENT_BARRAGE]: {
    tagColor: TagColor.BLUE,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.TONALZTICS_AUTO]: {
    tagColor: TagColor.YELLOW,
    letter: 'ga',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'ralos tossed',
  },
  [PlayerAttack.TONALZTICS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'G',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'ralosed',
  },
  [PlayerAttack.TONALZTICS_UNCHARGED]: {
    tagColor: TagColor.YELLOW,
    letter: 'g',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'ralos tossed',
  },
  [PlayerAttack.TWISTED_BOW]: {
    tagColor: TagColor.GREEN,
    letter: 'TB',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'bowed',
  },
  [PlayerAttack.VENATOR_BOW]: {
    tagColor: TagColor.GREEN,
    letter: 'VB',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'bowed',
  },
  [PlayerAttack.VERACS_FLAIL]: {
    tagColor: TagColor.RED,
    letter: 'VF',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'attacked',
  },
  [PlayerAttack.VOIDWAKER_AUTO]: {
    tagColor: TagColor.YELLOW,
    letter: 'vw',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'slashed',
  },
  [PlayerAttack.VOIDWAKER_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'VW',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: 'voidwakered',
  },
  [PlayerAttack.VOLATILE_NM_SPEC]: {
    tagColor: TagColor.BLUE,
    letter: 'VNM',
    ranged: true,
    special: true,
    style: CombatStyle.MAGIC,
    verb: 'volatiled',
  },
  [PlayerAttack.WEBWEAVER_AUTO]: {
    tagColor: TagColor.GREEN,
    letter: 'ww',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'bowed',
  },
  [PlayerAttack.WEBWEAVER_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'WW',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: 'webweavered',
  },
  [PlayerAttack.XGS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'XGS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: "XGS'd",
  },
  [PlayerAttack.ZCB_AUTO]: {
    tagColor: TagColor.GREEN,
    letter: 'zcb',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: "ZCB'd",
  },
  [PlayerAttack.ZCB_SPEC]: {
    tagColor: TagColor.GREEN,
    letter: 'ZCB',
    ranged: true,
    special: true,
    style: CombatStyle.RANGED,
    verb: "ZCB'd",
  },
  [PlayerAttack.ZGS_SPEC]: {
    tagColor: TagColor.YELLOW,
    letter: 'ZGS',
    ranged: false,
    special: true,
    style: CombatStyle.MELEE,
    verb: "ZGS'd",
  },
  [PlayerAttack.ZOMBIE_AXE]: {
    tagColor: TagColor.RED,
    letter: 'ZMB',
    ranged: false,
    special: false,
    style: CombatStyle.MELEE,
    verb: 'hacked at',
  },
  [PlayerAttack.UNKNOWN_BARRAGE]: {
    tagColor: undefined,
    letter: 'F',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'froze',
  },
  [PlayerAttack.UNKNOWN_BOW]: {
    tagColor: TagColor.GREEN,
    letter: 'UNK',
    ranged: true,
    special: false,
    style: CombatStyle.RANGED,
    verb: 'bowed',
  },
  [PlayerAttack.UNKNOWN_POWERED_STAFF]: {
    tagColor: TagColor.BLUE,
    letter: 'UNK',
    ranged: true,
    special: false,
    style: CombatStyle.MAGIC,
    verb: 'attacked',
  },
  [PlayerAttack.UNKNOWN]: {
    tagColor: undefined,
    letter: 'UNK',
    ranged: false,
    special: false,
    style: null,
    verb: 'attacked',
  },
};

export function bcfToPlayerAttack(bcfIdentifier: string): PlayerAttack {
  return (
    PlayerAttack[bcfIdentifier as keyof typeof PlayerAttack] ??
    PlayerAttack.UNKNOWN
  );
}

export function bcfToNpcAttack(bcfIdentifier: string): NpcAttack {
  return (
    NpcAttack[bcfIdentifier as keyof typeof NpcAttack] ?? NpcAttack.UNKNOWN
  );
}

/**
 * Returns the combat style for an attack type.
 * @param type The attack type.
 * @returns The combat style for the attack type, or `null` if unknown.
 */
export function getAttackStyle(type: PlayerAttack): CombatStyle | null {
  return ATTACK_METADATA[type]?.style ?? null;
}

/**
 * Standard description for attacks that don't have a specific description.
 *
 * @param attackName A human-readable name for the attack, to be used to
 * complete the sentence "Targeted Y with ..." or "Did ..."
 * @returns Function that generates a simple description of the attack.
 */
function basicDescription(attackName: string): NpcAttackDescriptionFunction {
  const description: NpcAttackDescriptionFunction = (target) => {
    if (target) {
      return (
        <span>
          Targeted {target} with {attackName}
        </span>
      );
    }
    return <span>Did {attackName}</span>;
  };

  return description;
}

export const NPC_ATTACK_METADATA: Record<NpcAttack, NpcAttackMetadata> = {
  [NpcAttack.UNKNOWN]: {
    imageUrl: '/images/huh.png',
    description: basicDescription('an unknown attack'),
  },
  [NpcAttack.TOB_MAIDEN_AUTO]: {
    imageUrl: '/maiden_auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.TOB_MAIDEN_BLOOD_THROW]: {
    imageUrl: '/maiden_blood_throw.png',
    description: (target) => (
      <span>Threw blood{target ? <> at {target}</> : ''}</span>
    ),
  },
  [NpcAttack.TOB_BLOAT_STOMP]: {
    imageUrl: '/bloat_stomp.webp',
    description: () => <span>Stomped</span>,
  },
  [NpcAttack.TOB_NYLO_BOSS_MELEE]: {
    imageUrl: '/nylo_boss_melee.png',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.TOB_NYLO_BOSS_RANGE]: {
    imageUrl: '/nylo_boss_range.png',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.TOB_NYLO_BOSS_MAGE]: {
    imageUrl: '/nylo_boss_mage.png',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.TOB_SOTE_MELEE]: {
    imageUrl: '/sote_melee.png',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.TOB_SOTE_BALL]: {
    imageUrl: '/sote_ball.png',
    description: basicDescription('a ball'),
  },
  [NpcAttack.TOB_SOTE_DEATH_BALL]: {
    imageUrl: '/sote_death_ball.png',
    description: (target) => (
      <span>Launched a death ball{target ? <> at {target}</> : ''}</span>
    ),
  },
  [NpcAttack.TOB_XARPUS_SPIT]: {
    imageUrl: '/xarpus_spit.png',
    description: (target) => (
      <span>Spat poison{target ? <> at {target}</> : ''}</span>
    ),
  },
  [NpcAttack.TOB_XARPUS_TURN]: {
    imageUrl: '/xarpus_turn.webp',
    description: () => <span>Turned</span>,
  },
  [NpcAttack.TOB_VERZIK_P1_AUTO]: {
    imageUrl: '/verzik_p1_auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.TOB_VERZIK_P2_BOUNCE]: {
    imageUrl: '/verzik_p2_bounce.png',
    description: (target) => <span>Bounced {target ?? 'someone'}</span>,
  },
  [NpcAttack.TOB_VERZIK_P2_CABBAGE]: {
    imageUrl: '/verzik_p2_cabbage.png',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.TOB_VERZIK_P2_ZAP]: {
    imageUrl: '/verzik_p2_zap.png',
    description: basicDescription('a zap'),
  },
  [NpcAttack.TOB_VERZIK_P2_PURPLE]: {
    imageUrl: '/verzik_p2_purple.png',
    description: basicDescription('a purple crab'),
  },
  [NpcAttack.TOB_VERZIK_P2_MAGE]: {
    imageUrl: '/verzik_p2_mage.webp',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.TOB_VERZIK_P3_AUTO]: {
    imageUrl: '/images/huh.png',
    description: basicDescription('an unknown attack'),
  },
  [NpcAttack.TOB_VERZIK_P3_MELEE]: {
    imageUrl: '/verzik_p3_melee.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.TOB_VERZIK_P3_RANGE]: {
    imageUrl: '/verzik_p3_range.webp',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.TOB_VERZIK_P3_MAGE]: {
    imageUrl: '/verzik_p3_mage.webp',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.TOB_VERZIK_P3_WEBS]: {
    imageUrl: '/verzik_p3_webs.webp',
    description: () => <span>Started releasing webs</span>,
  },
  [NpcAttack.TOB_VERZIK_P3_YELLOWS]: {
    imageUrl: '/verzik_p3_yellow.webp',
    description: () => <span>Spawned yellow pools</span>,
  },
  [NpcAttack.TOB_VERZIK_P3_BALL]: {
    imageUrl: '/verzik_p3_ball.webp',
    description: (target) => (
      <span>Launched a green ball{target ? <> at {target}</> : ''}</span>
    ),
  },
  [NpcAttack.COLOSSEUM_BERSERKER_AUTO]: {
    imageUrl: '/images/colosseum/fremennik-berserker.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_SEER_AUTO]: {
    imageUrl: '/images/colosseum/fremennik-seer.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_ARCHER_AUTO]: {
    imageUrl: '/images/colosseum/fremennik-archer.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_SHAMAN_AUTO]: {
    imageUrl: '/images/colosseum/shaman-auto.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_JAGUAR_AUTO]: {
    imageUrl: '/images/colosseum/jaguar-auto.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_JAVELIN_AUTO]: {
    imageUrl: '/images/colosseum/javelin-colossus.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_JAVELIN_TOSS]: {
    imageUrl: '/images/colosseum/javelin-toss.webp',
    description: basicDescription('a javelin toss'),
  },
  [NpcAttack.COLOSSEUM_MANTICORE_MAGE]: {
    imageUrl: '/images/colosseum/manticore-mage.webp',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.COLOSSEUM_MANTICORE_RANGE]: {
    imageUrl: '/images/colosseum/manticore-range.webp',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.COLOSSEUM_MANTICORE_MELEE]: {
    imageUrl: '/images/colosseum/manticore-melee.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.COLOSSEUM_SHOCKWAVE_AUTO]: {
    imageUrl: '/images/colosseum/shockwave-auto.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_MINOTAUR_AUTO]: {
    imageUrl: '/images/colosseum/minotaur-auto.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.COLOSSEUM_HEREDIT_THRUST]: {
    imageUrl: '/images/colosseum/heredit-thrust.webp',
    description: basicDescription('a trident stab'),
  },
  [NpcAttack.COLOSSEUM_HEREDIT_SLAM]: {
    imageUrl: '/images/colosseum/heredit-slam.webp',
    description: basicDescription('a shield bash'),
  },
  [NpcAttack.COLOSSEUM_HEREDIT_COMBO]: {
    imageUrl: '/images/colosseum/heredit-combo.webp',
    description: basicDescription('a combo attack'),
  },
  [NpcAttack.COLOSSEUM_HEREDIT_BREAK]: {
    imageUrl: '/images/colosseum/heredit-grapple.webp',
    description: basicDescription('a grapple attack'),
  },
  [NpcAttack.INFERNO_BAT_AUTO]: {
    imageUrl: '/images/inferno/bat-auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_BLOB_RANGED]: {
    imageUrl: '/images/inferno/blob-ranged.png',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.INFERNO_BLOB_MAGE]: {
    imageUrl: '/images/inferno/blob-mage.png',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.INFERNO_BLOB_MELEE]: {
    imageUrl: '/images/combat/punch.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.INFERNO_BLOBLET_RANGED_AUTO]: {
    imageUrl: '/images/npcs/7695.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_BLOBLET_MAGE_AUTO]: {
    imageUrl: '/images/npcs/7694.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_BLOBLET_MELEE_AUTO]: {
    imageUrl: '/images/npcs/7696.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_MELEER_AUTO]: {
    imageUrl: '/images/inferno/meleer-auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_MELEER_DIG]: {
    imageUrl: '/images/inferno/meleer-dig.png',
    description: () => <span>Dug</span>,
  },
  [NpcAttack.INFERNO_RANGER_AUTO]: {
    imageUrl: '/images/inferno/ranger-auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_RANGER_MELEE]: {
    imageUrl: '/images/combat/punch.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.INFERNO_MAGER_AUTO]: {
    imageUrl: '/images/inferno/mager-auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_MAGER_MELEE]: {
    imageUrl: '/images/combat/punch.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.INFERNO_MAGER_RESURRECT]: {
    imageUrl: '/images/inferno/mager-resurrect.png',
    description: (target) => <span>Resurrected {target ?? 'someone'}</span>,
  },
  [NpcAttack.INFERNO_JAD_RANGED]: {
    imageUrl: '/images/inferno/jad-ranged.png',
    description: basicDescription('a ranged attack'),
  },
  [NpcAttack.INFERNO_JAD_MAGE]: {
    imageUrl: '/images/inferno/jad-mage.png',
    description: basicDescription('a magic attack'),
  },
  [NpcAttack.INFERNO_JAD_MELEE]: {
    imageUrl: '/images/combat/punch.webp',
    description: basicDescription('a melee attack'),
  },
  [NpcAttack.INFERNO_JAD_HEALER_AUTO]: {
    imageUrl: '/images/npcs/7701.webp',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.INFERNO_ZUK_AUTO]: {
    imageUrl: '/images/inferno/zuk-auto.png',
    description: basicDescription('an auto attack'),
  },
  [NpcAttack.MOKHAIOTL_AUTO]: {
    imageUrl: '/images/huh.png',
    description: basicDescription('an orb'),
  },
  [NpcAttack.MOKHAIOTL_RANGED_AUTO]: {
    imageUrl: '/images/mokhaiotl/ranged-orb.png',
    description: basicDescription('a ranged orb'),
  },
  [NpcAttack.MOKHAIOTL_MAGE_AUTO]: {
    imageUrl: '/images/mokhaiotl/magic-orb.png',
    description: basicDescription('a magic orb'),
  },
  [NpcAttack.MOKHAIOTL_MELEE_AUTO]: {
    imageUrl: '/images/mokhaiotl/melee-orb.png',
    description: basicDescription('a melee orb'),
  },
  [NpcAttack.MOKHAIOTL_BALL]: {
    imageUrl: '/images/huh.png',
    description: () => <span>Launched a ball</span>,
  },
  [NpcAttack.MOKHAIOTL_RANGED_BALL]: {
    imageUrl: '/images/mokhaiotl/ranged-ball.png',
    description: () => <span>Launched a ranged ball</span>,
  },
  [NpcAttack.MOKHAIOTL_MAGE_BALL]: {
    imageUrl: '/images/mokhaiotl/magic-ball.png',
    description: () => <span>Launched a magic ball</span>,
  },
  [NpcAttack.MOKHAIOTL_CHARGE]: {
    imageUrl: '/images/mokhaiotl/charge.png',
    description: () => <span>Started charging</span>,
  },
  [NpcAttack.MOKHAIOTL_BLAST]: {
    imageUrl: '/images/mokhaiotl/blast.png',
    description: basicDescription('a blast'),
  },
  [NpcAttack.MOKHAIOTL_RACECAR]: {
    imageUrl: '/images/mokhaiotl/racecar.webp',
    description: (target) => (
      <span>Dug{target ? <> towards {target}</> : ''}</span>
    ),
  },
  [NpcAttack.MOKHAIOTL_SLAM]: {
    imageUrl: '/images/mokhaiotl/shockwave.png',
    description: () => <span>Slammed the ground</span>,
  },
  [NpcAttack.MOKHAIOTL_SHOCKWAVE]: {
    imageUrl: '/images/mokhaiotl/shockwave.png',
    description: () => <span>Released a shockwave</span>,
  },
  [NpcAttack.MOKHAIOTL_MELEE]: {
    imageUrl: '/images/mokhaiotl/melee.png',
    description: basicDescription('a melee attack'),
  },
};

const DEFAULT_WEAPON_IDS = new Map<PlayerAttack, number>(
  attackDefinitions
    .filter((def) => def.weaponIds?.length > 0 && def.weaponIds[0] !== -1)
    .map((def) => {
      const attackType = PlayerAttack[def.name as keyof typeof PlayerAttack];
      return [attackType, def.weaponIds[0]] as const;
    })
    .filter(([type]) => type !== undefined),
);

/**
 * Returns a default weapon ID for an attack type.
 * @param type The attack type.
 * @returns The default weapon ID for the attack type, or undefined if unknown.
 */
export function getDefaultWeaponId(type: PlayerAttack): number | undefined {
  return DEFAULT_WEAPON_IDS.get(type);
}

function weaponForAttack(
  attack: BCFAttackAction,
): { id: number; name: string } | null {
  const type = bcfToPlayerAttack(attack.attackType);
  const id = attack.weaponId ?? getDefaultWeaponId(type);
  if (id === undefined) {
    return null;
  }
  return { id, name: attack.weaponName ?? simpleItemCache.getItemName(id) };
}

/** Returns the image URL of the weapon used by `attack`. */
export function getWeaponImageUrl(attack: BCFAttackAction): string {
  const weapon = weaponForAttack(attack);
  if (weapon !== null) {
    return getItemImageUrl(weapon.id, weapon.name, 1);
  }

  switch (bcfToPlayerAttack(attack.attackType)) {
    case PlayerAttack.PUNCH:
      return '/images/combat/punch.webp';
    case PlayerAttack.KICK:
      return '/images/combat/kick.webp';
    default:
      return '/images/huh.png';
  }
}

export type ActionMetadata = {
  name: string;
  imageUrl?: string;
};

/** Returns the name and, if it has one, the image of `action`. */
export function getActionMetadata(action: BCFAction): ActionMetadata {
  switch (action.type) {
    case 'attack':
      return {
        name: weaponForAttack(action)?.name ?? action.attackType,
        imageUrl: getWeaponImageUrl(action),
      };
    case 'spell': {
      const type = PlayerSpell[action.spellType as keyof typeof PlayerSpell];
      const meta = SPELL_METADATA[type] ?? SPELL_METADATA[PlayerSpell.UNKNOWN];
      return { name: meta.name, imageUrl: meta.imageUrl };
    }
    case 'utility':
      return { name: action.utilityType };
    case 'death':
      return { name: 'Death', imageUrl: '/images/combat/skull.webp' };
    case 'npcAttack':
      return {
        name: action.attackType,
        imageUrl:
          NPC_ATTACK_METADATA[bcfToNpcAttack(action.attackType)].imageUrl,
      };
    case 'npcPhase':
      return { name: action.phaseType };
  }
}
