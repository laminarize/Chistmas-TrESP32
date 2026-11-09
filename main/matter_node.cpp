#include "matter_node.h"
#include "tree_effects.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <math.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_cluster.h>
#include <esp_matter_endpoint.h>
#include <esp_matter_attribute.h>
#include <esp_matter_feature.h>
#include <app/server/Server.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <zap_common/app/ClusterCallbacks.h>

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter_node";

#define VENDOR_NAME_STR     "Pixel Paw Studio"
#define PRODUCT_NAME_STR    "Christmas TrESP32"

#define NVS_NAMESPACE       "tree_state"
#define NVS_KEY_POWER       "power"
#define NVS_KEY_LEVEL       "level"
#define NVS_KEY_RGB         "rgb"
#define NVS_KEY_TWINKLE     "twinkle"
#define NVS_KEY_SPEED       "speed"

// SmartThings default Color Temperature bounds (2200K = 455 mireds, 6500K = 154 mireds)
#define CT_MIRED_MIN_6500K  154
#define CT_MIRED_MAX_2200K  455
// Threshold at the left end of the Color Temperature slider (~2200K - 2300K) for Twinkle OFF (Steady)
#define CT_MIRED_STEADY_THRESHOLD 435

static uint16_t s_light_endpoint_id = 1;

/**
 * Custom DeviceInstanceInfoProvider that overrides Manufacturer (VendorName)
 * to "Pixel Paw Studio" and Model (ProductName / ProductLabel) to "Christmas TrESP32"
 * while delegating all other factory/device fields to the default ESP32 provider.
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
        return m_default ? m_default->GetVendorId(vendorId) : CHIP_ERROR_NOT_IMPLEMENTED;
    }

    CHIP_ERROR GetProductId(uint16_t &productId) override
    {
        return m_default ? m_default->GetProductId(productId) : CHIP_ERROR_NOT_IMPLEMENTED;
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
        nvs_set_u8(handle, NVS_KEY_TWINKLE, tree_effects_get_twinkle() ? 1 : 0);
        nvs_set_u8(handle, NVS_KEY_SPEED, tree_effects_get_twinkle_speed());
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void load_state_from_nvs(bool *power, uint8_t *brightness, bool *rgb, bool *twinkle, uint8_t *speed)
{
    *power = true;
    *brightness = 254;
    *rgb = false;
    *twinkle = false;
    *speed = 128;

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t p = 1, l = 254, r = 0, t = 0, s = 128;
        if (nvs_get_u8(handle, NVS_KEY_POWER, &p) == ESP_OK) *power = (p != 0);
        if (nvs_get_u8(handle, NVS_KEY_LEVEL, &l) == ESP_OK) *brightness = l;
        if (nvs_get_u8(handle, NVS_KEY_RGB, &r) == ESP_OK) *rgb = (r != 0);
        if (nvs_get_u8(handle, NVS_KEY_TWINKLE, &t) == ESP_OK) *twinkle = (t != 0);
        if (nvs_get_u8(handle, NVS_KEY_SPEED, &s) == ESP_OK) *speed = s;
        nvs_close(handle);
    }
}

/**
 * Convert Twinkle ON/OFF + Speed (1..254) to Mireds (455..154)
 * - Steady (Twinkle OFF) -> 455 mireds (2200K, far left of slider)
 * - Twinkle ON -> 425 mireds (slowest, ~2350K) down to 154 mireds (fastest, 6500K)
 */
static uint16_t twinkle_to_mireds(bool twinkle, uint8_t speed)
{
    if (!twinkle) {
        return CT_MIRED_MAX_2200K;
    }
    uint16_t mired = 425 - (uint16_t)(((uint32_t)speed * (425 - CT_MIRED_MIN_6500K)) / 254);
    if (mired < CT_MIRED_MIN_6500K) mired = CT_MIRED_MIN_6500K;
    if (mired > 425) mired = 425;
    return mired;
}

/**
 * Simple Binary RGB vs. Warm White Selector from SmartThings Color Control:
 * - Tapping the White swatch (or center white of wheel, Saturation < 25) -> Warm White Mode (GPIO 5 / IN1)
 * - Selecting ANY color on the Rainbow picker (Saturation >= 25)         -> RGB Festive Multicolor Mode (GPIO 6 / IN2)
 */
