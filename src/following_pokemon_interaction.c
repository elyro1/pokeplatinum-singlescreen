#include "following_pokemon_interaction.h"

#include <string.h>

#include "constants/battle/condition.h"
#include "constants/items.h"
#include "constants/overworld_weather.h"
#include "constants/species.h"
#include "generated/bg_event_types.h"
#include "generated/genders.h"
#include "generated/map_headers.h"
#include "generated/object_events_gfx.h"

#include "field/field_system.h"
#include "overlay005/ov5_021EB1A0.h"

#include "bag.h"
#include "bg_window.h"
#include "field_message.h"
#include "field_overworld_state.h"
#include "field_system.h"
#include "field_task.h"
#include "heap.h"
#include "item.h"
#include "map_header.h"
#include "map_header_data.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "math_util.h"
#include "menu.h"
#include "message.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon_mood.h"
#include "render_window.h"
#include "rtc.h"
#include "save_player.h"
#include "script_manager.h"
#include "sound_playback.h"
#include "string_template.h"
#include "system.h"
#include "text.h"
#include "unk_020298BC.h"
#include "unk_020559DC.h"
#include "unk_020655F4.h"

#include "res/text/bank/following_pokemon.h"
#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#endif

static BOOL HasType(const FollowingPokemonSituation *situation, int type)
{
    return situation->type1 == type || situation->type2 == type;
}

int FollowingPokemon_SelectMessage(const FollowingPokemonSituation *s, u32 choice)
{
    // Physical condition takes precedence over scenery and positive mood.
    if (s->status & MON_CONDITION_ANY_POISON) {
        return FollowingPokemon_Text_395;
    }
    if (s->status & MON_CONDITION_SLEEP) {
        return FollowingPokemon_Text_404;
    }
    if (s->status & MON_CONDITION_PARALYSIS) {
        return FollowingPokemon_Text_47;
    }
    if (s->status & MON_CONDITION_BURN) {
        return FollowingPokemon_Text_245;
    }
    if (s->status & MON_CONDITION_FREEZE) {
        return FollowingPokemon_Text_248;
    }
    if (s->maxHP != 0 && s->hp * 4 <= s->maxHP) {
        return FollowingPokemon_Text_208;
    }

    if ((s->weather >= OVERWORLD_WEATHER_SNOWING && s->weather <= OVERWORLD_WEATHER_BLIZZARD)
        || s->weather == OVERWORLD_WEATHER_HAILING) {
        static const int ice[] = { FollowingPokemon_Text_182, FollowingPokemon_Text_184, FollowingPokemon_Text_507 };
        static const int cold[] = { FollowingPokemon_Text_17, FollowingPokemon_Text_183 };
        return HasType(s, TYPE_ICE) ? ice[choice % NELEMS(ice)] : cold[choice % NELEMS(cold)];
    }
    if (s->weather >= OVERWORLD_WEATHER_RAINING && s->weather <= OVERWORLD_WEATHER_THUNDERSTORM) {
        static const int wet[] = { FollowingPokemon_Text_92, FollowingPokemon_Text_93, FollowingPokemon_Text_593 };
        if (HasType(s, TYPE_FIRE)) {
            return choice & 1 ? FollowingPokemon_Text_194 : FollowingPokemon_Text_594;
        }
        return HasType(s, TYPE_WATER) ? wet[choice % NELEMS(wet)] : FollowingPokemon_Text_94;
    }
    if (s->mood < -40) {
        static const int sad[] = { FollowingPokemon_Text_15, FollowingPokemon_Text_232, FollowingPokemon_Text_523, FollowingPokemon_Text_703 };
        return sad[choice % NELEMS(sad)];
    }
    if (s->mood >= 40 || s->friendship >= 220) {
        static const int happy[] = { FollowingPokemon_Text_18, FollowingPokemon_Text_20, FollowingPokemon_Text_24, FollowingPokemon_Text_54, FollowingPokemon_Text_55, FollowingPokemon_Text_62, FollowingPokemon_Text_117, FollowingPokemon_Text_552 };
        return happy[choice % NELEMS(happy)];
    }
    if (s->cave) {
        static const int cave[] = { FollowingPokemon_Text_128, FollowingPokemon_Text_243, FollowingPokemon_Text_550 };
        return cave[choice % NELEMS(cave)];
    }
    if (s->grass) {
        static const int grass[] = { FollowingPokemon_Text_157, FollowingPokemon_Text_251, FollowingPokemon_Text_376, FollowingPokemon_Text_377 };
        return grass[choice % NELEMS(grass)];
    }
    if (s->night && (choice & 1)) {
        return choice & 2 ? FollowingPokemon_Text_402 : FollowingPokemon_Text_403;
    }
    static const int neutral[] = { FollowingPokemon_Text_5, FollowingPokemon_Text_105, FollowingPokemon_Text_146, FollowingPokemon_Text_191, FollowingPokemon_Text_197, FollowingPokemon_Text_201, FollowingPokemon_Text_269, FollowingPokemon_Text_272, FollowingPokemon_Text_393 };
    return neutral[choice % NELEMS(neutral)];
}

