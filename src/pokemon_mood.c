#include "pokemon_mood.h"

#include "constants/items.h"
#include "constants/species.h"

int PokemonMood_Get(Pokemon *mon)
{
    int mood = (s8)(Pokemon_GetValue(mon, MON_DATA_UNUSED_159, NULL) >> 8);
    return mood < -127 ? -127 : mood;
}

void PokemonMood_Adjust(Pokemon *mon, int delta)
{
    if (Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)
        || Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL) == SPECIES_NONE) {
        return;
    }
    int mood = PokemonMood_Get(mon) + delta;
    if (mood > 127) {
        mood = 127;
    } else if (mood < -127) {
        mood = -127;
    }
    u16 packed = Pokemon_GetValue(mon, MON_DATA_UNUSED_159, NULL);
    packed = (packed & 0xFF) | ((u16)(u8)mood << 8);
    Pokemon_SetValue(mon, MON_DATA_UNUSED_159, &packed);
}

void PokemonMood_StepTowardsNeutral(Pokemon *mon)
{
    int mood = PokemonMood_Get(mon);
    if (mood != 0) {
        PokemonMood_Adjust(mon, mood < 0 ? 1 : -1);
    }
}

void PokemonMood_OnFriendshipEvent(Pokemon *mon, int event)
{
    // HG/SS mood modifiers, kept separate from friendship's gain scaling.
    static const s8 changes[FRIENDSHIP_EVENT_MAX] = {
        40, 0, 0, 50, 40, 0, -80, -50, -100, 0
    };
    if (event >= 0 && event < FRIENDSHIP_EVENT_MAX && changes[event] != 0) {
        PokemonMood_Adjust(mon, changes[event]);
    }
}

void PokemonMood_OnItem(Pokemon *mon, u16 item)
{
    int delta = 0;
    switch (item) {
    case ITEM_RARE_CANDY: delta = 40; break;
    case ITEM_ENERGYPOWDER:
    case ITEM_HEAL_POWDER: delta = -20; break;
    case ITEM_ENERGY_ROOT: delta = -30; break;
    case ITEM_REVIVAL_HERB: delta = -40; break;
    case ITEM_HP_UP:
    case ITEM_PROTEIN:
    case ITEM_IRON:
    case ITEM_CARBOS:
    case ITEM_CALCIUM:
    case ITEM_ZINC: delta = 8; break;
    case ITEM_GUARD_SPEC:
    case ITEM_DIRE_HIT:
    case ITEM_X_ATTACK:
    case ITEM_X_DEFENSE:
    case ITEM_X_SPEED:
    case ITEM_X_ACCURACY:
    case ITEM_X_SPECIAL:
    case ITEM_X_SP_DEF: delta = 10; break;
    }
    if (delta != 0) {
        PokemonMood_Adjust(mon, delta);
    }
}
