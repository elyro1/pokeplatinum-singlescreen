#include "save_player.h"

#include <nitro.h>
#include <string.h>

#include "coins.h"
#include "game_options.h"
#include "play_time.h"
#include "savedata.h"
#include "trainer_info.h"

int Player_SaveSize(void)
{
    return sizeof(PlayerSave);
}

#ifdef SDK_PORT
// The port keeps the DS save layout byte for byte; see port/assets/ds_layout.h.
_Static_assert(sizeof(PlayerSave) == 44, "Player must keep its DS save size");
#endif

void Player_Init(PlayerSave *player)
{
    MI_CpuClearFast(player, sizeof(PlayerSave));

    Options_Init(&player->options);
    TrainerInfo_Init(&player->info);
    Coins_Init(&player->coins);
    PlayTime_Init(&player->playTime);
}

TrainerInfo *SaveData_GetTrainerInfo(SaveData *saveData)
{
    PlayerSave *state = SaveData_SaveTable(saveData, SAVE_TABLE_ENTRY_PLAYER);
    return &state->info;
}

Options *SaveData_GetOptions(SaveData *saveData)
{
    PlayerSave *state = SaveData_SaveTable(saveData, SAVE_TABLE_ENTRY_PLAYER);
    return &state->options;
}

u16 *SaveData_GetCoins(SaveData *saveData)
{
    PlayerSave *state = SaveData_SaveTable(saveData, SAVE_TABLE_ENTRY_PLAYER);
    return &state->coins;
}

PlayTime *SaveData_GetPlayTime(SaveData *saveData)
{
    PlayerSave *state = SaveData_SaveTable(saveData, SAVE_TABLE_ENTRY_PLAYER);
    return &state->playTime;
}
