#ifndef PORT_ROM_SETUP_H
#define PORT_ROM_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

// Search beside the executable, then in the working directory. Extract into
// the working directory, alongside existing game settings and saves.
int RomSetup_TryExtractPlatinum(void);
void RomSetup_AutoExtractBw2(void);

#ifdef __cplusplus
}
#endif

#endif
