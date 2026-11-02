#include "bwbattle/bw_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/overworld_weather.h"
#include "constants/string.h"
#include "constants/charcode.h"
#include "constants/battle/condition.h"
#include "generated/battle_backgrounds.h"
#include "generated/battle_terrains.h"
#include "generated/trainer_classes.h"
#include "generated/abilities.h"
#include "generated/battle_message_tags.h"
#include "generated/text_banks.h"
#include "res/text/bank/battle_strings.h"
#include "message.h"
#include "string_gf.h"

#include "bwbattle/bwbattle.h"
#include <simulator/sim.h>
#include "port/sim_config_prj.h"
#include "port/battle_timing.h"

#include "battle/battle_display.h"
#include "battle/healthbox.h"
#include "battle/party_gauge.h"
#include "battle/battle_system.h"
#include "battle/btlcmd.h"
#include "battle/battle_lib.h"
#include "generated/battle_sub_animations.h"
#include "struct_defs/battler_data.h"
#include "battle_anim/struct_ov12_02223764.h"
#include "struct_defs/trainer.h"
#include "pokemon_sprite.h"
#include "sprite_system.h"
#include "palette.h"
#include "party.h"
#include "pokemon.h"
#include "rtc.h"
#include "gx_layers.h"
#include "graphics.h"
#include "flags.h"
#include "render_window.h"
#include "bg_window.h"

// Platinum's battle through BW2's view (docs/bwbattle.md): this sets the view up from the battle and feeds it the
// Pokémon on the field. It is Platinum's code, and talks to bwbattle through bwbattle.h.

// Platinum's battlers are the view's positions 0 and 1 in a single battle and 2 to 5 in a double battle: in both
// games the player's are even and the opponent's odd, in the same places
#define BW_ADAPTER_BATTLERS MAX_BATTLERS
#define BW_VIEW_DOUBLES_POS_FIRST 2

// What the view shows of a battler: the Pokémon the display commands last set up (BwAdapter_SetBattlerMon), shown
// while Platinum's sprite for the battler exists, hidden while that sprite is
// While the intro, a send-out or a recall plays (BwAdapter_On*), the step queue adds and removes the battler's
// Pokémon in step with BW2's effects instead
typedef struct {
    BOOL known;   // The display commands have set the Pokémon
    BOOL changed; // It changed since it was added to the view
    BOOL added;
    BOOL visible;
    BOOL scripted; // The queue adds it: not added before, and kept while Platinum's sprite doesn't exist yet
    BOOL leaving;  // A recall plays: the queue removes it
    BOOL catching; // BW2 keeps the sprite while Platinum deletes it and recreates it after a breakout
    BOOL removed;  // The queue removed it: not added again until Platinum's sprite is gone
    BOOL bwTrainer; // The battler's trainer is BW2's, so Platinum's trainer sprite isn't drawn
    BOOL platinumTrainer; // Its animated cells replace the BW2 stand-in
    u8 trainerPos;
    const NNSG2dCellData *trainerCell;
    BOOL realSprite; // Platinum restored the Pokémon's own sprite over its substitute until it refreshes it
    BwBattleMon mon;
    u16 nickname[MON_NAME_LEN + 1];
    // The BW2 gauge that stands in for Platinum's healthbox (BwAdapter_SyncGauges), and what it shows
    BOOL gaugeShown;
    BOOL gaugeVisible;
    u16 announcedAbility; // Deduplicates one activation affecting several Pokémon in doubles
    BwBattleMon gaugeMon;
    s32 gaugeHp;
    BOOL gaugeHpMoving;
    s32 gaugeExp;
    u8 gaugeStatus;
} BwAdapterBattler;

// BW2's effects of the battle start and the send-outs (btlv_scu.c, btlv_core.c)
#define BW_EFFECT_WILD_APPEAR 0x231
#define BW_EFFECT_TO_PLAYER 0x232 // The camera turns to the player's side before the player throws
#define BW_EFFECT_TO_PLAYER_AND_PARTNER 0x233 // The same, with the partner beside the player
#define BW_EFFECT_PLAYER_SEND_OUT 0x234
#define BW_EFFECT_TRAINER_APPEAR 0x237
#define BW_EFFECT_TRAINER_THROW 0x238
#define BW_EFFECT_TRAINER_SEND_OUT 0x239
#define BW_EFFECT_FAINT 0x23b
#define BW_EFFECT_RECALL 0x26c
#define BW_EFFECT_SEND_OUT 0x26d // A send-out during the battle, on either side
#define BW_EFFECT_TRAINER_SLIDE_IN 0x270
#define BW_EFFECT_TRAINER_SLIDE_OUT 0x271

// What BW2's battle start, send-outs and recalls do (btlv_scu.c), one after another, each effect to its end
enum {
    BW_STEP_ADD_MON,
    BW_STEP_REMOVE_MON,
    BW_STEP_EFFECT,
    BW_STEP_SET_TRAINER,
    BW_STEP_REMOVE_TRAINER,
};

typedef struct {
    u8 type;
    u8 battler;
    u8 trainerPos;
    u8 participants; // Battlers whose commands have arrived for a shared intro effect
    u16 trainerClass;
    s16 platinumClass; // A Platinum class whose sprite stands in for BW2's (-1 none)
    u16 effect;
    int effectPos; // A view position, -1 for the field
} BwAdapterStep;

#define BW_STEPS_MAX 16

static BOOL sActive;
static BOOL sFastIntro;
static BOOL sFastIntroBattle;
static BOOL sFastPlayerSendOut;
// BW2's player stands on the field from the camera's turn to the player's side to the send-out
static BOOL sPlayerThrowing;
// A partner fights beside the player (a 2 vs 2 battle): BW2's multi battle, whose send-out brings the partner in
// beside the player (0x233)
static BOOL sPartner;
static BattleSystem *sBattleSys;
// Platinum draws the screen itself for a while (BwAdapter_SetSuspended)
static BOOL sSuspended;
static BOOL sDoubles;
static BwBattleSetup sSetup;
// A move animation BW2's view plays in place of Platinum's (BwAdapter_StartMoveAnimation)
static BOOL sMoveAnimPending;
static BwAdapterBattler sBattlers[BW_ADAPTER_BATTLERS];
static BwAdapterStep sSteps[BW_STEPS_MAX];
static int sStepCount;
static BOOL sStepEffectRunning;

BOOL BwAdapter_IsEnabled(void)
{
    return SIM_Config_prj_GetConfig()->bw2BattleVisuals && BwBattle_AssetsReady();
}

BOOL BwAdapter_IsActive(void)
{
    return sActive;
}

BOOL BwAdapter_IsDrawing(void)
{
    return sActive && !sSuspended;
}

// BW2's major status conditions (CONDITION_* of BW2's constants/battle.h) from Platinum's
static unsigned char BwAdapter_Status(u32 condition, bool *badlyPoisoned)
{
    *badlyPoisoned = (condition & MON_CONDITION_TOXIC) != 0;
    if (condition & MON_CONDITION_SLEEP) {
        return 2;
    }
    if (condition & (MON_CONDITION_POISON | MON_CONDITION_TOXIC)) {
        return 5;
    }
    if (condition & MON_CONDITION_BURN) {
        return 4;
    }
    if (condition & MON_CONDITION_FREEZE) {
        return 3;
    }
    if (condition & MON_CONDITION_PARALYSIS) {
        return 1;
    }
    return 0;
}

static void BwAdapter_FillMon(BwBattleMon *dest, Pokemon *mon)
{
    memset(dest, 0, sizeof(*dest));
    dest->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    dest->form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    dest->personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
    dest->level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
    dest->sex = Pokemon_GetGender(mon); // Male, female, genderless in both games
    dest->ball = Pokemon_GetValue(mon, MON_DATA_POKEBALL, NULL);
    dest->status = BwAdapter_Status(Pokemon_GetValue(mon, MON_DATA_STATUS, NULL), &dest->badlyPoisoned);
    dest->shiny = Pokemon_IsShiny(mon);
    dest->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL);
    dest->showSex = TRUE;
    dest->hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    dest->maxHp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    dest->exp = Pokemon_GetValue(mon, MON_DATA_EXPERIENCE, NULL);
}

// The first Pokémon of a party that can battle, as Platinum sends out
static Pokemon *BwAdapter_LeadMon(Party *party)
{
    for (int i = 0; i < Party_GetCurrentCount(party); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (Pokemon_GetValue(mon, MON_DATA_HP, NULL) != 0 && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            return mon;
        }
    }
    return NULL;
}

// BW2's arena for Platinum's battle background, by BACKGROUND_*: BW2's background type, the nearest look where
// BW2 has no match (docs/bwbattle.md). Its 21 types, from a survey of archive 151: 0 and 1 a grass field, 2 and 3
// a town, 4 and 5 a cave, 6 a forest, 7 a mountain, 8 the sea, 9 a desert, 10 a beach, 11 to 14 indoor rooms
// (purple, gold, red, grey), 15 and 17 a rocky plain, 16 a starry sky, 18 snow, 19 a grey hall, 20 an ice cave
static const u8 sBw2BgTypeOfBackground[BACKGROUND_MAX] = {
    [BACKGROUND_PLAIN] = 0,
    [BACKGROUND_WATER] = 8,
    [BACKGROUND_CITY] = 2,
    [BACKGROUND_FOREST] = 6,
    [BACKGROUND_MOUNTAIN] = 7,
    [BACKGROUND_SNOW] = 18,
    [BACKGROUND_INDOORS_1] = 14,
    [BACKGROUND_INDOORS_2] = 14,
    [BACKGROUND_INDOORS_3] = 19,
    [BACKGROUND_CAVE_1] = 4,
    [BACKGROUND_CAVE_2] = 5,
    [BACKGROUND_CAVE_3] = 20,
    [BACKGROUND_AARON] = 12,
    [BACKGROUND_BERTHA] = 15,
    [BACKGROUND_FLINT] = 13,
    [BACKGROUND_LUCIAN] = 11,
    [BACKGROUND_CYNTHIA] = 16,
    [BACKGROUND_DISTORTION_WORLD] = 16,
    [BACKGROUND_BATTLE_TOWER] = 19,
    [BACKGROUND_BATTLE_FACTORY] = 19,
    [BACKGROUND_BATTLE_ARCADE] = 19,
    [BACKGROUND_BATTLE_CASTLE] = 19,
    [BACKGROUND_BATTLE_HALL] = 19,
};

// BW2's terrain, which picks the platforms of the outdoor types (0 to 10), for Platinum's TERRAIN_*: 0 grass, 1
// dirt, 2 sand, 4 the type's own ground (rock in a cave or on a mountain), 6 a pond, 9 a marsh. The indoor types
// have one platform for every terrain
static const u8 sBw2TerrainOfTerrain[TERRAIN_MAX] = {
    [TERRAIN_PLAIN] = 1,
    [TERRAIN_SAND] = 2,
    [TERRAIN_GRASS] = 0,
    [TERRAIN_PUDDLE] = 6,
    [TERRAIN_MOUNTAIN] = 4,
    [TERRAIN_CAVE] = 4,
    [TERRAIN_SNOW] = 4,
    [TERRAIN_WATER] = 6,
    [TERRAIN_ICE] = 4,
    [TERRAIN_BUILDING] = 4,
    [TERRAIN_GREAT_MARSH] = 9,
    [TERRAIN_BRIDGE] = 4,
};

// BW2's field weather for Platinum's overworld weather, which picks the arena's light (BW2's battle lights, archive 61,
// from system/field_light_status.c's table). BW2's weathers aren't named in pokebw2, so they are picked by their light
// files: 1 and 9 dim the day's cycle (overcast), 8 also with a bluer night (rain), 7 and 6 stay dark all day (heavy rain,
// storm), and 11, 14, 10 and 13 stay bright (snow, a blizzard, hail, sand). 0 keeps the time of day's light
static const u8 sBw2WeatherOfOverworldWeather[] = {
    [OVERWORLD_WEATHER_CLOUDY] = 9,
    [OVERWORLD_WEATHER_RAINING] = 1,
    [OVERWORLD_WEATHER_HEAVY_RAIN] = 7,
    [OVERWORLD_WEATHER_THUNDERSTORM] = 6,
    [OVERWORLD_WEATHER_SNOWING] = 11,
    [OVERWORLD_WEATHER_HEAVY_SNOW] = 11,
    [OVERWORLD_WEATHER_BLIZZARD] = 14,
    [OVERWORLD_WEATHER_SLOW_ASHFALL] = 9,
    [OVERWORLD_WEATHER_SANDSTORM] = 13,
    [OVERWORLD_WEATHER_HAILING] = 10,
    [OVERWORLD_WEATHER_FOG] = 3,
    [OVERWORLD_WEATHER_DEEP_FOG] = 12,
};

