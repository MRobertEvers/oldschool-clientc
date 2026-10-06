import styles from './nylos.module.scss';

export function Mage({ children }: { children: React.ReactNode }) {
  return <span className={styles.mage}>{children}</span>;
}

export function Range({ children }: { children: React.ReactNode }) {
  return <span className={styles.range}>{children}</span>;
}

export function Melee({ children }: { children: React.ReactNode }) {
  return <span className={styles.melee}>{children}</span>;
}
