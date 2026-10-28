#include <assert.h>
#include <stdio.h>

// Run the real throw task with send-out cleanup occurring before the trainer exits.
#include "../../src/battle/battle_display.c"

struct BallRotation { int mode; };
static u32 sBattleType;
static BattlerData sPartner;
static s16 sX, sY;
static int sDeleted, sUnloaded, sCleared, sDone, sPartnerDelay;
static BOOL sFastIntro;
static int sCryCalls, sCryDelay, sCryPan, sCryDelayLoads;

BOOL BwAdapter_FastIntro(void) { return sFastIntro; }
ChatotCry *BattleSystem_GetChatotCry(BattleSystem *battleSys, int battler) { return NULL; }
void PokemonSprite_LoadCryDelay(NARC *narc, u8 *delay, u16 species, u16 type)
{
    sCryDelayLoads++;
    *delay = 17;
}
void Species_PlayDelayedCry(ChatotCry *cry, enum PokemonCryMod mod, u16 species, int form,
    int pan, int volume, int forceDefault, enum HeapID heap, u8 delay)
{
    assert(species == SPECIES_CHATOT && form == 0);
    assert(volume == SOUND_VOLUME_MAX && !forceDefault && heap == HEAP_ID_BATTLE);
    sCryCalls++;
    sCryDelay = delay;
    sCryPan = pan;
}

u32 BattleSystem_GetBattleType(BattleSystem *battleSys) { return sBattleType; }
BattlerData *BattleSystem_GetBattlerData(BattleSystem *battleSys, int battler) { return &sPartner; }
int BattleSystem_GetPartner(BattleSystem *battleSys, int battler) { return battler ^ 2; }
SpriteManager *BattleSystem_GetSpriteManager(BattleSystem *battleSys) { return NULL; }
SpriteSystem *BattleSystem_GetSpriteSystem(BattleSystem *battleSys) { return NULL; }
PaletteData *BattleSystem_GetPaletteData(BattleSystem *battleSys) { return NULL; }
void ManagedSprite_OffsetPositionXY(ManagedSprite *sprite, s16 x, s16 y) { assert(sprite); sX += x; sY += y; }
void ManagedSprite_GetPositionXY(ManagedSprite *sprite, s16 *x, s16 *y) { assert(sprite); *x = sX; *y = sY; }
u16 ManagedSprite_GetAnimationFrame(ManagedSprite *sprite) { return 3; }
void ManagedSprite_SetAnimationFrame(ManagedSprite *sprite, u16 frame) { }
void ManagedSprite_SetAnim(ManagedSprite *sprite, u32 anim) { }
void ManagedSprite_SetAnimateFlag(ManagedSprite *sprite, BOOL animate) { }
void Sprite_DeleteAndFreeResources(ManagedSprite *sprite) { assert(sprite); sDeleted++; }
BOOL SpriteManager_UnloadCharObjById(SpriteManager *manager, int id) { sUnloaded++; return TRUE; }
BOOL SpriteManager_UnloadPlttObjById(SpriteManager *manager, int id) { sUnloaded++; return TRUE; }
BOOL SpriteManager_UnloadCellObjById(SpriteManager *manager, int id) { sUnloaded++; return TRUE; }
BOOL SpriteManager_UnloadAnimObjById(SpriteManager *manager, int id) { sUnloaded++; return TRUE; }
void BattleController_EmitClearCommand(BattleSystem *battleSys, int battler, int command) { sCleared++; }
void Heap_Free(void *data) { }
void SysTask_Done(SysTask *task) { sDone++; }
BOOL Sound_PlayEffect(u16 seqID) { return TRUE; }
BallRotation *ov12_02237728(BallThrow *ballThrow) { assert(0); return NULL; }
BOOL ov12_022377F8(BallRotation *ball) { assert(ball); return FALSE; }
void ov12_0223783C(BallRotation *ball) { assert(ball); }
int ov12_02237890(BallRotation *ball) { assert(ball); return ball->mode; }
void ov12_0223786C(BallRotation *ball, int mode) { assert(ball); ball->mode = mode; }
void ov12_02237E0C(BallRotation *ball, int visible) { assert(ball); }
void ov12_02237E18(BallRotation *ball, s16 x, s16 y) { assert(ball); }
void ov12_02237E30(BallRotation *ball, BOOL animate) { assert(ball); }
void ov12_02237E34(BallRotation *ball, int value) { assert(ball); }
void ov12_02237E40(BallRotation *ball, int value) { assert(ball); }
void ov12_02237E4C(BallRotation *ball, int delay) { assert(ball); sPartnerDelay = delay; }

