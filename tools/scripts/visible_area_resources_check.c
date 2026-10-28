#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>
#include <nitro.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "field/field_system.h"
#include "generated/map_headers.h"
#include "overlay005/area_data.h"
#include "overlay005/map_prop.h"

#include "heap.h"
static uintptr_t base;
static int frame, scene = -1, age;
static const int scenes[][3] = {
    { MAP_HEADER_ROUTE_218, 120, 758 },
    { MAP_HEADER_JUBILIFE_CITY, 129, 758 },
    { MAP_HEADER_ROUTE_218, 120, 758 },
    { MAP_HEADER_JUBILIFE_CITY, 129, 758 },
    { MAP_HEADER_ETERNA_CITY, 318, 522 },
    { MAP_HEADER_ROUTE_218, 120, 758 },
    { MAP_HEADER_ROUTE_218_GATE_TO_JUBILIFE_CITY, 5, 5 },
    { MAP_HEADER_ROUTE_218, 120, 758 },
    { MAP_HEADER_JUBILIFE_CITY, 174, 752 }
};
static void crash(int sig)
{
    void *frames[40];
    int n = backtrace(frames, 40);
    fprintf(stderr, "CRASH base=%p scene=%d\n", (void *)base, scene);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "BACKTRACE %p %lx\n", frames[i], (unsigned long)((uintptr_t)frames[i] - base));
    }
    fflush(NULL);
    _exit(128 + sig);
}
__attribute__((constructor)) static void install(void)
{
    signal(SIGSEGV, crash);
    signal(SIGABRT, crash);
}
static int findbase(struct dl_phdr_info *i, size_t s, void *d)
{
    if (!i->dlpi_name[0]) {
        base = i->dlpi_addr;
    }
    return 0;
}
// Exercise cancellation before textures, after textures, partway through props,
// and after a complete load. Repetition also catches leaked texture/palette keys.
static void check_preview_cleanup(FieldSystem *field)
{
#ifdef CHECK_RESOURCES
    MapPropAnimationManager *(*newAnimations)(enum HeapID) = (void *)(base + OFF_NEW_ANIMATIONS);
    AreaDataManager *(*newArea)(int, MapPropAnimationManager *, const AreaDataManager *) = (void *)(base + OFF_NEW_AREA);
    AreaPreviewLoadResult (*step)(AreaDataManager *) = (void *)(base + OFF_AREA_STEP);
    void (*unload)(MapPropAnimationManager *) = (void *)(base + OFF_UNLOAD_ANIMATIONS);
    void (*freeAnimations)(MapPropAnimationManager *) = (void *)(base + OFF_FREE_ANIMATIONS);
    void (*freeArea)(AreaDataManager **) = (void *)(base + OFF_FREE_AREA);
    u32 (*freeBytes)(u32) = (void *)(base + OFF_HEAP_FREE);
    u32 gameplayBefore = freeBytes(HEAP_ID_FIELD1), before = freeBytes(HEAP_ID_FIELD_SCENERY);
    for (int repeat = 0; repeat < 8; repeat++) {
        const int limits[] = { 0, 1, 6, 256 };
        for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
            MapPropAnimationManager *animations = newAnimations(HEAP_ID_FIELD_SCENERY);
            AreaDataManager *area = newArea(6, animations, field->areaDataManager);
            AreaPreviewLoadResult result = AREA_PREVIEW_LOADING;
            for (int j = 0; j < limits[i] && result == AREA_PREVIEW_LOADING; j++) {
                result = step(area);
            }
            if (result == AREA_PREVIEW_LOAD_FAILED || (limits[i] == 256 && result != AREA_PREVIEW_READY)) {
                fprintf(stderr, "PREVIEW_LOAD_FAILED,%d,%u\n", repeat, i);
                _exit(9);
            }
            unload(animations);
            freeAnimations(animations);
            freeArea(&area);
            if (freeBytes(HEAP_ID_FIELD_SCENERY) != before || freeBytes(HEAP_ID_FIELD1) != gameplayBefore) {
                fprintf(stderr, "PREVIEW_HEAP_LEAK,%d,%u\n", repeat, i);
                _exit(10);
            }
        }
    }
    NNSGfdTexKey (*allocTexture)(u32, BOOL, u32) = (void *)(base + OFF_ALLOC_TEX);
    int (*freeTexture)(NNSGfdTexKey) = (void *)(base + OFF_FREE_TEX);
    NNSGfdPlttKey (*allocPalette)(u32, BOOL, u32) = (void *)(base + OFF_ALLOC_PALETTE);
    int (*freePalette)(NNSGfdPlttKey) = (void *)(base + OFF_FREE_PALETTE);
    int capacity[2] = { 0, 0 };
    for (int repeat = 0; repeat < 4; repeat++) {
        for (int palette = 0; palette < 2; palette++) {
            u32 blockers[256];
            int count = 0;
            while (count < 256) {
                u32 key = palette ? allocPalette(512, FALSE, 0) : allocTexture(65536, FALSE, 0);
                if (!key) {
                    break;
                }
                blockers[count++] = key;
            }
            if (count == 256 || (repeat && count != capacity[palette])) {
                fprintf(stderr, "PREVIEW_VRAM_LEAK,%d,%d\n", repeat, palette);
                _exit(11);
            }
            capacity[palette] = count;
            MapPropAnimationManager *animations = newAnimations(HEAP_ID_FIELD_SCENERY);
            AreaDataManager *area = newArea(6, animations, field->areaDataManager);
            if (step(area) != AREA_PREVIEW_LOAD_FAILED) {
                fprintf(stderr, "PREVIEW_EXPECTED_ALLOCATION_FAILURE,%d\n", palette);
                _exit(12);
            }
            unload(animations);
            freeAnimations(animations);
            freeArea(&area);
            for (int i = 0; i < count; i++) {
                if (palette) {
                    freePalette(blockers[i]);
                } else {
                    freeTexture(blockers[i]);
                }
            }
            if (freeBytes(HEAP_ID_FIELD_SCENERY) != before || freeBytes(HEAP_ID_FIELD1) != gameplayBefore) {
                fprintf(stderr, "PREVIEW_FAILURE_HEAP_LEAK\n");
                _exit(13);
            }
        }
    }
    fprintf(stdout, "CHECK_PREVIEW_CLEANUP,32\n");
    fprintf(stdout, "CHECK_PREVIEW_ALLOCATION_FAILURES,8\n");
