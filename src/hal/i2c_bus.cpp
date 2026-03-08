#include "hal/i2c_bus.h"

#ifdef DOPE_ASS_PLATFORM_ESP32P4
#include "esp_log.h"
#endif

namespace hal {

bool i2c_init(int port, int sda_pin, int scl_pin, int frequency_hz)
{
#ifdef DOPE_ASS_PLATFORM_ESP32P4
    static const char* TAG = "HAL/I2C";
    ESP_LOGI(TAG, "Init I2C port=%d sda=%d scl=%d freq=%d", port, sda_pin, scl_pin, frequency_hz);
#endif
    (void)port;
    (void)sda_pin;
    (void)scl_pin;
    (void)frequency_hz;
    return true;
}

} // namespace hal