static void BwAdapter_GetArena(BattleSystem *battleSys, BwBattleSetup *setup)
{
    enum BattleBackground background = BattleSystem_GetBackground(battleSys);
    enum BattleTerrain terrain = BattleSystem_GetTerrain(battleSys);

    int weather = BattleSystem_GetFieldWeather(battleSys);
    // Debug: PLATINUM_BWBATTLE_WEATHER=<overworld weather> stands in for the map's
    const char *debugWeather = getenv("PLATINUM_BWBATTLE_WEATHER");

    if (debugWeather != NULL) {
        weather = atoi(debugWeather);
    }

    setup->weather = weather >= 0 && weather < (int)NELEMS(sBw2WeatherOfOverworldWeather)
        ? sBw2WeatherOfOverworldWeather[weather]
        : 0;
    setup->bgType = background < BACKGROUND_MAX ? sBw2BgTypeOfBackground[background] : 0;
    setup->terrain = terrain < TERRAIN_MAX ? sBw2TerrainOfTerrain[terrain] : 0;
}

// Platinum's battle background (BG 3) is BW2's stage and field here: it would show wherever the 3D scene is clear,
// as when an effect hides the stage and field for its own background. It is hidden while the view draws, and shown
// again, if Platinum shows it, when the view stops
static void BwAdapter_ShowPlatinumBackground(BOOL show)
{
    int planes = GXLayers_EngineAGetLayers();

    GX_SetVisiblePlane(show ? planes : planes & ~GX_PLANEMASK_BG3);
}

// Platinum fades the screen through its palettes, which BW2's 3D scene doesn't use: a fade of the whole main BG
// palette (to black or white, the battle's start and end, the menus) is mirrored on the 3D layer (BG 0) with the
// brightness effect. The blend registers Platinum had are put back when the fade is over, unless Platinum set them
// itself meanwhile
static BOOL sFadeBlendSet;
static u16 sFadeSavedBlend[3];
static u16 sFadeWrittenBlend[3];

static void BwAdapter_RestoreFadeBlend(void)
{
    if (sFadeBlendSet) {
        BwBattleView_SetObjFade(0, 0);
        if (reg_G2_BLDCNT == sFadeWrittenBlend[0] && reg_G2_BLDY == sFadeWrittenBlend[2]) {
            reg_G2_BLDCNT = sFadeSavedBlend[0];
            reg_G2_BLDALPHA = sFadeSavedBlend[1];
            reg_G2_BLDY = sFadeSavedBlend[2];
        }
        sFadeBlendSet = FALSE;
    }
}

static void BwAdapter_MirrorFade(BattleSystem *battleSys)
{
    PaletteFadeControl *fade = &BattleSystem_GetPaletteData(battleSys)->buffers[PLTTBUF_MAIN_BG].selected;
    int level = fade->unfadedMask == 0xffff ? fade->cur : 0;

    if (level == 0) {
        BwAdapter_RestoreFadeBlend();
        return;
    }
    if (!sFadeBlendSet) {
        sFadeSavedBlend[0] = reg_G2_BLDCNT;
        sFadeSavedBlend[1] = reg_G2_BLDALPHA;
        sFadeSavedBlend[2] = reg_G2_BLDY;
        sFadeBlendSet = TRUE;
    }
    // Toward white brightens; black and any other color darken. BW2's 2D sprites (the gauges) have palettes of their
    // own (SIM_SetObjExtension), which fade the way Platinum's do
    G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG0, fade->target == 0x7fff ? level : -level);
    BwBattleView_SetObjFade(level, fade->target);
    sFadeWrittenBlend[0] = reg_G2_BLDCNT;
    sFadeWrittenBlend[2] = reg_G2_BLDY;
}

// Platinum's textbox and menus (BG 1) are blended over BW2's scene as BW2's message window is (btlv_core.c): BG 1
// at full weight over the layers under it at 3 of 16, which BW2_TEXTBOX_* below makes a band of the scene darkened
// under white text. Platinum's battle keeps a blend without a first target, for its translucent sprites
// (BattleAnimSystem), which the textbox's takes the place of; the registers are left alone while Platinum blends
// something else, and while a fade is mirrored on them, which puts the textbox's back after
#define BW_TEXTBOX_WEIGHT 16
#define BW_TEXTBOX_SCENE_WEIGHT 3
#define BW_BLEND_FIRST_TARGETS 0x3f

static u16 sTextboxBlend;
static u16 sTextboxSavedBlend[2];

static void BwAdapter_BlendTextbox(BOOL blend)
{
    if (blend) {
        if (sFadeBlendSet || (sTextboxBlend != 0 && reg_G2_BLDCNT == sTextboxBlend)) {
            return;
        }
        if (reg_G2_BLDCNT != 0 && (reg_G2_BLDCNT & BW_BLEND_FIRST_TARGETS) != 0) {
            return;
        }
        sTextboxSavedBlend[0] = reg_G2_BLDCNT;
        sTextboxSavedBlend[1] = reg_G2_BLDALPHA;
        G2_SetBlendAlpha(GX_BLEND_PLANEMASK_BG1,
            GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ
                | GX_BLEND_PLANEMASK_BD,
            BW_TEXTBOX_WEIGHT, BW_TEXTBOX_SCENE_WEIGHT);
        sTextboxBlend = reg_G2_BLDCNT;
    } else if (sTextboxBlend != 0) {
        if (reg_G2_BLDCNT == sTextboxBlend) {
            reg_G2_BLDCNT = sTextboxSavedBlend[0];
            reg_G2_BLDALPHA = sTextboxSavedBlend[1];
        }
        sTextboxBlend = 0;
    }
}

// Platinum's textbox drawn as BW2's message window: Platinum's message box frame (render_window.c, 18 tiles from
// BW_TEXTBOX_FRAME_TILE in palette BW_TEXTBOX_FRAME_PALETTE) spans the screen's width on rows 18 to 23, where BW2's frame
// is. Its tiles become BW2's (the top edge along the top row, the bottom edge along the bottom row, the fill between)
// and its palette BW2's, and the text palettes of the messages and the command menus take BW2's text, shadow and fill
// colors. Platinum loads these again with the battle screen and its menus, after which they are styled again
#define BW_TEXTBOX_FRAME_TILE 1
#define BW_TEXTBOX_FRAME_TILES 18
#define BW_TEXTBOX_FRAME_PALETTE 10
#define BW_TEXTBOX_MESSAGE_PALETTE 11
#define BW_TEXTBOX_MENU_PALETTE 13
// The text colors of Platinum's message font (font.c)
#define BW_TEXTBOX_PLATINUM_TEXT 1
#define BW_TEXTBOX_PLATINUM_SHADOW 2
#define BW_TEXTBOX_PLATINUM_FILL 15
// BW2's frame tiles, by MakeFrameScreen's order (bmp_win.c)
#define BW2_FRAME_TOP 1
#define BW2_FRAME_SIDE 3
#define BW2_FRAME_BOTTOM 6

static BOOL sTextboxLoaded;
static u8 sTextboxFrameTiles[BW_TEXTBOX_FRAME_TILES * TILE_SIZE_4BPP];
static u16 sTextboxColors[16];

// Which of BW2's tiles each of Platinum's frame tiles becomes: its top row is 0 to 5, its sides 6 to 11 and its bottom
// row 12 to 17 (DrawMessageBoxFrame); 8, which the frame doesn't use, is the fill too
static const u8 sBw2FrameTileOfPlatinum[BW_TEXTBOX_FRAME_TILES] = {
    BW2_FRAME_TOP, BW2_FRAME_TOP, BW2_FRAME_TOP, BW2_FRAME_TOP, BW2_FRAME_TOP, BW2_FRAME_TOP,
    BW2_FRAME_SIDE, BW2_FRAME_SIDE, BW2_FRAME_SIDE, BW2_FRAME_SIDE, BW2_FRAME_SIDE, BW2_FRAME_SIDE,
    BW2_FRAME_BOTTOM, BW2_FRAME_BOTTOM, BW2_FRAME_BOTTOM, BW2_FRAME_BOTTOM, BW2_FRAME_BOTTOM, BW2_FRAME_BOTTOM,
};

static BOOL BwAdapter_LoadTextbox(void)
{
    u8 tiles[BW_MSG_WINDOW_TILES * TILE_SIZE_4BPP];

    if (!sTextboxLoaded && BwBattleView_GetMessageWindow(tiles, sTextboxColors)) {
        for (int i = 0; i < BW_TEXTBOX_FRAME_TILES; i++) {
            memcpy(&sTextboxFrameTiles[i * TILE_SIZE_4BPP], &tiles[sBw2FrameTileOfPlatinum[i] * TILE_SIZE_4BPP],
                TILE_SIZE_4BPP);
        }
        sTextboxLoaded = TRUE;
    }
    return sTextboxLoaded;
}

// Sets a color where Platinum loaded it; the faded color too where the palette isn't faded
static BOOL BwAdapter_SetBgColor(PaletteData *paletteData, int index, u16 color)
{
    u16 *unfaded = PaletteData_GetUnfadedBuffer(paletteData, PLTTBUF_MAIN_BG);
    u16 *faded = PaletteData_GetFadedBuffer(paletteData, PLTTBUF_MAIN_BG);

    if (unfaded[index] == color) {
        return FALSE;
    }
    if (faded[index] == unfaded[index]) {
        faded[index] = color;
    }
    unfaded[index] = color;
    return TRUE;
}

static void BwAdapter_StyleTextPalette(PaletteData *paletteData, int palette)
{
    BwAdapter_SetBgColor(paletteData, PLTT_DEST(palette) + BW_TEXTBOX_PLATINUM_TEXT, sTextboxColors[BW_MSG_WINDOW_TEXT]);
    BwAdapter_SetBgColor(paletteData, PLTT_DEST(palette) + BW_TEXTBOX_PLATINUM_SHADOW,
        sTextboxColors[BW_MSG_WINDOW_SHADOW]);
    BwAdapter_SetBgColor(paletteData, PLTT_DEST(palette) + BW_TEXTBOX_PLATINUM_FILL, sTextboxColors[BW_MSG_WINDOW_FILL]);
}

// Platinum's textbox frame is on tile rows 18 to 23 (the message window at row 19, 4 rows), the screen's width across
#define BW_TEXTBOX_TOP_ROW 18
#define BW_TEXTBOX_END_ROW 24

// BW2's window spans the screen, so in a wider view BG 1's edge dots carry on into the sides on the window's rows
// (libntr's SIM_SetFieldEdgeRows); its frame columns are the same all down, so the band continues as it is
static void BwAdapter_ExtendTextbox(BOOL extend)
{
    if (extend) {
        SIM_SetFieldEdgeRows(1, BW_TEXTBOX_TOP_ROW * 8, BW_TEXTBOX_END_ROW * 8);
    } else {
        SIM_SetFieldEdgeRows(-1, 0, 0);
    }
}

void BwAdapter_StyleTextbox(BattleSystem *battleSys)
{
    PaletteData *paletteData;
    Window *window;
    const u8 *chars;

    if (!sActive || sSuspended || !BwAdapter_LoadTextbox()) {
        return;
    }
    paletteData = BattleSystem_GetPaletteData(battleSys);
    for (int i = 0; i < 16; i++) {
        BwAdapter_SetBgColor(paletteData, PLTT_DEST(BW_TEXTBOX_FRAME_PALETTE) + i, sTextboxColors[i]);
    }
    BwAdapter_StyleTextPalette(paletteData, BW_TEXTBOX_MESSAGE_PALETTE);
    BwAdapter_StyleTextPalette(paletteData, BW_TEXTBOX_MENU_PALETTE);

    BwAdapter_ExtendTextbox(TRUE);
    // Platinum frees its message window while it changes screens (the bag, the party)
    window = BattleSystem_GetWindow(battleSys, 0);
    if (!Window_IsInUse(window)) {
        return;
    }
    chars = Bg_GetCharPtr(BG_LAYER_MAIN_1);
    if (memcmp(&chars[BW_TEXTBOX_FRAME_TILE * TILE_SIZE_4BPP], sTextboxFrameTiles, sizeof(sTextboxFrameTiles)) != 0) {
        Bg_LoadTiles(BattleSystem_GetBgConfig(battleSys), BG_LAYER_MAIN_1, sTextboxFrameTiles,
            sizeof(sTextboxFrameTiles), BW_TEXTBOX_FRAME_TILE);
        // The scroll cursor is made from the frame's tiles (DrawMessageBoxScrollCursor)
        Window_DrawMessageBoxWithScrollCursor(window, TRUE, BW_TEXTBOX_FRAME_TILE, BW_TEXTBOX_FRAME_PALETTE);
    }
}

