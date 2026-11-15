#ifndef GUI_CREATEMON_UTILS_H
#define GUI_CREATEMON_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: You will need to free the pointer returned by this!
const char * GUI_CreateMon_GetSpeciesName(int speciesNo);
int GUI_CreateMon_GenPokemon(int speciesNo, int level);

#ifdef __cplusplus
}
#endif


#endif