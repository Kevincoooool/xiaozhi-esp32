#include "warm_controller.h"

#define TAG "WarmController"

void WarmController::Initialize(gpio_num_t warm_gpio) {
    warm_gpio_ = warm_gpio;
    InitializePwm();
}

void WarmController::InitializePwm() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = TIMER_NUM,
        .freq_hz = FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = warm_gpio_,
        .speed_mode = SPEED_MODE,
        .channel = CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = TIMER_NUM,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
}

void WarmController::SetLevel(uint8_t level) {
    if (level > 4) level = 4;  // 确保档位在0-4范围内
    current_level_ = level;
    uint32_t duty = LEVEL_PWM[level];
    ledc_set_duty(SPEED_MODE, CHANNEL, duty);
    ledc_update_duty(SPEED_MODE, CHANNEL);
    ESP_LOGI(TAG, "Set warm level to %d, duty: %lu", level, duty);
}

void WarmController::StopWarm() {
    current_level_ = 0;
    ledc_set_duty(SPEED_MODE, CHANNEL, 0);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

WarmController::~WarmController() {
    StopWarm();
}