// BW2's trainer class (BW2's constants/trainer_classes.h) for Platinum's, where BW2 has the same trainer or kind of
// trainer; the others (BW_NO_TRAINER) keep Platinum's sprite (docs/bwbattle.md). BW2's Sinnoh gym leaders and
// Cynthia are the Pokémon World Tournament's
#define BW_NO_TRAINER 0xffff

static const u16 sBw2ClassOfTrainerClass[] = {
    [TRAINER_CLASS_YOUNGSTER] = 2,
    [TRAINER_CLASS_LASS] = 3,
    [TRAINER_CLASS_TWINS] = 16,
    [TRAINER_CLASS_HIKER] = 74,
    [TRAINER_CLASS_BATTLE_GIRL] = 64,
    [TRAINER_CLASS_FISHERMAN] = 29,
    [TRAINER_CLASS_CYCLIST_MALE] = 90,
    [TRAINER_CLASS_CYCLIST_FEMALE] = 91,
    [TRAINER_CLASS_BLACK_BELT] = 51,
    [TRAINER_CLASS_ARTIST] = 33,
    [TRAINER_CLASS_BREEDER_MALE] = 17,
    [TRAINER_CLASS_BREEDER_FEMALE] = 18,
    [TRAINER_CLASS_POKEFAN_MALE] = 59,
    [TRAINER_CLASS_POKEFAN_FEMALE] = 60,
    [TRAINER_CLASS_ACE_TRAINER_MALE] = 50,
    [TRAINER_CLASS_ACE_TRAINER_FEMALE] = 49,
    [TRAINER_CLASS_WAITRESS] = 9,
    [TRAINER_CLASS_VETERAN] = 70,
    [TRAINER_CLASS_RICH_BOY] = 41,
    [TRAINER_CLASS_LADY] = 42,
    [TRAINER_CLASS_GENTLEMAN] = 76,
    [TRAINER_CLASS_SOCIALITE] = 75,
    [TRAINER_CLASS_BEAUTY] = 179,
    [TRAINER_CLASS_POLICEMAN] = 86,
    [TRAINER_CLASS_RANGER_MALE] = 24,
    [TRAINER_CLASS_RANGER_FEMALE] = 25,
    [TRAINER_CLASS_SCIENTIST] = 52,
    [TRAINER_CLASS_SWIMMER_MALE] = 84,
    [TRAINER_CLASS_SWIMMER_FEMALE] = 85,
    [TRAINER_CLASS_PSYCHIC_MALE] = 35,
    [TRAINER_CLASS_PSYCHIC_FEMALE] = 36,
    [TRAINER_CLASS_GUITARIST] = 146,
    [TRAINER_CLASS_ROUGHNECK] = 57,
    [TRAINER_CLASS_CLOWN] = 32, // Harlequin
    [TRAINER_CLASS_WORKER] = 26,
    [TRAINER_CLASS_SCHOOL_KID_MALE] = 4,
    [TRAINER_CLASS_SCHOOL_KID_FEMALE] = 5,
    [TRAINER_CLASS_LEADER_ROARK] = 167,
    [TRAINER_CLASS_LEADER_BYRON] = 172,
    [TRAINER_CLASS_CHAMPION_CYNTHIA] = 100,
    [TRAINER_CLASS_CHAMPION_IRIS] = 193,
    [TRAINER_CLASS_LEADER_GARDENIA] = 168,
    [TRAINER_CLASS_LEADER_WAKE] = 171,
    [TRAINER_CLASS_LEADER_MAYLENE] = 170,
    [TRAINER_CLASS_LEADER_FANTINA] = 169,
    [TRAINER_CLASS_LEADER_CANDICE] = 173,
    [TRAINER_CLASS_LEADER_VOLKNER] = 174,
    [TRAINER_CLASS_PARASOL_LADY] = 65,
    [TRAINER_CLASS_WAITER] = 8,
    [TRAINER_CLASS_MAID] = 87,
    // The closest of BW2's for Platinum's regular trainers BW2 has no class for: by look, or by what they do. BW2's
    // Backers are pairs, for Platinum's classes of two
    [TRAINER_CLASS_CAMPER] = 2, // Youngster
    [TRAINER_CLASS_PICNICKER] = 3, // Lass
    [TRAINER_CLASS_BUG_CATCHER] = 15, // Preschooler (boy)
    [TRAINER_CLASS_AROMA_LADY] = 42, // Lady
    [TRAINER_CLASS_COWGIRL] = 25, // Pokemon Ranger (female)
    [TRAINER_CLASS_JOGGER] = 53, // Striker
    [TRAINER_CLASS_POKE_KID] = 14, // Preschooler (girl)
    [TRAINER_CLASS_YOUNG_COUPLE] = 68, // Backers (male)
    [TRAINER_CLASS_NINJA_BOY] = 4, // School Kid (boy); BW2's Ninja Boy has no sprite
    [TRAINER_CLASS_DRAGON_TAMER] = 57, // Roughneck
    [TRAINER_CLASS_BIRD_KEEPER] = 28, // Backpacker (female)
    [TRAINER_CLASS_DOUBLE_TEAM] = 69, // Backers (female)
    [TRAINER_CLASS_COLLECTOR] = 59, // Pokefan (male)
    [TRAINER_CLASS_TUBER_MALE] = 15, // Preschooler (boy)
    [TRAINER_CLASS_TUBER_FEMALE] = 14, // Preschooler (girl)
    [TRAINER_CLASS_SAILOR] = 29, // Fisherman
    [TRAINER_CLASS_SIS_AND_BRO] = 68, // Backers (male)
    [TRAINER_CLASS_RUIN_MANIAC] = 74, // Hiker
    [TRAINER_CLASS_PI] = 76, // Gentleman
    [TRAINER_CLASS_ACE_TRAINER_SNOW_MALE] = 50, // Ace Trainer (male)
    [TRAINER_CLASS_ACE_TRAINER_SNOW_FEMALE] = 49, // Ace Trainer (female)
    [TRAINER_CLASS_SKIER_MALE] = 90, // Cyclist (male)
    [TRAINER_CLASS_SKIER_FEMALE] = 91, // Cyclist (female)
    [TRAINER_CLASS_BELLE_AND_PA] = 69, // Backers (female)
    [TRAINER_CLASS_RANCHER] = 70, // Veteran (male)
    [TRAINER_CLASS_INTERVIEWERS] = 68, // Backers (male)
    [TRAINER_CLASS_CAMERAMAN] = 26, // Worker
    [TRAINER_CLASS_REPORTER] = 48, // Clerk (female)
    [TRAINER_CLASS_IDOL] = 30, // Musician
};

// Platinum's trainers BW2 has no class for at all (the story's, the Elite Four, the Frontier Brains) keep Platinum's
// sprite, in BW2's scene: BW2's trainer stands at the trainer's place, so that BW2's effects move it, and is drawn as
// Platinum's sprite (BwBattleView_SetTrainerImage). BW2's Ace Trainer is the stand-in
#define BW_STAND_IN_TRAINER 50
// Platinum's sprites' sizes beside BW2's, in percent: the front ones as they are, the back ones as big as BW2's
#define BW_PLATINUM_FRONT_SCALE 100
#define BW_PLATINUM_BACK_SCALE 200
// The partner's place beside the player in BW2's multi battle (0x233)
#define BW_TRAINER_POS_PARTNER 12
#define BW_PLATINUM_TRAINER_SIZE 80
#define BW_PLATINUM_TRAINER_IMAGE_SIZE (BW_PLATINUM_TRAINER_SIZE * BW_PLATINUM_TRAINER_SIZE / 2)