typedef struct {
    u8 pool, slot;
    u16 action;
    u8 hp, mood, friendship, nature, gender, status, item, type;
    u8 grass, species, weather, facing, people, time, hidden, fashionCase, chance;
} ReactionRule;

#include "res/graphics/following_pokemon/reactions.inc"

const FollowingPokemonReaction *FollowingPokemon_GetReaction(u16 action)
{
    if (action == 0 || action >= NELEMS(sReactions) || !sReactions[action].supported) {
        return NULL;
    }
    return &sReactions[action];
}

const FollowingPokemonReactionFrame *FollowingPokemon_GetReactionAnimation(u16 animation)
{
    return animation > 0 && animation < NELEMS(sReactionAnimations) ? sReactionAnimations[animation] : NULL;
}

static BOOL MoodMatches(int range, int mood)
{
    switch (range) {
    case 0:
        return TRUE;
    case 1:
        return mood == 127;
    case 2:
        return mood >= 100 && mood < 127;
    case 3:
        return mood >= 50 && mood < 100;
    case 4:
        return mood >= 30 && mood < 50;
    case 5:
        return mood > -30 && mood < 30;
    case 6:
        return mood > -50 && mood <= -30;
    case 7:
        return mood > -127 && mood <= -50;
    case 8:
        return mood == -127;
    case 9:
        return mood >= 0;
    case 10:
        return mood < 0;
    }
    return FALSE;
}

static BOOL FriendshipMatches(int range, int value)
{
    switch (range) {
    case 0:
        return TRUE;
    case 1:
        return value == 255;
    case 2:
        return value >= 200 && value < 255;
    case 3:
        return value >= 150 && value < 200;
    case 4:
        return value >= 90 && value < 150;
    case 5:
        return value >= 60 && value < 90;
    case 6:
        return value >= 30 && value < 60;
    case 7:
        return value >= 1 && value < 30;
    case 8:
        return value == 0;
    case 9:
        return value >= 90;
    case 10:
        return value < 60;
    }
    return FALSE;
}

static BOOL SpeciesMatches(int wanted, int category)
{
    switch (wanted) {
    case 0:
        return TRUE;
    case 250:
        return category <= 19;
    case 251:
        return category <= 130;
    case 252:
        return category >= 140 && category <= 149;
    case 253:
        return category >= 160;
    case 254:
        return category >= 220;
    }
    return wanted == category;
}

