/*
 * The types a component is written against.
 *
 * `Int` is the important one. A value that came from game state is not a number —
 * it is an expression the compiler will print as CS2 — but it takes the operators a
 * number takes, because src/transform.js rewrites them. Typing it as `number |
 * IntExpr` is what makes `energy / 100` check: TypeScript sees arithmetic on a
 * union it allows, and the build sees a division to emit.
 */

declare module 'cs2dom' {
    /** A value the compiler can print as a CS2 int expression. */
    export type IntExpr = number & { readonly __cs2domInt: unique symbol };
    export type StringExpr = string & { readonly __cs2domString: unique symbol };
    export type BoolExpr = boolean & { readonly __cs2domBool: unique symbol };

    export type Int = number | IntExpr;
    export type Str = string | StringExpr;
    export type Bool = boolean | BoolExpr;

    export type HAlign = 'left' | 'centre' | 'center' | 'right';
    export type VAlign = 'top' | 'centre' | 'center' | 'bottom';
    export type PosModeH = 'abs' | 'abs_left' | 'abs_centre' | 'abs_center' | 'abs_right' | 'proportional';
    export type PosModeV = 'abs' | 'abs_top' | 'abs_centre' | 'abs_center' | 'abs_bottom' | 'proportional';
    export type SizeMode = 'abs' | 'minus' | 'proportional';

    /** A recorded change; handlers return these rather than performing anything. */
    export interface Action { readonly __cs2dom: 'action'; }

    export type Handler = (...args: any[]) => Action | Action[] | void;

    interface Events {
        onOp?: (op: IntExpr) => Action | Action[] | void;
        onClick?: Handler;
        onClickRepeat?: Handler;
        onMouseOver?: Handler;
        onMouseLeave?: Handler;
        onMouseRepeat?: Handler;
        onHold?: Handler;
        onRelease?: Handler;
        onDrag?: Handler;
        onDragComplete?: Handler;
        onScrollWheel?: Handler;
        onTargetEnter?: Handler;
        onTargetLeave?: Handler;
        onTimer?: Handler;
        onLoad?: Handler;
    }

    interface Common extends Events {
        /** The block name in the .if, and how a handler refers to this component. */
        id?: string;
        x?: Int;
        y?: Int;
        xMode?: PosModeH;
        yMode?: PosModeV;
        width?: Int;
        height?: Int;
        widthMode?: SizeMode;
        heightMode?: SizeMode;
        hidden?: Bool;
        transparency?: Int;
        noClickThrough?: Bool;
        clickMask?: number;
        scrollWidth?: Int;
        scrollHeight?: Int;
        name?: string;
        targetVerb?: Str;
        /** Right-click options, in order, or [index, text] pairs. */
        ops?: (string | [number, string])[];
    }

    export interface LayerProps extends Common { children?: any; }
    export interface RectProps extends Common { color?: Int; fill?: Bool; alpha?: boolean; }
    export interface TextProps extends Common {
        text?: Str; font?: Int; color?: Int; shadow?: Bool;
        halign?: HAlign; valign?: VAlign; lineHeight?: Int; children?: Str;
    }
    export interface GraphicProps extends Common {
        sprite?: Int; angle?: Int; tiled?: Bool; alpha?: boolean; outline?: Int;
        shadow?: Int; color?: Int; hFlip?: Bool; vFlip?: Bool;
    }
    export interface ModelProps extends Common {
        model?: Int; zoom?: Int; xAngle?: Int; yAngle?: Int; zAngle?: Int;
        xOffset?: Int; yOffset?: Int; seq?: Int; orthographic?: Bool;
    }
    export interface LineProps extends Common { color?: Int; lineWidth?: Int; lineDirection?: Bool; }

    export const Layer: (props: LayerProps) => any;
    export const Rect: (props: RectProps) => any;
    export const Text: (props: TextProps) => any;
    export const Graphic: (props: GraphicProps) => any;
    export const Model: (props: ModelProps) => any;
    export const Line: (props: LineProps) => any;

    /** A server variable. Its transmit re-runs whatever reads it. */
    export function useVarp(id: number): IntExpr;
    /** A packed field of a server variable; the trigger is the containing varp. */
    export function useVarbit(id: number, options?: { varp: number }): IntExpr;
    /** A skill's current level. */
    export function useStat(id: number): IntExpr;
    /** How many of one item an inventory holds. */
    export function useInvCount(inv: number, obj: number): IntExpr;
    /**
     * Client-side state backed by a varc. The setter records a write; the compiler
     * appends the updates of everything that reads it to the same script.
     */
    export function useState(initial: number, options?: { varc: number }): [IntExpr, (value: Int) => Action];
    export function useState(initial: string, options?: { varc: number }): [StringExpr, (value: Str) => Action];

    export const actions: {
        hide(id: string, hidden?: Bool): Action;
        show(id: string): Action;
        set(id: string, prop: string, value: Int | Str | Bool): Action;
        /** Send the click to the server the way an ordinary interface button does. */
        button(op?: number): Action;
        runScript(id: number, args?: (Int | Str)[]): Action;
    };

    export const cs2: {
        toString(value: Int): StringExpr;
        min(a: Int, b: Int): IntExpr;
        max(a: Int, b: Int): IntExpr;
        scale(value: Int, numerator: Int, denominator: Int): IntExpr;
        enumLookup(keyType: string, valueType: string, id: number, key: Int): IntExpr;
        clientClock(): IntExpr;
    };
}

/*
 * The JSX factory the transform points at.
 *
 * TypeScript requires the classic factory to be in scope wherever a tag is
 * written, but a component never calls it by hand — src/transform.js supplies it —
 * so it is declared globally rather than imported into every file.
 */
declare const __jsx: (tag: unknown, props: unknown, ...children: unknown[]) => unknown;
declare const __fragment: (props: unknown, ...children: unknown[]) => unknown;

declare namespace JSX {
    interface IntrinsicElements {
        Layer: import('cs2dom').LayerProps;
        Rect: import('cs2dom').RectProps;
        Text: import('cs2dom').TextProps;
        Graphic: import('cs2dom').GraphicProps;
        Model: import('cs2dom').ModelProps;
        Line: import('cs2dom').LineProps;
    }
    interface Element { }
    interface ElementChildrenAttribute { children: {}; }
}