// OAM sizes in dots, by shape (square, wide, tall) and size
static const u8 sOamWidth[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 sOamHeight[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

// The cell's transfer offset selects its tiles in the original character resource, even while Platinum uploads
// only the current frame into OBJ VRAM. An explicit origin keeps moving cells inside the bitmap without cropping.
static void BwAdapter_DrawPlatinumTrainerCell(const NNSG2dCharacterData *chars, const NNSG2dCellDataBank *cells,
    int cellIndex, int originX, int originY, u8 *image)
{
    const NNSG2dCellData *cell = NNS_G2dGetCellDataByIdx(cells, cellIndex);
    const u8 *pixels = chars->pRawData;
    u32 base = cells->pVramTransferData != NULL ? cells->pVramTransferData->pCellTransferDataArray[cellIndex].srcDataOffset : 0;
    u32 unit = TILE_SIZE_4BPP << (cells->mappingMode <= NNS_G2D_CHARACTERMAPPING_1D_256 ? cells->mappingMode : 0);

    memset(image, 0, BW_PLATINUM_TRAINER_IMAGE_SIZE);
    for (int i = 0; i < cell->numOAMAttrs; i++) {
        const NNSG2dCellOAMAttrData *oam = &cell->pOamAttrArray[i];
        int shape = (oam->attr0 >> 14) & 3;
        int size = (oam->attr1 >> 14) & 3;
        int ox = (oam->attr1 & 0x100) ? (int)(oam->attr1 & 0x1ff) - 0x200 : (oam->attr1 & 0x1ff);
        int oy = (s8)(oam->attr0 & 0xff);
        u32 tiles = base + (oam->attr2 & 0x3ff) * unit;

        if (shape > 2) {
            continue;
        }
        // 1D mapping: the OAM's tiles one after another, row by row
        for (int y = 0; y < sOamHeight[shape][size]; y++) {
            for (int x = 0; x < sOamWidth[shape][size]; x++) {
                int dx = ox + x + originX;
                int dy = oy + y + originY;
                int sx = !(oam->attr0 & 0x100) && (oam->attr1 & 0x1000) ? sOamWidth[shape][size] - 1 - x : x;
                int sy = !(oam->attr0 & 0x100) && (oam->attr1 & 0x2000) ? sOamHeight[shape][size] - 1 - y : y;
                u32 tile = tiles + ((sy / 8) * (sOamWidth[shape][size] / 8) + sx / 8) * TILE_SIZE_4BPP;
                u32 at = tile + (sy % 8) * 4 + (sx % 8) / 2;
                u8 dot;

                if (dx < 0 || dy < 0 || dx >= BW_PLATINUM_TRAINER_SIZE || dy >= BW_PLATINUM_TRAINER_SIZE
                    || at >= chars->szByte) {
                    continue;
                }
                dot = (sx & 1) ? pixels[at] >> 4 : pixels[at] & 0xf;
                if (dot != 0) {
                    u8 *dst = &image[(dy * BW_PLATINUM_TRAINER_SIZE + dx) / 2];

                    *dst = (dx & 1) ? (*dst & 0x0f) | (dot << 4) : (*dst & 0xf0) | dot;
                }
            }
        }
    }
}

// Platinum's sprite of the class, front (an opponent) or back (a partner), as its first cell draws it: 80 by 80 dots of 4
// bits, the cell's middle in the middle, with its 16 colors
static BOOL BwAdapter_LoadPlatinumTrainerImage(int trainerClass, int face, u8 *image, u16 *colors)
{
    TrainerClassGraphicIndex gfx;
    NNSG2dCharacterData *chars;
    NNSG2dCellDataBank *cells;
    NNSG2dPaletteData *palette;
    void *charFile;
    void *cellFile;
    void *plttFile;
    BOOL loaded = FALSE;

    SpriteSystem_SetTrainerClassGraphicsIndex(trainerClass, face, &gfx);
    charFile = Graphics_GetCharData(gfx.narcID, gfx.tiles, FALSE, &chars, HEAP_ID_BATTLE);
    cellFile = Graphics_GetCellBank(gfx.narcID, gfx.cells, FALSE, &cells, HEAP_ID_BATTLE);
    plttFile = Graphics_GetPlttData(gfx.narcID, gfx.palette, &palette, HEAP_ID_BATTLE);
    if (charFile != NULL && cellFile != NULL && plttFile != NULL && cells->numCells > 0) {
        BwAdapter_DrawPlatinumTrainerCell(chars, cells, 0, BW_PLATINUM_TRAINER_SIZE / 2,
            BW_PLATINUM_TRAINER_SIZE / 2, image);
        memcpy(colors, palette->pRawData, 16 * sizeof(u16));
        loaded = TRUE;
    }
    if (charFile != NULL) {
        Heap_Free(charFile);
    }
    if (cellFile != NULL) {
        Heap_Free(cellFile);
    }
    if (plttFile != NULL) {
        Heap_Free(plttFile);
    }
    return loaded;
}

static void BwAdapter_SetPlatinumTrainerImage(unsigned int viewPos, int trainerClass, int face)
{
    u8 *image = Heap_Alloc(HEAP_ID_BATTLE, BW_PLATINUM_TRAINER_IMAGE_SIZE);
    u16 colors[16];

    if (BwAdapter_LoadPlatinumTrainerImage(trainerClass, face, image, colors)) {
        BwBattleView_SetTrainerImage(viewPos, image, BW_PLATINUM_TRAINER_SIZE, BW_PLATINUM_TRAINER_SIZE, colors,
            face == FACE_BACK ? BW_PLATINUM_BACK_SCALE : BW_PLATINUM_FRONT_SCALE);
    }
    Heap_Free(image);
}

// Platinum still advances its hidden trainer sprite. Copy only changed cells into BW2's scene, keeping BW2's
// camera, position, fades and slide effects and Platinum's original animation timing.
static void BwAdapter_SyncTrainerImage(BwAdapterBattler *entry, ManagedSprite *managedSprite)
{
    Sprite *sprite = managedSprite->sprite;
    const NNSG2dCellDataBank *cells = managedSprite->resourceHeader->cellBank;
    const NNSG2dCharacterData *chars = managedSprite->resourceHeader->charData;
    const NNSG2dCellData *cell;

    if (!entry->platinumTrainer || chars == NULL || sprite->type != CELL_ANIM_TYPE_VRAM_CELL) {
        return;
    }
    cell = NNS_G2dGetCellAnimationCurrentCell(&((VRamCellAnimationData *)sprite->animData)->anim);
    if (cell == NULL || cell == entry->trainerCell) {
        return;
    }
    for (int i = 0; i < cells->numCells; i++) {
        if (NNS_G2dGetCellDataByIdx(cells, i) == cell) {
            int originX = BW_PLATINUM_TRAINER_SIZE / 2;
            int originY = BW_PLATINUM_TRAINER_SIZE / 2;
            u8 *image = Heap_Alloc(HEAP_ID_BATTLE, BW_PLATINUM_TRAINER_IMAGE_SIZE);

            if (NNS_G2dCellHasBR(cell)) {
                const NNSG2dCellBoundingRectS16 *bounds = NNS_G2dGetCellBoundingRect(cell);

                originX = -bounds->minX;
                originY = -bounds->minY;
            }
            BwAdapter_DrawPlatinumTrainerCell(chars, cells, i, originX, originY, image);
            // Front trainers' feet are 40 pixels below the cell origin in their standing pose. Preserve each
            // frame's offsets around that baseline so a jump rises and lands instead of being pinned to the floor.
            BwBattleView_UpdateTrainerImage(entry->trainerPos, image, originX, originY + BW_PLATINUM_TRAINER_SIZE / 2);
            Heap_Free(image);
            entry->trainerCell = cell;
            break;
        }
    }
}

// 0 in the table is BW2's Nate, whom no Platinum class maps to, so it stands for none
static u16 BwAdapter_Bw2TrainerClass(int trainerClass)
{
    if (trainerClass < 0 || trainerClass >= (int)NELEMS(sBw2ClassOfTrainerClass) || sBw2ClassOfTrainerClass[trainerClass] == 0) {
        return BW_NO_TRAINER;
    }
    return sBw2ClassOfTrainerClass[trainerClass];
}

static BwTrainerBack BwAdapter_TrainerBack(int trainerClass)
{
    switch (trainerClass) {
    case TRAINER_CLASS_PLAYER_MALE:
    case TRAINER_CLASS_DP_PLAYER_MALE:
    case TRAINER_CLASS_DP_PLAYER_MALE_2:
        return BW_TRAINER_BACK_LUCAS;
    case TRAINER_CLASS_PLAYER_FEMALE:
    case TRAINER_CLASS_DP_PLAYER_FEMALE:
    case TRAINER_CLASS_DP_PLAYER_FEMALE_2:
        return BW_TRAINER_BACK_DAWN;
    case TRAINER_CLASS_RIVAL:
        return BW_TRAINER_BACK_BARRY;
    case TRAINER_CLASS_TRAINER_CHERYL:
        return BW_TRAINER_BACK_CHERYL;
    case TRAINER_CLASS_TRAINER_RILEY:
        return BW_TRAINER_BACK_RILEY;
    case TRAINER_CLASS_TRAINER_MARLEY:
        return BW_TRAINER_BACK_MARLEY;
    case TRAINER_CLASS_TRAINER_BUCK:
        return BW_TRAINER_BACK_BUCK;
    case TRAINER_CLASS_TRAINER_MIRA:
        return BW_TRAINER_BACK_MIRA;
    default:
        return BW_TRAINER_BACK_NONE;
    }
}

static BOOL BwAdapter_ChooseFastIntro(BattleSystem *battleSys)
{
    u32 type = BattleSystem_GetBattleType(battleSys);
    u32 status = BattleSystem_GetBattleStatusMask(battleSys);

    if ((type & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_SAFARI | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_CATCH_TUTORIAL))
        || (status & (BATTLE_STATUS_RECORDED | BATTLE_STATUS_RECORDING | BATTLE_STATUS_FIRST_BATTLE | BATTLE_STATUS_GIRATINA))) {
        return FALSE;
    }
    return SIM_Config_prj_GetConfig()->fastWildBattleIntro;
}

BOOL BwAdapter_FastIntro(void)
{
    return sActive && sFastIntro;
}

BOOL BwAdapter_UsedFastIntro(void)
{
    return sActive && sFastIntroBattle;
}

void BwAdapter_EndIntro(void)
{
    sFastIntro = FALSE;
}

void BwAdapter_BattleCreate(BattleSystem *battleSys)
{
    BwBattleSetup setup;
    RTCTime time;
    int partnerClass = 0;
    u16 partnerBw2Class = BW_NO_TRAINER;
    BwTrainerBack partnerBack = BW_TRAINER_BACK_NONE;
    u32 battleType = BattleSystem_GetBattleType(battleSys);

    if (sActive || !BwAdapter_IsEnabled()) {
        return;
    }

    memset(&setup, 0, sizeof(setup));
    setup.battleStyle = (battleType & BATTLE_TYPE_DOUBLES) ? 1 : 0;
    setup.battleType = (battleType & BATTLE_TYPE_TRAINER) ? 1 : 0;
    setup.playerClientId = 0;
    setup.partnerClientId = 4; // None
    setup.enemyClientIds[0] = 1;
    setup.enemyClientIds[1] = 3;
    setup.playerSide = 0;
    setup.playerFemale = BattleSystem_GetTrainerGender(battleSys, BATTLER_PLAYER_1) != 0;
    // Lucas and Dawn use Nate's and Rosa's throw rigs, with the custom artwork registered below.
    // Opponents use the mapped classes, or a stand-in that the trainer steps replace.
    setup.trainerClasses[0] = setup.playerFemale ? 1 : 0;
    if (battleType & BATTLE_TYPE_TRAINER) {
        for (int client = 1; client < 4; client += 2) {
            u16 trainerClass = BwAdapter_Bw2TrainerClass(BattleSystem_GetTrainer(battleSys, client)->header.trainerType);

            setup.trainerClasses[client] = trainerClass != BW_NO_TRAINER ? trainerClass : 0;
        }
    }
    // Story partners share Nate's throw rig (Dawn uses Rosa's). Other classes retain the existing fallback.
    if (battleType & BATTLE_TYPE_2vs2) {
        partnerClass = BattleSystem_GetTrainer(battleSys, BATTLER_PLAYER_2)->header.trainerType;
        partnerBack = BwAdapter_TrainerBack(partnerClass);
        partnerBw2Class = BwAdapter_Bw2TrainerClass(partnerClass);
        setup.multi = true;
        setup.partnerClientId = BATTLER_PLAYER_2;
        setup.trainerClasses[BATTLER_PLAYER_2] = partnerBack != BW_TRAINER_BACK_NONE
            ? (partnerBack == BW_TRAINER_BACK_DAWN ? 1 : 0)
            : (partnerBw2Class != BW_NO_TRAINER ? partnerBw2Class : BW_STAND_IN_TRAINER);
    }
    BwAdapter_GetArena(battleSys, &setup);
    RTC_GetCurrentTime(&time);
    setup.hour = time.hour;
    setup.minute = time.minute;

    sSetup = setup;
    if (!BwBattleView_Create(&setup)) {
        return;
    }
    sPartner = (battleType & BATTLE_TYPE_2vs2) != 0;
    BwBattleView_SetTrainerBackSprite(8, setup.playerFemale ? BW_TRAINER_BACK_DAWN : BW_TRAINER_BACK_LUCAS);
    BwBattleView_SetTrainerBackSprite(10, setup.playerFemale ? BW_TRAINER_BACK_DAWN : BW_TRAINER_BACK_LUCAS);
    if (sPartner && partnerBack != BW_TRAINER_BACK_NONE) {
        BwBattleView_SetTrainerBackSprite(BW_TRAINER_POS_PARTNER, partnerBack);
    } else if (sPartner && partnerBw2Class == BW_NO_TRAINER) {
        BwAdapter_SetPlatinumTrainerImage(BW_TRAINER_POS_PARTNER, partnerClass, FACE_BACK);
    }
    sActive = TRUE;
    sFastIntro = BwAdapter_ChooseFastIntro(battleSys);
    sFastIntroBattle = sFastIntro;
    sFastPlayerSendOut = FALSE;
    sBattleSys = battleSys;
    sDoubles = (battleType & BATTLE_TYPE_DOUBLES) != 0;
    memset(sBattlers, 0, sizeof(sBattlers));
    sStepCount = 0;
    sStepEffectRunning = FALSE;
    sSuspended = FALSE;
    sPlayerThrowing = FALSE;
    sFadeBlendSet = FALSE;
}

void BwAdapter_SetBattlerMon(BattleSystem *battleSys, int battler, u16 species, u8 form, u32 personality, u8 gender,
    BOOL shiny, int partySlot)
{
    BwAdapterBattler *entry;
    Pokemon *mon;

    if (!sActive || battler < 0 || battler >= BW_ADAPTER_BATTLERS) {
        return;
    }

    entry = &sBattlers[battler];
    // A sprite command that shows the same sprite again (around a substitute) keeps the Pokémon the view has, and its
    // substitute
    if (partySlot < 0 && entry->known && entry->mon.species == species && entry->mon.form == form
        && entry->mon.personality == personality && entry->mon.sex == gender && entry->mon.shiny == (shiny != FALSE)) {
        return;
    }
    entry->announcedAbility = 0;
    // Without a party slot (a wild Pokémon appearing, a sprite changing), the Pokémon is the one already shown, or
    // else the party's lead
    if (partySlot >= 0) {
        mon = BattleSystem_GetPartyPokemon(battleSys, battler, partySlot);
    } else if (!entry->known) {
        mon = BwAdapter_LeadMon(BattleSystem_GetParty(battleSys, battler));
    } else {
        mon = NULL;
    }
    if (mon != NULL) {
        BwAdapter_FillMon(&entry->mon, mon);
        Pokemon_GetValue(mon, MON_DATA_NICKNAME, entry->nickname);
        entry->nickname[MON_NAME_LEN] = CHAR_EOS;
    } else if (!entry->known) {
        memset(&entry->mon, 0, sizeof(entry->mon));
        entry->nickname[0] = CHAR_EOS;
    }
    entry->mon.nickname = entry->nickname;

    // The sprite the command shows, which can differ from the party's Pokémon (Transform, form changes)
    entry->mon.species = species;
    entry->mon.form = form;
    entry->mon.personality = personality;
    entry->mon.sex = gender;
    entry->mon.shiny = shiny;
    entry->known = TRUE;
    entry->changed = TRUE;
}

static unsigned int BwAdapter_ViewPos(int battler)
{
    return sDoubles ? BW_VIEW_DOUBLES_POS_FIRST + battler : battler;
}

void BwAdapter_OnRestoreSprite(BattleSystem *battleSys, int battler)
{
    (void)battleSys;
    if (sActive && battler >= 0 && battler < BW_ADAPTER_BATTLERS) {
        sBattlers[battler].realSprite = TRUE;
    }
}

void BwAdapter_OnRefreshSprite(BattleSystem *battleSys, int battler)
{
    (void)battleSys;
    if (sActive && battler >= 0 && battler < BW_ADAPTER_BATTLERS) {
        sBattlers[battler].realSprite = FALSE;
    }
}

static BOOL BwAdapter_HasSubstitute(BattleSystem *battleSys, int battler)
{
    return (BattleMon_Get(BattleSystem_GetBattleContext(battleSys), battler, BATTLEMON_VOLATILE_STATUS, NULL)
               & VOLATILE_CONDITION_SUBSTITUTE)
        != 0;
}

// The view's substitute follows Platinum's where no effect gave it one (a Pokémon added again, Baton Pass): the doll
// shows while the battler has a substitute, unless Platinum shows its own sprite for now. Platinum takes the
// substitute away before the animation of its fading, which plays BW2's (BwAdapter_SubstituteEffect)
static void BwAdapter_SyncSubstitute(BattleSystem *battleSys, int battler)
{
    BwAdapterBattler *entry = &sBattlers[battler];
    bool shown;

    if (!entry->added || sStepCount != 0 || BwBattleView_IsEffectBusy() || sMoveAnimPending) {
        return;
    }
    if (!BwAdapter_HasSubstitute(battleSys, battler)) {
        entry->realSprite = FALSE;
    } else if (!BwBattleView_HasSubstitute(BwAdapter_ViewPos(battler), &shown) && !shown && !entry->realSprite) {
        BwBattleView_SetSubstitute(BwAdapter_ViewPos(battler), TRUE);
    }
}

#define BW_TRAINER_BG_PRIORITY 1

// Mirrors Platinum's battle sprites, which the battle still animates but doesn't draw: the view shows a battler's
// Pokémon while its sprite exists and hides it while the sprite is hidden (flickering, Fly, Dig)
static void BwAdapter_SyncBattlers(BattleSystem *battleSys)
{
    int battlers = BattleSystem_GetMaxBattlers(battleSys);

    for (int battler = 0; battler < battlers && battler < BW_ADAPTER_BATTLERS; battler++) {
        BwAdapterBattler *entry = &sBattlers[battler];
        PokemonSprite *sprite = BattlerData_GetPokemonSprite(BattleSystem_GetBattlerData(battleSys, battler));
        BOOL exists = sprite != NULL && sprite->active;
        BOOL visible = exists && !sprite->transforms.hide && !sprite->transforms.hide2;
        unsigned int viewPos = BwAdapter_ViewPos(battler);

        BattlerData *battlerData = BattleSystem_GetBattlerData(battleSys, battler);

        // BW2's trainer stands in for Platinum's. A trainer BW2 has no sprite for keeps Platinum's, in front of the 3D
        // scene, which covers the screen where Platinum's background was (BG priority 2 behind BG 0's 1)
        if (battlerData->managedSprite != NULL) {
            if (entry->bwTrainer) {
                ManagedSprite_SetDrawFlag(battlerData->managedSprite, FALSE);
                BwAdapter_SyncTrainerImage(entry, battlerData->managedSprite);
            } else {
                ManagedSprite_SetExplicitPriority(battlerData->managedSprite, BW_TRAINER_BG_PRIORITY);
            }
        }

        if (entry->removed && !exists) {
            entry->removed = FALSE;
        }
        if (entry->catching) {
            // Platinum deletes its sprite before BW2's ball reaches the Pokémon, then creates a new one as soon as
            // a failed catch ends. Keep BW2's sprite throughout, until Platinum's replacement is visible again.
            if (!BwBattleView_IsEffectBusy() && visible) {
                entry->catching = FALSE;
                entry->visible = visible;
                entry->changed = FALSE;
            }
            continue;
        }
        if (entry->scripted) {
            // The queue adds it; once Platinum's sprite is there the battler follows it again
            if (entry->added && exists) {
                entry->scripted = FALSE;
                entry->visible = visible;
            }
            continue;
        }
        if (entry->leaving || entry->removed) {
            continue;
        }
        if (entry->added && (!exists || entry->changed)) {
            BwBattleView_RemoveMon(viewPos);
            entry->added = FALSE;
        }
        if (exists && entry->known && !entry->added) {
            BwBattleView_AddMon(viewPos, &entry->mon);
            entry->added = TRUE;
            entry->changed = FALSE;
            entry->visible = TRUE;
        }
        // Battle effects own visibility, but the idle camera only moves the view. Keep following Platinum's target
        // blinking during command selection even while a camera effect is running.
        if (entry->added && visible != entry->visible && sStepCount == 0
            && (!BwBattleView_IsEffectBusy() || BwBattleView_IsIdleCameraActive())) {
            BwBattleView_SetMonVisible(viewPos, visible);
            entry->visible = visible;
        }
        BwAdapter_SyncSubstitute(battleSys, battler);
    }
}

// The gauges: BW2's stand in for Platinum's healthboxes, which the battle still runs but doesn't draw. A battler's gauge
// shows while its healthbox does, with what the healthbox shows: the HP and experience points as its bars animate them
// (HealthBox_DrawGauge), the level and the status

// The value a healthbox bar shows: what it last drew (HealthBox_DrawGauge), while the box still holds the value it
// held then, else the one it holds
static s32 BwAdapter_HealthboxValue(s32 held, s32 shown, s32 shownHeld)
{
    return held == shownHeld ? shown : held;
}

// BW2's status conditions (CONDITION_*) for a healthbox's, which are Platinum's status animations
static const u8 sBw2StatusOfHealthbox[] = { 0, 2, 5, 4, 3, 1 };

static void BwAdapter_SetHealthboxHidden(HealthBox *healthbox, BOOL hidden)
{
    if (healthbox->mainSprite != NULL) {
        healthbox->mainSprite->sprite->portHidden = hidden;
    }
    if (healthbox->arrowSprite != NULL) {
        healthbox->arrowSprite->sprite->portHidden = hidden;
    }
}

// Platinum's battle scripts turn a Pokémon's sprite into OBJs for a while (SpriteToOAM, around the send-outs and some
// moves): Platinum's sprite, which BW2's stands in for, so they aren't drawn while the view is
static void BwAdapter_SetMonObjHidden(BattlerData *battlerData, BOOL hidden)
{
    BattleMonOBJData *objData = battlerData->btlMonObjData;

    if (objData == NULL) {
        return;
    }
    for (int i = 0; i < (int)NELEMS(objData->managedSprites); i++) {
        if (objData->managedSprites[i] != NULL) {
            objData->managedSprites[i]->sprite->portHidden = hidden;
        }
    }
}

static void BwAdapter_SyncGauges(BattleSystem *battleSys)
{
    int battlers = BattleSystem_GetMaxBattlers(battleSys);

    for (int battler = 0; battler < battlers && battler < BW_ADAPTER_BATTLERS; battler++) {
        BwAdapterBattler *entry = &sBattlers[battler];
        HealthBox *healthbox = &BattleSystem_GetBattlerData(battleSys, battler)->healthbox;
        unsigned int viewPos = BwAdapter_ViewPos(battler);
        BOOL exists;
        BOOL shown;
        s32 hp;
        BOOL hpMoving;
        s32 exp;
        u8 status;

        // The Safari Zone's and Pal Park's boxes count balls, which BW2's gauges don't
        if (healthbox->type >= HEALTHBOX_TYPE_SAFARI_ZONE) {
            continue;
        }
        // The gauge is made with the healthbox and goes with it, and shows while the healthbox is drawn (Platinum
        // hides them during move animations)
        // (its sprite is made before its values are set)
        exists = entry->known && healthbox->mainSprite != NULL && healthbox->maxHP > 0;
        // ... and while the battler's Pokémon is on the field, as BW2's gauge goes with it (Platinum draws a fainted
        // Pokémon's healthbox again as the trainer comes back after the battle)
        shown = exists && ManagedSprite_GetDrawFlag(healthbox->mainSprite) && entry->added;
        BwAdapter_SetHealthboxHidden(healthbox, TRUE);
        BwAdapter_SetMonObjHidden(BattleSystem_GetBattlerData(battleSys, battler), TRUE);
        // A Pokémon that left the field (fainted, recalled) takes its gauge with it, as in BW2; a gauge is made only
        // to show
        if (!exists || !entry->added || (!entry->gaugeShown && !shown)) {
            if (entry->gaugeShown) {
                BwBattleView_HideGauge(viewPos);
                entry->gaugeShown = FALSE;
            }
            continue;
        }

        hp = BwAdapter_HealthboxValue(healthbox->curHP, healthbox->portShownHP, healthbox->portShownHPHeld);
        hpMoving = hp != healthbox->curHP;
        exp = BwAdapter_HealthboxValue(healthbox->curExp, healthbox->portShownExp, healthbox->portShownExpHeld);
        status = healthbox->status < NELEMS(sBw2StatusOfHealthbox) ? sBw2StatusOfHealthbox[healthbox->status] : 0;
        // A new Pokémon's gauge waits for BW2's send-out to end, as in BW2 (btlv_scu.c), while Platinum's healthbox
        // slides in during it
        if ((!entry->gaugeShown || entry->gaugeMon.species != entry->mon.species) && BwAdapter_IsBusy()) {
            if (entry->gaugeShown) {
                BwBattleView_HideGauge(viewPos);
                entry->gaugeShown = FALSE;
            }
            continue;
        }
        if (!entry->gaugeShown || entry->gaugeMon.level != healthbox->level || entry->gaugeMon.maxHp != healthbox->maxHP
            || entry->gaugeMon.species != entry->mon.species) {
            // As the healthbox has it: its level, HP and experience points (the box keeps those of the level)
            entry->gaugeMon = entry->mon;
            entry->gaugeMon.level = healthbox->level;
            entry->gaugeMon.hp = hp;
            entry->gaugeMon.maxHp = healthbox->maxHP;
            entry->gaugeMon.exp = Pokemon_GetSpeciesBaseExpAt(entry->mon.species, healthbox->level) + exp;
            entry->gaugeMon.status = status;
            entry->gaugeMon.badlyPoisoned = FALSE;
            BwBattleView_ShowGauge(viewPos, &entry->gaugeMon);
            // A gauge made again keeps the parts it had hidden, while it shows its bars: it is set all one way
            BwBattleView_SetGaugeVisible(viewPos, shown);
            entry->gaugeShown = TRUE;
            entry->gaugeVisible = shown;
            entry->gaugeHp = hp;
            entry->gaugeHpMoving = FALSE;
            entry->gaugeExp = exp;
            entry->gaugeStatus = status;
        }
        if (hp != entry->gaugeHp || hpMoving != entry->gaugeHpMoving) {
            BwBattleView_SetGaugeHp(viewPos, hp, hpMoving);
            entry->gaugeHp = hp;
            entry->gaugeHpMoving = hpMoving;
        }
        if (exp != entry->gaugeExp) {
            BwBattleView_SetGaugeExp(viewPos, exp);
            entry->gaugeExp = exp;
        }
        if (status != entry->gaugeStatus) {
            BwBattleView_SetGaugeStatus(viewPos, status);
            entry->gaugeStatus = status;
        }
        if (shown != entry->gaugeVisible) {
            BwBattleView_SetGaugeVisible(viewPos, shown);
            entry->gaugeVisible = shown;
        }
    }
}

static BOOL BwAdapter_IsEnemy(int battler)
{
    return (battler & 1) != 0;
}

// Ability events remain Platinum's. The popup uses the event's recorded ability rather than reading a potentially
// changed ability later (Trace, suppression), and a single activation can affect both opponents in doubles.
static BOOL BwAdapter_ShowAbility(BattleSystem *battleSys, int battler, int ability)
{
    if (!BwAdapter_IsDrawing() || battler < 0 || battler >= BattleSystem_GetMaxBattlers(battleSys)
        || ability <= ABILITY_NONE || ability > ABILITY_BAD_DREAMS || !sBattlers[battler].known) {
        return FALSE;
    }
    if (sBattlers[battler].announcedAbility == ability) {
        return TRUE;
    }
    MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG,
        TEXT_BANK_ABILITY_NAMES, HEAP_ID_BATTLE);
    String *name = MessageLoader_GetNewString(loader, ability);
    BOOL shown = BwBattleView_ShowAbilityPopup(BwAdapter_ViewPos(battler), sBattlers[battler].nickname,
        String_GetData(name));

    String_Free(name);
    MessageLoader_Free(loader);
    if (shown) {
        sBattlers[battler].announcedAbility = ability;
    }
    return shown;
}

