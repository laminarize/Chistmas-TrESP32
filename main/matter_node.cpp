#include "matter_node.h"
#include "tree_effects.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <sdkconfig.h>
#include <atomic>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <zap_common/app/ClusterCallbacks.h>

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter_node";

#define VENDOR_NAME_STR     "Pixel Paw Studio"
#define PRODUCT_NAME_STR    "Christmas TrESP32"
#define TREE_VENDOR_ID      0xFFF1
#define TREE_PRODUCT_ID     0x8000

// Endpoint 1 ColorTemperatureMireds encoding used by edge-driver/ for the RGB toggle switch:
//   <= 300 mireds (154) -> RGB Multicolor ON  (Polarity B / -29V)
//   >  300 mireds (455) -> Warm White (RGB OFF, Polarity A / +29V)
#define MIREDS_RGB_ON       154
#define MIREDS_WARM_WHITE   455
#define MIREDS_THRESHOLD    300

#define NVS_NAMESPACE       "tree_state"
#define NVS_KEY_POWER       "power"
#define NVS_KEY_LEVEL       "level"
#define NVS_KEY_RGB         "rgb"

static uint16_t s_light_endpoint_id = 1;
static std::atomic<bool> s_syncing_attributes{false};

/**
 * Custom DeviceInstanceInfoProvider that pins VendorId (0xFFF1), ProductId (0x8000),
 * Manufacturer ("Pixel Paw Studio"), and Model ("Christmas TrESP32") so our local
 * SmartThings Edge Driver (edge-driver/fingerprints.yml) always matches deterministically.
 */
class PixelPawDeviceInstanceInfoProvider : public chip::DeviceLayer::DeviceInstanceInfoProvider
{
public:
    void Init(chip::DeviceLayer::DeviceInstanceInfoProvider *default_provider)
    {
        m_default = default_provider;
    }

