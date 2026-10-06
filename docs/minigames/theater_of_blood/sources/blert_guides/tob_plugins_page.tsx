import { ResolvingMetadata } from 'next';

import { basicMetadata } from '@/utils/metadata';

import { MdxGuide } from '../../mdx-guide';

import content from './content.mdx';

export default function PluginGuide() {
  return <MdxGuide source={content} />;
}

export async function generateMetadata(
  _props: object,
  parent: ResolvingMetadata,
) {
  return basicMetadata(await parent, {
    title: 'Best RuneLite Plugins for Theatre of Blood (OSRS Guide)',
    description:
      'Comprehensive OSRS Theatre of Blood plugin guide: optimize raids with ' +
      'Nyloer, ToB QoL, Party, Special Attack Counter, and other top RuneLite ' +
      'tools.',
  });
}
