#ifndef CLIENTPROT_REV245_2_PACKETS_H
#define CLIENTPROT_REV245_2_PACKETS_H

/* Rev 245_2 client-to-server opcode table.
 * Moved here from src/osrs/packetout.h; packetout.h now includes this file
 * for backwards compatibility. */

#include "osrs/packetout.h"

/* Additional outbound opcodes not yet in packetout.h */
#define PKTOUT_LC245_2_EVENT_MOUSE_MOVE    236 /* index: 1 */
#define PKTOUT_LC245_2_EVENT_MOUSE_CLICK   241 /* index: 2 */
#define PKTOUT_LC245_2_EVENT_APPLET_FOCUS  127 /* index: 3 */
#define PKTOUT_LC245_2_EVENT_CAMERA        189 /* index: 4 */
#define PKTOUT_LC245_2_MAP_BUILD_COMPLETE  253 /* index: 5 */
#define PKTOUT_LC245_2_SEND_SNAPSHOT       230 /* index: 44 */

#endif /* CLIENTPROT_REV245_2_PACKETS_H */
