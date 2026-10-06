import { ResolvingMetadata } from 'next';

import { MdxGuide } from '@/guides/mdx-guide';
import { basicMetadata } from '@/utils/metadata';

import { Mage, Melee, Range } from '../../nylos';
import RoleLinks from '../role-links';

import content from './content.mdx';

const extraComponents = { Mage, Melee, Range, RoleLinks };

export default function RangerNyloGuide() {
  return <MdxGuide source={content} components={extraComponents} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'OSRS ToB 4s Nylocas Ranger Guide - Waves & Rotations',
    description:
      'Step-by-step OSRS Theatre of Blood 4s Nylocas Ranger guide: on-tick prefires, ' +
      'chin/bow/blowpipe rotations, wave timings, cleanup tips, and POV VOD.',
  });
}
