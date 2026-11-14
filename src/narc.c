#include "narc.h"

#include <nitro.h>
#include <string.h>

#include "heap.h"
#include "pc_frame_profile.h"
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
#include <stdio.h>
#include <stdlib.h>

#include "bwbattle/bwbattle.h"
#endif

// Mapping of NARC indices to filesystem paths.
static const char *sNarcPaths[] = {
    [NARC_INDEX_BATTLE__SKILL__WAZA_SEQ] = "battle/skill/waza_seq.narc",
    [NARC_INDEX_BATTLE__SKILL__SUB_SEQ] = "battle/skill/sub_seq.narc",
    [NARC_INDEX_POKETOOL__PERSONAL__PL_PERSONAL] = "poketool/personal/pl_personal.narc",
    [NARC_INDEX_POKETOOL__PERSONAL__PL_GROWTBL] = "poketool/personal/pl_growtbl.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__PL_POKEGRA] = "poketool/pokegra/pl_pokegra.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__HEIGHT] = "poketool/pokegra/height.narc",
    [NARC_INDEX_POKETOOL__TRGRA__TRBGRA] = "poketool/trgra/trbgra.narc",
    [NARC_INDEX_BATTLE__GRAPHIC__PL_BATT_BG] = "battle/graphic/pl_batt_bg.narc",
    [NARC_INDEX_WAZAEFFECT__WE] = "wazaeffect/we.arc",
    [NARC_INDEX_POKETOOL__WAZA__PL_WAZA_TBL] = "poketool/waza/pl_waza_tbl.narc",
    [NARC_INDEX_FIELDDATA__SCRIPT__SCR_SEQ] = "fielddata/script/scr_seq.narc",
    [NARC_INDEX_MSGDATA__SCENARIO__SCR_MSG] = "msgdata/scenario/scr_msg.narc",
    [NARC_INDEX_GRAPHIC__POKETCH] = "graphic/poketch.narc",
    [NARC_INDEX_GRAPHIC__PL_BAG_GRA] = "graphic/pl_bag_gra.narc",
    [NARC_INDEX_GRAPHIC__PL_FONT] = "graphic/pl_font.narc",
    [NARC_INDEX_ITEMTOOL__ITEMDATA__PL_ITEM_DATA] = "itemtool/itemdata/pl_item_data.narc",
    [NARC_INDEX_ITEMTOOL__ITEMDATA__ITEM_ICON] = "itemtool/itemdata/item_icon.narc",
    [NARC_INDEX_GRAPHIC__TMAP_GRA] = "graphic/tmap_gra.narc",
    [NARC_INDEX_GRAPHIC__BOX] = "graphic/box.narc",
    [NARC_INDEX_POKETOOL__ICONGRA__PL_POKE_ICON] = "poketool/icongra/pl_poke_icon.narc",
    [NARC_INDEX_GRAPHIC__PL_PLIST_GRA] = "graphic/pl_plist_gra.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTCLACT__WECHAR] = "wazaeffect/effectclact/wechar.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTCLACT__WEPLTT] = "wazaeffect/effectclact/wepltt.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTCLACT__WECELL] = "wazaeffect/effectclact/wecell.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTCLACT__WECELLANM] = "wazaeffect/effectclact/wecellanm.narc",
    [NARC_INDEX_GRAPHIC__IMAGECLIP] = "graphic/imageclip.narc",
    [NARC_INDEX_MSGDATA__PL_MSG] = "msgdata/pl_msg.narc",
    [NARC_INDEX_BATTLE__GRAPHIC__PL_BATT_OBJ] = "battle/graphic/pl_batt_obj.narc",
    [NARC_INDEX_DATA__UNDERG_RADAR] = "data/underg_radar.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTDATA__WAZA_PARTICLE] = "wazaeffect/effectdata/waza_particle.narc",
    [NARC_INDEX_BATTLE__SKILL__BE_SEQ] = "battle/skill/be_seq.narc",
    [NARC_INDEX_DATA__NAMEIN] = "data/namein.narc",
    [NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT] = "fielddata/eventdata/zone_event.narc",
    [NARC_INDEX_POKETOOL__PERSONAL__WOTBL] = "poketool/personal/wotbl.narc",
    [NARC_INDEX_POKETOOL__PERSONAL__EVO] = "poketool/personal/evo.narc",
    [NARC_INDEX_GRAPHIC__FONTOAM] = "graphic/fontoam.narc",
    [NARC_INDEX_GRAPHIC__FIELD_BOARD] = "graphic/field_board.narc",
    [NARC_INDEX_FIELDDATA__ENCOUNTDATA__D_ENC_DATA] = "fielddata/encountdata/d_enc_data.narc",
    [NARC_INDEX_GRAPHIC__PL_WINFRAME] = "graphic/pl_winframe.narc",
    [NARC_INDEX_GRAPHIC__PL_PST_GRA] = "graphic/pl_pst_gra.narc",
    [NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL] = "fielddata/build_model/build_model.narc",
    [NARC_INDEX_FIELDDATA__MAPMATRIX__MAP_MATRIX] = "fielddata/mapmatrix/map_matrix.narc",
    [NARC_INDEX_FIELDDATA__AREADATA__AREA_DATA] = "fielddata/areadata/area_data.narc",
    [NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREA_BUILD] = "fielddata/areadata/area_build_model/area_build.narc",
    [NARC_INDEX_FIELDDATA__AREADATA__AREA_MAP_TEX__MAP_TEX_SET] = "fielddata/areadata/area_map_tex/map_tex_set.narc",
    [NARC_INDEX_CONTEST__GRAPHIC__CONTEST_BG] = "contest/graphic/contest_bg.narc",
    [NARC_INDEX_CONTEST__GRAPHIC__CONTEST_OBJ] = "contest/graphic/contest_obj.narc",
    [NARC_INDEX_DATA__UGEFFECT_OBJ_GRAPHIC] = "data/ugeffect_obj_graphic.narc",
    [NARC_INDEX_DEMO__TITLE__TITLEDEMO] = "demo/title/titledemo.narc",
    [NARC_INDEX_FIELDDATA__AREADATA__AREA_MOVE_MODEL__MOVE_MODEL_LIST] = "fielddata/areadata/area_move_model/move_model_list.narc",
    [NARC_INDEX_DATA__UG_TRAP] = "data/ug_trap.narc",
    [NARC_INDEX_GRAPHIC__TRAINER_CASE] = "graphic/trainer_case.narc",
    [NARC_INDEX_DATA__UG_FOSSIL] = "data/ug_fossil.narc",
    [NARC_INDEX_GRAPHIC__NTAG_GRA] = "graphic/ntag_gra.narc",
    [NARC_INDEX_DATA__TRADELIST] = "data/tradelist.narc",
    [NARC_INDEX_DATA__UG_PARTS] = "data/ug_parts.narc",
    [NARC_INDEX_GRAPHIC__OPENING] = "graphic/opening.narc",
    [NARC_INDEX_POKETOOL__TRAINER__TRDATA] = "poketool/trainer/trdata.narc",
    [NARC_INDEX_POKETOOL__TRAINER__TRPOKE] = "poketool/trainer/trpoke.narc",
    [NARC_INDEX_POKETOOL__TRMSG__TRTBL] = "poketool/trmsg/trtbl.narc",
    [NARC_INDEX_POKETOOL__TRGRA__TRFGRA] = "poketool/trgra/trfgra.narc",
    [NARC_INDEX_PARTICLEDATA__PARTICLEDATA] = "particledata/particledata.narc",
    [NARC_INDEX_GRAPHIC__SHOP_GRA] = "graphic/shop_gra.narc",
    [NARC_INDEX_WAZAEFFECT__WE_SUB] = "wazaeffect/we_sub.narc",
    [NARC_INDEX_DATA__UG_ANIM] = "data/ug_anim.narc",
    [NARC_INDEX_DATA__WEATHER_SYS] = "data/weather_sys.narc",
    [NARC_INDEX_GRAPHIC__MENU_GRA] = "graphic/menu_gra.narc",
    [NARC_INDEX_FIELDDATA__LAND_DATA__LAND_DATA] = "fielddata/land_data/land_data.narc",
    [NARC_INDEX_ITEMTOOL__ITEMDATA__NUTS_DATA] = "itemtool/itemdata/nuts_data.narc",
    [NARC_INDEX_RESOURCE__ENG__ZUKAN__ZUKAN] = "resource/eng/zukan/zukan.narc",
    [NARC_INDEX_POKETOOL__POKEFOOT__POKEFOOT] = "poketool/pokefoot/pokefoot.narc",
    [NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET] = "fielddata/areadata/area_build_model/areabm_texset.narc",
    [NARC_INDEX_BATTLE__GRAPHIC__PL_B_PLIST_GRA] = "battle/graphic/pl_b_plist_gra.narc",
    [NARC_INDEX_GRAPHIC__CONFIG_GRA] = "graphic/config_gra.narc",
    [NARC_INDEX_GRAPHIC__UNIONROOM] = "graphic/unionroom.narc",
    [NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_DATA] = "application/zukanlist/zkn_data/zukan_data.narc",
    [NARC_INDEX_GRAPHIC__PMSI] = "graphic/pmsi.narc",
    [NARC_INDEX_BATTLE__GRAPHIC__B_BAG_GRA] = "battle/graphic/b_bag_gra.narc",
    [NARC_INDEX_GRAPHIC__OEKAKI] = "graphic/oekaki.narc",
    [NARC_INDEX_GRAPHIC__MAIL_GRA] = "graphic/mail_gra.narc",
    [NARC_INDEX_GRAPHIC__F_NOTE_GRA] = "graphic/f_note_gra.narc",
    [NARC_INDEX_DATA__MMODEL__MMODEL] = "data/mmodel/mmodel.narc",
    [NARC_INDEX_GRAPHIC__EV_POKESELECT] = "graphic/ev_pokeselect.narc",
    [NARC_INDEX_GRAPHIC__PORU_GRA] = "graphic/poru_gra.narc",
    [NARC_INDEX_GRAPHIC__TOUCH_SUBWINDOW] = "graphic/touch_subwindow.narc",
    [NARC_INDEX_GRAPHIC__FLD_COMACT] = "graphic/fld_comact.narc",
    [NARC_INDEX_GRAPHIC__RECORD] = "graphic/record.narc",
    [NARC_INDEX_GRAPHIC__PORUDEMO] = "graphic/porudemo.narc",
    [NARC_INDEX_GRAPHIC__PORUACT] = "graphic/poruact.narc",
    [NARC_INDEX_GRAPHIC__PL_WIFINOTE] = "graphic/pl_wifinote.narc",
    [NARC_INDEX_CONTEST__DATA__CONTEST_DATA] = "contest/data/contest_data.narc",
    [NARC_INDEX_APPLICATION__CUSTOM_BALL__DATA__CB_DATA] = "application/custom_ball/data/cb_data.narc",
    [NARC_INDEX_DATA__WIFIP2PMATCH] = "data/wifip2pmatch.narc",
    [NARC_INDEX_GRAPHIC__DEMO_TRADE] = "graphic/demo_trade.narc",
    [NARC_INDEX_POKEANIME__PL_POKE_ANM] = "pokeanime/pl_poke_anm.narc",
    [NARC_INDEX_GRAPHIC__MYSIGN] = "graphic/mysign.narc",
    [NARC_INDEX_FIELDDATA__MM_LIST__MOVE_MODEL_LIST] = "fielddata/mm_list/move_model_list.narc",
    [NARC_INDEX_DATA__FIELD_CUTIN] = "data/field_cutin.narc",
    [NARC_INDEX_DATA__DEMO_CLIMAX] = "data/demo_climax.narc",
    [NARC_INDEX_WAZAEFFECT__EFFECTDATA__BALL_PARTICLE] = "wazaeffect/effectdata/ball_particle.narc",
    [NARC_INDEX_WAZAEFFECT__PT_DEBUG__DEBUG_PARTICLE] = "wazaeffect/pt_debug/debug_particle.narc",
    [NARC_INDEX_GRAPHIC__DENDOU_PC] = "graphic/dendou_pc.narc",
    [NARC_INDEX_APPLICATION__WIFI_EARTH__WIFI_EARTH_PLACE] = "application/wifi_earth/wifi_earth_place.narc",
    [NARC_INDEX_BATTLE__TR_AI__TR_AI_SEQ] = "battle/tr_ai/tr_ai_seq.narc",
    [NARC_INDEX_GRAPHIC__WORLDTRADE] = "graphic/worldtrade.narc",
    [NARC_INDEX_GRAPHIC__DENDOU_DEMO] = "graphic/dendou_demo.narc",
    [NARC_INDEX_ARC__ENCDATA_EX] = "arc/encdata_ex.narc",
    [NARC_INDEX_DATA__MMODEL__FLDEFF] = "data/mmodel/fldeff.narc",
    [NARC_INDEX_BATTLE__GRAPHIC__VS_DEMO_GRA] = "battle/graphic/vs_demo_gra.narc",
    [NARC_INDEX_POKETOOL__PL_POKEZUKAN] = "poketool/pl_pokezukan.narc",
    [NARC_INDEX_ARC__BM_ANIME] = "arc/bm_anime.narc",
    [NARC_INDEX_ARC__BM_ANIME_LIST] = "arc/bm_anime_list.narc",
    [NARC_INDEX_GRAPHIC__FIELD_ENCOUNTEFFECT] = "graphic/field_encounteffect.narc",
    [NARC_INDEX_GRAPHIC__WAZA_OSHIE_GRA] = "graphic/waza_oshie_gra.narc",
    [NARC_INDEX_POKETOOL__POKEANM__PL_POKEANM] = "poketool/pokeanm/pl_pokeanm.narc",
    [NARC_INDEX_FIELDDATA__POKEMON_TRADE__FLD_TRADE] = "fielddata/pokemon_trade/fld_trade.narc",
    [NARC_INDEX_GRAPHIC__MYSTERY] = "graphic/mystery.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__PL_OTHERPOKE] = "poketool/pokegra/pl_otherpoke.narc",
    [NARC_INDEX_DEMO__EGG__DATA__EGG_DATA] = "demo/egg/data/egg_data.narc",
    [NARC_INDEX_DEMO__EGG__DATA__PARTICLE__EGG_DEMO_PARTICLE] = "demo/egg/data/particle/egg_demo_particle.narc",
    [NARC_INDEX_ARC__SHIP_DEMO] = "arc/ship_demo.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__HEIGHT_O] = "poketool/pokegra/height_o.narc",
    [NARC_INDEX_GRAPHIC__NUTMIXER] = "graphic/nutmixer.narc",
    [NARC_INDEX_GRAPHIC__BTOWER] = "graphic/btower.narc",
    [NARC_INDEX_DEMO__SHINKA__DATA__PARTICLE__SHINKA_DEMO_PARTICLE] = "demo/shinka/data/particle/shinka_demo_particle.narc",
    [NARC_INDEX_DEMO__INTRO__INTRO] = "demo/intro/intro.narc",
    [NARC_INDEX_ARC__TV] = "arc/tv.narc",
    [NARC_INDEX_GRAPHIC__ENDING] = "graphic/ending.narc",
    [NARC_INDEX_DEMO__TITLE__OP_DEMO] = "demo/title/op_demo.narc",
    [NARC_INDEX_APPLICATION__WIFI_EARTH__WIFI_EARTH] = "application/wifi_earth/wifi_earth.narc",
    [NARC_INDEX_GRAPHIC__RANKING] = "graphic/ranking.narc",
    [NARC_INDEX_ARC__PPARK] = "arc/ppark.narc",
    [NARC_INDEX_DEMO__SYOUJYOU__SYOUJYOU] = "demo/syoujyou/syoujyou.narc",
    [NARC_INDEX_DATA__SLOT] = "data/slot.narc",
    [NARC_INDEX_BATTLE__B_TOWER__BTDTR] = "battle/b_tower/btdtr.narc",
    [NARC_INDEX_BATTLE__B_TOWER__BTDPM] = "battle/b_tower/btdpm.narc",
    [NARC_INDEX_DEMO__INTRO__INTRO_TV] = "demo/intro/intro_tv.narc",
    [NARC_INDEX_POKETOOL__TRMSG__TRTBLOFS] = "poketool/trmsg/trtblofs.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__POKE_YOFS] = "poketool/pokegra/poke_yofs.narc",
    [NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_ENC_DIAMOND] = "application/zukanlist/zkn_data/zukan_enc_diamond.narc",
    [NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_ENC_PEARL] = "application/zukanlist/zkn_data/zukan_enc_pearl.narc",
    [NARC_INDEX_GRAPHIC__HIDEN_EFFECT] = "graphic/hiden_effect.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__POKE_SHADOW] = "poketool/pokegra/poke_shadow.narc",
    [NARC_INDEX_FIELDDATA__ENCOUNTDATA__P_ENC_DATA] = "fielddata/encountdata/p_enc_data.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__POKE_SHADOW_OFX] = "poketool/pokegra/poke_shadow_ofx.narc",
    [NARC_INDEX_POKETOOL__SHINZUKAN] = "poketool/shinzukan.narc",
    [NARC_INDEX_GRAPHIC__WIFI2DCHAR] = "graphic/wifi2dchar.narc",
    [NARC_INDEX_POKETOOL__POKE_EDIT__PL_POKE_DATA] = "poketool/poke_edit/pl_poke_data.narc",
    [NARC_INDEX_DEBUG__CB_EDIT__D_TEST] = "debug/cb_edit/d_test.narc",
    [NARC_INDEX_FRONTIER__SCRIPT__FR_SCRIPT] = "frontier/script/fr_script.narc",
    [NARC_INDEX_FRONTIER_BACKGROUNDS] = "resource/eng/frontier_graphic/frontier_bg.narc",
    [NARC_INDEX_RESOURCE__ENG__FRONTIER_GRAPHIC__FRONTIER_OBJ] = "resource/eng/frontier_graphic/frontier_obj.narc",
    [NARC_INDEX_APPLICATION__CUSTOM_BALL__EDIT__PL_CB_DATA] = "application/custom_ball/edit/pl_cb_data.narc",
    [NARC_INDEX_GRAPHIC__WIFI_UNIONOBJ] = "graphic/wifi_unionobj.narc",
    [NARC_INDEX_DATA__GURU2] = "data/guru2.narc",
    [NARC_INDEX_DATA__FLDTANIME] = "data/fldtanime.narc",
    [NARC_INDEX_ARC__SHIP_DEMO_PL] = "arc/ship_demo_pl.narc",
    [NARC_INDEX_PARTICLEDATA__PL_FRONTIER__FRONTIER_PARTICLE] = "particledata/pl_frontier/frontier_particle.narc",
    [NARC_INDEX_GRAPHIC__BUCKET] = "graphic/bucket.narc",
    [NARC_INDEX_ARC__CODEIN_GRA] = "arc/codein_gra.narc",
    [NARC_INDEX_ARC__AREA_WIN_GRA] = "arc/area_win_gra.narc",
    [NARC_INDEX_FIELDDATA__ENCOUNTDATA__PL_ENC_DATA] = "fielddata/encountdata/pl_enc_data.narc",
    [NARC_INDEX_RESOURCE__ENG__BATT_REC__BATT_REC_GRA] = "resource/eng/batt_rec/batt_rec_gra.narc",
    [NARC_INDEX_GRAPHIC__WORLDTIMER] = "graphic/worldtimer.narc",
    [NARC_INDEX_ARC__MANENE] = "arc/manene.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__POKEGRA] = "poketool/pokegra/pokegra.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__OTHERPOKE] = "poketool/pokegra/otherpoke.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__DP_HEIGHT] = "poketool/pokegra/dp_height.narc",
    [NARC_INDEX_POKETOOL__POKEGRA__DP_HEIGHT_O] = "poketool/pokegra/dp_height_o.narc",
    [NARC_INDEX_GRAPHIC__LOBBY_NEWS] = "graphic/lobby_news.narc",
    [NARC_INDEX_ARC__EMAIL_GRA] = "arc/email_gra.narc",
    [NARC_INDEX_RESOURCE__ENG__WIFI_LOBBY_MINIGAME__WLMNGM_TOOL] = "resource/eng/wifi_lobby_minigame/wlmngm_tool.narc",
    [NARC_INDEX_ARC__DEMO_TENGAN_GRA] = "arc/demo_tengan_gra.narc",
    [NARC_INDEX_APPLICATION__BALLOON__GRAPHIC__BALLOON_GRA] = "application/balloon/graphic/balloon_gra.narc",
    [NARC_INDEX_APPLICATION__WIFI_LOBBY__MAP_CONV__WFLBY_MAP] = "application/wifi_lobby/map_conv/wflby_map.narc",
    [NARC_INDEX_GRAPHIC__WIFI_LOBBY] = "graphic/wifi_lobby.narc",
    [NARC_INDEX_DATA__AREALIGHT] = "data/arealight.narc",
    [NARC_INDEX_DATA__UGROUNDEFFECT] = "data/ugroundeffect.narc",
    [NARC_INDEX_BATTLE__B_PL_TOWER__PL_BTDTR] = "battle/b_pl_tower/pl_btdtr.narc",
    [NARC_INDEX_BATTLE__B_PL_TOWER__PL_BTDPM] = "battle/b_pl_tower/pl_btdpm.narc",
    [NARC_INDEX_BATTLE__B_PL_STAGE__PL_BSDPM] = "battle/b_pl_stage/pl_bsdpm.narc",
    [NARC_INDEX_FIELDDATA__TORNWORLD__TW_ARC] = "fielddata/tornworld/tw_arc.narc",
    [NARC_INDEX_FIELDDATA__TORNWORLD__TW_ARC_ATTR] = "fielddata/tornworld/tw_arc_attr.narc",
    [NARC_INDEX_DATA__TW_ARC_ETC] = "data/tw_arc_etc.narc",
    [NARC_INDEX_SCRATCH_OFF_CARDS] = "resource/eng/scratch/scratch.narc",
    [NARC_INDEX_PARTICLEDATA__PL_POKELIST__POKELIST_PARTICLE] = "particledata/pl_pokelist/pokelist_particle.narc",
    [NARC_INDEX_GRAPHIC__WIFI_LOBBY_OTHER] = "graphic/wifi_lobby_other.narc",
    [NARC_INDEX_GRAPHIC__FOOTPRINT_BOARD] = "graphic/footprint_board.narc",
    [NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_ENC_PLATINUM] = "application/zukanlist/zkn_data/zukan_enc_platinum.narc",
    [NARC_INDEX_APPLICATION__BUCKET__BALLSLOW_DATA] = "application/bucket/ballslow_data.narc",
    [NARC_INDEX_PARTICLEDATA__PL_ETC__PL_ETC_PARTICLE] = "particledata/pl_etc/pl_etc_particle.narc",
    [NARC_INDEX_RESOURCE__ENG__PMS_AIKOTOBA__PMS_AIKOTOBA] = "resource/eng/pms_aikotoba/pms_aikotoba.narc",
    [NARC_INDEX_GRAPHIC__UNIONOBJ2D_ONLYFRONT] = "graphic/unionobj2d_onlyfront.narc",
    [NARC_INDEX_GRAPHIC__LIBRARY_TV] = "graphic/library_tv.narc",
    [NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_DATA_GIRA] = "application/zukanlist/zkn_data/zukan_data_gira.narc",
