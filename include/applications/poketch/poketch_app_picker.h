#ifndef POKEPLATINUM_POKETCH_APP_PICKER_H
#define POKEPLATINUM_POKETCH_APP_PICKER_H

#include "constants/heap.h"

#include "bg_window.h"
#include "poketch.h"

// Two-column list of the visible apps for the single-screen Pokétch. It draws on
// the app layer, so it only exists while no app is loaded.
typedef struct PoketchAppPicker PoketchAppPicker;

enum PoketchAppPickerResult {
    POKETCH_APP_PICKER_BROWSING = -1,
    POKETCH_APP_PICKER_CANCEL = -2,
};

PoketchAppPicker *PoketchAppPicker_New(BgConfig *bgConfig, const Poketch *poketch, enum HeapID heapID);
void PoketchAppPicker_Free(PoketchAppPicker *picker);
// Returns the chosen app ID, or one of PoketchAppPickerResult.
int PoketchAppPicker_HandleInput(PoketchAppPicker *picker, u32 pressedKeys, u32 repeatKeys, u32 cancelKeys);

#endif // POKEPLATINUM_POKETCH_APP_PICKER_H
