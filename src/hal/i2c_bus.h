/**
 * @file hal/i2c_bus.h
 * @brief I2C bus initialization helpers.
 */
#pragma once

namespace hal {

bool i2c_init(int port, int sda_pin, int scl_pin, int frequency_hz);

} // namespace hal
