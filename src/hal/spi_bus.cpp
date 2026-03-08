#include "hal/spi_bus.h"

#ifdef DOPE_ASS_PLATFORM_ESP32P4
#include "esp_log.h"
#endif

namespace hal {

bool spi_init(int host, int mosi_pin, int miso_pin, int sclk_pin, int frequency_hz)
{
#ifdef DOPE_ASS_PLATFORM_ESP32P4
    static const char* TAG = "HAL/SPI";
    ESP_LOGI(TAG, "Init SPI host=%d mosi=%d miso=%d sclk=%d freq=%d",
             host, mosi_pin, miso_pin, sclk_pin, frequency_hz);
#endif
    (void)host;
    (void)mosi_pin;
    (void)miso_pin;
    (void)sclk_pin;
    (void)frequency_hz;
    return true;
}

} // namespace hal