#ifdef SDK_PORT
    [NARC_INDEX_BW2_ZUKAN_GRA] = "bw2/a/1/5/7",
#endif
};

static s32 NARC_ReadDisk(FSFile *file, void *dest, s32 size)
{
    s32 read = FS_ReadFile(file, dest, size);
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (read > 0) {
        SIM_ProfileAdd(SIM_PROFILE_NARC_DISK_BYTES, read);
    }
#endif
    return read;
}

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
// Immutable process-lifetime archives. Model binding and event patches happen
// only in caller-owned copies. Each NARC handle has an independent cursor.
typedef struct {
    u8 *bytes;
    u32 size, fat, image, imageEnd, refs;
    u16 count;
} ResidentNARC;
static ResidentNARC sResidentNARCs[NELEMS(sNarcPaths)];
static u32 sResidentBytes;
static const enum NarcID sFieldNARCs[] = {
    NARC_INDEX_FIELDDATA__LAND_DATA__LAND_DATA,
    NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT,
    NARC_INDEX_FIELDDATA__MAPMATRIX__MAP_MATRIX,
    NARC_INDEX_FIELDDATA__AREADATA__AREA_DATA,
    NARC_INDEX_FIELDDATA__BUILD_MODEL__BUILD_MODEL,
    NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREA_BUILD,
    NARC_INDEX_FIELDDATA__AREADATA__AREA_MAP_TEX__MAP_TEX_SET,
    NARC_INDEX_FIELDDATA__AREADATA__AREA_BUILD_MODEL__AREABM_TEXSET,
    NARC_INDEX_DATA__MMODEL__MMODEL,
    NARC_INDEX_DATA__MMODEL__FLDEFF,
    NARC_INDEX_ARC__BM_ANIME,
    NARC_INDEX_ARC__BM_ANIME_LIST,
    NARC_INDEX_FIELDDATA__TORNWORLD__TW_ARC,
    NARC_INDEX_FIELDDATA__TORNWORLD__TW_ARC_ATTR,
    NARC_INDEX_DATA__TW_ARC_ETC,
    NARC_INDEX_FIELDDATA__SCRIPT__SCR_SEQ,
    NARC_INDEX_FIELDDATA__ENCOUNTDATA__PL_ENC_DATA,
    NARC_INDEX_MSGDATA__SCENARIO__SCR_MSG,
    // Step processing queries the Vs. Seeker and other bag item parameters.
    NARC_INDEX_ITEMTOOL__ITEMDATA__PL_ITEM_DATA,
    // Zone lighting and the map-name popup otherwise reopen files at borders.
    NARC_INDEX_DATA__AREALIGHT,
    NARC_INDEX_MSGDATA__PL_MSG,
    NARC_INDEX_ARC__AREA_WIN_GRA,
    NARC_INDEX_GRAPHIC__PL_FONT,
};
u32 NARC_PCWorkingBudget(void)
{
    const char *value = getenv("PLATINUM_CACHE_MB");
    unsigned long mb = value ? strtoul(value, NULL, 10) : 256;
    if (mb < 128 || mb > 1024) {
        mb = 256;
    }
    return mb * 1024 * 1024;
}
static u32 ResidentRead32(const u8 *p)
{
    return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static u16 ResidentRead16(const u8 *p)
{
    return p[0] | p[1] << 8;
}
static BOOL ResidentValidate(ResidentNARC *r)
{
    if (r->size < 16 || memcmp(r->bytes, "NARC", 4) || ResidentRead32(r->bytes + 8) != r->size) {
        return FALSE;
    }
    r->fat = ResidentRead16(r->bytes + 12);
    if (r->fat > r->size - 12 || memcmp(r->bytes + r->fat, "BTAF", 4)) {
        return FALSE;
    }
    u32 fatSize = ResidentRead32(r->bytes + r->fat + 4);
    r->count = ResidentRead16(r->bytes + r->fat + 8);
    if (fatSize < 12 + r->count * 8 || fatSize > r->size - r->fat) {
        return FALSE;
    }
    u32 fnt = r->fat + fatSize;
    if (fnt > r->size - 8 || memcmp(r->bytes + fnt, "BTNF", 4)) {
        return FALSE;
    }
    u32 fntSize = ResidentRead32(r->bytes + fnt + 4);
    if (fntSize < 8 || fntSize > r->size - fnt) {
        return FALSE;
    }
    r->image = fnt + fntSize;
    if (r->image > r->size - 8 || memcmp(r->bytes + r->image, "GMIF", 4)) {
        return FALSE;
    }
    u32 imageSize = ResidentRead32(r->bytes + r->image + 4);
    if (imageSize < 8 || imageSize > r->size - r->image) {
        return FALSE;
    }
    r->imageEnd = r->image + imageSize;
    for (u32 i = 0; i < r->count; i++) {
        u32 start = ResidentRead32(r->bytes + r->fat + 12 + i * 8);
        u32 end = ResidentRead32(r->bytes + r->fat + 16 + i * 8);
        if (end < start || end > imageSize - 8) {
            return FALSE;
        }
    }
    return TRUE;
}
static ResidentNARC *ResidentGet(enum NarcID id)
{
    const char *disabled = getenv("PLATINUM_LEGACY_LOADING");
    if (disabled && *disabled == '1') {
        return NULL;
    }
    BOOL selected = FALSE;
    for (u32 i = 0; i < NELEMS(sFieldNARCs); i++) {
        if (sFieldNARCs[i] == id) {
            selected = TRUE;
        }
    }
    if (!selected) {
        return NULL;
    }
    ResidentNARC *r = &sResidentNARCs[id];
    if (r->bytes) {
        return r;
    }
    FSFile file;
    FS_InitFile(&file);
    if (!FS_OpenFile(&file, sNarcPaths[id])) {
        fprintf(stderr, "Missing field archive: %s\n", sNarcPaths[id]);
        OS_Terminate();
        return NULL;
    }
    u32 size = FS_GetLength(&file);
    if (size < 16) {
        FS_CloseFile(&file);
        fprintf(stderr, "Truncated field archive: %s\n", sNarcPaths[id]);
        OS_Terminate();
        return NULL;
    }
    // Reserve three quarters of the configurable CPU budget for prepared data.
    if (size > NARC_PCWorkingBudget() / 4 - sResidentBytes) {
        FS_CloseFile(&file);
        return NULL;
    }
    u8 *bytes = malloc(size);
    if (!bytes) {
        FS_CloseFile(&file);
        return NULL;
    }
    s32 read = NARC_ReadDisk(&file, bytes, size);
    FS_CloseFile(&file);
    r->bytes = bytes;
    r->size = size;
    if (read != size || !ResidentValidate(r)) {
        free(bytes);
        memset(r, 0, sizeof(*r));
        // The disk parser assumes trusted offsets too; a corrupt archive is
        // an asset error, not a reason to retry with unchecked disk offsets.
        fprintf(stderr, "Invalid or truncated field archive: %s\n", sNarcPaths[id]);
        OS_Terminate();
        return NULL;
    }
    sResidentBytes += size;
    return r;
}
static ResidentNARC *ResidentByPath(const char *path)
{
    for (u32 i = 0; i < NELEMS(sFieldNARCs); i++) {
        enum NarcID id = sFieldNARCs[i];
        if (path == sNarcPaths[id] || !strcmp(path, sNarcPaths[id])) {
            return ResidentGet(id);
        }
    }
    return NULL;
}
static u32 ResidentSpan(const ResidentNARC *r, u32 member, u32 *start)
{
    GF_ASSERT(member < r->count);
    if (member >= r->count) {
        *start = 0;
        return 0;
    }
    *start = r->image + 8 + ResidentRead32(r->bytes + r->fat + 12 + member * 8);
    return r->image + 8 + ResidentRead32(r->bytes + r->fat + 16 + member * 8) - *start;
}
static void ResidentRead(const ResidentNARC *r, u32 start, u32 size, void *dest)
{
    GF_ASSERT(start <= r->size && size <= r->size - start);
    if (start > r->size || size > r->size - start) {
        return;
    }
    if (size) {
        memcpy(dest, r->bytes + start, size);
    }
    SIM_ProfileAdd(SIM_PROFILE_NARC_RAM_BYTES, size);
}
void NARC_PreloadFieldArchives(void)
{
    for (u32 i = 0; i < NELEMS(sFieldNARCs); i++) {
        ResidentGet(sFieldNARCs[i]);
    }
}
BOOL NARC_InvalidateFieldArchives(void)
{
    for (u32 i = 0; i < NELEMS(sResidentNARCs); i++) {
        if (sResidentNARCs[i].refs) {
            return FALSE;
        }
    }
    for (u32 i = 0; i < NELEMS(sResidentNARCs); i++) {
        free(sResidentNARCs[i].bytes);
        memset(&sResidentNARCs[i], 0, sizeof(ResidentNARC));
    }
    sResidentBytes = 0;
    SIM_ProfileSet(SIM_PROFILE_CPU_CACHE_BYTES, 0);
    return TRUE;
}
u32 NARC_FieldCacheBytes(void)
{
    return sResidentBytes;
}
#endif

static void ReadFromNarcMemberByPathAndIndex(void *dest, const char *path, int memberIndex, int offset, int bytesToRead);

static void ReadFromNarcMemberByPathAndIndex(void *dest, const char *path, int memberIndex, int offset, int bytesToRead)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    const u8 *optional = BwBattle_IrisResource(path, memberIndex, &optionalSize);
    if (optional) {
        u32 count = bytesToRead ? bytesToRead : optionalSize;
        GF_ASSERT(offset >= 0 && offset <= optionalSize && count <= optionalSize - offset);
        if (offset < 0 || offset > optionalSize || count > optionalSize - offset) {
            return;
        }

        if (dest) {
            memcpy(dest, optional + offset, count);
        }
        return;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    ResidentNARC *resident = ResidentByPath(path);
    if (resident) {
        u32 start;
        u32 size = ResidentSpan(resident, memberIndex, &start);
        // Explicit reads may span adjacent members (e.g. the battle item
        // table). Preserve that legacy contract within the validated image.
        u32 available = resident->imageEnd - start;
        u32 count = bytesToRead ? bytesToRead : size;
        GF_ASSERT(offset >= 0 && offset <= available && count <= available - offset);
        if (offset < 0 || offset > available || count > available - offset) {
            return;
        }
        ResidentRead(resident, start + offset, count, dest);
        return;
    }
#endif
    FSFile file;
    u32 btafStart = 0;
    u32 btnfStart = 0;
    u32 gmifStart = 0;
    u32 chunkSize = 0;
    u32 fileStart = 0;
    u32 fileEnd = 0;
    u16 fileCount = 0;

    FS_InitFile(&file);
    FS_OpenFile(&file, path);
    FS_SeekFile(&file, 12, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 2);

    btnfStart = btafStart;

    FS_SeekFile(&file, btnfStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 4);
    NARC_ReadDisk(&file, &fileCount, 2);

    GF_ASSERT(fileCount > memberIndex);

    gmifStart = btnfStart + btafStart;

    FS_SeekFile(&file, gmifStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 4);

    chunkSize = gmifStart + btafStart;

    FS_SeekFile(&file, btnfStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&file, &fileStart, 4);
    NARC_ReadDisk(&file, &fileEnd, 4);
    FS_SeekFile(&file, chunkSize + 8 + fileStart + offset, FS_SEEK_SET);

    if (bytesToRead) {
        btafStart = bytesToRead;
    } else {
        btafStart = fileEnd - fileStart;
    }

    GF_ASSERT(btafStart != 0);

    NARC_ReadDisk(&file, dest, btafStart);
    FS_CloseFile(&file);
}

static void *AllocAndReadFromNarcMemberByPathAndIndex(const char *path, int memberIndex, enum HeapID heapID, int offset, int bytesToRead, BOOL allocAtEnd)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    const u8 *optional = BwBattle_IrisResource(path, memberIndex, &optionalSize);
    if (optional) {
        u32 count = bytesToRead ? bytesToRead : optionalSize;
        GF_ASSERT(offset >= 0 && offset <= optionalSize && count <= optionalSize - offset);
        if (offset < 0 || offset > optionalSize || count > optionalSize - offset) {
            return NULL;
        }
        void *dest = allocAtEnd ? Heap_AllocAtEnd(heapID, count) : Heap_Alloc(heapID, count);
        if (dest) {
            memcpy(dest, optional + offset, count);
        }
        return dest;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    ResidentNARC *resident = ResidentByPath(path);
    if (resident) {
        u32 start;
        u32 size = ResidentSpan(resident, memberIndex, &start);
        // Explicit reads may span adjacent members (e.g. the battle item
        // table). Preserve that legacy contract within the validated image.
        u32 available = resident->imageEnd - start;
        u32 count = bytesToRead ? bytesToRead : size;
        GF_ASSERT(offset >= 0 && offset <= available && count <= available - offset);
        if (offset < 0 || offset > available || count > available - offset) {
            return NULL;
        }
        void *dest = allocAtEnd ? Heap_AllocAtEnd(heapID, count) : Heap_Alloc(heapID, count);
        if (dest) {
            ResidentRead(resident, start + offset, count, dest);
        }
        return dest;
    }
#endif
    FSFile file;
    u32 btafStart = 0;
    u32 btnfStart = 0;
    u32 gmifStart = 0;
    u32 chunkSize = 0;
    u32 fileStart = 0;
    u32 fileEnd = 0;
    void *dest;
    u16 fileCount = 0;

    FS_InitFile(&file);
    FS_OpenFile(&file, path);
    FS_SeekFile(&file, 12, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 2);

    btnfStart = btafStart;

    FS_SeekFile(&file, btnfStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 4);
    NARC_ReadDisk(&file, &fileCount, 2);

    GF_ASSERT(fileCount > memberIndex);

    gmifStart = btnfStart + btafStart;

    FS_SeekFile(&file, gmifStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &btafStart, 4);

    chunkSize = gmifStart + btafStart;

    FS_SeekFile(&file, btnfStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&file, &fileStart, 4);
    NARC_ReadDisk(&file, &fileEnd, 4);
    FS_SeekFile(&file, chunkSize + 8 + fileStart + offset, FS_SEEK_SET);

    if (bytesToRead) {
        btafStart = bytesToRead;
    } else {
        btafStart = fileEnd - fileStart;
    }

    GF_ASSERT(btafStart != 0);

    if (allocAtEnd == 0) {
        dest = Heap_Alloc(heapID, btafStart);
    } else {
        dest = Heap_AllocAtEnd(heapID, btafStart);
    }

    NARC_ReadDisk(&file, dest, btafStart);
    FS_CloseFile(&file);

    return dest;
}

void NARC_ReadWholeMemberByIndexPair(void *dest, enum NarcID narcID, int memberIndex)
{
    ReadFromNarcMemberByPathAndIndex(dest, sNarcPaths[narcID], memberIndex, 0, 0);
}

void *NARC_AllocAndReadWholeMemberByIndexPair(enum NarcID narcID, int memberIndex, enum HeapID heapID)
{
    return AllocAndReadFromNarcMemberByPathAndIndex(sNarcPaths[narcID], memberIndex, heapID, 0, 0, FALSE);
}

void *NARC_AllocAtEndAndReadWholeMemberByIndexPair(enum NarcID narcID, int memberIndex, enum HeapID heapID)
{
    return AllocAndReadFromNarcMemberByPathAndIndex(sNarcPaths[narcID], memberIndex, heapID, 0, 0, TRUE);
}

void NARC_ReadFromMemberByIndexPair(void *dest, enum NarcID narcID, int memberIndex, int offset, int bytesToRead)
{
    ReadFromNarcMemberByPathAndIndex(dest, sNarcPaths[narcID], memberIndex, offset, bytesToRead);
}

void *NARC_AllocAndReadFromMemberByIndexPair(enum NarcID narcID, int memberIndex, enum HeapID heapID, int offset, int bytesToRead)
{
    return AllocAndReadFromNarcMemberByPathAndIndex(sNarcPaths[narcID], memberIndex, heapID, offset, bytesToRead, FALSE);
}

void *NARC_AllocAtEndAndReadFromMemberByIndexPair(enum NarcID narcID, int memberIndex, enum HeapID heapID, int offset, int bytesToRead)
{
    return AllocAndReadFromNarcMemberByPathAndIndex(sNarcPaths[narcID], memberIndex, heapID, offset, bytesToRead, TRUE);
}

u32 NARC_GetMemberSizeByIndexPair(enum NarcID narcID, int memberIndex)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    if (BwBattle_IrisResource(sNarcPaths[narcID], memberIndex, &optionalSize)) {
        return optionalSize;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    ResidentNARC *resident = ResidentGet(narcID);
    if (resident) {
        u32 start;
        return ResidentSpan(resident, memberIndex, &start);
    }
#endif
    FSFile file;
    u32 chunkSize = 0;
    u32 btafStart = 0;
    u32 btnfStart = 0;
    u32 gmifStart = 0;
    u32 fileStart = 0;
    u32 fileEnd = 0;
    u16 fileCount = 0;

    FS_InitFile(&file);
    FS_OpenFile(&file, sNarcPaths[narcID]);
    FS_SeekFile(&file, 12, FS_SEEK_SET);
    NARC_ReadDisk(&file, &chunkSize, 2);

    btafStart = chunkSize;

    FS_SeekFile(&file, btafStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &chunkSize, 4);
    NARC_ReadDisk(&file, &fileCount, 2);

    GF_ASSERT(fileCount > memberIndex);

    btnfStart = btafStart + chunkSize;

    FS_SeekFile(&file, btnfStart + 4, FS_SEEK_SET);
    NARC_ReadDisk(&file, &chunkSize, 4);

    gmifStart = btnfStart + chunkSize;

    FS_SeekFile(&file, btafStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&file, &fileStart, 4);
    NARC_ReadDisk(&file, &fileEnd, 4);
    FS_SeekFile(&file, gmifStart + 8 + fileStart, FS_SEEK_SET);

    chunkSize = fileEnd - fileStart;

    GF_ASSERT(chunkSize != 0);
    // Bug: file is never closed!
    #ifdef SDK_PORT
    FS_CloseFile(&file);
    #endif

    return chunkSize;
}

NARC *NARC_ctor(enum NarcID narcID, enum HeapID heapID)
{
    NARC *narc = Heap_Alloc(heapID, sizeof(NARC));

    if (narc) {
        u32 btnfStart;
        u32 chunkSize;

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
        narc->narcID = narcID;
        narc->optionalData = NULL;
        narc->optionalSize = narc->optionalCursor = 0;
        narc->resident = ResidentGet(narcID);
        narc->cursor = narc->readEnd = 0;
        if (narc->resident) {
            ResidentNARC *r = (ResidentNARC *)narc->resident;
            r->refs++;
            narc->fatbStart = r->fat;
            narc->fimgStart = r->image;
            narc->numFiles = r->count;
            narc->cursor = r->fat + ResidentRead32(r->bytes + r->fat + 4) + 8;
            narc->readEnd = r->size;
            return narc;
        }
#endif
        narc->fatbStart = 0;

        FS_InitFile(&narc->file);
        FS_OpenFile(&narc->file, sNarcPaths[narcID]);
        FS_SeekFile(&narc->file, 12, FS_SEEK_SET);
        NARC_ReadDisk(&narc->file, &(narc->fatbStart), 2);
        FS_SeekFile(&narc->file, narc->fatbStart + 4, FS_SEEK_SET);
        NARC_ReadDisk(&narc->file, &chunkSize, 4);
        NARC_ReadDisk(&narc->file, &(narc->numFiles), 2);

        btnfStart = narc->fatbStart + chunkSize;

        FS_SeekFile(&narc->file, btnfStart + 4, FS_SEEK_SET);
        NARC_ReadDisk(&narc->file, &chunkSize, 4);

        narc->fimgStart = btnfStart + chunkSize;
    }

    return narc;
}

void NARC_dtor(NARC *param0)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (param0->resident) {
        ((ResidentNARC *)param0->resident)->refs--;
        Heap_Free(param0);
        return;
    }
#endif
    FS_CloseFile(&(param0->file));
    Heap_Free(param0);
}

