#include "pokedex_data_index.h"

#include <nitro.h>
#include <string.h>

#include "generated/text_banks.h"

#include "narc.h"

#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#endif

static enum NarcID sPokedexDataNarcID = NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_DATA;
static u32 sWeightMessageBankIndex = TEXT_BANK_SPECIES_WEIGHT;
static u32 sHeightMessageBankIndex = TEXT_BANK_SPECIES_HEIGHT;

void Pokedex_SetupGiratina(u32 formNun)
{
    if (formNun == 0) {
        sPokedexDataNarcID = NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_DATA_GIRA;
        sWeightMessageBankIndex = TEXT_BANK_SPECIES_WEIGHT_GIRA;
        sHeightMessageBankIndex = TEXT_BANK_SPECIES_HEIGHT_GIRA;
    } else {
        sPokedexDataNarcID = NARC_INDEX_APPLICATION__ZUKANLIST__ZKN_DATA__ZUKAN_DATA;
        sWeightMessageBankIndex = TEXT_BANK_SPECIES_WEIGHT;
        sHeightMessageBankIndex = TEXT_BANK_SPECIES_HEIGHT;
    }
}

enum NarcID Pokedex_Data_NARC_Index(void)
{
    return sPokedexDataNarcID;
}

BOOL Pokedex_UsesMetricUnits(void)
{
#ifdef SDK_PORT
    return SIM_Config_prj_GetConfig()->pokedexUnits == POKEDEX_UNITS_METRIC;
#else
    return FALSE;
#endif
}

u32 Weight_Message_Bank_Index(void)
{
    if (Pokedex_UsesMetricUnits()) {
        return sWeightMessageBankIndex == TEXT_BANK_SPECIES_WEIGHT_GIRA ? TEXT_BANK_SPECIES_WEIGHT_GIRA_METRIC : TEXT_BANK_SPECIES_WEIGHT_METRIC;
    }

    return sWeightMessageBankIndex;
}

u32 Height_Message_Bank_Index(void)
{
    if (Pokedex_UsesMetricUnits()) {
        return sHeightMessageBankIndex == TEXT_BANK_SPECIES_HEIGHT_GIRA ? TEXT_BANK_SPECIES_HEIGHT_GIRA_METRIC : TEXT_BANK_SPECIES_HEIGHT_METRIC;
    }

    return sHeightMessageBankIndex;
}
