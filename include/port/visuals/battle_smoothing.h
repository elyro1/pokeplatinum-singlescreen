#ifndef POKEPLATINUM_PORT_BATTLE_SMOOTHING_H
#define POKEPLATINUM_PORT_BATTLE_SMOOTHING_H

#include <nitro/types.h>

// Fixed-point render snapshots. They never replace authoritative animation state.
typedef struct BattleVisualPose {
    s32 position[3];
    s32 offset[3];
    s32 scale[3];
    s32 pivot[3];
    u16 rotation[3];
} BattleVisualPose;

typedef struct BattleVisualHistory {
    BattleVisualPose previous;
    BattleVisualPose current;
    u64 identity;
    u32 frame;
    u32 epoch;
    BOOL valid;
} BattleVisualHistory;

#ifdef SDK_PORT
void BattleSmoothing_Sample(BattleVisualHistory *history, const BattleVisualPose *pose,
    u64 identity, BOOL visible, BOOL enabled, BattleVisualPose *renderPose);
#endif

#endif
