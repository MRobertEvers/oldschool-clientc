import { ResolvingMetadata } from 'next';

import { MdxGuide } from '@/guides/mdx-guide';
import { basicMetadata } from '@/utils/metadata';

import { Mage, Range } from '../../nylos';
import RoleLinks from '../role-links';

import content from './content.mdx';

const extraComponents = { Mage, Range, RoleLinks };

export default function MageNyloGuide() {
  return <MdxGuide source={content} components={extraComponents} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'OSRS ToB 4s Nylocas Mage Guide - Waves & Rotations',
    description:
      'Step-by-step OSRS Theatre of Blood 4s Nylocas Mage guide: optimal prefires, ' +
      'barrage stacks, wave-by-wave rotations, plugin tips, and POV VOD.',
  });
}
