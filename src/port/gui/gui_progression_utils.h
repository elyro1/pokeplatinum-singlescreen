#ifndef GUI_PROGRESSION_UTILS_H
#define GUI_PROGRESSION_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif
int Progression_GetBadge(int badge);
void Progression_GiveBadge(int badge);
int Progression_HasDex(int national);
void Progression_GiveDex(int national);
int Progression_IsGameCompleted(void);
void Progression_SetGameCompleted(void);
void Progression_UnlockFlyLocations(void);
int Progression_FlagLimit(void);
int Progression_VarStart(void);
int Progression_VarEnd(void);
int Progression_GetFlag(int id);
void Progression_SetFlag(int id, int value);
int Progression_GetVar(int id);
void Progression_SetVar(int id, int value);
#ifdef __cplusplus
}
#endif
#endif
