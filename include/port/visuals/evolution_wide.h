#ifndef POKEPLATINUM_PORT_EVOLUTION_WIDE_H
#define POKEPLATINUM_PORT_EVOLUTION_WIDE_H

#include <nitro.h>

#include "constants/heap.h"

#include "bg_window.h"
#include "palette.h"

// Widescreen evolution scene (General/WidescreenEvolution). The arc backdrop
// on BG3 is drawn by the renderer as a smooth dome across the whole view, in
// the art's own palette colors, down behind the message box. The 3D layer
// (the Pokémon and the effects) spans the view too, and the letterbox bars
// become a darkening around the Pokémon.
//
// Begin turns the wide view on before the BGs are set up and returns FALSE
// (the 4:3 scene) when the setting is off or the view is 4:3. Setup follows
// once the backdrop palettes and the message box tiles are loaded.
BOOL EvolutionWide_Begin(void);
void EvolutionWide_Setup(BgConfig *bgConfig, PaletteData *paletteData, int messageBoxFrame, enum HeapID heapID);
// Each frame, with the scene's letterbox depth (window top, 0..40).
void EvolutionWide_ReplaceLetterbox(int windowTop);
void EvolutionWide_End(void);

#endif // POKEPLATINUM_PORT_EVOLUTION_WIDE_H