static BOOL ReactionRuleMatches(const ReactionRule *r, const FollowingPokemonSituation *s)
{
    static const u8 types[] = { 1, 7, 10, 8, 9, 13, 12, 14, 17, 0, 2, 3, 5, 4, 11, 6, 15, 16 };
    static const u8 natures[] = { 4, 5, 4, 4, 1, 4, 3, 2, 1, 2, 5, 6, 3, 1, 1, 3, 6, 3, 5, 6, 2, 2, 1, 3, 6 };
    int pct = s->maxHP ? s->hp * 100 / s->maxHP : 0;
    int hp = pct == 100 ? 1 : pct >= 75 ? 2
        : pct >= 50                     ? 3
        : pct >= 25                     ? 4
                                        : 5;
    int status = s->status & MON_CONDITION_ANY_POISON ? 5
        : s->status & MON_CONDITION_SLEEP             ? 8
        : s->status & MON_CONDITION_BURN              ? 2
        : s->status & MON_CONDITION_FREEZE            ? 3
        : s->status & MON_CONDITION_PARALYSIS         ? 4
                                                      : 1;
    int weather = s->weather == OVERWORLD_WEATHER_CLEAR                                           ? 1
        : s->weather >= OVERWORLD_WEATHER_RAINING && s->weather <= OVERWORLD_WEATHER_THUNDERSTORM ? 3
                                                                                                  : 0;
    if (r->hp && r->hp != hp) {
        return FALSE;
    }
    if (r->status && (r->status == 7 ? status == 1 : r->status != status)) {
        return FALSE;
    }
    if (!MoodMatches(r->mood, s->mood) || !FriendshipMatches(r->friendship, s->friendship)) {
        return FALSE;
    }
    if (r->nature && (s->nature >= NELEMS(natures) || r->nature != natures[s->nature])) {
        return FALSE;
    }
    if (r->gender && r->gender != s->gender) {
        return FALSE;
    }
    if (r->item && (r->item == 9 ? !s->hasItem : r->item != s->itemCategory)) {
        return FALSE;
    }
    if (r->type && !((s->type1 < NELEMS(types) && r->type == types[s->type1]) || (s->type2 < NELEMS(types) && r->type == types[s->type2]))) {
        return FALSE;
    }
    if (r->grass && r->grass != (s->grass ? 1 : 2)) {
        return FALSE;
    }
    if (!SpeciesMatches(r->species, s->species < NELEMS(sReactionSpecies) ? sReactionSpecies[s->species] : 0)) {
        return FALSE;
    }
    if (r->weather && r->weather != weather) {
        return FALSE;
    }
    if (r->facing && r->facing != s->facing) {
        return FALSE;
    }
    if (r->time && r->time != s->timeOfDay) {
        return FALSE;
    }
    if (r->people && (r->people == 5 ? s->nearbyPeople < 5 : r->people != s->nearbyPeople)) {
        return FALSE;
    }
    if (r->hidden && (r->hidden == 4 ? s->hiddenItems < 4 : r->hidden != s->hiddenItems)) {
        return FALSE;
    }
    if (r->fashionCase && !s->hasFashionCase) {
        return FALSE;
    }
    const FollowingPokemonReaction *action = FollowingPokemon_GetReaction(r->action);
    return action != NULL && (!action->accessory || s->hasFashionCase);
}

u16 FollowingPokemon_SelectReaction(const FollowingPokemonSituation *s, u32 choice)
{
    int message = FollowingPokemon_SelectMessage(s, choice);
    // Preserve physical-condition priority and the established Sinnoh weather
    // reactions. They must never become a happy question through a random roll.
    BOOL weather = s->weather >= OVERWORLD_WEATHER_RAINING && s->weather <= OVERWORLD_WEATHER_BLIZZARD;
    if (s->status || (s->maxHP && s->hp * 4 <= s->maxHP) || weather || s->weather == OVERWORLD_WEATHER_HAILING) {
        return sMessageReactions[message];
    }
    // HG order: urgent general conditions, thematic local conditions, then the
    // remaining general conditions. The generator excludes untranslated IDs.
    for (int phase = 0; phase < 3; phase++) {
        for (int i = 0; i < NELEMS(sReactionRules); i++) {
            const ReactionRule *r = &sReactionRules[i];
            if (phase == 0       ? (r->pool != 0 || r->slot >= 12)
                    : phase == 1 ? (r->pool == 0 || r->pool != s->pool)
                                 : (r->pool != 0 || r->slot < 12)) {
                continue;
            }
            // A local deterministic stream keeps selector tests reproducible
            // while giving each rule a separate probability roll.
            choice = choice * 1664525 + 1013904223;
            if ((choice >> 16) % 100 < r->chance && ReactionRuleMatches(r, s)) {
                return r->action;
            }
        }
    }
    return sMessageReactions[message];
}

enum ReactionState {
    REACTION_BEGIN,
    REACTION_STEP,
    REACTION_ANIMATION,
    REACTION_EMOTE,
    REACTION_MESSAGE,
    REACTION_WAIT,
    REACTION_EFFECTS,
    REACTION_QUESTION,
    REACTION_GIFT,
    REACTION_DONE
};

typedef struct {
    MapObject *object;
    u32 handle;
    u16 localID, mapID;
    BOOL wasPaused;
} ReactionMovementPause;

