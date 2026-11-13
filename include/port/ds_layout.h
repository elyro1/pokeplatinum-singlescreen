#ifndef POKEPLATINUM_PORT_DS_LAYOUT_H
#define POKEPLATINUM_PORT_DS_LAYOUT_H

// Needs u64/s64 from the includer, like the struct headers that use it (host
// tools include those without the NitroSDK).

// The DS compiler places 64-bit values on 4-byte boundaries inside structs,
// while 64-bit hosts use 8. Saved structs use these types for their 64-bit
// fields so the port keeps the DS save layout byte for byte. An aligned
// attribute on a typedef may lower the alignment on GCC and Clang.
#ifdef SDK_PORT
typedef u64 u64_ds __attribute__((aligned(4)));
typedef s64 s64_ds __attribute__((aligned(4)));
#else
typedef u64 u64_ds;
typedef s64 s64_ds;
#endif

#endif // POKEPLATINUM_PORT_DS_LAYOUT_H
