#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Matter Data Model with a single endpoint (Endpoint 1):
 *   - Endpoint 1 (Color Temperature Light 0x010C):
 *       * OnOff        -> Master Power On/Off
 *       * LevelControl -> Dimmer Slider (1% - 100%, 25 kHz PWM)
 *       * ColorControl -> RGB Toggle State (154 mireds = RGB Multicolor ON,
 *                         455 mireds = Warm White OFF), mapped to a clean
 *                         "RGB Multicolor" On/Off toggle switch on the same
 *                         single SmartThings card by edge-driver/.
 */
esp_err_t matter_node_init(void);

/**
 * Synchronize Matter attributes on Endpoint 1 with our NVS state AFTER
 * esp_matter::start() so SmartThings always sees the authoritative power,
 * brightness, and Warm White / RGB state.
 */
void matter_node_sync_after_start(void);

/**
 * Returns true while the Matter Identify cluster is actively identifying
 * (either timed IdentifyTime countdown or TriggerEffect).
 */
bool matter_node_is_identifying(void);

#ifdef __cplusplus
}
#endif
