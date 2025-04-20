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

    // 切换工作状态
    self->is_tightening_ = !self->is_tightening_;
    
    if (self->is_tightening_) {
        // 工作状态：启动气泵，拉低状态引脚
        self->speed_ = self->tightness_max_;
        gpio_set_level(self->status_gpio_, 0);  // 工作时拉低
    } else {
        // 不工作状态：停止气泵，拉高状态引脚
        self->speed_ = 0;  // 完全停止
        gpio_set_level(self->status_gpio_, 1);  // 不工作时拉高
    }

    ledc_set_duty(SPEED_MODE, CHANNEL, self->speed_);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

void PumpController::StartCycle(uint32_t interval_ms, uint32_t min_tight, uint32_t max_tight) {
    cycle_interval_ms_ = interval_ms;
    tightness_min_ = min_tight;
    tightness_max_ = max_tight;
    
    is_cycling_ = true;
    is_tightening_ = false;  // 初始状态为不工作
    speed_ = 0;
    
    // 初始状态设置
    gpio_set_level(status_gpio_, 1);  // 初始状态拉高
    
    esp_timer_stop(cycle_timer_);
    // 使用完整的间隔时间作为定时器周期
    esp_timer_start_periodic(cycle_timer_, cycle_interval_ms_ * 1000);
}

void PumpController::StopPump() {
    is_cycling_ = false;
    esp_timer_stop(cycle_timer_);
    speed_ = 0;
    ledc_set_duty(SPEED_MODE, CHANNEL, 0);
    ledc_update_duty(SPEED_MODE, CHANNEL);
    gpio_set_level(status_gpio_, 1);  // 停止时拉高状态引脚
}

void PumpController::SetPumpSpeed(uint32_t speed) {
    speed_ = speed;
    ledc_set_duty(SPEED_MODE, CHANNEL, speed_);
    ledc_update_duty(SPEED_MODE, CHANNEL);
}

void PumpController::StopCycle() {
    is_cycling_ = false;
    esp_timer_stop(cycle_timer_);
}



PumpController::~PumpController() {
    StopPump();
    if (cycle_timer_) {
        esp_timer_delete(cycle_timer_);
    }
}