    CHIP_ERROR GetVendorName(char *buf, size_t bufSize) override
    {
        if (bufSize < sizeof(VENDOR_NAME_STR)) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, VENDOR_NAME_STR);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        if (bufSize < sizeof(PRODUCT_NAME_STR)) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, PRODUCT_NAME_STR);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetProductLabel(char *buf, size_t bufSize) override
    {
        if (bufSize < sizeof(PRODUCT_NAME_STR)) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, PRODUCT_NAME_STR);
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetVendorId(uint16_t &vendorId) override
    {
        vendorId = TREE_VENDOR_ID;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetProductId(uint16_t &productId) override
    {
        productId = TREE_PRODUCT_ID;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPartNumber(char *buf, size_t bufSize) override
    {
        return m_default ? m_default->GetPartNumber(buf, bufSize) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetProductURL(char *buf, size_t bufSize) override
    {
        return m_default ? m_default->GetProductURL(buf, bufSize) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        return m_default ? m_default->GetSerialNumber(buf, bufSize) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetManufacturingDate(uint16_t &year, uint8_t &month, uint8_t &day) override
    {
        return m_default ? m_default->GetManufacturingDate(year, month, day) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetHardwareVersion(uint16_t &hardwareVersion) override
    {
        return m_default ? m_default->GetHardwareVersion(hardwareVersion) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetHardwareVersionString(char *buf, size_t bufSize) override
    {
        return m_default ? m_default->GetHardwareVersionString(buf, bufSize) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &uniqueIdSpan) override
    {
        return m_default ? m_default->GetRotatingDeviceIdUniqueId(uniqueIdSpan) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

private:
    chip::DeviceLayer::DeviceInstanceInfoProvider *m_default = nullptr;
};

static PixelPawDeviceInstanceInfoProvider s_pixel_paw_provider;

static void pixel_paw_basic_info_init_cb(chip::EndpointId endpoint_id)
{
    chip::DeviceLayer::DeviceInstanceInfoProvider *current = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
    if (current != &s_pixel_paw_provider) {
        s_pixel_paw_provider.Init(current);
        chip::DeviceLayer::SetDeviceInstanceInfoProvider(&s_pixel_paw_provider);
    }
    ESPMatterBasicInformationClusterServerInitCallback(endpoint_id);
}

static void save_state_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_POWER, tree_effects_get_power() ? 1 : 0);
        nvs_set_u8(handle, NVS_KEY_LEVEL, tree_effects_get_brightness());
        nvs_set_u8(handle, NVS_KEY_RGB, tree_effects_get_rgb_mode() ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void load_state_from_nvs(bool *power, uint8_t *brightness, bool *rgb)
{
    *power = true;
    *brightness = 254;
    *rgb = false;

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t p = 1, l = 254, r = 0;
        if (nvs_get_u8(handle, NVS_KEY_POWER, &p) == ESP_OK) *power = (p != 0);
        if (nvs_get_u8(handle, NVS_KEY_LEVEL, &l) == ESP_OK) *brightness = l;
        if (nvs_get_u8(handle, NVS_KEY_RGB, &r) == ESP_OK) *rgb = (r != 0);
        nvs_close(handle);
    }
}

static esp_err_t app_attribute_update_cb(
    attribute::callback_type_t type,
    uint16_t endpoint_id,
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t *val,
    void *priv_data)
{
    if (type != attribute::callback_type_t::POST_UPDATE || !val ||
        s_syncing_attributes.load(std::memory_order_relaxed)) {
        return ESP_OK;
    }

    if (endpoint_id != s_light_endpoint_id) {
        return ESP_OK;
    }

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        bool power = val->val.b;
        ESP_LOGI(TAG, "SmartThings Master Power: %s", power ? "ON" : "OFF");
        tree_effects_set_power(power);
        save_state_to_nvs();
    } else if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        uint8_t level = val->val.u8;
        ESP_LOGI(TAG, "SmartThings Dimming Slider: %u / 254 (%.0f%%)",
                 level, (float)level / 254.0f * 100.0f);
        tree_effects_set_brightness(level);
        save_state_to_nvs();
    } else if (cluster_id == ColorControl::Id &&
               attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
        uint16_t mireds = val->val.u16;
        bool rgb_on = (mireds <= MIREDS_THRESHOLD);
        ESP_LOGI(TAG, "SmartThings RGB Toggle (mireds=%u): %s -> %s",
                 mireds, rgb_on ? "ON" : "OFF",
                 rgb_on ? "RGB Multicolor (Polarity B)" : "Warm White (Polarity A)");
        tree_effects_set_dual_color(false);
        tree_effects_set_rgb_mode(rgb_on);
        save_state_to_nvs();
    }

    return ESP_OK;
}

static std::atomic<bool> s_identifying{false};
static std::atomic<int64_t> s_identify_effect_until_us{0};

static esp_err_t app_identification_cb(
    identification::callback_type_t type,
    uint16_t endpoint_id,
    uint8_t effect_id,
    uint8_t effect_variant,
    void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback (type=%d, effect=%u) for endpoint: %u",
             (int)type, effect_id, endpoint_id);
    if (type == identification::callback_type_t::START) {
        s_identifying.store(true, std::memory_order_relaxed);
    } else if (type == identification::callback_type_t::STOP) {
        s_identifying.store(false, std::memory_order_relaxed);
    } else if (type == identification::callback_type_t::EFFECT) {
        s_identify_effect_until_us.store(esp_timer_get_time() + 3000000LL, std::memory_order_relaxed);
    }
    return ESP_OK;
}

bool matter_node_is_identifying(void)
{
    if (s_identifying.load(std::memory_order_relaxed)) {
        return true;
    }
    int64_t until_us = s_identify_effect_until_us.load(std::memory_order_relaxed);
    return (until_us > 0) && (esp_timer_get_time() < until_us);
}

esp_err_t matter_node_init(void)
{
    bool initial_power;
    uint8_t initial_brightness;
    bool initial_rgb;

    load_state_from_nvs(&initial_power, &initial_brightness, &initial_rgb);

    tree_effects_set_power(initial_power);
    tree_effects_set_brightness(initial_brightness);
    tree_effects_set_dual_color(false);
    tree_effects_set_fade(false);
    tree_effects_set_twinkle(false);
    tree_effects_set_rgb_mode(initial_rgb);

    node::config_t node_config;
    strncpy(node_config.root_node.basic_information.node_label, PRODUCT_NAME_STR,
            sizeof(node_config.root_node.basic_information.node_label) - 1);

    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Override BasicInformation Cluster's DeviceInstanceInfoProvider so SmartThings reads
    // VendorId = 0xFFF1, ProductId = 0x8000, Manufacturer = "Pixel Paw Studio", Model = "Christmas TrESP32"
    endpoint_t *root_ep = endpoint::get(node, 0);
    if (root_ep) {
        cluster_t *basic_info_cluster = cluster::get(root_ep, BasicInformation::Id);
        if (basic_info_cluster) {
            cluster::basic_information::attribute::create_product_label(
                basic_info_cluster, (char *)PRODUCT_NAME_STR, strlen(PRODUCT_NAME_STR));
            cluster::set_init_and_shutdown_callbacks(
                basic_info_cluster,
                pixel_paw_basic_info_init_cb,
                ESPMatterBasicInformationClusterServerShutdownCallback);
        }
    }

    // Single Matter Endpoint (Endpoint 1): Power + Dimmer + RGB Toggle State
    uint16_t initial_mireds = initial_rgb ? MIREDS_RGB_ON : MIREDS_WARM_WHITE;

    endpoint::color_temperature_light::config_t light_config;
    light_config.on_off.on_off = initial_power;
    light_config.on_off_lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = initial_brightness;
    light_config.level_control.on_level = nullptr; // Keep Routine / pre-staged CurrentLevel on Turn-On
    light_config.level_control.options = 0x01; // ExecuteIfOff
    light_config.level_control_lighting.start_up_current_level = initial_brightness;
    light_config.color_control.color_mode = 2; // ColorTemperatureMireds
    light_config.color_control.enhanced_color_mode = 2;
    light_config.color_control.color_control_options = 0x01; // ExecuteIfOff
    light_config.color_control_color_temperature.color_temp_physical_min_mireds = MIREDS_RGB_ON;
    light_config.color_control_color_temperature.color_temp_physical_max_mireds = MIREDS_WARM_WHITE;
    light_config.color_control_color_temperature.couple_color_temp_to_level_min_mireds = MIREDS_RGB_ON;
    light_config.color_control_color_temperature.color_temperature_mireds = initial_mireds;
    light_config.color_control_color_temperature.start_up_color_temperature_mireds = nullptr;

    endpoint_t *light_ep = endpoint::color_temperature_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!light_ep) {
        ESP_LOGE(TAG, "Failed to create single light endpoint");
        return ESP_FAIL;
    }
    s_light_endpoint_id = endpoint::get_id(light_ep);

    // Mark deferred persistence for rapidly changing dimmer slider attribute
    attribute_t *level_attr = attribute::get(s_light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (level_attr) attribute::set_deferred_persistence(level_attr);

    ESP_LOGI(TAG, "Single Matter endpoint created on EP %u (%s - %s, VID=0x%04X, PID=0x%04X)",
             s_light_endpoint_id, VENDOR_NAME_STR, PRODUCT_NAME_STR, TREE_VENDOR_ID, TREE_PRODUCT_ID);
    return ESP_OK;
}

void matter_node_sync_after_start(void)
{
    bool power = tree_effects_get_power();
    uint8_t brightness = tree_effects_get_brightness();
    bool rgb = tree_effects_get_rgb_mode();
    uint16_t mireds = rgb ? MIREDS_RGB_ON : MIREDS_WARM_WHITE;

    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    s_syncing_attributes.store(true, std::memory_order_relaxed);
    OnOff::Attributes::OnOff::Set(s_light_endpoint_id, power);
    LevelControl::Attributes::OnLevel::SetNull(s_light_endpoint_id);
    LevelControl::Attributes::CurrentLevel::Set(s_light_endpoint_id, brightness);
    ColorControl::Attributes::ColorMode::Set(
        s_light_endpoint_id, static_cast<ColorControl::ColorModeEnum>(2));
    ColorControl::Attributes::EnhancedColorMode::Set(
        s_light_endpoint_id, static_cast<ColorControl::EnhancedColorModeEnum>(2));
    ColorControl::Attributes::ColorTemperatureMireds::Set(s_light_endpoint_id, mireds);
    s_syncing_attributes.store(false, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Synced single-endpoint Matter state: Power=%s, Level=%u, RGB=%s (mireds=%u)",
             power ? "ON" : "OFF", brightness, rgb ? "ON (Multicolor)" : "OFF (Warm White)", mireds);
}
