/*
 * The run-energy orb: a number that follows a server variable, and a button that
 * changes colour when the energy runs low.
 *
 * Everything here is decided at build time except the three props that read
 * `energy`. Those become one generated script bound to the energy varp's
 * transmit; the
 * rest is fields in the .if and costs nothing at runtime.
 */

import { Layer, Graphic, Text, useVarp, actions } from 'cs2dom';
import { fonts, sprites, varps } from './cache.gen';

const LOW_ENERGY = 20;
const AMBER = 0xff981f;
const GREEN = 0x00ff00;

export default function RunOrb() {
    const energy = useVarp(varps.sa_energy);
    const percent = energy / 100;
    const low = percent <= LOW_ENERGY;

    return (
        <Layer id="root" width={57} height={35}>
            <Text
                id="readout"
                x={0}
                y={5}
                width={24}
                height={13}
                font={fonts.p11_full}
                color={low ? AMBER : GREEN}
                halign="right"
                valign="centre"
                shadow
            >
                {`Dog ${percent}`}
            </Text>

            <Layer
                id="button"
                x={27}
                width={26}
                height={26}
                ops={['Toggle Run']}
                onOp={() => actions.button(1)}
            >
                <Graphic
                    id="orb"
                    width={26}
                    height={26}
                    sprite={low ? sprites.orb_icon_3 : sprites.orb_icon_4}
                />
            </Layer>
        </Layer>
    );
}
