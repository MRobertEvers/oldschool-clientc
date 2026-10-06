import { ResolvingMetadata } from 'next';

import { basicMetadata } from '@/utils/metadata';

import { MdxGuide } from '../../../mdx-guide';
import { Mage, Melee, Range } from '../nylos';

import content from './content.mdx';

const nyloComponents = { Mage, Range, Melee };

export default function TrioNylo() {
  return <MdxGuide source={content} components={nyloComponents} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'OSRS ToB Trio Nylocas Guide - Mage, Range & Melee Rotations',
    description:
      'Complete Theatre of Blood Trio Nylocas guide with optimal mage, range, ' +
      'and melee rotations, wave timings, and strategies for max efficiency runs.',
  });
}
