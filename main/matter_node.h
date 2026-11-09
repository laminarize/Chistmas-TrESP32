#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Matter Data Model for the Christmas TrESP32 node.
 *
 * Configures:
 * - Root Node (Endpoint 0: BasicInformation overridden with Manufacturer
 *   "Pixel Paw Studio" and Model "Christmas TrESP32")
 * - Single-Card Extended Color Light (Endpoint 1: OnOff, LevelControl,
 *   ColorControl with Hue/Saturation + XY + ColorTemperatureMireds)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t matter_node_init(void);

/**
 * @brief Synchronize Matter attribute values with persisted NVS tree state
 *        after esp_matter::start() has initialized the attribute database.
 */
void matter_node_sync_after_start(void);

#ifdef __cplusplus
}
#endif
