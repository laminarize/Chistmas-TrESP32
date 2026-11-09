#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lighting Modes matching the Polygroup PDT-015-14 Remote Control
 */
typedef enum {
    TREE_MODE_STEADY_WARM_WHITE     = 0,
    TREE_MODE_STEADY_MULTICOLOR     = 1,
    TREE_MODE_STEADY_DUAL           = 2,
    TREE_MODE_FADE_WARM_WHITE       = 3,
    TREE_MODE_FADE_MULTICOLOR       = 4,
    TREE_MODE_FADE_ALTERNATING      = 5,
    TREE_MODE_TWINKLE_WARM_WHITE    = 6,
    TREE_MODE_TWINKLE_MULTICOLOR    = 7,
    TREE_MODE_TWINKLE_ALTERNATING   = 8,
    TREE_MODE_DEMO                  = 9,
    TREE_MODE_MAX
} tree_mode_t;

/**
 * @brief Initialize the effects task and state machine.
 */
esp_err_t tree_effects_init(void);

/**
 * @brief Set the tree power state (On / Off).
 */
void tree_effects_set_power(bool power_on);

/**
 * @brief Get the current tree power state.
 */
bool tree_effects_get_power(void);

/**
 * @brief Set the global brightness (Matter LevelControl standard 0 - 254).
 */
void tree_effects_set_brightness(uint8_t brightness);

/**
 * @brief Get the current global brightness (0 - 254).
 */
uint8_t tree_effects_get_brightness(void);

/**
 * @brief Set the active lighting mode.
 */
void tree_effects_set_mode(tree_mode_t mode);

/**
 * @brief Get the current active lighting mode.
 */
tree_mode_t tree_effects_get_mode(void);

/**
 * @brief Set RGB Mode (false = Warm White, true = RGB / Multicolor).
 */
void tree_effects_set_rgb_mode(bool rgb_on);

/**
 * @brief Get current RGB Mode.
 */
bool tree_effects_get_rgb_mode(void);

/**
 * @brief Set Twinkle effect toggle (false = Steady, true = Twinkle).
 */
void tree_effects_set_twinkle(bool twinkle_on);

/**
 * @brief Get current Twinkle state.
 */
bool tree_effects_get_twinkle(void);

/**
 * @brief Set Twinkle animation speed (0 - 254).
 */
void tree_effects_set_twinkle_speed(uint8_t speed);

/**
 * @brief Get current Twinkle animation speed (0 - 254).
 */
uint8_t tree_effects_get_twinkle_speed(void);

/**
 * @brief Set Dual Color mode (both Warm White & RGB simultaneously).
 */
void tree_effects_set_dual_color(bool dual_on);

/**
 * @brief Get current Dual Color state.
 */
bool tree_effects_get_dual_color(void);

/**
 * @brief Set Fade animation mode.
 */
void tree_effects_set_fade(bool fade_on);

/**
 * @brief Get current Fade state.
 */
bool tree_effects_get_fade(void);

/**
 * @brief Get the human-readable name of a lighting mode.
 */
const char* tree_effects_get_mode_name(tree_mode_t mode);

#ifdef __cplusplus
}
#endif
