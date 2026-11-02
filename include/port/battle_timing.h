#ifndef POKEPLATINUM_PORT_BATTLE_TIMING_H
#define POKEPLATINUM_PORT_BATTLE_TIMING_H

#include <nitro/types.h>

// Ownership follows callbacks and their child tasks, never task priority.
typedef enum BattleTimingDomain {
    BATTLE_TIMING_NONE,
    BATTLE_TIMING_ANIMATION,
    BATTLE_TIMING_UI,
} BattleTimingDomain;

#ifdef SDK_PORT
void BattleTiming_SetActive(BOOL active);
BOOL BattleTiming_IsActive(void);
void BattleTiming_BeginFrame(void);
u32 BattleTiming_GetFrame(void);
u32 BattleTiming_GetVisualEpoch(void);
BOOL BattleTiming_ShouldSmoothAnimations(void);
BOOL BattleTiming_ShouldTick(BattleTimingDomain domain);
// Platinum animation updates per rendered frame: Smooth 0/1, Quick 1, Turbo 1/2.
// Nested animation owners already execute individual steps and receive at most one.
int BattleTiming_StepCount(BattleTimingDomain domain);
BOOL BattleTiming_IsExtraAnimationStep(void);
BOOL BattleTiming_SetExtraAnimationStep(BOOL extra);
// Scale only presentation pauses. Never use for animation, input or communication clocks.
int BattleTiming_PresentationDelay(int frames);
// Automatic battle-message page dwell: Quick caps at eight, Turbo at four.
int BattleTiming_MessagePageDelay(int frames);
// Battle exits: half duration in Quick, at most two updates in Turbo.
int BattleTiming_ExitDelay(int frames);
BattleTimingDomain BattleTiming_GetDomain(void);
BattleTimingDomain BattleTiming_SetDomain(BattleTimingDomain domain);
#else
static inline void BattleTiming_SetActive(BOOL active)
{
    (void)active;
}
static inline BOOL BattleTiming_IsActive(void)
{
    return FALSE;
}
static inline void BattleTiming_BeginFrame(void)
{
}
static inline u32 BattleTiming_GetFrame(void)
{
    return 0;
}
static inline u32 BattleTiming_GetVisualEpoch(void)
{
    return 0;
}
static inline BOOL BattleTiming_ShouldSmoothAnimations(void)
{
    return FALSE;
}
static inline BOOL BattleTiming_ShouldTick(BattleTimingDomain domain)
{
    (void)domain;
    return TRUE;
}
static inline int BattleTiming_StepCount(BattleTimingDomain domain)
{
    (void)domain;
    return 1;
}
static inline BOOL BattleTiming_IsExtraAnimationStep(void)
{
    return FALSE;
}
static inline BOOL BattleTiming_SetExtraAnimationStep(BOOL extra)
{
    (void)extra;
    return FALSE;
}
static inline int BattleTiming_PresentationDelay(int frames)
{
    return frames;
}
static inline int BattleTiming_MessagePageDelay(int frames)
{
    return frames;
}
static inline int BattleTiming_ExitDelay(int frames)
{
    return frames;
}
static inline BattleTimingDomain BattleTiming_GetDomain(void)
{
    return BATTLE_TIMING_NONE;
}
static inline BattleTimingDomain BattleTiming_SetDomain(BattleTimingDomain domain)
{
    (void)domain;
    return BATTLE_TIMING_NONE;
}
#endif

#endif
