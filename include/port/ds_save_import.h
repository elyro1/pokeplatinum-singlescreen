#ifndef POKEPLATINUM_PORT_DS_SAVE_IMPORT_H
#define POKEPLATINUM_PORT_DS_SAVE_IMPORT_H

// Converts any original DS save found in a save slot to the PC layout, keeping
// the untouched original beside it as saveN.bak. Other files are left alone.
void PC_ImportDsSaves(void);

#endif
