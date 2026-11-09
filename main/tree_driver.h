#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default GPIO pins for DRV8871 H-Bridge (Matches 5~ and 6~ on your carrier board)
#define TREE_DEFAULT_PIN_IN1  GPIO_NUM_5
#define TREE_DEFAULT_PIN_IN2  GPIO_NUM_6

// PWM Configuration (25 kHz ultrasonic to eliminate audible 5 kHz coil/capacitor whine)
#define TREE_PWM_FREQ_HZ      25000
#define TREE_PWM_MAX_DUTY     1023   // 10-bit resolution (0 - 1023)

/**
 * @brief Initialize the DRV8871 H-bridge driver using LEDC hardware PWM.
 *
 * @param pin_in1 GPIO connected to DRV8871 IN1
 * @param pin_in2 GPIO connected to DRV8871 IN2
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tree_driver_init(gpio_num_t pin_in1, gpio_num_t pin_in2);

/**
 * @brief Set the target brightness for each polarity (0 - 1023).
 *
 * Polarity A (duty_a) lights Warm White LEDs (+29V on Pin 1).
 * Polarity B (duty_b) lights Multicolor LEDs (-29V on Pin 1).
 * When both are > 0, the driver alternates between polarities at 200 Hz
 * so both colors appear steadily lit without flicker.
 *
 * @param duty_a Brightness for Polarity A / Warm White (0 to 1023)
 * @param duty_b Brightness for Polarity B / Multicolor (0 to 1023)
 */
void tree_driver_set_duties(uint32_t duty_a, uint32_t duty_b);

/**
 * @brief Immediately turn off all outputs (coast / high impedance).
 */
void tree_driver_all_off(void);

#ifdef __cplusplus
}
#endif
