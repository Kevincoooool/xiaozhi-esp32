#include "pump_controller.h"

#define TAG "PumpController"

void PumpController::Initialize(gpio_num_t pump_gpio, gpio_num_t status_gpio) {
    pump_gpio_ = pump_gpio;
    status_gpio_ = status_gpio;

    // 初始化状态指示GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << status_gpio_),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    InitializePwm();
    InitializeCycleTimer();
}

void PumpController::InitializePwm() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = TIMER_NUM,
        .freq_hz = FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = pump_gpio_,
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

void PumpController::InitializeCycleTimer() {
    const esp_timer_create_args_t timer_args = {
        .callback = OnCycleTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pump_cycle",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &cycle_timer_));
}

void PumpController::OnCycleTimer(void* arg) {
    auto* self = static_cast<PumpController*>(arg);
    if (!self->is_cycling_) return;

    uint32_t total_steps = self->cycle_interval_ms_ / TIMER_INTERVAL;
    uint32_t range = self->tightness_max_ - self->tightness_min_;
    if(total_steps == 0) total_steps = 1;
    uint32_t current_step = (range + total_steps - 1) / total_steps;

    if (self->is_tightening_) {
        gpio_set_level(self->status_gpio_, 1);
        if (self->speed_ + current_step < self->tightness_max_) {
            self->speed_ += current_step;
        } else {
            self->speed_ = self->tightness_max_;
            self->is_tightening_ = false;
        }
    } else {
        gpio_set_level(self->status_gpio_, 0);
        if (self->speed_ > self->tightness_min_ + current_step) {
            self->speed_ -= current_step;
        } else {
            self->speed_ = self->tightness_min_;
            self->is_tightening_ = true;
        }
    }

    ledc_set_duty(SPEED_MODE, CHANNEL, self->speed_);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

void PumpController::SetPumpSpeed(uint32_t speed) {
    speed_ = speed;
    ledc_set_duty(SPEED_MODE, CHANNEL, speed_);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

void PumpController::StartCycle(uint32_t interval_ms, uint32_t min_tight, uint32_t max_tight) {
    cycle_interval_ms_ = interval_ms;
    tightness_min_ = min_tight;
    tightness_max_ = max_tight;
    
    is_cycling_ = true;
    is_tightening_ = true;
    speed_ = tightness_min_;
    
    esp_timer_stop(cycle_timer_);
    esp_timer_start_periodic(cycle_timer_, TIMER_INTERVAL * 1000);
}

void PumpController::StopCycle() {
    is_cycling_ = false;
    esp_timer_stop(cycle_timer_);
}

void PumpController::StopPump() {
    is_cycling_ = false;
    esp_timer_stop(cycle_timer_);
    speed_ = 0;
    ledc_set_duty(SPEED_MODE, CHANNEL, 0);
    ledc_update_duty(SPEED_MODE, CHANNEL);
    gpio_set_level(status_gpio_, 0);
}

PumpController::~PumpController() {
    StopPump();
    if (cycle_timer_) {
        esp_timer_delete(cycle_timer_);
    }
}