#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/text.c"
#include "../../src/render_text.c"
#include "../../src/charcode.c"
#include "../../src/port/battle_timing.c"

static SIM_Config_prj_type sConfig;
static SysTask sTask;
static int sGlyphs, sCopies, sSounds, sCallbackCount;
static BOOL sBlockCallback;
System gSystem;
void ErrorHandling_AssertFail(void) { assert(0); }
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
void *Heap_Alloc(u32 heap, u32 size) { return calloc(1, size); }
void Heap_Free(void *ptr) { free(ptr); }
const charcode_t *String_GetData(const String *string) { return string->data; }
SysTask *SysTask_CreateOnPrintQueue(SysTaskFunc callback, void *param, u32 priority)
{
    assert(BattleTiming_GetDomain() == BATTLE_TIMING_NONE);
    sTask = (SysTask){ .callback = callback, .param = param };
    return &sTask;
}
void *SysTask_GetParam(SysTask *task) { return task->param; }
void SysTask_Done(SysTask *task) { task->param = NULL; }
enum RenderResult Font_RenderText(enum Font font, TextPrinter *printer) { return RenderText(printer); }
const TextGlyph *Font_TryLoadGlyph(enum Font font, charcode_t code) { static const TextGlyph glyph = { .width = 1, .height = 1 }; return &glyph; }
u8 Font_GetAttribute(u8 font, u8 attr) { return 1; }
void Window_CopyToVRAM(Window *window) { sCopies++; }
void Window_CopyGlyph(Window *window, const u8 *pixels, u16 w, u16 h, u16 x, u16 y, u16 table) { sGlyphs++; }
void Window_FillTilemap(Window *window, u8 value) {}
void Window_Scroll(Window *window, u8 dir, u8 distance, u8 fill) {}
BgConfig *Window_GetBgConfig(Window *window) { return window->bgConfig; }
u32 BgConfig_GetHeapID(BgConfig *config) { return HEAP_ID_BATTLE; }
u8 Window_GetBgLayer(Window *window) { return window->bgLayer; }
u8 Window_GetWidth(Window *window) { return window->width; }
u8 Window_GetXPos(Window *window) { return window->tilemapLeft; }
u8 Window_GetYPos(Window *window) { return window->tilemapTop; }
void Bg_FillTilemapRect(BgConfig *config, u8 layer, u16 fill, u8 x, u8 y, u8 w, u8 h, u8 palette) {}
void Bg_CopyTilemapBufferToVRAM(BgConfig *config, u8 layer) {}
void Window_BlitBitmapRect(Window *window, void *src, u16 sx, u16 sy, u16 sw, u16 sh, u16 dx, u16 dy, u16 dw, u16 dh) {}
void *Graphics_GetCharData(enum NarcID narc, u32 member, BOOL compressed, NNSG2dCharacterData **out, enum HeapID heap) { assert(0); return NULL; }
void MIi_CpuCopy32(const void *src, void *dst, u32 size) { memcpy(dst, src, size); }
BOOL Sound_PlayEffect(u16 id) { sSounds++; return TRUE; }

static BOOL Test_Callback(TextPrinterTemplate *printer, u16 param)
{
    sCallbackCount++;
    return param == 42 && sBlockCallback;
}

static u8 Start(const charcode_t *chars, int delay, TextPrinterCallback callback)
{
    static Window window;
    String *string = calloc(1, sizeof(String) + 512 * sizeof(charcode_t));
    unsigned count = 0;
    do { string->data[count] = chars[count]; } while (chars[count++] != CHAR_EOS);
    TextPrinterTemplate template = { .window = &window, .fontID = FONT_MESSAGE, .fgColor = 1,
        .bgColor = 15, .shadowColor = 2, .toPrint.string = string };
    u8 id = Text_AddPrinter(&template, delay, callback);
    // The printer retains raw characters, as in the game. Keep the buffer through the test.
    static String *previous;
    free(previous);
    previous = string;
    return id;
}

static void Tick(void)
{
    assert(sTask.param);
    sTask.callback(&sTask, sTask.param);
}