BOOL BwAdapter_IsAbilityPopupBusy(void)
{
    return sActive && BwBattleView_IsAbilityPopupBusy();
}

void BwAdapter_OnAbilityActivated(BattleSystem *battleSys, int battler, int ability)
{
    BwAdapter_ShowAbility(battleSys, battler, ability);
}

void BwAdapter_ReleaseAbilityPopups(void)
{
    if (sActive) {
        BwBattleView_SetAbilityPopupHeld(0, FALSE);
        BwBattleView_SetAbilityPopupHeld(1, FALSE);
    }
}

enum BwAbilityMessage BwAdapter_PrepareAbilityMessage(BattleSystem *battleSys, BattleMessage *message)
{
    int owner = -1;
    int ability = 0;

    if (!BwAdapter_IsDrawing()) {
        return BW_ABILITY_MESSAGE_NONE;
    }
    // A move copying, replacing or suppressing an ability is not an activation of the affected Pokémon's ability.
    if (message->id == BattleStrings_Text_PokemonsAbilityWasSuppressed_Ally
        || message->id == BattleStrings_Text_PokemonAcquiredAbility_Ally
        || message->id == BattleStrings_Text_PokemonCopiedPokemonsAbility_AllyAlly) {
        return BW_ABILITY_MESSAGE_NONE;
    }
    switch (message->tags) {
    case TAG_NICKNAME_ABILITY:
    case TAG_NICKNAME_ABILITY_NICKNAME:
    case TAG_NICKNAME_ABILITY_MOVE:
    case TAG_NICKNAME_ABILITY_ITEM:
    case TAG_NICKNAME_ABILITY_STAT:
    case TAG_NICKNAME_ABILITY_TYPE:
    case TAG_NICKNAME_ABILITY_STATUS:
    case TAG_NICKNAME_ABILITY_NUMBER:
    case TAG_NICKNAME_ABILITY_NICKNAME_MOVE:
    case TAG_NICKNAME_ABILITY_NICKNAME_ABILITY:
    case TAG_NICKNAME_ABILITY_NICKNAME_STAT:
        owner = message->params[0] & 0xff;
        ability = message->params[1];
        break;
    case TAG_ABILITY_NICKNAME:
        owner = message->params[1] & 0xff;
        ability = message->params[0];
        break;
    case TAG_NICKNAME_NICKNAME_ABILITY:
        if (message->id == BattleStrings_Text_PokemonTracedPokemonsAbility_AllyAlly) {
            owner = message->params[0] & 0xff;
            ability = ABILITY_TRACE;
        } else {
            owner = message->params[1] & 0xff;
            ability = message->params[2];
        }
        break;
    default:
        if (message->id == BattleStrings_Text_PokemonFriskedItsFoeAndFoundOneItem_Ally) {
            owner = message->params[0] & 0xff;
            ability = ABILITY_FRISK;
        }
        break;
    }
    if (!BwAdapter_ShowAbility(battleSys, owner, ability)) {
        return BW_ABILITY_MESSAGE_NONE;
    }
    BwBattleView_SetAbilityPopupHeld(BwAdapter_ViewPos(owner), TRUE);
    if (message->tags == TAG_NICKNAME_ABILITY_NICKNAME_ABILITY) {
        BwAdapter_ShowAbility(battleSys, message->params[2] & 0xff, message->params[3]);
    }
    switch (message->id) {
    case BattleStrings_Text_PokemonsAbilityCutsPokemonsStat_AllyAlly:
        message->id = BattleStrings_Text_PokemonsStatFell_Ally;
        message->tags = TAG_NICKNAME_STAT;
        message->params[0] = message->params[2];
        message->params[1] = message->params[3];
        break;
    case BattleStrings_Text_PokemonsAbilityRaisedItsStat_Ally:
        message->id = BattleStrings_Text_PokemonsStatRose_Ally;
        message->tags = TAG_NICKNAME_STAT;
        message->params[1] = message->params[2];
        break;
    case BattleStrings_Text_PokemonsAbilityMadeItRain_Ally:
        message->id = BattleStrings_Text_ItStartedToRain;
        message->tags = TAG_NONE;
        break;
    case BattleStrings_Text_PokemonsAbilityWhippedUpASandstorm_Ally:
        message->id = BattleStrings_Text_ASandstormBrewed;
        message->tags = TAG_NONE;
        break;
    case BattleStrings_Text_PokemonsAbilityIntensifiedTheSunsRays_Ally:
        message->id = BattleStrings_Text_TheSunlightTurnedHarsh;
        message->tags = TAG_NONE;
        break;
    case BattleStrings_Text_PokemonsAbilityWhippedUpAHailstorm_Ally:
        message->id = BattleStrings_Text_ItStartedToHail;
        message->tags = TAG_NONE;
        break;
    case BattleStrings_Text_PokemonIsExertingItsAbility_Ally:
    case BattleStrings_Text_PokemonWasAbility_Ally:
        return BW_ABILITY_MESSAGE_POPUP_ONLY;
    default:
        // Keep details such as Trace's copied ability, Forewarn's move, Frisk's item, status and immunity messages.
        break;
    }
    return BW_ABILITY_MESSAGE_TEXT;
}

