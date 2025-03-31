#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"

#include <driver/ledc.h>
#include <esp_log.h>
#include <algorithm>

#define TAG "Vibration"

namespace iot {

class Vibration : public Thing {
private:
    static constexpr gpio_num_t GPIO_NUM = GPIO_NUM_38;    // 电机控制GPIO
    static constexpr uint32_t FREQ_HZ = 20000;            // PWM频率25KHz
    static constexpr ledc_timer_t TIMER_NUM = LEDC_TIMER_2;
    static constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_2;
    static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t DUTY_MAX = 1024;            // 13位分辨率

    bool running_ = false;
    uint32_t speed_ = 0;  // 速度值 0-8192

    void InitializePwm() {
        ledc_timer_config_t timer_conf = {
            .speed_mode = SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = TIMER_NUM,
            .freq_hz = FREQ_HZ,
            .clk_cfg = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

        ledc_channel_config_t channel_conf = {
            .gpio_num = GPIO_NUM,
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

        // 启用渐变功能实现平滑控制
        // ESP_ERROR_CHECK(ledc_fade_func_install(0));
    }


public:
    Vibration() : Thing("Vibration", "可调力度，力度由大的2号电机控制") {
        InitializePwm();

        properties_.AddNumberProperty("Vibration", "力度 (0-100)", [this]() -> double {
            return (speed_ * 100.0) / DUTY_MAX;
        });

        // 可以修改代码中的映射关系
        methods_.AddMethod("SetVibration", "设置力度", ParameterList({
            Parameter("Vibration", "0到100之间的整数",kValueTypeNumber,  true)
            }), [this](const ParameterList& parameters) {
                double speed = static_cast<uint8_t>(parameters["Vibration"].number());
                // 将输入范围重新映射到70-100%
                uint32_t duty_cycle = 0 + ((1023 - 0) * speed) / 100; // 717约等于70%*1023
                ledc_set_duty(LEDC_LOW_SPEED_MODE, CHANNEL, duty_cycle);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, CHANNEL);
                ESP_LOGI(TAG, "Set Vibration speed to %.1f%%, duty: %lu", speed, duty_cycle);
            });
    }

    ~Vibration() {
        // 停止电机并清理资源
        running_ = false;
        ledc_stop(SPEED_MODE, CHANNEL, 0);
        ledc_fade_func_uninstall();
    }
};

} // namespace iot

DECLARE_THING(Vibration);