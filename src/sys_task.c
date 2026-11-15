#include "sys_task.h"

#include <nitro.h>
#include <string.h>

#include "port/gameplay/battle_timing.h"

#include "sys_task_manager.h"
#include "system.h"

SysTask *SysTask_Start(SysTaskFunc callback, void *param, u32 priority)
{
    return SysTaskManager_AddTask(gSystem.mainTaskMgr, callback, param, priority);
}

SysTask *SysTask_ExecuteOnVBlank(SysTaskFunc callback, void *param, u32 priority)
{
    BattleTimingDomain previous = BattleTiming_SetDomain(BATTLE_TIMING_NONE);
    SysTask *task = SysTaskManager_AddTask(gSystem.vBlankTaskMgr, callback, param, priority);
    BattleTiming_SetDomain(previous);
    return task;
}

SysTask *SysTask_CreateOnPrintQueue(SysTaskFunc callback, void *param, u32 priority)
{
    return SysTaskManager_AddTask(gSystem.printTaskMgr, callback, param, priority);
}

SysTask *SysTask_ExecuteAfterVBlank(SysTaskFunc callback, void *param, u32 priority)
{
    BattleTimingDomain previous = BattleTiming_SetDomain(BATTLE_TIMING_NONE);
    SysTask *task = SysTaskManager_AddTask(gSystem.postVBlankTaskMgr, callback, param, priority);
    BattleTiming_SetDomain(previous);
    return task;
}

static SysTask *StartWithTiming(SysTaskFunc callback, void *param, u32 priority, BattleTimingDomain domain)
{
    BattleTimingDomain previous = BattleTiming_SetDomain(domain);
    SysTask *task = SysTask_Start(callback, param, priority);
    BattleTiming_SetDomain(previous);
    return task;
}

SysTask *SysTask_StartBattleAnimation(SysTaskFunc callback, void *param, u32 priority)
{
    return StartWithTiming(callback, param, priority, BATTLE_TIMING_ANIMATION);
}

SysTask *SysTask_StartBattleUI(SysTaskFunc callback, void *param, u32 priority)
{
    return StartWithTiming(callback, param, priority, BATTLE_TIMING_UI);
}

SysTask *SysTask_StartUnscaled(SysTaskFunc callback, void *param, u32 priority)
{
    return StartWithTiming(callback, param, priority, BATTLE_TIMING_NONE);
}

void SysTask_Done(SysTask *task)
{
    SysTask_Delete(task);
}