void *NARC_AllocAndReadWholeMember(NARC *narc, u32 memberIndex, enum HeapID heapID)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    const void *optional = BwBattle_IrisResource(sNarcPaths[narc->narcID], memberIndex, &optionalSize);
    if (optional) {
        void *dest = Heap_Alloc(heapID, optionalSize);
        if (dest) {
            NARC_ReadFromMember(narc, memberIndex, 0, optionalSize, dest);
        }
        return dest;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        u32 start;
        u32 size = ResidentSpan(narc->resident, memberIndex, &start);
        void *dest = Heap_Alloc(heapID, size);
        if (dest) {
            NARC_ReadFromMember(narc, memberIndex, 0, size, dest);
        }
        return dest;
    }
#endif
    u32 fileStart;
    u32 fileEnd;
    void *dest;

    GF_ASSERT(narc->numFiles > memberIndex);

    FS_SeekFile(&narc->file, narc->fatbStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, &fileStart, 4);
    NARC_ReadDisk(&narc->file, &fileEnd, 4);
    FS_SeekFile(&narc->file, narc->fimgStart + 8 + fileStart, FS_SEEK_SET);

    dest = Heap_Alloc(heapID, fileEnd - fileStart);

    if (dest) {
        NARC_ReadDisk(&narc->file, dest, fileEnd - fileStart);
    }

    return dest;
}