#endif
}
__attribute__((noinline)) void visible_area_check(FieldSystem *field, int scene)
{
#ifdef CHECK_RESOURCES
    unsigned char *cache = *(void **)((char *)field->landDataMan + LAND_CACHE);
    if (!cache) {
        fprintf(stderr, "CACHE_FALLBACK\n");
        _exit(5);
    }
    BOOL (*bindTex)(NNSG3dResMdl *, NNSG3dResTex *) = (void *)(base + OFF_BINDTEX);
    BOOL (*bindPalette)(NNSG3dResMdl *, NNSG3dResTex *) = (void *)(base + OFF_BINDPALETTE);
    int foreign = 0, ready = 0;
    for (int i = 0; i < 64; i++) {
        unsigned char *chunk = cache + i * CHUNK_SIZE;
        if (*(int *)chunk < 0 || *(int *)(chunk + CHUNK_STAGE) != 5) {
            continue;
        }
        ready++;
        void *area = *(void **)(chunk + CHUNK_AREA);
        if (!area) {
            continue;
        }
        foreign++;
        AreaDataManager *resources = *(void **)((char *)area + AREA_RESOURCES);
        NNSG3dResMdl *model = *(void **)(chunk + CHUNK_MODEL);
        if (!bindTex(model, resources->mapTexture) || !bindPalette(model, resources->mapTexture)) {
            fprintf(stderr, "MISSING_TERRAIN,%d,%d\n", scene, *(int *)chunk);
            _exit(6);
        }
        MapPropManager *props = *(void **)(chunk + CHUNK_PROPS);
        if (props) {
            for (int p = 0; p < MAX_LOADED_MAP_PROPS; p++) {
                MapProp *prop = &props->loadedProps[p];
                if (prop->loaded && prop->modelID && (!bindTex(prop->model, resources->mapPropTexture) || !bindPalette(prop->model, resources->mapPropTexture))) {
                    fprintf(stderr, "MISSING_PROP,%d,%d,%d\n", scene, *(int *)chunk, prop->modelID);
                    _exit(7);
                }
            }
        }
        int refs = 0;
        for (int j = 0; j < 64; j++) {
            if (*(void **)(cache + j * CHUNK_SIZE + CHUNK_AREA) == area) {
                refs++;
            }
        }
        if (refs != *(int *)((char *)area + AREA_REFERENCES)) {
            fprintf(stderr, "BAD_REFS\n");
            _exit(8);
        }
    }
    fprintf(stdout, "CHECK_SCENE,%d,ready=%d,foreign=%d\n", scene, ready, foreign);
    fflush(stdout);
    if (scene == 0) {
        if (!foreign) {
            fprintf(stderr, "MISSING_NEIGHBOR_AREA\n");
            _exit(14);
        }
        check_preview_cleanup(field);
    }
#endif
}
static void capture(SDL_Window *w, int scene)
{
    int width, height;
    SDL_GL_GetDrawableSize(w, &width, &height);
    unsigned char *pixels = malloc(width * height * 3);
    void (*read)(int, int, int, int, unsigned, unsigned, void *) = SDL_GL_GetProcAddress("glReadPixels");
    read(0, 0, width, height, 0x1907, 0x1401, pixels);
    char path[128];
    snprintf(path, sizeof(path), "capture-%d.ppm", scene);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; y--) {
        fwrite(pixels + y * width * 3, 3, width, f);
    }
    fclose(f);
    free(pixels);
}
void SDL_GL_SwapWindow(SDL_Window *w)
{
    if (!base) {
        dl_iterate_phdr(findbase, NULL);
    }
    FieldSystem *(*fieldfn)(void) = (void *)(base + OFF_FIELD);
    BOOL (*busy)(FieldSystem *) = (void *)(base + OFF_BUSY);
    void (*warp)(int, int, int) = (void *)(base + OFF_WARP);
    void (*reset)(void) = (void *)(base + OFF_RESET), (*cycle)(void) = (void *)(base + OFF_CYCLE);
    FieldSystem *f = fieldfn();
    frame++;
    if (f && f->runningFieldMap && f->npcSceneRenderFrame > 120 && !busy(f)) {
        if (scene < 0 || ++age >= 150) {
            if (scene >= 0) {
                visible_area_check(f, scene);
                capture(w, scene);
                fprintf(stdout, "VISIBLE_SCENE,%d,%d\n", scene, f->location->mapHeaderID);
                fflush(stdout);
            }
            if (++scene >= sizeof(scenes) / sizeof(scenes[0])) {
                _exit(0);
            }
            reset();
            for (int i = 0; i < (scene % 2 ? 1 : 3); i++) {
                cycle();
            }
            warp(scenes[scene][0], scenes[scene][1], scenes[scene][2]);
            age = 0;
        }
    }
    if (scene < 0 && frame % 40 == 1) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = frame > 1200 ? SDLK_x : SDLK_z;
        SDL_PushEvent(&e);
    }
    if (scene < 0 && frame % 40 == 3) {
        SDL_Event e = { 0 };
        e.type = SDL_KEYUP;
        e.key.keysym.sym = frame > 1200 ? SDLK_x : SDLK_z;
        SDL_PushEvent(&e);
    }
    void (*swap)(SDL_Window *) = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    swap(w);
    if (frame > 10000) {
        _exit(4);
    }
}
