/**
 * @file hal/uart_bus.h
 * @brief UART bus initialization helpers.
 */
#pragma once

namespace hal {

bool uart_init(int uart_num, int tx_pin, int rx_pin, int baud_rate);

} // namespace hal
