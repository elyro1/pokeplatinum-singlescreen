#include "port/battle_timing.h"
#ifdef SDK_PORT
#include "system.h"
#endif

#include "sys_task_manager.h"

#include "nitro/types.h"
#include <nitro.h>
#include <string.h>

static void SysTaskManager_InitTask(SysTaskManager *sysTaskMgr, SysTask *task);
static void SysTaskManager_InitTasks(SysTaskManager *sysTaskMgr);
static SysTask *SysTaskManager_AllocTask(SysTaskManager *sysTaskMgr);
static BOOL SysTaskManager_FreeTask(SysTaskManager *sysTaskMgr, SysTask *task);
static SysTask *SysTaskManager_InternalAddTask(SysTaskManager *sysTaskMgr, SysTaskFunc callback, void *param, u32 priority);
static void SysTaskManager_ExecuteTasksInternal(SysTaskManager *sysTaskMgr);

static void SysTaskManager_InitTask(SysTaskManager *sysTaskMgr, SysTask *task)
{
    task->manager = sysTaskMgr;
    task->prevTask = task->nextTask = &sysTaskMgr->sentinelTask;
    task->priority = 0;
    task->param = NULL;
    task->callback = NULL;
}

static void SysTaskManager_InitTasks(SysTaskManager *sysTaskMgr)
{
    for (int i = 0; i < sysTaskMgr->maxTasks; i++) {
        SysTaskManager_InitTask(sysTaskMgr, &sysTaskMgr->tasks[i]);
        sysTaskMgr->taskStack[i] = &sysTaskMgr->tasks[i];
    }

    sysTaskMgr->stackPointer = 0;
}

static SysTask *SysTaskManager_AllocTask(SysTaskManager *sysTaskMgr)
{
    if (sysTaskMgr->stackPointer == sysTaskMgr->maxTasks) {
        return NULL;
    }

    SysTask *task = sysTaskMgr->taskStack[sysTaskMgr->stackPointer];
    sysTaskMgr->stackPointer++;

    return task;
}

static BOOL SysTaskManager_FreeTask(SysTaskManager *sysTaskMgr, SysTask *task)
{
    if (sysTaskMgr->stackPointer == 0) {
        return FALSE;
    }

    task->manager = sysTaskMgr;
    task->prevTask = task->nextTask = &sysTaskMgr->sentinelTask;
    task->priority = 0;
    task->param = NULL;
    task->callback = NULL;

    sysTaskMgr->stackPointer--;
    sysTaskMgr->taskStack[sysTaskMgr->stackPointer] = task;

    return TRUE;
}

u32 SysTaskManager_GetRequiredSize(u32 maxTasks)
{
    return sizeof(SysTaskManager) + (sizeof(SysTask *) + sizeof(SysTask)) * maxTasks;
}

SysTaskManager *SysTaskManager_Init(u32 maxTasks, void *memory)
{
    GF_ASSERT(memory);
    SysTaskManager *sysTaskMgr = memory;

    sysTaskMgr->taskStack = (SysTask **)((u8 *)(sysTaskMgr) + sizeof(SysTaskManager));
    sysTaskMgr->tasks = (SysTask *)((u8 *)(sysTaskMgr->taskStack) + sizeof(SysTask *) * maxTasks);
    sysTaskMgr->maxTasks = maxTasks;
    sysTaskMgr->stackPointer = 0;
    sysTaskMgr->locked = FALSE;

    SysTaskManager_InternalInit(sysTaskMgr);
    return sysTaskMgr;
}

void SysTaskManager_InternalInit(SysTaskManager *sysTaskMgr)
{
    SysTaskManager_InitTasks(sysTaskMgr);

    sysTaskMgr->sentinelTask.manager = sysTaskMgr;
    sysTaskMgr->sentinelTask.prevTask = sysTaskMgr->sentinelTask.nextTask = &sysTaskMgr->sentinelTask;
    sysTaskMgr->sentinelTask.priority = 0;
    sysTaskMgr->sentinelTask.param = NULL;
    sysTaskMgr->sentinelTask.callback = NULL;

    sysTaskMgr->currentTask = sysTaskMgr->sentinelTask.nextTask;
}

