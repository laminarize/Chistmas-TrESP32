#include "tree_effects.h"
#include "tree_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include <math.h>

static const char *TAG = "tree_effects";

// Square-wave twinkle floor: dimming phase dips to 30% of current dim setting (never fully off)
#define TWINKLE_DIM_FACTOR  0.30f

static bool s_power_on = true;
static uint8_t s_brightness = 254; // Matter max is 254 (maps to TREE_PWM_MAX_DUTY = 1024)
static bool s_rgb_mode = false;     // false = Warm White, true = RGB / Multicolor
static bool s_twinkle_on = false;   // false = Steady, true = Twinkle
static uint8_t s_twinkle_speed = 128; // 0 - 254 (default 50%)
static bool s_dual_color = false;   // Both White & RGB together
static bool s_fade_on = false;      // Fade mode
static tree_mode_t s_active_mode = TREE_MODE_STEADY_WARM_WHITE;
static SemaphoreHandle_t s_mutex = NULL;

static const char* s_mode_names[] = {
    "Steady Warm White",
    "Steady Multicolor",
    "Steady Dual Color",
    "Fade Warm White",
    "Fade Multicolor",
    "Fade Alternating",
    "Twinkle Warm White",
    "Twinkle Multicolor",
    "Twinkle Alternating",
    "Demo Mode"
};

const char* tree_effects_get_mode_name(tree_mode_t mode)
{
    if (mode < TREE_MODE_MAX) {
        return s_mode_names[mode];
    }
    return "Unknown";
}

void tree_effects_set_power(bool power_on)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_power_on = power_on;
        if (!s_power_on) {
            tree_driver_all_off();
        }
        xSemaphoreGive(s_mutex);
    }
}

bool tree_effects_get_power(void)
{
    bool p = true;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        p = s_power_on;
        xSemaphoreGive(s_mutex);
    }
    return p;
}

void tree_effects_set_brightness(uint8_t brightness)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_brightness = brightness;
        xSemaphoreGive(s_mutex);
    }
}

uint8_t tree_effects_get_brightness(void)
{
    uint8_t b = 254;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        b = s_brightness;
        xSemaphoreGive(s_mutex);
    }
    return b;
}

static void update_active_mode_locked(void)
{
    if (s_active_mode == TREE_MODE_DEMO) {
        return;
    }

    if (s_twinkle_on) {
        if (s_dual_color) {
            s_active_mode = TREE_MODE_TWINKLE_ALTERNATING;
        } else if (s_rgb_mode) {
            s_active_mode = TREE_MODE_TWINKLE_MULTICOLOR;
        } else {
            s_active_mode = TREE_MODE_TWINKLE_WARM_WHITE;
        }
    } else if (s_fade_on) {
        if (s_dual_color) {
            s_active_mode = TREE_MODE_FADE_ALTERNATING;
        } else if (s_rgb_mode) {
            s_active_mode = TREE_MODE_FADE_MULTICOLOR;
        } else {
            s_active_mode = TREE_MODE_FADE_WARM_WHITE;
        }
    } else {
        if (s_dual_color) {
            s_active_mode = TREE_MODE_STEADY_DUAL;
        } else if (s_rgb_mode) {
            s_active_mode = TREE_MODE_STEADY_MULTICOLOR;
        } else {
            s_active_mode = TREE_MODE_STEADY_WARM_WHITE;
        }
    }
}

void tree_effects_set_rgb_mode(bool rgb_on)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_rgb_mode = rgb_on;
        s_dual_color = false;
        update_active_mode_locked();
        ESP_LOGI(TAG, "RGB Mode set: %s -> Active Mode: %s", rgb_on ? "RGB" : "White", tree_effects_get_mode_name(s_active_mode));
        xSemaphoreGive(s_mutex);
    }
}

bool tree_effects_get_rgb_mode(void)
{
    bool r = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        r = s_rgb_mode;
        xSemaphoreGive(s_mutex);
    }
    return r;
}

void tree_effects_set_twinkle(bool twinkle_on)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_twinkle_on = twinkle_on;
        if (twinkle_on) {
            s_fade_on = false;
        }
        update_active_mode_locked();
        ESP_LOGI(TAG, "Twinkle set: %s -> Active Mode: %s", twinkle_on ? "ON" : "OFF", tree_effects_get_mode_name(s_active_mode));
        xSemaphoreGive(s_mutex);
    }
}

