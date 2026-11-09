#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "tree_driver.h"
#include "tree_effects.h"
#include "matter_node.h"

using namespace chip::DeviceLayer;

static const char *TAG = "christmas_tree_main";
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;

static void boot_button_task(void *pvParameters)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    int pressed_ms = 0;
    while (true) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            pressed_ms += 100;
            if (pressed_ms >= 3000) {
                ESP_LOGW(TAG, "BOOT button held for 3s -> Triggering Matter Factory Reset for pairing!");
                esp_matter::factory_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            pressed_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
        case DeviceEventType::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "IP Address changed / Wi-Fi connected");
            break;

        case DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Matter commissioning complete! Connected to SmartThings.");
            break;

        case DeviceEventType::kFailSafeTimerExpired:
            ESP_LOGW(TAG, "Commissioning fail-safe timer expired");
            break;

        case DeviceEventType::kFabricRemoved:
            ESP_LOGW(TAG, "Fabric removed (deleted from SmartThings) -> Resetting to re-enable BLE pairing!");
            esp_matter::factory_reset();
            break;

        default:
            break;
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "   SmartThings Matter Christmas Tree Controller");
    ESP_LOGI(TAG, "   Home Depot / Polygroup 2-Pin Retrofit       ");
    ESP_LOGI(TAG, "===============================================");

    // 1. Initialize NVS Flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 2. Initialize DRV8871 H-Bridge Driver (GPIO 5 and GPIO 6)
    ESP_ERROR_CHECK(tree_driver_init(TREE_DEFAULT_PIN_IN1, TREE_DEFAULT_PIN_IN2));

    // 3. Initialize Animation & Effects Engine
    ESP_ERROR_CHECK(tree_effects_init());

    // 4. Initialize Matter Data Model
    ESP_ERROR_CHECK(matter_node_init());

    // 5. Start Matter Stack (Automatically prints onboarding QR & setup codes)
    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    // Sync Matter attribute database with persisted tree_state
    matter_node_sync_after_start();

    // Start BOOT button (GPIO 0) 3-second hold monitor for easy factory reset
    xTaskCreate(boot_button_task, "boot_btn", 2048, nullptr, 5, nullptr);

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    ESP_LOGI(TAG, "System operational. Waiting for SmartThings commands...");
}
