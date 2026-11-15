#include "port/visuals/battle_smoothing.h"

#include "port/gameplay/battle_timing.h"

#define TELEPORT_DISTANCE (64 * 4096)

static BOOL Discontinuous(const BattleVisualPose *previous, const BattleVisualPose *current)
{
    for (int i = 0; i < 3; i++) {
        s64 delta = (s64)current->position[i] - previous->position[i];
        s64 offsetDelta = (s64)current->offset[i] - previous->offset[i];
        if (delta > TELEPORT_DISTANCE || delta < -TELEPORT_DISTANCE
            || offsetDelta > TELEPORT_DISTANCE || offsetDelta < -TELEPORT_DISTANCE
            || (previous->scale[i] < 0 && current->scale[i] >= 0)
            || (previous->scale[i] >= 0 && current->scale[i] < 0)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL SamePose(const BattleVisualPose *a, const BattleVisualPose *b)
{
    for (int i = 0; i < 3; i++) {
        if (a->position[i] != b->position[i] || a->offset[i] != b->offset[i]
            || a->scale[i] != b->scale[i] || a->pivot[i] != b->pivot[i]
            || a->rotation[i] != b->rotation[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static s32 Midpoint(s32 previous, s32 current)
{
    return (s32)((s64)previous + ((s64)current - previous) / 2);
}

void BattleSmoothing_Sample(BattleVisualHistory *history, const BattleVisualPose *pose,
    u64 identity, BOOL visible, BOOL enabled, BattleVisualPose *renderPose)
{
    *renderPose = *pose;
    if (!enabled || !visible || !BattleTiming_ShouldSmoothAnimations()) {
        history->valid = FALSE;
        return;
    }

    u32 frame = BattleTiming_GetFrame();
    u32 epoch = BattleTiming_GetVisualEpoch();
    BOOL tick = BattleTiming_ShouldTick(BATTLE_TIMING_ANIMATION);
    if (!history->valid || history->epoch != epoch || history->identity != identity
        || frame - history->frame > 2 || Discontinuous(&history->current, pose)
        // An off-tick mutation belongs to an unscaled path. Do not delay it.
        || (!tick && !SamePose(&history->current, pose))) {
        history->previous = history->current = *pose;
        history->identity = identity;
        history->epoch = epoch;
        history->frame = frame;
        history->valid = TRUE;
        return;
    }

    if (tick) {
        if (history->frame != frame) {
            history->previous = history->current;
        }
        history->current = *pose;
        *renderPose = history->previous;
    } else {
        for (int i = 0; i < 3; i++) {
            renderPose->position[i] = Midpoint(history->previous.position[i], history->current.position[i]);
            renderPose->offset[i] = Midpoint(history->previous.offset[i], history->current.offset[i]);
            renderPose->scale[i] = Midpoint(history->previous.scale[i], history->current.scale[i]);
            renderPose->pivot[i] = Midpoint(history->previous.pivot[i], history->current.pivot[i]);
            // Nintendo angle indices wrap at 65536; take the shortest signed arc.
            s16 delta = (s16)(history->current.rotation[i] - history->previous.rotation[i]);
            renderPose->rotation[i] = (u16)(history->previous.rotation[i] + delta / 2);
        }
    }
    history->frame = frame;
}