int main(void)
{
    static const FontAttributes attributes[FONT_MAX + 1];
    Text_SetFontAttributesPtr(attributes);
    BattleTiming_SetActive(TRUE);
    const charcode_t plain[] = { CHAR_A, CHAR_B, CHAR_C, CHAR_D, CHAR_E, CHAR_EOS };
    for (int speed = 0; speed < PORT_SPEED_COUNT; speed++) {
        for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
            for (int savedDelay = TEXT_SPEED_FAST; savedDelay <= TEXT_SPEED_SLOW; savedDelay++) {
                sConfig.textSpeed = speed;
                sConfig.battlePacing = pacing;
                BattleTiming_SetDomain(BATTLE_TIMING_ANIMATION);
                sGlyphs = 0;
                u8 id = Start(plain, savedDelay, NULL);
                assert(Text_IsPrinterActive(id));
                assert(BattleTiming_GetDomain() == BATTLE_TIMING_ANIMATION);
                Tick();
                assert(sGlyphs == (speed == PORT_SPEED_FASTER ? 2 : speed == PORT_SPEED_INSTANT ? 5 : 1));
                if (speed == PORT_SPEED_NORMAL) {
                    for (int i = 0; i < TEXT_SPEED_NORMAL - 1; i++) { Tick(); assert(sGlyphs == 1); }
                    Tick();
                    assert(sGlyphs == 2);
                }
                while (Text_IsPrinterActive(id)) Tick();
                assert(sGlyphs == 5);
            }
        }
    }
    // Normal has the original 30 FPS timing even when the port runs at 60 FPS.
    sConfig.textSpeed = PORT_SPEED_NORMAL;
    sConfig.enable60fps = TRUE;
    sGlyphs = 0;
    u8 normalId = Start(plain, TEXT_SPEED_FAST, NULL);
    Tick();
    for (int i = 0; i < TEXT_SPEED_NORMAL * 2 - 1; i++) { Tick(); assert(sGlyphs == 1); }
    Tick();
    assert(sGlyphs == 2);
    // Changing speed during printing takes effect without restarting the printer.
    sConfig.textSpeed = PORT_SPEED_FAST;
    Tick();
    assert(sGlyphs == 3);
    while (Text_IsPrinterActive(normalId)) Tick();
    sConfig.enable60fps = FALSE;

    // Native immediate and no-transfer callers remain synchronous, with their transfer semantics.
    sCopies = 0;
    assert(Start(plain, TEXT_SPEED_INSTANT, NULL) == MAX_TEXT_PRINTERS && sCopies == 1);
    assert(Start(plain, TEXT_SPEED_NO_TRANSFER, NULL) == MAX_TEXT_PRINTERS && sCopies == 1);

    // Instant text still waits for acknowledgement between pages.
    sConfig.textSpeed = PORT_SPEED_INSTANT;
    const charcode_t pages[] = { CHAR_A, CHAR_CONTROL_CLEAR, CHAR_B, CHAR_EOS };
    u8 id = Start(pages, TEXT_SPEED_SLOW, NULL);
    sGlyphs = 0;
    Tick();
    assert(sGlyphs == 1 && Text_IsPrinterActive(id));
    for (int i = 0; i < 4; i++) Tick();
    assert(sGlyphs == 1 && Text_IsPrinterActive(id));
    gSystem.pressedKeys = PAD_BUTTON_A;
    Tick();
    gSystem.pressedKeys = 0;
    assert(sSounds == 1);
    Tick();
    assert(sGlyphs == 2 && !Text_IsPrinterActive(id));

    // Embedded timed pauses and callbacks get one update, never the glyph budget.
    const charcode_t pause[] = { CHAR_A, CHAR_FORMAT_ARG, CHAR_CONTROL_PAUSE, 1, 3, CHAR_B, CHAR_EOS };
    id = Start(pause, TEXT_SPEED_FAST, NULL);
    sGlyphs = 0;
    Tick();
    for (int i = 0; i < 4; i++) { Tick(); assert(sGlyphs == 1); }
    Tick();
    assert(sGlyphs == 2 && !Text_IsPrinterActive(id));
    const charcode_t callback[] = { CHAR_A, CHAR_FORMAT_ARG, CHAR_CONTROL_CALLBACK, 1, 42, CHAR_B, CHAR_EOS };
    sBlockCallback = TRUE;
    id = Start(callback, TEXT_SPEED_FAST, Test_Callback);
    sGlyphs = sCallbackCount = 0;
    Tick();
    assert(sGlyphs == 1 && sBlockCallback && Text_IsPrinterActive(id));
    Tick();
    assert(sGlyphs == 1);
    sBlockCallback = FALSE;
    Tick();
    assert(sGlyphs == 1);
    Tick();
    assert(sGlyphs == 2 && !Text_IsPrinterActive(id));
    // Enemy tag-battle send-out messages end each trainer's page with an
    // automatic clear. Test the real printer without simulated button presses.
    for (int active = 0; active < 2; active++) {
        for (int fps = 0; fps < 2; fps++) {
            for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
                for (int skippable = 0; skippable < 2; skippable++) {
                    sConfig.enable60fps = fps;
                    sConfig.battlePacing = pacing;
                    BattleTiming_SetActive(active);
                    RenderControlFlags_SetAutoScrollFlags(skippable ? AUTO_SCROLL_NO_WAIT : AUTO_SCROLL_ENABLED);
                    int dwell = active && fps && skippable
                        ? (pacing == BATTLE_PACING_TURBO ? 4 : pacing == BATTLE_PACING_QUICK ? 8 : 100) : 100;
                    id = Start(pages, TEXT_SPEED_FAST, NULL);
                    sGlyphs = 0;
                    Tick();
                    for (int i = 0; i < dwell; i++) {
                        Tick();
                        assert(sGlyphs == 1 && Text_IsPrinterActive(id));
                    }
                    Tick(); // Release the automatic page wait.
                    Tick();
                    assert(sGlyphs == 2 && !Text_IsPrinterActive(id));
                }
            }
        }
    }
    // Switching to Quick/Turbo releases a dwell already past its new cap.
    for (int pacing = BATTLE_PACING_QUICK; pacing <= BATTLE_PACING_TURBO; pacing++) {
        TextPrinter waiting = { 0 };
        sConfig.battlePacing = BATTLE_PACING_SMOOTH;
        for (int i = 0; i < 20; i++) assert(!TextPrinter_WaitAutoMode(&waiting));
        sConfig.battlePacing = pacing;
        assert(TextPrinter_WaitAutoMode(&waiting));
    }
    TextPrinter waiting = { 0 };
    RenderControlFlags_SetAutoScrollFlags(AUTO_SCROLL_DISABLED);
    assert(!TextPrinter_Wait(&waiting)); // Required acknowledgements still wait.
    puts("Text speed: saved-setting override, instant pages, prompts, pauses and callbacks passed. Quick/Turbo cap offline automatic page dwell; Smooth, link/recorded, 30 FPS and required acknowledgements retain timing.");
}