typedef struct FollowerInteraction {
    OverworldPokemonHandle handle;
    u32 personality;
    u16 species, action;
    u8 slot, form, state, step, stage, frame, frameTicks;
    u8 savedFacing, printer, branches, clock;
    u16 ticks, beginTicks;
    BOOL paused, windowOpen, cancelled, active, animating;
    VecFx32 offset;
    FollowerEmote *emote;
    Menu *menu;
    Window window;
    String *text;
    StringTemplate *template;
    MessageLoader *loader;
    MapObjectManager *pausedManager;
    ReactionMovementPause *movementPauses;
    int pauseCount;
} FollowerInteraction;

static BOOL PauseReactionMovement(FieldSystem *field, FollowerInteraction *env)
{
    int capacity = MapObjectMan_GetMaxObjects(field->mapObjMan);
    env->movementPauses = Heap_AllocAtEnd(HEAP_ID_FIELD3, capacity * sizeof(ReactionMovementPause));
    if (env->movementPauses == NULL) {
        return FALSE;
    }
    env->pausedManager = field->mapObjMan;
    MapObject *object;
    int index = 0;
    while (env->pauseCount < capacity && MapObjectMan_FindObjectWithStatus(field->mapObjMan, &object, &index, MAP_OBJ_STATUS_0)) {
        ReactionMovementPause *pause = &env->movementPauses[env->pauseCount++];
        *pause = (ReactionMovementPause) { object, MapObject_GetPokemonActorHandle(object), MapObject_GetLocalID(object), MapObject_GetMapHeaderID(object), MapObject_IsMovementPaused(object) };
        MapObject_SetPauseMovementOn(object);
    }
    env->paused = TRUE;
    return TRUE;
}

static void RestoreReactionMovement(FieldSystem *field, FollowerInteraction *env)
{
    if (field->mapObjMan == env->pausedManager && env->movementPauses) {
        MapObject *object;
        int index = 0;
        while (MapObjectMan_FindObjectWithStatus(field->mapObjMan, &object, &index, MAP_OBJ_STATUS_0)) {
            for (int i = 0; i < env->pauseCount; i++) {
                ReactionMovementPause *pause = &env->movementPauses[i];
                if (pause->object == object && pause->handle == MapObject_GetPokemonActorHandle(object)
                    && pause->localID == MapObject_GetLocalID(object) && pause->mapID == MapObject_GetMapHeaderID(object)) {
                    if (!pause->wasPaused) {
                        MapObject_SetPauseMovementOff(object);
                    }
                    break;
                }
            }
        }
    }
    if (env->movementPauses) {
        Heap_Free(env->movementPauses);
    }
    env->movementPauses = NULL;
}

static Pokemon *InteractionPokemon(FieldSystem *field, const FollowerInteraction *env)
{
    Party *party = SaveData_GetParty(field->saveData);
    if (env->slot >= Party_GetCurrentCount(party)) {
        return NULL;
    }
    Pokemon *mon = Party_GetPokemonBySlotIndex(party, env->slot);
    if (Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL) != env->species
        || Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL) != env->personality
        || Pokemon_GetValue(mon, MON_DATA_FORM, NULL) != env->form
        || Pokemon_GetValue(mon, MON_DATA_HP, NULL) == 0
        || Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
        return NULL;
    }
    return mon;
}

BOOL FollowingPokemon_GetReactionOffset(const MapObject *object, VecFx32 *offset)
{
    FieldSystem *field = MapObject_FieldSystem(object);
    FollowerInteraction *env = field->followerInteraction;
    if (env == NULL || !env->active || env->cancelled || env->handle != MapObject_GetPokemonActorHandle(object)) {
        return FALSE;
    }
    *offset = env->offset;
    return TRUE;
}

static void CloseReactionMessage(FollowerInteraction *env)
{
    if (env->windowOpen) {
        if (Text_IsPrinterActive(env->printer)) {
            Text_RemovePrinter(env->printer);
        }
        Window_EraseMessageBox(&env->window, FALSE);
        Window_Remove(&env->window);
        env->windowOpen = FALSE;
    }
    if (env->text != NULL) {
        String_Free(env->text);
        env->text = NULL;
    }
}

