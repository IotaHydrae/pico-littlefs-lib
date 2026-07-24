/*
 * SD Card SPI Library — Raspberry Pi Pico SDK Port
 *
 * Implements sd_port_*() using the Pico C SDK (hardware_spi,
 * hardware_gpio, pico/time).
 */

/* User-configurable pin mapping — override via sdcard_port_pico.h */
#include "sdcard_port_pico.h"

/* Tell sdcard_port.h we provide optimised bulk SPI functions */
#define SD_PORT_HAVE_SPI_READ
#define SD_PORT_HAVE_SPI_WRITE

#include "sdcard_port.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

/* =========================================================================
 * SPI
 * ========================================================================= */

uint8_t sd_port_spi_rw(uint8_t tx)
{
	uint8_t rx;
	spi_write_read_blocking(SD_PORT_PICO_SPI, &tx, &rx, 1);
	return rx;
}

void sd_port_spi_read(uint8_t *dst, size_t len)
{
	spi_read_blocking(SD_PORT_PICO_SPI, 0xFF, dst, len);
}

void sd_port_spi_write(const uint8_t *src, size_t len)
{
	spi_write_blocking(SD_PORT_PICO_SPI, src, len);
}

/* =========================================================================
 * Chip select
 * ========================================================================= */

void sd_port_cs_low(void)
{
	gpio_put(SD_PORT_PICO_PIN_CS, 0);
}

void sd_port_cs_high(void)
{
	gpio_put(SD_PORT_PICO_PIN_CS, 1);
}

/* =========================================================================
 * Init
 * ========================================================================= */

void sd_port_spi_init(uint32_t freq_hz)
{
	/* Pull CS HIGH first — if we configure SPI before CS, the card
   * may see a floating/low CS and enter an unexpected state.
   * This is especially important after a CPU reset (e.g. picotool
   * reboot) where the card survived the reset but the Pico didn't. */
	gpio_init(SD_PORT_PICO_PIN_CS);
	gpio_set_dir(SD_PORT_PICO_PIN_CS, GPIO_OUT);
	gpio_put(SD_PORT_PICO_PIN_CS, 1); /* inactive */

	gpio_set_function(SD_PORT_PICO_PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(SD_PORT_PICO_PIN_MOSI, GPIO_FUNC_SPI);
	gpio_set_function(SD_PORT_PICO_PIN_SCK, GPIO_FUNC_SPI);

	spi_init(SD_PORT_PICO_SPI, freq_hz);
	spi_set_format(SD_PORT_PICO_SPI, 8, SPI_CPOL_0, SPI_CPHA_0,
		       SPI_MSB_FIRST);
}

uint32_t sd_port_spi_set_freq(uint32_t freq_hz)
{
	return spi_set_baudrate(SD_PORT_PICO_SPI, freq_hz);
}

uint32_t sd_port_spi_high_freq(void)
{
	/* SD cards support up to 25 MHz in SPI mode.  We use 20 MHz for a
   * safe margin; boards with short traces can raise this to 25 MHz. */
	return 31250000;
}

/* =========================================================================
 * Timing
 * ========================================================================= */

void sd_port_delay_us(uint32_t us)
{
	sleep_us(us);
}
void sd_port_delay_ms(uint32_t ms)
{
	sleep_ms(ms);
}
