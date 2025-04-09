#pragma once

#include <driver/ledc.h>
#include <esp_log.h>

class WarmController {
public:
    static WarmController& GetInstance() {
        static WarmController instance;
        return instance;
    }

    // 初始化温度控制
    void Initialize(gpio_num_t warm_gpio);
    
    // 设置温度档位 (0-4)
    void SetLevel(uint8_t level);
    
    // 关闭加热
    void StopWarm();

private:
    WarmController() = default;
    ~WarmController();

    static constexpr uint32_t FREQ_HZ = 20000;
    static constexpr ledc_timer_t TIMER_NUM = LEDC_TIMER_3;
    static constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_3;
    static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t DUTY_MAX = 1024;
    
    // 5档温度的PWM值
    static constexpr uint32_t LEVEL_PWM[5] = {
        205,   // 20% - 低温
        410,   // 40% - 中低温
        614,   // 60% - 中温
        819,   // 80% - 中高温
        1023   // 100% - 高温
    };

    gpio_num_t warm_gpio_ = GPIO_NUM_NC;
    uint32_t current_level_ = 0;

    void InitializePwm();
};