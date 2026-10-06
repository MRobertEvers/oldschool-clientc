import { Metadata, ResolvingMetadata } from 'next';

import { MdxGuide } from '@/guides/mdx-guide';
import { basicMetadata } from '@/utils/metadata';

import content from './content.mdx';

export default function HumidGuide() {
  return <MdxGuide source={content} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
): Promise<Metadata> {
  return basicMetadata(await parent, {
    title: 'OSRS ToB Humid Bloat Guide',
    description:
      'OSRS Theatre of Blood Humid Bloat guide covering the 5t Humidify cycle, ' +
      'engine mechanics, and Alex and Vintage entries, with a video guide.',
  });
}
