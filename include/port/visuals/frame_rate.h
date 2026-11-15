#ifndef POKEPLATINUM_PORT_FRAME_RATE_H
#define POKEPLATINUM_PORT_FRAME_RATE_H

#include <nitro/types.h>

// The 60 FPS setting runs the game's 30 Hz logic every frame, so code that counts
// frames scales its counts to keep the original speed.
#ifdef SDK_PORT
#include "port/config/sim_config_prj.h"

static inline BOOL FrameRate_Is60Fps(void)
{
    return SIM_Config_prj_GetConfig()->enable60fps;
}
#else
static inline BOOL FrameRate_Is60Fps(void)
{
    return FALSE;
}
#endif

// Updates per original 30 Hz frame: 2 at 60 FPS, otherwise 1.
static inline int FrameRate_Multiplier(void)
{
    return FrameRate_Is60Fps() ? 2 : 1;
}

static inline int FrameRate_Scale(int frames)
{
    return frames * FrameRate_Multiplier();
}

#endif
