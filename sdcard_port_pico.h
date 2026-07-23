/*
 * SD Card Port Configuration — Raspberry Pi Pico
 *
 * Override these macros before including sdcard_port_pico.c to change
 * the SPI peripheral or pin assignment.
 *
 * Wiring (defaults):
 *
 *   Pico pin   SD card pin   Signal
 *   --------   -----------   ------
 *   GP16       D0 / DO       MISO
 *   GP17       D3 / CS       CS   (GPIO, not hardware SPI CS)
 *   GP18       CLK / SCLK    SCK
 *   GP19       CMD / DI      MOSI
 *
 * Also connect:  VCC → 3.3V OUT,  GND → GND.
 */

#ifndef SDCARD_PORT_PICO_H
#define SDCARD_PORT_PICO_H

#ifndef SD_PORT_PICO_SPI
#define SD_PORT_PICO_SPI spi0
#endif

#ifndef SD_PORT_PICO_PIN_MISO
#define SD_PORT_PICO_PIN_MISO 16
#endif

#ifndef SD_PORT_PICO_PIN_CS
#define SD_PORT_PICO_PIN_CS 17
#endif

#ifndef SD_PORT_PICO_PIN_SCK
#define SD_PORT_PICO_PIN_SCK 18
#endif

#ifndef SD_PORT_PICO_PIN_MOSI
#define SD_PORT_PICO_PIN_MOSI 19
#endif

#endif /* SDCARD_PORT_PICO_H */
