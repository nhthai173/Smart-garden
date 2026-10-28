/**
 * @file    board_config.h
 * @brief   Pin map for the ESP32-C3 Super Mini.
 *
 * THIS IS A PLACEHOLDER MAP. Adjust it to the real v2 PCB and rebuild.
 * Changing a pin only means editing this file; no other code changes.
 *
 * Constraints of the ESP32-C3 Super Mini (very different from the v1 ESP32 -
 * pins 16/18/19/32/34/35 used in v1 do not exist on the C3):
 *
 *   Usable GPIO : 0..10, 20, 21   (13 pins in total)
 *   ADC1        : GPIO 0,1,2,3,4  <- analog sensors MUST be on these
 *   ADC2        : GPIO 5          <- unusable while WiFi is on. Avoid.
 *   Strapping   : GPIO 2, 8, 9    <- do not hang a load that pulls these at boot
 *   On-board LED: GPIO 8 (active LOW)
 *   BOOT button : GPIO 9
 *   UART0       : GPIO 20 (RX) / 21 (TX) - kept for the serial console
 */

#ifndef SG_BOARD_CONFIG_H
#define SG_BOARD_CONFIG_H

/* --- Valve relays (the v1 motorised ball valve uses all four) --- */
#define PIN_VALVE_POWER      5   /* R1 - supplies the valve motor             */
#define PIN_VALVE_DIRECTION  6   /* R2 - direction: off = open, on = close    */
#define PIN_PUMP_POWER       7   /* R3 - pump                                 */
#define PIN_AC_POWER        10   /* R4 - master AC supply                     */

/* A normally-closed solenoid valve needs one relay; it reuses R1. */
#define PIN_VALVE_SOLENOID   PIN_VALVE_POWER

/* The relays on the v1/v2 PCB are active-low. */
#define RELAY_ACTIVE_LEVEL   0

/* --- Sensors --- */
#define PIN_LEAK_ADC         3   /* ADC1_CH3 - bare probe (analog mode)       */
#define PIN_LEAK_DIGITAL     1   /* module with a comparator (digital mode)   */
#define PIN_VOLTAGE_ADC      4   /* ADC1_CH4 - divider measuring the supply   */
#define PIN_FLOW_SENSOR      0   /* flow-meter pulses (counted by nht_pulse)  */

/* --- On-board user interface --- */
#define PIN_STATUS_LED       8   /* on-board LED, active LOW */
#define LED_ACTIVE_LEVEL     0
#define PIN_BUTTON           9   /* BOOT button: hold 5 s = erase settings (factory defaults) */

/* Supply-measuring divider: Vin = Vadc * (R1 + R2) / R2 */
#define VOLTAGE_DIVIDER_RATIO   11.0f   /* R1 = 100k, R2 = 10k */

#endif /* SG_BOARD_CONFIG_H */
