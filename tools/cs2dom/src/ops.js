/*
 * The cs2-dom operation vocabulary: what a running client can be told to change
 * about a component, and the argument order each command wants.
 *
 * This is the layer the whole design turns on. The compiler writes these calls into
 * generated CS2; a live editing session would issue the same operations through the
 * VM's host requests (src/cs2vm2/cs2vm2_host.h has one CS2VM_HOST_REQUEST_IF_* per
 * entry below). One vocabulary, two backends — so whatever a preview does to a
 * component is something a bake can reproduce.
 *
 * `args` lists prop names in the order the command takes them, with the target
 * component appended last. Multi-prop entries are why the emitter cannot apply one
 * prop at a time: if_setposition carries x, y and both modes together, so changing
 * `y` alone still has to send the other three, taken from the node's static values.
 *
 * Argument orders are read off the shipped corpus (OSRS-Content/osrs239-content/
 * scripts), not guessed — and every entry is compiled by test/run_tests.js through
 * the real CS2 compiler, so a wrong arity fails the build rather than the client.
 */

export const OPS = {
    if_setposition: { args: ['x', 'y', 'xMode', 'yMode'] },
    if_setsize: { args: ['width', 'height', 'widthMode', 'heightMode'] },
    if_setscrollsize: { args: ['scrollWidth', 'scrollHeight'] },
    if_sethide: { args: ['hidden'] },
    if_settrans: { args: ['transparency'] },
    if_setnoclickthrough: { args: ['noClickThrough'] },
    if_settargetverb: { args: ['targetVerb'] },

    if_settext: { args: ['text'] },
    if_settextfont: { args: ['font'] },
    if_settextalign: { args: ['halign', 'valign', 'lineHeight'] },
    if_settextshadow: { args: ['shadow'] },
    if_setcolour: { args: ['color'] },

    if_setgraphic: { args: ['sprite'] },
    if_set2dangle: { args: ['angle'] },
    if_settiling: { args: ['tiled'] },
    if_setoutline: { args: ['outline'] },
    if_setgraphicshadow: { args: ['shadow'] },
    if_sethflip: { args: ['hFlip'] },
    if_setvflip: { args: ['vFlip'] },
    if_setfill: { args: ['fill'] },

    if_setlinewid: { args: ['lineWidth'] },
    if_setlinedirection: { args: ['lineDirection'] },

    if_setmodel: { args: ['model'] },
    if_setmodelanim: { args: ['seq'] },
    if_setmodelorthog: { args: ['orthographic'] },
    /* Six values in one call: offsets, then angles, then zoom. */
    if_setmodelangle: { args: ['xOffset', 'yOffset', 'xAngle', 'yAngle', 'zAngle', 'zoom'] },

    /* Op text is indexed, so it does not follow the prop pattern — the emitter
     * builds these directly from the `ops` prop. */
    if_setop: { args: null, indexed: true },
};

/** Setting a variable is a statement, not a command call — spelled by kind. */
export const VARIABLE_SYNTAX = {
    varp: (id) => `%var${id}`,
    varbit: (id) => `%varbit${id}`,
    varc: (id) => `%varcint${id}`,
    varcstr: (id) => `%varcstring${id}`,
};

/** Reading state that is not a variable needs a command. */
export const STATE_READ = {
    stat: (id) => `stat_base(${id})`,
};