void NARC_ReadWholeMember(NARC *narc, u32 memberIndex, void *dest)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    if (BwBattle_IrisResource(sNarcPaths[narc->narcID], memberIndex, &optionalSize)) {
        NARC_ReadFromMember(narc, memberIndex, 0, optionalSize, dest);
        return;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        u32 start;
        u32 size = ResidentSpan(narc->resident, memberIndex, &start);
        NARC_ReadFromMember(narc, memberIndex, 0, size, dest);
        return;
    }
#endif
    u32 fileStart;
    u32 fileEnd;

    GF_ASSERT(narc->numFiles > memberIndex);

    FS_SeekFile(&narc->file, narc->fatbStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, &fileStart, 4);
    NARC_ReadDisk(&narc->file, &fileEnd, 4);
    FS_SeekFile(&narc->file, narc->fimgStart + 8 + fileStart, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, dest, fileEnd - fileStart);
}

u32 NARC_GetMemberSize(NARC *narc, u32 memberIndex)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    unsigned int optionalSize;
    narc->optionalData = NULL;
    if (BwBattle_IrisResource(sNarcPaths[narc->narcID], memberIndex, &optionalSize)) {
        return optionalSize;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        u32 start;
        u32 size = ResidentSpan(narc->resident, memberIndex, &start);
        narc->cursor = narc->fatbStart + 20 + memberIndex * 8;
        narc->readEnd = ((const ResidentNARC *)narc->resident)->size;
        return size;
    }