static void ReleaseInteraction(FieldSystem *field, FollowerInteraction *env)
{
    if (env->menu) {
        Menu_DestroyForExit(env->menu, HEAP_ID_FIELD3);
        env->menu = NULL;
    }
    CloseReactionMessage(env);
    FollowingPokemon_FreeEmote(env->emote);
    env->emote = NULL;
    env->active = FALSE;
    memset(&env->offset, 0, sizeof(env->offset));
    MapObject *object = OverworldPokemon_GetObject(field, env->handle);
    if (object && env->paused) {
        MapObject_Face(object, env->savedFacing);
        sub_02062B68(object);
    }
    RestoreReactionMovement(field, env);
    env->paused = FALSE;
    if (env->loader) {
        MessageLoader_Free(env->loader);
    }
    if (env->template) {
        StringTemplate_Free(env->template);
    }
    env->loader = NULL;
    env->template = NULL;
    if (field->followerInteraction == env) {
        field->followerInteraction = NULL;
    }
}

void FollowingPokemon_CancelInteraction(FieldSystem *field)
{
    FollowerInteraction *env = field->followerInteraction;
    if (env == NULL) {
        return;
    }
    // The field task still owns env. Synchronously release field/GPU resources
    // before map teardown; its next invocation only frees the cancelled work.
    env->cancelled = TRUE;
    ReleaseInteraction(field, env);
}

static void ShowReactionMessage(FieldSystem *field, FollowerInteraction *env, int message)
{
    CloseReactionMessage(env);
    String *raw = MessageLoader_GetNewString(env->loader, message);
    env->text = String_Init(1024, HEAP_ID_FIELD3);
    StringTemplate_Format(env->template, env->text, raw);
    String_Free(raw);
    FieldMessage_AddWindow(field->bgConfig, &env->window, BG_LAYER_MAIN_3);
    FieldMessage_DrawWindow(&env->window, SaveData_GetOptions(field->saveData));
    env->printer = FieldMessage_Print(&env->window, env->text, SaveData_GetOptions(field->saveData), TRUE);
    env->windowOpen = TRUE;
}

static FollowingPokemonSituation BuildSituation(FieldSystem *field, Pokemon *mon, MapObject *object)
{
    u8 tile = MapObject_GetCurrTileBehavior(object);
    FollowingPokemonSituation s = {
        .status = Pokemon_GetValue(mon, MON_DATA_STATUS, NULL),
        .hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL),
        .maxHP = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL),
        .species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL),
        .type1 = Pokemon_GetValue(mon, MON_DATA_TYPE_1, NULL),
        .type2 = Pokemon_GetValue(mon, MON_DATA_TYPE_2, NULL),
        .friendship = Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL),
        .mood = PokemonMood_Get(mon),
        .weather = FieldOverworldState_GetWeather(SaveData_GetFieldOverworldState(field->saveData)),
        .night = IsNight(),
        .cave = MapHeader_IsCave(field->location->mapHeaderID),
        .grass = TileBehavior_IsTallGrass(tile) || TileBehavior_IsVeryTallGrass(tile),
        .nature = Pokemon_GetNature(mon),
        .gender = Pokemon_GetGender(mon) == GENDER_MALE ? 1 : 2,
        .timeOfDay = FieldSystem_GetTimeOfDay(field) + 1,
        .hasFashionCase = Bag_GetItemQuantity(SaveData_GetBag(field->saveData), ITEM_FASHION_CASE, HEAP_ID_FIELD3) != 0
    };
    static const u8 facing[] = { 3, 4, 2, 1 };
    s.facing = facing[MapObject_GetFacingDir(object)];
    u16 item = Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
    static const u8 pockets[] = { 4, 2, 1, 7, 6, 5, 3 };
    s.hasItem = item != ITEM_NONE;
    u32 pocket = s.hasItem ? Item_LoadParam(item, ITEM_PARAM_FIELD_POCKET, HEAP_ID_FIELD3) : 255;
    s.itemCategory = pocket < NELEMS(pockets) ? pockets[pocket] : 8;
    s.pool = s.cave ? 3 : MapHeader_IsBuilding(field->location->mapHeaderID) ? 1
        : MapHeader_HasWildEncounters(field->location->mapHeaderID)          ? 2
                                                                             : 1;
    if (s.weather >= OVERWORLD_WEATHER_SNOWING && s.weather <= OVERWORLD_WEATHER_BLIZZARD) {
        s.pool = 5;
    }
    if (field->location->mapHeaderID == MAP_HEADER_ETERNA_FOREST) {
        s.pool = 4;
    }
    MapObject *other;
    int index = 0;
    while (MapObjectMan_FindObjectWithStatus(field->mapObjMan, &other, &index, MAP_OBJ_STATUS_0)) {
        if (other == object || other == PlayerAvatar_GetMapObject(field->playerAvatar)
            || OverworldPokemonAssets_IsGraphicsID(MapObject_GetGraphicsID(other))) {
            continue;
        }
        int gfx = MapObject_GetGraphicsID(other);
        if (gfx == OBJ_EVENT_GFX_STRENGTH_BOULDER || gfx == OBJ_EVENT_GFX_ROCK_SMASH || gfx == OBJ_EVENT_GFX_CUT_TREE) {
            continue;
        }
        int dx = MapObject_GetX(other) - MapObject_GetX(object), dz = MapObject_GetZ(other) - MapObject_GetZ(object);
        if (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1 && !MapObject_CheckStatusFlag(other, MAP_OBJ_STATUS_HIDE)
            && s.nearbyPeople < 5) {
            s.nearbyPeople++;
        }
    }
    const BgEvent *events = MapHeaderData_GetBgEvents(field);
    for (int i = 0; events && i < MapHeaderData_GetNumBgEvents(field); i++) {
        if (events[i].type == BG_EVENT_TYPE_HIDDEN_ITEM
            && !FieldSystem_CheckFlag(field, Script_GetHiddenItemFlag(events[i].script)) && s.hiddenItems < 4) {
            s.hiddenItems++;
        }
    }
    return s;
}

