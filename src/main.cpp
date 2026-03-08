/**
 * @file main.cpp
 * @brief DOPE-ASS firmware entry point for ESP32-P4.
 *
 * Initialises hardware buses, the DOPE ballistic engine, and launches
 * the primary FreeRTOS tasks (sensor polling, BCE update, rendering).
 *
 * Phase 0: skeleton that boots, inits DOPE, and prints a heartbeat.
 */

#include "dope_ass_config.h"
#include "dope/dope_api.h"
#include "dope/dope_types.h"
#include "hal/i2c_bus.h"
#include "hal/spi_bus.h"
#include "hal/uart_bus.h"

#ifdef DOPE_ASS_PLATFORM_ESP32P4
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <cstring>

static const char* TAG = "DOPE-ASS";
#endif

// ---------------------------------------------------------------------------
// Forward declarations — will move to dedicated files in later phases
// ---------------------------------------------------------------------------
static void task_sensor_poll(void* arg);
static void task_bce_loop(void* arg);

#ifdef DOPE_ASS_PLATFORM_ESP32P4
struct SharedState {
    SensorFrame frame;
    RealtimeSolution solution;
    SemaphoreHandle_t frame_mutex;
};

static SharedState g_shared{};
#endif

// ---------------------------------------------------------------------------
// app_main  (ESP-IDF entry point)
// ---------------------------------------------------------------------------
#ifdef DOPE_ASS_PLATFORM_ESP32P4

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "DOPE-ASS v%d.%d  —  booting on ESP32-P4",
             DOPE_ASS_VERSION_MAJOR, DOPE_ASS_VERSION_MINOR);

    // ---- Phase 0: Initialise DOPE engine ----
    BCE_Init();
    ESP_LOGI(TAG, "DOPE engine initialised (BCE v%d.%d)",
             BCE_VERSION_MAJOR, BCE_VERSION_MINOR);

    if (!hal::i2c_init(DASS_I2C0_PORT, DASS_I2C0_SDA_PIN, DASS_I2C0_SCL_PIN, DASS_I2C0_FREQ_HZ) ||
        !hal::spi_init(DASS_SPI_HOST, DASS_SPI_MOSI_PIN, DASS_SPI_MISO_PIN, DASS_SPI_SCLK_PIN, DASS_SPI_FREQ_HZ) ||
        !hal::uart_init(DASS_LRF_UART_NUM, DASS_LRF_TX_PIN, DASS_LRF_RX_PIN, DASS_LRF_BAUD)) {
        ESP_LOGE(TAG, "Hardware bus init failed");
        esp_restart();
    }

    BulletProfile bp{};
    bp.bc = 0.462f;
    bp.drag_model = DragModel::G7;
    bp.muzzle_velocity_ms = 790.0f;
    bp.barrel_length_in = 24.0f;
    bp.reference_barrel_length_in = 24.0f;
    bp.mv_adjustment_factor = 0.0f;
    bp.mass_grains = 168.0f;
    bp.length_mm = 31.2f;
    bp.caliber_inches = 0.308f;
    bp.twist_rate_inches = 10.0f;
    BCE_SetBulletProfile(&bp);

    ZeroConfig zc{};
    zc.zero_range_m = 91.44f;
    zc.sight_height_mm = 38.1f;
    BCE_SetZeroConfig(&zc);

    std::memset(&g_shared.frame, 0, sizeof(g_shared.frame));
    std::memset(&g_shared.solution, 0, sizeof(g_shared.solution));
    g_shared.frame_mutex = xSemaphoreCreateMutex();
    if (g_shared.frame_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create frame mutex");
        esp_restart();
    }

    BaseType_t sensor_ok = xTaskCreatePinnedToCore(
        task_sensor_poll,
        "sensor_poll",
        DASS_STACK_SENSOR,
        nullptr,
        DASS_TASK_PRIO_SENSOR,
        nullptr,
        1
    );
    if (sensor_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor_poll task");
        esp_restart();
    }

    BaseType_t bce_ok = xTaskCreatePinnedToCore(
        task_bce_loop,
        "bce_loop",
        DASS_STACK_BCE,
        nullptr,
        DASS_TASK_PRIO_BCE,
        nullptr,
        1  // Pin to core 1 (core 0 handles WiFi/BT if ever enabled)
    );

    if (bce_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bce_loop task");
        esp_restart();
    }

    ESP_LOGI(TAG, "Startup complete — entering idle.");
}

#else  // Native build (for compile-checking only)

int main()
{
    BCE_Init();
    return 0;
}

#endif // DOPE_ASS_PLATFORM_ESP32P4

// ---------------------------------------------------------------------------
// Sensor polling task — Phase 1 synthetic frame source.
// ---------------------------------------------------------------------------
static void task_sensor_poll(void* /*arg*/)
{
#ifdef DOPE_ASS_PLATFORM_ESP32P4
    const TickType_t period = pdMS_TO_TICKS(1000 / DASS_SENSOR_POLL_HZ);

    while (true)
    {
        SensorFrame frame{};
        frame.timestamp_us = static_cast<uint64_t>(esp_timer_get_time());
        frame.accel_x = 0.0f;
        frame.accel_y = 0.0f;
        frame.accel_z = 9.80665f;
        frame.gyro_x = 0.0f;
        frame.gyro_y = 0.0f;
        frame.gyro_z = 0.0f;
        frame.imu_valid = true;
        frame.mag_valid = false;
        frame.baro_pressure_pa = 101325.0f;
        frame.baro_temperature_c = 20.0f;
        frame.baro_humidity = 0.5f;
        frame.baro_valid = true;
        frame.baro_humidity_valid = true;
        frame.lrf_range_m = 100.0f;
        frame.lrf_timestamp_us = frame.timestamp_us;
        frame.lrf_valid = true;
        frame.encoder_focal_length_mm = 25.0f;
        frame.encoder_valid = true;

        if (xSemaphoreTake(g_shared.frame_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_shared.frame = frame;
            xSemaphoreGive(g_shared.frame_mutex);
        }

        vTaskDelay(period);
    }
#endif
}

// ---------------------------------------------------------------------------
// BCE loop task — SensorFrame -> BCE -> RealtimeSolution -> serial logs.
// ---------------------------------------------------------------------------
static void task_bce_loop(void* /*arg*/)
{
#ifdef DOPE_ASS_PLATFORM_ESP32P4
    const TickType_t period = pdMS_TO_TICKS(1000 / DASS_BCE_UPDATE_HZ);
    uint32_t log_divider = 0;

    while (true)
    {
        SensorFrame frame{};
        if (xSemaphoreTake(g_shared.frame_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            frame = g_shared.frame;
            xSemaphoreGive(g_shared.frame_mutex);
        }

        BCE_Update(&frame);
        BCE_GetRealtimeSolution(&g_shared.solution);

        if (++log_divider >= DASS_BCE_UPDATE_HZ) {
            log_divider = 0;
            ESP_LOGI(TAG,
                     "mode=%u faults=0x%08lx range=%.2fm elev=%.2fMOA wind=%.2fMOA",
                     static_cast<unsigned>(g_shared.solution.solution_mode),
                     static_cast<unsigned long>(g_shared.solution.fault_flags),
                     g_shared.solution.range_m,
                     g_shared.solution.hold_elevation_moa,
                     g_shared.solution.hold_windage_moa);
        }

        vTaskDelay(period);
    }
#endif
}
