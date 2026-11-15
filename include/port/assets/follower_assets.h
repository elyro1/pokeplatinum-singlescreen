#ifndef PORT_FOLLOWER_ASSETS_H
#define PORT_FOLLOWER_ASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

// The follower and overworld encounter textures, converted from the player's BW2 extraction
// (archives 47, 48 and 208) to HG/SS's eight-frame layout with HG/SS's shiny palettes.
int FollowerAssets_Ready(void);
int FollowerAssets_ReadTexture(unsigned offset, unsigned size, void *dest);
// Converts again on the next FollowerAssets_Ready, after a BW2 extraction
void FollowerAssets_Reload(void);

#ifdef __cplusplus
}
#endif
#endif
