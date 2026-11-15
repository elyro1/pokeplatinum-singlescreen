#ifndef POKEPLATINUM_FIELD_VISUAL_EFFECTS_H
#define POKEPLATINUM_FIELD_VISUAL_EFFECTS_H
#include <nitro/types.h>
#include <nnsys/g3d/binres/res_struct.h>
typedef struct NNSG3dRenderObj_ NNSG3dRenderObj;

#include "struct_decls/map_object.h"

#include "field/field_system_decl.h"
#include "port/visuals/field_visuals.h"
#ifdef __cplusplus
extern "C" {
#endif
void FieldVisualEffects_SetScene(FieldSystem *field, BOOL allowed, enum FieldAtmosphereRegion region, int weather);
void FieldVisualEffects_Render(FieldSystem *field);
void FieldVisualEffects_EndScene(void);
BOOL FieldVisualEffects_DrawContactShadow(FieldSystem *field, MapObject *object, const VecFx32 *position, const VecFx32 *scale, const NNSG3dResMdlInfo *modelInfo);
// Draws the shadows a character's sprite casts on the ground, while its
// billboard's current frame is bound. Characters queue themselves when
// their contact shadow is drawn.
struct Billboard;
void FieldVisualEffects_DrawCastShadows(struct Billboard *billboard);
// Bracket drawing a character's sprite, which warm night light may tint.
BOOL FieldVisualEffects_BeginCharacter(struct Billboard *billboard);
void FieldVisualEffects_EndCharacter(BOOL active);
BOOL FieldVisualEffects_BeginModel(NNSG3dRenderObj *model);
void FieldVisualEffects_EndModel(NNSG3dRenderObj *model, BOOL active);
void FieldVisualEffects_SetMaterial(const NNSG3dResMdl *model, int material);
// Brackets drawing one prop so its materials can be matched by model ID.
void FieldVisualEffects_RegisterProp(int modelID, const VecFx32 *position, const VecFx32 *scale);
void FieldVisualEffects_EndProp(void);
float FieldVisualEffects_GetWaterCpuMs(void);
int FieldVisualEffects_GetLightCount(void);
int FieldVisualEffects_GetParticleCount(void);
int FieldVisualEffects_GetWaterCount(void);
int FieldVisualEffects_GetRippleCount(void);
#ifdef __cplusplus
}
#endif
#endif
