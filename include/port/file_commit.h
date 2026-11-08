#ifndef POKEPLATINUM_PORT_FILE_COMMIT_H
#define POKEPLATINUM_PORT_FILE_COMMIT_H

#include <stdio.h>

// Flushes a written file to disk. Returns nonzero on success.
int PC_FileSync(FILE *file);
// Atomically moves a finished file over its destination. Returns nonzero on success.
int PC_FileReplace(const char *from, const char *to);

#endif
