import { ChallengeType } from '@blert/common';
import { Metadata, ResolvingMetadata } from 'next';
import Image from 'next/image';

import Card from '@/components/card';
import { challengeLogo } from '@/logo';
import { basicMetadata } from '@/utils/metadata';

import GuideList from './guide-list';

import guideStyles from '../style.module.scss';
import styles from './style.module.scss';

export default function TobGuides() {
  return (
    <div className={guideStyles.guides}>
      <Card primary fixed>
        <div className={styles.titleRow}>
          <div className={styles.titleLogo}>
            <Image
              src={challengeLogo(ChallengeType.TOB)}
              alt="Theatre of Blood"
              height={36}
              width={36}
              style={{ objectFit: 'contain' }}
            />
          </div>
          <h1 className={styles.title}>Theatre of Blood Guides</h1>
        </div>
      </Card>
      <Card fixed>
        <GuideList />
      </Card>
    </div>
  );
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
): Promise<Metadata> {
  return basicMetadata(await parent, {
    title: 'OSRS Theatre of Blood Guides & Strategies',
    description:
      "Browse Blert's growing collection of OSRS Theatre of Blood guides, " +
      'including plugins, boss mechanics, room strategies, meta roles, ' +
      'and advanced tactics.',
  });
}
