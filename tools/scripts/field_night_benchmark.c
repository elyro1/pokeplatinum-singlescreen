#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <link.h>
#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "field/field_system.h"
#include "port/config/sim_config_prj.h"

// Benchmark preload: warps to NIGHT_MAP/NIGHT_X/NIGHT_Z, then runs each phase
// of NIGHT_MODES ("preview:mask,preview:mask,...") for 90 warm-up and 90
// sampled frames. preview is the number of time preview steps (1 night,
// 3 day); mask holds the graphics effects that are on, in the order below.
static uintptr_t base;
static int frame, age, ready, phase, phaseCount, draws;
static int previews[32], masks[32];
static int findbase(struct dl_phdr_info *i, size_t s, void *d)
{
    if (!i->dlpi_name[0])
        base = i->dlpi_addr;
    return 0;
}
static void (*realDraw)(unsigned, int, int);
static void draw(unsigned m, int f, int n)
{
    draws++;
    realDraw(m, f, n);
}
void *SDL_GL_GetProcAddress(const char *n)
{
    void *(*get)(const char *) = dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
    void *p = get(n);
    if (!strcmp(n, "glDrawArrays")) {
        realDraw = p;
        return draw;
    }
    return p;
}
static int envi(const char *name, int fallback)
{
    const char *value = getenv(name);
    return value && *value ? atoi(value) : fallback;
}
static void Apply(SIM_Config_prj_type *c, int mask)
{
    BOOL *effects[] = { &c->nightLighting, &c->nightGlows, &c->emissiveBloom, &c->softContactShadows,
        &c->castShadows, &c->waterEffects, &c->weatherEffects, &c->environmentalParticles };
    for (unsigned i = 0; i < sizeof(effects) / sizeof(effects[0]); i++)
        *effects[i] = (mask >> i) & 1;
}
void SDL_GL_SwapWindow(SDL_Window *w)
{
    if (!base) {
        dl_iterate_phdr(findbase, NULL);
        char modes[512];
        snprintf(modes, sizeof(modes), "%s", getenv("NIGHT_MODES"));
        for (char *mode = strtok(modes, ","); mode && phaseCount < 32; mode = strtok(NULL, ","))
            if (sscanf(mode, "%d:%d", &previews[phaseCount], &masks[phaseCount]) == 2)
                phaseCount++;
    }
    FieldSystem *(*fieldfn)(void) = (void *)(base + OFF_FIELD);
    BOOL (*busy)(FieldSystem *) = (void *)(base + OFF_BUSY);
    SIM_Config_prj_type *(*cfgfn)(void) = (void *)(base + OFF_CONFIG);
    void (*reset)(void) = (void *)(base + OFF_RESET), (*cycle)(void) = (void *)(base + OFF_CYCLE), (*warp)(int, int, int) = (void *)(base + OFF_WARP);
    FieldSystem *f = fieldfn();
    SIM_Config_prj_type *c = cfgfn();
    frame++;
    if (f && f->runningFieldMap && f->npcSceneRenderFrame > 120 && !busy(f)) {
        if (!ready) {
            ready = 1;
            warp(envi("NIGHT_MAP", 3), envi("NIGHT_X", 174), envi("NIGHT_Z", 752));
            age = -1;
        } else if (age >= 0) {
            age++;
            fprintf(stdout, age > 90 ? "NIGHT_SAMPLE,%d,%d,%d\n" : "NIGHT_WARM,%d,%d,%d\n", phase, frame - 1, draws);
            if (age == 180) {
                phase++;
                age = -1;
            }
        }
        if (age < 0) {
            if (phase >= phaseCount) {
                fflush(NULL);
                _exit(0);
            }
            reset();
            for (int i = 0; i < previews[phase]; i++)
                cycle();
            Apply(c, masks[phase]);
            age = 0;
        }
    }
    if (!ready && frame % 40 == 1) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = frame > 1200 ? SDLK_x : SDLK_z;
        SDL_PushEvent(&e);
    }
    if (!ready && frame % 40 == 3) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYUP;
        e.key.keysym.sym = frame > 1200 ? SDLK_x : SDLK_z;
        SDL_PushEvent(&e);
    }
    void (*swap)(SDL_Window *) = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    swap(w);
    void (*finish)(void) = SDL_GL_GetProcAddress("glFinish");
    finish();
    draws = 0;
    if (frame > 12000) {
        fflush(NULL);
        _exit(3);
    }
}