// The party gauges: BW2's balls stand in for Platinum's, which still run but aren't drawn
static const u8 sBw2BallOfStockStatus[] = {
    [STOCK_STATUS_NO_MON] = 0,
    [STOCK_STATUS_MON_ALIVE] = 1,
    [STOCK_STATUS_MON_FAINTED] = 2,
    [STOCK_STATUS_HAS_STATUS_CONDITION] = 3,
};

void BwAdapter_OnShowPartyGauge(BattleSystem *battleSys, int battler, const u8 *status)
{
    u8 balls[MAX_PARTY_SIZE];

    (void)battleSys;
    if (!sActive) {
        return;
    }
    for (int i = 0; i < MAX_PARTY_SIZE; i++) {
        balls[i] = status[i] < NELEMS(sBw2BallOfStockStatus) ? sBw2BallOfStockStatus[status[i]] : 0;
    }
    BwBattleView_ShowBallGauge(BwAdapter_IsEnemy(battler), balls);
}

void BwAdapter_OnHidePartyGauge(BattleSystem *battleSys, int battler)
{
    (void)battleSys;
    if (sActive) {
        BwBattleView_HideBallGauge(BwAdapter_IsEnemy(battler));
    }
}

static void BwAdapter_HidePartyGauges(BattleSystem *battleSys, BOOL hidden)
{
    for (int side = 0; side < PARTY_GAUGE_COUNT; side++) {
        PartyGauge *gauge = BattleSystem_GetPartyGauge(battleSys, side);

        if (gauge == NULL) {
            continue;
        }
        if (gauge->arrow.managedSprite != NULL) {
            gauge->arrow.managedSprite->sprite->portHidden = hidden;
        }
        for (int i = 0; i < MAX_PARTY_SIZE; i++) {
            if (gauge->pokeballs[i].managedSprite != NULL) {
                gauge->pokeballs[i].managedSprite->sprite->portHidden = hidden;
            }
        }
    }
}

// Platinum can deliver each battler's display command on a separate frame. Wait for the whole intro group before
// applying its setup steps, so both trainers/Pokémon are present when the shared camera or throw effect starts.
static u8 BwAdapter_IntroParticipants(u16 effect)
{
    u8 required;

    switch (effect) {
    case BW_EFFECT_TRAINER_APPEAR:
    case BW_EFFECT_TRAINER_THROW:
        required = FlagIndex(BATTLER_ENEMY_1);
        if (BattleSystem_GetBattleType(sBattleSys) & (BATTLE_TYPE_2vs2 | BATTLE_TYPE_TAG)) {
            required |= FlagIndex(BATTLER_ENEMY_2);
        }
        break;
    case BW_EFFECT_WILD_APPEAR:
    case BW_EFFECT_TRAINER_SEND_OUT:
        required = FlagIndex(BATTLER_ENEMY_1);
        if (sDoubles) {
            required |= FlagIndex(BATTLER_ENEMY_2);
        }
        break;
    case BW_EFFECT_PLAYER_SEND_OUT:
        required = FlagIndex(BATTLER_PLAYER_1);
        if (sDoubles) {
            required |= FlagIndex(BATTLER_PLAYER_2);
        }
        break;
    default:
        return 0;
    }
    return required;
}

// Setup steps precede their effects. Shared intro effects collect both battlers even when commands for the next
// phase arrive between them; repeated adjacent effects also share a single queue entry.
static void BwAdapter_QueueWithParticipants(const BwAdapterStep *step, u16 effect, int effectPos, u8 participants)
{
    int effectIndex = sStepCount - 1;
    BwAdapterStep *pending;

    // Commands for the next intro phase can arrive before the current phase's second participant.
    if (BwAdapter_IntroParticipants(effect) != 0) {
        for (int i = 0; i < sStepCount; i++) {
            if (sSteps[i].type == BW_STEP_EFFECT && sSteps[i].effect == effect) {
                effectIndex = i;
                break;
            }
        }
    }
    pending = effectIndex >= 0 ? &sSteps[effectIndex] : NULL;
    if (sStepCount + 2 > BW_STEPS_MAX) {
        return;
    }
    if (effect != 0 && pending != NULL && pending->type == BW_STEP_EFFECT && pending->effect == effect) {
        // The effect keeps the position it was first queued at, as BW2 plays it at the first of the two
        pending->participants |= participants;
        if (step != NULL) {
            memmove(&sSteps[effectIndex + 1], &sSteps[effectIndex],
                (sStepCount - effectIndex) * sizeof(sSteps[0]));
            sSteps[effectIndex] = *step;
            sStepCount++;
        }
        return;
    }
    if (step != NULL) {
        sSteps[sStepCount++] = *step;
    }
    if (effect != 0) {
        sSteps[sStepCount++] = (BwAdapterStep) { .type = BW_STEP_EFFECT, .participants = participants,
            .effect = effect, .effectPos = effectPos };
    }
}

static void BwAdapter_QueueWithEffect(const BwAdapterStep *step, u16 effect, int effectPos)
{
    BwAdapter_QueueWithParticipants(step, effect, effectPos, step != NULL ? FlagIndex(step->battler) : 0);
}

static BOOL BwAdapter_ShouldOverlapSendOutCry(u16 effect)
{
    return (effect == BW_EFFECT_PLAYER_SEND_OUT || effect == BW_EFFECT_TRAINER_SEND_OUT || effect == BW_EFFECT_SEND_OUT)
        && !(BattleSystem_GetBattleType(sBattleSys) & BATTLE_TYPE_LINK)
        && BattleTiming_MessagePageDelay(100) < 100;
}

static void BwAdapter_RunSteps(void)
{
    if (sStepEffectRunning && !BwBattleView_IsEffectBusy()) {
        sStepEffectRunning = FALSE;
        sFastPlayerSendOut = FALSE;
        BwBattle_SetCriesMuted(FALSE);
    }
    for (int i = 0; i < sStepCount; i++) {
        if (sSteps[i].type == BW_STEP_EFFECT) {
            u8 required = BwAdapter_IntroParticipants(sSteps[i].effect);

            if ((sSteps[i].participants & required) != required) {
                return;
            }
            break;
        }
    }
    while (sStepCount > 0) {
        BwAdapterStep step;
        BwAdapterBattler *entry;

        if (sStepEffectRunning && BwBattleView_IsEffectBusy()) {
            return;
        }
        sStepEffectRunning = FALSE;
        BwBattle_SetCriesMuted(FALSE);
        step = sSteps[0];
        memmove(&sSteps[0], &sSteps[1], (sStepCount - 1) * sizeof(sSteps[0]));
        sStepCount--;
        entry = &sBattlers[step.battler];

        switch (step.type) {
        case BW_STEP_ADD_MON:
            if (entry->added) {
                BwBattleView_RemoveMon(BwAdapter_ViewPos(step.battler));
            }
            BwBattleView_AddMon(BwAdapter_ViewPos(step.battler), &entry->mon);
            if (sFastIntro && BwAdapter_IsEnemy(step.battler)) {
                BwBattleView_SetMonVisible(BwAdapter_ViewPos(step.battler), TRUE);
            }
            entry->added = TRUE;
            entry->changed = FALSE;
            break;
        case BW_STEP_REMOVE_MON:
            if (entry->added) {
                BwBattleView_RemoveMon(BwAdapter_ViewPos(step.battler));
                entry->added = FALSE;
            }
            entry->leaving = FALSE;
            entry->removed = TRUE;
            break;
        case BW_STEP_EFFECT:
            // Send-out cries follow BW2's visible animation; Platinum still plays the wild encounter's cry.
            BwBattle_SetCriesMuted(step.effect != BW_EFFECT_PLAYER_SEND_OUT
                && step.effect != BW_EFFECT_TRAINER_SEND_OUT && step.effect != BW_EFFECT_SEND_OUT);
            if (sFastIntro && step.effect == BW_EFFECT_PLAYER_SEND_OUT) {
                sStepEffectRunning = BwBattleView_StartFastWildSendOut();
            } else if (BwAdapter_ShouldOverlapSendOutCry(step.effect)) {
                sStepEffectRunning = BwBattleView_StartFastSendOut(step.effectPos, step.effect);
            } else {
                sStepEffectRunning = BwBattleView_StartEffect(step.effectPos, step.effect);
            }
            sFastPlayerSendOut = sStepEffectRunning && sFastIntro && step.effect == BW_EFFECT_PLAYER_SEND_OUT;
            if (sStepEffectRunning && sFastIntro
                && (step.effect == BW_EFFECT_TO_PLAYER || step.effect == BW_EFFECT_TO_PLAYER_AND_PARTNER)) {
                BwBattleView_FinishOpeningEffect();
                if (!BwBattleView_IsEffectBusy()) {
                    sStepEffectRunning = FALSE;
                    BwBattle_SetCriesMuted(FALSE);
                }
            }
            if (sStepEffectRunning) {
                return;
            }
            break;
        case BW_STEP_SET_TRAINER:
            entry->platinumTrainer = step.platinumClass >= 0;
            entry->trainerPos = step.trainerPos;
            entry->trainerCell = NULL;
            BwBattleView_SetTrainer(step.trainerPos, step.trainerClass, FALSE);
            if (step.platinumClass >= 0) {
                BwAdapter_SetPlatinumTrainerImage(step.trainerPos, step.platinumClass, FACE_FRONT);
                ManagedSprite *trainer = BattleSystem_GetBattlerData(sBattleSys, step.battler)->managedSprite;

                if (trainer != NULL) {
                    BwAdapter_SyncTrainerImage(entry, trainer);
                }
            }
            break;
        case BW_STEP_REMOVE_TRAINER:
            entry->platinumTrainer = FALSE;
            BwBattleView_RemoveTrainer(step.trainerPos);
            break;
        }
    }
}

