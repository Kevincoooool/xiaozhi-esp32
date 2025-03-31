#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"

#include <driver/ledc.h>
#include <esp_log.h>
#include <algorithm>

#define TAG "Pump"

namespace iot {

class Pump : public Thing {
private:
    static constexpr gpio_num_t GPIO_NUM = GPIO_NUM_47;    // 气泵控制GPIO
    static constexpr uint32_t FREQ_HZ = 20000;            // PWM频率25KHz
    static constexpr ledc_timer_t TIMER_NUM = LEDC_TIMER_1;
    static constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_1;
    static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;
    static constexpr uint32_t DUTY_MAX = 1024;            // 13位分辨率

    bool running_ = false;
    uint32_t speed_ = 0;  // 速度值 0-8192

    // 循环控制相关参数
    bool is_cycling_ = false;
    uint32_t cycle_interval_ms_ = 1000;  // 循环间隔时间(ms)
    uint32_t tightness_min_ = 500;       // 最小收紧度(对应松开状态)
    uint32_t tightness_max_ = 1023;      // 最大收紧度
    esp_timer_handle_t cycle_timer_ = nullptr;
    bool is_tightening_ = true;          // 当前是否处于收紧阶段

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

    // 初始化循环定时器
    void InitializeCycleTimer() {
        const esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                static_cast<Pump*>(arg)->OnCycleTimer();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "pump_cycle",
            .skip_unhandled_events = true
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &cycle_timer_));
    }

    // 循环定时器回调
    // 移除固定步进值，改用动态计算
    static constexpr uint32_t TIMER_INTERVAL = 20;   // 定时器固定间隔(ms)
    uint32_t current_step_ = 0;  // 当前步进值

    // 循环定时器回调
    void OnCycleTimer() {
        if (!is_cycling_) return;

        // 根据循环间隔和当前范围动态计算步进值
        uint32_t total_steps = cycle_interval_ms_ / TIMER_INTERVAL;
        uint32_t range = tightness_max_ - tightness_min_;
        if(total_steps == 0) total_steps = 1; // 防止除以0
        current_step_ = (range + total_steps - 1) / total_steps; // 向上取整

        if (is_tightening_) {
            // 收紧阶段
            if (speed_ + current_step_ < tightness_max_) {
                speed_ += current_step_;
            } else {
                speed_ = tightness_max_;
                is_tightening_ = false;
            }
        } else {
            // 松开阶段
            if (speed_ > tightness_min_ + current_step_) {
                speed_ -= current_step_;
            } else {
                speed_ = tightness_min_;
                is_tightening_ = true;
            }
        }

        ledc_set_duty(SPEED_MODE, CHANNEL, speed_);
        ledc_update_duty(SPEED_MODE, CHANNEL);
    }

public:
    Pump() : Thing("Pump", "可调松紧度的气泵，支持自动循环收紧松开") {
        InitializePwm();
        InitializeCycleTimer();

        properties_.AddNumberProperty("Pump", "松紧度 (0-100)", [this]() -> double {
            return (speed_ * 100.0) / DUTY_MAX;
        });

        // 可以修改代码中的映射关系
        methods_.AddMethod("SetPump", "设置松紧度", ParameterList({
            Parameter("Pump", "0到100之间的整数",kValueTypeNumber,  true)
            }), [this](const ParameterList& parameters) {
                double speed = static_cast<uint8_t>(parameters["Pump"].number());
                // 将输入范围重新映射到70-100%
                speed_ = 0 + ((1023 - 0) * speed) / 100; // 717约等于70%*1023
                ledc_set_duty(LEDC_LOW_SPEED_MODE, CHANNEL, speed_);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, CHANNEL);
                ESP_LOGI(TAG, "Set Pump speed to %.1f%%, duty: %lu", speed, speed_);
            });

        // 添加循环控制方法
        methods_.AddMethod("StartCycle", "开始循环收紧松开", ParameterList({
            Parameter("Interval", "循环间隔(ms)", kValueTypeNumber, true),
            Parameter("TightnessMin", "最小收紧度(0-100)", kValueTypeNumber, true),
            Parameter("TightnessMax", "最大收紧度(0-100)", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            cycle_interval_ms_ = static_cast<uint32_t>(parameters["Interval"].number());
            
            // 将0-100的范围映射到实际PWM范围
            double min_tight = parameters["TightnessMin"].number();
            double max_tight = parameters["TightnessMax"].number();
            
            tightness_min_ = 0 + ((1023 - 0) * min_tight) / 100;
            tightness_max_ = 0 + ((1023 - 0) * max_tight) / 100;
            
            is_cycling_ = true;
            is_tightening_ = true;
            speed_ = tightness_min_;
            
            esp_timer_stop(cycle_timer_);
            esp_timer_start_periodic(cycle_timer_, TIMER_INTERVAL * 1000);  // 20ms 的更新间隔

            ESP_LOGI(TAG, "Started pump cycle: interval=%lums, min=%lu, max=%lu", 
                    cycle_interval_ms_, tightness_min_, tightness_max_);
        });

        methods_.AddMethod("StopCycle", "停止循环", ParameterList(), 
            [this](const ParameterList& parameters) {
                is_cycling_ = false;
                esp_timer_stop(cycle_timer_);
                // 停止后保持当前状态
                ESP_LOGI(TAG, "Stopped pump cycle at duty: %lu", speed_);
        });
    }

    ~Pump() {
        // 停止气泵并清理资源
        running_ = false;
        ledc_stop(SPEED_MODE, CHANNEL, 0);
        ledc_fade_func_uninstall();
        if (cycle_timer_) {
            esp_timer_delete(cycle_timer_);
        }
    }
};

} // namespace iot

DECLARE_THING(Pump);