static void CheckThrow(int face, u32 type, BOOL ownBall, BOOL partnerBall)
{
    BallRotation ball = { BALL_THROW_MODE_TRAINER_SEND_OUT };
    BallRotation partner = { BALL_THROW_MODE_TRAINER_SEND_OUT };
    ManagedSprite sprite = { 0 };
    BattlerData battler = { 0 };
    TrainerThrowBallData data = { 0 };

    sBattleType = type;
    sPartner.ballRotation = partnerBall ? &partner : NULL;
    sX = face == FACE_FRONT ? 155 : 0;
    sY = 80;
    sDeleted = sUnloaded = sCleared = sDone = sPartnerDelay = 0;
    battler.managedSprite = &sprite;
    battler.ballRotation = ownBall ? &ball : NULL;
    data.battlerData = &battler;
    data.face = face;
    data.state = face == FACE_FRONT ? 1 : 2;

    Task_ThrowTrainerBall(NULL, &data);
    if (ownBall) {
        assert(ball.mode == BALL_THROW_MODE_SEND_OUT);
        if ((type & BATTLE_TYPE_DOUBLES) && !(type & BATTLE_TYPE_2vs2) && partnerBall) {
            assert(partner.mode == BALL_THROW_MODE_SEND_OUT);
            if (face == FACE_FRONT) {
                assert(sPartnerDelay == 12);
            }
        }
    }

    // Task_ShowEncounter finishes early in fast mode and clears either ball independently.
    battler.ballRotation = NULL;
    sPartner.ballRotation = NULL;
    for (int frame = 0; frame < 100 && !sDone; frame++) {
        Task_ThrowTrainerBall(NULL, &data);
    }
    assert(sDone == 1);
    assert(sCleared == 1);
    assert(sDeleted == 1);
    assert(sUnloaded == 4);
    assert(battler.managedSprite == NULL);
}

static void CheckEncounterCry(BOOL fast, int face)
{
    BattlerData battler = { 0 };
    MonEncounterData data = { .battlerData = &battler, .species = SPECIES_CHATOT, .face = face };
    sFastIntro = fast;
    sCryCalls = sCryDelayLoads = 0;
    BOOL early = fast && face == FACE_FRONT;
    BattleDisplay_PlayEncounterCry(&data, TRUE);
    assert(sCryCalls == early);
    BattleDisplay_PlayEncounterCry(&data, FALSE);
    assert(sCryCalls == 1); // The later slide must never replay an early cry.
    assert(sCryDelay == (early ? 0 : 17));
    assert(sCryDelayLoads == !early);
    assert(sCryPan == (face == FACE_FRONT ? BATTLE_SOUND_PAN_RIGHT : BATTLE_SOUND_PAN_LEFT));
}

int main(void)
{
    for (int fast = 0; fast < 2; fast++) {
        CheckEncounterCry(fast, FACE_FRONT);
        CheckEncounterCry(fast, FACE_BACK);
    }
    const u32 types[] = { BATTLE_TYPE_TRAINER, BATTLE_TYPE_TRAINER_DOUBLES,
        BATTLE_TYPE_TRAINER_DOUBLES | BATTLE_TYPE_2vs2 };
    for (unsigned i = 0; i < NELEMS(types); i++) {
        for (int own = 0; own <= 1; own++) {
            for (int partner = 0; partner <= 1; partner++) {
                CheckThrow(FACE_FRONT, types[i], own, partner);
                CheckThrow(FACE_BACK, types[i], own, partner);
            }
        }
    }
    puts("Encounter cries: fast wild opponents play early once; ordinary/player cry timing preserved. Trainer throw cleanup: 24 scenarios passed.");
    return 0;
}
