#ifndef POKEPLATINUM_SCRIPTED_NPC_SCENE_H
#define POKEPLATINUM_SCRIPTED_NPC_SCENE_H

#include "struct_decls/map_object.h"

#include "field/field_system_decl.h"
#include "overlay005/map_object_anim_cmd.h"

// Starts a movement counted by WaitMovement. FALSE means no extra travel.
BOOL ScriptedNpcScene_Start(FieldSystem *fieldSystem, MapObject *object, u16 action, u16 maxTiles, BOOL exiting);

// Protect a departure's final walking leg, before its authored endpoint.
// Counts as ApplyMovement; the script still owns the eventual removal.
BOOL ScriptedNpcScene_StartDeparture(FieldSystem *fieldSystem, MapObject *object, const MapObjectAnimCmd *commands);

// Keep special-map object lifetime under the original script's control.
BOOL ScriptedNpcScene_StartRetainedExit(FieldSystem *fieldSystem, MapObject *object, u16 action, u16 maxTiles);

BOOL ScriptedNpcScene_ObjectVisible(FieldSystem *fieldSystem, MapObject *object);

#endif
