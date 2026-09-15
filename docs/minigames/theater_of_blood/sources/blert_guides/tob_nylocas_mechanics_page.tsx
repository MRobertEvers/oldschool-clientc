import {
  ChallengeType,
  LaneSpawnJson,
  NYLOCAS_WAVES,
  NyloJson,
  NyloStyleJson,
} from '@blert/common';
import { ResolvingMetadata } from 'next';
import Link from 'next/link';

import Article from '@/components/article';
import { GLOBAL_TOOLTIP_ID } from '@/components/tooltip';
import GuideTags from '@/guides/guide-tags';
import { basicMetadata } from '@/utils/metadata';

import styles from './style.module.scss';

export default function NyloMechanics() {
  return (
    <Article.Page>
      <Article.Heading level={1} text="Nylocas Room Mechanics" />
      <GuideTags
        challenge={ChallengeType.TOB}
        scale="all"
        level="intermediate"
      />
      <Article.Heading level={2} text="Overview" />
      <p>
        This guide explains the underlying mechanics of the Nylocas room,
        detailing how its waves and boss function. Understanding these mechanics
        provides the necessary background knowledge to recognize how and why
        strategies are built and executed.
      </p>
      <p>
        Role-specific strategy guides for the current meta across different
        scales can be found on the <Link href="/guides/tob">ToB guides</Link>{' '}
        page.
      </p>
      <Article.Heading level={2} text="Waves" />
      <p>
        There are 31 waves of Nylocas, which are always the same across
        encounters. Each wave spawns up to 6 Nylocas across the three lanes
        (east, south, and west) leading to the center of the room.
      </p>
      <p>
        The Nylocas spawned in each wave are detailed in{' '}
        <Article.Appendix.Ref id="waves" />.
      </p>
      <Article.Appendix.Define id="waves" title="Waves">
        <p>
          The table below lists the locations and styles of Nylocas spawned in
          each wave.
        </p>
        <strong>Legend:</strong>
        <span className={styles.legend}>
          <span className={styles.legendItem}>
            <NyloChips n={{ rotation: ['melee'], big: false, aggro: false }} />{' '}
            Melee
          </span>
          <span className={styles.legendItem}>
            <NyloChips n={{ rotation: ['magic'], big: false, aggro: false }} />{' '}
            Magic
          </span>
          <span className={styles.legendItem}>
            <NyloChips n={{ rotation: ['ranged'], big: false, aggro: false }} />{' '}
            Ranged
          </span>
          <span className={styles.legendItem}>
            <NyloChips n={{ rotation: ['magic'], big: true, aggro: false }} />{' '}
            Big
          </span>
          <span className={styles.legendItem}>
            <NyloChips n={{ rotation: ['magic'], big: false, aggro: true }} />{' '}
            Aggro
          </span>
          <span className={styles.legendItem}>
            <NyloChips
              n={{
                rotation: ['ranged', 'melee', 'ranged'],
                big: false,
                aggro: false,
              }}
            />{' '}
            Flicker
          </span>
        </span>
        <table className={styles.wavesTable}>
          <thead>
            <tr>
              <th></th>
              <th colSpan={2}>East</th>
              <th colSpan={2}>South</th>
              <th colSpan={2}>West</th>
            </tr>
            <tr>
              <th>Wave</th>
              <th>North</th>
              <th>South</th>
              <th>East</th>
              <th>West</th>
              <th>North</th>
              <th>South</th>
            </tr>
          </thead>
          <tbody>
            {NYLOCAS_WAVES.map((wave) => {
              function spawn(lane: LaneSpawnJson) {
                if (lane[1] === null) {
                  return (
                    <td colSpan={2}>
                      {lane[0] ? (
                        <NyloChips n={lane[0]} />
                      ) : (
                        <span className={styles.emptySpawn}>-</span>
                      )}
                    </td>
                  );
                }

                return (
                  <>
                    <td>
                      <NyloChips n={lane[0]} />
                    </td>
                    <td>
                      <NyloChips n={lane[1]} />
                    </td>
                  </>
                );
              }

              return (
                <tr key={wave.num}>
                  <td>{wave.num}</td>
                  {spawn(wave.east)}
                  {spawn(wave.south)}
                  {spawn(wave.west)}
                </tr>
              );
            })}
          </tbody>
        </table>
      </Article.Appendix.Define>
      <Article.Heading level={3} text="Nylocas" />
      <Article.Heading level={4} text="Styles" />
      <p>
        Nylocas spawn as one of three styles&mdash;magic, ranged, or
        melee&mdash;and in two sizes: small and big. When big Nylocas die, they
        spawn two small Nylocas (called <em>splits</em> to distinguish them from
        wave spawns), each of a random style.
      </p>
      <p>
        Nylocas can only be damaged by attacks of the same style as their
        current style. If a player attacks a Nylocas with the wrong style, that
        player can no longer damage it. This is called <em>nulling</em>. In Hard
        Mode, attacks with the wrong style additionally reflect damage back to
        the player.
      </p>
      <p>
        Some Nylocas <em>flicker</em>&mdash;change style&mdash;as they walk down
        their lane. This starts at wave 16, and every following wave has at
        least one Nylocas that flickers
        <Article.Appendix.Ref id="waves" variant="sup" />. Flickers switch style
        after passing the lane&apos;s halfway point, hold that style for 2
        ticks, then switch to their final style (which may match the original).
      </p>
      <Article.Heading level={4} text="Behavior" />
      <p>
        After spawning, most Nylocas path toward one of the four pillars in the
        room to attack it. Each wave-spawned Nylocas always targets the same
        pillar in every encounter. If a pillar is destroyed, its collapse deals
        significant damage to all players in the room, and the Nylocas that were
        attacking it start attacking players instead.
      </p>
      <p>
        Some wave-spawned Nylocas attack players instead of pillars. These are
        called <em>aggros</em>. As with all spawns, aggros are fixed across
        encounters. Splits from bigs cannot be aggros.
      </p>
      <p>
        All Nylocas (wave-spawned and splits) naturally explode once they have
        been alive for 52 ticks (31.2 seconds). This explosion damages players
        in a radius around the Nylocas.
      </p>
      <Article.Heading level={4} text="Demi-bosses" />
      <p>
        In Hard Mode, waves 10, 20, and 30 spawn a Nylocas Prinkipas (
        <em>demi/prince</em>) alongside their regular Nylocas. Demi-bosses have
        the same mechanics as the main boss. Refer to the{' '}
        <Link href="#nylocas-vasilias">Nylocas Vasilias</Link> section for
        details.
      </p>
      <Article.Heading level={3} text="Room cap" />
      <p>
        There is a limit to how many Nylocas can be in the room at once. This is
        called the <em>room cap</em>. From waves 1&ndash;19, the cap is 12 in
        Regular Mode and 15 in Hard Mode. At wave 20, the room cap increases to
        24 in both modes. Players refer to this as the <em>cap increase</em>{' '}
        wave.
      </p>
      <p>
        All regular Nylocas&mdash;small and big, wave-spawned and
        splits&mdash;count as 1 toward the room cap. Demi-bosses count as 3
        Nylocas.
      </p>
      <p>
        If the team is at or above the room cap, new waves of Nylocas do not
        spawn. A failed spawn is known as a <em>stall</em>, which is explained
        further in the <Link href="#stalls">Stalls</Link> section.
      </p>
      <p>
        As the cap is dramatically different before and after wave 20,
        strategies used by teams change between these two phases. Players call
        these phases the <em>pre-cap</em> and <em>post-cap</em>.
      </p>
      <p>
        The room cap counts Nylocas from the moment they spawn to the moment
        they despawn&mdash;not their death. Death animations can be delayed by
        various factors, which can influence whether the cap is exceeded or not.
        Death animation timing is explained in{' '}
        <Article.Appendix.Ref id="death-timing" />.
      </p>
      <Article.Heading level={3} text="Room cycle" />
      <p>
        The Nylocas room operates on a repeating 4-tick (2.4 second) cycle.
        Understanding this cycle is essential to running the room efficiently.
      </p>
      <Article.Heading level={4} text="Cycle definition" />
      <p>
        The Nylocas instance uses an absolute tick counter beginning when the
        first player enters the Nylocas area from the Bloat room. Wave checks
        are offset by 2 ticks from the absolute tick, so checks occur on ticks
        2, 6, 10, 14, ...
      </p>
      <p>
        In practice, plugins and players track a room cycle relative to the wave
        checks, where cycle tick 0 corresponds to absolute tick 2. The cycle
        then repeats every 4 ticks:
      </p>
      <table className={styles.cycleTable}>
        <thead>
          <tr>
            <th>Absolute</th>
            <th>0</th>
            <th>1</th>
            <th className={styles.spawnTick}>2</th>
            <th>3</th>
            <th>4</th>
            <th>5</th>
            <th className={styles.spawnTick}>6</th>
            <th>7</th>
            <th>8</th>
            <th>9</th>
            <th className={styles.spawnTick}>10</th>
            <th>11</th>
            <th>12</th>
            <th>13</th>
            <th className={styles.spawnTick}>14</th>
            <th>15</th>
            <th>16</th>
            <th>...</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td>Cycle</td>
            <td>2</td>
            <td>3</td>
            <td className={styles.spawnTick}>0</td>
            <td>1</td>
            <td>2</td>
            <td>3</td>
            <td className={styles.spawnTick}>0</td>
            <td>1</td>
            <td>2</td>
            <td>3</td>
            <td className={styles.spawnTick}>0</td>
            <td>1</td>
            <td>2</td>
            <td>3</td>
            <td className={styles.spawnTick}>0</td>
            <td>1</td>
            <td>2</td>
            <td>...</td>
          </tr>
        </tbody>
      </table>
      <Article.Heading level={4} text="Stalls" />
      <p>
        Waves only ever attempt to spawn on cycle tick 0. The first wave always
        spawns on the second cycle after starting the encounter. Due to this,
        starting on cycle tick 0 is optimal, as it allows the first wave to
        spawn on the next tick 0 (the fourth tick of the room).
      </p>
      <p>Two things can delay (stall) the next wave:</p>
      <ul>
        <li>
          <em>Natural stalls</em> are a built-in delay between waves to give
          players time to deal with the previous wave, expressed as a multiple
          of cycles. A new wave does not spawn until the previous wave&apos;s
          natural stall has passed. The natural stalls for each wave are listed
          in <Article.Appendix.Ref id="natural-stalls" />.
        </li>
        <li>
          <em>Room cap stalls</em> (always referred to as just
          &quot;stalls&quot;) occur if the room is at or above the room cap on
          the tick a wave is due to spawn. When this occurs, the wave does not
          spawn, and the room will check the cap again on the next cycle tick 0.
        </li>
      </ul>
      <Article.Appendix.Define id="natural-stalls" title="Natural Stalls">
        <p>
          The table below lists the natural number of ticks and cycles between
          each wave.
        </p>
        <p>
          <strong>Note:</strong> In Hard Mode, demi-boss waves (10, 20, 30)
          always have a natural stall of 16 ticks (4 cycles).
        </p>
        <table>
          <thead>
            <tr>
              <th>Wave</th>
              <th>Natural stall ticks</th>
              <th>Natural stall cycles</th>
            </tr>
          </thead>
          <tbody>
            {NYLOCAS_WAVES.slice(0, 30).map(({ num, naturalStall }) => {
              const cycles = Math.floor(naturalStall / 4);

              return (
                <tr key={num}>
                  <td>{num}</td>
                  <td>{naturalStall}</td>
                  <td>{cycles}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </Article.Appendix.Define>
      <p>
        The cycle of the room can be observed using the{' '}
        <Link href="https://runelite.net/plugin-hub/show/tobqol">ToB QoL</Link>{' '}
        plugin by ticking the <em>Instance Timer</em> option under the{' '}
        <em>Nylocas</em> section. It displays the current cycle tick (0-3)
        before entering the room, allowing players to start the room on the
        optimal tick 0.
      </p>
      <Article.Heading level={3} text="Avoiding stalls" />
      <p>
        Proficient teams manage the room cap to avoid stalling by prioritizing
        which Nylocas to kill and timing those kills around the wave spawn
        checks. Wave management is often less about total DPS and more about
        controlling when new Nylocas spawn and despawn relative to the checks.
      </p>
      <p>
        During pre-cap, teams are strategic about which Nylocas to attack and
        which to ignore. In post-cap, the focus generally shifts to killing bigs
        quickly to get their splits out early.
      </p>
      <p>Several techniques are common among Nylocas waves strategies:</p>
      <ul>
        <li>
          Prioritizing newer Nylocas and allowing older ones to expire
          naturally.
        </li>
        <li>
          Timing the popping of bigs such that their splits appear right after a
          spawn check, giving the most time to deal with the splits before the
          next check.
        </li>
        <li>
          Prefiring Nylocas in the lane from specific positions to control
          projectile travel time, ensuring the despawn occurs at a desired time
          <Article.Appendix.Ref id="death-timing" variant="sup" />.
        </li>
        <li>
          Particularly in pre-cap, playing around natural stalls by popping bigs
          when there is a longer delay between waves.
        </li>
        <li>Managing weapon attack cooldowns to align with the room cycle.</li>
      </ul>
      <Article.Heading level={2} text="Nylocas Vasilias" />
      <p>
        After the waves, Nylocas Vasilias (often called &quot;boss&quot; or
        &quot;Nylo King&quot;) spawns. The spawn occurs on the start of the{' '}
        <Article.Tooltip text="4 full cycles is 16t (9.6s). As the final death can occur on any cycle tick, up to 3 additional ticks could pass, giving a maximum spawn time of 19t (11.4s).">
          5th cycle
        </Article.Tooltip>{' '}
        following the despawn of the last regular Nylocas.
      </p>
      <Article.Heading
        level={3}
        text="Style rotation"
        idPrefix="nylocas-vasilias"
      />
      <p>
        Nylocas Vasilias switches between all three combat styles. It always
        begins as melee, and switches to a different random style every 10 ticks
        (6 seconds).
      </p>
      <p>
        Like regular Nylocas, the boss can only be damaged by attacks of the
        same style as them. Wrong style attacks heal the boss and reflect the
        damage back to the player.
      </p>
      <Article.Heading level={3} text="Attacks" idPrefix="nylocas-vasilias" />
      <p>
        In Regular Mode, Nylocas Vasilias uses an auto attack matching his
        current style, attacking twice per phase. Each attack targets a single
        player at random.
      </p>
      <Article.Heading level={3} text="Death" idPrefix="nylocas-vasilias" />
      <p>
        The room ends at the start of the cycle following Nylocas Vasilias&apos;
        despawn. As with regular Nylocas, death timing nuances
        <Article.Appendix.Ref id="death-timing" variant="sup" /> apply.
      </p>
      <Article.Appendix.Define
        id="death-timing"
        title="Death Animations & Timing"
      >
        <p>
          Every NPC has a fixed-length death animation, but the delay between an
          attack that kills the NPC, its hitsplat (HP reaching 0), and the start
          of the animation varies. Several factors influence this:
        </p>
        <ul>
          <li>
            Weapon type: Melee weapons apply their damage immediately on the
            following tick; ranged/magic weapons have projectile travel time.
          </li>
          <li>
            Projectile travel: Travel time varies with weapon and distance to
            the target.
          </li>
          <li>
            NPC movement: If an NPC is walking when it dies, it first stops
            walking before starting its death animation.
          </li>
        </ul>
        <p>
          The following table lists the specific delays for the Nylocas. In the
          examples, <span className={styles.mono}>t</span> refers to the death
          tick when HP reaches 0. Small Nylocas have two death animations: first
          a &quot;turn&quot; animation, then the main death animation. This
          second animation is shown via a game object, not the NPC itself; the
          despawn happens after the first animation.
        </p>
        <table>
          <thead>
            <tr>
              <th>Nylo size</th>
              <th>Cause</th>
              <th>Death anim length</th>
              <th>Notes</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td>Big</td>
              <td>Killed by player</td>
              <td>5t</td>
              <td>
                Stationary: anim <span className={styles.mono}>t+1</span>,
                despawn <span className={styles.mono}>t+6</span>.
                <br />
                Walking: stop on <span className={styles.mono}>t+1</span>, anim
                starts <span className={styles.mono}>t+2</span>, despawn{' '}
                <span className={styles.mono}>t+7</span>
              </td>
            </tr>
            <tr>
              <td>Big</td>
              <td>Natural explosion</td>
              <td>3t</td>
              <td>
                Animation begins on lifetime tick 53, despawn on 56; smalls
                spawn on despawn.
              </td>
            </tr>
            <tr>
              <td>Small</td>
              <td>Killed by player</td>
              <td>1t</td>
              <td>
                Stationary: anim <span className={styles.mono}>t+1</span>,
                despawn <span className={styles.mono}>t+2</span>.
                <br />
                Walking: stop and &quot;turn&quot; anim occur on the same tick{' '}
                <span className={styles.mono}>t+1</span>, despawn{' '}
                <span className={styles.mono}>t+2</span>.
              </td>
            </tr>
            <tr>
              <td>Small</td>
              <td>Natural explosion</td>
              <td>1t</td>
              <td>Animation begins on lifetime tick 52, despawn on 53.</td>
            </tr>
          </tbody>
        </table>
      </Article.Appendix.Define>
    </Article.Page>
  );
}

function StyleChip({ rotation }: { rotation: NyloStyleJson[] }) {
  return (
    <span className={styles.chip} aria-label={rotation.join(' then ')}>
      {rotation.map((s, i) => (
        <span key={i} className={`${styles.seg} ${styles[s]}`} />
      ))}
    </span>
  );
}

function NyloChips({ n }: { n: NyloJson }) {
  const classes = [styles.nylo, n.big && styles.big, n.aggro && styles.aggro]
    .filter(Boolean)
    .join(' ');
  const big = n.big ? ' big' : '';
  const aggro = n.aggro ? ' aggro' : '';
  const tooltip = `${n.rotation.join('-')}${big}${aggro}`;
  return (
    <span
      className={classes}
      data-tooltip-id={GLOBAL_TOOLTIP_ID}
      data-tooltip-content={tooltip}
    >
      <StyleChip rotation={n.rotation} />
    </span>
  );
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'OSRS ToB Nylocas Room Mechanics',
    description:
      'Learn the core OSRS Theatre of Blood Nylocas room mechanics: waves, room cap, stalls, cycle timing, and boss spawn explained in detail.',
  });
}