void SysTaskManager_ExecuteTasks(SysTaskManager *sysTaskMgr)
{
#ifdef SDK_PORT
    if (!sysTaskMgr->locked && BattleTiming_StepCount(BATTLE_TIMING_ANIMATION) > 1) {
        // A separate pass preserves task ordering and deletion/reuse semantics.
        // Only animation owners run; turn logic and input/UI tasks run once below.
        BOOL previous = BattleTiming_SetExtraAnimationStep(TRUE);
        SysTaskManager_ExecuteTasksInternal(sysTaskMgr);
        BattleTiming_SetExtraAnimationStep(previous);
    }
#endif
    SysTaskManager_ExecuteTasksInternal(sysTaskMgr);
}

static void SysTaskManager_ExecuteTasksInternal(SysTaskManager *sysTaskMgr)
{
    if (sysTaskMgr->locked) {
        return;
    }

    sysTaskMgr->currentTask = sysTaskMgr->sentinelTask.nextTask;

    while (sysTaskMgr->currentTask != &sysTaskMgr->sentinelTask) {
        sysTaskMgr->nextTask = sysTaskMgr->currentTask->nextTask;

#ifdef SDK_PORT
        if (BattleTiming_IsExtraAnimationStep() && sysTaskMgr->currentTask->timingDomain != BATTLE_TIMING_ANIMATION) {
            sysTaskMgr->currentTask = sysTaskMgr->nextTask;
            continue;
        }
#endif
        if (sysTaskMgr->currentTask->state == TASK_STATE_ACTIVE) {
            if (sysTaskMgr->currentTask->callback != NULL) {
#ifdef SDK_PORT
                BattleTimingDomain domain = sysTaskMgr->currentTask->timingDomain;
                SysTask *task = sysTaskMgr->currentTask;
                BOOL extra = BattleTiming_IsExtraAnimationStep();
                // Preserve short input edges across a skipped presentation update.
                // Live input/communication tasks have NONE and are never skipped.
                if (!extra) {
                    task->timingPressedKeys |= gSystem.pressedKeys;
                    task->timingRepeatKeys |= gSystem.pressedKeysRepeatable;
                    if (gSystem.touchPressed) {
                        task->timingTouchPressed = TRUE;
                        task->timingTouchX = gSystem.touchX;
                        task->timingTouchY = gSystem.touchY;
                    }
                }
                if (BattleTiming_ShouldTick(domain)) {
                    u32 pressedKeys = gSystem.pressedKeys;
                    u32 repeatKeys = gSystem.pressedKeysRepeatable;
                    u16 touchPressed = gSystem.touchPressed;
                    u16 touchX = gSystem.touchX, touchY = gSystem.touchY;
                    gSystem.pressedKeys = extra ? 0 : task->timingPressedKeys;
                    gSystem.pressedKeysRepeatable = extra ? 0 : task->timingRepeatKeys;
                    gSystem.touchPressed = extra ? FALSE : task->timingTouchPressed;
                    if (!extra && task->timingTouchPressed) {
                        gSystem.touchPressed = TRUE;
                        gSystem.touchX = task->timingTouchX;
                        gSystem.touchY = task->timingTouchY;
                    }
                    if (!extra) {
                        task->timingPressedKeys = task->timingRepeatKeys = 0;
                        task->timingTouchPressed = FALSE;
                    }
                    BattleTimingDomain previous = BattleTiming_SetDomain(domain);
                    task->callback(task, task->param);
                    // The callback may delete/reuse its task. Do not access it here.
                    BattleTiming_SetDomain(previous);
                    gSystem.pressedKeys = pressedKeys;
                    gSystem.pressedKeysRepeatable = repeatKeys;
                    gSystem.touchPressed = touchPressed;
                    gSystem.touchX = touchX;
                    gSystem.touchY = touchY;
                }
#else
                sysTaskMgr->currentTask->callback(sysTaskMgr->currentTask, sysTaskMgr->currentTask->param);
#endif
            }
        } else {
            sysTaskMgr->currentTask->state = TASK_STATE_ACTIVE;
        }

        sysTaskMgr->currentTask = sysTaskMgr->nextTask;
    }

    sysTaskMgr->currentTask->callback = NULL;
}

