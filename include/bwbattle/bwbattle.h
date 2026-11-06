#ifndef BWBATTLE_BWBATTLE_H
#define BWBATTLE_BWBATTLE_H

// The interface between Platinum's code and bwbattle (docs/bwbattle.md). Both sides include it, so it uses only plain C
// types: Platinum's headers and BW2's have the same names and can't meet in one file.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The archives bwbattle reads come from the player's Black 2 or White 2 ROM, extracted into this directory of the
// working directory, at their ROM paths (bw2/a/0/0/4, ...)
#define BWBATTLE_ASSET_DIR "bw2"
// BW2's sound archive, extracted with the archives; the view stays silent without it
#define BWBATTLE_SOUND_ARCHIVE BWBATTLE_ASSET_DIR "/swan_sound_data.sdat"

typedef enum {
    BWBATTLE_EXTRACT_OK,
    BWBATTLE_EXTRACT_CANT_OPEN, // The ROM file couldn't be read
    BWBATTLE_EXTRACT_NOT_BW2,   // Not a US Black 2 or White 2 ROM
    BWBATTLE_EXTRACT_BAD_ROM,   // Its file system doesn't have an archive bwbattle needs
    BWBATTLE_EXTRACT_CANT_WRITE // A file couldn't be written under BWBATTLE_ASSET_DIR
} BwBattleExtractResult;

// Whether every archive bwbattle needs has been extracted
bool BwBattle_AssetsReady(void);
// Iris's extra overworld archive and sound, plus the native-view conversions.
bool BwBattle_IrisAssetsReady(void);
bool BwBattle_IrisVsReady(void);
// Virtual members of graphic/field_encounteffect.narc. They exist only in
// memory, derived from the player's extraction; retail members stay unchanged.
enum {
    BWBATTLE_IRIS_VS_PALETTE = 0x1000,
    BWBATTLE_IRIS_VS_TILES,
    BWBATTLE_IRIS_VS_CELLS,
    BWBATTLE_IRIS_VS_ANIM,
};
const void *BwBattle_IrisResource(const char *path, unsigned int member, unsigned int *size);
// A member of an archive on disk, decompressed if it was compressed. NULL if it is missing or empty; free() the rest
void *BwBattle_ReadMember(const char *path, unsigned int member, unsigned int *size);
// Draws Spinda's spots on a 96x96 sprite, 12 tiles wide, each tile's pixels in order
void BwBattle_DrawSpindaSpots(unsigned char *tiles, unsigned int personality);
// The BW sprites setting's icons, which take the place of members of pl_poke_icon.narc (port/bw_mon_sprites.c)
const void *BwMonIcons_Resource(unsigned int member, unsigned int *size);
bool BwBattle_PlayHydreigonCry(void *handle, unsigned int player);
// Extracts the archives bwbattle needs from a ROM, skipping those already there
BwBattleExtractResult BwBattle_ExtractAssets(const char *romPath);
const char *BwBattle_ExtractResultMessage(BwBattleExtractResult result);

// Sets up what BW2's main loop gives the view: its archives, random numbers and interrupt tasks. Fails if the assets
// aren't extracted. Called once; later calls do nothing
bool BwBattle_Init(void);
// Run BW2's V-blank and H-blank tasks; the Platinum side calls them from its interrupts while the view runs
void BwBattle_VBlank(void);
void BwBattle_HBlank(void);

// A Pokémon as the view shows it. The Platinum side fills it in; the view reads it as BW2's battle Pokémon and party
// Pokémon. Values are BW2's where the games differ: status is BW2's CONDITION_* (constants/battle.h), sex 0 male,
// 1 female, 2 none
typedef struct BwBattleMon {
    unsigned short species;
    unsigned short form;
    unsigned int personality;
    unsigned char level;
    unsigned char sex;
    unsigned char ball;
    unsigned char status;
    bool shiny;
    bool isEgg;
    bool showSex; // FALSE for a Pokémon whose sex the gauge hides (Nidoran)
    bool badlyPoisoned;
    unsigned short hp;
    unsigned short maxHp;
    unsigned int exp;
    const unsigned short *nickname; // Platinum's character codes, ending at 0xffff
} BwBattleMon;

// The battle as the view sets itself up for it. Values are BW2's (battle/btl_setup.h)
typedef struct BwBattleSetup {
    unsigned char battleStyle; // 0 single, 1 double, 2 triple, 3 rotation
    unsigned char battleType;  // 0 wild, 1 trainer, ...
    unsigned int typeFlags;    // BW2's BtlSetup flags
    bool multi;
    unsigned char playerClientId;
    unsigned char partnerClientId;
    unsigned char enemyClientIds[2];
    unsigned char playerSide;
    bool playerFemale;
    unsigned int trainerClasses[4]; // BW2 trainer classes, by client
    // The background (BtlFieldEnv)
    unsigned int bgType;
    unsigned int terrain;
    unsigned char weather;
    unsigned char season;
    unsigned short zoneId;
    unsigned char hour;
    unsigned char minute;
} BwBattleSetup;

