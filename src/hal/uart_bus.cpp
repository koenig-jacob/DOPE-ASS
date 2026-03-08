#include "hal/uart_bus.h"

#ifdef DOPE_ASS_PLATFORM_ESP32P4
#include "esp_log.h"
#endif

namespace hal {

bool uart_init(int uart_num, int tx_pin, int rx_pin, int baud_rate)
{
#ifdef DOPE_ASS_PLATFORM_ESP32P4
    static const char* TAG = "HAL/UART";
    ESP_LOGI(TAG, "Init UART num=%d tx=%d rx=%d baud=%d", uart_num, tx_pin, rx_pin, baud_rate);
#endif
    (void)uart_num;
    (void)tx_pin;
    (void)rx_pin;
    (void)baud_rate;
    return true;
}

} // namespace hal