SysTask *SysTaskManager_AddTask(SysTaskManager *sysTaskMgr, SysTaskFunc callback, void *param, u32 priority)
{
    sysTaskMgr->locked = TRUE;
    SysTask *task = SysTaskManager_InternalAddTask(sysTaskMgr, callback, param, priority);
    sysTaskMgr->locked = FALSE;

    return task;
}

static SysTask *SysTaskManager_InternalAddTask(SysTaskManager *sysTaskMgr, SysTaskFunc callback, void *param, u32 priority)
{
    SysTask *task = SysTaskManager_AllocTask(sysTaskMgr);
    if (task == NULL) {
        return NULL;
    }

    task->priority = priority;
    task->param = param;
    task->callback = callback;
#ifdef SDK_PORT
    task->timingDomain = BattleTiming_GetDomain();
    task->timingPressedKeys = 0;
    task->timingRepeatKeys = 0;
    task->timingTouchPressed = 0;
#endif

    if (sysTaskMgr->currentTask->callback != NULL) {
        // The task being added should only run starting from the next frame.
        // If the priority of the task being added is greater than or equal to the current task's priority,
        // it would be executed in the current frame, so it is set to inactive.
        if (sysTaskMgr->currentTask->priority <= priority) {
            task->state = TASK_STATE_INACTIVE;
        } else {
            task->state = TASK_STATE_ACTIVE;
        }
    } else {
        task->state = TASK_STATE_ACTIVE;
    }

    for (SysTask *taskIter = sysTaskMgr->sentinelTask.nextTask; taskIter != &sysTaskMgr->sentinelTask; taskIter = taskIter->nextTask) {
        if (taskIter->priority > task->priority) {
            task->prevTask = taskIter->prevTask;
            task->nextTask = taskIter;
            taskIter->prevTask->nextTask = task;
            taskIter->prevTask = task;

            // Also update the task to be executed next, to avoid it accidentally being skipped.
            if (taskIter == sysTaskMgr->nextTask) {
                sysTaskMgr->nextTask = task;
            }

            return task;
        }
    }

    // If the task has the lowest priority, it is added to the end of the list.
    if (sysTaskMgr->nextTask == &sysTaskMgr->sentinelTask) {
        sysTaskMgr->nextTask = task;
    }

    task->prevTask = sysTaskMgr->sentinelTask.prevTask;
    task->nextTask = &sysTaskMgr->sentinelTask;

    sysTaskMgr->sentinelTask.prevTask->nextTask = task;
    sysTaskMgr->sentinelTask.prevTask = task;

    return task;
}

void SysTask_Delete(SysTask *task)
{
    if (task->manager->nextTask == task) {
        task->manager->nextTask = task->nextTask;
    }

    task->prevTask->nextTask = task->nextTask;
    task->nextTask->prevTask = task->prevTask;

    SysTaskManager_FreeTask(task->manager, task);
}

void SysTask_SetCallback(SysTask *task, SysTaskFunc callback)
{
    // Callback transitions keep the task's owner and any pending input edges.
    task->callback = callback;
}

void *SysTask_GetParam(SysTask *task)
{
    return task->param;
}

u32 SysTask_GetPriority(SysTask *task)
{
    return task->priority;
}
