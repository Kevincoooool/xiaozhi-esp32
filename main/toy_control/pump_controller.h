#pragma once

#include <driver/ledc.h>
#include <esp_timer.h>
#include <esp_log.h>

class PumpController {
public:
    static PumpController& GetInstance() {
        static PumpController instance;
        return instance;
    }

    // 初始化气泵
    void Initialize(gpio_num_t pump_gpio, gpio_num_t status_gpio);
    
    // 设置气泵强度
    void SetPumpSpeed(uint32_t speed);
    
    // 开始循环模式
    void StartCycle(uint32_t interval_ms, uint32_t min_tight, uint32_t max_tight);
    
    // 停止循环
    void StopCycle();
    
    // 完全停止气泵
    void StopPump();

private:
    PumpController() = default;
    ~PumpController();

    static constexpr uint32_t FREQ_HZ = 20000;
    static constexpr ledc_timer_t TIMER_NUM = LEDC_TIMER_1;
    static constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_1;
    static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t DUTY_MAX = 1024;
    static constexpr uint32_t TIMER_INTERVAL = 20;

    gpio_num_t pump_gpio_ = GPIO_NUM_NC;
    gpio_num_t status_gpio_ = GPIO_NUM_NC;
    bool is_cycling_ = false;
    uint32_t cycle_interval_ms_ = 1000;
    uint32_t tightness_min_ = 500;
    uint32_t tightness_max_ = 1023;
    uint32_t speed_ = 0;
    bool is_tightening_ = true;
    esp_timer_handle_t cycle_timer_ = nullptr;

    void InitializePwm();
    void InitializeCycleTimer();
    static void OnCycleTimer(void* arg);
};