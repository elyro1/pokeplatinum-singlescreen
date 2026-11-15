#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "constants/items.h"
#include "generated/moves.h"
#include "item.h"
#include "port/gameplay/gameplay_config.h"
#include "unk_02054884.h"

// Isolate field eligibility from encrypted Pokemon storage and species resource I/O.
static Party sParty;
static BOOL sEggs[MAX_PARTY_SIZE];
static u16 sMoves[MAX_PARTY_SIZE][LEARNED_MOVES_MAX];
static BOOL sCompatibility[MAX_PARTY_SIZE][NUM_HMS];

u32 Pokemon_GetValue(Pokemon *mon, enum PokemonDataParam param, void *dest)
{
    int slot = mon - sParty.pokemon;
    assert(slot >= 0 && slot < MAX_PARTY_SIZE);
    if (param == MON_DATA_IS_EGG) {
        return sEggs[slot];
    }
    assert(param >= MON_DATA_MOVE1 && param <= MON_DATA_MOVE4);
    return sMoves[slot][param - MON_DATA_MOVE1];
}

BOOL Pokemon_CanLearnTM(Pokemon *mon, u8 tmID)
{
    assert(tmID >= NUM_TMS && tmID < NUM_TMHMS);
    return sCompatibility[mon - sParty.pokemon][tmID - NUM_TMS];
}

int Party_GetCurrentCount(const Party *party)
{
    return party->currentCount;
}

Pokemon *Party_GetPokemonBySlotIndex(const Party *party, int slot)
{
    assert(slot >= 0 && slot < party->currentCount);
    return (Pokemon *)&party->pokemon[slot];
}

int main(void)
{
    GameplayConfig_LoadDefaults(GameplayConfig_Get());
    sParty.currentCount = MAX_PARTY_SIZE;
    for (int hm = 0; hm < NUM_HMS; hm++) {
        u16 move = Item_MoveForTMHM(ITEM_HM01 + hm);
        assert(move != MOVE_NONE);
        memset(sMoves, 0, sizeof(sMoves));
        memset(sCompatibility, 0, sizeof(sCompatibility));
        GameplayConfig_Get()->unlearnedHMs = FALSE;
        sCompatibility[2][hm] = TRUE;
        assert(Party_HasMonWithMove(&sParty, move) == PARTY_SLOT_NONE);
        // Existing learned HMs remain available with the option disabled.
        sMoves[4][3] = move;
        assert(Party_HasMonWithMove(&sParty, move) == 4);
        GameplayConfig_Get()->unlearnedHMs = TRUE;
        assert(Party_HasMonWithMove(&sParty, move) == 2);
        sEggs[2] = TRUE;
        assert(Party_HasMonWithMove(&sParty, move) == 4);
        sEggs[4] = TRUE;
        assert(Party_HasMonWithMove(&sParty, move) == PARTY_SLOT_NONE);
        sEggs[2] = sEggs[4] = FALSE;
        sMoves[4][3] = MOVE_NONE;
        sCompatibility[2][hm] = FALSE;
        assert(Party_HasMonWithMove(&sParty, move) == PARTY_SLOT_NONE);
    }
    // Non-HM field moves still require an actual learned move.
    memset(sCompatibility, 1, sizeof(sCompatibility));
    assert(Party_HasMonWithMove(&sParty, MOVE_FLASH) == PARTY_SLOT_NONE);
    assert(Party_HasMonWithMove(&sParty, MOVE_DIG) == PARTY_SLOT_NONE);
    sMoves[1][0] = MOVE_FLASH;
    assert(Party_HasMonWithMove(&sParty, MOVE_FLASH) == 1);
    sParty.currentCount = 0;
    assert(Party_HasMonWithMove(&sParty, MOVE_SURF) == PARTY_SLOT_NONE);
    puts("All eight HM compatibility, learned-move, egg, party, and disabled-option checks passed.");
    return 0;
}
