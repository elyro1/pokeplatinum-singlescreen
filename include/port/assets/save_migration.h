#ifndef POKEPLATINUM_PORT_SAVE_MIGRATION_H
#define POKEPLATINUM_PORT_SAVE_MIGRATION_H

// The port saves in the DS layout, so DS saves are used as they are. This
// converts saves written by older 64-bit builds, whose layout differed, keeping
// the untouched original beside them as saveN.bak, and pads short saves to the
// full 512 KiB flash size so every slot file also works on a DS or emulator.
void PC_MigrateSaves(void);

#endif
