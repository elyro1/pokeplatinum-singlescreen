#ifndef POKEPLATINUM_PORT_CONFIG_REPORT_H
#define POKEPLATINUM_PORT_CONFIG_REPORT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Warns about PLATINUM_* environment variables that the game doesn't read, e.g. misspelled ones
void ConfigReport_CheckEnvironment(void);
// --print-config: the project's settings and the PLATINUM_* variables that are set
void ConfigReport_Print(FILE *out);

#ifdef __cplusplus
}
#endif

#endif