static void SetReaction(FollowerInteraction *env, u16 action)
{
    env->action = action;
    env->step = env->stage = 0;
    env->state = REACTION_STEP;
}

static void PlayReactionSound(FollowerInteraction *env, int sound)
{
    if (sound == 2379 || sound == 2380) {
        Sound_PlayPokemonCryEx(sound == 2379 ? POKECRY_NORMAL : POKECRY_PINCH_NORMAL,
            env->species,
            0,
            127,
            HEAP_ID_FIELD3,
            env->form);
    }
}

static BOOL AdvanceReactionAnimation(FollowerInteraction *env, MapObject *object, const FollowingPokemonReactionStep *step)
{
    const FollowingPokemonReactionFrame *frames = FollowingPokemon_GetReactionAnimation(step->animation);
    if (frames == NULL || env->frame >= 10 || frames[env->frame].facing == 255) {
        memset(&env->offset, 0, sizeof(env->offset));
        MapObject_Face(object, env->savedFacing);
        env->animating = FALSE;
        return TRUE;
    }
    const FollowingPokemonReactionFrame *frame = &frames[env->frame];
    if (env->frameTicks == 0) {
        env->offset.x += frame->dx * FX32_ONE;
        env->offset.z += frame->dz * FX32_ONE;
        if (env->species != SPECIES_DIGLETT && env->species != SPECIES_DUGTRIO) {
            env->offset.y += frame->height * FX32_ONE;
        }
        if (frame->facing) {
            MapObject_Face(object, frame->facing - 1);
        }
        if (frame->sound) {
            PlayReactionSound(env, step->sound);
        }
    }
    if (++env->frameTicks >= frame->duration) {
        env->frame++;
        env->frameTicks = 0;
    }
    return FALSE;
}