#endif
    u32 fileStart;
    u32 fileEnd;

    GF_ASSERT(narc->numFiles > memberIndex);

    FS_SeekFile(&narc->file, narc->fatbStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, &fileStart, 4);
    NARC_ReadDisk(&narc->file, &fileEnd, 4);

    return fileEnd - fileStart;
}

void NARC_ReadFromMember(NARC *narc, u32 memberIndex, u32 offset, u32 bytesToRead, void *dest)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    narc->optionalData = BwBattle_IrisResource(sNarcPaths[narc->narcID], memberIndex, &narc->optionalSize);
    if (narc->optionalData) {
        narc->optionalCursor = offset;
        NARC_ReadFile(narc, bytesToRead, dest);
        return;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        u32 start;
        const ResidentNARC *resident = narc->resident;
        ResidentSpan(resident, memberIndex, &start);
        u32 available = resident->imageEnd - start;
        GF_ASSERT(offset <= available && bytesToRead <= available - offset);
        if (offset > available || bytesToRead > available - offset) {
            return;
        }
        narc->cursor = start + offset;
        narc->readEnd = resident->imageEnd;
        NARC_ReadFile(narc, bytesToRead, dest);
        return;
    }
#endif
    u32 fileStart;

    GF_ASSERT(narc->numFiles > memberIndex);

    FS_SeekFile(&narc->file, narc->fatbStart + 12 + memberIndex * 8, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, &fileStart, 4);
    FS_SeekFile(&narc->file, narc->fimgStart + 8 + fileStart + offset, FS_SEEK_SET);
    NARC_ReadDisk(&narc->file, dest, bytesToRead);
}

