#ifndef POKEPLATINUM_PORT_CRASH_CONTEXT_H
#define POKEPLATINUM_PORT_CRASH_CONTEXT_H

#include "overlay_manager.h"

// The game's part of crash and stall reports in log.txt (libntr's sim_crash): the running application, the map
// and the player's position, plus the settings logged at startup. PLATINUM_DEBUG_CRASH triggers a test crash.
void CrashContext_Init(void);
// Once per main-loop frame, with the top-level application
void CrashContext_Update(const ApplicationManager *application);

#endif
