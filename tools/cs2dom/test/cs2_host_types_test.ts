import type {
    CS2Host,
    CS2HostRequest,
    CS2HostRequestPayloadByKind,
    CS2HostResult,
    CS2HostDynamicStackResult,
} from "../src/generated/cs2_host.js";

type Equal<Left, Right> =
    (<Value>() => Value extends Left ? 1 : 2) extends
    (<Value>() => Value extends Right ? 1 : 2) ? true : false;
type Expect<Value extends true> = Value;

type WidthIsNumber = Expect<Equal<CS2HostResult<"CC_GETWIDTH">, number>>;
type CreateIsRef = Expect<Equal<
    CS2HostResult<"CC_CREATE">,
    { readonly componentId: number; readonly subId?: number; readonly generation?: number } | null
>>;
type DbIsDynamic = Expect<Equal<
    CS2HostResult<"DB_GETFIELD">,
    CS2HostDynamicStackResult
>>;

declare const host: CS2Host;

const width: number = host.CC_GETWIDTH(0x000c0002);
host.CC_SETPOSITION(0x000c0002, 4, 8, 0, 0);
host.CC_SETHIDE(0x000c0002, true);
const created = host.CC_CREATE(0x000c0002, 0, 7, 1, 0, 0);
const children = host.IF_CHILDREN_COLLECT(0x000c0002, 0, 0);
const dbField = host.DB_GETFIELD(7502, 0, 0);

const reflected: CS2HostRequest<"PUSH_VAR"> = {
    kind: "PUSH_VAR",
    opcode: 1,
    payload: { varp_id: 8677 },
};

const hook: CS2HostRequestPayloadByKind["CC_SETONCLICK"] = {
    component_id: 0x000c0002,
    script_id: 42,
    signature: "isY",
    trigger_ids: [1, 2],
    trigger_count: 2,
    int_args: [7],
    int_arg_count: 1,
    str_arg_mask: [1, 0],
    str_arg_count: 1,
    str_args: ["bank"],
};

const componentParam: CS2HostRequestPayloadByKind["CC_GETCOMPONENTPARAM"] = {
    component_id: 0x000c0002,
    param_id: 2365,
    value: -1,
    str_value: null,
    value_kind: 0,
};

// @ts-expect-error bool C fields are logical booleans on the TypeScript API.
host.CC_SETHIDE(0x000c0002, 1);
// @ts-expect-error CC_GETWIDTH takes exactly one numeric component id.
host.CC_GETWIDTH("12:2");
const badRequest: CS2HostRequest<"PUSH_VAR"> = {
    kind: "PUSH_VAR",
    // @ts-expect-error the opcode literal must match the discriminant.
    opcode: 2,
    payload: { varp_id: 8677 },
};
const badHook: CS2HostRequestPayloadByKind["CC_SETONCLICK"] = {
    ...hook,
    // @ts-expect-error U64 request fields are explicit low/high word tuples.
    str_arg_mask: [1],
};

void [width, created, children, dbField, reflected, hook, componentParam, badRequest, badHook];
type TypeAssertions = WidthIsNumber | CreateIsRef | DbIsDynamic;
declare const assertions: TypeAssertions;
void assertions;
