#include "tree_driver.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "tree_driver";

#define LEDC_TIMER_RES       LEDC_TIMER_10_BIT
#define LEDC_SPEED           LEDC_LOW_SPEED_MODE
#define LEDC_CH_IN1          LEDC_CHANNEL_0
#define LEDC_CH_IN2          LEDC_CHANNEL_1

static volatile uint32_t s_duty_a = 0;
static volatile uint32_t s_duty_b = 0;
static volatile uint32_t s_applied_a = 0xFFFFFFFF;
static volatile uint32_t s_applied_b = 0xFFFFFFFF;
static volatile bool s_timer_running = false;
static volatile uint8_t s_phase = 0;
static esp_timer_handle_t s_toggle_timer = NULL;

static void apply_channel_duties(uint32_t in1_duty, uint32_t in2_duty)
{
    if (s_applied_a != in1_duty) {
        ledc_set_duty(LEDC_SPEED, LEDC_CH_IN1, in1_duty);
        ledc_update_duty(LEDC_SPEED, LEDC_CH_IN1);
        s_applied_a = in1_duty;
    }
    if (s_applied_b != in2_duty) {
        ledc_set_duty(LEDC_SPEED, LEDC_CH_IN2, in2_duty);
        ledc_update_duty(LEDC_SPEED, LEDC_CH_IN2);
        s_applied_b = in2_duty;
    }
}

static void alternating_timer_cb(void *arg)
{
    uint32_t a = s_duty_a;
    uint32_t b = s_duty_b;

    if (a == 0 && b == 0) {
        apply_channel_duties(0, 0);
        return;
    }

    if (a > 0 && b == 0) {
        apply_channel_duties(a, 0);
        return;
    }

    if (a == 0 && b > 0) {
        apply_channel_duties(0, b);
        return;
    }

    // Both colors active: alternate at 200 Hz (2.5 ms per phase)
    if (s_phase == 0) {
        apply_channel_duties(a, 0);
        s_phase = 1;
    } else {
        apply_channel_duties(0, b);
        s_phase = 0;
    }
}

esp_err_t tree_driver_init(gpio_num_t pin_in1, gpio_num_t pin_in2)
{
    ESP_LOGI(TAG, "Initializing DRV8871 LEDC driver on IN1: GPIO %d, IN2: GPIO %d", pin_in1, pin_in2);

    // 1. Configure LEDC Timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_SPEED,
        .duty_resolution = LEDC_TIMER_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = TREE_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // 2. Configure Channel 0 (IN1)
    ledc_channel_config_t ch0_conf = {
        .gpio_num = pin_in1,
        .speed_mode = LEDC_SPEED,
        .channel = LEDC_CH_IN1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch0_conf));

    // 3. Configure Channel 1 (IN2)
    ledc_channel_config_t ch1_conf = {
        .gpio_num = pin_in2,
        .speed_mode = LEDC_SPEED,
        .channel = LEDC_CH_IN2,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1_conf));

    // 4. Create high-resolution timer for alternating polarity (2.5 ms interval = 400 Hz toggle)
    const esp_timer_create_args_t timer_args = {
        .callback = &alternating_timer_cb,
        .arg = NULL,
        .name = "tree_alt_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_toggle_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_toggle_timer, 2500)); // 2500 µs = 2.5 ms
    s_timer_running = true;

    tree_driver_all_off();
    return ESP_OK;
}

void tree_driver_set_duties(uint32_t duty_a, uint32_t duty_b)
{
    if (duty_a > TREE_PWM_MAX_DUTY) duty_a = TREE_PWM_MAX_DUTY;
    if (duty_b > TREE_PWM_MAX_DUTY) duty_b = TREE_PWM_MAX_DUTY;

    s_duty_a = duty_a;
    s_duty_b = duty_b;
}

void tree_driver_all_off(void)
{
    s_duty_a = 0;
    s_duty_b = 0;
    apply_channel_duties(0, 0);
}
