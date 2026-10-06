import Image from 'next/image';
import Link from 'next/link';

import styles from './style.module.scss';

type Role = 'mage' | 'melee-freeze' | 'ranger' | 'melee';

type RoleLinksProps = {
  active?: Role;
};

function RoleLink({ role, active }: { role: Role; active: boolean }) {
  const roleName =
    role === 'melee-freeze'
      ? 'Melee Freeze'
      : role[0].toUpperCase() + role.slice(1);

  let className = styles.link;
  if (active) {
    className += ` ${styles.active}`;
  }

  return (
    <Link className={className} href={`/guides/tob/nylocas/4s/${role}`}>
      <Image
        src={`/images/guides/tob/${role}.png`}
        alt={roleName}
        width={32}
        height={31}
        unoptimized
      />
      {roleName}
    </Link>
  );
}

export default function RoleLinks({ active }: RoleLinksProps) {
  return (
    <div className={styles.roleLinks}>
      <RoleLink role="mage" active={active === 'mage'} />
      <RoleLink role="melee-freeze" active={active === 'melee-freeze'} />
      <RoleLink role="ranger" active={active === 'ranger'} />
      <RoleLink role="melee" active={active === 'melee'} />
    </div>
  );
}
