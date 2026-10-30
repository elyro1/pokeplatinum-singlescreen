#ifndef POKEPLATINUM_FOLLOWING_POKEMON_INTERACTION_H
#define POKEPLATINUM_FOLLOWING_POKEMON_INTERACTION_H

#include "overworld_pokemon.h"

typedef struct FollowingPokemonSituation {
    u32 status;
    u16 hp, maxHP;
    u16 species;
    u16 weather;
    u8 type1, type2, friendship;
    s8 mood;
    BOOL night, cave, grass;
    u8 nature, gender, itemCategory, timeOfDay, facing, nearbyPeople, hiddenItems;
    u8 pool;
    BOOL hasItem, hasFashionCase;
} FollowingPokemonSituation;

typedef struct FollowingPokemonReactionStep {
    u16 animation, message, sound;
    u8 emote, wait;
} FollowingPokemonReactionStep;

typedef struct FollowingPokemonReaction {
    FollowingPokemonReactionStep steps[5];
    u8 count, question;
    u16 yesAction, noAction;
    s8 friendship, mood;
    u8 accessory, supported;
} FollowingPokemonReaction;

typedef struct FollowingPokemonReactionFrame {
    u8 facing, duration;
    s8 dx, height, dz;
    u8 sound;
} FollowingPokemonReactionFrame;

int FollowingPokemon_SelectMessage(const FollowingPokemonSituation *situation, u32 choice);
u16 FollowingPokemon_SelectReaction(const FollowingPokemonSituation *situation, u32 choice);
const FollowingPokemonReaction *FollowingPokemon_GetReaction(u16 action);
const FollowingPokemonReactionFrame *FollowingPokemon_GetReactionAnimation(u16 animation);
BOOL FollowingPokemon_StartInteraction(FieldSystem *fieldSystem, OverworldPokemonHandle handle, int partySlot);
void FollowingPokemon_CancelInteraction(FieldSystem *fieldSystem);
BOOL FollowingPokemon_GetReactionOffset(const MapObject *object, VecFx32 *offset);

#endif
