'use client';

import Image from 'next/image';
import Link from 'next/link';
import { useState } from 'react';

import { ButtonLink } from '@/components/button';
import { formatMonthYear } from '@/utils/time';

import styles from './style.module.scss';

type SectionProps = {
  label: string;
  stage?: string;
  icon?: string;
  image?: { src: string; size: number };
  children: React.ReactNode;
};

function Section({ label, stage, icon, image, children }: SectionProps) {
  const [open, setOpen] = useState(true);
  const id = `guides-section-${label.toLowerCase().replace(/\W+/g, '-')}`;

  return (
    <>
      <div className={styles.nodeCell} data-stage={stage}>
        <div className={image ? styles.imageNode : styles.iconNode}>
          {image ? (
            <Image
              src={image.src}
              alt={label}
              width={image.size}
              height={image.size}
              style={{ objectFit: 'contain' }}
            />
          ) : (
            <i className={`fas ${icon}`} />
          )}
        </div>
      </div>
      <div className={styles.sectionBody} data-stage={stage}>
        <button
          className={styles.sectionToggle}
          onClick={() => setOpen(!open)}
          aria-expanded={open}
          aria-controls={id}
        >
          <span className={styles.sectionLabel}>{label}</span>
          <i
            className={`fas fa-chevron-down ${styles.chevron} ${
              open ? '' : styles.collapsed
            }`}
          />
        </button>
        {open && (
          <div className={styles.sectionContent} id={id}>
            {children}
          </div>
        )}
      </div>
    </>
  );
}

type GuideCardProps = {
  href: string;
  title: string;
  description: string;
  updated: Date;
  scale?: number;
};

function GuideCard({
  href,
  title,
  description,
  updated,
  scale,
}: GuideCardProps) {
  return (
    <Link className={styles.guideCard} href={href} data-scale={scale}>
      <div className={styles.guideCardHead}>
        <span className={styles.guideCardTitle}>{title}</span>
        <span className={styles.guideCardDesc}>{description}</span>
      </div>
      <div className={styles.updated}>Updated {formatMonthYear(updated)}</div>
    </Link>
  );
}

const NYLO_4S_LINKS: { role: string; label: string }[] = [
  { role: 'mage', label: 'Mage' },
  { role: 'melee-freeze', label: 'Melee Freeze' },
  { role: 'ranger', label: 'Ranger' },
  { role: 'melee', label: 'Melee' },
];

export default function GuideList() {
  return (
    <div className={styles.guideList}>
      <div className={styles.spine} />

      <Section label="General" icon="fa-plug">
        <GuideCard
          href="/guides/tob/plugins"
          title="Plugins"
          description="Recommended RuneLite plugins and optimal settings"
          updated={new Date('2026-02-07')}
        />
      </Section>

      <Section
        label="The Pestilent Bloat"
        stage="bloat"
        image={{ src: '/bloat.webp', size: 44 }}
      >
        <GuideCard
          href="/guides/tob/bloat/humid"
          title="Humid Bloat"
          description="How to humidify at Bloat, with all Alex and Vintage Bloat entries"
          updated={new Date('2026-09-20')}
        />
      </Section>

      <Section
        label="The Nylocas"
        stage="nylocas"
        image={{ src: '/nyloking.webp', size: 46 }}
      >
        <GuideCard
          href="/guides/tob/nylocas/mechanics"
          title="Room Mechanics"
          description="Learn how the Nylocas room functions."
          updated={new Date('2026-04-05')}
        />
        <div className={styles.waveGuides}>
          <GuideCard
            href="/guides/tob/nylocas/trio"
            title="Trio Waves"
            description="Detailed wave-by-wave strategy for all roles."
            updated={new Date('2026-07-17')}
            scale={3}
          />
          <div className={styles.guideCard} data-scale={4}>
            <div className={styles.guideCardHead}>
              <Link
                className={styles.guideCardTitle}
                href="/guides/tob/nylocas/4s"
              >
                4s Waves
              </Link>
              <span className={styles.guideCardDesc}>
                Breakdown of the 4 player meta by role.
              </span>
            </div>
            <div className={styles.roles}>
              {NYLO_4S_LINKS.map(({ role, label }) => (
                <ButtonLink
                  className={styles.role}
                  simple
                  fontSize="0.8rem"
                  key={role}
                  href={`/guides/tob/nylocas/4s/${role}`}
                  variant="neutral"
                >
                  <Image
                    src={`/images/guides/tob/${role}.png`}
                    alt={label}
                    width={20}
                    height={19}
                    unoptimized
                  />
                  {label}
                </ButtonLink>
              ))}
            </div>
            <div className={styles.updated}>
              Updated {formatMonthYear(new Date('2026-02-23'))}
            </div>
          </div>
        </div>
      </Section>

      <div className={styles.terminatorNode}>
        <div className={styles.terminatorDot} />
      </div>
      <div className={styles.terminatorLabel}>Other rooms coming soon</div>
    </div>
  );
}
