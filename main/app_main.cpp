#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

#include <esp_matter.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "tree_driver.h"
#include "tree_effects.h"
#include "matter_node.h"

using namespace chip::DeviceLayer;

static const char *TAG = "christmas_tree_main";
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;
static constexpr gpio_num_t STATUS_LED_GPIO  = GPIO_NUM_38; // Rev C green Matter status LED (D6 + R11)

static std::atomic<bool> s_is_commissioned{false};
static std::atomic<bool> s_commissioning_window_open{false};

static void boot_button_task(void *pvParameters)
{
    gpio_config_t btn_conf = {};
    btn_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO);
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&btn_conf);

    gpio_config_t led_conf = {};
    led_conf.pin_bit_mask = (1ULL << STATUS_LED_GPIO);
    led_conf.mode = GPIO_MODE_OUTPUT;
    led_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&led_conf);
    gpio_set_level(STATUS_LED_GPIO, 0);

    int pressed_ms = 0;
    uint32_t tick = 0;
    while (true) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            // 5 Hz rapid blink during the 3-second factory-reset hold countdown
            gpio_set_level(STATUS_LED_GPIO, (tick % 2U) == 0U ? 1 : 0);
            pressed_ms += 100;
            if (pressed_ms >= 3000) {
                ESP_LOGW(TAG, "BOOT button held for 3s -> Triggering Matter Factory Reset for pairing!");
                gpio_set_level(STATUS_LED_GPIO, 0);
                esp_matter::factory_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            pressed_ms = 0;
            bool led_on = false;
            if (matter_node_is_identifying()) {
                // 2.5 Hz blink while Matter Identify is active
                led_on = ((tick / 2U) % 2U) == 0U;
            } else if (!s_is_commissioned.load(std::memory_order_relaxed) ||
                       s_commissioning_window_open.load(std::memory_order_relaxed)) {
                // 1 Hz blink while uncommissioned / BLE pairing window open
                led_on = ((tick / 5U) % 2U) == 0U;
            } else {
                // Solid ON once commissioned onto a Matter fabric
                led_on = true;
            }
            gpio_set_level(STATUS_LED_GPIO, led_on ? 1 : 0);
        }
        ++tick;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
        case DeviceEventType::kInterfaceIpAddressChanged:
            ESP_LOGI(TAG, "IP Address changed / Wi-Fi connected");
            break;

        case DeviceEventType::kCommissioningWindowOpened:
            ESP_LOGI(TAG, "Matter commissioning window opened (STATUS LED: 1 Hz pairing blink)");
            s_commissioning_window_open.store(true, std::memory_order_relaxed);
            break;

        case DeviceEventType::kCommissioningWindowClosed:
            ESP_LOGI(TAG, "Matter commissioning window closed");
            s_commissioning_window_open.store(false, std::memory_order_relaxed);
            break;

        case DeviceEventType::kCommissioningComplete:
            ESP_LOGI(TAG, "Matter commissioning complete! Connected to SmartThings.");
            s_is_commissioned.store(true, std::memory_order_relaxed);
            s_commissioning_window_open.store(false, std::memory_order_relaxed);
            break;

        case DeviceEventType::kFailSafeTimerExpired:
            ESP_LOGW(TAG, "Commissioning fail-safe timer expired");
            break;

        case DeviceEventType::kFabricRemoved:
            ESP_LOGW(TAG, "Fabric removed (deleted from SmartThings) -> Resetting to re-enable BLE pairing!");
            s_is_commissioned.store(false, std::memory_order_relaxed);
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

    // Seed initial Matter commissioning & window status before starting GPIO 38 status LED task
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        s_is_commissioned.store(chip::Server::GetInstance().GetFabricTable().FabricCount() > 0,
                                std::memory_order_relaxed);
        s_commissioning_window_open.store(
            chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen(),
            std::memory_order_relaxed);
    }

    // Start BOOT button (GPIO 0) 3-second hold monitor & GPIO 38 Matter Status LED driver
    xTaskCreate(boot_button_task, "boot_btn", 3072, nullptr, 5, nullptr);

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    ESP_LOGI(TAG, "System operational. Waiting for SmartThings commands...");
}