bool tree_effects_get_twinkle(void)
{
    bool t = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        t = s_twinkle_on;
        xSemaphoreGive(s_mutex);
    }
    return t;
}

void tree_effects_set_twinkle_speed(uint8_t speed)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_twinkle_speed = speed;
        ESP_LOGI(TAG, "Twinkle speed set: %d", speed);
        xSemaphoreGive(s_mutex);
    }
}

uint8_t tree_effects_get_twinkle_speed(void)
{
    uint8_t s = 128;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s = s_twinkle_speed;
        xSemaphoreGive(s_mutex);
    }
    return s;
}

void tree_effects_set_dual_color(bool dual_on)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_dual_color = dual_on;
        if (dual_on) {
            s_rgb_mode = false;
        }
        update_active_mode_locked();
        ESP_LOGI(TAG, "Dual Color set: %s -> Active Mode: %s", dual_on ? "ON" : "OFF", tree_effects_get_mode_name(s_active_mode));
        xSemaphoreGive(s_mutex);
    }
}

bool tree_effects_get_dual_color(void)
{
    bool d = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        d = s_dual_color;
        xSemaphoreGive(s_mutex);
    }
    return d;
}

void tree_effects_set_fade(bool fade_on)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_fade_on = fade_on;
        if (fade_on) {
            s_twinkle_on = false;
        }
        update_active_mode_locked();
        ESP_LOGI(TAG, "Fade set: %s -> Active Mode: %s", fade_on ? "ON" : "OFF", tree_effects_get_mode_name(s_active_mode));
        xSemaphoreGive(s_mutex);
    }
}

bool tree_effects_get_fade(void)
{
    bool f = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        f = s_fade_on;
        xSemaphoreGive(s_mutex);
    }
    return f;
}

void tree_effects_set_mode(tree_mode_t mode)
{
    if (mode >= TREE_MODE_MAX) {
        mode = TREE_MODE_STEADY_WARM_WHITE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_active_mode = mode;
        switch (mode) {
            case TREE_MODE_STEADY_WARM_WHITE:
                s_rgb_mode = false; s_twinkle_on = false; s_dual_color = false; s_fade_on = false;
                break;
            case TREE_MODE_STEADY_MULTICOLOR:
                s_rgb_mode = true;  s_twinkle_on = false; s_dual_color = false; s_fade_on = false;
                break;
            case TREE_MODE_STEADY_DUAL:
                s_rgb_mode = false; s_twinkle_on = false; s_dual_color = true;  s_fade_on = false;
                break;
            case TREE_MODE_FADE_WARM_WHITE:
                s_rgb_mode = false; s_twinkle_on = false; s_dual_color = false; s_fade_on = true;
                break;
            case TREE_MODE_FADE_MULTICOLOR:
                s_rgb_mode = true;  s_twinkle_on = false; s_dual_color = false; s_fade_on = true;
                break;
            case TREE_MODE_FADE_ALTERNATING:
                s_rgb_mode = false; s_twinkle_on = false; s_dual_color = true;  s_fade_on = true;
                break;
            case TREE_MODE_TWINKLE_WARM_WHITE:
                s_rgb_mode = false; s_twinkle_on = true;  s_dual_color = false; s_fade_on = false;
                break;
            case TREE_MODE_TWINKLE_MULTICOLOR:
                s_rgb_mode = true;  s_twinkle_on = true;  s_dual_color = false; s_fade_on = false;
                break;
            case TREE_MODE_TWINKLE_ALTERNATING:
                s_rgb_mode = false; s_twinkle_on = true;  s_dual_color = true;  s_fade_on = false;
                break;
            case TREE_MODE_DEMO:
                break;
            default:
                break;
        }
        ESP_LOGI(TAG, "Mode changed to: %s (%d)", tree_effects_get_mode_name(mode), mode);
        xSemaphoreGive(s_mutex);
    }
}

tree_mode_t tree_effects_get_mode(void)
{
    tree_mode_t m = TREE_MODE_STEADY_WARM_WHITE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        m = s_active_mode;
        xSemaphoreGive(s_mutex);
    }
    return m;
}

// Convert Matter 0-254 level into 0-TREE_PWM_MAX_DUTY (0-1024) driver duty
static inline uint32_t scale_duty(float factor, uint8_t brightness)
{
    if (brightness == 0 || factor <= 0.0f) {
        return 0;
    }
    if (factor > 1.0f) {
        factor = 1.0f;
    }
    if (brightness >= 254 && factor >= 1.0f) {
        return TREE_PWM_MAX_DUTY;
    }
    float peak = ((float)brightness / 254.0f) * (float)TREE_PWM_MAX_DUTY;
    uint32_t duty = (uint32_t)(factor * peak + 0.5f);
    if (duty > TREE_PWM_MAX_DUTY) {
        duty = TREE_PWM_MAX_DUTY;
    }
    return duty;
}

