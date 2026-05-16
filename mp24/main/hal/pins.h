/*
 * mp24/main/hal/pins.h — single source of truth for MP2.4 GPIO numbers
 * and I²C device addresses.
 *
 * Authoritative source: /mnt/project/pin_config.h (PCB-netlist-derived)
 * cross-checked against schematics + dashboard.c hardware verification.
 *
 * IMPORTANT: pin_config.h's COMMENT BLOCK at the bottom (about which
 * XL9555 bit maps to which button) is STALE — it reflects the original
 * schematic, which was discovered to be wrong on the actual PCB. The
 * canonical button-bit map lives in /mnt/project/dashboard.c
 * (`k_buttons[]`). See docs/MP24_PORT_PLAN.md §7 for the audit trail.
 */
#pragma once

/* ----------------------------------------------------------------- */
/* Display (ST7735 1.77" 128x160 SPI)                                */
/* ----------------------------------------------------------------- */
#define PIN_TFT_MOSI            5
#define PIN_TFT_SCK             4
#define PIN_TFT_DC              6
#define PIN_TFT_RST             7
#define PIN_TFT_CS              (-1)   /* CS tied to GND on display sub-PCB */

/* ----------------------------------------------------------------- */
/* I²C bus 0 — main bus (XL9555 U5 + U9 + AW9523B U12)              */
/* ----------------------------------------------------------------- */
#define PIN_I2C0_SDA           47
#define PIN_I2C0_SCL           48
#define I2C0_FREQ_HZ           100000
#define I2C0_PORT              I2C_NUM_0
#define I2C0_TIMEOUT_MS        100

/* I²C device addresses */
#define I2C_ADDR_XL9555_U5     0x20   /* numpad + A + B + (USB_DETECT) */
#define I2C_ADDR_XL9555_U9     0x21   /* joystick + C + D + VOL ± */
#define I2C_ADDR_AW9523B       0x5B   /* 8 SMD LEDs + amp SD_MODE */

/* ----------------------------------------------------------------- */
/* XL9555 expander interrupt lines (open-drain, active LOW)         */
/* ----------------------------------------------------------------- */
#define PIN_INT_U5              9    /* GPIO_INT1 */
#define PIN_INT_U9             10    /* GPIO_INT2 */

/* ----------------------------------------------------------------- */
/* Battery monitor                                                   */
/* ----------------------------------------------------------------- */
#define PIN_ADC_BATTERY         3    /* ADC1_CH2 — divider 2.0× to VBAT */
#define VBAT_DIVIDER_RATIO      2.0f
#define ADC_UNIT_BATTERY        ADC_UNIT_1
#define ADC_CHANNEL_BATTERY     ADC_CHANNEL_2

/* ----------------------------------------------------------------- */
/* I²S 1 (speaker amp MAX98357A + MEMS mic SPH0645)                  */
/* ----------------------------------------------------------------- */
#define PIN_I2S1_DOUT          38    /* uI2S1_DATA_SPK → amp DIN */
#define PIN_I2S1_BCLK          39    /* uI2S1_CLK_MIC_AMP (JTAG MTCK) */
#define PIN_I2S1_WS            40    /* uI2S1_WS (JTAG MTDO) */
/* MIC data-in line — to be confirmed from schematic in S-MP10 */

/* SD_MODE for the amp lives on AW9523B P1_1, not on the MCU. */
#define AW9523B_PIN_SD_MODE     9    /* P1_1 in 0..15 numbering (P0=0..7, P1=8..15) */

/* ----------------------------------------------------------------- */
/* GSM modem (Quectel EG912U-GL)                                     */
/* ----------------------------------------------------------------- */
/* Verified by tracing the v2.4 schematic, cross-checked against the
 * known-good GPIO9/10 = XL9555 INT1/INT2 mapping to nail down KiCad's
 * y-axis convention. No conflicts with existing pin assignments. */
#define PIN_MODEM_UART_TX      17    /* ESP32 → modem RX (uUART1_GSM_TX) */
#define PIN_MODEM_UART_RX      18    /* modem TX → ESP32 (uUART1_GSM_RX) */
#define PIN_MODEM_PWR_KEY      12    /* uGSM_PWR_KEY — active-high pulse to toggle power */
#define PIN_MODEM_RESET_N      16    /* uGSM_RESET_N — XTAL_32K_N pin (chip pad #22) in IO_MUX, used as GPIO; active-low */
#define PIN_MODEM_PSM_INT      11    /* uGSM_PSM_EXT_INT — wake-from-PSM interrupt (deferred to PSM session) */

#define MODEM_UART_PORT        UART_NUM_1
#define MODEM_UART_BAUD        115200

/* ----------------------------------------------------------------- */
/* Power supply: button input + system kill output                   */
/* ----------------------------------------------------------------- */
/* uBUTTON_PWR comes IN from the power-supply sub-sheet (where the
 * physical side-switch + load-switch IC sit) and lands on GPIO2 — an
 * input the ESP32 reads to detect press / hold events on the side
 * power button while the system is already running.
 *
 * uPOWER_OFF is an OUTPUT we drive to tell the power-supply sub-sheet
 * to cut the main rail — i.e., a software shutdown. Topology of the
 * downstream load-switch and polarity of this signal are not yet
 * verified, so this session leaves the pin in a safe default state
 * (input, no pull) and writes no values. Phase 2 will add the actual
 * power-down call after we verify polarity from observed behaviour
 * (e.g., reading the load-switch's CTRL pin idle state).
 *
 * Both pins also alias to ADC1_CH0 (GPIO1) and ADC1_CH1 (GPIO2), but
 * we use them as plain digital lines. */
#define PIN_PWR_BUTTON         2     /* uBUTTON_PWR — input, active-low (idle HIGH via PMIC pull-up; assumed) */
#define PIN_PWR_OFF            1     /* uPOWER_OFF  — output; left high-impedance for now */

/* ----------------------------------------------------------------- */
/* I²S2 — modem voice PCM (S-MP10 session, not yet used)             */
/* ----------------------------------------------------------------- */
#define PIN_I2S2_CLK           14    /* uI2S2_PHONE_CLK */
#define PIN_I2S2_DATA          13    /* uI2S2_PHONE_DATA */
#define PIN_I2S2_WS            21    /* uI2S2_PHONE_WS */