BOOL BwAdapter_CanStartCatch(void)
{
    return sActive && !BwAdapter_IsBusy() && !BwBattleView_IsEffectBusy();
}

BOOL BwAdapter_StartCatch(BattleSystem *battleSys, int battler, u16 ball, int shakes, BOOL caught, BOOL blocked)
{
    (void)battleSys;
    if (!BwAdapter_CanStartCatch()) {
        return FALSE;
    }
    if (blocked) {
        sStepEffectRunning = BwBattleView_StartBallBlocked(BwAdapter_ViewPos(battler), ball);
    } else {
        sStepEffectRunning = BwBattleView_StartCatch(BwAdapter_ViewPos(battler), ball, shakes, caught);
        if (sStepEffectRunning) {
            // The catch effect owns the sprite's lifetime as well as its visibility.
            sBattlers[battler].catching = TRUE;
        }
    }
    return sStepEffectRunning;
}

void BwAdapter_FinishCatch(void)
{
    if (sActive) {
        BwBattleView_ResumeEffect();
        sStepEffectRunning = TRUE;
    }
}

BOOL BwAdapter_IsBusy(void)
{
    return sActive && (sStepCount > 0 || (sStepEffectRunning && BwBattleView_IsEffectBusy()));
}

#define BW_TRAINER_POS_ENEMY 9
#define BW_TRAINER_POS_ENEMY_1 11
#define BW_TRAINER_POS_ENEMY_2 13


// The opponent's trainer position of a battler, apart when it has two trainers (as Platinum places their sprites)
static u8 BwAdapter_TrainerPos(BattleSystem *battleSys, int battler)
{
    u32 battleType = BattleSystem_GetBattleType(battleSys);

    if (battleType & (BATTLE_TYPE_2vs2 | BATTLE_TYPE_TAG)) {
        return battler == 3 ? BW_TRAINER_POS_ENEMY_2 : BW_TRAINER_POS_ENEMY_1;
    }
    return BW_TRAINER_POS_ENEMY;
}

// Where the trainer effects play: BW2 plays a single battle's at the opponent's Pokémon, the others' at the trainer
static int BwAdapter_TrainerEffectPos(BattleSystem *battleSys, int battler)
{
    return sDoubles ? BwAdapter_TrainerPos(battleSys, battler) : (int)BwAdapter_ViewPos(1);
}

void BwAdapter_OnSetEncounter(BattleSystem *battleSys, int battler)
{
    BwAdapterStep step = { .type = BW_STEP_ADD_MON, .battler = battler };

    (void)battleSys;
    if (!sActive || !BwAdapter_IsEnemy(battler)) {
        return;
    }
    sBattlers[battler].scripted = TRUE;
    // The wild appearance effect ends on an enemy close-up even when advanced offscreen.
    // Add the wild Pokémon directly so the first camera shot stays on the field.
    BwAdapter_QueueWithEffect(&step, sFastIntro ? 0 : BW_EFFECT_WILD_APPEAR, BwAdapter_ViewPos(battler));
}

void BwAdapter_OnTrainerEncounter(BattleSystem *battleSys, int battler, int trainerClass)
{
    u16 bwClass = BwAdapter_Bw2TrainerClass(trainerClass);
    BwAdapterStep step;

    if (!sActive) {
        return;
    }
    // The send-out effects add the player and partner with the back sprites registered at battle creation.
    if (!BwAdapter_IsEnemy(battler)) {
        sBattlers[battler].bwTrainer = battler == 0 || (battler == BATTLER_PLAYER_2 && sPartner);
        return;
    }
    sBattlers[battler].bwTrainer = TRUE;
    step = (BwAdapterStep) { .type = BW_STEP_SET_TRAINER, .battler = battler,
        .trainerPos = BwAdapter_TrainerPos(battleSys, battler),
        .trainerClass = bwClass != BW_NO_TRAINER ? bwClass : BW_STAND_IN_TRAINER,
        .platinumClass = bwClass != BW_NO_TRAINER ? -1 : trainerClass };
    BwAdapter_QueueWithEffect(&step, BW_EFFECT_TRAINER_APPEAR, BwAdapter_TrainerEffectPos(battleSys, battler));
}

void BwAdapter_OnThrowTrainerBall(BattleSystem *battleSys, int battler, int ballTypeIn)
{
    if (!sActive || ballTypeIn != BTLSCR_THROW_POKE_BALL) {
        return;
    }
    if (!BwAdapter_IsEnemy(battler)) {
        // A partner throws too (a tag battle), but BW2's player steps in once, until the send-out (0x234)
        if (!sPlayerThrowing) {
            sPlayerThrowing = TRUE;
            BwAdapter_QueueWithEffect(NULL, sPartner ? BW_EFFECT_TO_PLAYER_AND_PARTNER : BW_EFFECT_TO_PLAYER, -1);
        }
    } else if (sBattlers[battler].bwTrainer) {
        BwAdapter_QueueWithParticipants(NULL, BW_EFFECT_TRAINER_THROW, BwAdapter_TrainerEffectPos(battleSys, battler),
            FlagIndex(battler));
    }
}

void BwAdapter_OnShowMon(BattleSystem *battleSys, int battler, BOOL battleStart)
{
    BwAdapterStep step = { .type = BW_STEP_ADD_MON, .battler = battler };

    (void)battleSys;
    if (!sActive) {
        return;
    }
    sBattlers[battler].scripted = TRUE;
    sBattlers[battler].removed = FALSE;
    if (!battleStart) {
        BwAdapter_QueueWithEffect(&step, BW_EFFECT_SEND_OUT, BwAdapter_ViewPos(battler));
    } else if (BwAdapter_IsEnemy(battler)) {
        BwAdapter_QueueWithEffect(&step, BW_EFFECT_TRAINER_SEND_OUT, BwAdapter_ViewPos(battler));
    } else {
        sPlayerThrowing = FALSE;
        BwAdapter_QueueWithEffect(&step, BW_EFFECT_PLAYER_SEND_OUT, -1);
    }
}

void BwAdapter_OnReturnMon(BattleSystem *battleSys, int battler)
{
    BwAdapterStep step = { .type = BW_STEP_REMOVE_MON, .battler = battler };

    (void)battleSys;
    if (!sActive || !sBattlers[battler].added) {
        return;
    }
    // Held by the queue from now, so that Platinum's sprite going doesn't remove it first
    sBattlers[battler].leaving = TRUE;
    BwAdapter_QueueWithEffect(NULL, BW_EFFECT_RECALL, BwAdapter_ViewPos(battler));
    BwAdapter_QueueWithEffect(&step, 0, 0);
}

void BwAdapter_OnCommandSelection(BattleSystem *battleSys, BOOL selecting)
{
    (void)battleSys;
    if (sActive) {
        if (!selecting) {
            for (int i = 0; i < BW_ADAPTER_BATTLERS; i++) {
                sBattlers[i].announcedAbility = 0;
            }
        }
        BwBattleView_SetIdleCamera(selecting);
    }
}

void BwAdapter_OnFaint(BattleSystem *battleSys, int battler)
{
    BwAdapterStep step = { .type = BW_STEP_REMOVE_MON, .battler = battler };

    (void)battleSys;
    if (!sActive || !sBattlers[battler].added) {
        return;
    }
    // BW2's faint (btlv_scu.c): the Pokémon sinks and fades and its gauge goes; Platinum's sprite going doesn't remove
    // it first
    sBattlers[battler].leaving = TRUE;
    BwAdapter_QueueWithEffect(NULL, BW_EFFECT_FAINT, BwAdapter_ViewPos(battler));
    BwAdapter_QueueWithEffect(&step, 0, 0);
}

void BwAdapter_OnTrainerSlideIn(BattleSystem *battleSys, int battler, int trainerClass)
{
    u16 bwClass = BwAdapter_Bw2TrainerClass(trainerClass);
    BwAdapterStep step;

    if (!sActive || !BwAdapter_IsEnemy(battler)) {
        return;
    }
    sBattlers[battler].bwTrainer = TRUE;
    step = (BwAdapterStep) { .type = BW_STEP_SET_TRAINER, .battler = battler, .trainerPos = BW_TRAINER_POS_ENEMY,
        .trainerClass = bwClass != BW_NO_TRAINER ? bwClass : BW_STAND_IN_TRAINER,
        .platinumClass = bwClass != BW_NO_TRAINER ? -1 : trainerClass };
    BwAdapter_QueueWithEffect(&step, BW_EFFECT_TRAINER_SLIDE_IN, -1);
}

void BwAdapter_OnTrainerSlideOut(BattleSystem *battleSys, int battler)
{
    BwAdapterStep step = { .type = BW_STEP_REMOVE_TRAINER, .battler = battler, .trainerPos = BW_TRAINER_POS_ENEMY };

    (void)battleSys;
    if (!sActive || !BwAdapter_IsEnemy(battler) || !sBattlers[battler].bwTrainer) {
        return;
    }
    BwAdapter_QueueWithEffect(NULL, BW_EFFECT_TRAINER_SLIDE_OUT, -1);
    BwAdapter_QueueWithEffect(&step, 0, 0);
}

void BwAdapter_BattleDelete(BattleSystem *battleSys)
{
    (void)battleSys;
    BwAdapter_RestoreFadeBlend();
    BwAdapter_BlendTextbox(FALSE);
    BwAdapter_ExtendTextbox(FALSE);
    if (sActive) {
        BwAdapter_ShowPlatinumBackground(TRUE);
    }
    sMoveAnimPending = FALSE;
    sStepCount = 0;
    sStepEffectRunning = FALSE;
    if (sActive) {
        SIM_SetFieldWidescreen(FALSE);
        BwBattleView_Delete();
        sActive = FALSE;
        sBattleSys = NULL;
    }
}

// Debug: PLATINUM_BWBATTLE_BG_CYCLE=<directory> shows every background type with terrain 0, then every terrain of
// background type 0, two seconds each, and saves a screenshot of each to <directory>/bg<type>_t<terrain>.ppm, for
// mapping Platinum's terrains to BW2's (docs/bwbattle.md)
#define BW_BG_CYCLE_FRAMES 120
#define BW_BG_TYPES 21
#define BW_TERRAINS 20

void SIM_Autopilot_RequestShot(const char *path);

static void BwAdapter_DebugCycleArena(void)
{
    static int sFrame = -1;
    static const char *sDir;
    static char sPath[256];
    int step;

    if (sFrame < 0) {
        sDir = getenv("PLATINUM_BWBATTLE_BG_CYCLE");
        sFrame = 0;
    }
    if (sDir == NULL) {
        return;
    }

    step = sFrame / BW_BG_CYCLE_FRAMES;
    if (sFrame % BW_BG_CYCLE_FRAMES == BW_BG_CYCLE_FRAMES - 1) {
        SIM_Autopilot_RequestShot(sPath);
    } else if (sFrame % BW_BG_CYCLE_FRAMES == 0 && step < BW_BG_TYPES + BW_TERRAINS - 1) {
        sSetup.bgType = step < BW_BG_TYPES ? step : 0;
        sSetup.terrain = step < BW_BG_TYPES ? 0 : step - BW_BG_TYPES + 1;
        snprintf(sPath, sizeof(sPath), "%s/bg%02u_t%02u.ppm", sDir, sSetup.bgType, sSetup.terrain);
        BwBattleView_Delete();
        BwBattleView_Create(&sSetup);
        for (int i = 0; i < BW_ADAPTER_BATTLERS; i++) {
            sBattlers[i].added = FALSE;
        }
    }
    sFrame++;
}

// Debug: PLATINUM_BWBATTLE_MOVE_CYCLE=<moves> plays those moves' effects from the player's Pokemon at the opponent's,
// one after another once the battle waits for a command, and logs each and any that doesn't end. <moves> is a list
// of moves and ranges, such as 38,90,200-202 (numbers may be hex). From 0x231 on the numbers are BW2's other effects
// (BwBattleView_StartEffect), played at the opponent
#define BW_MOVE_CYCLE_TIMEOUT 1200
#define BW_MOVE_CYCLE_MAX_RANGES 64