void NARC_ReadFile(NARC *narc, u32 bytesToRead, void *dest)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->optionalData) {
        // Replacement files can be shorter than the member they stand in for, and some callers read a fixed size
        // past the member's end (the Poketch's large icons read 1152 bytes, BW2's icons are 1072). On the DS that
        // read runs into the next file, so give what the file has and zero the rest.
        u32 available = narc->optionalCursor < narc->optionalSize ? narc->optionalSize - narc->optionalCursor : 0;
        u32 copied = bytesToRead < available ? bytesToRead : available;

        memcpy(dest, (const u8 *)narc->optionalData + narc->optionalCursor, copied);
        memset((u8 *)dest + copied, 0, bytesToRead - copied);
        narc->optionalCursor += copied;
        return;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        GF_ASSERT(narc->cursor <= narc->readEnd && bytesToRead <= narc->readEnd - narc->cursor);
        if (narc->cursor > narc->readEnd || bytesToRead > narc->readEnd - narc->cursor) {
            return;
        }
        ResidentRead(narc->resident, narc->cursor, bytesToRead, dest);
        narc->cursor += bytesToRead;
        return;
    }
#endif
    NARC_ReadDisk(&narc->file, dest, bytesToRead);
}

void NARC_Seek(NARC *narc, u32 offset)
{
#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->optionalData) {
        GF_ASSERT(narc->optionalCursor <= narc->optionalSize && offset <= narc->optionalSize - narc->optionalCursor);
        if (narc->optionalCursor <= narc->optionalSize && offset <= narc->optionalSize - narc->optionalCursor) {
            narc->optionalCursor += offset;
        }
        return;
    }
#endif

#if defined(SDK_BUILD_LINUX) || defined(SDK_BUILD_WIN64)
    if (narc->resident) {
        GF_ASSERT(narc->cursor <= narc->readEnd && offset <= narc->readEnd - narc->cursor);
        if (narc->cursor <= narc->readEnd && offset <= narc->readEnd - narc->cursor) {
            narc->cursor += offset;
        }
        return;
    }
#endif
    FS_SeekFile(&narc->file, offset, FS_SEEK_CUR);
}

u16 NARC_GetFileCount(NARC *narc)
{
    return narc->numFiles;
}
