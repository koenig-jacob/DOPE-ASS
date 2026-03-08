/**
 * @file hal/spi_bus.h
 * @brief SPI bus initialization helpers.
 */
#pragma once

namespace hal {

bool spi_init(int host, int mosi_pin, int miso_pin, int sclk_pin, int frequency_hz);

} // namespace hal
