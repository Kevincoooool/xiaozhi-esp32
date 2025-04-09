#include "vibration_controller.h"

#define TAG "VibrationController"

void VibrationController::Initialize(gpio_num_t vibration_gpio) {
    vibration_gpio_ = vibration_gpio;
    InitializePwm();
}

void VibrationController::InitializePwm() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = TIMER_NUM,
        .freq_hz = FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = vibration_gpio_,
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

void VibrationController::SetLevel(uint8_t level) {
    if (level > 9) level = 9;  // 确保档位在0-9范围内
    current_level_ = level;
    uint32_t duty = LEVEL_PWM[level];
    ledc_set_duty(SPEED_MODE, CHANNEL, duty);
    ledc_update_duty(SPEED_MODE, CHANNEL);
    ESP_LOGI(TAG, "Set vibration level to %d, duty: %lu", level, duty);
}

void VibrationController::StopVibration() {
    current_level_ = 0;
    ledc_set_duty(SPEED_MODE, CHANNEL, 0);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

VibrationController::~VibrationController() {
    StopVibration();
}