void BwBattle_SetSetup(const BwBattleSetup *setup);

// Play Iris's original BW2 theme on Platinum's BGM handle/player. Its own
// archive/heap keeps move-effect sound cleanup from unloading the music.
bool BwBattle_PlayIrisMusic(void *handle, unsigned int player);
void BwBattle_StopMusic(void);

// The view (src/bwbattle/host), inside Platinum's battle once it has set up its screens. Create sets the battle up
// as BwBattle_SetSetup does; it fails if the assets aren't there. Main updates the view and draws its 3D scene, from
// Platinum's draw task in place of its battle sprites
bool BwBattleView_Create(const BwBattleSetup *setup);
void BwBattleView_Delete(void);
bool BwBattleView_IsActive(void);
bool BwBattleView_ShowAbilityPopup(unsigned int viewPos, const unsigned short *nickname, const unsigned short *ability);
bool BwBattleView_IsAbilityPopupBusy(void);
void BwBattleView_SetAbilityPopupHeld(unsigned int viewPos, bool held);
// Runs frames of BW2's battle view, of which the last is drawn: BW2's battle runs at 60 frames a second
// Battle effects and ambient motion have separate clocks (both are 60 Hz in Smooth).
void BwBattleView_Main(unsigned int frames, unsigned int idleFrames);
// The 3D view's width in dots: 256, or wider when the port draws the arena widescreen (libntr's
// SIM_SetFieldWidescreen); the 2D actors stay on the 256 in the middle
void BwBattleView_SetViewWidth(unsigned int width);
// Shows a Pokémon at a BW2 view position (0 the player's, 1 the opponent's in a single battle). The record has to stay
// valid while it is shown
void BwBattleView_AddMon(unsigned int viewPos, const BwBattleMon *mon);
void BwBattleView_RemoveMon(unsigned int viewPos);
void BwBattleView_SetMonVisible(unsigned int viewPos, bool visible);
// The Pokémon's substitute: whether it has one (BW2 swaps the doll out around its moves) and whether the doll shows
bool BwBattleView_HasSubstitute(unsigned int viewPos, bool *shown);
// Draws the trainer at viewPos (BwBattleView_SetTrainer) as an image in place of BW2's sprite, where it stands: width by
// height dots of 4 bits, its rows packed, low dots first, with its 16 colors, of which 0 is clear, at scalePercent of its
// size
void BwBattleView_SetTrainerImage(unsigned int viewPos, const unsigned char *image, unsigned int width,
    unsigned int height, const unsigned short *colors, unsigned int scalePercent);
