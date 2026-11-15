#ifndef POKEPLATINUM_PORT_ASSERTS_H
#define POKEPLATINUM_PORT_ASSERTS_H

#include <nitro/types.h>

#include "port/config/sim_config_prj.h"

// Whether a failed GF_ASSERT raises a communication error, and whether it stops in the debugger first.
static inline BOOL PortAsserts_Enabled(void)
{
    return SIM_Config_prj_GetConfig()->enableAsserts;
}

static inline BOOL PortAsserts_BreakDebugger(void)
{
    return SIM_Config_prj_GetConfig()->breakDebuggerOnGfAssert;
}

#endif
