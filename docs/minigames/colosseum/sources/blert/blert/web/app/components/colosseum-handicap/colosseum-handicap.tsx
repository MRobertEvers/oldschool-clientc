import { Handicap } from '@blert/common';
import Image from 'next/image';

import { GLOBAL_TOOLTIP_ID } from '@/components/tooltip';

import styles from './style.module.scss';

const HANDICAPS = {
  [Handicap.BEES]: {
    name: 'Bees!',
    imageUrl: '/images/colosseum/bees.webp',
    level: 'I',
  },
  [Handicap.BEES_2]: {
    name: 'Bees! (II)',
    imageUrl: '/images/colosseum/bees-2.webp',
    level: 'II',
  },
  [Handicap.BEES_3]: {
    name: 'Bees! (III)',
    imageUrl: '/images/colosseum/bees-3.webp',
    level: 'III',
  },
  [Handicap.BLASPHEMY]: {
    name: 'Blasphemy',
    imageUrl: '/images/colosseum/blasphemy.png',
    level: 'I',
  },
  [Handicap.BLASPHEMY_2]: {
    name: 'Blasphemy (II)',
    imageUrl: '/images/colosseum/blasphemy-2.webp',
    level: 'II',
  },
  [Handicap.BLASPHEMY_3]: {
    name: 'Blasphemy (III)',
    imageUrl: '/images/colosseum/blasphemy-3.webp',
    level: 'III',
  },
  [Handicap.DOOM]: {
    name: 'Doom',
    imageUrl: '/images/colosseum/doom.webp',
    level: 'I',
  },
  [Handicap.DOOM_2]: {
    name: 'Doom (II)',
    imageUrl: '/images/colosseum/doom-2.webp',
    level: 'II',
  },
  [Handicap.DOOM_3]: {
    name: 'Doom (III)',
    imageUrl: '/images/colosseum/doom-3.webp',
    level: 'III',
  },
  [Handicap.DYNAMIC_DUO]: {
    name: 'Dynamic Duo',
    imageUrl: '/images/colosseum/dynamic-duo.webp',
    level: 'I',
  },
  [Handicap.FRAILTY]: {
    name: 'Frailty',
    imageUrl: '/images/colosseum/frailty.webp',
    level: 'I',
  },
  [Handicap.FRAILTY_2]: {
    name: 'Frailty (II)',
    imageUrl: '/images/colosseum/frailty-2.webp',
    level: 'II',
  },
  [Handicap.FRAILTY_3]: {
    name: 'Frailty (III)',
    imageUrl: '/images/colosseum/frailty-3.webp',
    level: 'III',
  },
  [Handicap.MANTIMAYHEM]: {
    name: 'Mantimayhem',
    imageUrl: '/images/colosseum/mantimayhem.webp',
    level: 'I',
  },
  [Handicap.MANTIMAYHEM_2]: {
    name: 'Mantimayhem (II)',
    imageUrl: '/images/colosseum/mantimayhem-2.webp',
    level: 'II',
  },
  [Handicap.MANTIMAYHEM_3]: {
    name: 'Mantimayhem (III)',
    imageUrl: '/images/colosseum/mantimayhem-3.png',
    level: 'III',
  },
  [Handicap.MYOPIA]: {
    name: 'Myopia',
    imageUrl: '/images/colosseum/myopia.webp',
    level: 'I',
  },
  [Handicap.MYOPIA_2]: {
    name: 'Myopia (II)',
    imageUrl: '/images/colosseum/myopia-2.webp',
    level: 'II',
  },
  [Handicap.MYOPIA_3]: {
    name: 'Myopia (III)',
    imageUrl: '/images/colosseum/myopia-3.webp',
    level: 'III',
  },
  [Handicap.QUARTET]: {
    name: 'Quartet',
    imageUrl: '/images/colosseum/quartet.webp',
    level: 'I',
  },
  [Handicap.RED_FLAG]: {
    name: 'Red Flag',
    imageUrl: '/images/colosseum/red-flag.webp',
    level: 'I',
  },
  [Handicap.REENTRY]: {
    name: 'Reentry',
    imageUrl: '/images/colosseum/reentry.webp',
    level: 'I',
  },
  [Handicap.REENTRY_2]: {
    name: 'Reentry (II)',
    imageUrl: '/images/colosseum/reentry-2.webp',
    level: 'II',
  },
  [Handicap.REENTRY_3]: {
    name: 'Reentry (III)',
    imageUrl: '/images/colosseum/reentry-3.webp',
    level: 'III',
  },
  [Handicap.RELENTLESS]: {
    name: 'Relentless',
    imageUrl: '/images/colosseum/relentless.webp',
    level: 'I',
  },
  [Handicap.RELENTLESS_2]: {
    name: 'Relentless (II)',
    imageUrl: '/images/colosseum/relentless-2.webp',
    level: 'II',
  },
  [Handicap.RELENTLESS_3]: {
    name: 'Relentless (III)',
    imageUrl: '/images/colosseum/relentless-3.png',
    level: 'III',
  },
  [Handicap.SOLARFLARE]: {
    name: 'Solarflare',
    imageUrl: '/images/colosseum/solarflare.webp',
    level: 'I',
  },
  [Handicap.SOLARFLARE_2]: {
    name: 'Solarflare (II)',
    imageUrl: '/images/colosseum/solarflare-2.webp',
    level: 'II',
  },
  [Handicap.SOLARFLARE_3]: {
    name: 'Solarflare (III)',
    imageUrl: '/images/colosseum/solarflare-3.webp',
    level: 'III',
  },
  [Handicap.TOTEMIC]: {
    name: 'Totemic',
    imageUrl: '/images/colosseum/totemic.webp',
    level: 'I',
  },
  [Handicap.VOLATILITY]: {
    name: 'Volatility',
    imageUrl: '/images/colosseum/volatility.webp',
    level: 'I',
  },
  [Handicap.VOLATILITY_2]: {
    name: 'Volatility (II)',
    imageUrl: '/images/colosseum/volatility-2.webp',
    level: 'II',
  },
  [Handicap.VOLATILITY_3]: {
    name: 'Volatility (III)',
    imageUrl: '/images/colosseum/volatility-3.webp',
    level: 'III',
  },
};

type ColosseumHandicapProps = {
  handicap: Handicap;
  dimmed?: boolean;
};

export default function ColosseumHandicap(props: ColosseumHandicapProps) {
  const handicap = HANDICAPS[props.handicap] ?? {
    name: 'Unknown',
    imageUrl: '/images/huh.png',
  };

  let className = styles.handicap;
  if (props.dimmed) {
    className += ` ${styles.dimmed}`;
  }

  return (
    <div
      className={className}
      data-tooltip-id={GLOBAL_TOOLTIP_ID}
      data-tooltip-content={handicap.name}
    >
      <Image
        height={32}
        width={32}
        src={handicap.imageUrl}
        alt={handicap.name}
      />
      {handicap.level !== 'I' && (
        <span className={styles.level}>{handicap.level}</span>
      )}
    </div>
  );
}
