import { ResolvingMetadata } from 'next';

import { MdxGuide } from '@/guides/mdx-guide';
import { basicMetadata } from '@/utils/metadata';

import { Mage, Melee, Range } from '../../nylos';
import RoleLinks from '../role-links';

import content from './content.mdx';

const extraComponents = { Mage, Melee, Range, RoleLinks };

export default function MeleeNyloGuide() {
  return <MdxGuide source={content} components={extraComponents} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'OSRS ToB 4s Nylocas Melee Guide - Waves & Rotations',
    description:
      'Step-by-step OSRS Theatre of Blood 4s Nylocas Melee guide: ' +
      'first-tick prefires, split priority, south/west cleanup, ' +
      'wave-by-wave rotations, inventory tips, and POV VOD.',
  });
}
