#ifndef OSRS_CORE_CLIENTPROT_CORE_H
#define OSRS_CORE_CLIENTPROT_CORE_H

/* clientprot_core — retired unified facade.
 *
 * This header is intentionally empty.  The unified clientprot_core_emit()
 * facade, enum ClientProtOpKind, and CPArgs_* structs have been replaced by
 * the four-layer per-op pipeline in gamenet_send.h / gamenet_core_send.h /
 * clientprot_send.h.
 *
 * Files that included this header should now include osrs/gamenet_send.h
 * for outbound packet dispatch.
 */

#endif /* OSRS_CORE_CLIENTPROT_CORE_H */