static BOOL RunInteraction(FieldTask *task)
{
    FieldSystem *field = FieldTask_GetFieldSystem(task);
    FollowerInteraction *env = FieldTask_GetEnv(task);
    if (env->cancelled) {
        Heap_Free(env);
        return TRUE;
    }
    MapObject *object = OverworldPokemon_GetObject(field, env->handle);
    Pokemon *mon = InteractionPokemon(field, env);
    if (object == NULL || mon == NULL || FollowingPokemon_GetHandle(field) != env->handle) {
        ReleaseInteraction(field, env);
        Heap_Free(env);
        return TRUE;
    }
    // HG data uses 30 Hz timings. Only rendering runs at the port's optional
    // 60 Hz rate, keeping reaction duration and displacement platform neutral.
#ifdef SDK_PORT
    if (SIM_Config_prj_GetConfig()->enable60fps && (env->clock++ & 1)
        && (env->state == REACTION_ANIMATION || env->state == REACTION_EMOTE || env->state == REACTION_WAIT)) {
        return FALSE;
    }
#endif
    const FollowingPokemonReaction *action = FollowingPokemon_GetReaction(env->action);
    const FollowingPokemonReactionStep *step = action && env->step < action->count ? &action->steps[env->step] : NULL;
    switch (env->state) {
    case REACTION_BEGIN: {
        if (MapObject_IsMoving(object) || MapObject_IsMoving(PlayerAvatar_GetMapObject(field->playerAvatar))) {
            if (++env->beginTicks < 120) {
                return FALSE;
            }
            env->state = REACTION_DONE;
            break;
        }
        if (!PauseReactionMovement(field, env)) {
            env->state = REACTION_DONE;
            break;
        }
        env->active = TRUE;
        MapObject_TryFace(object, Direction_GetOpposite(PlayerAvatar_GetFacingDir(field->playerAvatar)));
        env->savedFacing = MapObject_GetFacingDir(object);
        FollowingPokemonSituation situation = BuildSituation(field, mon, object);
        env->loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, TEXT_BANK_FOLLOWING_POKEMON, HEAP_ID_FIELD3);
        env->template = StringTemplate_Default(HEAP_ID_FIELD3);
        StringTemplate_SetNickname(env->template, 0, Pokemon_GetBoxPokemon(mon));
        StringTemplate_SetSpeciesName(env->template, 1, Pokemon_GetBoxPokemon(mon));
        StringTemplate_SetPlayerName(env->template, 2, SaveData_GetTrainerInfo(field->saveData));
        StringTemplate_SetLocationName(env->template, 3, MapHeader_GetMapLabelTextID(field->location->mapHeaderID));
        StringTemplate_SetItemName(env->template, 4, Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL));
        SetReaction(env, FollowingPokemon_SelectReaction(&situation, LCRNG_Next()));
        if (FollowingPokemon_GetReaction(env->action) == NULL) {
            Sound_PlayPokemonCry(env->species, env->form);
            ShowReactionMessage(field, env, FollowingPokemon_SelectMessage(&situation, LCRNG_Next()));
            env->state = REACTION_GIFT; // ordinary message dismissal, then done
        }
        break;
    }
    case REACTION_STEP:
        if (step == NULL) {
            env->state = REACTION_EFFECTS;
            break;
        }
        if (env->stage == 0) {
            env->stage++;
            if (FollowingPokemon_GetReactionAnimation(step->animation)) {
                env->frame = env->frameTicks = 0;
                env->animating = TRUE;
                env->state = REACTION_ANIMATION;
                break;
            }
        }
        if (env->stage == 1) {
            env->stage++;
            if (step->emote) {
                env->emote = FollowingPokemon_CreateEmote(object, step->emote);
                env->ticks = 0;
                env->state = REACTION_EMOTE;
                break;
            }
        }
        if (env->stage == 2) {
            env->stage++;
            if (step->message) {
                ShowReactionMessage(field, env, step->message - 1);
                env->state = REACTION_MESSAGE;
                break;
            }
        }
        env->ticks = 0;
        env->state = REACTION_WAIT;
        break;
    case REACTION_ANIMATION:
        if (AdvanceReactionAnimation(env, object, step)) {
            env->state = REACTION_STEP;
        }
        break;
    case REACTION_EMOTE:
        FollowingPokemon_UpdateEmote(env->emote, object, env->ticks);
        if (++env->ticks >= 36) {
            FollowingPokemon_FreeEmote(env->emote);
            env->emote = NULL;
            env->state = REACTION_STEP;
        }
        break;
    case REACTION_MESSAGE:
        if (FieldMessage_FinishedPrinting(env->printer)
            && ((action->question && env->step + 1 == action->count) || (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B)))) {
            if (!action->question || env->step + 1 != action->count) {
                CloseReactionMessage(env);
            }
            env->state = REACTION_STEP;
        }
        break;
    case REACTION_WAIT:
        if (++env->ticks >= step->wait) {
            env->step++;
            env->stage = 0;
            env->state = REACTION_STEP;
        }
        break;
    case REACTION_EFFECTS:
        if (action == NULL) {
            env->state = REACTION_DONE;
            break;
        }
        PokemonMood_Adjust(mon, action->mood);
        if (action->friendship) {
            int friendship = Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL) + action->friendship;
            u8 value = friendship < 0 ? 0 : friendship > 255 ? 255
                                                             : friendship;
            Pokemon_SetValue(mon, MON_DATA_FRIENDSHIP, &value);
        }
        if (action->question && env->branches < 8) {
            static const WindowTemplate menuWindow = { BG_LAYER_MAIN_3, 25, 13, 6, 4, 13, 0x21F };
            LoadStandardWindowGraphics(field->bgConfig, BG_LAYER_MAIN_3, 1024 - (18 + 12) - 9, 11, STANDARD_WINDOW_SYSTEM, HEAP_ID_FIELD3);
            env->menu = Menu_MakeYesNoChoice(field->bgConfig, &menuWindow, 1024 - (18 + 12) - 9, 11, HEAP_ID_FIELD3);
            env->state = REACTION_QUESTION;
        } else if (action->accessory && action->accessory <= 100
            && Bag_GetItemQuantity(SaveData_GetBag(field->saveData), ITEM_FASHION_CASE, HEAP_ID_FIELD3)) {
            FashionCase *fashionCase = ImageClips_GetFashionCase(SaveData_GetImageClips(field->saveData));
            int accessory = action->accessory - 1;
            BOOL fits = FashionCase_CanFitAccessoryCount(fashionCase, accessory, 1);
            if (fits) {
                FashionCase_AddAccessory(fashionCase, accessory, 1);
            }
            StringTemplate_SetPlayerName(env->template, 0, SaveData_GetTrainerInfo(field->saveData));
            if (fits) {
                StringTemplate_SetContestAccessoryName(env->template, 1, accessory);
            } else {
                StringTemplate_SetContestAccessoryNameWithArticle(env->template, 1, accessory);
            }
            ShowReactionMessage(field, env, fits ? FollowingPokemon_Text_AccessoryStored : FollowingPokemon_Text_AccessoryFull);
            if (fits) {
                Sound_PlayFanfare(SEQ_ACCE_sseq);
            }
            env->state = REACTION_GIFT;
        } else {
            env->state = REACTION_DONE;
        }
        break;
    case REACTION_QUESTION: {
        menu_selection_t result = Menu_ProcessInputAndHandleExit(env->menu, HEAP_ID_FIELD3);
        if (result == MENU_NOTHING_CHOSEN) {
            break;
        }
        env->menu = NULL;
        CloseReactionMessage(env);
        env->branches++;
        u16 next = result == MENU_YES ? action->yesAction : action->noAction;
        if (FollowingPokemon_GetReaction(next)) {
            SetReaction(env, next);
        } else {
            env->state = REACTION_DONE;
        }
        break;
    }
    case REACTION_GIFT:
        if (FieldMessage_FinishedPrinting(env->printer) && !Sound_IsBGMPausedByFanfare()
            && (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B))) {
            env->state = REACTION_DONE;
        }
        break;
    case REACTION_DONE:
        ReleaseInteraction(field, env);
        Heap_Free(env);
        return TRUE;
    }
    // Paused MapObjects skip their draw update. Update just this actor, once,
    // without unpausing its movement controller or touching neighboring copies.
    if (env->active && MapObject_IsMovementPaused(object)) {
        sub_02062B68(object);
    }
    return FALSE;
}

BOOL FollowingPokemon_StartInteraction(FieldSystem *field, OverworldPokemonHandle handle, int slot)
{
    if (FieldSystem_IsRunningTask(field) || field->followerInteraction || !handle
        || handle != FollowingPokemon_GetHandle(field) || !OverworldPokemon_GetObject(field, handle)
        || slot < 0 || slot >= Party_GetCurrentCount(SaveData_GetParty(field->saveData))) {
        return FALSE;
    }
    FollowerInteraction *env = Heap_AllocAtEnd(HEAP_ID_FIELD3, sizeof(*env));
    if (env == NULL) {
        return FALSE;
    }
    memset(env, 0, sizeof(*env));
    Pokemon *mon = Party_GetPokemonBySlotIndex(SaveData_GetParty(field->saveData), slot);
    env->handle = handle;
    env->slot = slot;
    env->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    env->personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
    env->form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    field->followerInteraction = env;
    FieldSystem_CreateTask(field, RunInteraction, env);
    return TRUE;
}
