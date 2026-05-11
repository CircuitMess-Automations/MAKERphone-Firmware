/*
 * circuitos_shim — placeholder.
 *
 * Real implementations land in Sessions 3-6:
 *   S-MP02 Display (Display/Display.h backed by LovyanGFX on ESP-IDF SPI)
 *   S-MP03 I²C + expander drivers (a hal/ sub-component, then InputI2cKeypad)
 *   S-MP04 Input (Input/Input.h with the dashboard.c bit map)
 *   S-MP05 Audio (Audio/Piezo.h as PiezoI2S over MAX98357A)
 *   S-MP06 Battery + LoopManager (Loop/LoopManager.h on FreeRTOS task tick)
 *
 * For now this exists only so the component links cleanly.
 */

const char *circuitos_shim_version(void)
{
    return "circuitos_shim/0.0.1-S-MP01-placeholder";
}