static void apply_hue_sat_selection(uint8_t hue, uint8_t sat)
{
    bool is_rgb = (sat >= 25);
    ESP_LOGI(TAG, "Color Selector [Hue=%u, Sat=%u] -> %s",
             hue, sat, is_rgb ? "RGB (Festive Multicolor)" : "WARM WHITE");
    tree_effects_set_dual_color(false);
    tree_effects_set_rgb_mode(is_rgb);
    save_state_to_nvs();
}

/**
 * Fallback if controller sends CIE XY coordinates instead of Hue/Saturation:
 * - Only the neutral white region (around D65 / warm white swatch, dist < 0.055 from white point) selects Warm White
 * - Any saturated color on the wheel selects RGB Festive Multicolor Mode
 */
static void apply_xy_selection(uint16_t x_raw, uint16_t y_raw)
{
    float x = (float)x_raw / 65536.0f;
    float y = (float)y_raw / 65536.0f;

    float dx = x - 0.313f;
    float dy = y - 0.329f;
    float dist = sqrtf(dx * dx + dy * dy);

    bool is_rgb = (dist >= 0.055f);
    ESP_LOGI(TAG, "Color XY [%.3f, %.3f, dist=%.3f] -> %s",
             x, y, dist, is_rgb ? "RGB (Festive Multicolor)" : "WARM WHITE");
    tree_effects_set_dual_color(false);
    tree_effects_set_rgb_mode(is_rgb);
    save_state_to_nvs();
}

/**
 * Evaluate Color Temperature Slider (Mireds 455..154, i.e. 2200K..6500K) as:
 * - Twinkle Toggle: Far left (2200K-2300K, mireds >= 435) = Twinkle OFF (Steady)
 * - Twinkle Speed Slider: 2350K..6500K (mireds 434..154) = Twinkle ON with Speed 1..254
 */
static void apply_color_temp_twinkle(uint16_t mireds)
{
    if (mireds >= CT_MIRED_STEADY_THRESHOLD) {
        ESP_LOGI(TAG, "Twinkle Slider [%u mireds / ~%uK] -> Twinkle OFF (Steady)",
                 mireds, 1000000U / (mireds ? mireds : 1));
        tree_effects_set_twinkle(false);
    } else {
        uint16_t clamped = mireds;
        if (clamped < CT_MIRED_MIN_6500K) clamped = CT_MIRED_MIN_6500K;
        // Map [434 .. 154] -> [1 .. 254]
        uint32_t span = (CT_MIRED_STEADY_THRESHOLD - 1) - CT_MIRED_MIN_6500K;
        uint32_t pos  = (CT_MIRED_STEADY_THRESHOLD - 1) - clamped;
        uint8_t speed = (uint8_t)(1 + (pos * 253) / span);
        ESP_LOGI(TAG, "Twinkle Slider [%u mireds / ~%uK] -> Twinkle ON, Speed=%u/254 (%.0f%%)",
                 mireds, 1000000U / (mireds ? mireds : 1), speed, (float)speed / 254.0f * 100.0f);
        tree_effects_set_twinkle_speed(speed);
        tree_effects_set_twinkle(true);
    }
    save_state_to_nvs();
}

static esp_err_t app_attribute_update_cb(
    attribute::callback_type_t type,
    uint16_t endpoint_id,
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t *val,
    void *priv_data)
{
    if (type != attribute::callback_type_t::POST_UPDATE || !val) {
        return ESP_OK;
    }

    if (endpoint_id == s_light_endpoint_id) {
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
        } else if (cluster_id == ColorControl::Id) {
            if (attribute_id == ColorControl::Attributes::CurrentHue::Id ||
                attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
                uint8_t hue = 0;
                uint8_t sat = 0;
                ColorControl::Attributes::CurrentHue::Get(endpoint_id, &hue);
                ColorControl::Attributes::CurrentSaturation::Get(endpoint_id, &sat);
                apply_hue_sat_selection(hue, sat);
            } else if (attribute_id == ColorControl::Attributes::CurrentX::Id ||
                       attribute_id == ColorControl::Attributes::CurrentY::Id) {
                uint16_t x = 20493, y = 21561;
                ColorControl::Attributes::CurrentX::Get(endpoint_id, &x);
                ColorControl::Attributes::CurrentY::Get(endpoint_id, &y);
                apply_xy_selection(x, y);
            } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
                apply_color_temp_twinkle(val->val.u16);
            }
        }
    }

    return ESP_OK;
}

