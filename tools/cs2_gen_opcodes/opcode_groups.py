"""Logical CS2 opcode groups from the rev-239 client dispatcher.

The authoritative Java ladder is ``Statics.method6889`` in
``Deobfuscator/src_osrs239_rl1_12_33/deob/Statics.java``.  Re-derive its
boundaries with::

    sed -n '28060,28300p' Statics.java \
      | grep -oE 'var0 < [0-9]+|return method[0-9]+' \
      | paste - -

Opcodes below 100 are executed inline by ``Statics.method4464``.  Extended
opcodes are dispatched through the groups below.  A group may have two spans:
the 1000/2000 component pairs (and 1900/2900 actions) intentionally call the
same reference handler, selecting an active component for the lower span and
popping an explicit component for the upper span.

The broad spans after 4200 are not rounded guesses.  They preserve the exact
ladder, including empty leading centuries (for example chat is 4300..5099 even
though its implemented commands start at 5000).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable


@dataclass(frozen=True)
class OpcodeSpan:
    lo: int
    hi: int

    def contains(self, opcode: int) -> bool:
        return self.lo <= opcode < self.hi

    @property
    def label(self) -> str:
        return f"{self.lo}..{self.hi - 1}"


@dataclass(frozen=True)
class OpcodeGroup:
    enum_name: str
    slug: str
    summary: str
    spans: tuple[OpcodeSpan, ...]
    deob_handler: str

    def contains(self, opcode: int) -> bool:
        return any(span.contains(opcode) for span in self.spans)

    @property
    def label(self) -> str:
        return " / ".join(span.label for span in self.spans)


def _span(lo: int, hi: int) -> OpcodeSpan:
    return OpcodeSpan(lo, hi)


# Ordered by the reference dispatch, with shared-handler component pairs kept
# together.  Enum names and slugs are generated API: rename them deliberately.
OPCODE_GROUPS: tuple[OpcodeGroup, ...] = (
    OpcodeGroup("VM_CORE", "vm-core", "VM control flow, locals, vars and arrays", (_span(0, 100),), "inline"),
    OpcodeGroup("COMPONENT", "component", "component construction and addressing", (_span(100, 1000),), "method4548"),
    OpcodeGroup("COMPONENT_LAYOUT", "component-layout", "component position, size and visibility setters", (_span(1000, 1100), _span(2000, 2100)), "method5842"),
    OpcodeGroup("COMPONENT_APPEARANCE", "component-appearance", "component graphic, model and text setters", (_span(1100, 1200), _span(2100, 2200)), "method4754"),
    OpcodeGroup("COMPONENT_MODEL", "component-model", "component object and head-model setters", (_span(1200, 1300), _span(2200, 2300)), "method5661"),
    OpcodeGroup("COMPONENT_OP", "component-op", "component ops, dragging and key bindings", (_span(1300, 1400), _span(2300, 2400)), "method12438"),
    OpcodeGroup("COMPONENT_LISTENER", "component-listener", "component listener registration", (_span(1400, 1500), _span(2400, 2500)), "method4487"),
    OpcodeGroup("CC_GEOMETRY", "cc-geometry", "active-component geometry getters", (_span(1500, 1600),), "method1470"),
    OpcodeGroup("CC_APPEARANCE", "cc-appearance", "active-component appearance getters", (_span(1600, 1700),), "method6296"),
    OpcodeGroup("CC_INVENTORY", "cc-inventory", "active-component inventory and identity getters", (_span(1700, 1800),), "method12337"),
    OpcodeGroup("CC_TARGET", "cc-target", "active-component target and op getters", (_span(1800, 1900),), "method6843"),
    OpcodeGroup("COMPONENT_ACTION", "component-action", "component resize and trigger actions", (_span(1900, 2000), _span(2900, 3000)), "method1005"),
    OpcodeGroup("IF_GEOMETRY", "if-geometry", "explicit-component geometry getters", (_span(2500, 2600),), "method4787"),
    OpcodeGroup("IF_APPEARANCE", "if-appearance", "explicit-component appearance getters", (_span(2600, 2700),), "method8067"),
    OpcodeGroup("IF_INVENTORY", "if-inventory", "interface inventory, parent and identity getters", (_span(2700, 2800),), "method3056"),
    OpcodeGroup("IF_TARGET", "if-target", "explicit-component target and op getters", (_span(2800, 2900),), "method2"),
    OpcodeGroup("CLIENT", "client", "general client commands and preferences", (_span(3000, 3200),), "method6397"),
    OpcodeGroup("AUDIO_OPTIONS", "audio-options", "audio and client option commands", (_span(3200, 3300),), "method6695"),
    OpcodeGroup("CLIENT_STATE", "client-state", "client state, inventory, stats and coordinates", (_span(3300, 3400),), "method6548"),
    OpcodeGroup("ENUM", "enum", "enum lookups", (_span(3400, 3500),), "method6018"),
    OpcodeGroup("KEYBOARD", "keyboard", "keyboard state", (_span(3500, 3600),), "method6403"),
    OpcodeGroup("SOCIAL", "social", "friends, ignores and legacy clan chat", (_span(3600, 3700),), "method7997"),
    OpcodeGroup("UNUSED_3700", "unused-3700", "unhandled 3700-series commands", (_span(3700, 3800),), "method13645"),
    OpcodeGroup("CLAN", "clan", "clan settings and channels", (_span(3800, 3900),), "method4507"),
    OpcodeGroup("MARKET", "market", "Grand Exchange and trading-post commands", (_span(3900, 4000),), "method3202"),
    OpcodeGroup("MATH", "math", "integer maths and bit operations", (_span(4000, 4100),), "method2838"),
    OpcodeGroup("STRING", "string", "string operations", (_span(4100, 4200),), "method5814"),
    OpcodeGroup("OBJ", "obj", "object definitions and object search", (_span(4200, 4300),), "method2965"),
    OpcodeGroup("CHAT", "chat", "chat commands", (_span(4300, 5100),), "method11780"),
    OpcodeGroup("WINDOW", "window", "window-mode commands", (_span(5100, 5400),), "method9060"),
    OpcodeGroup("CAMERA", "camera", "camera commands", (_span(5400, 5600),), "method4488"),
    OpcodeGroup("LOGIN", "login", "logout and federated-login commands", (_span(5600, 5700),), "method6568"),
    OpcodeGroup("VIEWPORT", "viewport", "viewport, canvas, UI zoom and safe-area commands", (_span(5700, 6300),), "method6341"),
    OpcodeGroup("WORLD", "world", "world list, config params and platform commands", (_span(6300, 6600),), "method5150"),
    OpcodeGroup("WORLDMAP", "worldmap", "world-map and map-element commands", (_span(6600, 6700),), "method629"),
    OpcodeGroup("CLIENTOP_NPC", "clientop-npc", "client ops and active NPC queries", (_span(6700, 6800),), "method12492"),
    OpcodeGroup("CLIENTOP_LOC", "clientop-loc", "active location and object queries", (_span(6800, 6900),), "method3101"),
    OpcodeGroup("CLIENTOP_PLAYER", "clientop-player", "active player and login-state commands", (_span(6900, 7000),), "method6167"),
    OpcodeGroup("HIGHLIGHT", "highlight", "entity highlighting", (_span(7000, 7100),), "method8558"),
    OpcodeGroup("MINIMENU", "minimenu", "minimenu introspection", (_span(7100, 7200),), "method1135"),
    OpcodeGroup("OVERLAY", "overlay", "entity overlays, minimap and native extension commands", (_span(7200, 7500),), "method12357"),
    OpcodeGroup("DB", "db", "client database commands", (_span(7500, 7600),), "method870"),
    OpcodeGroup("LOOT", "loot", "loot-tracker native commands", (_span(7600, 7700),), "method10020"),
    OpcodeGroup("EXTENSION", "extension", "native extension and client-setting commands", (_span(7700, 8000),), "method11128"),
    OpcodeGroup("ARRAY", "array", "typed list and array commands", (_span(8000, 8100),), "method12336"),
    OpcodeGroup("TYPED_DATA", "typed-data", "typed long-keyed data commands", (_span(8100, 8600),), "method4514"),
    OpcodeGroup(
        "OP_COUNT",
        "op-count",
        "interpreter operation-count introspection",
        (_span(13000, 14000),),
        "method6817",
    ),
)


def validate_groups(groups: Iterable[OpcodeGroup] = OPCODE_GROUPS) -> None:
    """Reject invalid or overlapping spans before generating C tables."""

    occupied: list[tuple[int, int, str]] = []
    enum_names: set[str] = set()
    slugs: set[str] = set()
    for group in groups:
        if group.enum_name in enum_names:
            raise ValueError(f"duplicate opcode-group enum name: {group.enum_name}")
        if group.slug in slugs:
            raise ValueError(f"duplicate opcode-group slug: {group.slug}")
        enum_names.add(group.enum_name)
        slugs.add(group.slug)
        if not group.spans:
            raise ValueError(f"opcode group {group.enum_name} has no spans")
        for span in group.spans:
            if span.lo < 0 or span.hi <= span.lo:
                raise ValueError(f"invalid opcode span {span.lo}..{span.hi} in {group.enum_name}")
            occupied.append((span.lo, span.hi, group.enum_name))

    occupied.sort()
    for left, right in zip(occupied, occupied[1:]):
        if left[1] > right[0]:
            raise ValueError(
                f"opcode groups overlap: {left[2]} {left[0]}..{left[1] - 1} and "
                f"{right[2]} {right[0]}..{right[1] - 1}"
            )


def group_for_opcode(opcode: int) -> OpcodeGroup | None:
    for group in OPCODE_GROUPS:
        if group.contains(opcode):
            return group
    return None


def span_for_opcode(opcode: int) -> tuple[OpcodeGroup, OpcodeSpan] | None:
    for group in OPCODE_GROUPS:
        for span in group.spans:
            if span.contains(opcode):
                return group, span
    return None


validate_groups()
