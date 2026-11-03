// Real SDL dummy-device callback and SoundTouch, checked at static destruction.
#include <nitro.h>
#include <simulator/sim_audio.h>
#include <nitro/snd/common/work.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Audio shutdown check failed at line %d: %s\n", __LINE__, #condition); \
    std::_Exit(1); \
} } while (0)

static std::atomic<unsigned> ticks{0};
static SIM_config_type config{};
static bool failOpen;
static int openCalls, closeCalls;

extern "C" {
SNDSharedWork *SNDi_SharedWork = nullptr;
SIM_config_type *SIM_GetConfigPtr() { return &config; }
float SIM_Config_ValidateFastForwardMultiplier(float value) { return value; }
void SND_SeqMainMask(BOOL, u32) { ticks.fetch_add(1); }
void SND_ExChannelMainMask(BOOL, u32) {}
void SND_UpdateExChannel() {}

int __real_SDL_OpenAudio(SDL_AudioSpec *, SDL_AudioSpec *);
void __real_SDL_CloseAudio();
int __wrap_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    ++openCalls;
    if (failOpen) return SDL_SetError("intentional audio-open failure");
    return __real_SDL_OpenAudio(desired, obtained);
}
void __wrap_SDL_CloseAudio()
{
    ++closeCalls;
    __real_SDL_CloseAudio();
}
}

struct ExitProbe {
    ~ExitProbe()
    {
        CHECK(openCalls == 1);
        CHECK(closeCalls == (failOpen ? 0 : 1));
        CHECK(SDL_GetAudioStatus() == SDL_AUDIO_STOPPED);
        const auto stoppedTicks = ticks.load();
        SDL_Delay(20);
        CHECK(ticks.load() == stoppedTicks);
        CHECK(failOpen ? stoppedTicks == 0 : stoppedTicks > 0);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        std::puts("PASS: audio closes before static destruction; no callback after close");
    }
} exitProbe;

int main(int argc, char **argv)
{
    bool useExit = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "fail-open") == 0) failOpen = true;
        if (std::strcmp(argv[i], "exit") == 0) useExit = true;
    }
    config.masterVolume = 50;
    CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
    SIM_Audio_Init(44100);
    SIM_Audio_SetFastForwardMultiplier(5.0f);
    if (!failOpen) {
        for (int retry = 0; retry < 100 && ticks.load() == 0; ++retry) SDL_Delay(10);
        CHECK(ticks.load() > 0);
    }
    if (useExit) std::exit(0);
    return 0;
}