static esp_err_t app_identification_cb(
    identification::callback_type_t type,
    uint16_t endpoint_id,
    uint8_t effect_id,
    uint8_t effect_variant,
    void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback called for endpoint: %d", endpoint_id);
    return ESP_OK;
}

esp_err_t matter_node_init(void)
{
    bool initial_power;
    uint8_t initial_brightness;
    bool initial_rgb;
    bool initial_twinkle;
    uint8_t initial_speed;

    load_state_from_nvs(&initial_power, &initial_brightness, &initial_rgb,
                        &initial_twinkle, &initial_speed);

    tree_effects_set_power(initial_power);
    tree_effects_set_brightness(initial_brightness);
    tree_effects_set_dual_color(false);
    tree_effects_set_rgb_mode(initial_rgb);
    tree_effects_set_twinkle_speed(initial_speed);
    tree_effects_set_twinkle(initial_twinkle);

    node::config_t node_config;
    strncpy(node_config.root_node.basic_information.node_label, PRODUCT_NAME_STR,
            sizeof(node_config.root_node.basic_information.node_label) - 1);

    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

    // Override BasicInformation Cluster's DeviceInstanceInfoProvider so SmartThings reads
    // Manufacturer = "Pixel Paw Studio" and Model = "Christmas TrESP32"
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

    // Single-Card SmartThings Profile: Extended Color Light (0x010D -> light-color-level)
    endpoint::extended_color_light::config_t light_config;
    light_config.on_off.on_off = initial_power;
    light_config.on_off_lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = initial_brightness;
    light_config.level_control.on_level = initial_brightness;
    light_config.level_control_lighting.start_up_current_level = initial_brightness;

    uint16_t initial_mireds = twinkle_to_mireds(initial_twinkle, initial_speed);
    light_config.color_control.color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control_color_temperature.color_temperature_mireds = initial_mireds;
    light_config.color_control_color_temperature.color_temp_physical_min_mireds = CT_MIRED_MIN_6500K;
    light_config.color_control_color_temperature.color_temp_physical_max_mireds = CT_MIRED_MAX_2200K;
    light_config.color_control_color_temperature.couple_color_temp_to_level_min_mireds = CT_MIRED_MIN_6500K;
    light_config.color_control_color_temperature.start_up_color_temperature_mireds = nullptr;

    endpoint_t *light_ep = endpoint::extended_color_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!light_ep) {
        ESP_LOGE(TAG, "Failed to create extended_color_light endpoint");
        return ESP_FAIL;
    }
    s_light_endpoint_id = endpoint::get_id(light_ep);

    // Enable Hue & Saturation feature on ColorControl so SmartThings sends MoveToHueAndSaturation
    cluster_t *color_cluster = cluster::get(light_ep, ColorControl::Id);
    if (color_cluster) {
        cluster::color_control::feature::hue_saturation::config_t hs_cfg;
        hs_cfg.current_hue = initial_rgb ? 170 : 0;
        hs_cfg.current_saturation = initial_rgb ? 254 : 0;
        cluster::color_control::feature::hue_saturation::add(color_cluster, &hs_cfg);
    }

    // Mark deferred persistence for rapidly changing slider attributes
    attribute_t *level_attr = attribute::get(s_light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (level_attr) attribute::set_deferred_persistence(level_attr);
    attribute_t *ct_attr = attribute::get(s_light_endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
    if (ct_attr) attribute::set_deferred_persistence(ct_attr);

    ESP_LOGI(TAG, "Single-Card Extended Color Light created on Endpoint %d (%s - %s)",
             s_light_endpoint_id, VENDOR_NAME_STR, PRODUCT_NAME_STR);
    return ESP_OK;
}

void matter_node_sync_after_start(void)
{
    bool rgb = tree_effects_get_rgb_mode();
    bool twinkle = tree_effects_get_twinkle();
    uint8_t speed = tree_effects_get_twinkle_speed();
    uint16_t mireds = twinkle_to_mireds(twinkle, speed);

    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    ColorControl::Attributes::CurrentHue::Set(s_light_endpoint_id, rgb ? 170 : 0);
    ColorControl::Attributes::CurrentSaturation::Set(s_light_endpoint_id, rgb ? 254 : 0);
    ColorControl::Attributes::ColorTemperatureMireds::Set(s_light_endpoint_id, mireds);
    ESP_LOGI(TAG, "Synced Matter attributes with tree_state: Mode=%s, Twinkle=%s (Speed=%u)",
             rgb ? "RGB" : "Warm White", twinkle ? "ON" : "OFF", speed);
}
