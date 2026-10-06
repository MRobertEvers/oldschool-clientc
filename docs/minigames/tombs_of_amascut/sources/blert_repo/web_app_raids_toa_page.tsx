import { ChallengeType } from '@blert/common';
import { Metadata } from 'next';
import Image from 'next/image';
import Link from 'next/link';

import Card from '@/components/card';
import { challengeLogo } from '@/logo';

import styles from './style.module.scss';

export default function Page() {
  return (
    <div className={styles.toaPage}>
      <Card primary className={styles.logo}>
        <Image
          src={challengeLogo(ChallengeType.TOA)}
          alt="The Tombs of Amascut"
          width={160}
          height={100}
          style={{ objectFit: 'contain' }}
        />
      </Card>

      <Card
        header={{ title: 'The Tombs of Amascut' }}
        className={styles.toaCard}
      >
        <div className={styles.content}>
          <div className={styles.mascot}>
            <Image
              src="/tobdataegirl_toa.png"
              alt="ToA Preview"
              height={234}
              width={256}
              style={{ objectFit: 'contain' }}
            />
          </div>

          <div className={styles.info}>
            <h2>Coming Soon!</h2>
            <p>
              We are adding raid recording support for the Tombs of Amascut
              soon! Stay tuned for updates.
            </p>

            <div className={styles.ctaSection}>
              <p>
                If you have any questions, would like to help out, provide
                feedback, or just want to chat, feel free to join our{' '}
                <Link
                  href="https://discord.gg/c5Hgv3NnYe"
                  target="_blank"
                  rel="noreferrer noopener"
                >
                  Discord Server
                </Link>
                !
              </p>
              <p>
                Also, check out our <Link href="/guides">Guides</Link> for other
                supported content!
              </p>
            </div>
          </div>
        </div>
      </Card>
    </div>
  );
}

export const metadata: Metadata = {
  title: 'Tombs of Amascut',
  description: 'The Tombs of Amascut raid is coming soon!',
};