// Updates a fallback trainer's cell while retaining its palette and scale. The anchor is measured from the image's
// top left, so cell offsets (such as an Elite Four trainer jumping) remain part of the animation.
void BwBattleView_UpdateTrainerImage(unsigned int viewPos, const unsigned char *image, int anchorX, int anchorY);
typedef enum {
    BW_TRAINER_BACK_NONE,
    BW_TRAINER_BACK_LUCAS,
    BW_TRAINER_BACK_DAWN,
    BW_TRAINER_BACK_BARRY,
    BW_TRAINER_BACK_CHERYL,
    BW_TRAINER_BACK_RILEY,
    BW_TRAINER_BACK_MARLEY,
    BW_TRAINER_BACK_BUCK,
    BW_TRAINER_BACK_MIRA,
} BwTrainerBack;
// Replaces the trainer's artwork while keeping BW's multi-cell throw animation. Register before an effect adds it.
void BwBattleView_SetTrainerBackSprite(unsigned int viewPos, BwTrainerBack trainer);
// Gives the Pokémon the substitute and shows the doll, or takes it away, without an effect
void BwBattleView_SetSubstitute(unsigned int viewPos, bool substitute);
// Shows a trainer of a BW2 trainer class at a trainer position (8 the player's, 9 the opponent's, 11 and 13 two
// opponents'), still until an effect animates it unless animate is set
void BwBattleView_SetTrainer(unsigned int viewPos, unsigned int trainerClass, bool animate);
void BwBattleView_RemoveTrainer(unsigned int viewPos);
// Starts a move's effect from one view position at another; FALSE if the view has none for the move
bool BwBattleView_StartMove(unsigned int attackerPos, unsigned int defenderPos, unsigned short move);
// Starts one of BW2's other effects (archive 66: 0x231 and on) at a view position, or for the field with -1
bool BwBattleView_StartEffect(int viewPos, unsigned short effect);
// Fast wild opening: overlap wild shiny sparkles with the player's existing send-out.
bool BwBattleView_StartFastWildSendOut(void);
// Quick/Turbo send-outs finish their landing/shadow/camera sequence while cries play.
bool BwBattleView_StartFastSendOut(int viewPos, unsigned short effect);
// Finish an opening effect without drawing its intermediate camera shots.
void BwBattleView_FinishOpeningEffect(void);
// The HP and EXP gauge of the Pokémon at a view position: shown for a record (which has to stay valid while shown),
// then set at once to the HP, the experience points of the level and the status (BW2's CONDITION_*, 6 badly poisoned)
void BwBattleView_ShowGauge(unsigned int viewPos, const BwBattleMon *mon);
void BwBattleView_HideGauge(unsigned int viewPos);
void BwBattleView_SetGaugeVisible(unsigned int viewPos, bool visible);
void BwBattleView_SetGaugeHp(unsigned int viewPos, int hp, bool moving);
void BwBattleView_SetGaugeExp(unsigned int viewPos, int exp);
void BwBattleView_SetGaugeStatus(unsigned int viewPos, unsigned char status);
// BW2's message window (btlv_scu.c): its eight frame tiles (corners and edges, 4 bits a dot) and its 16 colors, of
// which 12 fills it, and 1 and 9 are its text and the text's shadow. False without them
#define BW_MSG_WINDOW_TILES 8
#define BW_MSG_WINDOW_FILL 12
#define BW_MSG_WINDOW_TEXT 1
#define BW_MSG_WINDOW_SHADOW 9
bool BwBattleView_GetMessageWindow(unsigned char tiles[BW_MSG_WINDOW_TILES * 32], unsigned short colors[16]);
// libntr's OBJ extension for the view's 2D sprites (BwBattleView_Create turns it on): off while the view stops for a
// menu's screen, whose sprites take the OAM entries the extension would read from BW2's characters and palettes
void BwBattleView_SetObjExtension(bool on);
// BW2's idle camera, which moves around the field while the commands are chosen; off always resets the camera,
// including the camera lock set by item scripts after the idle camera was stopped
void BwBattleView_SetIdleCamera(bool on);
bool BwBattleView_IsIdleCameraActive(void);
// Fades the view's 2D sprites (the gauges) level of 16 of the way to color, as Platinum's fades do its own, which don't
// reach the view's palettes; 0 puts them back
void BwBattleView_SetObjFade(unsigned int level, unsigned short color);
// A side's gauge of party balls (0 the player's, 1 the opponent's): each ball 0 none or unable to battle, 1 able, 2
// fainted, 3 with a status condition; the second trainer's party of a side from the fourth on
void BwBattleView_ShowBallGauge(unsigned int side, const unsigned char balls[6]);
void BwBattleView_HideBallGauge(unsigned int side);
// Throws a ball (an item number) at the Pokémon at a view position: it shakes (up to 3 times) and is caught or breaks
// free; or a trainer blocks it
bool BwBattleView_StartCatch(unsigned int viewPos, unsigned short ball, unsigned int shakes, bool caught);
bool BwBattleView_StartBallBlocked(unsigned int viewPos, unsigned short ball);
// The catch effect of a caught Pokémon stops after the catch, and goes on to its end with this
void BwBattleView_ResumeEffect(void);
// Whether an effect is still playing
bool BwBattleView_IsEffectBusy(void);

// What bwbattle asks the Platinum side for (src/bwbattle/adapter). Species and moves are the same numbers in both
// games up to Platinum's last
unsigned int BwPlat_GetLevelExp(unsigned short species, unsigned short form, unsigned char level);
// BW2's MOVE_CATEGORY_*: 0 status, 1 physical, 2 special
int BwPlat_GetMoveCategory(unsigned short move);
bool BwPlat_IsCaught(unsigned short species);
// Whether the view's effects leave out their cries, where Platinum's battle plays its own (wild encounters)
void BwBattle_SetCriesMuted(bool muted);
// A Pokémon's cry through Platinum's sound system, panned -128 (left) to 127 (right)
void BwPlat_PlayCry(unsigned short species, unsigned char form, int pan);
bool BwPlat_IsCryPlaying(void);
// Text in Platinum's font for the view's gauges and actors. Strings are Platinum's character codes, ending at 0xffff.
// DrawText draws at x, y (the top of the font's glyphs) into 16-color tiles (a tile's rows after each other, the
// tiles of a row of tiles after each other), tileWidth tiles across, in the given color indices, and returns the
// width drawn; TextWidth measures it
unsigned int BwPlat_DrawText(const unsigned short *text, void *tiles, unsigned int tileWidth, unsigned int tileHeight,
    int x, int y, unsigned char letter, unsigned char shadow, unsigned char background);
unsigned int BwPlat_TextWidth(const unsigned short *text);
// Writes a number in digits characters (padding 0 none, 1 spaces, 2 zeroes, as BW2's NUM_PAD_*), ending at 0xffff
void BwPlat_FormatNumber(unsigned short *dest, unsigned int size, int number, unsigned int digits, unsigned int pad);

#ifdef __cplusplus
}
#endif

#endif // BWBATTLE_BWBATTLE_H
