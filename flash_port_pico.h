/*
 * SPI Flash Port Configuration — Raspberry Pi Pico
 *
 * Override these macros before including flash_port_pico.c to change
 * the SPI peripheral or pin assignment.
 *
 * Wiring (defaults):
 *
 *   Pico pin   Flash pin    Signal
 *   --------   ---------    ------
 *   GP16       DI / IO1     MISO
 *   GP17       /CS          CS   (GPIO, not hardware SPI CS)
 *   GP18       CLK          SCK
 *   GP19       DO / IO0     MOSI
 *
 * Also connect:  VCC → 3.3V OUT,  GND → GND.
 */

#ifndef FLASH_PORT_PICO_H
#define FLASH_PORT_PICO_H

#ifndef SPI_FLASH_PORT_PICO_SPI
#define SPI_FLASH_PORT_PICO_SPI spi0
#endif

#ifndef SPI_FLASH_PORT_PICO_PIN_MISO
#define SPI_FLASH_PORT_PICO_PIN_MISO 16
#endif

#ifndef SPI_FLASH_PORT_PICO_PIN_CS
#define SPI_FLASH_PORT_PICO_PIN_CS 17
#endif

#ifndef SPI_FLASH_PORT_PICO_PIN_SCK
#define SPI_FLASH_PORT_PICO_PIN_SCK 18
#endif

#ifndef SPI_FLASH_PORT_PICO_PIN_MOSI
#define SPI_FLASH_PORT_PICO_PIN_MOSI 19
#endif

#endif /* FLASH_PORT_PICO_H */