static void effects_task(void *pvParameters)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(20); // 50 Hz update rate
    float angle = 0.0f;
    float twinkle_phase = 0.0f;
    uint32_t demo_timer_ticks = 0;
    tree_mode_t demo_sub_mode = TREE_MODE_STEADY_WARM_WHITE;

    while (1) {
        bool power;
        uint8_t brightness;
        uint8_t twinkle_speed;
        tree_mode_t mode;

        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            power = s_power_on;
            brightness = s_brightness;
            twinkle_speed = s_twinkle_speed;
            mode = s_active_mode;
            xSemaphoreGive(s_mutex);
        } else {
            vTaskDelay(delay_ticks);
            continue;
        }

        if (!power || brightness == 0) {
            tree_driver_all_off();
            vTaskDelay(delay_ticks);
            continue;
        }

        // Advance animation angle for fades (~6 second peaceful cycle)
        angle += 0.021f;
        if (angle >= 2.0f * M_PI) {
            angle -= 2.0f * M_PI;
        }

        // Crisp 50% duty square-wave twinkle (100% <-> 30% of current dim setting, never fully off):
        // At speed=1: ~0.34 Hz (~3.0 s cycle); speed=128: ~1.42 Hz (~0.70 s cycle); speed=254: 2.50 Hz (0.40 s cycle)
        float twinkle_freq_hz = 0.33f + 2.17f * ((float)twinkle_speed / 254.0f);
        twinkle_phase += twinkle_freq_hz * 0.020f;
        while (twinkle_phase >= 1.0f) {
            twinkle_phase -= 1.0f;
        }

        bool square_high = (twinkle_phase < 0.5f);
        float twinkle_a = square_high ? 1.0f : TWINKLE_DIM_FACTOR;

        // Handle Demo Mode cycling every 15 seconds (750 ticks at 50Hz)
        tree_mode_t effective_mode = mode;
        if (mode == TREE_MODE_DEMO) {
            demo_timer_ticks++;
            if (demo_timer_ticks >= 750) {
                demo_timer_ticks = 0;
                demo_sub_mode = (tree_mode_t)((demo_sub_mode + 1) % (TREE_MODE_DEMO));
                ESP_LOGI(TAG, "Demo cycling to: %s", tree_effects_get_mode_name(demo_sub_mode));
            }
            effective_mode = demo_sub_mode;
        }

        float sin_val = (sinf(angle) + 1.0f) * 0.5f; // 0.0 to 1.0
        float gamma_val = sin_val * sin_val;         // Natural breathing curve

        // NOTE: This tree uses unidirectional +29V smart-IC strings (Polarity A / IN1 only).
        // Reversing polarity (-29V on IN2 / duty_b) turns the entire tree off until the stock
        // TS-48W29V forward-polarity pulse-code (PLC) packets are captured and added.
        // Keep all active modes mapped onto Polarity A (Warm White) with duty_b = 0 for now.
        switch (effective_mode) {
            case TREE_MODE_STEADY_WARM_WHITE:
            case TREE_MODE_STEADY_MULTICOLOR:
            case TREE_MODE_STEADY_DUAL:
                tree_driver_set_duties(scale_duty(1.0f, brightness), 0);
                break;

            case TREE_MODE_FADE_WARM_WHITE:
            case TREE_MODE_FADE_MULTICOLOR:
            case TREE_MODE_FADE_ALTERNATING:
                tree_driver_set_duties(scale_duty(gamma_val, brightness), 0);
                break;

            case TREE_MODE_TWINKLE_WARM_WHITE:
            case TREE_MODE_TWINKLE_MULTICOLOR:
            case TREE_MODE_TWINKLE_ALTERNATING:
                tree_driver_set_duties(scale_duty(twinkle_a, brightness), 0);
                break;

            default:
                tree_driver_set_duties(scale_duty(1.0f, brightness), 0);
                break;
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t tree_effects_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create effects mutex");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t res = xTaskCreate(effects_task, "tree_effects_task", 4096, NULL, 5, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create effects task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tree effects engine initialized");
    return ESP_OK;
}
