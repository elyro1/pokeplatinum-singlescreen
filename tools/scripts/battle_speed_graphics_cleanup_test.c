#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../src/battle/battle_main.c"
#include "../../src/bg_window.c"

static int sFreed;
void Heap_Free(void *ptr) { assert(ptr); sFreed++; free(ptr); }
void SetVBlankCallback(Callback cb, void *data) {}
void BattleSubscreen_Free(BattleSubscreen *screen) {}
void BattleSubscreenBg_Free(BgConfig *bgConfig) {}
void GXLayers_EngineAToggleLayers(u8 layers, u8 enable) {}
void SpriteSystem_FreeResourcesAndManager(SpriteSystem *sys, SpriteManager *manager) {}
void SpriteSystem_Free(SpriteSystem *sys) {}
void VramTransfer_Free(void) {}
void Font_Free(enum Font font) {}

int main(void)
{
    for (int hasWindow = 0; hasWindow < 2; hasWindow++) {
        BgConfig bg = { 0 };
        Window window = { .pixels = hasWindow ? malloc(32) : NULL };
        BattleSystem sys = { .windows = &window, .bgConfig = &bg };
        sFreed = 0;
        BattleSystem_FreeGraphics(&sys);
        assert(window.pixels == NULL);
        assert(sFreed == hasWindow);
    }
    puts("Capture graphics cleanup: allocated and already-removed message windows both release safely.");
}
