#ifndef PORT_HGSS_ASSETS_H
#define PORT_HGSS_ASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

int HgssAssets_Ready(void);
// Extracts the 566 follower textures, replacing the previous bundle on success.
const char *HgssAssets_Extract(const char *romPath);
int HgssAssets_ReadTexture(unsigned offset, unsigned size, void *dest);

#ifdef __cplusplus
}
#endif
#endif
