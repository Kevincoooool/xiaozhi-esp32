#pragma once

#include <driver/ledc.h>
#include <esp_log.h>

class VibrationController {
public:
    static VibrationController& GetInstance() {
        static VibrationController instance;
        return instance;
    }

    // 初始化震动器
    void Initialize(gpio_num_t vibration_gpio);
    
    // 设置震动档位 (0-9)
    void SetLevel(uint8_t level);
    
    // 完全停止震动
    void StopVibration();

private:
    VibrationController() = default;
    ~VibrationController();

    static constexpr uint32_t FREQ_HZ = 20000;
    static constexpr ledc_timer_t TIMER_NUM = LEDC_TIMER_2;
    static constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_2;
    static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t DUTY_MAX = 1024;
    
    // 10档位的PWM值
    static constexpr uint32_t LEVEL_PWM[10] = {
        102,  // 10% 最弱
        204,  // 20%
        307,  // 30%
        409,  // 40%
        512,  // 50%
        614,  // 60%
        716,  // 70%
        819,  // 80%
        921,  // 90%
        1023  // 100% 最强
    };

    gpio_num_t vibration_gpio_ = GPIO_NUM_NC;
    uint32_t current_level_ = 0;

    void InitializePwm();
};