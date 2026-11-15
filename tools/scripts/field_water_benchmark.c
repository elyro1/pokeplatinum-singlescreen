#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <link.h>
#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "field/field_system.h"
#include "port/config/sim_config_prj.h"
static uintptr_t executableBase;
static int FindExecutable(struct dl_phdr_info *info, size_t size, void *data)
{
    if (!info->dlpi_name[0])
        executableBase = info->dlpi_addr;
    return 0;
}
void SDL_GL_SwapWindow(SDL_Window *window)
{
    static int frame, warped, age;
    if (!executableBase)
        dl_iterate_phdr(FindExecutable, NULL);
    FieldSystem *(*getField)(void) = (void *)(executableBase + FIELD_ADDRESS);
    BOOL (*busy)(FieldSystem *) = (void *)(executableBase + TASK_ADDRESS);
    void (*warp)(int, int, int) = (void *)(executableBase + WARP_ADDRESS);
    SIM_Config_prj_type *(*getConfig)(void) = (void *)(executableBase + CONFIG_ADDRESS);
    FieldSystem *field = getField();
    SIM_Config_prj_type *config = getConfig();
    config->enhancedDayNight = FALSE;
    config->regionalAtmosphere = FALSE;
    config->environmentalParticles = FALSE;
    if (HAS_METRICS) {
        config->nightLighting = FALSE;
        config->nightGlows = FALSE;
    }
    frame++;
    if (frame == 1) {
        const unsigned char *(*getString)(unsigned) = SDL_GL_GetProcAddress("glGetString");
        fprintf(stdout, "WATER_GPU,%s\n", getString(0x1f01));
        if (HAS_METRICS) {
            void (*profile)(BOOL) = (void *)(executableBase + PROFILE_ADDRESS);
            profile(TRUE);
        }
    }
    if (!warped && field && field->runningFieldMap && field->npcSceneRenderFrame > 120 && !busy(field)) {
        config->waterEffects = FALSE;
        int shore = atoi(getenv("WATER_SHORE") ? getenv("WATER_SHORE") : "0");
        warp(312, shore ? 46 : 40, shore ? 54 : 36);
        warped = 1;
    } else if (warped && field && field->location->mapHeaderID == 312 && field->runningFieldMap && field->npcSceneRenderFrame > 120 && !busy(field)) {
        age++;
        BOOL on = ((age - 1) / 1300) % 2 != 0;
        config->waterEffects = on;
        if ((age - 1) % 1300 >= 1000 && age <= 5200) {
            float cpu = -1, gpu = -1;
            int tiles = -1;
            if (HAS_METRICS) {
                float (*getCpu)(void) = (void *)(executableBase + CPU_ADDRESS);
                float (*getGpu)(void) = (void *)(executableBase + GPU_ADDRESS);
                int (*getTiles)(void) = (void *)(executableBase + TILES_ADDRESS);
                cpu = getCpu();
                gpu = on ? getGpu() : 0;
                tiles = getTiles();
            }
            fprintf(stdout, "WATER_SAMPLE,%s,%d,%.6f,%.6f,%d\n", on ? "on" : "off", frame - 1, cpu, gpu, tiles);
        }
        if (age >= 5200) {
            fflush(NULL);
            _exit(0);
        }
    }
    if (!warped && frame % 40 == 1) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = SDLK_z;
        SDL_PushEvent(&e);
    }
    if (!warped && frame % 40 == 3) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYUP;
        e.key.keysym.sym = SDLK_z;
        SDL_PushEvent(&e);
    }
    void (*swap)(SDL_Window *) = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    swap(window);
    // EGL pbuffer swaps may not submit commands; flush without waiting.
    void (*flush)(void) = SDL_GL_GetProcAddress("glFlush");
    flush();
    if (frame > 9000) {
        fflush(NULL);
        _exit(3);
    }
}