static void BwAdapter_DebugCycleMoves(BattleSystem *battleSys)
{
    static int sRanges[BW_MOVE_CYCLE_MAX_RANGES][2];
    static int sRangeCount = -1, sRange, sNext, sPlaying, sFrames;

    if (sRangeCount == -1) {
        const char *value = getenv("PLATINUM_BWBATTLE_MOVE_CYCLE");
        char *end;

        sRangeCount = 0;
        while (value != NULL && *value != '\0' && sRangeCount < BW_MOVE_CYCLE_MAX_RANGES) {
            int first = (int)strtol(value, &end, 0);
            int last = first;

            if (end == value) {
                break;
            }
            if (*end == '-') {
                value = end + 1;
                last = (int)strtol(value, &end, 0);
                if (end == value) {
                    break;
                }
            }
            sRanges[sRangeCount][0] = first;
            sRanges[sRangeCount][1] = last;
            sRangeCount++;
            value = *end == ',' ? end + 1 : end;
        }
        sRange = 0;
        sNext = sRangeCount > 0 ? sRanges[0][0] : 0;
    }
    if (sRange >= sRangeCount || sMoveAnimPending || !sBattlers[0].added || !sBattlers[1].added) {
        return;
    }
    if (BwBattleView_IsEffectBusy()) {
        if (++sFrames == BW_MOVE_CYCLE_TIMEOUT) {
            fprintf(stderr, "[bwbattle] move %d: effect still running after %d frames\n", sPlaying, sFrames);
        }
        return;
    }
    sFrames = 0;
    sPlaying = sNext;
    fprintf(stderr, "[bwbattle] move %d (0x%x)\n", sNext, sNext);
    if (sNext >= 0x231) {
        BwBattleView_StartEffect(BwAdapter_ViewPos(1), sNext);
    } else {
        BwBattleView_StartMove(BwAdapter_ViewPos(0), BwAdapter_ViewPos(1), sNext);
    }
    if (sNext < sRanges[sRange][1]) {
        sNext++;
    } else if (++sRange < sRangeCount) {
        sNext = sRanges[sRange][0];
    }
}

void BwAdapter_SetSuspended(BOOL suspended)
{
    if (!sActive || suspended == sSuspended) {
        return;
    }
    sSuspended = suspended;
    if (suspended) {
        BwAdapter_RestoreFadeBlend();
        BwAdapter_BlendTextbox(FALSE);
        BwAdapter_ExtendTextbox(FALSE);
        BwAdapter_ShowPlatinumBackground(TRUE);
        SIM_SetFieldWidescreen(FALSE);
        BwBattleView_SetObjExtension(false);
        if (sBattleSys != NULL) {
            for (int battler = 0; battler < BattleSystem_GetMaxBattlers(sBattleSys); battler++) {
                BwAdapter_SetHealthboxHidden(&BattleSystem_GetBattlerData(sBattleSys, battler)->healthbox, FALSE);
                BwAdapter_SetMonObjHidden(BattleSystem_GetBattlerData(sBattleSys, battler), FALSE);
            }
            BwAdapter_HidePartyGauges(sBattleSys, FALSE);
        }
        // Platinum's battle 3D state (battle_main.c's G3DPipeline_SetupCallback), which BW2's view changes every frame,
        // and the camera Platinum's sprites draw with (PokemonSpriteManager_New)
        NNS_G2dSetupSoftwareSpriteCamera();
        G3X_SetShading(GX_SHADING_TOON);
        G3X_AntiAlias(TRUE);
        G3X_AlphaTest(FALSE, 0);
        G3X_AlphaBlend(TRUE);
        G3X_EdgeMarking(FALSE);
        G3X_SetFog(FALSE, GX_FOGBLEND_COLOR_ALPHA, GX_FOGSLOPE_0x8000, 0);
        G3X_SetClearColor(GX_RGB(0, 0, 0), 0, G3X_DEPTH_MAX, 63, 0);
        G3_ViewPort(0, 0, HW_LCD_WIDTH - 1, HW_LCD_HEIGHT - 1);
    } else {
        BwBattleView_SetObjExtension(true);
    }
}

// BW2's battle runs at 60 frames a second, and Platinum's at 30: the port's 60 FPS loop runs Platinum's battle on
// every other frame (battle_timing.c) and a frame of BW2's on each. Without it, the loop runs at 30 Hz and each of its
// frames runs two of BW2's. Quick doubles effects; Turbo triples them. Ambient motion keeps its normal clock.
static unsigned int BwAdapter_ViewFrames(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();

    if (!config->enable60fps) {
        return 2;
    }
    return config->battlePacing == BATTLE_PACING_TURBO ? 3 : (config->battlePacing == BATTLE_PACING_QUICK ? 2 : 1);
}

BOOL BwAdapter_Draw(BattleSystem *battleSys)
{
    BwAdapter_DebugCycleArena();
    BwAdapter_DebugCycleMoves(battleSys);
    if (!sActive || sSuspended) {
        return FALSE;
    }
    BwAdapter_SyncBattlers(battleSys);
    BwAdapter_SyncGauges(battleSys);
    BwAdapter_HidePartyGauges(battleSys, TRUE);
    BwAdapter_RunSteps();
    BwAdapter_MirrorFade(battleSys);
    BwAdapter_StyleTextbox(battleSys);
    BwAdapter_BlendTextbox(TRUE);
    BwAdapter_ShowPlatinumBackground(FALSE);
    // The arena widescreen, as the field is (its 2D layers stay on the 256 dots in the middle)
    BwBattleView_SetViewWidth(SIM_SetFieldWidescreen(TRUE));
    unsigned int frames = BwAdapter_ViewFrames();
    if (sFastPlayerSendOut) {
        frames *= 2;
    }
    BwBattleView_Main(frames, SIM_Config_prj_GetConfig()->enable60fps ? 1 : 2);
    return TRUE;
}

void BwAdapter_VBlank(BattleSystem *battleSys)
{
    (void)battleSys;
    // Stopped, the view sends nothing (its sprites' OAM entries would show over the menu's)
    if (sActive && !sSuspended) {
        BwBattle_VBlank();
    }
}


// BW2's effects for Platinum's secondary animations (res/battle/scripts/common_anims/anim_subscripts.order), as
// BW2's battle server and client pick them; 0 keeps Platinum's. The weathers' play for the whole field
#define BW_EFFECT_FIELD 0x8000

static const u16 sBw2EffectOfSubAnim[] = {
    [1] = 0x256, // Sleep
    [2] = 0x257, // Poison
    [3] = 0x258, // Burn
    [4] = 0x259, // Freeze
    [5] = 0x25a, // Paralysis
    [6] = 0x25b, // Confusion
    [7] = 0x25c, // Infatuation
    [8] = 0x25d, // Level up
    [9] = 0x25e, // An item from the bag
    [10] = 0x25f, // A held item activating (Leftovers, berries)
    [12] = 0x261, // A stat rising
    [13] = 0x262, // A stat falling
    [14] = 0x263, // HP restored
    [19] = 0x268 | BW_EFFECT_FIELD, // Rain
    [20] = 0x269 | BW_EFFECT_FIELD, // Hail
    [21] = 0x26a | BW_EFFECT_FIELD, // Sandstorm
    [22] = 0x26b | BW_EFFECT_FIELD, // Sunlight
    [30] = 0x273, // Curse's damage
    [31] = 0x274, // Nightmare's damage
};

static BOOL BwAdapter_IsSubstituteAnim(int subAnim)
{
    return subAnim == BATTLE_ANIMATION_SUBSTITUTE_IN || subAnim == BATTLE_ANIMATION_SUBSTITUTE_OUT
        || subAnim == BATTLE_ANIMATION_SUB_OUT || subAnim == BATTLE_ANIMATION_SUB_IN;
}

// BW2's effects for Platinum's substitute animations, by what BW2's sprite shows: the doll appearing (0x26e) and
// fading (0x26f), and the doll swapped for the Pokémon (0x264) and back (0x265), which BW2's effects also play around
// a move of a Pokémon behind a substitute. 0 when the sprite already shows what the animation leads to
#define BW_EFFECT_SUBSTITUTE_OUT 0x264
#define BW_EFFECT_SUBSTITUTE_IN 0x265
#define BW_EFFECT_SUBSTITUTE_MAKE 0x26e
#define BW_EFFECT_SUBSTITUTE_FADE 0x26f

static u16 BwAdapter_SubstituteEffect(BattleSystem *battleSys, int battler, int subAnim)
{
    bool shown;
    BOOL active = BwBattleView_HasSubstitute(BwAdapter_ViewPos(battler), &shown);

    switch (subAnim) {
    case BATTLE_ANIMATION_SUBSTITUTE_IN:
        sBattlers[battler].realSprite = FALSE;
        return shown ? 0 : BW_EFFECT_SUBSTITUTE_MAKE;
    case BATTLE_ANIMATION_SUBSTITUTE_OUT:
        return active || shown ? BW_EFFECT_SUBSTITUTE_FADE : 0;
    case BATTLE_ANIMATION_SUB_OUT:
        return shown ? BW_EFFECT_SUBSTITUTE_OUT : 0;
    default:
        return active && !shown && !sBattlers[battler].realSprite && BwAdapter_HasSubstitute(battleSys, battler)
            ? BW_EFFECT_SUBSTITUTE_IN
            : 0;
    }
}

BOOL BwAdapter_StartMoveAnimation(BattleSystem *battleSys, MoveAnimation *animation)
{
    BOOL started;

    if (!sActive) {
        return FALSE;
    }
    // Bag items animate during command selection, before OnCommandSelection(FALSE). Once the idle camera's
    // delay expires it starts another effect before queued animations can run, so stop it before queueing ours.
    if (BwBattleView_IsIdleCameraActive()) {
        BwBattleView_SetIdleCamera(false);
    }
    if (animation->animMode == 0) {
        for (int i = 0; i < BW_ADAPTER_BATTLERS; i++) {
            sBattlers[i].announcedAbility = 0;
        }
        started = BwBattleView_StartMove(BwAdapter_ViewPos(animation->attacker),
            BwAdapter_ViewPos(animation->defender), animation->move);
    } else if (BwAdapter_IsSubstituteAnim(animation->secondaryAnimID)) {
        u16 effect = BwAdapter_SubstituteEffect(battleSys, animation->attacker, animation->secondaryAnimID);

        // Without an effect the animation is over at once; BW2's sprite already shows what Platinum's would
        started = effect == 0 || BwBattleView_StartEffect(BwAdapter_ViewPos(animation->attacker), effect);
    } else {
        BattleContext *context = BattleSystem_GetBattleContext(battleSys);

        if (animation->secondaryAnimID == BATTLE_ANIMATION_STAT_DROP
            && context->sideEffectType == SIDE_EFFECT_TYPE_ABILITY
            && context->attacker < BattleSystem_GetMaxBattlers(battleSys)
            && context->battleMons[context->attacker].ability == ABILITY_INTIMIDATE) {
            if (BwAdapter_ShowAbility(battleSys, context->attacker, ABILITY_INTIMIDATE)) {
                BwBattleView_SetAbilityPopupHeld(BwAdapter_ViewPos(context->attacker), TRUE);
            }
        } else if (animation->secondaryAnimID == BATTLE_ANIMATION_STAT_BOOST
            && context->sideEffectType == SIDE_EFFECT_TYPE_ABILITY
            && context->sideEffectMon < BattleSystem_GetMaxBattlers(battleSys)) {
            int owner = context->sideEffectMon;

            if (BwAdapter_ShowAbility(battleSys, owner, context->battleMons[owner].ability)) {
                BwBattleView_SetAbilityPopupHeld(BwAdapter_ViewPos(owner), TRUE);
            }
        }
        u16 effect = (u32)animation->secondaryAnimID < NELEMS(sBw2EffectOfSubAnim)
            ? sBw2EffectOfSubAnim[animation->secondaryAnimID]
            : 0;

        if (effect == 0) {
            return FALSE;
        }
        started = BwBattleView_StartEffect((effect & BW_EFFECT_FIELD) ? -1 : (int)BwAdapter_ViewPos(animation->attacker),
            effect & ~BW_EFFECT_FIELD);
    }
    if (started) {
        sMoveAnimPending = TRUE;
    }
    return started;
}

BOOL BwAdapter_IsMoveAnimationPending(void)
{
    return sMoveAnimPending;
}

BOOL BwAdapter_UpdateMoveAnimation(void)
{
    if (sMoveAnimPending && !BwBattleView_IsEffectBusy()) {
        sMoveAnimPending = FALSE;
    }
    return sMoveAnimPending;
}
